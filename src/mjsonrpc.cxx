/********************************************************************\

  Name:         mjsonrpc.cxx
  Created by:   Konstantin Olchanski

  Contents:     handler of MIDAS standard JSON-RPC requests

\********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <map>

#include "mjson.h"
#include "midas.h"
#include "msystem.h"

#include "mjsonrpc.h"

//////////////////////////////////////////////////////////////////////
//
// Specifications for JSON-RPC
//
// https://tools.ietf.org/html/rfc4627 - JSON RFC
// http://www.jsonrpc.org/specification - specification of JSON-RPC 2.0
// http://www.simple-is-better.org/json-rpc/transport_http.html
//
// NB - MIDAS JSON (odb.c and mjson.cxx) encode IEEE754/854 numeric values
//      NaN and +/-Inf into JSON strings "NaN", "Infinity" and "-Infinity"
//      for reasons unknown, the JSON standard does not specify a standard
//      way for encoding these numeric values.
//
//////////////////////////////////////////////////////////////////////
//
// JSON-RPC error codes:
//
// -32700	Parse error	Invalid JSON was received by the server. An error occurred on the server while parsing the JSON text.
// -32600	Invalid Request	The JSON sent is not a valid Request object.
// -32601	Method not found	The method does not exist / is not available.
// -32602	Invalid params	Invalid method parameter(s).
// -32603	Internal error	Internal JSON-RPC error.
// -32000 to -32099	Server error	Reserved for implementation-defined server-errors.
//
// Typical JSON-RPC request:
//
// {
//    "jsonrpc": "2.0",
//    "method": "sum",
//    "params": { "b": 34, "c": 56, "a": 12 },
//    "id": 123
// }
//
// Typical JSON-RPC reply:
//
// {
//    "jsonrpc": "2.0",
//    "result": 102,
//    "id": 5
// }
//
// Typical error reply:
//
// {
//    "jsonrpc": "2.0",
//    "error": {
//        "code": -32600,
//        "message": "Invalid Request.",
//        "data": "'method' is missing"
//        },
//    "id": 6
//    }
// }
//
//////////////////////////////////////////////////////////////////////
//
// JSON-RPC is documented via an automatically generated JSON Schema.
//
// For more information about JSON Schemas, see:
//
// https://tools.ietf.org/html/draft-zyp-json-schema-04
// http://spacetelescope.github.io/understanding-json-schema/
// http://json-schema.org/
//
// JSON Schema examples:
// http://json-schema.org/examples.html
// http://json-schema.org/example1.html
//
// JSON Schema visualization: (schema file has to have a .json extension)
// https://github.com/lbovet/docson
//
// Non-standard proposed JSON-RPC schema is *NOT* used: (no visualization tools)
// http://www.simple-is-better.org/json-rpc/jsonrpc20-schema-service-descriptor.html
//
// Variances of MIDAS JSON-RPC Schema from standard:
//
// - optional parameters end with "?" and have an "optional:true" attribute, i.e. "bUnique?"
// - array parameters end with "[]", JSON Schema array schema is not generated yet
//
//////////////////////////////////////////////////////////////////////

int mjsonrpc_debug = 0;

MJsonNode* mjsonrpc_make_error(int code, const char* message, const char* data)
{
   MJsonNode* errnode = MJsonNode::MakeObject();
   errnode->AddToObject("code",    MJsonNode::MakeInt(code));
   errnode->AddToObject("message", MJsonNode::MakeString(message));
   errnode->AddToObject("data",    MJsonNode::MakeString(data));

   MJsonNode* result = MJsonNode::MakeObject();
   result->AddToObject("error", errnode);
   return result;
}

MJsonNode* mjsonrpc_make_result(MJsonNode* node)
{
   MJsonNode* result = MJsonNode::MakeObject();
   result->AddToObject("result", node);
   return result;
}

MJsonNode* mjsonrpc_make_result(const char* name, MJsonNode* value, const char* name2, MJsonNode* value2, const char* name3, MJsonNode* value3)
{
   MJsonNode* node = MJsonNode::MakeObject();

   if (name)
      node->AddToObject(name, value);
   if (name2)
      node->AddToObject(name2, value2);
   if (name3)
      node->AddToObject(name3, value3);

   MJsonNode* result = MJsonNode::MakeObject();
   result->AddToObject("result", node);
   return result;
}

const MJsonNode* mjsonrpc_get_param(const MJsonNode* params, const char* name, MJsonNode** error)
{
   static MJsonNode* null_node = NULL;
   if (!null_node)
      null_node = MJsonNode::MakeNull();

   // NULL params is a request for documentation, return an empty object
   if (!params) {
      if (error)
         *error = MJsonNode::MakeObject();
      return null_node;
   }

   const MJsonNode* obj = params->FindObjectNode(name);
   if (!obj) {
      if (error)
         *error = mjsonrpc_make_error(-32602, "Invalid params", (std::string("missing parameter: ") + name).c_str());
      return null_node;
   }

   if (error)
      *error = NULL;
   return obj;
}

const MJsonNodeVector* mjsonrpc_get_param_array(const MJsonNode* params, const char* name, MJsonNode** error)
{
   // NULL params is a request for documentation, return NULL
   if (!params) {
      if (error)
         *error = MJsonNode::MakeObject();
      return NULL;
   }

   const MJsonNode* node = mjsonrpc_get_param(params, name, error);

   // handle error return from mjsonrpc_get_param()
   if (error && *error) {
      return NULL;
   }

   const MJsonNodeVector* v = node->GetArray();

   if (!v) {
      if (error)
         *error = mjsonrpc_make_error(-32602, "Invalid params", (std::string("parameter must be an array: ") + name).c_str());
      return NULL;
   }

   if (error)
      *error = NULL;
   return v;
}

MJSO* MJSO::MakeObjectSchema(const char* description) // constructor for object schema
{
   MJSO* p = new MJSO();
   if (description)
      p->AddToObject("description", MJsonNode::MakeString(description));
   p->AddToObject("type", MJsonNode::MakeString("object"));
   p->properties = MJsonNode::MakeObject();
   p->required = MJsonNode::MakeArray();
   p->AddToObject("properties", p->properties);
   p->AddToObject("required", p->required);
   return p;
}

MJSO* MJSO::MakeArraySchema(const char* description) // constructor for array schema
{
   MJSO* p = new MJSO();
   p->AddToObject("description", MJsonNode::MakeString(description));
   p->AddToObject("type", MJsonNode::MakeString("array"));
   p->items = MJsonNode::MakeArray();
   p->AddToObject("items", p->items);
   return p;
}

static std::string remove(const std::string s, char c)
{
   std::string::size_type pos = s.find(c);
   if (pos == std::string::npos)
      return s;
   else
      return s.substr(0, pos);
}

void MJSO::AddToSchema(MJsonNode* s, const char* xname)
{
   if (!xname)
      xname = "";

   bool optional = strchr(xname, '?');
   bool array = strchr(xname, '[');

   // remove the "?" and "[]" marker characters
   std::string name = xname;
   name = remove(name, '?');
   name = remove(name, '[');
   name = remove(name, ']');

   if (optional)
      s->AddToObject("optional", MJsonNode::MakeBool(true));

   if (array) { // insert an array schema
      MJSO* ss = MakeArraySchema(s->FindObjectNode("description")->GetString().c_str());
      s->DeleteObjectNode("description");
      ss->AddToSchema(s, "");
      s = ss;
   }

   if (items)
      items->AddToArray(s);
   else {
      assert(properties);
      assert(required);
      properties->AddToObject(name.c_str(), s);
      if (!optional) {
         required->AddToArray(MJsonNode::MakeString(name.c_str()));
      }
   }
}

MJSO* MJSO::I()
{
   return MakeObjectSchema(NULL);
}

void MJSO::D(const char* description)
{
   this->AddToObject("description", MJsonNode::MakeString(description));
}

MJSO* MJSO::Params()
{
   if (!params) {
      params = MakeObjectSchema(NULL);
      this->AddToSchema(params, "params");
   }
   return params;
}

MJSO* MJSO::Result()
{
   if (!result) {
      result = MakeObjectSchema(NULL);
      this->AddToSchema(result, "result");
   }
   return result;
}

MJSO* MJSO::PA(const char* description)
{
   MJSO* s = MakeArraySchema(description);
   this->AddToSchema(s, "params");
   return s;
}

MJSO* MJSO::RA(const char* description)
{
   MJSO* s = MakeArraySchema(description);
   this->AddToSchema(s, "result");
   return s;
}

void MJSO::P(const char* name, int mjson_type, const char* description)
{
   if (name == NULL)
      this->Add("params", mjson_type, description);
   else
      Params()->Add(name, mjson_type, description);
}

void MJSO::R(const char* name, int mjson_type, const char* description)
{
   if (name == NULL)
      this->Add("result", mjson_type, description);
   else
      Result()->Add(name, mjson_type, description);
}

void MJSO::Add(const char* name, int mjson_type, const char* description)
{
   MJsonNode* p = MJsonNode::MakeObject();
   p->AddToObject("description", MJsonNode::MakeString(description));
   if (mjson_type == MJSON_ARRAY)
      p->AddToObject("type", MJsonNode::MakeString("array"));
   else if (mjson_type == MJSON_OBJECT)
      p->AddToObject("type", MJsonNode::MakeString("object"));
   else if (mjson_type == MJSON_STRING)
      p->AddToObject("type", MJsonNode::MakeString("string"));
   else if (mjson_type == MJSON_INT)
      p->AddToObject("type", MJsonNode::MakeString("integer"));
   else if (mjson_type == MJSON_NUMBER)
      p->AddToObject("type", MJsonNode::MakeString("number"));
   else if (mjson_type == MJSON_BOOL)
      p->AddToObject("type", MJsonNode::MakeString("bool"));
   else if (mjson_type == MJSON_NULL)
      p->AddToObject("type", MJsonNode::MakeString("null"));
   else if (mjson_type == 0)
      ;
   else
      assert(!"invalid value of mjson_type");
   this->AddToSchema(p, name);
}

MJSO* MJSO::AddObject(const char* name, const char* description)
{
   MJSO* s = MakeObjectSchema(description);
   s->AddToObject("description", MJsonNode::MakeString(description));
   s->AddToObject("type", MJsonNode::MakeString("object"));
   this->AddToSchema(s, name);
   return s;
}

MJSO* MJSO::AddArray(const char* name, const char* description)
{
   MJSO* s = MakeArraySchema(description);
   s->AddToObject("description", MJsonNode::MakeString(description));
   s->AddToObject("type", MJsonNode::MakeString("array"));
   this->AddToSchema(s, name);
   return s;
}

MJSO::MJSO() // ctor
   : MJsonNode(MJSON_OBJECT)
{
   properties = NULL;
   required = NULL;
   items = NULL;
   params = NULL;
   result = NULL;
}

static MJsonNode* xnull(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("RPC method always returns null");
      doc->P(NULL, 0, "method parameters are ignored");
      doc->R(NULL, MJSON_NULL, "always returns null");
      return doc;
   }

   return mjsonrpc_make_result(MJsonNode::MakeNull());
}

static MJsonNode* js_cm_exist(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("calls MIDAS cm_exist() to check if given MIDAS program is running");
      doc->P("name", MJSON_STRING, "name of the program, corresponding to ODB /Programs/name");
      doc->P("unique?", MJSON_BOOL, "bUnique argument to cm_exist()");
      doc->R("status", MJSON_INT, "return status of cm_exist()");
      return doc;
   }

   MJsonNode* error = NULL;

   const char* name = mjsonrpc_get_param(params, "name", &error)->GetString().c_str();
   if (error)
      return error;

   int unique = mjsonrpc_get_param(params, "unique", NULL)->GetBool();

   int status = cm_exist(name, unique);

   if (mjsonrpc_debug)
      printf("cm_exist(%s,%d) -> %d\n", name, unique, status);

   return mjsonrpc_make_result("status", MJsonNode::MakeInt(status));
}

static MJsonNode* js_cm_shutdown(const MJsonNode* params)
{
   if (!params) {
      MJSO *doc = MJSO::I();
      doc->D("calls MIDAS cm_shutdown() to stop given MIDAS program");
      doc->P("name", MJSON_STRING, "name of the program, corresponding to ODB /Programs/name");
      doc->P("unique?", MJSON_BOOL, "bUnique argument to cm_shutdown()");
      doc->R("status", MJSON_INT, "return status of cm_shutdown()");
      return doc;
   }

   MJsonNode* error = NULL;

   const char* name = mjsonrpc_get_param(params, "name", &error)->GetString().c_str();
   if (error)
      return error;

   int unique = mjsonrpc_get_param(params, "unique", NULL)->GetBool();

   int status = cm_shutdown(name, unique);

   if (mjsonrpc_debug)
      printf("cm_shutdown(%s,%d) -> %d\n", name, unique, status);

   return mjsonrpc_make_result("status", MJsonNode::MakeInt(status));
}

static MJsonNode* start_program(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("start MIDAS program defined in ODB /Programs/name");
      doc->P("name", MJSON_STRING, "name of the program, corresponding to ODB /Programs/name");
      doc->R("status", MJSON_INT, "return status of ss_system()");
      return doc;
   }

   MJsonNode* error = NULL;

   const char* name = mjsonrpc_get_param(params, "name", &error)->GetString().c_str(); if (error) return error;

   std::string path = "";
   path += "/Programs/";
   path += name;
   path += "/Start command";

   HNDLE hDB;
   cm_get_experiment_database(&hDB, NULL);

   char command[256];
   int size = sizeof(command);
   int status = db_get_value(hDB, 0, path.c_str(), command, &size, TID_STRING, FALSE);

   if (status == DB_SUCCESS && command[0]) {
      status = ss_system(command);
   }

   return mjsonrpc_make_result("status", MJsonNode::MakeInt(status));
}

static int parse_array_index_list(const char* method, const char* path, std::vector<unsigned> *list)
{
   // parse array index in form of:
   // odbpath[number]
   // odbpath[number,number]
   // odbpath[number-number]
   // or any combination of them, i.e. odbpath[1,10-15,20,30-40]

   const char*s = strchr(path, '[');

   if (!s) {
      cm_msg(MERROR, method, "expected an array index character \'[\' in \"%s\"", path);
      return DB_OUT_OF_RANGE;
   }
  
   s++; // skip '[' itself

   while (s && (*s != 0)) {

      // check that we have a number
      if (!isdigit(*s)) {
         cm_msg(MERROR, method, "expected a number in array index in \"%s\" at \"%s\"", path, s);
         return DB_OUT_OF_RANGE;
      }

      unsigned value1 = strtoul(s, (char**)&s, 10);
      
      // array range, 
      if (*s == '-') {
         s++; // skip the minus char

         if (!isdigit(*s)) {
            cm_msg(MERROR, method, "expected a number in array index in \"%s\" at \"%s\"", path, s);
            return DB_OUT_OF_RANGE;
         }

         unsigned value2 = strtoul(s, (char**)&s, 10);

         if (value2 >= value1)
            for (unsigned i=value1; i<=value2; i++)
               list->push_back(i);
         else {
            // this is stupid. simple loop like this
            // for (unsigned i=value1; i>=value2; i--)
            // does not work for range 4-0, because value2 is 0,
            // and x>=0 is always true for unsigned numbers,
            // so we would loop forever... K.O.
            for (unsigned i=value1; i!=value2; i--)
               list->push_back(i);
            list->push_back(value2);
         }
      } else {
         list->push_back(value1);
      }

      if (*s == ',') {
         s++; // skip the comma char
         continue; // back to the begin of loop
      }

      if (*s == ']') {
         s++; // skip the closing bracket
         s = NULL;
         continue; // done
      }

      cm_msg(MERROR, method, "invalid char in array index in \"%s\" at \"%s\"", path, s);
      return DB_OUT_OF_RANGE;
   }

#if 0
   printf("parsed array indices for \"%s\" size is %d: ", path, (int)list->size());
   for (unsigned i=0; i<list->size(); i++)
      printf(" %d", (*list)[i]);
   printf("\n");
#endif

   return SUCCESS;
}

static MJsonNode* js_db_get_values(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("get values of ODB data from given subtrees");
      doc->P("paths[]", MJSON_STRING, "array of ODB subtree paths, see note on array indices");
      doc->P("omit_names?", MJSON_BOOL, "omit the /name entries");
      doc->P("omit_last_written?", MJSON_BOOL, "omit the /last_written entries");
      doc->P("omit_old_timestamp?", MJSON_NUMBER, "omit data older than given ODB timestamp");
      doc->R("data[]", 0, "values of ODB data for each path, all key names are in lower case, all symlinks are followed");
      doc->R("status[]", MJSON_INT, "return status of db_copy_json_values() or db_copy_json_index() for each path");
      doc->R("last_written?[]", MJSON_NUMBER, "last_written value of the ODB subtree for each path, absent if omit_last_written is true");
      return doc;
   }

   MJsonNode* error = NULL;

   const MJsonNodeVector* paths = mjsonrpc_get_param_array(params, "paths", &error); if (error) return error;

   bool omit_names = mjsonrpc_get_param(params, "omit_names", NULL)->GetBool();
   bool omit_last_written = mjsonrpc_get_param(params, "omit_last_written", NULL)->GetBool();
   double xomit_old_timestamp = mjsonrpc_get_param(params, "omit_old_timestamp", NULL)->GetNumber();
   time_t omit_old_timestamp = xomit_old_timestamp;

   MJsonNode* dresult = MJsonNode::MakeArray();
   MJsonNode* sresult = MJsonNode::MakeArray();
   MJsonNode* lwresult = MJsonNode::MakeArray();

   HNDLE hDB;
   cm_get_experiment_database(&hDB, NULL);

   for (unsigned i=0; i<paths->size(); i++) {
      int status = 0;
      HNDLE hkey;
      KEY key;
      std::string path = (*paths)[i]->GetString();

      status = db_find_key(hDB, 0, path.c_str(), &hkey);
      if (status != DB_SUCCESS) {
         dresult->AddToArray(MJsonNode::MakeNull());
         sresult->AddToArray(MJsonNode::MakeInt(status));
         lwresult->AddToArray(MJsonNode::MakeNull());
         continue;
      }

      status = db_get_key(hDB, hkey, &key);
      if (status != DB_SUCCESS) {
         dresult->AddToArray(MJsonNode::MakeNull());
         sresult->AddToArray(MJsonNode::MakeInt(status));
         lwresult->AddToArray(MJsonNode::MakeNull());
         continue;
      }

      if (path.find("[") != std::string::npos) {
         std::vector<unsigned> list;
         status = parse_array_index_list("js_db_get_values", path.c_str(), &list);

         if (status != SUCCESS) {
            dresult->AddToArray(MJsonNode::MakeNull());
            sresult->AddToArray(MJsonNode::MakeInt(status));
            lwresult->AddToArray(MJsonNode::MakeInt(key.last_written));
            continue;
         }

         if (list.size() > 1) {
            MJsonNode *ddresult = MJsonNode::MakeArray();
            MJsonNode *ssresult = MJsonNode::MakeArray();

            for (unsigned i=0; i<list.size(); i++) {
               char* buf = NULL;
               int bufsize = 0;
               int end = 0;
               
               status = db_copy_json_index(hDB, hkey, list[i], &buf, &bufsize, &end);
               if (status == DB_SUCCESS) {
                  ddresult->AddToArray(MJsonNode::MakeJSON(buf));
                  ssresult->AddToArray(MJsonNode::MakeInt(status));
               } else {
                  ddresult->AddToArray(MJsonNode::MakeNull());
                  ssresult->AddToArray(MJsonNode::MakeInt(status));
               }

               if (buf)
                  free(buf);
            }

            dresult->AddToArray(ddresult);
            sresult->AddToArray(ssresult);
            lwresult->AddToArray(MJsonNode::MakeInt(key.last_written));

         } else {
            char* buf = NULL;
            int bufsize = 0;
            int end = 0;
            
            status = db_copy_json_index(hDB, hkey, list[0], &buf, &bufsize, &end);
            if (status == DB_SUCCESS) {
               dresult->AddToArray(MJsonNode::MakeJSON(buf));
               sresult->AddToArray(MJsonNode::MakeInt(status));
               lwresult->AddToArray(MJsonNode::MakeInt(key.last_written));
            } else {
               dresult->AddToArray(MJsonNode::MakeNull());
               sresult->AddToArray(MJsonNode::MakeInt(status));
               lwresult->AddToArray(MJsonNode::MakeInt(key.last_written));
            }
            
            if (buf)
               free(buf);
         }
      } else {
         char* buf = NULL;
         int bufsize = 0;
         int end = 0;

         status = db_copy_json_values(hDB, hkey, &buf, &bufsize, &end, omit_names, omit_last_written, omit_old_timestamp);

         if (status == DB_SUCCESS) {
            dresult->AddToArray(MJsonNode::MakeJSON(buf));
            sresult->AddToArray(MJsonNode::MakeInt(status));
            lwresult->AddToArray(MJsonNode::MakeInt(key.last_written));
         } else {
            dresult->AddToArray(MJsonNode::MakeNull());
            sresult->AddToArray(MJsonNode::MakeInt(status));
            lwresult->AddToArray(MJsonNode::MakeInt(key.last_written));
         }

         if (buf)
            free(buf);
      }
   }

   if (omit_last_written) {
      delete lwresult;
      return mjsonrpc_make_result("data", dresult, "status", sresult);
   } else
      return mjsonrpc_make_result("data", dresult, "status", sresult, "last_written", lwresult);
}

static MJsonNode* js_db_ls(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("get contents of given ODB subdirectory in the \"ls\" json encoding - similar to odbedit command \"ls -l\"");
      doc->P("paths[]",  MJSON_STRING, "array of ODB subtree paths");
      doc->R("data[]",   MJSON_OBJECT, "keys and values of ODB data for each path");
      doc->R("status[]", MJSON_INT, "return status of db_copy_json_ls() for each path");
      return doc;
   }

   MJsonNode* error = NULL;

   const MJsonNodeVector* paths = mjsonrpc_get_param_array(params, "paths", &error); if (error) return error;

   MJsonNode* dresult = MJsonNode::MakeArray();
   MJsonNode* sresult = MJsonNode::MakeArray();

   HNDLE hDB;
   cm_get_experiment_database(&hDB, NULL);

   for (unsigned i=0; i<paths->size(); i++) {
      int status = 0;
      HNDLE hkey;
      std::string path = (*paths)[i]->GetString();

      status = db_find_key(hDB, 0, path.c_str(), &hkey);
      if (status != DB_SUCCESS) {
         dresult->AddToArray(MJsonNode::MakeNull());
         sresult->AddToArray(MJsonNode::MakeInt(status));
         continue;
      }

      char* buf = NULL;
      int bufsize = 0;
      int end = 0;

      status = db_copy_json_ls(hDB, hkey, &buf, &bufsize, &end);

      if (status == DB_SUCCESS) {
         dresult->AddToArray(MJsonNode::MakeJSON(buf));
         sresult->AddToArray(MJsonNode::MakeInt(status));
      } else {
         dresult->AddToArray(MJsonNode::MakeNull());
         sresult->AddToArray(MJsonNode::MakeInt(status));
      }

      if (buf)
         free(buf);
   }

   return mjsonrpc_make_result("data", dresult, "status", sresult);
}

static MJsonNode* js_db_copy(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("get complete ODB data in the \"save\" json encoding, suitable for reloading with odbedit command \"load\"");
      doc->P("paths[]",  MJSON_STRING, "array of ODB subtree paths");
      doc->R("data[]",   MJSON_OBJECT, "keys and values of ODB data for each path");
      doc->R("status[]", MJSON_INT, "return status of db_copy_json_save() for each path");
      return doc;
   }

   MJsonNode* error = NULL;

   const MJsonNodeVector* paths = mjsonrpc_get_param_array(params, "paths", &error); if (error) return error;

   MJsonNode* dresult = MJsonNode::MakeArray();
   MJsonNode* sresult = MJsonNode::MakeArray();

   HNDLE hDB;
   cm_get_experiment_database(&hDB, NULL);

   for (unsigned i=0; i<paths->size(); i++) {
      int status = 0;
      HNDLE hkey;
      std::string path = (*paths)[i]->GetString();

      status = db_find_key(hDB, 0, path.c_str(), &hkey);
      if (status != DB_SUCCESS) {
         dresult->AddToArray(MJsonNode::MakeNull());
         sresult->AddToArray(MJsonNode::MakeInt(status));
         continue;
      }

      char* buf = NULL;
      int bufsize = 0;
      int end = 0;

      status = db_copy_json_save(hDB, hkey, &buf, &bufsize, &end);

      if (status == DB_SUCCESS) {
         dresult->AddToArray(MJsonNode::MakeJSON(buf));
         sresult->AddToArray(MJsonNode::MakeInt(status));
      } else {
         dresult->AddToArray(MJsonNode::MakeNull());
         sresult->AddToArray(MJsonNode::MakeInt(status));
      }

      if (buf)
         free(buf);
   }

   return mjsonrpc_make_result("data", dresult, "status", sresult);
}

static MJsonNode* js_db_paste(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("write data into ODB");
      doc->P("paths[]", MJSON_STRING, "array of ODB subtree paths, see note on array indices");
      doc->P("values[]", 0, "array of data values written to ODB via db_paste_json() for each path");
      doc->R("status[]", MJSON_INT, "array of return status of db_paste_json() for each path");
      return doc;
   }

   MJsonNode* error = NULL;

   const MJsonNodeVector* paths  = mjsonrpc_get_param_array(params, "paths",  &error); if (error) return error;
   const MJsonNodeVector* values = mjsonrpc_get_param_array(params, "values", &error); if (error) return error;

   if (paths->size() != values->size()) {
      return mjsonrpc_make_error(-32602, "Invalid params", "paths and values should have the same length");
   }

   MJsonNode* sresult = MJsonNode::MakeArray();

   HNDLE hDB;
   cm_get_experiment_database(&hDB, NULL);

   for (unsigned i=0; i<paths->size(); i++) {
      int status = 0;
      HNDLE hkey;
      std::string path = (*paths)[i]->GetString();

      status = db_find_key(hDB, 0, path.c_str(), &hkey);
      if (status != DB_SUCCESS) {
         sresult->AddToArray(MJsonNode::MakeInt(status));
         continue;
      }

      const MJsonNode* v = (*values)[i];
      assert(v != NULL);

      if (path.find("[") != std::string::npos) {
         std::vector<unsigned> list;
         status = parse_array_index_list("js_db_paste", path.c_str(), &list);

         if (status != SUCCESS) {
            sresult->AddToArray(MJsonNode::MakeInt(status));
            continue;
         }

         // supported permutations of array indices and data values:
         // single index: intarray[1] -> data should be a single value: MJSON_ARRAY is rejected right here, MJSON_OBJECT is rejected by db_paste
         // multiple index intarray[1,2,3] -> data should be an array of equal length, or
         // multiple index intarray[1,2,3] -> if data is a single value, all array elements are set to this same value
         
         if (list.size() < 1) {
            cm_msg(MERROR, "js_db_paste", "invalid array indices for array path \"%s\"", path.c_str());
            sresult->AddToArray(MJsonNode::MakeInt(DB_TYPE_MISMATCH));
            continue;
         } else if (list.size() == 1) {
            if (v->GetType() == MJSON_ARRAY) {
               cm_msg(MERROR, "js_db_paste", "unexpected array of values for array path \"%s\"", path.c_str());
               sresult->AddToArray(MJsonNode::MakeInt(DB_TYPE_MISMATCH));
               continue;
            }

            status = db_paste_json_node(hDB, hkey, list[0], v);
            sresult->AddToArray(MJsonNode::MakeInt(status));
         } else if ((list.size() > 1) && (v->GetType() == MJSON_ARRAY)) {
            const MJsonNodeVector* vvalues = v->GetArray();

            if (list.size() != vvalues->size()) {
               cm_msg(MERROR, "js_db_paste", "length of values array %d should be same as number of indices %d for array path \"%s\"", (int)vvalues->size(), (int)list.size(), path.c_str());
               sresult->AddToArray(MJsonNode::MakeInt(DB_TYPE_MISMATCH));
               continue;
            }

            MJsonNode *ssresult = MJsonNode::MakeArray();

            for (unsigned i=0; i<list.size(); i++) {
               const MJsonNode* vv = (*vvalues)[i];

               if (vv == NULL) {
                  cm_msg(MERROR, "js_db_paste", "internal error: NULL array value at index %d for array path \"%s\"", i, path.c_str());
                  sresult->AddToArray(MJsonNode::MakeInt(DB_TYPE_MISMATCH));
                  continue;
               }

               status = db_paste_json_node(hDB, hkey, list[i], vv);
               ssresult->AddToArray(MJsonNode::MakeInt(status));
            }
            
            sresult->AddToArray(ssresult);
         } else {
            MJsonNode *ssresult = MJsonNode::MakeArray();
            for (unsigned i=0; i<list.size(); i++) {
               status = db_paste_json_node(hDB, hkey, list[i], v);
               ssresult->AddToArray(MJsonNode::MakeInt(status));
            }
            sresult->AddToArray(ssresult);
         }
      } else {
         status = db_paste_json_node(hDB, hkey, 0, v);
         sresult->AddToArray(MJsonNode::MakeInt(status));
      }
   }

   return mjsonrpc_make_result("status", sresult);
}

static MJsonNode* js_db_create(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("Create new ODB entries");
      MJSO* o = doc->PA("array of ODB paths to be created")->AddObject("", "arguments to db_create_key() and db_set_num_values()");
      o->Add("path", MJSON_STRING, "ODB path to be created");
      o->Add("type", MJSON_INT, "MIDAS TID_xxx type");
      o->Add("array_length?", MJSON_INT, "optional array length, default is 1");
      o->Add("string_length?", MJSON_INT, "for TID_STRING, optional string length, default is NAME_LENGTH");
      doc->R("status[]", MJSON_INT, "return status of db_create_key(), db_set_num_values() and db_set_data() (for TID_STRING) for each path");
      return doc;
   }

   MJsonNode* sresult = MJsonNode::MakeArray();

   const MJsonNodeVector* pp = params->GetArray();

   if (!pp) {
      return mjsonrpc_make_error(-32602, "Invalid params", "parameters must be an array of objects");
   }

   HNDLE hDB;
   cm_get_experiment_database(&hDB, NULL);

   for (unsigned i=0; i<pp->size(); i++) {
      const MJsonNode* p = (*pp)[i];
      std::string path = mjsonrpc_get_param(p, "path", NULL)->GetString();
      int type = mjsonrpc_get_param(p, "type", NULL)->GetInt();
      int array_length = mjsonrpc_get_param(p, "array_length", NULL)->GetInt();
      int string_length = mjsonrpc_get_param(p, "string_length", NULL)->GetInt();

      //printf("create odb [%s], type %d, array %d, string %d\n", path.c_str(), type, array_length, string_length);

      int status = db_create_key(hDB, 0, path.c_str(), type);

      if (status == DB_SUCCESS && string_length > 0 && type == TID_STRING) {
         HNDLE hKey;
         status = db_find_key(hDB, 0, path.c_str(), &hKey);
         if (status == DB_SUCCESS) {
            char* buf = (char*)calloc(1, string_length);
            assert(buf != NULL);
            int size = string_length;
            status = db_set_data(hDB, hKey, buf, size, 1, TID_STRING);
            free(buf);
         }
      }

      if (status == DB_SUCCESS && array_length > 1) {
         HNDLE hKey;
         status = db_find_key(hDB, 0, path.c_str(), &hKey);
         if (status == DB_SUCCESS)
            status = db_set_num_values(hDB, hKey, array_length);
      }

      sresult->AddToArray(MJsonNode::MakeInt(status));
   }

   return mjsonrpc_make_result("status", sresult);
}

static MJsonNode* js_db_delete(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("delete ODB keys");
      doc->P("paths[]", MJSON_STRING, "array of ODB paths to delete");
      doc->R("status[]", MJSON_INT, "return status of db_delete_key() for each path");
      return doc;
   }

   MJsonNode* error = NULL;

   const MJsonNodeVector* paths  = mjsonrpc_get_param_array(params, "paths",  &error); if (error) return error;

   MJsonNode* sresult = MJsonNode::MakeArray();

   HNDLE hDB;
   cm_get_experiment_database(&hDB, NULL);

   for (unsigned i=0; i<paths->size(); i++) {
      int status = 0;
      HNDLE hkey;
      std::string path = (*paths)[i]->GetString();

      status = db_find_key(hDB, 0, path.c_str(), &hkey);
      if (status != DB_SUCCESS) {
         sresult->AddToArray(MJsonNode::MakeInt(status));
         continue;
      }

      status = db_delete_key(hDB, hkey, false);
      sresult->AddToArray(MJsonNode::MakeInt(status));
   }

   return mjsonrpc_make_result("status", sresult);
}

static MJsonNode* js_db_resize(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("Change size of ODB arrays");
      doc->P("paths[]", MJSON_STRING, "array of ODB paths to resize");
      doc->P("new_lengths[]", MJSON_INT, "array of new lengths for each ODB path");
      doc->R("status[]", MJSON_INT, "return status of db_set_num_values() for each path");
      return doc;
   }

   MJsonNode* error = NULL;

   const MJsonNodeVector* paths   = mjsonrpc_get_param_array(params, "paths",  &error); if (error) return error;
   const MJsonNodeVector* lengths = mjsonrpc_get_param_array(params, "new_lengths", &error); if (error) return error;

   if (paths->size() != lengths->size()) {
      return mjsonrpc_make_error(-32602, "Invalid params", "arrays \"paths\" and \"new_lengths\" should have the same length");
   }

   MJsonNode* sresult = MJsonNode::MakeArray();

   HNDLE hDB;
   cm_get_experiment_database(&hDB, NULL);

   for (unsigned i=0; i<paths->size(); i++) {
      int status = 0;
      HNDLE hkey;
      std::string path = (*paths)[i]->GetString();

      status = db_find_key(hDB, 0, path.c_str(), &hkey);
      if (status != DB_SUCCESS) {
         sresult->AddToArray(MJsonNode::MakeInt(status));
         continue;
      }

      int length = (*lengths)[i]->GetInt();
      if (length < 1) {
         sresult->AddToArray(MJsonNode::MakeInt(DB_INVALID_PARAM));
         continue;
      }

      status = db_set_num_values(hDB, hkey, length);
      sresult->AddToArray(MJsonNode::MakeInt(status));
   }

   return mjsonrpc_make_result("status", sresult);
}

static MJsonNode* js_db_key(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("get ODB keys");
      doc->P("paths[]", MJSON_STRING, "array of ODB paths");
      doc->R("status[]", MJSON_INT, "return status of db_key() for each path");
      doc->R("keys[]", MJSON_OBJECT, "key data for each path");
      doc->R("keys[].type", MJSON_INT, "key type TID_xxx");
      doc->R("keys[].num_values", MJSON_INT, "array length, 1 for normal entries");
      doc->R("keys[].name", MJSON_STRING, "key name");
      doc->R("keys[].total_size", MJSON_INT, "data total size in bytes");
      doc->R("keys[].item_size", MJSON_INT, "array element size, string length for TID_STRING");
      doc->R("keys[].access_mode", MJSON_INT, "access mode bitmap of MODE_xxx");
      doc->R("keys[].notify_count", MJSON_INT, "number of hotlinks attached to this key");
      doc->R("keys[].last_written", MJSON_INT, "timestamp when data was last updated");
      return doc;
   }

   MJsonNode* error = NULL;

   const MJsonNodeVector* paths  = mjsonrpc_get_param_array(params, "paths",  &error); if (error) return error;

   MJsonNode* kresult = MJsonNode::MakeArray();
   MJsonNode* sresult = MJsonNode::MakeArray();

   HNDLE hDB;
   cm_get_experiment_database(&hDB, NULL);

   for (unsigned i=0; i<paths->size(); i++) {
      int status = 0;
      HNDLE hkey;
      KEY key;
      std::string path = (*paths)[i]->GetString();

      status = db_find_key(hDB, 0, path.c_str(), &hkey);
      if (status != DB_SUCCESS) {
         kresult->AddToArray(MJsonNode::MakeNull());
         sresult->AddToArray(MJsonNode::MakeInt(status));
         continue;
      }

      status = db_get_key(hDB, hkey, &key);
      if (status != DB_SUCCESS) {
         kresult->AddToArray(MJsonNode::MakeNull());
         sresult->AddToArray(MJsonNode::MakeInt(status));
         continue;
      }

      MJsonNode* jkey = MJsonNode::MakeObject();

      jkey->AddToObject("type", MJsonNode::MakeInt(key.type));
      jkey->AddToObject("num_values", MJsonNode::MakeInt(key.num_values));
      jkey->AddToObject("name", MJsonNode::MakeString(key.name));
      jkey->AddToObject("total_size", MJsonNode::MakeInt(key.total_size));
      jkey->AddToObject("item_size", MJsonNode::MakeInt(key.item_size));
      jkey->AddToObject("access_mode", MJsonNode::MakeInt(key.access_mode));
      jkey->AddToObject("notify_count", MJsonNode::MakeInt(key.notify_count));
      jkey->AddToObject("last_written", MJsonNode::MakeInt(key.last_written));

      kresult->AddToArray(jkey);
      sresult->AddToArray(MJsonNode::MakeInt(status));
   }

   return mjsonrpc_make_result("keys", kresult, "status", sresult);
}

static MJsonNode* js_db_rename(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("Change size of ODB arrays");
      doc->P("paths[]", MJSON_STRING, "array of ODB paths to rename");
      doc->P("new_names[]", MJSON_STRING, "array of new names for each ODB path");
      doc->R("status[]", MJSON_INT, "return status of db_rename_key() for each path");
      return doc;
   }

   MJsonNode* error = NULL;

   const MJsonNodeVector* paths = mjsonrpc_get_param_array(params, "paths",  &error); if (error) return error;
   const MJsonNodeVector* names = mjsonrpc_get_param_array(params, "new_names", &error); if (error) return error;

   if (paths->size() != names->size()) {
      return mjsonrpc_make_error(-32602, "Invalid params", "arrays \"paths\" and \"new_names\" should have the same length");
   }

   MJsonNode* sresult = MJsonNode::MakeArray();

   HNDLE hDB;
   cm_get_experiment_database(&hDB, NULL);

   for (unsigned i=0; i<paths->size(); i++) {
      int status = 0;
      HNDLE hkey;
      std::string path = (*paths)[i]->GetString();

      status = db_find_key(hDB, 0, path.c_str(), &hkey);
      if (status != DB_SUCCESS) {
         sresult->AddToArray(MJsonNode::MakeInt(status));
         continue;
      }

      std::string new_name = (*names)[i]->GetString();
      if (new_name.length() < 1) {
         sresult->AddToArray(MJsonNode::MakeInt(DB_INVALID_PARAM));
         continue;
      }

      status = db_rename_key(hDB, hkey, new_name.c_str());

      sresult->AddToArray(MJsonNode::MakeInt(status));
   }

   return mjsonrpc_make_result("status", sresult);
}

static MJsonNode* js_db_link(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("Create ODB symlinks");
      doc->P("new_links[]", MJSON_STRING, "array of new symlinks to be created");
      doc->P("target_paths[]", MJSON_STRING, "array of existing ODB paths for each link");
      doc->R("status[]", MJSON_INT, "return status of db_create_link() for each path");
      return doc;
   }

   MJsonNode* error = NULL;

   const MJsonNodeVector* target_paths = mjsonrpc_get_param_array(params, "target_paths",  &error); if (error) return error;
   const MJsonNodeVector* new_links = mjsonrpc_get_param_array(params, "new_links", &error); if (error) return error;

   if (target_paths->size() != new_links->size()) {
      return mjsonrpc_make_error(-32602, "Invalid params", "arrays \"target_paths\" and \"new_links\" should have the same length");
   }

   MJsonNode* sresult = MJsonNode::MakeArray();

   HNDLE hDB;
   cm_get_experiment_database(&hDB, NULL);

   for (unsigned i=0; i<new_links->size(); i++) {
      int status = 0;
      std::string target_path = (*target_paths)[i]->GetString();
      std::string new_link = (*new_links)[i]->GetString();
      if (new_link.length() < 1) {
         sresult->AddToArray(MJsonNode::MakeInt(DB_INVALID_PARAM));
         continue;
      }

      status = db_create_link(hDB, 0, new_link.c_str(), target_path.c_str());

      sresult->AddToArray(MJsonNode::MakeInt(status));
   }

   return mjsonrpc_make_result("status", sresult);
}

static MJsonNode* js_db_reorder(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("Change order of ODB keys in a subdirectory");
      doc->P("paths[]", MJSON_STRING, "array of new symlinks to be created");
      doc->P("indices[]", MJSON_INT, "array of existing ODB paths for each link");
      doc->R("status[]", MJSON_INT, "return status of db_reorder_key() for each path");
      return doc;
   }

   MJsonNode* error = NULL;

   const MJsonNodeVector* paths = mjsonrpc_get_param_array(params, "paths",  &error); if (error) return error;
   const MJsonNodeVector* indices = mjsonrpc_get_param_array(params, "indices", &error); if (error) return error;

   if (paths->size() != indices->size()) {
      return mjsonrpc_make_error(-32602, "Invalid params", "arrays \"paths\" and \"indices\" should have the same length");
   }

   MJsonNode* sresult = MJsonNode::MakeArray();

   HNDLE hDB;
   cm_get_experiment_database(&hDB, NULL);

   for (unsigned i=0; i<paths->size(); i++) {
      int status = 0;
      HNDLE hkey;
      std::string path = (*paths)[i]->GetString();
      int index = (*indices)[i]->GetInt();

      status = db_find_key(hDB, 0, path.c_str(), &hkey);
      if (status != DB_SUCCESS) {
         sresult->AddToArray(MJsonNode::MakeInt(status));
         continue;
      }

      status = db_reorder_key(hDB, hkey, index);

      sresult->AddToArray(MJsonNode::MakeInt(status));
   }

   return mjsonrpc_make_result("status", sresult);
}

static MJsonNode* js_cm_msg1(const MJsonNode* params)
{
   if (!params) {
      MJSO *doc = MJSO::I();
      doc->D("Generate a midas message using cm_msg1()");
      doc->P("facility?", MJSON_STRING, "message facility, default is \"midas\"");
      doc->P("user?", MJSON_STRING, "message user, default is \"javascript_commands\"");
      doc->P("type?", MJSON_INT, "message type, MT_xxx from midas.h, default is MT_INFO");
      doc->P("message", MJSON_STRING, "message text");
      doc->R("status", MJSON_INT, "return status of cm_msg1()");
      return doc;
   }

   MJsonNode* error = NULL;

   const char* facility = mjsonrpc_get_param(params, "facility", &error)->GetString().c_str();
   const char* user = mjsonrpc_get_param(params, "user", &error)->GetString().c_str();
   int type = mjsonrpc_get_param(params, "type", &error)->GetInt();
   const char* message = mjsonrpc_get_param(params, "message", &error)->GetString().c_str(); if (error) return error;

   if (strlen(facility)<1)
      facility = "midas";
   if (strlen(user)<1)
      user = "javascript_commands";
   if (type == 0)
      type = MT_INFO;

   int status = cm_msg1(type, __FILE__, __LINE__, facility, user, "%s", message);

   return mjsonrpc_make_result("status", MJsonNode::MakeInt(status));
}

static MJsonNode* js_al_reset_alarm(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("reset alarms");
      doc->P("alarms[]", MJSON_STRING, "array of alarm names");
      doc->R("status[]", MJSON_INT, "return status of al_reset_alarm() for each alarm");
      return doc;
   }

   MJsonNode* error = NULL;

   const MJsonNodeVector* alarms  = mjsonrpc_get_param_array(params, "alarms",  &error); if (error) return error;

   MJsonNode* sresult = MJsonNode::MakeArray();

   for (unsigned i=0; i<alarms->size(); i++) {
      int status = al_reset_alarm((*alarms)[i]->GetString().c_str());
      sresult->AddToArray(MJsonNode::MakeInt(status));
   }

   return mjsonrpc_make_result("status", sresult);
}

static MJsonNode* js_al_trigger_alarm(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("trigger an alarm");
      doc->P("name", MJSON_STRING, "alarm name");
      doc->P("message", MJSON_STRING, "alarm message");
      doc->P("class", MJSON_STRING, "alarm class");
      doc->P("condition", MJSON_STRING, "alarm condition");
      doc->P("type", MJSON_INT, "alarm type (AT_xxx)");
      doc->R("status", MJSON_INT, "return status of al_trigger_alarm()");
      return doc;
   }

   MJsonNode* error = NULL;

   const char* name = mjsonrpc_get_param(params, "name", &error)->GetString().c_str(); if (error) return error;
   const char* message = mjsonrpc_get_param(params, "message", &error)->GetString().c_str(); if (error) return error;
   const char* xclass = mjsonrpc_get_param(params, "class", &error)->GetString().c_str(); if (error) return error;
   const char* condition = mjsonrpc_get_param(params, "condition", &error)->GetString().c_str(); if (error) return error;
   int type = mjsonrpc_get_param(params, "type", &error)->GetInt(); if (error) return error;

   int status = al_trigger_alarm(name, message, xclass, condition, type);
   
   return mjsonrpc_make_result("status", MJsonNode::MakeInt(status));
}

static MJsonNode* js_al_trigger_class(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("trigger an alarm");
      doc->P("class", MJSON_STRING, "alarm class");
      doc->P("message", MJSON_STRING, "alarm message");
      doc->P("first?", MJSON_BOOL, "see al_trigger_class() in midas.c");
      doc->R("status", MJSON_INT, "return status of al_trigger_class()");
      return doc;
   }

   MJsonNode* error = NULL;

   const char* xclass = mjsonrpc_get_param(params, "class", &error)->GetString().c_str(); if (error) return error;
   const char* message = mjsonrpc_get_param(params, "message", &error)->GetString().c_str(); if (error) return error;
   bool first = mjsonrpc_get_param(params, "first", NULL)->GetBool();

   int status = al_trigger_class(xclass, message, first);

   return mjsonrpc_make_result("status", MJsonNode::MakeInt(status));
}

static MJsonNode* jrpc(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("make RPC call into frontend program via RPC_JRPC");
      doc->P("client_name", MJSON_STRING, "Connect to this MIDAS client, see cm_connect_client()");
      doc->P("cmd", MJSON_STRING, "Command passed to client");
      doc->P("args", MJSON_STRING, "Parameters passed to client as a string, could be JSON encoded");
      doc->P("max_reply_length?", MJSON_INT, "Optional maximum length of client reply. MIDAS RPC does not support returning strings of arbitrary length, maximum length has to be known ahead of time.");
      doc->R("reply", MJSON_STRING, "Reply from client as a string, could be JSON encoded");
      doc->R("status", MJSON_INT, "return status of cm_connect_client() and rpc_client_call()");
      return doc;
   }

   MJsonNode* error = NULL;

   std::string name   = mjsonrpc_get_param(params, "client_name", &error)->GetString(); if (error) return error;
   std::string cmd    = mjsonrpc_get_param(params, "cmd", &error)->GetString(); if (error) return error;
   std::string args   = mjsonrpc_get_param(params, "args", &error)->GetString(); if (error) return error;
   int max_reply_length = mjsonrpc_get_param(params, "max_reply_length", NULL)->GetInt();

   int status;

   int buf_length = 1024;

   if (max_reply_length > buf_length)
      buf_length = max_reply_length;

   char* buf = (char*)malloc(buf_length);
   buf[0] = 0;

   HNDLE hconn;

   status = cm_connect_client(name.c_str(), &hconn);

   if (status != RPC_SUCCESS) {
      return mjsonrpc_make_result("status", MJsonNode::MakeInt(status));
   }

   status = rpc_client_call(hconn, RPC_JRPC, cmd.c_str(), args.c_str(), buf, buf_length);

   if (status != RPC_SUCCESS) {
      free(buf);
      return mjsonrpc_make_result("status", MJsonNode::MakeInt(status));
   }

   MJsonNode* reply = MJsonNode::MakeString(buf);
   free(buf);

   // return status ignored on purpose.
   status = cm_disconnect_client(hconn, FALSE);
   
   return mjsonrpc_make_result("reply", reply, "status", MJsonNode::MakeInt(SUCCESS));
}

static MJsonNode* get_alarms(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("get alarm data");
      doc->P("get_all?", MJSON_BOOL, "get all alarms, even in alarm system not active and alarms not triggered");
      doc->R("status", MJSON_INT, "return status of midas library calls");
      doc->R("alarm_system_active", MJSON_BOOL, "value of ODB \"/Alarms/alarm system active\"");
      doc->R("alarms", MJSON_OBJECT, "alarm data, keyed by alarm name");
      doc->R("alarms[].triggered", MJSON_BOOL, "alarm is triggered");
      doc->R("alarms[].active", MJSON_BOOL, "alarm is enabled");
      doc->R("alarms[].class", MJSON_STRING, "alarm class");
      doc->R("alarms[].type", MJSON_INT, "alarm type AT_xxx");
      doc->R("alarms[].bgcolor", MJSON_STRING, "display background color");
      doc->R("alarms[].fgcolor", MJSON_STRING, "display foreground color");
      doc->R("alarms[].message", MJSON_STRING, "alarm ODB message field");
      doc->R("alarms[].condition", MJSON_STRING, "alarm ODB condition field");
      doc->R("alarms[].evaluated_value?", MJSON_STRING, "evaluated alarm condition (AT_EVALUATED alarms only)");
      doc->R("alarms[].periodic_next_time?", MJSON_STRING, "next time the periodic alarm will fire (AT_PERIODIC alarms only)");
      doc->R("alarms[].time_triggered_first", MJSON_STRING, "time when alarm was triggered");
      doc->R("alarms[].show_to_user", MJSON_STRING, "final alarm text shown to user by mhttpd");
      return doc;
   }

   //MJsonNode* error = NULL;

   bool get_all = mjsonrpc_get_param(params, "get_all", NULL)->GetBool();

   int status;
   HNDLE hDB;

   status = cm_get_experiment_database(&hDB, NULL);

   if (status != DB_SUCCESS) {
      return mjsonrpc_make_result("status", MJsonNode::MakeInt(status));
   }

   int flag;
   int size;
   int alarm_system_active = 0;

   /* check global alarm flag */
   flag = TRUE;
   size = sizeof(flag);
   status = db_get_value(hDB, 0, "/Alarms/Alarm System active", &flag, &size, TID_BOOL, TRUE);

   if (status != DB_SUCCESS) {
      return mjsonrpc_make_result("status", MJsonNode::MakeInt(status));
   }

   alarm_system_active = flag;

   if (!alarm_system_active)
      if (!get_all) {
         return mjsonrpc_make_result("status", MJsonNode::MakeInt(SUCCESS),
                                     "alarm_system_active", MJsonNode::MakeBool(alarm_system_active!=0),
                                     "alarms", MJsonNode::MakeObject());
      }

   /* go through all alarms */
   HNDLE hkey;
   status = db_find_key(hDB, 0, "/Alarms/Alarms", &hkey);

   if (status != DB_SUCCESS) {
      return mjsonrpc_make_result("status", MJsonNode::MakeInt(status));
   }

   MJsonNode* alarms = MJsonNode::MakeObject();

   for (int i = 0;; i++) {
      HNDLE hsubkey;
      KEY key;

      db_enum_link(hDB, hkey, i, &hsubkey);

      if (!hsubkey)
         break;

      status = db_get_key(hDB, hsubkey, &key);
         
      const char* name = key.name;

      flag = 0;
      size = sizeof(flag);
      status = db_get_value(hDB, hsubkey, "Triggered", &flag, &size, TID_INT, TRUE);

      // skip un-triggered alarms
      if (!flag)
         if (!get_all)
            continue;

      MJsonNode* a = MJsonNode::MakeObject();

      a->AddToObject("triggered", MJsonNode::MakeBool(flag!=0));

      flag = 1;
      size = sizeof(BOOL);
      status = db_get_value(hDB, hsubkey, "Active", &flag, &size, TID_BOOL, TRUE);

      a->AddToObject("active", MJsonNode::MakeBool(flag!=0));

      char alarm_class[NAME_LENGTH];
      strcpy(alarm_class, "Alarm");
      size = sizeof(alarm_class);
      status = db_get_value(hDB, hsubkey, "Alarm Class", alarm_class, &size, TID_STRING, TRUE);
         
      a->AddToObject("class", MJsonNode::MakeString(alarm_class));

      int atype = 0;
      size = sizeof(atype);
      status = db_get_value(hDB, hsubkey, "Type", &atype, &size, TID_INT, TRUE);
         
      a->AddToObject("type", MJsonNode::MakeInt(atype));

      char str[256];

      char bgcol[256];
      strcpy(bgcol, "red");
      sprintf(str, "/Alarms/Classes/%s/Display BGColor", alarm_class);
      size = sizeof(bgcol);
      status = db_get_value(hDB, 0, str, bgcol, &size, TID_STRING, TRUE);
         
      a->AddToObject("bgcolor", MJsonNode::MakeString(bgcol));

      char fgcol[256];
      strcpy(fgcol, "black");
      sprintf(str, "/Alarms/Classes/%s/Display FGColor", alarm_class);
      size = sizeof(fgcol);
      status = db_get_value(hDB, 0, str, fgcol, &size, TID_STRING, TRUE);
         
      a->AddToObject("fgcolor", MJsonNode::MakeString(fgcol));

      char msg[256];
      msg[0] = 0;
      size = sizeof(msg);
      status = db_get_value(hDB, hsubkey, "Alarm Message", msg, &size, TID_STRING, TRUE);
         
      a->AddToObject("message", MJsonNode::MakeString(msg));

      char cond[256];
      cond[0] = 0;
      size = sizeof(cond);
      status = db_get_value(hDB, hsubkey, "Condition", cond, &size, TID_STRING, TRUE);

      a->AddToObject("condition", MJsonNode::MakeString(cond));

      char show_to_user[256];
      
      if (atype == AT_EVALUATED) {
         char value_str[256];
         /* retrieve value */
         al_evaluate_condition(cond, value_str);
         // check for array overflow by at_evaluate_condition
         assert(strlen(value_str) + 1 < sizeof(value_str));

         sprintf(show_to_user, msg, value_str);
         // check for array overflow by sprintf()
         assert(strlen(show_to_user) + 1 < sizeof(show_to_user));

         a->AddToObject("evaluated_value", MJsonNode::MakeString(value_str));
      } else
         strlcpy(show_to_user, msg, sizeof(show_to_user));
      
      a->AddToObject("show_to_user", MJsonNode::MakeString(show_to_user));

      str[0] = 0;
      size = sizeof(str);
      status = db_get_value(hDB, hsubkey, "Time triggered first", str, &size, TID_STRING, TRUE);

      a->AddToObject("time_triggered_first", MJsonNode::MakeString(str));

      if (atype == AT_PERIODIC) {
         DWORD last = 0;
         size = sizeof(last);
         db_get_value(hDB, hsubkey, "Checked last", &last, &size, TID_DWORD, TRUE);

         if (last == 0) {
            last = ss_time();
            db_set_value(hDB, hsubkey, "Checked last", &last, size, 1, TID_DWORD);
         }

         int interval = 0;
         size = sizeof(interval);
         db_get_value(hDB, hsubkey, "Check interval", &interval, &size, TID_INT, TRUE);

         time_t tnext = last + interval;

         const char* snext = ctime(&tnext);

         if (!snext)
            snext = "<invalid time>";

         a->AddToObject("periodic_next_time", MJsonNode::MakeString(snext));
      }

      alarms->AddToObject(name, a);
   }

   return mjsonrpc_make_result("status", MJsonNode::MakeInt(SUCCESS),
                               "alarm_system_active", MJsonNode::MakeBool(alarm_system_active!=0),
                               "alarms", alarms);
}

