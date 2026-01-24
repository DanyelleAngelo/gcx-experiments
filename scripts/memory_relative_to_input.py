"""
Script para calcular consumo de memória relativo à entrada de cada algoritmo.
"""


import os
import pandas as pd
import glob

path_dir = "report/2025-08-12"
input_files = glob.glob(f"{path_dir}/*encoding.csv")
all_dataframes = []

exclude_files = {
    f"{path_dir}/pseudo-real-dblp.xml.0001.1-gcx-encoding.csv",
    f"{path_dir}/pseudo-real-dblp.xml.0001.2-gcx-encoding.csv",
    f"{path_dir}/pseudo-real-dblp.xml.00001.2-gcx-encoding.csv",
}

input_files = [
    f for f in input_files
    if os.path.basename(f) not in exclude_files
]
def is_desired_algorithm(alg):
    alg = alg.strip()
    return (
        #alg.startswith("REPAIR") or
        alg.startswith("PlainSlp_32Fblc") or
        alg.startswith("GCIS") or
        alg.startswith("GC8") or
        alg.startswith("GCX-y8")
        #alg.startswith("CBT")  
    )

for filename in input_files:
    df = pd.read_csv(
        filename, sep='|',
        usecols=[
            'file', 'algorithm', 'peak_comp_mib', 'peak_decomp_mib',
            'plain_size_mib', 'compressed_size_mib'
        ]
    )

    filtered_df = df[df['algorithm'].apply(lambda alg: is_desired_algorithm(alg))].copy()

    filtered_df['comp_per_input'] = filtered_df.apply(
        lambda row: row['peak_comp_mib'] / row['plain_size_mib'] if row['plain_size_mib'] > 0 else 0,
        axis=1
    )
    filtered_df['decomp_per_input'] = filtered_df.apply(
        lambda row: row['peak_decomp_mib'] / (row['compressed_size_mib'] + row['plain_size_mib']) 
        if (row['compressed_size_mib'] + row['plain_size_mib']) > 0 else 0,
        axis=1
    )

    all_dataframes.append(filtered_df)

final_df = pd.concat(all_dataframes, ignore_index=True)

output_file = os.path.join(path_dir, "memory_relative_to_input.csv")
final_df.to_csv(output_file, sep='|', index=False)

print(f"Arquivo salvo em: {output_file}")
