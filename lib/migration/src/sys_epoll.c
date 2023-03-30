#include <string.h>
#include <signal.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <popcorn.h>
#include <remote_io.h>
#include <message.h>
#include <local_io.h>

#ifndef EPOLL_VER
#define EPOLL_VER 1
#endif

extern void check_migrate (void *, void *);

extern int __do_epoll_wait (int ver, int epfd, struct epoll_event *events,
			    int maxevents, int timeout);
extern int __do_epoll_pwait (int ver, int epfd, struct epoll_event *events,
			     int maxevents, int timeout,
			     const sigset_t *sigmask);
extern void rio_poll (struct pcn_msg_hdr *hdr);

int
do_pcn_epoll (struct pcn_msg_hdr *hdr)
{
  struct pcn_msg_res ack;

  hdr->msg_type = PCN_TYPE_POLL;
  hdr->msg_size = sizeof (struct pcn_msg_hdr);
  rio_poll (hdr);

  rio_msg_get (pcn_data->pcn_server_sockfd, &ack, sizeof (ack));
  errno = ack.rio_errno;

  LIO_TOUCH (hdr);

  return ack.res;
}

int
pcn_epoll_wait (int ver, int epfd, struct epoll_event *events, int maxevents,
		int timeout)
{
  struct pcn_msg_hdr hdr;
  struct pcn_msg_epoll msg;
  struct iovec payload[1];
  ssize_t revents;
  int mid;

  /* Check if the server is down.  */
  if (!pcn_data->pcn_remote_io_active)
    return __do_epoll_wait (ver, epfd, events, maxevents, timeout);

  //lio_assert_empty_socket (pcn_data->pcn_server_sockfd);
  rio_disable_signals ();

  mid = rio_msg_id ();

  payload[0].iov_base = &msg;
  payload[0].iov_len = sizeof (msg);

  hdr.msg_type = PCN_TYPE_SYSCALL;
  hdr.msg_kind = PCN_SYS_EPOLL_WAIT;
  hdr.msg_id = mid;
  hdr.msg_async = PCN_SEND_NORET;
  hdr.msg_size = sizeof (struct pcn_msg_epoll);
  hdr.msg_errno = errno;

  msg.ver = ver;
  msg.epfd = epfd;
  msg.arg1 = maxevents;
  msg.arg2 = timeout;

  rio_msg_send_iov (pcn_data->pcn_server_sockfd, &hdr, payload, 1);
  revents = do_pcn_epoll (&hdr);

  if (revents > 0)
    rio_msg_get (pcn_data->pcn_server_sockfd, events,
		 revents * sizeof (struct epoll_event));

  if (revents > maxevents)
    lio_error ("%s: too many events returned\n", __FUNCTION__);

  rio_dbg_printf ("%s[%u]: epfd = %d, events = %lx, maxevents = %d, timeout = %d -- res = %ld, errno = %d\n",
		  __FUNCTION__, mid, epfd, events, maxevents,
		  timeout, revents, errno);

  if (revents > maxevents)
    lio_error ("%s: received too many events: %d / %d\n", __FUNCTION__, revents, maxevents);

  //lio_assert_empty_socket (pcn_data->pcn_server_sockfd);
  rio_enable_signals ();
  check_migrate (NULL, NULL);

  LIO_TOUCH (ver);
  LIO_TOUCH (epfd);
  LIO_TOUCH (events);
  LIO_TOUCH (maxevents);
  LIO_TOUCH (timeout);

  return revents;
}

int
pcn_epoll_pwait (int ver, int epfd, struct epoll_event *events,
		int maxevents, int timeout, const sigset_t *sigmask)
{
  struct pcn_msg_hdr hdr;
  struct pcn_msg_epoll_pwait msg;
  struct iovec payload[1];
  ssize_t revents;
  int mid;

  /* Check if the server is down.  */
  if (!pcn_data->pcn_remote_io_active)
    return __do_epoll_wait (ver, epfd, events, maxevents, timeout);

  //lio_assert_empty_socket (pcn_data->pcn_server_sockfd);
  rio_disable_signals ();

  mid = rio_msg_id ();

  payload[0].iov_base = &msg;
  payload[0].iov_len = sizeof (msg);

  hdr.msg_type = PCN_TYPE_SYSCALL;
  hdr.msg_kind = PCN_SYS_EPOLL_PWAIT;
  hdr.msg_id = mid;
  hdr.msg_async = PCN_SEND_NORET;
  hdr.msg_size = sizeof (struct pcn_msg_epoll_pwait);
  hdr.msg_errno = errno;

  msg.ver = ver;
  msg.epfd = epfd;
  msg.maxevents = maxevents;
  msg.timeout = timeout;

  if (sigmask == NULL)
    msg.sigmask = 0;
  else
    msg.sigmask = *(long *)sigmask;

  rio_msg_send_iov (pcn_data->pcn_server_sockfd, &hdr, payload, 1);
  revents = do_pcn_epoll (&hdr);

   if (revents > 0)
    rio_msg_get (pcn_data->pcn_server_sockfd, events,
		 revents * sizeof (struct epoll_event));

  rio_dbg_printf ("%s[%u]: epfd = %d, events = %lx, maxevents = %d, timeout = %d, sigmask = %lx -- res = %ld, errno = %d\n",
		  __FUNCTION__, mid, epfd, events, maxevents,
		  timeout, revents, errno);

  //lio_assert_empty_socket (pcn_data->pcn_server_sockfd);
  check_migrate (NULL, NULL);
  rio_enable_signals ();

  LIO_TOUCH (ver);
  LIO_TOUCH (epfd);
  LIO_TOUCH (events);
  LIO_TOUCH (maxevents);
  LIO_TOUCH (timeout);
  LIO_TOUCH (sigmask);

  return revents;
}

int
epoll_wait (int epfd, struct epoll_event *events, int maxevents,
	    int timeout)
{
  int res = pcn_epoll_wait (EPOLL_VER, epfd, events, maxevents, timeout);

  LIO_TOUCH (epfd);
  LIO_TOUCH (events);
  LIO_TOUCH (maxevents);
  LIO_TOUCH (timeout);

  return res;
}

int
epoll_pwait (int epfd, struct epoll_event *events,
	     int maxevents, int timeout, const sigset_t *sigmask)
{
  int res = pcn_epoll_pwait (EPOLL_VER, epfd, events, maxevents, timeout,
			     sigmask);

  LIO_TOUCH (epfd);
  LIO_TOUCH (events);
  LIO_TOUCH (maxevents);
  LIO_TOUCH (timeout);
  LIO_TOUCH (sigmask);

  return res;
}
