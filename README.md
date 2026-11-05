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

Ambiente: Linux com `g++`, OpenMP e OpenMPI (laboratório, Docker ou VM da disciplina),
Python 3 com `pandas` e `matplotlib` para os gráficos.

```bash
make                                   # compila sequencial, OpenMP e MPI
./sequencial/10_kmeans 20000000 8 20   # pontos, clusters, iterações

./scripts/medir_baseline.sh            # -> resultados/baseline.csv
./scripts/perfil.sh                    # -> resultados/perfil.csv
python3 scripts/analise_baseline.py    # -> tabelas + resultados/perfil_fases.png
```

## Protocolo de medição

- Compilação com `-O2`.
- Entradas: N = 1M, 5M, 10M e 20M pontos; K = 8; 20 iterações.
- Por tamanho: 1 execução de aquecimento descartada + 5 execuções medidas; usa-se a **mediana**.
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
