import os
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import glob
import re

def is_desired_algorithm(alg):
    alg = alg.strip()
    return (
        alg.startswith("GC") or
        alg.startswith("GCX-y")
    )

def group_algorithm(name: str) -> str:
    """Classifica algoritmos em grupos simplificados."""
    name_lower = name.lower()
    if name_lower.startswith("gcx-y"):
        return "gcx"
    if re.match(r"GC\d+", name_lower):
        return "gc*"
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
    df =  pd.concat(dfs, ignore_index=True)
    filtered_df = df[df['algorithm'].apply(lambda alg: is_desired_algorithm(alg))].copy()
    return filtered_df

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

    exclude_list = ['pseudo-real-dblp.xml.00001.2', 'pseudo-real-dblp.xml.0001.2', 'pseudo-real-dblp.xml.0001.1']
    data = data[~data['file'].isin(exclude_list)]

    def get_group(filename):
        fname_lower = filename.lower()
        if fname_lower.startswith('pseudo-real'):
            return 'pseudo-real'
        return fname_lower.split('-')[0]

    def extract_initial_x(algo_name):
        algo_name = str(algo_name).strip()
        match = re.search(r'GC(\d+)', str(algo_name)) # captura apenas GC2, GC8, GC....
        return int(match.group(1)) if match else None

    data['group'] = data['file'].apply(get_group)
    data['initial_x'] = data['algorithm'].apply(extract_initial_x)
    
    to_exclude_gc = [2, 4]
    plot_data = data[
        (data['initial_x'].notnull()) & 
        (~data['initial_x'].isin(to_exclude_gc))
    ].copy()
    plot_data = plot_data.sort_values(by=['initial_x'])

    target_groups = sorted(plot_data['group'].unique())
    num_groups = len(target_groups)
    
    if num_groups == 0:
        print("Nenhum grupo encontrado para plotar.")
        return

    fig, axes = plt.subplots(1, num_groups, figsize=(7 * num_groups, 8), squeeze=False)
    axes = axes.flatten()
    
    fig.suptitle(f'Análise Comparativa GCX: {metric.replace("_", " ").title()}', fontsize=18)

    for i, group in enumerate(target_groups):
        group_df = plot_data[plot_data['group'] == group]
        ax = axes[i]
        
        sns.lineplot(
            data=group_df, 
            x='initial_x', 
            y=metric, 
            hue='file', 
            marker="o",
            ax=ax,
            legend='brief'
        )
        
        ax.set_xscale('log', base=2)
        ticks = sorted(group_df['initial_x'].unique())
        ax.set_xticks(ticks)
        ax.set_xticklabels(ticks)
        
        y_max = group_df[metric].max()
        ax.set_ylim(-1, y_max * 1.1 if y_max > 0 else 10) 

        ax.set_title(f'Grupo: {group.upper()}', fontsize=14)
        ax.set_xlabel('X - tamanho da substring')
        ax.set_ylabel(metric.replace("_", " ").title() if i == 0 else "") 
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize='x-small', loc='upper right', bbox_to_anchor=(1, 1))

    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    
    filename = f"00_gcx_{metric}_vs_x.png"
    plt.savefig(os.path.join(output_dir, filename), dpi=300)
    plt.close()
    print(f"Gráfico gerado com {num_groups} grupos: {filename}")


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

    stats_path = os.path.join(output_dir, "00_extraction_time_stats.csv")
    stats.to_csv(stats_path, index=False)

    report_path = os.path.join(output_dir, "00_extract_time_analysis_report.txt")
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
              "00_mean_extraction_time_vs_substring_size.png", log_scale_x=True)

    plot_line(stats, 'substring_size', 'median', 'algorithm_group',
              "Substring Size", "Median Extraction Time (s)",
              "Median Extraction Time vs Substring Size by Algorithm Group",
              "00_median_extraction_time_vs_substring_size.png", log_scale_x=True)

    print(f"Análise concluída. Relatórios e gráficos salvos em '{output_dir}'.")


if __name__ == "__main__":
    path_dir="2025-08-12"
    encoding_files = glob.glob(os.path.join("report", path_dir, "*encoding.csv"))
    extract_files = glob.glob(os.path.join("report", path_dir, "*extract.csv"))

    #analyze_files(encoding_files, "report/2025-06-01", "compression_time")
    analyze_files(encoding_files, f"report/{path_dir}", "compression_ratio")
    #analyze_extraction_time(extract_files, "report/2025-06-01")
