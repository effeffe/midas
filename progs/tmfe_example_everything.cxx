/*******************************************************************\

  Name:         tmfe_example_everything.cxx
  Created by:   K.Olchanski

  Contents:     Example Front end to demonstrate all functions of TMFE class

\********************************************************************/

#undef NDEBUG // midas required assert() to be always enabled

#include <stdio.h>
#include <math.h> // M_PI

#include "tmfe.h"

class EqEverything :
   public TMFeEquipmentBase,
   public TMFeRpcHandlerInterface,
   public TMFePeriodicHandlerInterface,
   public TMFePollHandlerInterface
{
public:
   EqEverything() // ctor
   {
      printf("EqEverything::ctor!\n");
   }

   ~EqEverything() // dtor
   {
      printf("EqEverything::dtor!\n");
   }

   void Usage()
   {
      printf("EqEverything::Usage!\n");
   }

   TMFeResult Init(const std::vector<std::string>& args)
   {
      printf("EqEverything::Init!\n");
      fMfe->RegisterRpcHandler(this);
      fMfe->RegisterPeriodicHandler(fEq, this);
      fMfe->RegisterPollHandler(fEq, this);
      fMfe->RegisterTransitionStartAbort();
      fEq->SetStatus("Started...", "white");
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
      fEq->SetStatus("Running", "#00FF00");
      return TMFeOk();
   }

   TMFeResult HandleEndRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandleEndRun", "End run %d!", run_number);
      fEq->SetStatus("Stopped", "#FFFFFF");
      return TMFeOk();
   }

   TMFeResult HandlePauseRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandlePauseRun", "Pause run %d!", run_number);
      fEq->SetStatus("Paused", "#FFFF00");
      return TMFeOk();
   }

   TMFeResult HandleResumeRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandleResumeRun", "Resume run %d!", run_number);
      fEq->SetStatus("Running", "#00FF00");
      return TMFeOk();
   }

   TMFeResult HandleStartAbortRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandleStartAbortRun", "Begin run %d aborted!", run_number);
      fEq->SetStatus("Stopped", "#FFFFFF");
      return TMFeOk();
   }

   void SendData(double dvalue)
   {
      char buf[1024];
      fEq->ComposeEvent(buf, sizeof(buf));
      fEq->BkInit(buf, sizeof(buf));
         
      double* ptr = (double*)fEq->BkOpen(buf, "data", TID_DOUBLE);
      *ptr++ = dvalue;
      fEq->BkClose(buf, ptr);

      fEq->SendEvent(buf);
   }

   void HandlePeriodic()
   {
      printf("EqEverything::HandlePeriodic!\n");
      double t = TMFE::GetTime();
      double data = 100.0*sin(0*M_PI/2.0+M_PI*t/40);
      SendData(data);
      char status_buf[256];
      sprintf(status_buf, "value %.1f", data);
      fEq->SetStatus(status_buf, "#00FF00");
   }

   bool HandlePoll()
   {
      printf("EqEverything::HandlePoll!\n");
      double r = drand48();
      if (r > 0.90) {
         // return successful poll rarely
         return true;
      }
      return false;
   }

   void HandleRead()
   {
      printf("EqEverything::HandleRead!\n");
   }
};

struct EqInfoEverything: TMFeEqInfo { EqInfoEverything() {
   printf("EqInfoEverything::ctor!\n");
   EventID = 1;
   Period = 1000;
   LogHistory = 1;
   WriteEventsToOdb = true;
} };

static TMFeRegister eq_everything_register("tmfe_example_everything", "test_everything", __FILE__, new EqEverything(), new EqInfoEverything);

// example frontend hooks

static class EqEverythingHooks: public TMFeHooksInterface
{
public:
   EqEverythingHooks() // ctor
   {
      printf("EqEverythingHooks::ctor!\n");
      TMFE::Instance()->AddHooks(this);
   }

   void HandlePreConnect(const std::vector<std::string>& args)
   {
      printf("EqEverythingHooks::HandlePreConnect!\n");
   };
   
   void HandlePostConnect(const std::vector<std::string>& args)
   {
      printf("EqEverythingHooks::HandlePostConnect!\n");
   };
   
   void HandlePreDisconnect()
   {
      printf("EqEverythingHooks::HandlePreDisconnect!\n");
   };
   
   void HandlePostDisconnect()
   {
      printf("EqEverythingHooks::HandlePostDisconnect!\n");
   };
} eq_everything_hooks;

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
