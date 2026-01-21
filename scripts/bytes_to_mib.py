import glob
import pandas as pd
import graphs.utils as ut

'''
    Converte bytes to MiB
'''

padrao = 'report/2025-08-12/*encoding.csv'
arquivos = glob.glob(padrao)
BYTES_IN_MIB = 1024 * 1024
MB_TO_MIB = 1_000_000 / BYTES_IN_MIB

for caminho_arquivo in arquivos:
    df = pd.read_csv(caminho_arquivo, sep='|')

    def peak_comp_to_mib(row):
        if row['algorithm'].lower() == 'cbt':
            # peak_comp está em MB
            return row['peak_comp'] * MB_TO_MIB
        else:
            # peak_comp está em bytes
            return row['peak_comp'] / BYTES_IN_MIB

    def peak_decomp_to_mib(row):
        if row['algorithm'].lower() == 'cbt':
            return row['peak_decomp'] * MB_TO_MIB
        else:
            return row['peak_decomp'] / BYTES_IN_MIB

    df['peak_comp_mib'] = df.apply(peak_comp_to_mib, axis=1)
    df['peak_decomp_mib'] = df.apply(peak_decomp_to_mib, axis=1)

    df.to_csv(
        caminho_arquivo,
        sep='|',
        index=False,
        float_format='%.6f'
    )

print('Conversão concluída — colunas derivadas adicionadas.')