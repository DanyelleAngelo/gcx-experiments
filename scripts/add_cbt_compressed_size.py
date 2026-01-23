"""
    Para o algoritmo CBT, temos a taxa de compressão (coluna = Compression Ratio (%)), precisamos obter o tamanho do arquivo compactado (compressed_size), utilize esse script para isso.
"""
import pandas as pd
import glob
import re

df_ref = pd.read_csv("report/2025-08-12/cbt/results_bt.csv") # CBT reference
ratio_map = df_ref.set_index('Filename')['Compression Ratio (%)'].to_dict()

pattern = 'report/2025-08-12/*encoding.csv'
files = glob.glob(pattern)

prefix_regex = r'^(pseudo-real-|real-|artificial-)'
for file_path in files:
    df = pd.read_csv(file_path, sep='|')
    
    df['ref_key'] = df['file'].str.replace(prefix_regex, '', regex=True)
    

    def calcular_cbt_bytes(row):
        if row['algorithm'] == 'CBT':
            ratio = ratio_map.get(row['ref_key'])
            if ratio is not None:
                return row['plain_size'] * (ratio / 100)
        return row['compressed_size']

    df['compressed_size'] = df.apply(calcular_cbt_bytes, axis=1)

    df.drop(columns=['ref_key'], inplace=True)
    df.to_csv(file_path, sep='|', index=False)
    
    print(f"Arquivo processado: {file_path}")