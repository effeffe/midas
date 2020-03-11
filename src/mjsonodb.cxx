//
// ALPHA ROOT analyzer
//
// Access to ODB stored in JSON odb save file or ODB JSON dump in MIDAS data file.
//
// Name: mjsonodb.cxx
// Author: K.Olchanski, 28-May-2019
//

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h> // memset()
#include <errno.h> // errno

#include "mvodb.h"
#include "mjson.h"

static std::string toString(int i)
{
   char buf[256];
   sprintf(buf, "%d", i);
   return buf;
}

/// Access to ODB saved in JSON format inside midas .mid files

class JsonOdb : public MVOdb
{
public:
   MJsonNode* fRoot; // root of JSON document, NULL if we are a subdirectory
   MJsonNode* fDir;  // current ODB directory
   std::string fPath; // path to correct ODB directory
   bool fPrintError;

public:
   JsonOdb(MJsonNode* root, MJsonNode* dir, MVOdbError* error) // ctor
   {
      fPrintError = false;
      fRoot = root;
      fDir  = dir;
      fPath = "";
      SetOk(error);
   }

   ~JsonOdb() // dtor
   {
      if (fRoot) {
         delete fRoot;
         fRoot = NULL;
      }
      fDir = NULL;
   }

public:
   void SetPrintError(bool v)
   {
      fPrintError = true;
   }

   bool GetPrintError() const
   {
      return fPrintError;
   }

   void SetNotFound(MVOdbError* error, const char* varname)
   {
      std::string msg;
      msg += "Cannot find ";
      msg += "\"";
      msg += varname;
      msg += "\"";
      SetError(error, fPrintError, fPath, msg);
   }

   void SetVarError(MVOdbError* error, const char* varname, std::string msg)
   {
      std::string path;
      path += fPath;
      path += "/";
      path += varname;
      SetError(error, fPrintError, path, msg);
   }

   void SetWrongType(MVOdbError* error, const char* varname, const MJsonNode* node, const char* wanted_type)
   {
      std::string path;
      path += fPath;
      path += "/";
      path += varname;
      std::string msg;
      msg += "JSON node type mismatch: cannot convert node type ";
      msg += MJsonNode::TypeToString(node->GetType());
      msg += " to c++ type ";
      msg += "\"";
      msg += wanted_type;
      msg += "\"";
      SetError(error, fPrintError, path, msg);
   }

   bool IsReadOnly() const
   {
      return true;
   }

   template <typename T>
   bool GetJsonValue(const char* varname, const MJsonNode* node, T* value, MVOdbError *error);

   /// Follow the ODB path through the JSON tree
   static MJsonNode* FindPath(MJsonNode* dir, const char* path)
   {
      assert(dir);
      
      while (1) {
         // skip leading slashes
         while (*path == '/')
            path++;
         
         if (*path == 0)
            return dir;
         
         std::string elem;
         
         // copy the next path element into "elem"-
         // copy "path" until we hit "/" or end of string
         while (1) {
            if (*path==0 || *path=='/')
               break;
            elem += *path++;
         }
      
         //printf("looking for \"%s\" more \"%s\"\n", elem.c_str(), path);

         MJsonNode* found = NULL;

         const MJsonStringVector* s = dir->GetObjectNames();
         const MJsonNodeVector*   n = dir->GetObjectNodes();
         assert(s->size() == n->size());

         for (unsigned i=0; i<s->size(); i++) {
            if (strcasecmp(elem.c_str(), (*s)[i].c_str()) == 0) {
               if (dir->GetType() == MJSON_OBJECT) {
                  // found the right subdirectory, descend into it
                  found = (*n)[i];
                  break;
               } else {
                  return (*n)[i];
               }
            }
         }

         if (!found)
            return NULL;
         dir = found;
      }
   }
   
   MVOdb* Chdir(const char* subdir, bool create, MVOdbError* error)
   {
      MJsonNode* node = FindPath(fDir, subdir);
      if (!node) {
         SetNotFound(error, subdir);
         if (create) {
            return MakeNullOdb();
         } else {
            return NULL;
         }
      }

      if (node->GetType() != MJSON_OBJECT) {
         std::string msg;
         msg += "\"";
         msg += subdir;
         msg += "\"";
         msg += " JSON node is ";
         msg += "\"";
         msg += MJsonNode::TypeToString(node->GetType());
         msg += "\"";
         msg += " instead of subdirectory";
         SetError(error, fPrintError, fPath, msg);
         if (create)
            return MakeNullOdb();
         else
            return NULL;
      }

      //printf("Found subdir [%s]\n", subdir);
      //DumpTree(node);

      JsonOdb* x = new JsonOdb(NULL, node, error);
      x->fPath = fPath + "/" + subdir;
      
      SetOk(error);
      return x;
   }

   void ReadKey(const char* varname, int *tid, int *num_values, int *total_size, int *item_size, MVOdbError* error)
   {
      if (tid) *tid = 0;
      if (num_values) *num_values = 0;
      if (total_size) *total_size = 0;
      if (item_size)  *item_size = 0;
      // FIXME: not implemented
      SetOk(error);
   }

