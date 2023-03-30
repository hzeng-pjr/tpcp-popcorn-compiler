#ifndef IO_H
#define IO_H

#include <unistd.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <signal.h>
#include <poll.h>

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
extern int lio_gettid ();
extern int lio_kill (pid_t pid, int sig);
extern int lio_tgkill (pid_t pid, pid_t tid, int sig);
extern int lio_arch_prctl (int code, unsigned long addr);

extern int lio_rt_sigaction (int sig, struct ksigaction *kact,
			    struct ksigaction *koact, int nr);
extern int lio_sigprocmask (int sig, sigset_t *set, sigset_t *oset, int nr);
extern int lio_sigaddset (sigset_t *set, int sig);

extern int lio_poll (struct pollfd *fds, int nfds, int timeout);
extern int lio_ppoll (struct pollfd *fds, int nfds, const struct timespec *tp,
		      const sigset_t *sigmask);
extern int lio_select (int nfds, fd_set *readfds, fd_set *writefds,
                       fd_set *exceptfds, struct timeval *timeout);
extern int lio_pselect (int nfds, fd_set *readfds, fd_set *writefds,
                        fd_set *exceptfds, const struct timespec *timeout,
                        const sigset_t *sigmask);

extern int lio_strlen ();
extern int lio_strcmp (char *a, char *b);
extern void lio_memset (void *s, int c, size_t n);
extern void *lio_memcpy (void *restrict d, const void *s, size_t n);
extern void lio_spin ();

extern int lio_printf (const char *fmt, ...);
extern int lio_dbg_printf (const char *fmt, ...);
extern int lio_fprintf (int fd, const char *fmt, ...);
extern int lio_snprintf (char *str, size_t size, const char *fmt, ...);
extern void lio_print (char *str);
extern void lio_error (const char *restrict fmt, ...);
extern void lio_assert (int cond, char *msg, char *file, int lineno);

extern int rio_dbg_printf (const char *fmt, ...);
extern int rio_dbg_fprintf (int fd, const char *fmt, ...);
extern int rio_dbg_snprintf (char *str, size_t size, const char *fmt, ...);
extern int rio_dbg_vfprintf (int fd, const char *restrict fmt, va_list arg);

extern void *lio_malloc (size_t size);
extern void lio_free (void *ptr);
extern void *lio_calloc(size_t nmemb, size_t size);
extern void * lio_realloc(void *ptr, size_t size);

/* Workaround LLVM Stackmap liveout limitations.  */
#define LIO_TOUCH(var) (*(volatile typeof(var)*)&(var))

#endif
