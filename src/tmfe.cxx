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

TMFE::TMFE() // ctor
{
   fDB = 0;
   fOdbRoot = NULL;
   fShutdownRequested = false;
   fNextPeriodic = 0;
}

TMFE::~TMFE() // dtor
{
   assert(!"TMFE::~TMFE(): destruction of the TMFE singleton is not permitted!");
}

TMFE* TMFE::Instance()
{
   if (!gfMFE)
      gfMFE = new TMFE();
   
   return gfMFE;
}

TMFeError TMFE::Connect(const char* progname, const char* filename, const char* hostname, const char* exptname)
{
   if (progname)
      fFrontendName     = progname;
   if (filename)
      fFrontendFilename = filename;

   char local_hostname[256];
   local_hostname[0] = 0;
   ss_gethostname(local_hostname, sizeof(local_hostname));
   fFrontendHostname = local_hostname;

   int status;
  
   char xhostname[HOST_NAME_LENGTH];
   char xexptname[NAME_LENGTH];
   
   /* get default from environment */
   status = cm_get_environment(xhostname, sizeof(xhostname), xexptname, sizeof(xexptname));
   assert(status == CM_SUCCESS);
   
   if (hostname)
      strlcpy(xhostname, hostname, sizeof(xhostname));
   
   if (exptname)
      strlcpy(xexptname, exptname, sizeof(xexptname));
   
   fHostname = xhostname;
   fExptname = xexptname;
   
   fprintf(stderr, "TMFE::Connect: Program \"%s\" connecting to experiment \"%s\" on host \"%s\"\n", progname, fExptname.c_str(), fHostname.c_str());
   
   int watchdog = DEFAULT_WATCHDOG_TIMEOUT;
   //int watchdog = 60*1000;
   
   status = cm_connect_experiment1(fHostname.c_str(), fExptname.c_str(), progname, NULL, DEFAULT_ODB_SIZE, watchdog);
   
   if (status == CM_UNDEF_EXP) {
      fprintf(stderr, "TMidasOnline::connect: Error: experiment \"%s\" not defined.\n", fExptname.c_str());
      return TMFeError(status, "experiment is not defined");
   } else if (status != CM_SUCCESS) {
      fprintf(stderr, "TMidasOnline::connect: Cannot connect to MIDAS, status %d.\n", status);
      return TMFeError(status, "cannot connect");
   }

   status = cm_get_experiment_database(&fDB, NULL);
   if (status != CM_SUCCESS) {
      return TMFeError(status, "cm_get_experiment_database");
   }

   fOdbRoot = MakeMidasOdb(fDB);
  
   return TMFeError();
}

TMFeError TMFE::SetWatchdogSec(int sec)
{
   if (sec == 0) {
      cm_set_watchdog_params(false, 0);
   } else {
      cm_set_watchdog_params(true, sec*1000);
   }
   return TMFeError();
}

TMFeError TMFE::Disconnect()
{
   fprintf(stderr, "TMFE::Disconnect: Disconnecting from experiment \"%s\" on host \"%s\"\n", fExptname.c_str(), fHostname.c_str());
   cm_disconnect_experiment();
   return TMFeError();
}

TMFeError TMFE::RegisterEquipment(TMFeEquipment* eq)
{
   fEquipments.push_back(eq);
   return TMFeError();
}

void TMFE::EquipmentPeriodicTasks()
{
   double now = GetTime();

   if (fNextPeriodic == 0 || now >= fNextPeriodic) {
      int n = fPeriodicHandlers.size();
      fNextPeriodic = 0;
      for (int i=0; i<n; i++) {
         TMFePeriodicHandler* h = fPeriodicHandlers[i];
         double period = h->fEq->fCommon->Period/1000.0;
         //printf("periodic[%d] period %f, last call %f, next call %f (+%f)\n", i, period, h->fLastCallTime, h->fNextCallTime, now - h->fNextCallTime);
         if (period <= 0)
            continue;
         if (h->fNextCallTime == 0 || now >= h->fNextCallTime) {
            h->fLastCallTime = now;
            h->fNextCallTime = h->fLastCallTime + period;

            if (h->fNextCallTime < now) {
               fprintf(stderr, "TMFE::EquipmentPeriodicTasks: periodic equipment does not keep up!\n"); // FIXME
               while (h->fNextCallTime < now) {
                  h->fNextCallTime += period;
               }
            }

            if (fNextPeriodic == 0)
               fNextPeriodic = h->fNextCallTime;
            else if (h->fNextCallTime < fNextPeriodic)
               fNextPeriodic = h->fNextCallTime;

            h->fHandler->HandlePeriodic();

            now = GetTime();
         }
      }

      //printf("next periodic %f (+%f)\n", fNextPeriodic, fNextPeriodic - now);
   } else {
      //printf("next periodic %f (+%f), waiting\n", fNextPeriodic, fNextPeriodic - now);
   }
}

