#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <remote_io.h>
#include <server.h>

#include "test.h"

#define BUF_SZ 100

int
main ()
{
  uint32_t local_ip = pcn_get_ip ();
  struct iovec iv[3];
  char buf[BUF_SZ];
  char hello[] = "hello ";
  char bye[] = ", goodbye\n";

  snprintf (buf, BUF_SZ, "%d", server_port);

  iv[0].iov_base = hello;
  iv[0].iov_len = strlen (hello);

  iv[1].iov_base = buf;
  iv[1].iov_len = strlen (buf);

  iv[2].iov_base = bye;
  iv[2].iov_len = strlen (bye);

  writev (1, iv, 3);

  pcn_server_port = server_port;
  pcn_server_ip = local_ip;
  pcn_server_sockfd = pcn_server_connect (local_ip);

  pcn_writev (1, iv, 3);

  close (pcn_server_sockfd);

  return 0;
}
