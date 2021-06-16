/********************************************************************\

  Name:         tmfe.cxx
  Created by:   Konstantin Olchanski - TRIUMF

  Contents:     C++ MIDAS frontend

\********************************************************************/

#undef NDEBUG // midas required assert() to be always enabled

#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <signal.h> // signal()
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

TMFeResult TMFE::Connect(const char* progname, const char* hostname, const char* exptname)
{
   if (progname)
      fProgramName = progname;

   if (fProgramName.empty()) {
      return TMFeErrorMessage("TMFE::Connect: frontend program name is not set");
   }

   fHostname = ss_gethostname();

   int status;
  
   std::string env_hostname;
   std::string env_exptname;
   
   /* get default from environment */
   status = cm_get_environment(&env_hostname, &env_exptname);

   if (status != CM_SUCCESS) {
      return TMFeMidasError("Cannot connect to MIDAS", "cm_get_environment", status);
   }

   if (hostname && hostname[0]) {
      fMserverHostname = hostname;
   } else {
      fMserverHostname = env_hostname;
   }
   
   if (exptname && exptname[0]) {
      fExptname = exptname;
   } else {
      fExptname = env_exptname;
   }

   if (gfVerbose) {
      printf("TMFE::Connect: Program \"%s\" connecting to experiment \"%s\" on host \"%s\"\n", fProgramName.c_str(), fExptname.c_str(), fMserverHostname.c_str());
   }
   
   int watchdog = DEFAULT_WATCHDOG_TIMEOUT;
   //int watchdog = 60*1000;
   
   status = cm_connect_experiment1(fMserverHostname.c_str(), fExptname.c_str(), fProgramName.c_str(), NULL, DEFAULT_ODB_SIZE, watchdog);
   
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

   RegisterRPCs();

   if (gfVerbose) {
      printf("TMFE::Connect: Program \"%s\" connected to experiment \"%s\" on host \"%s\"\n", fProgramName.c_str(), fExptname.c_str(), fMserverHostname.c_str());
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
      printf("TMFE::Disconnect: Disconnecting from experiment \"%s\" on host \"%s\"\n", fExptname.c_str(), fMserverHostname.c_str());
   StopRpcThread();
   cm_disconnect_experiment();
   if (gfVerbose)
      printf("TMFE::Disconnect: Disconnected from experiment \"%s\" on host \"%s\"\n", fExptname.c_str(), fMserverHostname.c_str());
   return TMFeOk();
}

/////////////////////////////////////////////////////////
//            event buffer functions
/////////////////////////////////////////////////////////

TMEventBuffer::TMEventBuffer(TMFE* mfe) // ctor
{
   assert(mfe != NULL);
   fMfe = mfe;
};

TMEventBuffer::~TMEventBuffer() // dtor
{
   CloseBuffer();

   // poison all pointers
   fMfe = NULL;
};

TMFeResult TMEventBuffer::OpenBuffer(const char* bufname, size_t bufsize)
{
   if (fBufHandle) {
      return TMFeErrorMessage(msprintf("Event buffer \"%s\" is already open", fBufName.c_str()));
   }
   
   fBufName = bufname;

   if (bufsize == 0)
      bufsize = DEFAULT_BUFFER_SIZE;

   int status = bm_open_buffer(fBufName.c_str(), bufsize, &fBufHandle);

   if (status != BM_SUCCESS && status != BM_CREATED) {
      return TMFeMidasError(msprintf("Cannot open event buffer \"%s\"", fBufName.c_str()), "bm_open_buffer", status);
   }

   fBufSize = 0;
   fBufMaxEventSize = 0;

   uint32_t buf_size = 0;
   uint32_t max_event_size = 0;

   fMfe->fOdbRoot->RU32("Experiment/MAX_EVENT_SIZE", &max_event_size);
   fMfe->fOdbRoot->RU32((std::string("Experiment/Buffer Sizes/") + bufname).c_str(), &buf_size);

   if (buf_size > 0) {
      // limit event size to half the buffer size, so we can buffer two events
      int xmax_event_size = buf_size / 2;
      // add extra margin
      if (xmax_event_size > 1024)
         xmax_event_size -= 1024;
      if (max_event_size > xmax_event_size)
         max_event_size = xmax_event_size;
   }

   fBufSize = buf_size;
   fBufMaxEventSize = max_event_size;

   if (fBufSize == 0) {
      return TMFeErrorMessage(msprintf("Cannot get buffer size for event buffer \"%s\"", fBufName.c_str()));
   }

   if (fBufMaxEventSize == 0) {
      return TMFeErrorMessage(msprintf("Cannot get MAX_EVENT_SIZE for event buffer \"%s\"", fBufName.c_str()));
   }

   printf("TMEventBuffer::OpenBuffer: Buffer \"%s\" size %d, max event size %d\n", fBufName.c_str(), (int)fBufSize, (int)fBufMaxEventSize);

   return TMFeOk();
}

TMFeResult TMEventBuffer::CloseBuffer()
{
   if (!fBufHandle)
      return TMFeOk();
   
   fBufRequests.clear(); // no need to cancel individual requests, they are gone after we close the buffer
   
   int status = bm_close_buffer(fBufHandle);
   
   if (status != BM_SUCCESS) {
      fBufHandle = 0;
      return TMFeMidasError(msprintf("Cannot close event buffer \"%s\"", fBufName.c_str()), "bm_close_buffer", status);
   }
   
   fBufHandle = 0;
   fBufSize = 0;
   fBufMaxEventSize = 0;
   fBufReadCacheSize = 0;
   fBufWriteCacheSize = 0;
   
   return TMFeOk();
}

TMFeResult TMEventBuffer::SetCacheSize(size_t read_cache_size, size_t write_cache_size)
{
   int status = bm_set_cache_size(fBufHandle, read_cache_size, write_cache_size);
   
   if (status != BM_SUCCESS) {
      return TMFeMidasError(msprintf("Cannot set event buffer \"%s\" cache sizes: read %d, write %d", fBufName.c_str(), (int)read_cache_size, (int)write_cache_size), "bm_set_cache_size", status);
   }

   fBufReadCacheSize = read_cache_size;
   fBufWriteCacheSize = write_cache_size;
   
   return TMFeOk();
}

TMFeResult TMEventBuffer::AddRequest(int event_id, int trigger_mask, const char* sampling_type_string)
{
   if (!fBufHandle) {
      return TMFeErrorMessage(msprintf("AddRequest: Error: Event buffer \"%s\" is not open", fBufName.c_str()));
   }

   int sampling_type = 0;
   
   if (strcmp(sampling_type_string, "GET_ALL")==0) {
      sampling_type = GET_ALL;
   } else if (strcmp(sampling_type_string, "GET_NONBLOCKING")==0) {
      sampling_type = GET_NONBLOCKING;
   } else if (strcmp(sampling_type_string, "GET_RECENT")==0) {
      sampling_type = GET_RECENT;
   } else {
      sampling_type = GET_ALL;
   }
   
   int request_id = 0;
      
   int status = bm_request_event(fBufHandle, event_id, trigger_mask, sampling_type, &request_id, NULL);
   
   if (status != BM_SUCCESS) {
      return TMFeMidasError(msprintf("Cannot make event request on buffer \"%s\"", fBufName.c_str()), "bm_request_event", status);
   }
   
   fBufRequests.push_back(request_id);
   
   return TMFeOk();
}

TMFeResult TMEventBuffer::ReceiveEvent(std::vector<char> *e, int timeout_msec)
{
   if (!fBufHandle) {
      return TMFeErrorMessage(msprintf("ReceiveEvent: Error: Event buffer \"%s\" is not open", fBufName.c_str()));
   }

   assert(e != NULL);
   
   e->resize(0);
   
   int status = bm_receive_event_vec(fBufHandle, e, timeout_msec);
   
   if (status == BM_ASYNC_RETURN) {
      return TMFeOk();
   }
   
   if (status != BM_SUCCESS) {
      return TMFeMidasError(msprintf("Cannot receive event on buffer \"%s\"", fBufName.c_str()), "bm_receive_event", status);
   }
   
   return TMFeOk();
}

TMFeResult TMEventBuffer::SendEvent(const char *e)
{
   const EVENT_HEADER *pevent = (const EVENT_HEADER*)e;
   const size_t event_size = sizeof(EVENT_HEADER) + pevent->data_size;
   //const size_t total_size = ALIGN8(event_size);
   return SendEvent(1, &e, &event_size);
}

TMFeResult TMEventBuffer::SendEvent(const std::vector<char>& e)
{
   const EVENT_HEADER *pevent = (const EVENT_HEADER*)e.data();
   const size_t event_size = sizeof(EVENT_HEADER) + pevent->data_size;
   //const size_t total_size = ALIGN8(event_size);
   if (e.size() != event_size) {
      return TMFeErrorMessage(msprintf("Cannot send event, size mismatch: vector size %d, data_size %d, event_size %d", (int)e.size(), (int)pevent->data_size, (int)event_size).c_str());
   }

   return SendEvent(1, (char**)&pevent, &event_size);
}

TMFeResult TMEventBuffer::SendEvent(const std::vector<std::vector<char>>& e)
{
   int sg_n = e.size();
   const char* sg_ptr[sg_n];
   size_t sg_len[sg_n];
   for (int i=0; i<sg_n; i++) {
      sg_ptr[i] = e[i].data();
      sg_len[i] = e[i].size();
   }
   return SendEvent(sg_n, sg_ptr, sg_len);
}

TMFeResult TMEventBuffer::SendEvent(int sg_n, const char* const sg_ptr[], const size_t sg_len[])
{
   int status = bm_send_event_sg(fBufHandle, sg_n, sg_ptr, sg_len, BM_WAIT);

   if (status == BM_CORRUPTED) {
      fMfe->Msg(MERROR, "TMEventBuffer::SendEvent", "Cannot send event to buffer \"%s\": bm_send_event() returned %d, event buffer is corrupted, shutting down the frontend", fBufName.c_str(), status);
      fMfe->fShutdownRequested = true;
      return TMFeMidasError("Cannot send event, event buffer is corrupted, shutting down the frontend", "bm_send_event", status);
   } else if (status != BM_SUCCESS) {
      fMfe->Msg(MERROR, "TMEventBuffer::SendEvent", "Cannot send event to buffer \"%s\": bm_send_event() returned %d", fBufName.c_str(), status);
      return TMFeMidasError("Cannot send event", "bm_send_event", status);
   }

   return TMFeOk();
}

TMFeResult TMEventBuffer::FlushCache(bool wait)
{
   if (!fBufHandle)
      return TMFeOk();
   
   int flag = BM_NO_WAIT;
   if (wait)
      flag = BM_WAIT;
   
   /* flush of event socket in no-wait mode does nothing */
   if (wait && rpc_is_remote()) {
      int status = bm_flush_cache(0, flag);

      //printf("bm_flush_cache(0,%d) status %d\n", flag, status);

      if (status == BM_SUCCESS) {
         // nothing
      } else if (status == BM_ASYNC_RETURN) {
         // nothing
      } else {
         return TMFeMidasError("Cannot flush mserver event socket", "bm_flush_cache", status);
      }
   }

   int status = bm_flush_cache(fBufHandle, flag);

   //printf("bm_flush_cache(%d,%d) status %d\n", fBufHandle, flag, status);

   if (status == BM_SUCCESS) {
      // nothing
   } else if (status == BM_ASYNC_RETURN) {
      // nothing
   } else {
      return TMFeMidasError(msprintf("Cannot flush event buffer \"%s\"", fBufName.c_str()).c_str(), "bm_flush_cache", status);
   }

   return TMFeOk();
}

TMFeResult TMFE::EventBufferOpen(TMEventBuffer** pbuf, const char* bufname, size_t bufsize)
{
   assert(pbuf != NULL);
   assert(bufname != NULL);
   
   std::lock_guard<std::mutex> guard(fEventBuffersMutex);

   for (auto b : fEventBuffers) {
      if (!b)
         continue;

      if (b->fBufName == bufname) {
         *pbuf = b;
         if (bufsize != 0 && bufsize > b->fBufSize) {
            Msg(MERROR, "TMFE::EventBufferOpen", "Event buffer \"%s\" size %d is smaller than requested size %d", b->fBufName.c_str(), (int)b->fBufSize, (int)bufsize);
         }
         return TMFeOk();
      }
   }

   TMEventBuffer *b = new TMEventBuffer(this);

   fEventBuffers.push_back(b);

   *pbuf = b;

   TMFeResult r = b->OpenBuffer(bufname, bufsize);

   if (r.error_flag) {
      return r;
   }

   return TMFeOk();
}

TMFeResult TMFE::EventBufferFlushCacheAll(bool wait)
{
   int flag = BM_NO_WAIT;
   if (wait)
      flag = BM_WAIT;

   /* flush of event socket in no-wait mode does nothing */
   if (wait && rpc_is_remote()) {
      int status = bm_flush_cache(0, flag);

      //printf("bm_flush_cache(0,%d) status %d\n", flag, status);

      if (status == BM_SUCCESS) {
         // nothing
      } else if (status == BM_ASYNC_RETURN) {
         // nothing
      } else {
         return TMFeMidasError("Cannot flush mserver event socket", "bm_flush_cache", status);
      }
   }

   std::lock_guard<std::mutex> guard(fEventBuffersMutex);

   for (auto b : fEventBuffers) {
      if (!b)
         continue;

      TMFeResult r = b->FlushCache(wait);

      if (r.error_flag)
         return r;
   }

   return TMFeOk();
}

TMFeResult TMFE::EventBufferCloseAll()
{
   std::lock_guard<std::mutex> guard(fEventBuffersMutex);

   for (auto b : fEventBuffers) {
      if (!b)
         continue;
      TMFeResult r = b->CloseBuffer();
      if (r.error_flag)
         return r;

      delete b;
   }

   fEventBuffers.clear();

   return TMFeOk();
}

/////////////////////////////////////////////////////////
//            equipment functions
/////////////////////////////////////////////////////////

double TMFrontend::FePeriodicTasks()
{
   double now = TMFE::GetTime();

   double next_periodic = now + 60;

   int n = fFeEquipments.size();
   for (int i=0; i<n; i++) {
      TMFeEquipment* eq = fFeEquipments[i];
      if (!eq)
         continue;
      if (!eq->fEqConfEnabled)
         continue;
      if (!eq->fEqConfEnablePeriodic)
         continue;
      double period = eq->fEqConfPeriodMilliSec/1000.0;
      if (period <= 0)
         continue;
      if (eq->fEqPeriodicNextCallTime == 0)
         eq->fEqPeriodicNextCallTime = now + 0.5; // we are off by 0.5 sec with updating of statistics
      //printf("periodic[%d] period %f, last call %f, next call %f (%f)\n", i, period, eq->fEqPeriodicLastCallTime, eq->fEqPeriodicNextCallTime, now - eq->fEqPeriodicNextCallTime);
      if (now >= eq->fEqPeriodicNextCallTime) {
         eq->fEqPeriodicNextCallTime += period;
         
         if (eq->fEqPeriodicNextCallTime < now) {
            if (TMFE::gfVerbose)
               printf("TMFE::EquipmentPeriodicTasks: periodic equipment \"%s\" skipped some beats!\n", eq->fEqName.c_str());
            fMfe->Msg(MERROR, "TMFE::EquipmentPeriodicTasks", "Equipment \"%s\" skipped some beats!", eq->fEqName.c_str());
            while (eq->fEqPeriodicNextCallTime < now) {
               eq->fEqPeriodicNextCallTime += period;
            }
         }
         
         if (fMfe->fStateRunning || !eq->fEqConfReadOnlyWhenRunning) {
            eq->fEqPeriodicLastCallTime = now;
            //printf("handler %d eq [%s] call HandlePeriodic()\n", i, h->fEq->fName.c_str());                     
            eq->HandlePeriodic();
         }
         
         now = TMFE::GetTime();
      }
      
      if (eq->fEqPeriodicNextCallTime < next_periodic)
         next_periodic = eq->fEqPeriodicNextCallTime;
   }

   now = TMFE::GetTime();

   // update statistics
   for (auto eq : fFeEquipments) {
      if (!eq)
         continue;
      if (!eq->fEqConfEnabled)
         continue;
      double next = eq->fEqStatNextWrite; // NOTE: this is not thread-safe, possible torn read of "double"
      if (now > next) {
         eq->EqWriteStatistics();
         next = eq->fEqStatNextWrite; // NOTE: this is not thread-safe, possible torn read of "double"
      }
      if (next < next_periodic)
         next_periodic = next;
   }

   now = TMFE::GetTime();

   // flush write cache
   if ((fFeFlushWriteCachePeriodSec > 0) && (now >= fFeFlushWriteCacheNextCallTime)) {
      fMfe->EventBufferFlushCacheAll(false);
      fFeFlushWriteCacheNextCallTime = now + fFeFlushWriteCachePeriodSec;
      if (fFeFlushWriteCacheNextCallTime < next_periodic)
         next_periodic = fFeFlushWriteCacheNextCallTime;
   }

   return next_periodic;
}

double TMFrontend::FePollTasks(double next_periodic_time)
{
   //printf("poll %f next %f diff %f\n", TMFE::GetTime(), next_periodic_time, next_periodic_time - TMFE::GetTime());
   
   double poll_sleep_sec = 9999.0;
   while (!fMfe->fShutdownRequested) {
      bool poll_again = false;
      // NOTE: ok to use range-based for() loop, there will be a crash if HandlePoll() or HandlePollRead() modify fEquipments, so they should not do that. K.O.
      for (auto eq : fFeEquipments) {
         if (!eq)
            continue;
         if (!eq->fEqConfEnabled)
            continue;
         if (eq->fEqConfEnablePoll && !eq->fEqPollThreadRunning && !eq->fEqPollThreadStarting) {
            if (fMfe->fStateRunning || !eq->fEqConfReadOnlyWhenRunning) {
               if (eq->fEqConfPollSleepSec < poll_sleep_sec)
                  poll_sleep_sec = eq->fEqConfPollSleepSec;
               bool poll = eq->HandlePoll();
               if (poll) {
                  poll_again = true;
                  eq->HandlePollRead();
               }
            }
         }
      }
      if (!poll_again)
         break;

      if (next_periodic_time) {
         // stop polling if we need to run periodic activity
         double now = TMFE::TMFE::GetTime();
         if (now >= next_periodic_time)
            break;
      }
   }
   return poll_sleep_sec;
}

void TMFeEquipment::EqPollThread()
{
   if (TMFE::gfVerbose)
      printf("TMFeEquipment::EqPollThread: equipment \"%s\" poll thread started\n", fEqName.c_str());
   
   fEqPollThreadRunning = true;

   while (!fMfe->fShutdownRequested && !fEqPollThreadShutdownRequested) {
      if (fMfe->fStateRunning || !fEqConfReadOnlyWhenRunning) {
         bool poll = HandlePoll();
         if (poll) {
            HandlePollRead();
         } else {
            if (fEqConfPollSleepSec > 0) {
               TMFE::Sleep(fEqConfPollSleepSec);
            }
         }
      } else {
         TMFE::Sleep(0.1);
      }
   }
   if (TMFE::gfVerbose)
      printf("TMFeEquipment::EqPollThread: equipment \"%s\" poll thread stopped\n", fEqName.c_str());

   fEqPollThreadRunning = false;
}

void TMFeEquipment::EqStartPollThread()
{
   // NOTE: this is thread safe

   std::lock_guard<std::mutex> guard(fEqMutex);

   if (fEqPollThreadRunning || fEqPollThreadStarting || fEqPollThread) {
      fMfe->Msg(MERROR, "TMFeEquipment::EqStartPollThread", "Equipment \"%s\": poll thread is already running", fEqName.c_str());
      return;
   }

   fEqPollThreadShutdownRequested = false;
   fEqPollThreadStarting = true;

   fEqPollThread = new std::thread(&TMFeEquipment::EqPollThread, this);
}

void TMFeEquipment::EqStopPollThread()
{
   // NOTE: this is thread safe
   fEqPollThreadStarting = false;
   fEqPollThreadShutdownRequested = true;
   for (int i=0; i<100; i++) {
      if (!fEqPollThreadRunning) {
         std::lock_guard<std::mutex> guard(fEqMutex);
         if (fEqPollThread) {
            fEqPollThread->join();
            delete fEqPollThread;
            fEqPollThread = NULL;
         }
         return;
      }
      TMFE::Sleep(0.1);
   }
   if (fEqPollThreadRunning) {
      fMfe->Msg(MERROR, "TMFeEquipment::EqStopPollThread", "Equipment \"%s\": timeout waiting for shutdown of poll thread", fEqName.c_str());
   }
}

void TMFE::StopRun()
{
   char str[TRANSITION_ERROR_STRING_LENGTH];

   int status = cm_transition(TR_STOP, 0, str, sizeof(str), TR_SYNC, FALSE);
   if (status != CM_SUCCESS) {
      Msg(MERROR, "TMFE::StopRun", "Cannot stop run, error: %s", str);
      fRunStopRequested = false;
      return;
   }

   fRunStopRequested = false;

   bool logger_auto_restart = false;
   fOdbRoot->RB("Logger/Auto restart", &logger_auto_restart);

   int logger_auto_restart_delay = 0;
   fOdbRoot->RI("Logger/Auto restart delay", &logger_auto_restart_delay);

   if (logger_auto_restart) {
      Msg(MINFO, "TMFE::StopRun", "Run will restart after %d seconds", logger_auto_restart_delay);
      fRunStartTime = GetTime() + logger_auto_restart_delay;
   } else {
      fRunStartTime = 0;
   }
}

void TMFE::StartRun()
{
   fRunStartTime = 0;

   /* check if really stopped */
   int run_state = 0;
   fOdbRoot->RI("Runinfo/State", &run_state);

   if (run_state != STATE_STOPPED) {
      Msg(MERROR, "TMFE::StartRun", "Run start requested, but run is already in progress");
      return;
   }

   bool logger_auto_restart = false;
   fOdbRoot->RB("Logger/Auto restart", &logger_auto_restart);

   if (!logger_auto_restart) {
      Msg(MERROR, "TMFE::StartRun", "Run start requested, but logger/auto restart is off");
      return;
   }

   Msg(MTALK, "TMFE::StartRun", "Starting new run");

   char str[TRANSITION_ERROR_STRING_LENGTH];

   int status = cm_transition(TR_START, 0, str, sizeof(str), TR_SYNC, FALSE);
   if (status != CM_SUCCESS) {
      Msg(MERROR, "TMFE::StartRun", "Cannot restart run, error: %s", str);
   }
}

void TMFrontend::FePollMidas(double sleep_sec)
{
   assert(sleep_sec >= 0);
   bool debug = false;
   double now = TMFE::GetTime();
   double sleep_start = now;
   double sleep_end = now + sleep_sec;
   int count_yield_loops = 0;

   while (!fMfe->fShutdownRequested) {
      double next_periodic_time = 0;
      double poll_sleep = 1.0;

      if (!fFePeriodicThreadRunning) {
         next_periodic_time = FePeriodicTasks();
         poll_sleep = FePollTasks(next_periodic_time);
      } else {
         poll_sleep = FePollTasks(TMFE::GetTime() + 0.100);
      }

      if (fMfe->fRunStopRequested) {
         fMfe->StopRun();
         continue;
      }

      now = TMFE::GetTime();

      if (fMfe->fRunStartTime && now >= fMfe->fRunStartTime) {
         fMfe->StartRun();
         continue;
      }

      double sleep_time = sleep_end - now;

      if (next_periodic_time > 0 && next_periodic_time < sleep_end) {
         sleep_time = next_periodic_time - now;
      }

      int s = 0;
      if (sleep_time > 0)
         s = 1 + sleep_time*1000.0;

      if (poll_sleep*1000.0 < s) {
         s = 0;
      }

      if (debug) {
         printf("now %.6f, sleep_end %.6f, next_periodic %.6f, sleep_time %.6f, cm_yield(%d), poll period %.6f\n", now, sleep_end, next_periodic_time, sleep_time, s, poll_sleep);
      }

      int status = cm_yield(s);
      
      if (status == RPC_SHUTDOWN || status == SS_ABORT) {
         fMfe->fShutdownRequested = true;
         if (TMFE::gfVerbose) {
            fprintf(stderr, "TMFE::PollMidas: cm_yield(%d) status %d, shutdown requested...\n", s, status);
         }
      }

      now = TMFE::GetTime();
      double sleep_more = sleep_end - now;
      if (sleep_more <= 0)
         break;

      count_yield_loops++;

      if (poll_sleep < sleep_more) {
         TMFE::Sleep(poll_sleep);
      }
   }

   if (debug) {
      printf("TMFE::PollMidas: sleep %.1f msec, actual %.1f msec, %d loops\n", sleep_sec * 1000.0, (now - sleep_start) * 1000.0, count_yield_loops);
   }
}

void TMFE::Yield(double sleep_sec)
{
   double now = GetTime();
   //double sleep_start = now;
   double sleep_end = now + sleep_sec;

   while (!fShutdownRequested) {
      now = GetTime();

      double sleep_time = sleep_end - now;
      int s = 0;
      if (sleep_time > 0)
         s = 1 + sleep_time*1000.0;

      //printf("now %f, sleep_end %f, s %d\n", now, sleep_end, s);
      
      int status = cm_yield(s);
      
      if (status == RPC_SHUTDOWN || status == SS_ABORT) {
         fShutdownRequested = true;
         fprintf(stderr, "TMFE::Yield: cm_yield(%d) status %d, shutdown requested...\n", s, status);
      }

      now = GetTime();
      if (now >= sleep_end)
         break;
   }

   //printf("TMFE::Yield: msec %d, actual %f msec\n", msec, (now - sleep_start) * 1000.0);
}

void TMFE::MidasPeriodicTasks()
{
   cm_periodic_tasks();
}

void TMFE::RpcThread()
{
   if (TMFE::gfVerbose)
      printf("TMFE::RpcThread: RPC thread started\n");

   int msec = 1000;

   fRpcThreadRunning = true;
   ss_suspend_set_rpc_thread(ss_gettid());

   while (!fShutdownRequested && !fRpcThreadShutdownRequested) {

      int status = cm_yield(msec);

      if (status == RPC_SHUTDOWN || status == SS_ABORT) {
         fShutdownRequested = true;
         if (TMFE::gfVerbose)
            printf("TMFE::RpcThread: cm_yield(%d) status %d, shutdown requested...\n", msec, status);
      }
   }
   ss_suspend_exit();
   if (TMFE::gfVerbose)
      printf("TMFE::RpcThread: RPC thread stopped\n");
   fRpcThreadRunning = false;
}

void TMFrontend::FePeriodicThread()
{
   if (TMFE::gfVerbose)
      printf("TMFE::PeriodicThread: periodic thread started\n");
   
   fFePeriodicThreadRunning = true;
   while (!fMfe->fShutdownRequested && !fFePeriodicThreadShutdownRequested) {
      FePeriodicTasks();
      TMFE::Sleep(0.0005);
   }
   if (TMFE::gfVerbose)
      printf("TMFE::PeriodicThread: periodic thread stopped\n");
   fFePeriodicThreadRunning = false;
}

void TMFE::StartRpcThread()
{
   // NOTE: this is thread safe

   std::lock_guard<std::mutex> guard(fMutex);
   
   if (fRpcThreadRunning || fRpcThreadStarting || fRpcThread) {
      if (gfVerbose)
         printf("TMFE::StartRpcThread: RPC thread already running\n");
      return;
   }
   
   fRpcThreadStarting = true;
   fRpcThread = new std::thread(&TMFE::RpcThread, this);
}

void TMFrontend::FeStartPeriodicThread()
{
   // NOTE: this is thread safe

   std::lock_guard<std::mutex> guard(fFeMutex);

   if (fFePeriodicThreadRunning || fFePeriodicThreadStarting || fFePeriodicThread) {
      if (TMFE::gfVerbose)
         printf("TMFE::StartPeriodicThread: periodic thread already running\n");
      return;
   }

   fFePeriodicThreadStarting = true;
   fFePeriodicThread = new std::thread(&TMFrontend::FePeriodicThread, this);
}

void TMFE::StopRpcThread()
{
   // NOTE: this is thread safe
   
   fRpcThreadStarting = false;
   fRpcThreadShutdownRequested = true;
   
   for (int i=0; i<60; i++) {
      if (!fRpcThreadRunning) {
         std::lock_guard<std::mutex> guard(fMutex);
         if (fRpcThread) {
            fRpcThread->join();
            delete fRpcThread;
            fRpcThread = NULL;
            if (gfVerbose)
               printf("TMFE::StopRpcThread: RPC thread stopped\n");
         }
         return;
      }
      if (i>5) {
         fprintf(stderr, "TMFE::StopRpcThread: waiting for RPC thread to stop\n");
      }
      ::sleep(1);
   }

   fprintf(stderr, "TMFE::StopRpcThread: timeout waiting for RPC thread to stop\n");
}

void TMFrontend::FeStopPeriodicThread()
{
   // NOTE: this is thread safe
   
   fFePeriodicThreadStarting = false;
   fFePeriodicThreadShutdownRequested = true;

   for (int i=0; i<60; i++) {
      if (!fFePeriodicThreadRunning) {
         std::lock_guard<std::mutex> guard(fFeMutex);
         if (fFePeriodicThread) {
            fFePeriodicThread->join();
            delete fFePeriodicThread;
            fFePeriodicThread = NULL;
            if (TMFE::gfVerbose)
               printf("TMFE::StopPeriodicThread: periodic thread stopped\n");
         }
         return;
      }
      if (i>5) {
         fprintf(stderr, "TMFE::StopPeriodicThread: waiting for periodic thread to stop\n");
      }
      ::sleep(1);
   }

   fprintf(stderr, "TMFE::StopPeriodicThread: timeout waiting for periodic thread to stop\n");
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

   // NOTE: cannot use range-based for() loop, it uses an iterator and will crash if HandleRpc() modifies fEquipments. K.O.
   for (unsigned i=0; i<mfe->fRpcHandlers.size(); i++) {
      TMFeRpcHandlerInterface* h = mfe->fRpcHandlers[i];
      if (!h)
         continue;
      std::string result = "";
      TMFeResult r = h->HandleRpc(cmd, args, result);
      if (result.length() > 0) {
         //printf("Handler reply [%s]\n", C(r));
         strlcpy(return_buf, result.c_str(), return_max_length);
         return RPC_SUCCESS;
      }
   }

   return_buf[0] = 0;
   return RPC_SUCCESS;
}

class TMFrontendRpcHelper: public TMFeRpcHandlerInterface
{
public:
   TMFrontend* fFe = NULL;

public:
   TMFrontendRpcHelper(TMFrontend* fe) // ctor
   {
      if (TMFE::gfVerbose)
         printf("TMFrontendRpcHelper::ctor!\n");

      fFe = fe;
   }

   virtual ~TMFrontendRpcHelper() // dtor
   {
      if (TMFE::gfVerbose)
         printf("TMFrontendRpcHelper::dtor!\n");

      // poison pointers
      fFe = NULL;
   }

   TMFeResult HandleBeginRun(int run_number)
   {
      if (TMFE::gfVerbose)
         printf("TMFrontendRpcHelper::HandleBeginRun!\n");

      for (unsigned i=0; i<fFe->fFeEquipments.size(); i++) {
         TMFeEquipment* eq = fFe->fFeEquipments[i];
         if (!eq)
            continue;
         if (!eq->fEqConfEnabled)
            continue;
         eq->EqZeroStatistics();
         eq->EqWriteStatistics();
      }
      return TMFeOk();
   }

   TMFeResult HandleEndRun(int run_number)
   {
      if (TMFE::gfVerbose)
         printf("TMFrontendRpcHelper::HandleEndRun!\n");

      for (unsigned i=0; i<fFe->fFeEquipments.size(); i++) {
         TMFeEquipment* eq = fFe->fFeEquipments[i];
         if (!eq)
            continue;
         if (!eq->fEqConfEnabled)
            continue;
         eq->EqWriteStatistics();
      }

      TMFeResult r = fFe->fMfe->EventBufferFlushCacheAll();

      if (r.error_flag)
         return r;

      return TMFeOk();
   }
};

static INT tr_start(INT run_number, char *errstr)
{
   if (TMFE::gfVerbose)
      printf("TMFE::tr_start!\n");

   TMFE* mfe = TMFE::Instance();

   mfe->fRunNumber = run_number;
   mfe->fStateRunning = true;
   
   TMFeResult result;

   // NOTE: cannot use range-based for() loop, it uses an iterator and will crash if HandleBeginRun() modifies fEquipments. K.O.
   for (unsigned i=0; i<mfe->fRpcHandlers.size(); i++) {
      TMFeRpcHandlerInterface* h = mfe->fRpcHandlers[i];
      if (!h)
         continue;
      result = h->HandleBeginRun(run_number);
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

   // NOTE: cannot use range-based for() loop, it uses an iterator and will crash if HandleEndRun() modifies fEquipments. K.O.
   // NOTE: we need to stop thing in reverse order, otherwise TMFrontend code
   // does not work right - TMFrontend is registered first, and (correctly) runs
   // first at begin of run (to clear statistics, etc). But at the end of run
   // it needs to run last, to update the statistics, etc. after all the equipments
   // have done their end of run things and are finished. K.O.
   for (int i = (int)mfe->fRpcHandlers.size() - 1; i >= 0; i--) {
      TMFeRpcHandlerInterface* h = mfe->fRpcHandlers[i];
      if (!h)
         continue;
      TMFeResult xresult = h->HandleEndRun(run_number);
      if (xresult.error_flag) {
         // error handling in this function matches general transition error handling:
         // the "run stop" transition is always sucessful, the run always stops.
         // if some frontend returns an error, this error is remembered and is returned
         // as the transition over all status. K.O.
         result = xresult;
      }
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

   // NOTE: cannot use range-based for() loop, it uses an iterator and will crash if HandlePauseRun() modifies fEquipments. K.O.
   // NOTE: tr_pause runs in reverse order to match tr_stop. K.O.
   for (int i = (int)mfe->fRpcHandlers.size() - 1; i >= 0; i--) {
      TMFeRpcHandlerInterface* h = mfe->fRpcHandlers[i];
      if (!h)
         continue;
      result = h->HandlePauseRun(run_number);
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

   mfe->fRunNumber = run_number;
   mfe->fStateRunning = true;

   // NOTE: cannot use range-based for() loop, it uses an iterator and will crash if HandleResumeRun() modifies fEquipments. K.O.
   for (unsigned i=0; i<mfe->fRpcHandlers.size(); i++) {
      TMFeRpcHandlerInterface* h = mfe->fRpcHandlers[i];
      if (!h)
         continue;
      result = h->HandleResumeRun(run_number);
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

   // NOTE: cannot use range-based for() loop, it uses an iterator and will crash if HandleStartAbortRun() modifies fEquipments. K.O.
   for (unsigned i=0; i<mfe->fRpcHandlers.size(); i++) {
      TMFeRpcHandlerInterface* h = mfe->fRpcHandlers[i];
      if (!h)
         continue;
      result = h->HandleStartAbortRun(run_number);
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

void TMFE::RegisterRPCs()
{
   if (TMFE::gfVerbose)
      printf("TMFE::RegisterRPCs!\n");
   
   cm_register_function(RPC_JRPC, rpc_callback);
   cm_register_transition(TR_START, tr_start, 500);
   cm_register_transition(TR_STOP, tr_stop, 500);
   cm_register_transition(TR_PAUSE, tr_pause, 500);
   cm_register_transition(TR_RESUME, tr_resume, 500);
   cm_register_transition(TR_STARTABORT, tr_startabort, 500);
}

void TMFE::AddRpcHandler(TMFeRpcHandlerInterface* h)
{
   fRpcHandlers.push_back(h);
}

void TMFE::RemoveRpcHandler(TMFeRpcHandlerInterface* h)
{
   for (unsigned i=0; i<fRpcHandlers.size(); i++) {
      if (fRpcHandlers[i] == h) {
         fRpcHandlers[i] = NULL;
      }
   }
}

TMFrontend::TMFrontend() // ctor
{
   fMfe = TMFE::Instance();
}

TMFrontend::~TMFrontend() // dtor
{
   if (fFeRpcHelper) {
      fMfe->RemoveRpcHandler(fFeRpcHelper);
      delete fFeRpcHelper;
      fFeRpcHelper = NULL;
   }
   // poison all pointers
   fMfe = NULL;
}

TMFeResult TMFrontend::FeInitEquipments(const std::vector<std::string>& args)
{
   // NOTE: cannot use range-based for() loop, it uses an iterator and will crash if HandleInit() modifies fEquipments. K.O.
   for (unsigned i=0; i<fFeEquipments.size(); i++) {
      if (!fFeEquipments[i])
         continue;
      if (!fFeEquipments[i]->fEqConfEnabled)
         continue;
      TMFeResult r = fFeEquipments[i]->EqInit(args);
      if (r.error_flag)
         return r;
   }
   return TMFeOk();
}

void TMFrontend::FeStopEquipmentPollThreads()
{
   // NOTE: should not use range-based for() loop, it uses an iterator and it not thread-safe. K.O.
   for (unsigned i=0; i<fFeEquipments.size(); i++) {
      if (!fFeEquipments[i])
         continue;
      fFeEquipments[i]->EqStopPollThread();
   }
}

void TMFrontend::FeDeleteEquipments()
{
   // NOTE: this is thread-safe: we do not modify the fEquipments object. K.O.
   // NOTE: this is not thread-safe, we will race against ourselves and do multiple delete of fEquipents[i]. K.O.

   // NOTE: should not use range-based for() loop, it uses an iterator and it not thread-safe. K.O.
   for (unsigned i=0; i<fFeEquipments.size(); i++) {
      if (!fFeEquipments[i])
         continue;
      //printf("delete equipment [%s]\n", fFeEquipments[i]->fEqName.c_str());
      fMfe->RemoveRpcHandler(fFeEquipments[i]);
      delete fFeEquipments[i];
      fFeEquipments[i] = NULL;
   }

   fMfe->EventBufferFlushCacheAll();
   fMfe->EventBufferCloseAll();
}

TMFeResult TMFrontend::FeAddEquipment(TMFeEquipment* eq)
{
   // NOTE: not thread-safe, we modify the fEquipments object. K.O.
   
   // NOTE: should not use range-based for() loop, it uses an iterator and it not thread-safe. K.O.
   for (unsigned i=0; i<fFeEquipments.size(); i++) {
      if (!fFeEquipments[i])
         continue;
      if (fFeEquipments[i] == eq) {
         fprintf(stderr, "TMFE::AddEquipment: Fatal error: Equipment \"%s\" is already registered, bye...\n", fFeEquipments[i]->fEqName.c_str());
         exit(1);
         //return TMFeErrorMessage(msprintf("TMFE::AddEquipment: Equipment \"%s\" is already registered", fFeEquipments[i]->fEqName.c_str()));
      }
      if (fFeEquipments[i]->fEqName == eq->fEqName) {
         fprintf(stderr, "TMFE::AddEquipment: Fatal error: Duplicate equipment name \"%s\", bye...\n", eq->fEqName.c_str());
         exit(1);
         //return TMFeErrorMessage(std::string("TMFE::AddEquipment: Duplicate equipment name \"") + eq->fEqName + "\"");
      }
   }

   eq->fFe = this;

   // NOTE: fEquipments must be protected again multithreaded access here. K.O.
   fFeEquipments.push_back(eq);

   return TMFeOk();
}

TMFeResult TMFrontend::FeRemoveEquipment(TMFeEquipment* eq)
{
   // NOTE: this is thread-safe, we do not modify the fEquipments object. K.O.
   
   // NOTE: should not use range-based for() loop, it uses an iterator and it not thread-safe. K.O.
   for (unsigned i=0; i<fFeEquipments.size(); i++) {
      if (!fFeEquipments[i])
         continue;
      if (fFeEquipments[i] == eq) {
         fFeEquipments[i] = NULL;
         return TMFeOk();
      }
   }

   return TMFeErrorMessage(msprintf("TMFE::RemoveEquipment: Cannot find equipment \"%s\"", eq->fEqName.c_str()));
}

void TMFrontend::FeSetName(const char* program_name)
{
   assert(program_name != NULL);
   fMfe->fProgramName = program_name;
}

TMFeEquipment::TMFeEquipment(const char* eqname, const char* eqfilename) // ctor
{
   assert(eqname != NULL);
   assert(eqfilename != NULL);
   
   if (TMFE::gfVerbose)
      printf("TMFeEquipment::ctor: equipment name [%s] file [%s]\n", eqname, eqfilename);

   fMfe = TMFE::Instance();
   fEqName = eqname;
   fEqFilename = eqfilename;
   fEqStatNextWrite = TMFE::GetTime();
}

TMFeEquipment::~TMFeEquipment() // dtor
{
   if (TMFE::gfVerbose)
      printf("TMFeEquipment::dtor: equipment name [%s]\n", fEqName.c_str());

   EqStopPollThread();

   // free data and poison pointers
   fMfe = NULL;
   fFe  = NULL;
   fEqEventBuffer = NULL;
}

TMFeResult TMFeEquipment::EqInit(const std::vector<std::string>& args)
{
   TMFeResult r;

   r = EqPreInit();
   if (r.error_flag)
      return r;

   r = HandleInit(args);
   if (r.error_flag)
      return r;

   r = EqPostInit();
   if (r.error_flag)
      return r;

   return TMFeOk();
}

TMFeResult TMFeEquipment::EqReadCommon()
{
   if (TMFE::gfVerbose)
      printf("TMFeEquipment::EqReadCommon: for [%s]\n", fEqName.c_str());

   // list of ODB Common entries always read

   fOdbEqCommon->RB("Enabled", &fEqConfEnabled, true);
   fOdbEqCommon->RD("Event limit", &fEqConfEventLimit, true);

   if (fEqConfReadConfigFromOdb) {
      // list of ODB Common entries read if we want to control equipment from ODB

      fOdbEqCommon->RU16("Event ID",       &fEqConfEventID,        true);
      fOdbEqCommon->RU16("Trigger mask",   &fEqConfTriggerMask,    true);
      fOdbEqCommon->RS("Buffer",           &fEqConfBuffer,         true, NAME_LENGTH);
      fOdbEqCommon->RI("Type",             &fEqConfType,           true);
      fOdbEqCommon->RI("Source",           &fEqConfSource,         true);
      fOdbEqCommon->RS("Format",           &fEqConfFormat,         true, 8);
      fOdbEqCommon->RI("Read on",          &fEqConfReadOn,         true);
      fOdbEqCommon->RI("Period",           &fEqConfPeriodMilliSec, true);
      fOdbEqCommon->RU32("Num subevents",  &fEqConfNumSubEvents,   true);
      fOdbEqCommon->RI("Log history",      &fEqConfLogHistory,     true);
      fOdbEqCommon->RB("Hidden",           &fEqConfHidden,         true);
      fOdbEqCommon->RI("Write cache size", &fEqConfWriteCacheSize, true);

      // decode data from ODB Common

      fEqConfReadOnlyWhenRunning = !(fEqConfReadOn & (RO_PAUSED|RO_STOPPED));
      fEqConfWriteEventsToOdb    = (fEqConfReadOn & RO_ODB);
   }

   // list of ODB Common entries we read and write back to ODB, but do not actually use.

   //fOdbEqCommon->RS("Frontend host",       &fEqConfFrontendHost,     true, NAME_LENGTH);
   //fOdbEqCommon->RS("Frontend name",       &fEqConfFrontendName,     true, NAME_LENGTH);
   //fOdbEqCommon->RS("Frontend file name",  &fEqConfFrontendFileName, true, 256);
   //fOdbEqCommon->RS("Status",              &fEqConfStatus,           true, 256);
   //fOdbEqCommon->RS("Status color",        &fEqConfStatusColor,      true, NAME_LENGTH);

   return TMFeOk();
}

TMFeResult TMFeEquipment::EqWriteCommon(bool create)
{
   if (TMFE::gfVerbose)
      printf("TMFeEquipment::EqWriteCommon: for [%s]\n", fEqName.c_str());

   // encode data for ODB Common

   fEqConfReadOn = 0;
   if (fEqConfReadOnlyWhenRunning)
      fEqConfReadOn |= (RO_RUNNING);
   else
      fEqConfReadOn |= (RO_RUNNING|RO_PAUSED|RO_STOPPED);
   if (fEqConfWriteEventsToOdb)
      fEqConfReadOn |= RO_ODB;

   // write to ODB
   
   fOdbEqCommon->WU16("Event ID",          fEqConfEventID);
   fOdbEqCommon->WU16("Trigger mask",      fEqConfTriggerMask);
   fOdbEqCommon->WS("Buffer",              fEqConfBuffer.c_str(), NAME_LENGTH);
   fOdbEqCommon->WI("Type",                fEqConfType);
   fOdbEqCommon->WI("Source",              fEqConfSource);
   fOdbEqCommon->WS("Format",              fEqConfFormat.c_str(), 8);
   fOdbEqCommon->WB("Enabled",             fEqConfEnabled);
   fOdbEqCommon->WI("Read on",             fEqConfReadOn);
   fOdbEqCommon->WI("Period",              fEqConfPeriodMilliSec);
   fOdbEqCommon->WD("Event limit",         fEqConfEventLimit);
   fOdbEqCommon->WU32("Num subevents",     fEqConfNumSubEvents);
   fOdbEqCommon->WI("Log history",         fEqConfLogHistory);
   fOdbEqCommon->WS("Frontend host",       fMfe->fHostname.c_str(), NAME_LENGTH);
   fOdbEqCommon->WS("Frontend name",       fMfe->fProgramName.c_str(), NAME_LENGTH);
   fOdbEqCommon->WS("Frontend file name",  fEqFilename.c_str(), 256);
   if (create) {
      fOdbEqCommon->WS("Status",           "", 256);
      fOdbEqCommon->WS("Status color",     "", NAME_LENGTH);
   }
   fOdbEqCommon->WB("Hidden",              fEqConfHidden);
   fOdbEqCommon->WI("Write cache size",    fEqConfWriteCacheSize);
   return TMFeOk();
}

TMFeResult TMFeEquipment::EqPreInit()
{
   if (TMFE::gfVerbose)
      printf("TMFeEquipment::PreInit: for [%s]\n", fEqName.c_str());

   //
   // Apply frontend index
   //

   if (fEqName.find("%") != std::string::npos) {
      fEqName = msprintf(fEqName.c_str(), fFe->fFeIndex);
   }

   if (fEqConfBuffer.find("%") != std::string::npos) {
      fEqConfBuffer = msprintf(fEqConfBuffer.c_str(), fFe->fFeIndex);
   }

   //
   // create ODB /eq/name/common
   //

   fOdbEq = fMfe->fOdbRoot->Chdir((std::string("Equipment/") + fEqName).c_str(), true);
   fOdbEqCommon     = fOdbEq->Chdir("Common", false);
   if (!fOdbEqCommon) {
      if (TMFE::gfVerbose)
         printf("TMFeEquipment::PreInit: creating ODB common\n");
      fOdbEqCommon  = fOdbEq->Chdir("Common", true);
      EqWriteCommon(true);
   }
   fOdbEqSettings   = fOdbEq->Chdir("Settings", true);
   fOdbEqVariables  = fOdbEq->Chdir("Variables", true);
   fOdbEqStatistics = fOdbEq->Chdir("Statistics", true);

   TMFeResult r = EqReadCommon();

   if (r.error_flag)
      return r;

   if (rpc_is_remote()) {
      EqSetStatus((fMfe->fProgramName + "@" + fMfe->fHostname).c_str(), "greenLight");
   } else {
      EqSetStatus(fMfe->fProgramName.c_str(), "greenLight");
   }

   EqZeroStatistics();
   EqWriteStatistics();

   return TMFeOk();
}

TMFeResult TMFeEquipment::EqPostInit()
{
   if (TMFE::gfVerbose)
      printf("TMFeEquipment::EqPostInit: for [%s]\n", fEqName.c_str());

   if (!fEqConfEnabled) {
      EqSetStatus("Disabled", "yellowLight");
   }

   // open event buffer
   
   uint32_t odb_max_event_size = DEFAULT_MAX_EVENT_SIZE;
   fMfe->fOdbRoot->RU32("Experiment/MAX_EVENT_SIZE", &odb_max_event_size, true);

   if (fEqConfMaxEventSize == 0) {
      fEqConfMaxEventSize = odb_max_event_size;
   } else if (fEqConfMaxEventSize > odb_max_event_size) {
      fMfe->Msg(MERROR, "TMFeEquipment::EqPostInit", "Equipment \"%s\" requested event size %d is bigger than ODB MAX_EVENT_SIZE %d", fEqName.c_str(), (int)fEqConfMaxEventSize, odb_max_event_size);
      fEqConfMaxEventSize = odb_max_event_size;
   }

   if (!fEqConfBuffer.empty()) {
      TMFeResult r = fMfe->EventBufferOpen(&fEqEventBuffer, fEqConfBuffer.c_str(), fEqConfBufferSize);

      if (r.error_flag)
         return r;

      assert(fEqEventBuffer != NULL);

      if (fEqConfMaxEventSize > fEqEventBuffer->fBufMaxEventSize) {
         fMfe->Msg(MERROR, "TMFeEquipment::EqPostInit", "Equipment \"%s\" requested event size %d is bigger than event buffer \"%s\" max event size %d", fEqName.c_str(), (int)fEqConfMaxEventSize, fEqEventBuffer->fBufName.c_str(), (int)fEqEventBuffer->fBufMaxEventSize);
         fEqConfMaxEventSize = fEqEventBuffer->fBufMaxEventSize;
      }

      if (fEqConfWriteCacheSize > 0) {
         if (fEqEventBuffer->fBufWriteCacheSize == 0) {
            r = fEqEventBuffer->SetCacheSize(0, fEqConfWriteCacheSize);

            if (r.error_flag)
               return r;
         } else if (fEqConfWriteCacheSize < fEqEventBuffer->fBufWriteCacheSize) {
            fMfe->Msg(MERROR, "TMFeEquipment::EqPostInit", "Equipment \"%s\" requested write cache size %d for buffer \"%s\" is smaller then already set write cache size %d, ignoring it", fEqName.c_str(), (int)fEqConfWriteCacheSize, fEqEventBuffer->fBufName.c_str(), (int)fEqEventBuffer->fBufWriteCacheSize);
         } else if (fEqConfWriteCacheSize == fEqEventBuffer->fBufWriteCacheSize) {
            // do nothing
         } else {
            fMfe->Msg(MERROR, "TMFeEquipment::EqPostInit", "Equipment \"%s\" requested write cache size %d for buffer \"%s\" is different from already set write cache size %d", fEqName.c_str(), (int)fEqConfWriteCacheSize, fEqEventBuffer->fBufName.c_str(), (int)fEqEventBuffer->fBufWriteCacheSize);
         
            r = fEqEventBuffer->SetCacheSize(0, fEqConfWriteCacheSize);
            
            if (r.error_flag)
               return r;
         }
      }
   }

   if (TMFE::gfVerbose)
      printf("TMFeEquipment::EqPostInit: Equipment \"%s\", max event size: %d\n", fEqName.c_str(), (int)fEqConfMaxEventSize);

   // update ODB common

   TMFeResult r = EqWriteCommon();

   if (r.error_flag)
      return r;

   if (fEqConfEnabled && fEqConfEnableRpc) {
      fMfe->AddRpcHandler(this);
   }

   return TMFeOk();
};

TMFeResult TMFeEquipment::EqZeroStatistics()
{
   fEqMutex.lock();

   if (TMFE::gfVerbose)
      printf("TMFeEquipment::EqZeroStatistics: zero statistics for [%s]\n", fEqName.c_str());

   double now = TMFE::GetTime();
   
   fEqStatEvents = 0;
   fEqStatBytes = 0;
   fEqStatEpS = 0;
   fEqStatKBpS = 0;
   
   fEqStatLastTime = now;
   fEqStatLastEvents = 0;
   fEqStatLastBytes = 0;

   fEqStatNextWrite = now; // force immediate update

   fEqMutex.unlock();

   return TMFeOk();
}

TMFeResult TMFeEquipment::EqWriteStatistics()
{
   fEqMutex.lock();

   if (TMFE::gfVerbose)
      printf("TMFeEquipment::EqWriteStatistics: write statistics for [%s]\n", fEqName.c_str());

   double now = TMFE::GetTime();
   double elapsed = now - fEqStatLastTime;

   if (elapsed > 0.9 || fEqStatLastTime == 0) {
      fEqStatEpS = (fEqStatEvents - fEqStatLastEvents) / elapsed;
      fEqStatKBpS = (fEqStatBytes - fEqStatLastBytes) / elapsed / 1000.0;

      fEqStatLastTime = now;
      fEqStatLastEvents = fEqStatEvents;
      fEqStatLastBytes = fEqStatBytes;
   }

   //printf("TMFeEquipment::EqWriteStatistics: write statistics for [%s], now %f, elapsed %f, sent %f, eps %f, kps %f\n", fEqName.c_str(), now, elapsed, fEqStatEvents, fEqStatEpS, fEqStatKBpS);

   fOdbEqStatistics->WD("Events sent", fEqStatEvents);
   fOdbEqStatistics->WD("Events per sec.", fEqStatEpS);
   fOdbEqStatistics->WD("kBytes per sec.", fEqStatKBpS);

   fEqStatLastWrite = now;

   if (fEqConfPeriodStatisticsSec > 0) {
      // avoid creep of NextWrite: we start it at
      // time of initialization, then increment it strictly
      // by the period value, regardless of when it is actually
      // written to ODB (actual period is longer than requested
      // period because we only over-sleep, never under-sleep). K.O.
      while (fEqStatNextWrite <= now) {
         fEqStatNextWrite += fEqConfPeriodStatisticsSec;
      }
   } else {
      fEqStatNextWrite = now;
   }

   fEqMutex.unlock();
   
   return TMFeOk();
}

TMFeResult TMFeEquipment::ComposeEvent(char* event, size_t size) const
{
   EVENT_HEADER* pevent = (EVENT_HEADER*)event;
   pevent->event_id = fEqConfEventID;
   pevent->trigger_mask = fEqConfTriggerMask;
   pevent->serial_number = fEqSerial;
   pevent->time_stamp = TMFE::GetTime();
   pevent->data_size = 0;
   return TMFeOk();
}

TMFeResult TMFeEquipment::EqSendEvent(const char* event, bool write_to_odb)
{
   std::lock_guard<std::mutex> guard(fEqMutex);
   
   fEqSerial++;

   if (fEqEventBuffer == NULL) {
      return TMFeOk();
   }

   EVENT_HEADER* pevent = (EVENT_HEADER*)event;
   pevent->data_size = BkSize(event);

   TMFeResult r = fEqEventBuffer->SendEvent(event);

   if (r.error_flag)
      return r;

   fEqStatEvents += 1;
   fEqStatBytes  += sizeof(EVENT_HEADER) + pevent->data_size;

   if (fEqConfWriteEventsToOdb && write_to_odb) {
      TMFeResult r = EqWriteEventToOdb_locked(event);
      if (r.error_flag)
         return r;
   }

   if (fMfe->fStateRunning) {
      if (fEqConfEventLimit > 0) {
         if (fEqStatEvents >= fEqConfEventLimit) {
            if (!fMfe->fRunStopRequested) {
               fMfe->Msg(MINFO, "TMFeEquipment::EqSendEvent", "Equipment \"%s\" sent %.0f events out of %.0f requested, run will stop now", fEqName.c_str(), fEqStatEvents, fEqConfEventLimit);
            }
            fMfe->fRunStopRequested = true;
         }
      }
   }

   return TMFeOk();
}

TMFeResult TMFeEquipment::EqSendEvent(const std::vector<char>& event, bool write_to_odb)
{
   std::lock_guard<std::mutex> guard(fEqMutex);
   
   fEqSerial++;

   if (fEqEventBuffer == NULL) {
      return TMFeOk();
   }

   TMFeResult r = fEqEventBuffer->SendEvent(event);

   if (r.error_flag)
      return r;

   fEqStatEvents += 1;
   fEqStatBytes  += event.size();

   if (fEqConfWriteEventsToOdb && write_to_odb) {
      TMFeResult r = EqWriteEventToOdb_locked(event.data());
      if (r.error_flag)
         return r;
   }

   if (fMfe->fStateRunning) {
      if (fEqConfEventLimit > 0) {
         if (fEqStatEvents >= fEqConfEventLimit) {
            if (!fMfe->fRunStopRequested) {
               fMfe->Msg(MINFO, "TMFeEquipment::EqSendEvent", "Equipment \"%s\" sent %.0f events out of %.0f requested, run will stop now", fEqName.c_str(), fEqStatEvents, fEqConfEventLimit);
            }
            fMfe->fRunStopRequested = true;
         }
      }
   }

   return TMFeOk();
}

TMFeResult TMFeEquipment::EqSendEvent(const std::vector<std::vector<char>>& event, bool write_to_odb)
{
   std::lock_guard<std::mutex> guard(fEqMutex);
   
   fEqSerial++;

   if (fEqEventBuffer == NULL) {
      return TMFeOk();
   }

   TMFeResult r = fEqEventBuffer->SendEvent(event);

   if (r.error_flag)
      return r;

   fEqStatEvents += 1;
   for (auto v: event) {
      fEqStatBytes += v.size();
   }

   //if (fEqConfWriteEventsToOdb && write_to_odb) {
   //   TMFeResult r = EqWriteEventToOdb_locked(event.data());
   //   if (r.error_flag)
   //      return r;
   //}

   if (fMfe->fStateRunning) {
      if (fEqConfEventLimit > 0) {
         if (fEqStatEvents >= fEqConfEventLimit) {
            if (!fMfe->fRunStopRequested) {
               fMfe->Msg(MINFO, "TMFeEquipment::EqSendEvent", "Equipment \"%s\" sent %.0f events out of %.0f requested, run will stop now", fEqName.c_str(), fEqStatEvents, fEqConfEventLimit);
            }
            fMfe->fRunStopRequested = true;
         }
      }
   }

   return TMFeOk();
}

TMFeResult TMFeEquipment::EqSendEvent(int sg_n, const char* sg_ptr[], const size_t sg_len[], bool write_to_odb)
{
   std::lock_guard<std::mutex> guard(fEqMutex);
   
   fEqSerial++;

   if (fEqEventBuffer == NULL) {
      return TMFeOk();
   }

   TMFeResult r = fEqEventBuffer->SendEvent(sg_n, sg_ptr, sg_len);

   if (r.error_flag)
      return r;

   fEqStatEvents += 1;
   for (int i=0; i<sg_n; i++) {
      fEqStatBytes += sg_len[i];
   }

   //if (fEqConfWriteEventsToOdb && write_to_odb) {
   //   TMFeResult r = EqWriteEventToOdb_locked(event.data());
   //   if (r.error_flag)
   //      return r;
   //}

   if (fMfe->fStateRunning) {
      if (fEqConfEventLimit > 0) {
         if (fEqStatEvents >= fEqConfEventLimit) {
            if (!fMfe->fRunStopRequested) {
               fMfe->Msg(MINFO, "TMFeEquipment::EqSendEvent", "Equipment \"%s\" sent %.0f events out of %.0f requested, run will stop now", fEqName.c_str(), fEqStatEvents, fEqConfEventLimit);
            }
            fMfe->fRunStopRequested = true;
         }
      }
   }

   return TMFeOk();
}

TMFeResult TMFeEquipment::EqWriteEventToOdb(const char* event)
{
   std::lock_guard<std::mutex> guard(fEqMutex);
   return EqWriteEventToOdb_locked(event);
}

TMFeResult TMFeEquipment::EqWriteEventToOdb_locked(const char* event)
{
   std::string path = "";
   path += "/Equipment/";
   path += fEqName;
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
   bk_init32a(event + sizeof(EVENT_HEADER));
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

TMFeResult TMFeEquipment::EqSetStatus(char const* eq_status, char const* eq_color)
{
   if (eq_status) {
      fOdbEqCommon->WS("Status", eq_status, 256);
   }

   if (eq_color) {
      fOdbEqCommon->WS("Status color", eq_color, NAME_LENGTH);
   }

   return TMFeOk();
}

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

void TMFrontend::FeUsage(const char* argv0)
{
   fprintf(stderr, "\n");
   fprintf(stderr, "Usage: %s args... [-- equipment args...]\n", argv0);
   fprintf(stderr, "\n");
   fprintf(stderr, " --help -- print this help message\n");
   fprintf(stderr, " -h -- print this help message\n");
   fprintf(stderr, " -v -- report all activities\n");
   fprintf(stderr, "\n");
   fprintf(stderr, " -h hostname[:tcpport] -- connect to MIDAS mserver on given host and tcp port number\n");
   fprintf(stderr, " -e exptname -- connect to given MIDAS experiment\n");
   fprintf(stderr, "\n");
   fprintf(stderr, " -D -- Become a daemon\n");
   fprintf(stderr, " -O -- Become a daemon but keep stdout for saving in a log file: frontend -O >file.log 2>&1\n");
   fprintf(stderr, "\n");
   fprintf(stderr, " -i NNN -- Set frontend index number\n");
   fprintf(stderr, "\n");

   // NOTE: cannot use range-based for() loop, it uses an iterator and will crash if HandleUsage() modifies fEquipments. K.O.
   for (unsigned i=0; i<fFeEquipments.size(); i++) {
      if (!fFeEquipments[i])
         continue;
      fprintf(stderr, "Usage of equipment \"%s\":\n", fFeEquipments[i]->fEqName.c_str());
      fprintf(stderr, "\n");
      fFeEquipments[i]->HandleUsage();
      fprintf(stderr, "\n");
   }
}

int TMFrontend::FeMain(int argc, char* argv[])
{
   std::vector<std::string> args;
   for (int i=0; i<argc; i++) {
      args.push_back(argv[i]);
   }

   return FeMain(args);
}

TMFeResult TMFrontend::FeInit(const std::vector<std::string> &args)
{
   setbuf(stdout, NULL);
   setbuf(stderr, NULL);

   signal(SIGPIPE, SIG_IGN);

   std::vector<std::string> eq_args;

   bool help = false;
   std::string exptname;
   std::string hostname;
   bool daemon0 = false;
   bool daemon1 = false;

   for (unsigned int i=1; i<args.size(); i++) { // loop over the commandline options
      //printf("argv[%d] is %s\n", i, args[i].c_str());
      if (args[i] == "--") {
         // remaining arguments are passed to equipment Init()
         for (unsigned j=i+1; j<args.size(); j++)
            eq_args.push_back(args[j]);
         break;
      } else if (args[i] == "-v") {
         TMFE::gfVerbose = true;
      } else if (args[i] == "-D") {
         daemon0 = true;
      } else if (args[i] == "-O") {
         daemon1 = true;
      } else if (args[i] == "-h") {
         i++;
         if (i >= args.size()) { help = true; break; }
         hostname = args[i];
      } else if (args[i] == "-e") {
         i++;
         if (i >= args.size()) { help = true; break; }
         exptname = args[i];
      } else if (args[i] == "-i") {
         i++;
         if (i >= args.size()) { help = true; break; }
         fFeIndex = atoi(args[i].c_str());
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

   //
   // daemonize...
   //

   if (daemon0) {
      printf("Becoming a daemon...\n");
      ss_daemon_init(FALSE);
   } else if (daemon1) {
      printf("Becoming a daemon...\n");
      ss_daemon_init(TRUE);
   }

   //
   // apply frontend index to indexed frontend
   //

   if (fMfe->fProgramName.find("%") != std::string::npos) {
      fMfe->fProgramName = msprintf(fMfe->fProgramName.c_str(), fFeIndex);
   }
      
   TMFeResult r;

   // call arguments handler before calling the usage handlers. Otherwise,
   // if the arguments handler creates new equipments,
   // we will never see their Usage(). K.O.
   r = HandleArguments(eq_args);

   if (r.error_flag) {
      fprintf(stderr, "Fatal error: arguments handler error: %s, bye.\n", r.error_message.c_str());
      exit(1);
   }

   if (help) {
      FeUsage(args[0].c_str());
      HandleUsage();
      exit(1);
   }

   r = fMfe->Connect(NULL, hostname.c_str(), exptname.c_str());

   if (r.error_flag) {
      fprintf(stderr, "Fatal error: cannot connect to MIDAS, error: %s, bye.\n", r.error_message.c_str());
      exit(1);
   }

   r = HandleFrontendInit(eq_args);

   if (r.error_flag) {
      fprintf(stderr, "Fatal error: frontend init error: %s, bye.\n", r.error_message.c_str());
      exit(1);
   }

   fFeRpcHelper = new TMFrontendRpcHelper(this);
   fMfe->AddRpcHandler(fFeRpcHelper);

   //mfe->SetWatchdogSec(0);
   //mfe->SetTransitionSequenceStart(910);
   //mfe->SetTransitionSequenceStop(90);
   //mfe->DeregisterTransitionPause();
   //mfe->DeregisterTransitionResume();
   //mfe->RegisterTransitionStartAbort();

   r = FeInitEquipments(eq_args);

   if (r.error_flag) {
      fprintf(stderr, "Cannot initialize equipments, error message: %s, bye.\n", r.error_message.c_str());
      exit(1);
   }

   r = HandleFrontendReady(eq_args);

   if (r.error_flag) {
      fprintf(stderr, "Fatal error: frontend post-init error: %s, bye.\n", r.error_message.c_str());
      exit(1);
   }

   int run_state = 0;
   int run_number = 0;

   fMfe->fOdbRoot->RI("Runinfo/state", &run_state);
   fMfe->fOdbRoot->RI("Runinfo/run number", &run_number);

   if (run_state == STATE_RUNNING) {
      if (fMfe->fIfRunningCallExit) {
         fprintf(stderr, "Fatal error: Cannot start frontend, run is in progress!\n");
         exit(1);
      } else if (fMfe->fIfRunningCallBeginRun) {
         char errstr[TRANSITION_ERROR_STRING_LENGTH];
         tr_start(run_number, errstr);
      } else {
         fMfe->fRunNumber = run_number;
         fMfe->fStateRunning = true;
      }
   }

   return TMFeOk();
}

void TMFrontend::FeMainLoop()
{
   while (!fMfe->fShutdownRequested) {
      FePollMidas(0.100);
   }
}
   
void TMFrontend::FeShutdown()
{
   fMfe->StopRpcThread();
   FeStopPeriodicThread();
   FeStopEquipmentPollThreads();
   HandleFrontendExit();
   FeDeleteEquipments();
   fMfe->Disconnect();
}

int TMFrontend::FeMain(const std::vector<std::string> &args)
{
   TMFeResult r = FeInit(args);

   if (r.error_flag) {
      fprintf(stderr, "Fatal error: frontend init error: %s, bye.\n", r.error_message.c_str());
      exit(1);
   }

   FeMainLoop();
   FeShutdown();

   return 0;
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
