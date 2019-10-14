/*******************************************************************\

  Name:         sysmon.cxx
  Created by:   J.T.K.McKenna

  Contents:     Front end for monitoring CPU and Memory usage with MIDAS
  * 
  * Parse /proc/stat and /proc/memstat like htop
  * 
  * A new instance can be launched for each machine you may want  
  * to monitor... eg:
  * ssh mydaq sysmon
  * ssh myvme sysmon
  * ssh mypi sysmon

\********************************************************************/

#ifndef PROCSTATFILE
#define PROCSTATFILE "/proc/stat"
#endif

#ifndef PROCMEMINFOFILE
#define PROCMEMINFOFILE "/proc/meminfo"
#endif
#include <cstring>
#define String_startsWith(s, match) (strstr((s), (match)) == (s))

#undef NDEBUG // midas required assert() to be always enabled

#include <stdio.h>
#include <signal.h> // SIGPIPE
#include <assert.h> // assert()
#include <stdlib.h> // malloc()

#include "midas.h"
#include "tmfe.h"

class sysmon :
   public TMFeRpcHandlerInterface,
   public  TMFePeriodicHandlerInterface   
{
public:
   TMFE* fMfe;
   TMFeEquipment* fEq;

   int fEventSize;
   char* fEventBuf;

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
   
   unsigned long long int usertime, nicetime, systemtime, idletime;

   //Cycle through these 16 colours when installing History graphs
   std::string colours[16]={
     "#00AAFF", "#FF9000", "#FF00A0", "#00C030",
     "#A0C0D0", "#D0A060", "#C04010", "#807060",
     "#F0C000", "#2090A0", "#D040D0", "#90B000",
     "#B0B040", "#B0B0FF", "#FFA0A0", "#A0FFA0"};


   sysmon(TMFE* mfe, TMFeEquipment* eq) // ctor
   {
      fMfe = mfe;
      fEq  = eq;
      fEventSize = 0;
      fEventBuf  = NULL;
   }
   ~sysmon() // dtor
   {
      if (fEventBuf) {
         free(fEventBuf);
         fEventBuf = NULL;
      }
   }

//Custom functions:

   void BuildHostHistoryPlot()
   {

      MVOdb* LOAD = fMfe->fOdbRoot->Chdir((std::string("History/Display/sysmon/") + fMfe->fFrontendHostname).c_str(), true);

      //Insert myself into the history
      int size = 64;
      int NVARS=5;
      char var[64];
      std::vector<std::string> vars;
      /////////////////////////////////////////////////////
      // Setup variables to plot:
      /////////////////////////////////////////////////////
      sprintf(var,"%s:LOAD[%d]",fEq->fName.c_str(),0);
      vars.push_back(var);
      sprintf(var,"%s:LOAD[%d]",fEq->fName.c_str(),1);
      vars.push_back(var);
      sprintf(var,"%s:LOAD[%d]",fEq->fName.c_str(),2);
      vars.push_back(var);
      sprintf(var,"%s:MEMP",fEq->fName.c_str());
      vars.push_back(var);
      sprintf(var,"%s:SWAP",fEq->fName.c_str());
      vars.push_back(var);
      LOAD->WSA("Variables",vars,size);

      /////////////////////////////////////////////////////
      // Setup labels 
      /////////////////////////////////////////////////////
      size = 32;
      vars.clear();
      sprintf(var,"NICE CPU Load (%%)");
      vars.push_back(var);
      sprintf(var,"USER CPU Load (%%)");
      vars.push_back(var);
      sprintf(var,"SYSTEM CPU Load (%%)");
      vars.push_back(var);
      sprintf(var,"Memory Usage (%%)");
      vars.push_back(var);
      sprintf(var,"Swap Usage (%%)");
      vars.push_back(var);
      LOAD->WSA("Label",vars,size);
      
      /////////////////////////////////////////////////////
      // Setup colours:
      /////////////////////////////////////////////////////
      vars.clear();
      for (int i=0; i<NVARS; i++)
         vars.push_back(colours[i%16].c_str());
      LOAD->WSA("Colour",vars,size);

   }

   void BuildHostCPUPlot()
   {
      MVOdb* CPULOAD = fMfe->fOdbRoot->Chdir((std::string("History/Display/sysmon/") + fMfe->fFrontendHostname + "-CPU").c_str(), true);
      //Insert per CPU graphs into the history
      int size = 64;
      char var[64];
      std::vector<std::string> vars;

      /////////////////////////////////////////////////////
      // Setup variables to plot:
      /////////////////////////////////////////////////////
      for (int i=0; i<cpuCount; i++)
      {
         int h='0'+i/100;
         int t='0'+(i%100)/10;
         int u='0'+i%10+1;
         if (i<10)
            sprintf(var,"%s:CPU%c[3]",fEq->fName.c_str(),u);
         else if (i<100)
            sprintf(var,"%s:CP%c%c[3]",fEq->fName.c_str(),t,u);
         else if (i<1000)
            sprintf(var,"%s:C%c%c%c[3]",fEq->fName.c_str(),h,t,u);
         else
            cm_msg(MERROR, fEq->fName.c_str(), "Cannot handle a system with more than 1000 CPUs");
         vars.push_back(var);
      }
      CPULOAD->WSA("Variables",vars,size);

   
      /////////////////////////////////////////////////////
      // Setup labels 
      /////////////////////////////////////////////////////
      char strr[32];
      size = 32;
      vars.clear();
      for (int i=0; i<cpuCount; i++)
      {
         sprintf(var,"CPU%d Load (%%)",i+1);
         vars.push_back(var);
      }
      CPULOAD->WSA("Label",vars,size);
      
      /////////////////////////////////////////////////////
      // Setup colours:
      /////////////////////////////////////////////////////
      vars.clear();
      for (int i=0; i<cpuCount; i++)
         vars.push_back(colours[i%16].c_str());
      CPULOAD->WSA("Colour",vars,size);
   }

   void ReadCPUData()
   {
      //Largely from htop: https://github.com/hishamhm/htop (GNU licence)
      FILE* file = fopen(PROCSTATFILE, "r");
      if (file == NULL) {
         cm_msg(MERROR, fEq->fName.c_str(), "Cannot open " PROCSTATFILE);
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


   void Init()
   {
      fEventSize = 8*1024;
      fEq->fOdbEqSettings->RI("event_size", &fEventSize, true);
      if (fEventBuf) {
         free(fEventBuf);
      }
      fEventBuf = (char*)malloc(fEventSize);
      
      
      FILE* file = fopen(PROCSTATFILE, "r");
      if (file == NULL) {
         cm_msg(MERROR, fEq->fName.c_str(), "Cannot open " PROCSTATFILE);
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
      fEq->SetStatus("Running", "#00FF00");
      printf("Init done");
   }


   void SendEvent()
   {
      fEq->ComposeEvent(fEventBuf, fEventSize);
      fEq->BkInit(fEventBuf, fEventSize);
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
               cm_msg(MERROR, fEq->fName.c_str(), "Cannot handle a system with more than 1000 CPUs");
         }
         double* a = (double*)fEq->BkOpen(fEventBuf, name, TID_DOUBLE);
         for (int k=0; k<4; k++)
         {
            *a=CPULoad[k];
            a++;
         }
         fEq->BkClose(fEventBuf, a);
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
         cm_msg(MERROR, fEq->fName.c_str(), "Cannot open " PROCMEMINFOFILE);
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
      printf("-----------------------------\n");
      printf("MemUsed:  %lld kB (%lld GB) (%.2f%%)\n",usedMem,usedMem/1024/1024,mem_percent);
      printf("SwapUsed: %lld kB (%lld GB) (%.2f%%)\n",usedSwap,usedSwap/1024/1024,swap_percent);
      printf("-----------------------------\n");

      double* m = (double*)fEq->BkOpen(fEventBuf, "MEMP", TID_DOUBLE);
      *m=mem_percent;
      fEq->BkClose(fEventBuf, m+1);

      m = (double*)fEq->BkOpen(fEventBuf, "SWAP", TID_DOUBLE);
      *m=swap_percent;
      fEq->BkClose(fEventBuf, m+1);

      fEq->SendEvent(fEventBuf);
      fEq->WriteStatistics();
      return;

   }

   std::string HandleRpc(const char* cmd, const char* args)
   {
      fMfe->Msg(MINFO, "HandleRpc", "RPC cmd [%s], args [%s]", cmd, args);
      return "OK";
   }



   void HandlePeriodic()
   {
      printf("periodic!\n");
      SendEvent();
      //char buf[256];
      //sprintf(buf, "buffered %d (max %d), dropped %d, unknown %d, max flushed %d", gUdpPacketBufSize, fMaxBuffered, fCountDroppedPackets, fCountUnknownPackets, fMaxFlushed);
      //fEq->SetStatus(buf, "#00FF00");
      //fEq->WriteStatistics();
   }
};

static void usage()
{
   fprintf(stderr, "Usage: sysmon ...\n");
   exit(1);
}

int main(int argc, char* argv[])
{
   setbuf(stdout, NULL);
   setbuf(stderr, NULL);

   signal(SIGPIPE, SIG_IGN);

   const char* hostname=getenv("HOSTNAME");
   if (!hostname) {
       printf("Cannot get hostname from ENV\n");
      return 1;
   }
    
   std::string EquipmentName = "sysmon_";
   EquipmentName+=hostname;
/*
   std::string name = "";

   if (argc == 2) {
      name = argv[1];
   } else {
      usage(); // DOES NOT RETURN
   }
*/
   TMFE* mfe = TMFE::Instance();

   TMFeError err = mfe->Connect(EquipmentName.c_str(), __FILE__,"127.0.0.1","test");
   if (err.error) {
      printf("Cannot connect, bye.\n");
      return 1;
   }

   //mfe->SetWatchdogSec(0);

   TMFeCommon *common = new TMFeCommon();
   common->EventID = 99;
   common->LogHistory = 1;
   //common->Buffer = "SYSTEM";
   
   TMFeEquipment* eq = new TMFeEquipment(mfe, EquipmentName.c_str(), common);
   eq->Init();
   eq->SetStatus("Starting...", "white");
   eq->ZeroStatistics();
   eq->WriteStatistics();

   mfe->RegisterEquipment(eq);

   sysmon* myfe = new sysmon(mfe, eq);

   mfe->RegisterRpcHandler(myfe);

   //mfe->SetTransitionSequenceStart(910);
   //mfe->SetTransitionSequenceStop(90);
   mfe->DeregisterTransitionPause();
   mfe->DeregisterTransitionResume();

   myfe->Init();

   mfe->RegisterPeriodicHandler(eq, myfe);
   //eq->SetStatus("Running", "#00FF00");
   //eq->SetStatus("Started...", "white");

   while (!mfe->fShutdownRequested) {
      mfe->PollMidas(10);
   }

   mfe->Disconnect();

   return 0;
}





/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
