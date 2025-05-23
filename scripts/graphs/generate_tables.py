import pandas as pd
import constants as c
from typing import List

def hline(): return "\\hline\n"
def latex_row(cells): return " & ".join(cells) + " \\\\\n"

def format_size(size):
    return f"{size:,.2f}"

def format_metric(value):
    return "-" if pd.isna(value) else f"{value:.2f}"

def latex_escape(s) -> str:
    """Escapa caracteres especiais para LaTeX."""
    if isinstance(s, str):
        return (s.replace('\\', '\\textbackslash{}')
                 .replace('_', '\\_')
                 .replace('%', '\\%')
                 .replace('&', '\\&')
                 .replace('#', '\\#')
                 .replace('{', '\\{')
                 .replace('}', '\\}')
                 .replace('$', '\\$')
        )
    return s

def save_custom_latex_table(final_table: pd.DataFrame, filename: str, caption: str, label: str):
    n_columns = len(final_table.columns)
    n_algorithms = len(c.algorithms_report)

    with open(filename, "w", encoding="utf-8") as f:
        #inciializa o ambiente da tabela
        f.write("\\begin{table}[]\n\\centering\n")
        f.write("\\begin{tabular}{" + "|l|" + "c|" * n_columns + "}\n")

        #cabeçalho principal, indica operação e origem dos arquivos
        f.write(hline())
        f.write(f"\\multicolumn{{2}}{{|c|}}{{Repetitive corpus}} & "
                f"\\multicolumn{{{n_algorithms}}}{{c|}}{{{caption}}} \\\\\n")
        f.write(hline())

        #cabeçalho 2 (nome das colunas da tabela)
        headers = ["File", "Input Size (MB)"] + [latex_escape(col) for col in final_table.columns[1:]]
        f.write(latex_row(headers))
        f.write(hline())

        #linhas da tabela
        for idx, row in final_table.iterrows():
            cells = [latex_escape(idx)] + [latex_escape(cell) for cell in row]
            f.write(latex_row(cells))
            f.write(hline())

        #finaliza o ambiente da tabela
        f.write("\\end{tabular}\n")
        f.write(f"\\caption{{{caption}}}\n")
        f.write(f"\\label{{{label}}}\n")
        f.write("\\end{table}\n")

def save_custom_markdown_table(final_table: pd.DataFrame, filename: str, caption: str):
    cols = list(final_table.columns)
    n_algorithms = len(c.algorithms_report)
    center_pos = n_algorithms // 2

    with open(filename, "w", encoding="utf-8") as f:
        # Título
        title_row = ["Repetitive corpus", ""] + ["---------"] * n_algorithms
        title_row[2 + center_pos] = caption
        f.write("| " + " | ".join(title_row) + " |\n")

        #separadores
        separator = [":---"] * len(title_row)
        separator[2 + center_pos] = ":---:"
        f.write("|" + "|".join(separator) + "|\n\n")

        # linha com nome das colunas
        header_row = ["File", "Input Size (MB)"] + cols[2:]
        f.write("| " + " | ".join(header_row) + " |\n")
        #alinhamento da segunda linha
        f.write("|" + "|".join([":---"] + [":---:"] * (len(header_row) - 1)) + "|\n")

        #escreve o conteúdo de cada linha
        for idx, row in final_table.iterrows():
            row_str = [str(idx), str(row.iloc[0])] + [str(x) for x in row.iloc[2:]]
            f.write("| " + " | ".join(row_str) + " |\n")
        

def generate_tables(df_list: List[pd.DataFrame], metric: str, output_suffix: str, metric_labels, path_dir: str):
    """
        Gera tabelas LaTeX e Markdown com os resultados de compressão/descompressão.
    
        Args:
            df_list: Lista de DataFrames com os dados.
            metric: Métrica a ser usada (ex: 'compressed_size_ratio').
            output_suffix: Sufixo do nome do arquivo de saída.
            metric_labels: Dicionário com (caption, label) para cada métrica.
    """
    df = pd.concat(df_list).reset_index()
    df = df[df['algorithm'].isin(c.algorithms_report)]

    input_sizes = df.groupby('file')['plain_size'].first().map(format_size)

    metric_table = df.pivot(index='file', columns='algorithm', values=metric)
    metric_table = metric_table.reindex(columns=c.algorithms_report)
    metric_table = metric_table.apply(lambda col: col.map(format_metric))

    final_table = pd.concat([input_sizes, metric_table], axis=1)
    final_table.columns = ['Input Size'] + c.algorithms_report

    caption, label = metric_labels.get(metric, (metric, f"tab:{metric}"))

    save_custom_latex_table(final_table, filename=f"{path_dir}/{metric}_table.tex", caption=caption, label=label)
    save_custom_markdown_table(final_table, filename=f"{path_dir}/{metric}_table.md", caption=caption)

