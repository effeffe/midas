/********************************************************************\

  Name:         midasodb.cxx
  Created by:   K.Olchanski

  Contents:     MIDAS implementation of MVOdb ODB interface

\********************************************************************/

#include <stdio.h>
#include <string.h> // strlen()
#include <assert.h>
#include <stdlib.h> // malloc()

#include "mvodb.h"
#include "midas.h"

static std::string toString(int value)
{
   char buf[256];
   sprintf(buf, "%d", value);
   return buf;
}

class MidasOdb: public MVOdb
{
public:
   HNDLE fDB;
   std::string fRoot;
   bool fTrace;
   bool fPrintError;

public:
   MidasOdb(HNDLE hDB, const char* root)
   {
      fDB = hDB;
      fRoot = root;
      fTrace = false;
      fPrintError = true;
   }

   ~MidasOdb() // dtor
   {
      // poison the data members
      fDB = 0;
      fRoot = "deleted";
      fTrace = false;
      fPrintError = false;
   }

   std::string Path(const char* varname)
   {
      std::string path;
      path += fRoot;
      path += "/";
      path += varname;
      return path;
   }

   void SetPrintError(bool v)
   {
      fPrintError = v;
   }

   bool GetPrintError() const
   {
      return fPrintError;
   }

   bool IsReadOnly() const
   {
      return false;
   }

   MVOdb* Chdir(const char* subdir, bool create, MVOdbError* error)
   {
      std::string path = Path(subdir);
      HNDLE hkey;
      int status = db_find_key(fDB, 0, path.c_str(), &hkey);
      if (status == DB_SUCCESS) {
         KEY key;
         status = db_get_key(fDB, hkey, &key);
         if (status != DB_SUCCESS) {
            SetMidasStatus(error, fPrintError, path, "db_get_key", status);
            return MakeNullOdb();
         }
         if (key.type != TID_KEY) {
            SetError(error, fPrintError, path, "ODB path is not a directory");
            if (create)
               return MakeNullOdb();
            else
               return NULL;
         }
         return new MidasOdb(fDB, path.c_str());
      } else if (!create) {
         SetMidasStatus(error, fPrintError, path, "db_find_key", status);
         return NULL;
      } else {
         status = db_create_key(fDB, 0, path.c_str(), TID_KEY);
         if (status != DB_SUCCESS) {
            SetMidasStatus(error, fPrintError, path, "db_create_key", status);
            return MakeNullOdb();
         }
         return new MidasOdb(fDB, path.c_str());
      }
   }

   void RAInfo(const char* varname, int* num_elements, int* element_size, MVOdbError* error)
   {
      std::string path = Path(varname);

      if (num_elements)
         *num_elements = 0;
      if (element_size)
         *element_size = 0;

      int status;
      HNDLE hkey;
      status = db_find_key(fDB, 0, path.c_str(), &hkey);
      if (status != DB_SUCCESS)
         return;
      
      KEY key;
      status = db_get_key(fDB, hkey, &key);
      if (status != DB_SUCCESS)
         return;

      if (num_elements)
         *num_elements = key.num_values;

      if (element_size)
         *element_size = key.item_size;
   }

   void ReadKey(const char* varname, int *tid, int *num_values, int *total_size, int *item_size, MVOdbError* error)
   {
      if (tid) *tid = 0;
      if (num_values) *num_values = 0;
      if (total_size) *total_size = 0;
      if (item_size)  *item_size = 0;

      std::string path = Path(varname);

      int status;
      HNDLE hkey;

      status = db_find_key(fDB, 0, path.c_str(), &hkey);
      if (status != DB_SUCCESS) {
         SetMidasStatus(error, fPrintError, path, "db_find_key", status);
         return;
      }
      
      KEY key;
      status = db_get_key(fDB, hkey, &key);
      if (status != DB_SUCCESS) {
         SetMidasStatus(error, fPrintError, path, "db_get_key", status);
         return;
      }

      if (tid)
         *tid = key.type;
      
      if (num_values)
         *num_values = key.num_values;
      
      if (total_size)
         *total_size = key.total_size;

      if (item_size)
         *item_size = key.item_size;

      SetOk(error);
   }

