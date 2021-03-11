/********************************************************************\

  Name:         tmfe.cxx
  Created by:   Konstantin Olchanski - TRIUMF

  Contents:     C++ MIDAS frontend

\********************************************************************/

#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <sys/time.h> // gettimeofday()

#include "tmfe.h"

#include "midas.h"
#include "msystem.h"
#include "mrpc.h"

//////////////////////////////////////////////////////////////////////
// error handling
//////////////////////////////////////////////////////////////////////

TMFeResult TMFeErrorMessage(const std::string& message)
{
   return TMFeResult(0, message);
}

TMFeResult TMFeMidasError(const std::string& message, const char* midas_function_name, int midas_status)
{
   return TMFeResult(midas_status, message + msprintf(", %s() status %d", midas_function_name, midas_status));
}

//////////////////////////////////////////////////////////////////////
// TMFE singleton class
//////////////////////////////////////////////////////////////////////


TMFE::TMFE() // ctor
{
   if (gfVerbose)
      printf("TMFE::ctor!\n");
   fDB = 0;
   fOdbRoot = NULL;
   fShutdownRequested = false;
   fNextPeriodic = 0;

   fRpcThreadStarting = false;
   fRpcThreadRunning = false;
   fRpcThreadShutdownRequested = false;

   fPeriodicThreadStarting = false;
   fPeriodicThreadRunning = false;
   fPeriodicThreadShutdownRequested = false;
}

TMFE::~TMFE() // dtor
{
   if (gfVerbose)
      printf("TMFE::dtor!\n");
   assert(!"TMFE::~TMFE(): destruction of the TMFE singleton is not permitted!");
}

TMFE* TMFE::Instance()
{
   if (!gfMFE)
      gfMFE = new TMFE();
   
   return gfMFE;
}

TMFeResult TMFE::Connect(const char* xprogname, const char* filename, const char* hostname, const char* exptname)
{
   if (xprogname)
      fFrontendName     = xprogname;
   if (filename)
      fFrontendFilename = filename;

   fFrontendHostname = ss_gethostname();

   if (fFrontendName.empty()) {
      return TMFeErrorMessage("TMFE::Connect: frontend name is not set");
   }

   int status;
  
   std::string env_hostname;
   std::string env_exptname;
   
   /* get default from environment */
   status = cm_get_environment(&env_hostname, &env_exptname);

   if (status != CM_SUCCESS) {
      return TMFeMidasError("Cannot connect to MIDAS", "cm_get_environment", status);
   }

   if (hostname && hostname[0]) {
      fHostname = hostname;
   } else {
      fHostname = env_hostname;
   }
   
   if (exptname && exptname[0]) {
      fExptname = exptname;
   } else {
      fExptname = env_exptname;
   }

   if (gfVerbose) {
      printf("TMFE::Connect: Program \"%s\" connecting to experiment \"%s\" on host \"%s\"\n", fFrontendName.c_str(), fExptname.c_str(), fHostname.c_str());
   }
   
   int watchdog = DEFAULT_WATCHDOG_TIMEOUT;
   //int watchdog = 60*1000;
   
   status = cm_connect_experiment1(fHostname.c_str(), fExptname.c_str(), fFrontendName.c_str(), NULL, DEFAULT_ODB_SIZE, watchdog);
   
   if (status == CM_UNDEF_EXP) {
      return TMFeMidasError(msprintf("Cannot connect to MIDAS, experiment \"%s\" is not defined", fExptname.c_str()), "cm_connect_experiment1", status);
   } else if (status != CM_SUCCESS) {
      return TMFeMidasError("Cannot connect to MIDAS", "cm_connect_experiment1", status);
   }

   status = cm_get_experiment_database(&fDB, NULL);
   if (status != CM_SUCCESS) {
      return TMFeMidasError("Cannot connect to MIDAS", "cm_get_experiment_database", status);
   }

   fOdbRoot = MakeMidasOdb(fDB);

   if (gfVerbose) {
      printf("TMFE::Connect: Program \"%s\" connected to experiment \"%s\" on host \"%s\"\n", fFrontendName.c_str(), fExptname.c_str(), fHostname.c_str());
   }

   return TMFeOk();
}

TMFeResult TMFE::SetWatchdogSec(int sec)
{
   if (sec == 0) {
      cm_set_watchdog_params(false, 0);
   } else {
      cm_set_watchdog_params(true, sec*1000);
   }
   return TMFeOk();
}

TMFeResult TMFE::Disconnect()
{
   if (gfVerbose)
      printf("TMFE::Disconnect: Disconnecting from experiment \"%s\" on host \"%s\"\n", fExptname.c_str(), fHostname.c_str());
   StopRpcThread();
   StopPeriodicThread();
   cm_disconnect_experiment();
   if (gfVerbose)
      printf("TMFE::Disconnect: Disconnected from experiment \"%s\" on host \"%s\"\n", fExptname.c_str(), fHostname.c_str());
   return TMFeOk();
}

void TMFE::RegisterEquipment(TMFeEquipment* eq)
{
   for (unsigned i=0; i<fEquipments.size(); i++) {
      if (fEquipments[i] == eq)
         return;
   }
   fEquipments.push_back(eq);
}

void TMFE::UnregisterEquipment(TMFeEquipment* eq)
{
   for (unsigned i=0; i<fEquipments.size(); i++) {
      if (fEquipments[i] == eq) {
         fEquipments[i] = NULL;
         return;
      }
   }
}

class TMFeHandlerData
{
 public:
   TMFeEquipment *fEq = NULL;
   TMFeHandlerInterface *fHandler = NULL;
   bool fEqEnableRpc = false;
   bool fEqEnablePeriodic = false;
   bool fEqEnablePoll = false;

public: // periodic
   double fEqPeriodicLastCallTime = 0;
   double fEqPeriodicNextCallTime = 0;

