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

pattern = 'report/2025-08-12/*encoding.csv'
files = glob.glob(pattern)


for file_path in files:
    df = pd.read_csv(file_path, sep='|')

    plain_size = df['plain_size'].iloc[0]

    df['compression_ratio'] = df['compressed_size'].apply(
        lambda x: ut.compute_ratio_percentage(x, plain_size)
    )
    df['peak_comp_mib'] = df['peak_comp'] / BYTES_IN_MIB
    df['peak_decomp_mib'] = df['peak_decomp'] / BYTES_IN_MIB
    df['compressed_size_mib'] = df['compressed_size'] / BYTES_IN_MIB
    df['plain_size_mib'] = df['plain_size'] / BYTES_IN_MIB

    df.to_csv(file_path, sep='|', index=False, float_format='%.6f')

print('Conversão concluída e formatada.')
