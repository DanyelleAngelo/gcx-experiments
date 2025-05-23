#!/bin/bash
source utils.sh

readonly LCP_WINDOW=(2 4 8 16 32)
readonly COVERAGE_LIST=(2 4 8 16 32 64 128)
readonly STR_LEN=(1 10 100 1000 10000)
readonly EXTRACT_ENCODING=("PlainSlp_32Fblc"  "PlainSlp_FblcFblc")

#cabeçalhos
readonly COMPRESSION_HEADER="file|algorithm|peak_comp|stack_comp|compression_time|peak_decomp|stack_decomp|decompression_time|compressed_size|plain_size"
readonly EXTRACTION_HEADER="file|algorithm|peak|stack|time|substring_size"
readonly HEADER_REPORT_GRAMMAR="file|algorithm|nLevels|xs_size|level_cover_qtyRules|compressed_size|plain_size"

# paths
readonly GCIS_EXECUTABLE="external/GCIS/build/src/./gcis"
readonly REPAIR_EXECUTABLE="external/GCIS/external/repair-navarro"
readonly GLZA_EXECUTABLE="external/GLZA"
readonly GCX_PATH="../GCX/gcx/"
readonly GCX_MAIN_EXEC_PATH="$(pwd)/gcx_output"
readonly GC_STAR_PATH="../GCX/gc_/"
readonly GC_STAR_MAIN_EXEC_PATH="$(pwd)/gc_star_output"


compress_and_decompress_with_gcis() {
	echo -e "\n\t\t ${YELLOW}Starting compression/decompression using GCIS-$1 ${RESET}\n"
	CODEC=$1
	PLAIN=$2
	REPORT=$3
	FILE_NAME=$4
	OUTPUT="$COMP_DIR/$CURR_DATE/$FILE_NAME"
	echo -n "$FILE_NAME|GCIS-${CODEC}|" >> $report
	echo -e "${GREEN}Comprimindo arquivo...${RESET}\n"
	"$GCIS_EXECUTABLE" -c "$PLAIN" "$OUTPUT-gcis-$CODEC" "-$CODEC" "$REPORT"
	echo -e "${GREEN}Descomprimindo arquivo.. ${RESET}\n."
	"$GCIS_EXECUTABLE" -d "$OUTPUT-gcis-$CODEC" "$OUTPUT-gcis-$CODEC-plain" "-$CODEC" "$REPORT"
	echo "$(stat $stat_options $OUTPUT-gcis-$CODEC)|$5" >> $REPORT

	checks_equality "$PLAIN" "$OUTPUT-gcis-$CODEC-plain" "gcis"
	echo -e "\n\t ${YELLOW}Finishing compression/decompression operations on the $FILE file using GCIS-$CODEC. ${RESET}\n"
}

compress_and_decompress_with_repair() {
	echo -e "\n\t\t ${YELLOW}Starting compression/decompression using REPAIR ${RESET}\n"
	FILE=$1
	REPORT=$2
	FILE_NAME=$3
	OUTPUT="$COMP_DIR/$CURR_DATE/$FILE_NAME"
	size_plain=$4
	cp $FILE "$FILE-repair" #faz uma cópia do arquivo, para não ter sobrescrita do original ao descompactar
	
	"${REPAIR_EXECUTABLE}/./repair-navarro" "$FILE-repair" "$REPORT"
	"${REPAIR_EXECUTABLE}/./despair-navarro" "$FILE-repair" "$REPORT"
	checks_equality "$FILE" "$FILE-repair" "repair"

	repair_report_entry=$(tail -n 1 "$REPORT") #pega a linha gravada pelo repair
	sed -i '$d' "$REPORT" #apaga a linha recém gravada

	size_c=$(stat $stat_options $FILE-repair.C)
	size_r=$(stat $stat_options $FILE-repair.R)

	#grava as taxas de compressão considerando a codificação SLP
	echo -e "\n${YELLOW} Generating encodes with SLP...${RESET}"
	for encoding in "${EXTRACT_ENCODING[@]}"; do
		"external/ShapeSlp/build/./SlpEncBuild" -i "$FILE-repair" -o "$FILE-$encoding" -e $encoding -f NavarroRepair
		size_slp=$(stat $stat_options $FILE-$encoding)
		size=$((size_c + size_r + size_slp))

		echo -n "$FILE_NAME|REPAIR-$encoding|" >> $report
		echo "${repair_report_entry}$size|$size_plain" >> "$REPORT"

		echo -e "\nTamanhos: Size C $size_c , size R $size_r, size SLP $size_slp, total: $size\n\n"
	done

	echo -e "\n\t ${YELLOW}Finishing compression/decompression operations on the $FILE file using RePair. ${RESET}\n"
}

