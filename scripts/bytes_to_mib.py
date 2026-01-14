import glob
import pandas as pd
import graphs.utils as ut

'''
    Converte bytes to MiB
'''

padrao = 'report/2026-01-08/*encoding.csv'
arquivos = glob.glob(padrao)
BYTES_IN_MIB = 1024 * 1024

for caminho_arquivo in arquivos:
    df = pd.read_csv(caminho_arquivo, sep='|')

    if 'plain_size_mib' not in df.columns:
        df['plain_size_mib'] = df['plain_size'] / BYTES_IN_MIB

    if 'compressed_size_mib' not in df.columns:
        df['compressed_size_mib'] = df['compressed_size'] / BYTES_IN_MIB

    plain_size_mib = df['plain_size_mib'].iloc[0]

    df['compressed_size_ratio'] = df['compressed_size_mib'].apply(
        lambda x: ut.compute_ratio_percentage(x, plain_size_mib)
    )

    df.to_csv(
        caminho_arquivo,
        sep='|',
        index=False,
        float_format='%.6f'
    )
print('Conversão concluída — colunas derivadas adicionadas.')