   void ReadDir(std::vector<std::string>* varname, std::vector<int> *tid, std::vector<int> *num_values, std::vector<int> *total_size, std::vector<int> *item_size, MVOdbError* error)
   {
      // FIXME: incomplete!
      SetOk(error);
   }

   void ResizeArray(const char* varname, int new_size, MVOdbError* error)
   {
      std::string path = Path(varname);

      int status;
      HNDLE hkey;
      
      status = db_find_key(fDB, 0, path.c_str(), &hkey);

      if (status != DB_SUCCESS) {
         SetMidasStatus(error, fPrintError, path, "db_find_key", status);
         return;
      }
      
      status = db_set_num_values(fDB, hkey, new_size);
      if (status != SUCCESS) {
         SetMidasStatus(error, fPrintError, path, "db_set_num_values", status);
         return;
      }

      SetOk(error);
   }

   void ResizeStringArray(const char* varname, int new_size, int new_string_length, MVOdbError* error)
   {
      std::string path = Path(varname);
      
      int status = db_resize_string(fDB, 0, path.c_str(), new_size, new_string_length);
      if (status != SUCCESS) {
         SetMidasStatus(error, fPrintError, path, "db_resize_string", status);
         return;
      }

      SetOk(error);
   }

   void R(const char* varname, int tid, void *value, int size, bool create, MVOdbError* error)
   {
      assert(value);
      std::string path = Path(varname);
      int status = db_get_value(fDB, 0, path.c_str(), value, &size, tid, create);
      if (status != DB_SUCCESS) {
         SetMidasStatus(error, fPrintError, path, "db_get_value", status);
         return;
      }
      SetOk(error);
   }

   void RI(const char* varname, int *value, bool create, MVOdbError* error)
   {
      R(varname, TID_INT, value, sizeof(int), create, error);
   }

   void RU16(const char* varname, uint16_t *value, bool create, MVOdbError* error)
   {
      R(varname, TID_WORD, value, sizeof(uint16_t), create, error);
   }

   void RU32(const char* varname, uint32_t *value, bool create, MVOdbError* error)
   {
      R(varname, TID_DWORD, value, sizeof(uint32_t), create, error);
   }

   void RD(const char* varname, double *value, bool create, MVOdbError* error)
   {
      R(varname, TID_DOUBLE, value, sizeof(double), create, error);
   }

   void RF(const char* varname, float *value, bool create, MVOdbError* error)
   {
      R(varname, TID_FLOAT, value, sizeof(float), create, error);
   }

   void RB(const char* varname, bool *value, bool create, MVOdbError* error)
   {
      assert(value);
      BOOL v = *value;
      R(varname, TID_BOOL, &v, sizeof(BOOL), create, error);
      *value = v;
   }

   void RS(const char* varname, std::string* value, bool create, int create_string_length, MVOdbError* error)
   {
      assert(value);
      std::string path = Path(varname);

#ifdef HAVE_DB_GET_VALUE_STRING_CREATE_STRING_LENGTH
      int status = db_get_value_string(fDB, 0, path.c_str(), 0, value, create, create_string_length);
#else
#warning This MIDAS has an old version of db_get_value_string() and RS() will ignore the create_string_length argument.
      int status = db_get_value_string(fDB, 0, path.c_str(), 0, value, create);
#endif

      if (status != DB_SUCCESS) {
         SetMidasStatus(error, fPrintError, path, "db_get_value_string", status);
         return;
      }

      SetOk(error);
   }

   void RAI(const char* varname, int index, int tid, void *value, int size, MVOdbError* error)
   {
      assert(value);
      std::string path = Path(varname);
      path += "[";
      path += toString(index);
      path += "]";
      if (index < 0) {
         SetError(error, fPrintError, path, "RxAI() called with negative array index");
         return;
      }
      int status = db_get_value(fDB, 0, path.c_str(), value, &size, tid, FALSE);
      if (status != DB_SUCCESS) {
         SetMidasStatus(error, fPrintError, path, "db_get_value", status);
         return;
      }
      SetOk(error);
   }

