/*******************************************************************\

  Name:         tmfe_main.cxx
  Created by:   K.Olchanski

  Contents:     Main program for TMFE frontends

\********************************************************************/

#undef NDEBUG // midas required assert() to be always enabled

//#include <stdio.h>
//#include <stdlib.h>
//#include <unistd.h>
//#include <stdint.h>
//#include <sys/time.h>
//#include <sys/types.h>
//#include <sys/stat.h>
//#include <fcntl.h>
//#include <errno.h>
//#include <math.h>
//#include <ctype.h>
//#include <assert.h>
//#include <string.h>

#include "tmfe.h"

static void usage(const char* argv0)
{
   fprintf(stderr, "Usage: %s ...\n", argv0);
   exit(1);
}

int main(int argc, char* argv[])
{
   setbuf(stdout, NULL);
   setbuf(stderr, NULL);

   signal(SIGPIPE, SIG_IGN);

   std::vector<std::string> eq_args;

   //std::string name = "";
   //
   //if (argc == 2) {
   //   name = argv[1];
   //} else {
   //   usage(); // DOES NOT RETURN
   //}

   TMFE* mfe = TMFE::Instance();

   // call pre-connect hook before calling Usage(). Otherwise, if user creates
   // new equipments inside the hook, they will never see it's Usage(). K.O.
   mfe->CallPreConnectHooks(eq_args);

   if (0) {
      usage(argv[0]);
      mfe->Usage();
   }

   TMFeResult result = mfe->Connect(NULL, __FILE__);
   if (result.error_flag) {
      fprintf(stderr, "Cannot connect to MIDAS, error \"%s\", bye.\n", result.error_message.c_str());
      return 1;
   }

   mfe->CallPostConnectHooks(eq_args);

   //mfe->SetWatchdogSec(0);
   //mfe->SetTransitionSequenceStart(910);
   //mfe->SetTransitionSequenceStop(90);
   //mfe->DeregisterTransitionPause();
   //mfe->DeregisterTransitionResume();
   //mfe->RegisterTransitionStartAbort();

   mfe->InitEquipments(eq_args);

   while (!mfe->fShutdownRequested) {
      mfe->PollMidas(10);
   }

   mfe->CallPreDisconnectHooks();
   mfe->DeleteEquipments();
   mfe->Disconnect();
   mfe->CallPostDisconnectHooks();

   return 0;
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
