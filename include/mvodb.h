/********************************************************************\

  Name:         mvodb.h
  Created by:   K.Olchanski

  Contents:     Virtual ODB interface

\********************************************************************/

#ifndef INCLUDE_MVODB_H
#define INCLUDE_MVODB_H

#include <string>
#include <vector>
//#include <cstdio>
#include <stdint.h>

class MVOdbError;

class MVOdb
{
public:
   // check if this ODB interface allows writing
   virtual bool IsReadOnly() const = 0;
   
   // navigate into subdirectory.
   //
   // returns NULL is subdirname does not exist and "create" is false.
   // returns NULL is subdirname is not a subdirectory and "create" is false
   // never returns NULL if "create" is true - returns a subdirectory or NullOdb if there was an error
   
   virtual MVOdb* Chdir(const char* subdirname, bool create = false, MVOdbError* error = NULL) = 0;
   
   // read array information: number of elements and element size (string size for TID_STRING arrays)

   virtual void ReadKey(const char* varname, int *tid, int *num_values, int *total_size, int *item_size, MVOdbError* error = NULL) = 0;

   // read the contents of current directory

   virtual void ReadDir(std::vector<std::string>* varname, std::vector<int> *tid, std::vector<int> *num_values, std::vector<int> *total_size, std::vector<int> *item_size, MVOdbError* error = NULL) = 0;

   //
   // create and read individual odb variables
   //
   // all Rx() read functions do this:
   //
   // if varname exists, it's value read from odb and returned
   // if odb read fails (wrong data type, etc), value is left unchanged (but see db_get_value)
   // if varname does not exist and create is false, value is returned unchanged
   // if create is true, varname is created in odb with given value and given string length
   //
   // int a = 10; // default value
   // odb->RI("a", &a); // read from odb, keep default value if does not exist
   // odb->RI("a", &a, true); // read from odb, create with default value if does not exist
   //

   virtual void RB(const char* varname, bool   *value, bool create = false, MVOdbError* error = NULL) = 0; // TID_BOOL
   virtual void RI(const char* varname, int    *value, bool create = false, MVOdbError* error = NULL) = 0; // TID_INT
   virtual void RD(const char* varname, double *value, bool create = false, MVOdbError* error = NULL) = 0; // TID_DOUBLE
   virtual void RF(const char* varname, float  *value, bool create = false, MVOdbError* error = NULL) = 0; // TID_FLOAT
   virtual void RS(const char* varname, std::string *value, bool create = false, int create_string_length = 0, MVOdbError* error = NULL) = 0; // TID_STRING
   virtual void RU16(const char* varname, uint16_t *value, bool create = false, MVOdbError* error = NULL) = 0; // TID_WORD
   virtual void RU32(const char* varname, uint32_t *value, bool create = false, MVOdbError* error = NULL) = 0; // TID_DWORD

   //
   // read or create odb arrays
   //
   // all RxA() read functions do this:
   //
   // if array varname exists, it's contents is read into the "value" vector, size of "value" vector is same as the odb array size.
   // if odb read fails (wrong data type, etc), value is left unchanged (but see db_get_value)
   // if varname does not exist and create is false, value is returned unchanged
   // if create is true, a new array is created and filled with data from the "value" vector.
   // if "create_size" is non-zero, the newly created array size is resized to "create_size"
   //
   // std::vector<int> a;
   // odb->RIA("a", &a);
   // odb->RIA("a", &a, true);
   // odb->RIA("a", &a, true, 10);
   //
   // in addition, the RxA functions provide a way to ensure that arrays in odb have the correct (expected) size:
   //
   // if "value" is NULL, and "create" is true, and "create_size" is not zero,
   // if array varname exists, it's size is changed to "create_size"
   // if array varname does not exist, it is created with size "create_size":
   //
   // odb->RxA(varname, NULL, true, array_size); // create new array with size "array_size"
   //

   virtual void RBA(const char* varname, std::vector<bool>   *value, bool create = false, int create_size = 0, MVOdbError* error = NULL) = 0;
   virtual void RIA(const char* varname, std::vector<int>    *value, bool create = false, int create_size = 0, MVOdbError* error = NULL) = 0;
   virtual void RDA(const char* varname, std::vector<double> *value, bool create = false, int create_size = 0, MVOdbError* error = NULL) = 0;
   virtual void RFA(const char* varname, std::vector<float>  *value, bool create = false, int create_size = 0, MVOdbError* error = NULL) = 0;
   virtual void RSA(const char* varname, std::vector<std::string> *value, bool create = false, int create_size = 0, int create_string_length = 0, MVOdbError* error = NULL) = 0;
   virtual void RU16A(const char* varname, std::vector<uint16_t> *value, bool create = false, int create_size = 0, MVOdbError* error = NULL) = 0;
   virtual void RU32A(const char* varname, std::vector<uint32_t> *value, bool create = false, int create_size = 0, MVOdbError* error = NULL) = 0;

   //
   // read odb array elements
   //
   // all RxAI() read functions do this:
   //
   // if varname exists, it's value read from odb and returned
   // if odb read fails (index out of range, wrong data type, etc), value is left unchanged (but see db_get_value)
   // if varname does not exist, value is returned unchanged
   //
   // the RxAI() cannot create arrays, use RxA(varname, NULL, true, array_size) to ensure that arrays exist and have the correct (expected) size.
   //
   // instead of looping over all array elements, use RxA(varname, &v) to read the whole array at once.
   //

