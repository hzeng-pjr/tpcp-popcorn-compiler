#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <local_io.h>
#include <popcorn.h>
#include "system.h"

/* FIXME: This is defined in glibc, or it should be.  */
# define MAP_FIXED_NOREPLACE 0x100000     /* MAP_FIXED but do not unmap 
					     underlying mapping.  */

int
unload_ldso (struct mmap_entries *me)
{
  Elf64_Ehdr ehdr;
  Elf64_Phdr *phdrs;
  int fd, ret, size, i;
  size_t addr_min = SIZE_MAX, addr_max = 0;
  unsigned long map, base;

  fd = lio_open (me->name, O_RDONLY, 0);

  if (fd < 0)
    lio_error ("open failed\n");

  ret = lio_read (fd, &ehdr, sizeof (ehdr));

  if (ret < 0)
    lio_error ("failed to read ELF header\n");

  size = sizeof (Elf64_Phdr) * ehdr.e_phnum;

  phdrs = __builtin_alloca (size);

  ret = lio_pread (fd, phdrs, size, ehdr.e_phoff);

  if (ret < 0)
    lio_error ("failed to read ELF segment headers\n");

  /* Populate addr_{min,max}.  */
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      if (phdrs[i].p_type != PT_LOAD)
	continue;

      if (phdrs[i].p_vaddr < addr_min)
	addr_min = phdrs[i].p_vaddr;
    }

  map = me->start;
  base = map - addr_min;

  for (i = 0; i < ehdr.e_phnum; i++)
    {
      unsigned long this_min, this_max;
      long ret;

      if (phdrs[i].p_type != PT_LOAD)
	continue;

      this_min = phdrs[i].p_vaddr & -PAGE_SIZE;
      this_max = phdrs[i].p_vaddr + phdrs[i].p_memsz + PAGE_SIZE-1 & -PAGE_SIZE;

      ret = lio_munmap ((void *)(base + this_min), this_max - this_min);

      if (ret)
	lio_print ("ld.so munmap failed\n");
    }

  lio_close (fd);

  return 0;
}

void
unload_libs ()
{
  struct mmap_entries *me;
  int i, ret, len[3];
  int ps = sysconf(_SC_PAGE_SIZE);
  struct dl_pcn_data *pcn_data = (void *) DL_PCN_STATE;
  int ld_linux = -1;

  me = pcn_data->maps;

  /* Scan the library names for LD_LINUX, as strstr and other libc
     functions will not be available after the shared libraries have
     been unloaded.  */
  for (i = 0; i < pcn_data->num_maps; i++)
    {
      if (strstr (pcn_data->maps[i].name, LD_LINUX))
	{
	  ld_linux = i;
	  break;
	}
    }

  for (i = 0; i < pcn_data->num_maps; i++)
    {
      if (i == ld_linux)
	ret = unload_ldso (&pcn_data->maps[i]);
      else
	ret = lio_munmap ((void *)me[i].start, me[i].size);
    }
}

/* FIXME: Need to add the capability to automatically load in the
   proper shared libraries for different ISAs.  E.g., when the
   processor architecture is encoded in the file path.  */

/* May need to handle dlopened libraries differently.  Probably don't
   need to load libc or the other libraries listed as dependencies to
   the original executable.  */

/* This function is inspired after Musl's dynamic linker.  It returns
   the load address of teh entry point for the library, if any.  */