 public:
   TMFeHandlerData()
   {
      // empty
   }
   
   ~TMFeHandlerData()
   {
      fEq = NULL; // no delete, we do not own this object
      fHandler = NULL; // no delete, we do not own this object
   }
};

void TMFE::EquipmentPeriodicTasks()
{
   double now = GetTime();

   // run periodic equipments

   if (fNextPeriodic == 0 || now >= fNextPeriodic) {
      int n = fHandlers.size();
      fNextPeriodic = 0;
      for (int i=0; i<n; i++) {
         TMFeHandlerData* h = fHandlers[i];
         if (!h->fEqEnablePeriodic)
            continue;
         double period = h->fEq->fInfo->Period/1000.0;
         //printf("periodic[%d] period %f, last call %f, next call %f (+%f)\n", i, period, h->fLastCallTime, h->fNextCallTime, now - h->fNextCallTime);
         if (period <= 0)
            continue;
         if (h->fEqPeriodicNextCallTime == 0 || now >= h->fEqPeriodicNextCallTime) {
            h->fEqPeriodicLastCallTime = now;
            h->fEqPeriodicNextCallTime = h->fEqPeriodicLastCallTime + period;

            if (h->fEqPeriodicNextCallTime < now) {
               if (gfVerbose)
                  printf("TMFE::EquipmentPeriodicTasks: periodic equipment does not keep up!\n");
               while (h->fEqPeriodicNextCallTime < now) {
                  h->fEqPeriodicNextCallTime += period;
               }
            }

            if (fNextPeriodic == 0)
               fNextPeriodic = h->fEqPeriodicNextCallTime;
            else if (h->fEqPeriodicNextCallTime < fNextPeriodic)
               fNextPeriodic = h->fEqPeriodicNextCallTime;

            if (fStateRunning || !h->fEq->fInfo->ReadOnlyWhenRunning) {
               //printf("handler %d eq [%s] call HandlePeriodic()\n", i, h->fEq->fName.c_str());                     
               h->fHandler->HandlePeriodic();
            }

            now = GetTime();
         }
      }

      //printf("next periodic %f (+%f)\n", fNextPeriodic, fNextPeriodic - now);
   } else {
      //printf("next periodic %f (+%f), waiting\n", fNextPeriodic, fNextPeriodic - now);
   }

   now = GetTime();

   // update statistics
   for (auto e : fEquipments) {
      if (e) {
         if (now > e->fStatNextWrite) {
            e->WriteStatistics();
         }
      }
   }

}

void TMFE::EquipmentPollTasks()
{
   // NOTE: ok to use range-based for() loop, there will be a crash if HandlePoll() or HandleRead() modify fHandlers, so they should not do that. K.O.
   for (auto hd : fHandlers) {
      if (hd && hd->fEqEnablePoll && hd->fEq) {
         bool poll = hd->fHandler->HandlePoll();
         if (poll) {
            hd->fHandler->HandleRead();
         }
      }
   }
}

void TMFE::PollMidas(int msec)
{
   double now = GetTime();
   //double sleep_start = now;
   double sleep_end = now + msec/1000.0;

   while (!fShutdownRequested) {
      if (!fPeriodicThreadRunning) {
         EquipmentPeriodicTasks();
      }

      EquipmentPollTasks();

      now = GetTime();

      double sleep_time = sleep_end - now;
      int s = 0;
      if (sleep_time > 0)
         s = 1 + sleep_time*1000.0;

      //printf("now %f, sleep_end %f, s %d\n", now, sleep_end, s);
      
      int status = cm_yield(s);
      
      if (status == RPC_SHUTDOWN || status == SS_ABORT) {
         fShutdownRequested = true;
         if (gfVerbose)
            printf("TMFE::PollMidas: cm_yield(%d) status %d, shutdown requested...\n", msec, status);
      }

      now = GetTime();
      if (now >= sleep_end)
         break;
   }

   //printf("TMFE::PollMidas: msec %d, actual %f msec\n", msec, (now - sleep_start) * 1000.0);
}

void TMFE::MidasPeriodicTasks()
{
   cm_periodic_tasks();
}

static int tmfe_rpc_thread(void* param)
{
   if (TMFE::gfVerbose)
      printf("tmfe_rpc_thread: RPC thread started\n");

   int msec = 1000;
   TMFE* mfe = TMFE::Instance();
   mfe->fRpcThreadRunning = true;
   ss_suspend_set_rpc_thread(ss_gettid());
   while (!mfe->fShutdownRequested && !mfe->fRpcThreadShutdownRequested) {

      int status = cm_yield(msec);

      if (status == RPC_SHUTDOWN || status == SS_ABORT) {
         mfe->fShutdownRequested = true;
         if (TMFE::gfVerbose)
            printf("tmfe_rpc_thread: cm_yield(%d) status %d, shutdown requested...\n", msec, status);
      }
   }
   ss_suspend_exit();
   if (TMFE::gfVerbose)
      printf("tmfe_rpc_thread: RPC thread stopped\n");
   mfe->fRpcThreadRunning = false;
   return SUCCESS;
}

static int tmfe_periodic_thread(void* param)
{
   if (TMFE::gfVerbose)
      printf("tmfe_periodic_thread: periodic thread started\n");
   
   TMFE* mfe = TMFE::Instance();
   mfe->fPeriodicThreadRunning = true;
   while (!mfe->fShutdownRequested && !mfe->fPeriodicThreadShutdownRequested) {
      mfe->EquipmentPeriodicTasks();
      int status = ss_suspend(1000, 0);
      if (status == RPC_SHUTDOWN || status == SS_ABORT || status == SS_EXIT) {
         mfe->fShutdownRequested = true;
         if (TMFE::gfVerbose)
            printf("tmfe_periodic_thread: ss_susend() status %d, shutdown requested...\n", status);
      }
   }
   ss_suspend_exit();
   if (TMFE::gfVerbose)
      printf("tmfe_periodic_thread: periodic thread stopped\n");
   mfe->fPeriodicThreadRunning = false;
   return SUCCESS;
}

