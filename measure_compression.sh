#!/bin/bash
source utils.sh

COVERAGE_LIST=(2 4 8 16 32 64 128)
STR_LEN=(1 10 100 1000 10000)
EXTRACT_ENCODING=("PlainSlp_32Fblc"  "PlainSlp_FblcFblc")

#cabeçalhos
COMPRESSION_HEADER="file|algorithm|peak_comp|stack_comp|compression_time|peak_decomp|stack_decomp|decompression_time|compressed_size|plain_size"
EXTRACTION_HEADER="file|algorithm|peak|stack|time|substring_size"
HEADER_REPORT_GRAMMAR="file|algorithm|nLevels|xs_size|level_cover_qtyRules"

# paths
GCIS_EXECUTABLE="../../GCIS/build/src/./gc-is-codec"
REPAIR_EXECUTABLE="../../GCIS/external/repair/build/src"
GCX_PATH="../GCX/gcx/"
GCX_MAIN_EXEC_PATH="$(pwd)/gcx_output"
GC_STAR_PATH="../GCX/gc_/"
GC_STAR_MAIN_EXEC_PATH="$(pwd)/gc_star_output"

compress_and_decompress_with_gcis() {
	CODEC=$1
	PLAIN=$2
	REPORT=$3
	FILE_NAME=$4
	OUTPUT="$COMP_DIR/$CURR_DATE/$FILE_NAME"
	echo -n "$FILE_NAME|GCIS-${CODEC}|" >> $report
	"$GCIS_EXECUTABLE" -c "$PLAIN" "$OUTPUT-gcis-$CODEC" "-$CODEC" "$REPORT"
	"$GCIS_EXECUTABLE" -d "$OUTPUT-gcis-$CODEC" "$OUTPUT-gcis-$CODEC-plain" "-$CODEC" "$REPORT"
	echo "$(stat $stat_options $OUTPUT-gcis-$CODEC)|$5" >> $REPORT

	checks_equality "$PLAIN" "$OUTPUT-gcis-$CODEC-plain" "gcis"
}

compress_and_decompress_with_repair() {
	FILE=$1
	REPORT=$2
	FILE_NAME=$3
	OUTPUT="$COMP_DIR/$CURR_DATE/$FILE_NAME"
	echo -n "$FILE_NAME|REPAIR|" >> $report
	"${REPAIR_EXECUTABLE}/./repair" -i "$FILE" "$REPORT"
	"${REPAIR_EXECUTABLE}/./despair" -i "${FILE}" "$REPORT"
	size_prel=$(stat $stat_options $FILE.prel)
	size_seq=$(stat $stat_options $FILE.seq)
	size=$((size_prel + size_seq))
	echo "Size prel $size_prel , size seq $size_seq"
	echo "$size|$4" >> $REPORT


	checks_equality "$FILE" "$FILE.u" "gcis"
}

compress_and_decompress_with_gcx() {
	echo -e "\n${GREEN}%%% REPORT: Compresses the files, decompresses them, and compares the result with the original version${RESET}."

	make clean -C $GCX_PATH
	make compile MACROS="REPORT=1" -C $GCX_PATH OUTPUT=$GCX_MAIN_EXEC_PATH

	make clean -C $GC_STAR_PATH
	make compile MACROS="REPORT=1" -C $GC_STAR_PATH OUTPUT=$GC_STAR_MAIN_EXEC_PATH

	for file in $files; do
		report="$REPORT_DIR/$CURR_DATE/$file-gcx-encoding.csv"
        grammar_report="$REPORT_DIR/$CURR_DATE/$file-gcx-grammar.csv"
		echo $COMPRESSION_HEADER > $report; 
        echo $HEADER_REPORT_GRAMMAR > $grammar_report;
		plain_file_path="$RAW_FILES_DIR/$file"
		size_plain=$(stat $stat_options $plain_file_path)

		echo -e "\n\t${BLUE}####### FILE: $file ${RESET}"

		#perform compress and decompress with GCX
		echo -e "\n\t\t ${YELLOW}Starting compression/decompression using GCX ${RESET}\n"
		echo -n "$file|GCX|" >> $report
        echo -n "$file|GCX|" >> $grammar_report

		file_out="$COMP_DIR/$CURR_DATE/$file"
		./gcx_output -c $plain_file_path $file_out $report
		./gcx_output -d $file_out.gcx $file_out-plain $report
		checks_equality "$plain_file_path" "$file_out-plain" "gcx"
		echo "$(stat $stat_options $file_out.gcx)|$size_plain" >> $report

		#perform compress and decompress with GC*
		echo -e "\n\t\t ${YELLOW}Starting compression/decompression using GC* ${RESET}\n"
        for cover in "${COVERAGE_LIST[@]}"; do
            echo -e "\n\t${BLUE}####### FILE: $file, COVERAGE: ${cover} ${RESET}"
            echo -n "$file|GC$cover|" >> $report
            echo -n "$file|GC$cover|" >> $grammar_report

            file_out="$COMP_DIR/$CURR_DATE/$file-gc$cover"
            ./gc_star_output $plain_file_path $file_out c $cover $report
            ./gc_star_output $file_out.gcx $file_out-plain d $cover $report
			checks_equality "$plain_file_path" "$file_out-plain" "gcx"

            size_file=$(stat $stat_options $file_out.gcx)
            echo "$size_file|$size_plain" >> $report
        done

		#perform compress and decompress with GCIS
		echo -e "\n\t\t ${YELLOW}Starting compression/decompression using GCIS ${RESET}\n"
		compress_and_decompress_with_gcis "ef" "$plain_file_path" "$report" "$file" "$size_plain"
		compress_and_decompress_with_gcis "s8b" "$plain_file_path" "$report" "$file" "$size_plain"

		#perform compress and decompress with REPAIR
		echo -e "\n\t\t ${YELLOW}Starting compression/decompression using REPAIR ${RESET}\n"
		compress_and_decompress_with_repair "$plain_file_path" "$report" "$file" "$size_plain"
		echo -e "\n\t ${YELLOW}Finishing compression/decompression operations on the $file file. ${RESET}\n"
	done
	make clean -C $GCX_PATH
	make clean -C $GC_STAR_PATH
}

