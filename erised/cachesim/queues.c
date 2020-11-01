
//
//  queues.c
//
//  Created by pete on 1/29/16.
//  Copyright © 2016-2020 Pete. All rights reserved.
//


#include <stdlib.h>
#include <string.h>
#define EXTERN extern

#include "types.h"
#include "queues.h"


/*
simpleQueues 1.0v4 [May 3 2020]
	- changed the qBlock header to include an int64 key
	- added an insert blcok in order function


 // older:

 1.0v0 January 16 2017
 - rebuild of old code with old testing harness and new names for the API
 - we no longer try to keep count of the elements in a queue

 1.0v1
 - now #include types.h from the utilities project
 - now include utilities.c and .h to provide timenow()

 1.0v2 [May 8 2017]
 - changed qAppendQTail and qAppendQHead to NOT free the Queue structure that's supplying the list

 1.0v3 [April 7 2018]
 - changed Queue * qNewQs(uint64 count, char * name) to check for the existence of the malloc'd queues before initialising them
 */


static char * name_and_version = "simpleQueues 1.0v4 [May 3 2020]";

// ---------------------- qNameAndVersion --------------

char * qNameAndVersion(void) {
    return name_and_version;
}

// ------------------ qError ---------------

void qError(char * msg, char * s1, char * s2) {
	printf("\n\n==qERROR: %s %s %s\n", msg, s1 ? s1 : "", s2? s2: "");
}

// ------------------ qCheckQ ------------------

uint32 qCheckQ(Queue * q) {
    // checks the specified queue for consistency
    uint32 errors = qCheckSlice("queue", q -> head, q -> tail);
    return errors;
}


// ------------------ qCheckSlice ------------------

uint32 qCheckSlice(char * msg, qBlock * b0, qBlock * b1) {
    // checks the specified slice of blocks for consistency
    // check forward
    qBlock * front, * back;
    uint32 fwd = 0, bwd = 0;
    uint32 errors  = 0;
    front = b0;
    back = b1;
    // check going from front arrives at back
    while (b0 && (b0 != back)) {
        b0 = b0 -> next;
        fwd++;
    }
    if (b0 != back) {
        printf("\n\terror in traversing %s forward from %p..%p", msg, front, back);
        errors++;
    }

    // check going from back to front arrives at front
    b0 = back;
    while (b0 && (b0 != front)) {
        b0 = b0 -> prev;
        bwd++;
    }

    if (b0 != front) {
        printf("\n\terror in traversing %s backward from %p..%p", msg, front, back);
        errors++;
    }

    if (fwd != bwd) {
        printf("\n\tcount error in traversing %s from %p..%p; fwdcount=%d, bwdcount=%d", msg, front, back, fwd, bwd);
        errors++;
    }
    return errors;
}

// ------------------ qCountQ ----------------------

uint64 qCountQ(Queue * q) {
    uint64 n = 0;
    qBlock * b, * t;
    b = q -> head;
    t = q -> tail;
    if (b && t) {
        while (b) {
            n++;
            b = b -> next;
        }
        return n;
    }
    else {
        return 0;
    }
}

// ------------------ qSetQEmpty ----------------

void qSetQEmpty(Queue * q) {
	q -> head = NULL;
	q -> tail = NULL;
}

// -------------------------- qInitQ --------------------------

void qInitQ(Queue * q, char * name, uint64 n) {
    // initialise created queue
    if (name) {
        int l = (int)strlen(name);
        q -> qname = malloc(l + 16);
        sprintf(q -> qname, "%s[%llu]", name, n);
    }
    else q -> qname = NULL;

	qSetQEmpty(q);

//	q -> bt = btAny;
}