void TMFE::StartRpcThread()
{
   if (fRpcThreadRunning || fRpcThreadStarting) {
      if (gfVerbose)
         printf("TMFE::StartRpcThread: RPC thread already running\n");
      return;
   }
   
   fRpcThreadStarting = true;
   ss_thread_create(tmfe_rpc_thread, NULL);
}

void TMFE::StartPeriodicThread()
{
   if (fPeriodicThreadRunning || fPeriodicThreadStarting) {
      if (gfVerbose)
         printf("TMFE::StartPeriodicThread: periodic thread already running\n");
      return;
   }

   fPeriodicThreadStarting = true;
   ss_thread_create(tmfe_periodic_thread, NULL);
}

void TMFE::StopRpcThread()
{
   if (!fRpcThreadRunning)
      return;
   
   fRpcThreadStarting = false;
   fRpcThreadShutdownRequested = true;
   for (int i=0; i<60; i++) {
      if (!fRpcThreadRunning)
         break;
      if (i>5) {
         fprintf(stderr, "TMFE::StopRpcThread: waiting for RPC thread to stop\n");
      }
      ::sleep(1);
   }

   if (fRpcThreadRunning) {
      fprintf(stderr, "TMFE::StopRpcThread: timeout waiting for RPC thread to stop\n");
   }
}

void TMFE::StopPeriodicThread()
{
   if (!fPeriodicThreadRunning)
      return;
   
   fPeriodicThreadStarting = false;
   fPeriodicThreadShutdownRequested = true;
   for (int i=0; i<60; i++) {
      if (!fPeriodicThreadRunning)
         break;
      if (i>5) {
         fprintf(stderr, "TMFE::StopPeriodicThread: waiting for periodic thread to stop\n");
      }
      ::sleep(1);
   }

   if (fPeriodicThreadRunning) {
      fprintf(stderr, "TMFE::StopPeriodicThread: timeout waiting for periodic thread to stop\n");
   }
}

void TMFE::Msg(int message_type, const char *filename, int line, const char *routine, const char *format, ...)
{
   char message[1024];
   //printf("format [%s]\n", format);
   va_list ap;
   va_start(ap, format);
   vsnprintf(message, sizeof(message)-1, format, ap);
   va_end(ap);
   //printf("message [%s]\n", message);
   cm_msg(message_type, filename, line, routine, "%s", message);
   cm_msg_flush_buffer();
}

double TMFE::GetTime()
{
   struct timeval tv;
   gettimeofday(&tv, NULL);
   return tv.tv_sec*1.0 + tv.tv_usec/1000000.0;
}

#if 1
void TMFE::Sleep(double time)
{
   int status;
   fd_set fdset;
   struct timeval timeout;
      
   FD_ZERO(&fdset);
      
   timeout.tv_sec = time;
   timeout.tv_usec = (time-timeout.tv_sec)*1000000.0;
      
   status = select(1, &fdset, NULL, NULL, &timeout);
   
   //#ifdef EINTR
   //if (status < 0 && errno == EINTR) {
   //   return 0; // watchdog interrupt, try again
   //}
   //#endif
      
   if (status < 0) {
      TMFE::Instance()->Msg(MERROR, "TMFE::Sleep", "select() returned %d, errno %d (%s)", status, errno, strerror(errno));
   }
}
#endif

#if 0
void TMFE::Sleep(double time)
{
   struct timespec rqtp;
   struct timespec rmtp;
      
   rqtp.tv_sec = time;
   rqtp.tv_nsec = (time-rqtp.tv_sec)*1000000000.0;

   int status = nanosleep(&rqtp, &rmtp);
   
   //#ifdef EINTR
   //if (status < 0 && errno == EINTR) {
   //   return 0; // watchdog interrupt, try again
   //}
   //#endif
      
   if (status < 0) {
      TMFE::Instance()->Msg(MERROR, "TMFE::Sleep", "nanosleep() returned %d, errno %d (%s)", status, errno, strerror(errno));
   }
}
#endif

#if 0
void TMFE::Sleep(double time)
{
   struct timespec rqtp;
   struct timespec rmtp;
      
   rqtp.tv_sec = time;
   rqtp.tv_nsec = (time-rqtp.tv_sec)*1000000000.0;

   //int status = clock_nanosleep(CLOCK_REALTIME, 0, &rqtp, &rmtp);
   int status = clock_nanosleep(CLOCK_MONOTONIC, 0, &rqtp, &rmtp);
   
   //#ifdef EINTR
   //if (status < 0 && errno == EINTR) {
   //   return 0; // watchdog interrupt, try again
   //}
   //#endif
      
   if (status < 0) {
      TMFE::Instance()->Msg(MERROR, "TMFE::Sleep", "nanosleep() returned %d, errno %d (%s)", status, errno, strerror(errno));
   }
}
#endif

std::string TMFE::GetThreadId() ///< return identification of this thread
{
   return ss_tid_to_string(ss_gettid());
}

static INT rpc_callback(INT index, void *prpc_param[])
{
   const char* cmd  = CSTRING(0);
   const char* args = CSTRING(1);
   char* return_buf = CSTRING(2);
   int   return_max_length = CINT(3);

   if (TMFE::gfVerbose)
      printf("TMFE::rpc_callback: index %d, max_length %d, cmd [%s], args [%s]\n", index, return_max_length, cmd, args);

   TMFE* mfe = TMFE::Instance();

   // NOTE: cannot use range-based for() loop, it uses an iterator and will crash if HandleRpc() modifies fHandlers. K.O.
   for (unsigned i=0; i<mfe->fHandlers.size(); i++) {
      if (!mfe->fHandlers[i]->fEqEnableRpc)
         continue;
      std::string result = "";
      TMFeResult r = mfe->fHandlers[i]->fHandler->HandleRpc(cmd, args, result);
      if (result.length() > 0) {
         //printf("Handler reply [%s]\n", C(r));
         strlcpy(return_buf, result.c_str(), return_max_length);
         return RPC_SUCCESS;
      }
   }

   return_buf[0] = 0;
   return RPC_SUCCESS;
}

static INT tr_start(INT run_number, char *errstr)
{
   if (TMFE::gfVerbose)
      printf("TMFE::tr_start!\n");

   TMFE* mfe = TMFE::Instance();

   mfe->fRunNumber = run_number;
   mfe->fStateRunning = true;
   
   for (unsigned i=0; i<mfe->fEquipments.size(); i++) {
      if (mfe->fEquipments[i] == NULL)
         continue;
      mfe->fEquipments[i]->ZeroStatistics();
      mfe->fEquipments[i]->WriteStatistics();
   }

   TMFeResult result;

   // NOTE: cannot use range-based for() loop, it uses an iterator and will crash if HandleBeginRun() modifies fHandlers. K.O.
   for (unsigned i=0; i<mfe->fHandlers.size(); i++) {
      if (!mfe->fHandlers[i]->fEqEnableRpc)
         continue;
      result = mfe->fHandlers[i]->fHandler->HandleBeginRun(run_number);
      if (result.error_flag) {
         // error handling in this function matches general transition error handling:
         // on run start, the first user handler to return an error code
         // will abort the transition. This leaves everything in an
         // inconsistent state: frontends called before the abort
         // think the run is running, which it does not. They should register
         // a handler for the "start abort" transition. This transition calls
         // all already started frontends so they can cleanup their state. K.O.
         // 
         break;
      }
   }

   if (result.error_flag) {
      strlcpy(errstr, result.error_message.c_str(), TRANSITION_ERROR_STRING_LENGTH);
      return FE_ERR_DRIVER;
   }

   return SUCCESS;
}

static INT tr_stop(INT run_number, char *errstr)
{
   if (TMFE::gfVerbose)
      printf("TMFE::tr_stop!\n");

   TMFeResult result;

   TMFE* mfe = TMFE::Instance();

   // NOTE: cannot use range-based for() loop, it uses an iterator and will crash if HandleEndRun() modifies fHandlers. K.O.
   for (unsigned i=0; i<mfe->fHandlers.size(); i++) {
      if (!mfe->fHandlers[i]->fEqEnableRpc)
         continue;
      TMFeResult xresult = mfe->fHandlers[i]->fHandler->HandleEndRun(run_number);
      if (xresult.error_flag) {
         // error handling in this function matches general transition error handling:
         // the "run stop" transition is always sucessful, the run always stops.
         // if some frontend returns an error, this error is remembered and is returned
         // as the transition over all status. K.O.
         result = xresult;
      }
   }

   for (unsigned i=0; i<mfe->fEquipments.size(); i++) {
      if (mfe->fEquipments[i] == NULL)
         continue;
      mfe->fEquipments[i]->WriteStatistics();
   }

   mfe->fStateRunning = false;
   
   if (result.error_flag) {
      strlcpy(errstr, result.error_message.c_str(), TRANSITION_ERROR_STRING_LENGTH);
      return FE_ERR_DRIVER;
   }

   return SUCCESS;
}

static INT tr_pause(INT run_number, char *errstr)
{
   cm_msg(MINFO, "tr_pause", "tr_pause");

   TMFeResult result;

   TMFE* mfe = TMFE::Instance();

   // NOTE: cannot use range-based for() loop, it uses an iterator and will crash if HandlePauseRun() modifies fHandlers. K.O.
   for (unsigned i=0; i<mfe->fHandlers.size(); i++) {
      if (!mfe->fHandlers[i]->fEqEnableRpc)
         continue;
      result = mfe->fHandlers[i]->fHandler->HandlePauseRun(run_number);
      if (result.error_flag) {
         // error handling in this function matches general transition error handling:
         // logic is same as "start run"
         break;
      }
   }

   if (result.error_flag) {
      strlcpy(errstr, result.error_message.c_str(), TRANSITION_ERROR_STRING_LENGTH);
      return FE_ERR_DRIVER;
   }

   return SUCCESS;
}

static INT tr_resume(INT run_number, char *errstr)
{
   if (TMFE::gfVerbose)
      printf("TMFE::tr_resume!\n");

   TMFeResult result;

   TMFE* mfe = TMFE::Instance();

   // NOTE: cannot use range-based for() loop, it uses an iterator and will crash if HandleResumeRun() modifies fHandlers. K.O.
   for (unsigned i=0; i<mfe->fHandlers.size(); i++) {
      if (!mfe->fHandlers[i]->fEqEnableRpc)
         continue;
      result = mfe->fHandlers[i]->fHandler->HandleResumeRun(run_number);
      if (result.error_flag) {
         // error handling in this function matches general transition error handling:
         // logic is same as "start run"
         break;
      }
   }

   if (result.error_flag) {
      strlcpy(errstr, result.error_message.c_str(), TRANSITION_ERROR_STRING_LENGTH);
      return FE_ERR_DRIVER;
   }

   return SUCCESS;
}