static MJsonNode* get_debug(const MJsonNode* params)
{
   if (!params) {
      MJSO *doc = MJSO::I();
      doc->D("get current value of mjsonrpc_debug");
      doc->P(NULL, 0, "there are no input parameters");
      doc->R(NULL, MJSON_INT, "current value of mjsonrpc_debug");
      return doc;
   }

   return mjsonrpc_make_result("debug", MJsonNode::MakeInt(mjsonrpc_debug));
}

static MJsonNode* set_debug(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("set new value of mjsonrpc_debug");
      doc->P(NULL, MJSON_INT, "new value of mjsonrpc_debug");
      doc->R(NULL, MJSON_INT, "new value of mjsonrpc_debug");
      return doc;
   }

   mjsonrpc_debug = params->GetInt();
   return mjsonrpc_make_result("debug", MJsonNode::MakeInt(mjsonrpc_debug));
}

static MJsonNode* get_schema(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("Get the MIDAS JSON-RPC schema JSON object");
      doc->P(NULL, 0, "there are no input parameters");
      doc->R(NULL, MJSON_OBJECT, "returns the MIDAS JSON-RPC schema JSON object");
      return doc;
   }

   return mjsonrpc_make_result(mjsonrpc_get_schema());
}

typedef std::map<std::string, mjsonrpc_handler_t*> MethodHandlers;
typedef MethodHandlers::iterator MethodHandlersIterator;

