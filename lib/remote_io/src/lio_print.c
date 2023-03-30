/* This file is a modified version of dl-minimal.c.  */

#include <assert.h>
#include <stdarg.h>
#include <sys/uio.h>
#include <unistd.h>

#include <popcorn.h>
#include "local_io.h"

/* pcn_mode is defined elsewhere in glibc. It's set to 1 if the code
   is running in the RIO server, or 0 otherwise.  */
int pcn_mode = 0;

/* This function should be defined in glibc.  */
static int
rio_debug (void)
{
  return pcn_data->rio_debug == 1 || pcn_data->rio_debug == 3;
}

#define MIN(a,b) (((a)<(b))?(a):(b))
const char _itoa_lower_digits_rio[16] = "0123456789abcdef";

#if defined(__x86_64__) || defined(__aarch64__)
#define NEED_L
#endif

#define RIO_PRINTBUF_SZ 512
static char rio_printbuf[RIO_PRINTBUF_SZ];

/* We always use _itoa instead of _itoa_word in ld.so since the former
   also has to be present and it is never about speed when these
   functions are used.  */
static char *
pcn_itoa (unsigned long long int value, char *buflim, unsigned int base,
       int upper_case)
{
  assert (! upper_case);

  do
    *--buflim = _itoa_lower_digits_rio[value % base];
  while ((value /= base) != 0);

  return buflim;
}

static int
write_str (char *str, size_t size, struct iovec *iov, int niov)
{
  int ix, j, k;

  for (ix = 0, j = 0; ix < size && j < niov; j++)
    {
      const char *buf = iov[j].iov_base;

      for (k = 0; ix < size && k < iov[j].iov_len; k++, ix++)
	str[ix] = buf[k];
    }

  if (ix < size)
    str[ix] = '\0';

  return ix;
}

//#pragma GCC optimize ("O0")
/* Bare-bones printf implementation.  This function only knows about
   the formats and flags needed and can handle only up to 64 stripes in
   the output.  */
static int
pcn_dl_debug_vdprintf (int fd, int tag_p, int show_pid, char *str, size_t size,
                       const char *fmt, va_list arg)
{
# define NIOVMAX 64
  struct iovec iov[NIOVMAX];
  int niov = 0;
  const char *server_msg = "> pcn_server: ";
  char pidbuf[20];

  /* Print out [PID] prefix.  */
  if (show_pid)
    {
      rio_dbg_snprintf (pidbuf, 20, "(%d) ", pcn_data->rio_my_pid);
      iov[0].iov_base = pidbuf;
      iov[0].iov_len = lio_strlen (pidbuf);
      niov++;
    }

  if (pcn_mode == 1)
    {
      iov[niov].iov_base = (void *)server_msg;
      iov[niov].iov_len = lio_strlen (server_msg);
      niov++;
    }

  while (*fmt != '\0')
    {
      const char *startp = fmt;

      /* Skip everything except % and \n (if tags are needed).  */
      while (*fmt != '\0' && *fmt != '%' && (! tag_p || *fmt != '\n'))
	++fmt;

      /* Append constant string.  */
      assert (niov < NIOVMAX);
      if ((iov[niov].iov_len = fmt - startp) != 0)
	iov[niov++].iov_base = (char *) startp;

      if (*fmt == '%')
	{
	  /* It is a format specifier.  */
	  char fill = ' ';
	  int width = -1;
	  int prec = -1;
#ifdef NEED_L
	  int long_mod = 0;
#endif

	  /* Recognize zero-digit fill flag.  */
	  if (*++fmt == '0')
	    {
	      fill = '0';
	      ++fmt;
	    }

	  /* See whether with comes from a parameter.  Note that no other
	     way to specify the width is implemented.  */
	  if (*fmt == '*')
	    {
	      width = va_arg (arg, int);
	      ++fmt;
	    }

	  /* Handle precision.  */
	  if (*fmt == '.' && fmt[1] == '*')
	    {
	      prec = va_arg (arg, int);
	      fmt += 2;
	    }

	  /* Recognize the l modifier.  It is only important on some
	     platforms where long and int have a different size.  We
	     can use the same code for size_t.  */
	  if (*fmt == 'l' || *fmt == 'Z')
	    {
#ifdef NEED_L
	      long_mod = 1;
#endif
	      ++fmt;
	    }

	  switch (*fmt)
	    {
	      /* Integer formatting.  */
            case 'd':
	      {
		long int num;
                int inum;

		/* We have to make a difference if long and int have a
		   different size.  */
                if (long_mod)
                  num = va_arg (arg, unsigned long int);
                else
                  {
                    inum = va_arg (arg, unsigned int);
                    num = inum; // sign extend
                  }

                long int abs_num = num < 0 ? -num : num;

		/* We use alloca() to allocate the buffer with the most
		   pessimistic guess for the size.  Using alloca() allows
		   having more than one integer formatting in a call.  */
		char *buf = (char *) __builtin_alloca (3 * sizeof (long int));
		char *endp = &buf[3 * sizeof (long int)];
		char *cp = pcn_itoa (abs_num, endp, *fmt == 'x' ? 16 : 10, 0);

                if (num < 0)
                  *--cp = '-';

		/* Pad to the width the user specified.  */
		if (width != -1)
		  while (endp - cp < width)
		    *--cp = fill;

		iov[niov].iov_base = cp;
		iov[niov].iov_len = endp - cp;
		++niov;
	      }
	      break;

	    case 'u':
	    case 'x':
	      {
		/* We have to make a difference if long and int have a
		   different size.  */
#ifdef NEED_L
		unsigned long int num = (long_mod
					 ? va_arg (arg, unsigned long int)
					 : va_arg (arg, unsigned int));
#else
		unsigned long int num = va_arg (arg, unsigned int);
#endif
		/* We use alloca() to allocate the buffer with the most
		   pessimistic guess for the size.  Using alloca() allows
		   having more than one integer formatting in a call.  */
		char *buf = (char *) __builtin_alloca (3 * sizeof (unsigned long int));
		char *endp = &buf[3 * sizeof (unsigned long int)];
		char *cp = pcn_itoa (num, endp, *fmt == 'x' ? 16 : 10, 0);

		/* Pad to the width the user specified.  */
		if (width != -1)
		  while (endp - cp < width)
		    *--cp = fill;

		iov[niov].iov_base = cp;
		iov[niov].iov_len = endp - cp;
		++niov;
	      }
	      break;

	    case 's':
	      /* Get the string argument.  */
	      iov[niov].iov_base = va_arg (arg, char *);
	      iov[niov].iov_len = lio_strlen (iov[niov].iov_base);
	      if (prec != -1)
		iov[niov].iov_len = MIN ((size_t) prec, iov[niov].iov_len);
	      ++niov;
	      break;

	    case '%':
	      iov[niov].iov_base = (void *) fmt;
	      iov[niov].iov_len = 1;
	      ++niov;
	      break;

	    default:
              {
                int c = *fmt;
	        lio_error ("invalid format specifier '%d'\n", c);
              }
	    }
	  ++fmt;
	}
      else if (*fmt == '\n')
	{
	  /* See whether we have to print a single newline character.  */
	  if (fmt == startp)
	    {
	      iov[niov].iov_base = (char *) startp;
	      iov[niov++].iov_len = 1;
	    }
	  else
	    /* No, just add it to the rest of the string.  */
	    ++iov[niov - 1].iov_len;

	  /* Next line, print a tag again.  */
	  tag_p = 1;
	  ++fmt;
	}
    }

  /* Finally write the result.  */
  if (str != NULL)
    return write_str (str, size, iov, niov);
  else
    {
      /* Write diagnostics to a string, so that server and client
	 messages don't overwrite one another.  */
      int len = write_str (rio_printbuf, RIO_PRINTBUF_SZ, iov, niov);
      return lio_write (fd, rio_printbuf, len);
    }
}

