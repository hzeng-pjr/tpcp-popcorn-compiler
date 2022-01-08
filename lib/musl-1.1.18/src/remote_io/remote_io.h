#ifndef REMOTE_IO_H
#define REMOTE_IO_H

#include <sys/uio.h>

/* Server IP addresses.  */
extern uint32_t pcn_server_ip;
extern uint16_t pcn_server_port;
extern int pcn_server_sockfd;
extern int pcn_client_sockfd;

ssize_t musl_pcn_writev (int fd, const struct iovec *iov, int iovecnt);

#endif
