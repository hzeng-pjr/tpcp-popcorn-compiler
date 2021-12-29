#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <ifaddrs.h>
#include <net/if.h>

#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

#include <stdarg.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include <remote_io.h>
#include <server.h>
#include <message.h>

uint32_t pcn_server_ip;
uint16_t pcn_server_port;

/* On a server process, pcn_server_sockfd represents the socket connection
 * to the primary server if necessary. pcn_client_sockfd represents the
 * connection to the client.
 *
 * On the local application, pcn_server_sockfd represents the socket
 * connection to the local server, and pcn_client_sockfd is unused.
 */
int pcn_server_sockfd;
int pcn_client_sockfd;
uint32_t local_ip;

static int migrate_pending;

static int rio_debug;

static uint16_t alloc_server_port ();

#define RIO_BUF_SZ 512

void
rio_printf (char *str, ...)
{
  char buf[RIO_BUF_SZ];
  va_list ap;

  va_start (ap, str);
  vsnprintf (buf, RIO_BUF_SZ, str, ap);
  va_end (ap);

  syscall (SYS_write, 1, buf, strlen (buf));
}

void
pcn_server_init ()
{
  struct sockaddr_in sin;
  socklen_t len = sizeof (sin);

  local_ip = htonl (0x7f000001); /* 127.0.0.1  */
  pcn_server_port = alloc_server_port ();
  pcn_server_sockfd = pcn_server_connect (0);
}

/*
 * At present, this function returns the first non-local IPv4 address
 * on the machine calling it.  This may cause problems if the machine
 * is multihomed.
 */
uint32_t
pcn_get_ip ()
{
  struct ifaddrs *myaddrs, *ifa;
  void *in_addr;
  uint32_t ip = 0;

  local_ip = htonl (0x7f000001); /* 127.0.0.1  */

  if(getifaddrs(&myaddrs) != 0)
    {
      perror("getifaddrs");
      exit(1);
    }

  for (ifa = myaddrs; ifa != NULL; ifa = ifa->ifa_next)
    {
      struct sockaddr_in *s4;

      if (ifa->ifa_addr == NULL)
	continue;
      if (!(ifa->ifa_flags & IFF_UP))
	continue;

      if (ifa->ifa_addr->sa_family != AF_INET)
	continue;

      s4 = (struct sockaddr_in *)ifa->ifa_addr;
      in_addr = &s4->sin_addr;
      ip = *(long *) in_addr;

      if (ip != local_ip)
	break;
    }

  freeifaddrs(myaddrs);

  return ip;
}

