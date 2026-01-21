import json

compression_summary = {
    'peak_comp': {'GC*': 0.0, 'GCIS': 0.0, 'GCIS-s8b': 0.0, 'RePair (PlainSlp_32Fblc)':0.0, 'GC-': 0.0, '7zip': 0.0},
    'peak_decomp': {'GC*': 0.0, 'GCIS': 0.0, 'GCIS-s8b': 0.0, 'RePair (PlainSlp_32Fblc)':0.0, 'GC-': 0.0, '7zip': 0.0},
    'compression_time': {'GC*': 0.0, 'GCIS': 0.0, 'GCIS-s8b': 0.0, 'RePair (PlainSlp_32Fblc)':0.0, 'GC-': 0.0, '7zip': 0.0},
    'decompression_time': {'GC*': 0.0, 'GCIS': 0.0, 'GCIS-s8b': 0.0, 'RePair (PlainSlp_32Fblc)':0.0, 'GC-': 0.0, '7zip': 0.0},
    'compressed_size': {'GC*': 0.0, 'GCIS': 0.0, 'GCIS-s8b': 0.0, 'RePair (PlainSlp_32Fblc)':0.0, 'GC-': 0.0, '7zip': 0.0}
}

extract_summary = {
    'time': {'GC*': 0.0, 'GCIS': 0.0, 'GCIS-s8b': 0.0, 'PlainSlp_FblcFblc':0.0, 'PlainSlp_32Fblc':0.0, 'GC-': 0.0}
}

n_gcx_variations = 7

def set_summary(values, df):
    for key in values.keys():
        for algorithm in df['algorithm'].unique():
            if algorithm not in values[key]:
                values[key]['GC-'] += df[df['algorithm'] == algorithm][key].sum()
            else:
                mask =  df['algorithm'] == algorithm
                values[key][algorithm] += df[mask][key].sum()
        
def print_summary(df, summary):
    size=len(df)
    for key in summary.keys():
        for algorithm in summary[key].keys():
            summary[key][algorithm] = summary[key][algorithm]/size
            # if key == 'time':
            #     summary[key][algorithm] = (summary[key][algorithm]/10000)*1e6

        summary[key]['GC-'] = summary[key]['GC-'] / n_gcx_variations
    print("\n\t------ Average Values  ------")
    print(json.dumps(summary, indent=4))