void *
load_lib (char *lib)
{
  Elf64_Ehdr ehdr;
  Elf64_Phdr *phdrs;
  int fd, ret, size, i, old_map = -1;
  int elf_prot, elf_flags;
  unsigned long load_addr = 0;
  size_t total_size, off_start;
  size_t addr_min = SIZE_MAX, addr_max = 0, nsegs = 0;
  size_t dyn = 0;
  unsigned long map, base;
  struct dl_pcn_data *pcn_data = (void *) DL_PCN_STATE;

  //lio_printf ("loading %s...", lib);

  for (i = 0; i < pcn_data->num_maps; i++)
    if (lio_strcmp (lib, pcn_data->maps[i].name) == 0)
      {
	old_map = i;
	break;
      }

  if (old_map == -1)
    lio_error ("failed to detect previous mapping\n");

  fd = lio_open (lib, O_RDONLY, 0);

  if (fd < 0)
    lio_error ("open failed\n");

  ret = lio_read (fd, &ehdr, sizeof (ehdr));

  if (ret < 0)
    lio_error ("failed to read ELF header\n");

  size = sizeof (Elf64_Phdr) * ehdr.e_phnum;

  phdrs = __builtin_alloca (size);

  ret = lio_pread (fd, phdrs, size, ehdr.e_phoff);

  if (ret < 0)
    lio_error ("failed to read ELF segment headers\n");

  /* Populate addr_{min,max}.  */
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      if (phdrs[i].p_type == PT_DYNAMIC)
	dyn = phdrs[i].p_vaddr;
      if (phdrs[i].p_type != PT_LOAD)
	continue;

      nsegs++;

      if (phdrs[i].p_vaddr < addr_min)
	{
	  addr_min = phdrs[i].p_vaddr;
	  off_start = phdrs[i].p_offset;

	  elf_prot = 0;

	  if (phdrs[i].p_flags & PF_R)
	    elf_prot |= PROT_READ;
	  if (phdrs[i].p_flags & PF_W)
	    elf_prot |= PROT_WRITE;
	  if (phdrs[i].p_flags & PF_X)
	    elf_prot |= PROT_EXEC;
	}

      if (phdrs[i].p_vaddr + phdrs[i].p_memsz > addr_max)
	addr_max = phdrs[i].p_vaddr + phdrs[i].p_memsz;
    }

//  addr_max += PAGE_SIZE - 1;
//  addr_max &= -PAGE_SIZE;
//  addr_min &= -PAGE_SIZE;
//  off_start &= -PAGE_SIZE;
//  total_size = addr_max - addr_min + off_start;

//  map = (unsigned long) lio_mmap ((void *)pcn_data->maps[old_map].start,
//				 pcn_data->maps[old_map].size, elf_prot,
//				 MAP_PRIVATE | MAP_FIXED, fd, off_start);
//
//  if ((long) map < 0)
//    lio_error ("mmap failed\n");

  map = pcn_data->maps[old_map].start;
  base = map - addr_min;

  for (i = 0; i < ehdr.e_phnum; i++)
    {
      unsigned long this_min, this_max;
      long ret;

      elf_prot = 0;

      if (phdrs[i].p_type != PT_LOAD)
	continue;

      this_min = phdrs[i].p_vaddr & -PAGE_SIZE;
      this_max = phdrs[i].p_vaddr + phdrs[i].p_memsz + PAGE_SIZE-1 & -PAGE_SIZE;
      off_start = phdrs[i].p_offset & -PAGE_SIZE;

      if (phdrs[i].p_flags & PF_R)
	elf_prot |= PROT_READ;
      if (phdrs[i].p_flags & PF_W)
	elf_prot |= PROT_WRITE;
      if (phdrs[i].p_flags & PF_X)
	elf_prot |= PROT_EXEC;

      /* Skip the first segment as it has already been mapped.  */
//      if ((phdrs[i].p_vaddr & -PAGE_SIZE) == addr_min)
//	{
//	  unsigned long size = this_max - this_min;
//	  lio_munmap ((void *) (map + size), total_size - size);
//	  continue;
//	}

      ret = (long) lio_mmap ((void *)(base + this_min), this_max - this_min,
			    elf_prot, MAP_PRIVATE | MAP_FIXED, fd, off_start);
      if (ret < 0)
	lio_error ("load segment: mmap failed\n");

      if (phdrs[i].p_memsz > phdrs[i].p_filesz)
	{
	  size_t brk = (size_t) base + phdrs[i].p_vaddr + phdrs[i].p_filesz;
	  size_t pgbrk = brk + PAGE_SIZE - 1 & - PAGE_SIZE;
	  lio_memset ((void *)brk, 0, pgbrk - brk & PAGE_SIZE - 1);
	  if (pgbrk - (size_t) base < this_max)
	    {
	      ret = (long) lio_mmap ((void *)pgbrk,
				    (size_t)base + this_max - pgbrk,
				    elf_prot,
				    MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS,
				    -1, 0);
	      if (ret < 0)
		lio_error ("failed to set up bss\n");
	    }
	}
    }

  lio_close (fd);

  //lio_print ("success!\n");

  return (void *) (base + ehdr.e_entry);
}

