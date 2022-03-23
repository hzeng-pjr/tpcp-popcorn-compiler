#ifndef SYSTEM_H
#define SYSTEM_H

#include <elf.h>
#include <popcorn.h>

#define PAGE_SIZE 0x1000

// Linux kernel fs/binfmt_elf.c
#define ELF_MIN_ALIGN	PAGE_SIZE
#define ELF_PAGESTART(_v) ((_v) & ~(unsigned long)(ELF_MIN_ALIGN-1))
#define ELF_PAGEOFFSET(_v) ((_v) & (ELF_MIN_ALIGN-1))
#define ELF_PAGEALIGN(_v) (((_v) + ELF_MIN_ALIGN - 1) & ~(ELF_MIN_ALIGN - 1))

#define LD_LINUX "ld-2.31.so"

extern int unload_ldso (struct mmap_entries *me);
extern void unload_libs ();
extern void *load_lib (char *lib);
extern unsigned long get_base_address (Elf64_Phdr *phdrs, int phnum,
				       unsigned long entry);
extern void restore_rw_segments (Elf64_Phdr *phdrs, int phnum,
				 unsigned long entry);
extern void reset_dynamic (Elf64_Phdr *phdrs, int phnum, unsigned long entry,
			   char *exec, Elf64_Ehdr *ehdr, int fd);
extern void print_all_dso ();
extern int main_function (int argc, char *argv[]);

/* FIXME: Provided by glibc.  */
extern void _dl_rio_print_dso ();
extern void _dl_rio_populate_dso_entries ();

#endif
