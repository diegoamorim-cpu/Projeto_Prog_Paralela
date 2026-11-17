# Versão MPI — K-médias

> Medições no notebook Samsung Galaxy Book 2 (Intel Core i5-1235U, 12 threads),
> dentro do contêiner Docker do projeto, a mesma máquina do baseline e do OpenMP.
> Para refazer: `make && bash scripts/medir_mpi.sh` e depois `./scripts/analise_paralelo mpi`.
> Todas as 56 execuções deram checksum idêntico ao do sequencial.

## 1. Estratégia de paralelização

Modelo: **memória distribuída**. Cada processo MPI tem seu próprio espaço de
endereçamento; nada é compartilhado, tudo que um processo precisa saber dos
outros chega por mensagem.

### Decomposição escolhida: decomposição de domínio

Os N pontos são divididos em p blocos contíguos; o processo `r` fica com
`N/p` pontos (os primeiros `N % p` processos ficam com um ponto a mais, então
qualquer N funciona com qualquer p). Cada processo guarda **só os seus pontos**
(`px`, `py`, `rotulo` de tamanho N/p) e uma **cópia dos K centroides**.

Por que essa decomposição (e não, por exemplo, dividir os clusters entre processos):

- Os pontos são independentes na atribuição: o rótulo do ponto `i` depende só de
  `px[i]`, `py[i]` e dos centroides. Cada processo decide os seus rótulos sem
  falar com ninguém.
- A carga é uniforme (todo ponto custa K distâncias), então blocos de tamanho
  igual dão balanceamento perfeito; não há motivo para decomposição dinâmica.
- Dividir por **cluster** seria ruim: K = 8 é pequeno (limitaria o paralelismo
  a 8 processos) e todo processo precisaria de todos os pontos para saber quais
  pertencem ao seu cluster.
- O que precisa ser combinado entre processos são só as **somas por cluster**,
  que ocupam 3·K números (sx, sy, cnt), **independentes de N**. A comunicação é
  minúscula comparada à computação.

### Fluxo do programa

| Etapa | Onde | Comunicação |
|---|---|---|
| Geração dos N pontos (gerador serial, `s = s*a + 1`) | só rank 0 | — |
| Distribuição dos pontos | todos | `MPI_Scatterv` (px) e `MPI_Scatterv` (py) |
| Centroides iniciais (K primeiros pontos) | rank 0 → todos | `MPI_Bcast` (cx, cy) |
| **Laço de IT iterações:** | | |
| 1. Atribuição (~82 % do baseline) | local | nenhuma |
| 2a. Somas parciais sx, sy, cnt locais (~17 %) | local | nenhuma |
| Combinação das somas | todos | `MPI_Allreduce(SUM)` de 3·K doubles |
| 2b. Divisão (novos centroides) | todos, redundante | nenhuma |
| Tempo máximo entre processos | todos → rank 0 | `MPI_Reduce(MAX)` |

Decisões de implementação:

- **Uma única `Allreduce` por iteração.** As somas `sx`, `sy` e as contagens
  `cnt` vão no mesmo buffer de 3·K doubles. As contagens cabem exatas em `double`
  (N ≤ 2⁵³), então não é preciso misturar tipos. Isso paga a latência da coletiva
  uma vez, não três.
- **Todos calculam os novos centroides.** Depois da `Allreduce`, todos os
  processos têm as mesmas somas globais e fazem as K divisões (trabalho desprezível,
  O(K)). Isso evita um `Bcast` extra: todos continuam com centroides idênticos sem
  nova mensagem.
- **Geração e distribuição fora do cronômetro**, igual à geração dos pontos no
  baseline (ver seção 7 sobre o que isso significa).
- **Sincronização:** `MPI_Barrier` antes e depois do laço; o tempo reportado é o
  **maior** entre os processos, porque o programa só termina quando o mais lento termina.

### Operações coletivas usadas

| Coletiva | Para quê |
|---|---|
| `MPI_Scatterv` | distribuir px e py em blocos (tamanhos diferentes quando N não é múltiplo de p) |
| `MPI_Bcast` | enviar os centroides iniciais a todos |
| `MPI_Allreduce` (`MPI_SUM`) | somar as somas parciais **e** entregar o resultado a todos, a cada iteração |
| `MPI_Reduce` (`MPI_MAX`) | reportar o tempo do processo mais lento |
| `MPI_Barrier` | alinhar os cronômetros |

`Allreduce` (e não `Reduce` + `Bcast`) é a escolha natural: todos precisam do
resultado para calcular os centroides, e a implementação do OpenMPI usa
algoritmos de árvore/recursive doubling, com custo ~ log₂ p passos.

