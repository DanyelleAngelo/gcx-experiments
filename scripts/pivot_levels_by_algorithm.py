import pandas as pd

"""
 Lê CSV, limpa colunas, pivota 'algorithm' como colunas com valores 'nLevels', e salva resultado.
"""

df = pd.read_csv("report/2025-05-26/00_grammar_all_files.csv", sep="|")
df.columns = df.columns.str.strip()

print("Colunas após limpeza:", df.columns.tolist())

df = df[['file', 'algorithm', 'nLevels']]

tabela_final = df.pivot(index='file', columns='algorithm', values='nLevels')
tabela_final.reset_index(inplace=True)

tabela_final = tabela_final[['file'] + sorted([col for col in tabela_final.columns if col != 'file'])]
tabela_final.to_csv("report/2025-05-26/00_levels_per_algorithm.csv", index=False)

