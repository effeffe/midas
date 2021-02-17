/*******************************************************************\

  Name:         fetest.cxx
  Created by:   K.Olchanski

  Contents:     Front end for testing MIDAS

\********************************************************************/

#undef NDEBUG // midas required assert() to be always enabled

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>

#include "midas.h"

///* maximum event size produced by this frontend */
//INT max_event_size      = 4*1024*1024;

///* buffer size to hold events */
//INT event_buffer_size = 10*1024*1024;

int read_test_event(char *pevent, int off);
//int read_slow_event(char *pevent, int off);

/*-- Equipment list ------------------------------------------------*/

#if 0
#define EVID_TEST 1
#define EVID_SLOW 2
#define EVID_RANDOM 3

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

/********************************************************************\
              Callback routines for system transitions

  These routines are called whenever a system transition like start/
  stop of a run occurs. The routines are called on the following
  occations:

  frontend_init:  When the frontend program is started. This routine
                  should initialize the hardware.
  
  frontend_exit:  When the frontend program is shut down. Can be used
                  to releas any locked resources like memory, commu-
                  nications ports etc.

  begin_of_run:   When a new run is started. Clear scalers, open
                  rungates, etc.

  end_of_run:     Called on a request to stop a run. Can send 
                  end-of-run event and close run gates.

  pause_run:      When a run is paused. Should disable trigger events.

  resume_run:     When a run is resumed. Should enable trigger events.

\********************************************************************/

int event_size = 10*1024;

/*-- Frontend Init -------------------------------------------------*/

//HNDLE hSet;
//int test_rb_wait_sleep = 1;

void configure()
{
#if 0
   int size, status;

   size = sizeof(event_size);
   status = db_get_value(hDB, hSet, "event_size", &event_size, &size, TID_DWORD, TRUE);
   assert(status == DB_SUCCESS);

   printf("Event size set to %d bytes\n", event_size);

   size = sizeof(test_rb_wait_sleep);
   status = db_get_value(hDB, hSet, "rb_wait_sleep", &test_rb_wait_sleep, &size, TID_DWORD, TRUE);
   assert(status == DB_SUCCESS);

   printf("Ring buffer wait sleep %d ms\n", test_rb_wait_sleep);
#endif
}

#include "msystem.h"

int test_run_number = 0;
int test_rb_wait_count = 0;
int test_event_count = 0;
int test_rbh = 0;

#if 0
int test_thread(void *param)
{
   int status;
   EVENT_HEADER *pevent;
   void *p;
   EQUIPMENT* eq = &equipment[0];

   /* indicate activity to framework */
   signal_readout_thread_active(test_rbh, 1);

   while (!stop_all_threads) {
      if (test_run_number == 0) {
         // no run, wait
         ss_sleep(10);
         continue;
      }

      /* obtain buffer space */
      status = rb_get_wp(get_event_rbh(test_rbh), &p, 0);
      if (stop_all_threads)
         break;
      if (status == DB_TIMEOUT) {
         test_rb_wait_count ++;
         //printf("readout_thread: Ring buffer is full, waiting for space!\n");
         if (test_rb_wait_sleep)
            ss_sleep(test_rb_wait_sleep);
         continue;
      }
      if (status != DB_SUCCESS) {
         // catastrophic failure of ring buffer?
         break;
      }

      if (eq->info.period) {
         ss_sleep(eq->info.period);
      }

      if (1 /*readout_enabled()*/) {
        
         /* check for new event */
         //source = poll_event(multithread_eq->info.source, multithread_eq->poll_count, FALSE);

         if (1 /*source > 0*/) {

            if (stop_all_threads)
               break;

            pevent = (EVENT_HEADER *)p;
            
            /* compose MIDAS event header */
            pevent->event_id = eq->info.event_id;
            pevent->trigger_mask = eq->info.trigger_mask;
            pevent->data_size = 0;
            pevent->time_stamp = ss_time();
            pevent->serial_number = eq->serial_number++;

            /* call user readout routine */
            pevent->data_size = eq->readout((char *) (pevent + 1), 0);

            /* check event size */
            if (pevent->data_size + sizeof(EVENT_HEADER) > (DWORD) max_event_size) {
               cm_msg(MERROR, "readout_thread",
                        "Event size %ld larger than maximum size %d",
                        (long) (pevent->data_size + sizeof(EVENT_HEADER)),
                        max_event_size);
               assert(FALSE);
            }

            if (pevent->data_size > 0) {
               /* put event into ring buffer */
               rb_increment_wp(get_event_rbh(test_rbh), sizeof(EVENT_HEADER) + pevent->data_size);
            } else
               eq->serial_number--;
         }

      } else // readout_enabled
        ss_sleep(10);
   }

   signal_readout_thread_active(test_rbh, 0);

   return 0;
}
#endif