/* Write to debug file.  */
int
rio_dbg_printf (const char *fmt, ...)
{
  va_list arg;
  int ret;

  if (!rio_debug ())
    return 0;

  va_start (arg, fmt);
  ret = pcn_dl_debug_vdprintf (pcn_data->rio_debug_fd, 0, 1, NULL, 0, fmt, arg);
  va_end (arg);

  return ret;
}

/* Write to debug file.  */
int
rio_dbg_fprintf (int fd, const char *fmt, ...)
{
  va_list arg;
  int ret;

  if (rio_debug ())
    return 0;

  va_start (arg, fmt);
  ret = pcn_dl_debug_vdprintf (fd, 0, 1, NULL, 0, fmt, arg);
  va_end (arg);

  return ret;
}

int
rio_dbg_snprintf (char *str, size_t size, const char *fmt, ...)
{
  va_list arg;
  int ret, t;

  t = pcn_mode;
  pcn_mode = 0;

  va_start (arg, fmt);
  ret = pcn_dl_debug_vdprintf (-1, 0, 0, str, size, fmt, arg);
  va_end (arg);

  pcn_mode = t;

  return ret;
}

int
rio_dbg_vfprintf (int fd, const char *restrict fmt, va_list arg)
{
  return pcn_dl_debug_vdprintf (fd, 0, 1, NULL, 0, fmt, arg);
}

int
lio_dbg_printf (const char *fmt, ...)
{
  va_list arg;
  int ret;

  va_start (arg, fmt);
  ret = pcn_dl_debug_vdprintf (STDOUT_FILENO, 0, 1, NULL, 0, fmt, arg);
  va_end (arg);

  return ret;
}

/* Write to debug file.  */
int
lio_printf (const char *fmt, ...)
{
  va_list arg;
  int ret;

  va_start (arg, fmt);
  ret = pcn_dl_debug_vdprintf (lio_stdout, 0, 1, NULL, 0, fmt, arg);
  va_end (arg);

  return ret;
}

/* Write to debug file.  */
int
lio_fprintf (int fd, const char *fmt, ...)
{
  va_list arg;
  int ret;

  va_start (arg, fmt);
  ret = pcn_dl_debug_vdprintf (fd, 0, 1, NULL, 0, fmt, arg);
  va_end (arg);

  return ret;
}

int
lio_snprintf (char *str, size_t size, const char *fmt, ...)
{
  va_list arg;
  int ret;

  va_start (arg, fmt);
  ret = pcn_dl_debug_vdprintf (-1, 0, 0, str, size, fmt, arg);
  va_end (arg);

  return ret;
}
