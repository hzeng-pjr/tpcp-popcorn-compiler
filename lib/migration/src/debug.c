/*
 * Migration debugging helper functions.
 *
 * Author: Rob Lyerly <rlyerly@vt.edu>
 * Date: 3/3/2018
 */

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <local_io.h>

#include "config.h"
#include "platform.h"
#include "migrate.h"
#include "debug.h"

/*
 * Helpers for dumping register contents.
 */

#define UINT64( val ) ((uint64_t)val)
#define UPPER_HALF_128( val ) \
  ({ \
    uint64_t chunk; \
    memcpy(&chunk, ((void *)(&val) + 8), 8); \
    chunk; \
  })
#define LOWER_HALF_128( val ) \
  ({ \
    uint64_t chunk; \
    memcpy(&chunk, &(val), 8); \
    chunk; \
  })

void dump_regs_aarch64(const struct regset_aarch64 *regset, const char *log)
{
  size_t i;
  int stream;

  LIO_ASSERT (regset, "Invalid regset");
  if(log)
  {
    stream = lio_open(log, O_RDWR | O_CREAT, 0644);
    if(!stream) return;
  }
  else stream = lio_stderr;

  lio_fprintf(stream, "Register set located @ 0x%x\n", regset);
  lio_fprintf(stream, "Program counter: 0x%x\n", regset->pc);
  lio_fprintf(stream, "Stack pointer: 0x%x\n", regset->sp);

  for(i = 0; i < 31; i++)
  {
    if(i == 29) lio_fprintf(stream, "Frame pointer / ");
    else if(i == 30) lio_fprintf(stream, "Link register / ");
    lio_fprintf(stream, "X%u: %u / %u / %x\n", i,
            regset->x[i], regset->x[i], regset->x[i]);
  }

  for(i = 0; i < 32; i++)
  {
    uint64_t upper = UPPER_HALF_128(regset->v[i]),
             lower = LOWER_HALF_128(regset->v[i]);
    lio_fprintf(stream, "V%u: ", i);
    if(upper) lio_fprintf(stream, "%x", upper);
    lio_fprintf(stream, "%x\n", lower);
  }

  if (stream != lio_stderr)
    lio_close(stream);
}

void dump_regs_powerpc64(const struct regset_powerpc64 *regset,
                         const char *log)
{
  size_t i;
  int stream;

  LIO_ASSERT (regset, "Invalid regset");
  if(log)
  {
    stream = lio_open(log, O_RDWR | O_CREAT, 0644);
    if(!stream) return;
  }
  else stream = lio_stderr;

  lio_fprintf(stream, "Register set located @ 0x%x\n", regset);
  lio_fprintf(stream, "Program counter: 0x%x\n", regset->pc);
  lio_fprintf(stream, "Link register: 0x%x\n", regset->lr);
  lio_fprintf(stream, "Counter: %u / %u / %x / 0x%x\n",
	      UINT64(regset->ctr), UINT64(regset->ctr), UINT64(regset->ctr),
	      regset->ctr);

  for(i = 0; i < 32; i++)
  {
    if(i == 1) lio_fprintf(stream, "Stack pointer / ");
    else if(i == 2) lio_fprintf(stream, "Table-of-contents pointer / ");
    else if(i == 13) lio_fprintf(stream, "Frame-base pointer / ");
    lio_fprintf(stream, "R%u: %u / %u / %x\n", i,
            regset->r[i], regset->r[i], regset->r[i]);
  }

  for(i = 0; i < 32; i++)
    lio_fprintf(stream, "F%u: %x\n", i, regset->f[i]);

  if (stream != lio_stderr)
    lio_close(stream);
}

