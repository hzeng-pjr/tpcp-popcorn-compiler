/*
 * User-space stack rewriting implementation.  Includes all APIs to boostrap
 * and re-write the stack for a currently-executing program, all in userspace.
 *
 * Author: Rob Lyerly
 * Date: 3/1/2016
 */

#ifdef PTHREAD_TLS
#include <pthread.h>
#endif
#include <unistd.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/mman.h>

#include "stack_transform.h"
#include "definitions.h"
#include "util.h"
#include "arch/x86_64/regs.h"
#include "arch/aarch64/regs.h"

#define _UPOPCORN_BUILD

///////////////////////////////////////////////////////////////////////////////
// File-local API & definitions
///////////////////////////////////////////////////////////////////////////////

static st_handle aarch64_handle = NULL;
static st_handle x86_64_handle = NULL;
//static stack_bounds bounds = { .high = NULL, .low = NULL };

/*
 * Set inside of musl at __libc_start_main() to point to where function
 * activations begin on the stack.
 */
extern void* __popcorn_stack_base;

/*
 * Touch stack pages up to the OS-defined stack size limit, so that the OS
 * allocates them and we can divide the stack in half for rewriting.  Also,
 * calculate stack bounds for main thread.
 */
//static bool prep_stack(void);

/*
 * Get main thread's stack information from procfs.
 */
static bool get_main_stack(stack_bounds* bounds);

/*
 * Get thread's stack information from pthread library.
 */
//static bool get_thread_stack(stack_bounds* bounds);

/*
 * Rewrite from the current stack (metadata provided by handle_a) to a
 * transformed stack (handle_b).
 */
static int userspace_rewrite_internal(void* sp,
                                      void* regs,
                                      void* dest_regs,
                                      st_handle handle_a,
                                      st_handle handle_b);

///////////////////////////////////////////////////////////////////////////////
// User-space initialization, rewriting & teardown
///////////////////////////////////////////////////////////////////////////////

/*
 * Program name, as invoked by the shell.
 */
// Note: set by glibc/musl-libc, non-portable!
extern const char *__progname;

/*
 * Binary names.  User-code can define these symbols to override these
 * definitions in order to provide the names transparently.
 */
char* __attribute__((weak)) aarch64_fn = NULL;
static bool alloc_aarch64_fn = false;
char* __attribute__((weak)) x86_64_fn = NULL;
static bool alloc_x86_64_fn = false;

/*
 * Initialize rewriting meta-data on program startup.  Users *must* set the
 * names of binaries using one of the three methods described below.
 */
void __st_userspace_ctor(void)
{
#if 0
  /* Initialize the stack for the main thread. */
  if(!prep_stack())
  {
    ST_WARN("could not prepare stack for user-space rewriting\n");
    return;
  }
#endif

  /* Prepare libELF. */
  if(elf_version(EV_CURRENT) == EV_NONE)
  {
    ST_WARN("could not prepare libELF for reading binary\n");
    return;
  }

  /*
   * Initialize ST handles - tries the following approaches to finding the
   * binaries:
   *
   * 1. Check environment variables (defined in config.h)
   * 2. Check if application has overridden file name symbols (defined above)
   * 3. Add architecture suffixes to current binary name (defined by libc)
   */
  if(getenv(ENV_AARCH64_BIN)) aarch64_handle = st_init(getenv(ENV_AARCH64_BIN));
  else if(aarch64_fn) aarch64_handle = st_init(aarch64_fn);
  else {
    aarch64_fn = (char*)pmalloc(sizeof(char) * BUF_SIZE);
    snprintf(aarch64_fn, BUF_SIZE, "%s_aarch64", __progname);
    aarch64_handle = st_init(aarch64_fn);
    alloc_aarch64_fn = true;
  }

  if(!aarch64_handle) {
    ST_WARN("could not initialize aarch64 handle\n");
  }

  if(getenv(ENV_X86_64_BIN)) x86_64_handle = st_init(getenv(ENV_X86_64_BIN));
  else if(x86_64_fn) x86_64_handle = st_init(x86_64_fn);
  else {
    x86_64_fn = (char*)pmalloc(sizeof(char) * BUF_SIZE);
    snprintf(x86_64_fn, BUF_SIZE, "%s_x86-64", __progname);
    x86_64_handle = st_init(x86_64_fn);
    alloc_x86_64_fn = true;
  }

  if(!x86_64_handle) {
    ST_WARN("could not initialize x86-64 handle\n");
  }
}

/*
 * Free stack-transformation memory.
 */
void __st_userspace_dtor(void)
{
  if(aarch64_handle)
  {
    st_destroy(aarch64_handle);
    if(alloc_aarch64_fn) pfree(aarch64_fn);
  }

  if(x86_64_handle)
  {
    st_destroy(x86_64_handle);
    if(alloc_x86_64_fn) pfree(x86_64_fn);
  }
}


/* Public-facing rewrite macros */

// TODO: the program location stored in the regset doesn't correspond to a call
// site, only the location where the inline assembly grabbed the PC.  For now,
// correct the program location using the rewrite API's return address.

