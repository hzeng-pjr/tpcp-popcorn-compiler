#ifndef SYSTEM_H
#define SYSTEM_H

#include <sys/mman.h>
#include <elf.h>
#include <popcorn.h>

#define PAGE_SIZE 0x1000
#define MAX_INTERP 0x100

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
extern void get_pt_exec (int fd, Elf64_Phdr *phdrs, int phnum, void *interp);
extern void print_all_dso ();
extern int main_function (int argc, char *argv[]);
extern void reload_dynamic (Elf64_Phdr *phdrs, int phnum, int fd);

/* FIXME: Provided by glibc.  */
extern void _dl_rio_print_dso ();
extern void _dl_rio_populate_dso_entries ();

#define PCN_PT_INTERP_P (PROT_READ)
#define PCN_PT_INIT_P (PROT_READ | PROT_EXEC)
#define PCN_PT_PLT_P (PROT_READ | PROT_EXEC)
#define PCN_PT_FINI_P (PROT_READ | PROT_EXEC)
#define PCN_PT_RODATA_P (PROT_READ)
#define PCN_PT_DYNAMIC_P (PROT_READ | PROT_WRITE)
#endif
