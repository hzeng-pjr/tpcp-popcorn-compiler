/*
 * Utilities for timing stack rewriting operations.
 *
 * Author: Rob Lyerly
 * Date: 11/18/2015
 */

#include <time.h>

/* Generate a call site to get rewriting metadata for outermost frame. */
static void* __attribute__((noinline))
get_call_site() { return __builtin_return_address(0); }

#ifdef __aarch64__

/* Times rewriting the entire stack (aarch64) */
#define TIME_REWRITE( aarch64_bin, x86_64_bin ) \
  ({ \
    int ret; \
    struct timespec start = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec init = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec rewrite = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec end = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct regset_aarch64 regset; \
    struct regset_x86_64 regset_dest; \
    stack_bounds bounds = get_stack_bounds(); \
    READ_REGS_AARCH64(regset); \
    regset.pc = get_call_site(); \
    clock_gettime(CLOCK_MONOTONIC, &start); \
    st_handle src = st_init(aarch64_bin); \
    st_handle dest = st_init(x86_64_bin); \
    clock_gettime(CLOCK_MONOTONIC, &init); \
    if(src && dest) \
    { \
      ret = st_rewrite_stack(src, &regset, bounds.high, dest, &regset_dest, bounds.low); \
      clock_gettime(CLOCK_MONOTONIC, &rewrite); \
      st_destroy(src); \
      st_destroy(dest); \
      if(ret) \
        lio_fprintf(lio_stderr, "Couldn't re-write the stack\n"); \
      else \
      { \
        clock_gettime(CLOCK_MONOTONIC, &end); \
        lio_printf("[ST] Setup time: %u\n",				\
		   (init.tv_sec * 1000000000 + init.tv_nsec) -		\
		   (start.tv_sec * 1000000000 + start.tv_nsec));	\
        lio_printf("[ST] Transform time: %u\n",				\
		   (rewrite.tv_sec * 1000000000 + rewrite.tv_nsec) -	\
		   (init.tv_sec * 1000000000 + init.tv_nsec));		\
        lio_printf("[ST] Cleanup time: %u\n",				\
		   (end.tv_sec * 1000000000 + end.tv_nsec) -		\
		   (rewrite.tv_sec * 1000000000 + rewrite.tv_nsec));	\
        lio_printf("[ST] Total elapsed time: %u\n",			\
		   (end.tv_sec * 1000000000 + end.tv_nsec) -		\
		   (start.tv_sec * 1000000000 + start.tv_nsec));	\
      } \
    } \
    else \
    { \
      lio_fprintf(lio_stderr, "Couldn't open ELF information\n"); \
      if(src) st_destroy(src); \
      if(dest) st_destroy(dest); \
    } \
  })

/*
 * Times rewriting the entire stack (aarch64).  After rewriting, switches to
 * the re-written stack to check for correctness.
 */
#define TIME_AND_TEST_REWRITE( aarch64_bin, func ) \
  ({ \
    int ret; \
    struct timespec start = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec init = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec rewrite = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec end = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct regset_aarch64 regset, regset_dest; \
    stack_bounds bounds = get_stack_bounds(); \
    READ_REGS_AARCH64(regset); \
    regset.pc = get_call_site(); \
    clock_gettime(CLOCK_MONOTONIC, &start); \
    st_handle src = st_init(aarch64_bin); \
    clock_gettime(CLOCK_MONOTONIC, &init); \
    if(src) \
    { \
      ret = st_rewrite_stack(src, &regset, bounds.high, src, &regset_dest, bounds.low); \
      clock_gettime(CLOCK_MONOTONIC, &rewrite); \
      st_destroy(src); \
      if(ret) \
        lio_fprintf(lio_stderr, "Couldn't re-write the stack\n"); \
      else \
      { \
        clock_gettime(CLOCK_MONOTONIC, &end); \
        lio_printf("[ST] Setup time: %u\n",				\
		   (init.tv_sec * 1000000000 + init.tv_nsec) -		\
		   (start.tv_sec * 1000000000 + start.tv_nsec));	\
        lio_printf("[ST] Transform time: %u\n",			\
		   (rewrite.tv_sec * 1000000000 + rewrite.tv_nsec) -	\
		   (init.tv_sec * 1000000000 + init.tv_nsec));		\
        lio_printf("[ST] Cleanup time: %u\n",				\
		   (end.tv_sec * 1000000000 + end.tv_nsec) -		\
		   (rewrite.tv_sec * 1000000000 + rewrite.tv_nsec));	\
        lio_printf("[ST] Total elapsed time: %u\n",			\
		   (end.tv_sec * 1000000000 + end.tv_nsec) -		\
		   (start.tv_sec * 1000000000 + start.tv_nsec));	\
        post_transform = 1; \
        SET_REGS_AARCH64(regset_dest); \
        SET_FRAME_AARCH64(regset_dest.x[29], regset_dest.sp); \
        SET_PC_IMM(func); \
      } \
    } \
    else lio_fprintf(lio_stderr, "Couldn't open ELF information\n"); \
  })

