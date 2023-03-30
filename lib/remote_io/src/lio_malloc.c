/* This malloc is extremely simple. It always uses mmap for
   memory allocations.  This assumes that longs 64-bits in size. */

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/sockios.h>
#include <popcorn.h>
#include "local_io.h"

struct lio_alloc {
  long size;
  long unused;
  void *data[0];
};

/* Sizeof (lio_alloc is 16 bytes, so PCN_MASK is applied to the pointer to
   retrieve its value.  */
#define PCN_OFFSET (sizeof (struct lio_alloc))

# define MMAP lio_mmap
# define MUNMAP lio_munmap

#define LIO_MALLOC_DEBUG 0

void *
lio_malloc (size_t size)
{
  size_t asize = sizeof (struct lio_alloc) + size;
  struct lio_alloc *mem;

  if (size == 0)
    return NULL;

  mem = MMAP (NULL, asize, PROT_READ | PROT_WRITE,
	      MAP_ANON | MAP_PRIVATE, -1, 0);

  if (mem == NULL)
    return NULL;

  mem->size = size;

  return mem->data;
}

void
lio_free (void *ptr)
{
  struct lio_alloc *mem = (struct lio_alloc *) ((uintptr_t)ptr - PCN_OFFSET);

  if (ptr == NULL)
    return;

  MUNMAP (mem, mem->size + sizeof (struct lio_alloc));

  return;
}

void *
lio_calloc(size_t nmemb, size_t size)
{
  return lio_malloc (nmemb * size);
}

void *
lio_realloc(void *ptr, size_t size)
{
  struct lio_alloc *mem = (struct lio_alloc *) ((uintptr_t)ptr - PCN_OFFSET);
  void *new_mem = NULL;

  if (ptr == NULL)
    return lio_malloc (size);

  if (size == 0)
    {
      lio_free (ptr);
      return NULL;
    }

  new_mem = lio_malloc (size);
  lio_memcpy (new_mem, mem->data, mem->size);
  lio_free (ptr);

  return new_mem;
}
