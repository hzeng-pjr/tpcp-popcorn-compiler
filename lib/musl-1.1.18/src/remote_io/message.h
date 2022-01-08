#ifndef MESSAGE_H
#define MESSAGE_H

struct iovec;

typedef enum {
  PCN_TYPE_CONTROL,
  PCN_TYPE_SYSCALL,
} pcn_msg_type;

typedef enum {
  PCN_SYS_WRITE,
} pcn_syscall;

typedef enum {
  PCN_CTL_ACK,
  PCN_CTL_MIGRATE,
} pcn_control;

struct pcn_msg_hdr {
  pcn_msg_type msg_type;
  int msg_kind;
  int msg_id;
  int msg_size;  /* size of the message payload. */
};

int pcn_send (int fd, struct pcn_msg_hdr *hdr, struct iovec *payload, int cnt);

#endif
