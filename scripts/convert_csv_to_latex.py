"""
    Utilize esse algoritmo para converter um arquivo CSV em uma tabela latex (no formato do nosso artigo).
"""

import pandas as pd
import os

meus_algoritmos = [
    'PlainSlp_32Fblc',
    'GCIS-ef',
    'CBT',
    'GC8',
    'GCX-y8', 
]
path_dir="report/2026-01-08"
output_dir = f"{path_dir}/latex_tables"

def generate_latex_table(csv_path, algorithms_to_include, data_column, caption, label, output_filename):
    df = pd.read_csv(csv_path, sep="|")
    df.columns = df.columns.str.strip()

    # remove o tipo de dataset presente no arquivo    
    df['file'] = df['file'].str.split('-', n=1).str[-1]
    
    df = df[df['algorithm'].isin(algorithms_to_include)]
 
    # arquivos e tamanho como linhas, algoritmos como colunas
    pivot_df = df.pivot_table(
        index=['file', 'plain_size_mib'], 
        columns='algorithm', 
        values=data_column
    ).reset_index()

    pivot_df = pivot_df.sort_values(by='plain_size_mib').reset_index(drop=True)
    columns_order = ['file', 'plain_size_mib'] + [a for a in algorithms_to_include if a in pivot_df.columns]
    pivot_df = pivot_df.reindex(columns=columns_order)

    # --- início da geração do latex ---
    latex_lines = [
        r"\begin{table*}[t]",
        f"\\caption{{{caption} \\add{{Values in \\textbf{{bold}} indicate the best result.}}}}",
        f"\\label{{{label}}}",
        r"\centering",
        r"\setlength{\tabcolsep}{5pt}",
        f"\\begin{{tabular}}{{|l|c|{'c|' * len(algorithms_to_include)}}}",
        r"\hline",
        "Experiment & Size (MiB) & " + " & ".join(algorithms_to_include) + r" \\",
        r"\hline"
    ]

    for _, row in pivot_df.iterrows():
        file_name = f"\\texttt{{{row['file']}}}"
        size = f"{row['plain_size_mib']:.2f}"
        
        # encontrar o melhor resultado possível
        algo_values = row[algorithms_to_include]
        min_val = algo_values.min()
        
        formatted_values = []
        for val in algo_values:
            if pd.isna(val):
                formatted_values.append("-")
                continue
            val_str = f"{round(val, 2):,.2f}"
            
            if val == min_val:
                formatted_values.append(f"\\textbf{{{val_str}}}")
            else:
                formatted_values.append(val_str)
        
        line = f"{file_name} & {size} & " + " & ".join(formatted_values) + r" \\"
        latex_lines.append(line)

    latex_lines.extend([r"\hline", r"\end{tabular}", r"\end{table*}"])

    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    output_path = os.path.join(output_dir, output_filename)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(latex_lines))
    
    print(f"Sucesso! Tabela gerada em: {output_path}")

def peak_compression(input_file):
    generate_latex_table(
        csv_path=input_file,
        algorithms_to_include=meus_algoritmos,
        data_column='peak_comp_mib',
        caption="Peak memory usage during compression  (in MiB).",
        label="t:memory-compression",
        output_filename="memory_compression.tex"
    )

def peak_decompression(input_file):
    generate_latex_table(
        csv_path=input_file,
        algorithms_to_include=meus_algoritmos,
        data_column='peak_decomp_mib',
        caption="Peak memory usage during decompression (in MiB).",
        label="t:memory-decompression",
        output_filename="memory_decompression.tex"
    )

peak_compression(f"{path_dir}/memory_relative_to_input.csv")
peak_decompression(f"{path_dir}/memory_relative_to_input.csv")