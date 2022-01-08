#ifndef REMOTE_IO_H
#define REMOTE_IO_H

#include <sys/uio.h>

/* Server IP addresses.  */
extern uint32_t pcn_server_ip;
extern uint16_t pcn_server_port;
extern int pcn_server_sockfd;
extern int pcn_client_sockfd;
extern int pcn_remote_io_active;

ssize_t pcn_readv (int fd, const struct iovec *iov, int iovecnt);
ssize_t pcn_writev (int fd, const struct iovec *iov, int iovecnt);

int pcn_server_connect (uint32_t ip);
void pcn_start_server (void);
void pcn_migrate (void);

#endif
