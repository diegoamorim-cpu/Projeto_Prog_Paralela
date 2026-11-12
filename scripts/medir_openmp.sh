#!/usr/bin/env bash
# Medicoes da versao OpenMP -> resultados/openmp.csv
# Quatro experimentos:
#   forte    : N fixo (20M), varia o numero de threads           -> speedup e eficiencia
#   corrida  : compara reduction x atomic x critical na acumulacao
#   schedule : compara static, dynamic e guided na atribuicao
#   fraca    : N cresce junto com as threads (2M pontos por thread)
# Protocolo: 1 execucao de aquecimento descartada + REPS medidas (mediana na analise).
# Cada execucao e validada contra o checksum do sequencial para o mesmo N.
#
# Uso (na raiz do repositorio):  bash scripts/medir_openmp.sh
#      THREADS="1 2 4 8" REPS=3 bash scripts/medir_openmp.sh   (personalizar)
set -euo pipefail

SEQ=./sequencial/10_kmeans
BIN=./openmp/kmeans_omp
OUT=resultados/openmp.csv
K=8; IT=20
MAXT=$(nproc)
THREADS=${THREADS:-"1 2 4 6 8 10 12"}
REPS=${REPS:-5}

[ -x "$BIN" ] || { echo "Compile antes: make omp"; exit 1; }
mkdir -p resultados
echo "experimento,versao,N,K,IT,p,modo,schedule,rep,checksum,ok,tempo_s" > "$OUT"

declare -A GAB   # checksum de referencia (sequencial) por N
gabarito() {
    local N=$1
    if [ -z "${GAB[$N]:-}" ]; then
        GAB[$N]=$("$SEQ" "$N" "$K" "$IT" | sed -E 's/.*checksum=([0-9.]+).*/\1/')
    fi
    echo "${GAB[$N]}"
}

# roda: experimento N threads modo schedule reps
roda() {
    local exp=$1 N=$2 p=$3 modo=$4 sch=$5 reps=$6
    local ref; ref=$(gabarito "$N")
    OMP_NUM_THREADS=$p OMP_SCHEDULE=$sch "$BIN" "$N" "$K" "$IT" "$modo" > /dev/null   # aquecimento
    for r in $(seq 1 "$reps"); do
        local linha chk t ok
        linha=$(OMP_NUM_THREADS=$p OMP_SCHEDULE=$sch "$BIN" "$N" "$K" "$IT" "$modo")
        chk=$(echo "$linha" | sed -E 's/.*checksum=([0-9.]+).*/\1/')
        t=$(echo "$linha"   | sed -E 's/.*tempo=([0-9.]+).*/\1/')
        if [ "$chk" == "$ref" ]; then ok=SIM; else ok=NAO; fi
        echo "$exp,openmp,$N,$K,$IT,$p,$modo,${sch/,/:},$r,$chk,$ok,$t" >> "$OUT"   # "dynamic,1000" vira "dynamic:1000" no CSV
        printf "%-8s N=%-9s p=%-3s %-9s %-13s checksum=%s (%s) tempo=%s\n" "$exp" "$N" "$p" "$modo" "$sch" "$chk" "$ok" "$t"
        [ "$ok" == "SIM" ] || echo "   *** CHECKSUM DIFERENTE DO SEQUENCIAL ($ref) ***"
    done
}

echo "== 1/4 Escalabilidade forte (N=20M) =="
for p in $THREADS; do roda forte 20000000 "$p" reduction static "$REPS"; done

echo "== 2/4 Condicao de corrida: reduction x atomic x critical (N=1M) =="
for modo in reduction atomic critical; do
    for p in 1 2 4 "$MAXT"; do roda corrida 1000000 "$p" "$modo" static 3; done
done

echo "== 3/4 Politicas de escalonamento (N=5M, $MAXT threads) =="
for sch in static "static,1" "dynamic,1" "dynamic,1000" guided; do
    roda schedule 5000000 "$MAXT" reduction "$sch" 3
done

echo "== 4/4 Escalabilidade fraca (2M pontos por thread) =="
for p in $THREADS; do roda fraca $((2000000 * p)) "$p" reduction static 3; done

echo "Gravado em $OUT"
grep -q ",NAO," "$OUT" && echo "ATENCAO: ha execucoes com checksum errado!" || echo "Todas as execucoes bateram com o sequencial."
