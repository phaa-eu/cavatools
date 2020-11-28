#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <sys/time.h>

#define SLOW_FIFO
#include "shmfifo.h"
#include "caveat.h"

#define DEBUG


//static struct timespec timeout = { .tv_sec=0, .tv_nsec=50 };
static struct timespec timeout = { .tv_sec=0, .tv_nsec=100 };

#define futex_wait(addr, val) syscall(SYS_futex, addr, FUTEX_WAIT, val, &timeout)
#define futex_hibernate(addr, val) syscall(SYS_futex, addr, FUTEX_WAIT, val, (int*)0)
#define futex_wakeup(addr) syscall(SYS_futex, addr, FUTEX_WAKE, 1)


void fifo_put( struct fifo_t* fifo, uint64_t item )
{
  /* do we have still have known free space left? */
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


uint64_t fifo_get( struct fifo_t* fifo )
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

void fifo_flush( struct fifo_t* fifo )
{
  return;
  if (fifo->producer) {
    fifo->shm->head = fifo->cursor;
    fifo->boundary = fifo->shm->tail;
  }
  else
    fifo->boundary = fifo->shm->head;
}


void fifo_init( struct fifo_t* fifo, const char* shm_name, int consumer )
{
  fifo->shm_path = shm_name;
  fifo->producer = !consumer;
  if (fifo->producer) fifo->fd = shm_open(fifo->shm_path, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU);
  else                fifo->fd = shm_open(fifo->shm_path, O_RDWR, 0);
  dieif(fifo->fd<0, "shm_open() failed in fifo_init");
  if (fifo->producer)
    dieif(ftruncate(fifo->fd, sizeof(struct shmbuf_t))<0, "ftruncate() failed in fifo_init");
  if (fifo->producer) fifo->shm = (struct shmbuf_t*)mmap(NULL, sizeof(struct shmbuf_t), PROT_READ|PROT_WRITE, MAP_SHARED, fifo->fd, 0);
  else                fifo->shm = (struct shmbuf_t*)mmap(NULL, sizeof(struct shmbuf_t), PROT_READ|PROT_WRITE, MAP_SHARED, fifo->fd, 0);
  dieif(fifo->shm==0, "mmap() failed in fifo_init");
  fifo->shm->finished = 0;
  fifo->cursor = fifo->boundary = 0;
}


void fifo_fini( struct fifo_t* fifo )
{
  if (fifo->producer) {
    fifo_flush(fifo);
    /* wait for consumer to finish */
    futex_hibernate(&fifo->shm->finished, 0);
  }
  else {
    fifo->shm->finished = 1;
    futex_wakeup(&fifo->shm->finished);
  }
  dieif(munmap(fifo->shm, sizeof(struct shmbuf_t))<0, "munmap() failed in fifo_fini");
  if (fifo->producer)
    dieif(shm_unlink(fifo->shm_path)<0, "sem_unlink() failed in fifo_fini");
}


void fifo_debug( struct fifo_t* fifo, const char* msg )
{
  fprintf(stderr, "%s %s: head=%d, tail=%d, cursor=%d, boundary=%d\n",
	  fifo->producer ? "Producer" : "Consumer", msg,
	  fifo->shm->head, fifo->shm->tail, fifo->cursor, fifo->boundary);
}
