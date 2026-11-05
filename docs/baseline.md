# Baseline e Perfilamento — K-médias

> Números abaixo medidos numa máquina de teste com **2 núcleos** (Intel Xeon 2,1 GHz).
> **Refazer as medições na máquina do laboratório** com `./scripts/medir_baseline.sh`
> e `./scripts/perfil.sh` (depois `./scripts/analise_baseline`) antes da entrega; as conclusões não devem mudar.

## 1. O que o código faz

O programa gera N pontos 2D com coordenadas inteiras entre 0 e 999 (gerador
congruencial linear com semente fixa 2024, então a entrada é sempre a mesma).
Os K primeiros pontos viram os centroides iniciais. Depois repete IT vezes:

1. **Atribuição:** para cada ponto, calcula a distância ao quadrado até cada um
   dos K centroides e guarda em `rotulo[i]` o índice do mais próximo.
2. **Acumulação:** soma as coordenadas (`sx`, `sy`) e conta (`cnt`) os pontos de cada cluster.
3. **Divisão:** novo centroide = soma / contagem.

O resultado de validação é o *checksum*: a soma das coordenadas dos K centroides finais.

## 2. Complexidade

| Trecho | Custo por iteração | Total |
|---|---|---|
| Geração dos pontos (uma vez, fora do cronômetro) | — | O(N) |
| Fase 1: atribuição | O(N·K) | O(IT·N·K) |
| Fase 2a: acumulação | O(N) | O(IT·N) |
| Fase 2b: divisão | O(K) | O(IT·K) |

Total: **O(IT · N · K)**. Memória: O(N) (vetores `px`, `py`, `rotulo`).
Como N ≫ K, a divisão é desprezível e o custo está nas fases que percorrem os N pontos.

## 3. Baseline medido (mediana de 5, 1 aquecimento descartado)

| N | Checksum | Tempo (s) |
|---|---|---|
| 1 000 000 | 8109.0037 | 0,142 |
| 5 000 000 | 8099.9055 | 1,080 |
| 10 000 000 | 8100.4956 | 1,959 |
| 20 000 000 | 8101.4537 | 3,741 |

O tempo cresce de forma aproximadamente linear em N, como previsto (K e IT fixos).
Com N = 1M o tempo é curto demais (0,14 s) e o speedup fica dominado por ruído;
**para a demonstração e as curvas, usar 10M ou 20M.**

## 4. Perfilamento (hotspots)

Medido com a cópia instrumentada `10_kmeans_perfil.cpp` (mesma lógica, checksum idêntico),
que cronometra cada fase separadamente.

| N | Atribuição | Acumulação | Divisão + resto | Geração (fora do cronômetro) |
|---|---|---|---|---|
| 1M | 84,4 % | 15,6 % | 0,02 % | 0,009 s |
| 5M | 78,6 % | 20,8 % | 0,006 % | 0,058 s |
| 10M | 77,3 % | 22,9 % | 0,004 % | 0,119 s |
| 20M | 77,4 % | 23,2 % | 0,002 % | 0,232 s |

**Hotspot principal: a fase de atribuição (~77–84 %).** A acumulação vem em segundo
(~16–23 %). A divisão é irrelevante.

Por que a acumulação pesa mais com N grande: ela faz pouca conta por ponto (3 somas)
e lê `rotulo`, `px` e `py` da memória. Com N grande os vetores não cabem no cache
(20M pontos = 160 MB só de `px` + `py`), então essa fase é **limitada por banda de
memória**, não por processamento. Isso vai aparecer no speedup: a acumulação tende
a escalar pior que a atribuição.

## 5. Fração paralelizável e Lei de Amdahl

S(p) = 1 / ((1 − f) + f/p)

**Visão A — região cronometrada (o que o professor mede):** atribuição e acumulação
são paralelizáveis; só a divisão (O(K)) é serial. f ≈ **0,9998** →
speedup previsto ≈ 2,0 (2 núcleos), 4,0 (4), 8,0 (8), 16,0 (16). Praticamente linear.

**Visão B — programa inteiro:** a geração dos pontos é serial, porque o gerador
depende do valor anterior (`s = s*a + 1`). Com ela, f ≈ **0,94–0,95**:

| p | 2 | 4 | 8 | 16 | ∞ |
|---|---|---|---|---|---|
| Speedup previsto (20M) | 1,89 | 3,41 | 5,71 | 8,61 | ≈ 17,5 |

**Por que o speedup real vai ficar abaixo do previsto (para a discussão do Marco 2):**
- **Sincronização global a cada iteração:** as threads/processos precisam terminar
  a atribuição antes da acumulação e combinar as somas antes da próxima iteração.
  No MPI isso é uma `MPI_Allreduce` por iteração.
- **Banda de memória:** a acumulação é limitada por memória; mais núcleos dividindo
  o mesmo barramento não aceleram na mesma proporção.
- **Condição de corrida / false sharing na acumulação:** vários pontos do mesmo
  cluster escrevem em `sx[j]`, `sy[j]`, `cnt[j]`. Somas locais por thread e depois
  uma redução resolvem a corrida; vetores locais colados na memória podem causar false sharing.
- **Overhead de comunicação no MPI** (cresce com o número de processos).

## 6. Corretude: a ordem da soma não altera o resultado

Em geral, paralelizar uma soma em ponto flutuante muda a ordem das operações e
pode mudar as últimas casas decimais. **Neste problema isso não acontece:** as
coordenadas são inteiras (0 a 999) e a maior soma possível é 20M × 999 ≈ 2·10¹⁰,
muito abaixo de 2⁵³ ≈ 9·10¹⁵, o limite até onde um `double` representa inteiros
exatamente. Toda soma parcial é exata, então qualquer ordem dá o mesmo valor.

Teste feito: somar a fase 2 de trás para frente produziu checksum idêntico
(8109.0037 com 1M e 8101.4537 com 20M). Consequência para o grupo: `reduction`
no OpenMP e `MPI_Allreduce` no MPI devem dar **exatamente** o mesmo checksum do baseline.
Se der diferente, é bug (corrida ou decomposição errada), não arredondamento.

## 7. Plano de paralelização (resumo)

- **OpenMP:** `#pragma omp parallel for` na atribuição (pontos independentes,
  `schedule(static)` porque todo ponto custa K distâncias, carga uniforme).
  Na acumulação, somas locais por thread + combinação (ou `reduction` sobre arrays,
  OpenMP 4.5+). Comparar com `atomic` e `critical` para justificar a escolha.
- **MPI:** decomposição de domínio, cada processo fica com N/p pontos.
  Por iteração: atribuição local, somas parciais locais, `MPI_Allreduce` de
  `sx`, `sy`, `cnt` (3·K valores), e todos calculam os mesmos centroides.
  Comunicação pequena (independe de N), então deve escalar bem.
