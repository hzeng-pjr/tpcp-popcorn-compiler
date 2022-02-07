#ifndef IO_H
#define IO_H

#include <stddef.h>
#include <sys/mman.h>

#if defined (__x86_64__)
#define pcn_break()  do { asm volatile ("int3;"); } while (0)
#elif defined (__aarch64__)
#define pcn_break()  do { asm volatile ("int3;"); } while (0)
#else
#define pcn_break() spin()
#endif

extern void *do_mmap(void *addr, size_t length, int prot, int flags,
		     int fd, off_t offset);
extern int do_munmap (void *addr, size_t len);
extern int do_mprotect (void *addr, size_t len, int prot);
extern int do_write (int fd, const void *buf, unsigned long count);
extern ssize_t do_read(int fd, void *buf, size_t count);
extern ssize_t do_pread(int fd, void *buf, size_t count, off_t offset);
extern int do_open (const char *pathname, int flags, mode_t mode);
extern int do_close (int fd);
extern void do_exit (int status);
extern int do_strcmp (char *a, char *b);
extern void print (char *str);
extern void error (char *str);
extern void do_memset (void *s, int c, size_t n);
extern void do_spin ();

#endif