unsigned long
get_base_address (Elf64_Phdr *phdrs, int phnum, unsigned long entry)
{
  unsigned long base, padding = 0;
  int i;

  /* The base address of an executable may change if it was compiled
     as an executable shared library (PIE).  Need a method to extract
     the base address for the segments.  */

  base = entry & -PAGE_SIZE;

  /* Determine the base address for the executable of this running
     process. The formula is AT_ENTRY - (offset into the executable
     segment).  */
  for (i = 0; i < phnum; i++)
    {
      unsigned long elf_min, elf_max;

      elf_min = phdrs[i].p_vaddr;
      elf_max = elf_min + phdrs[i].p_memsz;

      elf_max += PAGE_SIZE - 1;
      elf_max &= -PAGE_SIZE;
      elf_min &= -PAGE_SIZE;

      if (phdrs[i].p_type != PT_LOAD)
	continue;

      if (phdrs[i].p_flags & PF_X)
	break;

      padding += elf_max - elf_min;
    }

  base -= padding;

  return base;
}

void
restore_rw_segments (Elf64_Phdr *phdrs, int phnum, unsigned long entry)
{
  unsigned long base, l1, l2 = 0;
  int i;

  base = get_base_address (phdrs, phnum, entry);

  //lio_printf ("%d phdrs detected; entry = %lx, base = %lx\n", phnum, entry, base);

  for (i = 0; i < phnum; i++)
    {
      void *addr;
      size_t len;
      int elf_prot = 0;
      unsigned long elf_min, elf_max;

      if (phdrs[i].p_type != PT_LOAD)
	continue;

      if (phdrs[i].p_flags & PF_R)
	elf_prot |= PROT_READ;
      if (phdrs[i].p_flags & PF_W)
	elf_prot |= PROT_WRITE;
      if (phdrs[i].p_flags & PF_X)
	elf_prot |= PROT_EXEC;

      elf_min = phdrs[i].p_vaddr;
      elf_max = elf_min + phdrs[i].p_memsz;

      elf_max += PAGE_SIZE - 1;
      elf_max &= -PAGE_SIZE;
      elf_min &= -PAGE_SIZE;

      if (phdrs[i].p_flags & PF_W)
	{
	  if (phdrs[i].p_vaddr < base)
	    lio_mprotect ((void *)(base + elf_min), elf_max - elf_min,
			 elf_prot);
	  else
	    lio_mprotect ((void *) elf_min, elf_max - elf_min, elf_prot);
	}
    }
}

void
reset_dynamic (Elf64_Phdr *phdrs, int phnum, unsigned long entry, char *exec,
	       Elf64_Ehdr *ehdr, int fd)
{
  Elf64_Shdr *shdrs;
  int ret, size;
  unsigned long base = 0;
  unsigned long l1, l2 = 0;
  int i;
  char *strtab;

  size = sizeof (Elf64_Shdr) * ehdr->e_shnum;

  if (ehdr->e_type == ET_DYN)
    base = get_base_address (phdrs, phnum, entry);

  shdrs = __builtin_alloca (size);

  ret = lio_pread (fd, shdrs, size, ehdr->e_shoff);

  if (ret < 0)
    lio_error ("failed to read ELF section headers\n");

  strtab = __builtin_alloca (shdrs[ehdr->e_shstrndx].sh_size);

  ret = lio_pread (fd, strtab, shdrs[ehdr->e_shstrndx].sh_size,
		  shdrs[ehdr->e_shstrndx].sh_offset);

  for (i = 0; i < ehdr->e_shnum; i++)
    {
      char *id = &strtab[shdrs[i].sh_name];
      void *mem;

      if (lio_strcmp (id, ".got") == 0 || lio_strcmp (id, ".got.plt") == 0)
	{
	  mem = (void *) (base + shdrs[i].sh_addr);
	  lio_pread (fd, mem, shdrs[i].sh_size, shdrs[i].sh_offset);
	}
    }
}