//// --------------- qSetQType -------------
//
//void qSetQType(Queue * q, blockType bt) {
//	q -> bt = bt;
//	uint64 size = strlen(q -> qname) + strlen(btName(bt)) + 16;
//	char * n = malloc(size);
//	strcpy(n, q -> qname);
//	strcat(n, "-'");
//	strcat(n, btName(bt));
//	strcat(n, "'");
//	//printf("\nqSetQType: freeing '%s' and setting '%s'", q -> qname, n);
//	free(q -> qname);
//	q -> qname = n;
//}
//
// -------------------------- qNewQ ---------------------------

Queue * qNewQ(char * name) {
    // create and initialise just one queue
    return qNewQs(1, name);
}

// -------------------------- qNewQs -------------------------

Queue * qNewQs(uint64 count, char * name) {
    // creates and initialises an array of 'count' queues
    Queue * q;
    uint64 i;
    q = malloc(count * sizeof(Queue));
    if (q) {
        for (i = 0; i < count; i++) {
            qInitQ(&q[i], name, i);
        }
    }
    return q;
}

// -------------------------- qKillQs ------------------------

void qKillQs(Queue * qs, uint64 count) {
    // kills the array of count queues at q
    free(qs);
}

// ------------------------- qKillQEntries --------------------------

void qKillQEntries(Queue * q) {
    // removes and frees all entries on the specified queue
    // must ONLY be used when the blocks were malloc()'d
    // does not free any memory associated with the blocks
    qBlock * ptr;
    ptr = qGetBlock(q);
    while (ptr) {
        free(ptr);
        ptr = qGetBlock(q);
    }
}

// --------------------------- AddQHead ---------------------------

void qAddQHead(Queue * q, qBlock * start, qBlock * end) {
    // adds the list of blocks to the head of q
    if (start && end) {
        start -> prev = NULL;
        end -> next = q -> head;
        if (q -> head) {
            // the queue is non-empty
            q -> head -> prev = end;
            q -> head = start;
        }
        else {
            // queue is empty
            q -> head = start;
            q -> tail = end;
        }
    }
}

// ---------------------------- qAddQTail -------------------------

void qAddQTail(Queue * q, qBlock * start, qBlock * end) {
    // adds the list of blocks to the tail of q
    if (start && end) {
        end -> next = NULL;
        start -> prev = q -> tail;
        if (q -> head) {
            // the queue is non-empty
            q -> tail -> next = start;
            q -> tail = end;
        }
        else {
            // the queue is empty
            q -> head = start;
            q -> tail = end;
        }

    }
}

// --------------------------- qAppendQTail --------------------

void qAppendQTail(Queue * qd, Queue * qs) {
    // places all the elements of qs on the tail of qd
    qBlock * start, *end;
    start = qFirstBlock(qs);
    end = qLastBlock(qs);

    qCutQ(qs, start, end);
    qAddQTail(qd, start, end);
}

// --------------------------- qAppendQHead --------------------

void qAppendQHead(Queue * qd, Queue * qs) {
    // places all the elements of qs at the head of qd
    qBlock * start, *end;
    start = qFirstBlock(qs);
    end = qLastBlock(qs);

    qCutQ(qs, start, end);
    qAddQHead(qd, start, end);
}

// --------------------------- qCutQ -------------------------

void qCutQ(Queue * q, qBlock * start, qBlock * end) {
    // cuts the blocks start..end out of queue q
    if ((start == NULL) || (end == NULL)) {
        return;            // no action if no slice
    }
    if ((q -> head == start) && (q -> tail == end)) {
        // whole queue
        q -> head = NULL;
        q -> tail = NULL;
    }
    else if (q -> head == start) {
        // cut from start to some way down
        q -> head = end -> next;
        end -> next -> prev = NULL;
    }
    else if (q -> tail == end) {
        // cut from middle to end
        q -> tail = start -> prev;
        start -> prev -> next = NULL;
    }
    else {
        // cut from middle
        start -> prev -> next = end -> next;
        end -> next -> prev = start -> prev;
    }
}

// --------------------------- qCutBlock -------------------------