/*
 * Rewrite from source to destination stack.
 */
int st_userspace_rewrite(void* sp,
                         void* regs,
                         void* dest_regs)
{
  if(!aarch64_handle || !x86_64_handle)
  {
    ST_WARN("could not load user-space rewriting information\n");
    return 1;
  }

#ifdef __aarch64__
  struct regset_aarch64* real_regs = (struct regset_aarch64*)regs;
  real_regs->pc = __builtin_return_address(0);
  return userspace_rewrite_internal(sp,
                                    regs,
                                    dest_regs,
                                    aarch64_handle,
                                    x86_64_handle);
#elif defined __x86_64__
  struct regset_x86_64* real_regs = (struct regset_x86_64*)regs;
  real_regs->rip = __builtin_return_address(0);
  return userspace_rewrite_internal(sp,
                                    regs,
                                    dest_regs,
                                    x86_64_handle,
                                    aarch64_handle);
#endif
}

/*
 * Rewrite from aarch64 -> aarch64.
 */
int st_userspace_rewrite_aarch64(void* sp,
                                 struct regset_aarch64* regs,
                                 struct regset_aarch64* dest_regs)
{
  if(!aarch64_handle)
  {
    ST_WARN("could not load user-space rewriting information\n");
    return 1;
  }

  regs->pc = __builtin_return_address(0);

#ifdef _SIMPLIFY_HOMOGENEOUS_MIGRATION
	unsigned long *rbp = __builtin_frame_address(1);
	memcpy(dest_regs, regs, sizeof(*dest_regs));
	dest_regs->x[29] = *rbp;
	dest_regs->sp = (unsigned long)(rbp + 1); // XXX need to check

	return 0;
#else
  return userspace_rewrite_internal(sp,
                                    regs,
                                    dest_regs,
                                    aarch64_handle,
                                    aarch64_handle);
#endif
}

/*
 * Rewrite from x86_64 -> x86_64.
 */
int st_userspace_rewrite_x86_64(void* sp,
                                struct regset_x86_64* regs,
                                struct regset_x86_64* dest_regs)
{
  if(!x86_64_handle)
  {
    ST_WARN("could not load user-space rewriting information\n");
    return 1;
  }

  regs->rip = __builtin_return_address(0);

#ifdef _SIMPLIFY_HOMOGENEOUS_MIGRATION
	unsigned long *rbp = __builtin_frame_address(1);
	memcpy(dest_regs, regs, sizeof(*dest_regs));
	dest_regs->rbp = *rbp;
	dest_regs->rsp = (unsigned long)(rbp + 1);

	return 0;
#else
  return userspace_rewrite_internal(sp,
                                    regs,
                                    dest_regs,
                                    x86_64_handle,
                                    x86_64_handle);
#endif
}

///////////////////////////////////////////////////////////////////////////////
// File-local API (implementation)
///////////////////////////////////////////////////////////////////////////////

#if 0
/*
 * Touch stack pages up to the OS-defined stack size limit, so that the OS
 * allocates them and we can divide the stack in half for rewriting.  Also,
 * calculate stack bounds for main thread.
 */
static bool prep_stack(void)
{

  if(!get_main_stack(&bounds)) return false;

  return true;
}
#endif

void upopcorn_get_stack_base_and_size(uintptr_t* upopcorn_stack_base, uintptr_t* upopcorn_stack_size);
/* Read stack information for the main thread from the procfs. */
static bool get_main_stack(stack_bounds* bounds)
{
#ifdef _UPOPCORN_BUILD
  uintptr_t upopcorn_stack_base;
  uintptr_t upopcorn_stack_size;
  upopcorn_get_stack_base_and_size(&upopcorn_stack_base, &upopcorn_stack_size);
  bounds->low = (void*)upopcorn_stack_base;
  bounds->high = (void*)upopcorn_stack_base+upopcorn_stack_size;
  ST_INFO("Preparing stack for main thread, addresses %p -> %p; psb %p\n",
	  bounds->low, bounds->high, __popcorn_stack_base);
  bool found = upopcorn_stack_base;
#if 0
  if(found)
  {
    size_t offset;
    /*
     * Get offset of main thread's stack pointer from stack base so we can avoid
     * clobbering argv & environment variables.
     */
    ASSERT(__popcorn_stack_base, "Stack base not correctly set by musl\n");
    offset = (uint64_t)(bounds->high - __popcorn_stack_base);
    offset += (offset % 0x10 ? 0x10 - (offset % 0x10) : 0);
    bounds->high -= offset;
  }
#endif
  ST_INFO("Prepped stack for main thread, addresses %p -> %p; psb %p\n",
	  bounds->low, bounds->high, __popcorn_stack_base);
#else
  /* /proc/<id>/maps fields */
  bool found = false;
  int fields;
  uint64_t start, end, offset, inode;
  char perms[8]; // should be no more than 4
  char dev[8]; // should be no more than 5
  char path[BUF_SIZE];

  /* File data, reading & parsing */
  char proc_fn[BUF_SIZE];
  FILE* proc_fp;
  char* lineptr;
  size_t linesz = BUF_SIZE;

  bounds->high = NULL;
  bounds->low = NULL;

  if(snprintf(proc_fn, BUF_SIZE, "/proc/%d/maps", getpid()) < 0) return false;
  if(!(proc_fp = fopen(proc_fn, "r"))) return false;
  if(!(lineptr = (char*)pmalloc(BUF_SIZE * sizeof(char)))) return false;
  while(getline(&lineptr, &linesz, proc_fp) >= 0)
  {
    fields = sscanf(lineptr, "%lx-%lx %s %lx %s %lu %s",
                    &start, &end, perms, &offset, dev, &inode, path);
    if(fields == 7 && !strncmp(path, "[stack]", 7))
    {
      bounds->high = (void*)end;
      bounds->low = (void*)start;
      found = true;
      break;
    }
  }
  pfree(lineptr);
  fclose(proc_fp);
#endif
  ST_INFO("procfs stack limits: %p -> %p\n", bounds->low, bounds->high);
  return found;
}

