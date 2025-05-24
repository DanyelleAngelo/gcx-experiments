import os
import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt
import statsmodels.api as sm
from scipy.stats import pearsonr
from sklearn.linear_model import LinearRegression


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

def create_graphs():
    """"
        Gera gráficos de dispersão para cada arquivo compactado, variáveis analisadas: taxa de compressão vs quantidade de níveis na gramática.
    """
    print("\n####### Criando tabela de dispersão\n")
    files= data["file"].unique()
    for file in files:
        subset= data[data["file"] == file]
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

        g.set_axis_labels("Número de níveis", "Taxa de compressão (%)")
        g.fig.suptitle(f"Relação entre nº de níveis e taxa de compressão\n{file}")

        g.fig.tight_layout()
        g.fig.subplots_adjust(top=0.85)

        file=f"{path_dir}/graphs/{file}_compressao_vs_nlevels.png"
        g.savefig(file)
        plt.close()

def interpret_coef(coef):
    if coef is None:
        return "Not enough data."
    elif coef > 0.01:
        return "Levels ↑ → Compression rate ↑"
    elif coef < -0.01:
        return "Levels ↑ → Compression rate ↓"
    else:
        return "No clear relation."

def create_table():
    """"
        - Cálcula  correlação de Pearson (grau de relação linear entre duas variáveis)
            - Perfeita (+1): quando x aumenta, y aumenta, 
            - Nenhuma (0): nenhuma relação, 
            - Negativa perfeita (-1): x aumenta, y diminuí
        - Cálcula o coeficiente angular entre y e x usando regressão linear simples.
    """
    
    resultados = []

    print("\n####### Calculando correlação de Pearson e coeficiente angular....\n")

    for (file, group), subset in data.groupby(["file", "group"]):
        if len(subset) < 2:
            print(f"Agrupamento inválido para arquivo {file}, grupo {group}")
            continue
        
        if subset["nLevels"].nunique() <= 1 or subset["compressed_size_ratio"].nunique() <= 1:
            print(f"Dados constantes para arquivo {file}, grupo {group}, pulando correlação")
            corr = float('nan')
            coef_angular = float('nan')
        else:
            x = subset["nLevels"].values.reshape(-1, 1)
            y = subset["compressed_size_ratio"].values

            corr, _ = pearsonr(subset["nLevels"], subset["compressed_size_ratio"])

            model = LinearRegression().fit(x, y)
            coef_angular = model.coef_[0]

            resultados.append({
                "file": file,
                "algorithm_group": group,
                "mean_nLevels": subset["nLevels"].mean(),
                "mean_compression_rate": subset["compressed_size_ratio"].mean(),
                "correlation": round(corr, 3),
                "regression_slope": round(coef_angular, 3),
                "interpretation": interpret_coef(coef_angular),
            })


    table = pd.DataFrame(resultados)
    table.to_csv(f"{path_dir}/compression_vs_levels_summary.csv")
    print(table)


create_graphs()
create_table()