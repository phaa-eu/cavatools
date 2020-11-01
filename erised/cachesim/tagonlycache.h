//
//  tagonlycache.c
//  engines
//
//  Created by Pete Wilson on 4/18/20.
//  Copyright © 2020 Kiva Design Groupe LLC. All rights reserved.
//

#ifndef cache_h
#define cache_h

#include <stdio.h>
#include <stdlib.h>
#include "types.h"
#include "utilities.h"
#include "container.h"
#include "queues.h"

typedef struct cachedata cacheData;

// ----------------- cacheWay -----------------

typedef struct {
	char * name;
	int64 * tags;
	int8 * dirty;
	int8 * valid;
	int64 * timetag;
} cacheWay;

// a miss Request is something internal to the processor.
// a miss is discovered either when trying to fetch an instruction, or when executing a load or store - all at the middle portion of phi0
// at the back end of phi0, the miss is discovered and a Request sent to the appopriate processor output port
// and the missReq returned to the pool
// meanwhile, a LdStore is created in parallel with the memReq.

// there's some duplication of work in this cycle...

typedef struct {
	qBlock header;
	int64 item_address;
} missReq;

// ------------- cacheLookup 'commands'----

typedef enum {read_a_value, read_a_line, read_nothing } cacheLookup;

typedef struct cachedata cacheData;

// -------------- cacheData ----------------

struct cachedata {
	char * name;
	void * owner;			// I'm a cache to what engine?
	int32 isACache;
	int64 counter;
	int64 requests;
	int64 ejections;
	int64 writebacks;

	void * memorySystem;

	cacheData * up;			// the cache above me

	int64 fasthits;			// how many hits we got with the hitvector
	uint8 * hitvector;		//an array of bytes (for an icache only) mapping the code segment
							// for pc N, if byte N is set the instruction is cached

	// managing dcache misses
	Queue * outstanding;	//  queue of outstanding Misses (see above)
	Queue * missPool;
	Queue * triggerPool;

	int64 reads;
	int64 writes;

	int64 hits;
	int64 misses;
	int64 result;

	int req_tag;			// tag used for outgoing requests

	// Geometry
	int32 log2Lines;
	int32 log2LineLength;
	int32 lines;
	int32 length;			// in BYTES

	int32 numways;
	cacheWay ** ways;
};

typedef struct {
	int64 timetag;
	int open;			// if negative, not open
} memPage;

typedef struct {
	char * name;
	int64 timetag;
	int64 reads;
	int64 writes;
	uint32 numpages;
	uint32 curropen;
	uint32 maxopen;
	memPage ** pages;
	uint32 dt_open;
	uint32 dt_closed;
} memoryBank;

typedef struct {
	char * name;
	cacheData * cache;
	uint32 numBanks;
	uint32 maxopen;
	memoryBank ** mbank;
} memorySystem;


typedef struct processordata processorData;

// ------------------ cacheEngine ---------------

//int cacheEngine(Engine * e, int phi);
void * newCacheData(void);
void configureCache(cacheData * cd, char * name, int32 numways, int32 log2LineLength, int32 log2Lines);

void getIndexAndTag(int64 address, cacheData * d, int64 * index_ptr, int64 * tag_ptr);

void printCacheLine(cacheData * cd, int way, int index);
int64 addrToTag(cacheData * cd, int64 address);
int64 tagToAddr(cacheData * cd, int64 tag);
int int64LineLength(cacheData * cd);

int64 addrMaskForCache(cacheData * cd);
int64 addrToLineStartAddr(cacheData * cd, int64 address);

int lookup(char access, int64 address, cacheData * d, cacheWay ** cway);
int fastcachelookup(int64 address, cacheData * d);

void reportCacheStats(cacheData * cache);

void printWay(cacheData * cd, cacheWay * way);
void printCache(cacheData * cache);

int intHitRate(int width, cacheData * cd);


memoryBank * newMemoryBank(int banknum, char * name, int numpages, int maxopen, int dtopen, int dtclosed);
memorySystem * newMemorySystem(char * name, int numbanks, cacheData * cache);

#endif /* tagonlycache_h */
