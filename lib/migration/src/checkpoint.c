#if _GBL_VARIABLE_MIGRATE == 1
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stack_transform.h>
#include <remote_io.h>
#include <local_io.h>
#include "platform.h"
#include "migrate.h"
#include "config.h"
#include "arch.h"
#include "internal.h"
#include "mapping.h"
#include "debug.h"
#include "system.h"


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

#define MUSL_PTHREAD_DESCRIPTOR_SIZE 288

/* musl-libc's architecture-specific function for setting the TLS pointer */
int __set_thread_area(void *);

/*
 * Convert a pointer to the start of the TLS region to the
 * architecture-specific thread pointer.  Derived from musl-libc's
 * per-architecture thread-pointer locations -- see each architecture's
 * "pthread_arch.h" file.
 */
static inline void *get_thread_pointer(void *raw_tls, enum arch dest)
{
	switch (dest) {
	case ARCH_AARCH64:
		return raw_tls - 16;
	case ARCH_POWERPC64:
		return raw_tls + 0x7000;	// <- TODO verify
	case ARCH_X86_64:
		return raw_tls - MUSL_PTHREAD_DESCRIPTOR_SIZE;
	default:
		assert(0 && "Unsupported architecture!");
		return NULL;
	}
}

//TODO: per thread
union {
	struct regset_aarch64 aarch;
	struct regset_powerpc64 powerpc;
	struct regset_x86_64 x86;
} regs_dst;
void* tls_dst=0x0;

/* Generate a call site to get rewriting metadata for outermost frame. */
static void* __attribute__((noinline))
get_call_site() { return __builtin_return_address(0); };

static void dummy(){lio_printf("%s: called\n", __func__);};

void
__migrate_shim_internal(enum arch dst_arch, void (*callback) (void *), void *callback_data)
{
	Elf64_Ehdr ehdr;
	Elf64_Phdr *phdrs;
	int phnum;
	unsigned long entry;
	int i, fd;
	struct dl_pcn_data *pcn_data = (void *) DL_PCN_STATE;
	void *t, *ld_start;
	int err, ret;

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

		lio_printf ("pcn_server_port = %u\n", pcn_server_port);

		/* Inform the I/O server of the impending migration.  */
		pcn_migrate ();
		close (pcn_server_sockfd);

		_dl_rio_populate_dso_entries ();
		print_all_dso ();
		unload_libs ();
		lio_print ("unload complete\n");

		GET_LOCAL_REGSET(regs_src);

		lio_printf ("GET_LOCAL_REGSET complete\n");
		lio_printf ("dest = %u\n", dst_arch);

		err = 0;
		switch (dst_arch) {
			case ARCH_AARCH64:
				err = !REWRITE_STACK(regs_src, regs_dst,
						     dst_arch);
				regs_dst.aarch.__magic = 0xAABBDEADBEAF;
				lio_printf ("rewrote stack\n");
				dump_regs_aarch64(&regs_dst.aarch, LOG_FILE);
				break;
			case ARCH_X86_64:
				err = !REWRITE_STACK(regs_src, regs_dst,
						     dst_arch);
				regs_dst.x86.__magic = 0xA8664DEADBEAF;
				lio_printf ("rewrote stack\n");
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
			lio_printf("Could not rewrite stack!\n");
			return;
		}
		lio_printf("dest arch is %d\n", dst_arch);
		tls_dst = get_thread_pointer(GET_TLS_POINTER, dst_arch);
		lio_printf("%s %u\n", __func__, __LINE__);
		set_restore_context(1);
		lio_printf("%s %u\n", __func__, __LINE__);
		clear_migrate_flag();

		//signal(SIGALRM, dummy);
		struct ksigaction kact, koact;

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

		lio_printf ("raising SIGALRM\n");

		//raise(SIGALRM); /* wil be catched by ptrace */
		lio_kill (lio_getpid (), SIGALRM);

		//sigprocmask(SIG_SETMASK, &old_sig_set, NULL);
		lio_sigprocmask (SIG_UNBLOCK, &old_sig_set, NULL, NSIG / 8);

		lio_printf("%s raising done %d\n", __func__, __LINE__);
		//while(1);
		return;
	}
	// Post-migration

	// Translate between architecture-specific thread descriptors
	// Note: TLS is now invalid until after migration!
	//__set_thread_area(get_thread_pointer(GET_TLS_POINTER, CURRENT_ARCH));
	set_restore_context(0);

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

	entry = ehdr.e_entry;
	restore_rw_segments (phdrs, phnum, entry);
	reset_dynamic (phdrs, phnum, entry, pcn_data->argv[0], &ehdr, fd);

	lio_close (fd);

	pcn_data->pcn_entry = (unsigned long) &&pcn_cont;
	ld_start = load_lib (pcn_data->maps[0].name); // Load ld-linux

#if defined (__x86_64__)
  asm volatile ("jmp *%0;\n\t" : : "r" (ld_start));
#elif defined (__aarch64__)
  asm volatile ("br %0;\n\t" : : "r" (ld_start));
#else
#lio_error "Unsupported arch"
#endif

 pcn_cont:
	pcn_server_sockfd = pcn_server_connect (pcn_server_ip);
	lio_printf ("pcn_server_port = %d\n", pcn_server_port);
}

/* Check if we should migrate, and invoke migration. */
void check_migrate(void (*callback) (void *), void *callback_data)
{
	enum arch dst_arch = do_migrate(NULL);
	if (dst_arch >= 0)
		__migrate_shim_internal(dst_arch, callback, callback_data);
}

/* Invoke migration to a particular node if we're not already there. */
void migrate(enum arch dst_arch, void (*callback) (void *), void *callback_data)
{
	__migrate_shim_internal(dst_arch, callback, callback_data);
}

#endif
