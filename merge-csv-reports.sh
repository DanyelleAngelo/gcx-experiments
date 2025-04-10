#!/bin/bash

CSV_DIR="report/2025-04-09-mac"

MAIN_PATH="./$CSV_DIR"
OTHER_PATH="report/2025-04-09-y2-mac"

for file in "$MAIN_PATH"/*.csv; do
    if [ -e "$file" ]; then
        break
    fi

    filename=$(basename "$file")
    other_file="$OTHER_PATH/$filename"

    if [ -f "$other_file" ]; then
        echo "Mesclando $filename"
        tail -n +2 "$other_file" >> "$file"
    else
        echo "⚠️  $filename não encontrado em $OTHER_PATH, pulando."
    fi
done