#if 0
INT frontend_init()
{
   int status, size;
   char str[256];
   printf("frontend_init!\n");

   // create settings tree
   size = sizeof(str);
   strlcpy(str, "some value", size);
   status = db_get_value(hDB, 0, "/equipment/test/Settings/some_setting", str, &size, TID_STRING, TRUE);
   assert(status == DB_SUCCESS);

   // save reference to settings tree
   status = db_find_key(hDB, 0, "/equipment/test/Settings", &hSet);
   assert(status == DB_SUCCESS);

   create_event_rb(test_rbh);
   ss_thread_create(test_thread, 0);

   //cm_set_watchdog_params (FALSE, 0);

   //set_rate_period(1000);

   configure();
   
   return SUCCESS;
}
#endif

/*-- Event readout -------------------------------------------------*/

int read_test_event(char *pevent, int off)
{
   bk_init32(pevent);

   char* pdata8;
   
   bk_create(pevent, "TEST", TID_BYTE, (void**)&pdata8);
   
   pdata8 += event_size;
   
   bk_close(pevent, pdata8);
   
   test_event_count ++;
  
   return bk_size(pevent);
}

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
      
      configure();
      
      int s = 0;
      fEq->fOdbEqSettings->RI("sleep_begin_of_run", &s, true);
      
      if (s) {
         printf("sleep_begin_of_run: calling ss_sleep(%d)\n", s);
         ss_sleep(s);
      }
      
      test_event_count = 0;
      test_rb_wait_count = 0;
      test_run_number = run_number; // tell thread to start running
      
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
      
      test_run_number = 0; // tell thread to stop running
      
      int s = 0;
      fEq->fOdbEqSettings->RI("sleep_end_of_run", &s, true);
      
      if (s) {
         printf("sleep_end_of_run: calling ss_sleep(%d)\n", s);
         ss_sleep(s);
      }
      
      printf("test_event_count: %d events sent, ring buffer wait count %d\n", test_event_count, test_rb_wait_count);
      
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
      
      test_run_number = 0; // tell thread to stop running

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
      
      test_run_number = run_number; // tell thread to start running

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
      
      test_run_number = run_number; // tell thread to start running

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
   
   TMFeEquipment* eq = new TMFeEquipment(mfe, "test", common);
   eq->Init();
   eq->SetStatus("Starting...", "white");
   eq->ZeroStatistics();
   eq->WriteStatistics();

   mfe->RegisterEquipment(eq);

   Myfe* myfe = new Myfe(mfe, eq);

   TMFeCommon *cor = new TMFeCommon();
   common->Period  = 1000;
   common->EventID = 2;
   common->LogHistory = 0;
   //common->Buffer = "SYSTEM";

   EqRandom* eqr = new EqRandom(mfe, "test_random", cor);
   eqr->Init();
   eqr->SetStatus("Starting...", "white");
   eqr->ZeroStatistics();
   eqr->WriteStatistics();

   mfe->RegisterEquipment(eqr);

   TMFeCommon *cos = new TMFeCommon();
   common->Period  = 1000;
   common->EventID = 3;
   common->LogHistory = 1;
   //common->Buffer = "SYSTEM";

   EqSlow* eqs = new EqSlow(mfe, "test_slow", cos);
   eqs->Init();
   eqs->SetStatus("Starting...", "white");
   eqs->ZeroStatistics();
   eqs->WriteStatistics();

   mfe->RegisterEquipment(eqs);

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
