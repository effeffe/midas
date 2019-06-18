/********************************************************************\
Example period frontend.  Exercises certain MIDAS functions, including
i) db_check_record to ensure that ODB matchs expectation
ii) deferred transition; frontend waits 5 extra seconds at end of run with deferred transition.
 Thomas Lindner (TRIUMF)
\********************************************************************/


#include <vector>
#include <stdio.h>
#include <algorithm>
#include <stdlib.h>
#include "midas.h"
#include "mfe.h"
#include <stdint.h>
#include <iostream>
#include <sstream>
#include <unistd.h>

/*-- Globals -------------------------------------------------------*/

/* The frontend name (client name) as seen by other MIDAS clients   */
const char *frontend_name = "fesimdaq";
/* The frontend file name, don't change it */
const char *frontend_file_name = __FILE__;

/* frontend_loop is called periodically if this variable is TRUE    */
BOOL frontend_call_loop = TRUE;

/* a frontend status page is displayed with this frequency in ms */
INT display_period = 0;

/* maximum event size produced by this frontend */
INT max_event_size =  3 * 1024 * 1024;

/* maximum event size for fragmented events (EQ_FRAGMENTED) --- not really used here */
INT max_event_size_frag = 2 * 1024 * 1024;

/* buffer size to hold events */
INT event_buffer_size = 20 * 1000000;
 void **info;
char  strin[256];
//HNDLE hDB;
HNDLE hSet;






/*-- Function declarations -----------------------------------------*/

INT frontend_init();
INT frontend_exit();
INT begin_of_run(INT run_number, char *error);
INT end_of_run(INT run_number, char *error);
INT pause_run(INT run_number, char *error);
INT resume_run(INT run_number, char *error);
INT frontend_loop();

INT read_trigger_event(char *pevent, INT off);
INT read_scaler_event(char *pevent, INT off);


/*-- Equipment list ------------------------------------------------*/

#undef USE_INT

