import pandas as pd
from pathlib import Path

"""
Script para obter médias e análises de execuções dos algoritmos selecionados.
"""

KEEP_ALGS = [
    "GC8", "GCX-y8", "PlainSlp_32Fblc", "GCIS-ef", "CBT", "REPAIR-PlainSlp_32Fblc", "bzip2", "7zip"
]
EXCLUDED_FILES = [
    "pseudo-real-dblp.xml.00001.2", 
    "pseudo-real-dblp.xml.0001.2", 
    "pseudo-real-dblp.xml.0001.1",
    # "artificial-fib41",
    # "artificial-tm29",
    # "artificial-rs.13"
]


def load_and_concat_files(folder: Path, pattern: str, filter_keep=True) -> pd.DataFrame:
    files = list(folder.glob(pattern))
    if not files:
        raise FileNotFoundError(f"Nenhum arquivo {pattern} encontrado em {folder}")

    dfs = []
    for csv_file in files:
        if any(excl in csv_file.name for excl in EXCLUDED_FILES):
            continue

        df = pd.read_csv(csv_file, sep="|")
        if filter_keep and "algorithm" in df.columns:
            df = df[df["algorithm"].isin(KEEP_ALGS)]

        dfs.append(df)

    if not dfs:
        raise ValueError(f"Nenhum CSV válido encontrado após filtragem para {pattern}")

    return pd.concat(dfs, ignore_index=True)


def analyze_avg_extract_time(dataset: pd.DataFrame, output_file: Path):
    avg_time = dataset.groupby("algorithm")["time"].mean().reset_index()
    avg_time["time_microseconds"] = avg_time["time"] * 1e6
    avg_time["time_microseconds"] = avg_time["time_microseconds"].apply(lambda x: f"{x:.2e}") # coloca em notação científica
    avg_time = avg_time.sort_values(by="time_microseconds")
    avg_time.to_csv(output_file, index=False)
    print(f"Médias de tempo salvas em: {output_file}\n{avg_time}")


def analyze_peak(dataset: pd.DataFrame, output_file: Path):
    avg_peak = dataset.groupby("algorithm")[["peak_comp", "peak_decomp"]].mean().reset_index()
    avg_peak["peak_comp_MiB"] = (avg_peak["peak_comp"] / (1024**2)).round(2)
    avg_peak["peak_decomp_MiB"] = (avg_peak["peak_decomp"] / (1024**2)).round(2)
    avg_peak_final = avg_peak[["algorithm", "peak_comp_MiB", "peak_decomp_MiB"]].sort_values(by="peak_comp_MiB")
    avg_peak_final.to_csv(output_file, sep="|", index=False)
    print(f"Arquivo salvo: {output_file}\n{avg_peak_final}")


def analyze_compression_speed(dataset: pd.DataFrame, output_file: Path):
    dataset["plain_size_mib"] = dataset["plain_size"] / (1024**2)
    dataset["compression_speed"] = dataset["plain_size_mib"] / dataset["compression_time"]
    dataset["decompression_speed"] = dataset["plain_size_mib"] / dataset["decompression_time"]

    avg_speed = dataset.groupby("algorithm")[["compression_speed", "decompression_speed"]].mean().reset_index()
    avg_speed = avg_speed.sort_values(by="compression_speed", ascending=False)
    avg_speed.to_csv(output_file, sep="|", index=False)
    print(f"Arquivo salvo: {output_file}\n{avg_speed}")


