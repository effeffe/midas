/*******************************************************************\

  Name:         sysmon.cxx
  Created by:   J.T.K.McKenna

  Contents:     Front end for monitoring CPU and Memory usage with MIDAS
  * 
  * Parse /proc/stat and /proc/memstat like htop
  * 
  * I trust users to assign a new instance number for each machine they 
  * want to monitor... eg:
  * ssh mydaq sysmon -i 1
  * ssh myvme sysmon -i 2
  * ssh mypi sysmon -i 3

\********************************************************************/

#ifndef PROCSTATFILE
#define PROCSTATFILE "/proc/stat"
#endif

#ifndef PROCMEMINFOFILE
#define PROCMEMINFOFILE "/proc/meminfo"
#endif

#define String_startsWith(s, match) (strstr((s), (match)) == (s))

#undef NDEBUG // midas required assert() to be always enabled

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>

#include "midas.h"
#include "mfe.h"



/*-- Globals -------------------------------------------------------*/

/* The frontend name (client name) as seen by other MIDAS clients   */

const char *frontend_name = "sysmon";

/* The frontend file name, don't change it */
const char *frontend_file_name = __FILE__;

/* frontend_loop is called periodically if this variable is TRUE    */
BOOL frontend_call_loop = FALSE;

/* a frontend status page is displayed with this frequency in ms */
//INT display_period = 3000;
INT display_period = 0;

/* maximum event size produced by this frontend */
INT max_event_size      = 4*1024*1024;
INT max_event_size_frag = 4*1024*1024;

/* buffer size to hold events */
INT event_buffer_size = 10*1024*1024;

/*-- Function declarations -----------------------------------------*/

INT frontend_init();
INT frontend_exit();
INT begin_of_run(INT run_number, char *error);
INT end_of_run(INT run_number, char *error);
INT pause_run(INT run_number, char *error);
INT resume_run(INT run_number, char *error);
INT frontend_loop();
INT poll_event(INT source, INT count, BOOL test);
INT interrupt_configure(INT cmd, INT source, PTYPE adr);

int read_system_load(char *pevent, int off);

/*-- Equipment list ------------------------------------------------*/

#define EVID_MONITOR 63

EQUIPMENT equipment[] = {

  { "sysmon%02d"   ,       /* equipment name */
    {
      EVID_MONITOR, 0,      /* event ID, trigger mask */
      "SYSTEM",             /* event buffer */
      EQ_PERIODIC,          /* equipment type */
      0,                    /* event source */
      "MIDAS",              /* format */
      TRUE,                 /* enabled */
      RO_ALWAYS,            /* Read when running */
      10000,                /* poll every so milliseconds */
      0,                    /* stop run after this event limit */
      0,                    /* number of sub events */
      1,                    /* history period */
      "", "", ""
    },
    read_system_load,/* readout routine */
  },
  { "" }
};

int cpuCount;
// Not all items in struct are logged, but all are calculated 
// leaving options to log more if we want to...
typedef struct CPUData_ {
   unsigned long long int totalTime;
   unsigned long long int userTime;
   unsigned long long int systemTime;
   unsigned long long int systemAllTime;
   unsigned long long int idleAllTime;
   unsigned long long int idleTime;
   unsigned long long int niceTime;
   unsigned long long int ioWaitTime;
   unsigned long long int irqTime;
   unsigned long long int softIrqTime;
   unsigned long long int stealTime;
   unsigned long long int guestTime;
   unsigned long long int totalPeriod;
   unsigned long long int userPeriod;
   unsigned long long int systemPeriod;
   unsigned long long int systemAllPeriod;
   unsigned long long int idleAllPeriod;
   unsigned long long int idlePeriod;
   unsigned long long int nicePeriod;
   unsigned long long int ioWaitPeriod;
   unsigned long long int irqPeriod;
   unsigned long long int softIrqPeriod;
   unsigned long long int stealPeriod;
   unsigned long long int guestPeriod;
} CPUData;
std::vector<CPUData*> cpus;
void ReadCPUData();
unsigned long long int usertime, nicetime, systemtime, idletime;

//Cycle through these 16 colours when installing History graphs
std::string colours[16]={
     "#00AAFF", "#FF9000", "#FF00A0", "#00C030",
     "#A0C0D0", "#D0A060", "#C04010", "#807060",
     "#F0C000", "#2090A0", "#D040D0", "#90B000",
     "#B0B040", "#B0B0FF", "#FFA0A0", "#A0FFA0"};

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

int event_size = 10*1024;

/*-- Frontend Init -------------------------------------------------*/

