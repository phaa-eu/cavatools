//
//  cache.c
//  engines
//
//  Created by Pete Wilson on 4/18/20.
//  Copyright © 2020 Kiva Design Groupe LLC. All rights reserved.
//

#define EXTERN extern

#define gTraceAll 	(0)
#define gReport 	(0)

#include <string.h>
#include "tagonlycache.h"

char * title = "tagonlycache 0.1v2 [Sept 17 2020]";

/*
 Versions
=========

 - tagonlycache 0.1v2 [Sept 17 2020]
	- serious error in the computation of the index in the cache from tag corrcted.

 0.1v1 - not committed; basic code from the standard cache source
 */


// ----------- addrMaskForCache ------------

int64 addrMaskForCache(cacheData * cd) {
	if (gTraceAll && gReport) printf("\n--addrMaskForCache %s: l2l = %d linelength=%d; ", cd->name, cd -> log2LineLength, 1 << (cd -> log2LineLength));
	int64 mask = (1 << (cd -> log2Lines)) - 1;
	if (gTraceAll && gReport) printf(": mask = 0x%08llx", mask);
	return mask;
}

// ------------ addrToLineStartAddr -------------

int64 addrToLineStartAddr(cacheData * cd, int64 address) {
	if (gTraceAll && gReport) printf("\n--addrToLineStartAddr %s: l2l = %d - address = 0x%08llx %lld; ", cd->name, cd -> log2Lines, address, address);
	int64 mask = addrMaskForCache(cd);
	if (gTraceAll && gReport) printf("\t notmask = 0x%08llx", ~mask);
	int64 start = address & (~mask);
	if (gTraceAll && gReport) printf("\t start = 0x%08llx", start);
	return start;
}

// ------------- addrToTag ---------------------

int64 addrToTag(cacheData * cd, int64 address) {
	if (gTraceAll && gReport) printf("\n\taddrToTag: addr is %lld 0x%llx", address, address);
	int64 tag = address >> (cd -> log2LineLength);
	return tag;
}

// ------------- tagToAddr ---------------------

int64 tagToAddr(cacheData * cd, int64 tag) {
	int64 addr = tag << (cd -> log2LineLength);
	return addr;
}

// -------------- getIndexAndTag --------------------

void getIndexAndTag(int64 address, cacheData * d, int64 * index_ptr, int64 * tag_ptr) {
	// we have a byte address coming in
	// compute the tag
	int64 temp = addrToTag(d, address);
	// given a tag, decide which line that should go to. mask the tag with a mask based on number of lines
	int64 mask = addrMaskForCache(d);
	int64 index = temp & mask;
	int64 tag = temp;
	*index_ptr = index;
	*tag_ptr = tag;
	if (gTraceAll && gReport) printf("\ngetIndexAndTag 0x%08llx (%lld) : linelength = %d bytes:: index = %d temp = 0x%08x mask = 0x%08x tagval = 0x%08x\n",
						address, address, d -> length, (int)index, (int)temp, (int)mask, (int)tag);
}

// -------------- int64LineLength --------------

int int64LineLength(cacheData * cd) {
	int len = cd -> length;
	len = len >> 3;
	return len;
}

// -------------- fastcachelookup -------------------

