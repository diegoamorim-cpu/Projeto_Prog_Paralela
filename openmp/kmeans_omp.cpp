/* K-medias paralelo com OpenMP (memoria compartilhada)
 * Parte do baseline 10_kmeans.cpp: mesma entrada, mesma logica, mesmo checksum.
 *
 * Compilar: g++ -O2 -fopenmp -o kmeans_omp kmeans_omp.cpp    (ou: make omp)
 * Executar: OMP_NUM_THREADS=8 ./kmeans_omp 20000000 8 20 reduction
 *           argumentos: pontos, clusters, iteracoes, modo da acumulacao
 *           modo = reduction (padrao) | atomic | critical
 *           politica de escalonamento da atribuicao: variavel OMP_SCHEDULE
 *           (ex.: OMP_SCHEDULE=static, OMP_SCHEDULE="dynamic,1000", OMP_SCHEDULE=guided)
 *
 * Estrategia:
 *  - Fase 1 (atribuicao, ~82% do tempo): cada ponto e independente -> laco paralelo.
 *    So le px, py, cx, cy (compartilhados, somente leitura) e escreve rotulo[i],
 *    que cada thread escreve em posicoes diferentes -> nao ha condicao de corrida.
 *  - Fase 2a (acumulacao, ~17%): varios pontos do mesmo cluster somam em sx[j],
 *    sy[j], cnt[j] -> CONDICAO DE CORRIDA. Tres solucoes, escolhidas pelo argumento:
 *      reduction : cada thread soma numa copia privada dos vetores; no fim do laco
 *                  o OpenMP combina as copias. Sem sincronizacao dentro do laco.
 *      atomic    : cada soma vira uma operacao atomica de hardware. Correto, mas
 *                  as threads disputam as mesmas K posicoes de memoria.
 *      critical  : cada ponto entra numa secao critica (uma thread por vez).
 *                  Correto, mas serializa a fase inteira.
 *  - Fase 2b (divisao, O(K)): trivial, fica serial.
 *  - Geracao dos pontos: fica serial e fora do cronometro, igual ao baseline
 *    (o gerador depende do valor anterior de s).
 *
 * Corretude: as coordenadas sao inteiras e as somas cabem exatas num double,
 * entao a ordem da soma nao muda o resultado -> checksum identico ao baseline.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cstdint>
#include <cmath>
#include <omp.h>
using namespace std;

int main(int argc, char** argv) {
    int N = (argc > 1) ? atoi(argv[1]) : 1000000;
    int K = (argc > 2) ? atoi(argv[2]) : 8;
    int IT = (argc > 3) ? atoi(argv[3]) : 20;
    const char* modo = (argc > 4) ? argv[4] : "reduction";
    int m = !strcmp(modo, "atomic") ? 1 : !strcmp(modo, "critical") ? 2 : 0;
    if (m == 0) modo = "reduction";

    // Sem OMP_SCHEDULE, o GCC trata schedule(runtime) como "dynamic,1" (uma
    // iteracao por vez), o que e ~20x mais lento aqui. Padrao do programa: static.
    if (!getenv("OMP_SCHEDULE")) omp_set_schedule(omp_sched_static, 0);

    // ---- entrada: identica ao baseline (serial) ----
    vector<double> px(N), py(N);
    uint64_t s = 2024ULL;
    for (int i = 0; i < N; i++) {
        s = s*6364136223846793005ULL + 1; px[i] = (double)((s >> 40) % 1000);
        s = s*6364136223846793005ULL + 1; py[i] = (double)((s >> 40) % 1000);
    }
    vector<double> cx(K), cy(K);
    for (int j = 0; j < K; j++) { cx[j] = px[j]; cy[j] = py[j]; }
    vector<int> rotulo(N, 0);

    // ponteiros crus: a reducao de arrays do OpenMP trabalha com secoes de ponteiro
    const double *PX = px.data(), *PY = py.data();
    double *CX = cx.data(), *CY = cy.data();
    int *ROT = rotulo.data();

    double t0 = omp_get_wtime();
    for (int it = 0; it < IT; it++) {
        // ---- fase 1: atribuicao (paralela, sem corrida) ----
        // schedule(runtime): a politica vem de OMP_SCHEDULE, para comparar
        // static/dynamic/guided sem recompilar
        #pragma omp parallel for schedule(runtime)
        for (int i = 0; i < N; i++) {
            double best = 1e18; int bj = 0;
            for (int j = 0; j < K; j++) {
                double dx = PX[i]-CX[j], dy = PY[i]-CY[j];
                double d = dx*dx + dy*dy;
                if (d < best) { best = d; bj = j; }
            }
            ROT[i] = bj;
        }   // barreira implicita: ninguem acumula antes de todos atribuirem

        // ---- fase 2a: acumulacao (corrida tratada conforme o modo) ----
        vector<double> sx(K, 0), sy(K, 0); vector<long> cnt(K, 0);
        double *SX = sx.data(), *SY = sy.data(); long *CNT = cnt.data();

        if (m == 0) {
            #pragma omp parallel for schedule(static) reduction(+: SX[:K], SY[:K], CNT[:K])
            for (int i = 0; i < N; i++) { int j = ROT[i]; SX[j] += PX[i]; SY[j] += PY[i]; CNT[j]++; }
        } else if (m == 1) {
            #pragma omp parallel for schedule(static)
            for (int i = 0; i < N; i++) {
                int j = ROT[i];
                #pragma omp atomic
                SX[j] += PX[i];
                #pragma omp atomic
                SY[j] += PY[i];
                #pragma omp atomic
                CNT[j]++;
            }
        } else {
            #pragma omp parallel for schedule(static)
            for (int i = 0; i < N; i++) {
                int j = ROT[i];
                #pragma omp critical
                { SX[j] += PX[i]; SY[j] += PY[i]; CNT[j]++; }
            }
        }

        // ---- fase 2b: divisao (serial, O(K)) ----
        for (int j = 0; j < K; j++) if (CNT[j]) { CX[j] = SX[j]/CNT[j]; CY[j] = SY[j]/CNT[j]; }
    }
    double t1 = omp_get_wtime();

    double soma = 0;
    for (int j = 0; j < K; j++) soma += cx[j] + cy[j];
    omp_sched_t tipo; int chunk; omp_get_schedule(&tipo, &chunk);
    const char* nome = tipo == omp_sched_static ? "static" : tipo == omp_sched_dynamic ? "dynamic"
                     : tipo == omp_sched_guided ? "guided" : "auto";
    printf("N=%d K=%d it=%d threads=%d modo=%s schedule=%s,%d  checksum=%.4f  tempo=%.6f s\n",
           N, K, IT, omp_get_max_threads(), modo, nome, chunk, soma, t1 - t0);
    return 0;
}
