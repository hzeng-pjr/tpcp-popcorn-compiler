/* writev system call forwarding.
 *
 * Eventually, this needs to be agnostic to the C library...
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <remote_io.h>
#include <message.h>
#include <sys/socket.h>
#include <syscall.h>
#include <unistd.h>

struct pcn_write_msg {
  int fd;
  int size;
  char buf[0];
};

extern uint32_t local_ip;
void rio_printf (char *str, ...);

void
rio_get_write (struct pcn_msg_hdr *hdr, int fd)
{
  struct pcn_write_msg *msg = malloc (hdr->msg_size);
  int res;

  //rio_printf ("%s\n", __FUNCTION__);

  res = recv (fd, msg, hdr->msg_size, 0);
  if (res < hdr->msg_size)
    rio_printf ("error: lost data\n");

  if (local_ip != pcn_server_ip) {
    struct iovec iov[1];

    iov[0].iov_base = msg->buf;
    iov[0].iov_len = msg->size;

    //syscall (SYS_writev, pcn_server_ip, iov, 2);
    pcn_writev (msg->fd, iov, 1);
  } else {
    write (msg->fd, msg->buf, msg->size);
  }

  free (msg);

//  hdr->msg_type = PCN_TYPE_CONTROL;
//  hdr->msg_kind = PCN_CTL_ACK;
//
//  write (fd, hdr, sizeof (struct pcn_msg_hdr));
}

ssize_t pcn_writev (int fd, const struct iovec *iov, int iovcnt)
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

  res = pcn_send (pcn_server_sockfd, &hdr, payload, iovcnt+1);

//  printf ("size: hdr = %zu, msg = %zu (%d), payload = %d\n",
//	  sizeof (hdr), sizeof (msg), msg.size, size);
//
//  printf ("transmitted %d bytes\n", res);

  //res = read (pcn_server_sockfd, &hdr, sizeof hdr);

  return res;
}
