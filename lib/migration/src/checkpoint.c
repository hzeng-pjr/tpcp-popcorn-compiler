#if _GBL_VARIABLE_MIGRATE == 1
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stack_transform.h>
#include <arpa/inet.h>
#include <remote_io.h>
#include <local_io.h>
#include <popcorn.h>
#include "platform.h"
#include "migrate.h"
#include "config.h"
#include "arch.h"
#include "internal.h"
#include "mapping.h"
#include "debug.h"
#include "system.h"

static struct dl_pcn_data *pd = (void *) DL_PCN_STATE;

int a = 100;
int __thread b;

/************************************************/
volatile long __migrate_gb_variable = -1;
static inline int do_migrate(void __attribute__ ((unused)) * fn)
{
	return __migrate_gb_variable;
}

static inline void clear_migrate_flag()
{
	__migrate_gb_variable = -1;
	asm volatile("": : :"memory");
}

static volatile long __restore_context=0;
static inline int get_restore_context()
{
	return __restore_context;
}

static inline void set_restore_context(int val)
{
	__restore_context=val;
}

/* Used by criu-het to restore the TLS.  */
void* tls_dst=0x0;

//TODO: per thread
union {
	struct regset_aarch64 aarch;
	struct regset_powerpc64 powerpc;
	struct regset_x86_64 x86;
} regs_dst;

/* Generate a call site to get rewriting metadata for outermost frame. */
static void* __attribute__((noinline))
get_call_site() { return __builtin_return_address(0); };

static void dummy(){lio_dbg_printf("%s: called\n", __func__);};

