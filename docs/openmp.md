# Versão OpenMP — K-médias

> Medições no notebook Samsung Galaxy Book 2 (Intel Core i5-1235U, 12 threads),
> dentro do contêiner Docker do projeto. Para refazer:
> `bash scripts/medir_openmp.sh` e depois `./scripts/analise_paralelo openmp`.
> Todas as 107 execuções deram checksum idêntico ao do sequencial.

## 1. Estratégia de paralelização

Padrão escolhido: **laço paralelo** (`#pragma omp parallel for`). O k-médias é um
laço sobre N pontos independentes, então não precisa de `sections` nem de `tasks`
(que servem para tarefas diferentes entre si ou recursão, como no mergesort).

| Trecho | Peso no baseline | Tratamento |
|---|---|---|
| Fase 1: atribuição | ~82 % | `parallel for`; cada thread recebe um bloco de pontos |
| Fase 2a: acumulação | ~17 % | `parallel for` com `reduction` sobre os vetores `sx`, `sy`, `cnt` |
| Fase 2b: divisão | ~0 % | serial (só K = 8 operações) |
| Geração dos pontos | fora do cronômetro | serial, igual ao baseline |

**Variáveis compartilhadas e privadas:**
- `px`, `py`, `cx`, `cy`: compartilhadas e **só lidas** dentro dos laços paralelos, então não há corrida.
- `rotulo[i]`: compartilhado, mas cada `i` é escrito por uma única thread, então não há corrida.
- `best`, `bj`, `dx`, `dy`, `d`: declaradas dentro do laço, portanto privadas de cada thread.
- `sx[j]`, `sy[j]`, `cnt[j]`: **aqui está a condição de corrida** (ver seção 2).

**Sincronização:** o fim de cada `parallel for` tem uma barreira implícita. Ela é
obrigatória: ninguém pode acumular antes de todos os pontos terem rótulo, e ninguém
pode começar a próxima iteração antes de os centroides novos estarem prontos. São
duas barreiras por iteração, 40 no total: é a "sincronização global a cada passo"
que o professor destaca neste problema.

## 2. Condição de corrida na acumulação: critical × atomic × reduction

Vários pontos do mesmo cluster executam `sx[j] += px[i]` ao mesmo tempo. Essa
operação é ler, somar e escrever. Se duas threads leem o mesmo valor antigo, uma
soma se perde e o checksum sai errado. As três soluções implementadas
(escolhidas pelo 4º argumento do programa):

| Modo | Como funciona |
|---|---|
| `critical` | cada ponto entra numa seção crítica; só uma thread por vez |
| `atomic` | cada soma vira uma instrução atômica do hardware |
| `reduction` | cada thread soma numa **cópia privada** dos vetores; o OpenMP combina as cópias uma vez no fim do laço |

Tempo (mediana, N = 1M, sequencial = 0,142 s):

| Threads | reduction | atomic | critical |
|---|---|---|---|
| 1 | 0,147 s | 0,429 s | 0,381 s |
| 2 | **0,099 s** | 1,014 s | 0,658 s |
| 4 | 0,117 s | 1,806 s | 1,890 s |
| 12 | 0,171 s | 2,006 s | 3,483 s |

**Escolha: `reduction`.** O que os números mostram:
- `atomic` e `critical` já são ~3× mais lentos que o sequencial **com 1 thread**:
  é o custo da instrução atômica ou da trava em cada um dos 20 milhões de pontos
  (1M pontos × 20 iterações).
- Com mais threads, eles **pioram** em vez de melhorar: com 12 threads, `atomic`
  fica 14× e `critical` 25× mais lento que o sequencial. Com K = 8, os vetores
  `sx`, `sy`, `cnt` cabem em poucas linhas de cache e todas as threads escrevem
  nelas o tempo todo. A linha de cache fica "pulando" entre os núcleos
  (*cache line ping-pong*), e no `critical` as threads ainda ficam na fila da trava.
- `reduction` é o único que acelera, porque não sincroniza dentro do laço. A
  combinação final custa só p × K somas por iteração.
- Com N = 1M, até o `reduction` piora acima de 2 threads: cada iteração dura só
  ~7 ms, e o custo fixo de criar a equipe de threads e combinar as cópias pesa.
  Com N grande (seção 4) isso some.

Gráfico: `resultados/openmp_corrida.svg`.

## 3. Políticas de escalonamento

Na atribuição, **todo ponto custa o mesmo** (K distâncias), então a teoria indica
`static`: divide os pontos em p blocos contíguos, sem custo de distribuição durante
a execução. Para comprovar, a atribuição usa `schedule(runtime)`, e a política é
escolhida pela variável `OMP_SCHEDULE`, sem recompilar.

Tempo (mediana, N = 5M, 12 threads):

| Política | Tempo | O que acontece |
|---|---|---|
| `static` | 0,293 s | p blocos contíguos, definidos de antemão |
| `static,1` | 0,583 s | pontos distribuídos um a um, em rodízio: threads vizinhas escrevem em posições vizinhas de `rotulo` (mesma linha de cache), o que causa *false sharing*, e cada thread perde a leitura sequencial da memória |
| `dynamic,1` | **4,172 s** | cada thread pede 1 ponto por vez a uma fila: 5M pedidos por iteração; o custo de distribuir supera o de calcular (14× mais lento que `static`) |
| `dynamic,1000` | 0,276 s | blocos de 1000 pontos: o custo da fila fica diluído |
| `guided` | **0,270 s** | blocos que começam grandes e diminuem |

**Resultado interessante:** `guided` e `dynamic,1000` ficaram ~6–8 % **mais rápidos**
que `static`, mesmo com a carga por ponto sendo uniforme. A explicação está no
processador, não no problema: o i5-1235U é **híbrido**, com 2 núcleos P rápidos e
8 núcleos E mais lentos. Com `static`, todas as threads recebem a mesma quantidade
de pontos, e as que estão nos núcleos lentos terminam por último, enquanto a
barreira espera por elas. Com `dynamic`/`guided`, quem está num núcleo rápido
termina seu bloco e pega outro, equilibrando o trabalho entre núcleos de
velocidades diferentes.

**Conclusão:** a carga é uniforme por ponto, mas os núcleos não são iguais. Numa
máquina de núcleos homogêneos (como, possivelmente, a do laboratório), `static`
seria o melhor. Neste notebook, `guided` ou `dynamic` com bloco grande dão um
pequeno ganho. Bloco pequeno (`dynamic,1`) é sempre desastroso. O programa usa
`static` como padrão, que é a escolha segura e portável.

Observação prática: sem `OMP_SCHEDULE`, o GCC trata `schedule(runtime)` como
`dynamic,1`. Por isso o programa força `static` quando a variável não está definida.

Gráfico: `resultados/openmp_schedule.svg`.

## 4. Escalabilidade forte (speedup e eficiência)

N = 20M fixo, `reduction` + `static`. Speedup calculado contra o **baseline
sequencial** (2,817 s), não contra a versão OpenMP com 1 thread.

| Threads | Tempo (s) | Speedup | Eficiência | Amdahl previsto |
|---|---|---|---|---|
| 1 | 3,007 | 0,94 | 93,7 % | 1,00 |
| 2 | 1,754 | 1,61 | 80,3 % | 2,00 |
| 4 | 1,486 | 1,90 | 47,4 % | 4,00 |
| 6 | 1,323 | 2,13 | 35,5 % | 6,00 |
| 8 | 1,128 | 2,50 | 31,2 % | 8,00 |
| 10 | 1,101 | **2,56** | 25,6 % | 10,00 |
| 12 | 1,322 | 2,13 | 17,8 % | 12,00 |

**Melhor resultado: speedup de 2,56× com 10 threads.** A distância para o previsto
por Amdahl é enorme, mas **não vem de parte serial**: dentro do laço cronometrado,
f ≈ 0,9998 (seção 5 do `baseline.md`). Ela vem do hardware:

1. **Banda de memória (principal causa).** O cálculo por ponto é pequeno (8
   distâncias) e os dados são grandes: a cada iteração o programa lê `px` e `py`
   duas vezes (320 MB cada vez) e escreve e lê `rotulo` (80 MB cada vez), cerca de
   800 MB por iteração, 16 GB nas 20 iterações. Com 10 threads isso é feito em
   1,1 s, ~15 GB/s de tráfego de memória. A partir daí, mais threads não ajudam,
   porque todas disputam o mesmo barramento. A escalabilidade fraca (seção 5) confirma.
2. **Processador híbrido.** Só 2 núcleos são rápidos (P). As threads 3 a 10 caem
   em núcleos E, mais lentos. Com `static`, a barreira espera a thread mais lenta.
   Por isso a eficiência despenca de 80 % (2 threads) para 47 % (4 threads).
3. **Limite de potência e de turbo.** O i5-1235U é um processador de 15 W. Com
   um núcleo ativo, ele chega a 4,4 GHz; com todos ativos, a frequência cai bastante
   para caber no limite de potência. Cada thread a mais deixa as outras mais lentas.
4. **Hyperthreading acima de 10 threads.** Com 12 threads, as 2 extras dividem os
   núcleos P com outras threads. Não há núcleo novo, só mais disputa, e o tempo
   **piora** (2,56× → 2,13×).
5. **Custo com 1 thread:** o OpenMP com 1 thread é ~7 % mais lento que o
   sequencial: é o custo de criar a região paralela e da redução, sem nenhum ganho.

Gráficos: `resultados/openmp_speedup.svg` e `resultados/openmp_eficiencia.svg`.

## 5. Escalabilidade fraca

N cresce junto com as threads (2M pontos por thread). O ideal seria o tempo ficar
constante (eficiência de 100 %).

| Threads | N | Tempo (s) | Eficiência fraca |
|---|---|---|---|
| 1 | 2M | 0,381 | 100 % |
| 2 | 4M | 0,445 | 85,5 % |
| 4 | 8M | 0,638 | 59,7 % |
| 6 | 12M | 0,783 | 48,6 % |
| 8 | 16M | 0,994 | 38,3 % |
| 10 | 20M | 1,177 | 32,4 % |
| 12 | 24M | 1,381 | 27,6 % |

O tempo cresce quase linearmente com N, mesmo com o número de threads crescendo
na mesma proporção. Cada thread nova traz mais 2M pontos para ler, mas o
barramento de memória é o mesmo. É a assinatura de um programa **limitado por
banda de memória**: acrescentar núcleos não acrescenta banda.

Gráfico: `resultados/openmp_fraca.svg`.

## 6. Validação

Cada execução do script compara o checksum com o do sequencial para o mesmo N e
marca `ok=SIM/NAO` no CSV. As 107 execuções (todos os modos, políticas e números
de threads) deram checksum idêntico. Isso é esperado: as coordenadas são inteiras
e as somas são exatas em `double`, então a ordem da redução não altera o
resultado (ver seção 6 do `baseline.md`).

## 7. Possíveis melhorias (não implementadas)

- Juntar as duas fases numa única região paralela (`#pragma omp parallel` com dois
  `#pragma omp for` dentro) evitaria criar a equipe de threads duas vezes por
  iteração. O ganho é pequeno com N grande.
- Como o gargalo é memória, a melhoria com mais efeito seria **ler menos dados**:
  fazer a acumulação no mesmo laço da atribuição (cada thread soma nas suas cópias
  privadas logo depois de decidir o rótulo) eliminaria a segunda leitura de `px`,
  `py` e `rotulo`, cortando o tráfego por iteração quase pela metade.
