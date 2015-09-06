/********************************************************************\

  Name:         mjsonrpc_handler.cxx
  Created by:   Konstantin Olchanski

  Contents:     handler of MIDAS standard JSON-RPC requests

\********************************************************************/

#include <stdio.h>
#include <assert.h>

#include "mjson.h"
#include "midas.h"

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

MJsonNode* mjsonrpc_make_result(const char* name, MJsonNode* value)
{
   MJsonNode* node = MJsonNode::MakeObject();
   node->AddToObject(name, value);

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
         *error = mjsonrpc_make_error(-1, "missing parameter", (std::string("missing parameter: ") + name).c_str());
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

   printf("cm_shutdown(%s,%d) -> %d\n", name, unique, status);

   return mjsonrpc_make_result("status", MJsonNode::MakeInt(status));
}

typedef MJsonNode* (handler_t)(const MJsonNode* params);

static struct {
   const char* method;
   handler_t*  handler;
} table[] = {
   { "null", null },
   { "cm_exist", js_cm_exist },
   { "cm_shutdown", js_cm_shutdown },
   { NULL, NULL } // mark end of the table
};

MJsonNode* mjsonrpc_dispatch(const char* method, const MJsonNode* params)
{
   for (int i=0; table[i].method != NULL; i++) {
      if (strcmp(method, table[i].method) == 0)
         return table[i].handler(params);
   }

   return NULL;
}

// http://www.simple-is-better.org/json-rpc/transport_http.html
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

std::string mjsonrpc_decode_post_data(const char* post_data)
{
   //printf("mjsonrpc call, data [%s]\n", post_data);
   MJsonNode *request = MJsonNode::Parse(post_data);

   assert(request != NULL); // Parse never returns NULL - either parsed data or an MJSON_ERROR node

   printf("mjsonrpc: request:\n");
   request->Dump();
   printf("\n");

   // find required request elements
   const MJsonNode* version = request->FindObjectNode("jsonrpc");
   const MJsonNode* method  = request->FindObjectNode("method");
   const MJsonNode* params  = request->FindObjectNode("params");
   const MJsonNode* id      = request->FindObjectNode("id");

   if (!version || !method || !params || !id || version->GetType() != MJSON_STRING || method->GetType() != MJSON_STRING) {
      std::string reply;
      reply += "{";
      reply += "\"jsonrpc\": \"2.0\",";
      reply += "\"error\":{";
      reply += "\"code\":-32600,";
      reply += "\"message\":\"malformed request\",";
      reply += "\"data\":\"something is missing\",";
      reply += "},";
      if (id)
         reply += "\"id\":" + id->Stringify();
      else
         reply += "\"id\":null";
      reply += "}";

      if (request)
         delete request;

      return reply;
   }

   const char* m = method->GetString().c_str();

   const MJsonNode* result = NULL;

   // special built-in methods

   if (strcmp(m, "echo") == 0) {
      result = mjsonrpc_make_result(request);
      request = NULL; // request object is now owned by the result object
   } else if (strcmp(m, "error") == 0) {
      result = mjsonrpc_make_error(-1, "test error", "test error");
   } else if (strcmp(m, "invalid_json") == 0) {
      if (request)
         delete request;
      return "this is invalid json data";
   }

   if (!result) {
      result = mjsonrpc_dispatch(m, params);
   }

   if (!result) {
      result = mjsonrpc_make_error(-32600, "unknown method", (std::string("unknown method [") + m + "]").c_str());
   }

   printf("mjsonrpc: dispatcher reply:\n");
   result->Dump();
   printf("\n");

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
      nerror = mjsonrpc_make_error(-1, "bad", "very bad");
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

