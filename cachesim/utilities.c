//
//  utilities.c
//  simpleADL
//
//  Created by pete on 1/17/17.
//  Copyright © 2017 Kiva Design. All rights reserved.
//
// edited or touched Nov 6 2018

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define EXTERN extern
#include "utilities.h"
#include "types.h"

static char * title = "kiva utility functions 1.0v6 [June 10 2019]";

/*
 1.0v6 [June 10 2019]
 - added btName() to give names to the types of blocks and queues
 1.0v5 [April 7 2018]
 - added isqrt() [validated elsewhere]
 1.0v4 [May 2017]
 - added teqFree()
 1.0v3
 - added teqAlloc()
 - by default utilityReport now set to 0
 1.0v2 May 2017
 - added char * utGetNameFromPath(char * p)
 1.0v1
 - initial version
 */



// ---------------- utilityTitleAndVersion ------------------

char * utilityTitleAndVersion(void) {
	return title;
}

// -------------- findent -----------------

void findent(FILE * f, int depth) {
	// outputs 'indent' tabs after a newline
	fprintf(f, "\n");
	while (depth >= 0) {
		fprintf(f, "\t");
		depth--;
	}
}

// ---------------- timenow ------------------

uint64 timenow(void) {
	// returns the current time in microseconds
	struct timeval tv;
	struct timezone tz;
	uint64 thetime;
	gettimeofday(&tv, &tz);
	thetime = ((uint64)tv.tv_sec * 1000000) + (uint64)tv.tv_usec;
	return thetime;
}


// -------------------- utGetNameFromPath ------------------

char * utGetNameFromPath(char * p) {
	char * path = strdup(p);
	// extract the last slice of the path to give the name
	uint32 len = (uint32)strlen(path);
	char * ptr = &path[len];
	*ptr = '\0';
	// walk backwards till we find a '/'
	while (*ptr != '/') ptr--;
	char * name = strdup(ptr + 1);
	free(path);
	return name;
}

// ---------------- comments on rn creation ----------------

/*

 https://fr.mathworks.com/matlabcentral/fileexchange/8054-triangular-distributed-random-variable-generator?requestedDomain=true

 %Script by Dr.Mongkut Piantanakulchai
 %To simulate the triangular distribution
 %Return a vector of random variable
 %The range of the value is between (a,b)
 %The mode is c (most probable value)
 %n is to total number of values generated
 %Example of using
 %X = trirnd(1,5,10,100000);
 % this will generate 100000 random numbers between 1 and 10 (where most probable
 % value is 5)
 % To visualize the result use the command
 % hist(X,50);

 function X=trirnd(a,c,b,n)
 X=zeros(n,1);
 for i=1:n
 %Assume a<X<c
 z=rand;
 if sqrt(z*(b-a)*(c-a))+a<c
 X(i)=sqrt(z*(b-a)*(c-a))+a;
 else
 X(i)=b-sqrt((1-z)*(b-a)*(b-c));
 end
 end %for
 %hist(X,50); Remove this comment % to look at histogram of X
 end %function

 */

// ----------------------- random numbers --------------------

#define rvM 			((1L << 31) - 1L)
#define rvd 			(16807L)
#define rvquotient 		(rvM / rvd)
#define rvremainder 	(rvM % rvd)

#define maxUniformStream (8)


//*********** NOTE: a stream must never get the value 0, or it won't ever change *******

static uint32 RandomStreams[maxUniformStream + 1];

// ----------------------------- uniform ---------------

uint32 uniform(uint32 stream, uint32 base, uint32 limit) {
	uint32 result;
	/* returns a uniformly distributed rv in base..limit
	 the smallest number returned is base
	 the largest number returned is base + (limit - 1)
	 base, limit MUST both be >= 0
	 limit MUST be > base
	 these are NOT checked in the runtime
	 */

	// ********* NOTE:  warnings about integer overflow in expressions in this function should be ignored.

	result = RandomStreams[stream];
	result = (rvd * (result % rvquotient)) -
	(rvremainder * (result / rvquotient));
	if (result == 0) {
		// kill any zero values
		result = rvd;
	}

	RandomStreams[stream] = result;

	//printf("\n\t\tRAW rv= %d 0x%08lx", result, result);

	// reduce the range of the value to base..limit
	result %= (limit - base);
	return (base + result);
}

// ----------------- isqrt -----------------------

uint32 isqrt(uint32 n) {
	uint32 rem = 0;
	uint32 root = 0;
	uint32 i;
	// validated with simple tests
	for (i = 0; i < 16; i++) {
		root <<= 1;
		rem <<= 2;
		rem += n >> 30;
		n <<= 2;

		if (root < rem) {
			root++;
			rem -= root;
			root++;
		}
	}
	return root >> 1;
}


// ----------------- triangular -------------------

uint32 triangular(uint32 stream, uint32 base, uint32 limit, uint32 mode) {
	// creates a triangularly-distributed RN in the range base, limit with peak at mode
	// only works for values in 0..1 as fractions...

	uint32 z = uniform(stream, base, limit);
	uint32 s = isqrt(z*(limit - base)*(mode - base)) + base;

	if (s < mode) {
		return s;
	}
	else {
		return limit-isqrt((1-z)*(limit-base)*(limit-mode));
	}

}
/* ---------------------- initUniform --------------------------------- */

void initUniform(void) {
	// initialise the seeds - must call before calling uniform

	int32 i;
	for (i = 0; i < maxUniformStream; i++) {
		RandomStreams[i] = i + 1;
	}
}

// ------------- sext ---------------------

int64 sext(uint32 n, uint64 v) {
	// sign extends v assuming it has n bits of data
	int64 r;
//	printf("\nsign extend '%lld' which comprises %d bits", v, n);
	int64 mask = -1LL;
	uint64 bit = (1LL << (n - 1));
//	printf("\n\tbit = %lld", bit);
	if (v & bit) {
		// need to sign extend
		mask = mask << n;
//		printf("\n\tmask = 0x%08llx", mask);
		r = mask | v;
//		printf("\n\tr = 0x%08llx %lld", r, r);
	}
	else {
		// nope
		r = v;
	}
	return r;
}