   void RIAI(const char* varname, int index, int *value, MVOdbError* error)
   {
      RAI(varname, index, TID_INT, value, sizeof(int), error);
   }

   void RU16AI(const char* varname, int index, uint16_t *value, MVOdbError* error)
   {
      RAI(varname, index, TID_WORD, value, sizeof(uint16_t), error);
   }

   void RU32AI(const char* varname, int index, uint32_t *value, MVOdbError* error)
   {
      RAI(varname, index, TID_DWORD, value, sizeof(uint32_t), error);
   }

   void RDAI(const char* varname, int index, double *value, MVOdbError* error)
   {
      RAI(varname, index, TID_DOUBLE, value, sizeof(double), error);
   }

   void RFAI(const char* varname, int index, float *value, MVOdbError* error)
   {
      RAI(varname, index, TID_FLOAT, value, sizeof(float), error);
   }

   void RBAI(const char* varname, int index, bool *value, MVOdbError* error)
   {
      assert(value);
      BOOL v = *value;
      RAI(varname, index, TID_BOOL, &v, sizeof(BOOL), error);
      *value = v;
   }

   void RSAI(const char* varname, int index, std::string* value, MVOdbError* error)
   {
      assert(value);
      std::string path = Path(varname);

      if (index < 0) {
         SetError(error, fPrintError, path, "RSAI() called with negative array index");
         return;
      }
   
      int status = db_get_value_string(fDB, 0, path.c_str(), index, value, FALSE);

      if (status != DB_SUCCESS) {
         SetMidasStatus(error, fPrintError, path, "db_get_value_string", status);
         return;
      }

      SetOk(error);
   }

   void RA(const std::string& path, int tid, void* buf, int size, MVOdbError* error)
   {
      int status = db_get_value(fDB, 0, path.c_str(), buf, &size, tid, FALSE);

      if (status != DB_SUCCESS) {
         SetMidasStatus(error, fPrintError, path, "db_get_value", status);
         return;
      }

      SetOk(error);
   }

   void GetArraySize(const char* varname, int* pnum_values, int* pitem_size, MVOdbError* error)
   {
      int xtid = 0;
      //int xnum_values = 0;
      int xtotal_size = 0;
      //int xitem_size = 0;

      ReadKey(varname, &xtid, pnum_values, &xtotal_size, pitem_size, error);

      if (xtid == TID_KEY) {
         *pnum_values = -1;
         *pitem_size = -1;
      }

      if (xtid == 0) {
         *pnum_values = -1;
         *pitem_size = -1;
      }
   }

   template <class X> void RXA(const char* varname, int tid, std::vector<X> *value, bool create, int create_size, MVOdbError* error)
   {
      std::string path = Path(varname);

      int num_values = 0;
      int item_size = 0;

      GetArraySize(varname, &num_values, &item_size, error);

      if (value == NULL) {
         if (create && create_size > 0) {
            if (num_values < 0) {
               // does not exist, create it
               X v = 0;
               W(varname, tid, &v, sizeof(X), error);
               if (error && error->fError)
                  return;
               ResizeArray(varname, create_size, error);
            } else if (num_values != create_size) {
               // wrong size, resize it
               ResizeArray(varname, create_size, error);
               return;
            }
         }
         return;
      }

      if (num_values > 0) { // array exists
         value->resize(num_values);
         RA(path, tid, &((*value)[0]), num_values*sizeof(X), error);
         return;
      }

      // array does not exist

      if (!create)
         return;
      
      WA(varname, tid, &((*value)[0]), value->size()*sizeof(X), value->size(), error);

      if (error && error->fError)
         return;

      if (create_size > 0) {
         if (create_size != (int)value->size()) {
            ResizeArray(varname, create_size, error);
         }
      }
   }

   void RIA(const char* varname, std::vector<int> *value, bool create, int create_size, MVOdbError* error)
   {
      RXA<int>(varname, TID_INT, value, create, create_size, error);
   }

