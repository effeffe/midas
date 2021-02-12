//
// fetest_tmfe_thread.cxx
//
// Frontend for test and example of multithreaded tmfe c++ frontend
//

#include <stdio.h>
#include <signal.h> // SIGPIPE
#include <assert.h> // assert()
#include <stdlib.h> // malloc()
#include <unistd.h> // sleep()

#include "midas.h"
#include "tmfe.h"

class Myfe :
   public TMFeRpcHandlerInterface,
   public  TMFePeriodicHandlerInterface   
{
public:
   TMFE* fMfe;
   TMFeEquipment* fEq;

   int fEventSize;
   char* fEventBuf;

   Myfe(TMFE* mfe, TMFeEquipment* eq) // ctor
   {
      fMfe = mfe;
      fEq  = eq;
      fEventSize = 0;
      fEventBuf  = NULL;
   }

   ~Myfe() // dtor
   {
      if (fEventBuf) {
         free(fEventBuf);
         fEventBuf = NULL;
      }
   }

   void Init()
   {
      fEventSize = 100;
      fEq->fOdbEqSettings->RI("event_size", &fEventSize, true);
      if (fEventBuf) {
         free(fEventBuf);
      }
      fEventBuf = (char*)malloc(fEventSize);
   }

   void SendEvent(double dvalue)
   {
      fEq->ComposeEvent(fEventBuf, fEventSize);
      fEq->BkInit(fEventBuf, fEventSize);
         
      double* ptr = (double*)fEq->BkOpen(fEventBuf, "test", TID_DOUBLE);
      *ptr = dvalue;
      fEq->BkClose(fEventBuf, ptr+1);

      fEq->SendEvent(fEventBuf);
   }

   TMFeResult HandleRpc(const char* cmd, const char* args, std::string& response)
   {
      fMfe->Msg(MINFO, "HandleRpc", "Thread %s, RPC cmd [%s], args [%s]", TMFE::GetThreadId().c_str(), cmd, args);
      return TMFeOk();
   }

   TMFeResult HandleBeginRun()
   {
      fMfe->Msg(MINFO, "HandleBeginRun", "Thread %s, Begin run!", TMFE::GetThreadId().c_str());
      fEq->SetStatus("Running", "#00FF00");
      return TMFeOk();
   }

   TMFeResult HandleEndRun()
   {
      fMfe->Msg(MINFO, "HandleEndRun", "Thread %s, End run!", TMFE::GetThreadId().c_str());
      fEq->SetStatus("Stopped", "#00FF00");
      return TMFeOk();
   }

   void HandlePeriodic()
   {
      printf("Thread %s, periodic!\n", TMFE::GetThreadId().c_str());
      //char buf[256];
      //sprintf(buf, "buffered %d (max %d), dropped %d, unknown %d, max flushed %d", gUdpPacketBufSize, fMaxBuffered, fCountDroppedPackets, fCountUnknownPackets, fMaxFlushed);
      //fEq->SetStatus(buf, "#00FF00");
      //fEq->WriteStatistics();
   }
};

static void usage()
{
   fprintf(stderr, "Usage: fetest_tmfe_thread ...\n");
   exit(1);
}

int main(int argc, char* argv[])
{
   setbuf(stdout, NULL);
   setbuf(stderr, NULL);

   signal(SIGPIPE, SIG_IGN);

   std::string name = "";

   if (argc == 2) {
      name = argv[1];
   } else {
      usage(); // DOES NOT RETURN
   }

   TMFE* mfe = TMFE::Instance();

   TMFeResult result = mfe->Connect("fetest_tmfe_thread", __FILE__);
   if (result.error_flag) {
      printf("Cannot connect, bye.\n");
      return 1;
   }

   //mfe->SetWatchdogSec(0);

   TMFeCommon *common = new TMFeCommon();
   common->EventID = 1;
   common->LogHistory = 1;
   //common->Buffer = "SYSTEM";
   
   TMFeEquipment* eq = new TMFeEquipment(mfe, "tmfe", common);
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

   myfe->Init();

   mfe->RegisterPeriodicHandler(eq, myfe);

   printf("Main thread is %s\n", TMFE::GetThreadId().c_str());

   mfe->StartRpcThread();
   mfe->StartPeriodicThread();

   eq->SetStatus("Started...", "white");

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
