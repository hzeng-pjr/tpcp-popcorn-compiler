#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>

#include <ifaddrs.h>
#include <net/if.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

#include <sys/syscall.h>

#include <popcorn.h>
#include "message.h"
#include "local_io.h"
#include <server.h>

#define POPCORN_KILLD_PORT 51239

#define OPT_KILL (1 << 0)
#define OPT_CLIENT (1 << 1)

#define BACKLOG 3

int daemonize = 1;
int verbose = 0;
int port = POPCORN_KILLD_PORT;

void
print_help (char *execname)
{
  printf ("%s options:\n", execname);
  printf ("  -c -> client mode; send kill message to server");
  printf ("  -i [string] -> ip address of the server\n");
  printf ("  -k -> terminate an active daemon\n");
  printf ("  -p [int] -> port to listen for signal requests");
  printf ("  -s -> run in standalone mode, without spawning a daemon\n");
  printf ("  -S [int] -> signal to send\n");
  printf ("  -v -> emit verbose diagnostics\n");
  printf ("  -h -> print usage\n");
}

void
terminate_daemon (void)
{

}

int
init_network (void)
{
  uint32_t myip = pcn_get_ip ();
  int sockfd = -1;
  struct addrinfo hints, *ai;
  int res;
  char buf[INET_ADDRSTRLEN], *s_addr = NULL, *s_port = NULL;

  memset (&hints, 0, sizeof (hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  snprintf (buf, INET_ADDRSTRLEN, "%d", port);
  s_port = buf;

  if ((res = getaddrinfo (s_addr, s_port, &hints, &ai)) != 0)
    lio_error ("getaddrinfo: %s\n", gai_strerror (res));

  assert (ai != NULL && ai->ai_family == AF_INET);

  sockfd = socket (ai->ai_family, ai->ai_socktype, ai->ai_protocol);
  if (sockfd < 0)
    {
      perror ("socket");
      lio_exit (EXIT_FAILURE);
    }

  if (bind (sockfd, ai->ai_addr, ai->ai_addrlen) == -1)
    {
      lio_close (sockfd);
      perror ("bind");
      lio_exit (EXIT_FAILURE);
    }

  if (listen (sockfd, 2) == -1) {
    perror ("listen");
    lio_exit (EXIT_FAILURE);
  }

  freeaddrinfo (ai);

  return sockfd;
}

void
do_kill (void)
{
  
}

int
main (int argc, char *argv[])
{
  int c;
  int options = 0;
  char *hostaddr = NULL;
  int sig;
  int sockfd;

  while ((c = getopt (argc, argv, "cki:hp:sS:v")) != -1)
    switch (c)
      {
      case 'c':
        options |= OPT_CLIENT;
        break;

      case 'k':
        options |= OPT_KILL;
        break;

      case 'i':
        hostaddr = strdup (optarg);
        break;

      case 'p':
        port = atoi (optarg);
        break;

      case 's':
        daemonize = 0;
        break;

      case 'S':
        sig = atoi (optarg);
        break;

      case 'v':
        verbose = 1;
        break;

      case 'h':
      default:
        print_help (argv[0]);
        exit (EXIT_SUCCESS);
      }

if (options & OPT_KILL)
  {
    terminate_daemon ();
    exit (EXIT_SUCCESS);
  }

if (daemonize && fork () != 0)
  exit (EXIT_SUCCESS);

sockfd = init_network ();

do_kill ();

  return 0;
}