void
get_pt_exec (int fd, Elf64_Phdr *phdrs, int phnum, void *interp)
{
  int i;

  for (i = 0; i < phnum; i++)
    {
      if (phdrs[i].p_type != PT_INTERP)
	continue;

      lio_pread (fd, interp, phdrs[i].p_filesz, phdrs[i].p_offset);

      return;
    }
}

void
print_all_dso ()
{
  struct dl_pcn_data *pcn_data = (void *) DL_PCN_STATE;
  int i;
  unsigned long size;

  for (i = 0; i < pcn_data->num_maps; i++)
    {
      size = pcn_data->maps[i].size + PAGE_SIZE - 1;
      size &= -PAGE_SIZE;

      lio_printf ("%s: %lx - %lx (%lu / %lx)\n", pcn_data->maps[i].name,
	      pcn_data->maps[i].start,
	      pcn_data->maps[i].start + pcn_data->maps[i].size, size,
	      pcn_data->maps[i].start + size);
    }
}

int
main_function (int argc, char *argv[])
{
  Elf64_Phdr *phdrs;
  int phnum;
  unsigned long entry;
  int i;
  struct dl_pcn_data *pcn_data;
  void *t, *ld_start;

  phnum = getauxval (AT_PHNUM);
  phdrs = (void *)getauxval (AT_PHDR);
  entry = getauxval (AT_ENTRY);

  lio_printf ("pid = %d\n", getpid ());

  pcn_data = lio_mmap ((void *)DL_PCN_STATE, PAGE_SIZE, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_FIXED_NOREPLACE | MAP_ANONYMOUS, 0, 0);

  if (pcn_data == (void *)-1 || pcn_data == (void *) -EEXIST)
    pcn_data = (void *) DL_PCN_STATE;

  lio_printf ("arg = %p\n", pcn_data->arg);

  _dl_rio_populate_dso_entries ();

  print_all_dso ();
  //lio_spin ();

  //pcn_break ();
  unload_libs ();
  //pcn_break ();

  restore_rw_segments (phdrs, phnum, entry);
  reset_dynamic (phdrs, phnum, entry, argv[0], NULL, 0);
  //lio_exit (EXIT_SUCCESS);

  //lio_spin ();

  pcn_data->pcn_entry = (unsigned long) &&pcn_cont;
  pcn_data->pcn_break = 1;

  ld_start = load_lib (pcn_data->maps[0].name); // Load ld-linux

  //pcn_break ();

#if defined (__x86_64__)
  asm volatile ("jmp *%0;\n\t" : : "r" (ld_start));
#elif defined (__aarch64__)
  asm volatile ("br %0;\n\t" : : "r" (ld_start));
#else
#lio_error "Unsupported arch"
#endif

  lio_spin ();

 pcn_cont:
  //pcn_break ();
  lio_print ("reloading libc complete!\n");

  lio_printf ("terminating\n");
  //spin ();
  //pcn_break ();

  return 0;
}

void
reload_segment (Elf64_Phdr *phdr, int seg, int prot, int fd, char *name)
{
  void *paddr = (void *)phdr[seg].p_paddr;
  int filesz = phdr[seg].p_filesz;
  int offset = phdr[seg].p_offset;
  
  //lio_printf ("loading %s %lx @ %u bytes\n", name, paddr, filesz);

  lio_mprotect (paddr, filesz, (PROT_READ | PROT_WRITE));
  lio_pread (fd, paddr, filesz, offset);
  lio_mprotect (paddr, filesz, prot);  
}

void
reload_dynamic (Elf64_Phdr *phdrs, int phnum, int fd)
{
  int i;

  for (i = 0; i < phnum; i++)
    if (phdrs[i].p_type == PT_DYNAMIC)
      reload_segment (phdrs, i-1, PCN_PT_DYNAMIC_P, fd, ".dynamic");
}
