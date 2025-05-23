import pandas as pd
import glob
import os
import graphs.utils as ut

output_dir="report/2025-05-23"
os.makedirs(output_dir, exist_ok=True)

path_files = "report/2025-05-19*"
pattern = os.path.join(path_files, "*grammar.csv")
files = glob.glob(pattern)

dataframes = []
selected_cols = ['file', 'algorithm', 'nLevels', 'compressed_size', 'plain_size']

for file in files:
    df = pd.read_csv(file, sep="|", usecols=selected_cols)
    df['compressed_size'] = df['compressed_size'].apply(lambda x: round(ut.bytes_to_mb(x),2))
    df['plain_size'] = df['plain_size'].apply(lambda x: round(ut.bytes_to_mb(x),2))
    
    plain_size = df['plain_size'].iloc[0]
    df['compressed_size_ratio'] = df['compressed_size'].apply(lambda x: ut.compute_ratio_percentage(x, plain_size))
    dataframes.append(df)


df_merged = pd.concat(dataframes, ignore_index=True)

unselected_algorithms=['GCX-y2', 'GC2', 'GC128', 'GC64']
filtered_df = df_merged[~df_merged['algorithm'].isin(unselected_algorithms)]
filtered_df.to_csv(f"{output_dir}/grammar.csv", index=False, sep="|")

print("Merge completo! Arquivo salvo como 'grammar.csv'")