   void RFA(const char* varname, std::vector<float> *value, bool create, int create_size, MVOdbError* error)
   {
      RXA<float>(varname, TID_FLOAT, value, create, create_size, error);
   }

   void RDA(const char* varname, std::vector<double> *value, bool create, int create_size, MVOdbError* error)
   {
      RXA<double>(varname, TID_DOUBLE, value, create, create_size, error);
   }

   void RU16A(const char* varname, std::vector<uint16_t> *value, bool create, int create_size, MVOdbError* error)
   {
      RXA<uint16_t>(varname, TID_WORD, value, create, create_size, error);
   }
   
   void RU32A(const char* varname, std::vector<uint32_t> *value, bool create, int create_size, MVOdbError* error)
   {
      RXA<uint32_t>(varname, TID_DWORD, value, create, create_size, error);
   }
   
   void RBA(const char* varname, std::vector<bool> *value, bool create, int create_size, MVOdbError* error)
   {
      std::vector<BOOL> xvalue;
      std::vector<BOOL> *xvalue_ptr = NULL;

      if (value) {
         for (std::size_t i=0; i<value->size(); i++) {
            if ((*value)[i])
               xvalue.push_back(TRUE);
            else
               xvalue.push_back(FALSE);
         }
         xvalue_ptr = &xvalue;
      }

      RXA<BOOL>(varname, TID_BOOL, xvalue_ptr, create, create_size, error);

      if (value) {
         for (std::size_t i=0; i<xvalue.size(); i++) {
            if (xvalue[i])
               value->push_back(true);
            else
               value->push_back(false);
         }
      }
   }

   void RSA(const char* varname, std::vector<std::string> *value, bool create, int create_size, int create_string_length, MVOdbError* error)
   {
      std::string path = Path(varname);

      int num_values = 0;
      int item_size = 0;

      GetArraySize(varname, &num_values, &item_size, error);

      if (value == NULL) {
         if (create && (create_size > 0) && (create_string_length > 0)) {
            if (num_values < 0) {
               // does not exist, create it
               WS(varname, "", create_string_length, error);
               if (error && error->fError)
                  return;
               ResizeStringArray(varname, create_size, create_string_length, error);
            } else if ((num_values != create_size) || (item_size != create_string_length)) {
               // wrong size, resize it
               ResizeStringArray(varname, create_size, create_string_length, error);
               return;
            }
         }
         return;
      }

      // array exists, read it
      
      if (num_values > 0) {
         value->clear();
         int bufsize = num_values*item_size;
         char* buf = (char*)malloc(bufsize);
         assert(buf != NULL);
         memset(buf, 0, bufsize);
         RA(path, TID_STRING, buf, bufsize, error);
         for (int i=0; i<num_values; i++) {
            value->push_back(buf+i*item_size);
         }
         free(buf);
         buf = NULL;
         return;
      }

      // array does not exist
      
      if (!create)
         return;

      //if (!(create_string_length > 0)) {
      //   SetError(error, fPrintError, path, "RSA() with create==true must have create_string_length>0");
      //   return;
      //}

      int string_length = 0;
      for (size_t i = 0; i < value->size(); i++) {
         if (((int)(*value)[i].length()) > string_length)
            string_length = (*value)[i].length();
      }
      string_length += 1; // add space for string terminator NUL character '\0'

      if (create_string_length > string_length)
         string_length = create_string_length;

      char* buf = NULL;

      int bufsize = value->size()*string_length;

      if (bufsize > 0) {
         buf = (char*)malloc(bufsize);
         assert(buf != NULL);
         memset(buf, 0, bufsize);

         for (size_t i=0; i<value->size(); i++) {
            strlcpy(buf+i*string_length, (*value)[i].c_str(), string_length);
         }
      }

      WA(varname, TID_STRING, buf, bufsize, value->size(), error);

      if (buf) {
         free(buf);
         buf = NULL;
      }

      if (error && error->fError)
         return;

      if ((create_size > 0) && (create_string_length > 0)) {
         if ((((int)value->size()) != create_size) || (string_length != create_string_length)) {
            // wrong size, resize it
            ResizeStringArray(varname, create_size, create_string_length, error);
         }
      }
   }

