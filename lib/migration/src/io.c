#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <elf.h>
#include <errno.h>
#include <sys/auxv.h>
#include "syscall_arch.h"

void
*do_mmap(void *addr, size_t length, int prot, int flags,
	      int fd, off_t offset)
{
  return (void *) __syscall6 (SYS_mmap, (uintptr_t) addr, length, prot,
			      flags, fd, offset);
}

int
do_munmap (void *addr, size_t len)
{
  return __syscall2 (SYS_munmap, (uintptr_t) addr, len);
}

int
do_mprotect (void *addr, size_t len, int prot)
{
  return __syscall3 (SYS_mprotect, (uintptr_t) addr, len, prot);
}

int
do_write (int fd, const void *buf, unsigned long count)
{
  return __syscall3 (SYS_write, fd, (uintptr_t) buf, count);
}

int
do_writev (int fd, const struct iovec *iov, int iovcnt)
{
  return __syscall3 (SYS_writev, fd, (uintptr_t) iov, iovcnt);
}

ssize_t
do_read(int fd, void *buf, size_t count)
{
  return __syscall3 (SYS_read, fd, (uintptr_t) buf, count);
}

ssize_t
do_pread(int fd, void *buf, size_t count, off_t offset)
{
  return __syscall4 (SYS_pread64, fd, (uintptr_t) buf, count, offset);
}

int
do_open (const char *pathname, int flags, mode_t mode)
{
#ifdef SYS_open
  return __syscall3 (SYS_open, (uintptr_t)pathname, flags, mode);
#else
  return __syscall4 (SYS_openat, AT_FDCWD, (uintptr_t)pathname, flags, mode);
#endif
}

int
do_close (int fd)
{
  return __syscall1 (SYS_close, fd);
}

void
do_exit (int status)
{
  __syscall1 (SYS_exit, status);
}

int
do_getpid ()
{
  return __syscall0 (SYS_getpid);
}

int
do_strlen (char *a)
{
  int i;

  for (i = 0; a[i] != '\0'; i++)
    ;

  return i;
}

int
do_strcmp (char *a, char *b)
{
  int i, res;

  for (i = 0; a[i] != '\0' && b[i] != '\0' && a[i] == b[i]; i++)
    ;

  return a[i] - b[i];
}

void
print (char *str)
{
  int len;

  for (len = 0; str[len] != '\0'; len++)
    ;

  do_write (1, str, len);
}

void
error (char *str)
{
  print (str);
  do_exit (-1);
}

void
do_memset (void *s, int c, size_t n)
{
  int i;
  uint8_t *t = s;

  for (i = 0; i < n; i++)
    t[i] = c;
}

void
do_spin ()
{
  volatile int lock = 1;
  while (lock)
    ;
}
