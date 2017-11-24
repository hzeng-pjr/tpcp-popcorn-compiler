#include <fcntl.h>
#include <stdarg.h>
#include "syscall.h"
#include "libc.h"

int __open_(const char *filename, int flags, mode_t mode)
{
	int fd = __sys_open_cp(filename, flags, mode);
	if (fd>=0 && (flags & O_CLOEXEC))
		__syscall(SYS_fcntl, fd, F_SETFD, FD_CLOEXEC);

	return __syscall_ret(fd);
}

int __open(const char *filename, int flags, ...)
{
	mode_t mode = 0;

	if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}
	return __open_(filename, flags, mode);
}

weak_alias(__open, open);

LFS64(__open);
