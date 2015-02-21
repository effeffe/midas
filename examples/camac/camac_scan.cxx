/********************************************************************\
  Name:         camac_scan.cxx
  Created by:   Konstantin Olchanski

  Contents:     Scan CAMAC crates
\********************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "mcstd.h"

int main(int argc, char *argv[])
{
   int status;

   status = cam_init();
   if (status != 1) {
      printf("Error: Cannot initialize camac: cam_init status %d\n", status);
      return 1;
   }

   for (int c=0; c<=1; c++) {
      printf("Scan of CAMAC crate %d:\n", c);
      for (int n=1; n<=24; n++) {
         int a = 0;
         int f = 0; // read
         int x = 0;
         int q = 0;
         DWORD read_data = 0;
         cam24i_q(c, n, a, f, &read_data, &x, &q);
         if (x || q)
            printf("C=%2d, N=%2d, X=%d, Q=%d, data=0x%08x\n", c, n, x, q, read_data);
         //sleep(1);
      }
   }

   return 0;
}

// end