   void W(const char* varname, int tid, const void* v, int size, MVOdbError* error)
   {
      std::string path = Path(varname);
   
      int status = db_set_value(fDB, 0, path.c_str(), v, size, 1, tid);

      if (status != DB_SUCCESS) {
         SetMidasStatus(error, fPrintError, path, "db_set_value", status);
         return;
      }

      SetOk(error);
   }

   void WB(const char* varname, bool v, MVOdbError* error)
   {
      BOOL vv = v;
      W(varname, TID_BOOL, &vv, sizeof(BOOL), error);
   }

   void WI(const char* varname, int v, MVOdbError* error)
   {
      W(varname, TID_INT, &v, sizeof(int), error);
   }

   void WU16(const char* varname, uint16_t v, MVOdbError* error)
   {
      W(varname, TID_WORD, &v, sizeof(uint16_t), error);
   }
   
   void WU32(const char* varname, uint32_t v, MVOdbError* error)
   {
      W(varname, TID_DWORD, &v, sizeof(uint32_t), error);
   }
   
   void WD(const char* varname, double v, MVOdbError* error)
   {
      W(varname, TID_DOUBLE, &v, sizeof(double), error);
   }

   void WF(const char* varname, float v, MVOdbError* error)
   {
      W(varname, TID_FLOAT, &v, sizeof(float), error);
   }

   void WS(const char* varname, const char* v, int string_length, MVOdbError* error)
   {
      if (string_length > 0) {
         char* buf = (char*)malloc(string_length);
         assert(buf);
         strlcpy(buf, v, string_length);
         W(varname, TID_STRING, buf, string_length, error);
         free(buf);
      } else {
         int len = strlen(v);
         W(varname, TID_STRING, v, len+1, error);
      }
   }

   void WAI(const char* varname, int index, int tid, const void* v, int size, MVOdbError* error)
   {
      std::string path = Path(varname);

      if (index < 0) {
         SetError(error, fPrintError, path, "WxAI() called with negative array index");
         return;
      }

      //printf("WAI(\"%s\", [%d], %d) path [%s], size %d\n", varname, index, tid, path.c_str(), size);

      int status;
      HNDLE hkey;

      status = db_find_key(fDB, 0, path.c_str(), &hkey);
   
      if (status != DB_SUCCESS) {
         SetMidasStatus(error, fPrintError, path, "db_find_key", status);
         return;
      }

      status = db_set_data_index(fDB, hkey, v, size, index, tid);

      if (status != DB_SUCCESS) {
         SetMidasStatus(error, fPrintError, path, "db_set_value", status);
         return;
      }

      SetOk(error);
   }

   void WBAI(const char* varname, int index, bool v, MVOdbError* error)
   {
      BOOL vv = v;
      WAI(varname, index, TID_BOOL, &vv, sizeof(BOOL), error);
   }

   void WIAI(const char* varname, int index, int v, MVOdbError* error)
   {
      WAI(varname, index, TID_INT, &v, sizeof(int), error);
   }

   void WU16AI(const char* varname, int index, uint16_t v, MVOdbError* error)
   {
      WAI(varname, index, TID_WORD, &v, sizeof(uint16_t), error);
   }
   
   void WU32AI(const char* varname, int index, uint32_t v, MVOdbError* error)
   {
      WAI(varname, index, TID_DWORD, &v, sizeof(uint32_t), error);
   }
   
   void WDAI(const char* varname, int index, double v, MVOdbError* error)
   {
      WAI(varname, index, TID_DOUBLE, &v, sizeof(double), error);
   }

   void WFAI(const char* varname, int index, float v, MVOdbError* error)
   {
      WAI(varname, index, TID_FLOAT, &v, sizeof(float), error);
   }