## 2. Comparação conceitual com o OpenMP

| | OpenMP | MPI |
|---|---|---|
| Dados | `px`, `py` compartilhados | cada processo tem seu bloco |
| Condição de corrida na acumulação | existe; resolvida com `reduction` | **não existe**: cada processo soma só nos seus vetores `loc` |
| Combinação das somas | feita pelo runtime no fim do laço | `MPI_Allreduce` explícita |
| Custo de sincronização | barreira implícita de threads | mensagens + barreira de processos |
| *False sharing* | possível em vetores colados | impossível (memórias separadas) |
| Escala além de uma máquina | não | sim (aqui só testado em uma máquina) |

Em MPI **não há condição de corrida** por construção: o que o OpenMP resolve com
`critical`/`atomic`/`reduction` aqui simplesmente não aparece, porque nenhum
vetor de soma é compartilhado.

## 3. Validação de corretude

Cada execução do script compara o checksum com o do sequencial para o mesmo N e
grava `ok=SIM/NAO` no CSV. As coordenadas são inteiras (0 a 999) e as somas são
exatas em `double`, então a ordem da `Allreduce` não altera o resultado
(seção 6 do `baseline.md`): **o checksum tem que ser idêntico ao baseline com
qualquer número de processos**.

Teste manual rápido (gabarito do README):

| N | p testados | checksum esperado | resultado |
|---|---|---|---|
| 1 000 000 | 1, 2, 3, 4 | 8109.0037 | idêntico (desenvolvimento) |
| 5 000 000 | 1, 2, 3, 4 | 8099.9055 | idêntico (desenvolvimento) |
| 20 000 000 | 1, 2, 4, 6, 8, 10, 12 | 8101.4537 | idêntico (medição completa) |

Execuções na medição completa: **56**, das quais **0** com checksum errado (escalabilidade
forte com N = 20M e fraca com N = 2M a 24M, de 1 a 12 processos).
Os testes com p = 3 (e N não múltiplo de p) exercitam o caso em que os blocos têm
tamanhos diferentes.

## 4. Escalabilidade forte (speedup e eficiência)

N = 20M fixo, p processos. Speedup calculado contra o **baseline sequencial**
(2,817 s, mediana do `baseline.md`), não contra o MPI com 1 processo.

| Processos | Tempo (s) | Speedup | Eficiência | Amdahl previsto |
|---|---|---|---|---|
| 1 | 3,256 | 0,87 | 86,5 % | 1,00 |
| 2 | 1,914 | 1,47 | 73,6 % | 2,00 |
| 4 | 1,545 | 1,82 | 45,6 % | 4,00 |
| 6 | 1,330 | 2,12 | 35,3 % | 6,00 |
| 8 | 1,159 | 2,43 | 30,4 % | 8,00 |
| 10 | 1,110 | 2,54 | 25,4 % | 10,00 |
| 12 | 1,035 | **2,72** | 22,7 % | 12,00 |

Gráficos: `resultados/mpi_speedup.svg` e `resultados/mpi_eficiencia.svg`.

### Por que o resultado fica abaixo de Amdahl

A parte serial dentro do laço cronometrado é só a divisão (O(K)), então Amdahl
prevê speedup quase linear (f ≈ 0,9998, seção 5 do `baseline.md`). O resultado
real ficou **bem abaixo** disso, pelos mesmos motivos do OpenMP, porque os
processos rodam **na mesma máquina**:

1. **Banda de memória.** A atribuição lê `px` e `py` (e escreve `rotulo`) a cada
   iteração; a acumulação lê tudo de novo. Com todos os processos no mesmo
   notebook, o barramento de memória é o mesmo. Mais processos não trazem mais
   banda. Se o MPI chegar a um teto parecido com o do OpenMP (~2,5×), é sinal de
   que o gargalo é a memória e não o modelo de programação.
2. **Processador híbrido (2 núcleos P + 8 núcleos E).** O bloco de cada processo
   tem o mesmo tamanho, e quem cai num núcleo E termina depois; a `Allreduce`
   espera o mais lento. Por isso parte do tempo de `comm` medido é **espera por
   desbalanceamento**, não transferência (ver seção 6).
3. **Frequência.** Com todos os núcleos ativos o clock cai para respeitar o limite
   de potência (15 W).
4. **p > 10 (hyperthreading).** Com 11–12 processos, núcleos físicos passam a ser
   divididos; é esperado que o tempo estabilize ou piore.
