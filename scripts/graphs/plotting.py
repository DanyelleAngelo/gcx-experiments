import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from constants import COLOR_MAP, MARKERS, COLOR_MAP
from matplotlib.ticker import ScalarFormatter
from matplotlib.backends.backend_pdf import PdfPages
import matplotlib.ticker as ticker



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
    rules = []
    if rules_str.endswith(','):
        rules_str = rules_str[:-1]
    for rule in rules_str.split(','):
        if not rule.strip():
            continue
        try:
            level, rest = rule.split(':')
            rule_size, rule_count = rest.split('-')
            rules.append((int(level), int(rule_size), int(rule_count)))
        except ValueError:
            print(f"Erro ao processar regra: {rule}")
    return rules


def generate_grammar_chart(df, information, output_dir):
    plt.figure(figsize=(10, 6))

    for i, (index, row) in enumerate(df.iterrows()):
        rules = process_rules(row['level_cover_qtyRules'])
        rule_sizes = [r[1] for r in rules]
        rule_counts = [r[2] for r in rules]
        label = row['algorithm']
        marker = MARKERS[i % len(MARKERS)]
        plt.plot(rule_sizes, rule_counts, label=label, marker=marker)
    
    file=df.index[0].upper().split("-")[-1]
    customize_chart(information, f"{information['title']} - {file}")

    plt.ticklabel_format(style='plain', axis='both')
    plt.tight_layout()

    output_path = f"{output_dir}/{information['output_file']}-{df.index[0]}.png"
    plt.savefig(output_path)
    plt.show()