void TMFE::PollMidas(int msec)
{
   double now = GetTime();
   //double sleep_start = now;
   double sleep_end = now + msec/1000.0;

   while (!fShutdownRequested) {
      EquipmentPeriodicTasks();

      now = GetTime();

      double sleep_time = sleep_end - now;
      int s = 0;
      if (sleep_time > 0)
         s = 1 + sleep_time*1000.0;

      //printf("now %f, sleep_end %f, s %d\n", now, sleep_end, s);
      
      int status = cm_yield(s);
      
      if (status == RPC_SHUTDOWN || status == SS_ABORT) {
         fShutdownRequested = true;
         fprintf(stderr, "TMFE::PollMidas: cm_yield(%d) status %d, shutdown requested...\n", msec, status);
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

std::string TMFeRpcHandlerInterface::HandleRpc(const char* cmd, const char* args)
{
   return "";
}

void TMFeRpcHandlerInterface::HandleBeginRun()
{
}

void TMFeRpcHandlerInterface::HandleEndRun()
{
}

void TMFeRpcHandlerInterface::HandlePauseRun()
{
}

void TMFeRpcHandlerInterface::HandleResumeRun()
{
}

static INT rpc_callback(INT index, void *prpc_param[])
{
   const char* cmd  = CSTRING(0);
   const char* args = CSTRING(1);
   char* return_buf = CSTRING(2);
   int   return_max_length = CINT(3);

   cm_msg(MINFO, "rpc_callback", "--------> rpc_callback: index %d, max_length %d, cmd [%s], args [%s]", index, return_max_length, cmd, args);

   TMFE* mfe = TMFE::Instance();

   for (unsigned i=0; i<mfe->fRpcHandlers.size(); i++) {
      std::string r = mfe->fRpcHandlers[i]->HandleRpc(cmd, args);
      if (r.length() > 0) {
         //printf("Handler reply [%s]\n", C(r));
         strlcpy(return_buf, r.c_str(), return_max_length);
         return RPC_SUCCESS;
      }
   }

   return_buf[0] = 0;
   return RPC_SUCCESS;
}

static INT tr_start(INT runno, char *errstr)
{
   cm_msg(MINFO, "tr_start", "tr_start");

   TMFE* mfe = TMFE::Instance();
   
   for (unsigned i=0; i<mfe->fEquipments.size(); i++) {
      mfe->fEquipments[i]->ZeroStatistics();
      mfe->fEquipments[i]->WriteStatistics();
   }

   for (unsigned i=0; i<mfe->fRpcHandlers.size(); i++) {
      mfe->fRpcHandlers[i]->HandleBeginRun();
   }

   return SUCCESS;
}

static INT tr_stop(INT runno, char *errstr)
{
   cm_msg(MINFO, "tr_stop", "tr_stop");

   TMFE* mfe = TMFE::Instance();
   for (unsigned i=0; i<mfe->fRpcHandlers.size(); i++) {
      mfe->fRpcHandlers[i]->HandleEndRun();
   }

   for (unsigned i=0; i<mfe->fEquipments.size(); i++) {
      mfe->fEquipments[i]->WriteStatistics();
   }


   return SUCCESS;
}

static INT tr_pause(INT runno, char *errstr)
{
   cm_msg(MINFO, "tr_pause", "tr_pause");

   TMFE* mfe = TMFE::Instance();
   for (unsigned i=0; i<mfe->fRpcHandlers.size(); i++) {
      mfe->fRpcHandlers[i]->HandlePauseRun();
   }

   return SUCCESS;
}

static INT tr_resume(INT runno, char *errstr)
{
   cm_msg(MINFO, "tr_resume", "tr_resume");

   TMFE* mfe = TMFE::Instance();
   for (unsigned i=0; i<mfe->fRpcHandlers.size(); i++) {
      mfe->fRpcHandlers[i]->HandleResumeRun();
   }

   return SUCCESS;
}

void TMFE::RegisterRpcHandler(TMFeRpcHandlerInterface* h)
{
   if (fRpcHandlers.size() == 0) {
      // for the first handler, register with MIDAS
      cm_register_function(RPC_JRPC, rpc_callback);
      cm_register_transition(TR_START, tr_start, 500);
      cm_register_transition(TR_STOP, tr_stop, 500);
      cm_register_transition(TR_PAUSE, tr_pause, 500);
      cm_register_transition(TR_RESUME, tr_resume, 500);
   }

   fRpcHandlers.push_back(h);
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

void TMFE::DeregisterTransitions()
{
   cm_deregister_transition(TR_START);
   cm_deregister_transition(TR_STOP);
   cm_deregister_transition(TR_PAUSE);
   cm_deregister_transition(TR_RESUME);
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

TMFePeriodicHandler::TMFePeriodicHandler()
{
   fEq = NULL;
   fHandler = NULL;
   fLastCallTime = 0;
   fNextCallTime = 0;
}

TMFePeriodicHandler::~TMFePeriodicHandler()
{
   fEq = NULL; // no delete, we do not own this object
   fHandler = NULL; // no delete, we do not own this object
   fLastCallTime = 0;
   fNextCallTime = 0;
}

void TMFE::RegisterPeriodicHandler(TMFeEquipment* eq, TMFePeriodicHandlerInterface* h)
{
   TMFePeriodicHandler *p = new TMFePeriodicHandler();
   p->fEq = eq;
   p->fHandler = h;
   fPeriodicHandlers.push_back(p);
   fNextPeriodic = 0;
}

TMFeCommon::TMFeCommon() // ctor
{
   EventID = 1;
   TriggerMask = 0;
   Buffer = "SYSTEM";
   Type = 0;
   Source = 0;
   Format = "MIDAS";
   Enabled = true;
   ReadOn = 0;
   Period = 1000;
   EventLimit = 0;
   NumSubEvents = 0;
   LogHistory = 1;
   //FrontendHost;
   //FrontendName;
   //FrontendFileName;
   //Status;
   //StatusColor;
   Hidden = false;
   WriteCacheSize = 100000;
};

TMFeEquipment::TMFeEquipment(TMFE* mfe, const char* name, TMFeCommon* common) // ctor
{
   fMfe  = mfe;
   fName = name;
   fCommon = common;
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
}

TMFeError TMFeEquipment::Init()
{
   //
   // create ODB /eq/name/common
   //

   fOdbEq = fMfe->fOdbRoot->Chdir((std::string("Equipment/") + fName).c_str(), true);
   fOdbEqCommon     = fOdbEq->Chdir("Common", true);
   fOdbEqSettings   = fOdbEq->Chdir("Settings", true);
   fOdbEqVariables  = fOdbEq->Chdir("Variables", true);
   fOdbEqStatistics = fOdbEq->Chdir("Statistics", true);

   fOdbEqCommon->RU16("Event ID",     &fCommon->EventID, true);
   fOdbEqCommon->RU16("Trigger mask", &fCommon->TriggerMask, true);
   fOdbEqCommon->RS("Buffer",         &fCommon->Buffer, true, NAME_LENGTH);
   fOdbEqCommon->RI("Type",           &fCommon->Type, true);
   fOdbEqCommon->RI("Source",         &fCommon->Source, true);
   fOdbEqCommon->RS("Format",         &fCommon->Format, true, 8);
   fOdbEqCommon->RB("Enabled",        &fCommon->Enabled, true);
   fOdbEqCommon->RI("Read on",        &fCommon->ReadOn, true);
   fOdbEqCommon->RI("Period",         &fCommon->Period, true);
   fOdbEqCommon->RD("Event limit",    &fCommon->EventLimit, true);
   fOdbEqCommon->RU32("Num subevents",  &fCommon->NumSubEvents, true);
   fOdbEqCommon->RI("Log history",    &fCommon->LogHistory, true);
   fOdbEqCommon->RS("Frontend host",  &fCommon->FrontendHost, true, NAME_LENGTH);
   fOdbEqCommon->RS("Frontend name",  &fCommon->FrontendName, true, NAME_LENGTH);
   fOdbEqCommon->RS("Frontend file name",  &fCommon->FrontendFileName, true, 256);
   fOdbEqCommon->RS("Status",         &fCommon->Status, true, 256);
   fOdbEqCommon->RS("Status color",   &fCommon->StatusColor, true, NAME_LENGTH);
   fOdbEqCommon->RB("Hidden",         &fCommon->Hidden, true);
   fOdbEqCommon->RI("Write cache size", &fCommon->WriteCacheSize, true);

   fCommon->FrontendHost = fMfe->fFrontendHostname;
   fCommon->FrontendName = fMfe->fFrontendName;
   fCommon->FrontendFileName = fMfe->fFrontendFilename;

   fCommon->Status = "";
   fCommon->Status += fMfe->fFrontendName;
   fCommon->Status += "@";
   fCommon->Status += fMfe->fFrontendHostname;
   fCommon->StatusColor = "greenLight";

   if (fCommon->Buffer.length() > 0) {
      int status = bm_open_buffer(fCommon->Buffer.c_str(), DEFAULT_BUFFER_SIZE, &fBufferHandle);
      if (status != BM_SUCCESS) {
         return TMFeError(status, "bm_open_buffer");
      }
   }

   fOdbEqCommon->WS("Frontend host", fCommon->FrontendHost.c_str(), NAME_LENGTH);
   fOdbEqCommon->WS("Frontend name", fCommon->FrontendName.c_str(), NAME_LENGTH);
   fOdbEqCommon->WS("Frontend file name", fCommon->FrontendFileName.c_str(), 256);
   fOdbEqCommon->WS("Status", fCommon->Status.c_str(), 256);
   fOdbEqCommon->WS("Status color", fCommon->StatusColor.c_str(), NAME_LENGTH);

   ZeroStatistics();
   WriteStatistics();

   return TMFeError();
};

TMFeError TMFeEquipment::ZeroStatistics()
{
   fStatEvents = 0;
   fStatBytes = 0;
   fStatEpS = 0;
   fStatKBpS = 0;
   
   fStatLastTime = 0;
   fStatLastEvents = 0;
   fStatLastBytes = 0;

   return TMFeError();
}

TMFeError TMFeEquipment::WriteStatistics()
{
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

   return TMFeError();
}

TMFeError TMFeEquipment::ComposeEvent(char* event, int size)
{
   EVENT_HEADER* pevent = (EVENT_HEADER*)event;
   pevent->event_id = fCommon->EventID;
   pevent->trigger_mask = fCommon->TriggerMask;
   pevent->serial_number = fSerial;
   pevent->time_stamp = TMFE::GetTime();
   pevent->data_size = 0;
   return TMFeError();
}

TMFeError TMFeEquipment::SendData(const char* buf, int size)
{
   if (fBufferHandle == 0) {
      return TMFeError();
   }
   int status = bm_send_event(fBufferHandle, (const EVENT_HEADER*)buf, size, BM_WAIT);
   if (status == BM_CORRUPTED) {
      TMFE::Instance()->Msg(MERROR, "TMFeEquipment::SendData", "bm_send_event() returned %d, event buffer is corrupted, shutting down the frontend", status);
      TMFE::Instance()->fShutdownRequested = true;
      return TMFeError(status, "bm_send_event: event buffer is corrupted, shutting down the frontend");
   } else if (status != BM_SUCCESS) {
      return TMFeError(status, "bm_send_event");
   }
   fStatEvents += 1;
   fStatBytes  += size;
   return TMFeError();
}

TMFeError TMFeEquipment::SendEvent(const char* event)
{
   fSerial++;
   return SendData(event, sizeof(EVENT_HEADER) + BkSize(event));
}

int TMFeEquipment::BkSize(const char* event)
{
   return bk_size((void*)(event + sizeof(EVENT_HEADER))); // FIXME: need const in prototype!
}

TMFeError TMFeEquipment::BkInit(char* event, int size)
{
   bk_init32(event + sizeof(EVENT_HEADER));
   return TMFeError();
}

void* TMFeEquipment::BkOpen(char* event, const char* name, int tid)
{
   void* ptr;
   bk_create(event + sizeof(EVENT_HEADER), name, tid, &ptr);
   return ptr;
}

TMFeError TMFeEquipment::BkClose(char* event, void* ptr)
{
   bk_close(event + sizeof(EVENT_HEADER), ptr);
   ((EVENT_HEADER*)event)->data_size = BkSize(event);
   return TMFeError();
}

TMFeError TMFeEquipment::SetStatus(char const* eq_status, char const* eq_color)
{
   HNDLE hDB;
   int status;

   status = cm_get_experiment_database(&hDB, NULL);
   if (status != CM_SUCCESS) {
      return TMFeError(status, "cm_get_experiment_database");
   }

   if (eq_status) {
      fOdbEqCommon->WS("Status", eq_status, 256);
   }

   if (eq_color) {
      fOdbEqCommon->WS("Status color", eq_color, NAME_LENGTH);
   }

   return TMFeError();
}

TMFeError TMFE::TriggerAlarm(const char* name, const char* message, const char* aclass)
{
   int status = al_trigger_alarm(name, message, aclass, message, AT_INTERNAL);

   if (status) {
      return TMFeError(status, "al_trigger_alarm");
   }

   return TMFeError();
}

TMFeError TMFE::ResetAlarm(const char* name)
{
   int status = al_reset_alarm(name);

   if (status) {
      return TMFeError(status, "al_reset_alarm");
   }

   return TMFeError();
}

// singleton instance
TMFE* TMFE::gfMFE = NULL;

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