void qCutBlock(Queue * q, qBlock * block) {
    // cuts the block 'block' out of queue q,
    if (block == NULL) return;

    if ((q -> head == block) && (q -> tail == block)) {
        // whole queue
        q -> head = NULL;
        q -> tail = NULL;
    }
    else if (q -> head == block) {
        // remove first block
        q -> head = block -> next;
        block -> next -> prev = NULL;
    }
    else if (q -> tail == block) {
        // cut last block
        q -> tail = block -> prev;
        block -> prev -> next = NULL;
    }
    else {
        // cut from middle
        block -> prev -> next = block -> next;
        block -> next -> prev = block -> prev;
    }
}

// --------------------------- qAddBlock -------------------------

void qAddBlock(Queue * q, qBlock * block) {
    // add block to tail of queue

//	if (q -> bt != btAny) {
//		if (q -> bt != block -> bt) {
//			qError("mismatched queue and block type", btName(q -> bt), btName(block -> bt));
//		}
//	}
    if (block) {
        block -> next = NULL;
        block -> prev = q -> tail;
        if (q -> head ) {
            // non-empty queue
            q -> tail -> next = block;
            q -> tail = block;
        }
        else {
            // empty queue
            q -> head = block;
            q -> tail = block;
        }
    }
}

// --------------------------- qAddBlockBefore -------------------------

void qAddBlockBefore(Queue * q, qBlock * block, qBlock * here) {
    // add 'block' to queue immediately before block 'here'
    // if q is empty, make block the q
    // if 'here' is NULL, add to end

    if (block == NULL) return;
    if (qFirstBlock(q) == NULL) {
        qAddBlock(q, block);
    }
    else if (here == NULL) {
        // didn't find anywhere; just add to tail
        qAddBlock(q, block);
    }
    else if (here -> prev == NULL) {
        // add to front
        qAddBlockHead(q, block);
    }
    else {
        // it's in the middle; chain in block after here->prev
        qAddBlockAfter(q, block, here->prev);
    }
}

// --------------------------- qAddBlockBeforeHead -------------------------

void qAddBlockBeforeHead(Queue * q, qBlock * block, qBlock * here) {
    // add 'block' to queue immediately before block 'here'
    // if q is empty, make block the q
    // if 'here' is NULL, add to head

    if (block == NULL) return;
    if (qFirstBlock(q) == NULL) {
        qAddBlock(q, block);
    }
    else if ((here == NULL) || (here -> prev == NULL)) {
        // add to front
        qAddBlockHead(q, block);
    }
    else {
        // it's in the middle; chain in block after here->prev
        qAddBlockAfter(q, block, here->prev);
    }
}

// --------------------------- AddBlockAfter -------------------------

void qAddBlockAfter(Queue * q, qBlock * block, qBlock * here) {
    // add block to queue immediately after block here

    if (block == NULL) return;
    if (qFirstBlock(q) == NULL) {
        // no queue: add to head
        qAddBlockHead(q, block);
    }
    else if ((here == NULL) || (here -> next == NULL)) {
        // no place to add, or 'here' is the tail of the queue, so add to tail
        qAddBlock(q, block);
    }
    else {
        // 'here' is in the middle; chain in block after here
        qBlock * next;

        next = here -> next;
        block -> next = next;
        block -> prev = here;
        here -> next = block;
        next -> prev = block;
    }
}

// --------------------------- qAddBlockAfterHead -------------------------

void qAddBlockAfterHead(Queue * q, qBlock * block, qBlock * here) {
    // add block to queue immediately after block here
    // add to front of queue if 'here' is NULL

    if (block == NULL) return;
    if ((qFirstBlock(q) == NULL) || (here == NULL)) {
        // no queue: add to head
        qAddBlockHead(q, block);
    }
    else if (here == q -> tail) {
        // add to tail in normal manner
        qAddBlock(q, block);
    }
    else {
        // 'here' is in the middle; chain in block after here
        qBlock * next;

        next = here -> next;
        block -> next = next;
        block -> prev = here;
        here -> next = block;
        next -> prev = block;
    }
}

