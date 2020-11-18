//
//  types.h
//  standard pete types
//
//  Created by pete on 1/29/16.
//  Copyright � 2016 Pete. All rights reserved.
//
// edited or touched Nov 6 2018

#ifndef types_h
#define types_h

// useful types
typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef unsigned long long uint64;

typedef char int8;
typedef short int16;
typedef int int32;
typedef long long int64;

typedef float float32;
typedef double float64;

typedef enum {blockTriple = 1023, btAny, blockStatement, blockStatementBlock, blockElement, blockInstruc, btMax } blockType;

#define TRUE (1)
#define FALSE (0)

char * btName(blockType bt);

#endif /* types_h */
