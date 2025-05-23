def bytes_to_mb(bytes):
    return bytes / (1024 * 1024)

def compute_ratio_percentage(compressed_size, plain_size):
    return (compressed_size/plain_size)*100

def set_locale(locale):
    if locale == 'en':
        import en as language
    else:
        import pt as language
    return language

def convert_columns(df, cols, func):
    for col in cols:
        df[col] = df[col].apply(lambda x: func(x) if isinstance(x, (int, float)) else 0)
