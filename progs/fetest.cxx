/*******************************************************************\

  Name:         fetest.cxx
  Created by:   K.Olchanski

  Contents:     Front end for testing MIDAS

\********************************************************************/

#undef NDEBUG // midas required assert() to be always enabled

#include <stdio.h>
#include <signal.h> // SIGPIPE
#include <assert.h> // assert()
#include <stdlib.h> // malloc()
#include <math.h> // M_PI

#include "midas.h"
#include "tmfe.h"

#include <string> // std::string
#include <thread> // std::thread

/*-- Equipment list ------------------------------------------------*/

#if 0
#define EVID_TEST 1
#define EVID_SLOW 2
#define EVID_RANDOM 3
#endif

class EqRandom :
   public TMFeEquipment,
   public TMFePeriodicHandlerInterface   
{
public:
   EqRandom(TMFE* mfe, const char* name, TMFeCommon* common)
      : TMFeEquipment(mfe, name, common)
   {


   }
   
   void HandlePeriodic()
   {
      char event[fMaxEventSize];

      ComposeEvent(event, fMaxEventSize);
      
      char* pbh = event + sizeof(EVENT_HEADER);
      
      double r = drand48();
      if (r < 0.3)
         bk_init(pbh);
      else if (r < 0.6)
         bk_init32(pbh);
      else
         bk_init32a(pbh);
      
      int nbank = 0+9*drand48(); // nbank range: 0..9, see how bank names are generated: RND0..RND9
      
      for (int i=0; i<nbank; i++) {
         int tid = 1+(TID_LAST-1)*drand48();
         int size = 0+100*drand48();

         //int total = bk_size(pbh);
         //printf("total %d, add %d, max %d\n", total, size, (int)fMaxEventSize);

         char name[5];
         name[0] = 'R';
         name[1] = 'N';
         name[2] = 'D';
         name[3] = '0' + (nbank-i-1);
         name[4] = 0;
         
         char* ptr;
         bk_create(pbh, name, tid, (void**)&ptr);
         
         for (int j=0; j<size; j++)
            ptr[j] = (nbank-i-1);
         
         bk_close(pbh, ptr + size);
      }

      //printf("sending %d, max %d\n", (int)bk_size(pbh), (int)fMaxEventSize);

      SendEvent(event);
      WriteEventToOdb(event);
   }

#if 0
  { "random"   ,         /* equipment name */
    {
      EVID_RANDOM, (1<<EVID_RANDOM),           /* event ID, trigger mask */
      "SYSTEM",             /* event buffer */
      EQ_PERIODIC,          /* equipment type */
      0,                    /* event source */
      "MIDAS",              /* format */
      TRUE,                 /* enabled */
      RO_RUNNING,           /* Read when running */
      100,                  /* poll every so milliseconds */
      0,                    /* stop run after this event limit */
      0,                    /* number of sub events */
      0,                    /* history period */
      "", "", ""
    },
    read_random_event,/* readout routine */
  },
#endif

};

class EqSlow :
   public TMFeEquipment,
   public TMFePeriodicHandlerInterface   
{
public:
   EqSlow(TMFE* mfe, const char* name, TMFeCommon* common)
      : TMFeEquipment(mfe, name, common)
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

      SendEvent(buf);
   }

   void HandlePeriodic()
   {
      //printf("EqSlow::HandlePeriodic!\n");
      double t = TMFE::GetTime();
      double data = 100.0*sin(-M_PI/2.0+M_PI*t/60);
      SendData(data);
      fOdbEqVariables->WD("data", data);
      WriteStatistics();
      char status_buf[256];
      sprintf(status_buf, "value %.1f", data);
      SetStatus(status_buf, "#00FF00");
   }

#if 0
  { "slow"   ,         /* equipment name */
    {
      EVID_SLOW, (1<<EVID_SLOW),           /* event ID, trigger mask */
      "SYSTEM",             /* event buffer */
      EQ_PERIODIC,          /* equipment type */
      0,                    /* event source */
      "MIDAS",              /* format */
      TRUE,                 /* enabled */
      RO_ALWAYS,            /* Read when running */
      1000,                 /* poll every so milliseconds */
      0,                    /* stop run after this event limit */
      0,                    /* number of sub events */
      1,                    /* history period */
      "", "", ""
    },
    read_slow_event,/* readout routine */
  },
#endif
};

class TMFeEquipmentBase
{
public:
   TMFE* fMfe = NULL;
   TMFeEquipment* fEq = NULL;

public:
   TMFeEquipmentBase(const char* name, TMFeCommon* common) // ctor
   {
      fMfe = TMFE::Instance();
      fEq  = new TMFeEquipment(fMfe, name, common);
   }

   virtual ~TMFeEquipmentBase() // dtor
   {
      if (fMfe)
         fMfe = NULL;
      if (fEq) {
         delete fEq;
         fEq = NULL;
      }
   }

