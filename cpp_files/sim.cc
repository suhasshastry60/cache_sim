#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <cmath>
#include "sim.h"
#include <cstdlib>
#include <bits/stdc++.h>
#include <vector>
using namespace std;

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

class cache_wrapper {
public:
    int size, assoc, blk_size;
    uint64_t addr_in;
    int tag_out, addr_out, resp_in, cache_num;
    
    // Cache storage: cache[set][way][meta_and_data]
    // meta_and_data: 0 = tag, 1 = metadata {blk_offset, lru, dirty, valid}
    vector<vector<vector<int>>> cache;
    int set_size, way_size;
    int tag_bits, index_bits, blk_offset_bits;
    
    cache_wrapper(int size, int assoc, int blk_size, uint64_t addr_in, 
                  int tag_out, int addr_out, int resp_in, int cache_num) {
        this->size = size;
        this->assoc = assoc;
        this->blk_size = blk_size;
        this->addr_in = addr_in;
        this->tag_out = tag_out;
        this->addr_out = addr_out;
        this->resp_in = resp_in;
        this->cache_num = cache_num;
        
        decode_addr(addr_in, tag_bits, index_bits, blk_offset_bits);
        
        set_size = (1 << index_bits);  // 2^index_bits = number of sets
        way_size = (assoc == 0) ? 1 : assoc;  // if assoc is 0, way is just 0, else 0 to assoc-1
        
        // Initialize 3D vector: cache[set][way][meta_and_data]
        // set: 0 to set_size-1
        // way: 0 if assoc==0, else 0 to assoc-1
        // meta_and_data: 0 = tag, 1 = metadata
        cache.resize(set_size, vector<vector<int>>(way_size, vector<int>(2, 0)));
        
        // Check cache for hit/miss
        check_cache(addr_in);
    }
     
    void decode_addr(uint64_t addr_in, int &tag, int &index_bits, int &blk_offset_bits) {
        if (addr_in <= (1ULL << 32) - 1) {
            int num_sets = size / (assoc * blk_size);
            index_bits = ceil(log2(num_sets));
            blk_offset_bits = ceil(log2(blk_size));
            tag = 32 - index_bits - blk_offset_bits;
        } else {
            cerr << "Error: Address exceeds 32-bit limit. Terminating execution." << endl;
            exit(EXIT_FAILURE);
        }
    }
    
    void check_cache(uint64_t addr) {
        // Extract tag and set index from address
        int set_idx = (addr >> blk_offset_bits) & ((1 << index_bits) - 1);
        int tag_val = (addr >> (blk_offset_bits + index_bits)) & ((1 << tag_bits) - 1);
        
        if (assoc == 0) {
            // Direct mapped: only way 0
            if (cache[set_idx][0][0] == tag_val) {
                int metadata = cache[set_idx][0][1];
                if ((metadata & 1) == 1) {  // Check valid bit (LSB)
                    cache_hit_read();
                } else {
                    cache_miss_read();
                }
            }
        } else {
            // Set associative: check all ways
            for (int w = 0; w < assoc; w++) {
                if (cache[set_idx][w][0] == tag_val) {
                    int metadata = cache[set_idx][w][1];
                    if ((metadata & 1) == 1) {  // Check valid bit (LSB)
                        cache_hit_read();
                    } else {
                        cache_miss_read();
                    }
                }
            }
        }
    }
    
    void cache_hit_read();
    void cache_miss_read();
};


int main (int argc, char *argv[]) {
   FILE *fp;			// File pointer.
   char *trace_file;		// This variable holds the trace file name.
   cache_params_t params;	// Look at the sim.h header file for the definition of struct cache_params_t.
   char rw;			// This variable holds the request's type (read or write) obtained from the trace.
   uint32_t addr;		// This variable holds the request's address obtained from the trace.
				// The header file <inttypes.h> above defines signed and unsigned integers of various sizes in a machine-agnostic way.  "uint32_t" is an unsigned integer of 32 bits.

   // Exit with an error if the number of command-line arguments is incorrect.
   if (argc != 9) {
      printf("Error: Expected 8 command-line arguments but was provided %d.\n", (argc - 1));
      exit(EXIT_FAILURE);
   }
    
   // "atoi()" (included by <stdlib.h>) converts a string (char *) to an integer (int).
   params.BLOCKSIZE = (uint32_t) atoi(argv[1]);
   params.L1_SIZE   = (uint32_t) atoi(argv[2]);
   params.L1_ASSOC  = (uint32_t) atoi(argv[3]);
   params.L2_SIZE   = (uint32_t) atoi(argv[4]);
   params.L2_ASSOC  = (uint32_t) atoi(argv[5]);
   params.PREF_N    = (uint32_t) atoi(argv[6]);
   params.PREF_M    = (uint32_t) atoi(argv[7]);
   trace_file       = argv[8];

   // Open the trace file for reading.
   fp = fopen(trace_file, "r");
   if (fp == (FILE *) NULL) {
      // Exit with an error if file open failed.
      printf("Error: Unable to open file %s\n", trace_file);
      exit(EXIT_FAILURE);
   }
    
   // Print simulator configuration.
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

   // Read requests from the trace file and echo them back.
   while (fscanf(fp, "%c %x\n", &rw, &addr) == 2) {	// Stay in the loop if fscanf() successfully parsed two tokens as specified.
      if (rw == 'r')
         printf("r %x\n", addr);
      else if (rw == 'w')
         printf("w %x\n", addr);
      else {
         printf("Error: Unknown request type %c.\n", rw);
	 exit(EXIT_FAILURE);
      }

      ///////////////////////////////////////////////////////
      // Issue the request to the L1 cache instance here.
      ///////////////////////////////////////////////////////
    }

    return(0);
}
