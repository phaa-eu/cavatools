/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

//#define SLOW_FIFO

#define FIFO_SIZE	(4096*4)
//#define FIFO_SIZE	16

  
struct shmbuf_t {		/* buffer in shared memory */
  uint64_t buffer[FIFO_SIZE];	/* circular buffer */
  int tail;			/* insertion index */
  int pad1[64/sizeof(int)-1];	/* pad to 64-byte cache line */
  int head;			/* removal index */
  int pad2[64/sizeof(int)-1];	/* pad to 64-byte cache line */
  int finished;			/* set when consumer finishes */
};

struct fifo_t {			/* descriptor in each process */
  struct shmbuf_t* shm;		/* from mmap() */
  int cursor;			/* local copy of head/tail in buffer */
  int boundary;			/* safe for cursor until here */
  int fd;			/* file descriptor number or SHM object */
  int producer;			/* set if we are producer */
  const char* shm_path;		/* name created in /dev/shm */
};

void fifo_flush( struct fifo_t* fifo );
void fifo_init( struct fifo_t* fifo, const char* shm_name, int consumer );
void fifo_fini( struct fifo_t* fifo );
void fifo_debug( struct fifo_t* fifo, const char* msg );


#ifdef SLOW_FIFO
void fifo_put( struct fifo_t* fifo, uint64_t item );
uint64_t fifo_get( struct fifo_t* fifo );
#else

#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <sys/time.h>

static struct timespec timeout = { .tv_sec=0, .tv_nsec=10000 };

#define futex_wait(addr, val) syscall(SYS_futex, addr, FUTEX_WAIT, val, &timeout)
#define futex_hibernate(addr, val) syscall(SYS_futex, addr, FUTEX_WAIT, val, (int*)0)
#define futex_wakeup(addr) syscall(SYS_futex, addr, FUTEX_WAKE, 1)


static inline void fifo_put( struct fifo_t* fifo, uint64_t item )
{
  /* oo we have still have known free space left? */
  int tailp1 = (fifo->shm->tail+1) % FIFO_SIZE;
  if (tailp1 == fifo->boundary) { /* no */
    /* spin sleep until fifo not full */
    while (tailp1 == fifo->shm->head)
      futex_wait(&fifo->shm->head, tailp1);
    /* save known free space to avoid cache ping-pong shm->head */
    fifo->boundary = fifo->shm->head;
  }
  fifo->shm->buffer[fifo->shm->tail] = item;
  fifo->shm->tail = tailp1;
}


static inline uint64_t fifo_get( struct fifo_t* fifo )
{
  /* do we have known valid elements in fifo? */
  if (fifo->shm->head == fifo->boundary) { /* no */
    /* spin sleep until fifo not empty */
    while (fifo->shm->tail == fifo->shm->head)
      futex_wait(&fifo->shm->tail, fifo->shm->head);
    /* save known free space to avoid cache ping-pong shm->tail */
    fifo->boundary = fifo->shm->tail;
  }
  uint64_t rv = fifo->shm->buffer[fifo->shm->head++];
  fifo->shm->head %= FIFO_SIZE;
  return rv;
}

#endif