compress_and_decompress_with_7zip() {
	file=$1
	plain_file_path=$2
	report=$3
	size_plain=$4
	compressed_file="$COMP_DIR/$CURR_DATE/$file"
	decompressed_file="$COMP_DIR/$CURR_DATE"

	SETE_ZIP_EXECUTABLE="external/7zip/CPP/7zip/Bundles/Alone2/_o/./7zz"

	echo -e "\n\t\t ${YELLOW}Starting compression using 7zip ${RESET}\n"
	echo -n "$file|7zip|" >> $report
	
	cd $RAW_FILES_DIR # é necessário alterar a pasta, para ele não inserir o path do arquivo
	"../../$SETE_ZIP_EXECUTABLE" a "../../$compressed_file.7z" "$file" -gcx_report="../../$report"
	cd ../../
	echo -e "\n\t\t ${YELLOW}Starting decompression using 7zip ${RESET}\n"
	"$SETE_ZIP_EXECUTABLE" x $compressed_file.7z -o$decompressed_file -gcx_report="$report"
	
	size=$(stat $stat_options $compressed_file.7z)
	echo "$size|$size_plain" >> $report

	checks_equality "$plain_file_path" $decompressed_file/$file "7zip"
	rm $decompressed_file/$file

	echo -e "\n\t ${YELLOW}Finishing compression/decompression operations on the $FILE file using 7zip. ${RESET}\n"
}

compress_and_decompress_with_bzip2() {
	file=$1
	report=$2
	file_name=$3
	output="$COMP_DIR/$CURR_DATE/$file_name"
	size_plain=$4

	cp $file $output #faz uma cópia do arquivo, para não ter sobrescrita do original ao descompactar
	echo -n "$file_name|bzip2|" >> $report
	BZIP2_EXECUTABLE="external/bzip2/build/./bzip2"

	echo -e "\n\t\t ${YELLOW}Starting compression using Bzip2 ${RESET}\n"
	"$BZIP2_EXECUTABLE" -f $output --gcx_report="$report"
	
	echo -e "\n\t\t ${YELLOW}Starting decompression using Bzip2 ${RESET}\n"
	"$BZIP2_EXECUTABLE" -d -kf $output.bz2 --gcx_report="$report"
	
	size=$(stat $stat_options $output.bz2)
	echo "$size|$size_plain" >> $report

	checks_equality "$plain_file_path" $output "bz2"

	echo -e "\n\t ${YELLOW}Finishing compression/decompression operations on the $file file using Bzip2. ${RESET}\n"
}

compress_and_decompress_with_gcx() {
	plain_file_path=$1
	report=$2
	file=$3
	size_plain=$4

    grammar_report="$REPORT_DIR/$CURR_DATE/$file-gcx-grammar.csv"
    echo $HEADER_REPORT_GRAMMAR > $grammar_report;
	
	#perform compress and decompress with GCX
	echo -e "\n\t\t ${YELLOW}Starting compression/decompression using GCX ${RESET}\n"
	for cover in "${LCP_WINDOW[@]}"; do
		echo -e "\tUsing initial window of size $cover for LCP calculation.\n"
		echo -n "$file|GCX-y$cover|" >> $report
		echo -n "$file|GCX-y$cover|" >> $grammar_report

		file_out="$COMP_DIR/$CURR_DATE/$file-y$cover"
		./gcx_output -c $plain_file_path $file_out $report $cover
		./gcx_output -d $file_out.gcx $file_out-plain $report
		checks_equality "$plain_file_path" "$file_out-plain" "gcx"
		
		compressed_size=$(stat $stat_options $file_out.gcx)
		echo "$compressed_size|$size_plain" >> $report
		echo "|$compressed_size|$size_plain" >> $grammar_report
	done

	#perform compress and decompress with GC*
	echo -e "\n\t\t ${YELLOW}Starting compression/decompression using GC* ${RESET}\n"
	for cover in "${COVERAGE_LIST[@]}"; do
		echo -e "\n\t${BLUE}####### FILE: $file, COVERAGE: ${cover} ${RESET}"
		echo -n "$file|GC$cover|" >> $report
		echo -n "$file|GC$cover|" >> $grammar_report

		file_out="$COMP_DIR/$CURR_DATE/$file-gc$cover"
		./gc_star_output -c $plain_file_path $file_out $cover $report
		./gc_star_output -d $file_out.gcx $file_out-plain $cover $report
		checks_equality "$plain_file_path" "$file_out-plain" "gcx"

		size_file=$(stat $stat_options $file_out.gcx)
		echo "$size_file|$size_plain" >> $report
	done
}

evaluate_compression_performance() {
	echo -e "\n${GREEN}%%% REPORT: Compresses the files, decompresses them, and compares the result with the original version${RESET}."

	build_tools
	for file in $files; do
		report="$REPORT_DIR/$CURR_DATE/$file-gcx-encoding.csv"
		echo $COMPRESSION_HEADER > $report;
		plain_file_path="$RAW_FILES_DIR/$file"
		size_plain=$(stat $stat_options $plain_file_path)

		echo -e "\n\t${BLUE}####### FILE: $file ${RESET}"

		#perform compress and decompress with GCX and GC*
		compress_and_decompress_with_gcx "$plain_file_path" "$report" "$file" "$size_plain"

		#perform compress and decompress with GCIS
		compress_and_decompress_with_gcis "ef" "$plain_file_path" "$report" "$file" "$size_plain"
		compress_and_decompress_with_gcis "s8b" "$plain_file_path" "$report" "$file" "$size_plain"

		#perform compress and decompress with REPAIR
		compress_and_decompress_with_repair "$plain_file_path" "$report" "$file" "$size_plain"

		#perform compress and decompress with 7zip
		compress_and_decompress_with_7zip $file $plain_file_path $report $size_plain

		#perform compress and decompress with bzip2
		compress_and_decompress_with_bzip2 "$plain_file_path" "$report" "$file" "$size_plain"
	done
	clean_tools
}


