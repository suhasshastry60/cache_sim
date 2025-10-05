// #include <stdio.h>
// #include <stdlib.h>
// #include <inttypes.h>
// #include <cmath>
// #include "sim.h"
// #include <cstdlib>
// #include <bits/stdc++.h>
// #include <vector>
// using namespace std;

// class cache_wrapper {
// public:
//     int size, assoc, blk_size;
//     uint64_t addr_in;
//     int tag_out, addr_out, resp_in, resp_out, cache_num;
//     char operation;  // Changed to char to hold 'r' or 'w'
    
//     // Cache storage: cache[set][way][meta_and_data]
//     // meta_and_data: 0 = tag, 1 = metadata {blk_offset, lru, dirty, valid}
//     vector<vector<vector<int>>> cache;
//     int set_size, way_size;
//     int tag_bits, index_bits, blk_offset_bits;
    
//     cache_wrapper(int size, int assoc, int blk_size, uint64_t addr_in, 
//                   int tag_out, int addr_out, int resp_in, int cache_num, char operation) {  // Added operation parameter
//         this->size = size;
//         this->assoc = assoc;
//         this->blk_size = blk_size;
//         this->addr_in = addr_in;
//         this->tag_out = tag_out;
//         this->addr_out = addr_out;
//         this->resp_in = resp_in;
//         this->cache_num = cache_num;
//         this->operation = operation;  // Store operation
        
//         decode_addr(addr_in, tag_bits, index_bits, blk_offset_bits);
        
//         set_size = (1 << index_bits);  // 2^index_bits = number of sets
//         way_size = (assoc == 1) ? 1 : assoc;  // if assoc is 1, way is just 0, else 0 to assoc-1
        
//         // Initialize 3D vector: cache[set][way][meta_and_data]
//         // set: 0 to set_size-1
//         // way: 0 if assoc==1, else 0 to assoc-1
//         // meta_and_data: 0 = tag, 1 = metadata
//         cache.resize(set_size, vector<vector<int>>(way_size, vector<int>(2, 0)));
        
//         // Check cache for hit/miss based on operation
//         if (operation == 'r') {  // Use single quotes for char
//             cache_read(addr_in);
//         } else if (operation == 'w') {  // Added else if for clarity
//             cache_write(addr_in);
//         }
//     }
     
//     void decode_addr(uint64_t addr_in, int &tag, int &index_bits, int &blk_offset_bits) {
//         if (addr_in <= (1ULL << 32) - 1) {
//             int num_sets = size / (assoc * blk_size);
//             index_bits = ceil(log2(num_sets));
//             blk_offset_bits = ceil(log2(blk_size));
//             tag = 32 - index_bits - blk_offset_bits;
//         } else {
//             cerr << "Error: Address exceeds 32-bit limit. Terminating execution." << endl;
//             exit(EXIT_FAILURE);
//         }
//     }
    
//     void cache_read(uint64_t addr, int &resp_out) {  // Output parameter using reference
//         // Extract tag and set index from address
//         int set_idx = (addr >> blk_offset_bits) & ((1 << index_bits) - 1);
//         int tag_val = (addr >> (blk_offset_bits + index_bits)) & ((1 << tag_bits) - 1);
        
//         if (assoc == 1) {
//             // Direct mapped: only way 0
//             if (cache[set_idx][0][0] == tag_val) {
//                 int metadata = cache[set_idx][0][1];
//                 int hit_tag  = cache[set_idx][0][0];
//                 if ((metadata & 1) == 1) {  // Check valid bit (LSB)
//                     cache_hit_read(hit_tag, resp_out);
//                 } else {
//                     addr_out = addr_in;
//                     cache_miss_read(resp_out);
//                 }
//             }
//         } else if (assoc > 1) {
//             // Set associative: check all ways
//             for (int w = 0; w < assoc; w++) {
//                 if (cache[set_idx][w][0] == tag_val) {
//                     int metadata = cache[set_idx][w][1];
//                     int hit_tag  = cache[set_idx][w][0];
//                     if ((metadata & 1) == 1) {  // Check valid bit (LSB)
//                         cache_hit_read(hit_tag, resp_out);
//                     } else {
//                         addr_out = addr_in;
//                         cache_miss_read(resp_out);
//                     }
//                 }
//             }
//         }
//     }
// void cache_hit_read(int hit_tag, int &resp_out) {  // Added int & for reference parameter
//     if (cache_num == 1) {
//         resp_out = 1;  // Set response output to 1 (hit)
//         return;
//     } else if (cache_num > 1) {
//         resp_out = hit_tag;
//         return;
//     }
// }
    