int fastcachelookup(int64 address, cacheData * d) {
	// read the address for lookup
	// compute index into the cache, and the matching value for the tag
//	return 1;
	// see if the address is set in the hitvector
	d -> reads++;
	if (d -> hitvector[address]) {
		d -> hits++;
		d -> fasthits++;
		return 1;
	}
	else {
		// gotta do a real cache lookup
		if (gTraceAll && gReport) printf("\nlookup address 0x%08llx ", address);

		// look up the address in the cache's hi
		int64 index, tag;
		getIndexAndTag(address, d, &index, &tag);

		if (gTraceAll && gReport) printf("\n\t\tindex = %lld; tag = 0x%08llx", index,  tag);

		for (int way = 0; way < d -> numways; way++) {
			cacheWay * cw = d -> ways[way];
			if ((cw -> tags[index] == tag) && (cw -> valid[index])) {
				// hit!
				if (gTraceAll && gReport) printf("\t- hit!");
				return 1;
			}
		}

		if (gTraceAll && gReport) printf("\t- MISS!");
		d -> misses++;
		// we missed. choose a way at random, eject the incumbent if any and install the new
		int way = uniform(0, 0, d -> numways);
		cacheWay * cw = d -> ways[way];

		if (cw -> valid[index] == 0) {
			// empty
			cw -> tags[index] = tag;
			cw -> valid[index] = 1;
			// mark the hitvector. first, get the address corresponding to the start of the cache line
			// we have computed the tag before
			int64 addr = tagToAddr(d, tag);
			int n = d -> length;
			for (int i = 0; i < n; i++) {
				d -> hitvector[addr++] = 1;
			}
		}
		else {
			// find the address for the line we're ejecting
			int64 addr = tagToAddr(d, cw -> tags[index]);
			int n = d -> length;
			for (int i = 0; i < n; i++) {
				d -> hitvector[addr++] = 0;
			}
			// eject the line
			d -> ejections++;
			cw -> tags[index] = tag;
			cw -> valid[index] = 1;

			// set up the hitvector
			addr = tagToAddr(d, tag);
			n = d -> length;
			for (int i = 0; i < n; i++) {
				d -> hitvector[addr++] = 1;
			}

		}
		return 0;
	}
}

// -------------- lookup -------------------

int lookup(char access, int64 address, cacheData * cache, cacheWay ** cway) {
	// read the address for lookup
	// compute index into the cache, and the matching value for the tag

	// look up the address in the cache's hi
	int64 index, tag;
	getIndexAndTag(address, cache, &index, &tag);
	if (access == 'r') cache -> reads++;
	else if (access == 'w') cache -> writes++;

	for (int way = 0; way < cache -> numways; way++) {
		cacheWay * cw = cache -> ways[way];
		if ((cw -> tags[index] == tag) && (cw -> valid[index])) {
			// hit!
			// note the hit
			cache -> hits++;
			// if it's a write, make it dirty
			if (access == 'w') cw -> dirty[index] = 1;
			*cway = cw;
			return 1;
		}
	}

	cache -> misses++;
	// we missed. choose a way at random, eject the incumbent if any and install the new
	int way = uniform(0, 0, cache -> numways);
	cacheWay * cw = cache -> ways[way];
	if (cw -> valid[index] == 0) {
		// a cacheline without validity
		cw -> tags[index] = tag;
		cw -> valid[index] = 1;
		cw -> dirty[index] = 0;
	}
	else {
		// we already have a cacheline here, so eject the line
		if (cw -> dirty[index]) {
			cache -> ejections++;
		}
		cw -> tags[index] = tag;
		cw -> valid[index] = 1;
		cw -> dirty[index] = 0;
	}
	*cway = cw;
	return 0;
}

// -------------- newCacheData -----------

void * newCacheData(void) {
	cacheData * cd = malloc(sizeof(cacheData));
	if (cd) {
		cd -> isACache = 0xcace;
		cd -> counter = 0;
		cd -> requests = 0;
		cd -> owner = NULL;
	}
	return cd;
}

// -------------- configureCache -------------

