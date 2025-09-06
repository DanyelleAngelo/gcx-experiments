import pandas as pd
from pathlib import Path
import graphs.utils as ut

keep = [
    "GC16", "GCX-y16", "PlainSlp_32Fblc", "GCIS-ef", "CBT", "REPAIR-PlainSlp_32Fblc", "bzip2"
]
excluded_files = ["pseudo-real-dblp.xml.00001.2", "pseudo-real-dblp.xml.0001.2", "pseudo-real-dblp.xml.0001.1"]


def load_and_concat_extract_files(folder: Path) -> pd.DataFrame:
    files = list(folder.glob("*extract.csv"))
    if not files:
        raise FileNotFoundError(f"Nenhum arquivo *extract.csv encontrado em {folder}")

    dfs = []
    for csv_file in files:
        if any(excluded in csv_file.name for excluded in excluded_files):
            continue
        df = pd.read_csv(csv_file, sep="|")

        if "algorithm" not in df.columns or "time" not in df.columns:
            print(f"Pulado {csv_file.name}: coluna 'algorithm' ou 'time' não encontrada")
            continue

        filtered_df = df[
            (df["algorithm"].isin(keep)) &
            (df["substring_size"] == 10_000)
        ].copy()
        filtered_df["time_ms"] = (filtered_df["time"] * 1_000).round(2)

        dfs.append(filtered_df)

    if not dfs:
        raise ValueError("Nenhum CSV válido foi encontrado após filtragem")

    df_concat = pd.concat(dfs, ignore_index=True)
    return df_concat
   
def load_and_concat_perf_files(folder: Path) -> pd.DataFrame:
    files = list(folder.glob("*encoding.csv"))
    if not files:
        raise FileNotFoundError(f"Nenhum arquivo *encoding.csv encontrado em {folder}")

    dfs = []
    for csv_file in files:
        if any(excluded in csv_file.name for excluded in excluded_files):
            continue
        df = pd.read_csv(csv_file, sep="|")
        filtered_df = df[df["algorithm"].isin(keep)].copy()
        dfs.append(filtered_df)

    df_concat = pd.concat(dfs, ignore_index=True)
    return df_concat

def extract_analyze(dataset, output_file):
    avg_time = dataset.groupby("algorithm")["time_ms"].mean().reset_index()
    avg_time = avg_time.sort_values(by="time_ms")

    print(avg_time)
    avg_time.to_csv(output_file, index=False)

def analyze_peak(dataset: pd.DataFrame, output_file: Path):
    avg_peak = dataset.groupby("algorithm")[["peak_comp", "peak_decomp"]].mean().reset_index()

    avg_peak["peak_comp_MiB"] = (avg_peak["peak_comp"] / (1024**2)).round(2)
    avg_peak["peak_decomp_MiB"] = (avg_peak["peak_decomp"] / (1024**2)).round(2)

    avg_peak_final = avg_peak[["algorithm", "peak_comp_MiB", "peak_decomp_MiB"]].sort_values(by="peak_comp_MiB")

    avg_peak_final.to_csv(output_file, sep="|", index=False)
    print(f"Arquivo salvo: {output_file}")
    print(avg_peak_final)


if __name__ == "__main__":
    input_folder = Path("report/2025-08-12")
    output_file = Path("report/2025-08-12/analise-memory.csv")

    df_all = load_and_concat_perf_files(input_folder)
    analyze_peak(df_all, output_file)
