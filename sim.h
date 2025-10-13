
#ifndef SIM_CACHE_H
#define SIM_CACHE_H

#include <cstdint>
#include <vector>
#include <cmath>
#include <iostream>
#include <iomanip>

using namespace std;

typedef struct {
  uint32_t valid;  
   uint32_t lru;    
} sb_info_t;

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

class cache_holder {
public:
    int size, assoc, blk_size;
    uint32_t addr_in;
    uint32_t addr_out;
    int tag_out, resp_in, resp_out, cache_num;
    char operation;
    cache_holder* next_cache;

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



    int PREF_N, PREF_M;
    vector<vector<uint32_t>> stream_buffer;  
    vector<sb_info_t> sb_params;  

    cache_holder(int size, int assoc, int blk_size, uint32_t addr_in,int tag_out, int addr_out, int resp_in, int cache_num, char operation, int pref_n, int pref_m, cache_holder* next = nullptr);

    void decode_addr(uint32_t addr_in_local, int &tag, int &index_bits_local, int &blk_offset_bits_local) {
        if (addr_in_local > ((1ULL << 32) - 1ULL)) 
        {
            cerr << "Error: Address exceeds 32-bit limit. Terminating execution." << endl;
            exit(EXIT_FAILURE);
        }

        int num_sets = size / (assoc * blk_size);
        


        if (blk_size > 1){
            blk_offset_bits_local = (int)ceil(log2(blk_size));
        }
        else {
            blk_offset_bits_local = 0;
        }

        if (num_sets > 1){
            index_bits_local = (int)ceil(log2(num_sets));
        }
        else {
            index_bits_local = 0;
        }

        tag = 32 - index_bits_local - blk_offset_bits_local;
    }

    int calculate_valid(uint32_t metadata) const {
        return metadata & 0x1;
    }

    int calculate_dirty(uint32_t metadata) const {
        return (metadata >> 1) & 0x1;
    }

    int calculate_lru(uint32_t metadata) const 
    {
        if (lru_bits == 0) return 0;
        int mask = (1 << lru_bits) - 1;
        return (metadata >> 2) & mask;
    }

    int calculate_blk_offset(uint32_t metadata) const 
    {
        int shift = 2 + lru_bits;
        if (blk_offset_bits == 0) {
            return 0;
        }
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
        if (lru_bits == 0) 
        {
            return metadata;
        }
        uint32_t mask = (1U << lru_bits) - 1U;
        metadata = (metadata & ~(mask << 2)) | ((lru_val & mask) << 2);
        return metadata;
    }

    uint32_t set_blk_offset(uint32_t metadata, int blk_off) {
        int shift = 2 + lru_bits;
        if (blk_offset_bits == 0) 
        {
            return metadata;
        }
        uint32_t mask = (1U << blk_offset_bits) - 1U;
        metadata = (metadata & ~(mask << shift)) | ((blk_off & mask) << shift);
        
        return metadata;
    }

    void lru_order(int set, int &dirty, uint32_t &eblk_addr) {
        for (int way = 0; way < assoc; way++) 
        {
            if (set < 0 || set >= set_size || way < 0 || way >= way_size) continue;
            uint32_t new_meta = cache[set][way][1];
            uint32_t new_tag = cache[set][way][0];
            if (calculate_valid(new_meta) == 1 && calculate_lru(new_meta) == assoc - 1) {
                dirty = calculate_dirty(new_meta);
                int blk_offset = calculate_blk_offset(new_meta);

                eblk_addr = (new_tag << (index_bits + blk_offset_bits)) |(set << blk_offset_bits) |blk_offset;
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
            } 
            
            else if (current_lru < hit_value) {
                new_meta = set_lru(new_meta, current_lru + 1);
                cache[set][way][1] = new_meta;
            }
        }
    }

    void lru_insert(int set, int new_way) {

        for (int w = 0; w < assoc; w++) {
        
            if (set < 0 || set >= set_size || w < 0 || w >= way_size) 
            {
                continue;
            }
        
            if (w == new_way) 
            {
                continue;
            }

            uint32_t meta = cache[set][w][1];

            if (calculate_valid(meta) == 0) 
            {
                continue;
            }

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
                    if (!found) 
                    {
                        cout << "       ";
                    }
                }
            } 
            
