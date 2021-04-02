/*******************************************************************\

  Name:         tmfe_example_frontend.cxx
  Created by:   K.Olchanski

  Contents:     Example of converting examples/experiment/frontend.cxx to TMFE framework

\********************************************************************/

#undef NDEBUG // midas required assert() to be always enabled

#include <stdio.h>
#include <math.h> // M_PI

#include "tmfe.h"

/********************************************************************\

  Name:         frontend.c
  Created by:   Stefan Ritt

  Contents:     Experiment specific readout code (user part) of
                Midas frontend. This example simulates a "trigger
                event" and a "periodic event" which are filled with
                random data.
 
                The trigger event is filled with two banks (ADC0 and TDC0),
                both with values with a gaussian distribution between
                0 and 4096. About 100 event are produced per second.
 
                The periodic event contains one bank (PRDC) with four
                sine-wave values with a period of one minute. The
                periodic event is produced once per second and can
                be viewed in the history system.

\********************************************************************/

#undef NDEBUG // midas required assert() to be always enabled

//#include "midas.h"
//#include "experim.h"

#if 0
const char *frontend_name = "Sample Frontend";
const char *frontend_file_name = __FILE__;
BOOL frontend_call_loop = FALSE;
INT display_period = 3000;
INT max_event_size = 10000;
INT max_event_size_frag = 5 * 1024 * 1024;
INT event_buffer_size = 100 * 10000;
BOOL equipment_common_overwrite = TRUE;

EQUIPMENT equipment[] = {

   {"Trigger",               /* equipment name */
      {1, 0,                 /* event ID, trigger mask */
         "SYSTEM",           /* event buffer */
         EQ_POLLED,          /* equipment type */
         0,                  /* event source */
         "MIDAS",            /* format */
         TRUE,               /* enabled */
         RO_RUNNING |        /* read only when running */
         RO_ODB,             /* and update ODB */
         100,                /* poll for 100ms */
         0,                  /* stop run after this event limit */
         0,                  /* number of sub events */
         0,                  /* don't log history */
         "", "", "",},
      read_trigger_event,    /* readout routine */
   },

   {"Periodic",              /* equipment name */
      {2, 0,                 /* event ID, trigger mask */
         "SYSTEM",           /* event buffer */
         EQ_PERIODIC,        /* equipment type */
         0,                  /* event source */
         "MIDAS",            /* format */
         TRUE,               /* enabled */
         RO_RUNNING | RO_TRANSITIONS |   /* read when running and on transitions */
         RO_ODB,             /* and update ODB */
         1000,               /* read every sec */
         0,                  /* stop run after this event limit */
         0,                  /* number of sub events */
         TRUE,               /* log history */
         "", "", "",},
      read_periodic_event,   /* readout routine */
   },

   {""}
};
#endif

