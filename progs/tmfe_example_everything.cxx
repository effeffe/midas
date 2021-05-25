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
   public TMFeEquipment
{
public:
   EqEverything(const char* eqname, const char* eqfilename) // ctor
      : TMFeEquipment(eqname, eqfilename)
   {
      printf("EqEverything::ctor!\n");

      // configure the equipment here:
      
      //fEqConfReadConfigFromOdb = false;
      fEqConfEventID = 1;
      fEqConfPeriodMilliSec = 1000;
      fEqConfLogHistory = 1;
      fEqConfWriteEventsToOdb = true;
      fEqConfEnablePoll = true; // enable polled equipment
      //fEqConfPollSleepSec = 0; // to create a "100% CPU busy" polling loop, set poll sleep time to zero 
   }

   ~EqEverything() // dtor
   {
      printf("EqEverything::dtor!\n");
   }

   void HandleUsage()
   {
      printf("EqEverything::HandleUsage!\n");
   }

   TMFeResult HandleInit(const std::vector<std::string>& args)
   {
      printf("EqEverything::HandleInit!\n");
      fMfe->RegisterTransitionStartAbort();
      fEqConfReadOnlyWhenRunning = false; // overwrite ODB Common RO_RUNNING to false
      fEqConfWriteEventsToOdb = true; // overwrite ODB Common RO_ODB to true
      EqSetStatus("Started...", "white");
      //EqStartPollThread();
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
      return TMFeOk();
   }

   TMFeResult HandleEndRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandleEndRun", "End run %d!", run_number);
      EqSetStatus("Stopped", "#FFFFFF");
      return TMFeOk();
   }

   TMFeResult HandlePauseRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandlePauseRun", "Pause run %d!", run_number);
      EqSetStatus("Paused", "#FFFF00");
      return TMFeOk();
   }

   TMFeResult HandleResumeRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandleResumeRun", "Resume run %d!", run_number);
      EqSetStatus("Running", "#00FF00");
      return TMFeOk();
   }

   TMFeResult HandleStartAbortRun(int run_number)
   {
      fMfe->Msg(MINFO, "HandleStartAbortRun", "Begin run %d aborted!", run_number);
      EqSetStatus("Stopped", "#FFFFFF");
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
      printf("EqEverything::HandlePeriodic!\n");
      double t = TMFE::GetTime();
      double data = 100.0*sin(0*M_PI/2.0+M_PI*t/40);
      SendData(data);
      char status_buf[256];
      sprintf(status_buf, "value %.1f", data);
      EqSetStatus(status_buf, "#00FF00");
   }

   bool HandlePoll()
   {
      //printf("EqEverything::HandlePoll!\n");

      if (!fMfe->fStateRunning) // only poll when running
         return false;

      double r = drand48();
      if (r > 0.999) {
         // return successful poll rarely
         printf("EqEverything::HandlePoll!\n");
         return true;
      }

      return false;
   }

   void HandlePollRead()
   {
      printf("EqEverything::HandlePollRead!\n");

         char buf[1024];

         ComposeEvent(buf, sizeof(buf));
         BkInit(buf, sizeof(buf));
         
         uint32_t* ptr = (uint32_t*)BkOpen(buf, "poll", TID_UINT32);
         for (int i=0; i<16; i++) {
            *ptr++ = lrand48();
         }
         BkClose(buf, ptr);
         
         EqSendEvent(buf, false); // do not write polled data to ODB and history
   }
};

// example frontend

class FeEverything: public TMFrontend
{
public:
   FeEverything() // ctor
   {
      printf("FeEverything::ctor!\n");
      FeSetName("tmfe_example_everything");
      FeAddEquipment(new EqEverything("tmfe_example_everything", __FILE__));
   }

   void HandleUsage()
   {
      printf("FeEverything::HandleUsage!\n");
   };
   
   TMFeResult HandleArguments(const std::vector<std::string>& args)
   {
      printf("FeEverything::HandleArguments!\n");
      return TMFeOk();
   };
   
   TMFeResult HandleFrontendInit(const std::vector<std::string>& args)
   {
      printf("FeEverything::HandleFrontendInit!\n");
      return TMFeOk();
   };
   
   TMFeResult HandleFrontendReady(const std::vector<std::string>& args)
   {
      printf("FeEverything::HandleFrontendReady!\n");
      //fMfe->StartPeriodicThread();
      //fMfe->StartRpcThread();
      return TMFeOk();
   };
   
   void HandleFrontendExit()
   {
      printf("FeEverything::HandleFrontendExit!\n");
   };
};

// boilerplate main function

int main(int argc, char* argv[])
{
   FeEverything fe_everything;
   return fe_everything.FeMain(argc, argv);
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
