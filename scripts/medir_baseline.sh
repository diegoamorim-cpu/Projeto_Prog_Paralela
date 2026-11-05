#!/usr/bin/env bash
# Mede o baseline sequencial e grava em resultados/baseline.csv
# Protocolo: 1 execucao de aquecimento (descartada) + REPS execucoes medidas.
# A analise usa a MEDIANA das REPS execucoes.
#
# Uso: ./scripts/medir_baseline.sh            (a partir da raiz do repositorio)
#      REPS=7 ./scripts/medir_baseline.sh     (muda o numero de repeticoes)
set -euo pipefail

BIN=./sequencial/10_kmeans
OUT=resultados/baseline.csv
REPS=${REPS:-5}
K=8
IT=20
TAMANHOS=(1000000 5000000 10000000 20000000)

mkdir -p resultados
echo "maquina,nucleos,N,K,IT,rep,checksum,tempo_s" > "$OUT"
MAQ=$(lscpu | grep "Model name" | sed 's/Model name: *//; s/,/ /g')
NUC=$(nproc)

for N in "${TAMANHOS[@]}"; do
    "$BIN" "$N" "$K" "$IT" > /dev/null          # aquecimento (descartado)
    for r in $(seq 1 "$REPS"); do
        linha=$("$BIN" "$N" "$K" "$IT")
        chk=$(echo "$linha" | sed -E 's/.*checksum=([0-9.]+).*/\1/')
        t=$(echo "$linha"   | sed -E 's/.*tempo=([0-9.]+).*/\1/')
        echo "$MAQ,$NUC,$N,$K,$IT,$r,$chk,$t" >> "$OUT"
        echo "N=$N rep=$r checksum=$chk tempo=$t"
    done
done
echo "Gravado em $OUT"
