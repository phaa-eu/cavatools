/*
  Copyright (c) 2020 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>


#include "shmfifo.h"
#include "caveat.h"



#define DEFAULT_BUFSIZE  12


/* Producer side fifo initialization.
     bufid	- number = file descriptor (already opened)
		  $name = shared memory segment /dev/shm/name
		  otherwise = trace file path name
     bufsize	- log-base-2 number of bytes
*/
struct fifo_t* fifo_create( const char* bufid, int bufsize )
{
  //  assert(sizeof(struct fifo_t) == 2*64);
  if (bufsize == 0)
    bufsize = DEFAULT_BUFSIZE;
  assert(bufsize > 3);
  int fd = shm_open(bufid, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU);
  dieif(fd<0, "shm_open() failed in fifo_create");
  size_t fsize = (1<<bufsize) + sizeof(struct fifo_t);
  dieif(ftruncate(fd, fsize)<0, "ftruncate() failed in fifo_create");
  struct fifo_t* fifo = (struct fifo_t*)mmap(NULL, fsize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  dieif(fifo==0, "mmap() failed in fifo_create");
  assert(((uint64_t)fifo & 0x3fL) == 0L);
  memset((char*)fifo, 0, fsize);
  fifo->size = bufsize;
  fifo->fd = fd;
  fifo->get_mask = fifo->put_mask = (1<<(bufsize-3))-1;
  fifo->id = bufid;
  return fifo;
}


/* Consumer side fifo initialization.
     bufid	- number = file descriptor (already opened)
		  $name = shared memory segment /dev/shm/name
		  otherwise = trace file path name
*/
struct fifo_t* fifo_open( const char* bufid )
{
  int fd = shm_open(bufid, O_RDWR, 0);
  dieif(fd<0, "shm_open() failed in fifo_open");
  struct fifo_t* fifo = (struct fifo_t*)mmap(NULL, sizeof(struct fifo_t), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  dieif(fifo==0, "first mmap() failed in fifo_open");
  size_t fsize = (1<<fifo->size) + sizeof(struct fifo_t);
  dieif(munmap(fifo, sizeof(struct fifo_t))<0, "munmap() failed in fifo_open");
  fifo = (struct fifo_t*)mmap(NULL, fsize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  dieif(fifo==0, "second mmap() failed in fifo_open");
  assert(((uint64_t)fifo & 0x3fL) == 0L);
  return fifo;
}


/* Consumer side fifo termination. */
void fifo_close( struct fifo_t* fifo )
{
  fifo->finished = 1;
  futex_wake(&fifo->finished);
  size_t fsize = (1<<fifo->size) + sizeof(struct fifo_t);
  dieif(munmap(fifo, fsize)<0, "munmap() failed in fifo_close");
}


/* Producer side fifo termination. */
void fifo_finish( struct fifo_t* fifo )
{
  fifo_flush(fifo);
  /* wait for consumer to finish */
  futex_hibernate(&fifo->finished, 0);
  size_t fsize = (1<<fifo->size) + sizeof(struct fifo_t);
  dieif(munmap(fifo, fsize)<0, "munmap() failed in fifo_finish");
}


void fifo_debug( struct fifo_t* fifo, const char* msg )
{
  fprintf(stderr, "%s: HEAD=%d, head=%d, TAIL=%d, tail=%d\n",
	  msg, fifo->HEAD, fifo->head, fifo->TAIL, fifo->tail);
}
