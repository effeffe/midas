#include <stdio.h>
#include <epicsStdlib.h>
#include "cadef.h"
#include "dbDefs.h"
#include "epicsTime.h"

int main()
{
   float value = 0;
   chid  id = NULL;
   struct ca_client_context *ca_context;

   setenv("EPICS_CA_ADDR_LIST", "hipa-cagw mpc1363", 1);
   setenv("EPICS_CA_AUTO_ADDR_LIST", "NO", 1);
   setenv("EPICS_CA_SERVER_PORT", "5062", 1);
   
   SEVCHK(ca_context_create(ca_enable_preemptive_callback), "ca_context_create");
   ca_context = ca_current_context();
   SEVCHK(ca_task_initialize(), "ca_task_initialize");
   SEVCHK(ca_create_channel("QSE42:IST:2", 0, 0, 0, &id), "ca_create_channel");
   ca_pend_io(0);
   
   SEVCHK(ca_get(DBR_FLOAT, id, &value), "ca_get");
   ca_pend_io(0);
   
   printf("QSE42:IST:2        %lg\n", value);
         
   return 0;
}