   void ReadDir(std::vector<std::string>* varname, std::vector<int> *tid, std::vector<int> *num_values, std::vector<int> *total_size, std::vector<int> *item_size, MVOdbError* error)
   {
      // FIXME: not implemented
      SetOk(error);
   }

   void RB(const char* varname, bool   *value, bool create, MVOdbError* error)
   {
      RBAI(varname, 0, value, error);
   };

   void RI(const char* varname, int    *value, bool create, MVOdbError* error)
   {
      RIAI(varname, 0, value, error);
   };

   void RD(const char* varname, double *value, bool create, MVOdbError* error)
   {
      RDAI(varname, 0, value, error);
   };
      
   void RF(const char* varname, float  *value, bool create, MVOdbError* error)
   {
      RFAI(varname, 0, value, error);
   };
      
   void RS(const char* varname, std::string *value, bool create, int create_string_length, MVOdbError* error)
   {
      RSAI(varname, 0, value, error);
   };

   void RU16(const char* varname, uint16_t *value, bool create, MVOdbError* error)
   {
      RU16AI(varname, 0, value, error);
   };

   void RU32(const char* varname, uint32_t *value, bool create, MVOdbError* error)
   {
      RU32AI(varname, 0, value, error);
   };

   template <typename T>
   void RXA(const char* varname, std::vector<T> *value, MVOdbError* error)
   {
      if (!value) {
         SetOk(error);
         return;
      }

      MJsonNode* node = FindPath(fDir, varname);
      if (!node) {
         SetNotFound(error, varname);
         return;
      }

      //DumpTree(node);

      if (node->GetType() == MJSON_OBJECT) {
         SetVarError(error, varname, "JSON node is a subdirectory");
         return;
      } else if (node->GetType() == MJSON_ARRAY) {

         const MJsonNodeVector* a = node->GetArray();

         int num_values = a->size();

         value->clear();

         for (int i=0; i<num_values; i++) {
            const MJsonNode* elem = (*a)[i];
            T v;
            bool ok = GetJsonValue<T>(varname, elem, &v, error);
            if (!ok)
               break;
            value->push_back(v);
         }
      } else {
         T v;
         bool ok = GetJsonValue<T>(varname, node, &v, error);
         if (!ok)
            return;
         value->clear();
         value->push_back(v);
      }
   };

   void RBA(const char* varname, std::vector<bool> *value, bool create, int create_size, MVOdbError* error)
   {
      RXA(varname, value, error);
   }

   void RIA(const char* varname, std::vector<int> *value,  bool create, int create_size, MVOdbError* error)
   {
      RXA(varname, value, error);
   }

   void RDA(const char* varname, std::vector<double> *value,  bool create, int create_size, MVOdbError* error)
   {
      RXA(varname, value, error);
   }
   
   void RFA(const char* varname, std::vector<float>  *value,  bool create, int create_size, MVOdbError* error)
   {
      RXA(varname, value, error);
   }
   
   void RSA(const char* varname, std::vector<std::string> *value, bool create, int create_size, int create_string_length, MVOdbError* error)
   {
      RXA(varname, value, error);
   }

   void RU16A(const char* varname, std::vector<uint16_t> *value,  bool create, int create_size, MVOdbError* error)
   {
      RXA(varname, value, error);
   }

   void RU32A(const char* varname, std::vector<uint32_t> *value,  bool create, int create_size, MVOdbError* error)
   {
      RXA(varname, value, error);
   }

   template <typename T>
   void RXAI(const char* varname, int index, T* value, MVOdbError* error)
   {
      if (!value) {
         SetOk(error);
         return;
      }

      MJsonNode* node = FindPath(fDir, varname);
      if (!node) {
         SetNotFound(error, varname);
         return;
      }

      //printf("varname [%s] index %d, found node %p:\n", varname, index, node);
      //node->Dump();

      if (node->GetType() == MJSON_OBJECT) {
         SetVarError(error, varname, "JSON node is a subdirectory");
         return;
      } else if (node->GetType() == MJSON_ARRAY) {
         //DumpTree(node);

         const MJsonNodeVector* a = node->GetArray();

         int num_values = a->size();
         
         if (index < 0) {
            std::string msg;
            msg += "bad index ";
            msg += toString(index);
            msg += " for array of size ";
            msg += toString(num_values);
            SetVarError(error, varname, msg);
            return;
         }
         
         if (index >= num_values) {
            std::string msg;
            msg += "bad index ";
            msg += toString(index);
            msg += " for array of size ";
            msg += toString(num_values);
            SetVarError(error, varname, msg);
            return;
         }
         
         MJsonNode* elem = (*a)[index];

         GetJsonValue<T>(varname, elem, value, error);
         return;
      } else {
         if (index != 0) {
            std::string msg;
            msg += "non-zero index ";
            msg += toString(index);
            msg += " for non-array";
            SetVarError(error, varname, msg);
            return;
         }

         GetJsonValue<T>(varname, node, value, error);
         return;
      }
   }

   void RBAI(const char* varname, int index, bool   *value, MVOdbError* error)
   {
      RXAI(varname, index, value, error);
   }