static INT tr_startabort(INT run_number, char *errstr)
{
   if (TMFE::gfVerbose)
      printf("TMFE::tr_startabort!\n");

   TMFeResult result;

   TMFE* mfe = TMFE::Instance();

   // NOTE: cannot use range-based for() loop, it uses an iterator and will crash if HandleStartAbortRun() modifies fHandlers. K.O.
   for (unsigned i=0; i<mfe->fHandlers.size(); i++) {
      if (!mfe->fHandlers[i]->fEqEnableRpc)
         continue;
      result = mfe->fHandlers[i]->fHandler->HandleStartAbortRun(run_number);
      if (result.error_flag) {
         // error handling in this function matches general transition error handling:
         // logic is same as "start run"
         break;
      }
   }

   mfe->fStateRunning = false;

   if (result.error_flag) {
      strlcpy(errstr, result.error_message.c_str(), TRANSITION_ERROR_STRING_LENGTH);
      return FE_ERR_DRIVER;
   }

   return SUCCESS;
}

void TMFE::SetTransitionSequenceStart(int seqno)
{
   cm_set_transition_sequence(TR_START, seqno);
}

void TMFE::SetTransitionSequenceStop(int seqno)
{
   cm_set_transition_sequence(TR_STOP, seqno);
}

void TMFE::SetTransitionSequencePause(int seqno)
{
   cm_set_transition_sequence(TR_PAUSE, seqno);
}

void TMFE::SetTransitionSequenceResume(int seqno)
{
   cm_set_transition_sequence(TR_RESUME, seqno);
}

void TMFE::SetTransitionSequenceStartAbort(int seqno)
{
   cm_set_transition_sequence(TR_STARTABORT, seqno);
}

void TMFE::DeregisterTransitions()
{
   cm_deregister_transition(TR_START);
   cm_deregister_transition(TR_STOP);
   cm_deregister_transition(TR_PAUSE);
   cm_deregister_transition(TR_RESUME);
   cm_deregister_transition(TR_STARTABORT);
}

void TMFE::DeregisterTransitionStart()
{
   cm_deregister_transition(TR_START);
}

void TMFE::DeregisterTransitionStop()
{
   cm_deregister_transition(TR_STOP);
}

void TMFE::DeregisterTransitionPause()
{
   cm_deregister_transition(TR_PAUSE);
}

void TMFE::DeregisterTransitionResume()
{
   cm_deregister_transition(TR_RESUME);
}

void TMFE::DeregisterTransitionStartAbort()
{
   cm_deregister_transition(TR_STARTABORT);
}

void TMFE::RegisterTransitionStartAbort()
{
   cm_register_transition(TR_STARTABORT, tr_startabort, 500);
}

void TMFE::RegisterHandler(TMFeEquipment* eq, TMFeHandlerInterface* h, bool enable_rpc, bool enable_periodic, bool enable_poll)
{
   TMFeHandlerData *p = new TMFeHandlerData();
   p->fEq = eq;
   p->fHandler = h;
   p->fEqEnableRpc = enable_rpc;
   p->fEqEnablePeriodic = enable_periodic;
   p->fEqEnablePoll = enable_poll;
   // FIXME: fHandlers must be protected against multi-threaded access. K.O.
   fHandlers.push_back(p);
   fNextPeriodic = 0;
   fNextPoll = 0;

   static bool gOnce = true;

   if (enable_rpc && gOnce) {
      gOnce = false;
      // for the first handler, register with MIDAS
      cm_register_function(RPC_JRPC, rpc_callback);
      cm_register_transition(TR_START, tr_start, 500);
      cm_register_transition(TR_STOP, tr_stop, 500);
      cm_register_transition(TR_PAUSE, tr_pause, 500);
      cm_register_transition(TR_RESUME, tr_resume, 500);
      cm_register_transition(TR_STARTABORT, tr_startabort, 500);
   }
}

void TMFE::Usage()
{
   for (unsigned i=0; i<fEquipmentBases.size(); i++) {
      fEquipmentBases[i]->Usage();
   }
}

TMFeResult TMFE::InitEquipments(const std::vector<std::string>& args)
{
   for (unsigned i=0; i<fEquipmentBases.size(); i++) {
      TMFeResult r;
      r = fEquipmentBases[i]->fEq->Init1();
      if (r.error_flag)
         return r;
      r = fEquipmentBases[i]->Init(args);
      if (r.error_flag)
         return r;
      r = fEquipmentBases[i]->fEq->Init2();
      if (r.error_flag)
         return r;
   }
   return TMFeOk();
}

void TMFE::DeleteEquipments()
{
   for (unsigned i=0; i<fEquipmentBases.size(); i++) {
      UnregisterEquipment(fEquipmentBases[i]->fEq);
      delete fEquipmentBases[i];
      fEquipmentBases[i] = NULL;
   }

   for (unsigned i=0; i<fEquipments.size(); i++) {
      if (fEquipments[i] == NULL)
         continue;
      //printf("delete equipment [%s]\n", fEquipments[i]->fName.c_str());
      delete fEquipments[i];
      fEquipments[i] = NULL;
   }
}

TMFeResult TMFE::CreateEquipment(const char* eqname, const char* eqfile, TMFeEquipmentBase* eqbase, TMFeEqInfo* eqinfo)
{
   assert(eqname != NULL);
   
   for (unsigned i=0; i<fEquipments.size(); i++) {
      if (fEquipments[i] == NULL)
         continue;
      if (fEquipments[i]->fName == eqname) {
         return TMFeErrorMessage(std::string("Duplicate equipment name \"") + eqname + "\"");
      }
   }

   if (eqfile)
      eqinfo->FrontendFileName = eqfile;

   TMFeEquipment* eq = new TMFeEquipment(this, eqname, eqfile, eqinfo);
   eqbase->fMfe = this;
   eqbase->fEq = eq;
   fEquipmentBases.push_back(eqbase);
   RegisterEquipment(eq);
   return TMFeOk();
}