/*
 * Time & test the re-write with a previously initialized handle.  Useful for
 * testing multi-threaded applications which all use the same handle.
 */
#define TIME_AND_TEST_NO_INIT( aarch64_handle, func ) \
  ({ \
    int ret; \
    struct timespec start = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec end = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct regset_aarch64 regset, regset_dest; \
    stack_bounds bounds = get_stack_bounds(); \
    READ_REGS_AARCH64(regset); \
    regset.pc = get_call_site(); \
    if(aarch64_handle) \
    { \
      clock_gettime(CLOCK_MONOTONIC, &start); \
      ret = st_rewrite_stack(aarch64_handle, &regset, bounds.high, \
                             aarch64_handle, &regset_dest, bounds.low); \
      if(ret) lio_fprintf(lio_stderr, "Couldn't re-write the stack\n"); \
      else \
      { \
        clock_gettime(CLOCK_MONOTONIC, &end); \
        lio_printf("[ST] Transform time: %u\n",				\
		   (end.tv_sec * 1000000000 + end.tv_nsec) -		\
		   (start.tv_sec * 1000000000 + start.tv_nsec));	\
        post_transform = 1; \
        SET_REGS_AARCH64(regset_dest); \
        SET_FRAME_AARCH64(regset_dest.x[29], regset_dest.sp); \
        SET_PC_IMM(func); \
      } \
    } \
    else lio_fprintf(lio_stderr, "Invalid stack transformation handle\n"); \
  })

#elif defined(__powerpc64__)

/*
 * Times rewriting the entire stack (powerpc64).  Returns elapsed time in
 * nanoseconds.
 */
#define TIME_REWRITE( powerpc64_bin, x86_64_bin ) \
  ({ \
    int ret; \
    struct timespec start = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec init = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec rewrite = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec end = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct regset_powerpc64 regset; \
    struct regset_x86_64 regset_dest; \
    stack_bounds bounds = get_stack_bounds(); \
    READ_REGS_POWERPC64(regset); \
    regset.pc = get_call_site(); \
    clock_gettime(CLOCK_MONOTONIC, &start); \
    st_handle src = st_init(powerpc64_bin); \
    st_handle dest = st_init(x86_64_bin); \
    clock_gettime(CLOCK_MONOTONIC, &init); \
    if(src && dest) \
    { \
      ret = st_rewrite_stack(src, &regset, bounds.high, dest, &regset_dest, bounds.low); \
      clock_gettime(CLOCK_MONOTONIC, &rewrite); \
      st_destroy(src); \
      st_destroy(dest); \
      if(ret) \
        lio_fprintf(lio_stderr, "Couldn't re-write the stack\n"); \
      else \
      { \
        clock_gettime(CLOCK_MONOTONIC, &end); \
        lio_printf("[ST] Setup time: %u\n",				\
		   (init.tv_sec * 1000000000 + init.tv_nsec) -		\
		   (start.tv_sec * 1000000000 + start.tv_nsec));	\
        lio_printf("[ST] Transform time: %u\n",				\
		   (rewrite.tv_sec * 1000000000 + rewrite.tv_nsec) -	\
		   (init.tv_sec * 1000000000 + init.tv_nsec));		\
        lio_printf("[ST] Cleanup time: %u\n",				\
		   (end.tv_sec * 1000000000 + end.tv_nsec) -		\
		   (rewrite.tv_sec * 1000000000 + rewrite.tv_nsec));	\
        lio_printf("[ST] Total elapsed time: %u\n",			\
		   (end.tv_sec * 1000000000 + end.tv_nsec) -		\
		   (start.tv_sec * 1000000000 + start.tv_nsec));	\
      } \
    } \
    else \
    { \
      lio_fprintf(lio_stderr, "Couldn't open ELF information\n"); \
      if(src) st_destroy(src); \
      if(dest) st_destroy(dest); \
    } \
  })

/*
 * Times rewriting the entire stack (powerpc64).  Returns elapsed time in
 * nanoseconds.  After rewriting, switches to the re-written stack to check
 * for correctness.
 */
