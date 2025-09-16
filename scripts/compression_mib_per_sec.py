""""
 Utilize esse script para obter a taxa de compressão por segundo de cada algoritmo
"""
import os
import pandas as pd
import glob
import csv

path_dir = "report/2025-08-12"
input_files = glob.glob(f"{path_dir}/*encoding.csv")
all_dataframes = []

def is_desired_algorithm(alg):
    alg = alg.strip()
    return (
        alg.startswith("REPAIR") or
        alg.startswith("GCIS") or
        alg.startswith("7zip") or
        alg.startswith("bzip2") or
        alg.startswith("GC8") or
        alg.startswith("GCX-y8")
    )

for filename in input_files:
    df = pd.read_csv(filename, sep='|', usecols=['file', 'algorithm', 'compression_time', 'decompression_time', 'plain_size'])
    filtered_df = df[df['algorithm'].apply(lambda alg: is_desired_algorithm(alg))].copy()

    filtered_df['plain_size_MiB'] = filtered_df['plain_size'] / (1024**2)
    filtered_df['MiB_per_sec - comp'] = filtered_df.apply(
        lambda row: row['plain_size_MiB'] / row['compression_time'] if row['compression_time'] > 0 else 0,
        axis=1
    )
    filtered_df['MiB_per_sec - decomp'] = filtered_df.apply(
        lambda row: row['plain_size_MiB'] / row['decompression_time'] if row['decompression_time'] > 0 else 0,
        axis=1
    )
    all_dataframes.append(filtered_df)

final_df = pd.concat(all_dataframes, ignore_index=True)
final_df.to_csv(f"{path_dir}/compression_mib_per_sec.csv", sep='|', index=False)