TMFeRegister::TMFeRegister(const char* fename, const char* eqname, const char* eqfile, TMFeEquipmentBase* eqbase, TMFeEqInfo* eqinfo)
{
   if (TMFE::gfVerbose)
      printf("TMFeRegister::ctor: Register equipment with fename [%s] eqname [%s] filename [%s]\n", fename, eqname, eqfile);
   TMFE* mfe = TMFE::Instance();
   if (fename) {
      if (mfe->fFrontendName.empty())
         mfe->fFrontendName = fename;
      else if (mfe->fFrontendName != fename) {
         fprintf(stderr, "TMFeRegister: Cannot register equipment \"%s\" with frontend name \"%s\" because TMFE frontend name is already set to \"%s\", sorry, bye!\n",
                 eqname, fename, mfe->fFrontendName.c_str());
         exit(1);
      }
   }
   mfe->CreateEquipment(eqname, eqfile, eqbase, eqinfo);
}

TMFeEquipment::TMFeEquipment(TMFE* mfe, const char* name, const char* filename, TMFeEqInfo* info) // ctor
{
   assert(mfe != NULL);
   assert(name != NULL);
   assert(filename != NULL);
   //assert(info != NULL);
   if (TMFE::gfVerbose)
      printf("TMFeEquipment::ctor: equipment name [%s] file [%s]\n", name, filename);
   fMfe  = mfe;
   fName = name;
   fFilename = filename;
   if (info)
      fInfo = info;
   else
      fInfo = new TMFeEqInfo;
   fBufferHandle = 0;
   fSerial = 0;
   fStatEvents = 0;
   fStatBytes  = 0;
   fStatEpS = 0;
   fStatKBpS = 0;
   fStatLastTime = 0;
   fStatLastEvents = 0;
   fStatLastBytes = 0;
   fOdbEq = NULL;
   fOdbEqCommon = NULL;
   fOdbEqSettings = NULL;
   fOdbEqVariables = NULL;

   fStatNextWrite = TMFE::GetTime();
}

TMFeEquipment::~TMFeEquipment() // dtor
{
   if (TMFE::gfVerbose)
      printf("TMFeEquipment::dtor: equipment name [%s]\n", fName.c_str());
   fMfe = NULL;
   if (fInfo) {
      delete fInfo;
      fInfo = NULL;
   }
}

TMFeResult TMFeEquipment::Init()
{
   TMFeResult r;

   r = Init1();
   if (r.error_flag)
      return r;

   r = Init2();
   if (r.error_flag)
      return r;

   return TMFeOk();
}

TMFeResult TMFeEquipment::ReadCommon()
{
   if (TMFE::gfVerbose)
      printf("TMFeEquipment::ReadCommon: for [%s]\n", fName.c_str());
   fOdbEqCommon->RU16("Event ID",     &fInfo->EventID, true);
   fOdbEqCommon->RU16("Trigger mask", &fInfo->TriggerMask, true);
   fOdbEqCommon->RS("Buffer",         &fInfo->Buffer, true, NAME_LENGTH);
   fOdbEqCommon->RI("Type",           &fInfo->Type, true);
   fOdbEqCommon->RI("Source",         &fInfo->Source, true);
   fOdbEqCommon->RS("Format",         &fInfo->Format, true, 8);
   fOdbEqCommon->RB("Enabled",        &fInfo->Enabled, true);
   fOdbEqCommon->RI("Read on",        &fInfo->ReadOn, true);
   fOdbEqCommon->RI("Period",         &fInfo->Period, true);
   fOdbEqCommon->RD("Event limit",    &fInfo->EventLimit, true);
   fOdbEqCommon->RU32("Num subevents",  &fInfo->NumSubEvents, true);
   fOdbEqCommon->RI("Log history",    &fInfo->LogHistory, true);
   fOdbEqCommon->RS("Frontend host",  &fInfo->FrontendHost, true, NAME_LENGTH);
   fOdbEqCommon->RS("Frontend name",  &fInfo->FrontendName, true, NAME_LENGTH);
   fOdbEqCommon->RS("Frontend file name",  &fInfo->FrontendFileName, true, 256);
   fOdbEqCommon->RS("Status",         &fInfo->Status, true, 256);
   fOdbEqCommon->RS("Status color",   &fInfo->StatusColor, true, NAME_LENGTH);
   fOdbEqCommon->RB("Hidden",         &fInfo->Hidden, true);
   fOdbEqCommon->RI("Write cache size", &fInfo->WriteCacheSize, true);
   return TMFeOk();
}

TMFeResult TMFeEquipment::WriteCommon() const
{
   if (TMFE::gfVerbose)
      printf("TMFeEquipment::WriteCommon: for [%s]\n", fName.c_str());
   fOdbEqCommon->WU16("Event ID",     fInfo->EventID);
   fOdbEqCommon->WU16("Trigger mask", fInfo->TriggerMask);
   fOdbEqCommon->WS("Buffer",         fInfo->Buffer.c_str(), NAME_LENGTH);
   fOdbEqCommon->WI("Type",           fInfo->Type);
   fOdbEqCommon->WI("Source",         fInfo->Source);
   fOdbEqCommon->WS("Format",         fInfo->Format.c_str(), 8);
   fOdbEqCommon->WB("Enabled",        fInfo->Enabled);
   fOdbEqCommon->WI("Read on",        fInfo->ReadOn);
   fOdbEqCommon->WI("Period",         fInfo->Period);
   fOdbEqCommon->WD("Event limit",    fInfo->EventLimit);
   fOdbEqCommon->WU32("Num subevents",  fInfo->NumSubEvents);
   fOdbEqCommon->WI("Log history",    fInfo->LogHistory);
   fOdbEqCommon->WS("Frontend host",  fInfo->FrontendHost.c_str(), NAME_LENGTH);
   fOdbEqCommon->WS("Frontend name",  fInfo->FrontendName.c_str(), NAME_LENGTH);
   fOdbEqCommon->WS("Frontend file name",  fInfo->FrontendFileName.c_str(), 256);
   fOdbEqCommon->WS("Status",         fInfo->Status.c_str(), 256);
   fOdbEqCommon->WS("Status color",   fInfo->StatusColor.c_str(), NAME_LENGTH);
   fOdbEqCommon->WB("Hidden",         fInfo->Hidden);
   fOdbEqCommon->WI("Write cache size", fInfo->WriteCacheSize);
   return TMFeOk();
}

