//
// tmfe_example.cxx
//
// Example tmfe c++ frontend with a periodic equipment
//

#include <stdio.h>
#include <signal.h> // SIGPIPE
#include <assert.h> // assert()
#include <stdlib.h> // malloc()
#include <math.h> // M_PI

#include "midas.h"
#include "tmfe.h"

class Myfe :
   public TMFeRpcHandlerInterface,
   public TMFePeriodicHandlerInterface   
{
public:
   TMFE* fMfe = NULL;
   TMFeEquipment* fEq = NULL;

   Myfe(TMFE* mfe, TMFeEquipment* eq) // ctor
   {
      fMfe = mfe;
      fEq  = eq;
   }

   ~Myfe() // dtor
   {
   }

   void SendData(double dvalue)
   {
      char buf[1024];
      fEq->ComposeEvent(buf, sizeof(buf));
      fEq->BkInit(buf, sizeof(buf));
         
      double* ptr = (double*)fEq->BkOpen(buf, "test", TID_DOUBLE);
      *ptr++ = dvalue;
      fEq->BkClose(buf, ptr);

      fEq->SendEvent(buf);
   }

   TMFeResult HandleRpc(const char* cmd, const char* args, std::string& response)
   {
      fMfe->Msg(MINFO, "HandleRpc", "RPC cmd [%s], args [%s]", cmd, args);
      return TMFeOk();
   }

   TMFeResult HandleBeginRun()
   {
      fMfe->Msg(MINFO, "HandleBeginRun", "Begin run!");
      fEq->SetStatus("Running", "#00FF00");
      return TMFeOk();
   }

   TMFeResult HandleEndRun()
   {
      fMfe->Msg(MINFO, "HandleEndRun", "End run!");
      fEq->SetStatus("Stopped", "#00FF00");
      return TMFeOk();
   }

   //void HandleStartAbortRun()
   //{
   //   fMfe->Msg(MINFO, "HandleStartAbortRun", "Begin run aborted!");
   //   fEq->SetStatus("Stopped", "#00FF00");
   //}

   void HandlePeriodic()
   {
      printf("periodic!\n");
      double t = TMFE::GetTime();
      double data = 100.0*sin(M_PI*t/60);
      SendData(data);
      fEq->fOdbEqVariables->WD("data", data);
      fEq->WriteStatistics();
      char status_buf[256];
      sprintf(status_buf, "value %.1f", data);
      fEq->SetStatus(status_buf, "#00FF00");
   }
};

static void usage()
{
   fprintf(stderr, "Usage: tmfe_example ...\n");
   exit(1);
}

int main(int argc, char* argv[])
{
   setbuf(stdout, NULL);
   setbuf(stderr, NULL);

   signal(SIGPIPE, SIG_IGN);

   //std::string name = "";
   //
   //if (argc == 2) {
   //   name = argv[1];
   //} else {
   //   usage(); // DOES NOT RETURN
   //}

   TMFE* mfe = TMFE::Instance();

   TMFeResult result = mfe->Connect("tmfe_example", __FILE__);
   if (result.error_flag) {
      fprintf(stderr, "Cannot connect to MIDAS, error \"%s\", bye.\n", result.error_message.c_str());
      return 1;
   }

   //mfe->SetWatchdogSec(0);

   TMFeCommon *common = new TMFeCommon();
   common->Period  = 1000;
   common->EventID = 1;
   common->LogHistory = 1;
   //common->Buffer = "SYSTEM";
   
   TMFeEquipment* eq = new TMFeEquipment(mfe, "tmfe_example", common);
   eq->Init();
   eq->SetStatus("Starting...", "white");
   eq->ZeroStatistics();
   eq->WriteStatistics();

   mfe->RegisterEquipment(eq);

   Myfe* myfe = new Myfe(mfe, eq);

   mfe->RegisterRpcHandler(myfe);

   //mfe->SetTransitionSequenceStart(910);
   //mfe->SetTransitionSequenceStop(90);
   //mfe->DeregisterTransitionPause();
   //mfe->DeregisterTransitionResume();
   //mfe->RegisterTransitionStartAbort();

   mfe->RegisterPeriodicHandler(eq, myfe);

   eq->SetStatus("Started...", "white");

   while (!mfe->fShutdownRequested) {
      mfe->PollMidas(10);
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
