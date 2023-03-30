#include <stdlib.h>
#include <sys/types.h>
#include <message.h>
#include <popcorn.h>
#include <remote_io.h>

extern void check_migrate (void *, void *);

void
rio_poll (struct pcn_msg_hdr *hdr)
{
  int status;

  while (1)
    {
      rio_msg_get (pcn_data->pcn_server_sockfd, &status, sizeof (int));
      //rio_dbg_printf ("%s: received %d\n", __FUNCTION__, status);

      if (status == PCN_POLL_RIO_COMPLETE)
	break;
      else if (rio_signal_pending ())
	{
          rio_dbg_printf ("%s: detected a signal\n", __FUNCTION__);
	  hdr->msg_kind = PCN_POLL_STOP;
	  rio_msg_send (pcn_data->pcn_server_sockfd, hdr);
          break;
	}
      else
	{
	  rio_enable_signals ();
	  check_migrate (NULL, NULL);
	  rio_disable_signals ();

	  hdr->msg_kind = PCN_POLL_RESUME;
	  rio_msg_send (pcn_data->pcn_server_sockfd, hdr);
	}
    }
}
