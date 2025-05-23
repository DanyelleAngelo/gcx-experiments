#include "../external/malloc_count/malloc_count.h"
#include "gcis.hpp"
#include "gcis_eliasfano.hpp"
#include "gcis_eliasfano_no_lcp.hpp"
#include "gcis_gap.hpp"
#include "gcis_s8b.hpp"
#include "gcis_unary.hpp"
#include "sais.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
//to gcx
#include "../external/malloc_count/stack_count.h"
#include <ctime>

using namespace std::chrono;
using timer = std::chrono::high_resolution_clock;

void load_string_from_file(char *&str, char *filename, int_t &n) {
    std::ifstream f(filename, std::ios::binary);
    f.seekg(0, std::ios::end);
    n = f.tellg();
    f.seekg(0, std::ios::beg);
    str = new char[n];
    f.read(str, n);
    f.close();
};

int main(int argc, char *argv[]) {

#ifdef MEM_MONITOR
    mm.event("GC-IS Init");
#endif

    if (argc != 6) { //adiciona 1 argumento para o report do gcx
        std::cerr << "Usage: \n"
                  << argv[0]
                  << " -c <file_to_be_encoded> <output> <codec flag>\n"
                  << argv[0]
                  << " -d <file_to_be_decoded> <output> <codec flag>\n"
                  << argv[0]
                  << " -s <file_to_be_decoded> <output> <codec flag>\n"
                  << argv[0]
                  << " -l <file_to_be_decoded> <output> <codec flag>\n"
                  << argv[0]
                  << " -e <encoded_file> <query file> <codec flag>\n";

        exit(EXIT_FAILURE);
    }

    // Dictionary type
    string codec_flag(argv[4]);
    gcis_interface *d;

    //To GCX
    char * file_dcx = argv[5];
    void* base = stack_count_clear();
    double duration = 0.0;
    clock_t clock_time;

    if (codec_flag == "-s8b") {
        d = new gcis_s8b_pointers;
    } else if (codec_flag == "-ef") {
        d = new gcis_dictionary<gcis_eliasfano_codec>;
    } else {
        cerr << "Invalid CODEC." << endl;
        cerr << "Use -s8b for Simple8b or -ef for Elias-Fano" << endl;
        return 0;
    }

    char *mode = argv[1];

    if (strcmp(mode, "-c") == 0) {
        int_t n;
        char *str;
        load_string_from_file(str, argv[2], n);
        std::ofstream output(argv[3], std::ios::binary);

#ifdef MEM_MONITOR
        mm.event("GC-IS Compress");
#endif

        auto start = timer::now();
        clock_time = clock(); //gcx

        d->encode(str, n);

        clock_time = clock() - clock_time; //gcx
        duration = ((double)clock_time)/CLOCKS_PER_SEC; //gcx

        auto stop = timer::now();

#ifdef MEM_MONITOR
        mm.event("GC-IS Save");
#endif

        cout << "input:\t" << n << " bytes" << endl;
        cout << "output:\t" << d->size_in_bytes() << " bytes" << endl;
        cout << "time: " << (double)duration_cast<seconds>(stop - start).count()
             << " seconds" << endl;

        d->serialize(output);
        output.close();
        delete[] str;
    } else if (strcmp(mode, "-d") == 0) {
        std::ifstream input(argv[2]);
        std::ofstream output(argv[3], std::ios::binary);

#ifdef MEM_MONITOR
        mm.event("GC-IS Load");
#endif

        d->load(input);

#ifdef MEM_MONITOR
        mm.event("GC-IS Decompress");
#endif

        auto start = timer::now();
        char *str;
        int_t n;
        clock_time = clock(); //gcx
        tie(str, n) = d->decode();
        clock_time = clock() - clock_time; //gcx
        duration = ((double)clock_time)/CLOCKS_PER_SEC; //gcx
        auto stop = timer::now();

        cout << "input:\t" << d->size_in_bytes() << " bytes" << endl;
        cout << "output:\t" << strlen(str) << " bytes" << endl;
        cout << "time: "
             << (double)duration_cast<milliseconds>(stop - start).count() /
                    1000.0
             << setprecision(2) << fixed << " seconds" << endl;

        output.write(str, n);
        input.close();
        output.close();
    } else if (strcmp(mode, "-s") == 0) {

        std::ifstream input(argv[2]);
        string outfile1 = argv[3];
        string outfile2 = outfile1 + ".sa";
        std::ofstream output1(outfile1, std::ios::binary);
        std::ofstream output2(outfile2, std::ios::binary);

#ifdef MEM_MONITOR
        mm.event("GC-IS/SACA Load");
#endif

        d->load(input);

#ifdef MEM_MONITOR
        mm.event("GC-IS/SACA Decompress");
#endif

        uint_t *SA;
        std::cout << "Building SA under decoding." << std::endl;
        auto start = timer::now();
        char *str;
        int_t n;
        tie(str, n) = d->decode_saca(&SA);
        auto stop = timer::now();

#if CHECK
        if (!d->suffix_array_check(SA, (unsigned char *)str, (uint_t)n,
                                   sizeof(char), 0))
            std::cout << "isNotSorted!!\n";
        else
            std::cout << "isSorted!!\n";
#endif

        cout << "input:\t" << d->size_in_bytes() << " bytes" << endl;
        cout << "output:\t" << n << " bytes" << endl;
        cout << "SA:\t" << n * sizeof(uint_t) << " bytes" << endl;
        std::cout << "time: "
                  << (double)duration_cast<seconds>(stop - start).count()
                  << " seconds" << endl;

        output1.write((const char *)&n, sizeof(n));
        output1.write((const char *)str, (n) * sizeof(char));
        output2.write((const char *)&n, sizeof(n));
        output2.write((const char *)SA, sizeof(uint_t) * n);
        for (int i = 0; i < n; i++) {
            cout << "SA[" << i << "] = " << SA[i] << endl;
        }
        output1.close();
        output2.close();
        // input.close();
        delete[] SA;
    } else if (strcmp(mode, "-l") == 0) {

        std::ifstream input(argv[2]);

#ifdef MEM_MONITOR
        mm.event("GC-IS/SACA+LCP Load");
#endif

        d->load(input);

#ifdef MEM_MONITOR
        mm.event("GC-IS/SACA_LCP Decompress");
#endif

        uint_t *SA;
        int_t *LCP;
        std::cout << "Building SA+LCP under decoding." << std::endl;
        auto start = timer::now();
        int_t n = 0;
        char *str = nullptr;
        tie(str, n) = d->decode_saca_lcp(&SA, &LCP);
        auto stop = timer::now();

#if CHECK
        if (!d->suffix_array_check(SA, (unsigned char *)str, (uint_t)n,
                                   sizeof(char), 0))
            std::cout << "isNotSorted!!\n";
        else
            std::cout << "isSorted!!\n";
        if (!d->lcp_array_check(SA, LCP, (unsigned char *)str, (uint_t)n,
                                sizeof(char), 0))
            std::cout << "isNotLCP!!\n";
        else
            std::cout << "isLCP!!\n";
#endif

        cout << "input:\t" << d->size_in_bytes() << " bytes" << endl;
        cout << "output:\t" << n << " bytes" << endl;
        cout << "SA:\t" << n * sizeof(uint_t) << " bytes" << endl;
        cout << "LCP:\t" << n * sizeof(uint_t) << " bytes" << endl;
        std::cout << "time: "
                  << (double)duration_cast<seconds>(stop - start).count()
                  << " seconds" << endl;

        string ouf_basename(argv[3]);
        string outfile1(ouf_basename + ".txt");
        string outfile2 = ouf_basename + ".sa";
        string outfile3 = ouf_basename + ".lcp";
        std::ofstream output1(outfile1, std::ios::binary);
        std::ofstream output2(outfile2, std::ios::binary);
        std::ofstream output3(outfile3, std::ios::binary);

        output1.write((const char *)str, (n) * sizeof(char));
        output2.write((const char *)&n, sizeof(n));
        output2.write((const char *)SA, sizeof(n) * n);
        output3.write((const char *)&n, sizeof(n));
        output3.write((const char *)LCP, sizeof(n) * n);
        for (int i = 0; i < n; i++) {
            cout << "SA[" << i << "] = " << SA[i] << endl;
        }
        for (int i = 0; i < n; i++) {
            cout << "LCP[" << i << "] = " << LCP[i] << endl;
        }

        output1.close();
        output2.close();
        output3.close();
        input.close();
        delete[] SA;
        delete[] LCP;
    } else if (strcmp(mode, "-e") == 0) {
        std::ifstream input(argv[2], std::ios::binary);
        std::ifstream query(argv[3]);

#ifdef MEM_MONITOR
        mm.event("GC-IS Load");
#endif

        d->load(input);

#ifdef MEM_MONITOR
        mm.event("GC-IS Extract");
#endif
        vector<pair<int, int>> v_query;
        uint64_t l, r;
        while (query >> l >> r) {
            v_query.push_back(make_pair(l, r));
        }
        duration = d->extract_batch(v_query); //duration to gcx
    } else {
        std::cerr << "Invalid mode, use: " << endl
                  << "-c for compression;" << endl
                  << "-d for decompression;" << endl
                  << "-e for extraction;" << endl
                  << "-s for building SA under decompression" << endl
                  << "-l for building SA+LCP under decompression" << endl;

        exit(EXIT_FAILURE);
    }

#ifdef MEM_MONITOR
    mm.event("GC-IS Finish");
#endif

    //To GCX
    printf("Gerando relatório para o GCX\n");
    FILE *report_dcx = fopen(file_dcx, "a");
    if(report_dcx == NULL) {
	printf("Erro ao abrir arquivo de relatório %s\n",file_dcx);
	exit(1);
    }
    long long int peak = malloc_count_peak();
    long long int stack = stack_count_usage(base);
    fprintf(report_dcx, "%lld|%lld|%5.4lf|", peak,stack,duration);
    printf("Time inserted into the GCX report: %5.4lf\n", duration);
    fclose(report_dcx);

    return 0;
}
