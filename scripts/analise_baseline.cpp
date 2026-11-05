/* Analise do baseline (C++).
 * Le resultados/baseline.csv e resultados/perfil.csv e gera:
 *   resultados/tabela_baseline.csv  mediana do tempo por tamanho
 *   resultados/tabela_perfil.csv    % por fase, fracao paralelizavel e Amdahl
 *   resultados/perfil_fases.svg     grafico de barras empilhadas
 * Compilar: make analise      Executar: ./scripts/analise_baseline
 * (sempre a partir da raiz do repositorio)
 */
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
using namespace std;

static vector<string> separa(const string& linha) {
    vector<string> campos; string c; stringstream ss(linha);
    while (getline(ss, c, ',')) campos.push_back(c);
    return campos;
}

static double mediana(vector<double> v) {
    sort(v.begin(), v.end());
    size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

static double amdahl(double f, int p) { return 1.0 / ((1.0 - f) + f / p); }

struct Fases { vector<double> ger, atrib, acum, serial, total, fA, fB; };

int main() {
    // ---------- baseline.csv: maquina,nucleos,N,K,IT,rep,checksum,tempo_s ----------
    ifstream fb("resultados/baseline.csv");
    if (!fb) { fprintf(stderr, "Nao achei resultados/baseline.csv\n"); return 1; }
    map<long, vector<double>> tempos;
    map<long, string> checksum;
    string linha, maquina, nucleos;
    getline(fb, linha);                         // cabecalho
    while (getline(fb, linha)) {
        auto c = separa(linha);
        if (c.size() < 8) continue;
        maquina = c[0]; nucleos = c[1];
        long N = stol(c[2]);
        checksum[N] = c[6];
        tempos[N].push_back(stod(c[7]));
    }

    printf("Maquina: %s (%s nucleos)\n\n== Baseline (mediana) ==\n", maquina.c_str(), nucleos.c_str());
    printf("%10s %12s %10s %10s %10s\n", "N", "checksum", "mediana_s", "min_s", "max_s");
    ofstream tb("resultados/tabela_baseline.csv");
    tb << "N,checksum,mediana_s,min_s,max_s\n";
    for (auto& [N, v] : tempos) {
        double med = mediana(v);
        double mn = *min_element(v.begin(), v.end()), mx = *max_element(v.begin(), v.end());
        printf("%10ld %12s %10.4f %10.4f %10.4f\n", N, checksum[N].c_str(), med, mn, mx);
        tb << N << "," << checksum[N] << "," << med << "," << mn << "," << mx << "\n";
    }

    // ---------- perfil.csv: N,K,IT,checksum,t_geracao,t_atrib,t_acum,t_div,t_total ----------
    ifstream fp("resultados/perfil.csv");
    if (!fp) { fprintf(stderr, "Nao achei resultados/perfil.csv\n"); return 1; }
    map<long, Fases> P;
    getline(fp, linha);
    while (getline(fp, linha)) {
        auto c = separa(linha);
        if (c.size() < 9) continue;
        long N = stol(c[0]);
        double ger = stod(c[4]), atrib = stod(c[5]), acum = stod(c[6]), total = stod(c[8]);
        double par = atrib + acum;
        // serial do laco = divisao + sobra; a sobra e tao pequena que o ruido
        // pode deixa-la negativa, entao corta em zero
        double serial = max(0.0, total - par);
        Fases& f = P[N];
        f.ger.push_back(ger); f.atrib.push_back(atrib); f.acum.push_back(acum);
        f.serial.push_back(serial); f.total.push_back(total);
        f.fA.push_back(par / (par + serial));          // visao A: so o laco cronometrado
        f.fB.push_back(par / (par + serial + ger));    // visao B: programa inteiro
    }

    struct Linha { long N; double ger, atrib, acum, serial, total, fA, fB; };
    vector<Linha> L;
    for (auto& [N, f] : P)
        L.push_back({N, mediana(f.ger), mediana(f.atrib), mediana(f.acum), mediana(f.serial),
                     mediana(f.total), mediana(f.fA), mediana(f.fB)});

    printf("\n== Perfil por fase (mediana, %% do laco cronometrado) ==\n");
    printf("%10s %9s %10s %10s %10s %10s\n", "N", "total_s", "%atrib", "%acum", "%serial", "geracao_s");
    for (auto& l : L)
        printf("%10ld %9.4f %10.2f %10.2f %10.4f %10.4f\n", l.N, l.total,
               100 * l.atrib / l.total, 100 * l.acum / l.total, 100 * l.serial / l.total, l.ger);

    int ps[] = {2, 4, 8, 16};
    printf("\n== Amdahl - visao A (so o laco cronometrado) ==\n%10s %8s", "N", "f");
    for (int p : ps) printf("   S(%2d)", p);
    printf("\n");
    for (auto& l : L) {
        printf("%10ld %8.5f", l.N, l.fA);
        for (int p : ps) printf(" %7.3f", amdahl(l.fA, p));
        printf("\n");
    }
    printf("\n== Amdahl - visao B (programa inteiro, geracao serial) ==\n%10s %8s", "N", "f");
    for (int p : ps) printf("   S(%2d)", p);
    printf("   S(inf)\n");
    for (auto& l : L) {
        printf("%10ld %8.5f", l.N, l.fB);
        for (int p : ps) printf(" %7.3f", amdahl(l.fB, p));
        printf(" %8.2f\n", 1.0 / (1.0 - l.fB));
    }

    ofstream tp("resultados/tabela_perfil.csv");
    tp << "N,t_total,t_atrib,t_acum,t_serial,t_geracao,pct_atrib,pct_acum,pct_serial,fA,fB";
    for (int p : ps) tp << ",SA_" << p;
    for (int p : ps) tp << ",SB_" << p;
    tp << ",SB_max\n";
    for (auto& l : L) {
        tp << l.N << "," << l.total << "," << l.atrib << "," << l.acum << "," << l.serial << ","
           << l.ger << "," << 100 * l.atrib / l.total << "," << 100 * l.acum / l.total << ","
           << 100 * l.serial / l.total << "," << l.fA << "," << l.fB;
        for (int p : ps) tp << "," << amdahl(l.fA, p);
        for (int p : ps) tp << "," << amdahl(l.fB, p);
        tp << "," << 1.0 / (1.0 - l.fB) << "\n";
    }

    // ---------- grafico SVG (barras empilhadas) ----------
    const double W = 720, H = 420, x0 = 70, y0 = 60, pw = 600, ph = 290;
    double ymax = 0;
    for (auto& l : L) ymax = max(ymax, l.atrib + l.acum + l.ger);
    double passo = ymax <= 1 ? 0.2 : ymax <= 2.5 ? 0.5 : ymax <= 6 ? 1 : 2;
    double topo = passo * (int)(ymax / passo + 1);
    auto Y = [&](double v) { return y0 + ph - v / topo * ph; };

    ofstream sv("resultados/perfil_fases.svg");
    sv << "<svg xmlns='http://www.w3.org/2000/svg' width='" << W << "' height='" << H
       << "' font-family='sans-serif' font-size='13'>\n<rect width='100%' height='100%' fill='white'/>\n"
       << "<text x='" << W / 2 << "' y='28' text-anchor='middle' font-size='16'>Perfil do baseline sequencial por fase</text>\n";
    for (double v = 0; v <= topo + 1e-9; v += passo) {
        sv << "<line x1='" << x0 << "' x2='" << x0 + pw << "' y1='" << Y(v) << "' y2='" << Y(v)
           << "' stroke='#e5e5e5'/>\n<text x='" << x0 - 8 << "' y='" << Y(v) + 4
           << "' text-anchor='end'>" << v << "</text>\n";
    }
    const char* cor[] = {"#2a6fdb", "#f08c2e", "#bbbbbb"};
    double slot = pw / L.size(), bw = slot * 0.6;
    for (size_t i = 0; i < L.size(); i++) {
        double x = x0 + i * slot + (slot - bw) / 2, base = 0;
        double val[] = {L[i].atrib, L[i].acum, L[i].ger};
        for (int k = 0; k < 3; k++) {
            sv << "<rect x='" << x << "' y='" << Y(base + val[k]) << "' width='" << bw
               << "' height='" << Y(base) - Y(base + val[k]) << "' fill='" << cor[k] << "'/>\n";
            base += val[k];
        }
        sv << "<text x='" << x + bw / 2 << "' y='" << y0 + ph + 20 << "' text-anchor='middle'>"
           << L[i].N / 1000000.0 << " M</text>\n";
    }
    sv << "<line x1='" << x0 << "' x2='" << x0 + pw << "' y1='" << y0 + ph << "' y2='" << y0 + ph << "' stroke='#333'/>\n"
       << "<text x='" << x0 + pw / 2 << "' y='" << H - 22 << "' text-anchor='middle'>Numero de pontos (N), K=8, 20 iteracoes</text>\n"
       << "<text transform='translate(20," << y0 + ph / 2 << ") rotate(-90)' text-anchor='middle'>Tempo (s, mediana)</text>\n";
    const char* nome[] = {"Fase 1: atribuicao", "Fase 2a: acumulacao", "Geracao dos pontos (fora do cronometro, serial)"};
    for (int k = 0; k < 3; k++) {
        double ly = y0 + 4 + k * 20;
        sv << "<rect x='" << x0 + 12 << "' y='" << ly << "' width='14' height='12' fill='" << cor[k] << "'/>\n"
           << "<text x='" << x0 + 32 << "' y='" << ly + 11 << "'>" << nome[k] << "</text>\n";
    }
    sv << "</svg>\n";

    printf("\nGerados: resultados/tabela_baseline.csv, resultados/tabela_perfil.csv, resultados/perfil_fases.svg\n");
    return 0;
}
