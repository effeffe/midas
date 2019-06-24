/********************************************************************\

  Name:         wdfe.cxx
  Created by:   Stefan Ritt

  Contents:     Example front-end for standalone WaveDREAM board

\********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <iostream>
#include "midas.h"
#include "mfe.h"
#include "experim.h"

#include "WDBLib.h"

/*-- Globals -------------------------------------------------------*/

/* The frontend name (client name) as seen by other MIDAS clients   */
const char *frontend_name = "WD Frontend";
/* The frontend file name, don't change it */
const char *frontend_file_name = __FILE__;

/* frontend_loop is called periodically if this variable is TRUE    */
BOOL frontend_call_loop = FALSE;

/* a frontend status page is displayed with this frequency in ms */
INT display_period = 3000;

/* maximum event size produced by this frontend */
INT max_event_size = 10000;

/* maximum event size for fragmented events (EQ_FRAGMENTED) */
INT max_event_size_frag = 5 * 1024 * 1024;

/* buffer size to hold events */
INT event_buffer_size = 100 * 10000;

/*-- Function declarations -----------------------------------------*/

INT frontend_init(void);
INT frontend_exit(void);
INT begin_of_run(INT run_number, char *error);
INT end_of_run(INT run_number, char *error);
INT pause_run(INT run_number, char *error);
INT resume_run(INT run_number, char *error);
INT frontend_loop(void);

INT read_trigger_event(char *pevent, INT off);
INT read_periodic_event(char *pevent, INT off);

INT poll_event(INT source, INT count, BOOL test);
INT interrupt_configure(INT cmd, INT source, POINTER_T adr);

/*-- Equipment list ------------------------------------------------*/

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

namespace WD { // put all wD globals into separate namespace

   // WD boards
   std::vector<WDB *> wdb;

   // waveform processor
   WP* wp;

   // WD events
   std::vector<WDEvent *>wde;

}

/*-- Frontend Init -------------------------------------------------*/

INT frontend_init()
{
   std::string drs_calib_path;

   // set calibration path according to WDBSYS
   const char *wavedaqpath = getenv("WDBSYS");
   if (wavedaqpath != nullptr) {
      drs_calib_path = wavedaqpath;
      drs_calib_path += "/software/wds/";
   } else {
      cm_msg(MINFO, "frontend_init", "Calibration filepath set to current directory. "\
             "Use WDBSYS environnement variable to point to your wavedaq repository");
      drs_calib_path = "./";
   }

   // create WDB object
   WDB *b = new WDB("wd134");
   try {
      b->SetVerbose(true);    // change for debugging
      b->Connect();
      b->ReceiveControlRegisters();

      b->SetSendBlocked(true); // update all control registers together

      b->SetDrsChTxEn(0xFFFF); // enable all DRS readout
      b->SetAdcChTxEn(0);      // disable ADC readout
      b->SetTdcChTxEn(0);      // disable TDC readout

      b->SetDaqClkSrcSel(1);   // select on-board clock oscillator

      b->SetDrsSampleFreq(2000);       // Sampling speed 2 GSPS

      b->SetExtAsyncTriggerEn(false);  // disable external trigger
      b->SetTriggerDelay(0);
      b->SetLeadTrailEdgeSel(0);       // trigger on leading edge
      b->SetPatternTriggerEn(true);    // enable internal pattern trigger

      b->SetTrgSrcPolarity(0xFFFF);    // negative signals
      b->SetTrgPtrnEn(0xFFFF);         // enable 16 trigger patterns
      for (int i=0 ; i<16 ; i++) {
         b->SetTrgSrcEnPtrn(i, i);     // set individual channel as only source for pattern
         b->SetTrgStatePtrn(i, i);     // set trigger coincidence for single channel
      }

      // now send all changes in one packet
      b->SetSendBlocked(false);
      b->SendControlRegisters();

      // check if PLLs are locked
      b->ResetAllPll();
      sleep_ms(100);
      b->GetPllLock(true);
      if (!b->GetLmkPllLock() || !b->GetDaqPllLock()) {
         cm_msg(MERROR, "frontend_init", "PLLs not locked on board %s. Mask = 0x%04X",
                 b->GetName().c_str(), b->GetPllLock(false));
         return FE_ERR_HW;
      }

      // Reset DRS FSM
      b->ResetDrsControlFsm();
      b->ResetPackager();

      // start DRS
      b->SetDaqSingle(false);
      b->SetDaqAuto(false);
      b->SetDaqNormal(true);

      // read all status registers
      b->ReceiveStatusRegisters();

      if (b->IsVerbose()) {
         std::cout << std::endl << "========== Board Info ==========" << std::endl;
         b->PrintVersion();
      }

      // load calibration data for this board
      b->LoadVoltageCalibration(b->GetDrsSampleFreqMhz(), drs_calib_path);
      b->LoadTimeCalibration(b->GetDrsSampleFreqMhz(), drs_calib_path);

      // create event storage for this board
      WD::wde.push_back(new WDEvent(b->GetSerialNumber()));

   } catch (std::runtime_error &e) {
      std::cout << std::endl;
      cm_msg(MERROR, "frontend_init", "%s", e.what());
      cm_msg(MERROR, "frontend_init", "Canot initialize %s, aborting.", b->GetName().c_str());
   }

   WD::wdb.push_back(b);

   // instantiate waveform processor
   WD::wp = new WP(WD::wdb);
   WD::wp->SetAllCalib(true);
   WD::wp->RequestAllBoards();

   // set destination port after waveform processor has been initialized
   for (auto b: WD::wdb)
      b->SetDestinationPort(WD::wp->GetServerPort());

   return SUCCESS;
}

/*-- Frontend Exit -------------------------------------------------*/

INT frontend_exit()
{
   return SUCCESS;
}

/*-- Begin of Run --------------------------------------------------*/

INT begin_of_run(INT run_number, char *error)
{
   return SUCCESS;
}

/*-- End of Run ----------------------------------------------------*/

INT end_of_run(INT run_number, char *error)
{
   return SUCCESS;
}

/*-- Pause Run -----------------------------------------------------*/

INT pause_run(INT run_number, char *error)
{
   return SUCCESS;
}

/*-- Resuem Run ----------------------------------------------------*/

INT resume_run(INT run_number, char *error)
{
   return SUCCESS;
}

/*-- Frontend Loop -------------------------------------------------*/

INT frontend_loop()
{
   /* if frontend_call_loop is true, this routine gets called when
      the frontend is idle or once between every event */
   return SUCCESS;
}

/*------------------------------------------------------------------*/

/********************************************************************\

  Readout routines for different events

\********************************************************************/

/*-- Trigger event routines ----------------------------------------*/

INT poll_event(INT source, INT count, BOOL test)
/* Polling routine for events. Returns TRUE if event
   is available. If test equals TRUE, don't return. The test
   flag is used to time the polling */
{
   int i;
   DWORD flag;

   for (i = 0; i < count; i++) {
      /* poll hardware and set flag to TRUE if new event is available */
      flag = TRUE;

      if (flag)
         if (!test)
            return TRUE;
   }

   return 0;
}

/*-- Interrupt configuration ---------------------------------------*/

INT interrupt_configure(INT cmd, INT source, POINTER_T adr)
{
   switch (cmd) {
   case CMD_INTERRUPT_ENABLE:
      break;
   case CMD_INTERRUPT_DISABLE:
      break;
   case CMD_INTERRUPT_ATTACH:
      break;
   case CMD_INTERRUPT_DETACH:
      break;
   }
   return SUCCESS;
}

/*-- Event readout -------------------------------------------------*/

INT read_trigger_event(char *pevent, INT off)
{
   WORD *pdata, a;

   /* init bank structure */
   bk_init(pevent);

   /* create structured ADC0 bank */
   bk_create(pevent, "ADC0", TID_WORD, (void **)&pdata);

   /* following code to "simulates" some ADC data */
   for (a = 0; a < 4; a++)
      *pdata++ = rand()%1024 + rand()%1024 + rand()%1024 + rand()%1024;

   bk_close(pevent, pdata);

   return bk_size(pevent);
}

/*-- Periodic event ------------------------------------------------*/

INT read_periodic_event(char *pevent, INT off)
{
   float *pdata;
   int a;

   /* init bank structure */
   bk_init(pevent);

   /* create SCLR bank */
   bk_create(pevent, "SCLR", TID_FLOAT, (void **)&pdata);

   /* following code "simulates" some values */
   for (a = 0; a < 4; a++)
      *pdata++ = 100*sin(M_PI*time(NULL)/60+a/2.0);

   bk_close(pevent, pdata);

   return bk_size(pevent);
}
