/********************************************************************\

  Name:         mjsonrpc_user.cxx
  Created by:   Konstantin Olchanski

  Contents:     handler of user-provided and experimental JSON-RPC requests

\********************************************************************/

#include <stdio.h>

#include "mjsonrpc.h"

//
// example 1: extract request parameters, return up to 3 results
//

static MJsonNode* user_example1(const MJsonNode* params)
{
   MJsonNode* error = NULL;

   const char* arg  = mjsonrpc_get_param(params, "arg", &error)->GetString().c_str(); if (error) return error;
   int optional_arg = mjsonrpc_get_param(params, "optional_arg", NULL)->GetInt();

   if (mjsonrpc_debug)
      printf("user_example1(%s,%d)\n", arg, optional_arg);

   return mjsonrpc_make_result("string", MJsonNode::MakeString(arg), "integer", MJsonNode::MakeInt(optional_arg));
}

//
// example 2: extract request parameters, return more than 3 results
//

static MJsonNode* user_example2(const MJsonNode* params)
{
   MJsonNode* error = NULL;

   const char* arg  = mjsonrpc_get_param(params, "arg", &error)->GetString().c_str(); if (error) return error;
   int optional_arg = mjsonrpc_get_param(params, "optional_arg", NULL)->GetInt();

   if (mjsonrpc_debug)
      printf("user_example1(%s,%d)\n", arg, optional_arg);

   MJsonNode* result = MJsonNode::MakeObject();

   result->AddToObject("string1", MJsonNode::MakeString(arg));
   result->AddToObject("string2", MJsonNode::MakeString("hello"));
   result->AddToObject("string3", MJsonNode::MakeString("world!"));
   result->AddToObject("value1", MJsonNode::MakeInt(optional_arg));
   result->AddToObject("value2", MJsonNode::MakeNumber(3.14));

   return mjsonrpc_make_result(result);
}

//
// example 3: return an error
//

static MJsonNode* user_example3(const MJsonNode* params)
{
   MJsonNode* error = NULL;

   int arg  = mjsonrpc_get_param(params, "arg", &error)->GetInt(); if (error) return error;

   if (mjsonrpc_debug)
      printf("user_example3(%d)\n", arg);

   if (arg)
      return mjsonrpc_make_result("status", MJsonNode::MakeInt(arg));
   else
      return mjsonrpc_make_error(15, "example error message", "example error data");
}

//
// to create your own rpc method handler, copy one of the examples here, register it in user_init below
//



//
// user_init function is called at startup time to register user rpc method handlers
//

void mjsonrpc_user_init()
{
   if (mjsonrpc_debug) {
      printf("mjsonrpc_user_init!\n");
   }

   // add user functions to the rpc list

   mjsonrpc_add_handler("user_example1", user_example1);
   mjsonrpc_add_handler("user_example2", user_example2);
   mjsonrpc_add_handler("user_example3", user_example3);
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */

