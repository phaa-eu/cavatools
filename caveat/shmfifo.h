/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>


  //#define LG_BUFFER_SIZE	0
  //#define NUM_BUFFERS	2	/* one always empty */
  
#define LG_BUFFER_SIZE	14
#define NUM_BUFFERS	16

#define BUFFER_SIZE	(1<<LG_BUFFER_SIZE)  
#define FIFO_SIZE	(NUM_BUFFERS*BUFFER_SIZE)

  
struct shmbuf_t {		/* buffer in shared memory */
  uint64_t buffer[FIFO_SIZE];	/* circular buffer */
  sem_t empty;			/* count number of empty buffers */
  sem_t full;			/* count number of full buffers */
  sem_t finished;		/* when consumer finishes */
};

struct fifo_t {			/* descriptor in each process */
  struct shmbuf_t* shmbuf;	/* from mmap() */
  long cursor;			/* to insert/remove in buffer */
  int fd;			/* file descriptor number or SHM object */
  int producer;			/* set if we are producer */
  const char* shm_path;		/* name created in /dev/shm */
};



static inline void fifo_put( struct fifo_t* fifo, uint64_t item )
{
  fifo->shmbuf->buffer[fifo->cursor++] = item;
  if (fifo->cursor % BUFFER_SIZE == 0) {
    sem_post(&fifo->shmbuf->full); /* we have filled another buffer */
    sem_wait(&fifo->shmbuf->empty); /* do not proceed until have empty */
  }
  fifo->cursor %= FIFO_SIZE;	/* optimizes to and */
}


static inline uint64_t fifo_get( struct fifo_t* fifo )
{
  if (fifo->cursor % BUFFER_SIZE == 0) {
    sem_post(&fifo->shmbuf->empty); /* we have emptied another buffer */
    sem_wait(&fifo->shmbuf->full); /* wait for at least one full buffer */
  }
  uint64_t rv = fifo->shmbuf->buffer[fifo->cursor++];
  fifo->cursor %= FIFO_SIZE;	/* optimizes to and */
  return rv;
}


static inline void fifo_flush( struct fifo_t* fifo )
{
  if (fifo->cursor % BUFFER_SIZE != 0)
    dieif(sem_post(&fifo->shmbuf->full)<0, "sem_post() failed in fifo_flush");
}


static inline void fifo_init( struct fifo_t* fifo, const char* shm_name, int consumer )
{
  fifo->shm_path = shm_name;
  fifo->producer = !consumer;
  if (fifo->producer) fifo->fd = shm_open(fifo->shm_path, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU);
  else                fifo->fd = shm_open(fifo->shm_path, O_RDWR, 0);
  dieif(fifo->fd<0, "shm_open() failed in fifo_init");
  if (fifo->producer)
    dieif(ftruncate(fifo->fd, sizeof(struct shmbuf_t))<0, "ftruncate() failed in fifo_init");
  if (fifo->producer) fifo->shmbuf = (struct shmbuf_t*)mmap(NULL, sizeof(struct shmbuf_t), PROT_READ|PROT_WRITE, MAP_SHARED, fifo->fd, 0);
  else                fifo->shmbuf = (struct shmbuf_t*)mmap(NULL, sizeof(struct shmbuf_t), PROT_READ|PROT_WRITE, MAP_SHARED, fifo->fd, 0);
  dieif(fifo->shmbuf==0, "mmap() failed in fifo_init");
  if (fifo->producer) {		/* all buffers initially empty, but insert posts first */
    dieif(sem_init(&fifo->shmbuf->empty, 1, NUM_BUFFERS-2)<0, "sem_init() failed in fifo_init");
    dieif(sem_init(&fifo->shmbuf-> full, 1,             0)<0, "sem_init() failed in fifo_init");
    dieif(sem_init(&fifo->shmbuf->finished, 1,          0)<0, "sem_init() failed in fifo_init");
  }
  fifo->cursor = 0;
}


static inline void fifo_fini( struct fifo_t* fifo )
{
  if (fifo->producer) {
    fifo_flush(fifo);
    /* wait for consumer to finish */
    dieif(sem_wait(&fifo->shmbuf->finished)<0, "sem_wait() failed in fifo_fini");
  }
  else
    dieif(sem_post(&fifo->shmbuf->finished)<0, "sem_post() failed in fifo_fini");
  dieif(munmap(fifo->shmbuf, sizeof(struct shmbuf_t))<0, "munmap() failed in fifo_fini");
  if (fifo->producer)
    dieif(shm_unlink(fifo->shm_path)<0, "sem_unlink() failed in fifo_fini");
}



#ifdef __cplusplus
}
#endif
