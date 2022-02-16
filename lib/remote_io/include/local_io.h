#ifndef IO_H
#define IO_H

#include <stddef.h>
#include <sys/mman.h>
#include <signal.h>

#if defined (__x86_64__)
#define pcn_break()  do { asm volatile ("int3;"); } while (0)
#elif defined (__aarch64__)
#define pcn_break()  do { asm volatile ("brk #01;"); } while (0)
#else
#define pcn_break() spin()
#endif

#define LIO_ASSERT(cond, msg) lio_assert ((int)(cond), msg, __FILE__, __LINE__)

struct ksigaction {
	void (*handler)(int);
	unsigned long flags;
	void (*restorer)(void);
	unsigned mask[2];
};

struct iovec;

#define lio_stdin 0
#define lio_stdout 1
#define lio_stderr 2

extern void *lio_mmap(void *addr, size_t length, int prot, int flags,
		     int fd, off_t offset);
extern int lio_munmap (void *addr, size_t len);
extern int lio_mprotect (void *addr, size_t len, int prot);
extern int lio_write (int fd, const void *buf, unsigned long count);
extern int lio_writev (int fd, const struct iovec *iov, int iovcnt);
extern ssize_t lio_read(int fd, void *buf, size_t count);
extern ssize_t lio_pread(int fd, void *buf, size_t count, off_t offset);
extern int lio_open (const char *pathname, int flags, mode_t mode);
extern int lio_close (int fd);
extern void lio_exit (int status);
extern int lio_getpid ();
extern int lio_kill (pid_t pid, int sig);

extern int lio_rt_sigaction (int sig, struct ksigaction *kact,
			    struct ksigaction *koact, int nr);
extern int lio_sigprocmask (int sig, sigset_t *set, sigset_t *oset, int nr);
extern int lio_sigaddset (sigset_t *set, int sig);

extern int lio_strlen ();
extern int lio_strcmp (char *a, char *b);
extern void lio_memset (void *s, int c, size_t n);
extern void lio_memcpy (void *d, void *s, size_t n);
extern void lio_spin ();

extern int lio_printf (const char *fmt, ...);
extern int lio_fprintf (int fd, const char *fmt, ...);
extern int lio_snprintf (char *str, size_t size, const char *fmt, ...);
extern void lio_print (char *str);
extern void lio_error (char *str);
extern void lio_assert (int cond, char *msg, char *file, int lineno);

#endif
