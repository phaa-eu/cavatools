/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <time.h>
#include <immintrin.h>


#define BATCH_SIZE  256
#define MAX_SPINS  100

struct fifo_t {
  const char* id;		/* descriptor, $name or path */
  int32_t head;			/* removal pointer (local copy) */
  volatile int32_t TAIL;	/* global copy of insertion pointer */
  uint32_t get_mask;		/* =(1<<size)-1 */
  int32_t size;			/* log-base-2 number of elements */
  int32_t pad1[32-6];		/* to 64-byte cache line */
  int32_t tail;			/* insertion pointer (local copy) */
  volatile int32_t HEAD;	/* global copy of removal pointer */
  uint32_t put_mask;		/* another copy in producer cache line */
  int32_t fd;			/* file descriptor number */
  volatile int32_t finished;	/* set flag when consumer is done */
  int32_t pad2[32-5];		/* to 64-byte cache line */
  volatile uint64_t buffer[0];	/* begining of buffer */
};

static struct timespec timeout = { .tv_sec=0, .tv_nsec=100 };

//#define futex_wait(addr, val) syscall(SYS_futex, addr, FUTEX_WAIT, val, &timeout, 0)
#define futex_wait(addr, val) syscall(SYS_futex, addr, FUTEX_WAIT, val,  (int*)0)
#define futex_hibernate(addr, val) syscall(SYS_futex, addr, FUTEX_WAIT, val, (int*)0)
#define futex_wake(addr) syscall(SYS_futex, addr, FUTEX_WAKE, 1)


struct fifo_t* fifo_create( const char* bufid, int bufsize );
/* Producer side fifo initialization.
     bufid	- number = file descriptor (already opened)
		  $name = shared memory segment /dev/shm/name
		  otherwise = trace file path name
     bufsize	- log-base-2 number of bytes
*/
struct fifo_t* fifo_open( const char* bufid );
/* Consumer side fifo initialization.
     bufid	- number = file descriptor (already opened)
		  $name = shared memory segment /dev/shm/name
		  otherwise = trace file path name
*/

void fifo_finish( struct fifo_t* fifo );
/* Producer side fifo termination. */
void fifo_close( struct fifo_t* fifo );
/* Consumer side fifo termination. */


void fifo_debug( struct fifo_t* fifo, const char* msg );


/* Put item in fifo */
static inline void fifo_put( struct fifo_t* fifo, uint64_t item )
{
  int tailp1 = (fifo->tail+1) & fifo->put_mask;
  if (tailp1 == fifo->HEAD) {
    int spins = MAX_SPINS;
    do {
      _mm_pause();
    } while (tailp1 == fifo->HEAD && --spins >= 0);
    while (tailp1 == fifo->HEAD)
      futex_wait(&fifo->HEAD, tailp1);
  }
  fifo->buffer[fifo->tail] = item;
  fifo->tail = tailp1;
  if (fifo->tail % BATCH_SIZE == 0) {
    fifo->TAIL = fifo->tail;
    futex_wake(&fifo->TAIL);
  }
}


/* Check if fifo empty */
static inline int fifo_empty( struct fifo_t* fifo )
{
  return fifo->head == fifo->TAIL;
}

/* Peek at head item in fifo.  Invalid if fifo empty */
static inline uint64_t fifo_peek( struct fifo_t* fifo )
{
  return fifo->buffer[fifo->head];
}

/* Get item from fifo */
static inline uint64_t fifo_get( struct fifo_t* fifo )
{
  if (fifo_empty(fifo)) {
    int spins = MAX_SPINS;
    do {
      _mm_pause();
    } while (fifo->head == fifo->TAIL && --spins >= 0);
    while (fifo->head == fifo->TAIL)
      futex_wait(&fifo->TAIL, fifo->head);
  }
  uint64_t rv = fifo->buffer[fifo->head++];
  fifo->head &= fifo->get_mask;
  if (fifo->head % BATCH_SIZE == 0) {
    fifo->HEAD = fifo->head;
    futex_wake(&fifo->HEAD);
  }
  return rv;
}





/* Make consumer status up to date */
static inline void fifo_flush( struct fifo_t* fifo )
{
  fifo->TAIL = fifo->tail;
  futex_wake(&fifo->TAIL);
}
