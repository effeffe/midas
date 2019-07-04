/********************************************************************\

  Name:         nullodb.cxx
  Created by:   K.Olchanski

  Contents:     NULL implementation of MVOdb ODB interface

  Discussion:   Writing to NULL ODB has no effect, reading from NULL ODB leaves the default values unchanged

\********************************************************************/

//#include <stdio.h>
//#include <string.h> // strlen()
//#include <assert.h>
//#include <stdlib.h> // malloc()

#include "mvodb.h"

class NullOdb: public MVOdb
{
public:
   bool fPrintError;

   NullOdb() // ctor
   {
      fPrintError = true;
   }

   void SetPrintError(bool v)
   {
      fPrintError = true;
   }

   bool GetPrintError() const
   {
      return fPrintError;
   }

   bool IsReadOnly() const
   {
      return true;
   }
   
   MVOdb* Chdir(const char* subdir, bool create, MVOdbError* error)
   {
      SetOk(error);
      if (create)
         return new NullOdb;
      else
         return NULL;
   }

   //void RAInfo(const char* varname, int* num_elements, int* element_size, MVOdbError* error)
   //{
   //   SetOk(error);
   //};

   void ReadKey(const char* varname, int *tid, int *num_values, int *total_size, int *item_size, MVOdbError* error)
   {
      if (tid) *tid = 0;
      if (num_values) *num_values = 0;
      if (total_size) *total_size = 0;
      if (item_size)  *item_size = 0;
      SetOk(error);
   }

   void ReadDir(std::vector<std::string>* varname, std::vector<int> *tid, std::vector<int> *num_values, std::vector<int> *total_size, std::vector<int> *item_size, MVOdbError* error)
   {
      SetOk(error);
   }

   void RB(const char* varname, bool   *value, bool create, MVOdbError* error) { SetOk(error); };
   void RI(const char* varname, int    *value, bool create, MVOdbError* error) { SetOk(error); };
   void RD(const char* varname, double *value, bool create, MVOdbError* error) { SetOk(error); };
   void RF(const char* varname, float  *value, bool create, MVOdbError* error) { SetOk(error); };
   void RS(const char* varname, std::string *value, bool create, int create_string_length, MVOdbError* error) { SetOk(error); };
   void RU16(const char* varname, uint16_t *value, bool create, MVOdbError* error) { SetOk(error); };
   void RU32(const char* varname, uint32_t *value, bool create, MVOdbError* error) { SetOk(error); };

   void RBA(const char* varname, std::vector<bool> *value, bool create, int create_size, MVOdbError* error) { SetOk(error); };
   void RIA(const char* varname, std::vector<int> *value,  bool create, int create_size, MVOdbError* error) { SetOk(error); };
   void RDA(const char* varname, std::vector<double> *value,  bool create, int create_size, MVOdbError* error) { SetOk(error); };
   void RFA(const char* varname, std::vector<float>  *value,  bool create, int create_size, MVOdbError* error) { SetOk(error); };
   void RSA(const char* varname, std::vector<std::string> *value, bool create, int create_size, int create_string_length, MVOdbError* error) { SetOk(error); };
   void RU16A(const char* varname, std::vector<uint16_t> *value,  bool create, int create_size, MVOdbError* error) { SetOk(error); };
   void RU32A(const char* varname, std::vector<uint32_t> *value,  bool create, int create_size, MVOdbError* error) { SetOk(error); };

   void RBAI(const char* varname, int index, bool   *value, MVOdbError* error) { SetOk(error); };
   void RIAI(const char* varname, int index, int    *value, MVOdbError* error) { SetOk(error); };
   void RDAI(const char* varname, int index, double *value, MVOdbError* error) { SetOk(error); };
   void RFAI(const char* varname, int index, float  *value, MVOdbError* error) { SetOk(error); };
   void RSAI(const char* varname, int index, std::string *value, MVOdbError* error) { SetOk(error); };
   void RU16AI(const char* varname, int index, uint16_t *value, MVOdbError* error) { SetOk(error); };
   void RU32AI(const char* varname, int index, uint32_t *value, MVOdbError* error) { SetOk(error); };

   void WB(const char* varname, bool v,   MVOdbError* error) { SetOk(error); };
   void WI(const char* varname, int v,    MVOdbError* error)  { SetOk(error); };
   void WD(const char* varname, double v, MVOdbError* error) { SetOk(error); };
   void WF(const char* varname, float  v, MVOdbError* error) { SetOk(error); };
   void WS(const char* varname, const char* v, int string_length, MVOdbError* error) { SetOk(error); };
   void WU16(const char* varname, uint16_t v, MVOdbError* error) { SetOk(error); };
   void WU32(const char* varname, uint32_t v, MVOdbError* error) { SetOk(error); };

   void WBA(const char* varname, const std::vector<bool>& v, MVOdbError* error) { SetOk(error); };
   void WIA(const char* varname, const std::vector<int>& v, MVOdbError* error) { SetOk(error); };
   void WDA(const char* varname, const std::vector<double>& v, MVOdbError* error) { SetOk(error); };
   void WFA(const char* varname, const std::vector<float>& v, MVOdbError* error) { SetOk(error); };
   void WSA(const char* varname, const std::vector<std::string>& data, int odb_string_length, MVOdbError* error) { SetOk(error); };
   void WU16A(const char* varname, const std::vector<uint16_t>& v, MVOdbError* error) { SetOk(error); };
   void WU32A(const char* varname, const std::vector<uint32_t>& v, MVOdbError* error) { SetOk(error); };

   void WBAI(const char* varname, int index, bool v,   MVOdbError* error) { SetOk(error); };
   void WIAI(const char* varname, int index, int v,    MVOdbError* error)  { SetOk(error); };
   void WDAI(const char* varname, int index, double v, MVOdbError* error) { SetOk(error); };
   void WFAI(const char* varname, int index, float  v, MVOdbError* error) { SetOk(error); };
   void WSAI(const char* varname, int index, const char* v, MVOdbError* error) { SetOk(error); };
   void WU16AI(const char* varname, int index, uint16_t v, MVOdbError* error) { SetOk(error); };
   void WU32AI(const char* varname, int index, uint32_t v, MVOdbError* error) { SetOk(error); };

   void Delete(const char* odbname, MVOdbError* error) { SetOk(error); };
};

MVOdb* MakeNullOdb()
{
   return new NullOdb();
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
