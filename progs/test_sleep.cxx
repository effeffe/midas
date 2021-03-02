//
// test_sleep.cxx
//
// Test the sleep function
//

#include <stdio.h>
//#include <signal.h> // SIGPIPE
//#include <assert.h> // assert()
//#include <stdlib.h> // malloc()
//#include <math.h> // M_PI

//#include "midas.h"
#include "tmfe.h"

void test(double total_sleep, double call_sleep)
{
   double start_time = TMFE::GetTime();
   double count = 0;
   double incr = 0;

   int loops = total_sleep/call_sleep;

   for (int i=0; i<loops; i++) {
      TMFE::Sleep(call_sleep);
      count += 1.0;
      incr += call_sleep;
   }
   
   double end_time = TMFE::GetTime();

   double elapsed = end_time - start_time;
   double actual_sleep = elapsed/count;

   printf("sleep %7.0f loops, %.6f sec per loop, %.6f sec total, %12.3f usec actual, %.3f usec actual per loop, oversleep %.3f usec, %.1f%%\n",
          count, call_sleep, incr,
          elapsed*1e6, actual_sleep*1e6,
          (actual_sleep - call_sleep)*1e6,
          (actual_sleep - call_sleep)/call_sleep*100.0);
}

int main(int argc, char* argv[])
{
   setbuf(stdout, NULL);
   setbuf(stderr, NULL);

   signal(SIGPIPE, SIG_IGN);
   
   //TMFE* mfe = TMFE::Instance();

   test(1.0, 0.1);
   test(1.0, 0.01);
   test(1.0, 0.001);
   test(1.0, 0.0001);
   test(1.0, 0.00001);
   test(1.0, 0.000001);
   test(0.1, 0.0000001);
   test(0.01, 0.00000001);

   return 0;
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