void printSymQ(char * msg, void * st, Queue * q);

// --------------------------- qAddBlockHead -------------------------

void qAddBlockHead(Queue * q, qBlock * block) {

	// there's something wrong with this

    // add block to FRONT of queue
    if (block == NULL) return;

	qBlock * head = qFirst(q);

     if (head) {
        // non-empty queue. point old head at new block. don't touch tail.
        head -> prev = block;
		// point new block at old head
		block -> next = head;
		 // make it have no prevs
		block -> prev = NULL;
		// set block as the head.
        q -> head = block;
    }
    else {
        // empty queue
        q -> head = block;
        q -> tail = block;
    }
}

// ----------------------- qGetBlockTail ----------------

qBlock * qGetBlockTail(Queue * q) {
	// gets the tail of the queue
	qBlock * block = q -> tail;
	qCutBlock(q, block);
	return block;
}
// ---------------------------- qGetBlock ----------------------

qBlock * qGetBlock(Queue * q) {
    // removes the first block on 'q' and returns it
    qBlock * block;

    block = q -> head;

    if (block) {
        // non-empty queue
        if (q -> tail == block) {
            // just one in queue; empty the queue
            qSetQEmpty(q);                    // empty the queue
        }
        else {
            q -> head = block -> next;        // move head
            block -> next -> prev = NULL;    // mark new head
        }
    }
    return block;
}

// -------------------------- qPushBlock ----------------------

void qPushBlock(Queue * q,qBlock * block) {
    // push and pop run a queue as a stack; push puts block on front of queue
    qAddBlockHead(q, block);
}

// -------------------------- qPushBlock ----------------------

qBlock * qPopBlock(Queue * q) {
    // push and pop run a queue as a stack; pop gets block from front of queue
    return qGetBlock(q);
}

// --------------------------- qInitBlock ---------------------

void qInitBlock(qBlock * block) {
    // initialise block pointers to values which will surely trap if used
    block -> next = NULL;
    block -> prev = NULL;
//	block -> bt = btAny;
}


//// ----------------- qSetBlockType ---------------
//
//void qSetBlockType(qBlock * b, blockType bt) {
//	b -> bt = bt;
//}
//
// --------------------------- qNewBlock -----------------------

void * qNewBlock(uint32 size) {
    // creates and initialises a new block of the desired size
    qBlock * block = malloc(size);
    if (block) qInitBlock(block);
    return block;
}

// ------------- qInsertKeyBlockInOrder --------------

void qInsertBlockInOrder(Queue * q, qBlock * block) {
	// q is a key-ordered queue of qKeyBlocks
	// it is not in any queue (pointers are NULL)
	// run down the queue looking for the insertion point, which is
	// in front of the first block with the same or later time

//	if (block -> key < 300) printf("\n--qInsertKeyBlockInOrder: block %p time %lld", block, block -> key);
	qBlock * here = qFirst(q);
	while (here) {
//		if (block -> key < 300) printf("\n\tchecking block %p at time %lld", here, here -> key);
		if (here -> key >= block -> key) break;
		here = qNext(here);
	}

	if (here) {
		// insert before here
//		if (block -> key < 300) printf("\n\t.. insert before block %p key %lld", here, here -> key);
		qAddBlockBefore(q, block, here);
	}
	else {
		qAddBlock(q, block);
//		if (block -> key < 300) printf("\t.. add to tail");
	}
}

// ------------- qPrintKeyQ ---------------

void qPrintKeyQ(Queue * q) {
	printf("\nKey Queue %p '%s'", q, q -> qname);
	qBlock * b = qFirst(q);
	while (b) {
		printf("\n\tblock %p has key %lld", b, b -> key);
		b = qNext(b);
	}
}
