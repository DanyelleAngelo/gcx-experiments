import pandas as pd
import numpy as np


df = pd.read_csv("report/2026-01-08/cbt/results_bt_enwiki.csv")

df["plain_bytes"] = df["Size (MB)"] * 1024 * 1024

df["ratio"] = pd.to_numeric(df["Compression Ratio (%)"], errors="coerce")

df["compressed_bytes"] = np.where(
    df["ratio"] > 0,
    df["plain_bytes"] / df["ratio"],
    df["plain_bytes"]
)

df["compressed_bytes"] = df["compressed_bytes"].round()

print(df[["Filename", "plain_bytes", "ratio", "compressed_bytes"]])

file_name = "cbt_data_with_compressed_bytes.csv"
df.to_csv(file_name, index=False)

print(f"Arquivo gerado: {file_name}")