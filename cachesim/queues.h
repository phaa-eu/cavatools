//
//  queues.h
//
//  Created by pete on 1/29/16.
//  Copyright © 2016-2020 Pete. All rights reserved.
//


#ifndef queues_h
#define queues_h

#include <stdio.h>
#include <stdlib.h>
#include "types.h"

// ==================== queue manipulation declarations ========================

// the Block structure. this must be the first item in a queueable structure
// (so the address of the structure is, conveniently, the address of the
// block)
// queues are doubly-linked for safety and for insertion/deletion speed & simplicity

// the header block for inclusion in each list element
typedef struct block qBlock;

struct block {
	qBlock * next;			// next block
	qBlock * prev;			// previous block
	int64 key;
};


// the queue structure, pointing at head and tail of the queue
typedef struct queue {
	char * qname;
	qBlock * head;
	qBlock * tail;
//	blockType bt;
} Queue;

// Queue procedures

char * qNameAndVersion(void);
qBlock * qGetQHead(Queue * q);
Queue * qGetQSlice(qBlock * front, qBlock * back);
void qInitQ(Queue * q, char * name, uint64 n);
void qSetQEmpty(Queue * q);
Queue * qNewQ(char * name);
uint64 qCountQ(Queue * q);
Queue * qNewQs(uint64 count, char * name);
void qKillQs(Queue * q, uint64 count);
void qAppendQHead(Queue * qd, Queue * qs);
void qAppendQTail(Queue * qd, Queue * qs);
void qAddQHead(Queue * q, qBlock * start, qBlock * end);
void qAddQTail(Queue * q, qBlock * start, qBlock * end);
void qCutQ(Queue * q, qBlock * start, qBlock * end);
void qAddBlock(Queue * q, qBlock * block);
void qAddBlockHead(Queue * q, qBlock * block);
void qAddBlockBefore(Queue * q, qBlock * block, qBlock * here);
void qAddBlockAfter(Queue * q, qBlock * block, qBlock * here);
void qAddBlockBeforeHead(Queue * q, qBlock * block, qBlock * here);
void qAddBlockAfterHead(Queue * q, qBlock * block, qBlock * here);
qBlock * qGetBlock(Queue* q);
qBlock * qGetBlockTail(Queue * q);
void qCutBlock(Queue * q, qBlock * block);
void * qNewBlock(uint32 size);

void qInitBlock(qBlock * block);
void qInsertBlockInOrder(Queue * q, qBlock * block);
void qPrintKeyQ(Queue * q);

int qSetQChecking(int check);
void qKillQEntries(Queue * q);				// removes and frees all entries in q
											// assumes all blocks allocated with malloc()

// checking queues
uint32 qCheckQ(Queue * q);
uint32 qCheckSlice(char * msg, qBlock * b0, qBlock * b1);
uint64 qCountQ(Queue * q);
//void qSetBlockType(qBlock * b, blockType bt);
//void qSetQType(Queue * q, blockType bt);

// queues as stacks
void qPushBlock(Queue * q, qBlock * block);
qBlock * qPopBlock(Queue * q);

// errors
void qError(char * msg, char * s1, char * s2) ;

// walk queues efficiently..

#define qNextBlock(p)		((p) -> header.next)
#define qPrevBlock(p)		((p) -> header.prev)
#define qFirstBlock(q)		((q) -> head)
#define qLastBlock(q)		((q) -> tail)
#define qEmpty(q)			((q -> head == NULL) && (q -> tail == NULL))

// untyped
#define qLast(q)			((void *)(qLastBlock((Queue *)q)))
#define qFirst(q)			((void *)(qFirstBlock((Queue *)q)))
#define qNext(b)			((void *)(((qBlock*)b) -> next))
#define qPrev(b)			((void *)(((qBlock*)b) -> prev))

#endif /* queues_h */