HNDLE hSet;
int test_rb_wait_sleep = 1;

// RPC handler

INT rpc_callback(INT index, void *prpc_param[])
{
   const char* cmd  = CSTRING(0);
   const char* args = CSTRING(1);
   char* return_buf = CSTRING(2);
   int   return_max_length = CINT(3);

   cm_msg(MINFO, "rpc_callback", "--------> rpc_callback: index %d, max_length %d, cmd [%s], args [%s]", index, return_max_length, cmd, args);

   //int example_int = strtol(args, NULL, 0);
   //int size = sizeof(int);
   //int status = db_set_value(hDB, 0, "/Equipment/" EQ_NAME "/Settings/example_int", &example_int, size, 1, TID_INT);

   char tmp[256];
   time_t now = time(NULL);
   sprintf(tmp, "{ \"current_time\" : [ %d, \"%s\"] }", (int)now, ctime(&now));

   strlcpy(return_buf, tmp, return_max_length);

   return RPC_SUCCESS;
}



#include "msystem.h"

void BuildHostHistoryPlot()
{
  //Insert myself into the history

   char path[256];
   int status;
   int size;
   int NVARS=5;
   
   /////////////////////////////////////////////////////
   // Setup variables to plot:
   /////////////////////////////////////////////////////
   size = 64; // String length in ODB
   sprintf(path,"/History/Display/sysmon/%s/Variables",getenv("HOSTNAME"));
   {
      char vars[size*NVARS];
      memset(vars, 0, size*NVARS);
      sprintf(vars+size*0,"%s:LOAD[%d]",equipment[0].name,0);
      sprintf(vars+size*1,"%s:LOAD[%d]",equipment[0].name,1);
      sprintf(vars+size*2,"%s:LOAD[%d]",equipment[0].name,2);
      sprintf(vars+size*3,"%s:MEMP",equipment[0].name);
      sprintf(vars+size*4,"%s:SWAP",equipment[0].name);
      status = db_set_value(hDB, 0, path,  vars, size*NVARS, NVARS, TID_STRING);
   }
   assert(status == DB_SUCCESS);

   /////////////////////////////////////////////////////
   // Setup labels 
   /////////////////////////////////////////////////////
   size = 32;
   sprintf(path,"/History/Display/sysmon/%s/Label",getenv("HOSTNAME"));
   {
      char vars[size*NVARS];
      memset(vars, 0, size*NVARS);
      sprintf(vars+size*0,"NICE CPU Load (%%)");
      sprintf(vars+size*1,"USER CPU Load (%%)");
      sprintf(vars+size*2,"SYSTEM CPU Load (%%)");
      sprintf(vars+size*3,"Memory Usage (%%)");
      sprintf(vars+size*4,"Swap Usage (%%)");
      status = db_set_value(hDB, 0, path,  vars, size*NVARS, NVARS, TID_STRING);
   }
   assert(status == DB_SUCCESS);

   /////////////////////////////////////////////////////
   // Setup colours:
   /////////////////////////////////////////////////////
   size = 32;
   sprintf(path,"/History/Display/sysmon/%s/Colour",getenv("HOSTNAME"));
   {
      char vars[size*NVARS];
      memset(vars, 0, size*NVARS);
      for (int i=0; i<NVARS; i++)
         sprintf(vars+size*i,"%s",(colours[i%16]).c_str());
      status = db_set_value(hDB, 0, path,  vars, size*NVARS, NVARS, TID_STRING);
   }
   assert(status == DB_SUCCESS);

}

