#!/usr/bin/env bash
# Medicoes da versao MPI -> resultados/mpi.csv (mesmo formato do openmp.csv)
#                        -> resultados/mpi_comm.csv (overhead de comunicacao)
# Experimentos:
#   forte : N fixo (20M), varia o numero de processos      -> speedup e eficiencia
#   fraca : N cresce junto com os processos (2M por proc)  -> eficiencia fraca
# O tempo de comunicacao (Allreduce) e gravado a parte, para a analise de overhead.
# Protocolo: 1 execucao de aquecimento descartada + REPS medidas (mediana na analise).
# Cada execucao e validada contra o checksum do sequencial para o mesmo N.
#
# Uso (na raiz do repositorio):  bash scripts/medir_mpi.sh
#      PROCS="1 2 4 8" REPS=3 bash scripts/medir_mpi.sh   (personalizar)
set -euo pipefail

SEQ=./sequencial/10_kmeans
BIN=./mpi/kmeans_mpi
OUT=resultados/mpi.csv
COMM=resultados/mpi_comm.csv
K=8; IT=20
PROCS=${PROCS:-"1 2 4 6 8 10 12"}
REPS=${REPS:-5}
# Se pedir mais processos que nucleos, o OpenMPI exige --oversubscribe
MPIFLAGS=${MPIFLAGS:-"--oversubscribe"}

[ -x "$BIN" ] || { echo "Compile antes: make mpi"; exit 1; }
mkdir -p resultados
echo "experimento,versao,N,K,IT,p,modo,schedule,rep,checksum,ok,tempo_s" > "$OUT"
echo "experimento,N,p,rep,tempo_s,comm_s" > "$COMM"

declare -A GAB
gabarito() {
    local N=$1
    if [ -z "${GAB[$N]:-}" ]; then
        GAB[$N]=$("$SEQ" "$N" "$K" "$IT" | sed -E 's/.*checksum=([0-9.]+).*/\1/')
    fi
    echo "${GAB[$N]}"
}

# roda: experimento N processos reps
roda() {
    local exp=$1 N=$2 p=$3 reps=$4
    local ref; ref=$(gabarito "$N")
    mpirun $MPIFLAGS -np "$p" "$BIN" "$N" "$K" "$IT" > /dev/null   # aquecimento
    for r in $(seq 1 "$reps"); do
        local linha chk t c ok
        linha=$(mpirun $MPIFLAGS -np "$p" "$BIN" "$N" "$K" "$IT")
        chk=$(echo "$linha" | sed -E 's/.*checksum=([0-9.]+).*/\1/')
        t=$(echo "$linha"   | sed -E 's/.*tempo=([0-9.]+).*/\1/')
        c=$(echo "$linha"   | sed -E 's/.*comm=([0-9.]+).*/\1/')
        if [ "$chk" == "$ref" ]; then ok=SIM; else ok=NAO; fi
        echo "$exp,mpi,$N,$K,$IT,$p,-,-,$r,$chk,$ok,$t" >> "$OUT"
        echo "$exp,$N,$p,$r,$t,$c" >> "$COMM"
        printf "%-6s N=%-9s p=%-3s checksum=%s (%s) tempo=%s comm=%s\n" "$exp" "$N" "$p" "$chk" "$ok" "$t" "$c"
        [ "$ok" == "SIM" ] || echo "   *** CHECKSUM DIFERENTE DO SEQUENCIAL ($ref) ***"
    done
}

echo "== 1/2 Escalabilidade forte (N=20M) =="
for p in $PROCS; do roda forte 20000000 "$p" "$REPS"; done

echo "== 2/2 Escalabilidade fraca (2M pontos por processo) =="
for p in $PROCS; do roda fraca $((2000000 * p)) "$p" 3; done

echo "Gravado em $OUT e $COMM"
grep -q ",NAO," "$OUT" && echo "ATENCAO: ha execucoes com checksum errado!" || echo "Todas as execucoes bateram com o sequencial."
