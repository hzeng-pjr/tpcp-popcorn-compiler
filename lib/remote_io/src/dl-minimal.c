#include <assert.h>
#include <stdarg.h>
#include <sys/uio.h>
#include "local_io.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
const char _itoa_lower_digits[16] = "0123456789abcdef";

#if defined(__x86_64__) || defined(__aarch64__)
#define NEED_L
#endif

/* We always use _itoa instead of _itoa_word in ld.so since the former
   also has to be present and it is never about speed when these
   functions are used.  */
char *
pcn_itoa (unsigned long long int value, char *buflim, unsigned int base,
       int upper_case)
{
  assert (! upper_case);

  do
    *--buflim = _itoa_lower_digits[value % base];
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

  return ix;
}

/* Bare-bones printf implementation.  This function only knows about
   the formats and flags needed and can handle only up to 64 stripes in
   the output.  */
static int
pcn_dl_debug_vdprintf (int fd, int tag_p, char *str, size_t size,
		       const char *fmt, va_list arg)
{
# define NIOVMAX 64
  struct iovec iov[NIOVMAX];
  int niov = 0;
  pid_t pid = 0;
  char pidbuf[12];

  while (*fmt != '\0')
    {
      const char *startp = fmt;

      if (tag_p > 0)
	{
	  /* Generate the tag line once.  It consists of the PID and a
	     colon followed by a tab.  */
	  if (pid == 0)
	    {
	      char *p;
	      pid = lio_getpid ();
	      assert (pid >= 0 && sizeof (pid_t) <= 4);
	      p = pcn_itoa (pid, &pidbuf[10], 10, 0);
	      while (p > pidbuf)
		*--p = ' ';
	      pidbuf[10] = ':';
	      pidbuf[11] = '\t';
	    }

	  /* Append to the output.  */
	  assert (niov < NIOVMAX);
	  iov[niov].iov_len = 12;
	  iov[niov++].iov_base = pidbuf;

	  /* No more tags until we see the next newline.  */
	  tag_p = -1;
	}

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
	      lio_error ("invalid format specifier");
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
    return lio_writev (fd, iov, niov);
}


/* Write to debug file.  */
int
lio_printf (const char *fmt, ...)
{
  va_list arg;
  int ret;

  va_start (arg, fmt);
  ret = pcn_dl_debug_vdprintf (lio_stdout, 0, NULL, 0, fmt, arg);
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
  ret = pcn_dl_debug_vdprintf (fd, 0, NULL, 0, fmt, arg);
  va_end (arg);

  return ret;
}

int
lio_snprintf (char *str, size_t size, const char *fmt, ...)
{
  va_list arg;
  int ret;

  va_start (arg, fmt);
  ret = pcn_dl_debug_vdprintf (-1, 0, str, size, fmt, arg);
  va_end (arg);

  return ret;
}
