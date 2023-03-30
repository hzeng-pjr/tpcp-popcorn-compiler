/* Wrapper function for the poll system call. */

#include <string.h>
#include <signal.h>
#include <alloca.h>
#include <poll.h>
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <popcorn.h>
#include <remote_io.h>
#include <message.h>
#include <local_io.h>

extern void check_migrate (void *, void *);
extern void rio_poll (struct pcn_msg_hdr *hdr);

static int
do_pcn_poll (int syscall, int mid, struct pollfd *fds, int nfds,
	     long tv_sec, long tv_nsec, const sigset_t *sigmask)
{
  struct pcn_msg_hdr hdr;
  static struct pcn_msg_poll *msg = NULL;
  struct iovec payload[2];
  sigset_t mask;
  int res, fdsize, msg_size;

  fdsize = sizeof (struct pollfd) * nfds;
  msg_size = fdsize + sizeof (struct pcn_msg_poll);

  /* Stackmaps can't cope with alloca, so keep this msg buffer
     persistent across multiple invocations.  */
  msg = lio_realloc (msg, msg_size);

  hdr.msg_type = PCN_TYPE_SYSCALL;
  hdr.msg_kind = syscall;
  hdr.msg_id = mid;
  hdr.msg_async = PCN_SEND_NORET;
  hdr.msg_size = msg_size;
  hdr.msg_errno = errno;

  msg->nfds = nfds;
  msg->tv_sec = tv_sec;
  msg->tv_nsec = tv_nsec;

  if (sigmask == NULL)
    {
      sigemptyset (&mask);
      lio_memcpy (&msg->sigmask, &mask, sizeof (sigset_t));
    }
  else
    msg->sigmask = *sigmask;

  payload[0].iov_base = msg;
  payload[0].iov_len = sizeof (struct pcn_msg_poll);
  payload[1].iov_base = fds;
  payload[1].iov_len = fdsize;

  res = rio_msg_send_iov (pcn_data->pcn_server_sockfd, &hdr, payload, 2);

  hdr.msg_type = PCN_TYPE_POLL;
  hdr.msg_size = sizeof (hdr);
  rio_poll (&hdr);

  rio_msg_get (pcn_data->pcn_server_sockfd, msg, msg_size);

  lio_memcpy (fds, msg->fds, fdsize);

  res = msg->nfds;
  errno = msg->tv_sec;

  LIO_TOUCH (syscall);
  LIO_TOUCH (mid);
  LIO_TOUCH (nfds);
  LIO_TOUCH (tv_sec);
  LIO_TOUCH (tv_nsec);
  LIO_TOUCH (sigmask);

  return res;
}

int
pcn_poll (struct pollfd *fds, int nfds, int timeout)
{
  int res, mid;
  long sec, nsec;

  /* Note the timeout provided to poll is in miliseconds.  */
  if (timeout == -1)
    {
      sec = -1;
      nsec = 0;
    }
  else
    {
      sec = timeout / 1000;
      nsec = (timeout % 1000) * 1000;
    }

  /* Check if the server is down.  */
  if (!pcn_data->pcn_remote_io_active)
    return lio_poll (fds, nfds, timeout);

  //lio_assert_empty_socket (pcn_data->pcn_server_sockfd);
  rio_disable_signals ();

  mid = rio_msg_id ();

  res = do_pcn_poll (PCN_SYS_POLL, mid, fds, nfds, sec, nsec, NULL);

  rio_dbg_printf ("%s[%u]: fds = %lx, nfds = %u, timeout = %d -- res = %d, errno = %d\n",
		  __FUNCTION__, mid, fds, nfds, timeout, res, errno);

  rio_enable_signals ();

  return res;
}

int
pcn_ppoll (struct pollfd *fds, int nfds, const struct timespec *tp,
	   const sigset_t *sigmask)
{
  int res, mid;

  /* Check if the server is down.  */
  if (!pcn_data->pcn_remote_io_active)
    return lio_ppoll (fds, nfds, tp, sigmask);

  //lio_assert_empty_socket (pcn_data->pcn_server_sockfd);
  rio_disable_signals ();

  mid = rio_msg_id ();

  res = do_pcn_poll (PCN_SYS_PPOLL, mid, fds, nfds, tp == NULL ? -1 : tp->tv_sec,
		     tp == NULL ? 0 : tp->tv_nsec, NULL);

  rio_dbg_printf ("%s[%u]: fds = %lx, nfds = %u, tp = %x, sigmask = %x -- res = %d, errno = %d\n",
		  __FUNCTION__, mid, fds, nfds, tp, sigmask, res, errno);

  rio_enable_signals ();

  return res;
}

extern volatile long __migrate_gb_variable;

int
poll (struct pollfd *fds, nfds_t nfds, int timeout)
{
  int res;

  res = pcn_poll (fds, nfds, timeout);

  /* Workaround a LLVM stackmap bug. At present, LLVM only captures
     variables that are liveout on return from function call. But these
     variables are not live, so coerce the compiler into making them
     live.  */
  LIO_TOUCH (fds);
  LIO_TOUCH (nfds);
  LIO_TOUCH (timeout);

  return res;
}

int
ppoll64 (struct pollfd *fds, nfds_t nfds, const struct timespec *timeout,                                                          
           const sigset_t *sigmask)
{
  return pcn_ppoll (fds, nfds, timeout, sigmask);
}
