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
   EqTrigger(const char* eqname, const char* eqfilename, TMFeEqInfo* eqinfo) // ctor
      : TMFeEquipment(eqname, eqfilename, eqinfo)
   {
      /* configure your equipment here */
      
      fEqInfo->ReadEqInfoFromOdb = false;
      fEqInfo->EventID = 1;
      fEqInfo->Buffer = "SYSTEM";
      fEqInfo->Period = 0; // in milliseconds
      fEqInfo->LogHistory = 0;
      fEqInfo->ReadOnlyWhenRunning = true;
      fEqInfo->WriteEventsToOdb = true;
      //fEqInfo->PollSleepSec = 0; // poll sleep time set to zero create a "100% CPU busy" polling loop
      fEqInfo->PollSleepSec = 0.010; // limit event rate to 100 Hz. In a real experiment remove this line
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

      fEqInfo->Enabled = false;
      
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

   void HandleRead()
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

static TMFeRegister eq_trigger_register("Sample Frontend", new EqTrigger("Trigger", __FILE__, NULL), true, false, true);

class EqPeriodic :
   public TMFeEquipment
{
public:
   EqPeriodic(const char* eqname, const char* eqfilename, TMFeEqInfo* eqinfo) // ctor
      : TMFeEquipment(eqname, eqfilename, eqinfo)
   {
      /* configure your equipment here */

      fEqInfo->ReadEqInfoFromOdb = false;
      fEqInfo->EventID = 2;
      fEqInfo->Buffer = "SYSTEM";
      fEqInfo->Period = 1000; // in milliseconds
      fEqInfo->LogHistory = 1;
      fEqInfo->ReadOnlyWhenRunning = true;
      fEqInfo->WriteEventsToOdb = true;
   }

   void HandlePeriodic()
   {
      char buf[1024];

      ComposeEvent(buf, sizeof(buf));
      BkInit(buf, sizeof(buf));

      /* create SCLR bank */
      float* pdata = (float*)BkOpen(buf, "PRDC", TID_FLOAT);
      
      /* following code "simulates" some values in sine wave form */
      for (int a = 0; a < 16; a++)
         *pdata++ = 100*sin(M_PI*time(NULL)/60+a/2.0)+100;
      
      BkClose(buf, pdata);

      EqSendEvent(buf);
   }
};

static TMFeRegister eq_periodic_register("Sample Frontend", new EqPeriodic("Periodic", __FILE__, NULL), true, true, false);

static class EqEverythingHooks: public TMFeHooksInterface
{
public:
   EqEverythingHooks() // ctor
   {
      /* register with the framework */
      TMFE::Instance()->AddHooks(this);
   }

   void HandlePostConnect(const std::vector<std::string>& args)
   {
      /* frontend_init: do all hardware setup common to all equipments needed before HandleInit() */
   };
   
   void HandlePostInit(const std::vector<std::string>& args)
   {
      /* do all hardware setup common to all equipments needed after HandleInit(), but before starting the main loop */
   };
   
   void HandlePreDisconnect()
   {
      /* frontend_exit: do all hardware shutdown before disconnect from midas */
   };
} eq_frontend_hooks;


/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
