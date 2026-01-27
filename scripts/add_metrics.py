""""
    Utilize esse algoritmos para adicionar ao seu dataset, métricas como:
        - compression ratio
        - peak memory (bytes -> MiB): espera-se que a métrica peak_comp e peak_decomp esteja em bytes
        - plain size (MiB)

"""

import glob
import pandas as pd
import graphs.utils as ut

BYTES_IN_MIB = 2 ** 20
BYTES_IN_GIB = 1024 ** 3

pattern = 'report/2025-08-12/*encoding.csv'
files = glob.glob(pattern)

def convert_to_mib(row, column_name):
    return row[column_name] / BYTES_IN_MIB

def convert_to_gib(row, column_name):
    return row[column_name] / BYTES_IN_GIB

for file_path in files:
    df = pd.read_csv(file_path, sep='|')

    plain_size = df['plain_size'].iloc[0]

    for col in ['peak_comp', 'peak_decomp', 'compressed_size']:
        df[f'{col}_gib'] = df.apply(lambda row: convert_to_gib(row, col), axis=1)
    for col in ['peak_comp', 'peak_decomp', 'compressed_size']:
        df[f'{col}_mib'] = df.apply(lambda row: convert_to_mib(row, col), axis=1)

    df['compression_ratio'] = df['compressed_size'].apply(
        lambda x: ut.compute_ratio_percentage(x, plain_size)
    )
    df['plain_size_mib'] = df['plain_size'] / BYTES_IN_MIB

    df.to_csv(file_path, sep='|', index=False, float_format='%.6f')

print('Conversão concluída e formatada.')