class EqTrigger :
   public TMFeEquipment
{
public:
   EqTrigger(const char* eqname, const char* eqfilename) // ctor
      : TMFeEquipment(eqname, eqfilename)
   {
      /* configure your equipment here */
      
      fEqConfReadConfigFromOdb = false;
      fEqConfEventID = 1;
      fEqConfBuffer = "SYSTEM";
      fEqConfPeriodMilliSec = 0; // in milliseconds
      fEqConfLogHistory = 0;
      fEqConfReadOnlyWhenRunning = true;
      //fEqConfWriteEventsToOdb = true;
      fEqConfEnablePoll = true; // enable polled equipment
      //fEqConfPollSleepSec = 0; // to create a "100% CPU busy" polling loop, set poll sleep time to zero 
      fEqConfPollSleepSec = 0.010; // limit event rate to 100 Hz. In a real experiment remove this line
   }

   ~EqTrigger() // dtor
   {
   }

   void HandleUsage()
   {
      printf("EqTrigger::Usage!\n");
   }

   TMFeResult HandleInit(const std::vector<std::string>& args)
   {
      /* put any hardware initialization here */


      /* start poll thread here */

      //EqStartPollThread();
      
      /* return TMFeErrorMessage("my error message") if frontend should not be started */
      return TMFeOk();
   }

   TMFeResult HandleRpc(const char* cmd, const char* args, std::string& response)
   {
      /* handler for JRPC into the frontend, see tmfe_example_everything.cxx */
      return TMFeOk();
   }

   TMFeResult HandleBeginRun(int run_number)
   {
      /* put here clear scalers etc. */
      return TMFeOk();
   }

   TMFeResult HandleEndRun(int run_number)
   {
      return TMFeOk();
   }

   bool HandlePoll()
   {
      /* Polling routine for events. Returns TRUE if event is available */
      return true;
   }

   void HandlePollRead()
   {
      char buf[1024];
      ComposeEvent(buf, sizeof(buf));
      BkInit(buf, sizeof(buf));

      /* create structured ADC0 bank */
      uint32_t* pdata = (uint32_t*)BkOpen(buf, "ADC0", TID_UINT32);
      
      /* following code to "simulates" some ADC data */
      for (int a = 0; a < 4; a++)
         *pdata++ = rand()%1024 + rand()%1024 + rand()%1024 + rand()%1024;

      BkClose(buf, pdata);

      /* create variable length TDC bank */
      pdata = (uint32_t*)BkOpen(buf, "TDC0", TID_UINT32);
      
      /* following code to "simulates" some TDC data */
      for (int a = 0; a < 4; a++)
         *pdata++ = rand()%1024 + rand()%1024 + rand()%1024 + rand()%1024;

      BkClose(buf, pdata);
      
      EqSendEvent(buf);
   }
};

class EqPeriodic :
   public TMFeEquipment
{
public:
   EqPeriodic(const char* eqname, const char* eqfilename) // ctor
      : TMFeEquipment(eqname, eqfilename)
   {
      /* configure your equipment here */

      fEqConfReadConfigFromOdb = false;
      fEqConfEventID = 2;
      fEqConfBuffer = "SYSTEM";
      fEqConfPeriodMilliSec = 1000; // in milliseconds
      fEqConfLogHistory = 1;
      fEqConfReadOnlyWhenRunning = true;
      fEqConfWriteEventsToOdb = true;
   }

   void HandlePeriodic()
   {
      char buf[1024];

      ComposeEvent(buf, sizeof(buf));
      BkInit(buf, sizeof(buf));

      /* create SCLR bank */
      uint32_t* pdata = (uint32_t*)BkOpen(buf, "PRDC", TID_UINT32);
      
      /* following code "simulates" some values in sine wave form */
      for (int a = 0; a < 16; a++)
         *pdata++ = 100*sin(M_PI*time(NULL)/60+a/2.0)+100;
      
      BkClose(buf, pdata);

      EqSendEvent(buf);
   }
};

class FeExample: public TMFrontend
{
public:
   FeExample() // ctor
   {
      /* register with the framework */
      FeSetName("Sample Frontend");
      FeAddEquipment(new EqTrigger("Trigger", __FILE__));
      FeAddEquipment(new EqPeriodic("Periodic", __FILE__));
   }

   TMFeResult HandleFrontendInit(const std::vector<std::string>& args)
   {
      /* called before equipment HandleInit(), do all hardware initialization here */

      printf("frontend init!\n");

      return TMFeOk();
   };
   
   TMFeResult HandleFrontendReady(const std::vector<std::string>& args)
   {
      /* called after equipment HandleInit(), anything that needs to be done
       * before starting the main loop goes here */

      printf("frontend ready!\n");

      /* start periodic and rpc threads here */

      //fMfe->StartPeriodicThread();
      //fMfe->StartRpcThread();

      return TMFeOk();
   };
   
   void HandleFrontendExit()
   {
      /* hardware shutdown goes here */

      printf("frontend exit!\n");
   };
};

int main(int argc, char* argv[])
{
   FeExample fe_example;
   return fe_example.FeMain(argc, argv);
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
