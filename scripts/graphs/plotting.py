import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from constants import COLOR_MAP, MARKERS, COLOR_MAP
from matplotlib.ticker import ScalarFormatter
import matplotlib.ticker as ticker
import matplotlib.cm as cm


width_bar = 0.2

def customize_chart(information, title):
    font=13
    plt.xlabel(information['x_label'], fontsize=font)
    plt.ylabel(information['y_label'], fontsize=font)
    plt.title(title, fontsize=font+2)
    plt.legend(title=information["legend"], fontsize=font)
    #plt.gca().yaxis.set_major_locator(MaxNLocator(nbins=20))
    plt.xticks(rotation=45)
    plt.tight_layout(pad=3.0)  
    plt.grid(linestyle=':', alpha=0.5)

def generate_chart_bar_without_gcn(results_gcx, others, information, output_dir, max_value=None):
    plt.figure(figsize=(10,8))
   
    target_column = information['col'] #analyzed metric
    gcx = results_gcx[results_gcx['algorithm'] == 'GCX']

    column_color = target_column + "_bar"
    plt.bar(gcx['algorithm'].tolist(), gcx[target_column], width=0.5, color=COLOR_MAP[column_color][0], edgecolor='black', label="GCX")

    j=1
    for _, row in others.iterrows():
        plt.bar(row['algorithm'], row[target_column], width=0.5, color=COLOR_MAP[column_color][j], edgecolor='black', label=row['algorithm'])
        j+=1

    file=results_gcx.index[0].upper().split("-")[-1]
    customize_chart(information, f"{information['title']} {file}")

    if max_value != None:
        plt.ylim(0, max_value)

    file = f"{output_dir}/{information['output_file']}-{results_gcx.index[0]}.png"
    plt.savefig(file)
    plt.close()


def generate_chart_bar(results_gcx, others, information, output_dir, max_value=None, without_gcn=False):
    if without_gcn == False:
        plt.figure(figsize=(10,8))
    
        target_column = information['col'] #analyzed metric
        
        algorithms = results_gcx['algorithm'].unique().tolist()
        gcx_number = len(algorithms)
        gc_star = results_gcx[~results_gcx['algorithm'].str.upper().str.startswith('GCX')]
        gcx = results_gcx[results_gcx['algorithm'].str.upper().str.startswith('GCX')]

        #GCX results
        plt.bar(gcx['algorithm'].tolist(), gcx[target_column], width=0.5, color=COLOR_MAP[target_column]["highlighted_color"], edgecolor='black', label="GCX")
        plt.bar(gc_star['algorithm'].tolist(), gc_star[target_column], width=0.5, color=COLOR_MAP[target_column]["default_color"], edgecolor='black', label="GC*")

        j=0
        #repair and gcis results
        for index, row in others.iterrows():
            x = np.linspace(-1, gcx_number, 35 + (j*10)) 
            y = np.full_like(x, row[target_column])
            if row['algorithm'] == "REPAIR":
                plt.scatter(x, y, color=COLOR_MAP['line'][j], marker=MARKERS[j], s=15, linestyle='None', label=row['algorithm'])
            else:
                plt.scatter(x, y, color=COLOR_MAP['line'][j], marker=MARKERS[j],  s=15, linestyle='None', label=row['algorithm'])
            j+=1

        file=results_gcx.index[0].upper().split("-")[-1]
        customize_chart(information, f"{information['title']} {file}")

        if max_value != None:
            plt.ylim(0, max_value)

        plt.xlim(-1, gcx_number)
        file = f"{output_dir}/{information['output_file']}-{results_gcx.index[0]}.png"
        plt.savefig(file)
        plt.close()
        return

    generate_chart_bar_without_gcn(results_gcx, others, information,output_dir, max_value)

def generate_chart_line(results, information, output_dir, max_value, min_value):
    plt.figure(figsize=(10,8))
    n_markers = len(MARKERS)
    j=0
    for algorithm, group in results.groupby('algorithm'):
        plt.plot(group['substring_size'], group['time'], marker=MARKERS[j], linewidth=0.5, label=algorithm)
        j= j+1
        if j > n_markers:
            j=0

    #plt.ylim([min_value, max_value+5])
    plt.xscale('log')
    plt.yscale('log')

    file=results.index[0].upper().split("-")[-1]
    customize_chart(information, f"{information['title']} - {file}")

    file = f"{output_dir}/{information['output_file']}-{results.index[0]}.png"
    plt.savefig(file)
    plt.close()

