/********************************************************************\

  Name:         mjsonrpc.cxx
  Created by:   Konstantin Olchanski

  Contents:     handler of MIDAS standard JSON-RPC requests

\********************************************************************/

#include <stdio.h>
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

static MJsonNode* null(const MJsonNode* params)
{
   return mjsonrpc_make_result(MJsonNode::MakeNull());
}

static MJsonNode* js_cm_exist(const MJsonNode* params)
{
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

static MJsonNode* js_db_copy(const MJsonNode* params)
{
   MJsonNode* error = NULL;

   const MJsonNodeVector* paths = mjsonrpc_get_param(params, "paths", &error)->GetArray(); if (error) return error;

   MJsonNode* dresult = MJsonNode::MakeArray();
   MJsonNode* sresult = MJsonNode::MakeArray();

   HNDLE hDB;
   cm_get_experiment_database(&hDB, NULL);

   for (unsigned i=0; i<paths->size(); i++) {
      HNDLE hkey;
      int status = db_find_key(hDB, 0, (*paths)[i]->GetString().c_str(), &hkey);
      if (status == DB_SUCCESS) {
         char* buf = NULL;
         int bufsize = 0;
         int end = 0;
         status = db_copy_json(hDB, hkey, &buf, &bufsize, &end, 1, 1, 1);
         if (status == DB_SUCCESS) {
            dresult->AddToArray(MJsonNode::Parse(buf));
         }
         if (buf)
            free(buf);
      }
      if (status != DB_SUCCESS)
         dresult->AddToArray(MJsonNode::MakeNull());
      sresult->AddToArray(MJsonNode::MakeInt(status));
   }

   return mjsonrpc_make_result("data", dresult, "status", sresult);
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
      const char* s = (*paths)[i]->GetString().c_str();
      if (strchr(s, '[')) {
         // indexing of array elements not implemented
         cm_msg(MERROR, "js_db_paste", "indexing array element for \"%s\" is not implemented", s);
         status = DB_NO_KEY;
      } else {
         status = db_find_key(hDB, 0, s, &hkey);
         if (status == DB_SUCCESS) {
            const MJsonNode* v = (*values)[i];
            assert(v != NULL);
            int index = 0;
            status = db_paste_json_node(hDB, hkey, index, v);
         }
      }
      sresult->AddToArray(MJsonNode::MakeInt(status));
   }

   return mjsonrpc_make_result("status", sresult);
}

static MJsonNode* get_debug(const MJsonNode* params)
{
   return mjsonrpc_make_result("debug", MJsonNode::MakeInt(mjsonrpc_debug));
}

static MJsonNode* set_debug(const MJsonNode* params)
{
   mjsonrpc_debug = params->GetInt();
   return mjsonrpc_make_result("debug", MJsonNode::MakeInt(mjsonrpc_debug));
}

static std::map<std::string, mjsonrpc_handler_t*> gHandlers;

void mjsonrpc_add_handler(const char* method, mjsonrpc_handler_t* handler)
{
   gHandlers[method] = handler;
}

void mjsonrpc_init()
{
   if (mjsonrpc_debug) {
      printf("mjsonrpc_init!\n");
   }

   mjsonrpc_add_handler("null", null);
   mjsonrpc_add_handler("get_debug",   get_debug);
   mjsonrpc_add_handler("set_debug",   set_debug);
   mjsonrpc_add_handler("cm_exist",    js_cm_exist);
   mjsonrpc_add_handler("cm_shutdown", js_cm_shutdown);
   mjsonrpc_add_handler("db_copy",     js_db_copy);
   mjsonrpc_add_handler("db_paste",    js_db_paste);
   mjsonrpc_add_handler("start_program", start_program);
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

