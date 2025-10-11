#ifndef SIM_CACHE_H
#define SIM_CACHE_H

#include <cstdint>
#include <vector>
#include <cmath>
#include <iostream>
#include <iomanip>

using namespace std;

typedef 
struct {
   uint32_t BLOCKSIZE;
   uint32_t L1_SIZE;
   uint32_t L1_ASSOC;
   uint32_t L2_SIZE;
   uint32_t L2_ASSOC;
   uint32_t PREF_N;
   uint32_t PREF_M;
} cache_params_t;

class cache_wrapper {
public:
    int size, assoc, blk_size;
    uint32_t addr_in;
    uint32_t addr_out;
    int tag_out, resp_in, resp_out, cache_num;
    char operation;
    cache_wrapper* next_cache;

    vector<vector<vector<uint32_t>>> cache;
    int set_size, way_size;
    int tag_bits, index_bits, blk_offset_bits;
    int lru_bits;
    int metadata_width;
    uint32_t reads = 0;
    uint32_t read_misses = 0;
    uint32_t writes = 0;
    uint32_t write_misses = 0;
    uint32_t writebacks = 0;
    uint32_t prefetches = 0;

    cache_wrapper(int size, int assoc, int blk_size, uint32_t addr_in,
                  int tag_out, int addr_out, int resp_in, int cache_num,
                  char operation, cache_wrapper* next = nullptr);

    void decode_addr(uint32_t addr_in_local, int &tag, int &index_bits_local, int &blk_offset_bits_local) {
        if (addr_in_local > ((1ULL << 32) - 1ULL)) {
            cerr << "Error: Address exceeds 32-bit limit. Terminating execution." << endl;
            exit(EXIT_FAILURE);
        }

        int num_sets = size / (assoc * blk_size);
        
        blk_offset_bits_local = (blk_size > 1) ? (int)log2(blk_size) : 0;
        index_bits_local = (num_sets > 1) ? (int)log2(num_sets) : 0;
        tag = 32 - index_bits_local - blk_offset_bits_local;
    }

    int calculate_valid(uint32_t metadata) const {
        return metadata & 0x1;
    }

    int calculate_dirty(uint32_t metadata) const {
        return (metadata >> 1) & 0x1;
    }

    int calculate_lru(uint32_t metadata) const {
        if (lru_bits == 0) return 0;
        int mask = (1 << lru_bits) - 1;
        return (metadata >> 2) & mask;
    }

    int calculate_blk_offset(uint32_t metadata) const {
        int shift = 2 + lru_bits;
        if (blk_offset_bits == 0) return 0;
        int mask = (1 << blk_offset_bits) - 1;
        return (metadata >> shift) & mask;
    }

    uint32_t set_valid(uint32_t metadata, int valid) {
        metadata = (metadata & ~0x1U) | (valid & 0x1);
        return metadata;
    }

    uint32_t set_dirty(uint32_t metadata, int dirty) {
        metadata = (metadata & ~0x2U) | ((dirty & 0x1) << 1);
        return metadata;
    }

    uint32_t set_lru(uint32_t metadata, int lru_val) {
        if (lru_bits == 0) return metadata;
        uint32_t mask = (1U << lru_bits) - 1U;
        metadata = (metadata & ~(mask << 2)) | ((lru_val & mask) << 2);
        return metadata;
    }

    uint32_t set_blk_offset(uint32_t metadata, int blk_off) {
        int shift = 2 + lru_bits;
        if (blk_offset_bits == 0) return metadata;
        uint32_t mask = (1U << blk_offset_bits) - 1U;
        metadata = (metadata & ~(mask << shift)) | ((blk_off & mask) << shift);
        return metadata;
    }

    void lru_order(int set, int &dirty, uint32_t &eblk_addr) {
        for (int way = 0; way < assoc; way++) {
            if (set < 0 || set >= set_size || way < 0 || way >= way_size) continue;
            uint32_t new_meta = cache[set][way][1];
            uint32_t new_tag = cache[set][way][0];
            if (calculate_valid(new_meta) == 1 && calculate_lru(new_meta) == assoc - 1) {
                dirty = calculate_dirty(new_meta);
                int blk_offset = calculate_blk_offset(new_meta);

                eblk_addr = (new_tag << (index_bits + blk_offset_bits)) |
                            (set << blk_offset_bits) |
                            blk_offset;
                return;
            }
        }
        dirty = 0;
        eblk_addr = 0;
    }

    void lru_update(int hit_value, int set) {
        for (int way = 0; way < assoc; way++) {
            if (set < 0 || set >= set_size || way < 0 || way >= way_size) continue;
            uint32_t new_meta = cache[set][way][1];
            int current_lru = calculate_lru(new_meta);

            if (current_lru == hit_value) {
                new_meta = set_lru(new_meta, 0);
                cache[set][way][1] = new_meta;
            } else if (current_lru < hit_value) {
                new_meta = set_lru(new_meta, current_lru + 1);
                cache[set][way][1] = new_meta;
            }
        }
    }