static MethodHandlers gHandlers;

void mjsonrpc_add_handler(const char* method, mjsonrpc_handler_t* handler)
{
   gHandlers[method] = handler;
}

void mjsonrpc_init()
{
   if (mjsonrpc_debug) {
      printf("mjsonrpc_init!\n");
   }

   // system methods
   mjsonrpc_add_handler("null", xnull);
   mjsonrpc_add_handler("get_debug",   get_debug);
   mjsonrpc_add_handler("set_debug",   set_debug);
   mjsonrpc_add_handler("get_schema",  get_schema);
   // interface to alarm functions
   mjsonrpc_add_handler("al_reset_alarm",    js_al_reset_alarm);
   mjsonrpc_add_handler("al_trigger_alarm",  js_al_trigger_alarm);
   mjsonrpc_add_handler("al_trigger_class",  js_al_trigger_class);
   // interface to midas.c functions
   mjsonrpc_add_handler("cm_exist",    js_cm_exist);
   mjsonrpc_add_handler("cm_msg1",     js_cm_msg1);
   mjsonrpc_add_handler("cm_shutdown", js_cm_shutdown);
   // interface to odb functions
   mjsonrpc_add_handler("db_copy",     js_db_copy);
   mjsonrpc_add_handler("db_paste",    js_db_paste);
   mjsonrpc_add_handler("db_get_values", js_db_get_values);
   mjsonrpc_add_handler("db_ls",       js_db_ls);
   mjsonrpc_add_handler("db_create", js_db_create);
   mjsonrpc_add_handler("db_delete", js_db_delete);
   mjsonrpc_add_handler("db_resize", js_db_resize);
   mjsonrpc_add_handler("db_rename", js_db_rename);
   mjsonrpc_add_handler("db_link",   js_db_link);
   mjsonrpc_add_handler("db_reorder", js_db_reorder);
   mjsonrpc_add_handler("db_key",    js_db_key);
   // methods that perform computations or invoke actions
   mjsonrpc_add_handler("get_alarms",  get_alarms);
   //mjsonrpc_add_handler("get_messages",  get_messages);
   mjsonrpc_add_handler("jrpc",  jrpc);
   mjsonrpc_add_handler("start_program", start_program);

   mjsonrpc_user_init();
}