TMFeResult TMFeEquipment::Init1()
{
   if (TMFE::gfVerbose)
      printf("TMFeEquipment::Init1: for [%s]\n", fName.c_str());

   //
   // create ODB /eq/name/common
   //

   fOdbEq = fMfe->fOdbRoot->Chdir((std::string("Equipment/") + fName).c_str(), true);
   fOdbEqCommon     = fOdbEq->Chdir("Common", true);
   fOdbEqSettings   = fOdbEq->Chdir("Settings", true);
   fOdbEqVariables  = fOdbEq->Chdir("Variables", true);
   fOdbEqStatistics = fOdbEq->Chdir("Statistics", true);

   TMFeResult r = ReadCommon();

   if (r.error_flag)
      return r;

   fInfo->FrontendHost = fMfe->fFrontendHostname;
   fInfo->FrontendName = fMfe->fFrontendName;
   if (!fFilename.empty()) {
      fInfo->FrontendFileName = fFilename;
   } else {
      fInfo->FrontendFileName = fMfe->fFrontendFilename;
   }

   fInfo->Status = "";
   fInfo->Status += fMfe->fFrontendName;
   fInfo->Status += "@";
   fInfo->Status += fMfe->fFrontendHostname;
   fInfo->StatusColor = "greenLight";

   ZeroStatistics();
   WriteStatistics();

   return TMFeOk();
}

TMFeResult TMFeEquipment::Init2()
{
   if (TMFE::gfVerbose)
      printf("TMFeEquipment::Init2: for [%s]\n", fName.c_str());

   // open event buffer
   
   uint32_t odb_max_event_size = DEFAULT_MAX_EVENT_SIZE;
   fMfe->fOdbRoot->RU32("Experiment/MAX_EVENT_SIZE", &odb_max_event_size, true);

   fMaxEventSize = odb_max_event_size;

   if (fInfo->Buffer.length() > 0) {
      int status = bm_open_buffer(fInfo->Buffer.c_str(), DEFAULT_BUFFER_SIZE, &fBufferHandle);

      if (status != BM_SUCCESS && status != BM_CREATED) {
         return TMFeMidasError(msprintf("Cannot open event buffer \"%s\"", fInfo->Buffer.c_str()), "bm_open_buffer", status);
      }

      uint32_t buffer_size = 0;
      fMfe->fOdbRoot->RU32(std::string("Experiment/Buffer Sizes/" + fInfo->Buffer).c_str(), &buffer_size);

      if (buffer_size > 0) {
         fBufferSize = buffer_size;

         // in bm_send_event(), maximum event size is the event buffer size,
         // here, we half it, to make sure we can buffer at least 2 events. K.O.

         uint32_t buffer_max_event_size = buffer_size/2;
      
         if (buffer_max_event_size < fMaxEventSize) {
            fMaxEventSize = buffer_max_event_size;
         }
      }
   }

   if (TMFE::gfVerbose)
      printf("TMFeEquipment::Init: Equipment \"%s\", max event size: %d, max event size in ODB: %d, event buffer \"%s\" size: %d\n", fName.c_str(), (int)fMaxEventSize, (int)odb_max_event_size, fInfo->Buffer.c_str(), (int)fBufferSize);

   // update ODB common

   TMFeResult r = WriteCommon();

   if (r.error_flag)
      return r;

   return TMFeOk();
};

TMFeResult TMFeEquipment::ZeroStatistics()
{
   fMutex.lock();

   if (TMFE::gfVerbose)
      printf("TMFeEquipment::ZeroStatistics: zero statistics for [%s]\n", fName.c_str());
   
   fStatEvents = 0;
   fStatBytes = 0;
   fStatEpS = 0;
   fStatKBpS = 0;
   
   fStatLastTime = 0;
   fStatLastEvents = 0;
   fStatLastBytes = 0;

   fStatNextWrite = TMFE::GetTime(); // force immediate update

   fMutex.unlock();

   return TMFeOk();
}

TMFeResult TMFeEquipment::WriteStatistics()
{
   fMutex.lock();

   if (TMFE::gfVerbose)
      printf("TMFeEquipment::WriteStatistics: write statistics for [%s]\n", fName.c_str());

   double now = TMFE::GetTime();
   double elapsed = now - fStatLastTime;

   if (elapsed > 0.9 || fStatLastTime == 0) {
      fStatEpS = (fStatEvents - fStatLastEvents) / elapsed;
      fStatKBpS = (fStatBytes - fStatLastBytes) / elapsed / 1000.0;

      fStatLastTime = now;
      fStatLastEvents = fStatEvents;
      fStatLastBytes = fStatBytes;
   }

   fOdbEqStatistics->WD("Events sent", fStatEvents);
   fOdbEqStatistics->WD("Events per sec.", fStatEpS);
   fOdbEqStatistics->WD("kBytes per sec.", fStatKBpS);

   fStatLastWrite = now;

   if (fInfo->PeriodStatisticsSec > 0) {
      // avoid creap of NextWrite: we start it at
      // time of initialization, then increment it strictly
      // by the period value, regardless of when it is actually
      // written to ODB (actual period is longer than requested
      // period because we only over-sleep, never under-sleep). K.O.
      while (fStatNextWrite <= now) {
         fStatNextWrite += fInfo->PeriodStatisticsSec;
      }
   } else {
      fStatNextWrite = now;
   }

   fMutex.unlock();
   
   return TMFeOk();
}