   void WSAI(const char* varname, int index, const char* v, MVOdbError* error)
   {
      int num_elements = 0;
      int element_size = 0;
      RAInfo(varname, &num_elements, &element_size, error);
      if (error && error->fError)
         return;
      if (element_size <= 0)
         return;
      char* buf = (char*)malloc(element_size);
      assert(buf);
      strlcpy(buf, v, element_size);
      WAI(varname, index, TID_STRING, buf, element_size, error);
      free(buf);
   }

   void WA(const char* varname, int tid, const void* v, int size, int count, MVOdbError* error)
   {
      std::string path = Path(varname);

      //printf("WA(tid %d, size %d, count %d)\n", tid, size, count);

      if (size == 0) {
         int status = db_create_key(fDB, 0, path.c_str(), tid);

         if (status != DB_SUCCESS) {
            SetMidasStatus(error, fPrintError, path, "db_create_key", status);
            return;
         }
      } else {
         int status = db_set_value(fDB, 0, path.c_str(), v, size, count, tid);

         //printf("WA db_set_value(tid %d, size %d, count %d) status %d\n", tid, size, count, status);
         
         if (status != DB_SUCCESS) {
            SetMidasStatus(error, fPrintError, path, "db_set_value", status);
            return;
         }
      }

      SetOk(error);
   }

   void WBA(const char* varname, const std::vector<bool>& v, MVOdbError* error)
   {
      unsigned num = v.size();
      BOOL val[num];
      
      for (unsigned i=0; i<num; i++) {
         val[i] = v[i];
      }
      
      WA(varname, TID_BOOL, val, num*sizeof(BOOL), num, error);
   }
   
   void WU16A(const char* varname, const std::vector<uint16_t>& v, MVOdbError* error)
   {
      WA(varname, TID_WORD, &v[0], v.size()*sizeof(uint16_t), v.size(), error);
   }
   
   void WU32A(const char* varname, const std::vector<uint32_t>& v, MVOdbError* error)
   {
      WA(varname, TID_DWORD, &v[0], v.size()*sizeof(uint32_t), v.size(), error);
   }
   
   void WIA(const char* varname, const std::vector<int>& v, MVOdbError* error)
   {
      WA(varname, TID_INT, &v[0], v.size()*sizeof(int), v.size(), error);
   }

   void WFA(const char* varname, const std::vector<float>& v, MVOdbError* error)
   {
      WA(varname, TID_FLOAT, &v[0], v.size()*sizeof(float), v.size(), error);
   }

   void WDA(const char* varname, const std::vector<double>& v, MVOdbError* error)
   {
      WA(varname, TID_DOUBLE, &v[0], v.size()*sizeof(double), v.size(), error);
   }

   void WSA(const char* varname, const std::vector<std::string>& v, int odb_string_size, MVOdbError* error)
   {
      unsigned num = v.size();
      unsigned length = odb_string_size;

      if (length == 0) {
         for (unsigned i=0; i<v.size(); i++) {
            if (v[i].length() > length)
               length = v[i].length();
         }
         length += 1; // for the string terminator NUL character
      }

      char val[length*num];
      memset(val, 0, length*num);
      
      for (unsigned i=0; i<num; i++)
         strlcpy(val+length*i, v[i].c_str(), length);
      
      WA(varname, TID_STRING, val, num*length, num, error);
   }

   void Delete(const char* odbname, MVOdbError* error)
   {
      std::string path = Path(odbname);

      //printf("Delete(%s)\n", path.c_str());

      HNDLE hKey;
      int status = db_find_key(fDB, 0, path.c_str(), &hKey);

      if (status == DB_NO_KEY) {
         SetOk(error);
         return;
      }

      if (status != DB_SUCCESS) {
         SetMidasStatus(error, fPrintError, path, "db_find_key", status);
         return;
      }

      status = db_delete_key(fDB, hKey, FALSE);

      if (status != DB_SUCCESS) {
         SetMidasStatus(error, fPrintError, path, "db_delete_key", status);
         return;
      }

      SetOk(error);
   };
};

MVOdb* MakeMidasOdb(int hDB, MVOdbError* error)
{
   SetOk(error);
   return new MidasOdb(hDB, "");
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