   virtual TMFeResult Init(const std::vector<std::string>& args) = 0;
   virtual void Usage() { };

private:
   TMFeEquipmentBase() // default ctor
   {
      assert(!"TMFeEquipmentBase: default constructor is not permitted!");
   }
};

class EqBulk :
   public TMFeEquipmentBase,
   public TMFeRpcHandlerInterface
{
public: // configuration
   int fEventSize = 0;
   double fEventSleep = 1.0;
   std::vector<char> fEventBuffer;

public: // internal state
   bool fThreadRunning = false;
   
public:
   EqBulk(const char* name, TMFeCommon* common) // ctor
      : TMFeEquipmentBase(name, common)
   {


   }

   ~EqBulk() // dtor
   {
      // wait for thread to finish
      while (fThreadRunning) {
         fMfe->Sleep(0.1);
      }
   }

   TMFeResult Init(const std::vector<std::string>& args)
   {
      fEq->SetStatus("Starting...", "white");

      fEq->fOdbEqSettings->RI("event_size", &fEventSize, true);
      fEq->fOdbEqSettings->RD("event_sleep_sec", &fEventSleep, true);

      printf("Event size set to %d bytes\n", fEventSize);

      fEventBuffer.resize(16+8+16+fEventSize+100);

      //size = sizeof(test_rb_wait_sleep);
      //status = db_get_value(hDB, hSet, "rb_wait_sleep", &test_rb_wait_sleep, &size, TID_DWORD, TRUE);
      //assert(status == DB_SUCCESS);
      //printf("Ring buffer wait sleep %d ms\n", test_rb_wait_sleep);

      new std::thread(&EqBulk::Thread, this);
      
      return TMFeOk();
   }

   void SendEvent()
   {
      char* buf = fEventBuffer.data();
      size_t bufsize = fEventBuffer.size();
      fEq->ComposeEvent(buf, bufsize);
      fEq->BkInit(buf, bufsize);
         
      char* ptr = (char*)fEq->BkOpen(buf, "bulk", TID_BYTE);
      ptr += fEventSize;
      fEq->BkClose(buf, ptr);

      fEq->SendEvent(buf);
      fEq->WriteStatistics();
   }

   void Thread()
   {
      printf("FeBulk::Thread: thread started\n");

      fEq->SetStatus("Thread running", "#00FF00");

      fThreadRunning = true;
      while (!fMfe->fShutdownRequested) {
         fMfe->Sleep(fEventSleep);
         SendEvent();
      }
      
      printf("FeBulk::Thread: thread finished\n");
      fThreadRunning = false;
   }

#if 0
   EQUIPMENT equipment[] = {

  { "test"   ,         /* equipment name */
    {
      EVID_TEST, (1<<EVID_TEST),           /* event ID, trigger mask */
      "SYSTEM",             /* event buffer */
      EQ_USER,              /* equipment type */
      0,                    /* event source */
      "MIDAS",              /* format */
      TRUE,                 /* enabled */
      RO_RUNNING,           /* Read when running */
      100,                  /* poll every so milliseconds */
      0,                    /* stop run after this event limit */
      0,                    /* number of sub events */
      0,                    /* no history */
      "", "", ""
    },
    read_test_event,/* readout routine */
  },
  { "" }
};
#endif
};
   
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

   TMFeResult HandleRpc(const char* cmd, const char* args, std::string& response)
   {
      fMfe->Msg(MINFO, "HandleRpc", "RPC cmd [%s], args [%s]", cmd, args);

      // RPC handler
      
      //int example_int = strtol(args, NULL, 0);
      //int size = sizeof(int);
      //int status = db_set_value(hDB, 0, "/Equipment/" EQ_NAME "/Settings/example_int", &example_int, size, 1, TID_INT);
      
      char tmp[256];
      time_t now = time(NULL);
      sprintf(tmp, "{ \"current_time\" : [ %d, \"%s\"] }", (int)now, ctime(&now));
      
      response = tmp;

      return TMFeOk();
   }

   TMFeResult HandleBeginRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandleBeginRun", "Begin run %d!", run_number);
      fEq->SetStatus("Running", "#00FF00");
      
      printf("begin_of_run %d\n", run_number);
      
      int fail = 0;
      fEq->fOdbEqSettings->RI("fail_begin_of_run", &fail, true);
      
      if (fail) {
         printf("fail_begin_of_run: returning error status %d\n", fail);
         return TMFeErrorMessage("begin of run failed by ODB setting!");
      }
      
      int s = 0;
      fEq->fOdbEqSettings->RI("sleep_begin_of_run", &s, true);
      
      if (s) {
         printf("sleep_begin_of_run: calling ss_sleep(%d)\n", s);
         ss_sleep(s);
      }
      
      return TMFeOk();
   }

   TMFeResult HandleEndRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandleEndRun", "End run %d!", run_number);
      fEq->SetStatus("Stopped", "#00FF00");

      printf("end_of_run %d\n", run_number);
      
      int fail = 0;
      fEq->fOdbEqSettings->RI("fail_end_of_run", &fail, true);
      
      if (fail) {
         printf("fail_end_of_run: returning error status %d\n", fail);
         return TMFeResult(fail, "end of run failed by ODB setting!");
      }
      
      int s = 0;
      fEq->fOdbEqSettings->RI("sleep_end_of_run", &s, true);
      
      if (s) {
         printf("sleep_end_of_run: calling ss_sleep(%d)\n", s);
         ss_sleep(s);
      }
      
      return TMFeOk();
   }

   TMFeResult HandlePauseRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandlePauseRun", "Pause run %d!", run_number);
      fEq->SetStatus("Stopped", "#00FF00");

      printf("pause_run %d\n", run_number);

      int fail = 0;
      fEq->fOdbEqSettings->RI("fail_pause_run", &fail, true);
      
      if (fail) {
         printf("fail_pause_run: returning error status %d\n", fail);
         return TMFeResult(fail, "pause run failed by ODB setting!");
      }
      
      return TMFeOk();
   }

   TMFeResult HandleResumeRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandleResumeRun", "Resume run %d!", run_number);
      fEq->SetStatus("Stopped", "#00FF00");

      printf("resume_run %d\n", run_number);

      int fail = 0;
      fEq->fOdbEqSettings->RI("fail_resume_run", &fail, true);
      
      if (fail) {
         printf("fail_resume_run: returning error status %d\n", fail);
         return TMFeResult(fail, "resume run failed by ODB setting!");
      }
      
      return TMFeOk();
   }

   TMFeResult HandleStartAbortRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandleStartAbortRun", "Begin run %d aborted!", run_number);
      fEq->SetStatus("Stopped", "#00FF00");

      printf("start abort run %d\n", run_number);

      int fail = 0;
      fEq->fOdbEqSettings->RI("fail_start_abort", &fail, true);
      
      if (fail) {
         printf("fail_start_abort: returning error status %d\n", fail);
         return TMFeResult(fail, "start abort failed by ODB setting!");
      }
      
      return TMFeOk();
   }

   void HandlePeriodic()
   {
   }
};

