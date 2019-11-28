#include "Cache.h"

/* Constants */
const unsigned block_size = 64; // Size of a cache line (in Bytes)

// TODO, you should try different size of cache, for example, 128KB, 256KB, 512KB, 1MB, 2MB
const unsigned cache_size = 1024; // Size of a cache (in KB)
// TODO, you should try different association configurations, for example 4, 8, 16
const unsigned assoc = 16;

const unsigned sig_table_size = 2048;
const unsigned counter_bits = 2;

Cache *initCache()
{
    Cache *cache = (Cache *)malloc(sizeof(Cache));

    Sig_table *sig_table = (Sig_table *)malloc(sizeof(Sig_table));
    cache->blk_mask = block_size - 1;

    unsigned num_blocks = cache_size * 1024 / block_size;
    cache->num_blocks = num_blocks;
    cache->index_mask = sig_table_size - 1;
//    printf("Num of blocks: %u\n", cache->num_blocks);

    // Initialize all cache blocks
    cache->blocks = (Cache_Block *)malloc(num_blocks * sizeof(Cache_Block));
    
    int i;
    for (i = 0; i < num_blocks; i++)
    {
        cache->blocks[i].tag = UINTMAX_MAX; 
        cache->blocks[i].valid = false;
        cache->blocks[i].dirty = false;
        cache->blocks[i].when_touched = 0;
        cache->blocks[i].frequency = 0;
        cache->blocks[i].signature = 0;
        cache->blocks[i].outcome = 0;
        initSatCounter(&(cache->blocks[i].srrip_counter), counter_bits);
    }

    cache->shct.counters =
        (Sat_Counter *)malloc(sig_table_size * sizeof(Sat_Counter));

    for (i = 0; i < sig_table_size; i++)
    {
        initSatCounter(&(cache->shct.counters[i]), counter_bits);
	cache->shct.counters[i].counter = 2;
    }


    // Initialize Set-way variables
    unsigned num_sets = cache_size * 1024 / (block_size * assoc);
    cache->num_sets = num_sets;
    cache->num_ways = assoc;
//    printf("Num of sets: %u\n", cache->num_sets);

    unsigned set_shift = log2(block_size);
    cache->set_shift = set_shift;
//    printf("Set shift: %u\n", cache->set_shift);

    unsigned set_mask = num_sets - 1;
    cache->set_mask = set_mask;
//    printf("Set mask: %u\n", cache->set_mask);

    unsigned tag_shift = set_shift + log2(num_sets);
    cache->tag_shift = tag_shift;
//    printf("Tag shift: %u\n", cache->tag_shift);

    // Initialize Sets
    cache->sets = (Set *)malloc(num_sets * sizeof(Set));
    for (i = 0; i < num_sets; i++)
    {
        cache->sets[i].ways = (Cache_Block **)malloc(assoc * sizeof(Cache_Block *));
    }

    // Combine sets and blocks
    for (i = 0; i < num_blocks; i++)
    {
        Cache_Block *blk = &(cache->blocks[i]);
        
        uint32_t set = i / assoc;
        uint32_t way = i % assoc;

        blk->set = set;
        blk->way = way;

        cache->sets[set].ways[way] = blk;
    }

    return cache;
}

bool accessBlock(Cache *cache, Request *req, uint64_t access_time)
{
    bool hit = false;

    uint64_t blk_aligned_addr = blkAlign(req->load_or_store_addr, cache->blk_mask);

    Cache_Block *blk = findBlock(cache, blk_aligned_addr);
   
    if (blk != NULL) 
    {
        hit = true;

        // Update access time	
        blk->when_touched = access_time;
        // Increment frequency counter
        ++blk->frequency;
        
	#ifdef SHIP
   	 uint64_t PC = blk->PC;
   	 unsigned mask = sig_table_size - 1;
   	 unsigned table_index = getIndex(PC, mask);
   	 unsigned counter_value = cache->shct.counters[table_index].counter;
	blk->outcome = 1;	
	incrementCounter(&(cache->shct.counters[table_index]));
	blk->srrip_counter.counter = 0;
	#endif

        if (req->req_type == STORE)
        {
            blk->dirty = true;
        }
    }

    return hit;
}