   void RIAI(const char* varname, int index, int    *value, MVOdbError* error)
   {
      RXAI(varname, index, value, error);
   }
   
   void RDAI(const char* varname, int index, double *value, MVOdbError* error)
   {
      RXAI(varname, index, value, error);
   }
   
   void RFAI(const char* varname, int index, float  *value, MVOdbError* error)
   {
      RXAI(varname, index, value, error);
   }
   
   void RSAI(const char* varname, int index, std::string *value, MVOdbError* error)
   {
      RXAI(varname, index, value, error);
   }
   
   void RU16AI(const char* varname, int index, uint16_t *value, MVOdbError* error)
   {
      RXAI(varname, index, value, error);
   }

   void RU32AI(const char* varname, int index, uint32_t *value, MVOdbError* error)
   {
      RXAI(varname, index, value, error);
   }

   // write functions do nothing

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

   // delete function does nothing

   void Delete(const char* odbname, MVOdbError* error) { SetOk(error); };
};

template<>
bool JsonOdb::GetJsonValue<int>(const char* varname, const MJsonNode* node, int* value, MVOdbError* error)
{
   switch (node->GetType()) {
   case MJSON_INT: *value = node->GetInt(); SetOk(error); return true;
   default: SetWrongType(error, varname, node, "int"); return false;
   }
}

template<>
bool JsonOdb::GetJsonValue<double>(const char* varname, const MJsonNode* node, double* value, MVOdbError* error)
{
   switch (node->GetType()) {
   //case MJSON_INT: *value = node->GetInt(); SetOk(error); return true;
   case MJSON_NUMBER: *value = node->GetDouble(); SetOk(error); return true;
   default: SetWrongType(error, varname, node, "double"); return false;
   }
}

template<>
bool JsonOdb::GetJsonValue<float>(const char* varname, const MJsonNode* node, float* value, MVOdbError* error)
{
   switch (node->GetType()) {
   //case MJSON_INT: *value = node->GetInt(); SetOk(error); return true;
   case MJSON_NUMBER: *value = node->GetDouble(); SetOk(error); return true;
   default: SetWrongType(error, varname, node, "float"); return false;
   }
}

template<>
bool JsonOdb::GetJsonValue<bool>(const char* varname, const MJsonNode* node, bool* value, MVOdbError* error)
{
   switch (node->GetType()) {
   //case MJSON_INT: *value = node->GetInt(); SetOk(error); return true;
   //case MJSON_NUMBER: *value = node->GetDouble(); SetOk(error); return true;
   case MJSON_BOOL: *value = node->GetBool(); SetOk(error); return true;
   default: SetWrongType(error, varname, node, "bool"); return false;
   }
}

template<>
bool JsonOdb::GetJsonValue<uint16_t>(const char* varname, const MJsonNode* node, uint16_t* value, MVOdbError* error)
{
   switch (node->GetType()) {
   case MJSON_INT: *value = (0xFFFF & node->GetInt()); SetOk(error); return true;
   case MJSON_STRING: *value = (0xFFFF & strtoul(node->GetString().c_str(), NULL, 0)); SetOk(error); return true;
   default: SetWrongType(error, varname, node, "uint16_t"); return false;
   }
}

template<>
bool JsonOdb::GetJsonValue<uint32_t>(const char* varname, const MJsonNode* node, uint32_t* value, MVOdbError* error)
{
   switch (node->GetType()) {
   case MJSON_INT: *value = node->GetInt(); SetOk(error); return true;
   case MJSON_STRING: *value = strtoul(node->GetString().c_str(), NULL, 0); SetOk(error); return true;
   default: SetWrongType(error, varname, node, "uint32_t"); return false;
   }
}

template<>
bool JsonOdb::GetJsonValue<std::string>(const char* varname, const MJsonNode* node, std::string* value, MVOdbError* error)
{
   switch (node->GetType()) {
   case MJSON_STRING: *value = node->GetString(); SetOk(error); return true;
   default: SetWrongType(error, varname, node, "std::string"); return false;
   }
}

MVOdb* MakeJsonFileOdb(const char* filename, MVOdbError* error)
{
   std::string data;

   {
      FILE *fp = fopen(filename, "r");
      if (!fp) {
         std::string msg;
         msg += "Cannot open file ";
         msg += "\"";
         msg += filename;
         msg += "\"";
         msg += " fopen() errno: ";
         msg += toString(errno);
         msg += " (";
         msg += strerror(errno);
         msg += ")";
         SetError(error, true, filename, msg);
         return MakeNullOdb();
      }
      
      while (1) {
         char buf[1024*1024];
         const char* s = fgets(buf, sizeof(buf), fp);
         if (!s)
            break;
         data += s;
      }
      
      fclose(fp);
      fp = NULL;
   }

   MJsonNode* root = MJsonNode::Parse(data.c_str());
   //root->Dump();
   return new JsonOdb(root, root, error);
}

MVOdb* MakeJsonBufferOdb(const char* buf, int bufsize, MVOdbError* error)
{
   MJsonNode* root = MJsonNode::Parse(buf);
   //root->Dump();
   return new JsonOdb(root, root, error);
   //return MakeNullOdb();
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
