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
    cache_wrapper* next_cache;  // Pointer to next level cache
    
    // Cache storage: cache[set][way][meta_and_data]
    // meta_and_data[0] = tag
    // meta_and_data[1] = metadata {blk_offset, lru, dirty, valid}
    vector<vector<vector<int>>> cache;
    int set_size, way_size;
    int tag_bits, index_bits, blk_offset_bits;
    int lru_bits;
    int metadata_width;
    
    cache_wrapper(int size, int assoc, int blk_size, uint64_t addr_in, 
                  int tag_out, int addr_out, int resp_in, int cache_num, 
                  char operation, cache_wrapper* next = nullptr) {
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
        this->next_cache = next;  // Link to next cache level
        
        decode_addr(addr_in, tag_bits, index_bits, blk_offset_bits);
        
        lru_bits = (assoc > 1) ? (int)ceil(log2(assoc)) : 0;
        metadata_width = 2 + lru_bits + blk_offset_bits;
        
        set_size = (1 << index_bits);
        way_size = (assoc == 1) ? 1 : assoc;
        
        cache.resize(set_size, vector<vector<int>>(way_size, vector<int>(2, 0)));
        
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
        return metadata & 0x1;
    }
    
    int get_dirty(int metadata) {
        return (metadata >> 1) & 0x1;
    }
    
    int get_lru(int metadata) {
        int mask = (1 << lru_bits) - 1;
        return (metadata >> 2) & mask;
    }
    
    int get_blk_offset(int metadata) {
        int shift = 2 + lru_bits;
        int mask = (1 << blk_offset_bits) - 1;
        return (metadata >> shift) & mask;
    }
    
    int set_valid(int metadata, int valid) {
        metadata = (metadata & ~0x1) | (valid & 0x1);
        return metadata;
    }
    
    int set_dirty(int metadata, int dirty) {
        metadata = (metadata & ~0x2) | ((dirty & 0x1) << 1);
        return metadata;
    }
    
    int set_lru(int metadata, int lru_val) {
        int mask = (1 << lru_bits) - 1;
        metadata = (metadata & ~(mask << 2)) | ((lru_val & mask) << 2);
        return metadata;
    }
    
    int set_blk_offset(int metadata, int blk_off) {
        int shift = 2 + lru_bits;
        int mask = (1 << blk_offset_bits) - 1;
        metadata = (metadata & ~(mask << shift)) | ((blk_off & mask) << shift);
        return metadata;
    }
    
    void lru_order(int set, int &dirty, uint64_t &eblk_addr) {
        for (int way = 0; way < assoc; way++) {
            int new_meta = cache[set][way][1];
            int new_tag = cache[set][way][0];
            
            if (get_lru(new_meta) == assoc - 1) {
                dirty = get_dirty(new_meta);
                int blk_offset = get_blk_offset(new_meta);
                
                eblk_addr = ((uint64_t)new_tag << (index_bits + blk_offset_bits)) |
                            ((uint64_t)set << blk_offset_bits) |
                            (uint64_t)blk_offset;
                return;
            }
        }
    }
    
    void lru_update(int hit_value, int set) {
        for (int way = 0; way < assoc; way++) {
            int new_meta = cache[set][way][1];
            int current_lru = get_lru(new_meta);
            
            if (current_lru == hit_value) {
                new_meta = set_lru(new_meta, 0);
                cache[set][way][1] = new_meta;
            } else if (current_lru < hit_value) {
                new_meta = set_lru(new_meta, current_lru + 1);
                cache[set][way][1] = new_meta;
            }
        }
    }
    
    void cache_read(uint64_t addr, int &resp_out) {
        int blk_offset = addr & ((1 << blk_offset_bits) - 1);
        int set_idx = (addr >> blk_offset_bits) & ((1 << index_bits) - 1);
        int tag_val = (addr >> (blk_offset_bits + index_bits)) & ((1 << tag_bits) - 1);
        
        if (assoc == 1) {
            // Direct mapped: only way 0
            int metadata = cache[set_idx][0][1];
            int stored_tag = cache[set_idx][0][0];
            
            if (stored_tag == tag_val && get_valid(metadata) == 1) {
                // Tag matches AND valid - HIT
                cache_hit_read(tag_val, resp_out);
            } else {
                // MISS - either tag doesn't match or not valid
                // Check if we need to evict (valid block with different tag)
                if (get_valid(metadata) == 1 && stored_tag != tag_val) {
                    // Valid block exists but different tag - need to evict
                    if (get_dirty(metadata) == 1) {
                        // Dirty block - write back to lower level
                        int blk_offset_evict = get_blk_offset(metadata);
                        uint64_t eblk_addr = ((uint64_t)stored_tag << (index_bits + blk_offset_bits)) | 
                                             ((uint64_t)set_idx << blk_offset_bits) | 
                                             (uint64_t)blk_offset_evict;
                        
                        int write_resp;
                        if (next_cache != nullptr) {
                            next_cache->cache_write(eblk_addr, write_resp);
                            if (write_resp != 1) {
                                cerr << "Error: Write-back to lower level failed." << endl;
                                exit(EXIT_FAILURE);
                            }
                        }
                    }
                }
                
                // Now fetch the requested block from lower level
                addr_out = addr;
                cache_miss_read(addr_out, resp_out);
                
                if (resp_out == 1) {
                    // Lower level hit - allocate in this cache
                    cache[set_idx][0][0] = tag_val;
                    
                    metadata = 0;
                    metadata = set_blk_offset(metadata, blk_offset);
                    metadata = set_lru(metadata, 0);
                    metadata = set_dirty(metadata, 0);  // Clean on read
                    metadata = set_valid(metadata, 1);
                    
                    cache[set_idx][0][1] = metadata;
                }
            }
        } else if (assoc > 1) {
            // Set associative: check all ways
            bool found = false;
            int hit_way = -1;
            
            for (int w = 0; w < assoc; w++) {
                int metadata = cache[set_idx][w][1];
                int stored_tag = cache[set_idx][w][0];
                
                if (stored_tag == tag_val && get_valid(metadata) == 1) {
                    // HIT
                    found = true;
                    hit_way = w;
                    cache_hit_read(tag_val, resp_out);
                    
                    // Update LRU
                    int old_lru = get_lru(metadata);
                    lru_update(old_lru, set_idx);
                    break;
                }
            }
            
            if (!found) {
                // MISS - find a way to allocate
                int victim_way = -1;
                
                // First, try to find an invalid way
                for (int w = 0; w < assoc; w++) {
                    int metadata = cache[set_idx][w][1];
                    if (get_valid(metadata) == 0) {
                        victim_way = w;
                        // what next??
                        break;
                    }
                }
                
                // If no invalid way, use LRU
                if (victim_way == -1) {
                    int dirty_bit;
                    uint64_t eblk_addr;
                    lru_order(set_idx, dirty_bit, eblk_addr);
                    
                    // Find the LRU way
                    for (int w = 0; w < assoc; w++) {
                        int metadata = cache[set_idx][w][1];
                        if (get_lru(metadata) == assoc - 1) {
                            victim_way = w;
                            
                            // Write back if dirty
                            if (dirty_bit == 1) {
                                int write_resp;
                                if (next_cache != nullptr) {
                                    next_cache->cache_write(eblk_addr, write_resp);
                                    if (write_resp != 1) {
                                        cerr << "Error: Write-back to lower level failed." << endl;
                                        exit(EXIT_FAILURE);
                                    }
                                }
                            }
                            break;
                        }
                    }
                }
                
                // Fetch from lower level
                addr_out = addr;
                cache_miss_read(addr_out, resp_out);
                
                if (resp_out == 1 && victim_way != -1) {
                    // Lower level hit - allocate in victim way
                    cache[set_idx][victim_way][0] = tag_val;
                    
                    int metadata = 0;
                    metadata = set_blk_offset(metadata, blk_offset);
                    metadata = set_lru(metadata, 0);  // MRU
                    metadata = set_dirty(metadata, 0);
                    metadata = set_valid(metadata, 1);
                    
                    cache[set_idx][victim_way][1] = metadata;
                    
                    // Update all other LRUs
                    lru_update(0, set_idx);
                }
            }
        }
    }

    void cache_hit_read(int hit_tag, int &resp_out) {
        resp_out = 1;  // Always return 1 on hit
        tag_out = hit_tag;
        return;
    }
    
    void cache_miss_read(uint64_t addr_out, int &resp_out) {
        if (next_cache != nullptr) {
            // Call next level's cache_read
            next_cache->cache_read(addr_out, resp_out);
        } else {
            // This is the last cache level - call main memory
            // For now, main memory always hits
            resp_out = 1;
        }
    }
    
    void cache_write(uint64_t addr, int &resp_out);
};