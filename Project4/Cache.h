#ifndef __CACHE_H__
#define __CACHE_H__

#include <assert.h>

#include <stdio.h>
#include <stdlib.h>

#include <math.h>
#include <stdint.h>

#include "Cache_Blk.h"
#include "Request.h"

//#define LRU
//#define LFU
#define SHIP



/* Cache */
typedef struct Set
{
    Cache_Block **ways; // Block ways within a set
}Set;

typedef struct Sig_table
{
	Sat_Counter* counters;
}Sig_table;

typedef struct Cache
{
    uint64_t blk_mask;
    unsigned num_blocks;
    unsigned index_mask;
    
    Cache_Block *blocks; // All cache blocks

    /* Set-Associative Information */
    unsigned num_sets; // Number of sets
    unsigned num_ways; // Number of ways within a set

    unsigned set_shift;
    unsigned set_mask; // To extract set index
    unsigned tag_shift; // To extract tag

    Set *sets; // All the sets of a cache
    Sig_table shct;
    
}Cache;

// Function Definitions
Cache *initCache();
bool accessBlock(Cache *cache, Request *req, uint64_t access_time);
bool insertBlock(Cache *cache, Request *req, uint64_t access_time, uint64_t *wb_addr);

// Helper Function
uint64_t blkAlign(uint64_t addr, uint64_t mask);
Cache_Block *findBlock(Cache *cache, uint64_t addr);

void initSatCounter(Sat_Counter *sat_counter, unsigned counter_bits);
void incrementCounter(Sat_Counter *sat_counter);
void decrementCounter(Sat_Counter *sat_counter);
unsigned getIndex(uint64_t branch_addr, unsigned index_mask);
// Replacement Policies
bool lru(Cache *cache, uint64_t addr, Cache_Block **victim_blk, uint64_t *wb_addr);
bool lfu(Cache *cache, uint64_t addr, Cache_Block **victim_blk, uint64_t *wb_addr);
bool srrip(Cache *cache, uint64_t addr, Cache_Block **victim_blk, uint64_t *wb_addr);


#endif