void
__migrate_shim_internal(enum arch dst_arch, void (*callback) (void *), void *callback_data)
{
	Elf64_Ehdr ehdr;
	Elf64_Phdr *phdrs;
	unsigned long entry;
	int i, fd, pt_dyn;
	void *t, *ld_start;
	int err, ret;
	char buf[INET_ADDRSTRLEN];

	if (!get_restore_context())		// Invoke migration
	{
		unsigned long sp = 0, bp = 0;
		union {
			struct regset_aarch64 aarch;
			struct regset_powerpc64 powerpc;
			struct regset_x86_64 x86;
		} regs_src;
		sigset_t old_sig_set;
		sigset_t new_sig_set;
		struct ksigaction kact, koact;

		b = 555;
		tls_dst = pd->thread_pointer;

		/* Deactivate the remote_io server while unloading libc.  */
		pd->pcn_remote_io_active = 0;

		lio_dbg_printf ("pcn_server_port = %u\n", pd->pcn_server_port);
		lio_dbg_printf ("pid = %u\n", lio_getpid ());
		lio_dbg_printf ("thread_pointer = %lx\n", tls_dst);

		/* Unload any signal handlers.  */
		pcn_unload_signals ();

		//test_signals ();
		//test_lio_signals ();

		/* Inform the I/O server of the impending migration.  */
		pcn_migrate ();
		close (pd->pcn_server_sockfd);

		_dl_rio_populate_dso_entries ();
		print_all_dso ();
		unload_libs ();
		lio_dbg_printf ("unload complete\n");

		GET_LOCAL_REGSET(regs_src);

		lio_dbg_printf ("GET_LOCAL_REGSET complete\n");

		err = 0;
		switch (dst_arch) {
			case ARCH_AARCH64:
				err = !REWRITE_STACK(regs_src, regs_dst,
						     dst_arch);
				regs_dst.aarch.__magic = 0xAABBDEADBEAF;
				lio_dbg_printf ("rewrote stack\n");
				dump_regs_aarch64(&regs_dst.aarch, LOG_FILE);
				break;
			case ARCH_X86_64:
				err = !REWRITE_STACK(regs_src, regs_dst,
						     dst_arch);
				regs_dst.x86.__magic = 0xA8664DEADBEAF;
				lio_dbg_printf ("rewrote stack\n");
				dump_regs_x86_64(&regs_dst.x86, LOG_FILE);
				break;
			case ARCH_POWERPC64:
				err = !REWRITE_STACK(regs_src, regs_dst,
						     dst_arch);
				dump_regs_powerpc64(&regs_dst.powerpc,
						    LOG_FILE);
				break;
			default: lio_error ("Unsupported architecture!");
		}
		if (err) {
			lio_dbg_printf("Could not rewrite stack!\n");
			return;
		}
		lio_dbg_printf("dest arch is %u\n", dst_arch);

		lio_dbg_printf("%s %u\n", __func__, __LINE__);
		set_restore_context(1);
		lio_dbg_printf("%s %u\n", __func__, __LINE__);
		clear_migrate_flag();

		//signal(SIGALRM, dummy);

		kact.handler = dummy;
		lio_memset (&kact.mask, 0, sizeof (sigset_t));
		kact.flags = 0;
		kact.restorer = NULL;
		lio_rt_sigaction (SIGALRM, &kact, &koact, _NSIG / 8);

		//sigemptyset(&new_sig_set);
		lio_memset (&new_sig_set, 0, sizeof (new_sig_set));

		//sigaddset(&new_sig_set, SIGALRM);
		lio_sigaddset (&new_sig_set, SIGALRM);

		//sigprocmask(SIG_UNBLOCK, &new_sig_set, &old_sig_set);
		lio_sigprocmask (SIG_UNBLOCK, &new_sig_set, &old_sig_set,
				_NSIG / 8);

		//raise(SIGALRM); /* wil be catched by ptrace */
		lio_tgkill (lio_getpid (), lio_gettid (), SIGALRM);

		//sigprocmask(SIG_SETMASK, &old_sig_set, NULL);
		lio_sigprocmask (SIG_UNBLOCK, &old_sig_set, NULL, NSIG / 8);

		lio_dbg_printf("%s raising done %u\n", __func__, __LINE__);
		while(1);
		return;
	}
	// Post-migration

	// Begin by restoring any shared libraries, beginning with
	// ld-linux. Eventually this should check whether the binary
	// is statically linked.

	// Translate between architecture-specific thread descriptors
	// Note: TLS is now invalid until after migration!
	//__set_thread_area(get_thread_pointer(GET_TLS_POINTER, CURRENT_ARCH));
	set_restore_context(0);

	lio_dbg_printf ("pid = %u\n", lio_getpid ());
	lio_dbg_printf ("a = %u\n", a);
	lio_dbg_printf ("b = %u\n", b);
	lio_dbg_printf ("dst_arch = %u\n", dst_arch);

	/* Populate phdrs.  */
	fd = lio_open (pd->filename, O_RDONLY, 0);
	if (fd < 0)
	{
		lio_dbg_printf ("failed to open %s... ", pd->filename);
		lio_error ("terminating");
	}

	ret = lio_read (fd, &ehdr, sizeof (ehdr));
	if (ret < 0)
		lio_error ("failed to read ELF header\n");

//	/* Allocate more storage for the phdrs as necessary.  */
//	if (pd->phdrs == NULL)
//	  {
//	    pd->phdrs = lio_mmap (pd + pd->pcn_data_size,
//					PCN_PAGE_SIZE, PROT_READ | PROT_WRITE,
//					MAP_PRIVATE | MAP_FIXED_NOREPLACE | MAP_ANONYMOUS,
//					0, 0);
//	    pd->pcn_data_size += PCN_PAGE_SIZE;
//	  }

	phdrs = __builtin_alloca (ehdr.e_phnum * sizeof (Elf64_Phdr));
	ret = lio_pread (fd, phdrs, ehdr.e_phnum * sizeof (Elf64_Phdr),
			 ehdr.e_phoff);
	if (ret < 0)
		lio_error ("failed to read ELF phdrs\n");

	/* This won't work with PIE binaries.  */
	pd->phnum = ehdr.e_phnum;
	pd->phdrs = (void *)phdrs[0].p_paddr;

	lio_dbg_printf ("inspecting phdrs\n");

	for (i = 0; i < ehdr.e_phnum; i++)
	  if (phdrs[i].p_paddr != ((Elf64_Phdr *) pd->phdrs)[i].p_paddr)
	    lio_error ("invalid phdr detected\n");
 	
	phdrs = pd->phdrs;

	/* Update the interpreter.  */
	pd->maps[0].name = __builtin_alloca (MAX_INTERP);
	get_pt_exec (fd, phdrs, pd->phnum, pd->maps[0].name);
	lio_dbg_printf ("interpreter = %s\n", pd->maps[0].name);

	/* Reload any ISA-specific segments.  */
	reload_dynamic (phdrs, ehdr.e_phnum, fd);

	entry = ehdr.e_entry;
	restore_rw_segments (pd->phdrs, pd->phnum, entry);
	reset_dynamic (pd->phdrs, pd->phnum, entry,
		       pd->argv[0], &ehdr, fd);

	lio_close (fd);

	pd->pcn_entry = (unsigned long) &&pcn_cont;
	ld_start = load_lib (pd->maps[0].name); // Load ld-linux

	//lio_dbg_printf ("restoring %lu\n", lio_getpid ());

	//lio_spin ();

#if defined (__x86_64__)
  asm volatile ("jmp *%0;\n\t" : : "r" (ld_start));
#elif defined (__aarch64__)
  asm volatile ("br %0;\n\t" : : "r" (ld_start));
#else
#lio_error "Unsupported arch"
#endif

 pcn_cont:
	/* Restore any signal handlers.  */
	pcn_restore_signals ();

	/* Activate the remote_io serverc.  */
	pd->pcn_remote_io_active = PCN_SERVER_READY;

	inet_ntop (AF_INET, &pd->pcn_server_ip, buf, INET_ADDRSTRLEN);
	lio_dbg_printf ("pcn remote server = %s:%u\n", buf, pd->pcn_server_port);
	pd->pcn_server_sockfd = pcn_server_connect (pd->pcn_server_ip);
}