void dump_regs_x86_64(const struct regset_x86_64 *regset, const char *log)
{
  size_t i;
  int stream;

  LIO_ASSERT (regset, "Invalid regset");
  if(log)
  {
    stream = lio_open(log, O_RDWR | O_CREAT, 0644);
    if(!stream) return;
  }
  else stream = lio_stderr;

  lio_fprintf(stream, "Register set located @ 0x%x\n", regset);
  lio_fprintf(stream, "Instruction pointer: 0x%x\n", regset->rip);
  lio_fprintf(stream, "RAX: %u / %u / %x\n",
          regset->rax, regset->rax, regset->rax);
  lio_fprintf(stream, "RDX: %u / %u / %x\n",
          regset->rdx, regset->rdx, regset->rdx);
  lio_fprintf(stream, "RCX: %u / %u / %x\n",
          regset->rcx, regset->rcx, regset->rcx);
  lio_fprintf(stream, "RBX: %u / %u / %x\n",
          regset->rbx, regset->rbx, regset->rbx);
  lio_fprintf(stream, "RSI: %u / %u / %x\n",
          regset->rsi, regset->rsi, regset->rsi);
  lio_fprintf(stream, "RDI: %u / %u / %x\n",
          regset->rdi, regset->rdi, regset->rdi);
  lio_fprintf(stream, "Frame pointer / RBP: %u / %u / %x\n",
          regset->rbp, regset->rbp, regset->rbp);
  lio_fprintf(stream, "Stack pointer / RSP: %u / %u / %x\n",
          regset->rsp, regset->rsp, regset->rsp);
  lio_fprintf(stream, "R8: %u / %u / %x\n",
          regset->r8, regset->r8, regset->r8);
  lio_fprintf(stream, "R9: %u / %u / %x\n",
          regset->r9, regset->r9, regset->r9);
  lio_fprintf(stream, "R10: %u / %u / %x\n",
          regset->r10, regset->r10, regset->r10);
  lio_fprintf(stream, "R11: %u / %u / %x\n",
          regset->r11, regset->r11, regset->r11);
  lio_fprintf(stream, "R12: %u / %u / %x\n",
          regset->r12, regset->r12, regset->r12);
  lio_fprintf(stream, "R13: %u / %u / %x\n",
          regset->r13, regset->r13, regset->r13);
  lio_fprintf(stream, "R14: %u / %u / %x\n",
          regset->r14, regset->rax, regset->rax);
  lio_fprintf(stream, "R15: %u / %u / %x\n",
          regset->r15, regset->r15, regset->r15);

  for(i = 0; i < 16; i++)
  {
    uint64_t upper = UPPER_HALF_128(regset->xmm[i]),
             lower = LOWER_HALF_128(regset->xmm[i]);
    lio_fprintf(stream, "XMM%u: ", i);
    if(upper) lio_fprintf(stream, "%x", upper);
    lio_fprintf(stream, "%x\n", lower);
  }

  if (stream != lio_stderr)
    lio_close(stream);
}

void dump_regs(const void *regset, const char *log)
{
#if defined __aarch64__
  dump_regs_aarch64(regset, log);
#elif defined __powerpc64__
  dump_regs_powerpc64(regset, log);
#else /* x86_64 */
  dump_regs_x86_64(regset, log);
#endif
}

/* Per-node debugging structures */
typedef struct {
  size_t threads;
  int fd;
  pthread_mutex_t lock;
  char padding[PAGESZ - (2 * sizeof(size_t)) - sizeof(pthread_mutex_t)];
} remote_debug_t;

static __attribute__((aligned(PAGESZ))) remote_debug_t
debug_info[MAX_POPCORN_NODES];

static void segfault_handler(int sig, siginfo_t *info, void *ctx)
{
#if _LOG == 1
#define BUFSIZE 512
#define LOG_WRITE( format, ... ) \
  do { \
    char buf[BUFSIZE]; \
    int sz = lio_snprintf(buf, BUFSIZE, format, __VA_ARGS__) + 1; \
    write(debug_info[nid].fd, buf, sz); \
  } while(0);

  // Note: *must* use trylock to ensure we don't block in signal handler
  //int nid = popcorn_getnid();
  int nid = 0;
  if(!pthread_mutex_trylock(&debug_info[nid].lock) && debug_info[nid].fd)
  {
    LOG_WRITE("%u: segfault @ 0x%x\n", info->si_pid, info->si_addr);
    pthread_mutex_unlock(&debug_info[nid].lock);
  }
#undef LOG_WRITE
#endif

  // TODO do we need to migrate back to the origin before exiting?
  kill(getpid(), SIGSEGV);
  _Exit(SIGSEGV);
}

/*
 * If first thread to arrive on a node, open files and register signal handlers
 * for resilient remote crashes.
 */
void remote_debug_init(int nid)
{
  struct sigaction act;

  if(nid < 0 || nid >= MAX_POPCORN_NODES) return;

  pthread_mutex_lock(&debug_info[nid].lock);
  if(!debug_info[nid].threads) // First thread to arrive on node
  {
#if _LOG == 1
    char fn[32];
    lio_snprintf(fn, 32, "/tmp/node-%d.log", nid);
    debug_info[nid].fd = open(fn, O_CREAT | O_APPEND);
#endif
    act.sa_sigaction = segfault_handler;
    sigfillset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    act.sa_restorer = NULL;
    sigaction(SIGSEGV, &act, NULL);
  }
  debug_info[nid].threads += 1;
  pthread_mutex_unlock(&debug_info[nid].lock);
}

/*
 * If the last thread to leave a node, close files.
 */
void remote_debug_cleanup(int nid)
{
  if(nid < 0 || nid >= MAX_POPCORN_NODES) return;

  pthread_mutex_lock(&debug_info[nid].lock);
  debug_info[nid].threads -= 1;
  if(!debug_info[nid].threads)
  {
#if _LOG == 1
    close(debug_info[nid].fd);
    debug_info[nid].fd = 0;
#endif
    // TODO do we want to clean up signal handler?
  }
  pthread_mutex_unlock(&debug_info[nid].lock);
}

#if _CLEAN_CRASH == 1
static void __attribute__((constructor)) __init_debug_info()
{
  size_t i;
  for(i = 0; i < MAX_POPCORN_NODES; i++)
    pthread_mutex_init(&debug_info[i].lock, NULL);
}
#endif