#define TIME_AND_TEST_REWRITE( powerpc64_bin, func ) \
  ({ \
    int ret; \
    struct timespec start = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec init = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec rewrite = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec end = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct regset_powerpc64 regset, regset_dest; \
    stack_bounds bounds = get_stack_bounds(); \
    READ_REGS_POWERPC64(regset); \
    regset.pc = get_call_site(); \
    clock_gettime(CLOCK_MONOTONIC, &start); \
    st_handle src = st_init(powerpc64_bin); \
    clock_gettime(CLOCK_MONOTONIC, &init); \
    if(src) \
    { \
      ret = st_rewrite_stack(src, &regset, bounds.high, src, &regset_dest, bounds.low); \
      clock_gettime(CLOCK_MONOTONIC, &rewrite); \
      st_destroy(src); \
      if(ret) \
        lio_fprintf(lio_stderr, "Couldn't re-write the stack\n"); \
      else \
      { \
        clock_gettime(CLOCK_MONOTONIC, &end); \
        lio_printf("[ST] Setup time: %u\n",				\
		   (init.tv_sec * 1000000000 + init.tv_nsec) -		\
		   (start.tv_sec * 1000000000 + start.tv_nsec));	\
        lio_printf("[ST] Transform time: %u\n",				\
		   (rewrite.tv_sec * 1000000000 + rewrite.tv_nsec) -	\
		   (init.tv_sec * 1000000000 + init.tv_nsec));		\
        lio_printf("[ST] Cleanup time: %u\n",				\
		   (end.tv_sec * 1000000000 + end.tv_nsec) -		\
		   (rewrite.tv_sec * 1000000000 + rewrite.tv_nsec));	\
        lio_printf("[ST] Total elapsed time: %u\n",			\
		   (end.tv_sec * 1000000000 + end.tv_nsec) -		\
		   (start.tv_sec * 1000000000 + start.tv_nsec));	\
        post_transform = 1; \
        SET_REGS_POWERPC64(regset_dest); \
        SET_FRAME_POWERPC64(regset_dest.r[31], regset_dest.r[1]); \
        SET_PC_IMM(func); \
      } \
    } \
    else \
      lio_fprintf(lio_stderr, "Couldn't open ELF information\n"); \
  })

/*
 * Time & test the re-write with a previously initialized handle.  Good for
 * testing multi-threaded applications which all use the same handle.
 */
#define TIME_AND_TEST_NO_INIT( powerpc64_handle, func ) \
  ({ \
    int ret; \
    struct timespec start = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec end = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct regset_powerpc64 regset, regset_dest; \
    stack_bounds bounds = get_stack_bounds(); \
    READ_REGS_POWERPC64(regset); \
    regset.pc = get_call_site(); \
    if(powerpc64_handle) \
    { \
      clock_gettime(CLOCK_MONOTONIC, &start); \
      ret = st_rewrite_stack(powerpc64_handle, &regset, bounds.high, \
                             powerpc64_handle, &regset_dest, bounds.low); \
      if(ret) lio_fprintf(lio_stderr, "Couldn't re-write the stack\n"); \
      else \
      { \
        clock_gettime(CLOCK_MONOTONIC, &end); \
        lio_printf("[ST] Transform time: %u\n",				\
		   (end.tv_sec * 1000000000 + end.tv_nsec) -		\
		   (start.tv_sec * 1000000000 + start.tv_nsec));	\
        post_transform = 1; \
        SET_REGS_POWERPC64(regset_dest); \
        SET_FRAME_POWERPC64(regset_dest.r[31], regset_dest.r[1]); \
        SET_PC_IMM(func); \
      } \
    } \
    else \
      lio_fprintf(lio_stderr, "Invalid stack transformation handle\n"); \
  })

#elif defined __x86_64__

/* Times rewriting the entire stack (x86-64) */
#define TIME_REWRITE( aarch64_bin, x86_64_bin ) \
  ({ \
    int ret; \
    struct timespec start = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec init = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec rewrite = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec end = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct regset_x86_64 regset; \
    struct regset_powerpc64 regset_dest; \
    stack_bounds bounds = get_stack_bounds(); \
    READ_REGS_X86_64(regset); \
    regset.rip = get_call_site(); \
    clock_gettime(CLOCK_MONOTONIC, &start); \
    st_handle src = st_init(x86_64_bin); \
    st_handle dest = st_init(powerpc64_bin); \
    clock_gettime(CLOCK_MONOTONIC, &init); \
    if(src && dest) \
    { \
      ret = st_rewrite_stack(src, &regset, bounds.high, dest, &regset_dest, bounds.low); \
      clock_gettime(CLOCK_MONOTONIC, &rewrite); \
      st_destroy(src); \
      st_destroy(dest); \
      if(ret) \
        lio_fprintf(lio_stderr, "Couldn't re-write the stack\n"); \
      else \
      { \
        clock_gettime(CLOCK_MONOTONIC, &end); \
        lio_printf("[ST] Setup time: %u\n",				\
		   (init.tv_sec * 1000000000 + init.tv_nsec) -		\
		   (start.tv_sec * 1000000000 + start.tv_nsec));	\
        lio_printf("[ST] Transform time: %u\n",				\
		   (rewrite.tv_sec * 1000000000 + rewrite.tv_nsec) -	\
		   (init.tv_sec * 1000000000 + init.tv_nsec));		\
        lio_printf("[ST] Cleanup time: %u\n",				\
		   (end.tv_sec * 1000000000 + end.tv_nsec) -		\
		   (rewrite.tv_sec * 1000000000 + rewrite.tv_nsec));	\
        lio_printf("[ST] Total elapsed time: %u\n",			\
		   (end.tv_sec * 1000000000 + end.tv_nsec) -		\
		   (start.tv_sec * 1000000000 + start.tv_nsec));	\
      } \
    } \
    else \
    { \
      lio_fprintf(lio_stderr, "Couldn't open ELF information\n"); \
      if(src) st_destroy(src); \
      if(dest) st_destroy(dest); \
    } \
  })

/*
 * Times rewriting the entire stack (x86-64).  Returns elapsed time in
 * nanoseconds.  After rewriting, switches to the re-written stack to check
 * for correctness.
 */
#define TIME_AND_TEST_REWRITE( x86_64_bin, func ) \
  ({ \
    int ret; \
    struct timespec start = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec init = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec rewrite = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec end = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct regset_x86_64 regset, regset_dest; \
    stack_bounds bounds = get_stack_bounds(); \
    READ_REGS_X86_64(regset); \
    regset.rip = get_call_site(); \
    clock_gettime(CLOCK_MONOTONIC, &start); \
    st_handle src = st_init(x86_64_bin); \
    clock_gettime(CLOCK_MONOTONIC, &init); \
    if(src) \
    { \
      ret = st_rewrite_stack(src, &regset, bounds.high, src, &regset_dest, bounds.low); \
      clock_gettime(CLOCK_MONOTONIC, &rewrite); \
      st_destroy(src); \
      if(ret) \
        lio_fprintf(lio_stderr, "Couldn't re-write the stack\n"); \
      else \
      { \
        clock_gettime(CLOCK_MONOTONIC, &end); \
        lio_printf("[ST] Setup time: %u\n",				\
		   (init.tv_sec * 1000000000 + init.tv_nsec) -		\
		   (start.tv_sec * 1000000000 + start.tv_nsec));	\
        lio_printf("[ST] Transform time: %u\n",				\
		   (rewrite.tv_sec * 1000000000 + rewrite.tv_nsec) -	\
		   (init.tv_sec * 1000000000 + init.tv_nsec));		\
        lio_printf("[ST] Cleanup time: %u\n",				\
		   (end.tv_sec * 1000000000 + end.tv_nsec) -		\
		   (rewrite.tv_sec * 1000000000 + rewrite.tv_nsec));	\
        lio_printf("[ST] Total elapsed time: %u\n",			\
		   (end.tv_sec * 1000000000 + end.tv_nsec) -		\
		   (start.tv_sec * 1000000000 + start.tv_nsec));	\
        post_transform = 1; \
        SET_REGS_X86_64(regset_dest); \
        SET_FRAME_X86_64(regset_dest.rbp, regset_dest.rsp); \
        SET_RIP_IMM(func); \
      } \
    } \
    else lio_fprintf(lio_stderr, "Couldn't open ELF information\n"); \
  })

/*
 * Time & test the re-write with a previously initialized handle.  Good for
 * testing multi-threaded applications which all use the same handle.
 */
#define TIME_AND_TEST_NO_INIT( x86_64_handle, func ) \
  ({ \
    int ret; \
    struct timespec start = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct timespec end = { .tv_sec = 0, .tv_nsec = 0 }; \
    struct regset_x86_64 regset, regset_dest; \
    stack_bounds bounds = get_stack_bounds(); \
    READ_REGS_X86_64(regset); \
    regset.rip = get_call_site(); \
    if(x86_64_handle) \
    { \
      clock_gettime(CLOCK_MONOTONIC, &start); \
      ret = st_rewrite_stack(x86_64_handle, &regset, bounds.high, \
                             x86_64_handle, &regset_dest, bounds.low); \
      if(ret) lio_fprintf(lio_stderr, "Couldn't re-write the stack\n"); \
      else \
      { \
        clock_gettime(CLOCK_MONOTONIC, &end); \
        lio_printf("[ST] Transform time: %u\n",				\
		   (end.tv_sec * 1000000000 + end.tv_nsec) -		\
		   (start.tv_sec * 1000000000 + start.tv_nsec));	\
        post_transform = 1; \
        SET_REGS_X86_64(regset_dest); \
        SET_FRAME_X86_64(regset_dest.rbp, regset_dest.rsp); \
        SET_RIP_IMM(func); \
      } \
    } \
    else lio_fprintf(lio_stderr, "Invalid stack transformation handle\n"); \
  })

#else

# error Unsupported architecture!

#endif