EQUIPMENT equipment[] = {

   {"SIMDAQ",               /* equipment name */
    {1, 0,                   /* event ID, trigger mask */
     "SYSTEM",               /* event buffer */
#ifdef USE_INT
     EQ_INTERRUPT,           /* equipment type */
#else
     EQ_PERIODIC,              /* equipment type */
#endif
     LAM_SOURCE(0, 0xFFFFFF),        /* event source crate 0, all stations */
     "MIDAS",                /* format */
     TRUE,                   /* enabled */
     RO_RUNNING,           /* read only when running */
     1000,                    /* poll for 1000ms */
     0,                      /* stop run after this event limit */
     0,                      /* number of sub events */
     0,                      /* don't log history */
     "", "", "",},
    read_trigger_event,      /* readout routine */
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

BOOL finished_readout=FALSE;

int nremaining = -1;
// Deferred transition where we take 5 extra events...
BOOL wait_end_cycle(int transition, BOOL first)
 {
   //printf("wait_end_cycle: transition %d, first %d\n", transition, first);
   
   if(first){
     std::cout << "Starting deferred transition" << std::endl;
     nremaining = 10;
   }

   if (finished_readout) {
     return TRUE;
   }

   return FALSE;
 }


#define SIMDAQSETTINGS_STR(_name) const char *_name[] = {\
"[.]",\
"Descrip = STRING : [256] ",\
"Parameter AAA = INT : 0",\
"Parameter BB2 = INT : 0",			\
"Parameter CC22 = INT : 0",\
"",\
NULL }

SIMDAQSETTINGS_STR(simdaqsettings_str);

/*-- Frontend Init -------------------------------------------------*/
INT frontend_init()
{

  
  int status = db_check_record(hDB, 0, "/Equipment/SIMDAQ/Settings", strcomb(simdaqsettings_str), TRUE);
  printf("Status %i\n",status);
  if (status == DB_STRUCT_MISMATCH) {
    cm_msg(MERROR, "init_simdaqsettings", "Aborting on mismatching /Equipment/SIMDAQ/Settings");
    cm_disconnect_experiment();
    abort();
  }

  cm_register_deferred_transition(TR_STOP, wait_end_cycle);
   return SUCCESS;
}




/*-- Frontend Exit -------------------------------------------------*/

INT frontend_exit()
{
  printf("Exiting fedcrc!\n");
   return SUCCESS;
}

/*-- Begin of Run --------------------------------------------------*/
// Upon run stasrt, read ODB settings and write them to DCRC
INT begin_of_run(INT run_number, char *error)
{

  nremaining = -1;
  finished_readout=FALSE;
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
  usleep(10000);
  return SUCCESS;
}

/*------------------------------------------------------------------*/

/********************************************************************\

  Readout routines for different events

\********************************************************************/

/*-- Trigger event routines ----------------------------------------*/
// Not currently used for DCRC readout
INT poll_event(INT source, INT count, BOOL test)
/* Polling routine for events. Returns TRUE if event
   is available. If test equals TRUE, don't return. The test
   flag is used to time the polling */
{
   int i;

   for (i = 0; i < count; i++) {
//      cam_lam_read(LAM_SOURCE_CRATE(source), &lam);

//      if (lam & LAM_SOURCE_STATION(source))
         if (!test)
            return 1;
   }

   usleep(1000);
   return 0;
}

/*-- Interrupt configuration ---------------------------------------*/
// This is not currently used by the DCRC readout
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

#include "math.h" // for RAND, and rand
double sampleNormal() {
    double u = ((double) rand() / (RAND_MAX)) * 2 - 1;
    double v = ((double) rand() / (RAND_MAX)) * 2 - 1;
    double r = u * u + v * v;
    if (r == 0 || r > 1) return sampleNormal();
    double c = sqrt(-2 * log(r) / r);
    return u * c;
}
#include <sys/time.h>
/*-- Event readout -------------------------------------------------*/
INT read_trigger_event(char *pevent, INT off)
{

  /* init bank structure */
  bk_init32(pevent);
  
  uint32_t *pdata32;
  /* create structured ADC0 bank of double words (i.e. 4-byte words)  */
  bk_create(pevent, "ADC0", TID_DWORD, (void **)&pdata32);
 
  // Add a header, with number of words in event.
  // Use the top two bits to indicate different control words.
  // 11 -> 0xcXXXXXXX  : overall header
  // 01 -> 0x4XXXXXXX  : trigger header
  // 00 -> 0x0XXXXXXX  : channel header
  // 10 -> 0x8XXXXXXX  : trailer
  
  // Write the bank header to the bank, containing the number of triggers
  *pdata32++ = 0xfa000200;

  int sample = floor(sampleNormal()*80)+1200;
  if((rand() % 100) > 80) sample += 200;
  *pdata32++ = 0xf800406b + sample;

  int sample2 = floor(sampleNormal()*120)+1000;
  *pdata32++ = 0xf801405e + sample2;

  int sample3 = floor(sampleNormal()*100)+1000;
  *pdata32++ = 0xf802405e + sample3;

  int sample4 = floor(sampleNormal()*20)+950;
  *pdata32++ = 0xf803405e + sample4;

  int sample5 = floor(sampleNormal()*50)+1300;
  *pdata32++ = 0xf804405e + sample5;

  int sample6 = floor(sampleNormal()*200)+2000;
  *pdata32++ = 0xf805405e + sample6;



  *pdata32++ = 0xfcfd57e8;

  
  int size2 = bk_close(pevent, pdata32);    
  

  uint32_t *pdata322;
  bk_create(pevent, "ADC1", TID_DWORD, (void **)&pdata322);
 

  //for(int i = 0; i < 100000; i++)
    //*pdata322++ = i;
  
  size2 = bk_close(pevent, pdata322);    

  uint32_t *pdata720;
  bk_create(pevent, "W200", TID_DWORD, (void **)&pdata720);
 

  *pdata720++ = 0xa00001f8;
  *pdata720++ = 0xfff703;
  *pdata720++ = 0x0;
  *pdata720++ = 0x13132f;
  
  int pulse_position = floor(sampleNormal()*80) + 200;
  for(int j = 0; j < 2; j++){

    if (j==1) pulse_position+=  5;
    int pulse_height = floor(sampleNormal()*30) +200;  
    pulse_height = pulse_height * (1.0 - ((double)pulse_position)/600.0);

    for(int i = 0; i < 504; i++){
      uint32_t word = 0;
      uint32_t sample = 400 + floor(sampleNormal()*2);
      if(i == pulse_position) sample += 0.2 * pulse_height;
      if(i == pulse_position+1) sample += 0.8 * pulse_height;
      if(i == pulse_position+2) sample += 1.0 * pulse_height;
      if(i == pulse_position+3) sample += 0.9 * pulse_height;
      if(i == pulse_position+4) sample += 0.5 * pulse_height;
      if(i == pulse_position+5) sample += 0.3 * pulse_height;
      if(i == pulse_position+6) sample += 0.1 * pulse_height;    
      //std::cout << i << " " << pulse_position << " " << pulse_height << " " << sample << std::endl;
      word = sample;
      i++;
      sample = 400 + floor(sampleNormal()*2);
      if(i == pulse_position) sample += 0.2 * pulse_height;
      if(i == pulse_position+1) sample += 0.8 * pulse_height;
      if(i == pulse_position+2) sample += 1.0 * pulse_height;
      if(i == pulse_position+3) sample += 0.9 * pulse_height;
      if(i == pulse_position+4) sample += 0.5 * pulse_height;
      if(i == pulse_position+5) sample += 0.3 * pulse_height;
      if(i == pulse_position+6) sample += 0.1 * pulse_height;    
      //std::cout << j << " " << i << " " << pulse_position << " " << pulse_height << " " << sample << std::endl;
      word += (sample << 16);
      *pdata720++ = word;
    }  
  }
  
  

  size2 = bk_close(pevent, pdata720);    



  struct timeval start; //,end;
  gettimeofday(&start,NULL);
  if(0)printf ("simdaq request: %f\n",start.tv_sec
      + 0.000001*start.tv_usec); 
  // close the bank

  // handle the deferred transition
  if(nremaining > 0){ std::cout << "deferring " << nremaining << std::endl; nremaining--;
  };

  if(nremaining == 0){
    finished_readout=TRUE;
    std::cout << "Finished readout... " << std::endl;
  }

    /*  
  printf("Start sleep\n");
  bool done = false;
  double time;
  while(!done){
    sleep(5);
    gettimeofday(&end,NULL);
    time = end.tv_sec - start.tv_sec + (end.tv_usec - start.tv_usec)*0.000001;
    if(time > 10) done = true;
  }
  
      //}
  printf("End sleep. time %f\n", time);
  */
  
  usleep(10000);
  return bk_size(pevent);
}