            else {
                if (calculate_valid(cache[s][0][1])) 
                {
                    cout << "   " << setw(6) << hex << cache[s][0][0];
                    if (calculate_dirty(cache[s][0][1])){
                        cout << " D";
                    }
                    else{
                        cout << "  ";
                    }
                } 

                else {
                    cout << "       ";
                }
            }
            cout << endl;
        }
        cout << dec;
    }

    void print_stream_buffers() const {

        if (PREF_N <= 0 || PREF_M <= 0) {
            return; 
        }
        
        cout << "\n===== Stream Buffer(s) contents =====\n";

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
        } 
        else {
            resp_out = 1;
        }
    }

    uint32_t generate_tag(uint32_t addr) {
        return (addr >> (blk_offset_bits + index_bits));
    }

    uint32_t get_block_addr(uint32_t addr) {
        return (addr >> blk_offset_bits);
    }


    void buffer_rearrange(int hit_buffer, int hit_index) {

        int shift_amount = hit_index + 1;
        for (int j = 0; j < PREF_M - shift_amount; j++) {
            stream_buffer[hit_buffer][j] = stream_buffer[hit_buffer][j + shift_amount];
        }

        for (int j = PREF_M - shift_amount; j < PREF_M; j++) {
            stream_buffer[hit_buffer][j] = UINT32_MAX;
        }
    }


    void buffer_refill(int hit_buffer, uint32_t base_addr) {
        int valid_count = 0;
        for (int j = 0; j < PREF_M; j++) {
            if (stream_buffer[hit_buffer][j] != UINT32_MAX) {
                valid_count++;
            } else {
                break;  
            }
        }
        
        if (valid_count >= PREF_M) {
            return;
        }


        uint32_t next_addr = base_addr + (valid_count * blk_size);


        for (int j = valid_count; j < PREF_M; j++) {
            stream_buffer[hit_buffer][j] = get_block_addr(next_addr);
            prefetches++;
            next_addr += blk_size;
        }
    }


    void sb_lru_update(int hit_buffer, bool was_invalid = false) {
        if (was_invalid) 
        {
            for (int i = 0; i < PREF_N; i++) {
                if (i != hit_buffer && sb_params[i].valid == 1) {
                    sb_params[i].lru++;
                }
            }
        } 
        else {
            uint32_t old_lru = sb_params[hit_buffer].lru;
            for (int i = 0; i < PREF_N; i++) {
                if (i != hit_buffer && sb_params[i].valid == 1) {
                    if (sb_params[i].lru < old_lru) {
                        sb_params[i].lru++;
                    }
                }
            }
        }
        
        sb_params[hit_buffer].lru = 0;
    }

    int find_sb_victim() 
    {
        for (int i = 0; i < PREF_N; i++) {
            if (sb_params[i].valid == 0) {
                return i;
            }
        }
        
        for (int i = 0; i < PREF_N; i++) {
            if (sb_params[i].lru == (uint32_t)(PREF_N - 1)) {
                return i;
            }
        }
        
        return 0; 
    }

    bool check_stream_buffers(uint32_t block_addr, int &sb_num, int &sb_pos) {
        sb_num = -1;
        sb_pos = -1;
        
        if (PREF_N <= 0 || PREF_M <= 0) {
            return false;
        }
        

        for (uint32_t lru_level = 0; lru_level < (uint32_t)PREF_N; lru_level++) {
            for (int i = 0; i < PREF_N; i++) {
                if (sb_params[i].valid == 1 && sb_params[i].lru == lru_level) {
                    for (int j = 0; j < PREF_M; j++) {
                        if (stream_buffer[i][j] != UINT32_MAX && stream_buffer[i][j] == block_addr) {
                            sb_num = i;
                            sb_pos = j;
                            return true;  
                        }
                    }
                    break;  
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
                cache_hit_read(tag_val, resp_out);
                int sb_num = -1;
                int sb_pos = -1;
                bool found_in_sb = check_stream_buffers(get_block_addr(addr), sb_num, sb_pos);
                
                if (found_in_sb) {
                    buffer_rearrange(sb_num, sb_pos);  
                    uint32_t pf_addr = addr + (PREF_M - sb_pos) * blk_size;
                    for (int pf = 0; pf < (sb_pos + 1); pf++) 
                    {
                        stream_buffer[sb_num][pf + (PREF_M - sb_pos - 1)] = get_block_addr(pf_addr);
                        prefetches++;
                        pf_addr += blk_size;
                    }
                    sb_lru_update(sb_num);  
                }
            } else {
                int sb_num = -1;
                int sb_pos = -1;
                bool found_in_sb = check_stream_buffers(get_block_addr(addr), sb_num, sb_pos);
                
                if (!found_in_sb) {
                    ++read_misses;
                }

                if (found_in_sb) {

                    if (calculate_valid(metadata) == 1 && stored_tag != tag_val) 
                    {
                        if (calculate_dirty(metadata) == 1)
                        {
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
                    
                    resp_out = 1;
                    cache[set_idx][0][0] = tag_val;
                    uint32_t new_metadata = 0;
                    new_metadata = set_blk_offset(new_metadata, blk_offset);
                    new_metadata = set_lru(new_metadata, 0);
                    new_metadata = set_dirty(new_metadata, 0);
                    new_metadata = set_valid(new_metadata, 1);
                    cache[set_idx][0][1] = new_metadata;
                    
                    buffer_rearrange(sb_num, sb_pos);
                    uint32_t pf_addr = addr + (PREF_M - sb_pos) * blk_size;
                    for (int pf = 0; pf < (sb_pos + 1); pf++) {
                        stream_buffer[sb_num][pf + (PREF_M - sb_pos - 1)] = get_block_addr(pf_addr);
                        prefetches++;
                        pf_addr += blk_size;
                    }
                    sb_lru_update(sb_num);
                    
                } 
                
                else {

                    if (calculate_valid(metadata) == 1 && stored_tag != tag_val) {
                        if (calculate_dirty(metadata) == 1) {
                            ++writebacks;
                            int blk_offset_evict = calculate_blk_offset(metadata);
                            uint32_t eblk_addr = (stored_tag << (index_bits + blk_offset_bits))|(set_idx << blk_offset_bits)|blk_offset_evict;
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

                    if (resp_out == 1) 
                    {
                        cache[set_idx][0][0] = tag_val;
                        uint32_t new_metadata = 0;
                        new_metadata = set_blk_offset(new_metadata, blk_offset);
                        new_metadata = set_lru(new_metadata, 0);
                        new_metadata = set_dirty(new_metadata, 0);
                        new_metadata = set_valid(new_metadata, 1);
                        cache[set_idx][0][1] = new_metadata;
                        

                        if (PREF_N > 0 && PREF_M > 0) {

                            int empty_sb = find_sb_victim();
                            bool was_invalid = (sb_params[empty_sb].valid == 0);
                            sb_params[empty_sb].valid = 1;
                            
                            for (int j = 0; j < PREF_M; j++) {
                                stream_buffer[empty_sb][j] = UINT32_MAX;
                            }
                            
                            uint32_t pf_addr = addr;
                            for (int j = 0; j < PREF_M; j++) {
                                pf_addr += blk_size;
                                stream_buffer[empty_sb][j] = get_block_addr(pf_addr);
                                prefetches++;
                            }
                            
                            sb_lru_update(empty_sb, was_invalid);
                        }
                    }
                }
            }
        } 
        
        
        else if (assoc > 1) {
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

                cache_hit_read(tag_val, resp_out);
                uint32_t metadata = cache[set_idx][hit_way][1];
                int old_lru = calculate_lru(metadata);
                lru_update(old_lru, set_idx);
                
                int sb_num = -1;
                int sb_pos = -1;
                bool found_in_sb = check_stream_buffers(get_block_addr(addr), sb_num, sb_pos);
                
                if (found_in_sb) {

                    buffer_rearrange(sb_num, sb_pos);  

                    uint32_t pf_addr = addr + (PREF_M - sb_pos) * blk_size;
                    for (int pf = 0; pf < (sb_pos + 1); pf++) {
                        stream_buffer[sb_num][pf + (PREF_M - sb_pos - 1)] = get_block_addr(pf_addr);
                        prefetches++;
                        pf_addr += blk_size;
                    }
                    sb_lru_update(sb_num); 
                }
            } 
            
            else {

                int sb_num = -1;
                int sb_pos = -1;
                bool found_in_sb = check_stream_buffers(get_block_addr(addr), sb_num, sb_pos);
                
                if (!found_in_sb) {
                    ++read_misses;
                }
                

                int victim_way = -1;

                for (int w = 0; w < assoc; w++) {
                    if (w >= way_size) {
                        break;
                    }
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
                        if (w >= way_size){
                            break;
                        }
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
                    if (found_in_sb) {

                        resp_out = 1;
                        
                        cache[set_idx][victim_way][0] = tag_val;
                        uint32_t metadata = 0;
                        metadata = set_blk_offset(metadata, blk_offset);
                        metadata = set_dirty(metadata, 0);
                        metadata = set_valid(metadata, 1);
                        cache[set_idx][victim_way][1] = metadata;
                        
                        lru_insert(set_idx, victim_way);
                        

                        buffer_rearrange(sb_num, sb_pos);
                        uint32_t pf_addr = addr + (PREF_M - sb_pos) * blk_size;
                        for (int pf = 0; pf < (sb_pos + 1); pf++) {
                            stream_buffer[sb_num][pf + (PREF_M - sb_pos - 1)] = get_block_addr(pf_addr);
                            prefetches++;
                            pf_addr += blk_size;
                        }
                        sb_lru_update(sb_num);
                    } 
                    
                    else {

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
                            
                            if (PREF_N > 0 && PREF_M > 0) {
                                int victim_sb = find_sb_victim();
                                bool was_invalid = (sb_params[victim_sb].valid == 0);
                                sb_params[victim_sb].valid = 1;
                                
                                for (int j = 0; j < PREF_M; j++) {
                                    stream_buffer[victim_sb][j] = 0;
                                }
                                
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

        int blk_offset =0;
        if (blk_offset_bits != 0) {
            blk_offset =(addr & ((1 << blk_offset_bits) - 1));
        }

        int set_idx =0;
        if (index_bits != 0) {
            set_idx = ((addr >> blk_offset_bits) & ((1 << index_bits) - 1));
        }

        uint32_t tag_val = (addr >> (blk_offset_bits + index_bits));

        ++writes;
        
        if (assoc == 1) {
            uint32_t metadata = cache[set_idx][0][1];
            uint32_t stored_tag = cache[set_idx][0][0];
            bool cache_hit = (stored_tag == tag_val && calculate_valid(metadata) == 1);
            
            if (cache_hit) {
                metadata = set_dirty(metadata, 1);
                cache[set_idx][0][1] = metadata;
                resp_out = 1;
                

                int sb_num = -1;
                int sb_pos = -1;
                bool found_in_sb = check_stream_buffers(get_block_addr(addr), sb_num, sb_pos);
                
                if (found_in_sb) {

                    buffer_rearrange(sb_num, sb_pos);  

                    uint32_t pf_addr = addr + (PREF_M - sb_pos) * blk_size;
                    for (int pf = 0; pf < (sb_pos + 1); pf++) {
                        stream_buffer[sb_num][pf + (PREF_M - sb_pos - 1)] = get_block_addr(pf_addr);
                        prefetches++;
                        pf_addr += blk_size;
                    }
                    sb_lru_update(sb_num);  
                }
            } 
            
            else {

                int sb_num = -1;
                int sb_pos = -1;
                bool found_in_sb = check_stream_buffers(get_block_addr(addr), sb_num, sb_pos);
                
                if (!found_in_sb) {
                    ++write_misses;
                }

                if (found_in_sb) {
                    
                    if (calculate_valid(metadata) == 1 && stored_tag != tag_val) {
                        if (calculate_dirty(metadata) == 1) {
                            ++writebacks;
                            int blk_offset_evict = calculate_blk_offset(metadata);
                            uint32_t eblk_addr = (stored_tag << (index_bits + blk_offset_bits)) |(set_idx << blk_offset_bits) |blk_offset_evict;
                            int write_resp;
                            if (next_cache != nullptr) 
                            {
                                next_cache->cache_write(eblk_addr, write_resp);

                                if (write_resp != 1) {
                                    cerr << "Error: Write-back to lower level failed." << endl;
                                    exit(EXIT_FAILURE);
                                }
                            }
                        }
                    }
                    
                    resp_out = 1;
                    cache[set_idx][0][0] = tag_val;
                    uint32_t new_metadata = 0;
                    new_metadata = set_blk_offset(new_metadata, blk_offset);
                    new_metadata = set_lru(new_metadata, 0);  
                    new_metadata = set_dirty(new_metadata, 1);  
                    new_metadata = set_valid(new_metadata, 1);
                    cache[set_idx][0][1] = new_metadata;
                    
                    buffer_rearrange(sb_num, sb_pos);
                    uint32_t pf_addr = addr + (PREF_M - sb_pos) * blk_size;


                    for (int pf = 0; pf < (sb_pos + 1); pf++) {
                        stream_buffer[sb_num][pf + (PREF_M - sb_pos - 1)] = get_block_addr(pf_addr);
                        prefetches++;
                        pf_addr += blk_size;
                    }
                    sb_lru_update(sb_num);
                    
                }
                
                else {

                    if (calculate_valid(metadata) == 1 && stored_tag != tag_val) {
                        if (calculate_dirty(metadata) == 1) {
                            ++writebacks;
                            int blk_offset_evict = calculate_blk_offset(metadata);
                            uint32_t eblk_addr = (stored_tag << (index_bits + blk_offset_bits))|(set_idx << blk_offset_bits)|blk_offset_evict;
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
                        
                        // Creating new stream buffer and prefetch
                        if (PREF_N > 0 && PREF_M > 0) {
                            int empty_sb = find_sb_victim();
                            bool was_invalid = (sb_params[empty_sb].valid == 0);
                            sb_params[empty_sb].valid = 1;
                            
                            for (int j = 0; j < PREF_M; j++) {
                                stream_buffer[empty_sb][j] = UINT32_MAX;
                            }
                            
                            uint32_t pf_addr = addr;

                            for (int j = 0; j < PREF_M; j++) {
                                pf_addr += blk_size;
                                stream_buffer[empty_sb][j] = get_block_addr(pf_addr);
                                prefetches++;
                            }
                            
                            sb_lru_update(empty_sb, was_invalid);
                        }
                    }
                }
            }
        } 
        
        else if (assoc > 1) {
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

                uint32_t metadata = cache[set_idx][hit_way][1];
                int old_lru = calculate_lru(metadata);
                metadata = set_dirty(metadata, 1);
                cache[set_idx][hit_way][1] = metadata;
                resp_out = 1;
                lru_update(old_lru, set_idx);
                
                int sb_num = -1;
                int sb_pos = -1;
                bool found_in_sb = check_stream_buffers(get_block_addr(addr), sb_num, sb_pos);
                
                if (found_in_sb) {

                    buffer_rearrange(sb_num, sb_pos);  

                    uint32_t pf_addr = addr + (PREF_M - sb_pos) * blk_size;
                    for (int pf = 0; pf < (sb_pos + 1); pf++) {
                        stream_buffer[sb_num][pf + (PREF_M - sb_pos - 1)] = get_block_addr(pf_addr);
                        prefetches++;
                        pf_addr += blk_size;
                    }
                    sb_lru_update(sb_num);  
                }
            } 
            
            else {

                int sb_num = -1;
                int sb_pos = -1;
                bool found_in_sb = check_stream_buffers(get_block_addr(addr), sb_num, sb_pos);
                
                if (!found_in_sb) {
                    ++write_misses;
                }
                int victim_way = -1;
                
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
                                if (next_cache != nullptr) 
                                {
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
                    if (found_in_sb) {
                        resp_out = 1;
                        
                        cache[set_idx][victim_way][0] = tag_val;
                        uint32_t metadata = 0;
                        metadata = set_blk_offset(metadata, blk_offset);
                        metadata = set_dirty(metadata, 1);  
                        metadata = set_valid(metadata, 1);
                        cache[set_idx][victim_way][1] = metadata;
                        
                        lru_insert(set_idx, victim_way);
                        
                        buffer_rearrange(sb_num, sb_pos);
                        uint32_t pf_addr = addr + (PREF_M - sb_pos) * blk_size;
                        for (int pf = 0; pf < (sb_pos + 1); pf++) {
                            stream_buffer[sb_num][pf + (PREF_M - sb_pos - 1)] = get_block_addr(pf_addr);
                            prefetches++;
                            pf_addr += blk_size;
                        }
                        sb_lru_update(sb_num);
                    } 
                    
                    else {
                        addr_out = addr;
                        cache_miss_read(addr_out, resp_out);
                        
                        if (resp_out == 1) {
                            cache[set_idx][victim_way][0] = tag_val;
                            uint32_t metadata = 0;
                            metadata = set_blk_offset(metadata, blk_offset);
                            metadata = set_dirty(metadata, 1);  
                            metadata = set_valid(metadata, 1);
                            cache[set_idx][victim_way][1] = metadata;

                            lru_insert(set_idx, victim_way);
                            
                            if (PREF_N > 0 && PREF_M > 0) {
                                int victim_sb = find_sb_victim();
                                bool was_invalid = (sb_params[victim_sb].valid == 0);
                                sb_params[victim_sb].valid = 1;
                                

                                for (int j = 0; j < PREF_M; j++) 
                                {
                                    stream_buffer[victim_sb][j] = 0;
                                }
                                
                                uint32_t prefetch_addr = addr;

                                for (int j = 0; j < PREF_M; j++) 
                                {
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