static MJsonNode* mjsonrpc_make_schema(MethodHandlers* h)
{
   MJsonNode* s = MJsonNode::MakeObject();

   s->AddToObject("$schema", MJsonNode::MakeString("http://json-schema.org/schema#"));
   s->AddToObject("id", MJsonNode::MakeString("MIDAS JSON-RPC autogenerated schema"));
   s->AddToObject("title", MJsonNode::MakeString("MIDAS JSON-RPC schema"));
   s->AddToObject("description", MJsonNode::MakeString("Autogenerated schema for all MIDAS JSON-RPC methods"));
   s->AddToObject("type", MJsonNode::MakeString("object"));

   MJsonNode* m = MJsonNode::MakeObject();

   for (MethodHandlersIterator iterator = h->begin(); iterator != h->end(); iterator++) {
      // iterator->first = key
      // iterator->second = value
      //printf("build schema for method \"%s\"!\n", iterator->first.c_str());
      MJsonNode* doc = iterator->second(NULL);
      if (doc == NULL)
         doc = MJsonNode::MakeObject();
      m->AddToObject(iterator->first.c_str(), doc);
   }

   s->AddToObject("properties", m);
   s->AddToObject("required", MJsonNode::MakeArray());

   return s;
}

MJsonNode* mjsonrpc_get_schema()
{
   return mjsonrpc_make_schema(&gHandlers);
}

