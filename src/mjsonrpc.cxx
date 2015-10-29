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

struct MjsonrpcDoc
{
   MJsonNode* doc;
   MJsonNode* params;
   MJsonNode* result;

   MJsonNode* params_required;
   MJsonNode* result_required;

   MjsonrpcDoc() // ctor
   {
      doc = MJsonNode::MakeObject();
      params = NULL;
      result = NULL;

      params_required = NULL;
      result_required = NULL;
   }

   ~MjsonrpcDoc() // dtor
   {

   }

   void D(const char* description)
   {
      doc->AddToObject("description", MJsonNode::MakeString(description));
   }

   void P(const char* param_name, int mjson_type, const char* description)
   {
      bool optional = strchr(param_name, '?');
      bool array = strchr(param_name, '[');

      std::string name = param_name;

      if (name.length() < 1)
         name = "anything";

      if (!params)
         params = MJsonNode::MakeObject();

      MJsonNode* p = MJsonNode::MakeObject();
      p->AddToObject("description", MJsonNode::MakeString(description));
      if (mjson_type != 0)
         p->AddToObject("type", MJsonNode::MakeString(MJsonNode::TypeToString(mjson_type)));
      params->AddToObject(name.c_str(), p);

      if (optional)
         p->AddToObject("optional", MJsonNode::MakeBool(true));

      if (!optional) {
         if (!params_required)
            params_required = MJsonNode::MakeArray();
         params_required->AddToArray(MJsonNode::MakeString(name.c_str()));
      }
   }

   void R(const char* result_name, int mjson_type, const char* description)
   {
      bool optional = strchr(result_name, '?');
      bool array = strchr(result_name, '[');

      std::string name = result_name;

      if (name.length() < 1)
         name = "anything";

      if (!result)
         result = MJsonNode::MakeObject();

      MJsonNode* r = MJsonNode::MakeObject();
      r->AddToObject("description", MJsonNode::MakeString(description));
      if (mjson_type != 0)
         r->AddToObject("type", MJsonNode::MakeString(MJsonNode::TypeToString(mjson_type)));
      result->AddToObject(name.c_str(), r);

      if (optional)
         r->AddToObject("optional", MJsonNode::MakeBool(true));

      if (!optional) {
         if (!result_required)
            result_required = MJsonNode::MakeArray();
         result_required->AddToArray(MJsonNode::MakeString(name.c_str()));
      }
   }

   MJsonNode* X()
   {
      doc->AddToObject("type", MJsonNode::MakeString("object"));

      MJsonNode* prop = MJsonNode::MakeObject();

      if (params) {
         MJsonNode* p = MJsonNode::MakeObject();
         p->AddToObject("type", MJsonNode::MakeString("object"));
         p->AddToObject("properties", params);
         if (params_required)
            p->AddToObject("required", params_required);
         prop->AddToObject("params", p);
      }

      if (result) {
         MJsonNode* r = MJsonNode::MakeObject();
         r->AddToObject("type", MJsonNode::MakeString("object"));
         r->AddToObject("properties", result);
         if (result_required)
            r->AddToObject("required", result_required);
         prop->AddToObject("result", r);
      }

      doc->AddToObject("properties", prop);

      MJsonNode* req = MJsonNode::MakeArray();
      if (params_required)
         req->AddToArray(MJsonNode::MakeString("params"));
      if (result_required)
         req->AddToArray(MJsonNode::MakeString("result"));
      doc->AddToObject("required", req);
      return doc;
   }
};

static MJsonNode* xnull(const MJsonNode* params)
{
   if (!params) {
      MjsonrpcDoc doc;
      doc.D("RPC method always returns null");
      doc.P("", 0, "method parameters are ignored");
      doc.R("", MJSON_NULL, "always returns null");
      return doc.X();
   }

   return mjsonrpc_make_result(MJsonNode::MakeNull());
}