#if 0
/* Read stack information for cloned threads from the pthread library. */
static bool get_thread_stack(stack_bounds* bounds)
{
  pthread_attr_t attr;
  size_t stack_size;
  int ret;
  bool retval;

  ret = pthread_getattr_np(pthread_self(), &attr);
  ret |= pthread_attr_getstack(&attr, &bounds->low, &stack_size);
  if(ret == 0)
  {
    // TODO is there any important data stored above muslc/start's stack frame?
    bounds->high = bounds->low + stack_size;
    if(stack_size != MAX_STACK_SIZE)
    {
      ST_WARN("unexpected stack size: expected %lx, got %lx\n",
              MAX_STACK_SIZE, stack_size);
      bounds->low = bounds->high - MAX_STACK_SIZE;
    }
    retval = true;
  }
  else
  {
    bounds->high = 0;
    bounds->low = 0;
    ST_WARN("could not get stack limits\n");
    retval = false;
  }
  ST_INFO("Thread stack limits: %p -> %p\n", bounds->low, bounds->high);
  return retval;
}
#endif

#define ERR_CHECK(func) if(func) do{perror(__func__); exit(-1);}while(0)
void *__mmap(void *start, size_t len, int prot, int flags, int fd, off_t off);

#ifndef ALL_STACK_BASE
#define ALL_STACK_BASE 0x600000000000UL
#endif
#ifndef ALL_STACK_ALIGN
#define ALL_STACK_ALIGN 0x1000
#endif

/* FIXME: don't allocate a new stack everytime: use old stack ? */
void* allocate_new_stack(unsigned long len)
{
	unsigned long ret;
	static unsigned long stack_base=ALL_STACK_BASE;

	len=(len+(len-1)) & ~(len-1);
	ERR_CHECK((__mmap((void*)stack_base, len, PROT_READ | PROT_WRITE,
			MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0)==MAP_FAILED));
	ret=stack_base;
	stack_base+=len;
	return (void*)ret;

}

/*
 * Rewrite from source to destination stack.  Logically, divides 8MB stack in
 * half, detects which half we're currently using and rewrite to the other.
 */
static int userspace_rewrite_internal(void* sp,
                                      void* regs,
                                      void* dest_regs,
                                      st_handle handle_a,
                                      st_handle handle_b)
{
  int retval = 0;
  void* stack_a, *stack_b, *cur_stack, *new_stack;
  stack_bounds bounds = { .high = NULL, .low = NULL };

  if(!sp || !regs || !dest_regs || !handle_a || !handle_b)
  {
    ST_WARN("invalid arguments\n");
    return 1;
  }

  /* If not already resolved, get stack limits for thread. */
  //if(bounds.high == NULL)
    if(!get_main_stack(&bounds)) return 1;

  if(sp < bounds.low || bounds.high <= sp)
  {
    ST_INFO("stack bounds %p -> %p; sp %p\n",
          bounds.low, bounds.high, sp);
    ST_WARN("invalid stack pointer\n");
    return 1;
  }
  
  ST_INFO("rewriting stack bounds %p -> %p\n",
          bounds.low, bounds.high);

  ST_INFO("Thread %ld beginning re-write\n", syscall(SYS_gettid));

  /* Divide stack into two halves. */
  stack_a = bounds.high;
  stack_b = bounds.low + B_STACK_OFFSET;

  /* Find which half the current stack uses and rewrite to other. */
  cur_stack = (sp >= stack_b) ? stack_a : stack_b;
  new_stack = (sp >= stack_b) ? stack_b : stack_a;
  //new_stack = allocate_new_stack(MAX_STACK_SIZE); //new_stack = (void*)((unsigned long)new_stack+MAX_STACK_SIZE);//stack end
  ST_INFO("On stack %p, rewriting to %p\n", cur_stack, new_stack);
  if(st_rewrite_stack(handle_a, regs, cur_stack,
                      handle_b, dest_regs, new_stack))
  {
    ST_WARN("stack transformation failed (%s -> %s)\n",
            arch_name(handle_a->arch), arch_name(handle_b->arch));
    retval = 1;
  }

  return retval;
}

