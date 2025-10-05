class cache_wrapper(size,assoc,blk_size,addr_in,tag_out,addr_out,resp_in,cache_num){

void decode_addr(uint64_t addr_in, int size, int assoc, int blk_size,
                 int &tag, int &index_bits, int &blk_offset_bits) {
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

   decode_addr(addr_in,tag,index,blk_offset);

   set = 2**index - 1;
   tag_depth = 2**tag - 1;
   way = assoc - 1;
   meta_data = 2;
   cache [tag_depth][set][way][meta_data];

   //Initialize everything to 0
   for (int set=0; set<max(set); set++){
      for (int way=0; way<assoc; way++){
         for (int meta_data=0; meta_data<2; meta_data++){
            cache [set][way][meta_data] = 0;
         }
      }
   }

   if (way == 0){
      if(cache[set][0][0] == tag){
         meta_data = cache[set][0][1];
         if (meta_data[0] == 1){
            cache_hit_read()
         }
      else cache_miss_read();
      }
   }
   else {
   for (int way=0; way<assoc; way++){
      if(cache[set][way][0] == tag){
         meta_data = cache[set][way][1];
         if (meta_data[0] == 1){
            cache_hit_read()
         }
      else cache_miss_read();
      }
}

}

function lru_order(input set,assoc, output dirty, eblk_addr){
//eblk_addr is the address of the block to be evicted
for (int way =0; way<assoc-1;way++){
   new_meta = cache[set][way][1];
   new_tag = cache[set][way][0];

   if (get_lru(new_meta) == max(assoc)-1){
      dirty = get_dirty(new_meta);
      blk_offset = get_blk_offset(new_meta);
      eblk_addr = {new_tag, set, blk_offset};
      }
   else
   continue;
   }
}

function lru_update(input hit_value,set, assoc, output update_done){
   for (int way =0; way<assoc-1;way++){
      new_meta = cache[set][way][1];
      if (get_lru(new_meta) == hit_value){
         set_lru(new_meta,0);
         cache[set][way][1] = new_meta;
      else if (get_lru(new_meta) < hit_value){
         old_lru = get_lru(new_meta);
         set_lru(new_meta,old_lru+1);
         cache[set][way][1] = new_meta;
      }
      }
            else continue;
}



function cache_miss_read();
input cache_num,addr_out
output resp_in,

   cache_$"cache_num".cache_read(addr_out,resp_in)

   cache[set][way][0] = tag














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
                    addr_out = addr;  // Send address to next level
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
            }
             else {
                // Tag not found - MISS
                int metadata = cache[set_idx][0][1];
                if (get_valid(metadata) == 0) {
                    // Found an invalid way - allocate here
                    addr_out = addr;  // Send address to next level
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
                } else if (get_valid(metadata) == 1 && get_dirty(metadata) == 1) {
                  new_tag = cache[set_idx][0][0];
                  new_meta = cache[set_idx][0][1];
                  blk_offset_new = get_blk_offset(new_meta);
                  eblk_addr = (new_tag << (index_bits + blk_offset_bits)) | (set_idx << blk_offset_bits) | blk_offset;
                  addr_out = eblk_addr;
                  cache_"#cache_num+1".cache_write(addr_out, resp_in);  // Write back to lower level
                  if (resp_in != 1) {
                     cerr << "Error: Write-back to lower level failed." << endl;
                     exit(EXIT_FAILURE);
                  } else {
                     // Write-back successful, update cache metadata
                     cache_"#cache_num+1".cache_read(addr, resp_in);
                     if (resp_in == 1) {  // If lower level returned a hit
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
            }











            
        } else if (assoc > 1) {
            // Set associative: check all ways
            for (int w = 0; w < assoc; w++) {
                if (cache[set_idx][w][0] == tag_val) {
                    int metadata = cache[set_idx][w][1];
                    if (get_valid(metadata) == 1) {  // Check valid bit
                        cache_hit_read(tag_val, resp_out);  // HIT in this level
                    } else {
                        addr_out = addr;  // Send address to next level
                        cache_miss_read(addr_out, resp_in);  // Query next level

                        if (resp_in == 1) {  // If lower level returned a hit
                        cache[set_idx][w][0] = tag_val;  // Allocate new tag
                        // Build metadata using helper functions
                        int metadata = 0;  // Start with all zeros
                        metadata = set_blk_offset(metadata, blk_offset);  // Set block offset



















                addr_out = addr;  // Send address to next level
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
                        addr_out = addr;  // Send address to next level
                        cache_miss_read(addr_out, resp_in);  // Query next level

                        if (resp_in == 1) {  // If lower level returned a hit
                        cache[set_idx][w][0] = tag_val;  // Allocate new tag
                        // Build metadata using helper functions
                        int metadata = 0;  // Start with all zeros
                        metadata = set_blk_offset(metadata, blk_offset);  // Set block offset
                        metadata = set_lru(metadata, 0);                  // Set LRU = 0
                        metadata = set_dirty(metadata, 0);                // Set dirty = 0
                        metadata = set_valid(metadata, 1);                // Set valid = 1
                        // After updating the lru of this way, increment lru of others
                        // need to update lru of other ways
                        //No need to worry about dirty bit as this block is invalid in the first place
                        cache[set_idx][w][1] = metadata;  // Store metadata
                }
                    }
                    return;  // Exit after handling hit/miss
                }
                else {
                    // Find the highest LRU way for replacement, check if they are invalid or dirty, if so get its tag,index and blk_offset, concat and make addr_out.
                    //do a writeback and then allocate the new block
                    uint64_t eblk_addr = 0;
                    int dirty = 0;
                    lru_order(set_idx, assoc, dirty, eblk_addr);  // Get address of block to evict
                    if (dirty == 1) {
                        // Write back to lower level
                        addr_out = eblk_addr;  // Address to write back
                        cache_"#cache_num+1".cache_write(addr_out, resp_in);  // Write back to lower level
                        // Implement write-back logic here if needed
                        if (resp_in != 1) {
                            cerr << "Error: Write-back to lower level failed." << endl;
                            exit(EXIT_FAILURE);
                        }
                        else {
                            cache_"#cache_num+1".cache_read(addr, resp_in);
                        if (resp_in == 1) {  // If lower level returned a hit
                        cache[set_idx][w][0] = tag_val;  // Allocate new tag
                        // Build metadata using helper functions
                        int metadata = 0;  // Start with all zeros
                        metadata = set_blk_offset(metadata, blk_offset);  // Set block offset
                        metadata = set_lru(metadata, 0);                  // Set LRU = 0
                        metadata = set_dirty(metadata, 0);                // Set dirty = 0
                        metadata = set_valid(metadata, 1);                // Set valid = 1
                        // After updating the lru of this way, increment lru of others
                        //No need to worry about dirty bit as this block is invalid in the first place
                        cache[set_idx][w][1] = metadata;  // Store metadata
                        }
                    }
                    }

                }
            }
        }
    }
