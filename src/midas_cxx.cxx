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

/* C++ wrapper for cm_get_path */

INT EXPRT cm_get_path_string(std::string* path)
{
   assert(path != NULL);
   char buf[MAX_STRING_LENGTH]; // should match size of _path_name in midas.c
   cm_get_path(buf, sizeof(buf));
   *path = buf;
   return CM_SUCCESS;
}

/* C++ wrapper for db_get_value */

INT EXPRT db_get_value_string(HNDLE hdb, HNDLE hKeyRoot, const char *key_name, int index, std::string* s, BOOL create)
{
   int status;
   int hkey;

   //printf("db_get_value_string: key_name [%s], index %d, string [%s], create %d\n", key_name, index, s->c_str(), create);

   status = db_find_key(hdb, hKeyRoot, key_name, &hkey);
   if (status == DB_SUCCESS) {
      KEY key;
      status = db_get_key(hdb, hkey, &key);
      if (status != DB_SUCCESS)
         return status;
      if (index < 0 || index >= key.num_values) {
         return DB_OUT_OF_RANGE;
      }
      int size = key.item_size;
      if (size == 0) {
         if (s)
            *s = "";
         //printf("db_get_value_string: return empty string, item_size %d\n", key.item_size);
         return DB_SUCCESS;
      }
      char* buf = (char*)malloc(size);
      assert(buf != NULL);
      status = db_get_data_index(hdb, hkey, buf, &size, index, TID_STRING);
      if (status != DB_SUCCESS) {
         free(buf);
         return status;
      }
      if (s)
         *s = buf;
      free(buf);
      //printf("db_get_value_string: return [%s], len %d, item_size %d, size %d\n", s->c_str(), s->length(), key.item_size, size);
      return DB_SUCCESS;
   } else if (!create) {
      // does not exist and not asked to create it
      return status;
   } else {
      //printf("db_get_value_string: creating [%s]\n", key_name);
      status = db_create_key(hdb, hKeyRoot, key_name, TID_STRING);
      if (status != DB_SUCCESS)
         return status;
      status = db_find_key(hdb, hKeyRoot, key_name, &hkey);
      if (status != DB_SUCCESS)
         return status;
      assert(s != NULL);
      int size = s->length() + 1; // 1 byte for final \0
      status = db_set_data_index(hdb, hkey, s->c_str(), size, index, TID_STRING);
      if (status != DB_SUCCESS)
         return status;
      //printf("db_get_value_string: created with value [%s]\n", s->c_str());
      return DB_SUCCESS;
   }
   // NOT REACHED
}

/* C++ wrapper for db_set_value */

INT EXPRT db_set_value_string(HNDLE hDB, HNDLE hKeyRoot, const char *key_name, const std::string* s)
{
   assert(s != NULL);
   int size = s->length() + 1; // 1 byte for final \0
   //printf("db_set_value_string: key_name [%s], string [%s], size %d\n", key_name, s->c_str(), size);
   return db_set_value(hDB, hKeyRoot, key_name, s->c_str(), size, 1, TID_STRING);
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
