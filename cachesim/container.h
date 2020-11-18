//
//  container.h
//  teq
//
//  Created by Pete Wilson on 6/6/17.
//  Copyright © 2017-2020 Kiva Design Groupe LLC. All rights reserved.
//

#ifndef container_h
#define container_h

#include <stdio.h>
#include "types.h"

// a standard Container holds a dynamically-variable array of pointers
typedef struct {
    char * name;
    int32 max;         // max number of elements
    int32 count;       // how many elements we have
	union {
		char **ptrs;		// array of pointers
		float64 *fvals;		// array of big floats
		int64 *ivals;		// array og ints
	} data;
} Container;

// a charContainer holds a dynamically-variable array of characters
typedef struct {
    char * name;
    int32 max;         // max number of elements
    int32 count;       // how many elements we have
    char * text;      // array of characters
} charContainer;

char * contNameAndVersion(void);

// ---------------- pointer containers

uint32 containerCount(Container * c);
uint32 allocateInContainer(Container * c);

Container * newContainer(uint32 count, char * name);
void zeroContainer(Container * c);
void fprintContainer(FILE * f, Container * c) ;
void printContainer(Container * c);

void * getPtrFromContainer(Container * c, uint64 n);        // get the n'th pointer; don't change the container
void addPtrToContainer(Container * c, void * p);            // add a pointer to the container
void pushPtrToContainer(Container * c, void * ptr);         // push a pointer into a container (same as add)
void * pullPtrFromContainer(Container * c);                 // pull a pointer from a container into ptr; return 1. If none, return 0

void * getTopPtrInContainer(Container * c);                 // if there is a pointer, copies top value into ptr and returns 1; else returns 0

// ----------- character containers

void fprintCharContainer(FILE * f, charContainer * c);
void printCharContainer(charContainer * c);
void zeroCharContainer(Container * c);
uint32 charContainerCount(charContainer * c);
uint32 allocateInCharContainer(charContainer * c) ;
char getCharFromContainer(charContainer * c, uint64 n) ;
void addCharToContainer(charContainer * c, char ch);
void addStringToContainer(charContainer * c, char * string);
char * getContainerAsString(charContainer * c);
charContainer * newCharContainer(uint32 count, char * name);

// -------------- 64 bit value containers -------------

float64 getFloat64FromContainer(Container * c, uint64 n);
int64 getInt64FromContainer(Container * c, uint64 n);
void addInt64ToContainer(Container * c, int64 v);
void addFloat64ToContainer(Container * c, float64 v);


int searchInContainer(Container * c, void * p);
int searchStringInContainer(Container * c, char * p);

#endif /* container_h */

