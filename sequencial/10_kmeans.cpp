/* Baseline 10 — Agrupamento por k-medias (k-means, algoritmo de Lloyd)
 * IESB 2026/2 — CCO085 — Prof. Rodrigo Goncalves Pinto
 * Aspecto interessante: fases paralelas alternadas — atribuicao (paralela)
 * e recalculo dos centroides (reducao) — com sincronizacao global a cada passo.
 *
 * Compilar: g++ -O2 -o 10_kmeans 10_kmeans.cpp
 * Executar: ./10_kmeans 1000000 8 20   (pontos; clusters; iteracoes)
 */
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstdint>
#include <cmath>
#include <ctime>
using namespace std;

static double agora() {
    timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int main(int argc, char** argv) {
    int N = (argc > 1) ? atoi(argv[1]) : 1000000;
    int K = (argc > 2) ? atoi(argv[2]) : 8;
    int IT = (argc > 3) ? atoi(argv[3]) : 20;

    vector<double> px(N), py(N);
    uint64_t s = 2024ULL;
    for (int i = 0; i < N; i++) {
        s = s*6364136223846793005ULL + 1; px[i] = (double)((s >> 40) % 1000);
        s = s*6364136223846793005ULL + 1; py[i] = (double)((s >> 40) % 1000);
    }
    vector<double> cx(K), cy(K);
    for (int j = 0; j < K; j++) { cx[j] = px[j]; cy[j] = py[j]; }  // centroides iniciais

    vector<int> rotulo(N, 0);

    double t0 = agora();
    for (int it = 0; it < IT; it++) {
        // fase 1 — atribuicao
        for (int i = 0; i < N; i++) {
            double best = 1e18; int bj = 0;
            for (int j = 0; j < K; j++) {
                double dx = px[i]-cx[j], dy = py[i]-cy[j];
                double d = dx*dx + dy*dy;
                if (d < best) { best = d; bj = j; }
            }
            rotulo[i] = bj;
        }
        // fase 2 — recalculo dos centroides (reducao)
        vector<double> sx(K, 0), sy(K, 0); vector<long> cnt(K, 0);
        for (int i = 0; i < N; i++) { int j = rotulo[i]; sx[j]+=px[i]; sy[j]+=py[i]; cnt[j]++; }
        for (int j = 0; j < K; j++) if (cnt[j]) { cx[j]=sx[j]/cnt[j]; cy[j]=sy[j]/cnt[j]; }
    }
    double t1 = agora();

    double soma = 0;
    for (int j = 0; j < K; j++) soma += cx[j] + cy[j];
    printf("N=%d K=%d it=%d  checksum=%.4f  tempo=%.6f s\n", N, K, IT, soma, t1 - t0);
    return 0;
}