def process_rules(rules_str):
    """
        Transforma uma string como '0:1-4,1:2-3' em uma lista de tuplas: [(0, 1, 4), (1, 2, 3)]
        onde cada tupla representa (nível, tamanho_da_regra, quantidade_de_regras).
    """
    rules = []
    rules_str = rules_str.rstrip(',')
    for rule in rules_str.split(','):
        try:
            level, rest = rule.split(':')
            rule_size, rule_count = rest.split('-')
            rules.append((int(level), int(rule_size), int(rule_count)))
        except ValueError:
            print(f"Erro ao processar regra: {rule}")
    return rules


def plot_grammar(df, ax, algorithms, color_map, bar_width=0.9, bar_spacing=1.5):
    """
        Plota um gráfico de barras representando a quantidade de regras por nível de gramática para cada algoritmo.
        Cada grupo de barras representa um algoritmo, e dentro de cada grupo, cada barra corresponde a um nível da gramática.
        A altura de cada barra indica a quantidade de regras distintas naquele nível, enquanto o rótulo acima da barra indica o tamanho da regra utilizada (X=...).
    """
    positions, heights, labels, colors = [], [], [], []
    current_x = 0
    xticks, xtick_labels = [], []
    i = 0

    for _, row in df.iterrows():
        alg = row['algorithm']
        if alg not in algorithms:
            continue

        rules = process_rules(row['level_cover_qtyRules'])
        num_levels = row['nLevels']

        for i, (level, rule_size, qty_rules) in enumerate(rules):
            positions.append(current_x + i * bar_width)
            heights.append(qty_rules)
            labels.append(f"X={rule_size}")
            colors.append(color_map[alg])
        
        xticks.append(current_x + (num_levels -1) * bar_width / 2)
        xtick_labels.append(f"{alg} ({row['compressed_size']} MB) ")
        current_x += num_levels * bar_width + bar_spacing # posição da primeira barra do próximo algoritmo
        
    bars = ax.bar(positions, heights, width= bar_width, color=colors, edgecolor= 'black', linewidth=1)

    # adiciona o tamanho da regra usada em cada nível (barra)
    for bar, label in zip(bars, labels):
        height = bar.get_height()
        ax.text(bar.get_x() + bar.get_width() / 2, height + max(heights) * 0.01, label, ha='center', va='bottom', fontsize=7)
    

    ax.set_xticks(xticks)
    ax.set_xticklabels(xtick_labels, fontsize=8, rotation=10)

def generate_grammar_chart(results, information, output_dir):
    fig, axs = plt.subplots(2, 1, figsize=(16, 10), gridspec_kw={'height_ratios': [1, 1]})
    fig.tight_layout(pad=8.0)

    grammar_groups = [
        results[results['algorithm'].str.startswith("GCX-y")], 
        results[~results['algorithm'].str.startswith("GCX-y")]
    ]
    grammar_groups_names = ["GCX", "GC*"]

    for i in range(0, len(grammar_groups)):
        ax = axs[i]
        algorithms = grammar_groups[i]['algorithm'].unique()
        colors = cm.get_cmap('tab20', len(algorithms))
        color_map = {alg: colors(j) for j, alg in enumerate(algorithms)}

        plot_grammar(grammar_groups[i], ax, algorithms, color_map)

        ax.set_title(f"{grammar_groups_names[i]} - {information['title']}", fontsize=10, fontweight='bold')
        ax.set_ylabel(information['y_label'])
        ax.set_xlabel(information['x_label'], labelpad=25)
        ax.ticklabel_format(style='plain', axis='y')
        ax.grid(linestyle=':', alpha=0.5)
        

        handles = [plt.Rectangle((0, 0), 1, 1, color=color_map[alg]) for alg in algorithms]
        ax.legend(
            handles,
            algorithms,
            title=f"{information['legend']} {grammar_groups_names[i]}",
            loc='upper left',
            bbox_to_anchor=(1, 1)
        )

        # adiciona o tamanho do arquivo em texto plano abaixo da legenda
        ax.text(
            1.01, 1.05,
            f"{information['plain_size']}: {results.iloc[0]['plain_size']} MB",
            transform=ax.transAxes,
            fontsize=9,
            va='top'
        )

    plt.tight_layout(pad=4.0)

    file = f"{output_dir}/{information['output_file']}-{results.index[0]}.png"
    #plt.show()
    plt.savefig(file)
    plt.close()