run_extract() {
	make clean -C $GCX_PATH
	make compile MACROS="REPORT=1 FILE_OUTPUT=1" -C $GCX_PATH OUTPUT=$GCX_MAIN_EXEC_PATH

	make clean -C $GC_STAR_PATH
	make compile MACROS="REPORT=1 FILE_OUTPUT=1"  -C $GC_STAR_PATH OUTPUT=$GC_STAR_MAIN_EXEC_PATH


	echo -e "\n${BLUE}####### Extract validation ${RESET}"
	for file in $files; do
		echo -e "\n\t${BLUE}Preparing for extract operation on the $file file. ${RESET}\n"

		plain_file_path="$RAW_FILES_DIR/$file"
		extract_dir="$REPORT_DIR/$CURR_DATE/extract"
		compressed_file="$COMP_DIR/$CURR_DATE/$file"

		report="$REPORT_DIR/$CURR_DATE/$file-gcx-extract.csv"
		echo $EXTRACTION_HEADER > $report;

		echo -e "\n${YELLOW} Starting encode with repair-navarro - $file .${RESET}"
		if [ ! -f "$plain_file_path.C" ]; then
			"../../GCIS/external/repair-navarro/./repair" "$plain_file_path"
		fi

		echo -e "\n${YELLOW} Generating encodes with SLP...${RESET}"
		for encoding in "${EXTRACT_ENCODING[@]}"; do
			if [ ! -f "$plain_file_path-$encoding" ]; then
				"../../ShapedSlp/build/./SlpEncBuild" -i $plain_file_path -o "$plain_file_path-$encoding" -e $encoding -f NavarroRepair
			fi
		done

		
		#generates intervals
		echo -e "\n${YELLOW} Generating search intervals... ${RESET}"
		python3 external/GCIS/scripts/generate_extract_input.py "$plain_file_path" "$extract_dir/$file"

		#perform extracting
		for length in "${STR_LEN[@]}"; do
			query="$extract_dir/${file}.${length}_extract"
			if [ -e $query ]; then
				echo -e "\n${YELLOW} Generating expected responses for searched interval...${RESET}"
				extract_answer="$extract_dir/${file}_${length}_substrings_expected_response.txt"
				python3 scripts/extract.py $plain_file_path $extract_answer $query

				echo -e "\n\t ${YELLOW}Starting extract with GCX - $file - INTERVAL SIZE $length.${RESET}"
				echo -n "$file|GCX|" >> $report
				extract_output="$extract_dir/${file}_${length}_substrings_results.txt"
				./gcx_output -e "$compressed_file.gcx" $extract_output $query $report
				echo "$length" >> $report
				checks_equality "$extract_output" "$extract_answer" "extract"
				rm $extract_output

				echo -e "\n\t ${YELLOW}Starting extract with GC* - INTERVAL SIZE $length.${RESET}"
				for cover in "${COVERAGE_LIST[@]}"; do
					echo -n "$file|GC$cover|" >> $report
					extract_output="$extract_dir/${file}_result_extract_gc${cover}_len${length}.txt"
					./gc_star_output "$compressed_file-gc$cover.gcx" $extract_output e $cover $query $report
					echo "$length" >> $report
					checks_equality "$extract_output" "$extract_answer" "extract"
					rm $extract_output
				done
				rm $extract_answer

				echo -e "\n${YELLOW}Starting extract with GCIS - $file - INTERVAL SIZE $length.${RESET}"
				echo -n "$file|GCIS-ef|" >> $report
				$GCIS_EXECUTABLE -e "$compressed_file-gcis-ef" $query -ef $report
				echo "$length" >> $report

				echo -e "\n${YELLOW} Starting extract with ShapedSlp - $file - INTERVAL SIZE $length.${RESET}"
				for encoding in "${EXTRACT_ENCODING[@]}"; do
					echo -n "$file|$encoding|" >> $report
					"../../ShapedSlp/build/./ExtractBenchmark" --input="$plain_file_path-$encoding" --encoding=$encoding --query_file=$query --file_report_gcx=$report
					echo "$length" >> $report
				done
			else
				echo "Unable to find $query file."
			fi
		done
	done
}

generate_graphs() {
	echo -e "\n\n${GREEN}%%% Starting the generation of the graphs. ${RESET}"

	python3 scripts/graphs/report.py "$REPORT_DIR/$CURR_DATE/*-gcx-encoding" "$REPORT_DIR/$CURR_DATE/graphs" "compress" "en" "report"
	python3 scripts/graphs/report.py "$REPORT_DIR/$CURR_DATE/*-gcx-extract" "$REPORT_DIR/$CURR_DATE/graphs" "extract" "en" "report"
	python3 scripts/graphs/report.py "$REPORT_DIR/$CURR_DATE/*-gcx-grammar" "$REPORT_DIR/$CURR_DATE/graphs" "grammar" "en" "report"

	echo -e "\n\n${GREEN}%%% FINISHED. ${RESET}"
}

if [ "$0" = "$BASH_SOURCE" ]; then
	check_and_create_folder
	download_files
	compress_and_decompress_with_gcx
	run_extract
	#generate_graphs
fi
