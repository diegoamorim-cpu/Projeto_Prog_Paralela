/* K-medias paralelo com MPI (memoria distribuida)
 * Parte do baseline 10_kmeans.cpp: mesma entrada, mesma logica, mesmo checksum.
 *
 * Compilar: mpicxx -O2 -o kmeans_mpi kmeans_mpi.cpp    (ou: make mpi)
 * Executar: mpirun -np 4 ./kmeans_mpi 20000000 8 20
 *           argumentos: pontos, clusters, iteracoes
 *
 * Estrategia: DECOMPOSICAO DE DOMINIO. Cada processo fica com ~N/p pontos.
 *  - Rank 0 gera os N pontos (o gerador e serial: s depende do valor anterior)
 *    e distribui com MPI_Scatterv (px e py). Isso fica FORA do cronometro,
 *    igual a geracao no baseline.
 *  - Os K centroides iniciais (primeiros K pontos) saem do rank 0 por MPI_Bcast.
 *  - A cada iteracao:
 *      fase 1 (local): atribuicao dos pontos do processo ao centroide mais proximo
 *      fase 2a (local): somas parciais sx, sy, cnt dos pontos do processo
 *      MPI_Allreduce(SUM) das somas parciais (3*K valores, independe de N)
 *      fase 2b: todos calculam os mesmos centroides novos (K divisoes, redundante
 *               de proposito: evita um Bcast extra)
 *  - Contagens (cnt) vao como double no mesmo buffer: assim e UMA so Allreduce
 *    por iteracao. Cabem exatas em double (N <= 2^53).
 *
 * Corretude: coordenadas inteiras (0..999), somas exatas em double, entao a
 * ordem da reducao nao muda o resultado -> checksum identico ao baseline com
 * qualquer numero de processos. Se divergir, e bug.
 *
 * Cronometro: MPI_Barrier antes de t0 e depois do laco; o tempo reportado e o
 * MAIOR entre os processos (o programa so termina quando o mais lento termina).
 * "comm" e o tempo gasto dentro da Allreduce (maximo entre processos) e inclui
 * a espera por processos mais lentos; serve para analisar o overhead.
 */
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstdint>
#include <mpi.h>
using namespace std;

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, p;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &p);

    int N = (argc > 1) ? atoi(argv[1]) : 1000000;
    int K = (argc > 2) ? atoi(argv[2]) : 8;
    int IT = (argc > 3) ? atoi(argv[3]) : 20;

    // ---- decomposicao: blocos contiguos; os primeiros (N % p) ficam com 1 ponto a mais ----
    vector<int> counts(p), displs(p);
    for (int r = 0, d = 0; r < p; r++) {
        counts[r] = N / p + (r < N % p ? 1 : 0);
        displs[r] = d;
        d += counts[r];
    }
    int nloc = counts[rank];

    // ---- entrada: rank 0 gera (identica ao baseline) e distribui ----
    vector<double> px_all, py_all;
    vector<double> cx(K), cy(K);
    if (rank == 0) {
        px_all.resize(N); py_all.resize(N);
        uint64_t s = 2024ULL;
        for (int i = 0; i < N; i++) {
            s = s*6364136223846793005ULL + 1; px_all[i] = (double)((s >> 40) % 1000);
            s = s*6364136223846793005ULL + 1; py_all[i] = (double)((s >> 40) % 1000);
        }
        for (int j = 0; j < K; j++) { cx[j] = px_all[j]; cy[j] = py_all[j]; }  // centroides iniciais
    }
    vector<double> px(nloc), py(nloc);
    MPI_Scatterv(px_all.data(), counts.data(), displs.data(), MPI_DOUBLE,
                 px.data(), nloc, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Scatterv(py_all.data(), counts.data(), displs.data(), MPI_DOUBLE,
                 py.data(), nloc, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Bcast(cx.data(), K, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Bcast(cy.data(), K, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    vector<double>().swap(px_all);   // libera a memoria do rank 0
    vector<double>().swap(py_all);

    vector<int> rotulo(nloc, 0);
    vector<double> loc(3 * K), glob(3 * K);   // [sx(K) | sy(K) | cnt(K)]
    double t_comm = 0;

    MPI_Barrier(MPI_COMM_WORLD);
    double t0 = MPI_Wtime();
    for (int it = 0; it < IT; it++) {
        // ---- fase 1: atribuicao (local, sem comunicacao) ----
        for (int i = 0; i < nloc; i++) {
            double best = 1e18; int bj = 0;
            for (int j = 0; j < K; j++) {
                double dx = px[i]-cx[j], dy = py[i]-cy[j];
                double d = dx*dx + dy*dy;
                if (d < best) { best = d; bj = j; }
            }
            rotulo[i] = bj;
        }
        // ---- fase 2a: somas parciais locais ----
        for (int j = 0; j < 3 * K; j++) loc[j] = 0;
        for (int i = 0; i < nloc; i++) {
            int j = rotulo[i];
            loc[j] += px[i]; loc[K + j] += py[i]; loc[2*K + j] += 1.0;
        }
        // ---- combinacao global: uma Allreduce por iteracao ----
        double c0 = MPI_Wtime();
        MPI_Allreduce(loc.data(), glob.data(), 3 * K, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        t_comm += MPI_Wtime() - c0;
        // ---- fase 2b: novos centroides (todos calculam o mesmo) ----
        for (int j = 0; j < K; j++)
            if (glob[2*K + j] > 0) { cx[j] = glob[j] / glob[2*K + j]; cy[j] = glob[K + j] / glob[2*K + j]; }
    }
    MPI_Barrier(MPI_COMM_WORLD);
    double t1 = MPI_Wtime();

    double tempo = t1 - t0, tempo_max, comm_max;
    MPI_Reduce(&tempo, &tempo_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&t_comm, &comm_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        double soma = 0;
        for (int j = 0; j < K; j++) soma += cx[j] + cy[j];
        printf("N=%d K=%d it=%d procs=%d  checksum=%.4f  tempo=%.6f s  comm=%.6f s\n",
               N, K, IT, p, soma, tempo_max, comm_max);
    }
    MPI_Finalize();
    return 0;
}