static void mjsonrpc_print_schema()
{
   MJsonNode *s = mjsonrpc_get_schema();
   s->Dump(0);
   std::string str = s->Stringify(1);
   printf("MJSON-RPC schema:\n");
   printf("%s\n", str.c_str());
   delete s;
}

static std::string indent(int x, const char* p = " ")
{
   if (x<1)
      return "";
   std::string s;
   for (int i=0; i<x; i++)
      s += p;
   return s;
}

struct NestedLine {
   int nest;
   bool span;
   std::string text;
};

typedef std::vector<NestedLine> NestedText;

NestedText nested_output;

static void nested_clear()
{
   nested_output.clear();
}

static void output(int nest, bool span, std::string text)
{
   if (text.length() < 1)
      return;

   NestedLine l;
   l.nest = nest;
   l.span = span;
   l.text = text;
   nested_output.push_back(l);
};

static std::string nested_dump()
{
   std::string s;

   for (unsigned i=0; i<nested_output.size(); i++) {
      char buf[256];
      sprintf(buf, "%d", nested_output[i].nest);
      s += std::string(buf) + ": " + nested_output[i].text;
      if (nested_output[i].span)
         s += " ----> span to end";
      s += "\n";
   }

   return s;
}

static std::string nested_print()
{
   std::vector<int> tablen;
   std::vector<std::string> tab;
   std::vector<std::string> tabx;

   tablen.push_back(0);
   tab.push_back("");
   tabx.push_back("");

   std::string xtab = "";
   int maxlen = 0;
   for (int n=0; ; n++) {
      int len = -1;
      for (unsigned i=0; i<nested_output.size(); i++) {
         int nn = nested_output[i].nest;
         bool pp = nested_output[i].span;
         if (pp)
            continue;
         if (nn != n)
            continue;
         int l = nested_output[i].text.length();
         if (l>len)
            len = l;
      }
      //printf("nest %d len %d\n", n, len);
      if (len < 0)
         break; // nothing with this nest level
      tablen.push_back(len);
      tab.push_back(indent(len, " ") + " | ");
      xtab += indent(len, " ") + " | ";
      tabx.push_back(xtab);
      maxlen += 3+len;
   }

   std::string s;
   int nest = 0;

   for (unsigned i=0; i<nested_output.size(); i++) {
      int n = nested_output[i].nest;
      bool p = nested_output[i].span;

      std::string pad;

      if (!p) {
         int ipad = tablen[n+1] - nested_output[i].text.length();
         pad = indent(ipad, " ");
      }

      std::string hr = indent(maxlen-tabx[n].length(), "-");

      if (n > nest)
         s += std::string(" | ") + nested_output[i].text + pad;
      else if (n == nest) {
         s += "\n";
         if (n == 0 || n == 1)
            s += tabx[n] + hr + "\n";
         s += tabx[n] + nested_output[i].text + pad;
      } else {
         s += "\n";
         if (n == 0 || n == 1)
            s += tabx[n] + hr + "\n";
         s += tabx[n] + nested_output[i].text + pad;
      }

      nest = n;
   }

   return s;
}