//     void cache_miss_read();
//     void cache_write(uint64_t addr);  // Added declaration for cache_write
// };



#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <cmath>
#include "sim.h"
#include <cstdlib>
#include <bits/stdc++.h>
#include <vector>
using namespace std;

class cache_wrapper {
public:
    int size, assoc, blk_size;
    uint64_t addr_in;
    int tag_out, addr_out, resp_in, resp_out, cache_num;
    char operation;
    
    // Cache storage: cache[set][way][meta_and_data]
    // meta_and_data: 0 = tag, 1 = metadata {blk_offset, lru, dirty, valid}
    vector<vector<vector<int>>> cache;
    int set_size, way_size;
    int tag_bits, index_bits, blk_offset_bits;
    
    cache_wrapper(int size, int assoc, int blk_size, uint64_t addr_in, 
                  int tag_out, int addr_out, int resp_in, int cache_num, char operation) {
        this->size = size;
        this->assoc = assoc;
        this->blk_size = blk_size;
        this->addr_in = addr_in;
        this->tag_out = tag_out;
        this->addr_out = addr_out;
        this->resp_in = resp_in;
        this->cache_num = cache_num;
        this->operation = operation;
        this->resp_out = 0;  // Initialize to miss
        
        decode_addr(addr_in, tag_bits, index_bits, blk_offset_bits);
        
        set_size = (1 << index_bits);  // 2^index_bits = number of sets
        way_size = (assoc == 1) ? 1 : assoc;  // if assoc is 1, way is just 0, else 0 to assoc-1
        
        // Initialize 3D vector: cache[set][way][meta_and_data]
        cache.resize(set_size, vector<vector<int>>(way_size, vector<int>(2, 0)));
        
        // Check cache for hit/miss based on operation
        if (operation == 'r') {
            cache_read(addr_in, this->resp_out);
        } else if (operation == 'w') {
            cache_write(addr_in, this->resp_out);
        }
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
    
    void cache_read(uint64_t addr, int &resp_out) {
        // Extract tag and set index from address
        int set_idx = (addr >> blk_offset_bits) & ((1 << index_bits) - 1);
        int tag_val = (addr >> (blk_offset_bits + index_bits)) & ((1 << tag_bits) - 1);
        
        if (assoc == 1) {
            // Direct mapped: only way 0
            if (cache[set_idx][0][0] == tag_val) {
                int metadata = cache[set_idx][0][1];
                if ((metadata & 1) == 1) {  // Check valid bit (LSB)
                    cache_hit_read(tag_val, resp_out);  // HIT in this level
                } else {
                    // Tag matches but not valid - MISS
                    addr_out = addr_in;  // Send address to next level
                    cache_miss_read(tag_val, resp_out);  // Query next level
                }
            } else {
                // Tag not found - MISS
                addr_out = addr_in;  // Send address to next level
                cache_miss_read(tag_val, resp_out);  // Query next level
            }
        } else if (assoc > 1) {
            // Set associative: check all ways
            bool found = false;
            for (int w = 0; w < assoc; w++) {
                if (cache[set_idx][w][0] == tag_val) {
                    found = true;
                    int metadata = cache[set_idx][w][1];
                    if ((metadata & 1) == 1) {  // Check valid bit (LSB)
                        cache_hit_read(tag_val, resp_out);  // HIT in this level
                    } else {
                        // Tag matches but not valid - MISS
                        addr_out = addr_in;  // Send address to next level
                        cache_miss_read(tag_val, resp_out);  // Query next level
                    }
                    break;
                }
            }
            if (!found) {
                // Tag not found - MISS
                addr_out = addr_in;  // Send address to next level
                cache_miss_read(tag_val, resp_out);  // Query next level
            }
        }
    }

    void cache_hit_read(int hit_tag, int &resp_out) {
        // Hit at this level
        resp_out = 1;  // Always return 1 on hit (any level)
        tag_out = hit_tag;  // Store the tag that hit
        return;
    }
    
    void cache_miss_read(int miss_tag, int &resp_out) {
        // Query next lower level (L2, L3, L4, ... or main memory)
        // 
        // Example implementation:
        // next_cache.cache_read(addr_out, resp_in);
        // 
        // if (resp_in == 1) {
        //     // Lower level hit - allocate block in this cache
        //     resp_out = 1;
        //     tag_out = miss_tag;  // Assign the tag that was missing
        //     // Allocate and mark valid in cache[set][way][0] = miss_tag
        //     // Set cache[set][way][1] valid bit
        // } else {
        //     // Lower level also missed
        //     resp_out = 0;
        // }
    }
    
    void cache_write(uint64_t addr, int &resp_out);
};