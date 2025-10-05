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

function cache_miss_read();
input cache_num,addr_out
output resp_in,

   cache_$"cache_num".cache_read(addr_out,resp_in)