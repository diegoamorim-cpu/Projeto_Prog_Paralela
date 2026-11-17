/* Analise das versoes paralelas (C++). Serve para OpenMP e para MPI.
 *
 * Le resultados/<versao>.csv  com as colunas
 *    experimento,versao,N,K,IT,p,modo,schedule,rep,checksum,ok,tempo_s
 * e resultados/tabela_baseline.csv (gerada por analise_baseline) para o tempo sequencial.
 *
 * Gera, para cada experimento presente no CSV:
 *   forte    -> <versao>_forte.csv, <versao>_tempo.svg, <versao>_speedup.svg, <versao>_eficiencia.svg
 *   corrida  -> <versao>_corrida.csv, <versao>_corrida.svg
 *   schedule -> <versao>_schedule.csv, <versao>_schedule.svg
 *   fraca    -> <versao>_fraca.csv, <versao>_fraca.svg
 *   comm     -> <versao>_comm_tabela.csv, <versao>_comm.svg  (se existir resultados/<versao>_comm.csv)
 *
 * Compilar: make analise      Executar: ./scripts/analise_paralelo openmp   (ou mpi)
 *
 * Definicoes:
 *   speedup    S(p) = T_sequencial / T_paralelo(p)   (contra o BASELINE, nao contra p=1)
 *   eficiencia E(p) = S(p) / p
 *   fraca      E_fraca(p) = T(1 thread, N1) / T(p threads, p*N1)   (ideal = 1, tempo constante)
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
using namespace std;

static vector<string> separa(const string& l) {
    vector<string> c; string x; stringstream ss(l);
    while (getline(ss, x, ',')) c.push_back(x);
    return c;
}
static double mediana(vector<double> v) {
    sort(v.begin(), v.end()); size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

// ------------------------------------------------------------ graficos SVG
struct Serie { string nome, cor; vector<pair<double, double>> pts; bool tracejada = false; };
static const char* CORES[] = {"#2a6fdb", "#f08c2e", "#2e9e5b", "#c0392b", "#8e44ad"};

static double passo_bonito(double maxv) {
    double bruto = maxv / 5, mag = pow(10, floor(log10(bruto))), r = bruto / mag;
    return (r <= 1 ? 1 : r <= 2 ? 2 : r <= 5 ? 5 : 10) * mag;
}
static string num(double v) { char b[32]; snprintf(b, sizeof b, "%g", v); return b; }

static void grafico_linhas(const string& arq, const string& titulo, const string& tx, const string& ty,
                           const vector<Serie>& S, double ymax_min = 0) {
    const double W = 720, H = 440, x0 = 70, y0 = 60, pw = 600, ph = 300;
    double xmax = 0, ymax = ymax_min;
    for (auto& s : S) for (auto& p : s.pts) { xmax = max(xmax, p.first); ymax = max(ymax, p.second); }
    double py = passo_bonito(ymax), topo = py * ceil(ymax / py + 1e-9);
    double px = xmax <= 12 ? 1 : passo_bonito(xmax), xt = px * ceil(xmax / px);
    auto X = [&](double v) { return x0 + v / xt * pw; };
    auto Y = [&](double v) { return y0 + ph - v / topo * ph; };
    ofstream f(arq);
    f << "<svg xmlns='http://www.w3.org/2000/svg' width='" << W << "' height='" << H
      << "' font-family='sans-serif' font-size='13'>\n<rect width='100%' height='100%' fill='white'/>\n"
      << "<text x='" << W / 2 << "' y='28' text-anchor='middle' font-size='16'>" << titulo << "</text>\n";
    for (double v = 0; v <= topo + 1e-9; v += py)
        f << "<line x1='" << x0 << "' x2='" << x0 + pw << "' y1='" << Y(v) << "' y2='" << Y(v) << "' stroke='#e5e5e5'/>"
          << "<text x='" << x0 - 8 << "' y='" << Y(v) + 4 << "' text-anchor='end'>" << num(v) << "</text>\n";
    for (double v = 0; v <= xt + 1e-9; v += px)
        f << "<text x='" << X(v) << "' y='" << y0 + ph + 20 << "' text-anchor='middle'>" << num(v) << "</text>\n";
    f << "<line x1='" << x0 << "' x2='" << x0 + pw << "' y1='" << y0 + ph << "' y2='" << y0 + ph << "' stroke='#333'/>\n"
      << "<text x='" << x0 + pw / 2 << "' y='" << H - 22 << "' text-anchor='middle'>" << tx << "</text>\n"
      << "<text transform='translate(20," << y0 + ph / 2 << ") rotate(-90)' text-anchor='middle'>" << ty << "</text>\n";
    for (size_t k = 0; k < S.size(); k++) {
        auto& s = S[k];
        f << "<polyline fill='none' stroke='" << s.cor << "' stroke-width='2'"
          << (s.tracejada ? " stroke-dasharray='6 4'" : "") << " points='";
        for (auto& p : s.pts) f << X(p.first) << "," << Y(p.second) << " ";
        f << "'/>\n";
        if (!s.tracejada)
            for (auto& p : s.pts) f << "<circle cx='" << X(p.first) << "' cy='" << Y(p.second) << "' r='4' fill='" << s.cor << "'/>\n";
        double ly = y0 + 4 + k * 20;
        f << "<line x1='" << x0 + 12 << "' x2='" << x0 + 34 << "' y1='" << ly + 6 << "' y2='" << ly + 6 << "' stroke='" << s.cor
          << "' stroke-width='2'" << (s.tracejada ? " stroke-dasharray='6 4'" : "") << "/>"
          << "<text x='" << x0 + 40 << "' y='" << ly + 11 << "'>" << s.nome << "</text>\n";
    }
    f << "</svg>\n";
}

static void grafico_barras(const string& arq, const string& titulo, const string& ty,
                           const vector<pair<string, double>>& B) {
    const double W = 720, H = 420, x0 = 70, y0 = 60, pw = 600, ph = 290;
    double ymax = 0; for (auto& b : B) ymax = max(ymax, b.second);
    double py = passo_bonito(ymax), topo = py * ceil(ymax / py + 1e-9);
    auto Y = [&](double v) { return y0 + ph - v / topo * ph; };
    ofstream f(arq);
    f << "<svg xmlns='http://www.w3.org/2000/svg' width='" << W << "' height='" << H
      << "' font-family='sans-serif' font-size='13'>\n<rect width='100%' height='100%' fill='white'/>\n"
      << "<text x='" << W / 2 << "' y='28' text-anchor='middle' font-size='16'>" << titulo << "</text>\n";
    for (double v = 0; v <= topo + 1e-9; v += py)
        f << "<line x1='" << x0 << "' x2='" << x0 + pw << "' y1='" << Y(v) << "' y2='" << Y(v) << "' stroke='#e5e5e5'/>"
          << "<text x='" << x0 - 8 << "' y='" << Y(v) + 4 << "' text-anchor='end'>" << num(v) << "</text>\n";
    double slot = pw / B.size(), bw = slot * 0.6;
    for (size_t i = 0; i < B.size(); i++) {
        double x = x0 + i * slot + (slot - bw) / 2;
        f << "<rect x='" << x << "' y='" << Y(B[i].second) << "' width='" << bw << "' height='" << Y(0) - Y(B[i].second)
          << "' fill='#2a6fdb'/><text x='" << x + bw / 2 << "' y='" << Y(B[i].second) - 6 << "' text-anchor='middle'>"
          << num(round(B[i].second * 1000) / 1000) << "</text><text x='" << x + bw / 2 << "' y='" << y0 + ph + 20
          << "' text-anchor='middle'>" << B[i].first << "</text>\n";
    }
    f << "<line x1='" << x0 << "' x2='" << x0 + pw << "' y1='" << y0 + ph << "' y2='" << y0 + ph << "' stroke='#333'/>\n"
      << "<text transform='translate(20," << y0 + ph / 2 << ") rotate(-90)' text-anchor='middle'>" << ty << "</text>\n</svg>\n";
}

// ------------------------------------------------------------ principal
int main(int argc, char** argv) {
    string v = argc > 1 ? argv[1] : "openmp";
    string rot = v == "mpi" ? "processos" : "threads";

    map<long, double> Tseq;   // mediana do sequencial por N
    {
        ifstream f("resultados/tabela_baseline.csv"); string l;
        if (!f) { fprintf(stderr, "Falta resultados/tabela_baseline.csv (rode ./scripts/analise_baseline)\n"); return 1; }
        getline(f, l);
        while (getline(f, l)) { auto c = separa(l); if (c.size() >= 3) Tseq[stol(c[0])] = stod(c[2]); }
    }

    // chave: experimento | N | p | modo | schedule  -> tempos
    struct Chave { string exp; long N; int p; string modo, sch;
        bool operator<(const Chave& o) const { return tie(exp, N, p, modo, sch) < tie(o.exp, o.N, o.p, o.modo, o.sch); } };
    map<Chave, vector<double>> T;
    int erros = 0, total = 0;
    {
        string arq = "resultados/" + v + ".csv";
        ifstream f(arq); string l;
        if (!f) { fprintf(stderr, "Falta %s\n", arq.c_str()); return 1; }
        getline(f, l);
        while (getline(f, l)) {
            auto c = separa(l); if (c.size() < 12) continue;
            total++; if (c[10] != "SIM") erros++;
            T[{c[0], stol(c[2]), stoi(c[5]), c[6], c[7]}].push_back(stod(c[11]));
        }
    }
    printf("Versao: %s  |  %d execucoes, %d com checksum errado\n", v.c_str(), total, erros);
    if (erros) printf("*** ATENCAO: corrija os erros de corretude antes de analisar desempenho ***\n");

    // ---------------- escalabilidade forte
    vector<pair<int, double>> forte; long Nf = 0;
    for (auto& [k, t] : T) if (k.exp == "forte") { forte.push_back({k.p, mediana(t)}); Nf = k.N; }
    if (!forte.empty()) {
        sort(forte.begin(), forte.end());
        double ts = Tseq.count(Nf) ? Tseq[Nf] : 0;
        if (!ts) printf("(sem baseline para N=%ld; speedup calculado contra p=1)\n", Nf);
        if (!ts) ts = forte[0].second;
        printf("\n== Escalabilidade forte (N=%ld, T_seq=%.4f s) ==\n%6s %10s %9s %11s\n", Nf, ts, rot.c_str(), "tempo_s", "speedup", "eficiencia");
        ofstream o("resultados/" + v + "_forte.csv"); o << "p,tempo_s,speedup,eficiencia\n";
        Serie s{"medido", CORES[0]}, id{"ideal (S = p)", "#999999", {}, true}, e{"medida", CORES[0]}, e1{"ideal (100%)", "#999999", {}, true};
        Serie tm{"medido", CORES[0]}, tid{"ideal (T_seq / p)", "#999999", {}, true};
        for (auto& [p, t] : forte) {
            double S = ts / t, E = S / p;
            printf("%6d %10.4f %9.3f %10.1f%%\n", p, t, S, 100 * E);
            o << p << "," << t << "," << S << "," << E << "\n";
            s.pts.push_back({p, S}); id.pts.push_back({p, p}); e.pts.push_back({p, 100 * E}); e1.pts.push_back({p, 100});
            tm.pts.push_back({p, t}); tid.pts.push_back({p, ts / p});
        }
        grafico_linhas("resultados/" + v + "_speedup.svg", "Speedup " + v + " (N=" + num(Nf / 1e6) + "M, K=8, 20 iteracoes)",
                       "Numero de " + rot, "Speedup (T_seq / T_p)", {s, id});
        grafico_linhas("resultados/" + v + "_tempo.svg", "Tempo de execucao " + v + " (N=" + num(Nf / 1e6) + "M)",
                       "Numero de " + rot, "Tempo (s, mediana)", {tm, tid});
        grafico_linhas("resultados/" + v + "_eficiencia.svg", "Eficiencia " + v + " (N=" + num(Nf / 1e6) + "M)",
                       "Numero de " + rot, "Eficiencia (%)", {e, e1}, 100);
    }

    // ---------------- condicao de corrida (modos)
    map<string, vector<pair<int, double>>> cor; long Nc = 0;
    for (auto& [k, t] : T) if (k.exp == "corrida") { cor[k.modo].push_back({k.p, mediana(t)}); Nc = k.N; }
    if (!cor.empty()) {
        printf("\n== Acumulacao: reduction x atomic x critical (N=%ld, T_seq=%.4f s) ==\n%10s %6s %10s %9s\n",
               Nc, Tseq.count(Nc) ? Tseq[Nc] : 0.0, "modo", rot.c_str(), "tempo_s", "speedup");
        ofstream o("resultados/" + v + "_corrida.csv"); o << "modo,p,tempo_s,speedup\n";
        vector<Serie> S; int i = 0;
        for (string m : {"reduction", "atomic", "critical"}) {
            if (!cor.count(m)) continue;
            auto& L = cor[m]; sort(L.begin(), L.end());
            Serie s{m, CORES[i++]};
            for (auto& [p, t] : L) {
                double sp = Tseq.count(Nc) ? Tseq[Nc] / t : 0;
                printf("%10s %6d %10.4f %9.3f\n", m.c_str(), p, t, sp);
                o << m << "," << p << "," << t << "," << sp << "\n";
                s.pts.push_back({p, t});
            }
            S.push_back(s);
        }
        grafico_linhas("resultados/" + v + "_corrida.svg", "Acumulacao: reduction x atomic x critical (N=" + num(Nc / 1e6) + "M)",
                       "Numero de " + rot, "Tempo (s, mediana)", S);
    }

    // ---------------- politicas de escalonamento
    vector<pair<string, double>> sch; long Ns = 0; int ps = 0;
    for (auto& [k, t] : T) if (k.exp == "schedule") { sch.push_back({k.sch, mediana(t)}); Ns = k.N; ps = k.p; }
    if (!sch.empty()) {
        vector<string> ordem = {"static", "static:1", "dynamic:1", "dynamic:1000", "guided"};
        sort(sch.begin(), sch.end(), [&](auto& a, auto& b) {
            return find(ordem.begin(), ordem.end(), a.first) < find(ordem.begin(), ordem.end(), b.first); });
        printf("\n== Escalonamento da atribuicao (N=%ld, %d %s) ==\n%14s %10s\n", Ns, ps, rot.c_str(), "schedule", "tempo_s");
        ofstream o("resultados/" + v + "_schedule.csv"); o << "schedule,tempo_s\n";
        for (auto& [n, t] : sch) { printf("%14s %10.4f\n", n.c_str(), t); o << n << "," << t << "\n"; }
        auto rot_sch = sch; for (auto& r : rot_sch) replace(r.first.begin(), r.first.end(), ':', ',');
        grafico_barras("resultados/" + v + "_schedule.svg", "Politicas de escalonamento (N=" + num(Ns / 1e6) + "M, " +
                       to_string(ps) + " " + rot + ")", "Tempo (s, mediana)", rot_sch);
    }

    // ---------------- escalabilidade fraca
    vector<tuple<int, long, double>> fr;
    for (auto& [k, t] : T) if (k.exp == "fraca") fr.push_back({k.p, k.N, mediana(t)});
    if (!fr.empty()) {
        sort(fr.begin(), fr.end());
        double t1 = get<2>(fr[0]);
        printf("\n== Escalabilidade fraca (N cresce com %s) ==\n%6s %10s %10s %11s\n", rot.c_str(), rot.c_str(), "N", "tempo_s", "eficiencia");
        ofstream o("resultados/" + v + "_fraca.csv"); o << "p,N,tempo_s,eficiencia_fraca\n";
        Serie e{"medida", CORES[0]}, e1{"ideal (100%)", "#999999", {}, true};
        for (auto& [p, N, t] : fr) {
            printf("%6d %10ld %10.4f %10.1f%%\n", p, N, t, 100 * t1 / t);
            o << p << "," << N << "," << t << "," << t1 / t << "\n";
            e.pts.push_back({p, 100 * t1 / t}); e1.pts.push_back({p, 100});
        }
        grafico_linhas("resultados/" + v + "_fraca.svg", "Escalabilidade fraca " + v + " (N proporcional a p)",
                       "Numero de " + rot, "Eficiencia fraca (%)", {e, e1}, 100);
    }
    // ---------------- overhead de comunicacao (so MPI: resultados/mpi_comm.csv)
    {
        ifstream f("resultados/" + v + "_comm.csv"); string l;
        if (f) {
            map<int, pair<vector<double>, vector<double>>> C;   // p -> (tempos, comm)
            getline(f, l);
            while (getline(f, l)) {
                auto c = separa(l); if (c.size() < 6 || c[0] != "forte") continue;
                C[stoi(c[2])].first.push_back(stod(c[4])); C[stoi(c[2])].second.push_back(stod(c[5]));
            }
            printf("\n== Overhead de comunicacao (experimento forte, mediana) ==\n%6s %10s %10s %10s %9s\n",
                   rot.c_str(), "tempo_s", "comm_s", "calculo_s", "comm/%");
            ofstream o("resultados/" + v + "_comm_tabela.csv"); o << "p,tempo_s,comm_s,calculo_s,pct_comm\n";
            Serie st{"tempo total", CORES[0]}, sc{"calculo (total - comm)", CORES[2]}, sm{"comm (Allreduce + espera)", CORES[1]};
            for (auto& [p, tc] : C) {
                double t = mediana(tc.first), c = mediana(tc.second);
                printf("%6d %10.4f %10.4f %10.4f %8.1f%%\n", p, t, c, t - c, 100 * c / t);
                o << p << "," << t << "," << c << "," << t - c << "," << 100 * c / t << "\n";
                st.pts.push_back({p, t}); sc.pts.push_back({p, t - c}); sm.pts.push_back({p, c});
            }
            if (!C.empty())
                grafico_linhas("resultados/" + v + "_comm.svg", "Calculo x comunicacao " + v + " (N=20M)",
                               "Numero de " + rot, "Tempo (s, mediana)", {st, sc, sm});
        }
    }
    printf("\nTabelas e graficos gravados em resultados/%s_*\n", v.c_str());
    return 0;
}