bool insertBlock(Cache *cache, Request *req, uint64_t access_time, uint64_t *wb_addr)
{
    // Step one, find a victim block
    uint64_t blk_aligned_addr = blkAlign(req->load_or_store_addr, cache->blk_mask);

    Cache_Block *victim = NULL;
    #ifdef LRU
        bool wb_required = lru(cache, blk_aligned_addr, &victim, wb_addr);
    #endif

    #ifdef LFU
        bool wb_required = lfu(cache, blk_aligned_addr, &victim, wb_addr);
    #endif

    #ifdef SHIP
        bool wb_required = srrip(cache, blk_aligned_addr, &victim, wb_addr);
    #endif
    assert(victim != NULL);

    // Step two, insert the new block
    uint64_t tag = req->load_or_store_addr >> cache->tag_shift;
    victim->tag = tag;
    
    victim->valid = true;

    victim->when_touched = access_time;
    ++victim->frequency;
    victim->outcome = 0;
    
    #ifdef SHIP
    
    uint64_t PC = req->PC;
    unsigned mask = sig_table_size - 1;
    unsigned table_index = getIndex(PC, mask);
    unsigned counter_value = cache->shct.counters[table_index].counter;
    victim->PC = req->PC;
    if (counter_value == 0)
    {
        victim->srrip_counter.counter = 3; 
    }
    else
    {
        victim->srrip_counter.counter = 2;
    }


    #endif

    if (req->req_type == STORE)
    {
        victim->dirty = true;
    }

    return wb_required;
//    printf("Inserted: %"PRIu64"\n", req->load_or_store_addr);
}

// Helper Functions
inline uint64_t blkAlign(uint64_t addr, uint64_t mask)
{
    return addr & ~mask;
}

Cache_Block *findBlock(Cache *cache, uint64_t addr)
{
//    printf("Addr: %"PRIu64"\n", addr);

    // Extract tag
    uint64_t tag = addr >> cache->tag_shift;
//    printf("Tag: %"PRIu64"\n", tag);

    // Extract set index
    uint64_t set_idx = (addr >> cache->set_shift) & cache->set_mask;
//    printf("Set: %"PRIu64"\n", set_idx);

    Cache_Block **ways = cache->sets[set_idx].ways;
    int i;
    for (i = 0; i < cache->num_ways; i++)
    {
        if (tag == ways[i]->tag && ways[i]->valid == true)
        {
            return ways[i];
        }
    }

    return NULL;
}

bool lru(Cache *cache, uint64_t addr, Cache_Block **victim_blk, uint64_t *wb_addr)
{
    uint64_t set_idx = (addr >> cache->set_shift) & cache->set_mask;
    //    printf("Set: %"PRIu64"\n", set_idx);
    Cache_Block **ways = cache->sets[set_idx].ways;

    // Step one, try to find an invalid block.
    int i;
    for (i = 0; i < cache->num_ways; i++)
    {
        if (ways[i]->valid == false)
        {
            *victim_blk = ways[i];
            return false; // No need to write-back
        }
    }

    // Step two, if there is no invalid block. Locate the LRU block
    Cache_Block *victim = ways[0];
    for (i = 1; i < cache->num_ways; i++)
    {
        if (ways[i]->when_touched < victim->when_touched)
        {
            victim = ways[i];
        }
    }

    // Step three, need to write-back the victim block
    *wb_addr = (victim->tag << cache->tag_shift) | (victim->set << cache->set_shift);
//    uint64_t ori_addr = (victim->tag << cache->tag_shift) | (victim->set << cache->set_shift);
//    printf("Evicted: %"PRIu64"\n", ori_addr);

    // Step three, invalidate victim
    victim->tag = UINTMAX_MAX;
    victim->valid = false;
    victim->dirty = false;
    victim->frequency = 0;
    victim->when_touched = 0;

    *victim_blk = victim;

    return true; // Need to write-back
}


