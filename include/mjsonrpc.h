/********************************************************************\

  Name:         mjsonrpc.h
  Created by:   Konstantin Olchanski

  Contents:     MIDAS JSON-RPC library

\********************************************************************/

#ifndef MJSONRPC_INCLUDE
#define MJSONRPC_INCLUDE

#include "mjson.h"

typedef MJsonNode* (mjsonrpc_handler_t)(const MJsonNode* params);

extern int mjsonrpc_debug;

void mjsonrpc_init();
void mjsonrpc_user_init();
void mjsonrpc_add_handler(const char* method, mjsonrpc_handler_t *handler);

MJsonNode* mjsonrpc_make_error(int code, const char* message, const char* data);
MJsonNode* mjsonrpc_make_result(MJsonNode* node);
MJsonNode* mjsonrpc_make_result(const char* name, MJsonNode* value, const char* name2 = NULL, MJsonNode* value2 = NULL, const char* name3 = NULL, MJsonNode* value3 = NULL);
const MJsonNode* mjsonrpc_get_param(const MJsonNode* params, const char* name, MJsonNode** error);

std::string mjsonrpc_decode_post_data(const char* post_data);

#endif

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */

