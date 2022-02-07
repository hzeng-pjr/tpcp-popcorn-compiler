#ifndef SYSCALL_ARCH_H
#define SYSCALL_ARCH_H

#if defined (__aarch64__)
#include <arch/aarch64/syscall_arch.h>
#elif defined (__x86_64__)
#include <arch/x86_64/syscall_arch.h>
#else
#error "Unsupported arch"
#endif

#endif