bool lfu(Cache *cache, uint64_t addr, Cache_Block **victim_blk, uint64_t *wb_addr)
{
    uint64_t set_idx = (addr >> cache->set_shift) & cache->set_mask;
    //    printf("Set: %"PRIu64"\n", set_idx);
    Cache_Block **ways = cache->sets[set_idx].ways;

    // Step one, try to find an invalid block.
    int i;
    for (i = 0; i < cache->num_ways; i++)
    {
        if (ways[i]->valid == false)
        {
            *victim_blk = ways[i];
            return false; // No need to write-back
        }
    }

    // Step two, if there is no invalid block. Locate the LRU block
    Cache_Block *victim = ways[0];
    for (i = 1; i < cache->num_ways; i++)
    {
        if (ways[i]->frequency < victim->frequency)
        {
            victim = ways[i];
        }
    }

    // Step three, need to write-back the victim block
    *wb_addr = (victim->tag << cache->tag_shift) | (victim->set << cache->set_shift);

    // Step three, invalidate victim
    victim->tag = UINTMAX_MAX;
    victim->valid = false;
    victim->dirty = false;
    victim->frequency = 0;
    victim->when_touched = 0;

    *victim_blk = victim;

    return true; // Need to write-back
}

bool srrip(Cache *cache, uint64_t addr, Cache_Block **victim_blk, uint64_t *wb_addr)
{
	uint64_t set_idx = (addr >> cache->set_shift) & cache->set_mask;	
	Cache_Block **ways = cache->sets[set_idx].ways;
	
	int i;
	// Find if there is an empty block
        for (i = 0; i < cache->num_ways; i++)
	{
		if (ways[i]->valid == false)
		{
			*victim_blk = ways[i];
			return false;
		}
	}
	// if there are no empty blocks, we need to find the first 
	// block that has a counter value of 3
	// TODO
	
	int found = 0;
    	Cache_Block *victim;
	while (found == 0)
	{
		for (i = 0; i < cache->num_ways; i++)
		{
			if (ways[i]->srrip_counter.counter == ways[i]->srrip_counter.max_val)
			{
	            		victim = ways[i];
				found = 1;
			}	
		}
		if (found == 0)
		{
			for (i = 0; i < cache->num_ways; i++)
			{
				incrementCounter(&(ways[i]->srrip_counter));
			}
		}
	}

	if (victim->outcome == 0)
	{
   		uint64_t PC = victim->PC;
   		unsigned mask = sig_table_size - 1;
   		unsigned table_index = getIndex(PC, mask);
		decrementCounter(&(cache->shct.counters[table_index]));
		
	}
    	// Step three, need to write-back the victim block
    	*wb_addr = (victim->tag << cache->tag_shift) | (victim->set << cache->set_shift);

	// Step three, invalidate victim
    	victim->tag = UINTMAX_MAX;
    	victim->valid = false;
    	victim->dirty = false;
    	victim->frequency = 0;
    	victim->when_touched = 0;
	victim->outcome = 0;
	

    	*victim_blk = victim;

    return true; // Need to write-back
	
}
// sat counter functions
inline void initSatCounter(Sat_Counter *sat_counter, unsigned counter_bits)
{
    sat_counter->counter_bits = counter_bits;
    sat_counter->counter = 0;
    sat_counter->max_val = (1 << counter_bits) - 1;
}

inline void incrementCounter(Sat_Counter *sat_counter)
{
    if (sat_counter->counter < sat_counter->max_val)
    {
        ++sat_counter->counter;
    }
}

inline void decrementCounter(Sat_Counter *sat_counter)
{
    if (sat_counter->counter > 0)
    {
        --sat_counter->counter;
    }
}

unsigned getIndex(uint64_t branch_addr, unsigned index_mask)
{
    unsigned instShiftAmt = 1;
    return (branch_addr >> instShiftAmt) & index_mask;
}

