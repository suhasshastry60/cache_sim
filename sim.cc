#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "sim.h"

/*  "argc" holds the number of command-line arguments.
    "argv[]" holds the arguments themselves.

    Example:
    ./sim 32 8192 4 262144 8 3 10 gcc_trace.txt
    argc = 9
    argv[0] = "./sim"
    argv[1] = "32"
    argv[2] = "8192"
    ... and so on
*/

cache_wrapper::cache_wrapper(int size, int assoc, int blk_size, uint32_t addr_in,
                  int tag_out, int addr_out, int resp_in, int cache_num,
              char operation, int pref_n, int pref_m, cache_wrapper* next) {
        this->size = size;
        this->assoc = assoc;
        this->blk_size = blk_size;
        this->addr_in = addr_in;
        this->tag_out = tag_out;
        this->addr_out = addr_out;
        this->resp_in = resp_in;
        this->cache_num = cache_num;
        this->operation = operation;
        this->resp_out = 0;
    this->next_cache = next;
    this->PREF_N = pref_n;
    this->PREF_M = pref_m;

        tag_bits = 0;
        index_bits = 0;
        blk_offset_bits = 0;

        decode_addr(addr_in, tag_bits, index_bits, blk_offset_bits);

    lru_bits = (assoc > 1) ? (int)ceil(log2((double)assoc)) : 0;
        metadata_width = 2 + lru_bits + blk_offset_bits;

        if (index_bits < 0 || index_bits > 30) {
            cerr << "Error: index_bits out of range: " << index_bits << endl;
            exit(EXIT_FAILURE);
        }

        set_size = (index_bits == 0) ? 1 : (1 << index_bits);
        way_size = (assoc == 1) ? 1 : assoc;

        if (set_size <= 0 || way_size <= 0) {
            cerr << "Error: invalid set_size/way_size computed\n";
            exit(EXIT_FAILURE);
        }

        cache.resize(set_size, vector<vector<uint32_t>>(way_size, vector<uint32_t>(2, 0)));

    // Initialize stream buffer if prefetcher is enabled
    if (PREF_N > 0 && PREF_M > 0) {
        stream_buffer.resize(PREF_N, vector<uint32_t>(PREF_M, UINT32_MAX));
        sb_params.resize(PREF_N);
        for (int i = 0; i < PREF_N; i++) {
            sb_params[i].valid = 0;
            sb_params[i].lru = 0;
        }
    }
}

class cache_interface {
public:
    cache_wrapper* L1;
    cache_wrapper* L2;

    cache_interface(cache_params_t params) {
        //fyi, if there is L2, prefetcher is in L2 and fetches it from memory, else add a prefetcher in L1 and fetch it from memory
        if (params.L2_SIZE > 0) { //also instantiate the prefetcher in L2
            L2 = new cache_wrapper(params.L2_SIZE, params.L2_ASSOC, params.BLOCKSIZE,
                                   0, 0, 0, 0, 2, 'r', params.PREF_N, params.PREF_M, nullptr);
            L1 = new cache_wrapper(params.L1_SIZE, params.L1_ASSOC, params.BLOCKSIZE,
                                   0, 0, 0, 0, 1, 'r', 0, 0, L2);
        } else {
            L2 = nullptr; //instantiate prefetcher in L1
            L1 = new cache_wrapper(params.L1_SIZE, params.L1_ASSOC, params.BLOCKSIZE,
                                   0, 0, 0, 0, 1, 'r', params.PREF_N, params.PREF_M, nullptr);
        }
    }

    ~cache_interface() {
        delete L1;
        if (L2 != nullptr) delete L2;
    }
};

