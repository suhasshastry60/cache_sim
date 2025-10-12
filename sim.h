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
   uint32_t valid;
   uint32_t lru;
} stream_buffer_params_t;

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

    // Stream buffer prefetcher
    int PREF_N, PREF_M;
    vector<vector<uint32_t>> stream_buffer;  // [PREF_N][PREF_M] storing tags
    vector<stream_buffer_params_t> sb_params;  // [PREF_N] for metadata

    cache_wrapper(int size, int assoc, int blk_size, uint32_t addr_in,
                  int tag_out, int addr_out, int resp_in, int cache_num,
                  char operation, int pref_n, int pref_m, cache_wrapper* next = nullptr);

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

    void print_stream_buffers() const {
        if (PREF_N <= 0 || PREF_M <= 0) {
            return;  // No stream buffers to print
        }
        
        cout << "\n===== Stream Buffer(s) contents =====\n";
        // Print in MRU to LRU order (lru value 0 to N-1)
        for (uint32_t lru_val = 0; lru_val < (uint32_t)PREF_N; lru_val++) {
            for (int i = 0; i < PREF_N; i++) {
                if (sb_params[i].valid == 1 && sb_params[i].lru == lru_val) {
                    for (int j = 0; j < PREF_M; j++) {
                        if (stream_buffer[i][j] != UINT32_MAX) {
                            cout << " " << setw(7) << hex << stream_buffer[i][j];
                        }
                    }
                    cout << " \n";
                    break;
                }
            }
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

    // Helper function to extract tag from address
    uint32_t generate_tag(uint32_t addr) {
        return (addr >> (blk_offset_bits + index_bits));
    }

    // Helper function to get block address (tag + index, without offset)
    uint32_t get_block_addr(uint32_t addr) {
        return (addr >> blk_offset_bits);
    }


    // Rearrange stream buffer after hit at hit_index (remove 0 to hit_index, shift rest left)
    void buffer_rearrange(int hit_buffer, int hit_index) {
        // Shift everything from hit_index+1 onwards to position 0
        int shift_amount = hit_index + 1;
        for (int j = 0; j < PREF_M - shift_amount; j++) {
            stream_buffer[hit_buffer][j] = stream_buffer[hit_buffer][j + shift_amount];
        }
        // Clear the remaining positions at the end
        for (int j = PREF_M - shift_amount; j < PREF_M; j++) {
            stream_buffer[hit_buffer][j] = UINT32_MAX;
        }
    }

    // Refill stream buffer from the end with next sequential blocks
    // base_addr is the address corresponding to the first entry (index 0) in the buffer AFTER rearrangement
    // Always refill to maintain M blocks total
    void buffer_refill(int hit_buffer, uint32_t base_addr) {
        // Count valid entries to determine how many to refill
        int valid_count = 0;
        for (int j = 0; j < PREF_M; j++) {
            if (stream_buffer[hit_buffer][j] != UINT32_MAX) {
                valid_count++;
            } else {
                break;  // Empty slots are at the end after rearrangement
            }
        }
        
        // If buffer is already full, nothing to refill
        if (valid_count >= PREF_M) {
            return;
        }

        // Calculate starting address for new blocks
        // The last valid block is at position (valid_count-1), which is base_addr + (valid_count-1) * blk_size
        // So the next block is at base_addr + valid_count * blk_size
        uint32_t next_addr = base_addr + (valid_count * blk_size);

        // Prefetch blocks to fill buffer to M blocks
        for (int j = valid_count; j < PREF_M; j++) {
            stream_buffer[hit_buffer][j] = get_block_addr(next_addr);
            prefetches++;
            next_addr += blk_size;
        }
    }

    // Update LRU for stream buffers (set hit_buffer to MRU)
    void sb_lru_update(int hit_buffer, bool was_invalid = false) {
        if (was_invalid) {
            // New allocation: increment all other valid buffers
            for (int i = 0; i < PREF_N; i++) {
                if (i != hit_buffer && sb_params[i].valid == 1) {
                    sb_params[i].lru++;
                }
            }
        } else {
            // Existing buffer accessed: only increment those that were more recent
            uint32_t old_lru = sb_params[hit_buffer].lru;
            for (int i = 0; i < PREF_N; i++) {
                if (i != hit_buffer && sb_params[i].valid == 1) {
                    if (sb_params[i].lru < old_lru) {
                        sb_params[i].lru++;
                    }
                }
            }
        }
        
        // Set hit_buffer to MRU
        sb_params[hit_buffer].lru = 0;
    }

    // Find victim stream buffer (invalid or LRU)
    int find_sb_victim() {
        // First, look for invalid entry
        for (int i = 0; i < PREF_N; i++) {
            if (sb_params[i].valid == 0) {
                return i;
            }
        }
        
        // Otherwise, find LRU entry
        for (int i = 0; i < PREF_N; i++) {
            if (sb_params[i].lru == (uint32_t)(PREF_N - 1)) {
                return i;
            }
        }
        
        return 0;  // Fallback
    }

    // Search for SB hit in MRU-first order (per section 4.3)
    // When multiple buffers hit, update only the MRU to avoid redundant prefetches
    bool find_sb_hit(uint32_t block_addr, int &hit_buffer, int &hit_index) {
        hit_buffer = -1;
        hit_index = -1;
        
        if (PREF_N <= 0 || PREF_M <= 0) {
            return false;
        }
        
        // Search in MRU-first order (lru=0, then lru=1, etc.)
        for (uint32_t lru_val = 0; lru_val < (uint32_t)PREF_N; lru_val++) {
            for (int i = 0; i < PREF_N; i++) {
                if (sb_params[i].valid == 1 && sb_params[i].lru == lru_val) {
                    for (int j = 0; j < PREF_M; j++) {
                        if (stream_buffer[i][j] != UINT32_MAX && stream_buffer[i][j] == block_addr) {
                            hit_buffer = i;
                            hit_index = j;
                            return true;
                        }
                    }
                    break;  // Only one buffer per lru_val
                }
            }
        }
        
        return false;
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
            bool cache_hit = (stored_tag == tag_val && calculate_valid(metadata) == 1);

            if (cache_hit) {
                // Cache HIT → Service from cache
                cache_hit_read(tag_val, resp_out);
                
                // But also check stream buffer for Scenario 4 (Cache HIT + SB HIT)
                int hit_buffer = -1;
                int hit_index = -1;
                bool sb_hit = find_sb_hit(get_block_addr(addr), hit_buffer, hit_index);
                
                if (sb_hit) {
                    // Scenario 4: Cache HIT + SB HIT → Continue prefetch stream
                    buffer_rearrange(hit_buffer, hit_index);
                    uint32_t prefetch_addr = addr + (PREF_M - hit_index) * blk_size;
                    for (int pf = 0; pf < (hit_index + 1); pf++) {
                        stream_buffer[hit_buffer][pf + (PREF_M - hit_index - 1)] = get_block_addr(prefetch_addr);
                        prefetches++;
                        prefetch_addr += blk_size;
                    }
                    sb_lru_update(hit_buffer);
                }
            } else {
                // Cache MISS → Check stream buffer (MRU-first search)
                int hit_buffer = -1;
                int hit_index = -1;
                bool sb_hit = find_sb_hit(get_block_addr(addr), hit_buffer, hit_index);
                
                // Increment read_misses only if both cache AND SB miss
                if (!sb_hit) {
                    ++read_misses;
                }

                if (sb_hit) {
                    // Scenario 2: Cache MISS + SB HIT → Allocate in cache from SB, update SB
                    
                    // Handle eviction if needed
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
                    
                    // Allocate in cache (copy from SB conceptually, but we just set metadata)
                    resp_out = 1;
                    cache[set_idx][0][0] = tag_val;
                    uint32_t new_metadata = 0;
                    new_metadata = set_blk_offset(new_metadata, blk_offset);
                    new_metadata = set_lru(new_metadata, 0);
                    new_metadata = set_dirty(new_metadata, 0);
                    new_metadata = set_valid(new_metadata, 1);
                    cache[set_idx][0][1] = new_metadata;
                    
                    // Update stream buffer: rearrange and prefetch (hit_index + 1) blocks
                    buffer_rearrange(hit_buffer, hit_index);
                    uint32_t prefetch_addr = addr + (PREF_M - hit_index) * blk_size;
                    for (int pf = 0; pf < (hit_index + 1); pf++) {
                        stream_buffer[hit_buffer][pf + (PREF_M - hit_index - 1)] = get_block_addr(prefetch_addr);
                        prefetches++;
                        prefetch_addr += blk_size;
                    }
                    sb_lru_update(hit_buffer);
                    
                } else {
                    // Scenario 1: Cache MISS + SB MISS → Fetch from memory, allocate SB with M blocks
                    
                    // Handle eviction if needed
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
                    
                    // Fetch from memory
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
                        
                        // Allocate stream buffer and prefetch M blocks
                        if (PREF_N > 0 && PREF_M > 0) {
                            int victim_sb = find_sb_victim();
                            bool was_invalid = (sb_params[victim_sb].valid == 0);
                            sb_params[victim_sb].valid = 1;
                            
                            // Clear old contents
                            for (int j = 0; j < PREF_M; j++) {
                                stream_buffer[victim_sb][j] = UINT32_MAX;
                            }
                            
                            // Prefetch M blocks: addr+1, addr+2, ..., addr+M
                            uint32_t prefetch_addr = addr;
                            for (int j = 0; j < PREF_M; j++) {
                                prefetch_addr += blk_size;
                                stream_buffer[victim_sb][j] = get_block_addr(prefetch_addr);
                                prefetches++;
                            }
                            
                            sb_lru_update(victim_sb, was_invalid);
                        }
                    }
                }
            }
        } else if (assoc > 1) {
            // Check cache first
            bool cache_hit = false;
            int hit_way = -1;
            
            for (int w = 0; w < assoc; w++) {
                if (w >= way_size) break;
                uint32_t metadata = cache[set_idx][w][1];
                uint32_t stored_tag = cache[set_idx][w][0];

                if (stored_tag == tag_val && calculate_valid(metadata) == 1) {
                    cache_hit = true;
                    hit_way = w;
                    break;
                }
            }

            if (cache_hit) {
                // Cache HIT → Service from cache
                cache_hit_read(tag_val, resp_out);
                uint32_t metadata = cache[set_idx][hit_way][1];
                int old_lru = calculate_lru(metadata);
                lru_update(old_lru, set_idx);
                
                // But also check stream buffer for Scenario 4 (Cache HIT + SB HIT)
                int hit_buffer = -1;
                int hit_index = -1;
                bool sb_hit = find_sb_hit(get_block_addr(addr), hit_buffer, hit_index);
                
                if (sb_hit) {
                    // Scenario 4: Cache HIT + SB HIT → Continue prefetch stream
                    buffer_rearrange(hit_buffer, hit_index);
                    uint32_t prefetch_addr = addr + (PREF_M - hit_index) * blk_size;
                    for (int pf = 0; pf < (hit_index + 1); pf++) {
                        stream_buffer[hit_buffer][pf + (PREF_M - hit_index - 1)] = get_block_addr(prefetch_addr);
                        prefetches++;
                        prefetch_addr += blk_size;
                    }
                    sb_lru_update(hit_buffer);
                }
            } else {
                // Cache MISS → Check stream buffer (MRU-first search)
                int hit_buffer = -1;
                int hit_index = -1;
                bool sb_hit = find_sb_hit(get_block_addr(addr), hit_buffer, hit_index);
                
                // Increment read_misses only if both cache AND SB miss
                if (!sb_hit) {
                    ++read_misses;
                }
                
                // Find victim way
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
                    if (sb_hit) {
                        // Scenario 2: Cache MISS + SB HIT
                        resp_out = 1;  // Data available from stream buffer
                        
                        cache[set_idx][victim_way][0] = tag_val;
                        uint32_t metadata = 0;
                        metadata = set_blk_offset(metadata, blk_offset);
                        metadata = set_dirty(metadata, 0);
                        metadata = set_valid(metadata, 1);
                        cache[set_idx][victim_way][1] = metadata;
                        
                        lru_insert(set_idx, victim_way);
                        
                        // Update stream buffer: rearrange and prefetch (hit_index + 1) blocks
                        buffer_rearrange(hit_buffer, hit_index);
                        uint32_t prefetch_addr = addr + (PREF_M - hit_index) * blk_size;
                        for (int pf = 0; pf < (hit_index + 1); pf++) {
                            stream_buffer[hit_buffer][pf + (PREF_M - hit_index - 1)] = get_block_addr(prefetch_addr);
                            prefetches++;
                            prefetch_addr += blk_size;
                        }
                        sb_lru_update(hit_buffer);
                    } else {
                        // Scenario 1: Cache MISS + SB MISS
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
                            
                            // Allocate stream buffer and prefetch M blocks
                            if (PREF_N > 0 && PREF_M > 0) {
                                int victim_sb = find_sb_victim();
                                bool was_invalid = (sb_params[victim_sb].valid == 0);
                                sb_params[victim_sb].valid = 1;
                                
                                // Clear old contents
                                for (int j = 0; j < PREF_M; j++) {
                                    stream_buffer[victim_sb][j] = 0;
                                }
                                
                                // Prefetch M blocks: addr+1, addr+2, ..., addr+M
                                uint32_t prefetch_addr = addr;
                                for (int j = 0; j < PREF_M; j++) {
                                    prefetch_addr += blk_size;
                                    stream_buffer[victim_sb][j] = get_block_addr(prefetch_addr);
                                    prefetches++;
                                }
                                
                                sb_lru_update(victim_sb, was_invalid);
                            }
                        }
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
            bool cache_hit = (stored_tag == tag_val && calculate_valid(metadata) == 1);
            
            if (cache_hit) {
                // Cache HIT → Mark dirty
                metadata = set_dirty(metadata, 1);
                cache[set_idx][0][1] = metadata;
                resp_out = 1;
                
                // But also check stream buffer for Scenario 4 (Cache HIT + SB HIT)
                int hit_buffer = -1;
                int hit_index = -1;
                bool sb_hit = find_sb_hit(get_block_addr(addr), hit_buffer, hit_index);
                
                if (sb_hit) {
                    // Scenario 4: Cache HIT + SB HIT → Continue prefetch stream
                    buffer_rearrange(hit_buffer, hit_index);
                    uint32_t prefetch_addr = addr + (PREF_M - hit_index) * blk_size;
                    for (int pf = 0; pf < (hit_index + 1); pf++) {
                        stream_buffer[hit_buffer][pf + (PREF_M - hit_index - 1)] = get_block_addr(prefetch_addr);
                        prefetches++;
                        prefetch_addr += blk_size;
                    }
                    sb_lru_update(hit_buffer);
                }
            } else {
                // Cache MISS → Check stream buffer (MRU-first search)
                int hit_buffer = -1;
                int hit_index = -1;
                bool sb_hit = find_sb_hit(get_block_addr(addr), hit_buffer, hit_index);
                
                // Increment write_misses only if both cache AND SB miss
                if (!sb_hit) {
                    ++write_misses;
                }

                if (sb_hit) {
                    // Scenario 2: Cache MISS + SB HIT → Allocate in cache from SB (mark dirty for write)
                    
                    // Handle eviction if needed
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
                    
                    // Allocate in cache (mark dirty for write)
                    resp_out = 1;
                    cache[set_idx][0][0] = tag_val;
                    uint32_t new_metadata = 0;
                    new_metadata = set_blk_offset(new_metadata, blk_offset);
                    new_metadata = set_lru(new_metadata, 0);  
                    new_metadata = set_dirty(new_metadata, 1);  // Mark dirty for write
                    new_metadata = set_valid(new_metadata, 1);
                    cache[set_idx][0][1] = new_metadata;
                    
                    // Update stream buffer: rearrange and prefetch (hit_index + 1) blocks
                    buffer_rearrange(hit_buffer, hit_index);
                    uint32_t prefetch_addr = addr + (PREF_M - hit_index) * blk_size;
                    for (int pf = 0; pf < (hit_index + 1); pf++) {
                        stream_buffer[hit_buffer][pf + (PREF_M - hit_index - 1)] = get_block_addr(prefetch_addr);
                        prefetches++;
                        prefetch_addr += blk_size;
                    }
                    sb_lru_update(hit_buffer);
                    
                } else {
                    // Scenario 1: Cache MISS + SB MISS → Fetch from memory, allocate SB with M blocks
                    
                    // Handle eviction if needed
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
                    
                    // Fetch from memory
                    addr_out = addr;
                    cache_miss_read(addr_out, resp_out);
                    
                    if (resp_out == 1) {
                        cache[set_idx][0][0] = tag_val;
                        uint32_t new_metadata = 0;
                        new_metadata = set_blk_offset(new_metadata, blk_offset);
                        new_metadata = set_lru(new_metadata, 0);  
                        new_metadata = set_dirty(new_metadata, 1);  // Mark dirty for write
                        new_metadata = set_valid(new_metadata, 1);
                        cache[set_idx][0][1] = new_metadata;
                        
                        // Allocate stream buffer and prefetch M blocks
                        if (PREF_N > 0 && PREF_M > 0) {
                            int victim_sb = find_sb_victim();
                            bool was_invalid = (sb_params[victim_sb].valid == 0);
                            sb_params[victim_sb].valid = 1;
                            
                            // Clear old contents
                            for (int j = 0; j < PREF_M; j++) {
                                stream_buffer[victim_sb][j] = UINT32_MAX;
                            }
                            
                            // Prefetch M blocks: addr+1, addr+2, ..., addr+M
                            uint32_t prefetch_addr = addr;
                            for (int j = 0; j < PREF_M; j++) {
                                prefetch_addr += blk_size;
                                stream_buffer[victim_sb][j] = get_block_addr(prefetch_addr);
                                prefetches++;
                            }
                            
                            sb_lru_update(victim_sb, was_invalid);
                        }
                    }
                }
            }
        } else if (assoc > 1) {
            // Check cache first
            bool cache_hit = false;
            int hit_way = -1;

            for (int w = 0; w < assoc; w++) {
                uint32_t metadata = cache[set_idx][w][1];
                uint32_t stored_tag = cache[set_idx][w][0];
                if (stored_tag == tag_val && calculate_valid(metadata) == 1) {
                    cache_hit = true;
                    hit_way = w;
                    break;
                }
            }

            if (cache_hit) {
                // Cache HIT → Mark dirty
                uint32_t metadata = cache[set_idx][hit_way][1];
                int old_lru = calculate_lru(metadata);
                metadata = set_dirty(metadata, 1);
                cache[set_idx][hit_way][1] = metadata;
                resp_out = 1;
                lru_update(old_lru, set_idx);
                
                // But also check stream buffer for Scenario 4 (Cache HIT + SB HIT)
                int hit_buffer = -1;
                int hit_index = -1;
                bool sb_hit = find_sb_hit(get_block_addr(addr), hit_buffer, hit_index);
                
                if (sb_hit) {
                    // Scenario 4: Cache HIT + SB HIT → Continue prefetch stream
                    buffer_rearrange(hit_buffer, hit_index);
                    uint32_t prefetch_addr = addr + (PREF_M - hit_index) * blk_size;
                    for (int pf = 0; pf < (hit_index + 1); pf++) {
                        stream_buffer[hit_buffer][pf + (PREF_M - hit_index - 1)] = get_block_addr(prefetch_addr);
                        prefetches++;
                        prefetch_addr += blk_size;
                    }
                    sb_lru_update(hit_buffer);
                }
            } else {
                // Cache MISS → Check stream buffer (MRU-first search)
                int hit_buffer = -1;
                int hit_index = -1;
                bool sb_hit = find_sb_hit(get_block_addr(addr), hit_buffer, hit_index);
                
                // Increment write_misses only if both cache AND SB miss
                if (!sb_hit) {
                    ++write_misses;
                }
                int victim_way = -1;
                
                // Find victim way
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

                if (victim_way != -1) {
                    if (sb_hit) {
                        // Scenario 2: Cache MISS + SB HIT
                        resp_out = 1;
                        
                        cache[set_idx][victim_way][0] = tag_val;
                        uint32_t metadata = 0;
                        metadata = set_blk_offset(metadata, blk_offset);
                        metadata = set_dirty(metadata, 1);  // Mark dirty for write
                        metadata = set_valid(metadata, 1);
                        cache[set_idx][victim_way][1] = metadata;
                        
                        lru_insert(set_idx, victim_way);
                        
                        // Update stream buffer: rearrange and prefetch (hit_index + 1) blocks
                        buffer_rearrange(hit_buffer, hit_index);
                        uint32_t prefetch_addr = addr + (PREF_M - hit_index) * blk_size;
                        for (int pf = 0; pf < (hit_index + 1); pf++) {
                            stream_buffer[hit_buffer][pf + (PREF_M - hit_index - 1)] = get_block_addr(prefetch_addr);
                            prefetches++;
                            prefetch_addr += blk_size;
                        }
                        sb_lru_update(hit_buffer);
                    } else {
                        // Scenario 1: Cache MISS + SB MISS
                        addr_out = addr;
                        cache_miss_read(addr_out, resp_out);
                        
                        if (resp_out == 1) {
                            cache[set_idx][victim_way][0] = tag_val;
                            uint32_t metadata = 0;
                            metadata = set_blk_offset(metadata, blk_offset);
                            metadata = set_dirty(metadata, 1);  // Mark dirty for write
                            metadata = set_valid(metadata, 1);
                            cache[set_idx][victim_way][1] = metadata;

                            lru_insert(set_idx, victim_way);
                            
                            // Allocate stream buffer and prefetch M blocks
                            if (PREF_N > 0 && PREF_M > 0) {
                                int victim_sb = find_sb_victim();
                                bool was_invalid = (sb_params[victim_sb].valid == 0);
                                sb_params[victim_sb].valid = 1;
                                
                                // Clear old contents
                                for (int j = 0; j < PREF_M; j++) {
                                    stream_buffer[victim_sb][j] = 0;
                                }
                                
                                // Prefetch M blocks: addr+1, addr+2, ..., addr+M
                                uint32_t prefetch_addr = addr;
                                for (int j = 0; j < PREF_M; j++) {
                                    prefetch_addr += blk_size;
                                    stream_buffer[victim_sb][j] = get_block_addr(prefetch_addr);
                                    prefetches++;
                                }
                                
                                sb_lru_update(victim_sb, was_invalid);
                            }
                        }
                    }
                }
            }
        }
    }
};

#endif
