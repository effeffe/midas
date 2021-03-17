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

class EqRandom :
   public TMFeEquipment
{
public:
   EqRandom(const char* eqname, const char* eqfilename) // ctor
      : TMFeEquipment(eqname, eqfilename)
   {
      fEqConfEventID = 2;
      fEqConfPeriodMilliSec = 1000;
      fEqConfLogHistory = 0;
      fEqConfWriteEventsToOdb = true;
      fEqConfReadOnlyWhenRunning = true;
   }
   
   TMFeResult HandleInit(const std::vector<std::string>& args)
   {
      fEqConfReadOnlyWhenRunning = true;
      fEqConfWriteEventsToOdb = true;
      fEqConfLogHistory = 0;
      return TMFeOk();
   }
   
   void HandlePeriodic()
   {
      char event[fEqMaxEventSize];

      ComposeEvent(event, fEqMaxEventSize);
      
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

      EqSendEvent(event);
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

static TMFeRegister eq_random_register("fetest", new EqRandom("test_random", __FILE__));

class EqSlow :
   public TMFeEquipment
{
public:
   EqSlow(const char* eqname, const char* eqfilename) // ctor
      : TMFeEquipment(eqname, eqfilename)
   {
      fEqConfEventID = 3;
      fEqConfPeriodMilliSec = 1000;
      fEqConfLogHistory = 1;
      fEqConfWriteEventsToOdb = true;
   }

   TMFeResult HandleInit(const std::vector<std::string>& args)
   {
      fEqConfReadOnlyWhenRunning = false;
      fEqConfWriteEventsToOdb = true;
      //fEqConfLogHistory = 1;
      return TMFeOk();
   }
   
   void SendData(double dvalue)
   {
      char buf[1024];
      ComposeEvent(buf, sizeof(buf));
      BkInit(buf, sizeof(buf));
         
      double* ptr = (double*)BkOpen(buf, "data", TID_DOUBLE);
      *ptr++ = dvalue;
      BkClose(buf, ptr);

      EqSendEvent(buf);
   }

   void HandlePeriodic()
   {
      //printf("EqSlow::HandlePeriodic!\n");
      double t = TMFE::GetTime();
      double data = 100.0*sin(-M_PI/2.0+M_PI*t/60);
      SendData(data);
      char status_buf[256];
      sprintf(status_buf, "value %.1f", data);
      EqSetStatus(status_buf, "#00FF00");
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

static TMFeRegister eq_slow_register("fetest", new EqSlow("test_slow", __FILE__));

class EqBulk :
   public TMFeEquipment
{
public: // configuration
   int fEventSize = 0;
   double fEventSleep = 1.0;
   std::vector<char> fEventBuffer;

public: // internal state
   bool fThreadRunning = false;
   
public:
   EqBulk(const char* eqname, const char* eqfilename) // ctor
      : TMFeEquipment(eqname, eqfilename)
   {
      fEqConfPeriodMilliSec = 1000;
      fEqConfEventID = 3;
      fEqConfReadOnlyWhenRunning = true;
   }

   ~EqBulk() // dtor
   {
      // wait for thread to finish
      while (fThreadRunning) {
         fMfe->Sleep(0.1);
      }
   }

   TMFeResult HandleInit(const std::vector<std::string>& args)
   {
      EqSetStatus("Starting...", "white");

      fEqConfReadOnlyWhenRunning = true;
      fEqConfWriteEventsToOdb = false;

      fOdbEqSettings->RI("event_size", &fEventSize, true);
      fOdbEqSettings->RD("event_sleep_sec", &fEventSleep, true);

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
      ComposeEvent(buf, bufsize);
      BkInit(buf, bufsize);
         
      char* ptr = (char*)BkOpen(buf, "bulk", TID_BYTE);
      ptr += fEventSize;
      BkClose(buf, ptr);

      EqSendEvent(buf);
   }

   void Thread()
   {
      printf("FeBulk::Thread: thread started\n");

      EqSetStatus("Thread running", "#00FF00");

      fThreadRunning = true;
      while (!fMfe->fShutdownRequested) {
         if (fMfe->fStateRunning || !fEqConfReadOnlyWhenRunning) {
            fMfe->Sleep(fEventSleep);
            SendEvent();
         } else {
            fMfe->Sleep(1.0);
         }
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

static TMFeRegister eq_bulk_register("fetest", new EqBulk("test_bulk", __FILE__));
   
class EqRpc :
   public TMFeEquipment
{
public:
   EqRpc(const char* eqname, const char* eqfilename) // ctor
      : TMFeEquipment(eqname, eqfilename)
   {
      fEqConfEventID = 1;
   }

   ~EqRpc() // dtor
   {
   }

   TMFeResult HandleInit(const std::vector<std::string>& args)
   {
      fEqConfBuffer = "SYSTEM";
      EqSetStatus("Started...", "white");
      return TMFeOk();
   }

   TMFeResult HandleRpc(const char* cmd, const char* args, std::string& response)
   {
      fMfe->Msg(MINFO, "HandleRpc", "RPC cmd [%s], args [%s]", cmd, args);

      // RPC handler
      
      char tmp[256];
      time_t now = time(NULL);
      sprintf(tmp, "{ \"current_time\" : [ %d, \"%s\"] }", (int)now, ctime(&now));
      
      response = tmp;

      return TMFeOk();
   }

   TMFeResult HandleBeginRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandleBeginRun", "Begin run %d!", run_number);
      EqSetStatus("Running", "#00FF00");
      
      printf("begin_of_run %d\n", run_number);
      
      int fail = 0;
      fOdbEqSettings->RI("fail_begin_of_run", &fail, true);
      
      if (fail) {
         printf("fail_begin_of_run: returning error status %d\n", fail);
         return TMFeErrorMessage("begin of run failed by ODB setting!");
      }
      
      int s = 0;
      fOdbEqSettings->RI("sleep_begin_of_run", &s, true);
      
      if (s) {
         printf("sleep_begin_of_run: calling ss_sleep(%d)\n", s);
         ss_sleep(s);
      }
      
      return TMFeOk();
   }

   TMFeResult HandleEndRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandleEndRun", "End run %d!", run_number);
      EqSetStatus("Stopped", "#00FF00");

      printf("end_of_run %d\n", run_number);
      
      int fail = 0;
      fOdbEqSettings->RI("fail_end_of_run", &fail, true);
      
      if (fail) {
         printf("fail_end_of_run: returning error status %d\n", fail);
         return TMFeResult(fail, "end of run failed by ODB setting!");
      }
      
      int s = 0;
      fOdbEqSettings->RI("sleep_end_of_run", &s, true);
      
      if (s) {
         printf("sleep_end_of_run: calling ss_sleep(%d)\n", s);
         ss_sleep(s);
      }
      
      return TMFeOk();
   }

   TMFeResult HandlePauseRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandlePauseRun", "Pause run %d!", run_number);
      EqSetStatus("Stopped", "#00FF00");

      printf("pause_run %d\n", run_number);

      int fail = 0;
      fOdbEqSettings->RI("fail_pause_run", &fail, true);
      
      if (fail) {
         printf("fail_pause_run: returning error status %d\n", fail);
         return TMFeResult(fail, "pause run failed by ODB setting!");
      }
      
      return TMFeOk();
   }

   TMFeResult HandleResumeRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandleResumeRun", "Resume run %d!", run_number);
      EqSetStatus("Stopped", "#00FF00");

      printf("resume_run %d\n", run_number);

      int fail = 0;
      fOdbEqSettings->RI("fail_resume_run", &fail, true);
      
      if (fail) {
         printf("fail_resume_run: returning error status %d\n", fail);
         return TMFeResult(fail, "resume run failed by ODB setting!");
      }
      
      return TMFeOk();
   }

   TMFeResult HandleStartAbortRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandleStartAbortRun", "Begin run %d aborted!", run_number);
      EqSetStatus("Stopped", "#00FF00");

      printf("start abort run %d\n", run_number);

      int fail = 0;
      fOdbEqSettings->RI("fail_start_abort", &fail, true);
      
      if (fail) {
         printf("fail_start_abort: returning error status %d\n", fail);
         return TMFeResult(fail, "start abort failed by ODB setting!");
      }
      
      return TMFeOk();
   }
};

static TMFeRegister eq_rpc_register("fetest", new EqRpc("test_rpc", __FILE__));

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
