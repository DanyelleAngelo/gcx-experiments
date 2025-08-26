import pandas as pd
from pathlib import Path
import re

'''
Utilize esse script para fazer merge de 2 datasets do mesmo tipo.
'''

MAIN_PATH = Path("report/2025-08-12")
OTHER_PATH = Path("report/2025-08-25")

def is_desired_algorithm(alg, existing_algorithms):
    alg = alg.strip()
    if alg in existing_algorithms:
        return False
    return (
        alg.startswith("REPAIR") or
        alg.startswith("GCIS") or
        alg.startswith("7zip") or
        alg.startswith("bzip2") or
        alg.startswith("GC2") or
        alg.startswith("GC4") or
        alg.startswith("GC8") or
        alg.startswith("GC16") or
        alg.startswith("GC32") or
        alg.startswith("GC64") or
        alg.startswith("GC128") or
        alg.startswith("GCX-y8") #or
        #(re.fullmatch(r'GC\d+', alg) is not None)
    )

pattern="*encoding.csv"
csv_files = list(MAIN_PATH.glob(pattern))

if not csv_files:
    print(f"⚠️  Não há arquivos {pattern} em {MAIN_PATH}")
    exit(1)

for file in csv_files:
    filename = file.name
    other_file = OTHER_PATH / filename

    if not other_file.exists():
        print(f"⚠️  {filename} não encontrado em {OTHER_PATH}, pulando.")
        continue

    print(f"🔄 Mesclando {filename}")

    df_main = pd.read_csv(file, sep="|")
    df_other = pd.read_csv(other_file, sep="|")

    existing_algorithms = set(df_main['algorithm'])

    df_filtered = df_other[df_other['algorithm'].apply(
        lambda alg: is_desired_algorithm(alg, existing_algorithms)
    )]

    if df_filtered.empty:
        print(f"ℹ️  Nenhum algoritmo novo para adicionar em {filename}")
        continue

    df_merged = pd.concat([df_filtered, df_main], ignore_index=True)

    df_merged.to_csv(file, sep="|", index=False)
    print(f"✅ {filename} atualizado com {len(df_filtered)} novos algoritmos.")