def analyze_gc_vs_repair(dataset: pd.DataFrame, output_file: Path):
    results = []
    epsilon = 1e-9

    for alg in ['GC8', 'GCX-y8']:
        alg_data = dataset[dataset['algorithm'] == alg].set_index('file')
        repair_data = dataset[dataset['algorithm'] == 'REPAIR-PlainSlp_32Fblc'].set_index('file')
        seven_zip_data = dataset[dataset['algorithm'] == '7zip'].set_index('file')

        # filtra os datasets que possuem os mesmos índices
        common_files = alg_data.index.intersection(repair_data.index).intersection(seven_zip_data.index)
        alg_data, repair_data, seven_zip_data = alg_data.loc[common_files], repair_data.loc[common_files], seven_zip_data.loc[common_files]

        # obtéms as proporções
        compression_speedup = repair_data['compression_time'] / (alg_data['compression_time'] )
        decompression_speedup = repair_data['decompression_time'] / (alg_data['decompression_time'] )
        compression_ratio_vs_repair = alg_data['compressed_size_ratio'] / (repair_data['compressed_size_ratio'] )
        repair_vs_7zip = (repair_data['compressed_size_ratio'] ) / seven_zip_data['compressed_size_ratio']


        # Resumo
        result = {
            'algorithm': alg,
            'best_compression_file': compression_speedup.idxmax(),
            'best_compression_speedup': round(compression_speedup.max(), 2),
            'worst_compression_file': compression_speedup.idxmin(),
            'worst_compression_speedup': round(compression_speedup.min(), 2),
            'best_decompression_file': decompression_speedup.idxmax(),
            'best_decompression_speedup': round(decompression_speedup.max(), 2),
            'worst_decompression_file': decompression_speedup.idxmin(),
            'worst_decompression_speedup': round(decompression_speedup.min(), 2),
            'best_compression_ratio_file': compression_ratio_vs_repair.idxmax(),
            'best_compression_ratio_vs_repair': compression_ratio_vs_repair.max(),
            'worst_compression_ratio_file': compression_ratio_vs_repair.idxmin(),
            'worst_compression_ratio_vs_repair': compression_ratio_vs_repair.min(),
            'best_repair_vs_7zip_file': repair_vs_7zip.idxmax(),
            'best_repair_vs_7zip': repair_vs_7zip.max(),
            'worst_repair_vs_7zip_file': repair_vs_7zip.idxmin(),
            'worst_repair_vs_7zip': repair_vs_7zip.min(),
            'avg_repair_vs_7zip': repair_vs_7zip.mean(),
        }
        results.append(result)
        print(f"{alg} comprimiu {round(result['best_compression_speedup'],2)}x mais rápido que REPAIR no arquivo {result['best_compression_file']} " \
        f"(melhor) e {round(result['worst_compression_speedup'],2)}x no arquivo {result['worst_compression_file']} (pior).")

        print( f"{alg} descomprime {round(result['best_decompression_speedup'],2)}x mais rápido que REPAIR no arquivo {result['best_decompression_file']} " \
            f"(melhor) e {round(result['worst_decompression_speedup'],2)}x no arquivo {result['worst_decompression_file']} (pior).")
        
        print(f"{alg} comprime entre {result['worst_compression_ratio_vs_repair']}x "\
            f"no arquivo {compression_ratio_vs_repair.idxmin()} e {result['best_compression_ratio_vs_repair']}x  "    \
            f"no arquivo {result['best_compression_ratio_file']} que o Repair.")

    print(f"REPAIR comprime em média {round(result['avg_repair_vs_7zip'], 2)}x menos que 7zip, melhor caso {round(result['best_repair_vs_7zip'],2)}x "\
        f"no arquivo {result['best_compression_ratio_file']} e pior caso {round(result['worst_repair_vs_7zip'],2)}x no arquivo {result['worst_repair_vs_7zip_file']}.")
        
    final_df = pd.DataFrame(results)
    final_df.to_csv(output_file, sep="|", index=False)
    print(f"\nArquivo salvo: {output_file}")


if __name__ == "__main__":
    input_folder = Path("report/2025-08-12")
    #df_perf = load_and_concat_files(input_folder, "*encoding.csv")
    #analyze_gc_vs_repair(df_perf, input_folder / "analise.csv")
    #analyze_compression_speed(df_perf, input_folder / "analise-speed.csv")
    #analyze_peak(df_extract, f"{input_folder}/analise-memory.csv")
    df_extract = load_and_concat_files(input_folder, "*extract.csv")
    analyze_avg_extract_time(df_extract,  f"{input_folder}/analise-extract.csv")
