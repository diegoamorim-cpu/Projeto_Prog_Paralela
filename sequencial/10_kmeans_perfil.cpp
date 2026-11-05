/* Copia INSTRUMENTADA de 10_kmeans.cpp, usada SOMENTE para o perfilamento.
 * A logica de calculo e a entrada sao identicas ao original; so foram
 * acrescentados cronometros em volta de cada trecho. O checksum tem que
 * bater com o do original (e isso que mostra que nao mudamos nada).
 *
 * Compilar: g++ -O2 -o 10_kmeans_perfil 10_kmeans_perfil.cpp
 * Executar: ./10_kmeans_perfil 1000000 8 20
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

    double tg0 = agora();
    vector<double> px(N), py(N);
    uint64_t s = 2024ULL;
    for (int i = 0; i < N; i++) {
        s = s*6364136223846793005ULL + 1; px[i] = (double)((s >> 40) % 1000);
        s = s*6364136223846793005ULL + 1; py[i] = (double)((s >> 40) % 1000);
    }
    vector<double> cx(K), cy(K);
    for (int j = 0; j < K; j++) { cx[j] = px[j]; cy[j] = py[j]; }
    vector<int> rotulo(N, 0);
    double t_geracao = agora() - tg0;

    double t_atrib = 0, t_acum = 0, t_div = 0;

    double t0 = agora();
    for (int it = 0; it < IT; it++) {
        double a = agora();
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
        double b = agora();
        // fase 2a — acumulacao das somas por cluster
        vector<double> sx(K, 0), sy(K, 0); vector<long> cnt(K, 0);
        for (int i = 0; i < N; i++) { int j = rotulo[i]; sx[j]+=px[i]; sy[j]+=py[i]; cnt[j]++; }
        double c = agora();
        // fase 2b — divisao (novos centroides)
        for (int j = 0; j < K; j++) if (cnt[j]) { cx[j]=sx[j]/cnt[j]; cy[j]=sy[j]/cnt[j]; }
        double d = agora();
        t_atrib += b - a; t_acum += c - b; t_div += d - c;
    }
    double t1 = agora();

    double soma = 0;
    for (int j = 0; j < K; j++) soma += cx[j] + cy[j];
    double total = t1 - t0;
    // linha no formato CSV: N,K,IT,checksum,t_geracao,t_atrib,t_acum,t_div,t_total
    printf("%d,%d,%d,%.4f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
           N, K, IT, soma, t_geracao, t_atrib, t_acum, t_div, total);
    return 0;
}
