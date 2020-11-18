//
//  container.c
//  teq
//
//  Created by Pete Wilson on 6/6/17.
//  Copyright © 2017-2020 Kiva Design Groupe LLC. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXTERN extern

#include "types.h"
#include "container.h"

extern void allocError(uint64 n, char * thing, char * filename, int32 linenumber);

static char * name_and_version = "containers 0.1v8 [October 8 2018]";

/*
 provides a uniform way of creating an array which can grow in size.
 generally used for pointers

 Versions

	- containers 0.1v8 [October 8 2018]
		- added get and add Float64 and Int64 from/to container

 	0.1v7 [October 8 2018]
 		- changed ContainerCount() to containerCount()
    0.1v6 [September 28 2018]
        - all Containers now hold just pointers. No 'ptrContainers'. Errors remain..
    0.1v5 [April 16 2018]
        - changed API for int pullValFromContainer(). returns 1 if there was something in the container, and writes the value removed to a pointer's variable
    0.1v4 [November 2017]
        - added pointer only containers (type Container)
    0.1v3   August 14 2017
        - added a set of functions to more efficiently push and pull 'word-sized' (64 bit) values to and from a Container

 */

// ------------------------- contNameAndVersion --------------

char * contNameAndVersion(void) {
    return name_and_version;
}

// ------------------ fprintContainer --------------

void fprintContainer(FILE * f, Container * c) {
	fprintf(f, "\n\nContainer '%s':", c -> name);
	fprintf(f, "\n\tcapacity  = %d", c -> max);
	fprintf(f, "\n\tcount     = %d", c -> count);
	// print the pointers
	for (int i = 0; i < c -> count; i++) {
		char * p = getPtrFromContainer(c, i);
		fprintf(f, "\n\t\tptr %d: %p", i, p);
	}
}

// -------------- printContainer ----------

void printContainer(Container * c) {
	fprintContainer(stdout, c);
}

// ----------------- zeroContainer ----------

void zeroContainer(Container * c) {
    c -> count = 0;
}

// ------------------ containerCount -----------------

uint32 containerCount(Container * c) {
    return c -> count;
}

// ----------- allocateInContainer ------------

uint32 allocateInContainer(Container * c) {
    // allocate space for one more pointer in the container, and return the index to it
//	printf("\nallocate in container '%s':", c -> name);
    if (c -> count >= c -> max) {
        // we need to grow the container - we use 50%
        uint32 newmax = (c -> max + (c -> max/2));
        //reallocate the data
//        printf("\n\t-- growing container from %d to %d pointers", c -> max, newmax);
		void * newdata = realloc(c -> data.ptrs, newmax * sizeof(char *));
        if (newdata) {
//            printf("..succeeded.");
			c -> data.ptrs = newdata;
            c -> max = newmax;
        }
        else {
            allocError(newmax * sizeof(char *), "reallocation of a container's data", __FILE__, __LINE__);
        }
    }
    // return the index to the current slot
    uint32 index = c -> count;

    // and increment the slot
    c -> count++;
//	printf("\n\tcount incremented to %d", c -> count);
    return index;
}

// -------------- getPtrFromContainer ---------

void * getPtrFromContainer(Container * c, uint64 n) {
    // Read the n'th pointer
    if (n >= c -> count) {
        printf("\n=== container error - asking for item %llu in container '%s' when count is %u", n, c -> name, c -> count);
        return NULL;        // safety - a NULL pointer should cause havoc fairly quickly
    }
	char * ptr = c -> data.ptrs[n];
    // printf("\nget ptr[%lld] %p from %s", n, ptr, c -> name);
    return ptr;
}

// -------------- getFloat64FromContainer ---------

float64 getFloat64FromContainer(Container * c, uint64 n) {
    // Read the n'th pointer
    if (n >= c -> count) {
        printf("\n=== container error - asking for item %llu in container '%s' when count is %u", n, c -> name, c -> count);
		return 0.0;        // safety - a NULL pointer should cause havoc fairly quickly
    }
	float64 v = c -> data.fvals[n];
    return v;
}

// -------------- getInt64FromContainer ---------

int64 getInt64FromContainer(Container * c, uint64 n) {
     // Read the n'th val
    if (n >= c -> count) {
        printf("\n=== container error - asking for item %llu in container '%s' when count is %u", n, c -> name, c -> count);
		return 0.0;        // safety - a NULL pointer should cause havoc fairly quickly
    }
	int64 v = c -> data.ivals[n];
    return v;
}

// -------------- addPtrToContainer ---------

void addPtrToContainer(Container * c, void * p) {
    // Add the pointer p to the container
    uint32 n = allocateInContainer(c);       // find index at which to store our pointer
	c -> data.ptrs[n] = p;                   // and store it
}

// ------------ addFloat64ToContainer -----------

void addFloat64ToContainer(Container * c, float64 v) {
    // Add the pointer p to the container
    uint32 n = allocateInContainer(c);       // find index at which to store our float64
	c -> data.fvals[n] = v;                  // and store it
}

// ------------ addInt64ToContainer -----------

void addInt64ToContainer(Container * c, int64 v) {
    // Add the pointer p to the container
    uint32 n = allocateInContainer(c);       // find index at which to store our int64
	c -> data.ivals[n] = v;                  // and store it
}

// ------------ newContainer --------------