   virtual void RBAI(const char* varname, int index, bool   *value, MVOdbError* error = NULL) = 0; // TID_BOOL
   virtual void RIAI(const char* varname, int index, int    *value, MVOdbError* error = NULL) = 0; // TID_INT
   virtual void RDAI(const char* varname, int index, double *value, MVOdbError* error = NULL) = 0; // TID_DOUBLE
   virtual void RFAI(const char* varname, int index, float  *value, MVOdbError* error = NULL) = 0; // TID_FLOAT
   virtual void RSAI(const char* varname, int index, std::string *value, MVOdbError* error = NULL) = 0; // TID_STRING
   virtual void RU16AI(const char* varname, int index, uint16_t *value, MVOdbError* error = NULL) = 0; // TID_WORD
   virtual void RU32AI(const char* varname, int index, uint32_t *value, MVOdbError* error = NULL) = 0; // TID_DWORD

   // create and write individual variables

   virtual void WB(const char* varname, bool   v, MVOdbError* error = NULL) = 0;
   virtual void WI(const char* varname, int    v, MVOdbError* error = NULL) = 0;
   virtual void WD(const char* varname, double v, MVOdbError* error = NULL) = 0;
   virtual void WF(const char* varname, float  v, MVOdbError* error = NULL) = 0;
   virtual void WS(const char* varname, const char* v, int string_length = 0, MVOdbError* error = NULL) = 0;
   virtual void WU16(const char* varname, uint16_t v, MVOdbError* error = NULL) = 0;
   virtual void WU32(const char* varname, uint32_t v, MVOdbError* error = NULL) = 0;

   // create and write whole arrays
   //
   // the WSA() function for writing string arrays requires string length argument
   // because ODB string arrays have fixed element length and it must be specified
   // at array creation (write) time
   //

   virtual void WBA(const char* varname, const std::vector<bool>&   v, MVOdbError* error = NULL) = 0;
   virtual void WIA(const char* varname, const std::vector<int>&    v, MVOdbError* error = NULL) = 0;
   virtual void WDA(const char* varname, const std::vector<double>& v, MVOdbError* error = NULL) = 0;
   virtual void WFA(const char* varname, const std::vector<float>&  v, MVOdbError* error = NULL) = 0;
   virtual void WSA(const char* varname, const std::vector<std::string>& v, int string_length, MVOdbError* error = NULL) = 0;
   virtual void WU16A(const char* varname, const std::vector<uint16_t>& v, MVOdbError* error = NULL) = 0;
   virtual void WU32A(const char* varname, const std::vector<uint32_t>& v, MVOdbError* error = NULL) = 0;

   // write array elements
   //
   // writing beyound the end of an existing array will grow the array
   //

   virtual void WBAI(const char* varname, int index, bool   v, MVOdbError* error = NULL) = 0;
   virtual void WIAI(const char* varname, int index, int    v, MVOdbError* error = NULL) = 0;
   virtual void WDAI(const char* varname, int index, double v, MVOdbError* error = NULL) = 0;
   virtual void WFAI(const char* varname, int index, float  v, MVOdbError* error = NULL) = 0;
   virtual void WSAI(const char* varname, int index, const char* v, MVOdbError* error = NULL) = 0;
   virtual void WU16AI(const char* varname, int index, uint16_t v, MVOdbError* error = NULL) = 0;
   virtual void WU32AI(const char* varname, int index, uint32_t v, MVOdbError* error = NULL) = 0;

   // delete odb entries from the current directory
   
   virtual void Delete(const char* odbname, MVOdbError* error = NULL) = 0;

   // report errors to stderr or not
   virtual void SetPrintError(bool v) = 0;
   virtual bool GetPrintError() const = 0;
};

MVOdb* MakeNullOdb();
MVOdb* MakeMidasOdb(int hDB, MVOdbError* error = NULL);

MVOdb* MakeXmlFileOdb(const char* filename, MVOdbError* error = NULL);
MVOdb* MakeXmlBufferOdb(const char* buf, int bufsize, MVOdbError* error = NULL);

MVOdb* MakeJsonFileOdb(const char* filename, MVOdbError* error = NULL);
MVOdb* MakeJsonBufferOdb(const char* buf, int bufsize, MVOdbError* error = NULL);
//MVOdb* MakeJsonRpcOdb(???);

/// Access ODB from a midas file dump. FOrmat could be .xml, .json or .odb
MVOdb* MakeFileDumpOdb(const char* buf, int bufsize, MVOdbError* error = NULL);

class MVOdbError
{
 public:
   bool fError; // true if there is an error, false if no error
   std::string fErrorString; // error text suitable for printing an error message
   std::string fPath; // odb path corresponding to the error
   int fStatus; // MIDAS ODB status numerical value

 public:
   MVOdbError();
};

void SetOk(MVOdbError* error);
void SetError(MVOdbError* error, bool print, const std::string& path, const std::string& message);
void SetMidasStatus(MVOdbError* error, bool print, const std::string& path, const char* midas_func_name, int status);

#endif

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
