/********************************************************************\

  Name:         split.cxx
  Created by:   Stefan Ritt

  Contents:     Simple program to read a .mid file and split it to
                one or several files according to the events

\********************************************************************/

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "midas.h"
#include "msystem.h"

/*------------------------------------------------------------------*/

unsigned char eb[100 * 1024 * 1024];

int main(int argc, char *argv[])
{
   if (argc != 2) {
      printf("Usage: split <filename.mid>\n");
      return 1;
   }

   int fh = open(argv[1], O_RDONLY | O_BINARY);
   if (fh < 0) {
      printf("Error: Cannot open file \"%s\"\n", argv[1]);
      return 1;
   }

   char str[80];
   strcpy(str, argv[1]);
   str[0] = 'x';
   str[1] = 'y';
   str[2] = 'y';
   int fho = open(str, O_WRONLY | O_CREAT | O_TRUNC);
   if (fho < 0) {
      printf("Error: Cannot open output file \"%s\"\n", str);
      return 1;
   }

   EVENT_HEADER eh;
   do {
      int i = read(fh, &eh, sizeof(eh));
      if (i < sizeof(eh))
         break; // end-of/file

      if ((unsigned short) eh.event_id == 0x8000) {
         // BOR event -> write to output file
         printf("Found run number %d, copied BOR event\n", eh.serial_number);
         read(fh, eb, eh.data_size);

         write(fho, &eh, sizeof(eh));   // copy event header
         write(fho, eb,  eh.data_size); // copy event

      } else if (eh.event_id == 1) {
         // event ID 1 -> write to output file
         read(fh, eb, eh.data_size);

         write(fho, &eh, sizeof(eh));   // copy event header
         write(fho, eb,  eh.data_size); // copy event

         printf("Copied event ID1 serial #%d\n", eh.serial_number);

      } else {
         // all other events -> just skip
         read(fh, eb, eh.data_size);

         printf("Skipped event ID %d\n", eh.event_id);
      }

   } while (true);

   close(fh);
   close(fho);
}
