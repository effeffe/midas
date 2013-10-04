/********************************************************************\

  Name:         mjson.cxx
  Created by:   Konstantin Olchanski

  Contents:     JSON encoder and decoder

\********************************************************************/

#include <stdio.h>
#include <assert.h>

#include "mjson.h"

std::vector<std::string> MJsonNode::GetKeys(const MJsonNodeMap& map) /// helper: get array keys
{
   std::vector<std::string> v;
   for(MJsonNodeMap::const_iterator it = map.begin(); it != map.end(); ++it)
      v.push_back(it->first);
      return v;
}

MJsonNode::~MJsonNode() // dtor
{
   for (unsigned i=0; i<arrayvalue.size(); i++)
      delete arrayvalue[i];
   arrayvalue.clear();

   for (MJsonNodeMap::iterator iter = objectvalue.begin(); iter != objectvalue.end(); iter++)
      delete iter->second;
   objectvalue.clear();

   // poison deleted nodes
   type = MJSON_NONE;
}

MJsonNode* MJsonNode::Parse(const char* jsonstring)
{
   return NULL;
}

static char toHexChar(int c)
{
   assert(c>=0);
   assert(c<=15);
   if (c <= 9)
      return '0' + c;
   else
      return 'A' + c;
}

static std::string quote(const char* s)
{
   std::string v;
   while (*s) {
      switch (*s) {
      case '\"': v += "\\\""; s++; break;
      case '\\': v += "\\\\"; s++; break;
      //case '/': v += "\\/"; s++; break;
      case '\b': v += "\\b"; s++; break;
      case '\f': v += "\\f"; s++; break;
      case '\n': v += "\\n"; s++; break;
      case '\r': v += "\\r"; s++; break;
      case '\t': v += "\\t"; s++; break;
      default: {
         if (iscntrl(*s)) {
            v += "\\u";
            v += "0";
            v += "0";
            v += toHexChar(((*s)>>4) & 0xF);
            v += toHexChar(((*s)>>0) & 0xF);
            s++;
            break;
         } else {
            v += *s; s++;
            break;
         }
      }
      }
   }
   return v;
}
   
std::string MJsonNode::Stringify(int flags) const
{
   switch (type) {
   case MJSON_ARRAY: {
      std::string v;
      v += "[ ";
      for (unsigned i=0; i<arrayvalue.size(); i++) {
         if (i > 0)
            v += ", ";
         v += arrayvalue[i]->Stringify(flags);
      }
      v += " ]";
      return v;
   }
   case MJSON_OBJECT: {
      std::string v;
      v += "{ ";
      int i=0;
      for (MJsonNodeMap::const_iterator iter = objectvalue.begin(); iter != objectvalue.end(); iter++, i++) {
         if (i > 0)
            v += ", ";
         v += std::string("\"") + quote(iter->first.c_str()) + "\"";
         v += ": ";
         v += iter->second->Stringify(flags);
      }
      v += " }";
      return v;
   }
   case MJSON_STRING: {
      return std::string("\"") + quote(stringvalue.c_str()) + "\"";
   }
   case MJSON_INT: {
      char buf[256];
      sprintf(buf, "%d", intvalue);
      return buf;
   }
   case MJSON_NUMBER: {
      return "number";
   }
   case MJSON_BOOL:
      if (intvalue)
         return "true";
      else
         return "false";
   case MJSON_NULL:
      return "null";
   default:
      assert(!"should not come here");
      return ""; // NOT REACHED
   }
}
   
MJsonNode* MJsonNode::MakeArray()
{
   MJsonNode* n = new MJsonNode();
   n->type = MJSON_ARRAY;
   return n;
}

MJsonNode* MJsonNode::MakeObject()
{
   MJsonNode* n = new MJsonNode();
   n->type = MJSON_OBJECT;
   return n;
}

MJsonNode* MJsonNode::MakeString(const char* value)
{
   MJsonNode* n = new MJsonNode();
   n->type = MJSON_STRING;
   n->stringvalue = value;
   return n;
}

MJsonNode* MJsonNode::MakeInt(int value)
{
   MJsonNode* n = new MJsonNode();
   n->type = MJSON_INT;
   n->intvalue = value;
   n->numbervalue = value;
   return n;
}

MJsonNode* MJsonNode::MakeNumber(double value)
{
   MJsonNode* n = new MJsonNode();
   n->type = MJSON_NUMBER;
   n->numbervalue = value;
   return n;
}

MJsonNode* MJsonNode::MakeBool(bool value)
{
   MJsonNode* n = new MJsonNode();
   n->type = MJSON_BOOL;
   if (value)
      n->intvalue = 1;
   else
      n->intvalue = 0;
   return n;
}

MJsonNode* MJsonNode::MakeNull()
{
   MJsonNode* n = new MJsonNode();
   n->type = MJSON_NULL;
   return n;
}

void MJsonNode::AddToArray(MJsonNode* node)
{
   if (type == MJSON_ARRAY) {
      arrayvalue.push_back(node);
      return;
   }

   assert(!"not an array");
}

void MJsonNode::AddToObject(const char* name, MJsonNode* node) /// add node to an object
{
   if (type == MJSON_OBJECT) {
      objectvalue[name] = node;
      return;
   }

   assert(!"not an object");
}

int MJsonNode::GetType() const /// get node type: MJSON_xxx
{
   return type;
}

const MJsonNodeVector* MJsonNode::GetArray() const
{
   if (type == MJSON_ARRAY || type == MJSON_NULL)
      return &arrayvalue;
   else
      return NULL;
}

const MJsonNodeMap* MJsonNode::GetObject() const
{
   if (type == MJSON_OBJECT || type == MJSON_NULL)
      return &objectvalue;
   else
      return NULL;
}

std::string MJsonNode::GetString() const
{
   if (type == MJSON_STRING)
      return stringvalue;
   else
      return "";
}

int MJsonNode::GetInt() const
{
   if (type == MJSON_INT)
      return intvalue;
   else
      return 0;
}

double MJsonNode::GetNumber() const
{
   if (type == MJSON_INT)
      return intvalue;
   else if (type == MJSON_NUMBER)
      return numbervalue;
   else
      return 0;
}

bool MJsonNode::GetBool() const /// get boolean value, false if not a boolean or value is JSON "null"
{
   if (type == MJSON_BOOL)
      return (intvalue != 0);
   else
      return false;
}

MJsonNode::MJsonNode() // private constructor
{
   // C++ does not know how to initialize elemental types, we have to do it by hand:
   type = MJSON_NONE;
   intvalue = 0;
   numbervalue = 0;
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
