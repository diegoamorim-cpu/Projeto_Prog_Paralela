# K-médias paralelo (OpenMP + MPI)

Projeto Acadêmico de Programação Paralela (CCO085) · IESB 2026/2 · Prof. Rodrigo Gonçalves
Problema 10: agrupamento por k-médias (algoritmo de Lloyd).

## Estrutura

```
sequencial/   10_kmeans.cpp        baseline do professor (NÃO alterar)
              10_kmeans_perfil.cpp cópia com cronômetros por fase (só para perfilamento)
openmp/       kmeans_omp.cpp       versão memória compartilhada
mpi/          kmeans_mpi.cpp       versão memória distribuída
scripts/      medição (geram CSV) e análise (geram tabelas e gráficos)
resultados/   CSVs e gráficos
docs/         análise do baseline e relatório
```

## Rodar do zero

### Ambiente

Linux com `g++`, OpenMP e OpenMPI (laboratório ou VM da disciplina). No Windows,
use o contêiner do projeto (Docker Desktop aberto):

```bash
docker build -t kmeans-pp .                      # uma vez
MSYS_NO_PATHCONV=1 docker run -it --rm -v "$(pwd -W):/projeto" kmeans-pp bash
# (Linux/Mac: docker run -it --rm -v "$(pwd):/projeto" kmeans-pp bash)
```

Tudo é C++: a análise também é um programa C++, que gera as tabelas e os gráficos em SVG.

### Compilar, medir e analisar

```bash
make                                   # compila sequencial, OpenMP e MPI
./sequencial/10_kmeans 20000000 8 20   # pontos, clusters, iterações

bash scripts/medir_baseline.sh         # -> resultados/baseline.csv
bash scripts/perfil.sh                 # -> resultados/perfil.csv
./scripts/analise_baseline             # -> tabelas + resultados/perfil_fases.svg

bash scripts/medir_openmp.sh           # -> resultados/openmp.csv
./scripts/analise_paralelo openmp      # -> tabelas + graficos openmp_*.svg

bash scripts/medir_mpi.sh              # -> resultados/mpi.csv e mpi_comm.csv
./scripts/analise_paralelo mpi         # -> tabelas + graficos mpi_*.svg
```

### Versão OpenMP

```bash
OMP_NUM_THREADS=8 ./openmp/kmeans_omp 20000000 8 20 reduction
# 4o argumento: reduction (padrao) | atomic | critical
# politica da atribuicao: OMP_SCHEDULE=static | "dynamic,1000" | guided ...
```

### Versão MPI

```bash
mpirun -np 8 ./mpi/kmeans_mpi 20000000 8 20
# no contêiner, com mais processos que núcleos: mpirun --oversubscribe -np 12 ...
# a saída traz também comm= (tempo dentro da MPI_Allreduce, maior entre os processos)
```

## Protocolo de medição

- Compilação com `-O2`.
- Baseline: N = 1M, 5M, 10M e 20M pontos; K = 8; 20 iterações.
- OpenMP: escalabilidade forte com N = 20M e 1, 2, 4, 6, 8, 10 e 12 threads;
  escalabilidade fraca com 2M pontos por thread; comparação critical × atomic × reduction
  com N = 1M; comparação de políticas de escalonamento com N = 5M.
- MPI: escalabilidade forte com N = 20M e 1, 2, 4, 6, 8, 10 e 12 processos;
  escalabilidade fraca com 2M pontos por processo; tempo de comunicação gravado à parte.
- Cada configuração: 1 execução de aquecimento descartada + 5 execuções medidas
  (3 nos experimentos de corrida, escalonamento e fraca); usa-se a **mediana**.
- O tempo medido é o do laço de iterações (o cronômetro do próprio código do professor).
- Os scripts registram a máquina (`lscpu`) e o número de núcleos (`nproc`) no CSV.
- O speedup é sempre calculado contra o tempo do **sequencial**, não contra a versão paralela com 1 thread/processo.

## Validação de corretude

Cada versão imprime `checksum=`. Para a mesma entrada (N, K, IT), o checksum das
versões OpenMP e MPI tem que ser **idêntico** ao do sequencial, com qualquer número
de threads ou processos. Gabarito:

| N | K | IT | checksum |
|---|---|---|---|
| 1 000 000 | 8 | 20 | 8109.0037 |
| 5 000 000 | 8 | 20 | 8099.9055 |
| 10 000 000 | 8 | 20 | 8100.4956 |
| 20 000 000 | 8 | 20 | 8101.4537 |

Os scripts de medição rodam o sequencial para cada N usado e gravam, em cada linha
do CSV, a coluna `ok` (`SIM` quando o checksum é idêntico ao do sequencial).
A análise informa quantas execuções falharam. Na medição do OpenMP, as 107
execuções deram `ok=SIM`; na do MPI, as 56 execuções também.

## Resultados e discussão

- `docs/baseline.md`: complexidade, perfilamento, Amdahl e protocolo de medição.
- `docs/openmp.md`: corrida (critical × atomic × reduction), escalonamento,
  escalabilidade forte e fraca, e o afastamento em relação a Amdahl.
- `docs/mpi.md`: decomposição de domínio, coletivas, overhead de comunicação,
  escalabilidade forte e fraca.
- `resultados/`: CSVs brutos, tabelas e gráficos (`*_tempo.svg`, `*_speedup.svg`,
  `*_eficiencia.svg`, `*_fraca.svg`, `*_corrida.svg`, `*_schedule.svg`, `mpi_comm.svg`).

Máquina de medição: notebook Intel Core i5-1235U (2 núcleos P + 8 núcleos E, 12 threads).
