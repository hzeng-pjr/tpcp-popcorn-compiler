#include <stdio.h>
#include <stdint.h>
#include <arpa/inet.h>

#include <remote_io.h>
#include <server.h>
#include "test.h"

int
main ()
{
  pcn_server_port = server_port;

  pcn_start_server ();

  return 0;
}
