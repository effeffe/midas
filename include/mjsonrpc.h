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


MJsonNode* mjsonrpc_get_schema();
std::string mjsonrpc_schema_to_text(const MJsonNode *schema);

MJsonNode* mjsonrpc_make_error(int code, const char* message, const char* data);
MJsonNode* mjsonrpc_make_result(MJsonNode* node);
MJsonNode* mjsonrpc_make_result(const char* name, MJsonNode* value, const char* name2 = NULL, MJsonNode* value2 = NULL, const char* name3 = NULL, MJsonNode* value3 = NULL);
const MJsonNode* mjsonrpc_get_param(const MJsonNode* params, const char* name, MJsonNode** error);

std::string mjsonrpc_decode_post_data(const char* post_data);

///
/// MIDAS JSON Schema Objects for documenting RPC calls.
///
/// Typical use:
///
/// static MJsonNode* js_cm_exist(const MJsonNode* params)
/// {
///   if (!params) {
///      MJSO* doc = MJSO::I();
///      doc->D("calls MIDAS cm_exist() to check if given MIDAS program is running");
///      doc->P("name", MJSON_STRING, "name of the program, corresponding to ODB /Programs/name");
///      doc->P("unique?", MJSON_BOOL, "bUnique argument to cm_exist()");
///      doc->R("status", MJSON_INT, "return status of cm_exist()");
///      return doc;
///   }
///   ... rpc function body
/// }
///

class MJSO: public MJsonNode
{
protected:
   MJsonNode *properties;
   MJsonNode *required;
   MJsonNode *items;

   MJSO* params;
   MJSO* result;

protected:
   static MJSO* MakeObjectSchema(const char* description); // constructor for object schema
   static MJSO* MakeArraySchema(const char* description); // constructor for array schema
   void AddToSchema(MJsonNode* s, const char* name); // add subschema to this schema

public:
   static MJSO* I(); // create top level schema
   void D(const char* description); // add description
   MJSO* Params(); // create "params"
   MJSO* Result(); // create "result"
   MJSO* PA(const char* description); // ???
   MJSO* RA(const char* description); // ???
   void P(const char* name, int mjson_type, const char* description); // document a parameter
   void R(const char* name, int mjson_type, const char* description); // document a return value
   void Add(const char* name, int mjson_type, const char* description); // add object property or array item
   MJSO* AddObject(const char* name, const char* description); // add sub-object
   MJSO* AddArray(const char* name, const char* description); // add sub-array

protected:
   MJSO(); // ctor
};

#endif

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */

