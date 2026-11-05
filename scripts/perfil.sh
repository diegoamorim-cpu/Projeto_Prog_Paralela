#!/usr/bin/env bash
# Perfilamento por fase, usando a copia instrumentada do baseline.
# Grava resultados/perfil.csv (1 aquecimento + REPS execucoes por tamanho).
set -euo pipefail

BIN=./sequencial/10_kmeans_perfil
OUT=resultados/perfil.csv
REPS=${REPS:-5}
K=8
IT=20
TAMANHOS=(1000000 5000000 10000000 20000000)

mkdir -p resultados
echo "N,K,IT,checksum,t_geracao,t_atrib,t_acum,t_div,t_total" > "$OUT"
for N in "${TAMANHOS[@]}"; do
    "$BIN" "$N" "$K" "$IT" > /dev/null          # aquecimento (descartado)
    for r in $(seq 1 "$REPS"); do
        "$BIN" "$N" "$K" "$IT" | tee -a "$OUT"
    done
done
echo "Gravado em $OUT"
