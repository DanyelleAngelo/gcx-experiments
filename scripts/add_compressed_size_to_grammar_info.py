import os
import pandas as pd

caminho = '../report/2025-04-15-mac/'

arquivos_grammar = [f for f in os.listdir(caminho) if f.endswith('gcx-grammar.csv')]
arquivos_encoding = [f for f in os.listdir(caminho) if f.endswith('gcx-encoding.csv')]

for grammar_file in arquivos_grammar:
    encoding_file = grammar_file.replace('-grammar.csv', '-encoding.csv')
    
    if encoding_file in arquivos_encoding:
        arquivo_grammar = pd.read_csv(os.path.join(caminho, grammar_file), delimiter='|')
        arquivo_encoding = pd.read_csv(os.path.join(caminho, encoding_file), delimiter='|')

        arquivo_encoding_reduced = arquivo_encoding[['algorithm', 'compressed_size']]
        merged_df = pd.merge(arquivo_grammar, arquivo_encoding_reduced, on='algorithm', how='left')
        
        merged_df.to_csv(os.path.join(caminho, grammar_file), index=False, sep='|') 

        print(f'Arquivo {grammar_file} atualizado com sucesso!')
    else:
        print(f'Arquivo de encoding correspondente para {grammar_file} não encontrado.')