static void usage()
{
   fprintf(stderr, "Usage: fetest ...\n");
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

   TMFeResult result = mfe->Connect("fetest", __FILE__);
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
   
   TMFeEquipment* eq = new TMFeEquipment(mfe, "test_rpc", common);
   eq->Init();
   eq->SetStatus("Starting...", "white");
   eq->ZeroStatistics();
   eq->WriteStatistics();

   mfe->RegisterEquipment(eq);

   Myfe* myfe = new Myfe(mfe, eq);

   TMFeCommon *cor = new TMFeCommon();
   cor->Period  = 1000;
   cor->EventID = 2;
   cor->LogHistory = 0;
   //common->Buffer = "SYSTEM";

   EqRandom* eqr = new EqRandom(mfe, "test_random", cor);
   eqr->Init();
   eqr->SetStatus("Starting...", "white");
   eqr->ZeroStatistics();
   eqr->WriteStatistics();

   mfe->RegisterEquipment(eqr);

   TMFeCommon *cos = new TMFeCommon();
   cos->Period  = 1000;
   cos->EventID = 3;
   cos->LogHistory = 1;
   //common->Buffer = "SYSTEM";

   EqSlow* eqs = new EqSlow(mfe, "test_slow", cos);
   eqs->Init();
   eqs->SetStatus("Starting...", "white");
   eqs->ZeroStatistics();
   eqs->WriteStatistics();

   mfe->RegisterEquipment(eqs);

   TMFeCommon *cob = new TMFeCommon();
   cob->Period  = 1000;
   cob->EventID = 3;
   cob->LogHistory = 1;
   //common->Buffer = "SYSTEM";

   EqBulk* eqb = new EqBulk("test_bulk", cos);
   eqb->fEq->Init();
   eqb->fEq->ZeroStatistics();
   eqb->fEq->WriteStatistics();
   eqb->Init(eq_args);

   mfe->RegisterEquipment(eqb->fEq);

   //mfe->SetTransitionSequenceStart(910);
   //mfe->SetTransitionSequenceStop(90);
   //mfe->DeregisterTransitionPause();
   //mfe->DeregisterTransitionResume();
   mfe->RegisterTransitionStartAbort();

   mfe->RegisterRpcHandler(myfe);
   //mfe->RegisterRpcHandler(eqr);

   mfe->RegisterPeriodicHandler(eq, myfe);
   mfe->RegisterPeriodicHandler(eq, eqr);
   mfe->RegisterPeriodicHandler(eq, eqs);

   eq->SetStatus("Started...", "white");

   while (!mfe->fShutdownRequested) {
      mfe->PollMidas(10);
   }

   delete eqb;
   eqb = NULL;

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
