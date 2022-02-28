/********************************************************************\

  Name:         mtfe.c
  Created by:   Stefan Ritt

  Contents:     Multi-threaded frontend example.

  $Id: frontend.c 4089 2007-11-27 07:28:17Z ritt@PSI.CH $

\********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include "midas.h"
#include "msystem.h"

#include "mfe.h"

/*-- Globals -------------------------------------------------------*/

/* The frontend name (client name) as seen by other MIDAS clients   */
const char *frontend_name = "Sample Frontend";
/* The frontend file name, don't change it */
const char *frontend_file_name = __FILE__;

/* frontend_loop is called periodically if this variable is TRUE    */
BOOL frontend_call_loop = FALSE;

/* a frontend status page is displayed with this frequency in ms */
INT display_period = 3000;

/* maximum event size produced by this frontend */
INT max_event_size = 1024 * 1024;

/* maximum event size for fragmented events (EQ_FRAGMENTED) */
INT max_event_size_frag = 32 * 1024 * 1024;

/* buffer size to hold events */
INT event_buffer_size = 4 * max_event_size;

/*-- Function declarations -----------------------------------------*/

INT trigger_thread(void *param);
INT read_scaler_event(char *pevent, INT off);

/*-- Equipment list ------------------------------------------------*/

BOOL equipment_common_overwrite = TRUE;

EQUIPMENT equipment[] = {

   {"Trigger",               /* equipment name */
    {1, 0,                   /* event ID, trigger mask */
     "SYSTEM",               /* event buffer */
     EQ_USER,                /* equipment type */
     0,                      /* event source (not used) */
     "MIDAS",                /* format */
     TRUE,                   /* enabled */
     RO_RUNNING,             /* read only when running */
     500,                    /* poll for 500ms */
     0,                      /* stop run after this event limit */
     0,                      /* number of sub events */
     0,                      /* don't log history */
     "", "", "",},
    NULL,                    /* readout routine */
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

/*-- Frontend Init -------------------------------------------------*/

INT frontend_init()
{
   /* for this demo, use three readout threads */
   for (int i=0 ; i<3 ; i++) {
      
      /* create a ring buffer for each thread */
      create_event_rb(i);
      
      /* create readout thread */
      ss_thread_create(trigger_thread, (void*)(PTYPE)i);
   }
   
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
/* Polling is not used in this example */
{
   return 0;
}

/*-- Interrupt configuration ---------------------------------------*/

INT interrupt_configure(INT cmd, INT source, POINTER_T adr)
/* Interrupts are not used in this example */
{
   return SUCCESS;
}

/*-- Event readout -------------------------------------------------*/

INT trigger_thread(void *param)
{
   EVENT_HEADER *pevent;
   WORD *pdata, *padc;
   int  index, i, status, exit = FALSE;
   INT rbh;
   
   /* index of this thread */
   index = (int)(PTYPE)param;
   
   /* tell framework that we are alive */
   signal_readout_thread_active(index, TRUE);

   /* set name of thread as seen by OS */
   ss_thread_set_name(std::string(equipment[0].name) + "RT" + std::to_string(index));
   
   /* Initialize hardware here ... */
   printf("Start readout thread %d\n", index);
   
   /* Obtain ring buffer for inter-thread data exchange */
   rbh = get_event_rbh(index);
   
   while (is_readout_thread_enabled()) {

      if (!readout_enabled()) {
         // do not produce events when run is stopped
         ss_sleep(10);
         continue;
      }

      /* check for new event (poll) */
      status = ss_sleep(100); // for this demo, just sleep a bit

      if (status) { // if event available, read it out

         // check once more in case state changed during the poll
         if (!is_readout_thread_enabled())
            break;

         // obtain buffer space
         do {
            status = rb_get_wp(rbh, (void **) &pevent, 0);
            if (status == DB_TIMEOUT) {
               ss_sleep(10);
               // check for readout thread disable, thread might be stop from main thread
               // in case Ctrl-C is hit for example
               if (!is_readout_thread_enabled()) {
                  exit = TRUE;
                  break;
               }
            }
         } while (status != DB_SUCCESS);

         if (exit)
            break;

         bm_compose_event_threadsafe(pevent, 1, 0, 0, &equipment[0].serial_number);
         pdata = (WORD *)(pevent + 1);
         
         /* init bank structure */
         bk_init32(pdata);
         
         /* create ADC0 bank */
         bk_create(pdata, "ADC0", TID_WORD, (void **)&padc);
         
         /* just put in some random numbers in this demo */
         int len = 32 + rand() % 10000;
         for (i=0 ; i<len; i++)
            *padc++ = len;
         
         bk_close(pdata, padc);
         
         pevent->data_size = bk_size(pdata);

         /* send event to ring buffer */
         rb_increment_wp(rbh, sizeof(EVENT_HEADER) + pevent->data_size);
      }
   }
   
   /* tell framework that we are finished */
   signal_readout_thread_active(index, FALSE);
   
   printf("Stop readout thread %d\n", index);

   return 0;
}