void
__migrate_shim_internal1(enum arch dst_arch, void (*callback) (void *), void *callback_data)
{
	Elf64_Ehdr ehdr;
	Elf64_Phdr *phdrs;
	int phnum;
	unsigned long entry;
	int i, fd;
	struct dl_pcn_data *pcn_data = (void *) DL_PCN_STATE;
	void *t, *ld_start;
	int err, ret;

	sigset_t old_sig_set;
	sigset_t new_sig_set;
	struct ksigaction kact, koact;

	lio_dbg_printf ("entering %s...\n", __FUNCTION__);
	lio_dbg_printf ("pid = %u\n", lio_getpid ());

	_dl_rio_populate_dso_entries ();
	print_all_dso ();
	unload_libs ();
	lio_dbg_printf ("unload complete\n");
	
	// Reload any shared libraries, beginning with ld-linux.
	
	pcn_data->pcn_entry = (unsigned long) &&pcn_cont;

	/* Populate phdrs.  */
	fd = lio_open (pcn_data->argv[0], O_RDONLY, 0);
	if (fd < 0)
		lio_error ("open failed");

	ret = lio_read (fd, &ehdr, sizeof (ehdr));
	if (ret < 0)
		lio_error ("failed to read ELF header\n");

	phnum = ehdr.e_phnum;
	phdrs = __builtin_alloca (phnum * sizeof (Elf64_Phdr));

	ret = lio_read (fd, phdrs, phnum * sizeof (Elf64_Phdr));
	if (ret < 0)
		lio_error ("failed to read ELF phdrs\n");

	/* Update the interpreter.  */
	pcn_data->maps[0].name = __builtin_alloca (MAX_INTERP);
	get_pt_exec (fd, phdrs, phnum, pcn_data->maps[0].name);

	entry = ehdr.e_entry;
	restore_rw_segments (phdrs, phnum, entry);
	reset_dynamic (phdrs, phnum, entry, pcn_data->argv[0], &ehdr, fd);

	lio_close (fd);

	pcn_data->pcn_entry = (unsigned long) &&pcn_cont;
	ld_start = load_lib (pcn_data->maps[0].name); // Load ld-linux

	lio_dbg_printf ("ld_start = %lx", ld_start);

	lio_spin ();

#if defined (__x86_64__)
  asm volatile ("jmp *%0;\n\t" : : "r" (ld_start));
#elif defined (__aarch64__)
  asm volatile ("br %0;\n\t" : : "r" (ld_start));
#else
#lio_error "Unsupported arch"
#endif

pcn_cont:
	printf ("reload complete!\n");
}

extern int __libc_start_main_popcorn (void *, int, void *, void *);

/* Check if we should migrate, and invoke migration. */
void check_migrate(void (*callback) (void *), void *callback_data)
{
	enum arch dst_arch = do_migrate(NULL);
	if (dst_arch >= 0)
		__migrate_shim_internal(dst_arch, callback, callback_data);
	else if (dst_arch == 100)
		__libc_start_main_popcorn (NULL, 0, NULL, NULL);
}

/* Invoke migration to a particular node if we're not already there. */
void migrate(enum arch dst_arch, void (*callback) (void *), void *callback_data)
{
	__migrate_shim_internal(dst_arch, callback, callback_data);
}

#endif
