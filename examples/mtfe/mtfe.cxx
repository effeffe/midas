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

/*-- Function declarations -----------------------------------------*/

INT trigger_thread(void *param);

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
