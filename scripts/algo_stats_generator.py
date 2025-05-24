import os
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import glob
import re


def group_algorithm(name: str) -> str:
    """Classifica algoritmos em grupos simplificados."""
    name_lower = name.lower()
    if name_lower.startswith("gcx-y"):
        return "gcx"
    if re.match(r"gc\d+", name_lower):
        return "g*"
    return name_lower


def load_and_concat_csv(files: list[str], sep='|') -> pd.DataFrame:
    """Carrega múltiplos arquivos CSV e concatena em um único DataFrame."""
    dfs = []
    for file in files:
        try:
            dfs.append(pd.read_csv(file, sep=sep))
        except Exception as e:
            print(f"Erro ao ler '{file}': {e}")
    if not dfs:
        raise ValueError("Nenhum arquivo válido foi carregado.")
    return pd.concat(dfs, ignore_index=True)


def write_metric_report(data: pd.DataFrame, metric: str, output_path: str):
    """Gera relatório de estatísticas básicas para um métrico específico."""
    means = data.groupby('algorithm')[metric].mean()
    medians = data.groupby('algorithm')[metric].median()
    metric_name = metric.replace('_', ' ').title()
    best_mean = means.min()
    comparison = ((means - best_mean) / best_mean * 100).sort_values()
    ranking = means.sort_values().reset_index()
    ranking.columns = ['Algorithm', f'Mean {metric_name}']

    with open(output_path, 'w') as f:
        f.write(f"Mean {metric_name} by algorithm:\n{means.to_string()}\n\n")
        f.write(f"Median {metric_name} by algorithm:\n{medians.to_string()}\n\n")
        f.write(f"Percentage comparison of mean {metric_name} relative to the best algorithm:\n{comparison.to_string()}\n\n")
        f.write(f"Ranking of algorithms by mean {metric_name}:\n{ranking.to_string()}\n")


def analyze_files(files: list[str], output_dir: str, metric: str):
    os.makedirs(output_dir, exist_ok=True)
    data = load_and_concat_csv(files)

    data['group'] = data['algorithm'].apply(group_algorithm)

    report_path = os.path.join(output_dir, f"{metric}_analysis_report.txt")
    write_metric_report(data, metric, report_path)

    filename=f"{metric}_vs_compressed_size.png"
    plt.figure(figsize=(12, 8))
    sns.scatterplot(data=data, x='compressed_size', y=metric, hue='algorithm', palette='tab10', alpha=0.7)
    plt.xlabel('Compressed Size (bytes)')
    plt.ylabel(f'{metric.replace("_", " ").title()} (seconds)')
    plt.title(f'Scatter Plot: {metric.replace("_", " ").title()} vs Compressed Size')
    plt.legend(bbox_to_anchor=(1.05, 1), loc='upper left')
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, filename))
    plt.close()


def analyze_extraction_time(files: list[str], output_dir: str):
    os.makedirs(output_dir, exist_ok=True)
    data = load_and_concat_csv(files)

    data['algorithm_group'] = data['algorithm'].apply(group_algorithm)

    stats = data.groupby(['algorithm_group', 'substring_size'])['time'].agg(
        count='count',
        mean='mean',
        median='median',
        std='std',
        min='min',
        max='max'
    ).reset_index()

    stats_path = os.path.join(output_dir, "extraction_time_stats.csv")
    stats.to_csv(stats_path, index=False)

    report_path = os.path.join(output_dir, "extract_time_analysis_report.txt")
    write_metric_report(data, 'time', report_path)

    # Função auxiliar para gerar gráfico de linha
    def plot_line(data, x, y, hue, xlabel, ylabel, title, filename, log_scale_x=False):
        plt.figure(figsize=(14, 8))
        sns.lineplot(data=data, x=x, y=y, hue=hue, marker='o')
        if log_scale_x:
            plt.xscale('log')
        plt.xlabel(xlabel)
        plt.ylabel(ylabel)
        plt.title(title)
        plt.legend(title=hue)
        plt.grid(True, which="both", linestyle="--", linewidth=0.5)
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, filename))
        plt.close()

    plot_line(stats, 'substring_size', 'mean', 'algorithm_group',
              "Substring Size", "Mean Extraction Time (s)",
              "Mean Extraction Time vs Substring Size by Algorithm Group",
              "mean_extraction_time_vs_substring_size.png", log_scale_x=True)

    plot_line(stats, 'substring_size', 'median', 'algorithm_group',
              "Substring Size", "Median Extraction Time (s)",
              "Median Extraction Time vs Substring Size by Algorithm Group",
              "median_extraction_time_vs_substring_size.png", log_scale_x=True)

    print(f"Análise concluída. Relatórios e gráficos salvos em '{output_dir}'.")


if __name__ == "__main__":
    encoding_files = glob.glob(os.path.join("report", "2025-05-19", "*encoding.csv"))
    extract_files = glob.glob(os.path.join("report", "2025-05-19", "*extract.csv"))

    analyze_files(encoding_files, "report/2025-05-23", "compression_time")
    analyze_extraction_time(extract_files, "report/2025-05-23")