static MJsonNode* js_cm_exist(const MJsonNode* params)
{
   if (!params) {
      MjsonrpcDoc doc;
      doc.D("calls MIDAS cm_exist() to check if given MIDAS program is running");
      doc.P("name", MJSON_STRING, "name of the program, corresponding to ODB /Programs/name");
      doc.P("unique?", MJSON_BOOL, "bUnique argument to cm_exist()");
      doc.R("status", MJSON_NUMBER, "return status of cm_exist()");
      return doc.X();
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
      MjsonrpcDoc doc;
      doc.D("calls MIDAS cm_shutdown() to stop given MIDAS program");
      doc.P("name", MJSON_STRING, "name of the program, corresponding to ODB /Programs/name");
      doc.P("unique?", MJSON_BOOL, "bUnique argument to cm_shutdown()");
      doc.R("status", MJSON_NUMBER, "return status of cm_shutdown()");
      return doc.X();
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
      MjsonrpcDoc doc;
      doc.D("start MIDAS program defined in ODB /Programs/name");
      doc.P("name", MJSON_STRING, "name of the program, corresponding to ODB /Programs/name");
      doc.R("status", MJSON_NUMBER, "return status of ss_system()");
      return doc.X();
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

static MJsonNode* x_db_copy(const MJsonNode* params, bool values_flag)
{
   if (!params) {
      MjsonrpcDoc doc;
      if (values_flag)
         doc.D("get values of ODB data from given subtrees");
      else
         doc.D("get copies of given ODB subtrees in the \"save\" json encoding");
      doc.P("paths[]", MJSON_STRING, "array of ODB subtree paths");
      if (values_flag)
         doc.R("data[]", 0, "values of ODB data for each path, all key names are in lower case, all symlinks are followed");
      else
         doc.R("data[]", 0, "copy of ODB data for each path");
      doc.R("status[]", MJSON_NUMBER, "return status of db_copy_json() for each path");
      doc.R("last_written[]", MJSON_NUMBER, "last_written value of the ODB subtree for each path");
      return doc.X();
   }

   MJsonNode* error = NULL;

   const MJsonNodeVector* paths = mjsonrpc_get_param(params, "paths", &error)->GetArray(); if (error) return error;

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
         status = parse_array_index_list("js_db_copy", path.c_str(), &list);

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

         if (values_flag)
            status = db_copy_json_values(hDB, hkey, &buf, &bufsize, &end);
         else
            status = db_copy_json_save(hDB, hkey, &buf, &bufsize, &end);

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

   return mjsonrpc_make_result("data", dresult, "status", sresult, "last_written", lwresult);
}

static MJsonNode* js_db_copy(const MJsonNode* params)
{
   return x_db_copy(params, false);
}

static MJsonNode* js_db_get_values(const MJsonNode* params)
{
   return x_db_copy(params, true);
}

static MJsonNode* js_db_paste(const MJsonNode* params)
{
   MJsonNode* error = NULL;

   const MJsonNodeVector* paths  = mjsonrpc_get_param(params, "paths",  &error)->GetArray(); if (error) return error;
   const MJsonNodeVector* values = mjsonrpc_get_param(params, "values", &error)->GetArray(); if (error) return error;

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

         if (list.size() > 1) {
            if (v->GetType() != MJSON_ARRAY) {
               cm_msg(MERROR, "js_db_paste", "expected an array of values for array path \"%s\"", path.c_str());
               sresult->AddToArray(MJsonNode::MakeInt(DB_TYPE_MISMATCH));
               continue;
            }

            const MJsonNodeVector* vvalues = v->GetArray();

            MJsonNode *ssresult = MJsonNode::MakeArray();

            for (unsigned i=0; i<list.size(); i++) {
               const MJsonNode* vv = (*vvalues)[i];
               assert(vv != NULL);
               
               status = db_paste_json_node(hDB, hkey, list[i], vv);
               ssresult->AddToArray(MJsonNode::MakeInt(status));
            }
            
            sresult->AddToArray(ssresult);
         } else {
            status = db_paste_json_node(hDB, hkey, list[0], v);
            sresult->AddToArray(MJsonNode::MakeInt(status));
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
      MjsonrpcDoc doc;
      doc.D("get copies of given ODB subtrees in the \"save\" json encoding");
      doc.P("[]", 0, "array of ODB paths to be created");
      doc.P("[].path", MJSON_STRING, "ODB path");
      doc.P("[].type", MJSON_NUMBER, "MIDAS TID_xxx type");
      doc.P("[].array_length?", MJSON_NUMBER, "optional array length, default is 1");
      doc.P("[].string_length?", MJSON_NUMBER, "for TID_STRING, optional string length, default is NAME_LENGTH");
      doc.R("status[]", MJSON_NUMBER, "return status of db_create() for each path");
      return doc.X();
   }

   MJsonNode* sresult = MJsonNode::MakeArray();

   const MJsonNodeVector* pp = params->GetArray();

   HNDLE hDB;
   cm_get_experiment_database(&hDB, NULL);

   for (unsigned i=0; i<pp->size(); i++) {
      const MJsonNode* p = (*pp)[i];
      std::string path = mjsonrpc_get_param(p, "path", NULL)->GetString();
      int type = mjsonrpc_get_param(p, "type", NULL)->GetInt();
      int array_length = mjsonrpc_get_param(p, "array_length", NULL)->GetInt();
      int string_length = mjsonrpc_get_param(p, "string_length", NULL)->GetInt();

      printf("create odb [%s], type %d, array %d, string %d\n", path.c_str(), type, array_length, string_length);

      int status = DB_SUCCESS;

      sresult->AddToArray(MJsonNode::MakeInt(status));
   }

   return mjsonrpc_make_result("status", sresult);
}

static MJsonNode* get_debug(const MJsonNode* params)
{
   if (!params) {
      MjsonrpcDoc doc;
      doc.D("get current value of mjsonrpc_debug");
      doc.P("", 0, "there are no input parameters");
      doc.R("", MJSON_NUMBER, "current value of mjsonrpc_debug");
      return doc.X();
   }

   return mjsonrpc_make_result("debug", MJsonNode::MakeInt(mjsonrpc_debug));
}

static MJsonNode* set_debug(const MJsonNode* params)
{
   if (!params) {
      MjsonrpcDoc doc;
      doc.D("set new value of mjsonrpc_debug");
      doc.P("", MJSON_NUMBER, "new value of mjsonrpc_debug");
      doc.R("", MJSON_NUMBER, "new value of mjsonrpc_debug");
      return doc.X();
   }

   mjsonrpc_debug = params->GetInt();
   return mjsonrpc_make_result("debug", MJsonNode::MakeInt(mjsonrpc_debug));
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

   mjsonrpc_add_handler("null", xnull);
   mjsonrpc_add_handler("get_debug",   get_debug);
   mjsonrpc_add_handler("set_debug",   set_debug);
   mjsonrpc_add_handler("cm_exist",    js_cm_exist);
   mjsonrpc_add_handler("cm_shutdown", js_cm_shutdown);
   mjsonrpc_add_handler("db_copy",     js_db_copy);
   mjsonrpc_add_handler("db_paste",    js_db_paste);
   mjsonrpc_add_handler("db_get_values", js_db_get_values);
   mjsonrpc_add_handler("db_create", js_db_create);
   mjsonrpc_add_handler("start_program", start_program);
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

static void mjsonrpc_print_schema()
{
   MJsonNode *s = mjsonrpc_make_schema(&gHandlers);
   s->Dump(0);
   std::string str = s->Stringify(1);
   printf("MJSON-RPC schema:\n");
   printf("%s\n", str.c_str());
   delete s;
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
   static bool gFirstTime = true;
   if (gFirstTime) {
      gFirstTime = false;
      mjsonrpc_init();
      mjsonrpc_user_init();
      mjsonrpc_print_schema();
   }

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