run_extract() {
	build_tools

	echo -e "\n${BLUE}####### Extract validation ${RESET}"
	for file in $files; do
		echo -e "\n\t${BLUE}Preparing for extract operation on the $file file. ${RESET}\n"

		plain_file_path="$RAW_FILES_DIR/$file"
		extract_dir="$REPORT_DIR/$CURR_DATE/extract"
		compressed_file="$COMP_DIR/$CURR_DATE/$file"

		report="$REPORT_DIR/$CURR_DATE/$file-gcx-extract.csv"
		echo $EXTRACTION_HEADER > $report;

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

				#perform extract with GCX
				for cover in "${LCP_WINDOW[@]}"; do
					echo -e "\n\t ${YELLOW}Starting extract with GCX - $file - INTERVAL SIZE $length.${RESET}"
					echo -e "\tUsing initial window of size $cover for LCP calculation.\n"
					echo -n "$file|GCX-y$cover|" >> $report
					extract_output="$extract_dir/${file}_${length}_substrings_results.txt"
					./gcx_output -e "$compressed_file-y$cover.gcx" $extract_output $query $report
					echo "$length" >> $report
					checks_equality "$extract_output" "$extract_answer" "extract"
					rm $extract_output
				done

				#perform extract with GC*
				echo -e "\n\t ${YELLOW}Starting extract with GC* - INTERVAL SIZE $length.${RESET}"
				for cover in "${COVERAGE_LIST[@]}"; do
					echo -n "$file|GC$cover|" >> $report
					extract_output="$extract_dir/${file}_result_extract_gc${cover}_len${length}.txt"
					./gc_star_output -e "$compressed_file-gc$cover.gcx" $extract_output $cover $query $report
					echo "$length" >> $report
					checks_equality "$extract_output" "$extract_answer" "extract"
					rm $extract_output
				done
				rm $extract_answer

				#perform extract with GCIS
				echo -e "\n${YELLOW}Starting extract with GCIS - $file - INTERVAL SIZE $length.${RESET}"
				echo -n "$file|GCIS-ef|" >> $report
				$GCIS_EXECUTABLE -e "$compressed_file-gcis-ef" $query -ef $report
				echo "$length" >> $report

				#perform extract with RePair
				echo -e "\n${YELLOW} Starting extract with ShapedSlp - $file - INTERVAL SIZE $length.${RESET}"
				for encoding in "${EXTRACT_ENCODING[@]}"; do
					echo -n "$file|$encoding|" >> $report
					"external/ShapeSlp/build/./ExtractBenchmark" --input="$plain_file_path-$encoding" --encoding=$encoding --query_file=$query --file_report_gcx=$report
					echo "$length" >> $report
				done
			else
				echo "Unable to find $query file."
			fi
		done
	done
	clean_tools
}

generate_graphs() {
	echo -e "\n\n${GREEN}%%% Starting the generation of the graphs. ${RESET}"

	python3 scripts/graphs/report.py "$REPORT_DIR/$CURR_DATE/*-gcx-encoding" "$REPORT_DIR/$CURR_DATE" "compress" "en" "report"
	#python3 scripts/graphs/report.py "$REPORT_DIR/$CURR_DATE/*-gcx-extract" "$REPORT_DIR/$CURR_DATE" "extract" "en" "report"
	#python3 scripts/graphsE/report.py "$REPORT_DIR/$CURR_DATE/*-gcx-grammar" "$REPORT_DIR/$CURR_DAT" "grammar" "en" "report"

	echo -e "\n\n${GREEN}%%% FINISHED. ${RESET}"
}

build_tools() {
    make clean -C "$GCX_PATH" OUTPUT="$GCX_MAIN_EXEC_PATH"
    make compile -C "$GCX_PATH" MACROS="REPORT=1 FILE_OUTPUT=1" OUTPUT="$GCX_MAIN_EXEC_PATH"
    
    make clean -C "$GC_STAR_PATH" OUTPUT="$GC_STAR_MAIN_EXEC_PATH"
    make compile -C "$GC_STAR_PATH" MACROS="REPORT=1 FILE_OUTPUT=1" OUTPUT="$GC_STAR_MAIN_EXEC_PATH"
}

clean_tools() {
    make clean -C "$GCX_PATH" OUTPUT="$GCX_MAIN_EXEC_PATH"
    make clean -C "$GC_STAR_PATH" OUTPUT="$GC_STAR_MAIN_EXEC_PATH"
}

if [ "$0" = "$BASH_SOURCE" ]; then
	check_and_create_folder
	download_files
	evaluate_compression_performance
	run_extract
	generate_graphs
fi