TMFeResult TMFeEquipment::ComposeEvent(char* event, size_t size) const
{
   EVENT_HEADER* pevent = (EVENT_HEADER*)event;
   pevent->event_id = fInfo->EventID;
   pevent->trigger_mask = fInfo->TriggerMask;
   pevent->serial_number = fSerial;
   pevent->time_stamp = TMFE::GetTime();
   pevent->data_size = 0;
   return TMFeOk();
}

TMFeResult TMFeEquipment::SendEvent(const char* event)
{
   std::lock_guard<std::mutex> guard(fMutex);
   
   fSerial++;

   if (fBufferHandle == 0) {
      return TMFeOk();
   }

   EVENT_HEADER* pevent = (EVENT_HEADER*)event;
   pevent->data_size = BkSize(event);

   int status = bm_send_event(fBufferHandle, pevent, sizeof(EVENT_HEADER) + pevent->data_size, BM_WAIT);
   if (status == BM_CORRUPTED) {
      TMFE::Instance()->Msg(MERROR, "TMFeEquipment::SendData", "bm_send_event() returned %d, event buffer is corrupted, shutting down the frontend", status);
      TMFE::Instance()->fShutdownRequested = true;
      return TMFeMidasError("Cannot send event, event buffer is corrupted, shutting down the frontend", "bm_send_event", status);
   } else if (status != BM_SUCCESS) {
      return TMFeMidasError("Cannot send event", "bm_send_event", status);
   }

   fStatEvents += 1;
   fStatBytes  += sizeof(EVENT_HEADER) + pevent->data_size;

   if (fInfo->WriteEventsToOdb) {
      TMFeResult r = WriteEventToOdb_locked(event);
      if (r.error_flag)
         return r;
   }

   return TMFeOk();
}

TMFeResult TMFeEquipment::WriteEventToOdb(const char* event)
{
   std::lock_guard<std::mutex> guard(fMutex);
   return WriteEventToOdb_locked(event);
}

TMFeResult TMFeEquipment::WriteEventToOdb_locked(const char* event)
{
   std::string path = "";
   path += "/Equipment/";
   path += fName;
   path += "/Variables";

   HNDLE hKeyVar = 0;

   int status = db_find_key(fMfe->fDB, 0, path.c_str(), &hKeyVar);
   if (status != DB_SUCCESS) {
      return TMFeMidasError(msprintf("Cannot find \"%s\" in ODB", path.c_str()), "db_find_key", status);
   }

   status = cm_write_event_to_odb(fMfe->fDB, hKeyVar, (const EVENT_HEADER*) event, FORMAT_MIDAS);
   if (status != SUCCESS) {
      return TMFeMidasError("Cannot write event to ODB", "cm_write_event_to_odb", status);
   }
   return TMFeOk();
}

int TMFeEquipment::BkSize(const char* event) const
{
   return bk_size(event + sizeof(EVENT_HEADER));
}

TMFeResult TMFeEquipment::BkInit(char* event, size_t size) const
{
   bk_init32(event + sizeof(EVENT_HEADER));
   return TMFeOk();
}

void* TMFeEquipment::BkOpen(char* event, const char* name, int tid) const
{
   void* ptr;
   bk_create(event + sizeof(EVENT_HEADER), name, tid, &ptr);
   return ptr;
}

TMFeResult TMFeEquipment::BkClose(char* event, void* ptr) const
{
   bk_close(event + sizeof(EVENT_HEADER), ptr);
   ((EVENT_HEADER*)event)->data_size = BkSize(event);
   return TMFeOk();
}

TMFeResult TMFeEquipment::SetStatus(char const* eq_status, char const* eq_color)
{
   if (eq_status) {
      fOdbEqCommon->WS("Status", eq_status, 256);
   }

   if (eq_color) {
      fOdbEqCommon->WS("Status color", eq_color, NAME_LENGTH);
   }

   return TMFeOk();
}

TMFeEquipmentBase::~TMFeEquipmentBase() // dtor
{
   if (fMfe)
      fMfe = NULL;
   if (fEq) {
      delete fEq;
      fEq = NULL;
   }
}

void TMFeEquipmentBase::Usage()
{
   // empty, to be overwritten by user handler
};

TMFeResult TMFE::TriggerAlarm(const char* name, const char* message, const char* aclass)
{
   int status = al_trigger_alarm(name, message, aclass, message, AT_INTERNAL);

   if (status) {
      return TMFeMidasError("Cannot trigger alarm", "al_trigger_alarm", status);
   }

   return TMFeOk();
}

TMFeResult TMFE::ResetAlarm(const char* name)
{
   int status = al_reset_alarm(name);

   if (status) {
      return TMFeMidasError("Cannot reset alarm", "al_reset_alarm", status);
   }

   return TMFeOk();
}

void TMFE::AddHooks(TMFeHooksInterface* hooks)
{
   fHooks.push_back(hooks);
}

void TMFE::CallPreConnectHooks(const std::vector<std::string>& args)
{
   for (auto h : fHooks) {
      if (h)
         h->HandlePreConnect(args);
   }
}

void TMFE::CallPostConnectHooks(const std::vector<std::string>& args)
{
   for (auto h : fHooks) {
      if (h)
         h->HandlePostConnect(args);
   }
}

void TMFE::CallPreDisconnectHooks()
{
   for (auto h : fHooks) {
      if (h)
         h->HandlePreDisconnect();
   }
}

void TMFE::CallPostDisconnectHooks()
{
   for (auto h : fHooks) {
      if (h)
         h->HandlePostDisconnect();
   }
}

// singleton instance
TMFE* TMFE::gfMFE = NULL;

// static data members
bool TMFE::gfVerbose = false;

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
