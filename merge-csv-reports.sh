#!/bin/bash

MAIN_PATH="report/2025-04-09-mac"
OTHER_PATH="report/2025-04-10-mac"

csv_files=("$MAIN_PATH"/*.csv)

if [ ${#csv_files[@]} -eq 0 ]; then
    echo "⚠️  Não há arquivos CSV em $MAIN_PATH"
    exit 1
fi

for file in "${csv_files[@]}"; do
    filename=$(basename "$file")
    other_file="$OTHER_PATH/$filename"

    if [ -f "$other_file" ]; then
        echo "Mesclando $filename"
        tail -n +2 "$other_file" >> "$file"
    else
        echo "⚠️  $filename não encontrado em $OTHER_PATH, pulando."
    fi
done


