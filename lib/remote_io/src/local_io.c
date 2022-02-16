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
#include <signal.h>
#include "syscall_arch.h"
#include "local_io.h"

/* Glibc: sysdeps/unix/sysv/linux/sigopts.h  */

/* Return a mask that includes the bit for SIG only.  */
# define __sigmask(sig) \
  (((unsigned long int) 1) << (((sig) - 1) % (8 * sizeof (unsigned long int))))

/* Return the word index for SIG.  */
# define __sigword(sig) (((sig) - 1) / (8 * sizeof (unsigned long int)))


void
*lio_mmap(void *addr, size_t length, int prot, int flags,
	      int fd, off_t offset)
{
  return (void *) __syscall6 (SYS_mmap, (uintptr_t) addr, length, prot,
			      flags, fd, offset);
}

int
lio_munmap (void *addr, size_t len)
{
  return __syscall2 (SYS_munmap, (uintptr_t) addr, len);
}

int
lio_mprotect (void *addr, size_t len, int prot)
{
  return __syscall3 (SYS_mprotect, (uintptr_t) addr, len, prot);
}

int
lio_write (int fd, const void *buf, unsigned long count)
{
  return __syscall3 (SYS_write, fd, (uintptr_t) buf, count);
}

int
lio_writev (int fd, const struct iovec *iov, int iovcnt)
{
  return __syscall3 (SYS_writev, fd, (uintptr_t) iov, iovcnt);
}

ssize_t
lio_read(int fd, void *buf, size_t count)
{
  return __syscall3 (SYS_read, fd, (uintptr_t) buf, count);
}

ssize_t
lio_pread(int fd, void *buf, size_t count, off_t offset)
{
  return __syscall4 (SYS_pread64, fd, (uintptr_t) buf, count, offset);
}

int
lio_open (const char *pathname, int flags, mode_t mode)
{
#ifdef SYS_open
  return __syscall3 (SYS_open, (uintptr_t)pathname, flags, mode);
#else
  return __syscall4 (SYS_openat, AT_FDCWD, (uintptr_t)pathname, flags, mode);
#endif
}

int
lio_close (int fd)
{
  return __syscall1 (SYS_close, fd);
}

void
lio_exit (int status)
{
  __syscall1 (SYS_exit, status);
}

int
lio_getpid ()
{
  return __syscall0 (SYS_getpid);
}

int
lio_kill (pid_t pid, int sig)
{
  return __syscall2 (SYS_kill, pid, sig);
}

int
lio_arch_prctl (int code, unsigned long addr)
{
#ifdef __x86_64__
  return __syscall2 (SYS_arch_prctl, code, addr);
#else
  return 0;
#endif
}

int
lio_rt_sigaction (int sig, struct ksigaction *kact, struct ksigaction *koact,
		 int nr)
{
  return __syscall4 (SYS_rt_sigaction,  sig, (uintptr_t) kact,
		     (uintptr_t) koact, nr);
}

int
lio_sigprocmask (int sig, sigset_t *set, sigset_t *oset, int nr)
{
  return __syscall4 (SYS_rt_sigprocmask, sig, (uintptr_t) set,
		     (uintptr_t) oset, nr);
}

int
lio_sigaddset (sigset_t *set, int sig)
{
  unsigned long int __mask = __sigmask (sig);
  unsigned long int __word = __sigword (sig);

  set->__val[__word] |= __mask;

  return 0;
}

int
lio_strlen (char *a)
{
  int i;

  for (i = 0; a[i] != '\0'; i++)
    ;

  return i;
}

int
lio_strcmp (char *a, char *b)
{
  int i;

  for (i = 0; a[i] != '\0' && b[i] != '\0' && a[i] == b[i]; i++)
    ;

  return a[i] - b[i];
}

void
lio_print (char *str)
{
  int len;

  for (len = 0; str[len] != '\0'; len++)
    ;

  lio_write (1, str, len);
}

void
lio_error (char *str)
{
  lio_print (str);
  lio_exit (-1);
}

void
lio_assert (int cond, char *msg, char *file, int lineno)
{
  if (cond)
    return;

  lio_printf ("%s:%u -- %s\n", file, lineno, msg);
  lio_exit (EXIT_FAILURE);
}

void
lio_memset (void *s, int c, size_t n)
{
  int i;
  uint8_t *t = s;

  for (i = 0; i < n; i++)
    t[i] = c;
}

void
lio_memcpy (void *d, void *s, size_t n)
{
  int i;
  uint8_t *dd = d;
  uint8_t *ss = s;

  for (i = 0; i < n; i++)
    dd[i] = ss[i];
}

void
lio_spin ()
{
  volatile int lock = 1;
  while (lock)
    ;
}
