import glob
import pandas as pd

def is_desired_algorithm(alg, existing_algorithms):
    alg = alg.strip()
    if alg in existing_algorithms:
        return False
    return (
        alg.startswith("REPAIR") or
        alg.startswith("GCIS") or
        alg.startswith("7zip") or
        alg.startswith("bzip2") or
        alg.startswith("GC8") or
        alg.startswith("GCX-y16")
    )

input_files = glob.glob("report/2025-06-01/*encoding.csv")

existing_algorithms = set()
all_data = []

for filename in input_files:
    df = pd.read_csv(filename, sep='|', usecols=['file', 'algorithm', 'compression_time', 'decompression_time', 'compressed_size_ratio'])
    filtered_df = df[df['algorithm'].apply(lambda alg: is_desired_algorithm(alg, existing_algorithms))]
    all_data.append(filtered_df)

result_df = pd.concat(all_data, ignore_index=True)

compression_time = result_df.pivot(index='file', columns='algorithm', values='compression_time')
decompression_time = result_df.pivot(index='file', columns='algorithm', values='decompression_time')
compressed_size_ratio = result_df.pivot(index='file', columns='algorithm', values='compressed_size_ratio')

result = pd.concat(
    [compression_time, decompression_time, compressed_size_ratio],
    axis=1,
    keys=['compression_time', 'decompression_time', 'compressed_size_ratio']
)
result = result.reset_index()

result.to_csv("report/2025-06-01/00_best_algorithms.csv", index=False)

print(f"Processados {len(input_files)} arquivos, total de {len(result_df)} linhas filtradas.")
