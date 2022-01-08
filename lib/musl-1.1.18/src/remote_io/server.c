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

#include <sys/wait.h>
#include <signal.h>

#include "syscall.h"

#include "remote_io.h"
#include "server.h"
#include "message.h"

#define RIO_BUF_SZ 512

int pcn_remote_io_active;
uint32_t pcn_server_ip;
uint16_t pcn_server_port;
int pcn_server_sockfd;
int pcn_client_sockfd;

void
musl_rio_printf (char *str, ...)
{
  char buf[RIO_BUF_SZ];
  va_list ap;

  va_start (ap, str);
  vsnprintf (buf, RIO_BUF_SZ, str, ap);
  va_end (ap);

  syscall (SYS_write, 1, buf, strlen (buf));
}

int
musl_pcn_send (int fd, struct pcn_msg_hdr *hdr, struct iovec *payload, int cnt)
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