void configureCache(cacheData * cd, char * name, int32 numways, int32 log2LineLength, int32 log2Lines) {
	// sets up the specified cacheData
	cd -> name = strdup(name);

	cd -> hits = 0;
	cd -> fasthits = 0;
	cd -> misses = 0;
	cd -> ejections = 0;

	cd -> reads = 0;
	cd -> writes = 0;

	cd -> lines =  1 << log2Lines;
	cd -> length = 1 << log2LineLength;		// BYTES

	cd -> log2Lines = log2Lines;
	cd -> log2LineLength = log2LineLength;

	cd -> numways = numways;

	cd -> ejections = 0;
	cd -> writebacks = 0;

	cd -> outstanding = qNewQ("outstanding misses");

	if (gTraceAll && gReport) {
		printf("\nConfiguring cache %s:", name);
		printf("\n\tways         = %d", numways);
		printf("\n\tline length  = %d bytes", cd -> length);
		printf("\n\tlines per way= %d", cd -> lines);
	}

	cd -> ways = malloc(numways * sizeof(cacheWay *));

	// initialise all the cache ways
	for (int32 way = 0; way < numways; way++) {
		cacheWay * cw = malloc(sizeof(cacheWay));
		char * wayname = malloc(64);
		sprintf(wayname, "way[%d]", way);
		cw -> name = wayname;
		cd -> ways[way] = cw;
		cw -> tags = malloc(cd -> lines * sizeof(int64));
		cw -> valid = malloc(cd -> lines * sizeof(int8));
		cw -> dirty = malloc(cd -> lines * sizeof(int8));
		cw -> timetag = malloc(cd -> lines * sizeof(int64));

		for (int line = 0; line < cd -> lines; line++) {
			cw -> tags[line] = 0;
			cw -> dirty[line] = 0;
			cw -> valid[line] = 0;
			cw -> timetag[line] = 0;
		}
	}
}

// ------------- intHitRate --------------------

int intHitRate(int width, cacheData * cd) {
	int v;
//	printf("\nintHitRate: for cache '%s'. Reads = %lld writes = %lld", cd -> name, cd -> reads, cd -> writes);
	if ((cd -> reads + cd -> writes) > 0) {
		float hits = ((float)cd -> hits/(float)(cd -> reads + cd -> writes)) * (float) width;
		v = (int) hits;
		if (v > 100) {
			printf("\n--%d--", v);
		}
	}
	else {
		v = 0;
	}
	return v;
}

// -------------- reportCacheStats ----------------

void reportCacheStats(cacheData * cache) {
//	if ((cpu -> currtime != 0) && (final == 1)) {
	printf("\n\nConfiguration for cache '%s':", cache -> name);
		printf("\n======================================================");
		for (int i = 0; i < strlen(cache -> name); i++) {
			putchar('=');
		}
		printf("\n\tways                = %d", cache -> numways);
		printf("\n\tline length         = %d bytes", cache -> length);
		printf("\n\tlines per way       = %d", cache -> lines);
		int capacity =((cache -> numways) * cache -> lines) * (cache -> length);
		if (capacity > 1024) {
			printf("\n\tCapacity            = %dKB", capacity / 1024);
		}
		else {
			printf("\n\tCapacity          = %dB", capacity);
		}
		printf("\n\nStatistics:");
		printf("\n\treads               = %lld", cache -> reads);
		printf("\n\twrites              = %lld", cache -> writes);
		printf("\n\tejections           = %lld", cache -> ejections);
		printf("\n\tfasthits            = %lld", cache -> fasthits);
		printf("\n\thits                = %lld", cache -> hits);
		printf("\n\tmisses              = %lld", cache -> misses);
		printf("\n\thit rate            = %.1f%%",((float)cache -> hits/(float)(cache -> reads + cache -> writes)) * 100.0);
	}


// ------------------- printWay -------------------

void printWay(cacheData * cd, cacheWay * way) {
	// prints out the cacheway
	int lines = cd -> lines;
	for (int i = 0; i < lines; i++) {
		printf("\n\t");
		if (way -> dirty[i]) printf("d"); else printf("-");
		if (way -> valid[i]) printf("V"); else printf("-");
		printf("\t0x%08llx\t", way -> tags[i]);
	}
}

// ------------------- printCache -------------------

void printCache(cacheData * cache) {
	// prints out the complete contents of the specified cache
	printf("\nState of cache: %s", cache -> name);
	cacheWay ** cw = cache -> ways;
	for (int i = 0; i < cache -> numways; i++) {
		printf("\n\nWay[%d]", i);
		printWay(cache, cw[i]);
	}
}
