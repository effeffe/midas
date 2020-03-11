/********************************************************************\

  Name:         mvodb.cxx
  Created by:   K.Olchanski

  Contents:     common functions of MVOdb ODB interface

\********************************************************************/

#include <stdio.h> // sprintf(), fprintf()

#include "mvodb.h"

MVOdb::~MVOdb() // dtor
{
   // empty
}

static std::string toString(int value)
{
   char buf[256];
   sprintf(buf, "%d", value);
   return buf;
}

MVOdbError::MVOdbError()
{
   SetOk(this);
}

void SetOk(MVOdbError* error)
{
   if (error) {
      error->fError = false;
      error->fErrorString = "";
      error->fPath = "";
      error->fStatus = 0;
   }
}

void SetMidasStatus(MVOdbError* error, bool print, const std::string& path, const char* midas_func_name, int status)
{
   if (error) {
      error->fStatus = status;
      error->fPath = path;
      if (status == 1) {
         error->fError = false;
         error->fErrorString = "";
      } else {
         error->fError = true;
         error->fErrorString = "";
         error->fErrorString += "MIDAS ";
         error->fErrorString += midas_func_name;
         error->fErrorString += "()";
         error->fErrorString += " at ODB path \"";
         error->fErrorString += path;
         error->fErrorString += "\" returned status ";
         error->fErrorString += toString(status);
         if (print) {
            fprintf(stderr, "MVOdb::SetMidasStatus: %s\n", error->fErrorString.c_str());
         }
      }
   } else {
      if (print) {
         fprintf(stderr, "MVOdb::SetMidasStatus: Error: MIDAS %s() at ODB path \"%s\" returned status %d\n", midas_func_name, path.c_str(), status);
      }
   }
}

void SetError(MVOdbError* error, bool print, const std::string& path, const std::string& message)
{
   if (error) {
      error->fStatus = 0;
      error->fPath = path;
      error->fError = true;
      error->fErrorString = "";
      error->fErrorString += message;
      error->fErrorString += " at ODB path \"";
      error->fErrorString += path;
      error->fErrorString += "\"";
      if (print) {
         fprintf(stderr, "MVOdb::SetError: %s\n", error->fErrorString.c_str());
      }
   } else {
      if (print) {
         fprintf(stderr, "MVOdb::SetError: Error: %s at ODB path \"%s\"\n", message.c_str(), path.c_str());
      }
   }
}

MVOdb* MakeFileDumpOdb(const char* buf, int bufsize, MVOdbError* error)
{
   //printf("MakeFileDumpOdb: odb dump size %d, first char \'%c\'\n", bufsize, buf[0]);
   if (buf[0] == '[') {
      // ODB format
      char str[256];
      sprintf(str, "MakeFileDumpOdb: old ODB dump format is not supported, sorry");
      SetError(error, false, "buffer", str);
      return MakeNullOdb();
   } else if (buf[0] == '<') {
      // XML format
      return MakeXmlBufferOdb(buf, bufsize, error);
   } else if (buf[0] == '{') {
      // JSON format
      return MakeJsonBufferOdb(buf, bufsize, error);
   } else {
      // unknown format
      char str[256];
      sprintf(str, "MakeFileDumpOdb: unknown ODB dump format, first char is \'%c\' (%d), dump size %d", buf[0], buf[0], bufsize);
      SetError(error, false, "buffer", str);
      return MakeNullOdb();
   }
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
