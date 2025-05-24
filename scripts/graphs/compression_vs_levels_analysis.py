import os
import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt
import statsmodels.api as sm

""""
    Esse script gera um gráfico de dispersão, variáveis analisadas: taxa de compressão vs quantidade de níveis na gramática.
"""

path_dir="report/2025-05-23"
os.makedirs(f"{path_dir}/graphs", exist_ok=True)

data = pd.read_csv(f"{path_dir}/grammar.csv", sep="|")

def group_algorithm(algo):
    if algo.startswith("GCX"):
        return "gcx"
    elif algo.startswith("GC"):
        return "g*"
    return algo

data["group"] = data["algorithm"].apply(group_algorithm)

files= data["file"].unique()
for file in files:
    subset= data[data["file"] == file]
    print(subset)
    g = sns.lmplot(
        data=subset,
        x="nLevels",
        y="compressed_size_ratio",
        hue="algorithm", #troque para group para ver uma análise baseada no tipo do algoritmo
        ci=None, #remove intervalo de confiança
        #markers=["o", "s"],
        palette="muted",
        height=5,
        aspect=1,
    )
    break

    g.set_axis_labels("Número de níveis", "Taxa de compressão (%)")
    g.fig.suptitle(f"Relação entre nº de níveis e taxa de compressão\n{file}")

    g.fig.tight_layout()
    g.fig.subplots_adjust(top=0.85)

    file=f"{path_dir}/graphs/{file}_compressao_vs_nlevels.png"
    g.savefig(file)
    plt.close()