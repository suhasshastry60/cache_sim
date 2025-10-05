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
    // meta_and_data[0] = tag
    // meta_and_data[1] = metadata {blk_offset, lru, dirty, valid}
    vector<vector<vector<int>>> cache;
    int set_size, way_size;
    int tag_bits, index_bits, blk_offset_bits;
    int lru_bits;
    int metadata_width;  // Total bits needed for metadata
    
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
        
        // Calculate LRU bits needed
        lru_bits = (assoc > 1) ? (int)ceil(log2(assoc)) : 0;
        
        // Calculate total metadata width
        // valid(1) + dirty(1) + lru(lru_bits) + blk_offset(blk_offset_bits)
        metadata_width = 2 + lru_bits + blk_offset_bits;
        
        set_size = (1 << index_bits);  // 2^index_bits = number of sets
        way_size = (assoc == 1) ? 1 : assoc;  // if assoc is 1, way is just 0, else 0 to assoc-1
        
        // Initialize 3D vector: cache[set][way][meta_and_data]
        // meta_and_data has 2 entries: [0]=tag, [1]=metadata
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
            index_bits = (int)ceil(log2(num_sets));
            blk_offset_bits = (int)ceil(log2(blk_size));
            tag = 32 - index_bits - blk_offset_bits;
        } else {
            cerr << "Error: Address exceeds 32-bit limit. Terminating execution." << endl;
            exit(EXIT_FAILURE);
        }
    }
    
    // Helper functions to access metadata fields
    int get_valid(int metadata) {
        return metadata & 0x1;  // Bit [0]
    }
    
    int get_dirty(int metadata) {
        return (metadata >> 1) & 0x1;  // Bit [1]
    }
    
    int get_lru(int metadata) {
        int mask = (1 << lru_bits) - 1;  // Create mask for lru_bits
        return (metadata >> 2) & mask;  // Bits [lru_bits+1:2]
    }
    
    int get_blk_offset(int metadata) {
        int shift = 2 + lru_bits;
        int mask = (1 << blk_offset_bits) - 1;
        return (metadata >> shift) & mask;  // Bits [blk_offset_bits+lru_bits+1:lru_bits+2]
    }
    
    int set_valid(int metadata, int valid) {
        metadata = (metadata & ~0x1) | (valid & 0x1);  // Set bit [0]
        return metadata;
    }
    
    int set_dirty(int metadata, int dirty) {
        metadata = (metadata & ~0x2) | ((dirty & 0x1) << 1);  // Set bit [1]
        return metadata;
    }
    
    int set_lru(int metadata, int lru_val) {
        int mask = (1 << lru_bits) - 1;
        metadata = (metadata & ~(mask << 2)) | ((lru_val & mask) << 2);  // Set bits [lru_bits+1:2]
        return metadata;
    }
    
    int set_blk_offset(int metadata, int blk_off) {
        int shift = 2 + lru_bits;
        int mask = (1 << blk_offset_bits) - 1;
        metadata = (metadata & ~(mask << shift)) | ((blk_off & mask) << shift);
        return metadata;
    }
    
    void cache_read(uint64_t addr, int &resp_out) {
        // Extract tag and set index from address
        int blk_offset = addr & ((1 << blk_offset_bits) - 1);
        int set_idx = (addr >> blk_offset_bits) & ((1 << index_bits) - 1);
        int tag_val = (addr >> (blk_offset_bits + index_bits)) & ((1 << tag_bits) - 1);
        
        if (assoc == 1) {
            // Direct mapped: only way 0
            if (cache[set_idx][0][0] == tag_val) {
                int metadata = cache[set_idx][0][1];
                if (get_valid(metadata) == 1) {  // Check valid bit
                    cache_hit_read(tag_val, resp_out);  // HIT in this level
                } else {
                    // Tag matches but not valid - MISS
                    addr_out = addr_in;  // Send address to next level
                    cache_miss_read(addr_out, resp_out);  // Query next level
                }
            } else {
                // Tag not found - MISS
                addr_out = addr_in;  // Send address to next level
                cache_miss_read(addr_out, resp_out);  // Query next level

                if (resp_out == 1) {  // If lower level returned a hit
                    cache[set_idx][0][0] = tag_val;  // Allocate new tag
                    // Build metadata using helper functions
                    int metadata = 0;  // Start with all zeros
                    metadata = set_blk_offset(metadata, blk_offset);  // Set block offset
                    metadata = set_lru(metadata, 0);                  // Set LRU = 0
                    metadata = set_dirty(metadata, 0);                // Set dirty = 0
                    metadata = set_valid(metadata, 1);                // Set valid = 1
                    
                    cache[set_idx][0][1] = metadata;  // Store metadata
                }
            }
        } else if (assoc > 1) {
            // Set associative: check all ways
            for (int w = 0; w < assoc; w++) {
                if (cache[set_idx][w][0] == tag_val) {
                    int metadata = cache[set_idx][w][1];
                    if (get_valid(metadata) == 1) {  // Check valid bit
                        cache_hit_read(tag_val, resp_out);  // HIT in this level
                    } else {
                        // We need to add lru stuff here
                        // identify LRU way to evict and replace`
                        addr_out = addr_in;  // Send address to next level
                        cache_miss_read(addr_out, resp_out);  // Query next level

                        if (resp_out == 1) {  // If lower level returned a hit
                        cache[set_idx][w][0] = tag_val;  // Allocate new tag
                        // Build metadata using helper functions
                        int metadata = 0;  // Start with all zeros
                        metadata = set_blk_offset(metadata, blk_offset);  // Set block offset
                        metadata = set_lru(metadata, 0);                  // Set LRU = 0
                        metadata = set_dirty(metadata, 0);                // Set dirty = 0
                        metadata = set_valid(metadata, 1);                // Set valid = 1

                        cache[set_idx][w][1] = metadata;  // Store metadata
                }
                    }
                    break;
                }
            }
        }
    }

    void cache_hit_read(int hit_tag, int &resp_out) {
        // Hit at this level
        resp_out = 1;  // Always return 1 on hit (any level)
        tag_out = hit_tag;  // Store the tag that hit
        return;
    }

    void cache_miss_read(int addr_out, int &resp_out) {
        // Query next lower level (L2, L3, L4, ... or main memory)
        // 
        // Example implementation:
        // next_cache.cache_read(addr_out, resp_in);
        // 
        // if (resp_in == 1) {
        //     // Lower level hit - allocate block in this cache
        //     cache[set_idx][way][0] = miss_tag;  // Store tag
        //     
        //     int metadata = 0;
        //     metadata = set_valid(metadata, 1);     // Mark as valid
        //     metadata = set_dirty(metadata, 0);     // Not dirty on read
        //     metadata = set_lru(metadata, 0);       // Reset LRU
        //     metadata = set_blk_offset(metadata, addr_in & ((1 << blk_offset_bits) - 1));
        //     
        //     cache[set_idx][way][1] = metadata;  // Store metadata
        //     
        //     resp_out = 1;
        //     tag_out = miss_tag;
        // } else {
        //     // Lower level also missed
        //     resp_out = 0;
        // }
    }
    
    void cache_write(uint64_t addr, int &resp_out);
};