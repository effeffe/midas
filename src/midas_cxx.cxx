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

/* C++ wrapper for db_get_value */

INT EXPRT db_get_value_string(HNDLE hdb, HNDLE hKeyRoot, const char *key_name, int index, std::string* s, BOOL create)
{
   int status;
   int hkey;

   //printf("db_get_value_string: key_name [%s], index %d, string [%s], create %d\n", key_name, index, s->c_str(), create);

   if (index > 0 && create) {
      cm_msg(MERROR, "db_get_value_string", "cannot resize odb string arrays, please use db_resize_string() instead");
      return DB_OUT_OF_RANGE;
   }

   status = db_find_key(hdb, hKeyRoot, key_name, &hkey);
   if (status == DB_SUCCESS) {
      KEY key;
      status = db_get_key(hdb, hkey, &key);
      if (status != DB_SUCCESS)
         return status;
      if (index < 0 || index >= key.num_values)
         return DB_OUT_OF_RANGE;
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
      status = db_set_data(hdb, hkey, s->c_str(), size, 1, TID_STRING);
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

/********************************************************************/
/**
Change size of string arrays.

This function can change the number of elements and the string element length of an array of strings.
@param hDB  ODB handle obtained via cm_get_experiment_database().
@param hKey Handle for key where search starts, zero for root.
@param key_name Odb key name, if NULL, will resize ODB entry pointed to by hKey
@param num_values New number of array elements, if 0, remains unchanged
@param max_string_length New max string length for array elements, if 0, remains unchanged
@return DB_SUCCESS, or error from db_find_key, db_create_key, db_get_data(), db_set_data()
*/
INT EXPRT db_resize_string(HNDLE hdb, HNDLE hKeyRoot, const char *key_name, int num_values, int max_string_length)
{
   int status;
   int hkey;

   //printf("db_resize_string: key_name [%s], num_values %d, max_string_length %d\n", key_name, num_values, max_string_length);

   int old_num_values = 0;
   int old_item_size = 0;
   int old_size = 0;
   char* old_data = NULL;

   if (key_name) {
      status = db_find_key(hdb, hKeyRoot, key_name, &hkey);
   } else {
      hkey = hKeyRoot;
      status = DB_SUCCESS;
   }
   if (status == DB_SUCCESS) {
      KEY key;
      status = db_get_key(hdb, hkey, &key);
      if (status != DB_SUCCESS)
         return status;
      old_num_values = key.num_values;
      old_item_size = key.item_size;
      old_size = old_num_values * old_item_size;
      old_data = (char*)malloc(old_size);
      assert(old_data != NULL);
      int size = old_size;
      status = db_get_data(hdb, hkey, old_data, &size, TID_STRING);
      if (status != DB_SUCCESS) {
         free(old_data);
         return status;
      }
      assert(size == old_size);
   } else {
      status = db_create_key(hdb, hKeyRoot, key_name, TID_STRING);
      if (status != DB_SUCCESS)
         return status;
      status = db_find_key(hdb, hKeyRoot, key_name, &hkey);
      if (status != DB_SUCCESS)
         return status;
   }

   //printf("old_num_values %d, old_item_size %d, old_size %d\n", old_num_values, old_item_size, old_size);

   int item_size = max_string_length;

   if (item_size < 1)
      item_size = old_item_size;

   if (num_values < 1)
      num_values = old_num_values;

   int new_size = num_values * item_size;
   char* new_data = (char*)malloc(new_size);
   assert(new_data);

   memset(new_data, 0, new_size);

   for (int i=0; i<old_num_values; i++) {
      const char* old_ptr = old_data + i*old_item_size;
      char* new_ptr = new_data + i*item_size;
      strlcpy(new_ptr, old_ptr, item_size);
   }

   status = db_set_data(hdb, hkey, new_data, new_size, num_values, TID_STRING);

   if (old_data)
      free(old_data);
   if (new_data)
      free(new_data);
   
   return status;
}

/*------------------------------------------------------------------*/
std::string strcomb1(const char **list)
/* convert list of strings into single string to be used by db_paste() */
{
   std::string s;

   for (int i = 0; list[i]; i++) {
      s += list[i];
      s += "\n";
   }

   return s;
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