static uint16_t
alloc_server_port ()
{
  int sockfd = socket (AF_INET, SOCK_STREAM, 0);
  struct addrinfo hints, *ai;
  struct sockaddr_in sin;
  socklen_t len = sizeof (sin);
  char buf[INET_ADDRSTRLEN];
  int res;

  inet_ntop (AF_INET, &pcn_server_ip, buf, sizeof (buf));

  memset (&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if ((res = getaddrinfo (buf, NULL, &hints, &ai)) != 0) {
    rio_printf ("%s: %s\n", __FUNCTION__, gai_strerror (res));
    exit (EXIT_FAILURE);
  }

  assert (ai != NULL);

  if (bind (sockfd, ai->ai_addr, ai->ai_addrlen) < 0) {
    rio_printf ("%s: failed to allocate IP port\n", __FUNCTION__);
    exit (EXIT_FAILURE);
  }

  if (getsockname(sockfd, (struct sockaddr *)&sin, &len) == -1) {
    perror("getsockname");
    exit (EXIT_FAILURE);
  }

  close (sockfd);
  freeaddrinfo (ai);

  return sin.sin_port;
}

static int
connect_to (uint32_t ip, uint16_t port)
{
  int sockfd, res;
  struct addrinfo hints, *ai;
  char s_addr[INET_ADDRSTRLEN], s_port[INET_ADDRSTRLEN];

  memset (&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  inet_ntop (AF_INET, &ip, s_addr, INET_ADDRSTRLEN);
  snprintf (s_port, INET_ADDRSTRLEN, "%d", port);

  if ((res = getaddrinfo (s_addr, s_port, &hints, &ai)) != 0) {
    rio_printf ("getaddrinfo: %s\n", gai_strerror (res));
    exit (EXIT_FAILURE);
  }

  assert (ai != NULL && ai->ai_family == AF_INET);

  if (rio_debug)
    rio_printf ("connecting to %s:%s\n", s_addr, s_port);

  sockfd = socket (ai->ai_family, ai->ai_socktype, ai->ai_protocol);
  if (sockfd < 0)
    {
      perror ("socket");
      exit (EXIT_FAILURE);
    }

  if (connect (sockfd, ai->ai_addr, ai->ai_addrlen) == -1) {
    perror ("connection failed");
    rio_printf ("   -> %s:%s\n", s_addr, s_port);
    exit (EXIT_FAILURE);
  }

  return sockfd;
}

static void
do_control (struct pcn_msg_hdr *hdr, int fd)
{
  if (local_ip != pcn_server_ip) {
    write (pcn_server_ip, hdr, sizeof (struct pcn_msg_hdr));
    return;
  }

  switch (hdr->msg_kind) {
  case PCN_CTL_MIGRATE:
    rio_printf ("server: received migration request\n");
    migrate_pending = 1;
    break;

  default:
    ;
  }
}

static void
do_syscall (struct pcn_msg_hdr *hdr, int fd)
{
  if (rio_debug) {
    char s_addr[INET_ADDRSTRLEN], l_addr[INET_ADDRSTRLEN];

    inet_ntop (AF_INET, &pcn_server_ip, s_addr, INET_ADDRSTRLEN);
    inet_ntop (AF_INET, &local_ip, l_addr, INET_ADDRSTRLEN);

    rio_printf ("%s: %s -> %s\n", __FUNCTION__, s_addr, l_addr);
  }

  switch (hdr->msg_kind) {
  case PCN_SYS_WRITE:
    rio_get_write (hdr, fd);
    break;

  default:
    ;
  }
}

/* Returns 1 if a client has been dropped.  */
static int
process_message (int fd)
{
  struct pcn_msg_hdr hdr;
  int res;

  res = read (fd, &hdr, sizeof (hdr));

  if (res == 0) {
    if (!migrate_pending) {
      rio_printf ("client hung up... terminating\n");
      exit (EXIT_SUCCESS);
    }

    rio_printf ("client hung up\n");

    return 1;
  }

  if (res < sizeof (hdr)) {
    rio_printf ("something went wrong - malformed message (%d)\n", fd);
    exit (EXIT_FAILURE);
  }

  switch (hdr.msg_type) {
  case PCN_TYPE_CONTROL:
    do_control (&hdr, fd);
    break;

  case PCN_TYPE_SYSCALL:
    do_syscall (&hdr, fd);
    break;

  default:
    rio_printf ("unexpected message type: %d\n", hdr.msg_type);
    ;
  }

  return 0;
}

static void
remote_io_server (int listen_fd)
{
  /* There are at most three socket descriptors to keep track of:
   *   1: The listener socket
   *   2: The socket to the primary server
   *   3: The socket to the client
   *
   * The listener socket is planced in pfds[0]. If a connection is
   * required to the primary server, it will be in pfds[1]. Otherwise
   * the socket to the client will be placed in pfds[1] or pfds[2]
   * depending on the existance of the primary server connection.
   */
  struct pollfd pfds[3];
  int fd_count = 1;

  local_ip = pcn_get_ip ();

  //rio_printf ("starting server:%hd\n", pcn_server_port);

  pfds[0].fd = listen_fd;
  pfds[0].events = POLLIN;

  if (local_ip != pcn_server_ip)
    {
      pcn_server_sockfd = connect_to (pcn_server_ip, pcn_server_port);
      pfds[1].fd = pcn_server_sockfd;
      pfds[1].events = POLLIN;
      fd_count = 2;
    }

  while (1) {
    int poll_count = poll (pfds, fd_count, -1);
    struct sockaddr_storage remoteaddr;
    socklen_t addrlen;
    int i;

    if (poll_count == -1) {
      perror ("poll");
      exit (EXIT_FAILURE);
    }

    if (rio_debug)
      rio_printf ("%s: connections = %d\n", __FUNCTION__, fd_count);

    for (i = 0; i < fd_count; i++) {
      if (pfds[i].revents & POLLIN) {
	int res;

	if (pfds[i].fd != listen_fd) {
	  res = process_message (pfds[i].fd);

	  if (res == 1) {
	    assert (pfds[i].fd == pcn_client_sockfd);

	    fd_count--;
	    pcn_client_sockfd = -1;
	    pfds[fd_count].fd = pcn_client_sockfd;
	  }

	  continue;
	}

	addrlen = sizeof (remoteaddr);

	pcn_client_sockfd = accept (listen_fd, (struct sockaddr *)&remoteaddr,
				    &addrlen);

	if (pcn_client_sockfd < 0)
	  perror ("failed to accept client");
	else {
	  char buf[INET_ADDRSTRLEN];
	  struct sockaddr_in *sin = (struct sockaddr_in *)&remoteaddr;

	  if (1 || rio_debug) {
	    inet_ntop (remoteaddr.ss_family, &sin->sin_addr, buf,
		       INET6_ADDRSTRLEN);
	    rio_printf ("%s: accepted client %s\n", __FUNCTION__, buf);
	  }

	  pfds[fd_count].fd = pcn_client_sockfd;
	  pfds[fd_count].events = POLLIN;
	  pfds[fd_count].revents = 0;
	  fd_count++;
	  migrate_pending = 0;
	}
      } else if (pfds[i].revents & POLLHUP) {
	assert (pfds[i].fd == pcn_client_sockfd);
	// terminate server if client necessary
	// FIXME: this is detected by receiving 0 bytes

	if (rio_debug)
	  rio_printf ("client disconnected... shutting down\n");

	exit (EXIT_SUCCESS);
      }
    }
  }

  if (pcn_server_ip != local_ip)
    ; // coordinate shutdown
  exit (EXIT_SUCCESS);
}

/*
 * Connect to the I/O server.
 *
 * A Popcorn application may be launched on machine A and
 * checkpoint/restored onto a machine B. Note that machines A and B
 * may be the same machine, e.g. Application launched on A, migrates
 * over to B, then migrates back to A.
 *
 * A Popcorn application has at most two servers, a server running on
 * the local host and the primary server running on the machine that
 * originally launched the Popcorn application. The secondary server
 * on a remote machine is necessary to handle signal forwarding
 * without introducing additional threads into the Popcorn
 * application. Communication between the Popcorn application and a
 * server running on the same machine is conducted via local network.
 *
 * A new server is spawned in two cases:
 *   1) When the Popcorn application is first launched on Machine A.
 *   2) When the Popcorn application has migrated to Machine B != A.
 *
 * This function is responsible for setting up a server if necessary.
 * It returns a socket descriptor to the local server.
 */
int
pcn_server_connect (uint32_t ip)
{
  uint32_t myip = pcn_get_ip ();
  int sockfd = -1;
  struct addrinfo hints, *ai;
  //struct sockaddr_in sin;
  //socklen_t len = sizeof (sin);
  uint16_t port = pcn_server_port;
  int res;
  char buf[INET_ADDRSTRLEN], *s_addr = NULL, *s_port = NULL;

  rio_printf ("entering %s\n", __FUNCTION__);

  /* TODO: handle 'ip' for remote servers".  */
  if (myip != pcn_server_ip)
    {
      memset (&hints, 0, sizeof (hints));
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_flags = AI_PASSIVE;

      snprintf (buf, INET_ADDRSTRLEN, "%d", pcn_server_port);
      s_port = buf;

      if ((res = getaddrinfo (s_addr, s_port, &hints, &ai)) != 0) {
	rio_printf ("getaddrinfo: %s\n", gai_strerror (res));
	exit (EXIT_FAILURE);
      }

      assert (ai != NULL && ai->ai_family == AF_INET);

      sockfd = socket (ai->ai_family, ai->ai_socktype, ai->ai_protocol);
      if (sockfd < 0)
	{
	  perror ("socket");
	  exit (EXIT_FAILURE);
	}

      if (bind (sockfd, ai->ai_addr, ai->ai_addrlen) == -1)
	{
	  close (sockfd);
	  perror ("bind");
	  exit (EXIT_FAILURE);
	}

      if (listen (sockfd, 2) == -1) {
	perror ("listen");
	exit (EXIT_FAILURE);
      }

      if (pcn_server_ip == 0)
	{
	  pcn_server_ip = myip;
	  pcn_server_port = port;
	}

      freeaddrinfo (ai);

      res = fork ();

      if (res != 0) {
	/* Run the application on the fork'ed process so that CRIU
	   does not attempt to suspend the server. Eventually, this
	   might need to use a standalone server.  */

	remote_io_server (sockfd);   /* Never return.  */
      }

      close (sockfd);
    }

  /* Return a socket to the server running on the local machine.  */
  return connect_to (local_ip, port);
  //return connect_to (pcn_server_ip, port);
}

/* Launch a server without forking a new process for testing purposes.  */
void
pcn_start_server ()
{
  uint32_t myip = pcn_get_ip ();
  int sockfd = -1;
  struct addrinfo hints, *ai;
  uint16_t port = pcn_server_port;
  int res;
  char buf[INET_ADDRSTRLEN], *s_addr = NULL, *s_port = NULL;

  memset (&hints, 0, sizeof (hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  snprintf (buf, INET_ADDRSTRLEN, "%d", pcn_server_port);
  s_port = buf;

  if ((res = getaddrinfo (s_addr, s_port, &hints, &ai)) != 0) {
    rio_printf ("getaddrinfo: %s\n", gai_strerror (res));
    exit (EXIT_FAILURE);
  }

  assert (ai != NULL && ai->ai_family == AF_INET);

  sockfd = socket (ai->ai_family, ai->ai_socktype, ai->ai_protocol);
  if (sockfd < 0)
    {
      perror ("socket");
      exit (EXIT_FAILURE);
    }

  if (bind (sockfd, ai->ai_addr, ai->ai_addrlen) == -1)
    {
      close (sockfd);
      perror ("bind");
      exit (EXIT_FAILURE);
    }

  if (pcn_server_ip == 0)
    {
      pcn_server_ip = myip;
      pcn_server_port = port;
    }

  if (listen (sockfd, 2) == -1) {
    perror ("listen");
    exit (EXIT_FAILURE);
  }

  freeaddrinfo (ai);

  remote_io_server (sockfd);
}

int
pcn_send (int fd, struct pcn_msg_hdr *hdr, struct iovec *payload, int cnt)
{
  int size = 0;
  int i;
  struct iovec out[cnt+1];

  for (i = 0; i < cnt; i++)
    size += payload[i].iov_len;

  hdr->msg_size = size;

  out[0].iov_base = hdr;
  out[0].iov_len = sizeof (struct pcn_msg_hdr);

  for (i = 1; i <= cnt; i++) {
    out[i].iov_base = payload[i-1].iov_base;
    out[i].iov_len = payload[i-1].iov_len;
  }

  /* This write will terminate with a SIGPIPE if the connection
     between the client and the host has closed unexpectedly.  */
  return syscall (SYS_writev, fd, out, cnt + 1);
}

void
pcn_migrate ()
{
  struct pcn_msg_hdr hdr;

  hdr.msg_type = PCN_TYPE_CONTROL;
  hdr.msg_kind = PCN_CTL_MIGRATE;

  pcn_send (pcn_server_sockfd, &hdr, NULL, 0);

  close (pcn_server_sockfd);

  pcn_server_sockfd = -1;
}
