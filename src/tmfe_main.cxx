/*******************************************************************\

  Name:         tmfe_main.cxx
  Created by:   K.Olchanski

  Contents:     Main program for TMFE frontends

\********************************************************************/

#undef NDEBUG // midas required assert() to be always enabled

#include <stdio.h>
#include <signal.h> // signal(), SIGPIPE, etc
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
   fprintf(stderr, "\n");
   fprintf(stderr, "Usage: %s args... -- [equipment args...]\n", argv0);
   fprintf(stderr, "\n");
   fprintf(stderr, " -v -- set the TMFE verbose flag to report all major activity\n");
   fprintf(stderr, " -h -- print this help message\n");
   fprintf(stderr, " --help -- print this help message\n");
   fprintf(stderr, "\n");
   fprintf(stderr, " -h hostname[:port] -- connect to MIDAS mserver on given host and port number\n");
   fprintf(stderr, " -e exptname -- connect to given MIDAS experiment\n");
   fprintf(stderr, "\n");
   exit(1);
}

int main(int argc, char* argv[])
{
   setbuf(stdout, NULL);
   setbuf(stderr, NULL);

   signal(SIGPIPE, SIG_IGN);

   std::vector<std::string> args;
   for (int i=0; i<argc; i++) {
      args.push_back(argv[i]);
   }

   std::vector<std::string> eq_args;

   bool help = false;
   std::string exptname;
   std::string hostname;

   for (unsigned int i=1; i<args.size(); i++) { // loop over the commandline options
      //printf("argv[%d] is %s\n", i, args[i].c_str());
      if (args[i] == "--") {
         // remaining arguments are passed to equipment Init()
         for (unsigned j=i+1; j<args.size(); j++)
            eq_args.push_back(args[j]);
         break;
      } else if (args[i] == "-v") {
         TMFE::gfVerbose = true;
      } else if (args[i] == "-h") {
         i++;
         if (i >= args.size()) { help = true; break; }
         hostname = args[i];
      } else if (args[i] == "-e") {
         i++;
         if (i >= args.size()) { help = true; break; }
         exptname = args[i];
      } else if (args[i] == "--help") {
         help = true;
         break;
      } else if (args[i][0] == '-') {
         help = true;
         break;
      } else {
         help = true;
         break;
      }
   }

   TMFE* mfe = TMFE::Instance();

   // call pre-connect hook before calling Usage(). Otherwise, if user creates
   // new equipments inside the hook, they will never see it's Usage(). K.O.
   mfe->CallPreConnectHooks(eq_args);

   if (help) {
      usage(argv[0]);
      mfe->Usage();
   }

   TMFeResult r = mfe->Connect(NULL, __FILE__, hostname.c_str(), exptname.c_str());
   if (r.error_flag) {
      fprintf(stderr, "Cannot connect to MIDAS, error message: %s, bye.\n", r.error_message.c_str());
      return 1;
   }

   mfe->CallPostConnectHooks(eq_args);

   //mfe->SetWatchdogSec(0);
   //mfe->SetTransitionSequenceStart(910);
   //mfe->SetTransitionSequenceStop(90);
   //mfe->DeregisterTransitionPause();
   //mfe->DeregisterTransitionResume();
   //mfe->RegisterTransitionStartAbort();

   r = mfe->InitEquipments(eq_args);

   if (r.error_flag) {
      fprintf(stderr, "Cannot initialize equipments, error message: %s, bye.\n", r.error_message.c_str());
      return 1;
   }

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
