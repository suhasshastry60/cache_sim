#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <cmath>
#include "sim.h"
#include <bits/stdc++.h>
#include <vector>
#include <iomanip>
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
    uint64_t reads = 0;
    uint64_t read_misses = 0;
    uint64_t writes = 0;
    uint64_t write_misses = 0;
    uint64_t writebacks = 0;
    uint64_t prefetches = 0;

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

        // Initialize bit-fields to safe defaults before decode call
        tag_bits = 0;
        index_bits = 0;
        blk_offset_bits = 0;

        decode_addr(addr_in, tag_bits, index_bits, blk_offset_bits);

        lru_bits = (assoc > 1) ? (int)ceil(log2((double)assoc)) : 0; //change, dumber
        metadata_width = 2 + lru_bits + blk_offset_bits;

        // guard index_bits (reasonable range)
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

        cache.resize(set_size, vector<vector<int>>(way_size, vector<int>(2, 0)));

    }
// move to header, simplify the error messages, make it human, can remove safety measures, seems too well thought out
    void decode_addr(uint64_t addr_in_local, int &tag, int &index_bits_local, int &blk_offset_bits_local) {
        // Validate basic params
        if (blk_size == 0) {
            cerr << "Error: blk_size cannot be 0\n";
            exit(EXIT_FAILURE);
        }
        if (assoc == 0) {
            cerr << "Error: assoc cannot be 0\n";
            exit(EXIT_FAILURE);
        }
        if (size == 0) {
            cerr << "Error: size cannot be 0\n";
            exit(EXIT_FAILURE);
        }

        if (addr_in_local > ((1ULL << 32) - 1ULL)) { //could restrict to 32 bit address space, use 2**32 -1
            cerr << "Error: Address exceeds 32-bit limit. Terminating execution." << endl;
            exit(EXIT_FAILURE);
        }

        int num_sets = size / (assoc * blk_size);
        if (num_sets <= 0) {
            cerr << "Error: computed num_sets <= 0. Check size, assoc, blk_size.\n";
            exit(EXIT_FAILURE);
        }

        // blk_offset_bits: require power-of-two block size (safe assumption)
        {
            int tmp = blk_size;
            int bits = 0;
            while (tmp > 1) { tmp >>= 1; ++bits; }
            if ((1 << bits) != blk_size) {
                cerr << "Error: blk_size must be a power of two. blk_size=" << blk_size << endl;
                exit(EXIT_FAILURE);
            }
            blk_offset_bits_local = bits;
        }

        // index bits: require power-of-two num_sets (common case)
        {
            int tmp = num_sets;
            int bits = 0;
            while (tmp > 1) { tmp >>= 1; ++bits; }
            if ((1 << bits) != num_sets) {
                cerr << "Error: num_sets must be a power of two. num_sets=" << num_sets << endl;
                exit(EXIT_FAILURE);
            }
            index_bits_local = bits;
        }

        tag = 32 - index_bits_local - blk_offset_bits_local;
        if (tag <= 0) {
            cerr << "Error: computed tag bits <= 0\n";
            exit(EXIT_FAILURE);
        }
    }

    // Helper functions to access metadata fields, move everything to header
    int get_valid(int metadata) const {
        return metadata & 0x1;
    }

    int get_dirty(int metadata) const {
        return (metadata >> 1) & 0x1;
    }

    int get_lru(int metadata) const {
        if (lru_bits == 0) return 0;
        int mask = (1 << lru_bits) - 1;
        return (metadata >> 2) & mask;
    }

    int get_blk_offset(int metadata) const {
        int shift = 2 + lru_bits;
        if (blk_offset_bits == 0) return 0;
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
        if (lru_bits == 0) return metadata;
        int mask = (1 << lru_bits) - 1;
        metadata = (metadata & ~(mask << 2)) | ((lru_val & mask) << 2);
        return metadata;
    }

    int set_blk_offset(int metadata, int blk_off) {
        int shift = 2 + lru_bits;
        if (blk_offset_bits == 0) return metadata;
        int mask = (1 << blk_offset_bits) - 1;
        metadata = (metadata & ~(mask << shift)) | ((blk_off & mask) << shift);
        return metadata;
    }

    void lru_order(int set, int &dirty, uint64_t &eblk_addr) {
        for (int way = 0; way < assoc; way++) {
            if (set < 0 || set >= set_size || way < 0 || way >= way_size) continue;
            int new_meta = cache[set][way][1];
            int new_tag = cache[set][way][0];

            if (get_lru(new_meta) == assoc - 1) { //Add the && get_valid(new_meta) == 1 check
                dirty = get_dirty(new_meta);
                int blk_offset = get_blk_offset(new_meta);

                eblk_addr = ((uint64_t)new_tag << (index_bits + blk_offset_bits)) |
                            ((uint64_t)set << blk_offset_bits) |
                            (uint64_t)blk_offset;
                return;
            }
        }
        dirty = 0;
        eblk_addr = 0;
    }

    void lru_update(int hit_value, int set) {
        for (int way = 0; way < assoc; way++) {
            if (set < 0 || set >= set_size || way < 0 || way >= way_size) continue;
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
    void lru_insert(int set, int new_way) {
        for (int w = 0; w < assoc; w++) {
            if (set < 0 || set >= set_size || w < 0 || w >= way_size) continue;
            int meta = cache[set][w][1];

            // Skip invalid ways
            if (get_valid(meta) == 0) continue;

            int curr_lru = get_lru(meta);
            // Increment all valid ones
            meta = set_lru(meta, curr_lru + 1);
            cache[set][w][1] = meta;
        }

        // Finally set the new one to MRU = 0
        int new_meta = cache[set][new_way][1];
        new_meta = set_lru(new_meta, 0);
        cache[set][new_way][1] = new_meta;
    }


void print_contents() const {
    cout << "===== " << (cache_num == 1 ? "L1" : "L2") << " contents =====\n";
    for (int s = 0; s < set_size; ++s) {
        cout << "set " << setw(2) << right << hex << s << ":";

        for (int w = 0; w < assoc; ++w) {
            if (get_valid(cache[s][w][1])) {
                cout << "   " << setw(6) << cache[s][w][0];
                if (get_dirty(cache[s][w][1]))
                    cout << " D";
                else
                    cout << "  ";
            } else {
                cout << "       ";
            }
        }
        cout << endl;
    }
    cout << dec; // reset to decimal for future output
}

//change uint64_t to uint32_t, get rid of all conversion stuff, can simplify the logic a bit
    void cache_read(uint64_t addr, int &resp_out) {
        uint32_t addr32 = (uint32_t)addr;
        uint32_t blk_mask = (blk_offset_bits == 0) ? 0U : ((1U << blk_offset_bits) - 1U);
        int blk_offset = (blk_offset_bits == 0) ? 0 : (addr32 & blk_mask);
        int set_idx = (blk_offset_bits == 32) ? 0 : ((addr32 >> blk_offset_bits) & ((1U << index_bits) - 1U));
        int tag_val = (addr32 >> (blk_offset_bits + index_bits)) & ((1U << tag_bits) - 1U);

        ++reads;
//can remove these safety checks
        if (set_idx < 0 || set_idx >= set_size) {
            cerr << "Error: set_idx out of range in cache_read: " << set_idx << endl;
            resp_out = 0;
            return;
        }

        if (assoc == 1) {
            // Direct mapped: only way 0
            int metadata = cache[set_idx][0][1];
            int stored_tag = cache[set_idx][0][0];

            if (stored_tag == tag_val && get_valid(metadata) == 1) {
                // Tag matches AND valid - HIT
                cache_hit_read(tag_val, resp_out);
                //increment cache hits counter, cache_hit_read++
            } else {
                ++read_misses;
                // MISS - either tag doesn't match or not valid
                // Check if we need to evict (valid block with different tag)
                if (get_valid(metadata) == 1 && stored_tag != tag_val) {
                    // Valid block exists but different tag - need to evict
                    if (get_dirty(metadata) == 1) {
                        ++writebacks;
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
            int hit_way = -1; //why is it -1

            for (int w = 0; w < assoc; w++) {
                if (w >= way_size) break; //can remove
                int metadata = cache[set_idx][w][1];
                int stored_tag = cache[set_idx][w][0];

                if (stored_tag == tag_val && get_valid(metadata) == 1) {
                    // HIT
                    found = true;
                    hit_way = w;
                    cache_hit_read(tag_val, resp_out);
                    //increment cache hits counter, cache_hit_read++

                    // Update LRU
                    int old_lru = get_lru(metadata);
                    lru_update(old_lru, set_idx);
                    break;
                }
            }

            if (!found) {
                ++read_misses;
                int victim_way = -1;
                bool inserted_invalid = false;

                // 1. Try to find an invalid way
                for (int w = 0; w < assoc; w++) {
                    if (w >= way_size) break;
                    int metadata = cache[set_idx][w][1];
                    if (get_valid(metadata) == 0) {
                        victim_way = w;
                        inserted_invalid = true;
                        break;
                    }
                }

                // 2. If no invalid way → LRU replacement
                if (victim_way == -1) {
                    int dirty_bit;
                    uint64_t eblk_addr;
                    lru_order(set_idx, dirty_bit, eblk_addr);

                    for (int w = 0; w < assoc; w++) {
                        if (w >= way_size) break;
                        int metadata = cache[set_idx][w][1];
                        if (get_lru(metadata) == assoc - 1 && get_valid(metadata) == 1) {
                            victim_way = w;

                            // Write back if dirty
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

                // 3. Fetch from lower level
                if (victim_way != -1) {
                    addr_out = addr;
                    cache_miss_read(addr_out, resp_out);

                    if (resp_out == 1) {
                        // Allocate in victim way
                        cache[set_idx][victim_way][0] = tag_val;

                        int metadata = 0;
                        metadata = set_blk_offset(metadata, blk_offset);
                        metadata = set_dirty(metadata, 0);
                        metadata = set_valid(metadata, 1);

                        cache[set_idx][victim_way][1] = metadata;

                        // 4. Update LRU
                        if (inserted_invalid) {
                            lru_insert(set_idx, victim_way);
                        } else {
                            int old_lru = get_lru(cache[set_idx][victim_way][1]);
                            lru_update(old_lru, set_idx);
                        }
                    }
                }
            }
        }
    }

    void cache_hit_read(int hit_tag, int &resp_out) {
        resp_out = 1;  // Always return 1 on hit
        tag_out = hit_tag;
        return;
    }

    void cache_miss_read(uint64_t addr_out_local, int &resp_out) {
        if (next_cache != nullptr) {
            // Call next level's cache_read
            next_cache->cache_read(addr_out_local, resp_out);
        } else {
            // This is the last cache level - call main memory
            // For now, main memory always hits
            resp_out = 1;
        }
    }

    void cache_write(uint64_t addr, int &resp_out) {
        int blk_offset = addr & ((1 << blk_offset_bits) - 1);
        int set_idx = (addr >> blk_offset_bits) & ((1 << index_bits) - 1);
        int tag_val = (addr >> (blk_offset_bits + index_bits)) & ((1 << tag_bits) - 1);
        ++writes;

        if (assoc == 1) {
            int metadata = cache[set_idx][0][1];
            int stored_tag = cache[set_idx][0][0];
            if (stored_tag == tag_val && get_valid(metadata) == 1) {
                metadata = set_dirty(metadata, 1);
                cache[set_idx][0][1] = metadata;
                resp_out = 1;
            } else {
                ++write_misses;
                if (get_valid(metadata) == 1 && stored_tag != tag_val) {
                    if (get_dirty(metadata) == 1) {
                        ++writebacks;
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
                addr_out = addr;
                cache_miss_read(addr_out, resp_out);
                if (resp_out == 1) {
                    cache[set_idx][0][0] = tag_val;
                    metadata = 0;
                    metadata = set_blk_offset(metadata, blk_offset);
                    metadata = set_lru(metadata, 0);  
                    metadata = set_dirty(metadata, 1);
                    metadata = set_valid(metadata, 1);
                    cache[set_idx][0][1] = metadata;
                }
            }
        } else if (assoc > 1) {
            bool found = false;
            int victim_way = -1;
            bool inserted_invalid = false;

            for (int w = 0; w < assoc; w++) {
                int metadata = cache[set_idx][w][1];
                int stored_tag = cache[set_idx][w][0];
                if (stored_tag == tag_val && get_valid(metadata) == 1) {
                    found = true;
                    int old_lru = get_lru(metadata);
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
                    int metadata = cache[set_idx][w][1];
                    if (get_valid(metadata) == 0) {
                        victim_way = w;
                        inserted_invalid = true; // ✅ mark insertion into invalid way
                        break;
                    }
                }

                if (victim_way == -1) {
                    int dirty_bit;
                    uint64_t eblk_addr;
                    lru_order(set_idx, dirty_bit, eblk_addr);
                    for (int w = 0; w < assoc; w++) {
                        int metadata = cache[set_idx][w][1];
                        if (get_lru(metadata) == assoc - 1 && get_valid(metadata) == 1) {
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

                addr_out = addr;
                cache_miss_read(addr_out, resp_out);
                if (resp_out == 1 && victim_way != -1) {
                    cache[set_idx][victim_way][0] = tag_val;
                    int metadata = 0;
                    metadata = set_blk_offset(metadata, blk_offset);
                    metadata = set_dirty(metadata, 1);
                    metadata = set_valid(metadata, 1);
                    cache[set_idx][victim_way][1] = metadata;

                    // ✅ Updated LRU handling
                    if (inserted_invalid) {
                        lru_insert(set_idx, victim_way);
                    } else {
                        int old_lru = get_lru(cache[set_idx][victim_way][1]);
                        lru_update(old_lru, set_idx);
                    }
                }
            }
        }
    }
}; // end cache_wrapper

class cache_interface {
public:
    cache_wrapper* L1;
    cache_wrapper* L2;

    cache_interface(cache_params_t params) {
        // Create caches in reverse order (L2 first, then L1)
        if (params.L2_SIZE > 0) {
            L2 = new cache_wrapper(params.L2_SIZE, params.L2_ASSOC, params.BLOCKSIZE,
                                   0, 0, 0, 0, 2, 'r', nullptr);
            L1 = new cache_wrapper(params.L1_SIZE, params.L1_ASSOC, params.BLOCKSIZE,
                                   0, 0, 0, 0, 1, 'r', L2);
        } else {
            L2 = nullptr;
            L1 = new cache_wrapper(params.L1_SIZE, params.L1_ASSOC, params.BLOCKSIZE,
                                   0, 0, 0, 0, 1, 'r', nullptr);
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
        cache_sys->L1->cache_read((uint64_t)addr, resp);
    } else if (rw == 'w') {
        cache_sys->L1->cache_write((uint64_t)addr, resp);
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

    cout << "\n===== Measurements =====\n";
    auto L1 = cache_sys->L1;
    auto L2 = cache_sys->L2;

    double l1_miss_rate = (L1->reads + L1->writes > 0) ?
        static_cast<double>(L1->read_misses + L1->write_misses) / (L1->reads + L1->writes) : 0.0;

    double l2_miss_rate = (L2 && L2->reads > 0) ?
        static_cast<double>(L2->read_misses) / L2->reads : 0.0;

    uint64_t mem_traffic = (L2) ?
        L2->read_misses + L2->write_misses + L2->writebacks :
        L1->read_misses + L1->write_misses + L1->writebacks;

    printf("a. L1 reads:                   %lu\n", L1->reads);
    printf("b. L1 read misses:             %lu\n", L1->read_misses);
    printf("c. L1 writes:                  %lu\n", L1->writes);
    printf("d. L1 write misses:            %lu\n", L1->write_misses);
    printf("e. L1 miss rate:               %.4f\n", l1_miss_rate);
    printf("f. L1 writebacks:              %lu\n", L1->writebacks);
    printf("g. L1 prefetches:              %lu\n", L1->prefetches);

    if (L2) {
        printf("h. L2 reads (demand):          %lu\n", L2->reads);
        printf("i. L2 read misses (demand):    %lu\n", L2->read_misses);
        printf("j. L2 reads (prefetch):        %d\n", 0);
        printf("k. L2 read misses (prefetch):  %d\n", 0);
        printf("l. L2 writes:                  %lu\n", L2->writes);
        printf("m. L2 write misses:            %lu\n", L2->write_misses);
        printf("n. L2 miss rate:               %.4f\n", l2_miss_rate);
        printf("o. L2 writebacks:              %lu\n", L2->writebacks);
        printf("p. L2 prefetches:              %lu\n", L2->prefetches);
    }

    printf("q. memory traffic:             %lu\n", mem_traffic);

    delete cache_sys;
    fclose(fp);

    return 0;
}
