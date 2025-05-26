import pandas as pd
from pathlib import Path
import graphs.utils as ut

def load_and_concat_grammar_files(folder: Path) -> pd.DataFrame:
    """
    Lê todos os arquivos terminados em *grammar.csv na pasta e concatena em um único DataFrame.
    """
    files = list(folder.glob("*grammar.csv"))
    if not files:
        raise FileNotFoundError(f"Nenhum arquivo *grammar.csv encontrado em {folder}")

    dfs = []
    for f in files:
        df = pd.read_csv(f, sep="|")
        df.drop(columns='level_cover_qtyRules', inplace=True)
        df['compressed_size'] = df['compressed_size'].apply(lambda x: round(ut.bytes_to_mb(x),2))
        df['plain_size'] = df['plain_size'].apply(lambda x: round(ut.bytes_to_mb(x),2))
        dfs.append(df)

    df_concat = pd.concat(dfs, ignore_index=True)
    return df_concat


def save_dataframe(df: pd.DataFrame, path: Path):
    """
    Salva o DataFrame no arquivo CSV com separador pipe e sem índice.
    """
    df.to_csv(path, sep="|", index=False)
    print(f"Arquivo salvo em {path}")


if __name__ == "__main__":
    input_folder = Path("report/2025-05-26")
    output_file = Path("report/2025-05-26/00_grammar_all_files.csv")

    df_all = load_and_concat_grammar_files(input_folder)
    save_dataframe(df_all, output_file)
