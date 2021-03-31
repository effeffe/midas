//
// tmfe_example_multithread.cxx
//
// Example tmfe multithreaded c++ frontend
//

#include <stdio.h>
#include <signal.h> // SIGPIPE
#include <assert.h> // assert()
#include <stdlib.h> // malloc()
#include <unistd.h> // sleep()
#include <math.h> // M_PI

#include "midas.h"
#include "tmfe.h"

class Myfe :
   public TMFeEquipment
{
public:
   Myfe(const char* eqname, const char* eqfilename) // ctor
      : TMFeEquipment(eqname, eqfilename)
   {
   }

   ~Myfe() // dtor
   {
   }

   void SendData(double dvalue)
   {
      char buf[1024];
      ComposeEvent(buf, sizeof(buf));
      BkInit(buf, sizeof(buf));
         
      double* ptr = (double*)BkOpen(buf, "test", TID_DOUBLE);
      *ptr++ = dvalue;
      BkClose(buf, ptr);

      EqSendEvent(buf);
   }

   TMFeResult HandleRpc(const char* cmd, const char* args, std::string& response)
   {
      fMfe->Msg(MINFO, "HandleRpc", "Thread %s, RPC cmd [%s], args [%s]", TMFE::GetThreadId().c_str(), cmd, args);
      return TMFeOk();
   }

   TMFeResult HandleBeginRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandleBeginRun", "Thread %s, Begin run %d!", TMFE::GetThreadId().c_str(), run_number);
      EqSetStatus("Running", "#00FF00");
      return TMFeOk();
   }

   TMFeResult HandleEndRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandleEndRun", "Thread %s, End run %d!", TMFE::GetThreadId().c_str(), run_number);
      EqSetStatus("Stopped", "#00FF00");
      return TMFeOk();
   }

   //TMFeResult HandleStartAbortRun(int run_number)
   //{
   //   fMfe->Msg(MINFO, "HandleStartAbortRun", "Begin run %d aborted!", run_number);
   //   EqSetStatus("Stopped", "#00FF00");
   //   return TMFeOk();
   //}

   void HandlePeriodic()
   {
      printf("Thread %s, periodic!\n", TMFE::GetThreadId().c_str());
      double t = TMFE::GetTime();
      double data = 100.0*sin(M_PI/2.0+M_PI*t/60);
      SendData(data);
      fOdbEqVariables->WD("data", data);
      char status_buf[256];
      sprintf(status_buf, "value %.1f", data);
      EqSetStatus(status_buf, "#00FF00");
   }
};

static void usage()
{
   fprintf(stderr, "Usage: tmfe_example_mt ...\n");
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

   TMFeResult result = mfe->Connect("tmfe_example_mt", __FILE__);
   if (result.error_flag) {
      fprintf(stderr, "Cannot connect to MIDAS, error \"%s\", bye.\n", result.error_message.c_str());
      return 1;
   }

   //mfe->SetWatchdogSec(0);

   TMFeEquipment* eq = new Myfe("tmfe_example_mt", __FILE__);
   eq->fEqConfEventID = 1;
   eq->fEqConfLogHistory = 1;
   eq->fEqConfPeriodMilliSec = 1000; // milliseconds
   eq->fEqConfBuffer = "SYSTEM";

   eq->EqInit(eq_args);
   eq->EqSetStatus("Starting...", "white");

   mfe->AddRpcHandler(eq);

   //mfe->SetTransitionSequenceStart(910);
   //mfe->SetTransitionSequenceStop(90);

   //mfe->DeregisterTransitionPause();
   //mfe->DeregisterTransitionResume();

   //mfe->RegisterTransitionStartAbort();

   printf("Main thread is %s\n", TMFE::GetThreadId().c_str());

   mfe->StartRpcThread();
   //mfe->StartPeriodicThread();

   eq->EqSetStatus("Started...", "white");

   while (!mfe->fShutdownRequested) {
      ::sleep(1);
   }

   mfe->Disconnect();

   return 0;
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
