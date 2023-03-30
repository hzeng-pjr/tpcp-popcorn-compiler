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

static unsigned char
populate_args (void *r, void *w, void *e,
	       void *ir, void *iw, void *ie)
{
  unsigned char args = 0;

  if (ir != NULL)
    {
      memcpy (r, ir, sizeof (fd_set));
      set_arg (args, 1);
    }

  if (iw != NULL)
    {
      memcpy (w, iw, sizeof (fd_set));
      set_arg (args, 2);
    }

  if (ie != NULL)
    {
      memcpy (e, ie, sizeof (fd_set));
      set_arg (args, 3);
    }

  return args;
}

static void
collect_args (void *r, void *w, void *e, struct pcn_msg_select_res *msg)
{
  if (r != NULL)
    memcpy (r, &msg->readfds, sizeof (fd_set));

  if (w != NULL)
    memcpy (w, &msg->writefds, sizeof (fd_set));

  if (e != NULL)
    memcpy (e, &msg->exceptfds, sizeof (fd_set));
}

int
pselect (int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	     const struct timespec *timeout, const sigset_t *sigmask)
{
  struct pcn_msg_hdr hdr;
  struct pcn_msg_pselect msg;
  struct pcn_msg_select_res rmsg;
  struct iovec payload;
  int res, mid;
  unsigned char args = 0;

  /* Check if the server is down.  */
  if (!pcn_data->pcn_remote_io_active)
    return lio_pselect (nfds, readfds, writefds, exceptfds, timeout, sigmask);

  //lio_assert_empty_socket (pcn_data->pcn_server_sockfd);
  rio_disable_signals ();

  mid = rio_msg_id ();

  hdr.msg_type = PCN_TYPE_SYSCALL;
  hdr.msg_kind = PCN_SYS_PSELECT;
  hdr.msg_id = mid;
  hdr.msg_async = PCN_SEND_NORET;
  hdr.msg_size = sizeof (msg);
  hdr.msg_errno = errno;

  msg.nfds = nfds;

  args = populate_args (&msg.readfds, &msg.writefds, &msg.exceptfds,
			readfds, writefds, exceptfds);

  if (timeout != NULL)
    {
      memcpy (&msg.ts, timeout, sizeof (struct timespec));
      set_arg (args, 4);
    }

  if (sigmask != NULL)
    {
      memcpy (&msg.sigmask, sigmask, sizeof (long));
      set_arg (args, 5);
    }

  msg.args = args;

  payload.iov_base = &msg;
  payload.iov_len = sizeof (msg);

  rio_msg_send_iov (pcn_data->pcn_server_sockfd, &hdr, &payload, 1);

  hdr.msg_type = PCN_TYPE_POLL;
  hdr.msg_size = sizeof (hdr);
  rio_poll (&hdr);

  rio_msg_get (pcn_data->pcn_server_sockfd, &rmsg, sizeof (rmsg));

  res = rmsg.res;

  if (res < 0)
    errno = rmsg.rio_errno;

  collect_args (readfds, writefds, exceptfds, &rmsg);

  rio_dbg_printf ("%s[%u]: nfds = %d, readfds = %ls, writefds = %lx execpfds = %lx, timeout = %lx, sigmask = %lx -- res = %u\n",
		  __FUNCTION__, mid, nfds, readfds, writefds,
		  exceptfds, sigmask, res);

  rio_enable_signals ();
  check_migrate (NULL, NULL);

  LIO_TOUCH (hdr);
  LIO_TOUCH (msg);
  LIO_TOUCH (rmsg);
  LIO_TOUCH (payload);
  LIO_TOUCH (res);
  LIO_TOUCH (mid);
  LIO_TOUCH (args);

  return res;
}

int
select (int nfds, fd_set *readfds, fd_set *writefds,
	fd_set *exceptfds, struct timeval *timeout)
{
  struct pcn_msg_hdr hdr;
  struct pcn_msg_select msg;
  struct pcn_msg_select_res rmsg;
  struct iovec payload;
  int res, mid;
  unsigned char args = 0;

  /* Check if the server is down.  */
  if (!pcn_data->pcn_remote_io_active)
    return lio_select (nfds, readfds, writefds, exceptfds, timeout);

  //lio_assert_empty_socket (pcn_data->pcn_server_sockfd);
  rio_disable_signals ();

  mid = rio_msg_id ();

  hdr.msg_type = PCN_TYPE_SYSCALL;
  hdr.msg_kind = PCN_SYS_SELECT;
  hdr.msg_id = mid;
  hdr.msg_async = PCN_SEND_NORET;
  hdr.msg_size = sizeof (msg);
  hdr.msg_errno = errno;

  msg.nfds = nfds;

  args = populate_args (&msg.readfds, &msg.writefds, &msg.exceptfds,
			readfds, writefds, exceptfds);

  if (timeout != NULL)
    {
      memcpy (&msg.tv, timeout, sizeof (struct timeval));
      set_arg (args, 4);
    }

  msg.args = args;

  payload.iov_base = &msg;
  payload.iov_len = sizeof (msg);

  rio_msg_send_iov (pcn_data->pcn_server_sockfd, &hdr, &payload, 1);

  hdr.msg_type = PCN_TYPE_POLL;
  hdr.msg_size = sizeof (hdr);
  rio_poll (&hdr);

  rio_msg_get (pcn_data->pcn_server_sockfd, &rmsg, sizeof (rmsg));

  res = rmsg.res;

  if (res < 0)
    errno = rmsg.rio_errno;

  collect_args (readfds, writefds, exceptfds, &rmsg);

  rio_dbg_printf ("%s[%u]: nfds = %d, readfds = %lx, writefds = %lx execpfds = %lx, timeout = %lx -- res = %u\n",
		  __FUNCTION__, mid, nfds, readfds, writefds,
		  exceptfds, res);

  rio_enable_signals ();
  check_migrate (NULL, NULL);

  LIO_TOUCH (hdr);
  LIO_TOUCH (msg);
  LIO_TOUCH (rmsg);
  LIO_TOUCH (payload);
  LIO_TOUCH (res);
  LIO_TOUCH (mid);
  LIO_TOUCH (args);

  return res;
}