std::string mjsonrpc_schema_to_html_anything(const MJsonNode* schema, int nest_level);

static std::string mjsonrpc_schema_to_html_object(const MJsonNode* schema, int nest_level)
{
   const MJsonNode* d = schema->FindObjectNode("description");
   std::string description;
   if (d)
      description = d->GetString();

   std::string xshort = "object";
   if (description.length() > 1)
      xshort += "</td><td>" + description;

   const MJsonNode* properties = schema->FindObjectNode("properties");

   const MJsonNodeVector* required_list = NULL;
   const MJsonNode* r = schema->FindObjectNode("required");
   if (r)
      required_list = r->GetArray();

   if (!properties) {
      output(nest_level, false, "object");
      output(nest_level+1, true, description);
      return xshort;
   }

   const MJsonStringVector *names = properties->GetObjectNames();
   const MJsonNodeVector *nodes = properties->GetObjectNodes();

   if (!names || !nodes) {
      output(nest_level, false, "object");
      output(nest_level+1, true, description);
      return xshort;
   }

   std::string nest = indent(nest_level * 4);

   std::string s;

   s += nest + "<table border=1>\n";

   if (description.length() > 1) {
      s += nest + "<tr>\n";
      s += nest + "  <td colspan=3>" + description + "</td>\n";
      s += nest + "</tr>\n";
   }

   output(nest_level, true, description);

   for (unsigned i=0; i<names->size(); i++) {
      std::string name = (*names)[i];
      const MJsonNode* node = (*nodes)[i];

      bool required = false;
      if (required_list)
         for (unsigned j=0; j<required_list->size(); j++)
            if ((*required_list)[j])
               if ((*required_list)[j]->GetString() == name) {
                  required = true;
                  break;
               }

      bool is_array = false;
      const MJsonNode* type = node->FindObjectNode("type");
      if (type && type->GetString() == "array")
         is_array = true;

      if (is_array)
         name += "[]";

      if (!required)
         name += "?";

      output(nest_level, false, name);

      s += nest + "<tr>\n";
      s += nest + "  <td>" + name + "</td>\n";
      s += nest + "  <td>";
      s += mjsonrpc_schema_to_html_anything(node, nest_level + 1);
      s += "</td>\n";
      s += nest + "</tr>\n";
   }

   s += nest + "</table>\n";

   return s;
}

