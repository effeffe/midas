/********************************************************************\
  Name:         camac_init.cxx
  Created by:   Konstantin Olchanski

  Contents:     Test initialize camac interface
\********************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "mcstd.h"

int main(int argc, char *argv[])
{
   int status;

   status = cam_init();
   if (status != 1) {
      printf("Error: Cannot initialize camac: cam_init status %d\n", status);
      return 1;
   }

   printf("cam_init() status %d\n", status);

   return 0;
}

// end