void BuildHostCPUPlot()
{
   //Insert per CPU graphs into the history
   int status, size;
   char path[256];
   int NVARS=cpuCount;
   /////////////////////////////////////////////////////
   // Setup variables to plot:
   /////////////////////////////////////////////////////
   size = 64;
   sprintf(path,"/History/Display/sysmon/%s-CPU/Variables",getenv("HOSTNAME"));
   {
      char vars[size*NVARS];
      memset(vars, 0, size*NVARS);
      for (int i=0; i<cpuCount; i++)
      {
         int h='0'+i/100;
         int t='0'+(i%100)/10;
         int u='0'+i%10+1;
         if (i<10)
            sprintf(vars+size*i,"%s:CPU%c[3]",equipment[0].name,u);
         else if (i<100)
            sprintf(vars+size*i,"%s:CP%c%c[3]",equipment[0].name,t,u);
         else if (i<1000)
            sprintf(vars+size*i,"%s:C%c%c%c[3]",equipment[0].name,h,t,u);
         else
         {
            cm_msg(MERROR, frontend_name, "Cannot handle a system with more than 1000 CPUs");
            exit(FE_ERR_HW);
         }
      }
      status = db_set_value(hDB, 0, path,  vars, size*NVARS, NVARS, TID_STRING);
   }
   assert(status == DB_SUCCESS);

   /////////////////////////////////////////////////////
   // Setup labels 
   /////////////////////////////////////////////////////
   size = 32;
   sprintf(path,"/History/Display/sysmon/%s-CPU/Label",getenv("HOSTNAME"));
   {
      char vars[size*NVARS];
      memset(vars, 0, size*NVARS);
      for (int i=0; i<cpuCount; i++)
         sprintf(vars+size*i,"CPU%d Load (%%)",i+1);
      status = db_set_value(hDB, 0, path,  vars, size*NVARS, NVARS, TID_STRING);
   }
   assert(status == DB_SUCCESS);

   /////////////////////////////////////////////////////
   // Setup colours:
   /////////////////////////////////////////////////////
   size = 32;
   sprintf(path,"/History/Display/sysmon/%s-CPU/Colour",getenv("HOSTNAME"));
   {
      char vars[size*NVARS];
      memset(vars, 0, size*NVARS);
      for (int i=0; i<NVARS; i++)
         sprintf(vars+size*i,"%s",(colours[i%16]).c_str());
      status = db_set_value(hDB, 0, path,  vars, size*NVARS, NVARS, TID_STRING);
   }
   assert(status == DB_SUCCESS);

}


INT frontend_init()
{
   int status;
   
   printf("frontend_init!\n");

   FILE* file = fopen(PROCSTATFILE, "r");
   if (file == NULL) {
      cm_msg(MERROR, frontend_name, "Cannot open " PROCSTATFILE);
      return FE_ERR_HW;
   }
   char buffer[256];
   int Ncpus = -1;
   do {
      Ncpus++;
      fgets(buffer, 255, file);
   } while (String_startsWith(buffer, "cpu"));
   fclose(file);
   cpuCount = MAX(Ncpus - 1, 1);

   //Note, cpus[0] is a total for all CPUs
   for (int i = 0; i <= cpuCount; i++) {
      cpus.push_back(new CPUData);
   }
   ReadCPUData();

   BuildHostHistoryPlot();
   BuildHostCPUPlot();

#ifdef RPC_JRPC
   status = cm_register_function(RPC_JRPC, rpc_callback);
   assert(status == SUCCESS);
#endif
   return SUCCESS;
}

/*-- Frontend Exit -------------------------------------------------*/

INT frontend_exit()
{
   printf("frontend_exit!\n");
   return SUCCESS;
}

/*-- Begin of Run --------------------------------------------------*/

INT begin_of_run(INT run_number, char *error)
{
   printf("begin_of_run %d\n", run_number);

   int fail = 0;
   int status;
   int size;

   size = sizeof(fail);
   status = db_get_value(hDB, hSet, "fail_begin_of_run", &fail, &size, TID_INT, TRUE);
   assert(status == DB_SUCCESS);

   if (fail) {
      printf("fail_begin_of_run: returning error status %d\n", fail);
      return fail;
   }
   
   
   int s = 0;
   size = sizeof(s);
   status = db_get_value(hDB, hSet, "sleep_begin_of_run", &s, &size, TID_INT, TRUE);
   assert(status == DB_SUCCESS);
   
   if (s) {
      printf("sleep_begin_of_run: calling ss_sleep(%d)\n", s);
      ss_sleep(s);
   }
   return SUCCESS;
}

/*-- End of Run ----------------------------------------------------*/

INT end_of_run(INT run_number, char *error)
{
   printf("end_of_run %d\n", run_number);

   return SUCCESS;
}

/*-- Pause Run -----------------------------------------------------*/

INT pause_run(INT run_number, char *error)
{
   printf("pause_run %d\n", run_number);

   int fail = 0;
   int status;
   int size;

   size = sizeof(fail);
   status = db_get_value(hDB, hSet, "fail_pause_run", &fail, &size, TID_INT, TRUE);
   assert(status == DB_SUCCESS);

   if (fail) {
      printf("fail_pause_run: returning error status %d\n", fail);
      return fail;
   }
   
   return SUCCESS;
}

/*-- Resume Run ----------------------------------------------------*/