static std::string mjsonrpc_schema_to_html_array(const MJsonNode* schema, int nest_level)
{
   const MJsonNode* d = schema->FindObjectNode("description");
   std::string description;
   if (d)
      description = d->GetString();

   std::string xshort = "array";
   if (description.length() > 1)
      xshort += "</td><td>" + description;

   const MJsonNode* items = schema->FindObjectNode("items");

   if (!items) {
      output(nest_level, false, "array");
      output(nest_level+1, true, description);
      return xshort;
   }

   const MJsonNodeVector *nodes = items->GetArray();

   if (!nodes) {
      output(nest_level, false, "array");
      output(nest_level+1, true, description);
      return xshort;
   }

   std::string nest = indent(nest_level * 4);

   std::string s;

   //s += "array</td><td>";

   s += nest + "<table border=1>\n";

   if (description.length() > 1) {
      s += nest + "<tr>\n";
      s += nest + "  <td>" + description + "</td>\n";
      s += nest + "</tr>\n";
   }

   output(nest_level, true, description);

   for (unsigned i=0; i<nodes->size(); i++) {
      output(nest_level, false, "array of");

      s += nest + "<tr>\n";
      s += nest + "  <td> array of " + mjsonrpc_schema_to_html_anything((*nodes)[i], nest_level + 1) + "</td>\n";
      s += nest + "</tr>\n";
   }

   s += nest + "</table>\n";

   return s;
}

