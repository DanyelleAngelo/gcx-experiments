import glob
import pandas as pd

padrao = 'report/2025-06-01/*encoding.csv'
arquivos = glob.glob(padrao)

for caminho_arquivo in arquivos:
    df = pd.read_csv(caminho_arquivo, sep='|')
    df['plain_size'] = df['plain_size'] / (1024 * 1024)
    df['compressed_size'] = df['compressed_size'] / (1024 * 1024)
    df.to_csv(caminho_arquivo, sep='|', index=False, float_format='%.6f')

print('Conversão concluída e formatada.')
