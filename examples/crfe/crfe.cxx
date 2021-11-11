/********************************************************************\

  Name:         crfe.cxx
  Created by:   Stefan Ritt

  Contents:     Code for modern slow control front-end "Clock and Reset"
                to illustrate manual generation of slow control
                events and hardware updates via cm_watch().

                The values of

                /Equipment/Clock Reset/Settings/Active
                /Equipment/Clock Reset/Settings/Delay

                are propagated to hardware when the ODB value chanes.

                The triggers

                /Equipment/Clock Reset/Settings/Reset Trigger
                /Equipment/Clock Reset/Settings/Sync Trigger

                can be set to TRUE to trigger a specific action
                in this front-end.

                For a real program, the "TODO" lines have to be 
                replaced by actual hardware acces.

                Custom page
                -----------

                The custom page "cr.html" in this directory can be
                used to control the settins of this frontend. To
                do so, set "/Custom/Path" in the ODB to this 
                directory and create a string

                /Custom/Clock Reset = cr.html

                then click on "Clock Reset" on the left lower corner
                in the web status page.

\********************************************************************/

#include <stdio.h>
#include "midas.h"
#include "mfe.h"

/*-- Globals -------------------------------------------------------*/

/* The frontend name (client name) as seen by other MIDAS clients   */
const char *frontend_name = "CR Frontend";
/* The frontend file name, don't change it */
const char *frontend_file_name = __FILE__;

/* frontend_loop is called periodically if this variable is TRUE    */
BOOL frontend_call_loop = FALSE;

/* a frontend status page is displayed with this frequency in ms    */
INT display_period = 1000;

/* maximum event size produced by this frontend */
INT max_event_size = 10000;

/* maximum event size for fragmented events (EQ_FRAGMENTED) */
INT max_event_size_frag = 5 * 1024 * 1024;

/* buffer size to hold events */
INT event_buffer_size = 10 * 10000;

/*-- Function declarations -----------------------------------------*/

INT read_cr_event(char *pevent, INT off);
void cr_settings_changed(HNDLE, HNDLE, int, void *);

/*-- Equipment list ------------------------------------------------*/

/* Default values for /Equipment/Clock Reset/Settings */
const char *cr_settings_str[] = {
"Active = BOOL : 1",
"Delay = INT : 0",
"Reset Trigger = BOOL : 0",
"Sync Trigger = BOOL : 0",
"Names CRT1 = STRING[4] :",
"[32] Temp0",
"[32] Temp1",
"[32] Temp2",
"[32] Temp3",
NULL
};

EQUIPMENT equipment[] = {

   {"Clock Reset",              /* equipment name */
    {10, 0,                     /* event ID, trigger mask */
     "SYSTEM",                  /* event buffer */
     EQ_PERIODIC,               /* equipment type */
     0,                         /* event source */
     "MIDAS",                   /* format */
     TRUE,                      /* enabled */
     RO_ALWAYS | RO_ODB,        /* read always and update ODB */
     10000,                     /* read every 10 sec */
     0,                         /* stop run after this event limit */
     0,                         /* number of sub events */
     1,                         /* log history every event */
     "", "", ""} ,
    read_cr_event,              /* readout routine */
   },

   {""}
};


/*-- Dummy routines ------------------------------------------------*/

INT poll_event(INT source, INT count, BOOL test)
{
   return 1;
};

INT interrupt_configure(INT cmd, INT source, POINTER_T adr)
{
   return 1;
};

/*-- Frontend Init -------------------------------------------------*/

INT frontend_init()
{
   HNDLE hKey;

   // create Settings structure in ODB
   db_create_record(hDB, 0, "Equipment/Clock Reset/Settings", strcomb1(cr_settings_str).c_str());
   db_find_key(hDB, 0, "/Equipment/Clock Reset", &hKey);
   assert(hKey);

   db_watch(hDB, hKey, cr_settings_changed, NULL);

   /*
    * Set our transition sequence. The default is 500. Setting it
    * to 600 means we are called AFTER most other clients.
    */
   cm_set_transition_sequence(TR_START, 600);

   return CM_SUCCESS;
}

/*-- Frontend Exit -------------------------------------------------*/

INT frontend_exit()
{
   return CM_SUCCESS;
}

/*-- Frontend Loop -------------------------------------------------*/

INT frontend_loop()
{
   return CM_SUCCESS;
}

/*-- Begin of Run --------------------------------------------------*/

#define N_READBACK 4