int main(int argc, char *argv[]) {
    FILE *fp;
    char *trace_file;
    cache_params_t params;
    char rw;
    uint32_t addr;

    if (argc != 9) {
        printf("Error: Expected 8 command-line arguments but was provided %d.\n", (argc - 1));
        exit(EXIT_FAILURE);
    }

    params.BLOCKSIZE = (uint32_t) atoi(argv[1]);
    params.L1_SIZE   = (uint32_t) atoi(argv[2]);
    params.L1_ASSOC  = (uint32_t) atoi(argv[3]);
    params.L2_SIZE   = (uint32_t) atoi(argv[4]);
    params.L2_ASSOC  = (uint32_t) atoi(argv[5]);
    params.PREF_N    = (uint32_t) atoi(argv[6]);
    params.PREF_M    = (uint32_t) atoi(argv[7]);
    trace_file       = argv[8];

    fp = fopen(trace_file, "r");
    if (fp == (FILE *) NULL) {
        printf("Error: Unable to open file %s\n", trace_file);
        exit(EXIT_FAILURE);
    }

    printf("===== Simulator configuration =====\n");
    printf("BLOCKSIZE:  %u\n", params.BLOCKSIZE);
    printf("L1_SIZE:    %u\n", params.L1_SIZE);
    printf("L1_ASSOC:   %u\n", params.L1_ASSOC);
    printf("L2_SIZE:    %u\n", params.L2_SIZE);
    printf("L2_ASSOC:   %u\n", params.L2_ASSOC);
    printf("PREF_N:     %u\n", params.PREF_N);
    printf("PREF_M:     %u\n", params.PREF_M);
    printf("trace_file: %s\n", trace_file);
    printf("\n");

    // Create cache interface
    cache_interface* cache_sys = new cache_interface(params);

    // Read requests from the trace file and echo them back.
    int resp;
while (fscanf(fp, " %c %x", &rw, &addr) == 2) {
    if (rw == 'r') {
        cache_sys->L1->cache_read(addr, resp);
    } else if (rw == 'w') {
        cache_sys->L1->cache_write(addr, resp);
    } else {
        printf("Error: Unknown request type %c.\n", rw);
        exit(EXIT_FAILURE);
    }
}

    // Print cache contents with fixed formatting
    cache_sys->L1->print_contents();
    if (cache_sys->L2 != nullptr) {
        cache_sys->L2->print_contents();
    }
    
    // Print stream buffer contents (from the cache that has the prefetcher)
    if (cache_sys->L2 != nullptr) {
        cache_sys->L2->print_stream_buffers();
    } else {
        cache_sys->L1->print_stream_buffers();
    }

    cout << "\n===== Measurements =====\n";
    auto L1 = cache_sys->L1;
    auto L2 = cache_sys->L2;

    double l1_miss_rate = (L1->reads + L1->writes > 0) ?
        static_cast<double>(L1->read_misses + L1->write_misses) / (L1->reads + L1->writes) : 0.0;

    double l2_miss_rate = (L2 && L2->reads > 0) ?
        static_cast<double>(L2->read_misses) / L2->reads : 0.0;

    uint32_t mem_traffic = (L2) ?
        L2->read_misses + L2->write_misses + L2->writebacks + L2->prefetches :
        L1->read_misses + L1->write_misses + L1->writebacks + L1->prefetches;

    printf("a. L1 reads:                   %u\n", L1->reads);
    printf("b. L1 read misses:             %u\n", L1->read_misses);
    printf("c. L1 writes:                  %u\n", L1->writes);
    printf("d. L1 write misses:            %u\n", L1->write_misses);
    printf("e. L1 miss rate:               %.4f\n", l1_miss_rate);
    printf("f. L1 writebacks:              %u\n", L1->writebacks);
    printf("g. L1 prefetches:              %u\n", L1->prefetches);
    printf("h. L2 reads (demand):          %u\n", L2 ? L2->reads : 0);
    printf("i. L2 read misses (demand):    %u\n", L2 ? L2->read_misses : 0);
    printf("j. L2 reads (prefetch):        %d\n", 0);
    printf("k. L2 read misses (prefetch):  %d\n", 0);
    printf("l. L2 writes:                  %u\n", L2 ? L2->writes : 0);
    printf("m. L2 write misses:            %u\n", L2 ? L2->write_misses : 0);
    printf("n. L2 miss rate:               %.4f\n", l2_miss_rate);
    printf("o. L2 writebacks:              %u\n", L2 ? L2->writebacks : 0);
    printf("p. L2 prefetches:              %u\n", L2 ? L2->prefetches : 0);
    printf("q. memory traffic:             %u\n", mem_traffic);

    delete cache_sys;
    fclose(fp);

    return 0;
}