std::string mjsonrpc_schema_to_html_anything(const MJsonNode* schema, int nest_level)
{
   std::string type;
   std::string description;
   //bool        optional = false;

   const MJsonNode* t = schema->FindObjectNode("type");
   if (t)
      type = t->GetString();
   else
      type = "any";

   const MJsonNode* d = schema->FindObjectNode("description");
   if (d)
      description = d->GetString();

   //const MJsonNode* o = schema->FindObjectNode("optional");
   //if (o)
   //   optional = o->GetBool();

   if (type == "object") {
      return mjsonrpc_schema_to_html_object(schema, nest_level);
   } else if (type == "array") {
      return mjsonrpc_schema_to_html_array(schema, nest_level);
   } else {
      //if (optional)
      //   output(nest_level, false, "?");
      //else
      //   output(nest_level, false, "!");
      output(nest_level, false, type);
      output(nest_level+1, true, description);
      if (description.length() > 1) {
         return (type + "</td><td>" + description);
      } else {
         return (type);
      }
   }
}

std::string mjsonrpc_schema_to_text(const MJsonNode* schema)
{
   std::string s;
   nested_clear();
   mjsonrpc_schema_to_html_anything(schema, 0);
   //s += "<pre>\n";
   //s += nested_dump();
   //s += "</pre>\n";
   s += nested_print();
   return s;
}

static void add(std::string* s, const char* text)
{
   assert(s != NULL);
   if (s->length() > 0)
      *s += ", ";
   *s += text;
}

std::string mjsonrpc_decode_post_data(const char* post_data)
{
   //printf("mjsonrpc call, data [%s]\n", post_data);
   MJsonNode *request = MJsonNode::Parse(post_data);

   assert(request != NULL); // Parse never returns NULL - either parsed data or an MJSON_ERROR node

   if (mjsonrpc_debug) {
      printf("mjsonrpc: request:\n");
      request->Dump();
      printf("\n");
   }

   if (request->GetType() == MJSON_ERROR) {
      std::string reply;
      reply += "{";
      reply += "\"jsonrpc\": \"2.0\",";
      reply += "\"error\":{";
      reply += "\"code\":-32700,";
      reply += "\"message\":\"Parse error\",";
      reply += "\"data\":\"" + MJsonNode::Encode(request->GetError().c_str()) + "\"";
      reply += "},";
      reply += "\"id\":null";
      reply += "}";

      if (request)
         delete request;

      if (mjsonrpc_debug) {
         printf("mjsonrpc: invalid json: reply:\n");
         printf("%s\n", reply.c_str());
         printf("\n");
      }

      return reply;
   }

   // find required request elements
   const MJsonNode* version = request->FindObjectNode("jsonrpc");
   const MJsonNode* method  = request->FindObjectNode("method");
   const MJsonNode* params  = request->FindObjectNode("params");
   const MJsonNode* id      = request->FindObjectNode("id");

   std::string bad = "";

   if (!version)
      add(&bad, "jsonrpc version is missing");
   if (!method)
      add(&bad, "method is missing");
   if (!params)
      add(&bad, "params is missing");
   if (!id)
      add(&bad, "id is missing");

   if (version&&version->GetType() != MJSON_STRING)
      add(&bad, "jsonrpc version is not a string");
   if (version&&version->GetString() != "2.0")
      add(&bad, "jsonrpc version is not 2.0");

   if (method&&method->GetType() != MJSON_STRING)
      add(&bad, "method is not a string");

   if (bad.length() > 0) {
      std::string reply;
      reply += "{";
      reply += "\"jsonrpc\": \"2.0\",";
      reply += "\"error\":{";
      reply += "\"code\":-32600,";
      reply += "\"message\":\"Invalid request\",";
      reply += "\"data\":\"" + MJsonNode::Encode(bad.c_str()) + "\"";
      reply += "},";
      if (id)
         reply += "\"id\":" + id->Stringify();
      else
         reply += "\"id\":null";
      reply += "}";

      if (request)
         delete request;

      if (mjsonrpc_debug) {
         printf("mjsonrpc: invalid request: reply:\n");
         printf("%s\n", reply.c_str());
         printf("\n");
      }

      return reply;
   }

   const char* m = method->GetString().c_str();

   const MJsonNode* result = NULL;

   // special built-in methods

   if (strcmp(m, "echo") == 0) {
      result = mjsonrpc_make_result(request);
      request = NULL; // request object is now owned by the result object
   } else if (strcmp(m, "error") == 0) {
      result = mjsonrpc_make_error(1, "test error", "test error");
   } else if (strcmp(m, "invalid_json") == 0) {
      if (request)
         delete request;
      if (mjsonrpc_debug) {
         printf("mjsonrpc: reply with invalid json\n");
      }
      return "this is invalid json data";
   } else if (strcmp(m, "test_nan_inf") == 0) {
      double one = 1;
      double zero = 0;
      double nan = zero/zero;
      double plusinf = one/zero;
      double minusinf = -one/zero;
      MJsonNode* n = MJsonNode::MakeArray();
      n->AddToArray(MJsonNode::MakeNumber(nan));
      n->AddToArray(MJsonNode::MakeNumber(plusinf));
      n->AddToArray(MJsonNode::MakeNumber(minusinf));
      result = mjsonrpc_make_result("test_nan_plusinf_minusinf", n);
   } else {
      std::string mm = m;
      mjsonrpc_handler_t *h = gHandlers[mm];
      if (h)
         result = (*h)(params);
      else
         result = mjsonrpc_make_error(-32601, "Method not found", (std::string("unknown method: ") + m).c_str());
   }

   if (mjsonrpc_debug) {
      printf("mjsonrpc: handler reply:\n");
      result->Dump();
      printf("\n");
   }

   const MJsonNode *nerror  = result->FindObjectNode("error");
   const MJsonNode *nresult = result->FindObjectNode("result");

   std::string reply;
   reply += "{";
   reply += "\"jsonrpc\": \"2.0\",";
   if (nerror) {
      reply += "\"error\":" + nerror->Stringify() + ",";
   } else if (nresult) {
      reply += "\"result\":" + nresult->Stringify() + ",";
   } else {
      nerror = mjsonrpc_make_error(-32603, "Internal error", "bad dispatcher reply: no result and no error");
      reply += "\"error\":" + nerror->Stringify() + ",";
      delete nerror;
      nerror = NULL;
   }
   if (id)
      reply += "\"id\":" + id->Stringify();
   else
      reply += "\"id\":null";
   reply += "}";

   if (request)
      delete request;

   if (result)
      delete result;

   return reply;
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */

