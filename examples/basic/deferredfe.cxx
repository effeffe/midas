/********************************************************************\

  Name:         deferredfe.cxx
  Created by:   Stefan Ritt

  Contents:     Experiment specific readout code (user part) of
                Midas frontend. This example demonstrates the
                implementation of the deferred transition request.


  $Id:$

\********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include "midas.h"

#include "mfe.h"

/*-- Globals -------------------------------------------------------*/

/* The frontend name (client name) as seen by other MIDAS clients   */
   const char *frontend_name = "deferredfe";

/* The frontend file name, don't change it */
   const char *frontend_file_name = __FILE__;

/* frontend_loop is called periodically if this variable is TRUE    */
   BOOL frontend_call_loop = FALSE;

/* a frontend status page is displayed with this frequency in ms */
   INT display_period = 000;

/* maximum event size produced by this frontend */
   INT max_event_size = 10000;

/* maximum event size for fragmented events (EQ_FRAGMENTED) */
   INT max_event_size_frag = 5 * 1024 * 1024;

/* buffer size to hold events */
   INT event_buffer_size = 10 * 10000;

/* number of channels */
#define N_ADC  4
#define N_TDC  8
#define N_SCLR 4

/* CAMAC crate and slots */
#define CRATE      0
#define SLOT_IO   23
#define SLOT_ADC   1
#define SLOT_TDC   2
#define SLOT_SCLR  3

/*-- Function declarations -----------------------------------------*/

   INT frontend_init();
   INT frontend_exit();
   INT begin_of_run(INT run_number, char *error);
   INT end_of_run(INT run_number, char *error);
   INT pause_run(INT run_number, char *error);
   INT resume_run(INT run_number, char *error);
   INT frontend_loop();

   INT read_deferred_event(char *pevent, INT off);

/*-- Equipment list ------------------------------------------------*/

#undef USE_INT

   EQUIPMENT equipment[] = {

      {"Deferred",              /* equipment name */
       {2, 0,                    /* event ID, trigger mask */
       "SYSTEM",                /* event buffer */
       EQ_PERIODIC,             /* equipment type */
       0,                       /* event source */
       "MIDAS",                 /* format */
       TRUE,                    /* enabled */
       RO_RUNNING |             /* read when running */
       RO_ODB,                  /* and update ODB */
       2000,                    /* read every 2 sec */
       0,                       /* stop run after this event limit */
       0,                       /* number of sub events */
       0,                       /* log history */
       "", "", "",},
       read_deferred_event,     /* readout routine */
      },
      {""}
   };


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
int pseudo_delay = 0;
BOOL transition_PS_requested = FALSE;
BOOL end_of_mcs_cycle = FALSE;

//-- Deferred transition callback
BOOL wait_end_cycle(int transition, BOOL first)
{
   // Get there after every
   if (first) {
      // Get there as soon as transition is requested
      transition_PS_requested = TRUE;
      printf("Transition requested...\n");
      // Defer the transition now
      return FALSE;
   }
   // Check user flag
   if (end_of_mcs_cycle) {
      // User flag set, ready to perform deferred transition now
      transition_PS_requested = FALSE;
      end_of_mcs_cycle = FALSE;
      return TRUE;
   } else {
      // User not ready for transition, defers it...
      return FALSE;
   }
}

/*-- Frontend Init -------------------------------------------------*/
INT frontend_init()
{
   // register for deferred transition
   cm_register_deferred_transition(TR_STOP, wait_end_cycle);
   cm_register_deferred_transition(TR_PAUSE, wait_end_cycle);


   /* hardware initialization */

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
   /* put here clear scalers etc. */
   pseudo_delay = 0;
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

INT interrupt_configure(INT cmd, INT source, PTYPE adr)
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

/*-- Deferred event --------------------------------------------------*/
INT read_deferred_event(char *pevent, INT off)
{
   DWORD *pdata, a;

   /* init bank structure */
   bk_init(pevent);


   /* create SCLR bank */
   bk_create(pevent, "SCLR", TID_DWORD, (void **) &pdata);

   /* read scaler bank */
   for (a = 0; a < N_SCLR; a++)
      *pdata++ = rand()%1024 + rand()%1024 + rand()%1024 + rand()%1024;

   bk_close(pevent, pdata);

   if (transition_PS_requested) {
      // transition acknowledged, but...
      // carry on until hardware condition satisfied
      // ...
      if (pseudo_delay++ < 5) {
         // Ignore transition 
         printf("Transition ignored, ");
      } else {
         // Time to do transition
         printf("End of cycle... perform transition\n");
         end_of_mcs_cycle = TRUE;
      }
   }
   printf("Event ID:%d - Event#: %d\n", EVENT_ID(pevent), SERIAL_NUMBER(pevent));
   return bk_size(pevent);
}