    void lru_insert(int set, int new_way) {
        for (int w = 0; w < assoc; w++) {
            if (set < 0 || set >= set_size || w < 0 || w >= way_size) continue;
            if (w == new_way) continue;
            uint32_t meta = cache[set][w][1];

            if (calculate_valid(meta) == 0) continue;

            int curr_lru = calculate_lru(meta);
            meta = set_lru(meta, curr_lru + 1);
            cache[set][w][1] = meta;
        }

        uint32_t new_meta = cache[set][new_way][1];
        new_meta = set_lru(new_meta, 0);
        cache[set][new_way][1] = new_meta;
    }

    void print_contents() const {
        cout << "===== " << (cache_num == 1 ? "L1" : "L2") << " contents =====\n";
        for (int s = 0; s < set_size; ++s) {
            cout << "set " << setw(6) << right << dec << s << ":";

            if (assoc > 1) {
                for (int lru_val = 0; lru_val < assoc; ++lru_val) {
                    bool found = false;
                    for (int w = 0; w < assoc; ++w) {
                        if (calculate_valid(cache[s][w][1]) && calculate_lru(cache[s][w][1]) == lru_val) {
                            cout << "   " << setw(6) << hex << cache[s][w][0];
                            if (calculate_dirty(cache[s][w][1]))
                                cout << " D";
                            else
                                cout << "  ";
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        cout << "       ";
                    }
                }
            } else {
                if (calculate_valid(cache[s][0][1])) {
                    cout << "   " << setw(6) << hex << cache[s][0][0];
                    if (calculate_dirty(cache[s][0][1]))
                        cout << " D";
                    else
                        cout << "  ";
                } else {
                    cout << "       ";
                }
            }
            cout << endl;
        }
        cout << dec;
    }

    void cache_hit_read(int hit_tag, int &resp_out) {
        resp_out = 1;
        tag_out = hit_tag;
        return;
    }

    void cache_miss_read(uint32_t addr_out_local, int &resp_out) {
        if (next_cache != nullptr) {
            next_cache->cache_read(addr_out_local, resp_out);
        } else {
            resp_out = 1;
        }
    }

    void cache_read(uint32_t addr, int &resp_out) {
        uint32_t blk_mask = (blk_offset_bits == 0) ? 0U : ((1U << blk_offset_bits) - 1U);
        int blk_offset = (blk_offset_bits == 0) ? 0 : (addr & blk_mask);
        int set_idx = (index_bits == 0) ? 0 : ((addr >> blk_offset_bits) & ((1U << index_bits) - 1U));
        uint32_t tag_val = (addr >> (blk_offset_bits + index_bits));

        ++reads;

        if (set_idx < 0 || set_idx >= set_size) {
            cerr << "Error: set_idx out of range in cache_read: " << set_idx << endl;
            resp_out = 0;
            return;
        }

        if (assoc == 1) {
            uint32_t metadata = cache[set_idx][0][1];
            uint32_t stored_tag = cache[set_idx][0][0];

            if (stored_tag == tag_val && calculate_valid(metadata) == 1) {
                cache_hit_read(tag_val, resp_out);
            } else {
                ++read_misses;
                if (calculate_valid(metadata) == 1 && stored_tag != tag_val) {
                    if (calculate_dirty(metadata) == 1) {
                        ++writebacks;
                        int blk_offset_evict = calculate_blk_offset(metadata);
                        uint32_t eblk_addr = (stored_tag << (index_bits + blk_offset_bits)) |
                                             (set_idx << blk_offset_bits) |
                                             blk_offset_evict;

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

                addr_out = addr;
                cache_miss_read(addr_out, resp_out);

                if (resp_out == 1) {
                    cache[set_idx][0][0] = tag_val;

                    uint32_t new_metadata = 0;
                    new_metadata = set_blk_offset(new_metadata, blk_offset);
                    new_metadata = set_lru(new_metadata, 0);
                    new_metadata = set_dirty(new_metadata, 0);
                    new_metadata = set_valid(new_metadata, 1);

                    cache[set_idx][0][1] = new_metadata;
                }
            }
        } else if (assoc > 1) {
            bool found = false;

            for (int w = 0; w < assoc; w++) {
                if (w >= way_size) break;
                uint32_t metadata = cache[set_idx][w][1];
                uint32_t stored_tag = cache[set_idx][w][0];

                if (stored_tag == tag_val && calculate_valid(metadata) == 1) {
                    found = true;
                    cache_hit_read(tag_val, resp_out);

                    int old_lru = calculate_lru(metadata);
                    lru_update(old_lru, set_idx);
                    break;
                }
            }

            if (!found) {
                ++read_misses;
                int victim_way = -1;

                for (int w = 0; w < assoc; w++) {
                    if (w >= way_size) break;
                    uint32_t metadata = cache[set_idx][w][1];
                    if (calculate_valid(metadata) == 0) {
                        victim_way = w;
                        break;
                    }
                }

                if (victim_way == -1) {
                    int dirty_bit;
                    uint32_t eblk_addr;
                    lru_order(set_idx, dirty_bit, eblk_addr);

                    for (int w = 0; w < assoc; w++) {
                        if (w >= way_size) break;
                        uint32_t metadata = cache[set_idx][w][1];
                        if (calculate_lru(metadata) == assoc - 1 && calculate_valid(metadata) == 1) {
                            victim_way = w;

                            if (dirty_bit == 1) {
                                ++writebacks;
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

                if (victim_way == -1) {
                    victim_way = 0;
                }

                if (victim_way != -1) {
                    addr_out = addr;
                    cache_miss_read(addr_out, resp_out);

                    if (resp_out == 1) {
                        cache[set_idx][victim_way][0] = tag_val;

                        uint32_t metadata = 0;
                        metadata = set_blk_offset(metadata, blk_offset);
                        metadata = set_dirty(metadata, 0);
                        metadata = set_valid(metadata, 1);

                        cache[set_idx][victim_way][1] = metadata;

                        lru_insert(set_idx, victim_way);
                    }
                }
            }
        }
    }

    void cache_write(uint32_t addr, int &resp_out) {
        int blk_offset = (blk_offset_bits == 0) ? 0 : (addr & ((1 << blk_offset_bits) - 1));
        int set_idx = (index_bits == 0) ? 0 : ((addr >> blk_offset_bits) & ((1 << index_bits) - 1));
        uint32_t tag_val = (addr >> (blk_offset_bits + index_bits));
        ++writes;

        if (assoc == 1) {
            uint32_t metadata = cache[set_idx][0][1];
            uint32_t stored_tag = cache[set_idx][0][0];
            if (stored_tag == tag_val && calculate_valid(metadata) == 1) {
                metadata = set_dirty(metadata, 1);
                cache[set_idx][0][1] = metadata;
                resp_out = 1;
            } else {
                ++write_misses;
                if (calculate_valid(metadata) == 1 && stored_tag != tag_val) {
                    if (calculate_dirty(metadata) == 1) {
                        ++writebacks;
                        int blk_offset_evict = calculate_blk_offset(metadata);
                        uint32_t eblk_addr = (stored_tag << (index_bits + blk_offset_bits)) | 
                                            (set_idx << blk_offset_bits) | 
                                            blk_offset_evict;
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
                addr_out = addr;
                cache_miss_read(addr_out, resp_out);
                if (resp_out == 1) {
                    cache[set_idx][0][0] = tag_val;
                    uint32_t new_metadata = 0;
                    new_metadata = set_blk_offset(new_metadata, blk_offset);
                    new_metadata = set_lru(new_metadata, 0);  
                    new_metadata = set_dirty(new_metadata, 1);
                    new_metadata = set_valid(new_metadata, 1);
                    cache[set_idx][0][1] = new_metadata;
                }
            }
        } else if (assoc > 1) {
            bool found = false;
            int victim_way = -1;

            for (int w = 0; w < assoc; w++) {
                uint32_t metadata = cache[set_idx][w][1];
                uint32_t stored_tag = cache[set_idx][w][0];
                if (stored_tag == tag_val && calculate_valid(metadata) == 1) {
                    found = true;
                    int old_lru = calculate_lru(metadata);
                    metadata = set_dirty(metadata, 1);
                    cache[set_idx][w][1] = metadata;
                    resp_out = 1;
                    lru_update(old_lru, set_idx);
                    break;
                }
            }

            if (!found) {
                ++write_misses;
                for (int w = 0; w < assoc; w++) {
                    uint32_t metadata = cache[set_idx][w][1];
                    if (calculate_valid(metadata) == 0) {
                        victim_way = w;
                        break;
                    }
                }

                if (victim_way == -1) {
                    int dirty_bit;
                    uint32_t eblk_addr;
                    lru_order(set_idx, dirty_bit, eblk_addr);
                    for (int w = 0; w < assoc; w++) {
                        uint32_t metadata = cache[set_idx][w][1];
                        if (calculate_lru(metadata) == assoc - 1 && calculate_valid(metadata) == 1) {
                            victim_way = w;
                            if (dirty_bit == 1) {
                                ++writebacks;
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

                if (victim_way == -1) {
                    victim_way = 0;
                }

                addr_out = addr;
                cache_miss_read(addr_out, resp_out);
                if (resp_out == 1 && victim_way != -1) {
                    cache[set_idx][victim_way][0] = tag_val;
                    uint32_t metadata = 0;
                    metadata = set_blk_offset(metadata, blk_offset);
                    metadata = set_dirty(metadata, 1);
                    metadata = set_valid(metadata, 1);
                    cache[set_idx][victim_way][1] = metadata;

                    lru_insert(set_idx, victim_way);
                }
            }
        }
    }
};

#endif
