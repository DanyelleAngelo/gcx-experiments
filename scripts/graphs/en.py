
COMPRESS_AND_DECOMPRESS = {
    'cmp_time': {
        "col": "compression_time",
        "y_label": "Compression time (s).",
        "x_label": "Algorithms",
        "title": "Compression time - ",
        "output_file": "compression_time",
        "legend":  "Algorithm"
    },
    'dcmp_time': {
        "col": "decompression_time",
        "y_label": "Decompression time (s).",
        "x_label": "Algorithms",
        "title": "Decompression time - ",
        "output_file": "decompression_time",
        "legend":  "Algorithms"
    },
    'peak_comp': {
        "col": "peak_comp",
        "stack": "stack_comp",
        "y_label": "Memory usage MB (peak)",
        "x_label": "Algorithms",
        "title": "Memory usage - ",
        "output_file": "memory_usage_comp",
        "legend":  "Algorithms"
    },
    'peak_decomp': {
        "col": "peak_decomp",
        "stack": "stack_decomp",
        "y_label": "Memory usage MB (peak)",
        "x_label": "Algorithms",
        "title": "Memory usage - ",
        "output_file": "memory_usage_decomp",
        "legend":  "Algorithms"
    },
    'ratio': {
        "col": "compressed_size_ratio",
        "y_label": "Compression ratio (%).",
        "x_label": "Algorithms",
        "title": "Compression ratio - ",
        "output_file": "ratio",
        "legend":  "Algorithms"
    }
}

EXTRACT = {
    'time': {
        "col": "time",
        "y_label": "Extraction time (\u03BCs)",
        "x_label": "Substring length",
        "title": "Extraction time (\u03BCs)",
        "output_file": "extracting_time",
        "legend":  "Algorithms"
    },
    'peak': {
        "col": "peak",
        "y_label": "Pico de memória em MB (log)",
        "x_label": "Algorithms",
        "title": "Pico de memória durante a extração - ",
        "output_file": "peak_memory_usage_extract"
    },
    'stack': {
        "col": "stack",
        "y_label": "Uso de memória em MB (log)",
        "x_label": "Algorithms",
        "title": "Uso de memória durante a extração (stack) - ",
        "output_file": "stack_memory_usage_extract"
    }
}

GRAMMAR = {
    "x_label": "Algorithms (levels represented by bars)",
    "y_label": "Number of rules",
    "title": "Distribution of rules per level in the generated grammar",
    "output_file": "grammar_info",
    "legend": "Variations of ",
    "plain_size": "Original file size",
}

metric_labels = {
    'compressed_size_ratio': ("Compression Ratio (\\%)", "tab:compression_ratio"),
    'compression_time': ("Compression Time (s)", "tab:compression_time"),
    'decompression_time': ("Decompression Time (s)", "tab:decompression_time"),
}