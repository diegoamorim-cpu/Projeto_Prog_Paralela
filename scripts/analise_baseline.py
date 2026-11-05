"""Le resultados/baseline.csv e resultados/perfil.csv e gera:
  - resultados/tabela_baseline.csv  (mediana por tamanho)
  - resultados/tabela_perfil.csv    (tempo e % por fase, fracao paralelizavel, Amdahl)
  - resultados/perfil_fases.png     (grafico de barras empilhadas)
Uso: python3 scripts/analise_baseline.py   (a partir da raiz do repositorio)
"""
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

base = pd.read_csv("resultados/baseline.csv")
perf = pd.read_csv("resultados/perfil.csv")

# ---- baseline: mediana por tamanho ----
tb = base.groupby("N").agg(checksum=("checksum", "first"),
                           mediana_s=("tempo_s", "median"),
                           min_s=("tempo_s", "min"),
                           max_s=("tempo_s", "max")).reset_index()
tb.to_csv("resultados/tabela_baseline.csv", index=False)
print("== Baseline (mediana) ==")
print(tb.to_string(index=False))

# ---- perfil: calcula por execucao e depois tira a mediana ----
fases = ["t_atrib", "t_acum", "t_div"]
perf["paralelo"] = perf["t_atrib"] + perf["t_acum"]
# serial do laco = divisao + sobra (alocacao dos vetores, controle do laco);
# a sobra e tao pequena que pode dar negativa por ruido -> corta em zero
perf["serial_laco"] = (perf["t_total"] - perf["paralelo"]).clip(lower=0)
# visao A: so a regiao cronometrada pelo professor (laco das iteracoes)
perf["fA"] = perf["paralelo"] / (perf["paralelo"] + perf["serial_laco"])
# visao B: programa inteiro; a geracao dos pontos e serial (o gerador LCG
# depende do valor anterior de s, entao nao da para paralelizar direto)
perf["fB"] = perf["paralelo"] / (perf["paralelo"] + perf["serial_laco"] + perf["t_geracao"])

tp = perf.groupby("N")[["t_geracao"] + fases + ["serial_laco", "t_total", "fA", "fB"]].median().reset_index()
for c in ["t_atrib", "t_acum", "serial_laco"]:
    tp["pct_" + c] = 100 * tp[c] / tp["t_total"]
for v in ["A", "B"]:
    f = tp["f" + v]
    for p in [2, 4, 8, 16]:
        tp[f"S{v}_{p}"] = 1 / ((1 - f) + f / p)
    tp[f"S{v}_max"] = 1 / (1 - f).clip(lower=1e-9)
tp.to_csv("resultados/tabela_perfil.csv", index=False)

pd.set_option("display.width", 200)
print("\n== Perfil por fase (mediana, % do laco principal) ==")
print(tp[["N", "t_total", "t_atrib", "t_acum", "serial_laco", "t_geracao",
          "pct_t_atrib", "pct_t_acum", "pct_serial_laco"]].round(4).to_string(index=False))
print("\n== Amdahl - visao A (so o laco cronometrado) ==")
print(tp[["N", "fA", "SA_2", "SA_4", "SA_8", "SA_16"]].round(4).to_string(index=False))
print("\n== Amdahl - visao B (programa inteiro, geracao serial) ==")
print(tp[["N", "fB", "SB_2", "SB_4", "SB_8", "SB_16", "SB_max"]].round(3).to_string(index=False))

# ---- grafico ----
fig, ax = plt.subplots(figsize=(7, 4))
rot = [f"{n/1e6:g} M" for n in tp["N"]]
ax.bar(rot, tp["t_atrib"], label="Fase 1: atribuição", color="#2a6fdb")
ax.bar(rot, tp["t_acum"], bottom=tp["t_atrib"], label="Fase 2a: acumulação", color="#f08c2e")
ax.bar(rot, tp["t_geracao"], bottom=tp["t_atrib"] + tp["t_acum"],
       label="Geração dos pontos (fora do cronômetro, serial)", color="#bbbbbb")
ax.set_xlabel("Número de pontos (N), K=8, 20 iterações")
ax.set_ylabel("Tempo (s, mediana)")
ax.set_title("Perfil do baseline sequencial por fase")
ax.legend(frameon=False)
ax.spines[["top", "right"]].set_visible(False)
fig.tight_layout()
fig.savefig("resultados/perfil_fases.png", dpi=150)
print("\nGrafico: resultados/perfil_fases.png")
