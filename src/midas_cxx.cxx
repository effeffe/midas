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
   
   if (n > 0) {
      free(flist);
   }

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

/*------------------------------------------------------------------*/

int cm_exec_script(const char* odb_path_to_script)
/********************************************************************\

  Routine: cm_exec_script

  Purpose: Execute script from /Script tree

  exec_script is enabled by the tree /Script
  The /Script struct is composed of list of keys
  from which the name of the key is the button name
  and the sub-structure is a record as follow:

  /Script/<button_name> = <script command> (TID_STRING)

  The "Script command", containing possible arguements,
  is directly executed.

  /Script/<button_name>/<script command>
                        <soft link1>|<arg1>
                        <soft link2>|<arg2>
                           ...

  The arguments for the script are derived from the
  subtree below <button_name>, where <button_name> must be
  TID_KEY. The subtree may then contain arguments or links
  to other values in the ODB, like run number etc.

\********************************************************************/
{
   HNDLE hDB, hkey;
   KEY key;
   int status;

   status = cm_get_experiment_database(&hDB, NULL);
   if (status != DB_SUCCESS)
      return status;

   status = db_find_key(hDB, 0, odb_path_to_script, &hkey);
   if (status != DB_SUCCESS)
      return status;
   
   status = db_get_key(hDB, hkey, &key);
   if (status != DB_SUCCESS)
      return status;

   std::string command;

   if (key.type == TID_STRING) {
      int status = db_get_value_string(hDB, 0, odb_path_to_script, 0, &command, FALSE);
      if (status != DB_SUCCESS) {
         cm_msg(MERROR, "cm_exec_script", "Script ODB \"%s\" of type TID_STRING, db_get_value_string() error %d", odb_path_to_script, status);
         return status;
      }
   } else if (key.type == TID_KEY) {
      for (int i = 0;; i++) {
         HNDLE hsubkey;
         KEY subkey;
         db_enum_key(hDB, hkey, i, &hsubkey);
         if (!hsubkey)
            break;
         db_get_key(hDB, hsubkey, &subkey);

         if (i > 0)
            command += " ";

         if (subkey.type == TID_KEY) {
            cm_msg(MERROR, "cm_exec_script", "Script ODB \"%s/%s\" should not be TID_KEY", odb_path_to_script, subkey.name);
            return DB_TYPE_MISMATCH;
         } else {
            int size = subkey.item_size;
            char *buf = (char*)malloc(size);
            assert(buf != NULL);
            int status = db_get_data(hDB, hsubkey, buf, &size, subkey.type);
            if (status != DB_SUCCESS) {
               cm_msg(MERROR, "cm_exec_script", "Script ODB \"%s/%s\" of type %d, db_get_data() error %d", odb_path_to_script, subkey.name, subkey.type, status);
               free(buf);
               return status;
            }
            if (subkey.type == TID_STRING) {
               command += buf;
            } else {
               char str[256];
               db_sprintf(str, buf, subkey.item_size, 0, subkey.type);
               command += str;
            }
            free(buf);
         }
      }
   } else {
      cm_msg(MERROR, "cm_exec_script", "Script ODB \"%s\" has invalid type %d, should be TID_STRING or TID_KEY", odb_path_to_script, key.type);
      return DB_TYPE_MISMATCH;
   }

   // printf("exec_script: %s\n", command.c_str());

   if (command.length() > 0) {
      cm_msg(MINFO, "cm_exec_script", "Executing script \"%s\" from ODB \"%s\"", command.c_str(), odb_path_to_script);
      ss_system(command.c_str());
   }

   return SUCCESS;
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
