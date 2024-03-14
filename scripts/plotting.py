import matplotlib.pyplot as plt
import matplotlib.cm as cm
import numpy as np
import random
import pandas as pd
import math

LINE_STYLE = ["--", ":", "-.", "-","--"]

color_map= {
    'compression_time': {
        "default_color": "#007599", "highlighted_color": "#00ffff", "gcis": cm.get_cmap('PiYG')
    },
    'decompression_time': {
        "default_color": "#007599", "highlighted_color": "#00ffff", "gcis": cm.get_cmap('PiYG')
    },
    'compressed_size': {
        "default_color": "#bc5090", "highlighted_color": "#ffa600", "gcis": cm.get_cmap('coolwarm')
    },
    'memory': cm.get_cmap('Set1'),
    'default': cm.get_cmap('winter')
}



width_bar = 0.2

def customize_chart(information, title):
    plt.xlabel(information['x_label'])
    plt.ylabel(information['y_label'])
    plt.title(title)
    plt.legend(title="Algoritmo")

    plt.xticks(rotation=45)
    plt.tight_layout(pad=3.0)  
    plt.grid(linestyle=':', alpha=0.5)

def generate_chart_bar(results_dcx, results_gcis, information, output_dir, max_value):
    col = information['col']
    plt.figure(figsize=(10,8))

    algorithm = results_dcx['algorithm'].unique().tolist()

    dcx_column = algorithm.index('GCX')
    default_color = color_map[col]["default_color"]
    colors = [default_color] * len(algorithm)
    colors[dcx_column] = color_map[col]["highlighted_color"]

    plt.bar(algorithm, results_dcx[col], width=0.5, color=colors, edgecolor='black', label="GCX")

    j=0
    for index, row in results_gcis.iterrows():
        plt.axhline(y=row[col], color=color_map[col]["gcis"](j), linestyle=LINE_STYLE[j], linewidth=2, label=row['algorithm'])
        j+=1

    file=results_dcx.index[0].upper().split("-")[-1]
    customize_chart(information, f"{information['title']} {file}")
    # plt.yticks(np.arange(0, max_value, 3))
    # plt.ylim(0, max_value)

    file = f"{output_dir}/{information['output_file']}-{results_dcx.index[0]}.png"
    plt.savefig(file)
    plt.close()

def generate_memory_chart(results_dcx, results_gcis, information, output_dir, max_value):
    plt.figure(figsize=(10,8))
    algorithm = results_dcx['algorithm'].unique().tolist()

    indexes = np.arange(len(algorithm))
    operation='memory'

    plt.bar(indexes - width_bar, results_dcx[information['col']], label='GCX - Peak', width=width_bar, align='center', color=color_map[operation](0))
    plt.bar(indexes, results_dcx[information['stack']], label='GCX - Stack', width=width_bar, align='center', color=color_map[operation](1))

    i=0
    for index, row in results_gcis.iterrows():
        plt.axhline(y=row[information['col']], linestyle=LINE_STYLE[i], color=color_map[operation](i+2), label=f"GCIS {index} - peak")
        i+=1
        plt.axhline(y=row[information['stack']], linestyle=LINE_STYLE[i], color=color_map[operation](i+2), label=f"GCIS {index} - stack")
        i+=1

    file=results_dcx.index[0].upper().split("-")[-1]
    customize_chart(information, f"{information['title']} {file}")
    plt.xticks(indexes, algorithm)
    plt.ylim(0, max_value)

    file = f"{output_dir}/{information['output_file']}-{results_dcx.index[0]}.png"
    plt.savefig(file)
    plt.close()

def generate_chart_line(results, information, output_dir, max_value, min_value):
    plt.figure(figsize=(10,8))

    for algorithm, group in results.groupby('algorithm'):
        plt.plot(group['substring_size'], group['time'], marker='o', linewidth=0.5, label=algorithm)
    
    plt.ylim([min_value, max_value+5])
    plt.xscale('log')
    plt.yscale('log')

    file=results.index[0].upper().split("-")[-1]
    customize_chart(information, f"{information['title']} - {file}")
    file = f"{output_dir}/{information['output_file']}-{results.index[0]}.png"
    plt.savefig(file)
    plt.close()
