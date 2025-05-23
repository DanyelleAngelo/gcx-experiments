import os
import sys
import glob

import logging
import pandas as pd

from typing import List
import plotting as plt
import utils as ut
import generate_tables as gt
from summary import *


pd.options.mode.chained_assignment = None 

compress_max_values = {
    'peak_comp': 0.0,
    'peak_decomp': 0.0,
    'compression_time': 0.0,
    'decompression_time': 0.0,
    'compressed_size': 0.0
}

extract_values = {
    'time': 0.0,
    "min_time": 0.0
}

sep_decimal= "."

def generate_grammar_chart(df_list, output_dir, language):
    for df in df_list:
        print(f"\n## FILE: {df.index[0]}")
        df['compressed_size'] = df['compressed_size'].apply(lambda x: round(ut.bytes_to_mb(x),2))
        df['plain_size'] = df['plain_size'].apply(lambda x: round(ut.bytes_to_mb(x),2))
    
        plt.generate_grammar_chart(df, language.GRAMMAR, output_dir)

def generate_extract_chart(df_list, output_dir, language):
    for df in df_list:
        print(f"\n## FILE: {df.index[0]}")
        print(f"\t- Creating charts extract comparison between GCX and GCIS")
        plt.generate_chart_line(df, language.EXTRACT["time"], output_dir, extract_values["time"], extract_values["min_time"])

def generate_compress_chart(df_list, output_dir, language):
    for df in df_list:
        pattern = r'^(GCX-y\d+|GC\d+)$'
        dcx = df[df['algorithm'].str.match(pattern, na=False)]
        others =  df[~df['algorithm'].str.match(pattern, na=False)]
       
        print(f"\n## FILE: {df.index[0]}")
        plt.generate_chart_bar(dcx, others, language.COMPRESS_AND_DECOMPRESS['cmp_time'], output_dir)
        plt.generate_chart_bar(dcx, others, language.COMPRESS_AND_DECOMPRESS['dcmp_time'], output_dir)

        plt.generate_chart_bar(dcx, others, language.COMPRESS_AND_DECOMPRESS['ratio'], output_dir, 100)

        #plt.generate_chart_bar(dcx, others, language.COMPRESS_AND_DECOMPRESS['peak_comp'], output_dir)
        #plt.generate_chart_bar(dcx, others, language.COMPRESS_AND_DECOMPRESS['peak_decomp'], output_dir)

def set_max_values(values, df):
    for key in values.keys():
        if key in df:
            column_max = df[key].max()
            if column_max > values[key]:
                values[key] = column_max


def remove_and_log_incomplete_rows(df):
    incomplete_rows = df[df.isnull().any(axis=1)]
    if not incomplete_rows.empty:
        for idx, row in incomplete_rows.iterrows():
            row_dict = row.to_dict()
            logging.warning(f"[WARNING] Removendo linha incompleta: {row.file, row.algorithm}")
    return df.dropna()

def prepare_dataset(df: pd.DataFrame, operation: str):
    """
        Remove linhas incompletas, remove prefixo do nome do arquivo que indica a natureza do dado; e normaliza dados:
        - Converte bytes para MB
        - Calcula taxa de compressao
        - Converte segundos para microsegundos
    """
    df = remove_and_log_incomplete_rows(df)

    df['algorithm'] = df['algorithm'].replace({
        'REPAIR-PlainSlp_32Fblc': 'RePair (32Fblc)',
        'REPAIR-PlainSlp_FblcFblc': 'RePair (FblcFblc)'
    })
    
    df.loc[:, 'file'] = df['file'].str.replace('pseudo-real-', '', regex=False) \
        .str.replace('real-', '', regex=False) \
        .str.strip()

    if operation == 'compress':
        plain_size = df['plain_size'].iloc[0]
        df['compressed_size_ratio'] = df['compressed_size'].apply(lambda x: ut.compute_ratio_percentage(x, plain_size))
        ut.convert_columns(df, ['plain_size', 'peak_comp', 'peak_decomp'], ut.bytes_to_mb)

    elif operation == 'extract':
        df['time'] = df['time'].apply(lambda x: (x/10000)*1e6) 

    return df

def get_data_frame(path: str, operation: str, report: bool) -> List[pd.DataFrame]:
    files = glob.glob(f"{path}*.csv")
    df_list = []

    for file in files:
        df = pd.read_csv(file, sep='|', decimal=sep_decimal, on_bad_lines='skip')
        if operation == "compress":
            df = prepare_dataset(df, operation)
            set_max_values(compress_max_values, df)
            set_summary(compression_summary, df)
        elif operation == "extract":
            df = prepare_dataset(df, operation)
            set_max_values(extract_values, df)
            set_summary(extract_summary, df)
        df.set_index('file', inplace=True)

        df_list.append(df)

    return df_list

def generate_charts(operation, df_list, graph_path_dir, language):
    if not os.path.isdir(graph_path_dir):
        logging.warning(f'A pasta "{graph_path_dir}" não existe.')
        return

    if operation == "compress":
        print("\n\t------ Compress and Decompress ------")
        generate_compress_chart(df_list, graph_path_dir, language)
    elif operation == "extract":
        print("\n\t------ Extract ------")
        generate_extract_chart(df_list, graph_path_dir, language)
    elif operation == "grammar":
        print("\n\t------ Informações da gramática ------")
        generate_grammar_chart(df_list, graph_path_dir, language)

def generate_tables(df_list, language, table_path_dir):
    print("\n\nGerando latex e markdown table...")

    os.makedirs(table_path_dir, exist_ok=True)
    gt.generate_tables(
        df_list,
        metric='compressed_size_ratio',
        output_suffix='compression_ratio',
        metric_labels=language.metric_labels,
        path_dir=table_path_dir)

    gt.generate_tables(
        df_list,
        metric='compression_time',
        output_suffix='compression_time',
        metric_labels=language.metric_labels,
        path_dir=table_path_dir)

    gt.generate_tables(
        df_list,
        metric='decompression_time',
        output_suffix='decompression_time',
        metric_labels=language.metric_labels,
        path_dir=table_path_dir)

def main(argv):
    if len(argv) < 5:
        print("Uso: script.py <input_dir> <output_dir> <operation> <locale> [report]")
        sys.exit(1)

    path = argv[1]
    operation = argv[3]
    locale = argv[4]
    report =  argv[5] if len(sys.argv) > 5 else False
    graph_path_dir = f"{argv[2]}/graphs/{locale}"

    language = ut.set_locale(locale)
    df_list = get_data_frame(path, operation, report)

    generate_charts(operation, df_list, graph_path_dir, language)
    
    if report:
        if operation == "compress":
            #print_summary(df_list, compression_summary)
            table_path_dir = f"{argv[2]}/tables"
            generate_tables(df_list, language, table_path_dir)
        elif operation == "extract":
            print_summary(df_list, extract_summary)


if __name__ == '__main__':
    sys.exit(main(sys.argv))