/********************************************************************\

  Name:         MIDAS_CXX.CXX
  Created by:   Stefan Ritt

  Contents:     MIDAS main library funcitons

  $Id$

\********************************************************************/

#include "midas.h"
#include "msystem.h"
#include <assert.h>
#include <signal.h>

#ifndef HAVE_STRLCPY
#include "strlcpy.h"
#endif

/********************************************************************/
/**
 Retrieve list of message facilities by searching logfiles on disk
 @param  list             List of facilities
 @return status           SUCCESS
 */

INT EXPRT cm_msg_facilities(STRING_LIST *list)
{
   char path[256], *flist;
   
   cm_msg_get_logfile("midas", 0, path, sizeof(path), NULL, 0);
   
   if (strrchr(path, DIR_SEPARATOR))
      *strrchr(path, DIR_SEPARATOR) = 0;
   else
      path[0] = 0;
   
   int n = ss_file_find(path, "*.log", &flist);

   for (int i=0 ; i<n ; i++) {
      char* p = flist+i*MAX_STRING_LENGTH;
      if (strchr(p, '_') == NULL && !(p[0] >= '0' && p[0] <= '9')) {
         char *s = strchr(p, '.');
         if (s)
            *s = 0;
         list->push_back(p);
      }
   }
   free(flist);
   
   return SUCCESS;
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
