/********************************************************************\

  Name:         wdfe.cxx
  Created by:   Stefan Ritt

  Contents:     Example front-end for standalone WaveDREAM board

\********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <iostream>
#include "midas.h"
#include "mfe.h"
#include "experim.h"

#include "WDBLib.h"
#include "DCBLib.h"

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
INT max_event_size = 1024 * 1014;

/* maximum event size for fragmented events (EQ_FRAGMENTED) */
INT max_event_size_frag = 5 * max_event_size;

/* buffer size to hold events */
INT event_buffer_size = 5 * max_event_size;


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

BOOL equipment_common_overwrite = TRUE;

EQUIPMENT equipment[] = {

   {"Trigger",               /* equipment name */
      {1, 0,                 /* event ID, trigger mask */
         "SYSTEM",           /* event buffer */
         EQ_POLLED,          /* equipment type */
         0,                  /* event source */
         "MIDAS",            /* format */
         TRUE,               /* enabled */
         RO_RUNNING,         /* read only when running */
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

namespace WD { // put all WD globals into separate namespace

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
      drs_calib_path += "/sw/wds/";
   } else {
      cm_msg(MINFO, "frontend_init", "Calibration filepath set to current directory. "\
             "Use WDBSYS environnement variable to point to your wavedaq repository");
      drs_calib_path = "./";
   }

   // create WDB object
   WDB *b = new WDB("wd026");
   try {
      b->SetVerbose(true);                // change for debugging
      b->Connect();                       // connect to board, throws exception if unsuccessful
      b->ReceiveControlRegisters();

      // Reset DRS FSM
      b->ResetDrsControlFsm();
      b->ResetPackager();

      // Stop DRS
      b->SetDaqSingle(false);
      b->SetDaqAuto(false);
      b->SetDaqNormal(false);

      b->SetSendBlock(true);              // update all control registers together

      //---- readout settings
      b->SetDrsChTxEn(0xFFFF);            // enable all DRS readout
      b->SetAdcChTxEn(0);                 // disable ADC readout
      b->SetTdcChTxEn(0);                 // disable TDC readout
      b->SetSclTxEn(0);                   // disable scaler readout

      //---- board settings
      b->SetDaqClkSrcSel(1);              // select on-board clock oscillator
      b->SetDrsSampleFreq(1000);          // Sampling speed 1 GSPS
      b->SetFeGain(-1, 1);                // set FE gain to 1
      b->SetFePzc(-1, false);             // disable pole-zero cancellation
      b->SetInterPkgDelay(0x753);         // minimum inter-packet delay
      b->SetFeMux(-1, WDB::cFeMuxInput);  // set multiplexer to input

      //---- trigger settings
      b->SetExtAsyncTriggerEn(false);     // disable external trigger
      b->SetTriggerDelay(0);              // minimal inter-packet delay
      b->SetLeadTrailEdgeSel(0);          // trigger on leading edge
      b->SetPatternTriggerEn(true);       // enable internal pattern trigger
      b->SetDacTriggerLevelV(-1, -0.02);  // set trigger level to -20 mV for all channels

      b->SetTrgSrcPolarity(0xFFFF);       // negative signals
      b->SetTrgPtrnEn(0xFFFF);            // enable 16 trigger patterns
      // b->SetTrgPtrnEn(0x0001);            // enable first trigger pattern
      for (int i=0 ; i<16 ; i++) {
         b->SetTrgSrcEnPtrn(i, (1<<i));   // set individual channel as only source for pattern
         b->SetTrgStatePtrn(i, (1<<i));   // set trigger coincidence for single channel
      }

      // now send all changes in one packet
      b->SetSendBlock(false);
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
      cm_msg(MERROR, "frontend_init", "Cannot initialize %s, aborting.", b->GetName().c_str());
      return FE_ERR_HW;
   }

   WD::wdb.push_back(b);

   // instantiate waveform processor
   WD::wp = new WP();
   WD::wp->SetAllCalib(true);
   WD::wp->SetWDBList(WD::wdb);
   WD::wp->SetRequestedBoard(WD::wdb);

   // set destination port after waveform processor has been initialized
   for (auto b: WD::wdb)
      b->SetDestinationPort(WD::wp->GetServerPort());

   // start DRS for first event
   for (auto b: WD::wdb)
      b->SetDaqSingle(true);

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
   // start DRS for first event
   for (auto b: WD::wdb) {
      b->ResetDrsControlFsm();
      b->ResetEventCounter();
      b->SetDaqSingle(true);
   }

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
   bool flag;

   for (i = 0; i < count; i++) {
      /* poll hardware and set flag to TRUE if new event is available */
      flag = WD::wp->WaitNewEvent(10);

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
   float *pdata;
   WDEvent event(WD::wdb[0]->GetSerialNumber());

   // read current event
   bool bNewEvent = WD::wp->GetLastEvent(WD::wdb[0], 500, event);

   // start DRS for next event
   WD::wdb[0]->SetDaqSingle(true);

   if (!bNewEvent)
      return 0;

   // init bank structure
   bk_init32(pevent);

   // store time arrays for all channels on first event
   if (SERIAL_NUMBER(pevent) == 0) {
      bk_create(pevent, "DRST", TID_FLOAT, (void **) &pdata);
      for (int i = 0; i < 16; i++) {
         memcpy(pdata, event.mWfTDRS[i], sizeof(float) * 1024);
         pdata += 1024;
      }
      bk_close(pevent, pdata);
   }

   // create calibrated time array
   bk_create(pevent, "DRS0", TID_FLOAT, (void **) &pdata);

   // copy over 16 channels
   for (int i=0 ; i<16 ; i++) {
      memcpy(pdata, event.mWfUDRS[i], sizeof(float)*1024);
      pdata += 1024;
   }

   bk_close(pevent, pdata);

   return bk_size(pevent);
}

/*-- Periodic event ------------------------------------------------*/

INT read_periodic_event(char *pevent, INT off)
{
   DWORD *pdata;

   std::vector<unsigned long long> scaler;
   WD::wdb[0]->GetScalers(scaler, true);

   // init bank structure
   bk_init(pevent);

   // create SCLR bank
   bk_create(pevent, "SCLR", TID_DWORD, (void **)&pdata);

   for (int i = 0; i < scaler.size(); i++)
      *pdata++ = scaler[i];

   bk_close(pevent, pdata);

   return bk_size(pevent);
}