5. **Custo com 1 processo.** Com p = 1 o MPI ficou ~15 % mais lento que o baseline
   (3,26 s × 2,82 s). Não é comunicação: o `comm` medido é de 0,0001 s. Parte é
   variação do notebook entre dias: na mesma sessão da medição, o sequencial levou
   2,98 s, e não 2,82 s. O resto é diferença de código gerado e de execução sob o
   `mpirun`. As 5 repetições com p = 1 variaram de 2,86 s a 3,54 s, o que mostra
   quanto o notebook oscila (turbo e temperatura).

**Melhor speedup: 2,72× com 12 processos**, muito perto do teto do OpenMP
(2,56× com 10 threads). Os dois modelos batem no **mesmo teto** de ~2,5–2,7× na
mesma máquina, o que confirma que o gargalo é o hardware (banda de memória,
núcleos E, limite de potência), e não o modelo de programação.

Uma diferença: com 12, o MPI continuou melhorando (2,54× → 2,72×), enquanto o
OpenMP piorou (2,56× → 2,13×). Essa diferença deve ser lida com cuidado: as
repetições do OpenMP com 12 threads variaram muito (1,05 s a 1,58 s), e as do MPI
com 12 processos também (0,96 s a 1,21 s). Com essa oscilação, a conclusão segura
é que acima de 10 trabalhadores **não há mais ganho consistente**, nos dois modelos.

## 5. Escalabilidade fraca

N cresce com p (2M pontos por processo). O ideal é o tempo ficar constante.

| Processos | N | Tempo (s) | Eficiência fraca |
|---|---|---|---|
| 1 | 2M | 0,298 | 100 % |
| 2 | 4M | 0,314 | 94,9 % |
| 4 | 8M | 0,549 | 54,3 % |
| 6 | 12M | 0,803 | 37,1 % |
| 8 | 16M | 0,913 | 32,7 % |
| 10 | 20M | 1,035 | 28,8 % |
| 12 | 24M | 1,537 | 19,4 % |

Gráfico: `resultados/mpi_fraca.svg`.

Leitura: a comunicação por iteração (3·K = 24 doubles = 192 bytes) não cresce com
N. Se a eficiência fraca cair, a causa **não** é o volume de comunicação, e sim a
banda de memória compartilhada e a heterogeneidade dos núcleos, como no OpenMP.
O resultado confirma: a eficiência fraca do MPI (94,9 % → 54,3 % → 28,8 % com
2, 4 e 10 processos) acompanha a do OpenMP (85,5 % → 59,7 % → 32,4 %). A queda
grande de 2 para 4 é o ponto em que os trabalhadores saem dos núcleos P e o
barramento de memória começa a saturar. Com 12 processos, a eficiência cai para
19,4 % e a comunicação sobe para 36 % do tempo (seção 6): com mais processos
que núcleos físicos, um processo que perde a vez no núcleo atrasa todos na `Allreduce`.

## 6. Análise do overhead de comunicação

O programa mede o tempo dentro da `MPI_Allreduce` (campo `comm=`), somado nas IT
iterações, e reporta o maior valor entre os processos. O script grava isso em
`resultados/mpi_comm.csv`.

### Custo teórico

A mensagem tem 3·K·8 = **192 bytes** (K = 8), que é minúscula: a `Allreduce` é
dominada pela **latência**, não pela largura de banda. Num modelo simples de
árvore, o custo por chamada é ~ α · ⌈log₂ p⌉ (α = latência de uma mensagem).
Em memória compartilhada (processos na mesma máquina, sem rede), α é de
microssegundos. O custo **total** é IT × α · log₂ p, ou seja, **independe de N**.

Consequências esperadas:
- Com N grande, o tempo de computação (∝ N/p) domina e a fração `comm/tempo` é pequena.
- Com N pequeno (1M) ou p grande, `comm/tempo` cresce: é o regime em que o MPI
  deixa de compensar.

### Medição (N = 20M, mediana)

| Processos | Tempo total (s) | `comm` (s) | Cálculo (s) | `comm` / total |
|---|---|---|---|---|
| 1 | 3,256 | 0,0001 | 3,256 | 0,0 % |
| 2 | 1,914 | 0,047 | 1,867 | 2,5 % |
| 4 | 1,545 | 0,180 | 1,365 | 11,7 % |
| 6 | 1,330 | 0,172 | 1,158 | 12,9 % |
| 8 | 1,159 | 0,120 | 1,039 | 10,3 % |
| 10 | 1,110 | 0,161 | 0,949 | 14,5 % |
| 12 | 1,035 | 0,209 | 0,826 | 20,2 % |

Gráfico: `resultados/mpi_comm.svg` (tempo total, cálculo e comunicação por número de processos).

A tabela e o gráfico são gerados por `./scripts/analise_paralelo mpi` a partir de
`resultados/mpi_comm.csv` (gravados em `resultados/mpi_comm_tabela.csv`).

### Atenção ao interpretar `comm`

O cronômetro da `Allreduce` **inclui a espera** pelos processos mais lentos: se um
processo termina a atribuição antes, o tempo que ele passa esperando na
`Allreduce` aparece como "comunicação". Em p = 1 o valor é praticamente zero
(não há com quem conversar), o que confirma que o custo vem da coordenação. Num
processador híbrido como o i5-1235U, boa parte do `comm` para p ≥ 3 é espera
por desbalanceamento entre núcleos P e E, e não custo de transferência.
Para separar os dois efeitos seria preciso cronometrar também a espera antes da
`Allreduce` (por exemplo, com um `MPI_Barrier` cronometrado imediatamente antes).
Isso não foi implementado.

**Leitura dos números:**

- **p = 2: só 2,5 %.** Os dois processos provavelmente rodam nos 2 núcleos P,
  que têm a mesma velocidade, então terminam a atribuição juntos e quase não esperam.
- **p = 4 a 10: 10–15 %.** A partir daqui, há processos em núcleos E, mais lentos.
  Os processos dos núcleos P chegam antes na `Allreduce` e ficam parados esperando.
- **p = 12: 20 %** (e 36 % na escalabilidade fraca). Há mais processos que núcleos
  físicos, e qualquer processo que perde a vez no núcleo atrasa todos.

**O custo é espera, não transferência.** A `Allreduce` move 192 bytes. Com
20 iterações e uma latência de poucos microssegundos por passo em memória
compartilhada, a transferência em si somaria menos de 1 ms. O medido é 0,1–0,2 s,
100 a 200 vezes mais. Quase todo o `comm` é **desbalanceamento de carga**: os
blocos têm o mesmo número de pontos, mas os núcleos não têm a mesma velocidade.
Isso é o mesmo fenômeno que, no OpenMP, fez `guided` e `dynamic` ganharem do
`static` (seção 3 do `openmp.md`). Uma decomposição com blocos proporcionais à
velocidade de cada núcleo reduziria essa espera, mas não é portável.

## 7. Limitações e honestidade sobre o modelo

- **Distribuição inicial fora do cronômetro.** O rank 0 gera os N pontos e os
  distribui com `Scatterv` (320 MB para N = 20M). Isso não entra no tempo, igual
  à geração no baseline. Num cluster de verdade, mover os dados pela rede teria
  custo relevante; aqui, com todos os processos na mesma máquina, o `Scatterv` é
  uma cópia de memória.
- **Memória do rank 0.** Durante a distribuição o rank 0 guarda todos os pontos
  (px + py = 320 MB com N = 20M); o programa libera esse espaço logo depois.
  Uma alternativa seria cada processo gerar só o seu bloco com *jump-ahead* do
  gerador congruencial, o que evitaria o `Scatterv` mas mudaria a estrutura do
  código fornecido pelo professor.
- **Tudo numa máquina.** Os processos disputam o mesmo barramento de memória;
  os resultados **não** representam um cluster com memória por nó.

## 8. Possíveis melhorias (não implementadas)

- **Fundir atribuição e acumulação** num só laço (somar nas variáveis locais logo
  depois de achar o rótulo): corta a segunda leitura de `px`, `py` e `rotulo`
  e reduz o tráfego de memória, que é o gargalo.
- **Híbrido MPI + OpenMP:** 1 processo por socket/nó e threads dentro dele
  reduz o número de participantes da `Allreduce` e aproveita memória compartilhada.
- **`MPI_Iallreduce`** não ajuda aqui: o algoritmo precisa dos centroides novos
  para a próxima iteração, então não há trabalho independente para sobrepor.

## 9. Como reproduzir

```bash
make                           # compila tudo (inclui mpi/kmeans_mpi)
mpirun --oversubscribe -np 4 ./mpi/kmeans_mpi 5000000 8 20   # teste rápido
bash scripts/medir_mpi.sh      # -> resultados/mpi.csv e resultados/mpi_comm.csv
./scripts/analise_paralelo mpi # -> tabelas + resultados/mpi_*.svg
```
Variáveis do script: `PROCS="1 2 4 8"`, `REPS=3`, `MPIFLAGS=""`.
