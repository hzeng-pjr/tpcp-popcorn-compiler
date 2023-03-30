#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>

struct pcn_msg_hdr;

uint32_t pcn_get_ip ();

void rio_get_write (struct pcn_msg_hdr *hdr, int fd);
void rio_poll (struct pcn_msg_hdr *hdr);

#endif
