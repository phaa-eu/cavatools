
//  utilities.h
//  simpleADL
//
//  Created by pete on 1/17/17.
//  Copyright © 2017 Kiva Design. All rights reserved.
//
// edited or touched Nov 6 2018

#ifndef utilities_h
#define utilities_h

#include <stdio.h>
#include "types.h"

char * utilityTitleAndVersion(void);

uint64 timenow(void);
void findent(FILE * f, int depth) ;

void * teqAlloc(char * msg, uint64 size);
void teqFree(char * msg, void * ptr);

char * utGetNameFromPath(char * p);
uint32 isqrt(uint32 n);
uint32 uniform(uint32 stream, uint32 base, uint32 limit);
uint32 triangular(uint32 stream, uint32 base, uint32 limit, uint32 mode);
void initUniform(void);
void findent(FILE * f, int depth);

int64 sext(uint32 n, uint64 v);
void allocError(uint64 n, char * thing, char * filename, int32 linenumber);

#endif /* utilities_h */
