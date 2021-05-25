/*******************************************************************\

  Name:         tmfe_example_indexed.cxx
  Created by:   K.Olchanski

  Contents:     Example of indexed equipment

\********************************************************************/

#undef NDEBUG // midas required assert() to be always enabled

#include <stdio.h>
#include <math.h> // M_PI

#include "tmfe.h"

class EqPeriodic :
   public TMFeEquipment
{
public:
   EqPeriodic(const char* eqname, const char* eqfilename) // ctor
      : TMFeEquipment(eqname, eqfilename)
   {
      /* configure your equipment here */

      fEqConfReadConfigFromOdb = false;
      fEqConfEventID = 3;
      fEqConfBuffer = "BUF%02d";
      fEqConfPeriodMilliSec = 1000; // in milliseconds
      fEqConfLogHistory = 0;
      fEqConfWriteEventsToOdb = false;
   }

   void HandlePeriodic()
   {
      char buf[1024];

      ComposeEvent(buf, sizeof(buf));
      BkInit(buf, sizeof(buf));

      /* create SCLR bank */
      uint32_t* pdata = (uint32_t*)BkOpen(buf, "PRDC", TID_UINT32);
      
      /* following code "simulates" some values in sine wave form */
      for (int a = 0; a < 16; a++)
         *pdata++ = 100*sin(M_PI*time(NULL)/60+a/2.0)+100;
      
      BkClose(buf, pdata);

      EqSendEvent(buf);
   }
};

class FeExample: public TMFrontend
{
public:
   FeExample() // ctor
   {
      /* register with the framework */
      FeSetName("example_%02d");
      FeAddEquipment(new EqPeriodic("example_%02d", __FILE__));
   }
};

int main(int argc, char* argv[])
{
   FeExample fe_example;
   return fe_example.FeMain(argc, argv);
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