INT begin_of_run(INT run_number, char *error)
{
   /*
    * This example code starts a run transition, then waits on some
    * external conditions by polling an array in the ODB. It expects
    * that other programs set the readback array in the ODB to the
    * current run number
    */

   HNDLE hDB, hKey;
   INT readback[N_READBACK], i;
   cm_get_experiment_database(&hDB, nullptr);

   // set requested run number in ODB
   db_set_value(hDB, 0, "/Equipment/Clock Reset/Run transitions/Requested run number", &run_number, sizeof(INT), 1, TID_INT);

   // retrieve readback array form ODB
   int size = sizeof(readback);
   memset(readback, 0, size);
   db_find_key(hDB, 0, "/Equipment/Clock Reset/Run transitions/FEB readback", &hKey);
   if (!hKey) {
      // create readback array if not existing
      db_create_key(hDB, 0, "/Equipment/Clock Reset/Run transitions/FEB readback", TID_INT);
      db_find_key(hDB, 0, "/Equipment/Clock Reset/Run transitions/FEB readback", &hKey);
      db_set_data(hDB, hKey, readback, size, N_READBACK, TID_INT);
   }

   // set equipment status for status web page
   set_equipment_status("Clock Reset", "Waiting for readback", "yellowLight");

   // wait until readback succeeds
   int start_time = ss_time();
   do {
      db_get_data(hDB, hKey, readback, &size, TID_INT);
      for (i=0 ; i<N_READBACK ; i++) {
         if (readback[i] != run_number)
            break;
      }
      if (i == N_READBACK)
         break;

      // don't waste 100% CPU time
      ss_sleep(10);

   } while (ss_time() < start_time + 5); // wait maximal 5 seconds

   if (i < N_READBACK) {
      strcpy(error, "Timeout receiving FEB feedback");
      return FE_ERR_HW;
   }

   // set equipment status for status web page
   set_equipment_status("Clock Reset", "Ok", "greenLight");

   return CM_SUCCESS;
}

/*-- End of Run ----------------------------------------------------*/

INT end_of_run(INT run_number, char *error)
{
   return CM_SUCCESS;
}

/*-- Pause Run -----------------------------------------------------*/

INT pause_run(INT run_number, char *error)
{
   return CM_SUCCESS;
}

/*-- Resume Run ----------------------------------------------------*/

INT resume_run(INT run_number, char *error)
{
   return CM_SUCCESS;
}

/*--- Read Clock and Reset Event to be put into data stream --------*/

 /*
  *
 // example event to be directly injected into the stream
 struct EVENT {
   BANK_HEADER header;
   BANK32 bank;
   DWORD d1[4];
 };

 struct EVENT e = {
   { // BANK_HEADER
     sizeof(BANK32) + ALIGN8(4*sizeof(DWORD)),  // total size
     BANK_FORMAT_VERSION | BANK_FORMAT_32BIT    // flags
   },
   { // BANK32
     {'N', 'A', 'M', 'E'},  // bank name
     TID_DWORD,             // type
     4*sizeof(DWORD)        // data size
   },
   {  1, 2, 3, 4 }           // data
 };

 Inject event via -->

 INT read_cr_event(char *pevent, INT off)
 {
    memcpy(pevent, &e, sizeof(e));
    return bk_size(pevent);
 }
 */

INT read_cr_event(char *pevent, INT off)
{
   bk_init(pevent);

   float *pdata;
   bk_create(pevent, "CRT1", TID_FLOAT, (void **)&pdata);

   *pdata++ = (float) rand() / RAND_MAX;
   *pdata++ = (float) rand() / RAND_MAX;
   *pdata++ = (float) rand() / RAND_MAX;
   *pdata++ = (float) rand() / RAND_MAX;

   bk_close(pevent, pdata);

   return bk_size(pevent);
}

/*--- Called whenever settings have changed ------------------------*/

void cr_settings_changed(HNDLE hDB, HNDLE hKey, INT, void *)
{
   KEY key;

   db_get_key(hDB, hKey, &key);

   if (std::string(key.name) == "Active") {
      BOOL value;
      int size = sizeof(value);
      db_get_data(hDB, hKey, &value, &size, TID_BOOL);
      cm_msg(MINFO, "cr_settings_changed", "Set active to %d", value);
      // TODO: propagate to hardware
   }

   if (std::string(key.name) == "Delay") {
      INT value;
      int size = sizeof(value);
      db_get_data(hDB, hKey, &value, &size, TID_INT);
      cm_msg(MINFO, "cr_settings_changed", "Set delay to %d", value);
      // TODO: propagate to hardware
   }

   if (std::string(key.name) == "Reset Trigger") {
      BOOL value;
      int size = sizeof(value);
      db_get_data(hDB, hKey, &value, &size, TID_BOOL);
      if (value) {
         cm_msg(MINFO, "cr_settings_changed", "Execute reset");
         // TODO: propagate to hardware
         value = FALSE; // reset flag in ODB
         db_set_data(hDB, hKey, &value, sizeof(value), 1, TID_BOOL);
      }
   }

   if (std::string(key.name) == "Sync Trigger") {
      BOOL value;
      int size = sizeof(value);
      db_get_data(hDB, hKey, &value, &size, TID_BOOL);
      if (value) {
         cm_msg(MINFO, "cr_settings_changed", "Execute sync");
         // TODO: propagate to hardware
         value = FALSE; // reset flag in ODB
         db_set_data(hDB, hKey, &value, sizeof(value), 1, TID_BOOL);
      }
   }
}
