/* writev system call forwarding.
 *
 * Eventually, this needs to be agnostic to the C library...
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <syscall.h>
#include <unistd.h>

#include "syscall.h"
#include "remote_io.h"
#include "message.h"

struct pcn_write_msg {
  int fd;
  int size;
  char buf[0];
};

ssize_t musl_pcn_writev (int fd, const struct iovec *iov, int iovcnt)
{
  int i, size = 0;
//  int off = 0;
//  uint8_t *buf;
  struct pcn_msg_hdr hdr;
  struct iovec payload[iovcnt + 1];
  struct pcn_write_msg msg;
  int res;

  /* Check if the server is down.  */
  if (pcn_server_sockfd < 0)
    return syscall (SYS_writev, fd, iov, iovcnt);

  for (i = 0; i < iovcnt; i++)
    size += iov[i].iov_len;

//  buf = malloc (size);
//
//  for (i = 0; i < iovcnt; i++) {
//    memcpy (buf + off, iov[i].iov_base, iov[i].iov_len);
//    off += iov[i].iov_len;
//  }

  msg.fd = fd;
  msg.size = size;

  payload[0].iov_base = &msg;
  payload[0].iov_len = sizeof (msg);

  for (i = 1; i < iovcnt+1; i++) {
    payload[i].iov_base = iov[i-1].iov_base;
    payload[i].iov_len = iov[i-1].iov_len;
  }

  hdr.msg_type = PCN_TYPE_SYSCALL;
  hdr.msg_kind = PCN_SYS_WRITE;
  hdr.msg_id = 0;

  res = musl_pcn_send (pcn_server_sockfd, &hdr, payload, iovcnt+1);

//  printf ("size: hdr = %zu, msg = %zu (%d), payload = %d\n",
//	  sizeof (hdr), sizeof (msg), msg.size, size);
//
//  printf ("transmitted %d bytes\n", res);

  //res = read (pcn_server_sockfd, &hdr, sizeof hdr);

  return res;
}