Container * newContainer(uint32 count, char * name) {
    // create a new pointer container capable of holding up to 'count' pointers
    uint64 s = sizeof(Container) + 21;
    Container * c = malloc(s);
    if (c) {
        c -> name = strdup(name);
		c -> data.ptrs = malloc((count + 8) * sizeof(void *));
        c -> max = count;
        c -> count = 0;
		if (c -> data.ptrs == NULL) {
            allocError(count * sizeof(char *), "pointer container's data array", __FILE__, __LINE__);
            free(c);
            c = NULL;
        }
   }
    else {
        allocError(s, "GP container", __FILE__, __LINE__);
    }
    return c;
}

// ------------ pushPtrToContainer --------------

void pushPtrToContainer(Container * c, void * ptr) {
    // push a pointer into a container. Same as add.
    addPtrToContainer(c, ptr);
}

// ------------ pullPtrFromContainer --------------

void * pullPtrFromContainer(Container * c) {
    // pull a pointer from a container into ptr; return 1. If none, return 0
    if (c -> count > 0) {
        char * p = getPtrFromContainer(c, c -> count - 1);
        c -> count--;
        return p;
    }
    return NULL;
}

// ------------ getTopPtrInContainer --------------

void * getTopPtrInContainer(Container * c) {
     // if there is a pointer, copies top value into ptr and returns 1; else returns 0
    uint64 n = c -> count;
    if (n == 0) {
        return NULL;
    }
    char * p = getPtrFromContainer(c, n - 1);
    return p;
}


// -------------- searchInContainer -----------

int searchInContainer(Container * c, void * p) {
	// see if we've already got the pointer p in this container; if so, return its index
	// if not, return -1
	for (int i = 0; i < containerCount(c); i++) {
		void * ptr = getPtrFromContainer(c, i);
		if (ptr == p) return i;
	}
	return -1;
}

// -------------- searchStringInContainer -----------

int searchStringInContainer(Container * c, char * p) {
	// see if we've already got the string indicated by p in this container; if so, return its index
	// if not, return -1
	for (int i = 0; i < containerCount(c); i++) {
		void * ptr = getPtrFromContainer(c, i);
		if (strcmp(p, ptr) == 0) return i;
	}
	return -1;
}

// ================== character containers =====================

// ------------------ fprintCharContainer --------------

void fprintCharContainer(FILE * f, charContainer * c) {
	fprintf(f, "\n\nContainer '%s':", c -> name);
	fprintf(f, "\n\tcapacity  = %d", c -> max);
	fprintf(f, "\n\tcount     = %d", c -> count);
	fprintf(f, "\n\ttext      = '");
	// print the characters
	for (int i = 0; i < c -> count; i++) {
		char ch = getCharFromContainer(c, i);
		fprintf(f, "%c", ch);
		fprintf(f, "'");
	}
}

// -------------- printCharContainer ----------

void printCharContainer(charContainer * c) {
	fprintCharContainer(stdout, c);
}

// ----------------- zeroCharContainer ----------

void zeroCharContainer(Container * c) {
    c -> count = 0;
}

// ------------------ charContainerCount -----------------

uint32 charContainerCount(charContainer * c) {
    return c -> count;
}

// ----------- allocateInCharContainer ------------

uint32 allocateInCharContainer(charContainer * c) {
    // allocate space for one more character in the container, and return the index to it
    if (c -> count >= c -> max) {
        // we need to grow the container - we use 50%
        uint32 newmax = (c -> max + (c -> max/2));
        //reallocate the data
        //printf("\n-- growing container %p from %lld to %lld pointers", c, c -> max, newmax);
        void * newtext = realloc(c -> text, newmax * sizeof(char));
        if (newtext) {
            c -> text = newtext;
            c -> max = newmax;
        }
        else {
            allocError(newmax * sizeof(char *), "reallocation of a character container's text", __FILE__, __LINE__);
        }
    }
    // return the index to the current slot
    uint32 index = c -> count;

    // and increment the slot
    c -> count++;
    return index;
}

// -------------- getCharFromContainer ---------

char getCharFromContainer(charContainer * c, uint64 n) {
    // Read the n'th character
    if (n >= c -> count) {
        printf("\n=== char container error - asking for item %llu in container '%s' when count is %u", n, c -> name, c -> count);
        return '\0';
    }
    char ch = c -> text[n];
    // printf("\nget ptr[%lld] %p from %s", n, ptr, c -> name);
    return ch;
}

// -------------- addCharToContainer ---------

void addCharToContainer(charContainer * c, char ch) {
    // Add the character ch to the container
    uint32 n = allocateInCharContainer(c);       // find index at which to store our character
    // printf("\nadd '%c'' to %s[%lld]", ch, c -> name, n);
    c -> text[n] = ch;                        // and store it
}

// ------------ addStringToContainer ----------

void addStringToContainer(charContainer * c, char * string) {
	// initially, we'll do this the easy way
	while (*string) {
		addCharToContainer(c, *string++);
	}
}

// ----------- getContainerAsString -----------

char * getContainerAsString(charContainer * c) {
	uint32 len = c -> count;
	char * string = malloc(len + 1);
	if (string) {
		for (int i = 0; i < len; i++) {
			string[i] = getCharFromContainer(c, i);
		}
	}
	return string;
}

// ------------ newCharContainer --------------

charContainer * newCharContainer(uint32 count, char * name) {
    // create a new character container capable of holding up to 'count' characters
    uint64 s = sizeof(charContainer);
    charContainer * c = malloc(s);
    if (c) {
        c -> name = strdup(name);
        c -> text = malloc((count + 1) * sizeof(char));
        c -> max = count;
        c -> count = 0;
        if (c -> text == NULL) {
            allocError(count * sizeof(char), "char container's data array", __FILE__, __LINE__);
            free(c);
            c = NULL;
        }
   }
    else {
        allocError(s, "char container", __FILE__, __LINE__);
    }
    return c;
}