INT resume_run(INT run_number, char *error)
{
   printf("resume_run %d\n", run_number);

   int fail = 0;
   int status;
   int size;

   size = sizeof(fail);
   status = db_get_value(hDB, hSet, "fail_resume_run", &fail, &size, TID_INT, TRUE);
   assert(status == DB_SUCCESS);

   if (fail) {
      printf("fail_resume_run: returning error status %d\n", fail);
      return fail;
   }

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

INT poll_event(INT source, INT count, BOOL test)
/* Polling routine for events. Returns TRUE if event
   is available. If test equals TRUE, don't return. The test
   flag is used to time the polling */
{
   if (test) {
      ss_sleep (count);
   }
   return (0);
}

/*-- Interrupt configuration ---------------------------------------*/

INT interrupt_configure(INT cmd, INT source, PTYPE adr)
{
   printf("interrupt_configure!\n");

   switch(cmd)
      {
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
void ReadCPUData()
{
   //Largely from htop: https://github.com/hishamhm/htop (GNU licence)
   FILE* file = fopen(PROCSTATFILE, "r");
   if (file == NULL) {
      cm_msg(MERROR, frontend_name, "Cannot open " PROCSTATFILE);
   }
   for (int i = 0; i <= cpuCount; i++) {
      char buffer[256];
      int cpuid;
      unsigned long long int ioWait, irq, softIrq, steal, guest, guestnice;
      unsigned long long int systemalltime, idlealltime, totaltime, virtalltime;
      ioWait = irq = softIrq = steal = guest = guestnice = 0;
      // Dependending on your kernel version,
      // 5, 7, 8 or 9 of these fields will be set.
      // The rest will remain at zero.
      fgets(buffer, 255, file);
      if (i == 0)
         sscanf(buffer, "cpu  %16llu %16llu %16llu %16llu %16llu %16llu %16llu %16llu %16llu %16llu", &usertime, &nicetime, &systemtime, &idletime, &ioWait, &irq, &softIrq, &steal, &guest, &guestnice);
      else {
         sscanf(buffer, "cpu%4d %16llu %16llu %16llu %16llu %16llu %16llu %16llu %16llu %16llu %16llu", &cpuid, &usertime, &nicetime, &systemtime, &idletime, &ioWait, &irq, &softIrq, &steal, &guest, &guestnice);
         assert(cpuid == i - 1);
      }
      // Guest time is already accounted in usertime
      usertime = usertime - guest;
      nicetime = nicetime - guestnice;
      // Fields existing on kernels >= 2.6
      // (and RHEL's patched kernel 2.4...)
      idlealltime = idletime + ioWait;
      systemalltime = systemtime + irq + softIrq;
      virtalltime = guest + guestnice;
      totaltime = usertime + nicetime + systemalltime + idlealltime + steal + virtalltime;
      CPUData* cpuData = cpus.at(i);
      cpuData->userPeriod = usertime - cpuData->userTime;
      cpuData->nicePeriod = nicetime - cpuData->niceTime;
      cpuData->systemPeriod = systemtime - cpuData->systemTime;
      cpuData->systemAllPeriod = systemalltime - cpuData->systemAllTime;
      cpuData->idleAllPeriod = idlealltime - cpuData->idleAllTime;
      cpuData->idlePeriod = idletime - cpuData->idleTime;
      cpuData->ioWaitPeriod = ioWait - cpuData->ioWaitTime;
      cpuData->irqPeriod = irq - cpuData->irqTime;
      cpuData->softIrqPeriod = softIrq - cpuData->softIrqTime;
      cpuData->stealPeriod = steal - cpuData->stealTime;
      cpuData->guestPeriod = virtalltime - cpuData->guestTime;
      cpuData->totalPeriod = totaltime - cpuData->totalTime;
      cpuData->userTime = usertime;
      cpuData->niceTime = nicetime;
      cpuData->systemTime = systemtime;
      cpuData->systemAllTime = systemalltime;
      cpuData->idleAllTime = idlealltime;
      cpuData->idleTime = idletime;
      cpuData->ioWaitTime = ioWait;
      cpuData->irqTime = irq;
      cpuData->softIrqTime = softIrq;
      cpuData->stealTime = steal;
      cpuData->guestTime = virtalltime;
      cpuData->totalTime = totaltime;
   }
   fclose(file);
   //end htop code
}

/*-- Event readout -------------------------------------------------*/
#include <fstream>
int read_system_load(char *pevent, int off)
{
   bk_init32(pevent);


   ReadCPUData();

   //Calculate load:
   double CPULoadTotal[4]; //nice, user, system, total
   for (int j=0; j<4; j++)
      CPULoadTotal[j]=0;

   double CPULoad[4]; //nice, user, system, total
   for (int i = 0; i <= cpuCount; i++) {
      CPUData* cpuData = (cpus[i]);
      double total = (double) ( cpuData->totalPeriod == 0 ? 1 : cpuData->totalPeriod);
      CPULoad[0] = cpuData->nicePeriod / total * 100.0;
      CPULoad[1] = cpuData->userPeriod / total * 100.0;
      CPULoad[2] = cpuData->systemPeriod / total * 100.0;
      CPULoad[3]=CPULoad[0]+CPULoad[1]+CPULoad[2];

      for (int j=0; j<4; j++)
      {
         CPULoadTotal[j]+=CPULoad[j];
      }

      // This is a little long for just setting a bank name, but it
      // avoids format-truncation warnings and supports machines with upto
      // 1000 CPUs... another case can be put in when we reach that new limit
      char name[5]="LOAD";
      //i==0 is a total for ALL Cpus
      if (i!=0)
      {
         int h='0'+i/100;
         int t='0'+(i%100)/10;
         int u='0'+i%10;
         if (i<10)
            snprintf(name,5,"CPU%c",u);
         else if (i<100)
            snprintf(name,5,"CP%c%c",t,u);
         else if (i<1000)
            snprintf(name,5,"C%c%c%c",h,t,u);
         else
            cm_msg(MERROR, frontend_name, "Cannot handle a system with more than 1000 CPUs");
      }
      double* a;
      bk_create(pevent, name, TID_DOUBLE, (void**)&a);
      for (int k=0; k<4; k++)
      {
         *a=CPULoad[k];
         a++;
      }
      bk_close(pevent,a);
   
   }
   
   //Again from htop:
   unsigned long long int totalMem;
   unsigned long long int usedMem;
   unsigned long long int freeMem;
   unsigned long long int sharedMem;
   unsigned long long int buffersMem;
   unsigned long long int cachedMem;
   unsigned long long int totalSwap;
   unsigned long long int usedSwap;
   unsigned long long int freeSwap;
   FILE* file = fopen(PROCMEMINFOFILE, "r");
   if (file == NULL) {
      cm_msg(MERROR, frontend_name, "Cannot open " PROCMEMINFOFILE);
   }
   char buffer[128];
   while (fgets(buffer, 128, file)) {
       switch (buffer[0]) {
      case 'M':
         if (String_startsWith(buffer, "MemTotal:"))
            sscanf(buffer, "MemTotal: %32llu kB", &totalMem);
         else if (String_startsWith(buffer, "MemFree:"))
            sscanf(buffer, "MemFree: %32llu kB", &freeMem);
         else if (String_startsWith(buffer, "MemShared:"))
            sscanf(buffer, "MemShared: %32llu kB", &sharedMem);
         break;
      case 'B':
         if (String_startsWith(buffer, "Buffers:"))
            sscanf(buffer, "Buffers: %32llu kB", &buffersMem);
         break;
      case 'C':
         if (String_startsWith(buffer, "Cached:"))
            sscanf(buffer, "Cached: %32llu kB", &cachedMem);
         break;
      case 'S':
         if (String_startsWith(buffer, "SwapTotal:"))
            sscanf(buffer, "SwapTotal: %32llu kB", &totalSwap);
         if (String_startsWith(buffer, "SwapFree:"))
            sscanf(buffer, "SwapFree: %32llu kB", &freeSwap);
         break;
      }
   }
   fclose(file);
   //end htop code

   usedMem = totalMem - cachedMem- freeMem;
   usedSwap = totalSwap - freeSwap;
   double mem_percent=100.*(double)usedMem/(double)totalMem;
   double swap_percent=100;
   if (totalSwap) //If there is an swap space, calculate... else always say 100% used
      swap_percent=100*(double)usedSwap/(double)totalSwap;
#ifdef FE_DEBUG
   printf("-----------------------------\n");
   printf("MemUsed:  %lld kB (%lld GB) (%.2f%%)\n",usedMem,usedMem/1024/1024,mem_percent);
   printf("SwapUsed: %lld kB (%lld GB) (%.2f%%)\n",usedSwap,usedSwap/1024/1024,swap_percent);
   printf("-----------------------------\n");
#endif
   double* m;
   bk_create(pevent, "MEMP", TID_DOUBLE, (void**)&m);
   *m=mem_percent;
   bk_close(pevent,m+1);

   bk_create(pevent, "SWAP", TID_DOUBLE, (void**)&m);
   *m=swap_percent;
   bk_close(pevent,m+1);

   return bk_size(pevent);
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
