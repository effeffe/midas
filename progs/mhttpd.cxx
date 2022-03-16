/********************************************************************\

  Name:         mhttpd.cxx
  Created by:   Stefan Ritt

  Contents:     Web server program for midas RPC calls

\********************************************************************/

#undef NDEBUG // midas required assert() to be always enabled

#include <stdio.h>
#include <math.h> // fabs()
#include <assert.h>
#include <algorithm> // std::sort()
#include <thread> // std::thread
#include <deque>  // std::deque
#include <mutex>  // std::mutex
#include <condition_variable>  // std::condition_variable
#include <atomic> // std::atomic<>

#include "midas.h"
#include "msystem.h"
#include "mxml.h"
#ifndef HAVE_STRLCPY
#include "strlcpy.h"
#endif

#include "mgd.h"
#include "history.h"

#ifdef HAVE_MSCB
#include "mscb.h"
#endif

//#define OLD_SEQUENCER 1

#ifdef OLD_SEQUENCER
#include "sequencer.h"
#endif

#include "mjsonrpc.h"
#include "mvodb.h"

#define STRLCPY(dst, src) strlcpy((dst), (src), sizeof(dst))
#define STRLCAT(dst, src) strlcat((dst), (src), sizeof(dst))

/* refresh times in seconds */
#define DEFAULT_REFRESH 60

#ifdef HAVE_MONGOOSE6
static MUTEX_T* request_mutex = NULL;
#endif

static std::mutex gMutex;
static MVOdb* gOdb = NULL;

// FIXME: what does "referer" do?!?
//char referer[256];

/*------------------------------------------------------------------*/

#define MAX_GROUPS    32
#define MAX_VARS     100

/*------------------------------------------------------------------*/

static std::string toString(int i)
{
   char buf[256];
   sprintf(buf, "%d", i);
   return buf;
}

/*------------------------------------------------------------------*/

class Attachment
{
public:
   char*  attachment_buffer[3];
   size_t attachment_size[3];
public:
   Attachment() // ctor
   {
      for (int i=0; i<3; i++) {
         attachment_buffer[i] = NULL;
         attachment_size[i] = 0;
      }
   }
   ~Attachment() // dtor
   {
      for (int i=0; i<3; i++) {
         clear(i);
      }
   }
   void clear(int i)
   {
      if (attachment_size[i]) {
         attachment_size[i] = 0;
         free(attachment_buffer[i]);
         attachment_buffer[i] = NULL;
      }
   }
};
static BOOL elog_mode = FALSE;
static BOOL history_mode = FALSE;
static BOOL verbose = FALSE;

// month name from midas.c
extern const char *mname[];

static const char default_type_list[20][NAME_LENGTH] = {
   "Routine",
   "Shift summary",
   "Minor error",
   "Severe error",
   "Fix",
   "Question",
   "Info",
   "Modification",
   "Reply",
   "Alarm",
   "Test",
   "Other"
};

static const char default_system_list[20][NAME_LENGTH] = {
   "General",
   "DAQ",
   "Detector",
   "Electronics",
   "Target",
   "Beamline"
};

struct MimetypeTableEntry {
   std::string ext;
   std::string mimetype;
};

const MimetypeTableEntry gMimetypeTable[] = {
   { ".HTML",  "text/html"  },
   { ".HTM",   "text/html"  },
   { ".CSS",   "text/css"   },
   { ".TXT",   "text/plain" },
   { ".ASC",   "text/plain" },
   
   { ".ICO",   "image/x-icon"  },
   { ".GIF",   "image/gif"     },
   { ".JPG",   "image/jpeg"    },
   { ".JPEG",  "image/jpeg"    },
   { ".PNG",   "image/png"     },
   { ".SVG",   "image/svg+xml" },
   { ".BMP",   "image/bmp"     },
                               
   { ".MP3",   "audio/mpeg"    },
   { ".OGG",   "audio/ogg"     },
   { ".MID",   "audio/midi"    },
   { ".WAV",   "audio/wav"     },

   { ".XML",   "application/xml"        },
   { ".JS",    "application/javascript" },
   { ".JSON",  "application/json"       },
   { ".PS",    "application/postscript" },
   { ".EPS",   "application/postscript" },
   { ".PDF",   "application/pdf"        },
   { ".ZIP",   "application/zip"        },
   { ".XLS",   "application/x-msexcel"  },
   { ".DOC",   "application/msword"     },
   { "", "" }
};

static MVOdb* gMimeTypesOdb = NULL;

static std::string GetMimetype(const std::string& ext)
{
   if (gMimeTypesOdb) {
      std::string mimetype;
      gMimeTypesOdb->RS(ext.c_str(), &mimetype);
      if (mimetype.length() > 0) {
         //printf("GetMimetype: %s -> %s from ODB\n", ext.c_str(), mimetype.c_str());
         return mimetype;
      }
   }

   for (int i=0; gMimetypeTable[i].ext[0]; i++) {
      if (ext == gMimetypeTable[i].ext) {
         //printf("GetMimetype: %s -> %s from built-in table\n", ext.c_str(), gMimetypeTable[i].mimetype.c_str());
         return gMimetypeTable[i].mimetype;
      }
   }

   //printf("GetMimetype: %s -> not found\n", ext.c_str());
   return "";
}

static void SaveMimetypes(MVOdb* odb)
{
   gMimeTypesOdb = odb;

   for (int i=0; gMimetypeTable[i].ext.length() > 0; i++) {
      std::string tmp = gMimetypeTable[i].mimetype;
      gMimeTypesOdb->RS(gMimetypeTable[i].ext.c_str(), &tmp, true);
   }
}

#define HTTP_ENCODING "UTF-8"

/*------------------------------------------------------------------*/

const unsigned char favicon_png[] = {
   0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
   0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
   0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10,
   0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x91, 0x68,
   0x36, 0x00, 0x00, 0x00, 0x07, 0x74, 0x49, 0x4D,
   0x45, 0x07, 0xD4, 0x0B, 0x1A, 0x08, 0x37, 0x07,
   0x0D, 0x7F, 0x16, 0x5C, 0x00, 0x00, 0x00, 0x09,
   0x70, 0x48, 0x59, 0x73, 0x00, 0x00, 0x2E, 0x23,
   0x00, 0x00, 0x2E, 0x23, 0x01, 0x78, 0xA5, 0x3F,
   0x76, 0x00, 0x00, 0x00, 0x04, 0x67, 0x41, 0x4D,
   0x41, 0x00, 0x00, 0xB1, 0x8F, 0x0B, 0xFC, 0x61,
   0x05, 0x00, 0x00, 0x01, 0x7D, 0x49, 0x44, 0x41,
   0x54, 0x78, 0xDA, 0x63, 0xFC, 0xFF, 0xFF, 0x3F,
   0x03, 0x29, 0x80, 0x09, 0xAB, 0xE8, 0xD2, 0x65,
   0x77, 0x36, 0x6F, 0x7E, 0x8A, 0x5D, 0xC7, 0x7F,
   0x0C, 0x30, 0x67, 0xEE, 0x0D, 0x56, 0xCE, 0xCD,
   0x5C, 0xBC, 0x3B, 0xB6, 0x6D, 0x7F, 0x81, 0x29,
   0xCB, 0x88, 0xE6, 0x24, 0x20, 0x57, 0x50, 0x7C,
   0xDD, 0xCF, 0x1F, 0x6C, 0x40, 0xCB, 0xB5, 0xB5,
   0x05, 0xCF, 0x1C, 0xB7, 0x42, 0xB3, 0x80, 0x05,
   0x8D, 0xCF, 0xC8, 0xC8, 0x58, 0x5A, 0x2A, 0xFB,
   0xF6, 0x4D, 0x37, 0x1B, 0xAB, 0xA0, 0xB4, 0x4C,
   0x0A, 0x51, 0x4E, 0x02, 0x82, 0x85, 0xCB, 0x12,
   0x0E, 0x1D, 0xAB, 0xC7, 0x2A, 0xC5, 0x82, 0x69,
   0xC4, 0xAF, 0x5F, 0x7F, 0x1E, 0x3F, 0xF8, 0xCD,
   0xCB, 0xF1, 0xF5, 0xEF, 0xDF, 0x7F, 0xCC, 0xCC,
   0x4C, 0x84, 0x6D, 0x98, 0x59, 0xD5, 0xEB, 0xCF,
   0xA5, 0x16, 0xC4, 0xAB, 0x71, 0x72, 0xCB, 0x21,
   0x4C, 0x59, 0x74, 0x03, 0x5E, 0x3F, 0x7F, 0xB3,
   0x6B, 0xD6, 0x22, 0x46, 0xA6, 0x7F, 0x0C, 0x0C,
   0x7F, 0xD7, 0x75, 0x4D, 0xFB, 0xF1, 0xFD, 0x27,
   0x81, 0x78, 0xB8, 0x7D, 0xE9, 0x0A, 0xCB, 0xFF,
   0xDF, 0x4C, 0x8C, 0x8C, 0x40, 0xF6, 0xAD, 0x4B,
   0x67, 0x1F, 0xDE, 0xBD, 0x8B, 0x45, 0x03, 0x3C,
   0x60, 0x8F, 0x9D, 0xD8, 0xB3, 0xEB, 0x74, 0xB5,
   0x90, 0x26, 0x07, 0x03, 0x48, 0xE4, 0x3F, 0x8F,
   0xF6, 0xFF, 0x1B, 0x0F, 0x9A, 0x1E, 0x3E, 0x3A,
   0xFB, 0xF3, 0xDB, 0x8F, 0xB7, 0x0F, 0x9E, 0x43,
   0x83, 0xF1, 0xCF, 0xDF, 0x3F, 0x8A, 0x29, 0xCE,
   0x3F, 0x7F, 0xFD, 0xFC, 0xCF, 0xF0, 0xDF, 0x98,
   0xE9, 0xB5, 0x8F, 0xBD, 0x8A, 0x3C, 0x6F, 0xEC,
   0xB9, 0x2D, 0x47, 0xFE, 0xFC, 0xFF, 0x6F, 0x16,
   0x6C, 0xF3, 0xEC, 0xD3, 0x1C, 0x2E, 0x96, 0xEF,
   0xBF, 0xAB, 0x7E, 0x32, 0x7D, 0xE2, 0x10, 0xCE,
   0x88, 0xF4, 0x69, 0x2B, 0x60, 0xFC, 0xF4, 0xF5,
   0x97, 0x78, 0x8A, 0x36, 0xD8, 0x44, 0x86, 0x18,
   0x0D, 0xD7, 0x29, 0x95, 0x13, 0xD8, 0xD9, 0x58,
   0xE1, 0x0E, 0xF8, 0xF1, 0xF3, 0xDB, 0xC6, 0xD6,
   0xEC, 0x5F, 0x53, 0x8E, 0xBF, 0xFE, 0xC3, 0x70,
   0x93, 0x8D, 0x6D, 0xDA, 0xCB, 0x0B, 0x4C, 0x3F,
   0xFF, 0xFC, 0xFA, 0xCF, 0x0C, 0xB4, 0x09, 0x84,
   0x54, 0xD5, 0x74, 0x91, 0x55, 0x03, 0x01, 0x07,
   0x3B, 0x97, 0x96, 0x6E, 0xC8, 0x17, 0xFE, 0x7F,
   0x4F, 0xF8, 0xFE, 0xBC, 0x95, 0x16, 0x60, 0x62,
   0x62, 0x64, 0xE1, 0xE6, 0x60, 0x73, 0xD1, 0xB2,
   0x7A, 0xFA, 0xE2, 0xF1, 0xDF, 0x3F, 0xFF, 0xC4,
   0x78, 0x44, 0x31, 0xA3, 0x45, 0x2B, 0xD0, 0xE3,
   0xF6, 0xD9, 0xE3, 0x2F, 0x2E, 0x9D, 0x29, 0xA9,
   0xAC, 0x07, 0xA6, 0x03, 0xF4, 0xB4, 0x44, 0x10,
   0x00, 0x00, 0x75, 0x65, 0x12, 0xB0, 0x49, 0xFF,
   0x3F, 0x68, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
   0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
};

const unsigned char favicon_ico[] = {
   0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x10, 0x10,
   0x10, 0x00, 0x01, 0x00, 0x04, 0x00, 0x28, 0x01,
   0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x28, 0x00,
   0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x20, 0x00,
   0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00,
   0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB4, 0x0F,
   0x0A, 0x00, 0x5C, 0x86, 0x4C, 0x00, 0x2F, 0x5E,
   0x1A, 0x00, 0xBF, 0xD3, 0xD7, 0x00, 0x29, 0x17,
   0x8D, 0x00, 0x50, 0xA7, 0xA4, 0x00, 0x59, 0x57,
   0x7F, 0x00, 0xC6, 0xA3, 0xAC, 0x00, 0xFC, 0xFE,
   0xFC, 0x00, 0x28, 0x12, 0x53, 0x00, 0x58, 0x7D,
   0x72, 0x00, 0xC4, 0x3A, 0x34, 0x00, 0x3C, 0x3D,
   0x69, 0x00, 0xC5, 0xB6, 0xB9, 0x00, 0x94, 0x92,
   0x87, 0x00, 0x7E, 0x7A, 0xAA, 0x00, 0x88, 0x88,
   0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x81, 0x22,
   0xD8, 0x88, 0x88, 0x88, 0xF6, 0xD8, 0x82, 0x22,
   0xE8, 0x88, 0x88, 0x8D, 0x44, 0x98, 0x82, 0x22,
   0xA8, 0x88, 0x88, 0x8F, 0x44, 0x48, 0x82, 0x22,
   0x25, 0x76, 0x67, 0x55, 0x44, 0xF8, 0x88, 0x88,
   0x3A, 0xC9, 0x9C, 0x53, 0x83, 0x88, 0x88, 0x88,
   0x8D, 0x99, 0x99, 0x38, 0x88, 0x88, 0x88, 0x88,
   0x88, 0x99, 0x9C, 0x88, 0x88, 0x88, 0x88, 0x88,
   0x88, 0xF9, 0x9D, 0x88, 0x88, 0x88, 0x88, 0x88,
   0x88, 0x8A, 0x58, 0x88, 0x88, 0x88, 0x88, 0x88,
   0x88, 0x85, 0xD8, 0x88, 0x88, 0x88, 0x88, 0x88,
   0x88, 0xEA, 0xAE, 0x88, 0x88, 0x88, 0x88, 0x88,
   0x88, 0x00, 0x0B, 0x88, 0x88, 0x88, 0x88, 0x88,
   0x88, 0x70, 0x0D, 0x88, 0x88, 0x88, 0x88, 0x88,
   0x88, 0x87, 0xD8, 0x88, 0x88, 0x88, 0x88, 0x88,
   0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*------------------------------------------------------------------*/

class Param;
class Return;
void show_hist_page(MVOdb* odb, Param* p, Return* r, const char *dec_path, char *buffer, int *buffer_size, int refresh);
int vaxis(gdImagePtr im, gdFont * font, int col, int gcol, int x1, int y1, int width,
          int minor, int major, int text, int label, int grid, double ymin, double ymax,
          BOOL logaxis);
void haxis(gdImagePtr im, gdFont * font, int col, int gcol, int x1, int y1, int width,
           int minor, int major, int text, int label, int grid, double xmin, double xmax);
void get_elog_url(char *url, int len);
void show_header(Return* r, const char *title, const char *method, const char *path, int refresh);
void show_navigation_bar(Return* r, const char *cur_page);

/*------------------------------------------------------------------*/

char *stristr(const char *str, const char *pattern)
{
   char c1, c2, *ps, *pp;

   if (str == NULL || pattern == NULL)
      return NULL;

   while (*str) {
      ps = (char *) str;
      pp = (char *) pattern;
      c1 = *ps;
      c2 = *pp;
      if (toupper(c1) == toupper(c2)) {
         while (*pp) {
            c1 = *ps;
            c2 = *pp;

            if (toupper(c1) != toupper(c2))
               break;

            ps++;
            pp++;
         }

         if (!*pp)
            return (char *) str;
      }
      str++;
   }

   return NULL;
}

/*------------------------------------------------------------------*/

static double GetTimeSec()
{
   struct timeval tv;
   gettimeofday(&tv, NULL);
   return tv.tv_sec*1.0 + tv.tv_usec/1000000.0;
}

/*------------------------------------------------------------------*/

class RequestTrace
{
public:
   double fTimeReceived; // time request received
   double fTimeLocked; // time lock is taken
   double fTimeUnlocked; // time lock is released
   double fTimeProcessed; // time processing of request is done
   double fTimeSent; // time sending the request is done
   bool fCompleted; // flag the request completed, it's RequestTrace is safe to delete
   std::string fMethod; // request HTTP method
   std::string fUri; // request URL/URI
   std::string fQuery; // request query string
   std::string fRPC; // request RPC
   std::string fResource; // request file resource, with full path
   bool fAuthOk; // password check passed

public:
   RequestTrace() // ctor
   {
      fTimeReceived = 0;
      fTimeLocked = 0;
      fTimeUnlocked = 0;
      fTimeProcessed = 0;
      fTimeSent = 0;
      fCompleted = false;
      fAuthOk = false;
   }

   void PrintTrace0() const
   {
      printf("%.3f ", fTimeReceived);
      printf("%.3f ", fTimeLocked-fTimeReceived);
      printf("%.3f ", fTimeUnlocked-fTimeLocked);
      printf("%.3f ", fTimeProcessed-fTimeUnlocked);
      printf("%.3f ", fTimeSent-fTimeProcessed);
      printf("A ");
      printf("%d ", fAuthOk);
      printf("T ");
      printf("%.3f ", fTimeSent-fTimeReceived);
      printf("%.3f ", fTimeLocked-fTimeReceived);
      printf("%.3f ", fTimeProcessed-fTimeLocked);
      printf("M %s ", fMethod.c_str());
      printf("URL %s ", fUri.c_str());
      if (fRPC.length() > 0) {
         printf("RPC %s ", fRPC.c_str());
      }
      printf("\n");
   };
};

static int http_trace = 0;

class RequestTraceBuf
{
public:
   MUTEX_T* fMutex;
   std::vector<RequestTrace*> fBuf;

public:
   RequestTraceBuf() // ctor
   {
      int status;
      status = ss_mutex_create(&fMutex, FALSE);
      assert(status==SS_SUCCESS || status==SS_CREATED);
   }

   //RequestTrace* NewTrace() // RequestTrace factory
     //{
     // RequestTrace* t = new RequestTrace;
     // fBuf.push_back(t);
     // return t;
   //}

   void AddTrace(RequestTrace* t)
   {
      fBuf.push_back(t);
   }

   void AddTraceMTS(RequestTrace* t)
   {
      ss_mutex_wait_for(fMutex, 0);
      if (http_trace) {
         t->PrintTrace0();
      }
      delete t;
      //AddTrace(t);
      ss_mutex_release(fMutex);
   }

   void Clear() // clear all completed requests
   {
      // delete all completed requests
      for (unsigned i=0; i<fBuf.size(); i++) {
         if (fBuf[i] && fBuf[i]->fCompleted) {
            delete fBuf[i];
            fBuf[i] = NULL;
         }
      }

      // compact all non-completed requests
      unsigned k = 0;
      for (unsigned i=0; i<fBuf.size(); i++) {
         if (fBuf[i]) {
            if (fBuf[k] != NULL) {
               for (; k<i; k++) {
                  if (fBuf[k] == NULL) {
                     break;
                  }
               }
            }
            // if we found an empty spot between "k" and "i" move "i" there
            // if there is no empty spot, then "i" does not need to be moved
            if (fBuf[k] == NULL) {
               fBuf[k] = fBuf[i];
               fBuf[i] = NULL;
            }
         }
      }
   }
};

static RequestTraceBuf* gTraceBuf = NULL;

/*------------------------------------------------------------------*/

/* size of buffer for incoming data, must fit sum of all attachments */
#define WEB_BUFFER_SIZE (6*1024*1024)

class Return
{
public:

   size_t return_size;
   char *return_buffer;

   int strlen_retbuf;
   int return_length;

public:
   Return() // ctor
   {
      return_size = WEB_BUFFER_SIZE;
      return_buffer = (char*)malloc(return_size);
      assert(return_buffer != NULL);

      strlen_retbuf = 0;
      return_length = 0;
   }

   ~Return() // dtor
   {
      if (return_buffer)
         free(return_buffer);
      return_buffer = NULL;
      return_size = 0;
      strlen_retbuf = 0;
      return_length = 0;
   }

   void reset()
   {
      strlen_retbuf = 0;
   }

   void zero()
   {
      memset(return_buffer, 0, return_size);
      strlen_retbuf = 0;
      return_length = 0;
   }

int return_grow(size_t len)
{
   //printf("size %d, grow %d, room %d\n", return_size, len, return_size - strlen_retbuf);

   for (int i=0; i<1000; i++) { // infinite loop with protection against infinite looping
      if (strlen_retbuf + len < return_size-40)
         return SUCCESS;

      return_size *= 2;
      return_buffer = (char*)realloc(return_buffer, return_size);

      assert(return_buffer);

      //printf("new size %d\n", return_size);
   }

   assert(!"Cannot happen!"); // does not return
   return 0;
}

/*------------------------------------------------------------------*/

void rmemcpy(const void *buf, int len)
{
   return_grow(len);
   memcpy(return_buffer + strlen_retbuf, buf, len);
   strlen_retbuf += len;
   return_length = strlen_retbuf;
}

/*------------------------------------------------------------------*/

void rread(const char* filename, int fh, int len)
{
   return_grow(len);
   int rd = read(fh, return_buffer + strlen_retbuf, len);
   if (rd != len) {
      cm_msg(MERROR, "rread", "Cannot read file \'%s\', read of %d returned %d, errno %d (%s)", filename, len, rd, errno, strerror(errno));
      memset(return_buffer + strlen_retbuf, 0, len);
   }
   strlen_retbuf += len;
   return_length = strlen_retbuf;
}

/*------------------------------------------------------------------*/

void rsputs(const char *str)
{
   size_t len = strlen(str);

   return_grow(len);

   if (strlen_retbuf + len > return_size-40) {
      strcpy(return_buffer, "<H1>Error: return buffer too small</H1>");
      strlen_retbuf = strlen(return_buffer);
   } else {
      strcpy(return_buffer + strlen_retbuf, str);
      strlen_retbuf += len;
   }

   return_length = strlen_retbuf;
}

/*------------------------------------------------------------------*/

void rsputs2(const char *str)
{
   size_t len = strlen(str);

   return_grow(len);

   if (strlen_retbuf + len > return_size) {
      strlcpy(return_buffer, "<H1>Error: return buffer too small</H1>", return_size);
      strlen_retbuf = strlen(return_buffer);
   } else {
      int j = strlen_retbuf;
      for (size_t i = 0; i < len; i++) {
         if (strncmp(str + i, "http://", 7) == 0) {
            int k;
            char link[256];
            char* p = (char *) (str + i + 7);

            i += 7;
            for (k = 0; *p && *p != ' ' && *p != '\n'; k++, i++)
               link[k] = *p++;
            link[k] = 0;

            sprintf(return_buffer + j, "<a href=\"http://%s\">http://%s</a>", link, link);
            j += strlen(return_buffer + j);
         } else
            switch (str[i]) {
            case '<':
               strlcat(return_buffer, "&lt;", return_size);
               j += 4;
               break;
            case '>':
               strlcat(return_buffer, "&gt;", return_size);
               j += 4;
               break;
            default:
               return_buffer[j++] = str[i];
            }
      }

      return_buffer[j] = 0;
      strlen_retbuf = j;
   }

   return_length = strlen_retbuf;
}

/*------------------------------------------------------------------*/

void rsprintf(const char *format, ...) MATTRPRINTF(2,3)
{
   va_list argptr;
   char str[10000];

   va_start(argptr, format);
   vsprintf(str, (char *) format, argptr);
   va_end(argptr);

   // catch array overrun. better too late than never...
   assert(strlen(str) < sizeof(str));

   return_grow(strlen(str));

   if (strlen_retbuf + strlen(str) > return_size)
      strcpy(return_buffer, "<H1>Error: return buffer too small</H1>");
   else
      strcpy(return_buffer + strlen_retbuf, str);

   strlen_retbuf += strlen(str);
   return_length = strlen_retbuf;
}
};

/*------------------------------------------------------------------*/

/* Parameter handling functions similar to setenv/getenv */

#define MAX_PARAM    500
#define PARAM_LENGTH 256
#define TEXT_SIZE  50000

class Param
{
public:
   char _param[MAX_PARAM][PARAM_LENGTH];
   char *_value[MAX_PARAM];
   char _text[TEXT_SIZE];

public:
   Param() // ctor
   {
      initparam();
   }

   ~Param() // dtor
   {
      freeparam();
   }

   void initparam()
   {
      memset(_param, 0, sizeof(_param));
      memset(_value, 0, sizeof(_value));
      _text[0] = 0;
   }

   void setparam(const char *param, const char *value)
   {
      int i;

      if (equal_ustring(param, "text")) {
         if (strlen(value) >= TEXT_SIZE)
            printf("Error: parameter value too big\n");

         strlcpy(_text, value, TEXT_SIZE);
         _text[TEXT_SIZE - 1] = 0;
         return;
      }

      for (i = 0; i < MAX_PARAM; i++)
         if (_param[i][0] == 0)
            break;

      if (i < MAX_PARAM) {
         strlcpy(_param[i], param, PARAM_LENGTH);

         int size = strlen(value)+1;
         _value[i] = (char*)malloc(size);
         strlcpy(_value[i], value, size);
         _value[i][strlen(value)] = 0;

      } else {
         printf("Error: parameter array too small\n");
      }
   }

   void freeparam()
   {
      int i;

      for (i=0 ; i<MAX_PARAM ; i++)
         if (_value[i] != NULL) {
            free(_value[i]);
            _value[i] = NULL;
         }
   }

   void printparam()
   {
      int i;

      for (i = 0; i < MAX_PARAM && _param[i][0]; i++) {
         printf("param %d name [%s] value [%s]\n", i, _param[i], _value[i]);;
      }
   }

   const char *getparam(const char *param)
   {
      int i;

      if (equal_ustring(param, "text"))
         return _text;

      for (i = 0; i < MAX_PARAM && _param[i][0]; i++)
         if (equal_ustring(param, _param[i]))
            break;

      if (i == MAX_PARAM)
         return NULL;

      if (_value[i] == NULL)
         return "";

      return _value[i];
   }

   std::string xgetparam(const char *param)
   {
      const char* s = getparam(param);
      if (s)
         return s;
      else
         return "";
   }

   BOOL isparam(const char *param)
   {
      int i;

      for (i = 0; i < MAX_PARAM && _param[i][0]; i++)
         if (equal_ustring(param, _param[i]))
            break;

      if (i < MAX_PARAM && _param[i][0])
         return TRUE;

      return FALSE;
   }

   void unsetparam(const char *param)
   {
      int i;

      for (i = 0; i < MAX_PARAM; i++)
         if (equal_ustring(param, _param[i]))
            break;

      if (i < MAX_PARAM) {
         _param[i][0] = 0;
         _value[i][0] = 0;
      }
   }
};

/*------------------------------------------------------------------*/

const char *mhttpd_revision(void)
{
   return cm_get_revision();
}

/*------------------------------------------------------------------*/

static std::string UrlDecode(const char* p)
/********************************************************************\
   Decode the given string in-place by expanding %XX escapes
\********************************************************************/
{
   std::string s;

   //printf("URL decode: [%s] --> ", p);

   while (*p) {
      if (*p == '%') {
         /* Escape: next 2 chars are hex representation of the actual character */
         p++;
         if (isxdigit(p[0]) && isxdigit(p[1])) {
            int i = 0;
            char str[3];
            str[0] = p[0];
            str[1] = p[1];
            str[2] = 0;
            sscanf(str, "%02X", &i);

            s += (char) i;
            p += 2;
         } else
            s += '%';
      } else if (*p == '+') {
         /* convert '+' to ' ' */
         s += ' ';
         p++;
      } else {
         s += *p++;
      }
   }

   //printf("[%s]\n", s.c_str());

   return s;
}

static void urlDecode(char *p)
/********************************************************************\
   Decode the given string in-place by expanding %XX escapes
\********************************************************************/
{
   //char *px = p;
   char *pD, str[3];
   int i;

   //printf("URL decode: [%s] --> ", p);

   pD = p;
   while (*p) {
      if (*p == '%') {
         /* Escape: next 2 chars are hex representation of the actual character */
         p++;
         if (isxdigit(p[0]) && isxdigit(p[1])) {
            str[0] = p[0];
            str[1] = p[1];
            str[2] = 0;
            sscanf(str, "%02X", &i);

            *pD++ = (char) i;
            p += 2;
         } else
            *pD++ = '%';
      } else if (*p == '+') {
         /* convert '+' to ' ' */
         *pD++ = ' ';
         p++;
      } else {
         *pD++ = *p++;
      }
   }
   *pD = '\0';

   //printf("[%s]\n", px);
}

static void urlEncode(char *ps, int ps_size)
/*
   Encode mhttpd ODB path for embedding into HTML <a href="?cmd=odb&odb_path=xxx"> elements.
   Encoding is intended to be compatible with RFC 3986 section 2 (adding of %XX escapes)
*/
{
   char *pd, *p;
   int len = strlen(ps);
   char *str = (char*)malloc(len*3 + 10); // at worst, each input character is expanded into 3 output characters

   pd = str;
   p = ps;
   while (*p) {
      if (isalnum(*p)) {
         *pd++ = *p++;
      } else {
         sprintf(pd, "%%%02X", (*p)&0xFF);
         pd += 3;
         p++;
      }
   }
   *pd = '\0';

   if (/* DISABLES CODE */ (0)) {
      printf("urlEncode [");
      for (p=ps; *p!=0; p++)
         printf("0x%02x ", (*p)&0xFF);
      printf("]\n");

      printf("urlEncode [%s] -> [%s]\n", ps, str);
   }

   strlcpy(ps, str, ps_size);
   free(str);
}

static std::string urlEncode(const char *text)
/*
   Encode mhttpd ODB path for embedding into HTML <a href="?cmd=odb&odb_path=xxx"> elements.
   Encoding is intended to be compatible with RFC 3986 section 2 (adding of %XX escapes)
*/
{
   std::string encoded;

   const char* p = text;
   while (*p) {
      if (isalnum(*p)) {
         encoded += *p++;
      } else {
         char buf[16];
         sprintf(buf, "%%%02X", (*p)&0xFF);
         encoded += buf;
         p++;
      }
   }

   if (/* DISABLES CODE */ (0)) {
      printf("urlEncode [");
      for (p=text; *p!=0; p++)
         printf("0x%02x ", (*p)&0xFF);
      printf("]\n");

      printf("urlEncode [%s] -> [%s]\n", text, encoded.c_str());
   }

   return encoded;
}

/*------------------------------------------------------------------*/

std::vector<std::string> get_resource_paths()
{
   HNDLE hDB;
   int status;

   cm_get_experiment_database(&hDB, NULL);

   std::vector<std::string> paths;

   // add /Experiment/Resources
   std::string buf;
   status = db_get_value_string(hDB, 0, "/Experiment/Resources", 0, &buf, TRUE);
   if (status == DB_SUCCESS && buf.length() > 0)
      paths.push_back(buf);

   // add  "/Logger/History/IMAGE/History dir"
   paths.push_back(cm_get_history_path("IMAGE"));

   char* s = getcwd(NULL, 0);
   assert(s);
   std::string cwd = s;
   free(s);
   if (!cwd.empty()) {
      paths.push_back(cwd + "/");
      paths.push_back(cwd + "/resources/");
   }
   paths.push_back(cm_get_path());
   paths.push_back(cm_get_path() + "resources/");
   char *m = getenv("MIDASSYS");
   if (m) {
      paths.push_back(std::string(m) + "/resources/");
   }

   return paths;
}

/*------------------------------------------------------------------*/

bool open_resource_file(const char *filename, std::string* ppath, FILE** pfp)
{
   // resource file names should not start with a directory separator "/"
   // or contain ".." as this will allow them to escape the mhttpd filename "jail"
   // by asking file files names like "../../etc/passwd", etc.

   if (strlen(filename) < 1) {
      cm_msg(MERROR, "open_resource_file", "Invalid resource file name \'%s\' is too short",
              filename);
      return false;
   }

   if (filename[0] == DIR_SEPARATOR) {
      cm_msg(MERROR, "open_resource_file", "Invalid resource file name \'%s\' starting with \'%c\' which is not allowed",
              filename, DIR_SEPARATOR);
      return false;
   }

   if (strstr(filename, "..") != NULL) {
      cm_msg(MERROR, "open_resource_file", "Invalid resource file name \'%s\' containing \'..\' which is not allowed",
             filename);
      return false;
   }

   std::vector<std::string> paths = get_resource_paths();

   std::vector<std::string> paths_not_found;

   for (unsigned i=0; i<paths.size(); i++) {
      std::string path = paths[i];
      if (path.length() < 1)
         continue;
      if (path[0] == '#')
         continue;

      // expand env.variables before we add the filename.
      // the filename comes from the URL and if the URL
      // has '$' characters we will try to expand them
      // as an env.variable and maybe escape the file jail.

      std::string xpath = cm_expand_env(path.c_str());

      if (xpath[xpath.length()-1] != DIR_SEPARATOR)
         xpath += DIR_SEPARATOR_STR;
      xpath += filename;

      //printf("path [%s] [%s] [%s]\n", paths[i].c_str(), path.c_str(), xpath.c_str());

      FILE* fp = fopen(xpath.c_str(), "r");
      if (fp) {
         struct stat statbuf;
         int status = fstat(fileno(fp), &statbuf);
         if (status != 0) {
            cm_msg(MERROR, "open_resource_file", "Cannot fstat() file \'%s\', error %d (%s)", xpath.c_str(), errno, strerror(errno));
            fclose(fp);
            fp = NULL;
         }

         if (statbuf.st_mode & S_IFREG) {
            // good, normal file
            //printf("%s: regular!\n", xpath.c_str());
         //} else if (statbuf.st_mode & S_IFLNK) {
            // symlink
            //printf("%s: symlink!\n", xpath.c_str());
         } else if (statbuf.st_mode & S_IFDIR) {
            cm_msg(MERROR, "open_resource_file", "File \'%s\' for resource \'%s\' is a directory", xpath.c_str(), filename);
            fclose(fp);
            fp = NULL;
         } else {
            cm_msg(MERROR, "open_resource_file", "File \'%s\' for resource \'%s\' is not a regular file, st_mode is 0x%08x", xpath.c_str(), filename, statbuf.st_mode);
            fclose(fp);
            fp = NULL;
         }

         if (fp) {
            if (ppath)
               *ppath = xpath;
            if (pfp) {
               *pfp = fp;
            } else {
               fclose(fp);
               fp = NULL;
            }
            //cm_msg(MINFO, "open_resource_file", "Resource file \'%s\' is \'%s\'", filename, xpath.c_str());
            return true;
         }
      }

      paths_not_found.push_back(xpath);
   }

   std::string s;
   for (unsigned i=0; i<paths_not_found.size(); i++) {
      if (i>0)
         s += ", ";
      s += paths_not_found[i];
   }

   cm_msg(MERROR, "open_resource_file", "Cannot find resource file \'%s\', tried %s", filename, s.c_str());
   return false;
}

/*------------------------------------------------------------------*/

std::string get_content_type(const char* filename)
{
   std::string ext_upper;
   const char* p = filename;
   const char* last_dot = NULL;
   for (; *p; p++) {
      if (*p == '.')
         last_dot = p;
      if (*p == DIR_SEPARATOR)
         last_dot = NULL;
   }

   if (last_dot) {
      p = last_dot;
      for (; *p; p++)
         ext_upper += toupper(*p);
   }

   //printf("filename: [%s], ext [%s]\n", filename, ext_upper.c_str());

   std::string type = GetMimetype(ext_upper);
   if (type.length() > 0)
      return type;

   cm_msg(MERROR, "get_content_type", "Unknown HTTP Content-Type for resource file \'%s\', file extension \'%s\'", filename, ext_upper.c_str());

   return "text/plain";
}

/*------------------------------------------------------------------*/

bool send_fp(Return* r, const std::string& path, FILE* fp)
{
   assert(fp != NULL);

   // send HTTP headers

   r->rsprintf("HTTP/1.1 200 Document follows\r\n");
   r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
   r->rsprintf("Accept-Ranges: bytes\r\n");

   // send HTTP cache control headers

   time_t now = time(NULL);
   now += (int) (3600 * 24);
   struct tm gmt_tms;
   gmtime_r(&now, &gmt_tms);
   const char* format = "%A, %d-%b-%y %H:%M:%S GMT";

   char str[256];
   strftime(str, sizeof(str), format, &gmt_tms);
   r->rsprintf("Expires: %s\r\n", str);

   // send Content-Type header

   r->rsprintf("Content-Type: %s\r\n", get_content_type(path.c_str()).c_str());

   // send Content-Length header

   struct stat stat_buf;
   fstat(fileno(fp), &stat_buf);
   int length = stat_buf.st_size;
   r->rsprintf("Content-Length: %d\r\n", length);

   // send end of headers

   r->rsprintf("\r\n");

   // send file data

   r->rread(path.c_str(), fileno(fp), length);

   fclose(fp);

   return true;
}

bool send_file(Return* r, const std::string& path, bool generate_404 = true)
{
   FILE *fp = fopen(path.c_str(), "rb");

   if (!fp) {
      if (generate_404) {
         /* header */
         r->rsprintf("HTTP/1.1 404 Not Found\r\n");
         r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
         r->rsprintf("Content-Type: text/plain; charset=%s\r\n", HTTP_ENCODING);
         r->rsprintf("\r\n");
         r->rsprintf("Error: Cannot read \"%s\", fopen() errno %d (%s)\n", path.c_str(), errno, strerror(errno));
      }
      return false;
   }

   return send_fp(r, path, fp);
}

bool send_resource(Return* r, const std::string& name, bool generate_404 = true)
{
   std::string path;
   FILE *fp = NULL;

   bool found = open_resource_file(name.c_str(), &path, &fp);

   if (!found) {
      if (generate_404) {
         /* header */
         r->rsprintf("HTTP/1.1 404 Not Found\r\n");
         r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
         r->rsprintf("Content-Type: text/plain; charset=%s\r\n", HTTP_ENCODING);
         r->rsprintf("\r\n");
         r->rsprintf("Error: resource file \"%s\" not found, see messages\n", name.c_str());
      }
      return false;
   }

   return send_fp(r, path, fp);
}

/*------------------------------------------------------------------*/

INT sendmail(const char* from_host, const char *smtp_host, const char *from, const char *to, const char *subject, const char *text)
{
   struct sockaddr_in bind_addr;
   struct hostent *phe;
   int i, s, strsize, offset;
   char *str, buf[256];

   if (verbose)
      printf("\n\nEmail from %s to %s, SMTP host %s:\n", from, to, smtp_host);

   /* create a new socket for connecting to remote server */
   s = socket(AF_INET, SOCK_STREAM, 0);
   if (s == -1)
      return -1;

   /* connect to remote node port 25 */
   memset(&bind_addr, 0, sizeof(bind_addr));
   bind_addr.sin_family = AF_INET;
   bind_addr.sin_port = htons((short) 25);

   phe = gethostbyname(smtp_host);
   if (phe == NULL)
      return -1;
   memcpy((char *) &(bind_addr.sin_addr), phe->h_addr, phe->h_length);

   if (connect(s, (const sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
      closesocket(s);
      return -1;
   }

   strsize = TEXT_SIZE + 1000;
   str = (char*)malloc(strsize);

   recv_string(s, str, strsize, 3000);
   if (verbose)
      puts(str);

   /* drain server messages */
   do {
      str[0] = 0;
      recv_string(s, str, strsize, 300);
      if (verbose)
         puts(str);
   } while (str[0]);

   sprintf(str, "HELO %s\r\n", from_host);
   send(s, str, strlen(str), 0);
   if (verbose)
      puts(str);
   recv_string(s, str, strsize, 3000);
   if (verbose)
      puts(str);

   if (strchr(from, '<')) {
      strlcpy(buf, strchr(from, '<') + 1, sizeof(buf));
      if (strchr(buf, '>'))
         *strchr(buf, '>') = 0;
   } else
      strlcpy(buf, from, sizeof(buf));

   sprintf(str, "MAIL FROM: %s\n", buf);
   send(s, str, strlen(str), 0);
   if (verbose)
      puts(str);
   recv_string(s, str, strsize, 3000);
   if (verbose)
      puts(str);

   sprintf(str, "RCPT TO: <%s>\r\n", to);
   send(s, str, strlen(str), 0);
   if (verbose)
      puts(str);
   recv_string(s, str, strsize, 3000);
   if (verbose)
      puts(str);

   sprintf(str, "DATA\r\n");
   send(s, str, strlen(str), 0);
   if (verbose)
      puts(str);
   recv_string(s, str, strsize, 3000);
   if (verbose)
      puts(str);

   sprintf(str, "To: %s\r\nFrom: %s\r\nSubject: %s\r\n", to, from, subject);
   send(s, str, strlen(str), 0);
   if (verbose)
      puts(str);

   sprintf(str, "X-Mailer: mhttpd revision %s\r\n", mhttpd_revision());
   send(s, str, strlen(str), 0);
   if (verbose)
      puts(str);

   ss_tzset(); // required for localtime_r()
   time_t now;
   time(&now);
   struct tm tms;
   localtime_r(&now, &tms);
   strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S", &tms);
   offset = (-(int) timezone);
   if (tms.tm_isdst)
      offset += 3600;
   sprintf(str, "Date: %s %+03d%02d\r\n", buf, (int) (offset / 3600),
           (int) ((abs((int) offset) / 60) % 60));
   send(s, str, strlen(str), 0);
   if (verbose)
      puts(str);

   sprintf(str, "Content-Type: TEXT/PLAIN; charset=US-ASCII\r\n\r\n");
   send(s, str, strlen(str), 0);
   if (verbose)
      puts(str);

   /* analyze text for "." at beginning of line */
   const char* p = text;
   str[0] = 0;
   while (strstr(p, "\r\n.\r\n")) {
      i = (POINTER_T) strstr(p, "\r\n.\r\n") - (POINTER_T) p + 1;
      strlcat(str, p, i);
      p += i + 4;
      strlcat(str, "\r\n..\r\n", strsize);
   }
   strlcat(str, p, strsize);
   strlcat(str, "\r\n", strsize);
   send(s, str, strlen(str), 0);
   if (verbose)
      puts(str);

   /* send ".<CR>" to signal end of message */
   sprintf(str, ".\r\n");
   send(s, str, strlen(str), 0);
   if (verbose)
      puts(str);
   recv_string(s, str, strsize, 3000);
   if (verbose)
      puts(str);

   sprintf(str, "QUIT\n");
   send(s, str, strlen(str), 0);
   if (verbose)
      puts(str);
   recv_string(s, str, strsize, 3000);
   if (verbose)
      puts(str);

   closesocket(s);
   free(str);

   return 1;
}

/*------------------------------------------------------------------*/

void redirect(Return *r, const char *path)
{
   char str[256];

   //printf("redirect to [%s]\n", path);

   strlcpy(str, path, sizeof(str));
   if (str[0] == 0)
      strcpy(str, "./");

   /* redirect */
   r->rsprintf("HTTP/1.1 302 Found\r\n");
   r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
   r->rsprintf("Content-Type: text/html; charset=%s\r\n", HTTP_ENCODING);

   if (strncmp(path, "http:", 5) == 0)
      r->rsprintf("Location: %s\r\n\r\n<html>redir</html>\r\n", str);
   else if (strncmp(path, "https:", 6) == 0)
      r->rsprintf("Location: %s\r\n\r\n<html>redir</html>\r\n", str);
   else {
      r->rsprintf("Location: %s\r\n\r\n<html>redir</html>\r\n", str);
   }
}

void redirect_307(Return *r, const char *path)
{
   //printf("redirect_307 to [%s]\n", path);

   /* redirect */
   r->rsprintf("HTTP/1.1 307 Temporary Redirect\r\n");
   r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
   r->rsprintf("Content-Type: text/html; charset=%s\r\n", HTTP_ENCODING);
   r->rsprintf("Location: %s\r\n", path);
   r->rsprintf("\r\n");
   r->rsprintf("<html>redirect to %s</html>\r\n", path);
}

void redirect2(Return* r, const char *path)
{
   redirect(r, path);
}

/*------------------------------------------------------------------*/

struct search_data
{
   Return* r;
   const char* search_name;
};

INT search_callback(HNDLE hDB, HNDLE hKey, KEY * key, INT level, void *info)
{
   search_data* sinfo = (search_data*)info;
   int i;
   INT size, status;

   Return* r = sinfo->r;
   const char* search_name = sinfo->search_name;

   /* convert strings to uppercase */

   char str1[MAX_ODB_PATH];
   for (i = 0; key->name[i]; i++)
      str1[i] = toupper(key->name[i]);
   str1[i] = 0;

   char str2[MAX_ODB_PATH];
   for (i = 0; key->name[i]; i++)
      str2[i] = toupper(search_name[i]);
   str2[i] = 0;

   if (strstr(str1, str2) != NULL) {
      char path[MAX_ODB_PATH];
      char data[10000];
      db_get_path(hDB, hKey, str1, MAX_ODB_PATH);
      strlcpy(path, str1 + 1, sizeof(path));    /* strip leading '/' */
      strlcpy(str1, path, sizeof(str1));
      urlEncode(str1, sizeof(str1));

      if (key->type == TID_KEY || key->type == TID_LINK) {
         /* for keys, don't display data value */
         r->rsprintf("<tr><td class=\"ODBkey\"><a href=\"?cmd=odb&odb_path=/%s\">/%s</a></tr>\n", path, path);
      } else {
         /* strip variable name from path */
         char* p = path + strlen(path) - 1;
         while (*p && *p != '/')
            *p-- = 0;
         if (*p == '/')
            *p = 0;

         /* display single value */
         if (key->num_values == 1) {
            char data_str[MAX_ODB_PATH];
            size = sizeof(data);
            status = db_get_data(hDB, hKey, data, &size, key->type);
            if (status == DB_NO_ACCESS)
               strcpy(data_str, "<no read access>");
            else
               db_sprintf(data_str, data, key->item_size, 0, key->type);

            r->rsprintf("<tr><td class=\"ODBkey\">");
            r->rsprintf("<a href=\"?cmd=odb&odb_path=/%s\">/%s/%s</a></td>", path, path, key->name);
            r->rsprintf("<td class=\"ODBvalue\">%s</td></tr>\n", data_str);
         } else {
            /* display first value */
            r->rsprintf("<tr><td rowspan=%d class=\"ODBkey\">", key->num_values);
            r->rsprintf("<a href=\"?cmd=odb&odb_path=/%s\">/%s/%s\n", path, path, key->name);

            for (int i = 0; i < key->num_values; i++) {
               size = sizeof(data);
               db_get_data(hDB, hKey, data, &size, key->type);

               char data_str[MAX_ODB_PATH];
               db_sprintf(data_str, data, key->item_size, i, key->type);

               if (i > 0)
                  r->rsprintf("<tr>");

               r->rsprintf("<td class=\"ODBvalue\">[%d] %s</td></tr>\n", i, data_str);
            }
         }
      }
   }

   return SUCCESS;
}

/*------------------------------------------------------------------*/

void show_help_page(Return* r, const char* dec_path)
{
   const char *s;
   char str[256];
   int status;

   show_header(r, "Help", "", "./", 0);
   r->rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   r->rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   show_navigation_bar(r, "Help");

   r->rsprintf("<table class=\"mtable\" style=\"width: 95%%\">\n");
   r->rsprintf("  <tr>\n");
   r->rsprintf("    <td class=\"mtableheader\">MIDAS Help Page</td>\n");
   r->rsprintf("  </tr>\n");
   r->rsprintf("  <tr>\n");
   r->rsprintf("    <td>\n");
   r->rsprintf("      <table>\n");

   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">Documentation:</td>\n");
   r->rsprintf("          <td style=\"text-align:left;\"><a href=\"https://midas.triumf.ca\">https://midas.triumf.ca</a></td>\n");
   r->rsprintf("        </tr>\n");
   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">Discussion Forum:</td>\n");
   r->rsprintf("          <td style=\"text-align:left;\"><a href=\"https://midas.triumf.ca/elog/Midas/\">https://midas.triumf.ca/elog/Midas/</a></td>\n");
   r->rsprintf("        </tr>\n");
   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">Code:</td>\n");
   r->rsprintf("          <td style=\"text-align:left;\"><a href=\"https://bitbucket.org/tmidas/midas/\">https://bitbucket.org/tmidas/midas/</a></td>\n");
   r->rsprintf("        </tr>\n");
   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">Report a bug:</td>\n");
   r->rsprintf("          <td style=\"text-align:left;\"><a href=\"https://bitbucket.org/tmidas/midas/issues/\">https://bitbucket.org/tmidas/midas/issues/</a></td>\n");
   r->rsprintf("        </tr>\n");

   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">Version:</td>\n");
   r->rsprintf("          <td style=\"text-align:left;\">%s</td>\n", cm_get_version());
   r->rsprintf("        </tr>\n");
   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">Revision:</td>\n");
   std::string rev = cm_get_revision();
   std::string url = "https://bitbucket.org/tmidas/midas/commits/";
   // rev format looks like this:
   // Fri Nov 24 10:15:54 2017 -0800 - midas-2017-07-c-171-gb8928d5c-dirty on branch develop
   // -gXXX is the commit hash
   // -dirty should be removed from the hash url, if present
   // " " before "on branch" should be removed from the hash url
   std::string::size_type pos = rev.find("-g");
   if (pos != std::string::npos) {
      std::string hash = rev.substr(pos+2);
      pos = hash.find("-dirty");
      if (pos != std::string::npos) {
         hash = hash.substr(0, pos);
      }
      pos = hash.find(" ");
      if (pos != std::string::npos) {
         hash = hash.substr(0, pos);
      }
      url += hash;
      r->rsprintf("          <td style=\"text-align:left;\"><a href=\"%s\">%s</a></td>\n", url.c_str(), rev.c_str());
   } else {
      r->rsprintf("          <td style=\"text-align:left;\">%s</td>\n", rev.c_str());
   }
   r->rsprintf("        </tr>\n");

   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">MIDASSYS:</td>\n");
   s = getenv("MIDASSYS");
   if (!s) s = "(unset)";
   strlcpy(str, s, sizeof(str));
   r->rsprintf("          <td style=\"text-align:left;\">%s</td>\n", str);
   r->rsprintf("        </tr>\n");

   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">mhttpd current directory:</td>\n");
   if (!getcwd(str, sizeof(str)))
      str[0] = 0;
   r->rsprintf("          <td style=\"text-align:left;\">%s</td>\n", str);
   r->rsprintf("        </tr>\n");

   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">Exptab file:</td>\n");
   r->rsprintf("          <td style=\"text-align:left;\">%s</td>\n", cm_get_exptab_filename().c_str());
   r->rsprintf("        </tr>\n");

   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">Experiment:</td>\n");
   r->rsprintf("          <td style=\"text-align:left;\">%s</td>\n", cm_get_experiment_name().c_str());
   r->rsprintf("        </tr>\n");

   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">Experiment directory:</td>\n");
   r->rsprintf("          <td style=\"text-align:left;\">%s</td>\n", cm_get_path().c_str());
   r->rsprintf("        </tr>\n");

   STRING_LIST list;
   status = cm_msg_facilities(&list);

   if (status == CM_SUCCESS) {
      if (list.size() == 1) {
         r->rsprintf("        <tr>\n");
         r->rsprintf("          <td style=\"text-align:right;\">System logfile:</td>\n");
         std::string s;
         cm_msg_get_logfile("midas", 0, &s, NULL, NULL);
         r->rsprintf("          <td style=\"text-align:left;\">%s</td>\n", s.c_str());
         r->rsprintf("        </tr>\n");
      } else {
         r->rsprintf("        <tr>\n");
         r->rsprintf("          <td style=\"text-align:right;\">Logfiles:</td>\n");
         r->rsprintf("          <td style=\"text-align:left;\">\n");
         for (unsigned i=0 ; i<list.size() ; i++) {
            if (i>0)
               r->rsputs("<br />\n");
            std::string s;
            cm_msg_get_logfile(list[i].c_str(), 0, &s, NULL, NULL);
            r->rsputs(s.c_str());
         }
         r->rsprintf("\n          </td>\n");
         r->rsprintf("        </tr>\n");
      }
   }

   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">Image history:</td>\n");
   r->rsprintf("          <td style=\"text-align:left;\">%s</td>\n", cm_get_history_path("IMAGE").c_str());
   r->rsprintf("        </tr>\n");

   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">Resource paths:</td>\n");
   r->rsprintf("          <td style=\"text-align:left;\">");
   std::vector<std::string> resource_paths = get_resource_paths();
   for (unsigned i=0; i<resource_paths.size(); i++) {
      if (i>0)
         r->rsputs("<br>");
      r->rsputs(resource_paths[i].c_str());
      std::string exp = cm_expand_env(resource_paths[i].c_str());
      //printf("%d %d [%s] [%s]\n", resource_paths[i].length(), exp.length(), resource_paths[i].c_str(), exp.c_str());
      if (exp != resource_paths[i]) {
         r->rsputs(" (");
         r->rsputs(exp.c_str());
         r->rsputs(")");
      }
   }
   r->rsprintf("          </td>\n");
   r->rsprintf("        </tr>\n");

   std::string path;

   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">midas.css:</td>\n");
   if (open_resource_file("midas.css", &path, NULL))
      r->rsprintf("          <td style=\"text-align:left;\">%s</td>\n", path.c_str());
   else
      r->rsprintf("          <td style=\"text-align:left;\">NOT FOUND</td>\n");
   r->rsprintf("        </tr>\n");

   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">midas.js:</td>\n");
   if (open_resource_file("midas.js", &path, NULL))
      r->rsprintf("          <td style=\"text-align:left;\">%s</td>\n", path.c_str());
   else
      r->rsprintf("          <td style=\"text-align:left;\">NOT FOUND</td>\n");
   r->rsprintf("        </tr>\n");

   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">controls.js:</td>\n");
   if (open_resource_file("controls.js", &path, NULL))
      r->rsprintf("          <td style=\"text-align:left;\">%s</td>\n", path.c_str());
   else
      r->rsprintf("          <td style=\"text-align:left;\">NOT FOUND</td>\n");
   r->rsprintf("        </tr>\n");

   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">mhttpd.js:</td>\n");
   if (open_resource_file("mhttpd.js", &path, NULL))
      r->rsprintf("          <td style=\"text-align:left;\">%s</td>\n", path.c_str());
   else
      r->rsprintf("          <td style=\"text-align:left;\">NOT FOUND</td>\n");
   r->rsprintf("        </tr>\n");

   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">obsolete.js:</td>\n");
   if (open_resource_file("obsolete.js", &path, NULL))
      r->rsprintf("          <td style=\"text-align:left;\">%s</td>\n", path.c_str());
   else
      r->rsprintf("          <td style=\"text-align:left;\">NOT FOUND</td>\n");
   r->rsprintf("        </tr>\n");

   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">Obsolete mhttpd.css:</td>\n");
   if (open_resource_file("mhttpd.css", &path, NULL))
      r->rsprintf("          <td style=\"text-align:left;\">%s</td>\n", path.c_str());
   else
      r->rsprintf("          <td style=\"text-align:left;\">NOT FOUND</td>\n");
   r->rsprintf("        </tr>\n");

   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">JSON-RPC schema:</td>\n");
   r->rsprintf("          <td style=\"text-align:left;\"><a href=\"?mjsonrpc_schema\">json format</a> or <a href=\"?mjsonrpc_schema_text\">text table format</a></td>\n");
   r->rsprintf("        </tr>\n");

   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">JavaScript examples:</td>\n");
   r->rsprintf("          <td style=\"text-align:left;\"><a href=\"?cmd=example\">example.html</a></td>\n");
   r->rsprintf("        </tr>\n");

   r->rsprintf("        <tr>\n");
   r->rsprintf("          <td style=\"text-align:right;\">Custom page example:</td>\n");
   r->rsprintf("          <td style=\"text-align:left;\"><a href=\"?cmd=custom_example\">custom_example.html</a></td>\n");
   r->rsprintf("        </tr>\n");

   r->rsprintf("      </table>\n");
   r->rsprintf("    </td>\n");
   r->rsprintf("  </tr>\n");
   r->rsprintf("</table>\n");

   r->rsprintf("<table class=\"mtable\" style=\"width: 95%%\">\n");
   r->rsprintf("  <tr>\n");
   r->rsprintf("    <td class=\"mtableheader\">Contributions</td>\n");
   r->rsprintf("  </tr>\n");
   r->rsprintf("  <tr>\n");
   r->rsprintf("    <td>\n");
   r->rsprintf("Pierre-Andre&nbsp;Amaudruz - Sergio&nbsp;Ballestrero - Suzannah&nbsp;Daviel - Peter&nbsp;Green - Qing&nbsp;Gu - Greg&nbsp;Hackman - Gertjan&nbsp;Hofman - Paul&nbsp;Knowles - Exaos&nbsp;Lee - Thomas&nbsp;Lindner - Shuoyi&nbsp;Ma - Rudi&nbsp;Meier - Bill&nbsp;Mills - Glenn&nbsp;Moloney - Dave&nbsp;Morris - John&nbsp;M&nbsp;O'Donnell - Konstantin&nbsp;Olchanski - Chris&nbsp;Pearson - Renee&nbsp;Poutissou - Stefan&nbsp;Ritt - Ryu&nbsp;Sawada - Tamsen&nbsp;Schurman - Andreas&nbsp;Suter - Jan&nbsp;M.&nbsp;Wouters - Piotr&nbsp;Adam&nbsp;Zolnierczuk\n");
   r->rsprintf("    </td>\n");
   r->rsprintf("  </tr>\n");
   r->rsprintf("</table>\n");

   r->rsprintf("</div></form>\n");
   r->rsprintf("</body></html>\r\n");
}

/*------------------------------------------------------------------*/

void show_header(Return* r, const char *title, const char *method, const char *path, int refresh)
{
   HNDLE hDB;
   time_t now;
   char str[256];

   cm_get_experiment_database(&hDB, NULL);

   /* header */
   r->rsprintf("HTTP/1.1 200 Document follows\r\n");
   r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
   r->rsprintf("Cache-control: private, max-age=0, no-cache\r\n");
   r->rsprintf("Expires: Fri, 01 Jan 1983 00:00:00 GMT\r\n");
   r->rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

   r->rsprintf("<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\n");
   r->rsprintf("<html><head>\n");

   /* style sheet */
   r->rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
   r->rsprintf("<link rel=\"stylesheet\" href=\"mhttpd.css\" type=\"text/css\" />\n");
   r->rsprintf("<link rel=\"stylesheet\" href=\"midas.css\" type=\"text/css\" />\n");

   /* auto refresh */
   if (refresh > 0)
      r->rsprintf("<meta http-equiv=\"Refresh\" content=\"%02d\">\n", refresh);

   r->rsprintf("<title>%s</title></head>\n", title);

   strlcpy(str, path, sizeof(str));
   urlEncode(str, sizeof(str));

   if (equal_ustring(method, "POST"))
      r->rsprintf
          ("<body><form name=\"form1\" method=\"POST\" action=\"%s\" enctype=\"multipart/form-data\">\n\n",
           str);
   else if (equal_ustring(method, "GET"))
      r->rsprintf("<body><form name=\"form1\" method=\"GET\" action=\"%s\">\n\n", str);

   /* title row */

   std::string exptname;
   db_get_value_string(hDB, 0, "/Experiment/Name", 0, &exptname, TRUE);
   time(&now);
}

/*------------------------------------------------------------------*/

void show_text_header(Return* r)
{
   r->rsprintf("HTTP/1.1 200 Document follows\r\n");
   r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
   r->rsprintf("Access-Control-Allow-Origin: *\r\n");
   r->rsprintf("Cache-control: private, max-age=0, no-cache\r\n");
   r->rsprintf("Expires: Fri, 01 Jan 1983 00:00:00 GMT\r\n");
   r->rsprintf("Content-Type: text/plain; charset=%s\r\n\r\n", HTTP_ENCODING);
}

/*------------------------------------------------------------------*/

void show_error(Return* r, const char *error)
{
   /* header */
   r->rsprintf("HTTP/1.1 200 Document follows\r\n");
   r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
   r->rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

   r->rsprintf("<html><head>\n");
   r->rsprintf("<link rel=\"stylesheet\" href=\"mhttpd.css\" type=\"text/css\" />\n");
   r->rsprintf("<title>MIDAS error</title></head>\n");
   r->rsprintf("<body><H1>%s</H1></body></html>\n", error);
}

/*------------------------------------------------------------------*/

void show_error_404(Return* r, const char *error)
{
   /* header */
   r->rsprintf("HTTP/1.1 404 Not Found\r\n");
   r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
   r->rsprintf("Content-Type: text/plain\r\n");
   r->rsprintf("\r\n");

   r->rsprintf("MIDAS error: %s\n", error);
}

/*------------------------------------------------------------------*/

void show_navigation_bar(Return* r, const char *cur_page)
{
   r->rsprintf("<script>\n");
   r->rsprintf("window.addEventListener(\"load\", function(e) { mhttpd_init('%s', 1000); });\n", cur_page);
   r->rsprintf("</script>\n");

   r->rsprintf("<!-- header and side navigation will be filled in mhttpd_init -->\n");
   r->rsprintf("<div id=\"mheader\"></div>\n");
   r->rsprintf("<div id=\"msidenav\"></div>\n");
   r->rsprintf("<div id=\"mmain\">\n");
}

/*------------------------------------------------------------------*/

void check_obsolete_odb(HNDLE hDB, const char* odb_path)
{
   HNDLE hKey;
   int status = db_find_key(hDB, 0, odb_path, &hKey);
   if (status == DB_SUCCESS) {
      cm_msg(MERROR, "check_obsolete_odb", "ODB \"%s\" is obsolete, please delete it.", odb_path);
   }
}

void init_menu_buttons(MVOdb* odb)
{
   HNDLE hDB;
   BOOL true_value = TRUE;
   BOOL false_value = FALSE;
   int size = sizeof(true_value);
   cm_get_experiment_database(&hDB, NULL);
   db_get_value(hDB, 0, "/Experiment/Menu/Status",     &true_value,  &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Start",      &false_value, &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Transition", &true_value,  &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/ODB",        &true_value,  &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Messages",   &true_value,  &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Chat",       &true_value,  &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Elog",       &true_value,  &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Alarms",     &true_value,  &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Programs",   &true_value,  &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Buffers",    &true_value,  &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/History",    &true_value,  &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/OldHistory", &true_value,  &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/MSCB",       &true_value,  &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Sequencer",  &true_value,  &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Config",     &true_value,  &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Example",    &false_value, &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Help",       &true_value,  &size, TID_BOOL, TRUE);

   //std::string buf;
   //status = db_get_value_string(hDB, 0, "/Experiment/Menu buttons", 0, &buf, FALSE);
   //if (status == DB_SUCCESS) {
   //   cm_msg(MERROR, "init_menu_buttons", "ODB \"/Experiment/Menu buttons\" is obsolete, please delete it.");
   //}

   check_obsolete_odb(hDB, "/Experiment/Menu buttons");
   check_obsolete_odb(hDB, "/Experiment/Menu/OldSequencer");
#ifndef OLD_SEQUENCER
   check_obsolete_odb(hDB, "/Experiment/Menu/NewSequencer");
#endif
}

/*------------------------------------------------------------------*/

void init_mhttpd_odb(MVOdb* odb)
{
   HNDLE hDB;
   HNDLE hKey;
   int status;
   std::string s;
   cm_get_experiment_database(&hDB, NULL);

   status = db_find_key(hDB, 0, "/Experiment/Base URL", &hKey);
   if (status == DB_SUCCESS) {
      cm_msg(MERROR, "init_mhttpd_odb", "ODB \"/Experiment/Base URL\" is obsolete, please delete it.");
   }

   status = db_find_key(hDB, 0, "/Experiment/CSS File", &hKey);
   if (status == DB_SUCCESS) {
      cm_msg(MERROR, "init_mhttpd_odb", "ODB \"/Experiment/CSS File\" is obsolete, please delete it.");
   }

   status = db_find_key(hDB, 0, "/Experiment/JS File", &hKey);
   if (status == DB_SUCCESS) {
      cm_msg(MERROR, "init_mhttpd_odb", "ODB \"/Experiment/JS File\" is obsolete, please delete it.");
   }

   status = db_find_key(hDB, 0, "/Experiment/Start-Stop Buttons", &hKey);
   if (status == DB_SUCCESS) {
      cm_msg(MERROR, "init_mhttpd_odb", "ODB \"/Experiment/Start-Stop Buttons\" is obsolete, please delete it.");
   }

   bool xdefault = true;
   odb->RB("Experiment/Pause-Resume Buttons", &xdefault, true);

#ifdef HAVE_MONGOOSE616
   check_obsolete_odb(hDB, "/Experiment/midas http port");
   check_obsolete_odb(hDB, "/Experiment/midas https port");
   check_obsolete_odb(hDB, "/Experiment/http redirect to https");
   check_obsolete_odb(hDB, "/Experiment/Security/mhttpd hosts");
#endif

   status = db_find_key(hDB, 0, "/Logger/Message file", &hKey);
   if (status == DB_SUCCESS) {
      cm_msg(MERROR, "init_mhttpd_odb", "ODB \"/Logger/Message file\" is obsolete, please delete it and use \"/Logger/Message dir\" and \"/Logger/message file date format\" instead.");
   }

   check_obsolete_odb(hDB, "/Logger/Watchdog timeout");
}

/*------------------------------------------------------------------*/

void init_elog_odb()
{
   HNDLE hDB;
   int size;
   HNDLE hkey;
   cm_get_experiment_database(&hDB, NULL);

   BOOL external_elog = FALSE;
   std::string external_elog_url;

   size = sizeof(external_elog);
   db_get_value(hDB, 0, "/Elog/External Elog", &external_elog, &size, TID_BOOL, TRUE);
   db_get_value_string(hDB, 0, "/Elog/URL", 0, &external_elog_url, TRUE);

   BOOL allow_delete = FALSE;
   BOOL allow_edit = FALSE;
   size = sizeof(BOOL);
   db_get_value(hDB, 0, "/Elog/Allow delete", &allow_delete, &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Elog/Allow edit", &allow_edit, &size, TID_BOOL, TRUE);
   //db_get_value(hDB, 0, "/Elog/Display run number", &display_run_number, &size, TID_BOOL, TRUE);

   if (db_find_key(hDB, 0, "/Elog/Buttons", &hkey) != DB_SUCCESS) {
      const char def_button[][NAME_LENGTH] = { "8h", "24h", "7d" };
      db_set_value(hDB, 0, "/Elog/Buttons", def_button, NAME_LENGTH*3, 3, TID_STRING);
   }


   /* get type list from ODB */
   size = 20 * NAME_LENGTH;
   if (db_find_key(hDB, 0, "/Elog/Types", &hkey) != DB_SUCCESS) {
      db_set_value(hDB, 0, "/Elog/Types", default_type_list, NAME_LENGTH * 20, 20, TID_STRING);
   }

   /* get system list from ODB */
   size = 20 * NAME_LENGTH;
   if (db_find_key(hDB, 0, "/Elog/Systems", &hkey) != DB_SUCCESS)
      db_set_value(hDB, 0, "/Elog/Systems", default_system_list, NAME_LENGTH * 20, 20, TID_STRING);
}

/*------------------------------------------------------------------*/

void strencode(Return* r, const char *text)
{
   int i;

   for (i = 0; i < (int) strlen(text); i++) {
      switch (text[i]) {
      case '\n':
         r->rsprintf("<br>\n");
         break;
      case '<':
         r->rsprintf("&lt;");
         break;
      case '>':
         r->rsprintf("&gt;");
         break;
      case '&':
         r->rsprintf("&amp;");
         break;
      case '\"':
         r->rsprintf("&quot;");
         break;
      default:
         r->rsprintf("%c", text[i]);
      }
   }
}

/*------------------------------------------------------------------*/

void strencode2(char *b, char *text)
{
   int i;

   for (i = 0; i < (int) strlen(text); i++) {
      switch (text[i]) {
      case '\n':
         sprintf(b, "<br>\n");
         b += 6;
         break;
      case '<':
         sprintf(b, "&lt;");
         b += 4;
         break;
      case '>':
         sprintf(b, "&gt;");
         b += 4;
         break;
      case '&':
         sprintf(b, "&amp;");
         b += 5;
         break;
      case '\"':
         sprintf(b, "&quot;");
         b += 6;
         break;
      default:
         sprintf(b, "%c", text[i]);
         b += 1;
         break;
      }
   }
   *b = 0;
}

/*------------------------------------------------------------------*/

void strencode3(Return* r, char *text)
{
   int i;

   for (i = 0; i < (int) strlen(text); i++) {
      switch (text[i]) {
      case '<':
         r->rsprintf("&lt;");
         break;
      case '>':
         r->rsprintf("&gt;");
         break;
      case '&':
         r->rsprintf("&amp;");
         break;
      case '\"':
         r->rsprintf("&quot;");
         break;
      default:
         r->rsprintf("%c", text[i]);
      }
   }
}

/*------------------------------------------------------------------*/

void strencode4(Return* r, char *text)
{
   int i;

   for (i = 0; i < (int) strlen(text); i++) {
      switch (text[i]) {
         case '\n':
            r->rsprintf("<br>\n");
            break;
         case '<':
            r->rsprintf("&lt;");
            break;
         case '>':
            r->rsprintf("&gt;");
            break;
         case '&':
            r->rsprintf("&amp;");
            break;
         case '\"':
            r->rsprintf("&quot;");
            break;
         case ' ':
            r->rsprintf("&nbsp;");
            break;
         default:
            r->rsprintf("%c", text[i]);
      }
   }
}

/*------------------------------------------------------------------*/

void gen_odb_attachment(Return* r, const char *path, std::string& bout)
{
   HNDLE hDB, hkeyroot, hkey;
   KEY key;
   INT i, j, size;
   char data_str[25600], hex_str[25600];
   char data[1024];
   time_t now;
   char b[1024];

   cm_get_experiment_database(&hDB, NULL);
   db_find_key(hDB, 0, path, &hkeyroot);
   assert(hkeyroot);

   /* title row */
   //size = sizeof(str);
   //str[0] = 0;
   //db_get_value(hDB, 0, "/Experiment/Name", str, &size, TID_STRING, TRUE);
   time(&now);

   bout += "<table border=3 cellpadding=1 class=\"dialogTable\">\n";
   char ctimebuf[32];
   ctime_r(&now, ctimebuf);
   sprintf(b, "<tr><th colspan=2>%s</tr>\n", ctimebuf);
   bout += b;
   sprintf(b, "<tr><th colspan=2>%s</tr>\n", path);
   bout += b;

   /* enumerate subkeys */
   for (i = 0;; i++) {
      db_enum_link(hDB, hkeyroot, i, &hkey);
      if (!hkey)
         break;
      db_get_key(hDB, hkey, &key);

      /* resolve links */
      if (key.type == TID_LINK) {
         db_enum_key(hDB, hkeyroot, i, &hkey);
         db_get_key(hDB, hkey, &key);
      }

      if (key.type == TID_KEY) {
         /* for keys, don't display data value */
         sprintf(b, "<tr><td colspan=2>%s</td></tr>\n", key.name);
         bout += b;
      } else {
         /* display single value */
         if (key.num_values == 1) {
            size = sizeof(data);
            db_get_data(hDB, hkey, data, &size, key.type);
            //printf("data size %d [%s]\n", size, data);
            db_sprintf(data_str, data, key.item_size, 0, key.type);
            assert(strlen(data_str) < sizeof(data_str));
            db_sprintfh(hex_str, data, key.item_size, 0, key.type);
            assert(strlen(hex_str) < sizeof(hex_str));

            if (data_str[0] == 0 || equal_ustring(data_str, "<NULL>")) {
               strcpy(data_str, "(empty)");
               hex_str[0] = 0;
            }

            if (strcmp(data_str, hex_str) != 0 && hex_str[0]) {
               //sprintf(b, "<tr><td>%s</td><td>%s (%s)</td></tr>\n", key.name, data_str, hex_str);
               bout += "<tr><td>";
               bout += key.name;
               bout += "</td><td>";
               bout += data_str;
               bout += " (";
               bout += hex_str;
               bout += ")</td></tr>\n";
            } else {
               sprintf(b, "<tr><td>%s</td><td>", key.name);
               bout += b;
               strencode2(b, data_str);
               bout += b;
               bout += "</td></tr>\n";
            }
         } else {
            /* display first value */
            sprintf(b, "<tr><td rowspan=%d>%s</td>\n", key.num_values, key.name);
            bout += b;

            for (j = 0; j < key.num_values; j++) {
               size = sizeof(data);
               db_get_data_index(hDB, hkey, data, &size, j, key.type);
               db_sprintf(data_str, data, key.item_size, 0, key.type);
               assert(strlen(data_str) < sizeof(data_str));
               db_sprintfh(hex_str, data, key.item_size, 0, key.type);
               assert(strlen(hex_str) < sizeof(hex_str));

               if (data_str[0] == 0 || equal_ustring(data_str, "<NULL>")) {
                  strcpy(data_str, "(empty)");
                  hex_str[0] = 0;
               }

               if (j > 0) {
                  bout += "<tr>";
               }

               if (strcmp(data_str, hex_str) != 0 && hex_str[0]) {
                  //sprintf(b, "<td>[%d] %s (%s)<br></td></tr>\n", j, data_str, hex_str);
                  bout += "<td>[";
                  bout += toString(j);
                  bout += "] ";
                  bout += data_str;
                  bout += " (";
                  bout += hex_str;
                  bout += ")<br></td></tr>\n";
               } else {
                  //sprintf(b, "<td>[%d] %s<br></td></tr>\n", j, data_str);
                  bout += "<td>[";
                  bout += toString(j);
                  bout += "] ";
                  bout += data_str;
                  bout += "<br></td></tr>\n";
               }
            }
         }
      }
   }

   bout += "</table>\n";
}

/*------------------------------------------------------------------*/

void submit_elog(MVOdb* odb, Param* pp, Return* r, Attachment* a)
{
   char path[256], path1[256];
   char mail_to[256], mail_from[256], mail_list[256],
       smtp_host[256], tag[80], mail_param[1000];
   char *p, *pitem;
   HNDLE hDB, hkey;
   char att_file[3][256];
   int fh, size, n_mail;
   char mhttpd_full_url[256];

   cm_get_experiment_database(&hDB, NULL);
   strlcpy(att_file[0], pp->getparam("attachment0"), sizeof(att_file[0]));
   strlcpy(att_file[1], pp->getparam("attachment1"), sizeof(att_file[1]));
   strlcpy(att_file[2], pp->getparam("attachment2"), sizeof(att_file[2]));

   /* check for valid attachment files */
   for (int i = 0; i < 3; i++) {
      char str[256];
      sprintf(str, "attachment%d", i);
      //printf("submit_elog: att %d, [%s] param [%s], size %d\n", i, str, pp->getparam(str), a->_attachment_size[i]);
      if (pp->getparam(str) && *pp->getparam(str) && a->attachment_size[i] == 0) {
         /* replace '\' by '/' */
         strlcpy(path, pp->getparam(str), sizeof(path));
         strlcpy(path1, path, sizeof(path1));
         while (strchr(path, '\\'))
            *strchr(path, '\\') = '/';

         /* check if valid ODB tree */
         if (db_find_key(hDB, 0, path, &hkey) == DB_SUCCESS) {
            std::string bout;
            gen_odb_attachment(r, path, bout);
            int bufsize = bout.length()+1;
            char* buf = (char*)M_MALLOC(bufsize);
            memcpy(buf, bout.c_str(), bufsize);
            strlcpy(att_file[i], path, sizeof(att_file[0]));
            strlcat(att_file[i], ".html", sizeof(att_file[0]));
            a->attachment_buffer[i] = buf;
            a->attachment_size[i] = bufsize;
         }
         /* check if local file */
         else if ((fh = open(path1, O_RDONLY | O_BINARY)) >= 0) {
            size = lseek(fh, 0, SEEK_END);
            char* buf = (char*)M_MALLOC(size);
            lseek(fh, 0, SEEK_SET);
            int rd = read(fh, buf, size);
            if (rd < 0)
               rd = 0;
            close(fh);
            strlcpy(att_file[i], path, sizeof(att_file[0]));
            a->attachment_buffer[i] = buf;
            a->attachment_size[i] = rd;
         } else if (strncmp(path, "/HS/", 4) == 0) {
            char* buf = (char*)M_MALLOC(100000);
            size = 100000;
            strlcpy(str, path + 4, sizeof(str));
            if (strchr(str, '?')) {
               p = strchr(str, '?') + 1;
               p = strtok(p, "&");
               while (p != NULL) {
                  pitem = p;
                  p = strchr(p, '=');
                  if (p != NULL) {
                     *p++ = 0;
                     urlDecode(pitem); // parameter name
                     urlDecode(p); // parameter value

                     pp->setparam(pitem, p);

                     p = strtok(NULL, "&");
                  }
               }
               *strchr(str, '?') = 0;
            }
            show_hist_page(odb, pp, r, "image.gif", buf, &size, 0);
            strlcpy(att_file[i], str, sizeof(att_file[0]));
            a->attachment_buffer[i] = buf;
            a->attachment_size[i] = size;
            pp->unsetparam("scale");
            pp->unsetparam("offset");
            pp->unsetparam("width");
            pp->unsetparam("index");
         } else {
            r->rsprintf("HTTP/1.1 200 Document follows\r\n");
            r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
            r->rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

            r->rsprintf("<html><head>\n");
            r->rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
            r->rsprintf("<link rel=\"stylesheet\" href=\"midas.css\" type=\"text/css\" />\n");
            r->rsprintf("<link rel=\"stylesheet\" href=\"mhttpd.css\" type=\"text/css\" />\n");
            r->rsprintf("<title>ELog Error</title></head>\n");
            r->rsprintf("<i>Error: Attachment file <i>%s</i> not valid.</i><p>\n", pp->getparam(str));
            r->rsprintf("Please go back and enter a proper filename (use the <b>Browse</b> button).\n");
            r->rsprintf("<body></body></html>\n");
            return;
         }
      }
   }

   int edit = atoi(pp->getparam("edit"));
   //printf("submit_elog: edit [%s] %d, orig [%s]\n", pp->getparam("edit"), edit, pp->getparam("orig"));

   tag[0] = 0;
   if (edit) {
      strlcpy(tag, pp->getparam("orig"), sizeof(tag));
   }

   int status = el_submit(atoi(pp->getparam("run")),
                          pp->getparam("author"),
                          pp->getparam("type"),
                          pp->getparam("system"),
                          pp->getparam("subject"),
                          pp->getparam("text"),
                          pp->getparam("orig"),
                          *pp->getparam("html") ? "HTML" : "plain",
                          att_file[0], a->attachment_buffer[0], a->attachment_size[0],
                          att_file[1], a->attachment_buffer[1], a->attachment_size[1],
                          att_file[2], a->attachment_buffer[2], a->attachment_size[2],
                          tag, sizeof(tag));

   //printf("el_submit status %d, tag [%s]\n", status, tag);

   if (status != EL_SUCCESS) {
      cm_msg(MERROR, "submit_elog", "el_submit() returned status %d", status);
   }

   /* supersede host name with "/Elog/Host name" */
   std::string elog_host_name;
   db_get_value_string(hDB, 0, "/Elog/Host name", 0, &elog_host_name, TRUE);

   // K.O. FIXME: we cannot guess the Elog URL like this because
   // we do not know if access is through a proxy or redirect
   // we do not know if it's http: or https:, etc. Better
   // to read the whole "mhttpd_full_url" string from ODB.
   sprintf(mhttpd_full_url, "http://%s/", elog_host_name.c_str());

   /* check for mail submissions */
   mail_param[0] = 0;
   n_mail = 0;

   for (int index = 0; index <= 1; index++) {
      std::string str;
      str += "/Elog/Email ";
      if (index == 0)
         str += pp->getparam("type");
      else
         str += pp->getparam("system");

      if (db_find_key(hDB, 0, str.c_str(), &hkey) == DB_SUCCESS) {
         size = sizeof(mail_list);
         db_get_data(hDB, hkey, mail_list, &size, TID_STRING);

         if (db_find_key(hDB, 0, "/Elog/SMTP host", &hkey) != DB_SUCCESS) {
            show_error(r, "No SMTP host defined under /Elog/SMTP host");
            return;
         }
         size = sizeof(smtp_host);
         db_get_data(hDB, hkey, smtp_host, &size, TID_STRING);

         p = strtok(mail_list, ",");
         for (int i = 0; p; i++) {
            strlcpy(mail_to, p, sizeof(mail_to));

            std::string exptname;
            db_get_value_string(hDB, 0, "/Experiment/Name", 0, &exptname, TRUE);

            sprintf(mail_from, "MIDAS %s <MIDAS@%s>", exptname.c_str(), elog_host_name.c_str());

            std::string mail_text;
            mail_text += "A new entry has been submitted by ";
            mail_text += pp->getparam("author");
            mail_text += "\n";
            mail_text += "\n";

            mail_text += "Experiment : ";
            mail_text += exptname.c_str();
            mail_text += "\n";

            mail_text += "Type       : ";
            mail_text += pp->getparam("type");
            mail_text += "\n";

            mail_text += "System     : ";
            mail_text += pp->getparam("system");
            mail_text += "\n";

            mail_text += "Subject    : ";
            mail_text += pp->getparam("subject");
            mail_text += "\n";

            mail_text += "Link       : ";
            mail_text += mhttpd_full_url;
            mail_text += "/EL/";
            mail_text += tag;
            mail_text += "\n";

            mail_text += "\n";

            mail_text += pp->getparam("text");
            mail_text += "\n";

            sendmail(elog_host_name.c_str(), smtp_host, mail_from, mail_to, pp->getparam("type"), mail_text.c_str());

            if (mail_param[0] == 0)
               strlcpy(mail_param, "?", sizeof(mail_param));
            else
               strlcat(mail_param, "&", sizeof(mail_param));
            sprintf(mail_param + strlen(mail_param), "mail%d=%s", n_mail++, mail_to);

            p = strtok(NULL, ",");
            if (!p)
               break;
            while (*p == ' ')
               p++;
         }
      }
   }

   r->rsprintf("HTTP/1.1 302 Found\r\n");
   r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());

   //if (mail_param[0])
   //   r->rsprintf("Location: ../EL/%s?%s\n\n<html>redir</html>\r\n", tag, mail_param + 1);
   //else
   //   r->rsprintf("Location: ../EL/%s\n\n<html>redir</html>\r\n", tag);

   if (mail_param[0])
      r->rsprintf("Location: ?cmd=Show+elog&tag=%s&%s\n\n<html>redir</html>\r\n", tag, mail_param + 1);
   else
      r->rsprintf("Location: ?cmd=Show+elog&tag=%s\n\n<html>redir</html>\r\n", tag);
}

/*------------------------------------------------------------------*/

void show_elog_attachment(Param* p, Return* r, const char* path)
{
   HNDLE hDB;
   int size;
   int status;
   char file_name[256];

   cm_get_experiment_database(&hDB, NULL);
   file_name[0] = 0;
   if (hDB > 0) {
      size = sizeof(file_name);
      memset(file_name, 0, size);

      status = db_get_value(hDB, 0, "/Logger/Elog dir", file_name, &size, TID_STRING, FALSE);
      if (status != DB_SUCCESS)
         db_get_value(hDB, 0, "/Logger/Data dir", file_name, &size, TID_STRING, TRUE);

      if (file_name[0] != 0)
         if (file_name[strlen(file_name) - 1] != DIR_SEPARATOR)
            strlcat(file_name, DIR_SEPARATOR_STR, sizeof(file_name));
   }
   strlcat(file_name, path, sizeof(file_name));

   int fh = open(file_name, O_RDONLY | O_BINARY);
   if (fh > 0) {
      lseek(fh, 0, SEEK_END);
      int length = TELL(fh);
      lseek(fh, 0, SEEK_SET);

      r->rsprintf("HTTP/1.1 200 Document follows\r\n");
      r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
      r->rsprintf("Accept-Ranges: bytes\r\n");
      //r->rsprintf("Content-disposition: attachment; filename=%s\r\n", path);

      r->rsprintf("Content-Type: %s\r\n", get_content_type(file_name).c_str());

      r->rsprintf("Content-Length: %d\r\n\r\n", length);

      r->rread(file_name, fh, length);

      close(fh);
   }

   return;
}

/*------------------------------------------------------------------*/

BOOL is_editable(char *eq_name, char *var_name)
{
   HNDLE hDB, hkey;
   KEY key;
   char str[256];
   int i, size;

   cm_get_experiment_database(&hDB, NULL);
   sprintf(str, "/Equipment/%s/Settings/Editable", eq_name);
   db_find_key(hDB, 0, str, &hkey);

   /* if no editable entry found, use default */
   if (!hkey) {
      return (equal_ustring(var_name, "Demand") ||
              equal_ustring(var_name, "Output") || strncmp(var_name, "D_", 2) == 0);
   }

   db_get_key(hDB, hkey, &key);
   for (i = 0; i < key.num_values; i++) {
      size = sizeof(str);
      db_get_data_index(hDB, hkey, str, &size, i, TID_STRING);
      if (equal_ustring(var_name, str))
         return TRUE;
   }
   return FALSE;
}

void show_eqtable_page(Param* pp, Return* r, int refresh)
{
   int i, j, k, colspan, size, n_var, i_edit, i_set, line;
   char str[256], eq_name[32], group[32], name[NAME_LENGTH+32];
   char group_name[MAX_GROUPS][32], data[256], style[80];
   HNDLE hDB;
   char data_str[256], hex_str[256], odb_path[256];

   cm_get_experiment_database(&hDB, NULL);

   /* check if variable to edit */
   i_edit = -1;
   if (equal_ustring(pp->getparam("cmd"), "Edit"))
      i_edit = atoi(pp->getparam("index"));

   /* check if variable to set */
   i_set = -1;
   if (equal_ustring(pp->getparam("cmd"), "Set"))
      i_set = atoi(pp->getparam("index"));

   /* get equipment and group */
   if (pp->getparam("eq"))
      strlcpy(eq_name, pp->getparam("eq"), sizeof(eq_name));
   strlcpy(group, "All", sizeof(group));
   if (pp->getparam("group") && *pp->getparam("group"))
      strlcpy(group, pp->getparam("group"), sizeof(group));

#if 0
   /* check for "names" in settings */
   if (eq_name[0]) {
      sprintf(str, "/Equipment/%s/Settings", eq_name);
      HNDLE hkeyset;
      db_find_key(hDB, 0, str, &hkeyset);
      HNDLE hkeynames = 0;
      if (hkeyset) {
         for (i = 0;; i++) {
            db_enum_link(hDB, hkeyset, i, &hkeynames);

            if (!hkeynames)
               break;

            KEY key;
            db_get_key(hDB, hkeynames, &key);

            if (strncmp(key.name, "Names", 5) == 0)
               break;
         }
      }

      /* redirect if no names found */
      if (!hkeyset || !hkeynames) {
         /* redirect */
         sprintf(str, "?cmd=odb&odb_path=/Equipment/%s/Variables", eq_name);
         redirect(r, str);
         return;
      }
   }
#endif

   sprintf(str, "%s", group);
   show_header(r, "MIDAS slow control", "", str, i_edit == -1 ? refresh : 0);
   r->rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   r->rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   r->rsprintf("<script type=\"text/javascript\" src=\"obsolete.js\"></script>\n");
   show_navigation_bar(r, "SC");

   /*---- menu buttons ----*/

   r->rsprintf("<tr><td colspan=15>\n");

   if (equal_ustring(pp->getparam("cmd"), "Edit"))
      r->rsprintf("<input type=submit name=cmd value=Set>\n");

   r->rsprintf("</tr>\n\n");
   r->rsprintf("</table>");  //end header table

   r->rsprintf("<table class=\"ODBtable\" style=\"max-width:700px;\">");  //body table

   /*---- enumerate SC equipment ----*/

   r->rsprintf("<tr><td class=\"subStatusTitle\" colspan=15><i>Equipment:</i> &nbsp;&nbsp;\n");

   HNDLE hkeyeqroot;
   db_find_key(hDB, 0, "/Equipment", &hkeyeqroot);
   if (hkeyeqroot)
      for (i = 0;; i++) {
         HNDLE hkeyeq;
         db_enum_link(hDB, hkeyeqroot, i, &hkeyeq);

         if (!hkeyeq)
            break;

         KEY eqkey;
         db_get_key(hDB, hkeyeq, &eqkey);

         HNDLE hkeyset;
         db_find_key(hDB, hkeyeq, "Settings", &hkeyset);
         if (hkeyset) {
            for (j = 0;; j++) {
               HNDLE hkeynames;
               db_enum_link(hDB, hkeyset, j, &hkeynames);

               if (!hkeynames)
                  break;

               KEY key;
               db_get_key(hDB, hkeynames, &key);

               if (strncmp(key.name, "Names", 5) == 0) {
                  if (equal_ustring(eq_name, eqkey.name))
                     r->rsprintf("<b>%s</b> &nbsp;&nbsp;", eqkey.name);
                  else {
                     r->rsprintf("<a href=\"?cmd=eqtable&eq=%s\">%s</a> &nbsp;&nbsp;", urlEncode(eqkey.name).c_str(), eqkey.name);
                  }
                  break;
               }
            }
         }
      }
   r->rsprintf("</tr>\n");

   if (!eq_name[0]) {
      r->rsprintf("</table>");
      return;
   }

   /*---- display SC ----*/

   n_var = 0;
   sprintf(str, "/Equipment/%s/Settings/Names", eq_name);
   HNDLE hkeyeqnames;
   db_find_key(hDB, 0, str, &hkeyeqnames);

   if (hkeyeqnames) {

      /*---- single name array ----*/
      r->rsprintf("<tr><td colspan=15><i>Groups:</i> &nbsp;&nbsp;");

      /* "all" group */
      if (equal_ustring(group, "All"))
         r->rsprintf("<b>All</b> &nbsp;&nbsp;");
      else
         r->rsprintf("<a href=\"?cmd=eqtable&eq=%s\">All</a> &nbsp;&nbsp;", urlEncode(eq_name).c_str());

      /* collect groups */

      memset(group_name, 0, sizeof(group_name));
      KEY key;
      db_get_key(hDB, hkeyeqnames, &key);

      for (int level = 0; ; level++) {
         bool next_level = false;
         for (i = 0; i < key.num_values; i++) {
            size = sizeof(str);
            db_get_data_index(hDB, hkeyeqnames, str, &size, i, TID_STRING);

            char *s = strchr(str, '%');
            for (int k=0; s && k<level; k++)
               s = strchr(s+1, '%');

            if (s) {
               *s = 0;
               if (strchr(s+1, '%'))
                   next_level = true;

               //printf("try group [%s] name [%s], level %d, %d\n", str, s+1, level, next_level);

               for (j = 0; j < MAX_GROUPS; j++) {
                  if (equal_ustring(group_name[j], str) || group_name[j][0] == 0)
                     break;
               }
               if (group_name[j][0] == 0)
                  strlcpy(group_name[j], str, sizeof(group_name[0]));
            }
         }

         if (!next_level)
            break;
      }

      for (i = 0; i < MAX_GROUPS && group_name[i][0]; i++) {
         if (equal_ustring(group_name[i], group))
            r->rsprintf("<b>%s</b> &nbsp;&nbsp;", group_name[i]);
         else {
            r->rsprintf("<a href=\"?cmd=eqtable&eq=%s&group=%s\">%s</a> &nbsp;&nbsp;", urlEncode(eq_name).c_str(), urlEncode(group_name[i]).c_str(), group_name[i]);
         }
      }

      r->rsprintf("<i>ODB:</i> &nbsp;&nbsp;");
      r->rsprintf("<a href=\"?cmd=odb&odb_path=Equipment/%s/Common\">Common</a> &nbsp;&nbsp;", urlEncode(eq_name).c_str());
      r->rsprintf("<a href=\"?cmd=odb&odb_path=Equipment/%s/Settings\">Settings</a> &nbsp;&nbsp;", urlEncode(eq_name).c_str());
      r->rsprintf("<a href=\"?cmd=odb&odb_path=Equipment/%s/Variables\">Variables</a> &nbsp;&nbsp;", urlEncode(eq_name).c_str());
      r->rsprintf("</tr>\n");

      /* count variables */
      sprintf(str, "/Equipment/%s/Variables", eq_name);
      HNDLE hkeyvar;
      db_find_key(hDB, 0, str, &hkeyvar);
      if (!hkeyvar) {
         r->rsprintf("</table>");
         return;
      }
      for (i = 0;; i++) {
         HNDLE hkey;
         db_enum_link(hDB, hkeyvar, i, &hkey);
         if (!hkey)
            break;
      }

      if (i == 0 || i > 15) {
         r->rsprintf("</table>");
         return;
      }

      /* title row */
      colspan = 15 - i;
      r->rsprintf("<tr class=\"subStatusTitle\"><th colspan=%d>Names", colspan);

      /* display entries for this group */
      for (int i = 0;; i++) {
         HNDLE hkey;
         db_enum_link(hDB, hkeyvar, i, &hkey);

         if (!hkey)
            break;

         KEY key;
         db_get_key(hDB, hkey, &key);
         r->rsprintf("<th>%s", key.name);
      }

      r->rsprintf("</tr>\n");

      /* data for current group */
      sprintf(str, "/Equipment/%s/Settings/Names", eq_name);
      int num_values = 0;
      HNDLE hkeyset;
      db_find_key(hDB, 0, str, &hkeyset);
      if (hkeyset) {
         KEY key;
         db_get_key(hDB, hkeyset, &key);
         num_values = key.num_values;
      }
      for (int i = 0; i < num_values; i++) {
         size = sizeof(str);
         db_get_data_index(hDB, hkeyset, str, &size, i, TID_STRING);

         strlcpy(name, str, sizeof(name));

         //printf("group [%s], name [%s], str [%s]\n", group, name, str);

         if (!equal_ustring(group, "All")) {
            // check if name starts with the name of the group we want to display
            char *s = strstr(name, group);
            if (s != name)
               continue;
            if (name[strlen(group)] != '%')
               continue;
         }

         if (strlen(name) < 1)
            sprintf(name, "[%d]", i);

         if (i % 2 == 0)
            r->rsprintf("<tr class=\"ODBtableEven\"><td colspan=%d>%s", colspan, name);
         else
            r->rsprintf("<tr class=\"ODBtableOdd\"><td colspan=%d>%s", colspan, name);

         for (int j = 0;; j++) {
            HNDLE hkey;
            db_enum_link(hDB, hkeyvar, j, &hkey);
            if (!hkey)
               break;

            KEY varkey;
            db_get_key(hDB, hkey, &varkey);

            /* check if "variables" array is shorter than the "names" array */
            if (i >= varkey.num_values)
               continue;

            size = sizeof(data);
            db_get_data_index(hDB, hkey, data, &size, i, varkey.type);
            db_sprintf(str, data, varkey.item_size, 0, varkey.type);

            if (is_editable(eq_name, varkey.name)) {
               if (n_var == i_set) {
                  /* set value */
                  strlcpy(str, pp->getparam("value"), sizeof(str));
                  db_sscanf(str, data, &size, 0, varkey.type);
                  db_set_data_index(hDB, hkey, data, size, i, varkey.type);

                  /* redirect (so that 'reload' does not reset value) */
                  r->reset();
                  sprintf(str, "%s", group);
                  redirect(r, str);
                  return;
               }
               if (n_var == i_edit) {
                  r->rsprintf("<td align=center>");
                  r->rsprintf("<input type=text size=10 maxlenth=80 name=value value=\"%s\">\n", str);
                  r->rsprintf("<input type=submit size=20 name=cmd value=Set>\n");
                  r->rsprintf("<input type=hidden name=index value=%d>\n", i_edit);
                  n_var++;
               } else {
                  sprintf(odb_path, "Equipment/%s/Variables/%s[%d]", eq_name, varkey.name, i);
                  r->rsprintf("<td align=center>");
                  r->rsprintf("<a href=\"#\" onClick=\"ODBInlineEdit(this.parentNode,\'%s\', 0);return false;\" >%s</a>", odb_path, str);
                  n_var++;
               }
            } else
               r->rsprintf("<td align=center>%s", str);
         }

         r->rsprintf("</tr>\n");
      }
   } else {
      /*---- multiple name arrays ----*/
      r->rsprintf("<tr><td colspan=15><i>Groups:</i> ");

      /* "all" group */
      if (equal_ustring(group, "All"))
         r->rsprintf("<b>All</b> &nbsp;&nbsp;");
      else
         r->rsprintf("<a href=\"?cmd=eqtable&eq=%s\">All</a> &nbsp;&nbsp;", eq_name);

      /* groups from Variables tree */

      sprintf(str, "/Equipment/%s/Variables", eq_name);
      HNDLE hkeyvar;
      db_find_key(hDB, 0, str, &hkeyvar);

      if (hkeyvar) {
         for (int i = 0;; i++) {
            HNDLE hkey;
            db_enum_link(hDB, hkeyvar, i, &hkey);

            if (!hkey)
               break;

            KEY key;
            db_get_key(hDB, hkey, &key);

            if (equal_ustring(key.name, group)) {
               r->rsprintf("<b>%s</b> &nbsp;&nbsp;", key.name);
            } else {
               r->rsprintf("<a href=\"?cmd=eqtable&eq=%s&group=%s\">%s</a> &nbsp;&nbsp;", urlEncode(eq_name).c_str(), urlEncode(key.name).c_str(), key.name);
            }
         }
      }

      r->rsprintf("<i>ODB:</i> &nbsp;&nbsp;");
      r->rsprintf("<a href=\"?cmd=odb&odb_path=Equipment/%s/Common\">Common</a> &nbsp;&nbsp;", urlEncode(eq_name).c_str());
      r->rsprintf("<a href=\"?cmd=odb&odb_path=Equipment/%s/Settings\">Settings</a> &nbsp;&nbsp;", urlEncode(eq_name).c_str());
      r->rsprintf("<a href=\"?cmd=odb&odb_path=Equipment/%s/Variables\">Variables</a> &nbsp;&nbsp;", urlEncode(eq_name).c_str());
      r->rsprintf("</tr>\n");

      /* enumerate variable arrays */
      line = 0;
      for (i = 0;; i++) {
         HNDLE hkey;
         db_enum_link(hDB, hkeyvar, i, &hkey);

         if (line % 2 == 0)
            strlcpy(style, "ODBtableEven", sizeof(style));
         else
            strlcpy(style, "ODBtableOdd", sizeof(style));

         if (!hkey)
            break;

         KEY varkey;
         db_get_key(hDB, hkey, &varkey);

         if (!equal_ustring(group, "All") && !equal_ustring(varkey.name, group))
            continue;

         /* title row */
         r->rsprintf("<tr class=\"subStatusTitle\"><th colspan=9>Names<th>%s</tr>\n", varkey.name);

         if (varkey.type == TID_KEY) {
            HNDLE hkeyroot = hkey;

            /* enumerate subkeys */
            for (j = 0;; j++) {
               db_enum_key(hDB, hkeyroot, j, &hkey);
               if (!hkey)
                  break;

               KEY key;
               db_get_key(hDB, hkey, &key);

               if (key.type == TID_KEY) {
                  /* for keys, don't display data value */
                  r->rsprintf("<tr class=\"%s\"><td colspan=9>%s<br></tr>\n", style, key.name);
               } else {
                  /* display single value */
                  if (key.num_values == 1) {
                     size = sizeof(data);
                     db_get_data(hDB, hkey, data, &size, key.type);
                     db_sprintf(data_str, data, key.item_size, 0, key.type);
                     db_sprintfh(hex_str, data, key.item_size, 0, key.type);

                     if (data_str[0] == 0 || equal_ustring(data_str, "<NULL>")) {
                        strcpy(data_str, "(empty)");
                        hex_str[0] = 0;
                     }

                     if (strcmp(data_str, hex_str) != 0 && hex_str[0])
                        r->rsprintf
                            ("<tr class=\"%s\" ><td colspan=9>%s<td align=center>%s (%s)<br></tr>\n",
                             style, key.name, data_str, hex_str);
                     else
                        r->rsprintf("<tr class=\"%s\"><td colspan=9>%s<td align=center>%s<br></tr>\n",
                                 style, key.name, data_str);
                     line++;
                  } else {
                     /* display first value */
                     r->rsprintf("<tr class=\"%s\"><td colspan=9 rowspan=%d>%s\n", style, key.num_values,
                              key.name);

                     for (k = 0; k < key.num_values; k++) {
                        size = sizeof(data);
                        db_get_data_index(hDB, hkey, data, &size, k, key.type);
                        db_sprintf(data_str, data, key.item_size, 0, key.type);
                        db_sprintfh(hex_str, data, key.item_size, 0, key.type);

                        if (data_str[0] == 0 || equal_ustring(data_str, "<NULL>")) {
                           strcpy(data_str, "(empty)");
                           hex_str[0] = 0;
                        }

                        if (k > 0)
                           r->rsprintf("<tr>");

                        if (strcmp(data_str, hex_str) != 0 && hex_str[0])
                           r->rsprintf("<td>[%d] %s (%s)<br></tr>\n", k, data_str, hex_str);
                        else
                           r->rsprintf("<td>[%d] %s<br></tr>\n", k, data_str);
                        line++;
                     }
                  }
               }
            }
         } else {
            /* data for current group */
            sprintf(str, "/Equipment/%s/Settings/Names %s", eq_name, varkey.name);
            HNDLE hkeyset;
            db_find_key(hDB, 0, str, &hkeyset);
            KEY key;
            if (hkeyset)
               db_get_key(hDB, hkeyset, &key);

            if (varkey.num_values > 1000)
               r->rsprintf("<tr class=\"%s\"><td colspan=9>%s<td align=center><i>... %d values ...</i>",
                        style, varkey.name, varkey.num_values);
            else {
               for (j = 0; j < varkey.num_values; j++) {

                  if (line % 2 == 0)
                     strlcpy(style, "ODBtableEven", sizeof(style));
                  else
                     strlcpy(style, "ODBtableOdd", sizeof(style));

                  if (hkeyset && j<key.num_values) {
                     size = sizeof(name);
                     db_get_data_index(hDB, hkeyset, name, &size, j, TID_STRING);
                  } else {
                     sprintf(name, "%s[%d]", varkey.name, j);
                  }

                  if (strlen(name) < 1) {
                     sprintf(name, "%s[%d]", varkey.name, j);
                  }

                  r->rsprintf("<tr class=\"%s\"><td colspan=9>%s", style, name);

                  size = sizeof(data);
                  db_get_data_index(hDB, hkey, data, &size, j, varkey.type);
                  db_sprintf(str, data, varkey.item_size, 0, varkey.type);

                  if (is_editable(eq_name, varkey.name)) {
                     if (n_var == i_set) {
                        /* set value */
                        strlcpy(str, pp->getparam("value"), sizeof(str));
                        db_sscanf(str, data, &size, 0, varkey.type);
                        db_set_data_index(hDB, hkey, data, size, j, varkey.type);

                        /* redirect (so that 'reload' does not reset value) */
                        r->reset();
                        sprintf(str, "%s", group);
                        redirect(r, str);
                        return;
                     }
                     if (n_var == i_edit) {
                        r->rsprintf("<td align=center><input type=text size=10 maxlenth=80 name=value value=\"%s\">\n", str);
                        r->rsprintf("<input type=submit size=20 name=cmd value=Set></tr>\n");
                        r->rsprintf("<input type=hidden name=index value=%d>\n", i_edit);
                        r->rsprintf("<input type=hidden name=cmd value=Set>\n");
                        n_var++;
                     } else {
                        sprintf(odb_path, "Equipment/%s/Variables/%s[%d]", eq_name, varkey.name, j);

                        r->rsprintf("<td align=cernter>");
                        r->rsprintf("<a href=\"#\" onClick=\"ODBInlineEdit(this.parentNode,\'%s\', 0);return false;\" >%s</a>", odb_path, str);
                        n_var++;
                     }

                  } else
                     r->rsprintf("<td align=center>%s\n", str);
                  r->rsprintf("</tr>\n");
                  line++;
               }
            }

            r->rsprintf("</tr>\n");
         }
      }
   }

   r->rsprintf("</table>\n");
   r->rsprintf("</div>\n"); // closing for <div id="mmain">
   r->rsprintf("</form>\n");
   r->rsprintf("</body></html>\r\n");
}

/*------------------------------------------------------------------*/

char *find_odb_tag(char *p, char *path, char *format, int *edit, char *type, char *pwd, char *tail)
{
   char str[256], *ps, *pt;
   BOOL in_script;

   *edit = 0;
   *tail = 0;
   *format = 0;
   pwd[0] = 0;
   in_script = FALSE;
   strcpy(type, "text");
   do {
      while (*p && *p != '<')
         p++;

      /* return if end of string reached */
      if (!*p)
         return NULL;

      p++;
      while (*p && ((*p == ' ') || iscntrl(*p)))
         p++;

      strncpy(str, p, 6);
      str[6] = 0;
      if (equal_ustring(str, "script"))
         in_script = TRUE;

      strncpy(str, p, 7);
      str[7] = 0;
      if (equal_ustring(str, "/script"))
         in_script = FALSE;

      strncpy(str, p, 4);
      str[4] = 0;
      if (equal_ustring(str, "odb ")) {
         ps = p - 1;
         p += 4;
         while (*p && ((*p == ' ') || iscntrl(*p)))
            p++;

         do {
            strncpy(str, p, 7);
            str[7] = 0;
            if (equal_ustring(str, "format=")) {
               p += 7;
               if (*p == '\"') {
                  p++;
                  while (*p && *p != '\"')
                     *format++ = *p++;
                  *format = 0;
                  if (*p == '\"')
                    p++;
               } else {
                  while (*p && *p != ' ' && *p != '>')
                     *format++ = *p++;
                  *format = 0;
               }

            } else {

            strncpy(str, p, 4);
            str[4] = 0;
            if (equal_ustring(str, "src=")) {
               p += 4;
               if (*p == '\"') {
                  p++;
                  while (*p && *p != '\"')
                     *path++ = *p++;
                  *path = 0;
                  if (*p == '\"')
                    p++;
               } else {
                  while (*p && *p != ' ' && *p != '>')
                     *path++ = *p++;
                  *path = 0;
               }
            } else {

               if (in_script)
                  break;

               strncpy(str, p, 5);
               str[5] = 0;
               if (equal_ustring(str, "edit=")) {
                  p += 5;

                  if (*p == '\"') {
                     p++;
                     *edit = atoi(p);
                     if (*p == '\"')
                       p++;
                  } else {
                     *edit = atoi(p);
                     while (*p && *p != ' ' && *p != '>')
                        p++;
                  }

               } else {

                  strncpy(str, p, 5);
                  str[5] = 0;
                  if (equal_ustring(str, "type=")) {
                     p += 5;
                     if (*p == '\"') {
                        p++;
                        while (*p && *p != '\"')
                           *type++ = *p++;
                        *type = 0;
                        if (*p == '\"')
                          p++;
                     } else {
                        while (*p && *p != ' ' && *p != '>')
                           *type++ = *p++;
                        *type = 0;
                     }
                  } else {
                     strncpy(str, p, 4);
                     str[4] = 0;
                     if (equal_ustring(str, "pwd=")) {
                        p += 4;
                        if (*p == '\"') {
                           p++;
                           while (*p && *p != '\"')
                              *pwd++ = *p++;
                           *pwd = 0;
                           if (*p == '\"')
                             p++;
                        } else {
                           while (*p && *p != ' ' && *p != '>')
                              *pwd++ = *p++;
                           *pwd = 0;
                        }
                     } else {
                        if (strchr(p, '=')) {
                           strlcpy(str, p, sizeof(str));
                           pt = strchr(str, '=')+1;
                           if (*pt == '\"') {
                              pt++;
                              while (*pt && *pt != '\"')
                                 pt++;
                              if (*pt == '\"')
                                 pt++;
                              *pt = 0;
                           } else {
                              while (*pt && *pt != ' ' && *pt != '>')
                                 pt++;
                              *pt = 0;
                           }
                           if (tail[0]) {
                              strlcat(tail, " ", 256);
                              strlcat(tail, str, 256);
                           } else {
                              strlcat(tail, str, 256);
                           }
                           p += strlen(str);
                        }
                     }
                  }
               }
            }
            }

            while (*p && ((*p == ' ') || iscntrl(*p)))
               p++;

            if (*p == '<') {
               cm_msg(MERROR, "find_odb_tag", "Invalid odb tag '%s'", ps);
               return NULL;
            }
         } while (*p != '>');

         return ps;
      }

      while (*p && *p != '>')
         p++;

   } while (1);

}

/*------------------------------------------------------------------*/

void show_odb_tag(Param* pp, Return* r, const char *path, const char *keypath1, const char *format, int n_var, int edit, char *type, char *pwd, char *tail)
{
   int size, index, i_edit, i_set;
   char str[TEXT_SIZE], data[TEXT_SIZE], options[1000], full_keypath[256], keypath[256], *p;
   HNDLE hDB, hkey;
   KEY key;

   /* check if variable to edit */
   i_edit = -1;
   if (equal_ustring(pp->getparam("cmd"), "Edit"))
      i_edit = atoi(pp->getparam("index"));

   /* check if variable to set */
   i_set = -1;
   if (equal_ustring(pp->getparam("cmd"), "Set"))
      i_set = atoi(pp->getparam("index"));

   /* check if path contains index */
   strlcpy(full_keypath, keypath1, sizeof(full_keypath));
   strlcpy(keypath, keypath1, sizeof(keypath));
   index = 0;

   if (strchr(keypath, '[') && strchr(keypath, ']')) {
      for (p = strchr(keypath, '[') + 1; *p && *p != ']'; p++)
         if (!isdigit(*p))
            break;

      if (*p && *p == ']') {
         index = atoi(strchr(keypath, '[') + 1);
         *strchr(keypath, '[') = 0;
      }
   }

   cm_get_experiment_database(&hDB, NULL);
   db_find_key(hDB, 0, keypath, &hkey);
   if (!hkey)
      r->rsprintf("<b>Key \"%s\" not found in ODB</b>\n", keypath);
   else {
      db_get_key(hDB, hkey, &key);
      size = sizeof(data);
      db_get_data_index(hDB, hkey, data, &size, index, key.type);

      if (format && strlen(format)>0)
         db_sprintff(str, format, data, key.item_size, 0, key.type);
      else
         db_sprintf(str, data, key.item_size, 0, key.type);

      if (equal_ustring(type, "checkbox")) {

         if (pp->isparam("cbi"))
            i_set = atoi(pp->getparam("cbi"));
         if (n_var == i_set) {
            /* toggle state */
            if (key.type == TID_BOOL) {
               if (str[0] == 'y')
                  strcpy(str, "n");
               else
                  strcpy(str, "y");
            } else {
               if (atoi(str) > 0)
                  strcpy(str, "0");
               else
                  strcpy(str, "1");
            }

            db_sscanf(str, data, &size, 0, key.type);
            db_set_data_index(hDB, hkey, data, size, index, key.type);
         }

         options[0] = 0;
         if (str[0] == 'y' || atoi(str) > 0)
            strcat(options, "checked ");
         if (!edit)
            strcat(options, "disabled ");
         else {
            if (edit == 1) {
               strlcat(options, "onClick=\"o=document.createElement('input');o.type='hidden';o.name='cbi';o.value='", sizeof(options));
               sprintf(options+strlen(options), "%d", n_var);
               strlcat(options, "';document.form1.appendChild(o);", sizeof(options));
               strlcat(options, "document.form1.submit();\" ", sizeof(options));
            }
         }

         if (tail[0])
            strlcat(options, tail, sizeof(options));

         r->rsprintf("<input type=\"checkbox\" %s>\n", options);

      } else { // checkbox

         if (edit == 1) {
            if (n_var == i_set) {
               /* set value */
               strlcpy(str, pp->getparam("value"), sizeof(str));
               db_sscanf(str, data, &size, 0, key.type);
               db_set_data_index(hDB, hkey, data, size, index, key.type);

               /* read back value */
               size = sizeof(data);
               db_get_data_index(hDB, hkey, data, &size, index, key.type);
               db_sprintf(str, data, key.item_size, 0, key.type);
            }

            if (n_var == i_edit) {
               r->rsprintf("<input type=text size=10 maxlength=80 name=value value=\"%s\">\n",
                        str);
               r->rsprintf("<input type=submit size=20 name=cmd value=Set>\n");
               r->rsprintf("<input type=hidden name=index value=%d>\n", n_var);
               r->rsprintf("<input type=hidden name=cmd value=Set>\n");
            } else {
               if (edit == 2) {
                  /* edit handling through user supplied JavaScript */
                  r->rsprintf("<a href=\"#\" %s>", tail);
               } else {
                  /* edit handling through form submission */
                  if (pwd[0]) {
                     r->rsprintf("<a onClick=\"promptpwd('%s?cmd=Edit&index=%d&pnam=%s')\" href=\"#\">", path, n_var, pwd);
                  } else {
                     r->rsprintf("<a href=\"%s?cmd=Edit&index=%d\" %s>", path, n_var, tail);
                  }
               }

               r->rsputs(str);
               r->rsprintf("</a>");
            }
         } else if (edit == 2) {
            r->rsprintf("<a href=\"#\" onclick=\"ODBEdit('%s')\">\n", full_keypath);
            r->rsputs(str);
            r->rsprintf("</a>");
         }
           else
            r->rsputs(str);
      }
   }
}

/*------------------------------------------------------------------*/

/* add labels using following syntax under /Custom/Images/<name.gif>/Labels/<name>:

   [Name]    [Description]                       [Example]

   Src       ODB path for vairable to display    /Equipment/Environment/Variables/Input[0]
   Format    Formt for float/double              %1.2f Deg. C
   Font      Font to use                         small | medium | giant
   X         X-position in pixel                 90
   Y         Y-position from top                 67
   Align     horizontal align left/center/right  left
   FGColor   Foreground color RRGGBB             000000
   BGColor   Background color RRGGBB             FFFFFF
*/

static const char *cgif_label_str[] = {
   "Src = STRING : [256] ",
   "Format = STRING : [32] %1.1f",
   "Font = STRING : [32] Medium",
   "X = INT : 0",
   "Y = INT : 0",
   "Align = INT : 0",
   "FGColor = STRING : [8] 000000",
   "BGColor = STRING : [8] FFFFFF",
   NULL
};

typedef struct {
   char src[256];
   char format[32];
   char font[32];
   int x, y, align;
   char fgcolor[8];
   char bgcolor[8];
} CGIF_LABEL;

/* add labels using following syntax under /Custom/Images/<name.gif>/Bars/<name>:

   [Name]    [Description]                       [Example]

   Src       ODB path for vairable to display    /Equipment/Environment/Variables/Input[0]
   X         X-position in pixel                 90
   Y         Y-position from top                 67
   Width     Width in pixel                      20
   Height    Height in pixel                     100
   Direction 0(vertical)/1(horiz.)               0
   Axis      Draw axis 0(none)/1(left)/2(right)  1
   Logscale  Draw logarithmic axis               n
   Min       Min value for axis                  0
   Max       Max value for axis                  10
   FGColor   Foreground color RRGGBB             000000
   BGColor   Background color RRGGBB             FFFFFF
   BDColor   Border color RRGGBB                 808080
*/

static const char *cgif_bar_str[] = {
   "Src = STRING : [256] ",
   "X = INT : 0",
   "Y = INT : 0",
   "Width = INT : 10",
   "Height = INT : 100",
   "Direction = INT : 0",
   "Axis = INT : 1",
   "Logscale = BOOL : n",
   "Min = DOUBLE : 0",
   "Max = DOUBLE : 10",
   "FGColor = STRING : [8] 000000",
   "BGColor = STRING : [8] FFFFFF",
   "BDColor = STRING : [8] 808080",
   NULL
};

typedef struct {
   char src[256];
   int x, y, width, height, direction, axis;
   BOOL logscale;
   double min, max;
   char fgcolor[8];
   char bgcolor[8];
   char bdcolor[8];
} CGIF_BAR;

/*------------------------------------------------------------------*/

int evaluate_src(char *key, char *src, double *fvalue)
{
   HNDLE hDB, hkeyval;
   KEY vkey;
   int i, n, size, ivalue;
   char str[256], data[256], value[256];

   cm_get_experiment_database(&hDB, NULL);

   /* separate source from operators */
   for (i=0 ; i<(int)strlen(src) ; i++)
      if (src[i] == '>' || src[i] == '&')
         break;
   strncpy(str, src, i);
   str[i] = 0;

   /* strip trailing blanks */
   while (strlen(str) > 0 && str[strlen(str)-1] == ' ')
      str[strlen(str)-1] = 0;

   db_find_key(hDB, 0, str, &hkeyval);
   if (!hkeyval) {
      cm_msg(MERROR, "evaluate_src", "Invalid Src key \"%s\" for Fill \"%s\"",
             src, key);
      return 0;
   }

   db_get_key(hDB, hkeyval, &vkey);
   size = sizeof(data);
   db_get_value(hDB, 0, src, data, &size, vkey.type, FALSE);
   db_sprintf(value, data, size, 0, vkey.type);
   if (equal_ustring(value, "NAN"))
      return 0;

   if (vkey.type == TID_BOOL) {
      *fvalue = (value[0] == 'y');
   } else
      *fvalue = atof(value);

   /* evaluate possible operators */
   do {
      if (src[i] == '>' && src[i+1] == '>') {
         i+=2;
         n = atoi(src+i);
         while (src[i] == ' ' || isdigit(src[i]))
            i++;
         ivalue = (int)*fvalue;
         ivalue >>= n;
         *fvalue = ivalue;
      }

      if (src[i] == '&') {
         i+=1;
         while (src[i] == ' ')
            i++;
         if (src[i] == '0' && src[i+1] == 'x')
            sscanf(src+2+i, "%x", &n);
         else
            n = atoi(src+i);
         while (src[i] == ' ' || isxdigit(src[i]) || src[i] == 'x')
            i++;
         ivalue = (int)*fvalue;
         ivalue &= n;
         *fvalue = ivalue;
      }

   } while (src[i]);

   return 1;
}

/*------------------------------------------------------------------*/

std::string add_custom_path(const std::string& filename)
{
   // do not append custom path to absolute filenames

   if (filename[0] == '/')
      return filename;
   if (filename[0] == DIR_SEPARATOR)
      return filename;

   HNDLE hDB;
   cm_get_experiment_database(&hDB, NULL);

   std::string custom_path = "";

   int status = db_get_value_string(hDB, 0, "/Custom/Path", 0, &custom_path, TRUE);

   if (status != DB_SUCCESS)
      return filename;

   if (custom_path.length() < 1)
      return filename;

   if ((custom_path == "/") || !strchr(custom_path.c_str(), DIR_SEPARATOR)) {
      cm_msg(MERROR, "add_custom_path", "ODB /Custom/Path has a forbidden value \"%s\", please change it", custom_path.c_str());
      return filename;
   }


   std::string full_filename = custom_path;
   if (full_filename[full_filename.length()-1] != DIR_SEPARATOR)
      full_filename += DIR_SEPARATOR_STR;
   full_filename += filename;

   return full_filename;
}

/*------------------------------------------------------------------*/

void show_custom_file(Return* r, const char *name)
{
   char str[256];
   std::string filename;
   HNDLE hDB;

   cm_get_experiment_database(&hDB, NULL);

   HNDLE hkey;
   sprintf(str, "/Custom/%s", name);
   db_find_key(hDB, 0, str, &hkey);

   if (!hkey) {
      sprintf(str, "/Custom/%s&", name);
      db_find_key(hDB, 0, str, &hkey);
      if (!hkey) {
         sprintf(str, "/Custom/%s!", name);
         db_find_key(hDB, 0, str, &hkey);
      }
   }

   if(!hkey){
      sprintf(str,"show_custom_file: Invalid custom page: \"/Custom/%s\" not found in ODB", name);
      show_error_404(r, str);
      return;
   }

   int status;
   KEY key;

   status = db_get_key(hDB, hkey, &key);

   if (status != DB_SUCCESS) {
      char errtext[512];
      sprintf(errtext, "show_custom_file: Error: db_get_key() for \"%s\" status %d", str, status);
      show_error_404(r, errtext);
      return;
   }

   int size = key.total_size;
   char* ctext = (char*)malloc(size);

   status = db_get_data(hDB, hkey, ctext, &size, TID_STRING);

   if (status != DB_SUCCESS) {
      char errtext[512];
      sprintf(errtext, "show_custom_file: Error: db_get_data() for \"%s\" status %d", str, status);
      show_error_404(r, errtext);
      free(ctext);
      return;
   }

   filename = add_custom_path(ctext);

   free(ctext);

   send_file(r, filename, true);

   return;
}

/*------------------------------------------------------------------*/

void show_custom_gif(Return* rr, const char *name)
{
   char str[256], data[256], value[256], src[256];
   int i, index, length, status, size, width, height, bgcol, fgcol, bdcol, r, g, b, x, y;
   HNDLE hDB, hkeygif, hkeyroot, hkey, hkeyval;
   double fvalue, ratio;
   KEY key, vkey;
   gdImagePtr im;
   gdGifBuffer gb;
   gdFontPtr pfont;
   FILE *f;
   CGIF_LABEL label;
   CGIF_BAR bar;

   cm_get_experiment_database(&hDB, NULL);

   /* find image description in ODB */
   sprintf(str, "/Custom/Images/%s", name);
   db_find_key(hDB, 0, str, &hkeygif);
   if (!hkeygif) {

      // If we don't have Images directory,
      // then just treat this like any other custom file.
      show_custom_file(rr, name);
      return;
   }

   /* load background image */
   std::string filename;
   db_get_value_string(hDB, hkeygif, "Background", 0, &filename, FALSE);

   std::string full_filename = add_custom_path(filename);

   f = fopen(full_filename.c_str(), "rb");
   if (f == NULL) {
      sprintf(str, "show_custom_gif: Cannot open file \"%s\"", full_filename.c_str());
      show_error_404(rr, str);
      return;
   }

   im = gdImageCreateFromGif(f);
   fclose(f);

   if (im == NULL) {
      sprintf(str, "show_custom_gif: File \"%s\" is not a GIF image", filename.c_str());
      show_error_404(rr, str);
      return;
   }

   cm_get_experiment_database(&hDB, NULL);

   /*---- draw labels ----------------------------------------------*/

   db_find_key(hDB, hkeygif, "Labels", &hkeyroot);
   if (hkeyroot) {
      for (index = 0;; index++) {
         db_enum_key(hDB, hkeyroot, index, &hkey);
         if (!hkey)
            break;
         db_get_key(hDB, hkey, &key);

         size = sizeof(label);
         status = db_get_record1(hDB, hkey, &label, &size, 0, strcomb1(cgif_label_str).c_str());
         if (status != DB_SUCCESS) {
            cm_msg(MERROR, "show_custom_gif", "Cannot open data record for label \"%s\"",
                   key.name);
            continue;
         }

         if (label.src[0] == 0) {
            cm_msg(MERROR, "show_custom_gif", "Empty Src key for label \"%s\"", key.name);
            continue;
         }

         db_find_key(hDB, 0, label.src, &hkeyval);
         if (!hkeyval) {
            cm_msg(MERROR, "show_custom_gif", "Invalid Src key \"%s\" for label \"%s\"",
                   label.src, key.name);
            continue;
         }

         db_get_key(hDB, hkeyval, &vkey);
         size = sizeof(data);
         status = db_get_value(hDB, 0, label.src, data, &size, vkey.type, FALSE);

         if (label.format[0]) {
            if (vkey.type == TID_FLOAT)
               sprintf(value, label.format, *(((float *) data)));
            else if (vkey.type == TID_DOUBLE)
               sprintf(value, label.format, *(((double *) data)));
            else if (vkey.type == TID_INT)
               sprintf(value, label.format, *(((INT *) data)));
            else if (vkey.type == TID_BOOL) {
               if (strstr(label.format, "%c"))
                  sprintf(value, label.format, *(((INT *) data)) ? 'y' : 'n');
               else
                  sprintf(value, label.format, *(((INT *) data)));
            } else
               db_sprintf(value, data, size, 0, vkey.type);
         } else
            db_sprintf(value, data, size, 0, vkey.type);

         sscanf(label.fgcolor, "%02x%02x%02x", &r, &g, &b);
         fgcol = gdImageColorAllocate(im, r, g, b);
         if (fgcol == -1)
            fgcol = gdImageColorClosest(im, r, g, b);

         sscanf(label.bgcolor, "%02x%02x%02x", &r, &g, &b);
         bgcol = gdImageColorAllocate(im, r, g, b);
         if (bgcol == -1)
            bgcol = gdImageColorClosest(im, r, g, b);

         /* select font */
         if (equal_ustring(label.font, "Small"))
            pfont = gdFontSmall;
         else if (equal_ustring(label.font, "Medium"))
            pfont = gdFontMediumBold;
         else if (equal_ustring(label.font, "Giant"))
            pfont = gdFontGiant;
         else
            pfont = gdFontMediumBold;

         width = strlen(value) * pfont->w + 5 + 5;
         height = pfont->h + 2 + 2;

         if (label.align == 0) {
            /* left */
            gdImageFilledRectangle(im, label.x, label.y, label.x + width,
                                   label.y + height, bgcol);
            gdImageRectangle(im, label.x, label.y, label.x + width, label.y + height,
                             fgcol);
            gdImageString(im, pfont, label.x + 5, label.y + 2, value, fgcol);
         } else if (label.align == 1) {
            /* center */
            gdImageFilledRectangle(im, label.x - width / 2, label.y, label.x + width / 2,
                                   label.y + height, bgcol);
            gdImageRectangle(im, label.x - width / 2, label.y, label.x + width / 2,
                             label.y + height, fgcol);
            gdImageString(im, pfont, label.x + 5 - width / 2, label.y + 2, value, fgcol);
         } else {
            /* right */
            gdImageFilledRectangle(im, label.x - width, label.y, label.x,
                                   label.y + height, bgcol);
            gdImageRectangle(im, label.x - width, label.y, label.x, label.y + height,
                             fgcol);
            gdImageString(im, pfont, label.x - width + 5, label.y + 2, value, fgcol);
         }
      }
   }

   /*---- draw bars ------------------------------------------------*/

   db_find_key(hDB, hkeygif, "Bars", &hkeyroot);
   if (hkeyroot) {
      for (index = 0;; index++) {
         db_enum_key(hDB, hkeyroot, index, &hkey);
         if (!hkey)
            break;
         db_get_key(hDB, hkey, &key);

         size = sizeof(bar);
         status = db_get_record1(hDB, hkey, &bar, &size, 0, strcomb1(cgif_bar_str).c_str());
         if (status != DB_SUCCESS) {
            cm_msg(MERROR, "show_custom_gif", "Cannot open data record for bar \"%s\"",
                   key.name);
            continue;
         }

         if (bar.src[0] == 0) {
            cm_msg(MERROR, "show_custom_gif", "Empty Src key for bar \"%s\"", key.name);
            continue;
         }

         db_find_key(hDB, 0, bar.src, &hkeyval);
         if (!hkeyval) {
            cm_msg(MERROR, "show_custom_gif", "Invalid Src key \"%s\" for bar \"%s\"",
                   bar.src, key.name);
            continue;
         }

         db_get_key(hDB, hkeyval, &vkey);
         size = sizeof(data);
         status = db_get_value(hDB, 0, bar.src, data, &size, vkey.type, FALSE);
         db_sprintf(value, data, size, 0, vkey.type);
         if (equal_ustring(value, "NAN"))
            continue;

         fvalue = atof(value);

         sscanf(bar.fgcolor, "%02x%02x%02x", &r, &g, &b);
         fgcol = gdImageColorAllocate(im, r, g, b);
         if (fgcol == -1)
            fgcol = gdImageColorClosest(im, r, g, b);

         sscanf(bar.bgcolor, "%02x%02x%02x", &r, &g, &b);
         bgcol = gdImageColorAllocate(im, r, g, b);
         if (bgcol == -1)
            bgcol = gdImageColorClosest(im, r, g, b);

         sscanf(bar.bdcolor, "%02x%02x%02x", &r, &g, &b);
         bdcol = gdImageColorAllocate(im, r, g, b);
         if (bdcol == -1)
            bdcol = gdImageColorClosest(im, r, g, b);

         if (bar.min == bar.max)
            bar.max += 1;

         if (bar.logscale) {
            if (fvalue < 1E-20)
               fvalue = 1E-20;
            ratio = (log(fvalue) - log(bar.min)) / (log(bar.max) - log(bar.min));
         } else
            ratio = (fvalue - bar.min) / (bar.max - bar.min);
         if (ratio < 0)
            ratio = 0;
         if (ratio > 1)
            ratio = 1;

         if (bar.direction == 0) {
            /* vertical */
            ratio = (bar.height - 2) - ratio * (bar.height - 2);
            r = (int) (ratio + 0.5);

            gdImageFilledRectangle(im, bar.x, bar.y, bar.x + bar.width,
                                   bar.y + bar.height, bgcol);
            gdImageRectangle(im, bar.x, bar.y, bar.x + bar.width, bar.y + bar.height,
                             bdcol);
            gdImageFilledRectangle(im, bar.x + 1, bar.y + r + 1, bar.x + bar.width - 1,
                                   bar.y + bar.height - 1, fgcol);

            if (bar.axis == 1)
               vaxis(im, gdFontSmall, bdcol, 0, bar.x, bar.y + bar.height, bar.height, -3,
                     -5, -7, -8, 0, bar.min, bar.max, bar.logscale);
            else if (bar.axis == 2)
               vaxis(im, gdFontSmall, bdcol, 0, bar.x + bar.width, bar.y + bar.height,
                     bar.height, 3, 5, 7, 10, 0, bar.min, bar.max, bar.logscale);

         } else {
            /* horizontal */
            ratio = ratio * (bar.height - 2);
            r = (int) (ratio + 0.5);

            gdImageFilledRectangle(im, bar.x, bar.y, bar.x + bar.height,
                                   bar.y + bar.width, bgcol);
            gdImageRectangle(im, bar.x, bar.y, bar.x + bar.height, bar.y + bar.width,
                             bdcol);
            gdImageFilledRectangle(im, bar.x + 1, bar.y + 1, bar.x + r,
                                   bar.y + bar.width - 1, fgcol);

            if (bar.axis == 1)
               haxis(im, gdFontSmall, bdcol, 0, bar.x, bar.y, bar.height, -3, -5, -7, -18,
                     0, bar.min, bar.max);
            else if (bar.axis == 2)
               haxis(im, gdFontSmall, bdcol, 0, bar.x, bar.y + bar.width, bar.height, 3,
                     5, 7, 8, 0, bar.min, bar.max);
         }
      }
   }

   /*---- draw fills -----------------------------------------------*/

   db_find_key(hDB, hkeygif, "Fills", &hkeyroot);
   if (hkeyroot) {
      for (index = 0;; index++) {
         db_enum_key(hDB, hkeyroot, index, &hkey);
         if (!hkey)
            break;
         db_get_key(hDB, hkey, &key);

         size = sizeof(src);
         src[0] = 0;
         db_get_value(hDB, hkey, "Src", src, &size, TID_STRING, TRUE);

         if (src[0] == 0) {
            cm_msg(MERROR, "show_custom_gif", "Empty Src key for Fill \"%s\"", key.name);
            continue;
         }

         if (!evaluate_src(key.name, src, &fvalue))
            continue;

         x = y = 0;
         size = sizeof(x);
         db_get_value(hDB, hkey, "X", &x, &size, TID_INT, TRUE);
         db_get_value(hDB, hkey, "Y", &y, &size, TID_INT, TRUE);

         size = sizeof(data);
         status = db_get_value(hDB, hkey, "Limits", data, &size, TID_DOUBLE, FALSE);
         if (status != DB_SUCCESS) {
            cm_msg(MERROR, "show_custom_gif", "No \"Limits\" entry for Fill \"%s\"",
                   key.name);
            continue;
         }
         for (i = 0; i < size / (int) sizeof(double); i++)
            if (*((double *) data + i) > fvalue)
               break;
         if (i > 0)
            i--;

         db_find_key(hDB, hkey, "Fillcolors", &hkeyval);
         if (!hkeyval) {
            cm_msg(MERROR, "show_custom_gif", "No \"Fillcolors\" entry for Fill \"%s\"",
                   key.name);
            continue;
         }

         size = sizeof(data);
         strcpy(data, "FFFFFF");
         status = db_get_data_index(hDB, hkeyval, data, &size, i, TID_STRING);
         if (status == DB_SUCCESS) {
            sscanf(data, "%02x%02x%02x", &r, &g, &b);
            fgcol = gdImageColorAllocate(im, r, g, b);
            if (fgcol == -1)
               fgcol = gdImageColorClosest(im, r, g, b);
            gdImageFill(im, x, y, fgcol);
         }
      }
   }

   /* generate GIF */
   gdImageInterlace(im, 1);
   gdImageGif(im, &gb);
   gdImageDestroy(im);
   length = gb.size;

   rr->rsprintf("HTTP/1.1 200 Document follows\r\n");
   rr->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());

   rr->rsprintf("Content-Type: image/gif\r\n");
   rr->rsprintf("Content-Length: %d\r\n", length);
   rr->rsprintf("Cache-control: private, max-age=0, no-cache\r\n");
   rr->rsprintf("Expires: Fri, 01-Jan-1983 00:00:00 GMT\r\n\r\n");

   rr->rmemcpy(gb.data, length);
}



/*------------------------------------------------------------------*/

void do_jrpc_rev0(Param* p, Return* r)
{
   static RPC_LIST rpc_list[] = {
      { 9999, "mhttpd_jrpc_rev0", {
            {TID_STRING, RPC_IN}, // arg0
            {TID_STRING, RPC_IN}, // arg1
            {TID_STRING, RPC_IN}, // arg2
            {TID_STRING, RPC_IN}, // arg3
            {TID_STRING, RPC_IN}, // arg4
            {TID_STRING, RPC_IN}, // arg5
            {TID_STRING, RPC_IN}, // arg6
            {TID_STRING, RPC_IN}, // arg7
            {TID_STRING, RPC_IN}, // arg8
            {TID_STRING, RPC_IN}, // arg9
            {0}} },
      { 0 }
   };

   int count = 0, substring = 0, rpc;

   const char *xname   = p->getparam("name");
   const char *srpc    = p->getparam("rpc");

   if (!srpc || !xname) {
      show_text_header(r);
      r->rsprintf("<INVALID_ARGUMENTS>");
      return;
   }

   char sname[256];
   strlcpy(sname, xname, sizeof(sname));

   if (sname[strlen(sname)-1]=='*') {
      sname[strlen(sname)-1] = 0;
      substring = 1;
   }

   rpc = atoi(srpc);

   if (rpc<RPC_MIN_ID || rpc>RPC_MAX_ID) {
      show_text_header(r);
      r->rsprintf("<INVALID_RPC_ID>");
      return;
   }

   rpc_list[0].id = rpc;
   rpc_register_functions(rpc_list, NULL);

   show_text_header(r);
   r->rsprintf("calling rpc %d | ", rpc);

   if (1) {
      int status, i;
      char str[256];
      HNDLE hDB, hrootkey, hsubkey, hkey;

      cm_get_experiment_database(&hDB, NULL);

      /* find client which exports FCNA function */
      status = db_find_key(hDB, 0, "System/Clients", &hrootkey);
      if (status == DB_SUCCESS) {
         for (i=0; ; i++) {
            status = db_enum_key(hDB, hrootkey, i, &hsubkey);
            if (status == DB_NO_MORE_SUBKEYS)
               break;

            sprintf(str, "RPC/%d", rpc);
            status = db_find_key(hDB, hsubkey, str, &hkey);
            if (status == DB_SUCCESS) {
               char client_name[NAME_LENGTH];
               HNDLE hconn;
               int size;

               size = sizeof(client_name);
               status = db_get_value(hDB, hsubkey, "Name", client_name, &size, TID_STRING, FALSE);
               if (status != DB_SUCCESS)
                  continue;

               if (strlen(sname) > 0) {
                  if (substring) {
                     if (strstr(client_name, sname) != client_name)
                        continue;
                  } else {
                     if (strcmp(sname, client_name) != 0)
                        continue;
                  }
               }

               count++;

               r->rsprintf("client %s", client_name);

               status = cm_connect_client(client_name, &hconn);
               r->rsprintf(" %d", status);

               if (status == RPC_SUCCESS) {
                  status = rpc_client_call(hconn, rpc,
                                           p->getparam("arg0"),
                                           p->getparam("arg1"),
                                           p->getparam("arg2"),
                                           p->getparam("arg3"),
                                           p->getparam("arg4"),
                                           p->getparam("arg5"),
                                           p->getparam("arg6"),
                                           p->getparam("arg7"),
                                           p->getparam("arg8"),
                                           p->getparam("arg9")
                                           );
                  r->rsprintf(" %d", status);

                  //status = cm_disconnect_client(hconn, FALSE);
                  r->rsprintf(" %d", status);
               }

               r->rsprintf(" | ");
            }
         }
      }
   }

   r->rsprintf("rpc %d, called %d clients\n", rpc, count);
}

/*------------------------------------------------------------------*/

void do_jrpc_rev1(Param* p, Return* r)
{
   static RPC_LIST rpc_list[] = {
      { 9998, "mhttpd_jrpc_rev1", {
            {TID_STRING, RPC_OUT}, // return string
            {TID_INT,    RPC_IN},  // return string max length
            {TID_STRING, RPC_IN}, // arg0
            {TID_STRING, RPC_IN}, // arg1
            {TID_STRING, RPC_IN}, // arg2
            {TID_STRING, RPC_IN}, // arg3
            {TID_STRING, RPC_IN}, // arg4
            {TID_STRING, RPC_IN}, // arg5
            {TID_STRING, RPC_IN}, // arg6
            {TID_STRING, RPC_IN}, // arg7
            {TID_STRING, RPC_IN}, // arg8
            {TID_STRING, RPC_IN}, // arg9
            {0}} },
      { 0 }
   };

   int status, count = 0, substring = 0, rpc;

   const char *xname   = p->getparam("name");
   const char *srpc    = p->getparam("rpc");

   if (!srpc || !xname) {
      show_text_header(r);
      r->rsprintf("<INVALID_ARGUMENTS>");
      return;
   }

   char sname[256];
   strlcpy(sname, xname, sizeof(sname));

   if (sname[strlen(sname)-1]=='*') {
      sname[strlen(sname)-1] = 0;
      substring = 1;
   }

   rpc = atoi(srpc);

   if (rpc<RPC_MIN_ID || rpc>RPC_MAX_ID) {
      show_text_header(r);
      r->rsprintf("<INVALID_RPC_ID>");
      return;
   }

   rpc_list[0].id = rpc;
   status = rpc_register_functions(rpc_list, NULL);

   //printf("cm_register_functions() for format \'%s\' status %d\n", sformat, status);

   show_text_header(r);

   std::string reply_header;
   std::string reply_body;

   //r->rsprintf("<?xml version=\"1.0\" encoding=\"%s\"?>\n", HTTP_ENCODING);
   //r->rsprintf("<!-- created by MHTTPD on (timestamp) -->\n");
   //r->rsprintf("<jrpc_rev1>\n");
   //r->rsprintf("  <rpc>%d</rpc>\n", rpc);

   if (1) {
      HNDLE hDB, hrootkey, hsubkey, hkey;

      cm_get_experiment_database(&hDB, NULL);

      int buf_length = 1024;

      int max_reply_length = atoi(p->getparam("max_reply_length"));
      if (max_reply_length > buf_length)
         buf_length = max_reply_length;

      char* buf = (char*)malloc(buf_length);

      /* find client which exports our RPC function */
      status = db_find_key(hDB, 0, "System/Clients", &hrootkey);
      if (status == DB_SUCCESS) {
         for (int i=0; ; i++) {
            status = db_enum_key(hDB, hrootkey, i, &hsubkey);
            if (status == DB_NO_MORE_SUBKEYS)
               break;

            char str[256];
            sprintf(str, "RPC/%d", rpc);
            status = db_find_key(hDB, hsubkey, str, &hkey);
            if (status == DB_SUCCESS) {
               char client_name[NAME_LENGTH];
               HNDLE hconn;
               int size;

               size = sizeof(client_name);
               status = db_get_value(hDB, hsubkey, "Name", client_name, &size, TID_STRING, FALSE);
               if (status != DB_SUCCESS)
                  continue;

               if (strlen(sname) > 0) {
                  if (substring) {
                     if (strstr(client_name, sname) != client_name)
                        continue;
                  } else {
                     if (strcmp(sname, client_name) != 0)
                        continue;
                  }
               }

               count++;

               //r->rsprintf("  <client>\n");
               //r->rsprintf("    <name>%s</name>\n", client_name);

               int connect_status = -1;
               int call_status = -1;
               int call_length = 0;
               int disconnect_status = -1;

               connect_status = cm_connect_client(client_name, &hconn);

               //r->rsprintf("    <connect_status>%d</connect_status>\n", status);

               if (connect_status == RPC_SUCCESS) {
                  buf[0] = 0;

                  call_status = rpc_client_call(hconn, rpc,
                                                buf,
                                                buf_length,
                                                p->getparam("arg0"),
                                                p->getparam("arg1"),
                                                p->getparam("arg2"),
                                                p->getparam("arg3"),
                                                p->getparam("arg4"),
                                                p->getparam("arg5"),
                                                p->getparam("arg6"),
                                                p->getparam("arg7"),
                                                p->getparam("arg8"),
                                                p->getparam("arg9")
                                                );

                  //r->rsprintf("    <rpc_status>%d</rpc_status>\n", status);
                  ////r->rsprintf("    <data>%s</data>\n", buf);
                  //r->rsputs("<data>");
                  //r->rsputs(buf);
                  //r->rsputs("</data>\n");

                  if (call_status == RPC_SUCCESS) {
                     call_length = strlen(buf);
                     reply_body += buf;
                  }

                  //disconnect_status = cm_disconnect_client(hconn, FALSE);
                  //r->rsprintf("    <disconnect_status>%d</disconnect_status>\n", status);
               }

               //r->rsprintf("  </client>\n");

               if (reply_header.length() > 0)
                  reply_header += " | ";

               char tmp[256];
               sprintf(tmp, "%s %d %d %d %d", client_name, connect_status, call_status, disconnect_status, call_length);
               reply_header += tmp;
            }
         }
      }

      free(buf);
   }

   //r->rsprintf("  <called_clients>%d</called_clients>\n", count);
   //r->rsprintf("</jrpc_rev1>\n");

   if (reply_header.length() > 0) {
      r->rsputs(reply_header.c_str());
      r->rsputs(" || ");
      r->rsputs(reply_body.c_str());
      r->rsputs("\n");
   }
}

/*------------------------------------------------------------------*/

void do_jrpc(Param* p, Return* r)
{
   int status;

   const char *name   = p->getparam("name");
   const char *cmd    = p->getparam("rcmd");
   const char *args   = p->getparam("rarg");

   if (!name || !cmd || !args) {
      show_text_header(r);
      r->rsprintf("<INVALID_ARGUMENTS>");
      return;
   }

   show_text_header(r);

   int buf_length = 1024;

   int max_reply_length = atoi(p->getparam("max_reply_length"));
   if (max_reply_length > buf_length)
      buf_length = max_reply_length;

   char* buf = (char*)malloc(buf_length);
   buf[0] = 0;

   HNDLE hconn;

   status = cm_connect_client(name, &hconn);

   if (status != RPC_SUCCESS) {
      r->rsprintf("<RPC_CONNECT_ERROR>%d</RPC_CONNECT_ERROR>", status);
      return;
   }

   status = rpc_client_call(hconn, RPC_JRPC, cmd, args, buf, buf_length);

   if (status != RPC_SUCCESS) {
      r->rsprintf("<RPC_CALL_ERROR>%d</RPC_CALL_ERROR>", status);
      return;
   }

   r->rsprintf("%s", buf);

   //status = cm_disconnect_client(hconn, FALSE);

   free(buf);
}

/*------------------------------------------------------------------*/

void output_key(Param* p, Return* r, HNDLE hkey, int index, const char *format)
{
   int size, i;
   char str[TEXT_SIZE];
   HNDLE hDB, hsubkey;
   KEY key;
   char data[TEXT_SIZE];

   cm_get_experiment_database(&hDB, NULL);

   db_get_key(hDB, hkey, &key);
   if (key.type == TID_KEY) {
      for (i=0 ; ; i++) {
         db_enum_key(hDB, hkey, i, &hsubkey);
         if (!hsubkey)
            break;
         output_key(p, r, hsubkey, -1, format);
      }
   } else {
      if (key.item_size <= (int)sizeof(data)) {
         size = sizeof(data);
         db_get_data(hDB, hkey, data, &size, key.type);
         if (index == -1) {
            for (i=0 ; i<key.num_values ; i++) {
               if (p->isparam("name") && atoi(p->getparam("name")) == 1) {
                  if (key.num_values == 1)
                     r->rsprintf("%s:", key.name);
                  else
                     r->rsprintf("%s[%d]:", key.name, i);
               }
               if (format && format[0])
                  db_sprintff(str, format, data, key.item_size, i, key.type);
               else
                  db_sprintf(str, data, key.item_size, i, key.type);
               r->rsputs(str);
               if (i<key.num_values-1)
                  r->rsputs("\n");
            }
         } else {
            if (p->isparam("name") && atoi(p->getparam("name")) == 1)
               r->rsprintf("%s[%d]:", key.name, index);
            if (index >= key.num_values)
               r->rsputs("<DB_OUT_OF_RANGE>");
            else {
               if (p->isparam("format"))
                  db_sprintff(str, p->getparam("format"), data, key.item_size, index, key.type);
               else
                  db_sprintf(str, data, key.item_size, index, key.type);
               r->rsputs(str);
            }
         }
         r->rsputs("\n");
      }
   }
}

/*------------------------------------------------------------------*/

bool starts_with(const std::string& s1, const char* s2)
{
   if (s1.length() < strlen(s2))
      return false;
   return (strncasecmp(s1.c_str(), s2, strlen(s2)) == 0);
}

//static bool ends_with_char(const std::string& s, char c)
//{
//   if (s.length() < 1)
//      return false;
//   return s[s.length()-1] == c;
//}

/*------------------------------------------------------------------*/

void javascript_commands(Param* p, Return* r, const char *cookie_cpwd)
{
   int status;
   int size, i, n, index, type;
   unsigned int t;
   char str[TEXT_SIZE], format[256], facility[256], user[256];
   HNDLE hDB, hkey;
   KEY key;
   char data[TEXT_SIZE];

   cm_get_experiment_database(&hDB, NULL);

   // process common parameters

   const int ENCODING_NONE = 0;
   const int ENCODING_ODB = 1;
   const int ENCODING_XML = 2;
   const int ENCODING_JSON = 3;

   std::string cmd_parameter;
   std::string encoding_parameter;
   int encoding = ENCODING_NONE; // default encoding
   bool jsonp = false; // default is no JSONP wrapper
   std::string jsonp_callback; // default is no JSONP
   bool single = false; // single encoding
   bool multiple = false; // multiple encoding
   std::vector<std::string> odb; // multiple odb parameters
   //HNDLE hodb; // ODB handle for single odb parameter
   //std::vector<HNDLE> hodbm; // ODB handle for multiple odb parameter

   if (p->isparam("cmd")) {
      cmd_parameter = p->getparam("cmd");
   }

   if (p->isparam("encoding")) {
      encoding_parameter = p->getparam("encoding");
   }

   if (encoding_parameter.length() > 0) {
      if (starts_with(encoding_parameter, "odb"))
         encoding = ENCODING_ODB;
      else if (starts_with(encoding_parameter, "xml"))
         encoding = ENCODING_XML;
      else if (starts_with(encoding_parameter, "json"))
         encoding = ENCODING_JSON;
   }

   if (encoding == ENCODING_JSON) {
      if (p->isparam("callback")) {
         jsonp = true;
         jsonp_callback = p->getparam("callback");
      }
   }

   if (p->isparam("odb")) {
      single = true;
      odb.push_back(p->getparam("odb"));
   }

   if (p->isparam("odb0")) {
      multiple = true;
      for (int i=0 ; ; i++) {
         char ppath[256];
         sprintf(ppath, "odb%d", i);
         if (!p->isparam(ppath))
            break;
         odb.push_back(p->getparam(ppath));
      }
   }

   if (/* DISABLES CODE */ (0)) {
      printf("command [%s], encoding %d [%s], jsonp %d, single %d, multiple %d, odb array size %d\n", cmd_parameter.c_str(), encoding, encoding_parameter.c_str(), jsonp, single, multiple, (int)odb.size());
   }

   /* process "jset" command */
   if (equal_ustring(p->getparam("cmd"), "jset")) {

      if (*p->getparam("pnam")) {
         std::string ppath;
         ppath += "/Custom/Pwd/";
         ppath += p->getparam("pnam");
         str[0] = 0;
         db_get_value(hDB, 0, ppath.c_str(), str, &size, TID_STRING, TRUE);
         if (!equal_ustring(cookie_cpwd, str)) {
            show_text_header(r);
            r->rsprintf("Invalid password!");
            return;
         }
      }
      strlcpy(str, p->getparam("odb"), sizeof(str));
      if (strchr(str, '[')) {
         if (*(strchr(str, '[')+1) == '*')
            index = -1;
         else
            index = atoi(strchr(str, '[')+1);
         *strchr(str, '[') = 0;
      } else
         index = 0;

      if (db_find_key(hDB, 0, str, &hkey) == DB_SUCCESS && p->isparam("value")) {
         db_get_key(hDB, hkey, &key);
         memset(data, 0, sizeof(data));
         if (key.item_size <= (int)sizeof(data)) {
            if (index == -1) {
               const char* ptr = p->getparam("value");
               for (i=0 ; ptr != NULL ; i++) {
                  size = sizeof(data);
                  db_sscanf(ptr, data, &size, 0, key.type);
                  if (strchr(data, ','))
                     *strchr(data, ',') = 0;
                  db_set_data_index(hDB, hkey, data, key.item_size, i, key.type);
                  ptr = strchr(ptr, ',');
                  if (ptr != NULL)
                     ptr++;
               }
            } else {
               size = sizeof(data);
               db_sscanf(p->getparam("value"), data, &size, 0, key.type);

               /* extend data size for single string if necessary */
               if ((key.type == TID_STRING || key.type == TID_LINK)
                   && (int) strlen(data) + 1 > key.item_size && key.num_values == 1) {
                  key.item_size = strlen(data) + 1;
                  db_set_data(hDB, hkey, data, key.item_size, 1, key.type);
               } else
                  db_set_data_index(hDB, hkey, data, key.item_size, index, key.type);
            }
         }
      } else {
         if (p->isparam("value") && p->isparam("type") && p->isparam("len")) {
            int type = atoi(p->getparam("type"));
            if (type == 0) {
               show_text_header(r);
               r->rsprintf("Invalid type %d!", type);
               return;
            }
            db_create_key(hDB, 0, str, type);
            db_find_key(hDB, 0, str, &hkey);
            if (!hkey) {
               show_text_header(r);
               r->rsprintf("Cannot create \'%s\' type %d", str, type);
               return;
            }
            db_get_key(hDB, hkey, &key);
            memset(data, 0, sizeof(data));
            size = sizeof(data);
            db_sscanf(p->getparam("value"), data, &size, 0, key.type);
            if (key.type == TID_STRING)
               db_set_data(hDB, hkey, data, atoi(p->getparam("len")), 1, TID_STRING);
            else {
               for (i=0 ; i<atoi(p->getparam("len")) ; i++)
                  db_set_data_index(hDB, hkey, data, rpc_tid_size(key.type), i, key.type);
            }
         }
      }

      show_text_header(r);
      r->rsprintf("OK");
      return;
   }

   /* process "jget" command */
   if (equal_ustring(p->getparam("cmd"), "jget")) {

      if (p->isparam("odb")) {
         strlcpy(str, p->getparam("odb"), sizeof(str));
         if (strchr(str, '[')) {
            if (*(strchr(str, '[')+1) == '*')
               index = -1;
            else
               index = atoi(strchr(str, '[')+1);
            *strchr(str, '[') = 0;
         } else
            index = 0;

         show_text_header(r);

         status = db_find_key(hDB, 0, str, &hkey);

         if (status == DB_SUCCESS)
            output_key(p, r, hkey, index, p->getparam("format"));
         else
            r->rsputs("<DB_NO_KEY>");
      }

      if (p->isparam("odb0")) {
         show_text_header(r);
         for (i=0 ; ; i++) {
            char ppath[256];
            sprintf(ppath, "odb%d", i);
            sprintf(format, "format%d", i);
            if (p->isparam(ppath)) {
               strlcpy(str, p->getparam(ppath), sizeof(str));
               if (strchr(str, '[')) {
                  if (*(strchr(str, '[')+1) == '*')
                     index = -1;
                  else
                     index = atoi(strchr(str, '[')+1);
                  *strchr(str, '[') = 0;
               } else
                  index = 0;
               if (i > 0)
                  r->rsputs("$#----#$\n");
               if (db_find_key(hDB, 0, str, &hkey) == DB_SUCCESS)
                  output_key(p, r, hkey, index, p->getparam(format));
               else
                  r->rsputs("<DB_NO_KEY>");

            } else
               break;
         }
      }

      return;
   }

   /* process "jcopy" command */
   if (equal_ustring(p->getparam("cmd"), "jcopy")) {

      bool fmt_odb  = false;
      bool fmt_xml  = false;
      bool fmt_json = true;
      bool fmt_jsonp = false;
      int follow_links = 1;
      int save_keys = 1;
      int recurse = 1;
      const char* fmt = NULL;
      const char* jsonp_callback = "callback";

      if (p->isparam("encoding")) {
         fmt = p->getparam("encoding");
      } else if (p->isparam("format")) {
         fmt = p->getparam("format");
      }

      if (fmt) {
         fmt_odb  = (equal_ustring(fmt, "odb") > 0);
         fmt_xml  = (equal_ustring(fmt, "xml") > 0);
         fmt_json = (strstr(fmt, "json") != NULL);

         if (fmt_odb)
            fmt_xml = fmt_json = false;
         if (fmt_xml)
            fmt_odb = fmt_json = false;
         if (fmt_json)
            fmt_odb = fmt_xml = false;

         if (fmt_json)
            fmt_jsonp = (strstr(fmt, "-p") != NULL);
         if (fmt_jsonp && p->isparam("callback"))
            jsonp_callback = p->getparam("callback");
         if (fmt_json && strstr(fmt, "-nofollowlinks"))
            follow_links = 0;
         if (fmt_json && strstr(fmt, "-nokeys"))
            save_keys = 2;
         if (fmt_json && strstr(fmt, "-nolastwritten"))
            save_keys = 0;
         if (fmt_json && strstr(fmt, "-norecurse"))
            recurse = 0;
      }

      if (p->isparam("odb")) {
         strlcpy(str, p->getparam("odb"), sizeof(str));

         show_text_header(r);

         if (fmt_json)
            status = db_find_link(hDB, 0, str, &hkey);
         else
            status = db_find_key(hDB, 0, str, &hkey);
         if (status == DB_SUCCESS) {

            if (fmt_jsonp) {
               r->rsputs(jsonp_callback);
               r->rsputs("(");
            }

            int end = 0;
            int bufsize = WEB_BUFFER_SIZE;
            char* buf = (char *)malloc(bufsize);

            if (fmt_xml)
               db_copy_xml(hDB, hkey, buf, &bufsize);
            else if (fmt_json)
               db_copy_json_obsolete(hDB, hkey, &buf, &bufsize, &end, save_keys, follow_links, recurse);
            else
               db_copy(hDB, hkey, buf, &bufsize, (char *)"");

            r->rsputs(buf);
            free(buf);

            if (fmt_jsonp) {
               r->rsputs(");\n");
            }
         } else
            r->rsputs("<DB_NO_KEY>");
      }

      if (p->isparam("odb0")) {
         show_text_header(r);
         if (fmt_jsonp) {
            r->rsputs(jsonp_callback);
            r->rsputs("(");
         }
         if (fmt_xml) {
            r->rsprintf("<?xml version=\"1.0\" encoding=\"%s\"?>\n", HTTP_ENCODING);
            r->rsputs("<jcopy>\n");
            r->rsputs("<data>\n");
         } else if (fmt_json)
            r->rsputs("[\n");
         else
            r->rsputs("");
         for (int i=0 ; ; i++) {
            char ppath[256];
            sprintf(ppath, "odb%d", i);
            if (!p->isparam(ppath))
               break;
            strlcpy(str, p->getparam(ppath), sizeof(str));

            if (i > 0) {
               if (fmt_xml)
                  r->rsputs("</data>\n<data>\n");
               else if (fmt_json)
                  r->rsputs(",\n");
               else
                  r->rsputs("$#----#$\n");
            }

            if (fmt_json)
               status = db_find_link(hDB, 0, str, &hkey);
            else
               status = db_find_key(hDB, 0, str, &hkey);
            if (status != DB_SUCCESS) {
               if (fmt_xml)
                  r->rsputs("<DB_NO_KEY/>\n");
               else if (fmt_json) {
                  char tmp[256];
                  sprintf(tmp, "{ \"/error\" : %d }\n", status);
                  r->rsputs(tmp);
               } else
                  r->rsputs("<DB_NO_KEY>\n");
               continue;
            }

            int end = 0;
            int bufsize = WEB_BUFFER_SIZE;
            char* buf = (char *)malloc(bufsize);

            if (fmt_xml) {
               db_copy_xml(hDB, hkey, buf, &bufsize);
               const char* s = strstr(buf, "-->");
               if (s)
                  s+=4;
               else
                  s = buf;
               r->rsputs(s);
            } else if (fmt_json) {
               db_copy_json_obsolete(hDB, hkey, &buf, &bufsize, &end, save_keys, follow_links, recurse);
               r->rsputs(buf);
            } else {
               db_copy(hDB, hkey, buf, &bufsize, (char *)"");
               r->rsputs(buf);
            }

            free(buf);
         }

         if (fmt_xml)
            r->rsputs("</data>\n</jcopy>\n");
         else if (fmt_json)
            r->rsputs("]\n");
         else
            r->rsputs("");

         if (fmt_jsonp) {
            r->rsputs(");\n");
         }
      }
      return;
   }

   /* process "jkey" command */
   if (equal_ustring(p->getparam("cmd"), "jkey")) {

      // test:
      // curl "http://localhost:8080?cmd=jkey&odb0=/runinfo/run+number&odb1=/nonexistant&odb2=/&encoding=json&callback=aaa"

      show_text_header(r);

      if (jsonp) {
         r->rsputs(jsonp_callback.c_str());
         r->rsputs("(");
      }

      if (multiple) {
         switch (encoding) {
         default:
            break;
         case ENCODING_JSON:
            r->rsprintf("[ ");
            break;
         }
      }

      for (unsigned i=0; i<odb.size(); i++) {
         status = db_find_key(hDB, 0, odb[i].c_str(), &hkey);
         if (status == DB_SUCCESS)
            status = db_get_key(hDB, hkey, &key);
         switch (encoding) {
         default:
            if (multiple && i>0)
               r->rsputs("$#----#$\n");
            if (status == DB_SUCCESS) {
               r->rsprintf("%s\n", key.name);
               r->rsprintf("TID_%s\n", rpc_tid_name(key.type));
               r->rsprintf("%d\n", key.num_values);
               r->rsprintf("%d\n", key.item_size);
               r->rsprintf("%d\n", key.last_written);
            } else {
               r->rsputs("<DB_NO_KEY>\n");
            }
            break;
         case ENCODING_JSON:
            if (multiple && i>0)
               r->rsprintf(", ");
            if (status == DB_SUCCESS) {
               r->rsprintf("{ ");
               r->rsprintf("\"name\":\"%s\",", key.name);
               r->rsprintf("\"type\":%d,", key.type);
               r->rsprintf("\"type_name\":\"TID_%s\",", rpc_tid_name(key.type));
               r->rsprintf("\"num_values\":%d,", key.num_values);
               r->rsprintf("\"item_size\":%d,", key.item_size);
               r->rsprintf("\"last_written\":%d", key.last_written);
               r->rsprintf(" }");
            } else {
               r->rsprintf("{ \"/error\":%d }", status);
            }
            break;
         }
      }

      if (multiple) {
         switch (encoding) {
         default:
            break;
         case ENCODING_JSON:
            r->rsprintf(" ]");
            break;
         }
      }

      if (jsonp) {
         r->rsputs(");\n");
      }

      return;
   }

   /* process "jcreate" command */
   if (equal_ustring(p->getparam("cmd"), "jcreate")) {

      // test:
      // curl "http://localhost:8080?cmd=jcreate&odb0=/test/foo&type0=7&odb1=/nonexistant&type1=100&odb2=/test/bar&type2=12&encoding=json&callback=aaa"
      // curl "http://localhost:8080?cmd=jcreate&odb=/test/foo&type=7"
      // curl "http://localhost:8080?cmd=jcreate&odb=/test/foo70&type=7&arraylen=10"
      // curl "http://localhost:8080?cmd=jcreate&odb=/test/foo12s&type=12&strlen=32"
      // curl "http://localhost:8080?cmd=jcreate&odb=/test/foo12s5&type=12&strlen=32&arraylen=5"
      // curl "http://localhost:8080?cmd=jcreate&odb0=/test/foo12s5x&type0=12&strlen0=32&arraylen0=5"


      show_text_header(r);

      if (jsonp) {
         r->rsputs(jsonp_callback.c_str());
         r->rsputs("(");
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            r->rsprintf("[ ");
            break;
         }
      }

      for (unsigned i=0; i<odb.size(); i++) {
         HNDLE hkey = 0;
         int type = 0;
         int arraylength = 0;
         int strlength = 0;

         if (single) {
            type = atoi(p->getparam("type"));
            arraylength = atoi(p->getparam("arraylen"));
            strlength = atoi(p->getparam("strlen"));
         }
         else if (multiple) {
            char buf[256];
            sprintf(buf, "type%d", i);
            type = atoi(p->getparam(buf));
            sprintf(buf, "arraylen%d", i);
            arraylength = atoi(p->getparam(buf));
            sprintf(buf, "strlen%d", i);
            strlength = atoi(p->getparam(buf));
         }

         status = db_create_key(hDB, 0, odb[i].c_str(), type);

         if (status == DB_SUCCESS) {
            status = db_find_link(hDB, 0, odb[i].c_str(), &hkey);
         }

         if (status == DB_SUCCESS && hkey && type == TID_STRING && strlength > 0) {
            char* s = (char*)calloc(strlength, 1); // initialized to zero
            status = db_set_data(hDB, hkey, s, strlength, 1, TID_STRING);
            free(s);
         }

         if (status == DB_SUCCESS && hkey && arraylength > 1) {
            status = db_set_num_values(hDB, hkey, arraylength);
         }

         switch (encoding) {
         default:
         case ENCODING_JSON:
            if (multiple && i>0)
               r->rsprintf(", ");
            r->rsprintf("%d", status);
            break;
         }
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            r->rsprintf(" ]");
            break;
         }
      }

      if (jsonp) {
         r->rsputs(");\n");
      }

      return;
   }

   /* process "jresize" command */
   if (equal_ustring(p->getparam("cmd"), "jresize")) {

      // test:

      // curl "http://localhost:8080?cmd=jresize&odb=/test/foo70&arraylen=5"
      // curl "http://localhost:8080?cmd=jresize&odb=/test/foo12s5&arraylen=5"
      // curl "http://localhost:8080?cmd=jresize&odb=/test/foo12s5&strlen=16"
      // curl "http://localhost:8080?cmd=jresize&odb=/test/foo12s5&strlen=30&arraylen=10"

      show_text_header(r);

      if (jsonp) {
         r->rsputs(jsonp_callback.c_str());
         r->rsputs("(");
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            r->rsprintf("[ ");
            break;
         }
      }

      for (unsigned i=0; i<odb.size(); i++) {
         HNDLE hkey;
         KEY key;
         int arraylength = 0;
         int strlength = 0;

         if (single) {
            arraylength = atoi(p->getparam("arraylen"));
            strlength = atoi(p->getparam("strlen"));
         }
         else if (multiple) {
            char buf[256];
            sprintf(buf, "arraylen%d", i);
            arraylength = atoi(p->getparam(buf));
            sprintf(buf, "strlen%d", i);
            strlength = atoi(p->getparam(buf));
         }

         status = db_find_key(hDB, 0, odb[i].c_str(), &hkey);

         if (status == DB_SUCCESS && hkey) {
            status = db_get_key(hDB, hkey, &key);
         }

         if (status == DB_SUCCESS && hkey && key.type == TID_STRING && strlength > 0) {
            int oldsize = key.item_size * key.num_values;
            char* olddata = (char*)malloc(oldsize);
            int size = oldsize;
            status = db_get_data(hDB, hkey, olddata, &size, TID_STRING);

            if (status == DB_SUCCESS) {
               int newsize = strlength * key.num_values;
               char* s = (char*)calloc(newsize, 1); // initialized to zero
               for (int k=0; k<key.num_values; k++) {
                  strlcpy(s + strlength*k, olddata + key.item_size*k, strlength);
               }

               status = db_set_data(hDB, hkey, s, newsize, key.num_values, TID_STRING);
               free(s);
            }

            free(olddata);
         }

         if (status == DB_SUCCESS && hkey && arraylength > 0) {
            status = db_set_num_values(hDB, hkey, arraylength);
         }

         switch (encoding) {
         default:
         case ENCODING_JSON:
            if (multiple && i>0)
               r->rsprintf(", ");
            r->rsprintf("%d", status);
            break;
         }
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            r->rsprintf(" ]");
            break;
         }
      }

      if (jsonp) {
         r->rsputs(");\n");
      }

      return;
   }

   /* process "jrename" command */
   if (equal_ustring(p->getparam("cmd"), "jrename")) {

      // test:
      // curl "http://localhost:8080?cmd=jrename&odb0=/test/foo&type0=7&odb1=/nonexistant&type1=100&odb2=/test/bar&type2=12&encoding=json&callback=aaa"
      // curl "http://localhost:8080?cmd=jrename&odb=/test/foo&name=foofoo"

      show_text_header(r);

      if (jsonp) {
         r->rsputs(jsonp_callback.c_str());
         r->rsputs("(");
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            r->rsprintf("[ ");
            break;
         }
      }

      for (unsigned i=0; i<odb.size(); i++) {
         const char* name = NULL;
         if (single)
            name = p->getparam("name");
         else if (multiple) {
            char buf[256];
            sprintf(buf, "name%d", i);
            name = p->getparam(buf);
         }
         status = db_find_key(hDB, 0, odb[i].c_str(), &hkey);
         if (status == DB_SUCCESS) {
            status = db_rename_key(hDB, hkey, name);
         }
         switch (encoding) {
         default:
         case ENCODING_JSON:
            if (multiple && i>0)
               r->rsprintf(", ");
            r->rsprintf("%d", status);
            break;
         }
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            r->rsprintf(" ]");
            break;
         }
      }

      if (jsonp) {
         r->rsputs(");\n");
      }

      return;
   }

   /* process "jlink" command */
   if (equal_ustring(p->getparam("cmd"), "jlink")) {

      // test:
      // curl "http://localhost:8080?cmd=jlink&odb=/test/link&dest=/test/foo"
      // curl "http://localhost:8080?cmd=jlink&odb0=/test/link0&dest0=/test/foo&odb1=/test/link1&dest1=/test/foo"

      show_text_header(r);

      if (jsonp) {
         r->rsputs(jsonp_callback.c_str());
         r->rsputs("(");
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            r->rsprintf("[ ");
            break;
         }
      }

      for (unsigned i=0; i<odb.size(); i++) {
         const char* dest = NULL;
         if (single)
            dest = p->getparam("dest");
         else if (multiple) {
            char buf[256];
            sprintf(buf, "dest%d", i);
            dest = p->getparam(buf);
         }

         status = db_create_link(hDB, 0, odb[i].c_str(), dest);

         switch (encoding) {
         default:
         case ENCODING_JSON:
            if (multiple && i>0)
               r->rsprintf(", ");
            r->rsprintf("%d", status);
            break;
         }
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            r->rsprintf(" ]");
            break;
         }
      }

      if (jsonp) {
         r->rsputs(");\n");
      }

      return;
   }

   /* process "jreorder" command */
   if (equal_ustring(p->getparam("cmd"), "jreorder")) {

      // test:
      // curl "http://localhost:8080?cmd=jreorder&odb0=/test/foo&index0=0&odb1=/test/bar&index1=1"
      // curl "http://localhost:8080?cmd=jreorder&odb=/test/bar&index=0"

      show_text_header(r);

      if (jsonp) {
         r->rsputs(jsonp_callback.c_str());
         r->rsputs("(");
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            r->rsprintf("[ ");
            break;
         }
      }

      for (unsigned i=0; i<odb.size(); i++) {
         int index = 0;
         if (single)
            index = atoi(p->getparam("index"));
         else if (multiple) {
            char buf[256];
            sprintf(buf, "index%d", i);
            index = atoi(p->getparam(buf));
         }

         status = db_find_key(hDB, 0, odb[i].c_str(), &hkey);
         if (status == DB_SUCCESS) {
            status = db_reorder_key(hDB, hkey, index);
         }

         switch (encoding) {
         default:
         case ENCODING_JSON:
            if (multiple && i>0)
               r->rsprintf(", ");
            r->rsprintf("%d", status);
            break;
         }
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            r->rsprintf(" ]");
            break;
         }
      }

      if (jsonp) {
         r->rsputs(");\n");
      }

      return;
   }

   /* process "jdelete" command */
   if (equal_ustring(p->getparam("cmd"), "jdelete")) {

      // test:
      // curl "http://localhost:8080?cmd=jdelete&odb0=/test/foo&odb1=/nonexistant&odb2=/test/bar&encoding=json&callback=aaa"
      // curl "http://localhost:8080?cmd=jdelete&odb=/test/foo"

      show_text_header(r);

      if (jsonp) {
         r->rsputs(jsonp_callback.c_str());
         r->rsputs("(");
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            r->rsprintf("[ ");
            break;
         }
      }

      for (unsigned i=0; i<odb.size(); i++) {
         BOOL follow_links = 0;
         status = db_find_link(hDB, 0, odb[i].c_str(), &hkey);
         if (status == DB_SUCCESS) {
            status = db_delete_key(hDB, hkey, follow_links);
         }
         switch (encoding) {
         default:
         case ENCODING_JSON:
            if (multiple && i>0)
               r->rsprintf(", ");
            r->rsprintf("%d", status);
            break;
         }
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            r->rsprintf(" ]");
            break;
         }
      }

      if (jsonp) {
         r->rsputs(");\n");
      }

      return;
   }

   /* process "jmsg" command */
   if (equal_ustring(p->getparam("cmd"), "jmsg")) {

      if (p->getparam("f") && *p->getparam("f"))
         strlcpy(facility, p->getparam("f"), sizeof(facility));
      else
         strlcpy(facility, "midas", sizeof(facility));

      n = 1;
      if (p->getparam("n") && *p->getparam("n"))
         n = atoi(p->getparam("n"));

      t = 0;
      if (p->getparam("t") && p->getparam("t"))
         t = atoi(p->getparam("t"));

      show_text_header(r);
      char* messages = NULL;
      int num_messages = 0;
      cm_msg_retrieve2(facility, t, n, &messages, &num_messages);
      if (messages) {
         r->rsputs(messages);
         free(messages);
      }
      return;
   }

   /* process "jgenmsg" command */
   if (equal_ustring(p->getparam("cmd"), "jgenmsg")) {

      if (p->getparam("facility") && *p->getparam("facility"))
         strlcpy(facility, p->getparam("facility"), sizeof(facility));
      else
         strlcpy(facility, "midas", sizeof(facility));

      if (p->getparam("user") && *p->getparam("user"))
         strlcpy(user, p->getparam("user"), sizeof(user));
      else
         strlcpy(user, "javascript_commands", sizeof(user));

      if (p->getparam("type") && *p->getparam("type"))
         type = atoi(p->getparam("type"));
      else
         type = MT_INFO;

      if (p->getparam("msg") && *p->getparam("msg")) {
         cm_msg1(type, __FILE__, __LINE__, facility, user, "%s", p->getparam("msg"));
      }

      show_text_header(r);
      r->rsputs("Message successfully created\n");
      return;
   }

   /* process "jalm" command */
   if (equal_ustring(p->getparam("cmd"), "jalm")) {

      show_text_header(r);
      al_get_alarms(str, sizeof(str));
      r->rsputs(str);
      return;
   }

   /* process "jrpc" command */
   if (equal_ustring(p->getparam("cmd"), "jrpc_rev0")) {
      do_jrpc_rev0(p, r);
      return;
   }

   /* process "jrpc" command */
   if (equal_ustring(p->getparam("cmd"), "jrpc_rev1")) {
      do_jrpc_rev1(p, r);
      return;
   }

   /* process "jrpc" command */
   if (equal_ustring(p->getparam("cmd"), "jrpc")) {
      do_jrpc(p, r);
      return;
   }
}

/*------------------------------------------------------------------*/

void show_custom_page(Param* pp, Return* r, const char *cookie_cpwd)
{
   int size, n_var, fh, index, edit;
   char keypath[256], type[32], *p, *ps;
   char pwd[256], tail[256];
   HNDLE hDB, hkey;
   KEY key;
   char data[TEXT_SIZE];

   std::string path = pp->getparam("page");

   if (path[0] == 0) {
      show_error_404(r, "show_custom_page: Invalid custom page: \"page\" parameter is empty");
      return;
   }

   if (strstr(path.c_str(), "..")) {
      std::string str;
      str += "Invalid custom page name \'";
      str += path;
      str += "\' contains \'..\'";
      show_error_404(r, str.c_str());
      return;
   }

   if (strstr(path.c_str(), ".gif")) {
      show_custom_gif(r, path.c_str());
      return;
   }

   if (strchr(path.c_str(), '.')) {
      show_custom_file(r, path.c_str());
      return;
   }

   cm_get_experiment_database(&hDB, NULL);

   std::string xpath = std::string("/Custom/") + path;
   db_find_key(hDB, 0, xpath.c_str(), &hkey);
   if (!hkey) {
      xpath = std::string("/Custom/") + path + "&";
      db_find_key(hDB, 0, xpath.c_str(), &hkey);
      if (!hkey) {
         xpath = std::string("/Custom/") + path + "!";
         db_find_key(hDB, 0, xpath.c_str(), &hkey);
      }
   }

   if (hkey) {
      char* ctext;
      int status;

      status = db_get_key(hDB, hkey, &key);
      assert(status == DB_SUCCESS);
      size = key.total_size;
      ctext = (char*)malloc(size);
      status = db_get_data(hDB, hkey, ctext, &size, TID_STRING);
      if (status != DB_SUCCESS) {
         char errtext[256];
         sprintf(errtext, "show_custom_page: Error: db_get_data() for \"%s\" status %d", xpath.c_str(), status); // FIXME: overflows "errtext"
         show_error_404(r, errtext);
         free(ctext);
         return;
      }

      std::string content_type = "text/html";

      /* check if filename */
      if (strchr(ctext, '\n') == 0) {
         std::string full_filename = add_custom_path(ctext);
         fh = open(full_filename.c_str(), O_RDONLY | O_BINARY);
         if (fh < 0) {
            char str[256];
            sprintf(str, "show_custom_page: Cannot open file \"%s\", errno %d (%s)", full_filename.c_str(), errno, strerror(errno)); // FIXME: overflows "str"
            show_error_404(r, str);
            free(ctext);
            return;
         }
         free(ctext);
         ctext = NULL;
         size = lseek(fh, 0, SEEK_END) + 1;
         lseek(fh, 0, SEEK_SET);
         ctext = (char*)malloc(size+1);
         int rd = read(fh, ctext, size);
         if (rd > 0) {
            ctext[rd] = 0; // make sure string is zero-terminated
            size = rd;
         } else {
            ctext[0] = 0;
            size = 0;
         }
         close(fh);

         content_type = get_content_type(full_filename.c_str());
      }

      /* check for valid password */
      if (equal_ustring(pp->getparam("cmd"), "Edit")) {
         p = ps = ctext;
         n_var = 0;
         do {
            char format[256];

            p = find_odb_tag(ps, keypath, format, &edit, type, pwd, tail);
            if (p == NULL)
               break;
            ps = strchr(p, '>') + 1;

            if (pwd[0] && n_var == atoi(pp->getparam("index"))) {
               char str[256];
               size = NAME_LENGTH;
               strlcpy(str, path.c_str(), sizeof(str)); // FIXME: overflows "str"
               if (strlen(str)>0 && str[strlen(str)-1] == '&')
                  str[strlen(str)-1] = 0;
               std::string ppath;
               ppath += "/Custom/Pwd/";
               if (pp->getparam("pnam") && *pp->getparam("pnam")) {
                  ppath += pp->getparam("pnam");
               } else {
                  ppath += str;
               }
               str[0] = 0;
               db_get_value(hDB, 0, ppath.c_str(), str, &size, TID_STRING, TRUE);
               if (!equal_ustring(cookie_cpwd, str)) {
                  show_error_404(r, "show_custom_page: Invalid password!");
                  free(ctext);
                  return;
               } else
                  break;
            }

            n_var++;
         } while (p != NULL);
      }

      /* process toggle command */
      if (equal_ustring(pp->getparam("cmd"), "Toggle")) {

         if (pp->getparam("pnam") && *pp->getparam("pnam")) {
            char ppath[MAX_ODB_PATH];
            sprintf(ppath, "/Custom/Pwd/%s", pp->getparam("pnam"));
            char str[256];
            str[0] = 0;
            db_get_value(hDB, 0, ppath, str, &size, TID_STRING, TRUE);
            if (!equal_ustring(cookie_cpwd, str)) {
               show_error_404(r, "show_custom_page: Invalid password!");
               free(ctext);
               return;
            }
         }
         std::string podb = pp->getparam("odb");
         std::string::size_type pos = podb.find('[');
         if (pos != std::string::npos) {
            index = atoi(podb.substr(pos+1).c_str());
            podb.resize(pos);
            //printf("found index %d in [%s] [%s]\n", index, pp->getparam("odb"), podb.c_str());
         } else
            index = 0;

         if (db_find_key(hDB, 0, podb.c_str(), &hkey)) {
            db_get_key(hDB, hkey, &key);
            memset(data, 0, sizeof(data));
            if (key.item_size <= (int)sizeof(data)) {
               size = sizeof(data);
               db_get_data_index(hDB, hkey, data, &size, index, key.type);
               char str[256];
               db_sprintf(str, data, size, 0, key.type); // FIXME: overflows "str"
               if (atoi(str) == 0)
                  db_sscanf("1", data, &size, 0, key.type);
               else
                  db_sscanf("0", data, &size, 0, key.type);
               db_set_data_index(hDB, hkey, data, key.item_size, index, key.type);
            }
         }

         /* redirect (so that 'reload' does not toggle again) */
         redirect(r, path.c_str());
         free(ctext);
         return;
      }

      /* HTTP header */
      r->rsprintf("HTTP/1.1 200 Document follows\r\n");
      r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
      r->rsprintf("Content-Type: %s; charset=%s\r\n\r\n", content_type.c_str(), HTTP_ENCODING);

      /* interprete text, replace <odb> tags with ODB values */
      p = ps = ctext;
      n_var = 0;
      do {
         char format[256];
         p = find_odb_tag(ps, keypath, format, &edit, type, pwd, tail);
         if (p != NULL)
            *p = 0;
         r->rsputs(ps);

         if (p == NULL)
            break;
         ps = strchr(p + 1, '>') + 1;

         show_odb_tag(pp, r, path.c_str(), keypath, format, n_var, edit, type, pwd, tail);
         n_var++;

      } while (p != NULL);

      if (equal_ustring(pp->getparam("cmd"), "Set") || pp->isparam("cbi")) {
         /* redirect (so that 'reload' does not change value) */
         r->reset();
         redirect(r, path.c_str());
      }

      free(ctext);
      ctext = NULL;
   } else {
      char str[256];
      sprintf(str, "Invalid custom page: Page \"%s\" not found in ODB", path.c_str()); // FIXME: overflows "str"
      show_error_404(r, str);
      return;
   }
}

/*------------------------------------------------------------------*/

static void show_cnaf_page(Param* p, Return* rr)
{
   char str[256];
   int c, n, a, f, d, q, x, r, ia, id, w;
   int i, size, status;
   HNDLE hDB, hrootkey, hsubkey, hkey;

   static char client_name[NAME_LENGTH];
   static HNDLE hconn = 0;

   cm_get_experiment_database(&hDB, NULL);

   /* find FCNA server if not specified */
   if (hconn == 0) {
      /* find client which exports FCNA function */
      status = db_find_key(hDB, 0, "System/Clients", &hrootkey);
      if (status == DB_SUCCESS) {
         for (i = 0;; i++) {
            status = db_enum_key(hDB, hrootkey, i, &hsubkey);
            if (status == DB_NO_MORE_SUBKEYS)
               break;

            sprintf(str, "RPC/%d", RPC_CNAF16);
            status = db_find_key(hDB, hsubkey, str, &hkey);
            if (status == DB_SUCCESS) {
               size = sizeof(client_name);
               db_get_value(hDB, hsubkey, "Name", client_name, &size, TID_STRING, TRUE);
               break;
            }
         }
      }

      if (client_name[0]) {
         status = cm_connect_client(client_name, &hconn);
         if (status != RPC_SUCCESS)
            hconn = 0;
      }
   }

   /* header */
   rr->rsprintf("HTTP/1.1 200 Document follows\r\n");
   rr->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
   rr->rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

   rr->rsprintf("<html><head>\n");
   rr->rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
   rr->rsprintf("<link rel=\"stylesheet\" href=\"midas.css\" type=\"text/css\" />\n");
   rr->rsprintf("<link rel=\"stylesheet\" href=\"mhttpd.css\" type=\"text/css\" />\n");
   rr->rsprintf("<title>MIDAS CAMAC interface</title></head>\n");
   rr->rsprintf("<body><form method=\"GET\" action=\"CNAF\">\n\n");

   /* title row */

   size = sizeof(str);
   str[0] = 0;
   db_get_value(hDB, 0, "/Experiment/Name", str, &size, TID_STRING, TRUE);

   rr->rsprintf("<table border=3 cellpadding=1>\n");
   rr->rsprintf("<tr><th colspan=3>MIDAS experiment \"%s\"", str);

   if (client_name[0] == 0)
      rr->rsprintf("<th colspan=3 class=\"redLight\">No CAMAC server running</tr>\n");
   else if (hconn == 0)
      rr->rsprintf("<th colspan=3 class=\"redLight\">Cannot connect to %s</tr>\n", client_name);
   else
      rr->rsprintf("<th colspan=3>CAMAC server: %s</tr>\n", client_name);

   /* default values */
   c = n = 1;
   a = f = d = q = x = 0;
   r = 1;
   ia = id = w = 0;

   /*---- menu buttons ----*/

   rr->rsprintf("<tr><td colspan=3>\n");
   rr->rsprintf("<input type=submit name=cmd value=Execute>\n");

   rr->rsprintf("<td colspan=3>\n");
   rr->rsprintf("<input type=submit name=cmd value=ODB>\n");
   rr->rsprintf("<input type=submit name=cmd value=Status>\n");
   rr->rsprintf("<input type=submit name=cmd value=Help>\n");
   rr->rsprintf("</tr>\n\n");

   /* header */
   rr->rsprintf("<tr><th>N");
   rr->rsprintf("<th>A");
   rr->rsprintf("<th>F");
   rr->rsprintf("<th colspan=3>Data");

   /* execute commands */
   size = sizeof(d);

   const char* cmd = p->getparam("cmd");
   if (equal_ustring(cmd, "C cycle")) {
      rpc_client_call(hconn, RPC_CNAF16, CNAF_CRATE_CLEAR, 0, 0, 0, 0, 0, &d, &size, &x,
                      &q);

      rr->rsprintf("<tr><td colspan=6 class=\"greenLight\">C cycle executed sucessfully</tr>\n");
   } else if (equal_ustring(cmd, "Z cycle")) {
      rpc_client_call(hconn, RPC_CNAF16, CNAF_CRATE_ZINIT, 0, 0, 0, 0, 0, &d, &size, &x,
                      &q);

      rr->rsprintf("<tr><td colspan=6 class=\"greenLight\">Z cycle executed sucessfully</tr>\n");
   } else if (equal_ustring(cmd, "Clear inhibit")) {
      rpc_client_call(hconn, RPC_CNAF16, CNAF_INHIBIT_CLEAR, 0, 0, 0, 0, 0, &d, &size, &x,
                      &q);

      rr->rsprintf
          ("<tr><td colspan=6 class=\"greenLight\">Clear inhibit executed sucessfully</tr>\n");
   } else if (equal_ustring(cmd, "Set inhibit")) {
      rpc_client_call(hconn, RPC_CNAF16, CNAF_INHIBIT_SET, 0, 0, 0, 0, 0, &d, &size, &x,
                      &q);

      rr->rsprintf
          ("<tr><td colspan=6 class=\"greenLight\">Set inhibit executed sucessfully</tr>\n");
   } else if (equal_ustring(cmd, "Execute")) {
      c = atoi(p->getparam("C"));
      n = atoi(p->getparam("N"));
      a = atoi(p->getparam("A"));
      f = atoi(p->getparam("F"));
      r = atoi(p->getparam("R"));
      w = atoi(p->getparam("W"));
      id = atoi(p->getparam("ID"));
      ia = atoi(p->getparam("IA"));

      const char* pd = p->getparam("D");
      if (strncmp(pd, "0x", 2) == 0)
         sscanf(pd + 2, "%x", &d);
      else
         d = atoi(p->getparam("D"));

      /* limit repeat range */
      if (r == 0)
         r = 1;
      if (r > 100)
         r = 100;
      if (w > 1000)
         w = 1000;

      for (i = 0; i < r; i++) {
         status = SUCCESS;

         if (hconn) {
            size = sizeof(d);
            status =
                rpc_client_call(hconn, RPC_CNAF24, CNAF, 0, c, n, a, f, &d, &size, &x,
                                &q);

            if (status == RPC_NET_ERROR) {
               /* try to reconnect */
               //cm_disconnect_client(hconn, FALSE);
               status = cm_connect_client(client_name, &hconn);
               if (status != RPC_SUCCESS) {
                  hconn = 0;
                  client_name[0] = 0;
               }

               if (hconn) {
                  status = rpc_client_call(hconn, RPC_CNAF24, CNAF, 0, c, n, a, f, &d, &size, &x, &q);
               }
            }
         }

         if (status != SUCCESS) {
            rr->rsprintf
                ("<tr><td colspan=6 class=\"redLight\">Error executing function, code = %d</tr>",
                 status);
         } else {
            rr->rsprintf("<tr align=center><td>%d", n);
            rr->rsprintf("<td>%d", a);
            rr->rsprintf("<td>%d", f);
            rr->rsprintf("<td colspan=3>%d / 0x%04X  Q%d X%d", d, d, q, x);
         }

         d += id;
         a += ia;

         if (w > 0)
            ss_sleep(w);
      }
   }

   /* input fields */
   rr->rsprintf
       ("<tr align=center><td><input type=text size=3 name=N value=%d>\n",
        n);
   rr->rsprintf("<td><input type=text size=3 name=A value=%d>\n", a);
   rr->rsprintf("<td><input type=text size=3 name=F value=%d>\n", f);
   rr->rsprintf
       ("<td colspan=3><input type=text size=8 name=D value=%d></tr>\n",
        d);

   /* control fields */
   rr->rsprintf("<tr><td colspan=2>Repeat");
   rr->rsprintf("<td><input type=text size=3 name=R value=%d>\n", r);

   rr->rsprintf
       ("<td align=center colspan=3><input type=submit name=cmd value=\"C cycle\">\n");
   rr->rsprintf("<input type=submit name=cmd value=\"Z cycle\">\n");

   rr->rsprintf("<tr><td colspan=2>Repeat delay [ms]");
   rr->rsprintf("<td><input type=text size=3 name=W value=%d>\n", w);

   rr->rsprintf
       ("<td align=center colspan=3><input type=submit name=cmd value=\"Set inhibit\">\n");
   rr->rsprintf("<input type=submit name=cmd value=\"Clear inhibit\">\n");

   rr->rsprintf("<tr><td colspan=2>Data increment");
   rr->rsprintf("<td><input type=text size=3 name=ID value=%d>\n", id);

   rr->rsprintf
       ("<td colspan=3 align=center>Branch <input type=text size=3 name=B value=0>\n");

   rr->rsprintf("<tr><td colspan=2>A increment");
   rr->rsprintf("<td><input type=text size=3 name=IA value=%d>\n", ia);

   rr->rsprintf
       ("<td colspan=3 align=center>Crate <input type=text size=3 name=C value=%d>\n",
        c);

   rr->rsprintf("</table></body>\r\n");
}

/*------------------------------------------------------------------*/

#ifdef HAVE_MSCB

typedef struct {
   signed char id;
   char name[32];
} NAME_TABLE;

static const NAME_TABLE prefix_table[] = {
   {PRFX_PICO, "pico",},
   {PRFX_NANO, "nano",},
   {PRFX_MICRO, "micro",},
   {PRFX_MILLI, "milli",},
   {PRFX_NONE, "",},
   {PRFX_KILO, "kilo",},
   {PRFX_MEGA, "mega",},
   {PRFX_GIGA, "giga",},
   {PRFX_TERA, "tera",},
   {99}
};

static const NAME_TABLE unit_table[] = {

   {UNIT_METER, "meter",},
   {UNIT_GRAM, "gram",},
   {UNIT_SECOND, "second",},
   {UNIT_MINUTE, "minute",},
   {UNIT_HOUR, "hour",},
   {UNIT_AMPERE, "ampere",},
   {UNIT_KELVIN, "kelvin",},
   {UNIT_CELSIUS, "deg. celsius",},
   {UNIT_FARENHEIT, "deg. farenheit",},

   {UNIT_HERTZ, "hertz",},
   {UNIT_PASCAL, "pascal",},
   {UNIT_BAR, "bar",},
   {UNIT_WATT, "watt",},
   {UNIT_VOLT, "volt",},
   {UNIT_OHM, "ohm",},
   {UNIT_TESLA, "tesls",},
   {UNIT_LITERPERSEC, "liter/sec",},
   {UNIT_RPM, "RPM",},
   {UNIT_FARAD, "farad",},

   {UNIT_BOOLEAN, "boolean",},
   {UNIT_BYTE, "byte",},
   {UNIT_WORD, "word",},
   {UNIT_DWORD, "dword",},
   {UNIT_ASCII, "ascii",},
   {UNIT_STRING, "string",},
   {UNIT_BAUD, "baud",},

   {UNIT_PERCENT, "percent",},
   {UNIT_PPM, "RPM",},
   {UNIT_COUNT, "counts",},
   {UNIT_FACTOR, "factor",},
   {0}
};

/*------------------------------------------------------------------*/

void print_mscb_var(char *value, char *evalue, char *unit, MSCB_INFO_VAR *info_chn, void *pdata)
{
   char str[80];
   signed short sdata;
   unsigned short usdata;
   signed int idata;
   unsigned int uidata;
   float fdata;
   int i;

   value[0] = 0;
   evalue[0] = 0;

   if (info_chn->unit == UNIT_STRING) {
      memset(str, 0, sizeof(str));
      strncpy(str, (char *)pdata, info_chn->width);
      for (i = 0; i < (int) strlen(str); i++)
         switch (str[i]) {
         case 1:
            strcat(value, "\\001");
            break;
         case 2:
            strcat(value, "\\002");
            break;
         case 9:
            strcat(value, "\\t");
            break;
         case 10:
            strcat(value, "\\n");
            break;
         case 13:
            strcat(value, "\\r");
            break;
         default:
            value[strlen(value) + 1] = 0;
            value[strlen(value)] = str[i];
            break;
         }
      strlcpy(evalue, value, 256);
   } else {
      switch (info_chn->width) {
      case 0:
         strcpy(value, "0");
         strcpy(evalue, "0");
         break;

      case 1:
         if (info_chn->flags & MSCBF_SIGNED) {
            sprintf(value, "%d (0x%02X/", *((signed char *)pdata), *((signed char *)pdata));
            sprintf(evalue, "%d", *((signed char *)pdata));
         } else {
            sprintf(value, "%u (0x%02X/", *((unsigned char *)pdata), *((unsigned char *)pdata));
            sprintf(evalue, "%u", *((unsigned char *)pdata));
         }

         for (i = 0; i < 8; i++)
            if (*((unsigned char *)pdata) & (0x80 >> i))
               sprintf(value + strlen(value), "1");
            else
               sprintf(value + strlen(value), "0");
         sprintf(value + strlen(value), ")");
         break;

      case 2:
         if (info_chn->flags & MSCBF_SIGNED) {
            sdata = *((signed short *)pdata);
            WORD_SWAP(&sdata);
            sprintf(value, "%d (0x%04X)", sdata, sdata);
            sprintf(evalue, "%d", sdata);
         } else {
            usdata = *((unsigned short *)pdata);
            WORD_SWAP(&usdata);
            sprintf(value, "%u (0x%04X)", usdata, usdata);
            sprintf(evalue, "%u", usdata);
         }
         break;

      case 4:
         if (info_chn->flags & MSCBF_FLOAT) {
            fdata = *((float *)pdata);
            DWORD_SWAP(&fdata);
            sprintf(value, "%1.6lg", fdata);
            sprintf(evalue, "%1.6lg", fdata);
         } else {
            if (info_chn->flags & MSCBF_SIGNED) {
               idata = *((signed int *)pdata);
               DWORD_SWAP(&idata);
               sprintf(value, "%d (0x%08X)", idata, idata);
               sprintf(evalue, "%d", idata);
            } else {
               uidata = *((unsigned int *)pdata);
               DWORD_SWAP(&uidata);
               sprintf(value, "%u (0x%08X)", uidata, uidata);
               sprintf(evalue, "%u", uidata);
            }
         }
         break;
      }
   }

   /* evaluate prefix */
   unit[0] = 0;
   if (info_chn->prefix) {
      for (i = 0; prefix_table[i].id != 99; i++)
	if ((unsigned char)prefix_table[i].id == info_chn->prefix)
            break;
      if (prefix_table[i].id)
         strcpy(unit, prefix_table[i].name);
   }

   /* evaluate unit */
   if (info_chn->unit && info_chn->unit != UNIT_STRING) {
      for (i = 0; unit_table[i].id; i++)
	if ((unsigned char)unit_table[i].id == info_chn->unit)
            break;
      if (unit_table[i].id)
         strcat(unit, unit_table[i].name);
   }
}

static int cmp_int(const void *a, const void *b)
{
  return *((int *)a) > *((int *)b);
}

/*------------------------------------------------------------------*/

void create_mscb_tree()
{
   HNDLE hDB, hKeySubm, hKeyEq, hKeyAdr, hKey, hKeyDev;
   KEY key;
   int i, j, k, l, size, address[1000], dev_badr[1000], dev_adr[1000], dev_chn[1000],
      n_address, n_dev_adr;
   char mscb_dev[256], mscb_pwd[32], eq_name[32];

   cm_get_experiment_database(&hDB, NULL);

   db_create_key(hDB, 0, "MSCB/Submaster", TID_KEY);
   db_find_key(hDB, 0, "MSCB/Submaster", &hKeySubm);
   assert(hKeySubm);

   /*---- go through equipment list ----*/
   db_find_key(hDB, 0, "Equipment", &hKeyEq);
   if (hKeyEq) {
      for (i=0 ; ; i++) {
         db_enum_key(hDB, hKeyEq, i, &hKey);
         if (!hKey)
            break;
         db_get_key(hDB, hKey, &key);
         strcpy(eq_name, key.name);
         db_find_key(hDB, hKey, "Settings/Devices", &hKeyDev);
         if (hKeyDev) {
            for (j=0 ;; j++) {
               db_enum_key(hDB, hKeyDev, j, &hKey);
               if (!hKey)
                  break;

               if (db_find_key(hDB, hKey, "MSCB Address", &hKeyAdr) == DB_SUCCESS) {
                  /* mscbdev type of device */
                  size = sizeof(mscb_dev);
                  if (db_get_value(hDB, hKey, "Device", mscb_dev, &size, TID_STRING, FALSE) != DB_SUCCESS)
                     continue;
                  size = sizeof(mscb_pwd);
                  if (db_get_value(hDB, hKey, "Pwd", mscb_pwd, &size, TID_STRING, FALSE) != DB_SUCCESS)
                     continue;

                  size = sizeof(dev_adr);
                  db_get_data(hDB, hKeyAdr, dev_adr, &size, TID_INT);
                  n_dev_adr = size / sizeof(int);
               } else if (db_find_key(hDB, hKey, "Block Address", &hKeyAdr) == DB_SUCCESS) {
                  /* mscbhvr type of device */
                  size = sizeof(mscb_dev);
                  if (db_get_value(hDB, hKey, "MSCB Device", mscb_dev, &size, TID_STRING, FALSE) != DB_SUCCESS)
                     continue;
                  size = sizeof(mscb_pwd);
                  if (db_get_value(hDB, hKey, "MSCB Pwd", mscb_pwd, &size, TID_STRING, FALSE) != DB_SUCCESS)
                     continue;

                  n_dev_adr = 0;
                  size = sizeof(dev_badr);
                  db_get_data(hDB, hKeyAdr, dev_badr, &size, TID_INT);
                  size = sizeof(dev_chn);
                  if (db_get_value(hDB, hKey, "Block Channels", dev_chn, &size, TID_INT, FALSE) == DB_SUCCESS) {
                     for (k=0 ; k<size/(int)sizeof(int) && n_dev_adr < (int)(sizeof(dev_adr)/sizeof(int)) ; k++) {
                        for (l=0 ; l<dev_chn[k] ; l++)
                           dev_adr[n_dev_adr++] = dev_badr[k]+l;
                     }
                  }
               } else
                  continue;

               /* create or open submaster entry */
               db_find_key(hDB, hKeySubm, mscb_dev, &hKey);
               if (!hKey) {
                  db_create_key(hDB, hKeySubm, mscb_dev, TID_KEY);
                  db_find_key(hDB, hKeySubm, mscb_dev, &hKey);
                  assert(hKey);
               }

               /* get old address list */
               size = sizeof(address);
               if (db_get_value(hDB, hKey, "Address", address, &size, TID_INT, FALSE) == DB_SUCCESS)
                  n_address = size / sizeof(int);
               else
                  n_address = 0;

               /* merge with new address list */
               for (k=0 ; k<n_dev_adr ; k++) {
                  for (l=0 ; l<n_address ; l++)
                     if (address[l] == dev_adr[k])
                        break;

                  if (l == n_address)
                     address[n_address++] = dev_adr[k];
               }

               /* sort address list */
               qsort(address, n_address, sizeof(int), cmp_int);

               /* store new address list */
               db_set_value(hDB, hKey, "Pwd", mscb_pwd, 32, 1, TID_STRING);
               db_set_value(hDB, hKey, "Comment", eq_name, 32, 1, TID_STRING);
               db_set_value(hDB, hKey, "Address", address, n_address*sizeof(int), n_address, TID_INT);
            }
         }
      }
   }
}

/*------------------------------------------------------------------*/

void show_mscb_page(Param* p, Return* r, int refresh)
{
   int i, j, n, ind, fi, fd, status, size, n_addr, *addr, cur_node, adr, show_hidden;
   unsigned int uptime;
   BOOL comment_created;
   float fvalue;
   char *pd;
   char dbuf[256], evalue[256], unit[256], cur_subm_name[256];
   HNDLE hDB, hKeySubm, hKeyCurSubm, hKey, hKeyAddr, hKeyComm;
   KEY key;
   MSCB_INFO info;
   MSCB_INFO_VAR info_var;
   int ping_addr[0x10000];
   char *node_comment;

   cm_get_experiment_database(&hDB, NULL);

   status = db_find_key(hDB, 0, "MSCB/Submaster", &hKeySubm);
   if (!hKeySubm)
      create_mscb_tree();

   strlcpy(cur_subm_name, p->getparam("subm"), sizeof(cur_subm_name));
   if (cur_subm_name[0] == 0) {
      db_enum_key(hDB, hKeySubm, 0, &hKeyCurSubm);
      if (!hKeyCurSubm) {
         char errorstr[256];
         sprintf(errorstr, "No submaster defined under /MSCB/Submaster");
         show_error(r, errorstr);
         return;
      }
      db_get_key(hDB, hKeyCurSubm, &key);
      strcpy(cur_subm_name, key.name);
   } else
      db_find_key(hDB, hKeySubm, cur_subm_name, &hKeyCurSubm);

   if (p->isparam("node"))
      cur_node = atoi(p->getparam("node"));
   else
      cur_node = -1;

   /* perform MSCB rescan */
   if (p->isparam("mcmd") && equal_ustring(p->getparam("mcmd"), "Rescan") && p->isparam("subm")) {
      /* create Pwd and Comment if not there */
      char tmp[32];
      size = 32;
      tmp[0] = 0;
      db_get_value(hDB, hKeyCurSubm, "Pwd", (void *)tmp, &size, TID_STRING, true);
      tmp[0] = 0;
      db_get_value(hDB, hKeyCurSubm, "Comment", (void *)tmp, &size, TID_STRING, true);

      db_find_key(hDB, hKeyCurSubm, "Address", &hKeyAddr);
      if (hKeyAddr) {
         /* get current address array */
         db_get_key(hDB, hKeyAddr, &key);
         n_addr = key.num_values;
         addr = (int *)malloc(sizeof(int)*n_addr);
         size = sizeof(int)*n_addr;
         db_get_data(hDB, hKeyAddr, addr, &size, TID_INT);
      } else {
         /* create new address array */
         db_create_key(hDB, hKeyCurSubm, "Address", TID_INT);
         db_find_key(hDB, hKeyCurSubm, "Address", &hKeyAddr);
         n_addr = 0;
         addr = (int *)malloc(sizeof(int));
      }

      comment_created = FALSE;
      db_find_key(hDB, hKeyCurSubm, "Node comment", &hKeyComm);
      if (hKeyComm) {
         /* get current node comments */
         db_get_key(hDB, hKeyComm, &key);
         node_comment = (char *)malloc(32*key.num_values);
         size = 32*key.num_values;
         db_get_data(hDB, hKeyComm, node_comment, &size, TID_STRING);
      } else {
         /* create new comment array */
         db_create_key(hDB, hKeyCurSubm, "Node comment", TID_STRING);
         db_find_key(hDB, hKeyCurSubm, "Node comment", &hKeyComm);
         node_comment = (char *)malloc(32);
         comment_created = TRUE;
      }

      fd = mscb_init(cur_subm_name, 0, "", FALSE);
      if (fd >= 0) {
         /* fill table of possible addresses */
         for (i=0 ; i<0x10000 ; i++)
            ping_addr[i] = 0;
         for (i=0 ; i<1000 ; i++)        // 0..999
            ping_addr[i] = 1;
         for (i=0 ; i<0x10000 ; i+=100)  // 100, 200, ...
            ping_addr[i] = 1;
         for (i=0 ; i<0x10000 ; i+= 0x100)
            ping_addr[i] = 1;            // 256, 512, ...
         for (i=0xFF00 ; i<0x10000 ; i++)
            ping_addr[i] = 1;            // 0xFF00-0xFFFF

         for (ind = n = 0; ind < 0x10000; ind++) {
            if (!ping_addr[ind])
               continue;

            status = mscb_ping(fd, (unsigned short) ind, 1, 0);
            if (status == MSCB_SUCCESS) {

               /* node found, search next 100 as well */
               for (j=ind; j<ind+100 && j<0x10000 ; j++)
                  if (j >= 0)
                     ping_addr[j] = 1;

               status = mscb_info(fd, (unsigned short) ind, &info);

               if (status == MSCB_SUCCESS) {
                  /* check if node already in list */
                  for (j=0 ; j<n_addr ; j++)
                     if (addr[j] == ind)
                        break;
                  if (j == n_addr) {
                     addr = (int *)realloc(addr, sizeof(int)*(n_addr+1));
                     addr[n_addr] = ind;
                     node_comment = (char *)realloc(node_comment, 32*(n_addr+1));
                     /* use node name as default comment */
                     strncpy(node_comment+n_addr*32, info.node_name, 32);
                     n_addr ++;
                  } else if (comment_created) {
                     node_comment = (char *)realloc(node_comment, 32*n_addr);
                     /* use node name as default comment */
                     strncpy(node_comment+j*32, info.node_name, 32);
                  }
               }
            }
         }

         db_set_data(hDB, hKeyAddr, addr, n_addr*sizeof(int), n_addr, TID_INT);
         free(addr);
         db_set_data(hDB, hKeyComm, node_comment, n_addr*32, n_addr, TID_STRING);
         free(node_comment);

         char redirstr[512];
         sprintf(redirstr, "?cmd=mscb&subm=%s", cur_subm_name);
         redirect(r, redirstr);
         return;

      } else {
         char errorstr[512];
         sprintf(errorstr, "Cannot talk to submaster \"%s\"", cur_subm_name);
         show_error(r, errorstr);
         return;
      }
   }

   /* write data to node */
   if (p->isparam("subm") && p->isparam("node") &&
       p->isparam("idx") && p->isparam("value")) {
      i = atoi(p->getparam("idx"));
      char value[256];
      strlcpy(value, p->getparam("value"), sizeof(value));

      fd = mscb_init(cur_subm_name, 0, "", FALSE);
      if (fd >= 0) {
         status = mscb_info_variable(fd,
                                     (unsigned short) cur_node, (unsigned char) i, &info_var);
         if (status == MSCB_SUCCESS) {
            if (info_var.unit == UNIT_STRING) {
               char valstr[256];
               strlcpy(valstr, value, sizeof(valstr));
               if (strlen(valstr) > 0 && valstr[strlen(valstr) - 1] == '\n')
                  valstr[strlen(valstr) - 1] = 0;

               status = mscb_write(fd, (unsigned short) cur_node,
                                   (unsigned char) i, valstr, strlen(valstr) + 1);
            } else {
               if (info_var.flags & MSCBF_FLOAT) {
                  fvalue = (float) atof(value);
                  memcpy(&dbuf, &fvalue, sizeof(float));
               } else {
                  if (value[1] == 'x')
                     sscanf(value + 2, "%x", (int *)&dbuf);
                  else
                     *((int *)dbuf) = atoi(value);
               }

               status = mscb_write(fd, (unsigned short) cur_node,
                                   (unsigned char) i, dbuf, info_var.width);
            }
         }
      }
      char redirstr[512];
      sprintf(redirstr, "?cmd=mscb&subm=%s&node=%d", cur_subm_name, cur_node);
      redirect(r, redirstr);
      return;
   }

   if (p->isparam("hidden"))
      show_hidden = atoi(p->getparam("hidden"));
   else
      show_hidden = FALSE;

   show_header(r, "MSCB", "GET", "./", refresh);
   r->rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   r->rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   show_navigation_bar(r, "MSCB");

   /* style sheet */
   r->rsprintf("<style type=\"text/css\">\r\n");
   r->rsprintf("select { width:150px; background-color:#FFFFE0; font-size:12px; }\r\n");
   r->rsprintf(".subm {\r\n");
   r->rsprintf("  background-color:#E0E0E0; text-align:center; font-weight:bold;\r\n");
   r->rsprintf("  padding:5px;\r\n");
   r->rsprintf("  vertical-align:top;\r\n");
   r->rsprintf("  font-size:16px;\r\n");
   r->rsprintf("  border-right:1px solid #808080;\r\n");
   r->rsprintf("}\r\n");
   r->rsprintf(".node {\r\n");
   r->rsprintf("  background-color:#E0E0E0; text-align:center; font-weight:bold;\r\n");
   r->rsprintf("  padding:5px;\r\n");
   r->rsprintf("  vertical-align:top;\r\n");
   r->rsprintf("  font-size:16px;\r\n");
   r->rsprintf("  border-right:1px solid #808080;\r\n");
   r->rsprintf("}\r\n");
   r->rsprintf(".vars {\r\n");
   r->rsprintf("  background-color:#E0E0E0; text-align:center; font-weight:bold;\r\n");
   r->rsprintf("  padding:5px;\r\n");
   r->rsprintf("  vertical-align:top;\r\n");
   r->rsprintf("  font-size:10px;\r\n");
   r->rsprintf("}\r\n");
   r->rsprintf(".v1 {\r\n");
   r->rsprintf("  padding:3px;\r\n");
   r->rsprintf("  font-weight:bold;\r\n");
   r->rsprintf("  font-size:12px;\r\n");
   r->rsprintf("}\r\n");
   r->rsprintf(".v2 {\r\n");
   r->rsprintf("  background-color:#F0F0F0;\r\n");
   r->rsprintf("  padding:3px;\r\n");
   r->rsprintf("  font-size:12px;\r\n");
   r->rsprintf("  border:1px solid #808080;\r\n");
   r->rsprintf("  border-right:1px solid #FFFFFF;\r\n");
   r->rsprintf("  border-bottom:1px solid #FFFFFF;\r\n");
   r->rsprintf("}\r\n");
   r->rsprintf(".v3 {\r\n");
   r->rsprintf("  padding:3px;\r\n");
   r->rsprintf("  font-size:12px;\r\n");
   r->rsprintf("}\r\n");
   r->rsprintf("</style>\r\n\r\n");

   /* javascript */
   r->rsprintf("<script type=\"text/javascript\">\r\n");
   r->rsprintf("function mscb_edit(index, value)\r\n");
   r->rsprintf("{\r\n");
   r->rsprintf("   var new_value = prompt('Please enter new value', value);\r\n");
   r->rsprintf("   if (new_value != undefined) {\r\n");
   r->rsprintf("     window.location.search = '?cmd=mscb&subm=%s&node=%d&idx='+index+'&value='+new_value;\n", cur_subm_name, cur_node);
   r->rsprintf("   }\n");
   r->rsprintf("}\r\n");
   r->rsprintf("</script>\r\n\r\n");

   /*---- main content ----*/

   r->rsprintf("<table class=\"mtable\">");  //main table
   r->rsprintf("<tr><th class=\"mtableheader\" colspan=2>MSCB</th><tr>");

   /*---- menu buttons ----*/

   r->rsprintf("<tr><td colspan=2>\n");
   r->rsprintf("<table width=100%%><tr>\n");
   r->rsprintf("<td><input type=button value=Reload onclick=\"window.location.search='?cmd=mscb&subm=%s&node=%d&rnd=%d'\"></td>\n", cur_subm_name, cur_node, rand());

   r->rsprintf("<tr><td colspan=\"2\" cellpadding=\"0\" cellspacing=\"0\">\r\n");

   status = db_find_key(hDB, 0, "MSCB/Submaster", &hKeySubm);
   if (status != DB_SUCCESS) {
      r->rsprintf("<h1>No MSCB Submasters defined in ODB</h1>\r\n");
      r->rsprintf("</td></tr>\r\n");
      r->rsprintf("</table>\r\n"); //submaster table
      r->rsprintf("</td></tr>\r\n");
      r->rsprintf("</table>\r\n");  //main table
      r->rsprintf("</div>\n"); // closing for <div id="mmain">
      r->rsprintf("</form>\n");
      r->rsprintf("</body></html>\r\n");
      return;
   }

   r->rsprintf("<table width=\"100%%\" cellpadding=\"0\" cellspacing=\"0\">");

   /*---- submaster list ----*/
   r->rsprintf("<tr><td class=\"subm\">\r\n");
   r->rsprintf("Submaster<hr>\r\n");

   /* count submasters */
   for (i = 0;;i++) {
      db_enum_key(hDB, hKeySubm, i, &hKey);
      if (!hKey)
         break;
   }
   if (i<2)
      i = 2;

   r->rsprintf("<select name=\"subm\" id=\"subm\" size=%d ", i);
   r->rsprintf("onChange=\"window.location.search='?cmd=mscb&subm='+document.getElementById('subm').value;\">\r\n");
   hKeyCurSubm = 0;
   for (i = 0;;i++) {
      db_enum_key(hDB, hKeySubm, i, &hKey);
      if (!hKey)
         break;
      db_get_key(hDB, hKey, &key);
      char str[NAME_LENGTH+10+256];
      strlcpy(str, key.name, sizeof(str));
      char comment[256];
      size = sizeof(comment);
      if (db_get_value(hDB, hKey, "Comment", comment, &size, TID_STRING, FALSE) == DB_SUCCESS) {
         strlcat(str, ": ", sizeof(str));
         strlcat(str, comment, sizeof(str));
      }

      if ((cur_subm_name[0] && equal_ustring(cur_subm_name, key.name)) ||
          (cur_subm_name[0] == 0 && i == 0)) {
         r->rsprintf("<option value=\"%s\" selected>%s</option>\r\n", key.name, str);
         hKeyCurSubm = hKey;
      } else
         r->rsprintf("<option value=\"%s\">%s</option>\r\n", key.name, str);
   }
   r->rsprintf("</select>\r\n");

   /*---- node list ----*/
   r->rsprintf("<td class=\"node\">\r\n");
   r->rsprintf("Node ");

   r->rsprintf("<script type=\"text/javascript\">\n");
   r->rsprintf("<!--\n");
   r->rsprintf("function rescan()\n");
   r->rsprintf("{\n");
   r->rsprintf("   flag = confirm('Rescan can take up to one minute.');\n");
   r->rsprintf("   if (flag == true)\n");
   r->rsprintf("      window.location.href = '?cmd=mscb&mcmd=Rescan&subm=%s';\n", cur_subm_name);
   r->rsprintf("}\n");
   r->rsprintf("//-->\n");
   r->rsprintf("</script>\n");

   r->rsprintf("<input type=button name=cmd value=\"Rescan\" onClick=\"rescan();\">");
   r->rsprintf("<hr>\r\n");

   if (!hKeyCurSubm) {
      r->rsprintf("No submaster found in ODB\r\n");
      r->rsprintf("</td></tr>\r\n");
      r->rsprintf("</table>\r\n");  //inner submaster table
      r->rsprintf("</td></tr>\r\n");
      r->rsprintf("</table>\r\n");  //submaster table
      r->rsprintf("</td></tr>\r\n");
      r->rsprintf("</table>\r\n"); //main table
      r->rsprintf("</div>\n"); // closing for <div id="mmain">
      r->rsprintf("</form>\n");
      r->rsprintf("</body></html>\r\n");
      return;
   }

   db_find_key(hDB, hKeyCurSubm, "Address", &hKeyAddr);
   db_find_key(hDB, hKeyCurSubm, "Node comment", &hKeyComm);

   i = 10;
   if (hKeyAddr) {
      db_get_key(hDB, hKeyAddr, &key);
      i = key.num_values;

      if (hKeyComm == 0) {
         db_create_key(hDB, hKeyCurSubm, "Node comment", TID_STRING);
         db_find_key(hDB, hKeyCurSubm, "Node comment", &hKeyComm);
      }
      db_get_key(hDB, hKeyComm, &key);
      if (key.num_values < i) {
         char str[32] = "";
         for (int j=key.num_values ; j<i ; j++)
            db_set_data_index(hDB, hKeyComm, str, 32, j, TID_STRING);
      }
   }
   if (i < 2)
      i = 2;

   r->rsprintf("<select name=\"node\" id=\"node\" size=%d ", i);
   r->rsprintf("onChange=\"window.location.search='?cmd=mscb&subm=%s&node='+document.getElementById('node').value;\">\r\n", cur_subm_name);

   if (hKeyAddr) {
      db_get_key(hDB, hKeyAddr, &key);
      size = sizeof(adr);

      /* check if current node is in list */
      for (i = 0; i<key.num_values ;i++) {
         size = sizeof(adr);
         db_get_data_index(hDB, hKeyAddr, &adr, &size, i, TID_INT);
         if (adr == cur_node)
            break;
      }
      if (i == key.num_values) // if not found, use first one in list
         db_get_data_index(hDB, hKeyAddr, &cur_node, &size, 0, TID_INT);

      for (i = 0; i<key.num_values ;i++) {
         char str[100+256];
         size = sizeof(adr);
         db_get_data_index(hDB, hKeyAddr, &adr, &size, i, TID_INT);
         if (hKeyComm) {
            char comment[256];
            size = sizeof(comment);
            db_get_data_index(hDB, hKeyComm, comment, &size, i, TID_STRING);
            sprintf(str, "%d: %s", adr, comment);
         } else {
            sprintf(str, "%d", adr);
         }
         if (cur_node == 0 && i == 0)
            cur_node = adr;
         if (adr == cur_node)
            r->rsprintf("<option selected>%s</option>\r\n", str);
         else
            r->rsprintf("<option>%s</option>\r\n", str);
      }
   }
   r->rsprintf("</select>\r\n");

   /*---- node contents ----*/
   r->rsprintf("<td class=\"vars\">\r\n");
   r->rsprintf("<table>\r\n");
   db_get_key(hDB, hKeyCurSubm, &key);
   if (cur_node != -1)
      r->rsprintf("<tr><td colspan=3 align=center><b>%s:%d</b>", key.name, cur_node);
   else
      r->rsprintf("<tr><td colspan=3 align=center><b>%s</b>", key.name);
   r->rsprintf("<hr></td></tr>\r\n");

   char passwd[32];
   passwd[0] = 0;
   size = 32;
   db_get_value(hDB, hKeyCurSubm, "Pwd", passwd, &size, TID_STRING, TRUE);

   fd = mscb_init(key.name, 0, passwd, FALSE);
   if (fd < 0) {
      if (fd == EMSCB_WRONG_PASSWORD)
         r->rsprintf("<tr><td colspan=3><b>Invalid password</b></td>");
      else
         r->rsprintf("<tr><td colspan=3><b>Submaster does not respond</b></td>");
      goto mscb_error;
   }
   mscb_set_eth_max_retry(fd, 3);
   mscb_set_max_retry(1);

   status = mscb_ping(fd, cur_node, 0, 1);
   if (status != MSCB_SUCCESS) {
      r->rsprintf("<tr><td colspan=3><b>No response from node</b></td>");
      goto mscb_error;
   }
   status = mscb_info(fd, (unsigned short) cur_node, &info);
   if (status != MSCB_SUCCESS) {
      r->rsprintf("<tr><td colspan=3><b>No response from node</b></td>");
      goto mscb_error;
   }
   char tr16[17];
   strlcpy(tr16, info.node_name, sizeof(tr16));
   r->rsprintf("<tr><td class=\"v1\">Node name<td colspan=2 class=\"v2\">%s</tr>\n", tr16);
   r->rsprintf("<tr><td class=\"v1\">GIT revision<td colspan=2 class=\"v2\">%d</tr>\n", info.revision);

   if (info.rtc[0] && info.rtc[0] != 0xFF) {
      for (i=0 ; i<6 ; i++)
         info.rtc[i] = (info.rtc[i] / 0x10) * 10 + info.rtc[i] % 0x10;
      r->rsprintf("<tr><td class=\"v1\">Real Time Clock<td colspan=2 class=\"v2\">%02d-%02d-%02d %02d:%02d:%02d</td>\n",
         info.rtc[0], info.rtc[1], info.rtc[2],
         info.rtc[3], info.rtc[4], info.rtc[5]);
   }

   status = mscb_uptime(fd, (unsigned short) cur_node, &uptime);
    if (status == MSCB_SUCCESS)
      r->rsprintf("<tr><td class=\"v1\">Uptime<td colspan=2 class=\"v2\">%dd %02dh %02dm %02ds</tr>\n",
             uptime / (3600 * 24),
             (uptime % (3600 * 24)) / 3600, (uptime % 3600) / 60,
             (uptime % 60));

   r->rsprintf("<tr><td colspan=3><hr></td></tr>\r\n");

   /* check for hidden variables */
   for (i=0 ; i < info.n_variables ; i++) {
      mscb_info_variable(fd, cur_node, i, &info_var);
      if (info_var.flags & MSCBF_HIDDEN)
         break;
   }
   if (i < info.n_variables) {
      char str[32];
      strcpy(str, show_hidden ? " checked" : "");
      r->rsprintf("<tr><td colspan=3><input type=checkbox%s name=\"hidden\" value=\"1\"", str);
      r->rsprintf("onChange=\"window.location.search=?cmd=mscb&subm=%s&node=%d&hidden=1\">Display hidden variables<hr></td></tr>\r\n", cur_subm_name, cur_node);
   }

   /* read variables in blocks of 100 bytes */
   for (fi=0 ; fi < info.n_variables ; ) {
      for (i=fi,size=0 ; i < info.n_variables && size < 100; i++) {
         mscb_info_variable(fd, cur_node, i, &info_var);
         size += info_var.width;
      }

      size = sizeof(dbuf);
      status = mscb_read_range(fd, cur_node, fi, i-1, dbuf, &size);
      if (status != MSCB_SUCCESS) {
         r->rsprintf("<tr><td colspan=3><b>Error reading data from node</b></td>");
         goto mscb_error;
      }
      pd = dbuf;

      for (j=fi ; j<i ; j++) {
         status = mscb_info_variable(fd, cur_node, j, &info_var);
         if ((info_var.flags & MSCBF_HIDDEN) == 0 || show_hidden) {
            char tr8[9];
            strlcpy(tr8, info_var.name, sizeof(tr8));
            r->rsprintf("<tr><td class=\"v1\">%s</td>\r\n", tr8);
            r->rsprintf("<td class=\"v2\">\r\n");
            char value[256];
            print_mscb_var(value, evalue, unit, &info_var, pd);
            r->rsprintf("<a href=\"#\" onClick=\"mscb_edit(%d,'%s')\">%s</a>",
               j, evalue, value);
            r->rsprintf("</td><td class=\"v3\">%s</td>", unit);
            r->rsprintf("</tr>\r\n");
         }
         pd += info_var.width;
      }

      fi = i;
   }

mscb_error:
   r->rsprintf("</tr></table>\r\n");
   r->rsprintf("</td></tr></table>\r\n");
   r->rsprintf("</td></tr></table>\r\n");
   r->rsprintf("</td></tr></table>\r\n");
   r->rsprintf("</div></body></html>\r\n");
}

#endif // HAVE_MSCB

/*------------------------------------------------------------------*/

void show_password_page(Return* r, const char* dec_path, const char *password)
{
   r->rsprintf("HTTP/1.1 200 Document follows\r\n");
   r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
   r->rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

   r->rsprintf("<html><head>\n");
   r->rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
   r->rsprintf("<link rel=\"stylesheet\" href=\"midas.css\" type=\"text/css\" />\n");
   r->rsprintf("<link rel=\"stylesheet\" href=\"mhttpd.css\" type=\"text/css\" />\n");
   r->rsprintf("<title>Enter password</title></head><body>\n\n");

   r->rsprintf("<form method=\"GET\" action=\".\">\n\n");

   /*---- page header ----*/
   r->rsprintf("<table class=\"headerTable\"><tr><td></td><tr></table>\n");

   r->rsprintf("<table class=\"dialogTable\">\n");  //main table
   if (password[0])
      r->rsprintf("<tr><th class=\"redLight\">Wrong password!</tr>\n");

   r->rsprintf("<tr><th>Please enter password</tr>\n");
   r->rsprintf("<tr><td align=center><input type=password name=pwd></tr>\n");
   r->rsprintf("<tr><td align=center><input type=submit value=Submit></tr>");

   r->rsprintf("</table>\n");

   r->rsprintf("</div>\n"); // closing for <div id="mmain">
   r->rsprintf("</form>\n");
   r->rsprintf("</body></html>\r\n");
}

/*------------------------------------------------------------------*/

BOOL check_web_password(Return* r, HNDLE hDB, const char* dec_path, const char *password, const char *redir)
{
   HNDLE hkey;
   INT size;
   char str[256];

   /* check for password */
   db_find_key(hDB, 0, "/Experiment/Security/Web Password", &hkey);
   if (hkey) {
      size = sizeof(str);
      db_get_data(hDB, hkey, str, &size, TID_STRING);
      if (strcmp(password, str) == 0)
         return TRUE;

      /* show web password page */
      r->rsprintf("HTTP/1.1 200 Document follows\r\n");
      r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
      r->rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

      r->rsprintf("<html><head>\n");
      r->rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
      r->rsprintf("<link rel=\"stylesheet\" href=\"midas.css\" type=\"text/css\" />\n");
      r->rsprintf("<link rel=\"stylesheet\" href=\"mhttpd.css\" type=\"text/css\" />\n");
      r->rsprintf("<title>Enter password</title></head><body>\n\n");

      r->rsprintf("<form method=\"GET\" action=\".\">\n\n");

      /* define hidden fields for current experiment and destination */
      if (redir[0])
         r->rsprintf("<input type=hidden name=redir value=\"%s\">\n", redir);

      /*---- page header ----*/
      r->rsprintf("<table class=\"headerTable\"><tr><td></td><tr></table>\n");

      r->rsprintf("<table class=\"dialogTable\">\n");  //main table

      if (password[0])
         r->rsprintf("<tr><th class=\"redLight\">Wrong password!</tr>\n");

      r->rsprintf
          ("<tr><th>Please enter password to obtain write access</tr>\n");
      r->rsprintf("<tr><td align=center><input type=password name=wpwd></tr>\n");
      r->rsprintf("<tr><td align=center><input type=submit value=Submit></tr>");

      r->rsprintf("</table>\n");

      r->rsprintf("</div>\n"); // closing for <div id="mmain">
      r->rsprintf("</form>\n");
      r->rsprintf("</body></html>\r\n");

      return FALSE;
   } else
      return TRUE;
}

/*------------------------------------------------------------------*/

void show_odb_page(Param* pp, Return* r, const char* dec_path, int write_access)
{
   int keyPresent, size, status, line, link_index;
   char colspan;
   char style[32];
   HNDLE hDB, hkey, hkeyroot;
   KEY key;
   DWORD delta;

   cm_get_experiment_database(&hDB, NULL);

   //printf("path [%s]\n", dec_path);

   if (strcmp(dec_path, "root") == 0) {
      dec_path = "";
   }

   char xdecpath[256];
   strlcpy(xdecpath, dec_path, sizeof(xdecpath));
   if (strrchr(xdecpath, '/'))
      strlcpy(xdecpath, strrchr(xdecpath, '/')+1, sizeof(xdecpath));
   if (xdecpath[0] == 0)
      strlcpy(xdecpath, "root", sizeof(xdecpath));
   show_header(r, "MIDAS online database", "", xdecpath, 0);

   /* use javascript file */
   r->rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   r->rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   r->rsprintf("<script type=\"text/javascript\" src=\"obsolete.js\"></script>\n");
   r->rsprintf("<script type=\"text/javascript\" src=\"controls.js\"></script>\n");

   /* find key via path */
   status = db_find_key(hDB, 0, dec_path, &hkeyroot);
   if (status != DB_SUCCESS) {
      r->rsprintf("Error: cannot find key %s<P>\n", dec_path);
      r->rsprintf("</body></html>\r\n");
      return;
   }

   char xdec_path[MAX_ODB_PATH];

   /* if key is not of type TID_KEY, cut off key name */
   db_get_key(hDB, hkeyroot, &key);
   if (key.type != TID_KEY) {
      strlcpy(xdec_path, dec_path, sizeof(xdec_path));
      
      /* strip variable name from path */
      char* p = xdec_path + strlen(xdec_path) - 1;
      while (*p && *p != '/')
         *p-- = 0;
      if (*p == '/')
         *p = 0;

      status = db_find_key(hDB, 0, xdec_path, &hkeyroot);
      if (status != DB_SUCCESS) {
         r->rsprintf("Error: cannot find key %s<P>\n", xdec_path);
         r->rsprintf("</body></html>\r\n");
         return;
      }

      dec_path = xdec_path;
   }

   //strlcpy(enc_path, dec_path, enc_path_size);
   //urlEncode(enc_path, enc_path_size);

   char odbpath[MAX_ODB_PATH];
   status = db_get_path(hDB, hkeyroot, odbpath, sizeof(odbpath));

   /*---- navigation bar ----*/

   colspan = 7;

   if (elog_mode) {
      r->rsprintf("<table class=\"mtableheader\">\n");
      r->rsprintf("<tr><td colspan=%d>\n", colspan);
      r->rsprintf("<input type=button value=ELog onclick=\"self.location=\'?cmd=Alarms\';\">\n");
      r->rsprintf("</td></tr></table>\n\n");
   } else
      show_navigation_bar(r, "ODB");

   /*---- begin ODB directory table ----*/

   r->rsprintf("<table class=\"mtable\" style=\"border-spacing:0px;\">\n");
   r->rsprintf("<tr><th colspan=%d class=\"mtableheader\">Online Database Browser</tr>\n", colspan);
   //buttons:
   if(!elog_mode){
      r->rsprintf("<tr><td colspan=%d>\n", colspan);
      r->rsprintf("<input type=button value=Find onclick=\"self.location=\'?cmd=Find\';\">\n");
      r->rsprintf("<input type=button value=Create onclick=\"dlgShow('dlgCreate')\">\n");
      r->rsprintf("<input type=button value=Link   onclick=\"dlgShow('dlgLink')\">\n");
      r->rsprintf("<input type=button value=Delete onclick=\"dlgShow('dlgDelete')\">\n");
      r->rsprintf("<input type=button value=\"Create Elog from this page\" onclick=\"self.location=\'?cmd=Create Elog from this page&odb_path=%s\';\"></td></tr>\n", urlEncode(odbpath).c_str());
   }

   /*---- Build the Delete dialog------------------------------------*/

   std::string dd = "";

   dd += "<!-- Demo dialog -->\n";
   dd += "<div id=\"dlgDelete\" class=\"dlgFrame\">\n";
   dd += "<div class=\"dlgTitlebar\">Delete ODB entry</div>\n";
   dd += "<div class=\"dlgPanel\">\n";
   dd += "<div id=odbpath>";
   dd += "\"";
   dd += MJsonNode::Encode(odbpath);
   dd += "\"";
   dd += "</div>\n";
   dd += "<div><br></div>\n";

   dd += "<table class=\"dialogTable\">\n";
   dd += "<th colspan=2>Delete ODB entries:</th>\n";

   std::vector<std::string> delete_list;

   int count_delete = 0;

   /*---- ODB display -----------------------------------------------*/

   /* display root key */
   r->rsprintf("<tr><td colspan=%d class='ODBpath'><b>", colspan);
   r->rsprintf("<a href=\"?cmd=odb\">/</a> \n");

   std::string enc_root_path;

   /*---- display path ----*/
   {
      const char* p = dec_path;
      while (*p) {
         std::string pd;
         while (*p && *p != '/')
            pd += *p++;
         
         enc_root_path += urlEncode(pd.c_str());
         
         if (pd.length() > 0)
            r->rsprintf("<a href=\"?cmd=odb&odb_path=%s\">%s</a>\n / ", enc_root_path.c_str(), pd.c_str());
         
         enc_root_path += "/";
         if (*p == '/')
            p++;
      }
   }

   r->rsprintf("</b></tr>\n");

   /* enumerate subkeys */
   keyPresent = 0;
   for(int scan=0; scan<2; scan++){
      if(scan==1 && keyPresent==1) {
         r->rsprintf("<tr class=\"titleRow\">\n");
         r->rsprintf("<th class=\"ODBkey\">Key</th>\n");
         r->rsprintf("<th class=\"ODBvalue\">Value&nbsp;");
         r->rsprintf("<script type=\"text/javascript\">\n");
         r->rsprintf("function expand()\n");
         r->rsprintf("{\n");
         r->rsprintf("  var n = document.getElementsByName('ext');\n");
         r->rsprintf("  for (i=0 ; i<n.length ; i++) {\n");
         r->rsprintf("    if (n[i].style.display == 'none')\n");
         r->rsprintf("       n[i].style.display = 'table-cell';\n");
         r->rsprintf("    else\n");
         r->rsprintf("       n[i].style.display = 'none';\n");
         r->rsprintf("  }\n");
         r->rsprintf("  if (document.getElementById('expp').expflag === true) {\n");
         r->rsprintf("    document.getElementById('expp').expflag = false;\n");
         r->rsprintf("    document.getElementById('expp').innerHTML = '&#x21E5;';\n");
         r->rsprintf("  } else {\n");
         r->rsprintf("    document.getElementById('expp').expflag = true;\n");
         r->rsprintf("    document.getElementById('expp').innerHTML = '&#x21E4;';\n");
         r->rsprintf("  }\n");
         r->rsprintf("}\n");
         r->rsprintf("</script>");
         r->rsprintf("<div style=\"display:inline;float:right\"><a id=\"expp\"href=\"#\" onClick=\"expand();return false;\">&#x21E5;</div>");
         r->rsprintf("</th>\n");
         r->rsprintf("<th class=\"ODBvalue\" name=\"ext\" style=\"display:none\">Type</th>\n");
         r->rsprintf("<th class=\"ODBvalue\" name=\"ext\" style=\"display:none\">#Val</th>\n");
         r->rsprintf("<th class=\"ODBvalue\" name=\"ext\" style=\"display:none\">Size</th>\n");
         r->rsprintf("<th class=\"ODBvalue\" name=\"ext\" style=\"display:none\">Written</th>\n");
         r->rsprintf("<th class=\"ODBvalue\" name=\"ext\" style=\"display:none\">Mode</th>\n");
         r->rsprintf("</tr>\n");
      }
      line = 0;
      for (int i = 0;; i++) {
         db_enum_link(hDB, hkeyroot, i, &hkey);
         if (!hkey)
            break;
         db_get_link(hDB, hkey, &key);

         if (scan == 0) {
            delete_list.push_back(key.name);
         }

         if (line % 2 == 0)
            strlcpy(style, "ODBtableEven", sizeof(style));
         else
            strlcpy(style, "ODBtableOdd", sizeof(style));

         std::string keyname = key.name;
         std::string enc_keyname = urlEncode(key.name);

         std::string enc_full_path = enc_root_path + enc_keyname;

         std::string odb_path = dec_path;
         if (odb_path.length() > 0 && odb_path[odb_path.length() - 1] != '/')
            odb_path += "/";
         odb_path += key.name;

         /* resolve links */
         std::string link_ref;
         char link_name[MAX_ODB_PATH];
         link_name[0] = 0;
         status = DB_SUCCESS;
         if (key.type == TID_LINK) {
            size = sizeof(link_name);
            db_get_link_data(hDB, hkey, link_name, &size, TID_LINK);

            status = db_find_key(hDB, 0, link_name, &hkey);

            if (status == DB_SUCCESS)
               db_get_key(hDB, hkey, &key);

            //sprintf(link_ref, "?cmd=Set&odb_path=%s", full_path);
            link_ref = "?cmd=Set&odb_path=";
            link_ref += enc_full_path;

            if (status == DB_SUCCESS && link_name[0] == 0) {
               // fake the case when an empty link somehow resolves
               sprintf(link_name, "%s", "(empty)");
            }
         }

         std::string ref;

         if (link_name[0]) {
            if (enc_root_path.back() == '/' && link_name[0] == '/') {
               //sprintf(ref, "?cmd=Set&odb_path=%s%s", root_path, link_name+1);
               ref = "";
               ref += "?cmd=Set&odb_path=";
               ref += enc_root_path;
               ref += urlEncode(link_name + 1);
            } else {
               //sprintf(ref, "?cmd=Set&odb_path=%s%s", root_path, link_name);
               ref = "";
               ref += "?cmd=Set&odb_path=";
               ref += enc_root_path;
               ref += urlEncode(link_name);
            }
         } else {
            //sprintf(ref, "?cmd=Set&odb_path=%s", full_path);
            ref = "";
            ref += "?cmd=Set&odb_path=";
            ref += enc_full_path;
         }

         if (status != DB_SUCCESS) {
            if (scan == 1) {
               r->rsprintf("<tr><td class=\"yellowLight\">");
               r->rsprintf("%s <i>&rarr; <a href=\"%s\">%s</a></i><td><b><div style=\"color:red\">&lt;cannot resolve link&gt;</div></b></tr>\n", keyname.c_str(), link_ref.c_str(), link_name[0]?link_name:"(empty)");
            }
         } else {

            if (key.type == TID_KEY && scan == 0) {
               /* for keys, don't display data value */
               r->rsprintf("<tr><td colspan=%d class=\"ODBdirectory\"><a href=\"?cmd=odb&odb_path=%s\">&#x25B6 %s</a>\n", colspan, enc_full_path.c_str(), keyname.c_str());
               if (link_name[0])
                  r->rsprintf("<i>&rarr; <a href=\"?cmd=odb&odb_path=%s\">%s</a></i>", link_ref.c_str(), link_name);
               r->rsprintf("</tr>\n");
            } else if(key.type != TID_KEY && scan == 1) {

               if (strchr(link_name, '['))
                  link_index = atoi(strchr(link_name, '[')+1);
               else
                  link_index = -1;

               /* display single value */
               if (key.num_values == 1 || link_index != -1) {
                  char data[TEXT_SIZE];
                  char data_str[TEXT_SIZE];
                  size = sizeof(data);
                  db_get_data(hDB, hkey, data, &size, key.type);
                  if (link_index != -1)
                     db_sprintf(data_str, data, key.item_size, link_index, key.type);
                  else
                     db_sprintf(data_str, data, key.item_size, 0, key.type);
                  assert(strlen(data_str) < sizeof(data_str));

                  if (key.type == TID_STRING) {
                     if (strlen(data_str) >= MAX_STRING_LENGTH-1) {
                        strlcat(data_str, "...(truncated)", sizeof(data_str));
                     }
                  }

                  char hex_str[256];

                  if (key.type != TID_STRING) {
                     if (link_index != -1)
                        db_sprintfh(hex_str, data, key.item_size, link_index, key.type);
                     else
                        db_sprintfh(hex_str, data, key.item_size, 0, key.type);
                     assert(strlen(hex_str) < sizeof(hex_str));
                  } else {
                     hex_str[0] = 0;
                  }

                  if (data_str[0] == 0 || equal_ustring(data_str, "<NULL>")) {
                     strcpy(data_str, "(empty)");
                     hex_str[0] = 0;
                  }

                  r->rsprintf("<tr>\n");
                  if (strcmp(data_str, hex_str) != 0 && hex_str[0]) {
                     if (link_name[0]) {
                        r->rsprintf("<td class=\"ODBkey\">\n");
                        r->rsprintf("%s <i>&rarr; ", keyname.c_str());
                        r->rsprintf("<a href=\"%s\">%s</a></i>\n", link_ref.c_str(), link_name);
                        r->rsprintf("<td class=\"%s\">\n", style);
                        if (!write_access)
                           r->rsprintf("%s (%s)", data_str, hex_str);
                        else {
                           r->rsprintf("<a href=\"%s\" onClick=\"ODBInlineEdit(this.parentNode,\'%s\');return false;\" ", ref.c_str(), odb_path.c_str());
                           r->rsprintf("onFocus=\"ODBInlineEdit(this.parentNode,\'%s\');\">%s (%s)</a>\n", odb_path.c_str(), data_str, hex_str);
                        }
                     } else {
                        r->rsprintf("<td class=\"ODBkey\">\n");
                        r->rsprintf("%s<td class=\"%s\">", keyname.c_str(), style);
                        if (!write_access)
                           r->rsprintf("%s (%s)", data_str, hex_str);
                        else {
                           r->rsprintf("<a href=\"%s\" onClick=\"ODBInlineEdit(this.parentNode,\'%s\');return false;\" ", ref.c_str(), odb_path.c_str());
                           r->rsprintf("onFocus=\"ODBInlineEdit(this.parentNode,\'%s\');\">%s (%s)</a>\n", odb_path.c_str(), data_str, hex_str);
                        }
                     }
                  } else {
                     if (strchr(data_str, '\n')) {
                        if (link_name[0]) {
                           r->rsprintf("<td class=\"ODBkey\">");
                           r->rsprintf("%s <i>&rarr; <a href=\"%s\">%s</a></i><td class=\"ODBvalue\">", keyname.c_str(), link_ref.c_str(), link_name);
                        } else
                           r->rsprintf("<td class=\"ODBkey\">%s<td class=\"%s\">", keyname.c_str(), style);
                        r->rsprintf("\n<pre>");
                        strencode3(r, data_str);
                        r->rsprintf("</pre>");
                        if (strlen(data) > strlen(data_str))
                           r->rsprintf("<i>... (%d bytes total)<p>\n", (int)strlen(data));

                        r->rsprintf("<a href=\"%s\">Edit</a>\n", ref.c_str());
                     } else {
                        if (link_name[0]) {
                           r->rsprintf("<td class=\"ODBkey\">\n");
                           r->rsprintf("%s <i>&rarr; <a href=\"%s\">%s</a></i><td class=\"%s\">", keyname.c_str(), link_ref.c_str(), link_name, style);
                           if (!write_access)
                              strencode(r, data_str);
                           else {
                              r->rsprintf("<a href=\"%s\" onClick=\"ODBInlineEdit(this.parentNode,\'%s\');return false;\" ", ref.c_str(), odb_path.c_str());
                              r->rsprintf("onFocus=\"ODBInlineEdit(this.parentNode,\'%s\');\">", odb_path.c_str());
                              strencode(r, data_str);
                              r->rsprintf("</a>\n");
                           }
                        } else {
                           r->rsprintf("<td class=\"ODBkey\">%s<td class=\"%s\">", keyname.c_str(), style);
                           if (!write_access) {
                              strencode(r, data_str);
                           } else {
                              r->rsprintf("<a href=\"%s\" onClick=\"ODBInlineEdit(this.parentNode,\'%s\');return false;\" ", ref.c_str(), odb_path.c_str());
                              r->rsprintf("onFocus=\"ODBInlineEdit(this.parentNode,\'%s\');\">", odb_path.c_str());
                              strencode(r, data_str);
                              r->rsprintf("</a>\n");
                           }
                        }
                     }
                  }

                  /* extended key information */
                  r->rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\">");
                  r->rsprintf("%s", rpc_tid_name(key.type));
                  r->rsprintf("</td>\n");

                  r->rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\">");
                  r->rsprintf("%d", key.num_values);
                  r->rsprintf("</td>\n");

                  r->rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\">");
                  r->rsprintf("%d", key.item_size);
                  r->rsprintf("</td>\n");

                  r->rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\">");
                  db_get_key_time(hDB, hkey, &delta);
                  if (delta < 60)
                     r->rsprintf("%ds", delta);
                  else if (delta < 3600)
                     r->rsprintf("%1.0lfm", delta / 60.0);
                  else if (delta < 86400)
                     r->rsprintf("%1.0lfh", delta / 3600.0);
                  else if (delta < 86400 * 99)
                     r->rsprintf("%1.0lfh", delta / 86400.0);
                  else
                     r->rsprintf(">99d");
                  r->rsprintf("</td>\n");

                  r->rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\">");
                  if (key.access_mode & MODE_READ)
                     r->rsprintf("R");
                  if (key.access_mode & MODE_WRITE)
                     r->rsprintf("W");
                  if (key.access_mode & MODE_DELETE)
                     r->rsprintf("D");
                  if (key.access_mode & MODE_EXCLUSIVE)
                     r->rsprintf("E");
                  r->rsprintf("</td>\n");

                  line++;
                  r->rsprintf("</tr>\n");
               } else { /* display array value */
                  /* check for exceeding length */
                  if (key.num_values > 1000 && !pp->isparam("all"))
                     r->rsprintf("<tr><td class=\"ODBkey\">%s<td class=\"%s\"><span style=\"font-style: italic\"><a href=\"?cmd=odb&odb_path=%s&all=1\">... %d values ...</a></span>\n", keyname.c_str(), style, enc_full_path.c_str(), key.num_values);
                  else {
                     /* display first value */
                     if (link_name[0])
                        r->rsprintf("<tr><td class=\"ODBkey\" rowspan=%d>%s<br><i>&rarr; <a href=\"%s\">%s</a></i>\n", key.num_values, keyname.c_str(), link_ref.c_str(), link_name);
                     else
                        r->rsprintf("<tr><td class=\"ODBkey\" rowspan=%d>%s\n", key.num_values, keyname.c_str());

                     for (int j = 0; j < key.num_values; j++) {
                        char data[TEXT_SIZE];
                        char data_str[TEXT_SIZE];
                        char hex_str[256];

                        if (line % 2 == 0)
                           strlcpy(style, "ODBtableEven", sizeof(style));
                        else
                           strlcpy(style, "ODBtableOdd", sizeof(style));

                        size = sizeof(data);
                        db_get_data_index(hDB, hkey, data, &size, j, key.type);
                        db_sprintf(data_str, data, key.item_size, 0, key.type);
                        assert(strlen(data_str) < sizeof(data_str));

                        if (key.type == TID_STRING || key.type == TID_LINK) {
                           hex_str[0] = 0;
                        } else {
                           db_sprintfh(hex_str, data, key.item_size, 0, key.type);
                           assert(strlen(hex_str) < sizeof(hex_str));
                        }

                        if (key.type == TID_STRING) {
                           if (strlen(data_str) >= MAX_STRING_LENGTH-1) {
                              strlcat(data_str, "...(truncated)", sizeof(data_str));
                           }
                        }

                        if (data_str[0] == 0 || equal_ustring(data_str, "<NULL>")) {
                           strcpy(data_str, "(empty)");
                           hex_str[0] = 0;
                        }

                        //sprintf(ref, "?cmd=Set&odb_path=%s&index=%d", full_path, j);
                        ref = "";
                        ref += "?cmd=Set&odb_path=";
                        ref += enc_full_path;
                        ref += "&index=";
                        ref += toString(j);

                        std::string tmpstr;
                        //sprintf(str, "%s[%d]", odb_path, j);
                        tmpstr += odb_path;
                        tmpstr += "[";
                        tmpstr += toString(j);
                        tmpstr += "]";

                        if (j > 0)
                           r->rsprintf("<tr>");

                        r->rsprintf("<td class=\"%s\">[%d]&nbsp;", style, j);
                        if (!write_access)
                           r->rsprintf("<a href=\"%s\">", ref.c_str());
                        else {
                           r->rsprintf("<a href=\"%s\" onClick=\"ODBInlineEdit(this.parentNode,\'%s\');return false;\" ", ref.c_str(), tmpstr.c_str());
                           r->rsprintf("onFocus=\"ODBInlineEdit(this.parentNode,\'%s\');\">", tmpstr.c_str());
                        }
                        if (strcmp(data_str, hex_str) != 0 && hex_str[0])
                           r->rsprintf("%s (%s)</a>\n", data_str, hex_str);
                        else
                           r->rsprintf("%s</a>\n", data_str);

                        if (j == 0) {
                           /* extended key information */
                           r->rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\" rowspan=%d>", key.num_values);
                           r->rsprintf("%s", rpc_tid_name(key.type));
                           r->rsprintf("</td>\n");

                           r->rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\" rowspan=%d>", key.num_values);
                           r->rsprintf("%d", key.num_values);
                           r->rsprintf("</td>\n");

                           r->rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\" rowspan=%d>", key.num_values);
                           r->rsprintf("%d", key.item_size);
                           r->rsprintf("</td>\n");

                           r->rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\" rowspan=%d>", key.num_values);
                           db_get_key_time(hDB, hkey, &delta);
                           if (delta < 60)
                              r->rsprintf("%ds", delta);
                           else if (delta < 3600)
                              r->rsprintf("%1.0lfm", delta / 60.0);
                           else if (delta < 86400)
                              r->rsprintf("%1.0lfh", delta / 3600.0);
                           else if (delta < 86400 * 99)
                              r->rsprintf("%1.0lfh", delta / 86400.0);
                           else
                              r->rsprintf(">99d");
                           r->rsprintf("</td>\n");

                           r->rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\" rowspan=%d>", key.num_values);
                           if (key.access_mode & MODE_READ)
                              r->rsprintf("R");
                           if (key.access_mode & MODE_WRITE)
                              r->rsprintf("W");
                           if (key.access_mode & MODE_DELETE)
                              r->rsprintf("D");
                           if (key.access_mode & MODE_EXCLUSIVE)
                              r->rsprintf("E");
                           r->rsprintf("</td>\n");
                        }
                        line++;
                     }

                     r->rsprintf("</tr>\n");
                  }
               }
            } else if(key.type != TID_KEY){
               keyPresent = 1;  //flag that we've seen a key on the first pass, and should therefore write the Key / Value headline
            }
         }
      }
   }
   r->rsprintf("</table>\n");
   r->rsprintf("</div>\n"); // <div id="mmain">

   /*---- Build the Delete dialog------------------------------------*/

   std::sort(delete_list.begin(), delete_list.end());

   for (unsigned i=0; i<delete_list.size(); i++) {
      std::string name = delete_list[i];

      dd += "<tr><td style=\"text-align:left;\" align=left><input align=left type=checkbox id=delete";
      dd += toString(count_delete++);
      dd += " value=\'";
      dd += "\"";
      dd += MJsonNode::Encode(name.c_str());
      dd += "\"";
      dd += "\'>";
      dd += name;
      dd += "</input></td></tr>\n";
   }

   dd += "</table>\n";
   dd += "<input type=button value=Delete onClick='mhttpd_delete_page_handle_delete(event);'>\n";
   dd += "<input type=button value=Cancel onClick='mhttpd_delete_page_handle_cancel(event);'>\n";
   dd += "</div>\n";
   dd += "</div>\n";

   r->rsputs(dd.c_str());

   /*---- Build the Create dialog------------------------------------*/

   std::string cd = "";

   cd += "<!-- Demo dialog -->\n";
   cd += "<div id=\"dlgCreate\" class=\"dlgFrame\">\n";
   cd += "<div class=\"dlgTitlebar\">Create ODB entry</div>\n";
   cd += "<div class=\"dlgPanel\">\n";
   cd += "<br />\n";
   cd += "<div id=odbpath>";
   cd += "\"";
   cd += MJsonNode::Encode(odbpath);
   cd += "\"";
   cd += "</div>\n";
   cd += "<div><br></div>\n";

   cd += "<table class=\"dialogTable\">\n";
   cd += "<th colspan=2>Create ODB entry:</th>\n";
   cd += "<tr>";
   cd += "<td>Type";
   cd += "<td>";
   cd += "<select type=text size=1 id=create_tid name=type>";
   cd += "<option value=7>Integer (32-bit)";
   cd += "<option value=9>Float (4 Bytes)";
   cd += "<option value=12>String";
   cd += "<option selected value=15>Subdirectory";
   cd += "<option value=1>Byte";
   cd += "<option value=2>Signed byte";
   cd += "<option value=3>Character (8-bit)";
   cd += "<option value=4>Word (16-bit)";
   cd += "<option value=5>Short integer (16-bit)";
   cd += "<option value=6>Double Word (32-bit)";
   cd += "<option value=8>Boolean";
   cd += "<option value=10>Double float (8 Bytes)";
   //cd += "<option value=16>Symbolic link";
   cd += "</select>";
   cd += "</tr>\n";
   cd += "<tr><td>Name<td><input type=text size=31 maxlength=31 id=create_name name=value></tr>\n";
   cd += "<tr><td>Array size<td><input type=text size=31 maxlength=31 id=create_array_length name=index value=1></tr>\n";
   cd += "<tr><td>String length<td><input type=text size=31 maxlength=31 id=create_strlen name=strlen value=32></tr>\n";
   cd += "</table>\n";
   cd += "<input type=button value=Create onClick='mhttpd_create_page_handle_create(event);'>\n";
   cd += "<input type=button value=Cancel onClick='mhttpd_create_page_handle_cancel(event);'>\n";
   cd += "</div>\n";
   cd += "</div>\n";

   r->rsputs(cd.c_str());

   /*---- Build the Link dialog------------------------------------*/

   std::string ld = "";

   ld += "<!-- Demo dialog -->\n";
   ld += "<div id=\"dlgLink\" class=\"dlgFrame\">\n";
   ld += "<div class=\"dlgTitlebar\">Create a link to an ODB entry</div>\n";
   ld += "<div class=\"dlgPanel\">\n";
   ld += "<br />\n";
   ld += "<div id=link_odbpath>";
   ld += "\"";
   ld += MJsonNode::Encode(odbpath);
   ld += "\"";
   ld += "</div>\n";
   ld += "<div><br></div>\n";

   ld += "<table class=\"dialogTable\">\n";
   ld += "<th colspan=2>Create a link to an ODB entry:</th>\n";
   ld += "<tr><td>Name<td><input type=text size=31 maxlength=31 id=link_name name=value></tr>\n";
   ld += "<tr><td>Link target<td><input type=text size=31 maxlength=256 id=link_target name=target></tr>\n";
   ld += "</table>\n";
   ld += "<input type=button value=Link onClick='mhttpd_link_page_handle_link(event);'>\n";
   ld += "<input type=button value=Cancel onClick='mhttpd_link_page_handle_cancel(event);'>\n";
   ld += "</div>\n";
   ld += "</div>\n";

   r->rsputs(ld.c_str());
}

/*------------------------------------------------------------------*/

void show_set_page(Param* pp, Return* r,
                   const char *group,
                   int index, const char *value)
{
   int status, size;
   HNDLE hDB, hkey;
   KEY key;
   char data_str[TEXT_SIZE], str[256];
   char data[TEXT_SIZE];

   std::string odb_path = pp->getparam("odb_path");

   //printf("show_set_page: odb_path [%s] group [%s] index %d value [%s]\n", odb_path.c_str(), group, index, value);

   cm_get_experiment_database(&hDB, NULL);

   /* show set page if no value is given */
   if (!pp->isparam("value") && !*pp->getparam("text")) {
      status = db_find_link(hDB, 0, odb_path.c_str(), &hkey);
      if (status != DB_SUCCESS) {
         r->rsprintf("Error: cannot find key %s<P>\n", odb_path.c_str());
         return;
      }
      db_get_link(hDB, hkey, &key);

      show_header(r, "Set value", "POST", "", 0);
      //close header:
      r->rsprintf("</table>");

      //main table:
      r->rsprintf("<table class=\"dialogTable\">");

      if (index > 0)
         r->rsprintf("<input type=hidden name=index value=\"%d\">\n", index);
      else
         index = 0;

      if (group[0])
         r->rsprintf("<input type=hidden name=group value=\"%s\">\n", group);

      r->rsprintf("<input type=hidden name=odb_path value=\"%s\">\n", odb_path.c_str());

      strlcpy(data_str, rpc_tid_name(key.type), sizeof(data_str));
      if (key.num_values > 1) {
         sprintf(str, "[%d]", key.num_values);
         strlcat(data_str, str, sizeof(data_str));

         sprintf(str, "%s[%d]", odb_path.c_str(), index);
      } else
         strlcpy(str, odb_path.c_str(), sizeof(str));

      r->rsprintf("<tr><th colspan=2>Set new value - type = %s</tr>\n", data_str);
      r->rsprintf("<tr><td>%s<td>\n", str);

      /* set current value as default */
      size = sizeof(data);
      db_get_link_data(hDB, hkey, data, &size, key.type);
      db_sprintf(data_str, data, key.item_size, index, key.type);

      if (equal_ustring(data_str, "<NULL>"))
         data_str[0] = 0;

      if (strchr(data_str, '\n') != NULL) {
         r->rsprintf("<textarea rows=20 cols=80 name=\"text\">\n");
         strencode3(r, data);
         r->rsprintf("</textarea>\n");
      } else {
         size = 20;
         if ((int) strlen(data_str) > size)
            size = strlen(data_str) + 3;
         if (size > 80)
            size = 80;

         r->rsprintf("<input type=\"text\" size=%d maxlength=256 name=\"value\" value=\"", size);
         strencode(r, data_str);
         r->rsprintf("\">\n");
      }

      r->rsprintf("</tr>\n");

      r->rsprintf("<tr><td align=center colspan=2>");
      r->rsprintf("<input type=submit name=cmd value=Set>");
      r->rsprintf("<input type=submit name=cmd value=Cancel>");
      r->rsprintf("</tr>");
      r->rsprintf("</table>");

      r->rsprintf("<input type=hidden name=cmd value=Set>\n");

      r->rsprintf("</div>\n"); // closing for <div id="mmain">
      r->rsprintf("</form>\n");
      r->rsprintf("</body></html>\r\n");
      return;
   } else {
      /* set value */

      status = db_find_link(hDB, 0, odb_path.c_str(), &hkey);
      if (status != DB_SUCCESS) {
         r->rsprintf("Error: cannot find key %s<P>\n", odb_path.c_str());
         return;
      }
      db_get_link(hDB, hkey, &key);

      memset(data, 0, sizeof(data));

      if (pp->getparam("text") && *pp->getparam("text"))
         strlcpy(data, pp->getparam("text"), sizeof(data));
      else
         db_sscanf(value, data, &size, 0, key.type);

      if (index < 0)
         index = 0;

      /* extend data size for single string if necessary */
      if ((key.type == TID_STRING || key.type == TID_LINK)
          && (int) strlen(data) + 1 > key.item_size && key.num_values == 1)
         key.item_size = strlen(data) + 1;

      if (key.item_size == 0)
         key.item_size = rpc_tid_size(key.type);

      if (key.num_values > 1)
         status = db_set_link_data_index(hDB, hkey, data, key.item_size, index, key.type);
      else
         status = db_set_link_data(hDB, hkey, data, key.item_size, 1, key.type);

      if (status == DB_NO_ACCESS)
         r->rsprintf("<h2>Write access not allowed</h2>\n");

      redirect(r, "");

      return;
   }
}

/*------------------------------------------------------------------*/

void show_find_page(Return* r, const char *value)
{
   HNDLE hDB, hkey;

   cm_get_experiment_database(&hDB, NULL);

   if (value[0] == 0) {
      /* without value, show find dialog */
      show_header(r, "Find value", "GET", "", 0);

      //end header:
      r->rsprintf("</table>");

      //find dialog:
      r->rsprintf("<table class=\"dialogTable\">");

      r->rsprintf("<tr><th colspan=2>Find string in Online Database</tr>\n");
      r->rsprintf("<tr><td>Enter substring (case insensitive)\n");

      r->rsprintf("<td><input type=\"text\" size=\"20\" maxlength=\"80\" name=\"value\">\n");
      r->rsprintf("</tr>");

      r->rsprintf("<tr><td align=center colspan=2>");
      r->rsprintf("<input type=submit name=cmd value=Find>");
      r->rsprintf("<input type=submit name=cmd value=Cancel>");
      r->rsprintf("</tr>");
      r->rsprintf("</table>");

      r->rsprintf("<input type=hidden name=cmd value=Find>");

      r->rsprintf("</div>\n"); // closing for <div id="mmain">
      r->rsprintf("</form>\n");
      r->rsprintf("</body></html>\r\n");
   } else {
      show_header(r, "Search results", "GET", "", 0);

      r->rsprintf("<table class=\"mtable\">\n");
      r->rsprintf("<tr><th colspan=2 class=\"mtableheader\">");
      r->rsprintf("Results of search for substring \"%s\"</tr>\n", value);
      r->rsprintf("<tr><th class=\"titlerow\">Key<th>Value</tr>\n");

      /* start from root */
      db_find_key(hDB, 0, "", &hkey);
      assert(hkey);

      /* scan tree, call "search_callback" for each key */
      search_data data;
      data.r = r;
      data.search_name = value;

      db_scan_tree(hDB, hkey, 0, search_callback, (void *)&data);

      r->rsprintf("</table>");
      r->rsprintf("</div>\n"); // closing for <div id="mmain">
      r->rsprintf("</form>\n");
      r->rsprintf("</body></html>\r\n");
   }
}

/*------------------------------------------------------------------*/

#define LN10 2.302585094
#define LOG2 0.301029996
#define LOG5 0.698970005

void haxis(gdImagePtr im, gdFont * font, int col, int gcol,
           int x1, int y1, int width,
           int minor, int major, int text, int label, int grid, double xmin, double xmax)
{
   double dx, int_dx, frac_dx, x_act, label_dx, major_dx, x_screen, maxwidth;
   int tick_base, major_base, label_base, n_sig1, n_sig2, xs;
   char str[80];
   double base[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000 };

   if (xmax <= xmin || width <= 0)
      return;

   /* use 5 as min tick distance */
   dx = (xmax - xmin) / (double) (width / 5);

   frac_dx = modf(log(dx) / LN10, &int_dx);
   if (frac_dx < 0) {
      frac_dx += 1;
      int_dx -= 1;
   }

   tick_base = frac_dx < LOG2 ? 1 : frac_dx < LOG5 ? 2 : 3;
   major_base = label_base = tick_base + 1;

   /* rounding up of dx, label_dx */
   dx = pow(10, int_dx) * base[tick_base];
   major_dx = pow(10, int_dx) * base[major_base];
   label_dx = major_dx;

   /* number of significant digits */
   if (xmin == 0)
      n_sig1 = 0;
   else
      n_sig1 = (int) floor(log(fabs(xmin)) / LN10) - (int) floor(log(fabs(label_dx)) / LN10) + 1;

   if (xmax == 0)
      n_sig2 = 0;
   else
      n_sig2 =
         (int) floor(log(fabs(xmax)) / LN10) - (int) floor(log(fabs(label_dx)) / LN10) + 1;

   n_sig1 = MAX(n_sig1, n_sig2);
   n_sig1 = MAX(n_sig1, 4);

   /* determination of maximal width of labels */
   sprintf(str, "%1.*lG", n_sig1, floor(xmin / dx) * dx);
   maxwidth = font->h / 2 * strlen(str);
   sprintf(str, "%1.*lG", n_sig1, floor(xmax / dx) * dx);
   maxwidth = MAX(maxwidth, font->h / 2 * strlen(str));
   sprintf(str, "%1.*lG", n_sig1, floor(xmax / dx) * dx + label_dx);
   maxwidth = MAX(maxwidth, font->h / 2 * strlen(str));

   /* increasing label_dx, if labels would overlap */
   while (maxwidth > 0.7 * label_dx / (xmax - xmin) * width) {
      label_base++;
      label_dx = pow(10, int_dx) * base[label_base];
      if (label_base % 3 == 2 && major_base % 3 == 1) {
         major_base++;
         major_dx = pow(10, int_dx) * base[major_base];
      }
   }

   x_act = floor(xmin / dx) * dx;

   gdImageLine(im, x1, y1, x1 + width, y1, col);

   do {
      x_screen = (x_act - xmin) / (xmax - xmin) * width + x1;
      xs = (int) (x_screen + 0.5);

      if (x_screen > x1 + width + 0.001)
         break;

      if (x_screen >= x1) {
         if (fabs(floor(x_act / major_dx + 0.5) - x_act / major_dx) <
             dx / major_dx / 10.0) {

            if (fabs(floor(x_act / label_dx + 0.5) - x_act / label_dx) <
                dx / label_dx / 10.0) {
               /* label tick mark */
               gdImageLine(im, xs, y1, xs, y1 + text, col);

               /* grid line */
               if (grid != 0 && xs > x1 && xs < x1 + width)
                  gdImageLine(im, xs, y1, xs, y1 + grid, col);

               /* label */
               if (label != 0) {
                  sprintf(str, "%1.*lG", n_sig1, x_act);
                  gdImageString(im, font, (int) xs - font->w * strlen(str) / 2,
                                y1 + label, str, col);
               }
            } else {
               /* major tick mark */
               gdImageLine(im, xs, y1, xs, y1 + major, col);

               /* grid line */
               if (grid != 0 && xs > x1 && xs < x1 + width)
                  gdImageLine(im, xs, y1 - 1, xs, y1 + grid, gcol);
            }

         } else
            /* minor tick mark */
            gdImageLine(im, xs, y1, xs, y1 + minor, col);

      }

      x_act += dx;

      /* supress 1.23E-17 ... */
      if (fabs(x_act) < dx / 100)
         x_act = 0;

   } while (1);
}

/*------------------------------------------------------------------*/

void sec_to_label(char *result, int sec, int base, int force_date)
{
   char mon[80];
   time_t t_sec;

   t_sec = (time_t) sec;

   struct tm tms;
   localtime_r(&t_sec, &tms);
   strcpy(mon, mname[tms.tm_mon]);
   mon[3] = 0;

   if (force_date) {
      if (base < 600)
         sprintf(result, "%02d %s %02d %02d:%02d:%02d",
                 tms.tm_mday, mon, tms.tm_year % 100, tms.tm_hour, tms.tm_min,
                 tms.tm_sec);
      else if (base < 3600 * 24)
         sprintf(result, "%02d %s %02d %02d:%02d",
                 tms.tm_mday, mon, tms.tm_year % 100, tms.tm_hour, tms.tm_min);
      else
         sprintf(result, "%02d %s %02d", tms.tm_mday, mon, tms.tm_year % 100);
   } else {
      if (base < 600)
         sprintf(result, "%02d:%02d:%02d", tms.tm_hour, tms.tm_min, tms.tm_sec);
      else if (base < 3600 * 3)
         sprintf(result, "%02d:%02d", tms.tm_hour, tms.tm_min);
      else if (base < 3600 * 24)
         sprintf(result, "%02d %s %02d %02d:%02d",
                 tms.tm_mday, mon, tms.tm_year % 100, tms.tm_hour, tms.tm_min);
      else
         sprintf(result, "%02d %s %02d", tms.tm_mday, mon, tms.tm_year % 100);
   }
}

void taxis(gdImagePtr im, gdFont * font, int col, int gcol,
           int x1, int y1, int width, int xr,
           int minor, int major, int text, int label, int grid, double xmin, double xmax)
{
   int dx, x_act, label_dx, major_dx, x_screen, maxwidth;
   int tick_base, major_base, label_base, xs, xl;
   char str[80];
   const int base[] = { 1, 5, 10, 60, 300, 600, 1800, 3600, 3600 * 6, 3600 * 12, 3600 * 24, 0 };
   time_t ltime;
   int force_date, d1, d2;
   struct tm tms;

   if (xmax <= xmin || width <= 0)
      return;

   /* force date display if xmax not today */
   ltime = ss_time();
   localtime_r(&ltime, &tms);
   d1 = tms.tm_mday;
   ltime = (time_t) xmax;
   localtime_r(&ltime, &tms);
   d2 = tms.tm_mday;
   force_date = (d1 != d2);

   /* use 5 pixel as min tick distance */
   dx = (int) ((xmax - xmin) / (double) (width / 5) + 0.5);

   for (tick_base = 0; base[tick_base]; tick_base++) {
      if (base[tick_base] > dx)
         break;
   }
   if (!base[tick_base])
      tick_base--;
   dx = base[tick_base];

   if (base[tick_base + 1])
      major_base = tick_base + 1;
   else
      major_base = tick_base;
   major_dx = base[major_base];

   if (base[major_base + 1])
      label_base = major_base + 1;
   else
      label_base = major_base;
   label_dx = base[label_base];

   do {
      sec_to_label(str, (int) (xmin + 0.5), label_dx, force_date);
      maxwidth = font->h / 2 * strlen(str);

      /* increasing label_dx, if labels would overlap */
      if (maxwidth > 0.7 * label_dx / (xmax - xmin) * width) {
         if (base[label_base + 1])
            label_dx = base[++label_base];
         else
            label_dx += 3600 * 24;
      } else
         break;
   } while (1);

   x_act =
       (int) floor((double) (xmin - ss_timezone()) / label_dx) * label_dx + ss_timezone();

   gdImageLine(im, x1, y1, x1 + width, y1, col);

   do {
      x_screen = (int) ((x_act - xmin) / (xmax - xmin) * width + x1 + 0.5);
      xs = (int) (x_screen + 0.5);

      if (x_screen > x1 + width + 0.001)
         break;

      if (x_screen >= x1) {
         if ((x_act - ss_timezone()) % major_dx == 0) {
            if ((x_act - ss_timezone()) % label_dx == 0) {
               /* label tick mark */
               gdImageLine(im, xs, y1, xs, y1 + text, col);

               /* grid line */
               if (grid != 0 && xs > x1 && xs < x1 + width)
                  gdImageLine(im, xs, y1, xs, y1 + grid, col);

               /* label */
               if (label != 0) {
                  sec_to_label(str, x_act, label_dx, force_date);

                  /* if labels at edge, shift them in */
                  xl = (int) xs - font->w * strlen(str) / 2;
                  if (xl < 0)
                     xl = 0;
                  if (xl + font->w * (int) strlen(str) > xr)
                     xl = xr - font->w * strlen(str);
                  gdImageString(im, font, xl, y1 + label, str, col);
               }
            } else {
               /* major tick mark */
               gdImageLine(im, xs, y1, xs, y1 + major, col);

               /* grid line */
               if (grid != 0 && xs > x1 && xs < x1 + width)
                  gdImageLine(im, xs, y1 - 1, xs, y1 + grid, gcol);
            }

         } else
            /* minor tick mark */
            gdImageLine(im, xs, y1, xs, y1 + minor, col);

      }

      x_act += dx;

      /* supress 1.23E-17 ... */
      if (fabs((double)x_act) < dx / 100)
         x_act = 0;

   } while (1);
}

/*------------------------------------------------------------------*/

int vaxis(gdImagePtr im, gdFont * font, int col, int gcol,
          int x1, int y1, int width,
          int minor, int major, int text, int label, int grid, double ymin, double ymax,
          BOOL logaxis)
{
   double dy, int_dy, frac_dy, y_act, label_dy, major_dy, y_screen, y_next;
   int tick_base, major_base, label_base, n_sig1, n_sig2, ys, max_width;
   int last_label_y;
   char str[80];
   const double base[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000 };

   if (ymax <= ymin || width <= 0)
      return 0;

   // data type "double" only has about 15 significant digits, if difference between ymax and ymin is less than 10 significant digits, bailout! Otherwise code below goes into infinite loop
   if (fabs(ymax - ymin) <= 1e-10)
      return 0;

   if (logaxis) {
      dy = pow(10, floor(log(ymin) / LN10));
      label_dy = dy;
      major_dy = dy * 10;
      n_sig1 = 4;
   } else {
      dy = (ymax - ymin) / (double) (width / 5);

      frac_dy = modf(log(dy) / LN10, &int_dy);
      if (frac_dy < 0) {
         frac_dy += 1;
         int_dy -= 1;
      }

      tick_base = frac_dy < LOG2 ? 1 : frac_dy < LOG5 ? 2 : 3;
      major_base = label_base = tick_base + 1;

      /* rounding up of dy, label_dy */
      dy = pow(10, int_dy) * base[tick_base];
      major_dy = pow(10, int_dy) * base[major_base];
      label_dy = major_dy;

      /* number of significant digits */
      if (ymin == 0)
         n_sig1 = 0;
      else
         n_sig1 =
             (int) floor(log(fabs(ymin)) / LN10) -
             (int) floor(log(fabs(label_dy)) / LN10) + 1;

      if (ymax == 0)
         n_sig2 = 0;
      else
         n_sig2 =
             (int) floor(log(fabs(ymax)) / LN10) -
             (int) floor(log(fabs(label_dy)) / LN10) + 1;

      n_sig1 = MAX(n_sig1, n_sig2);
      n_sig1 = MAX(n_sig1, 4);

      /* increasing label_dy, if labels would overlap */
      while (label_dy / (ymax - ymin) * width < 1.5 * font->h) {
         label_base++;
         label_dy = pow(10, int_dy) * base[label_base];
         if (label_base % 3 == 2 && major_base % 3 == 1) {
            major_base++;
            major_dy = pow(10, int_dy) * base[major_base];
         }
      }
   }

   max_width = 0;
   y_act = floor(ymin / dy) * dy;

   if (x1 != 0 || y1 != 0)
      gdImageLine(im, x1, y1, x1, y1 - width, col);

   last_label_y = y1 + 2 * font->h;

   do {
      if (logaxis)
         y_screen = y1 - (log(y_act) - log(ymin)) / (log(ymax) - log(ymin)) * width;
      else
         y_screen = y1 - (y_act - ymin) / (ymax - ymin) * width;
      ys = (int) (y_screen + 0.5);

      if (y_screen < y1 - width - 0.001)
         break;

      if (y_screen <= y1 + 0.001) {
         if (fabs(floor(y_act / major_dy + 0.5) - y_act / major_dy) <
             dy / major_dy / 10.0) {
            if (fabs(floor(y_act / label_dy + 0.5) - y_act / label_dy) <
                dy / label_dy / 10.0) {
               if (x1 != 0 || y1 != 0) {
                  /* label tick mark */
                  gdImageLine(im, x1, ys, x1 + text, ys, col);

                  /* grid line */
                  if (grid != 0 && y_screen < y1 && y_screen > y1 - width) {
                     if (grid > 0)
                        gdImageLine(im, x1 + 1, ys, x1 + grid, ys, gcol);
                     else
                        gdImageLine(im, x1 - 1, ys, x1 + grid, ys, gcol);
                  }

                  /* label */
                  if (label != 0) {
                     sprintf(str, "%1.*lG", n_sig1, y_act);
                     if (label < 0)
                        gdImageString(im, font, x1 + label - font->w * strlen(str),
                                      ys - font->h / 2, str, col);
                     else
                        gdImageString(im, font, x1 + label, ys - font->h / 2, str, col);

                     last_label_y = ys - font->h / 2;
                  }
               } else {
                  sprintf(str, "%1.*lG", n_sig1, y_act);
                  max_width = MAX(max_width, (int) (font->w * strlen(str)));
               }
            } else {
               if (x1 != 0 || y1 != 0) {
                  /* major tick mark */
                  gdImageLine(im, x1, ys, x1 + major, ys, col);

                  /* grid line */
                  if (grid != 0 && y_screen < y1 && y_screen > y1 - width)
                     gdImageLine(im, x1, ys, x1 + grid, ys, col);
               }
            }
            if (logaxis) {
               dy *= 10;
               major_dy *= 10;
               label_dy *= 10;
            }

         } else {
            if (x1 != 0 || y1 != 0) {
               /* minor tick mark */
               gdImageLine(im, x1, ys, x1 + minor, ys, col);
            }

            /* for logaxis, also put labes on minor tick marks */
            if (logaxis) {
               if (label != 0) {
                  if (x1 != 0 || y1 != 0) {
                     /* calculate position of next major label */
                     y_next = pow(10, floor(log(y_act) / LN10) + 1);
                     y_screen =
                         (int) (y1 -
                                (log(y_next) - log(ymin)) / (log(ymax) -
                                                             log(ymin)) * width + 0.5);

                     if (ys + font->h / 2 < last_label_y
                         && ys - font->h / 2 > y_screen + font->h / 2) {
                        sprintf(str, "%1.*lG", n_sig1, y_act);
                        if (label < 0)
                           gdImageString(im, font, x1 + label - font->w * strlen(str),
                                         ys - font->h / 2, str, col);
                        else
                           gdImageString(im, font, x1 + label, ys - font->h / 2, str,
                                         col);
                     }

                     last_label_y = ys - font->h / 2;
                  } else {
                     sprintf(str, "%1.*lG", n_sig1, y_act);
                     max_width = MAX(max_width, (int) (font->w * strlen(str)));
                  }
               }
            }
         }
      }

      y_act += dy;

      /* supress 1.23E-17 ... */
      if (fabs(y_act) < dy / 100)
         y_act = 0;

   } while (1);

   return max_width + abs(label);
}

/*------------------------------------------------------------------*/

int time_to_sec(const char *str)
{
   double s;

   s = atof(str);
   switch (str[strlen(str) - 1]) {
   case 'm':
   case 'M':
      s *= 60;
      break;
   case 'h':
   case 'H':
      s *= 3600;
      break;
   case 'd':
   case 'D':
      s *= 3600 * 24;
      break;
   }

   return (int) s;
}

/*------------------------------------------------------------------*/

time_t string_to_time(const char *str)
{
   time_t t = 0;
   for (; *str != 0; str++) {
      if (*str < '0')
         break;
      if (*str > '9')
         break;
      t *= 10;
      t += *str - '0';
   }
   return t;
}

/*------------------------------------------------------------------*/

std::string time_to_string(time_t t)
{
   char buf[256];
   sprintf(buf, "%.0f", (double)t);
   return buf;
}

/*------------------------------------------------------------------*/

static bool gDoSetupHistoryWatch = true;
static bool gDoReloadHistory = false;

static void history_watch_callback(HNDLE hDB, HNDLE hKey, int index, void* info)
{
   //printf("history_watch_callback %d %d %d\n", hDB, hKey, index);
   gDoReloadHistory = true;
   cm_msg(MINFO, "history_watch_callback", "History configuration may have changed, will reconnect");
}

static MidasHistoryInterface* gMh = NULL;
static HNDLE gMhkey = 0;

/*------------------------------------------------------------------*/

static MidasHistoryInterface* get_history(bool reset = false)
{
   int status;
   HNDLE hDB;

   // history reconnect requested by watch callback?

   if (gDoReloadHistory) {
      gDoReloadHistory = false;
      reset = true;
   }

   // disconnect from previous history

   if (reset && gMh) {
      gMh->hs_disconnect();
      delete gMh;
      gMh = NULL;
      gMhkey = 0;
   }

   status = cm_get_experiment_database(&hDB, NULL);
   assert(status == CM_SUCCESS);

   // setup a watch on history configuration

   if (gDoSetupHistoryWatch) {
      HNDLE hKey;
      gDoSetupHistoryWatch = false;

      status = db_find_key(hDB, 0, "/Logger/History", &hKey);
      if (status == DB_SUCCESS)
         status = db_watch(hDB, hKey, history_watch_callback, NULL);

      status = db_find_key(hDB, 0, "/History/LoggerHistoryChannel", &hKey);
      if (status == DB_SUCCESS)
         status = db_watch(hDB, hKey, history_watch_callback, NULL);
   }

   // find out if ODB settings have changed and we need to connect to a different history channel

   HNDLE hKey = 0;
   status = hs_find_reader_channel(hDB, &hKey, verbose);
   if (status != HS_SUCCESS)
      return gMh;

   //printf("mh %p, hKey %d, mhkey %d\n", mh, hKey, mhkey);

   if (gMh && hKey == gMhkey) // same channel as before
      return gMh;

   if (gMh) {
      delete gMh;
      gMh = NULL;
      gMhkey = 0;
   }

   status = hs_get_history(hDB, hKey, HS_GET_READER|HS_GET_INACTIVE, verbose, &gMh);
   if (status != HS_SUCCESS || gMh==NULL) {
      cm_msg(MERROR, "get_history", "Cannot configure history, hs_get_history() status %d", status);
      gMh = NULL;
      return NULL;
   }

   gMhkey = hKey;

   // cm_msg(MINFO, "get_history", "Reading history from channel \'%s\' type \'%s\'", mh->name, mh->type);

   return gMh;
}

/*------------------------------------------------------------------*/

#ifdef OS_WINNT
#undef DELETE
#endif

#define ALLOC(t,n) (t*)calloc(sizeof(t),(n))
#define DELETE(x) if (x) { free(x); (x)=NULL; }
#define DELETEA(x, n) if (x) { for (int i=0; i<(n); i++) { free((x)[i]); (x)[i]=NULL; }; DELETE(x); }
#define STRDUP(x) strdup(x)

struct HistoryData
{
   int nvars;
   int alloc_nvars;
   char** event_names;
   char** var_names;
   int* var_index;
   int* odb_index;
   int* status;
   int* num_entries;
   time_t** t;
   double** v;

   bool have_last_written;
   time_t* last_written;

   time_t tstart;
   time_t tend;
   time_t scale;

   void Allocate(int xnvars) {
      if (alloc_nvars > 0)
         Free();
      nvars = 0;
      alloc_nvars = xnvars;
      event_names = ALLOC(char*, alloc_nvars);
      var_names = ALLOC(char*, alloc_nvars);
      var_index = ALLOC(int, alloc_nvars);
      odb_index = ALLOC(int, alloc_nvars);
      status = ALLOC(int, alloc_nvars);
      num_entries = ALLOC(int, alloc_nvars);
      t = ALLOC(time_t*, alloc_nvars);
      v = ALLOC(double*, alloc_nvars);

      have_last_written = false;
      last_written = ALLOC(time_t, alloc_nvars);
   }

   void Free() {
      DELETEA(event_names, alloc_nvars);
      DELETEA(var_names, alloc_nvars);
      DELETE(var_index);
      DELETE(odb_index);
      DELETE(status);
      DELETE(num_entries);
      DELETEA(t, alloc_nvars);
      DELETEA(v, alloc_nvars);
      DELETE(last_written);
      nvars = 0;
      alloc_nvars = 0;
      have_last_written = false;
   }

   void Print() const {
      printf("this %p, nvars %d. tstart %d, tend %d, scale %d\n", this, nvars, (int)tstart, (int)tend, (int)scale);
      for (int i=0; i<nvars; i++) {
         printf("var[%d]: [%s/%s][%d] %d entries, status %d", i, event_names[i], var_names[i], var_index[i], num_entries[i], status[i]);
         if (status[i]==HS_SUCCESS && num_entries[i]>0 && t[i] && v[i])
            printf(", t %d:%d, v %g:%g", (int)t[i][0], (int)t[i][num_entries[i]-1], v[i][0], v[i][num_entries[i]-1]);
         printf(" last_written %d", (int)last_written[i]);
         printf("\n");
      }
   }

   HistoryData() // ctor
   {
      nvars = 0;
      alloc_nvars = 0;
      have_last_written = false;
      tstart = 0;
      tend = 0;
      scale = 0;
   }

   ~HistoryData() // dtor
   {
      if (alloc_nvars > 0)
         Free();
   }
};

#define READ_HISTORY_DATA         0x1
#define READ_HISTORY_RUNMARKER    0x2
#define READ_HISTORY_LAST_WRITTEN 0x4

struct HistVar
{
   std::string event_name;
   std::string tag_name;
   std::string formula;
   std::string colour;
   std::string label;
   bool        show_raw_value = false;
   int         order = -1;
   double factor = 1.0;
   double offset = 0;
   double voffset = 0;
};

struct HistPlot
{
   std::string timescale = "1h";
   double minimum = 0;
   double maximum = 0;
   bool zero_ylow = false;
   bool log_axis  = false;
   bool show_run_markers = true;
   bool show_values = true;
   bool show_fill   = true;
   bool show_factor = false;
   bool enable_factor = true;

   std::vector<HistVar> vars;
};

static void LoadHistPlotFromOdb(MVOdb* odb, HistPlot* hp, const char* group, const char* panel);

int read_history(const HistPlot& hp, /*HNDLE hDB, const char *group, const char *panel,*/ int index, int flags, time_t tstart, time_t tend, time_t scale, HistoryData *data)
{
   //HNDLE hkeypanel, hkeydvar, hkey;
   //KEY key;
   //char path[256];
   //int n_vars;
   int status;
   int debug = 1;

   //strlcpy(path, group, sizeof(path));
   //strlcat(path, "/", sizeof(path));
   //strlcat(path, panel, sizeof(path));

   //printf("read_history, path %s, index %d, flags 0x%x, start %d, end %d, scale %d, data %p\n", path, index, flags, (int)tstart, (int)tend, (int)scale, data);

   /* connect to history */
   MidasHistoryInterface* mh = get_history();
   if (mh == NULL) {
      //r->rsprintf(str, "History is not configured\n");
      return HS_FILE_ERROR;
   }

#if 0
   /* check panel name in ODB */
   status = db_find_key(hDB, 0, "/History/Display", &hkey);
   if (!hkey) {
      cm_msg(MERROR, "read_history", "Cannot find \'/History/Display\' in ODB, status %d", status);
      return HS_FILE_ERROR;
   }

   /* check panel name in ODB */
   status = db_find_key(hDB, hkey, path, &hkeypanel);
   if (!hkeypanel) {
      cm_msg(MERROR, "read_history", "Cannot find \'%s\' in ODB, status %d", path, status);
      return HS_FILE_ERROR;
   }

   status = db_find_key(hDB, hkeypanel, "Variables", &hkeydvar);
   if (!hkeydvar) {
      cm_msg(MERROR, "read_history", "Cannot find \'%s/Variables\' in ODB, status %d", path, status);
      return HS_FILE_ERROR;
   }

   db_get_key(hDB, hkeydvar, &key);
   n_vars = key.num_values;
#endif

   data->Allocate(hp.vars.size()+2);

   data->tstart = tstart;
   data->tend = tend;
   data->scale = scale;

   for (size_t i=0; i<hp.vars.size(); i++) {
      if (index != -1 && (size_t)index != i)
         continue;

      //char str[256];
      //int size = sizeof(str);
      //status = db_get_data_index(hDB, hkeydvar, str, &size, i, TID_STRING);
      //if (status != DB_SUCCESS) {
      //   cm_msg(MERROR, "read_history", "Cannot read tag %d in panel %s, status %d", i, path, status);
      //   continue;
      //}

      /* split varname in event, variable and index: "event/tag[index]" */

      //char *p = strchr(str, ':');
      //if (!p)
         //   p = strchr(str, '/');
      //
      //if (!p) {
      //   cm_msg(MERROR, "read_history", "Tag \"%s\" has wrong format in panel \"%s\"", str, path);
      //   continue;
      //}

      //*p = 0;

      data->odb_index[data->nvars] = i;
      data->event_names[data->nvars] = STRDUP(hp.vars[i].event_name.c_str());
      data->var_names[data->nvars] = STRDUP(hp.vars[i].tag_name.c_str());
      data->var_index[data->nvars] = 0;

      char *q = strchr(data->var_names[data->nvars], '[');
      if (q) {
         data->var_index[data->nvars] = atoi(q+1);
         *q = 0;
      }

      data->nvars++;
   } // loop over variables

   /* write run markes if selected */
   if (flags & READ_HISTORY_RUNMARKER) {

      data->event_names[data->nvars+0] = STRDUP("Run transitions");
      data->event_names[data->nvars+1] = STRDUP("Run transitions");

      data->var_names[data->nvars+0] = STRDUP("State");
      data->var_names[data->nvars+1] = STRDUP("Run number");

      data->var_index[data->nvars+0] = 0;
      data->var_index[data->nvars+1] = 0;

      data->odb_index[data->nvars+0] = -1;
      data->odb_index[data->nvars+1] = -2;

      data->nvars += 2;
   }

   bool get_last_written = false;

   if (flags & READ_HISTORY_DATA) {
      status = mh->hs_read(tstart, tend, scale,
                           data->nvars,
                           data->event_names,
                           data->var_names,
                           data->var_index,
                           data->num_entries,
                           data->t,
                           data->v,
                           data->status);

      if (debug) {
         printf("read_history: nvars %d, hs_read() status %d\n", data->nvars, status);
         for (int i=0; i<data->nvars; i++) {
            printf("read_history: %d: event [%s], var [%s], index %d, odb index %d, status %d, num_entries %d\n", i, data->event_names[i], data->var_names[i], data->var_index[i], data->odb_index[i], data->status[i], data->num_entries[i]);
         }
      }

      if (status != HS_SUCCESS) {
         cm_msg(MERROR, "read_history", "Complete history failure, hs_read() status %d, see messages", status);
         return HS_FILE_ERROR;
      }

      for (int i=0; i<data->nvars; i++) {
         if (data->status[i] != HS_SUCCESS || data->num_entries[i] < 1) {
            get_last_written = true;
            break;
         }
      }
   }

   if (flags & READ_HISTORY_LAST_WRITTEN)
      get_last_written = true;

   if (get_last_written) {
      data->have_last_written = true;

      status = mh->hs_get_last_written(
                           tstart,
                           data->nvars,
                           data->event_names,
                           data->var_names,
                           data->var_index,
                           data->last_written);

      if (status != HS_SUCCESS) {
         data->have_last_written = false;
      }
   }

   return SUCCESS;
}

int get_hist_last_written(MVOdb* odb, const char *group, const char *panel, time_t endtime, int index, int want_all, time_t *plastwritten)
{
   //HNDLE hDB;
   int status;

   time_t now = ss_time();

   if (endtime == 0)
      endtime = now;

   HistoryData  hsxxx;
   HistoryData* hsdata = &hsxxx;

   //cm_get_experiment_database(&hDB, NULL);

   HistPlot hp;
   LoadHistPlotFromOdb(odb, &hp, group, panel);

   double tstart = ss_millitime();

   int flags = READ_HISTORY_LAST_WRITTEN;

   status = read_history(hp, /*hDB, group, panel,*/ index, flags, endtime, endtime, 0, hsdata);

   if (status != HS_SUCCESS) {
      //sprintf(str, "Complete history failure, read_history() status %d, see messages", status);
      return status;
   }

   if (!hsdata->have_last_written) {
      //sprintf(str, "Complete history failure, read_history() status %d, see messages", status);
      return HS_FILE_ERROR;
   }

   int count = 0;
   time_t tmin = endtime;
   time_t tmax = 0;

   for (int k=0; k<hsdata->nvars; k++) {
      int i = hsdata->odb_index[k];

      if (i<0)
         continue;
      if (index != -1 && index != i)
         continue;

      time_t lw = hsdata->last_written[k];

      if (lw==0) // no last_written for this variable, skip it.
         continue;

      if (lw > endtime)
         lw = endtime; // just in case hs_get_last_written() returns dates in the "future" for this plot

      if (lw > tmax)
         tmax = lw;

      if (lw < tmin)
         tmin = lw;

      count++;

      //printf("odb index %d, last_written[%d] = %.0f, tmin %.0f, tmax %.0f, endtime %.0f\n", i, k, (double)lw, (double)tmin, (double)tmax, (double)endtime);
   }

   if (count == 0) // all variables have no last_written
      return HS_FILE_ERROR;

   if (want_all)
      *plastwritten = tmin; // all variables have data
   else
      *plastwritten = tmax; // at least one variable has data

   //printf("tmin %.0f, tmax %.0f, endtime %.0f, last written %.0f\n", (double)tmin, (double)tmax, (double)endtime, (double)*plastwritten);

   double tend = ss_millitime();

   if (/* DISABLES CODE */ (0))
      printf("get_hist_last_written: elapsed time %f ms\n", tend-tstart);

   return HS_SUCCESS;
}

void generate_hist_graph(MVOdb* odb, Return* rr, const char *hgroup, const char *hpanel, char *buffer, int *buffer_size,
                         int width, int height,
                         time_t xendtime,
                         int scale,
                         int index,
                         int labels, const char *bgcolor, const char *fgcolor, const char *gridcolor)
{
   HNDLE hDB;
   //KEY key;
   gdImagePtr im;
   gdGifBuffer gb;
   int i, j, k, l;
   //int n_vars;
   int size, status, r, g, b;
   //int x_marker;
   int length;
   int white, grey, red;
   //int black, ltgrey, green, blue;
   int fgcol, bgcol, gridcol;
   int curve_col[MAX_VARS];
   int state_col[3];
   char str[256], *p;
   //INT var_index[MAX_VARS];
   //char event_name[MAX_VARS][NAME_LENGTH];
   //char tag_name[MAX_VARS][64];
   //char var_name[MAX_VARS][NAME_LENGTH];
   //char varname[64];
   //char key_name[256];
   //float factor[MAX_VARS], offset[MAX_VARS];
   //BOOL logaxis, runmarker;
   //double xmin, xrange;
   double ymin, ymax;
   double upper_limit[MAX_VARS], lower_limit[MAX_VARS];
   //float minvalue = (float) -HUGE_VAL;
   //float maxvalue = (float) +HUGE_VAL;
   //int show_values = 0;
   //int sort_vars = 0;
   //int old_vars = 0;
   time_t starttime, endtime;
   int flags;

   time_t now = ss_time();

   if (xendtime == 0)
      xendtime = now;

   HistPlot hp;
   LoadHistPlotFromOdb(odb, &hp, hgroup, hpanel);

   std::vector<int> var_index; var_index.resize(hp.vars.size());

   for (size_t i=0; i<hp.vars.size(); i++) {
      var_index[i] = 0;
      const char *vp = strchr(hp.vars[i].tag_name.c_str(), '[');
      if (vp) {
         var_index[i] = atoi(vp + 1);
      }
   }

   int logaxis = hp.log_axis;
   double minvalue = hp.minimum;
   double maxvalue = hp.maximum;
   
   if ((minvalue == 0) && (maxvalue == 0)) {
      minvalue = -HUGE_VAL;
      maxvalue = +HUGE_VAL;
   }

   std::vector<int> x[MAX_VARS];
   std::vector<double> y[MAX_VARS];

   HistoryData  hsxxx;
   HistoryData* hsdata = &hsxxx;

   cm_get_experiment_database(&hDB, NULL);

   /* generate image */
   im = gdImageCreate(width, height);

   /* allocate standard colors */
   sscanf(bgcolor, "%02x%02x%02x", &r, &g, &b);
   bgcol = gdImageColorAllocate(im, r, g, b);
   sscanf(fgcolor, "%02x%02x%02x", &r, &g, &b);
   fgcol = gdImageColorAllocate(im, r, g, b);
   sscanf(gridcolor, "%02x%02x%02x", &r, &g, &b);
   gridcol = gdImageColorAllocate(im, r, g, b);

   grey = gdImageColorAllocate(im, 192, 192, 192);
   //ltgrey = gdImageColorAllocate(im, 208, 208, 208);
   white = gdImageColorAllocate(im, 255, 255, 255);
   //black = gdImageColorAllocate(im, 0, 0, 0);
   red = gdImageColorAllocate(im, 255, 0, 0);
   //green = gdImageColorAllocate(im, 0, 255, 0);
   //blue = gdImageColorAllocate(im, 0, 0, 255);

   curve_col[0] = gdImageColorAllocate(im, 0, 0, 255);
   curve_col[1] = gdImageColorAllocate(im, 0, 192, 0);
   curve_col[2] = gdImageColorAllocate(im, 255, 0, 0);
   curve_col[3] = gdImageColorAllocate(im, 0, 192, 192);
   curve_col[4] = gdImageColorAllocate(im, 255, 0, 255);
   curve_col[5] = gdImageColorAllocate(im, 192, 192, 0);
   curve_col[6] = gdImageColorAllocate(im, 128, 128, 128);
   curve_col[7] = gdImageColorAllocate(im, 128, 255, 128);
   curve_col[8] = gdImageColorAllocate(im, 255, 128, 128);
   curve_col[9] = gdImageColorAllocate(im, 128, 128, 255);
   for (i=10; i<MAX_VARS; i++)
      curve_col[i] = gdImageColorAllocate(im, 128, 128, 128);

   state_col[0] = gdImageColorAllocate(im, 255, 0, 0);
   state_col[1] = gdImageColorAllocate(im, 255, 255, 0);
   state_col[2] = gdImageColorAllocate(im, 0, 255, 0);

   /* Set transparent color. */
   gdImageColorTransparent(im, grey);

   /* Title */
   gdImageString(im, gdFontGiant, width / 2 - (strlen(hpanel) * gdFontGiant->w) / 2, 2, (char*)hpanel, fgcol);

   /* connect to history */
   MidasHistoryInterface *mh = get_history();
   if (mh == NULL) {
      sprintf(str, "History is not configured, see messages");
      gdImageString(im, gdFontSmall, width / 2 - (strlen(str) * gdFontSmall->w) / 2, height / 2, str, red);
      goto error;
   }

   ///* check panel name in ODB */
   //sprintf(str, "/History/Display/%s/%s", hgroup, hpanel);
   //db_find_key(hDB, 0, str, &hkeypanel);
   //if (!hkeypanel) {
   //   sprintf(str, "Cannot find /History/Display/%s/%s in ODB", hgroup, hpanel);
   //   gdImageString(im, gdFontSmall, width / 2 - (strlen(str) * gdFontSmall->w) / 2, height / 2, str, red);
   //   goto error;
   //}

   //db_find_key(hDB, hkeypanel, "Variables", &hkeydvar);
   //if (!hkeydvar) {
   //   sprintf(str, "Cannot find /History/Display/%s/%s/Variables in ODB", hgroup, hpanel);
   //   gdImageString(im, gdFontSmall, width / 2 - (strlen(str) * gdFontSmall->w) / 2, height / 2, str, red);
   //   goto error;
   //}

   //db_get_key(hDB, hkeydvar, &key);
   //n_vars = key.num_values;

   if (hp.vars.empty()) {
      sprintf(str, "No variables in panel %s/%s", hgroup, hpanel);
      gdImageString(im, gdFontSmall, width / 2 - (strlen(str) * gdFontSmall->w) / 2, height / 2, str, red);
      goto error;
   }

   if (hp.vars.size() > MAX_VARS) {
      sprintf(str, "Too many variables in panel %s/%s", hgroup, hpanel);
      gdImageString(im, gdFontSmall, width / 2 - (strlen(str) * gdFontSmall->w) / 2, height / 2, str, red);
      goto error;
   }

   ymin = ymax = 0;
   //logaxis = runmarker = 0;

   for (i = 0; i < (int)hp.vars.size(); i++) {
      if (index != -1 && index != i)
         continue;

      //size = sizeof(str);
      //status = db_get_data_index(hDB, hkeydvar, str, &size, i, TID_STRING);
      //if (status != DB_SUCCESS) {
      //   sprintf(str, "Cannot read tag %d in panel %s/%s, status %d", i, hgroup, hpanel, status);
      //   gdImageString(im, gdFontSmall, width / 2 - (strlen(str) * gdFontSmall->w) / 2, height / 2, str, red);
      //   goto error;
      //}
      //
      //strlcpy(tag_name[i], str, sizeof(tag_name[0]));

      ///* split varname in event, variable and index */
      //char *tp = strchr(tag_name[i], ':');
      //if (tp) {
      //   strlcpy(event_name[i], tag_name[i], sizeof(event_name[0]));
      //   char *ep = strchr(event_name[i], ':');
      //   if (ep)
      //      *ep = 0;
      //   strlcpy(var_name[i], tp+1, sizeof(var_name[0]));
      //   var_index[i] = 0;
      //   char *vp = strchr(var_name[i], '[');
      //   if (vp) {
      //      var_index[i] = atoi(vp + 1);
      //      *vp = 0;
      //  }
      //} else {
      //   sprintf(str, "Tag \"%s\" has wrong format in panel \"%s/%s\"", tag_name[i], hgroup, hpanel);
      //   gdImageString(im, gdFontSmall, width / 2 - (strlen(str) * gdFontSmall->w) / 2, height / 2, str, red);
      //   goto error;
      //}
      //
      //db_find_key(hDB, hkeypanel, "Colour", &hkey);
      //if (hkey) {
      //   size = sizeof(str);
      //   status = db_get_data_index(hDB, hkey, str, &size, i, TID_STRING);
      //   if (status == DB_SUCCESS) {
      //      if (str[0] == '#') {
      //         char sss[3];
      //         int r, g, b;
      //
      //         sss[0] = str[1];
      //         sss[1] = str[2];
      //         sss[2] = 0;
      //         r = strtoul(sss, NULL, 16);
      //         sss[0] = str[3];
      //         sss[1] = str[4];
      //         sss[2] = 0;
      //         g = strtoul(sss, NULL, 16);
      //         sss[0] = str[5];
      //         sss[1] = str[6];
      //         sss[2] = 0;
      //         b = strtoul(sss, NULL, 16);
      //
      //         curve_col[i] = gdImageColorAllocate(im, r, g, b);
      //	    }
      //	 }
      //}

      if (hp.vars[i].colour[0] == '#') {
         const char* str = hp.vars[i].colour.c_str();
         char sss[3];
         int r, g, b;
         
         sss[0] = str[1];
         sss[1] = str[2];
         sss[2] = 0;
         r = strtoul(sss, NULL, 16);
         sss[0] = str[3];
         sss[1] = str[4];
         sss[2] = 0;
         g = strtoul(sss, NULL, 16);
         sss[0] = str[5];
         sss[1] = str[6];
         sss[2] = 0;
         b = strtoul(sss, NULL, 16);
         
         curve_col[i] = gdImageColorAllocate(im, r, g, b);
      }

      /* get timescale */
      if (scale == 0) {
         //std::string ts = "1h";
         //status = db_get_value_string(hDB, hkeypanel, "Timescale", 0, &ts, TRUE);
         //if (status != DB_SUCCESS) {
         //   /* delete old integer key */
         //   db_find_key(hDB, hkeypanel, "Timescale", &hkey);
         //   if (hkey)
         //      db_delete_key(hDB, hkey, FALSE);
         //
         //   ts = "1h";
         //   status = db_get_value_string(hDB, hkeypanel, "Timescale", 0, &ts, TRUE);
         //}

         scale = time_to_sec(hp.timescale.c_str());
      }

      //for (j = 0; j < MAX_VARS; j++) {
      //   factor[j] = 1;
      //   offset[j] = 0;
      //}

      ///* get factors */
      //size = sizeof(float) * n_vars;
      //db_get_value(hDB, hkeypanel, "Factor", factor, &size, TID_FLOAT, TRUE);

      ///* get offsets */
      //size = sizeof(float) * n_vars;
      //db_get_value(hDB, hkeypanel, "Offset", offset, &size, TID_FLOAT, TRUE);

      ///* get axis type */
      //size = sizeof(logaxis);
      //logaxis = 0;
      //db_get_value(hDB, hkeypanel, "Log axis", &logaxis, &size, TID_BOOL, TRUE);

      ///* get show_values type */
      //size = sizeof(show_values);
      //show_values = 0;
      //db_get_value(hDB, hkeypanel, "Show values", &show_values, &size, TID_BOOL, TRUE);

      ///* get sort_vars type */
      //size = sizeof(sort_vars);
      //sort_vars = 0;
      //db_get_value(hDB, hkeypanel, "Sort vars", &sort_vars, &size, TID_BOOL, TRUE);

      ///* get old_vars type */
      //size = sizeof(old_vars);
      //old_vars = 0;
      //db_get_value(hDB, hkeypanel, "Show old vars", &old_vars, &size, TID_BOOL, TRUE);

      ///* get min value */
      //size = sizeof(minvalue);
      //minvalue = (float) -HUGE_VAL;
      //db_get_value(hDB, hkeypanel, "Minimum", &minvalue, &size, TID_FLOAT, TRUE);

      ///* get max value */
      //size = sizeof(maxvalue);
      //maxvalue = (float) +HUGE_VAL;
      //db_get_value(hDB, hkeypanel, "Maximum", &maxvalue, &size, TID_FLOAT, TRUE);

      //if ((minvalue == 0) && (maxvalue == 0)) {
      //   minvalue = (float) -HUGE_VAL;
      //   maxvalue = (float) +HUGE_VAL;
      //}

      ///* get runmarker flag */
      //size = sizeof(runmarker);
      //runmarker = 1;
      //db_get_value(hDB, hkeypanel, "Show run markers", &runmarker, &size, TID_BOOL, TRUE);

      /* make ODB path from tag name */
      std::string odbpath;
      HNDLE hkeyeq = 0;
      HNDLE hkeyroot;
      db_find_key(hDB, 0, "/Equipment", &hkeyroot);
      if (hkeyroot) {
         for (j = 0;; j++) {
            HNDLE hkeyeq;
            db_enum_key(hDB, hkeyroot, j, &hkeyeq);

            if (!hkeyeq)
               break;

            KEY key;
            db_get_key(hDB, hkeyeq, &key);
            if (equal_ustring(key.name, hp.vars[i].event_name.c_str())) {
               /* check if variable is individual key under variables/ */
               sprintf(str, "Variables/%s", hp.vars[i].tag_name.c_str());
               HNDLE hkey;
               db_find_key(hDB, hkeyeq, str, &hkey);
               if (hkey) {
                  //sprintf(odbpath, "/Equipment/%s/Variables/%s", event_name[i], var_name[i]);
                  odbpath = "";
                  odbpath += "/Equipment/";
                  odbpath += hp.vars[i].event_name;
                  odbpath += "/Variables/";
                  odbpath += hp.vars[i].tag_name;
                  break;
               }

               /* check if variable is in setttins/names array */
               HNDLE hkeynames;
               db_find_key(hDB, hkeyeq, "Settings/Names", &hkeynames);
               if (hkeynames) {
                  /* extract variable name and Variables/<key> */
                  strlcpy(str, hp.vars[i].tag_name.c_str(), sizeof(str));
                  p = str + strlen(str) - 1;
                  while (p > str && *p != ' ')
                     p--;
                  std::string key_name = p + 1;
                  *p = 0;

                  std::string varname = str;

                  /* find key in single name array */
                  db_get_key(hDB, hkeynames, &key);
                  for (k = 0; k < key.num_values; k++) {
                     size = sizeof(str);
                     db_get_data_index(hDB, hkeynames, str, &size, k, TID_STRING);
                     if (equal_ustring(str, varname.c_str())) {
                        //sprintf(odbpath, "/Equipment/%s/Variables/%s[%d]", event_name[i], key_name, k);
                        odbpath = "";
                        odbpath += "/Equipment/";
                        odbpath += hp.vars[i].event_name;
                        odbpath += "/Variables/";
                        odbpath += key_name;
                        odbpath += "[";
                        odbpath += toString(k);
                        odbpath += "]";
                        break;
                     }
                  }
               } else {
                  /* go through /variables/<name> entries */
                  HNDLE hkeyvars;
                  db_find_key(hDB, hkeyeq, "Variables", &hkeyvars);
                  if (hkeyvars) {
                     for (k = 0;; k++) {
                        db_enum_key(hDB, hkeyvars, k, &hkey);

                        if (!hkey)
                           break;

                        /* find "settins/names <key>" for this key */
                        db_get_key(hDB, hkey, &key);

                        /* find key in key_name array */
                        std::string key_name = key.name;

                        std::string path;
                        //sprintf(str, "Settings/Names %s", key_name);
                        path += "Settings/Names ";
                        path += key_name;

                        HNDLE hkeynames;
                        db_find_key(hDB, hkeyeq, path.c_str(), &hkeynames);
                        if (hkeynames) {
                           db_get_key(hDB, hkeynames, &key);
                           for (l = 0; l < key.num_values; l++) {
                              size = sizeof(str);
                              db_get_data_index(hDB, hkeynames, str, &size, l, TID_STRING);
                              if (equal_ustring(str, hp.vars[i].tag_name.c_str())) {
                                 //sprintf(odbpath, "/Equipment/%s/Variables/%s[%d]", event_name[i], key_name, l);
                                 odbpath = "";
                                 odbpath += "/Equipment/";
                                 odbpath += hp.vars[i].event_name;
                                 odbpath += "/Variables/";
                                 odbpath += key_name;
                                 odbpath += "[";
                                 odbpath += toString(l);
                                 odbpath += "]";
                                 break;
                              }
                           }
                        }
                     }
                  }
               }

               break;
            }
         }

         if (!hkeyeq) {
            db_find_key(hDB, 0, "/History/Links", &hkeyroot);
            if (hkeyroot) {
               for (j = 0;; j++) {
                  HNDLE hkey;
                  db_enum_link(hDB, hkeyroot, j, &hkey);

                  if (!hkey)
                     break;

                  KEY key;
                  db_get_key(hDB, hkey, &key);
                  if (equal_ustring(key.name, hp.vars[i].event_name.c_str())) {
                     db_enum_key(hDB, hkeyroot, j, &hkey);
                     db_find_key(hDB, hkey, hp.vars[i].tag_name.c_str(), &hkey);
                     if (hkey) {
                        db_get_key(hDB, hkey, &key);
                        char xodbpath[MAX_ODB_PATH];
                        db_get_path(hDB, hkey, xodbpath, sizeof(xodbpath));
                        odbpath = xodbpath;
                        if (key.num_values > 1) {
                           odbpath += "[";
                           odbpath += toString(var_index[i]);
                           odbpath += "]";
                        }
                        break;
                     }
                  }
               }
            }
         }
      }

      /* search alarm limits */
      upper_limit[i] = lower_limit[i] = -12345;
      db_find_key(hDB, 0, "Alarms/Alarms", &hkeyroot);
      if (odbpath.length() > 0 && hkeyroot) {
         for (j = 0;; j++) {
            HNDLE hkey;
            db_enum_key(hDB, hkeyroot, j, &hkey);

            if (!hkey)
               break;

            size = sizeof(str);
            db_get_value(hDB, hkey, "Condition", str, &size, TID_STRING, TRUE);

            if (strstr(str, odbpath.c_str())) {
               if (strchr(str, '<')) {
                  p = strchr(str, '<') + 1;
                  if (*p == '=')
                     p++;
                  if (hp.enable_factor) {
                     lower_limit[i] = (hp.vars[i].factor * (atof(p) - hp.vars[i].voffset) + hp.vars[i].offset);
                  } else {
                     lower_limit[i] = atof(p);
                  }
               }
               if (strchr(str, '>')) {
                  p = strchr(str, '>') + 1;
                  if (*p == '=')
                     p++;
                  if (hp.enable_factor) {
                     upper_limit[i] = (hp.vars[i].factor * (atof(p) - hp.vars[i].voffset) + hp.vars[i].offset);
                  } else {
                     upper_limit[i] = atof(p);
                  }
               }
            }
         }
      }
   } // loop over variables

   //starttime = now - scale + toffset;
   //endtime = now + toffset;

   starttime = xendtime - scale;
   endtime = xendtime;

   //printf("now %d, scale %d, xendtime %d, starttime %d, endtime %d\n", now, scale, xendtime, starttime, endtime);

   flags = READ_HISTORY_DATA;
   if (hp.show_run_markers)
      flags |= READ_HISTORY_RUNMARKER;

   status = read_history(hp, /*hDB, hgroup, hpanel,*/ index, flags, starttime, endtime, scale/1000+1, hsdata);

   if (status != HS_SUCCESS) {
      sprintf(str, "Complete history failure, read_history() status %d, see messages", status);
      gdImageString(im, gdFontSmall, width / 2 - (strlen(str) * gdFontSmall->w) / 2, height / 2, str, red);
      goto error;
   }

   DWORD n_point[MAX_VARS];
   char var_status[MAX_VARS][256];

   for (int k=0; k<hsdata->nvars; k++) {
      int i = hsdata->odb_index[k];

      if (i<0)
         continue;

      if (index != -1 && index != i)
         continue;

      n_point[i] = 0;

      var_status[i][0] = 0;
      if (hsdata->status[k] == HS_UNDEFINED_VAR) {
         sprintf(var_status[i], "not found in history");
         continue;
      } else if (hsdata->status[k] != HS_SUCCESS) {
         sprintf(var_status[i], "hs_read() error %d, see messages", hsdata->status[k]);
         continue;
      }

      int n_vp = 0;
      for (int j=0; j<hsdata->num_entries[k]; j++) {
         int xx = (int)(hsdata->t[k][j]);
         double yy = hsdata->v[k][j];

         /* skip NaNs */
         if (ss_isnan(yy))
            continue;

         /* skip INFs */
         if (!ss_isfin(yy))
            continue;

         /* avoid overflow */
         if (yy > 1E30)
            yy = 1E30f;

         /* apply factor and offset */
         if (hp.enable_factor) {
            yy = hp.vars[i].factor * (yy - hp.vars[i].voffset) + hp.vars[i].offset;
         }

         /* calculate ymin and ymax */
         if ((i == 0 || index != -1) && n_vp == 0)
            ymin = ymax = yy;
         else {
            if (yy > ymax)
               ymax = yy;
            if (yy < ymin)
               ymin = yy;
         }

         /* increment number of valid points */

         x[i].push_back(xx);
         y[i].push_back(yy);

         n_vp++;

      } // loop over data

      n_point[i] = n_vp;

      assert(x[i].size() == y[i].size());
   }

   //int flag;
   int xmaxm;
   int row;
   int xold;
   int yold;
   int aoffset;
   double yb1, yb2, yf1, yf2;
   int xs, ys;
   int x1, x2;
   int y1, y2;
   int xs_old;
   double ybase;

   gdPoint poly[3];

   if (ymin < minvalue)
      ymin = minvalue;

   if (ymax > maxvalue)
      ymax = maxvalue;

   /* check if ylow = 0 */
   if (index == -1) {
      //flag = 0;
      //size = sizeof(flag);
      //db_get_value(hDB, hkeypanel, "Zero ylow", &flag, &size, TID_BOOL, TRUE);
      if (hp.zero_ylow && ymin > 0)
         ymin = 0;
   }

   /* if min and max too close together, switch to linear axis */
   if (logaxis && ymin > 0 && ymax > 0) {
      yb1 = pow(10, floor(log(ymin) / LN10));
      yf1 = floor(ymin / yb1);
      yb2 = pow(10, floor(log(ymax) / LN10));
      yf2 = floor(ymax / yb2);

      if (yb1 == yb2 && yf1 == yf2)
         logaxis = 0;
      else {
         /* round down and up ymin and ymax */
         ybase = pow(10, floor(log(ymin) / LN10));
         ymin = (floor(ymin / ybase) * ybase);
         ybase = pow(10, floor(log(ymax) / LN10));
         ymax = ((floor(ymax / ybase) + 1) * ybase);
      }
   }

   /* avoid negative limits for log axis */
   if (logaxis) {
      if (ymax <= 0)
         ymax = 1;
      if (ymin <= 0)
         ymin = 1E-12f;
   }

   /* increase limits by 5% */
   if (ymin == 0 && ymax == 0) {
      ymin = -1;
      ymax = 1;
   } else {
      if (!logaxis) {
         ymax += (ymax - ymin) / 20.f;

         if (ymin != 0)
            ymin -= (ymax - ymin) / 20.f;
      }
   }

   /* avoid ymin == ymax */
   if (ymax == ymin) {
      if (logaxis) {
         ymax *= 2;
         ymin /= 2;
      } else {
         ymax += 10;
         ymin -= 10;
      }
   }

   /* calculate X limits */
   //xmin = (double) (-scale / 3600.0 + toffset / 3600.0);
   //xmax = (double) (toffset / 3600.0);
   //xrange = xmax - xmin;
   //xrange = scale/3600.0;

   /* caluclate required space for Y-axis */
   aoffset = vaxis(im, gdFontSmall, fgcol, gridcol, 0, 0, height, -3, -5, -7, -8, 0, ymin, ymax, logaxis);
   aoffset += 2;

   x1 = aoffset;
   y1 = height - 20;
   x2 = width - 20;
   y2 = 20;

   gdImageFilledRectangle(im, x1, y2, x2, y1, bgcol);

   /* draw axis frame */
   taxis(im, gdFontSmall, fgcol, gridcol, x1, y1, x2 - x1, width, 3, 5, 9, 10, 0, (double)starttime, (double)endtime);

   vaxis(im, gdFontSmall, fgcol, gridcol, x1, y1, y1 - y2, -3, -5, -7, -8, x2 - x1, ymin, ymax, logaxis);
   gdImageLine(im, x1, y2, x2, y2, fgcol);
   gdImageLine(im, x2, y2, x2, y1, fgcol);

   xs = ys = xold = yold = 0;

   /* old code for run markers, new code is below */

   /* write run markes if selected */
   if (/* DISABLES CODE */ (0) && hp.show_run_markers) {

      const char* event_names[] = {
         "Run transitions",
         "Run transitions",
         0 };

      const char* tag_names[] = {
         "State",
         "Run number",
         0 };

      const int tag_indexes[] = {
         0,
         0,
         0 };

      int num_entries[3];
      time_t *tbuf[3];
      double *dbuf[3];
      int     st[3];

      num_entries[0] = 0;
      num_entries[1] = 0;

      status = mh->hs_read(starttime - scale, endtime, 0,
                           2, event_names, tag_names, tag_indexes,
                           num_entries, tbuf, dbuf, st);

      //printf("read run info: status %d, entries %d %d\n", status, num_entries[0], num_entries[1]);

      int n_marker = num_entries[0];

      if (status == HS_SUCCESS && n_marker > 0 && n_marker < 100) {
         xs_old = -1;
         xmaxm = x1;
         for (j = 0; j < (int) n_marker; j++) {
            int col;

            // explicit algebra manipulation to clarify computations:

            //xmin = (double) (-scale / 3600.0 + toffset / 3600.0);
            //xrange = scale/3600.0;
            //time_t starttime = now - scale + toffset;

            //x_marker = (int)(tbuf[1][j] - now);
            //xs = (int) ((x_marker / 3600.0 - xmin) / xrange * (x2 - x1) + x1 + 0.5);
            //xs = (int) (((tbuf[1][j] - now) / 3600.0 - xmin) / xrange * (x2 - x1) + x1 + 0.5);
            //xs = (int) (((tbuf[1][j] - now) / 3600.0 - (-scale / 3600.0 + toffset / 3600.0)) / (scale/3600.0) * (x2 - x1) + x1 + 0.5);
            //xs = (int) (((tbuf[1][j] - now) - (-scale + toffset)) / (scale/1.0) * (x2 - x1) + x1 + 0.5);
            xs = (int) ((tbuf[1][j] - starttime) / (scale/1.0) * (x2 - x1) + x1 + 0.5);

            if (xs < x1)
               continue;
            if (xs >= x2)
               continue;

            double run_number = dbuf[1][j];

            if (xs <= xs_old)
               xs = xs_old + 1;
            xs_old = xs;

            if (dbuf[0][j] == 1)
               col = state_col[0];
            else if (dbuf[0][j] == 2)
               col = state_col[1];
            else if (dbuf[0][j] == 3)
               col = state_col[2];
            else
               col = state_col[0];

            gdImageDashedLine(im, xs, y1, xs, y2, col);

            sprintf(str, "%.0f", run_number);

            if (dbuf[0][j] == STATE_RUNNING) {
               if (xs > xmaxm) {
                  gdImageStringUp(im, gdFontSmall, xs + 0, y2 + 2 + gdFontSmall->w * strlen(str), str, fgcol);
                  xmaxm = xs - 2 + gdFontSmall->h;
               }
            } else if (dbuf[0][j] == STATE_STOPPED) {
               if (xs + 2 - gdFontSmall->h > xmaxm) {
                  gdImageStringUp(im, gdFontSmall, xs + 2 - gdFontSmall->h, y2 + 2 + gdFontSmall->w * strlen(str), str, fgcol);
                  xmaxm = xs - 1;
               }
            }
         }
      }

      if (num_entries[0]) {
         free(tbuf[0]);
         free(dbuf[0]);
         tbuf[0] = NULL;
         dbuf[0] = NULL;
      }

      if (num_entries[1]) {
         free(tbuf[1]);
         free(dbuf[1]);
         tbuf[1] = NULL;
         dbuf[1] = NULL;
      }
   }

   /* write run markes if selected */
   if (hp.show_run_markers) {

      int index_state = -1;
      int index_run_number = -1;

      for (int k=0; k<hsdata->nvars; k++) {
         if (hsdata->odb_index[k] == -1)
            index_state = k;

         if (hsdata->odb_index[k] == -2)
            index_run_number = k;
      }

      bool ok = true;

      if (ok)
         ok = (index_state >= 0) && (index_run_number >= 0);

      if (ok)
         ok = (hsdata->status[index_state] == HS_SUCCESS);

      if (ok)
         ok = (hsdata->status[index_run_number] == HS_SUCCESS);

      if (/* DISABLES CODE */ (0) && ok)
         printf("read run info: indexes: %d, %d, status: %d, %d, entries: %d, %d\n", index_state, index_run_number, hsdata->status[index_state], hsdata->status[index_run_number], hsdata->num_entries[index_state], hsdata->num_entries[index_run_number]);

      if (ok)
         ok = (hsdata->num_entries[index_state] == hsdata->num_entries[index_run_number]);

      int n_marker = hsdata->num_entries[index_state];

      if (ok && n_marker > 0 && n_marker < 100) {
         xs_old = -1;
         xmaxm = x1;
         for (j = 0; j < (int) n_marker; j++) {
            int col;

            // explicit algebra manipulation to clarify computations:

            //xmin = (double) (-scale / 3600.0 + toffset / 3600.0);
            //xrange = scale/3600.0;
            //time_t starttime = now - scale + toffset;

            //x_marker = (int)(tbuf[1][j] - now);
            //xs = (int) ((x_marker / 3600.0 - xmin) / xrange * (x2 - x1) + x1 + 0.5);
            //xs = (int) (((tbuf[1][j] - now) / 3600.0 - xmin) / xrange * (x2 - x1) + x1 + 0.5);
            //xs = (int) (((tbuf[1][j] - now) / 3600.0 - (-scale / 3600.0 + toffset / 3600.0)) / (scale/3600.0) * (x2 - x1) + x1 + 0.5);
            //xs = (int) (((tbuf[1][j] - now) - (-scale + toffset)) / (scale/1.0) * (x2 - x1) + x1 + 0.5);
            xs = (int) ((hsdata->t[index_state][j] - starttime) / (scale/1.0) * (x2 - x1) + x1 + 0.5);

            if (xs < x1)
               continue;
            if (xs >= x2)
               continue;

            double run_number = hsdata->v[index_run_number][j];

            if (xs <= xs_old)
               xs = xs_old + 1;
            xs_old = xs;

            int state = (int)hsdata->v[index_state][j];

            if (state == 1)
               col = state_col[0];
            else if (state == 2)
               col = state_col[1];
            else if (state == 3)
               col = state_col[2];
            else
               col = state_col[0];

            gdImageDashedLine(im, xs, y1, xs, y2, col);

            sprintf(str, "%.0f", run_number);

            if (state == STATE_RUNNING) {
               if (xs > xmaxm) {
                  gdImageStringUp(im, gdFontSmall, xs + 0, y2 + 2 + gdFontSmall->w * strlen(str), str, fgcol);
                  xmaxm = xs - 2 + gdFontSmall->h;
               }
            } else if (state == STATE_STOPPED) {
               if (xs + 2 - gdFontSmall->h > xmaxm) {
                  gdImageStringUp(im, gdFontSmall, xs + 2 - gdFontSmall->h, y2 + 2 + gdFontSmall->w * strlen(str), str, fgcol);
                  xmaxm = xs - 1;
               }
            }
         }
      }
   }

   for (i = 0; i < (int)hp.vars.size(); i++) {
      if (index != -1 && index != i)
         continue;

      /* draw alarm limits */
      if (lower_limit[i] != -12345) {
         if (logaxis) {
            if (lower_limit[i] <= 0)
               ys = y1;
            else
               ys = (int) (y1 - (log(lower_limit[i]) - log(ymin)) / (log(ymax) - log(ymin)) * (y1 - y2) + 0.5);
         } else {
            ys = (int) (y1 - (lower_limit[i] - ymin) / (ymax - ymin) * (y1 - y2) + 0.5);
         }

         if (xs < 0)
            xs = 0;
         if (xs >= width)
            xs = width-1;
         if (ys < 0)
            ys = 0;
         if (ys >= height)
            ys = height-1;

         if (ys > y2 && ys < y1) {
            gdImageDashedLine(im, x1, ys, x2, ys, curve_col[i]);

            poly[0].x = x1;
            poly[0].y = ys;
            poly[1].x = x1 + 5;
            poly[1].y = ys;
            poly[2].x = x1;
            poly[2].y = ys - 5;

            gdImageFilledPolygon(im, poly, 3, curve_col[i]);
         }
      }
      if (upper_limit[i] != -12345) {
         if (logaxis) {
            if (upper_limit[i] <= 0)
               ys = y1;
            else
               ys = (int) (y1 - (log(upper_limit[i]) - log(ymin)) / (log(ymax) - log(ymin)) * (y1 - y2) + 0.5);
         } else {
            ys = (int) (y1 - (upper_limit[i] - ymin) / (ymax - ymin) * (y1 - y2) + 0.5);
         }

         if (xs < 0)
            xs = 0;
         if (xs >= width)
            xs = width-1;
         if (ys < 0)
            ys = 0;
         if (ys >= height)
            ys = height-1;

         if (ys > y2 && ys < y1) {
            gdImageDashedLine(im, x1, ys, x2, ys, curve_col[i]);

            poly[0].x = x1;
            poly[0].y = ys;
            poly[1].x = x1 + 5;
            poly[1].y = ys;
            poly[2].x = x1;
            poly[2].y = ys + 5;

            gdImageFilledPolygon(im, poly, 3, curve_col[i]);
         }
      }

      for (j = 0; j < (int) n_point[i]; j++) {
         //xmin = (double) (-scale / 3600.0 + toffset / 3600.0);
         //xrange = scale/3600.0;
         //xs = (int) (((x[i][j]-now) / 3600.0 - xmin) / xrange * (x2 - x1) + x1 + 0.5);
         //xs = (int) (((x[i][j] - now + scale - toffset) / 3600.0) / xrange * (x2 - x1) + x1 + 0.5);
         //xs = (int) (((x[i][j] - starttime) / 3600.0) / xrange * (x2 - x1) + x1 + 0.5);
         xs = (int) (((x[i][j] - starttime)/1.0) / (1.0*scale) * (x2 - x1) + x1 + 0.5);

         if (logaxis) {
            if (y[i][j] <= 0)
               ys = y1;
            else
               ys = (int) (y1 - (log(y[i][j]) - log(ymin)) / (log(ymax) - log(ymin)) * (y1 - y2) + 0.5);
         } else {
            ys = (int) (y1 - (y[i][j] - ymin) / (ymax - ymin) * (y1 - y2) + 0.5);
         }

         if (xs < 0)
            xs = 0;
         if (xs >= width)
            xs = width-1;
         if (ys < 0)
            ys = 0;
         if (ys >= height)
            ys = height-1;

         if (j > 0)
            gdImageLine(im, xold, yold, xs, ys, curve_col[i]);
         xold = xs;
         yold = ys;
      }

      if (n_point[i] > 0) {
         poly[0].x = xs;
         poly[0].y = ys;
         poly[1].x = xs + 12;
         poly[1].y = ys - 6;
         poly[2].x = xs + 12;
         poly[2].y = ys + 6;

         gdImageFilledPolygon(im, poly, 3, curve_col[i]);
      }
   }

   if (labels) {
      for (i = 0; i < (int)hp.vars.size(); i++) {
         if (index != -1 && index != i)
            continue;

         //str[0] = 0;
         //status = db_find_key(hDB, hkeypanel, "Label", &hkeydvar);
         //if (status == DB_SUCCESS) {
         //   size = sizeof(str);
         //   status = db_get_data_index(hDB, hkeydvar, str, &size, i, TID_STRING);
         //}

         std::string str = hp.vars[i].label.c_str();

         if (str.empty()) {
            if (hp.enable_factor) {
               str = hp.vars[i].tag_name;

               if (hp.vars[i].voffset > 0)
                  str += msprintf(" - %G", hp.vars[i].voffset);
               else if (hp.vars[i].voffset < 0)
                  str += msprintf(" + %G", -hp.vars[i].voffset);

               if (hp.vars[i].factor != 1) {
                  if (hp.vars[i].voffset == 0)
                     str += msprintf(" * %+G", hp.vars[i].factor);
                  else {
                     str = msprintf("(%s) * %+G", str.c_str(), hp.vars[i].factor);
                  }
               }

               if (hp.vars[i].offset > 0)
                  str += msprintf(" + %G", hp.vars[i].offset);
               else if (hp.vars[i].offset < 0)
                  str += msprintf(" - %G", -hp.vars[i].offset);

            } else {
               str = hp.vars[i].tag_name;
            }
         }

         int k=0;
         for (int j=0; j<hsdata->nvars; j++)
            if (hsdata->odb_index[j] == i) {
               k = j;
               break;
            }

         if (/* DISABLES CODE */ (0)) {
            printf("graph %d: odb index %d, n_point %d, num_entries %d, have_last_written %d %d, status %d, var_status [%s]\n", i, k, n_point[i], hsdata->num_entries[k], hsdata->have_last_written, (int)hsdata->last_written[k], hsdata->status[k], var_status[i]);
         }

         if (hp.show_values) {
            char xstr[256];
            if (n_point[i] > 0) {
               sprintf(xstr," = %g", y[i][n_point[i]-1]);
            } else if (hsdata->num_entries[k] > 0) {
               sprintf(xstr," = all data is NaN or INF");
            } else if (hsdata->have_last_written) {
               if (hsdata->last_written[k]) {
                  char ctimebuf[32];
                  ctime_r(&hsdata->last_written[k], ctimebuf);
                  sprintf(xstr," = last data %s", ctimebuf);
                  // kill trailing '\n'
                  char*s = strchr(xstr, '\n');
                  if (s) *s=0;
                  // clear the unnecessary error status report
                  if (hsdata->status[k] == HS_UNDEFINED_VAR)
                     var_status[i][0] = 0;
               } else {
                  sprintf(xstr," = no data ever");
               }
            } else {
               sprintf(xstr," = no data");
            }
            str += xstr;
         }

         if (strlen(var_status[i]) > 1) {
            char xstr[300];
            sprintf(xstr," (%s)", var_status[i]);
            str += xstr;
         }

         row = index == -1 ? i : 0;

         gdImageFilledRectangle(im,
                                x1 + 10,
                                y2 + 10 + row * (gdFontMediumBold->h + 10),
                                x1 + 10 + str.length() * gdFontMediumBold->w + 10,
                                y2 + 10 + row * (gdFontMediumBold->h + 10) +
                                gdFontMediumBold->h + 2 + 2, white);
         gdImageRectangle(im, x1 + 10, y2 + 10 + row * (gdFontMediumBold->h + 10),
                          x1 + 10 + str.length() * gdFontMediumBold->w + 10,
                          y2 + 10 + row * (gdFontMediumBold->h + 10) +
                          gdFontMediumBold->h + 2 + 2, curve_col[i]);

         gdImageString(im, gdFontMediumBold,
                       x1 + 10 + 5, y2 + 10 + 2 + row * (gdFontMediumBold->h + 10),
                       (char*)str.c_str(),
                       curve_col[i]);
      }
   }

   gdImageRectangle(im, x1, y2, x2, y1, fgcol);

 error:

   /* generate GIF */
   gdImageInterlace(im, 1);
   gdImageGif(im, &gb);
   gdImageDestroy(im);
   length = gb.size;

   if (buffer == NULL) {
      rr->rsprintf("HTTP/1.1 200 Document follows\r\n");
      rr->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());

      rr->rsprintf("Content-Type: image/gif\r\n");
      rr->rsprintf("Content-Length: %d\r\n", length);
      rr->rsprintf("Cache-control: private, max-age=0, no-cache\r\n");
      rr->rsprintf("Expires: Fri, 01-Jan-1983 00:00:00 GMT\r\n\r\n");

      rr->rmemcpy(gb.data, length);
   } else {
      if (length > *buffer_size) {
         printf("return buffer too small\n");
         return;
      }

      memcpy(buffer, gb.data, length);
      *buffer_size = length;
   }
}

/*------------------------------------------------------------------*/

time_t mktime_with_dst(const struct tm* ptms)
{
   // this silly stuff is required to correctly handle daylight savings time (Summer time/Winter time)
   // when we fill "struct tm" from user input, we cannot know if daylight savings time is in effect
   // and we do not know how to initialize the value of tms.tm_isdst.
   // This can cause the output of mktime() to be off by one hour.
   // (Rules for daylight savings time are set by national and local govt and in some locations, changes yearly)
   // (There are no locations with 2 hour or half-hour daylight savings that I know of)
   // (Yes, "man mktime" talks about using "tms.tm_isdst = -1")
   //
   // We assume the user is using local time and we convert in two steps:
   //
   // first we convert "struct tm" to "time_t" using mktime() with unknown tm_isdst
   // second we convert "time_t" back to "struct tm" using localtime_r()
   // this fills "tm_isdst" with correct value from the system time zone database
   // then we reset all the time fields (except for sub-minute fields not affected by daylight savings)
   // and call mktime() again, now with the correct value of "tm_isdst".
   // K.O. 2013-09-14

   struct tm tms = *ptms;
   struct tm tms2;
   time_t t1 = ss_mktime(&tms);
   localtime_r(&t1, &tms2);
   tms2.tm_year = ptms->tm_year;
   tms2.tm_mon  = ptms->tm_mon;
   tms2.tm_mday = ptms->tm_mday;
   tms2.tm_hour = ptms->tm_hour;
   tms2.tm_min  = ptms->tm_min;
   time_t t2 = ss_mktime(&tms2);
   //printf("t1 %.0f, t2 %.0f, diff %d\n", (double)t1, (double)t2, (int)(t1-t2));
   return t2;
}

/*------------------------------------------------------------------*/

static std::string add_param_to_url(const char* name, const char* value)
{
   std::string s;
   s += name; // FIXME: should be URI-encoded
   s += "=";
   s += value; // FIXME: should be URI-encoded
   return s;
}

/*------------------------------------------------------------------*/

void show_query_page(Param* p, Return* r)
{
   int i;
   HNDLE hDB;

   if (p->getparam("m1") && *p->getparam("m1")) {
      struct tm tms;
      memset(&tms, 0, sizeof(struct tm));

      tms.tm_year = atoi(p->getparam("y1")) % 100;

      std::string m1 = p->getparam("m1");
      for (i = 0; i < 12; i++)
         if (equal_ustring(m1.c_str(), mname[i]))
            break;
      if (i == 12)
         i = 0;

      tms.tm_mon = i;
      tms.tm_mday = atoi(p->getparam("d1"));
      tms.tm_hour = atoi(p->getparam("h1"));

      if (tms.tm_year < 90)
         tms.tm_year += 100;

      time_t ltime_start = mktime_with_dst(&tms);

      memset(&tms, 0, sizeof(struct tm));
      tms.tm_year = atoi(p->getparam("y2")) % 100;

      std::string m2 = p->getparam("m2");
      for (i = 0; i < 12; i++)
         if (equal_ustring(m2.c_str(), mname[i]))
            break;
      if (i == 12)
         i = 0;

      tms.tm_mon = i;
      tms.tm_mday = atoi(p->getparam("d2"));
      tms.tm_hour = atoi(p->getparam("h2"));

      if (tms.tm_year < 90)
         tms.tm_year += 100;

      time_t ltime_end = mktime_with_dst(&tms);

      if (ltime_end == ltime_start)
         ltime_end += 3600 * 24;

      std::string redir;
      redir += "?cmd=oldhistory&";
      redir += add_param_to_url("group", p->getparam("group"));
      redir += "&";
      redir += add_param_to_url("panel", p->getparam("panel"));
      redir += "&";
      redir += add_param_to_url("scale", toString((int)(ltime_end - ltime_start)).c_str());
      redir += "&";
      redir += add_param_to_url("time", time_to_string(ltime_end).c_str());
      if (p->isparam("hindex")) {
         redir += "&";
         redir += add_param_to_url("index", p->getparam("hindex"));
      }
      redirect(r, redir.c_str());
      return;
   }

   cm_get_experiment_database(&hDB, NULL);
   show_header(r, "History", "GET", "", 0);

   /* set the times */

   time_t now = time(NULL);

   time_t starttime = now - 3600 * 24;
   time_t endtime = now;
   bool full_day = true;

   if (p->isparam("htime")) {
      endtime = string_to_time(p->getparam("htime"));

      if (p->isparam("hscale")) {
         starttime = endtime - atoi(p->getparam("hscale"));
         full_day = false;
      } else {
         starttime = endtime - 3600 * 24;
         full_day = false;
      }
   }

   /* menu buttons */
   r->rsprintf("<tr><td colspan=2>\n");
   r->rsprintf("<input type=hidden name=cmd value=OldHistory>\n");
   r->rsprintf("<input type=submit name=hcmd value=Query>\n");
   r->rsprintf("<input type=submit name=hcmd value=Cancel>\n");
   if (p->isparam("group"))
      r->rsprintf("<input type=hidden name=group value=\"%s\">\n", p->getparam("group"));
   if (p->isparam("panel"))
      r->rsprintf("<input type=hidden name=panel value=\"%s\">\n", p->getparam("panel"));
   if (p->isparam("htime"))
      r->rsprintf("<input type=hidden name=htime value=\"%s\">\n", p->getparam("htime"));
   if (p->isparam("hscale"))
      r->rsprintf("<input type=hidden name=hscale value=\"%s\">\n", p->getparam("hscale"));
   if (p->isparam("hindex"))
      r->rsprintf("<input type=hidden name=hindex value=\"%s\">\n", p->getparam("hindex"));
   r->rsprintf("</tr>\n\n");
   r->rsprintf("</table>");  //end header

   r->rsprintf("<table class=\"dialogTable\">");  //main table

   struct tm tms;
   localtime_r(&starttime, &tms);
   tms.tm_year += 1900;

   r->rsprintf("<tr><td nowrap>Start date:</td>");

   r->rsprintf("<td>Month: <select name=\"m1\">\n");
   r->rsprintf("<option value=\"\">\n");
   for (i = 0; i < 12; i++)
      if (i == tms.tm_mon)
         r->rsprintf("<option selected value=\"%s\">%s\n", mname[i], mname[i]);
      else
         r->rsprintf("<option value=\"%s\">%s\n", mname[i], mname[i]);
   r->rsprintf("</select>\n");

   r->rsprintf("&nbsp;Day: <select name=\"d1\">");
   r->rsprintf("<option selected value=\"\">\n");
   for (i = 0; i < 31; i++)
      if (i + 1 == tms.tm_mday)
         r->rsprintf("<option selected value=%d>%d\n", i + 1, i + 1);
      else
         r->rsprintf("<option value=%d>%d\n", i + 1, i + 1);
   r->rsprintf("</select>\n");

   int start_hour = tms.tm_hour;
   if (full_day)
      start_hour = 0;

   r->rsprintf("&nbsp;Hour: <input type=\"text\" size=5 maxlength=5 name=\"h1\" value=\"%d\">", start_hour);

   r->rsprintf("&nbsp;Year: <input type=\"text\" size=5 maxlength=5 name=\"y1\" value=\"%d\">", tms.tm_year);
   r->rsprintf("</td></tr>\n");

   r->rsprintf("<tr><td nowrap>End date:</td>");

   localtime_r(&endtime, &tms);
   tms.tm_year += 1900;

   r->rsprintf("<td>Month: <select name=\"m2\">\n");
   r->rsprintf("<option value=\"\">\n");
   for (i = 0; i < 12; i++)
      if (i == tms.tm_mon)
         r->rsprintf("<option selected value=\"%s\">%s\n", mname[i], mname[i]);
      else
         r->rsprintf("<option value=\"%s\">%s\n", mname[i], mname[i]);
   r->rsprintf("</select>\n");

   r->rsprintf("&nbsp;Day: <select name=\"d2\">");
   r->rsprintf("<option selected value=\"\">\n");
   for (i = 0; i < 31; i++)
      if (i + 1 == tms.tm_mday)
         r->rsprintf("<option selected value=%d>%d\n", i + 1, i + 1);
      else
         r->rsprintf("<option value=%d>%d\n", i + 1, i + 1);
   r->rsprintf("</select>\n");

   int end_hour = tms.tm_hour;
   if (full_day)
      end_hour = 24;

   r->rsprintf("&nbsp;Hour: <input type=\"text\" size=5 maxlength=5 name=\"h2\" value=\"%d\">", end_hour);

   r->rsprintf("&nbsp;Year: <input type=\"text\" size=5 maxlength=5 name=\"y2\" value=\"%d\">", tms.tm_year);
   r->rsprintf("</td></tr>\n");

   r->rsprintf("</table>\n");
   r->rsprintf("</div>\n"); // closing for <div id="mmain">
   r->rsprintf("</form>\n");
   r->rsprintf("</body></html>\r\n");
}

/*------------------------------------------------------------------*/
/* history plot code starts here                                    */
/*------------------------------------------------------------------*/

static int cmp_names(const void *a, const void *b)
{
  int i;
  const char*sa = (const char*)a;
  const char*sb = (const char*)b;

  int debug = 0;

  // Cannot use strcmp() because it does not know how to compare numerical values, e.g.
  // it thinks "111" is smaller than "9"
  //return strcmp(sa, sb);

  if (debug)
    printf("compare [%s] and [%s]\n", sa, sb);

  for (i=0; ; i++) {
    if (sa[i]==0 && sb[i]==0)
      return 0; // both strings have the same length and the same characters

    //printf("index %d, char [%c] [%c], isdigit %d %d\n", i, sa[i], sb[i], isdigit(sa[i]), isdigit(sb[i]));

    if (isdigit(sa[i]) && isdigit(sb[i])) {
      int va = atoi(sa+i);
      int vb = atoi(sb+i);

      if (debug)
        printf("index %d, values %d %d\n", i, va, vb);

      if (va < vb)
        return -1;
      else if (va > vb)
        return 1;

      // values are equal, skip the the end of the digits, compare any trailing text
      continue;
    }

    if (sa[i]==sb[i]) {
      continue;
    }

    if (debug)
      printf("index %d, char [%c] [%c]\n", i, sa[i], sb[i]);

    if (sa[i] == 0) // string sa is shorter
      return -1;
    else if (sb[i] == 0) // string sb is shorter
      return 1;

    if (sa[i]<sb[i])
      return -1;
    else
      return 1;
  }

  // NOT REACHED
}

const bool cmp_events(const std::string& a, const std::string& b)
{
  return cmp_names(a.c_str(), b.c_str()) < 0;
}

const bool cmp_events1(const std::string& a, const std::string& b)
{
  return a < b;
}

const bool cmp_tags(const TAG& a, const TAG& b)
{
  return cmp_names(a.name, b.name) < 0;
}

#if 0
static int cmp_tags(const void *a, const void *b)
{
  const TAG*sa = (const TAG*)a;
  const TAG*sb = (const TAG*)b;
  return cmp_names(sa->name, sb->name);
}

static void sort_tags(int ntags, TAG* tags)
{
   qsort(tags, ntags, sizeof(TAG), cmp_tags);
}
#endif

#define STRLCPY(dst, src) strlcpy((dst), (src), sizeof(dst))
#define STRLCAT(dst, src) strlcat((dst), (src), sizeof(dst))

int xdb_get_data_index(HNDLE hDB, const char* str, void *value, int size, int index, int tid)
{
   HNDLE hKey;
   int status = db_find_key(hDB, 0, str, &hKey);
   if (status != DB_SUCCESS)
      return status;

   KEY key;
   db_get_key(hDB, hKey, &key);
   if (index >= key.num_values)
      return DB_OUT_OF_RANGE;

   status = db_get_data_index(hDB, hKey, value, &size, index, tid);
   return status;
}

static int xdb_find_key(HNDLE hDB, HNDLE dir, const char* str, HNDLE* hKey, int tid, int size)
{
   int status = db_find_key(hDB, dir, str, hKey);
   if (status == DB_SUCCESS)
      return status;

   db_create_key(hDB, dir, str, tid);
   status = db_find_key(hDB, dir, str, hKey);
   if (status != DB_SUCCESS || !*hKey) {
      cm_msg(MERROR, "xdb_find_key", "Invalid ODB path \"%s\"", str);
      str = "bad_xdb_find_key";
      db_create_key(hDB, dir, str, tid);
      db_find_key(hDB, dir, str, hKey);
   }
   assert(*hKey);

   if (tid == TID_STRING) {
      db_set_data_index(hDB, *hKey, "", size, 0, TID_STRING);
   }

   return status;
}

static bool cmp_vars(const HistVar &a, const HistVar &b)
{
   return a.order < b.order;
}

static void PrintHistPlot(const HistPlot& hp)
{
   printf("hist plot: %d variables\n", (int)hp.vars.size());
   printf("timescale: %s, minimum: %f, maximum: %f, zero_ylow: %d, log_axis: %d, show_run_markers: %d, show_values: %d, show_fill: %d, show_factor %d, enable_factor: %d\n", hp.timescale.c_str(), hp.minimum, hp.maximum, hp.zero_ylow, hp.log_axis, hp.show_run_markers, hp.show_values, hp.show_fill, hp.show_factor, hp.enable_factor);
   
   for (size_t i=0; i<hp.vars.size(); i++) {
      printf("var[%d] event [%s][%s] formula [%s], colour [%s] label [%s] show_raw_value %d factor %f offset %f voffset %f order %d\n", (int)i, hp.vars[i].event_name.c_str(), hp.vars[i].tag_name.c_str(), hp.vars[i].formula.c_str(), hp.vars[i].colour.c_str(), hp.vars[i].label.c_str(), hp.vars[i].show_raw_value, hp.vars[i].factor, hp.vars[i].offset , hp.vars[i].voffset, hp.vars[i].order);
   }
}

static std::string NextHistPlotColour(const HistPlot& hp)
{
   const char* const colour[] =
      {
       "#00AAFF", "#FF9000", "#FF00A0", "#00C030",
       "#A0C0D0", "#D0A060", "#C04010", "#807060",
       "#F0C000", "#2090A0", "#D040D0", "#90B000",
       "#B0B040", "#B0B0FF", "#FFA0A0", "#A0FFA0",
       NULL };

   for (int i=0; colour[i]; i++) {
      bool in_use = false;
      
      for (size_t j=0; j<hp.vars.size(); j++)
         if (hp.vars[j].colour == colour[i]) {
            in_use = true;
            break;
         }
      
      if (!in_use)
         return colour[i];
   }
   
   return "#808080";
}

static int NextHistPlotOrder(const HistPlot& hp)
{
   int order = 0;
   for (size_t i=0; i<hp.vars.size(); i++)
      if (hp.vars[i].order > order)
         order = hp.vars[i].order;
   return order + 10;
}

static void SplitEventAndTagNames(std::string var_name, std::string& event_name, std::string& tag_name) {
   event_name = "";
   tag_name = "";

   std::vector<size_t> colons;

   for (size_t i = 0; i < var_name.size(); i++) {
      if (var_name[i] == ':') {
         colons.push_back(i);
      }
   }

   if (colons.size() == 0) {
      // No colons - leave the tag name empty
      event_name = var_name;
   } else { 
      size_t split_pos;
      size_t slash_pos = var_name.find("/");
      bool uses_per_variable_naming = (slash_pos != std::string::npos);

      if (uses_per_variable_naming && colons.size() % 2 == 1) {
         size_t middle_colon_pos = colons[colons.size() / 2];
         std::string slash_to_mid = var_name.substr(slash_pos + 1, middle_colon_pos - slash_pos - 1);
         std::string mid_to_end = var_name.substr(middle_colon_pos + 1);

         if (slash_to_mid == mid_to_end) {
            // Special case - we have a string of the form Beamlime/GS2:FC1:GS2:FC1.
            // Logger has already warned people that having colons in the equipment/event 
            // names is a bad idea, so we only need to worry about them in the tag name.
            split_pos = middle_colon_pos;
         } else {
            // We have a string of the form Beamlime/Demand:GS2:FC1. Split at the first colon.
            split_pos = colons[0];
         }
      } else {
         // Normal case - split at the fist colon.
         split_pos = colons[0];
      }

      event_name = var_name.substr(0, split_pos);
      tag_name = var_name.substr(split_pos + 1);
   }
}

static void LoadHistPlotFromOdb(MVOdb* odb, HistPlot* hp, const char* group, const char* panel)
{
   std::string path = "History/Display/";
   path += group;
   path += "/";
   path += panel;
   
   MVOdb* o = odb->Chdir(path.c_str());
   if (!o) {
      return;
   }

   o->RS("Timescale",   &hp->timescale);
   o->RD("Minimum",     &hp->minimum);
   o->RD("Maximum",     &hp->maximum);
   o->RB("Zero ylow",   &hp->zero_ylow);
   o->RB("Log axis",    &hp->log_axis);
   o->RB("Zero ylow",   &hp->zero_ylow);
   o->RB("Show run markers", &hp->show_run_markers);
   o->RB("Show values", &hp->show_values);
   o->RB("Show fill",   &hp->show_fill);
   o->RB("Show factor", &hp->show_factor);
   //o->RB("Enable factor and offset", &hp->enable_factor);

   std::vector<std::string> hist_vars;
   std::vector<std::string> hist_formula;
   std::vector<std::string> hist_colour;
   std::vector<std::string> hist_label;
   std::vector<bool>   hist_show_raw_value;
   std::vector<double> hist_factor;
   std::vector<double> hist_offset;
   std::vector<double> hist_voffset;

   o->RSA("Variables", &hist_vars);
   o->RSA("Formula",   &hist_formula);
   o->RSA("Colour",    &hist_colour);
   o->RSA("Label",     &hist_label);
   o->RBA("Show raw value", &hist_show_raw_value);
   o->RDA("Factor",    &hist_factor);
   o->RDA("Offset",    &hist_offset);
   o->RDA("VOffset",   &hist_voffset);

   // fix broken plots with "factor" all zero. for reasons
   // unknown the new history code has corrupted many
   // history plot definitions like this. K.O.
   {
      bool all_zero = true;
      for (size_t i=0; i<hist_factor.size(); i++) {
         if (hist_factor[i] != 0)
            all_zero = false;
      }
      if (all_zero) {
         for (size_t i=0; i<hist_factor.size(); i++) {
            hist_factor[i] = 1.0;
         }
      }
   }

   size_t num = std::max(hist_vars.size(), hist_formula.size());
   num = std::max(num, hist_colour.size());
   num = std::max(num, hist_label.size());
   num = std::max(num, hist_show_raw_value.size());
   num = std::max(num, hist_factor.size());
   num = std::max(num, hist_offset.size());
   num = std::max(num, hist_voffset.size());

   hist_vars.resize(num);
   hist_formula.resize(num);
   hist_colour.resize(num);
   hist_label.resize(num);
   hist_show_raw_value.resize(num);
   hist_factor.resize(num, 1.0);
   hist_offset.resize(num, 0.0);
   hist_voffset.resize(num, 0.0);

   for (size_t i=0; i<num; i++) {
      HistVar v;

      SplitEventAndTagNames(hist_vars[i], v.event_name, v.tag_name);

      v.formula = hist_formula[i];
      v.colour  = hist_colour[i];
      v.label   = hist_label[i];
      v.show_raw_value = hist_show_raw_value[i];
      v.factor  = hist_factor[i];
      v.offset  = hist_offset[i];
      v.voffset = hist_voffset[i];
      v.order   = NextHistPlotOrder(*hp);

      // one-time migration of factor and offset to formula
      if (hp->enable_factor && v.formula.empty()) {
         if (v.factor!=1 || v.offset!=0 || v.voffset!=0) {
            v.formula = msprintf("%g%+g*(x%+g)", v.offset, v.factor, -v.voffset);
         }
      }
      
      hp->vars.push_back(v);
   }
   
   printf("Load from ODB %s: ", path.c_str());
   PrintHistPlot(*hp);

   delete o;
}

static void LoadHistPlotFromParam(HistPlot* hp, Param* p)
{
   hp->timescale        = p->getparam("timescale");
   hp->minimum          = strtod(p->getparam("minimum"), NULL);
   hp->maximum          = strtod(p->getparam("maximum"), NULL);
   hp->zero_ylow        = *p->getparam("zero_ylow");
   hp->log_axis         = *p->getparam("log_axis");
   hp->show_run_markers = *p->getparam("run_markers");
   hp->show_values      = *p->getparam("show_values");
   hp->show_fill        = *p->getparam("show_fill");
   hp->show_factor      = *p->getparam("show_factor");
   //hp->enable_factor    = *p->getparam("enable_factor");
   
   for (int index=0; ; index++) {
      char str[256];
      sprintf(str, "event%d", index);
      
      //printf("param event %d: [%s] [%s] [%d]\n", index, str, p->getparam(str), *p->getparam(str));
      
      if (!p->isparam(str))
         break;
      
      if (*p->getparam(str) == '/') // "/empty"
         continue;
      
      HistVar v;

      v.event_name = p->xgetparam(str);
      
      sprintf(str, "var%d", index);
      v.tag_name = p->xgetparam(str);
      
      sprintf(str, "form%d", index);
      v.formula = p->xgetparam(str);
      
      sprintf(str, "col%d", index);
      v.colour = p->xgetparam(str);
      
      sprintf(str, "lab%d", index);
      v.label = p->xgetparam(str);
      
      sprintf(str, "raw%d", index);
      v.show_raw_value = atoi(p->xgetparam(str).c_str());
      
      sprintf(str, "factor%d", index);
      if (p->isparam(str)) {
         v.factor = atof(p->xgetparam(str).c_str());
      } else {
         v.factor = 1.0;
      }
      
      sprintf(str, "offset%d", index);
      v.offset = atof(p->xgetparam(str).c_str());
      
      sprintf(str, "voffset%d", index);
      v.voffset = atof(p->xgetparam(str).c_str());
      
      sprintf(str, "ord%d", index);
      if (p->isparam(str)) {
         v.order = atoi(p->xgetparam(str).c_str());
      } else {
         v.order = NextHistPlotOrder(*hp);
      }

      hp->vars.push_back(v);
   }
   
   /* correctly number newly added variables */
   for (size_t index=0; index<hp->vars.size(); index++) {
      if (hp->vars[index].order < 0)
         hp->vars[index].order = NextHistPlotOrder(*hp);
   }
   
   printf("Load from param:\n");
   PrintHistPlot(*hp);
}

static void AddHistPlotSelectedParam(HistPlot& hp, Param* p)
{
   int seln = atoi(p->getparam("seln"));
   for (int i=0; i<seln; i++) {
      char str[256];
      sprintf(str, "sel%d", i);
      
      std::string par = p->getparam(str);
      if (par.length() < 1)
         continue;

      std::string event_name, tag_name;
      SplitEventAndTagNames(par, event_name, tag_name);
      
      if (tag_name == "")
         continue;
      
      HistVar v;
      
      v.event_name = event_name;
      v.tag_name   = tag_name;
      v.colour     = NextHistPlotColour(hp);
      v.order      = NextHistPlotOrder(hp);
      
      hp.vars.push_back(v);
   }
}

static void SaveHistPlotToOdb(MVOdb* odb, const HistPlot& hp, const char* group, const char* panel)
{
   if (strlen(group) < 1) {
      cm_msg(MERROR, "SaveHistPlotToOdb", "Error: Cannot write history plot to ODB, group \"%s\", panel \"%s\", invalid group name", group, panel);
      return;
   }

   if (strlen(panel) < 1) {
      cm_msg(MERROR, "SaveHistPlotToOdb", "Error: Cannot write history plot to ODB, group \"%s\", panel \"%s\", invalid panel name", group, panel);
      return;
   }
   
   std::string path = "History/Display/";
   path += group;
   path += "/";
   path += panel;

   printf("Save to ODB %s: ", path.c_str());
   PrintHistPlot(hp);

   MVOdb* o = odb->Chdir(path.c_str(), true);
   
   o->WS("Timescale", hp.timescale.c_str());
   o->WD("Minimum", hp.minimum);
   o->WD("Maximum", hp.maximum);
   o->WB("Zero ylow", hp.zero_ylow);
   o->WB("Log axis", hp.log_axis);
   o->WB("Show run markers", hp.show_run_markers);
   o->WB("Show values", hp.show_values);
   o->WB("Show fill", hp.show_fill);
   o->WB("Show factor and offset", hp.show_factor);
   //o->WB("Enable factor and offset", hp.enable_factor);

   std::vector<std::string> hist_vars;
   std::vector<std::string> hist_formula;
   std::vector<std::string> hist_colour;
   std::vector<std::string> hist_label;
   std::vector<bool>        hist_show_raw_value;
   std::vector<double> hist_factor;
   std::vector<double> hist_offset;
   std::vector<double> hist_voffset;

   for (size_t i=0; i<hp.vars.size(); i++) {
      hist_vars.push_back(hp.vars[i].event_name + ":" + hp.vars[i].tag_name);
      hist_formula.push_back(hp.vars[i].formula);
      hist_colour.push_back(hp.vars[i].colour);
      hist_label.push_back(hp.vars[i].label);
      hist_show_raw_value.push_back(hp.vars[i].show_raw_value);
      hist_factor.push_back(hp.vars[i].factor);
      hist_offset.push_back(hp.vars[i].offset);
      hist_voffset.push_back(hp.vars[i].voffset);
   }

   if (hp.vars.size() > 0) {
      o->WSA("Variables", hist_vars, 64);
      o->WSA("Formula",   hist_formula, 64);
      o->WSA("Colour",    hist_colour, NAME_LENGTH);
      o->WSA("Label",     hist_label, NAME_LENGTH);
      o->WBA("Show raw value", hist_show_raw_value);
      o->WDA("Factor",    hist_factor);
      o->WDA("Offset",    hist_offset);
      o->WDA("VOffset",   hist_voffset);
   } else {
      o->Delete("Variables");
      o->Delete("Formula");
      o->Delete("Colour");
      o->Delete("Label");
      o->Delete("Show raw value");
      o->Delete("Factor");
      o->Delete("Offset");
      o->Delete("VOffset");
   }

   delete o;
}

static void DeleteHistPlotDeleted(HistPlot& hp)
{
   /* delete variables according to "hist_order" */
   
   while (1) {
      bool something_deleted = false;
      for (unsigned i=0; i<hp.vars.size(); i++) {
         if (hp.vars[i].order <= 0) {
            hp.vars.erase(hp.vars.begin() + i);
            something_deleted = true;
         }
      }
      if (!something_deleted)
         break;
   }
}

static void SortHistPlotVars(HistPlot& hp)
{
   /* sort variables according to "hist_order" */
   
   bool need_sort = false;
   for (size_t i=1; i<hp.vars.size(); i++) {
      if (hp.vars[i-1].order >= hp.vars[i].order) {
         need_sort = true;
      }
   }
   
   if (need_sort) {
      /* sort variables by order */
      std::sort(hp.vars.begin(), hp.vars.end(), cmp_vars);
      
      /* renumber the variables according to the new sorted order */
      for (size_t index=0; index<hp.vars.size(); index++)
         hp.vars[index].order = (index+1)*10;
   }
}

void show_hist_config_page(MVOdb* odb, Param* p, Return* r, const char *hgroup, const char *hpanel)
{
   int status;
   int max_display_events = 20;
   int max_display_tags = 200;
   char str[256], hcmd[256];

   odb->RI("History/MaxDisplayEvents", &max_display_events, true);
   odb->RI("History/MaxDisplayTags", &max_display_tags, true);

   strlcpy(hcmd, p->getparam("hcmd"), sizeof(hcmd));

   if (equal_ustring(hcmd, "Clear history cache")) {
      //printf("clear history cache!\n");
      strcpy(hcmd, "Refresh");
      MidasHistoryInterface *mh = get_history();
      if (mh)
         mh->hs_clear_cache();
   }

   //printf("cmd [%s]\n", cmd);
   //printf("cmdx [%s]\n", p->getparam("cmdx"));

   HistPlot hp;

   if (equal_ustring(hcmd, "refresh") || equal_ustring(hcmd, "save")) {
      LoadHistPlotFromParam(&hp, p);
      DeleteHistPlotDeleted(hp);
   } else {
      LoadHistPlotFromOdb(odb, &hp, hgroup, hpanel);
   }

   SortHistPlotVars(hp);

   if (strlen(p->getparam("seln")) > 0)
      AddHistPlotSelectedParam(hp, p);

   //hp->Print();

   if (hcmd[0] && equal_ustring(hcmd, "save")) {
      SaveHistPlotToOdb(odb, hp, hgroup, hpanel);

      if (p->getparam("redir") && *p->getparam("redir"))
         redirect(r, p->getparam("redir"));
      else {
         sprintf(str, "?cmd=oldhistory&group=%s&panel=%s", hgroup, hpanel);
         redirect(r, str);
      }
      return;
   }

   show_header(r, "History Config", "GET", "", 0);
   r->rsprintf("</table>");  //close header table

   r->rsprintf("<table class=\"mtable\">"); //open main table

   r->rsprintf("<tr><th colspan=11 class=\"subStatusTitle\">History Panel \"%s\" / \"%s\"</th></tr>\n", hgroup, hpanel);

   /* menu buttons */
   r->rsprintf("<tr><td colspan=11>\n");

   r->rsprintf("<input type=button value=Refresh ");
   r->rsprintf("onclick=\"document.form1.hcmd.value='Refresh';document.form1.submit()\">\n");

   r->rsprintf("<input type=button value=Save ");
   r->rsprintf("onclick=\"document.form1.hcmd.value='Save';document.form1.submit()\">\n");

   {
   r->rsprintf("<input type=button value=Cancel ");
   std::string url = "?cmd=oldhistory&group=";
   url += hgroup;
   url += "&panel=";
   url += hpanel;
   url += "&hcmd=Cancel";
   if (p->getparam("redir")) {
      char enc[256];
      strlcpy(enc, p->getparam("redir"), sizeof(enc));
      urlEncode(enc, sizeof(enc));
      url += "&redir=";
      url += enc;
   }
   r->rsprintf("onclick=\"window.location.search='%s'\">\n", url.c_str());
   r->rsprintf("<input type=button value=\"Edit in ODB\"");
   }
   {
   std::string url = "?cmd=odb&odb_path=";
   url += "/History/Display/";
   url += urlEncode(hgroup);
   url += "/";
   url += urlEncode(hpanel);
   r->rsprintf("onclick=\"window.location.search='%s'\">\n", url.c_str());
   }
   r->rsprintf("<input type=button value=\"Clear history cache\"");
   r->rsprintf("onclick=\"document.form1.hcmd.value='Clear history cache';document.form1.submit()\">\n");
   r->rsprintf("<input type=button value=\"Delete panel\"");
   r->rsprintf("onclick=\"window.location.search='?cmd=oldhistory&group=%s&panel=%s&hcmd=Delete%%20panel'\">\n", hgroup, hpanel);
   r->rsprintf("</td></tr>\n");

   r->rsprintf("<tr><td colspan=11>\n");

   /* sort_vars */
   int sort_vars = *p->getparam("sort_vars");
   r->rsprintf("<input type=checkbox %s name=sort_vars value=1 onclick=\"this.form.submit();\">Sort variable names", sort_vars?"checked":"");

   /* old_vars */
   int old_vars = *p->getparam("old_vars");
   r->rsprintf("&nbsp;&nbsp;<input type=checkbox %s name=old_vars value=1 onclick=\"this.form.submit();\">Show deleted and renamed variables", old_vars?"checked":"");

   if (hp.show_factor)
      r->rsprintf("&nbsp;&nbsp;<input type=checkbox checked name=show_factor value=1 onclick=\"document.form1.hcmd.value='Refresh';document.form1.submit()\">");
   else
      r->rsprintf("&nbsp;&nbsp;<input type=checkbox name=show_factor value=1 onclick=\"document.form1.hcmd.value='Refresh';document.form1.submit()\">");
   r->rsprintf("Show&nbsp;factor&nbsp;and&nbsp;offset\n");

   /* hidden command for refresh */
   r->rsprintf("<input type=hidden name=cmd value=Oldhistory>\n");
   r->rsprintf("<input type=hidden name=hcmd value=Refresh>\n");
   r->rsprintf("<input type=hidden name=panel value=\"%s\">\n", hpanel);
   r->rsprintf("<input type=hidden name=group value=\"%s\">\n", hgroup);

   if (p->getparam("redir") && *p->getparam("redir"))
      r->rsprintf("<input type=hidden name=redir value=\"%s\">\n", p->getparam("redir"));

   r->rsprintf("</td></tr>\n");

   r->rsprintf("<tr><td colspan=4 style='text-align:right'>Time scale (in units 'm', 'h', 'd'):</td>\n");
   r->rsprintf("<td colspan=3><input type=text size=12 name=timescale value=%s></td><td colspan=4></td></tr>\n", hp.timescale.c_str());

   r->rsprintf("<tr><td colspan=4 style='text-align:right'>Minimum (set to '-inf' for autoscale):</td>\n");
   r->rsprintf("<td colspan=3><input type=text size=12 name=minimum value=%f></td><td colspan=4></td></tr>\n", hp.minimum);

   r->rsprintf("<tr><td colspan=4 style='text-align:right'>Maximum (set to 'inf' for autoscale):</td>\n");
   r->rsprintf("<td colspan=3><input type=text size=12 name=maximum value=%f></td><td colspan=4></td></tr>\n", hp.maximum);

   r->rsprintf("<tr><td colspan=11>");
   
   if (hp.zero_ylow)
      r->rsprintf("<input type=checkbox checked name=zero_ylow value=1>");
   else
      r->rsprintf("<input type=checkbox name=zero_ylow value=1>");
   r->rsprintf("Zero&nbsp;Y;&nbsp;axis\n");

   if (hp.log_axis)
      r->rsprintf("<input type=checkbox checked name=log_axis value=1>");
   else
      r->rsprintf("<input type=checkbox name=log_axis value=1>");
   r->rsprintf("Logarithmic&nbsp;Y&nbsp;axis\n");

   if (hp.show_run_markers)
      r->rsprintf("&nbsp;&nbsp;<input type=checkbox checked name=run_markers value=1>");
   else
      r->rsprintf("&nbsp;&nbsp;<input type=checkbox name=run_markers value=1>");
   r->rsprintf("Show&nbsp;run&nbsp;markers\n");

   if (hp.show_values)
      r->rsprintf("&nbsp;&nbsp;<input type=checkbox checked name=show_values value=1>");
   else
      r->rsprintf("&nbsp;&nbsp;<input type=checkbox name=show_values value=1>");
   r->rsprintf("Show&nbsp;values&nbsp;of&nbsp;variables\n");

   if (hp.show_fill)
      r->rsprintf("&nbsp;&nbsp;<input type=checkbox checked name=show_fill value=1>");
   else
      r->rsprintf("&nbsp;&nbsp;<input type=checkbox name=show_fill value=1>");
   r->rsprintf("Show&nbsp;graph&nbsp;fill\n");

   r->rsprintf("</td></tr>\n");

   /*---- events and variables ----*/

   /* get display event name */

   printf("AAA!\n");

   MidasHistoryInterface* mh = get_history();
   if (mh == NULL) {
      r->rsprintf(str, "History is not configured\n");
      return;
   }

   time_t t = time(NULL);

   if (old_vars)
      t = 0;

   std::vector<std::string> events;

   if (!old_vars)
      hs_read_event_list(&events);

   if (events.size() == 0)
      mh->hs_get_events(t, &events);

#if 0
   for (unsigned i=0; i<events.size(); i++)
      printf("event %d: \"%s\"\n", i, events[i].c_str());
#endif

   // has to be sorted or equipment name code below would not work
   //std::sort(events.begin(), events.end(), cmp_events);
   std::sort(events.begin(), events.end(), cmp_events1);

   if (strlen(p->getparam("cmdx")) > 0) {
      r->rsprintf("<tr><th colspan=8 class=\"subStatusTitle\">List of available history variables</th></tr>\n");
      r->rsprintf("<tr><th colspan=1>Sel<th colspan=1>Equipment<th colspan=1>Event<th colspan=1>Variable</tr>\n");

      std::string cmdx = p->xgetparam("cmdx");
      std::string xeqname;

      int i=0;
      for (unsigned e=0; e<events.size(); e++) {
         std::string eqname;
         eqname = events[e].substr(0, events[e].find("/"));

         if (eqname.length() < 1)
            eqname = events[e];

         bool once = false;
         if (eqname != xeqname)
            once = true;

         std::string qcmd = "Expand " + eqname;

         //printf("param [%s] is [%s]\n", qcmd.c_str(), p->getparam(qcmd.c_str()));

         bool collapsed = true;

         if (cmdx == qcmd)
            collapsed = false;

         if (strlen(p->getparam(qcmd.c_str())) > 0)
            collapsed = false;

         if (collapsed) {
            if (eqname == xeqname)
               continue;

            r->rsprintf("<tr align=left>\n");
            r->rsprintf("<td></td>\n");
            r->rsprintf("<td>%s</td>\n", eqname.c_str());
            r->rsprintf("<td><input type=submit name=cmdx value=\"%s\"></td>\n", qcmd.c_str());
            r->rsprintf("<td>%s</td>\n", "");
            r->rsprintf("</tr>\n");
            xeqname = eqname;
            continue;
         }

         if (once)
            r->rsprintf("<tr><input type=hidden name=\"%s\" value=%d></tr>\n", qcmd.c_str(), 1);

         std::string rcmd = "Expand " + events[e];

         //printf("param [%s] is [%s]\n", rcmd.c_str(), p->getparam(rcmd.c_str()));

         collapsed = true;

         if (cmdx == rcmd)
            collapsed = false;

         if (strlen(p->getparam(rcmd.c_str())) > 0)
            collapsed = false;

         if (collapsed) {
            r->rsprintf("<tr align=left>\n");
            r->rsprintf("<td></td>\n");
            r->rsprintf("<td>%s</td>\n", eqname.c_str());
            r->rsprintf("<td>%s</td>\n", events[e].c_str());
            r->rsprintf("<td><input type=submit name=cmdx value=\"%s\"></td>\n", rcmd.c_str());
            r->rsprintf("</tr>\n");
            continue;
         }

         r->rsprintf("<tr><input type=hidden name=\"%s\" value=%d></tr>\n", rcmd.c_str(), 1);

         xeqname = eqname;

         std::vector<TAG> tags;

         status = mh->hs_get_tags(events[e].c_str(), t, &tags);

         if (status == HS_SUCCESS && tags.size() > 0) {

            if (sort_vars)
               std::sort(tags.begin(), tags.end(), cmp_tags);

            for (unsigned v=0; v<tags.size(); v++) {

               for (unsigned j=0; j<tags[v].n_data; j++) {
                  char tagname[256];

                  if (tags[v].n_data == 1)
                     sprintf(tagname, "%s", tags[v].name);
                  else
                     sprintf(tagname, "%s[%d]", tags[v].name, j);

		  bool checked = false;
#if 0
		  for (int index=0; index<MAX_VARS; index++) {
		    if (equal_ustring(vars[index].event_name, events[e].c_str()) && equal_ustring(vars[index].var_name, tagname)) {
		      checked = true;
		      break;
		    }
		  }
#endif

		  r->rsprintf("<tr align=left>\n");
		  r->rsprintf("<td><input type=checkbox %s name=\"sel%d\" value=\"%s:%s\"></td>\n", checked?"checked":"", i++, events[e].c_str(), tagname);
		  r->rsprintf("<td>%s</td>\n", eqname.c_str());
		  r->rsprintf("<td>%s</td>\n", events[e].c_str());
		  r->rsprintf("<td>%s</td>\n", tagname);
		  r->rsprintf("</tr>\n");
	       }
	    }
	 }
      }

      r->rsprintf("<tr>\n");
      r->rsprintf("<td></td>\n");
      r->rsprintf("<td>\n");
      r->rsprintf("<input type=hidden name=seln value=%d>\n", i);
      r->rsprintf("<input type=submit value=\"Add Selected\">\n");
      r->rsprintf("</td>\n");
      r->rsprintf("</tr>\n");
   }

   r->rsprintf("<tr><td colspan=11 style='text-align:left'>New history: displayed_value = formula(history_value)</td></tr>\n");
   r->rsprintf("<tr><td colspan=11 style='text-align:left'>Old history: displayed_value = offset + factor*(history_value - voffset)</td></tr>\n");
   r->rsprintf("<tr><td colspan=11 style='text-align:left'>Formula format: \"3*x+4\", \"10*Math.sin(x)\", etc. all javascript math functions can be used</td></tr>\n");
   r->rsprintf("<tr><td colspan=11 style='text-align:left'>To display the raw history value instead of computed formula or offset vallue, check the \"raw\" checkbox</td></tr>\n");
   r->rsprintf("<tr><td colspan=11 style='text-align:left'>To reorder entries: enter new ordering in the \"order\" column and press \"refresh\"</td></tr>\n");
   r->rsprintf("<tr><td colspan=11 style='text-align:left'>To delete entries: enter \"-1\" or leave blank the \"order\" column and press \"refresh\"</td></tr>\n");

   r->rsprintf("<tr>\n");
   r->rsprintf("<th>Col<th>Event<th>Variable<th>Formula<th>Colour<th>Label<th>Raw<th>Order");
   if (hp.show_factor) {
      r->rsprintf("<th>Factor<th>Offset<th>VOffset");
   }
   r->rsprintf("</tr>\n");

   //print_vars(vars);

   size_t nvars = hp.vars.size();
   for (size_t index = 0; index <= nvars; index++) {

      r->rsprintf("<tr>");

      if (index < nvars) {
         if (hp.vars[index].colour.empty())
            hp.vars[index].colour = NextHistPlotColour(hp);
         r->rsprintf("<td style=\"background-color:%s\">&nbsp;<td>\n", hp.vars[index].colour.c_str());
      } else {
         r->rsprintf("<td>&nbsp;<td>\n");
      }

      /* event and variable selection */

      r->rsprintf("<select name=\"event%d\" size=1 onChange=\"document.form1.submit()\">\n", (int)index);

      /* enumerate events */

      /* empty option */
      r->rsprintf("<option value=\"/empty\">&lt;empty&gt;\n");

      if (index==nvars) { // last "empty" entry
         for (unsigned e=0; e<events.size(); e++) {
            const char *p = events[e].c_str();
            r->rsprintf("<option value=\"%s\">%s\n", p, p);
         }
      } else if ((int)events.size() > max_display_events) { // too many events
         r->rsprintf("<option selected value=\"%s\">%s\n", hp.vars[index].event_name.c_str(), hp.vars[index].event_name.c_str());
         r->rsprintf("<option>(%d events omitted)\n", (int)events.size());
      } else { // show all events
         bool found = false;
         for (unsigned e=0; e<events.size(); e++) {
            const char *s = "";
            const char *p = events[e].c_str();
            if (equal_ustring(hp.vars[index].event_name.c_str(), p)) {
               s = "selected";
               found = true;
            }
            r->rsprintf("<option %s value=\"%s\">%s\n", s, p, p);
         }
         if (!found) {
            const char *p = hp.vars[index].event_name.c_str();
            r->rsprintf("<option selected value=\"%s\">%s\n", p, p);
         }
      }

      r->rsprintf("</select></td>\n");

      //if (hp.vars[index].order <= 0)
      //   hp.vars[index].order = (index+1)*10;

      if (index < nvars) {
         bool found_tag = false;
         std::string selected_tag = hp.vars[index].tag_name;

         r->rsprintf("<td><select name=\"var%d\">\n", (int)index);

         std::vector<TAG> tags;

         status = mh->hs_get_tags(hp.vars[index].event_name.c_str(), t, &tags);

         if (status == HS_SUCCESS && tags.size() > 0) {

            if (/* DISABLES CODE */ (0)) {
               printf("Compare %d\n", cmp_names("AAA", "BBB"));
               printf("Compare %d\n", cmp_names("BBB", "AAA"));
               printf("Compare %d\n", cmp_names("AAA", "AAA"));
               printf("Compare %d\n", cmp_names("A", "AAA"));
               printf("Compare %d\n", cmp_names("A111", "A1"));
               printf("Compare %d\n", cmp_names("A111", "A2"));
               printf("Compare %d\n", cmp_names("A111", "A222"));
               printf("Compare %d\n", cmp_names("A111a", "A111b"));
            }

            if (sort_vars)
               std::sort(tags.begin(), tags.end(), cmp_tags);

            if (/* DISABLES CODE */ (0)) {
               printf("Event [%s] %d tags\n", hp.vars[index].event_name.c_str(), (int)tags.size());

               for (unsigned v=0; v<tags.size(); v++) {
                 printf("tag[%d] [%s]\n", v, tags[v].name);
               }
            }

            unsigned count_tags = 0;
            for (unsigned v=0; v<tags.size(); v++)
               count_tags += tags[v].n_data;

            //printf("output %d option tags\n", count_tags);

            if ((int)count_tags < max_display_tags) {
               for (unsigned v=0; v<tags.size(); v++) {

                 for (unsigned j=0; j<tags[v].n_data; j++) {
                    std::string tagname;

                    if (tags[v].n_data == 1)
                       tagname = tags[v].name;
                    else {
                       char buf[256];
                       sprintf(buf, "[%d]", j);
                       tagname = std::string(tags[v].name) + buf;
                    }

                    if (equal_ustring(selected_tag.c_str(), tagname.c_str())) {
                       r->rsprintf("<option selected value=\"%s\">%s\n", tagname.c_str(), tagname.c_str());
                       found_tag = true;
                    }
                    else
                       r->rsprintf("<option value=\"%s\">%s\n", tagname.c_str(), tagname.c_str());

                    //printf("%d [%s] [%s] [%s][%s] %d\n", (int)index, vars[index].event_name, tagname, vars[index].var_name, selected_var, found_var);
                 }
               }
            }
         }

         if (!found_tag)
            if (hp.vars[index].tag_name.length() > 0)
               r->rsprintf("<option selected value=\"%s\">%s\n", hp.vars[index].tag_name.c_str(), hp.vars[index].tag_name.c_str());

         r->rsprintf("</select></td>\n");
         r->rsprintf("<td><input type=text size=15 maxlength=256 name=\"form%d\" value=%s></td>\n", (int)index, hp.vars[index].formula.c_str());
         r->rsprintf("<td><input type=text size=8 maxlength=10 name=\"col%d\" value=%s></td>\n", (int)index, hp.vars[index].colour.c_str());
         r->rsprintf("<td><input type=text size=8 maxlength=%d name=\"lab%d\" value=\"%s\"></td>\n", NAME_LENGTH, (int)index, hp.vars[index].label.c_str());
         if (hp.vars[index].show_raw_value)
            r->rsprintf("<td><input type=checkbox checked name=\"raw%d\" value=1></td>", (int)index);
         else
            r->rsprintf("<td><input type=checkbox name=\"raw%d\" value=1></td>", (int)index);
         r->rsprintf("<td><input type=text size=3 maxlength=32 name=\"ord%d\" value=\"%d\"></td>\n", (int)index, hp.vars[index].order);
         if (hp.show_factor) {
            r->rsprintf("<td><input type=text size=6 maxlength=32 name=\"factor%d\" value=\"%g\"></td>\n", (int)index, hp.vars[index].factor);
            r->rsprintf("<td><input type=text size=6 maxlength=32 name=\"offset%d\" value=\"%g\"></td>\n", (int)index, hp.vars[index].offset);
            r->rsprintf("<td><input type=text size=6 maxlength=32 name=\"voffset%d\" value=\"%g\"></td>\n", (int)index, hp.vars[index].voffset);
         } else {
            r->rsprintf("<input type=hidden name=\"factor%d\" value=\"%f\">\n", (int)index, hp.vars[index].factor);
            r->rsprintf("<input type=hidden name=\"offset%d\" value=\"%f\">\n", (int)index, hp.vars[index].offset);
            r->rsprintf("<input type=hidden name=\"voffset%d\" value=\"%f\">\n", (int)index, hp.vars[index].voffset);
         }
      } else {
         r->rsprintf("<td colspan=2><input type=submit name=cmdx value=\"List all variables\"></td>\n");
      }

      r->rsprintf("</tr>\n");
   }

   r->rsprintf("</table>\n");
   //r->rsprintf("</form>\n");
   r->rsprintf("</div>\n"); // closing for <div id="mmain">
   r->rsprintf("</form>\n");
   r->rsprintf("</body></html>\r\n");
}

/*------------------------------------------------------------------*/

void export_hist(MVOdb* odb, Return* r, const char *group, const char *panel, time_t endtime, int scale, int index, int labels)
{
   //HNDLE hDB, hkey, hkeypanel;
   //int size;
   int status;
   //char str[256];

   int debug = 0;

   ss_tzset(); // required for localtime_r()

#if 0
   cm_get_experiment_database(&hDB, NULL);

   /* check panel name in ODB */
   sprintf(str, "/History/Display/%s/%s", group, panel);
   db_find_key(hDB, 0, str, &hkeypanel);
   if (!hkeypanel) {
      sprintf(str, "Cannot find /History/Display/%s/%s in ODB\n", group, panel);
      show_error(r, str);
      return;
   }

   /* get runmarker flag */
   BOOL runmarker = 1;
   size = sizeof(runmarker);
   db_get_value(hDB, hkeypanel, "Show run markers", &runmarker, &size, TID_BOOL, TRUE);

   if (scale == 0) {
      /* get timescale */
      std::string ts = "1h";
      status = db_get_value_string(hDB, hkeypanel, "Timescale", 0, &ts, TRUE);
      if (status != DB_SUCCESS) {
         /* delete old integer key */
         db_find_key(hDB, hkeypanel, "Timescale", &hkey);
         if (hkey)
            db_delete_key(hDB, hkey, FALSE);

         ts = "1h";
         status = db_get_value_string(hDB, hkeypanel, "Timescale", 0, &ts, TRUE);
      }

      scale = time_to_sec(ts.c_str());
   }
#endif

   time_t now = ss_time();

   if (endtime == 0)
      endtime = now;

   HistoryData hsxxx;
   HistoryData* hsdata = &hsxxx;

   HistPlot hp;
   LoadHistPlotFromOdb(odb, &hp, group, panel);

   time_t starttime = endtime - scale;

   //printf("start %.0f, end %.0f, scale %.0f\n", (double)starttime, (double)endtime, (double)scale);

   status = read_history(hp, /*hDB, group, panel,*/ index, hp.show_run_markers, starttime, endtime, 0, hsdata);
   if (status != HS_SUCCESS) {
      char str[256];
      sprintf(str, "History error, status %d\n", status);
      show_error(r, str);
      return;
   }

   if (debug)
      hsdata->Print();

   int *i_var = (int *)malloc(sizeof(int)*hsdata->nvars);

   for (int i = 0; i < hsdata->nvars; i++)
      i_var[i] = -1;

   time_t t = 0;

   /* find first time where all variables are available */
   for (int i = 0; i < hsdata->nvars; i++)
      if (hsdata->odb_index[i] >= 0)
         if (hsdata->num_entries[i] > 0)
            if ((t == 0) || (hsdata->t[i][0] > t))
               t = hsdata->t[i][0];

   if (t == 0 && hsdata->nvars > 1) {
      show_error(r, "No history available for choosen period");
      free(i_var);
      return;
   }

   int run_index = -1;
   int state_index = -1;
   int n_run_number = 0;
   time_t* t_run_number = NULL;
   if (hp.show_run_markers)
      for (int i = 0; i < hsdata->nvars; i++) {
         if (hsdata->odb_index[i] == -2) {
            n_run_number = hsdata->num_entries[i];
            t_run_number = hsdata->t[i];
            run_index = i;
         } else if (hsdata->odb_index[i] == -1) {
            state_index = i;
         }
      }

   //printf("runmarker %d, state %d, run %d\n", runmarker, state_index, run_index);

   /* header */
   r->rsprintf("HTTP/1.1 200 Document follows\r\n");
   r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
   r->rsprintf("Accept-Ranges: bytes\r\n");
   r->rsprintf("Cache-control: private, max-age=0, no-cache\r\n");
   r->rsprintf("Expires: Fri, 01 Jan 1983 00:00:00 GMT\r\n");
   r->rsprintf("Content-Type: text/plain\r\n");
   r->rsprintf("Content-disposition: attachment; filename=\"export.csv\"\r\n");
   r->rsprintf("\r\n");

   /* output header line with variable names */
   if (hp.show_run_markers && t_run_number)
      r->rsprintf("Time, Timestamp, Run, Run State, ");
   else
      r->rsprintf("Time, Timestamp, ");

   for (int i = 0, first = 1; i < hsdata->nvars; i++) {
      if (hsdata->odb_index[i] < 0)
         continue;
      if (hsdata->num_entries[i] <= 0)
         continue;
      if (!first)
         r->rsprintf(", ");
      first = 0;
      r->rsprintf("%s", hsdata->var_names[i]);
   }
   r->rsprintf("\n");

   int i_run = 0;

   do {

      if (debug)
         printf("hsdata %p, t %d, irun %d\n", hsdata, (int)t, i_run);

      /* find run number/state which is valid for t */
      if (hp.show_run_markers && t_run_number)
         while (i_run < n_run_number-1 && t_run_number[i_run+1] <= t)
            i_run++;

      //printf("irun %d\n", i_run);

      /* find index for all variables which is valid for t */
      for (int i = 0; i < hsdata->nvars; i++)
         while (hsdata->num_entries[i] > 0 && i_var[i] < hsdata->num_entries[i] - 1 && hsdata->t[i][i_var[i]+1] <= t)
            i_var[i]++;

      /* finish if last point for all variables reached */
      bool done = true;
      for (int i = 0 ; i < hsdata->nvars ; i++)
         if (hsdata->num_entries[i] > 0 && i_var[i] < hsdata->num_entries[i]) {
            done = false;
            break;
         }

      if (debug) {
         printf("step to time %d: ", (int)t);
         for (int i = 0; i < hsdata->nvars; i++) {
            printf(" [%d] %d, ", hsdata->num_entries[i], i_var[i]);
         }
         printf(" done: %d\n", done);
      }

      if (done)
         break;

      struct tm tms;
      localtime_r(&t, &tms);

      char fmt[256];
      //strcpy(fmt, "%c");
      strcpy(fmt, "%Y.%m.%d %H:%M:%S");
      char str[256];
      strftime(str, sizeof(str), fmt, &tms);

      if (t_run_number && run_index>=0 && state_index>=0) {
         if (t_run_number[i_run] <= t)
            r->rsprintf("%s, %d, %.0f, %.0f, ", str, (int)t, hsdata->v[run_index][i_run], hsdata->v[state_index][i_run]);
         else
            r->rsprintf("%s, %d, N/A, N/A, ", str, (int)t);
      } else
         r->rsprintf("%s, %d, ", str, (int)t);

      if (debug) {
         for (int i= 0 ; i < hsdata->nvars ; i++)
            printf(" %d (%g)", i_var[i], hsdata->v[i][i_var[i]]);
         printf("\n");
      }

      for (int i=0, first=1 ; i<hsdata->nvars ; i++) {
         if (i_var[i] < 0)
            continue;
         if (hsdata->odb_index[i] < 0)
            continue;
         if (!first)
            r->rsprintf(", ");
         first = 0;
         //r->rsprintf("(%d %g)", i_var[i], hsdata->v[i][i_var[i]]);
         r->rsprintf("%g", hsdata->v[i][i_var[i]]);
      }
      r->rsprintf("\n");

      /* find next t as smallest delta t */
      int dt = -1;
      for (int i = 0 ; i < hsdata->nvars ; i++)
         if (i_var[i]>=0 && hsdata->odb_index[i]>=0 && hsdata->num_entries[i]>0 && i_var[i]<hsdata->num_entries[i]-1) {
            int xdt = hsdata->t[i][i_var[i]+1] - t;
            if (debug)
               printf("var %d, i_var %d->%d, t %d->%d, dt %d\n", i, i_var[i], i_var[i]+1, (int)hsdata->t[i][i_var[i]], (int)hsdata->t[i][i_var[i]+1], xdt);
            if (dt <= 0 || xdt < dt)
               dt = xdt;
         }

      if (debug)
         printf("dt %d\n", dt);

      if (dt <= 0)
         break;

      t += dt;

   } while (1);

   free(i_var);
}

/*------------------------------------------------------------------*/

void show_hist_page(MVOdb* odb, Param* p, Return* r, const char *dec_path, char *buffer, int *buffer_size, int refresh)
{
   HNDLE hDB, hkey, hikeyp, hkeyp, hkeybutton;
   KEY key, ikey;
   int i, j, k, scale, index, width, size, status, labels;
   char hgroup[256], hpanel[256], hcmd[256];
   const char def_button[][NAME_LENGTH] = { "10m", "1h", "3h", "12h", "24h", "3d", "7d" };

   cm_get_experiment_database(&hDB, NULL);

   hcmd[0] = hgroup[0] = hpanel[0] = 0;

   if (p->getparam("group") && *p->getparam("group"))
      strlcpy(hgroup, p->getparam("group"), sizeof(hgroup));
   if (p->getparam("panel") && *p->getparam("panel"))
      strlcpy(hpanel, p->getparam("panel"), sizeof(hpanel));
   if (p->getparam("hcmd") && *p->getparam("hcmd"))
      strlcpy(hcmd, p->getparam("hcmd"), sizeof(hcmd));

   if (equal_ustring(hcmd, "Reset")) {
      std::string redir;
      //sprintf(str, "?cmd=oldhistory&group=%s&panel=%s", hgroup, hpanel);
      redir += "?cmd=oldhistory&group=";
      redir += hgroup;
      redir += "&panel=";
      redir += hpanel;
      redirect(r, redir.c_str());
      return;
   }

   if (equal_ustring(hcmd, "Query")) {
      show_query_page(p, r);
      return;
   }

   if (equal_ustring(hcmd, "Cancel")) {
      //sprintf(str, "?cmd=oldhistory&group=%s&panel=%s", hgroup, hpanel);
      if (p->getparam("redir") && *p->getparam("redir"))
         redirect(r, p->getparam("redir"));
      else {
         std::string redir;
         redir += "?cmd=oldhistory&group=";
         redir += hgroup;
         redir += "&panel=";
         redir += hpanel;
         redirect(r, redir.c_str());
      }
      return;
   }

   if (equal_ustring(hcmd, "Config") ||
       equal_ustring(hcmd, "Save")
       || equal_ustring(hcmd, "Clear history cache")
       || equal_ustring(hcmd, "Refresh")) {

      show_hist_config_page(odb, p, r, hgroup, hpanel);
      return;
   }

   if (equal_ustring(hcmd, "New")) {
      show_header(r, "History", "GET", "", 0);

      r->rsprintf("<table class=\"dialogTable\">");
      r->rsprintf("<tr><th class=\"subStatusTitle\" colspan=2>New History Item</th><tr>");
      r->rsprintf("<tr><td align=center colspan=2>\n");
      r->rsprintf("Select group: &nbsp;&nbsp;");
      r->rsprintf("<select id=\"group\" name=\"group\">\n");

      /* list existing groups */
      db_find_key(hDB, 0, "/History/Display", &hkey);
      if (hkey) {
         for (i = 0;; i++) {
            db_enum_link(hDB, hkey, i, &hkeyp);

            if (!hkeyp)
               break;

            db_get_key(hDB, hkeyp, &key);
            if (equal_ustring(hgroup, key.name))
               r->rsprintf("<option selected>%s</option>\n", key.name);
            else
               r->rsprintf("<option>%s</option>\n", key.name);
         }
      }
      if (!hkey || i == 0)
         r->rsprintf("<option>Default</option>\n");
      r->rsprintf("</select><p>\n");

      r->rsprintf("Or enter new group name: &nbsp;&nbsp;");
      r->rsprintf("<input type=text size=15 maxlength=31 id=new_group name=new_group>\n");

      r->rsprintf("<tr><td align=center colspan=2>\n");
      r->rsprintf("<br>Panel name: &nbsp;&nbsp;");
      r->rsprintf("<input type=text size=15 maxlength=31 id=panel name=panel><br><br>\n");
      r->rsprintf("</td></tr>\n");

      r->rsprintf("<tr><td align=center colspan=2>");
      std::string str = "?cmd=oldhistory&hcmd=createnew";
      str += "&new_group='+document.getElementById('new_group').value+'";
      str += "&group='+document.getElementById('group').value+'";
      str += "&panel='+document.getElementById('panel').value+'";
      r->rsprintf("<input type=button value=Submit onclick=\"window.location.search='%s'\">\n", str.c_str());
      r->rsprintf("</td></tr>\n");

      r->rsprintf("</table>\r\n");
      r->rsprintf("</div>\n"); // closing for <div id="mmain">
      r->rsprintf("</form>\n");
      r->rsprintf("</body></html>\r\n");
      return;
   }

   if (equal_ustring(hcmd, "Delete Panel")) {
      std::string path;
      //sprintf(str, "/History/Display/%s/%s", hgroup, hpanel);
      path += "/History/Display/";
      path += hgroup;
      path += "/";
      path += hpanel;
      if (db_find_key(hDB, 0, path.c_str(), &hkey)==DB_SUCCESS)
         db_delete_key(hDB, hkey, FALSE);

      redirect(r, "?cmd=oldhistory");
      return;
   }

   if (equal_ustring(hcmd, "createnew")) {

      /* strip leading/trailing spaces */
      while (hpanel[0] == ' ') {
         char str[256];
         strlcpy(str, hpanel+1, sizeof(str));
         strlcpy(hpanel, str, sizeof(hpanel));
      }
      while (strlen(hpanel)> 1 && hpanel[strlen(hpanel)-1] == ' ')
         hpanel[strlen(hpanel)-1] = 0;

      /* use new group if present */
      if (p->isparam("new_group") && *p->getparam("new_group"))
         strlcpy(hgroup, p->getparam("new_group"), sizeof(hgroup));

      /* configure that panel */
      show_hist_config_page(odb, p, r, hgroup, hpanel);
      return;
   }

   const char* pscale = p->getparam("scale");
   if (pscale == NULL || *pscale == 0)
      pscale = p->getparam("hscale");
   const char* pwidth = p->getparam("width");
   if (pwidth == NULL || *pwidth == 0)
      pwidth = p->getparam("hwidth");
   const char* pheight = p->getparam("height");
   if (pheight == NULL || *pheight == 0)
      pheight = p->getparam("hheight");
   const char* pindex = p->getparam("index");
   if (pindex == NULL || *pindex == 0)
      pindex = p->getparam("hindex");

   labels = 1;
   if (*p->getparam("labels") && atoi(p->getparam("labels")) == 0)
      labels = 0;

   std::string bgcolor = "FFFFFF";
   if (*p->getparam("bgcolor"))
      bgcolor = p->xgetparam("bgcolor");

   std::string fgcolor = "000000";
   if (*p->getparam("fgcolor"))
      fgcolor = p->xgetparam("fgcolor");

   std::string gridcolor = "A0A0A0";
   if (*p->getparam("gcolor"))
      gridcolor = p->xgetparam("gcolor");

   /* evaluate scale and offset */

   time_t endtime = 0;
   if (p->isparam("time"))
      endtime = string_to_time(p->getparam("time"));
   else if (p->isparam("htime"))
      endtime = string_to_time(p->getparam("htime"));

   if (pscale && *pscale)
      scale = time_to_sec(pscale);
   else
      scale = 0;

   index = -1;
   if (pindex && *pindex)
      index = atoi(pindex);

#ifdef BROKEN
   if (equal_ustring(hcmd, "Create ELog")) {
      std::string xurl;
      status = db_get_value_string(hDB, 0, "/Elog/URL", 0, &xurl, FALSE);
      if (status == DB_SUCCESS) {
         char url[256];
         get_elog_url(url, sizeof(url));

         /*---- use external ELOG ----*/
         fsize = 100000;
         char* fbuffer = (char*)M_MALLOC(fsize);
         assert(fbuffer != NULL);

         int width = 640;
         int height = 400;

         if (equal_ustring(pmag, "Large")) {
            width = 1024;
            height = 768;
         } else if (equal_ustring(pmag, "Small")) {
            width = 320;
            height = 200;
         } else if (atoi(pmag) > 0) {
            width = atoi(pmag);
            height = 200;
         }

         printf("hereA\n");
         generate_hist_graph(odb, r, hgroup, hpanel, fbuffer, &fsize, width, height, endtime, scale, index, labels, bgcolor.c_str(), fgcolor.c_str(), gridcolor.c_str());

         /* save temporary file */
         std::string dir;
         db_get_value_string(hDB, 0, "/Elog/Logbook Dir", 0, &dir, TRUE);
         if (dir.length() > 0 && dir[dir.length()-1] != DIR_SEPARATOR)
            dir += DIR_SEPARATOR_STR;

         time_t now = time(NULL);
         localtime_r(&now, &tms);

         char file_name[256];
         sprintf(file_name, "%02d%02d%02d_%02d%02d%02d_%s.gif",
                  tms.tm_year % 100, tms.tm_mon + 1, tms.tm_mday,
                  tms.tm_hour, tms.tm_min, tms.tm_sec, hpanel); // FIXME: overflows file_name
         std::string fname = dir + file_name;

         /* save attachment */
         fh = open(fname.c_str(), O_CREAT | O_RDWR | O_BINARY, 0644);
         if (fh < 0) {
            cm_msg(MERROR, "show_hist_page", "Cannot write attachment file \"%s\", open() errno %d (%s)", fname.c_str(), errno, strerror(errno));
         } else {
            int wr = write(fh, fbuffer, fsize);
            if (wr != fsize) {
               cm_msg(MERROR, "show_hist_page", "Cannot write attachment file \"%s\", write(%d) returned %d, errno %d (%s)", fname.c_str(), fsize, wr, errno, strerror(errno));
            }
            close(fh);
         }

         /* redirect to ELOG */
         if (strlen(url) > 1 && url[strlen(url)-1] != '/')
            strlcat(url, "/", sizeof(url));
         strlcat(url, "?cmd=New&fa=", sizeof(url));
         strlcat(url, file_name, sizeof(url));
         redirect(r, url);

         M_FREE(fbuffer);
         return;

      } else {
         char str[MAX_STRING_LENGTH];
         /*---- use internal ELOG ----*/
         sprintf(str, "\\HS\\%s.gif", hpanel); // FIXME: overflows str
         if (p->getparam("hscale") && *p->getparam("hscale"))
            sprintf(str + strlen(str), "?scale=%s", p->getparam("hscale"));
         if (p->getparam("htime") && *p->getparam("htime")) {
            if (strchr(str, '?'))
               strlcat(str, "&", sizeof(str));
            else
               strlcat(str, "?", sizeof(str));
            sprintf(str + strlen(str), "time=%s", p->getparam("htime"));
         }
         //if (p->getparam("hoffset") && *p->getparam("hoffset")) {
         //   if (strchr(str, '?'))
         //      strlcat(str, "&", sizeof(str));
         //   else
         //      strlcat(str, "?", sizeof(str));
         //   sprintf(str + strlen(str), "offset=%s", p->getparam("hoffset"));
         //}
         if (p->getparam("hwidth") && *p->getparam("hwidth")) {
            if (strchr(str, '?'))
               strlcat(str, "&", sizeof(str));
            else
               strlcat(str, "?", sizeof(str));
            sprintf(str + strlen(str), "width=%s", p->getparam("hwidth"));
         }
         if (p->getparam("hindex") && *p->getparam("hindex")) {
            if (strchr(str, '?'))
               strlcat(str, "&", sizeof(str));
            else
               strlcat(str, "?", sizeof(str));
            sprintf(str + strlen(str), "index=%s", p->getparam("hindex"));
         }

         show_elog_new(r, hpanel, NULL, FALSE, str, "../../EL/");
         return;
      }
   }
#endif

   if (equal_ustring(hcmd, "Export")) {
      export_hist(odb, r, hgroup, hpanel, endtime, scale, index, labels);
      return;
   }

   if (strstr(dec_path, ".gif")) {
      int width =  640;
      int height = 400;
      if (equal_ustring(pwidth, "Large")) {
         width = 1024;
         height = 768;
      } else if (equal_ustring(pwidth, "Small")) {
         width = 320;
         height = 200;
      } else if (atoi(pwidth) > 0) {
         width = atoi(pwidth);
         if (atoi(pheight) > 0)
            height = atoi(pheight);
         else
            height = (int)(0.625 * width);
      }

      //printf("dec_path [%s], buf %p, %p, width %d, height %d, endtime %ld, scale %d, index %d, labels %d\n", dec_path, buffer, buffer_size, width, height, endtime, scale, index, labels);

      generate_hist_graph(odb, r, hgroup, hpanel, buffer, buffer_size, width, height, endtime, scale, index, labels, bgcolor.c_str(), fgcolor.c_str(), gridcolor.c_str());

      return;
   }

   if (history_mode && index < 0)
      return;

   time_t now = time(NULL);

   /* evaluate offset shift */
   if (equal_ustring(p->getparam("shift"), "leftmaxall")) {
      if (endtime == 0)
         endtime = now;
      time_t last_written = 0;
      status = get_hist_last_written(odb, hgroup, hpanel, endtime, index, 1, &last_written);
      if (status == HS_SUCCESS)
         endtime = last_written + scale/2;
   }

   if (equal_ustring(p->getparam("shift"), "leftmax")) {
      if (endtime == 0)
         endtime = now;
      time_t last_written = 0;
      status = get_hist_last_written(odb, hgroup, hpanel, endtime, index, 0, &last_written);
      if (status == HS_SUCCESS)
         if (last_written != endtime)
            endtime = last_written + scale/2;
   }

   if (equal_ustring(p->getparam("shift"), "left")) {
      if (endtime == 0)
         endtime = now;
      endtime -= scale/2;
      //offset -= scale / 2;
   }

   if (equal_ustring(p->getparam("shift"), "right")) {
      if (endtime == 0)
         endtime = now;
      endtime += scale/2;
      if (endtime > now)
         endtime = now;
   }

   if (equal_ustring(p->getparam("shift"), "rightmax")) {
      endtime = 0;
   }

   if (equal_ustring(p->getparam("shift"), "zoomin")) {
      if (endtime == 0)
         endtime = now;
      endtime -= scale / 4;
      scale /= 2;
   }

   if (equal_ustring(p->getparam("shift"), "zoomout")) {
      if (endtime == 0)
         endtime = now;
      endtime += scale / 2;
      if (endtime > now)
         endtime = now;
      scale *= 2;
   }

   int xrefresh = refresh;
   if (endtime != 0)
      xrefresh = 0;
   show_header(r, hpanel, "GET", "", xrefresh);

   r->rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   r->rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   show_navigation_bar(r, "History");

   r->rsprintf("<table class=\"mtable\">");
   r->rsprintf("<tr><th class=\"mtableheader\" colspan=2>History</th></tr>");

   {
      /* check if panel exists */
      std::string path;
      //sprintf(str, "/History/Display/%s/%s", hgroup, hpanel);
      path += "/History/Display/";
      path += hgroup;
      path += "/";
      path += hpanel;
      status = db_find_key(hDB, 0, path.c_str(), &hkey);
      if (status != DB_SUCCESS && !equal_ustring(hpanel, "All") && !equal_ustring(hpanel,"")) {
         r->rsprintf("<h1>Error: History panel \"%s\" in group \"%s\" does not exist</h1>\n", hpanel, hgroup);
         r->rsprintf("</table>\r\n");
         r->rsprintf("</div>\n"); // closing for <div id="mmain">
         r->rsprintf("</form>\n");
         r->rsprintf("</body></html>\r\n");
         return;
      }
   }

   /* define hidden field for parameters */
   if (pscale && *pscale)
      r->rsprintf("<input type=hidden name=hscale id=hscale value=%d>\n", scale);
   else {
      /* if no scale and offset given, get it from default */
      if (hpanel[0] && !equal_ustring(hpanel, "All") && hgroup[0]) {
         std::string path;
         //sprintf(str, "/History/Display/%s/%s/Timescale", hgroup, hpanel); // FIXME: overflows str
         path += "/History/Display/";
         path += hgroup;
         path += "/";
         path += hpanel;
         path += "/Timescale";

         std::string scalestr = "1h";
         status = db_get_value_string(hDB, 0, path.c_str(), 0, &scalestr, TRUE);
         if (status != DB_SUCCESS) {
            /* delete old integer key */
            db_find_key(hDB, 0, path.c_str(), &hkey);
            if (hkey)
               db_delete_key(hDB, hkey, FALSE);

            scalestr = "1h";
            db_get_value_string(hDB, 0, path.c_str(), 0, &scalestr, TRUE);
         }

         r->rsprintf("<input type=hidden name=hscale id=hscale value=%s>\n", scalestr.c_str());
         scale = time_to_sec(scalestr.c_str());
      }
   }

   if (endtime != 0)
      r->rsprintf("<input type=hidden name=htime id=htime value=%s>\n", time_to_string(endtime).c_str());
   if (pwidth && *pwidth)
      r->rsprintf("<input type=hidden name=hwidth id=hwidth value=%s>\n", pwidth);
   if (pheight && *pheight)
      r->rsprintf("<input type=hidden name=hheight id=hheight value=%s>\n", pheight);
   if (pindex && *pindex)
      r->rsprintf("<input type=hidden name=hindex id=hindex value=%s>\n", pindex);

   r->rsprintf("</td></tr>\n");

   if (hgroup[0] == 0) {
      /* "New" button */
      r->rsprintf("<tr><td colspan=2><input type=\"button\" name=\"New\" value=\"New\" ");
      r->rsprintf("onClick=\"window.location.href='?cmd=oldhistory&hcmd=New'\"></td></tr>\n");

      /* links for history panels */
      r->rsprintf("<tr><td colspan=2 style=\"text-align:left;\">\n");
      if (!hpanel[0])
         r->rsprintf("<b>Please select panel:</b><br>\n");

      /* table for panel selection */
      r->rsprintf("<table class=\"historyTable\">");

      /* "All" link */
      r->rsprintf("<tr><td colspan=2 class=\"titleCell\">\n");
      if (equal_ustring(hgroup, "All"))
         r->rsprintf("All &nbsp;&nbsp;");
      else
         r->rsprintf("<a href=\"?cmd=oldhistory&group=All\">ALL</a>\n");
      r->rsprintf("</td></tr>\n");

      /* Setup History table links */
      db_find_key(hDB, 0, "/History/Display", &hkey);
      if (!hkey) {
         /* create default panel */
         char str[256];
         strcpy(str, "System:Trigger per sec.");
         strcpy(str + 2 * NAME_LENGTH, "System:Trigger kB per sec.");
         db_set_value(hDB, 0, "/History/Display/Default/Trigger rate/Variables", str, 64, 2, TID_STRING);
         strcpy(str, "1h");
         db_set_value(hDB, 0, "/History/Display/Default/Trigger rate/Time Scale", str, NAME_LENGTH, 1, TID_STRING);

         strcpy(str, "1h");
         db_set_value(hDB, 0, "/History/Display/Default/Trigger rate/Timescale", str, NAME_LENGTH, 1, TID_STRING);
         i = 1;
         db_set_value(hDB, 0, "/History/Display/Default/Trigger rate/Zero ylow", &i, sizeof(BOOL), 1, TID_BOOL);
         i = 1;
         db_set_value(hDB, 0, "/History/Display/Default/Trigger rate/Show run markers", &i, sizeof(BOOL), 1, TID_BOOL);

         strcpy(str, "");
         db_set_value(hDB, 0, "/History/Display/Default/Trigger rate/Formula", str, 64, 1, TID_STRING);
         db_set_value_index(hDB, 0, "/History/Display/Default/Trigger rate/Formula", str, 64, 1, TID_STRING, FALSE);
      }

      db_find_key(hDB, 0, "/History/Display", &hkey);
      if (hkey) {
         for (i = 0;; i++) {
            db_enum_link(hDB, hkey, i, &hkeyp);

            if (!hkeyp)
               break;

            // Group key
            db_get_key(hDB, hkeyp, &key);

            char enc_name[256];
            strlcpy(enc_name, key.name, sizeof(enc_name));
            urlEncode(enc_name, sizeof(enc_name));

            if (equal_ustring(hpanel, key.name))
               r->rsprintf("<tr><td class=\"titleCell\">%s</td>\n<td>", key.name);
            else
               r->rsprintf("<tr><td class=\"titleCell\"><a href=\"?cmd=oldhistory&group=%s\">%s</a></td>\n<td>", enc_name, key.name);

            for (j = 0;; j++) {
               // scan items
               db_enum_link(hDB, hkeyp, j, &hikeyp);

               if (!hikeyp) {
                  r->rsprintf("</tr>");
                  break;
               }
               // Item key
               db_get_key(hDB, hikeyp, &ikey);

               char enc_iname[256];
               strlcpy(enc_iname, ikey.name, sizeof(enc_iname));
               urlEncode(enc_iname, sizeof(enc_iname));

               if (equal_ustring(hpanel, ikey.name))
                  r->rsprintf("<small><b>%s</b></small> &nbsp;", ikey.name);
               else
                  r->rsprintf("<small><a href=\"?cmd=oldhistory&group=%s&panel=%s\">%s</a></small> &nbsp;\n", enc_name, enc_iname, ikey.name);
            }
         }
      }

      r->rsprintf("</table></tr>\n");

   } else {
      int found = 0;

      /* show drop-down selectors */
      r->rsprintf("<tr><td colspan=2>\n");

      r->rsprintf("Group:\n");

      r->rsprintf("<select title=\"Select group\" id=\"fgroup\" onChange=\"window.location.search='?cmd=oldhistory&group='+document.getElementById('fgroup').value;\">\n");

      db_find_key(hDB, 0, "/History/Display", &hkey);
      if (hkey) {
         hkeyp = 0;
         for (i = 0;; i++) {
            db_enum_link(hDB, hkey, i, &hikeyp);

            if (!hikeyp)
               break;

            if (i == 0)
               hkeyp = hikeyp;

            // Group key
            db_get_key(hDB, hikeyp, &key);

            if (equal_ustring(key.name, hgroup)) {
               r->rsprintf("<option selected value=\"%s\">%s\n", key.name, key.name);
               hkeyp = hikeyp;
            } else
               r->rsprintf("<option value=\"%s\">%s\n", key.name, key.name);
         }

         if (equal_ustring("ALL", hgroup)) {
            r->rsprintf("<option selected value=\"%s\">%s\n", "ALL", "ALL");
         } else {
            r->rsprintf("<option value=\"%s\">%s\n", "ALL", "ALL");
         }

         r->rsprintf("</select>\n");
         r->rsprintf("&nbsp;&nbsp;Panel:\n");
         r->rsprintf("<select title=\"Select panel\" id=\"fpanel\" ");
         r->rsprintf("onChange=\"window.location.search='?cmd=oldhistory&group='+document.getElementById('fgroup').value+");
         r->rsprintf("'&panel='+document.getElementById('fpanel').value;\">\n");

         found = 0;
         if (hkeyp) {
            for (i = 0;; i++) {
               // scan panels
               db_enum_link(hDB, hkeyp, i, &hikeyp);

               if (!hikeyp)
                  break;

               // Item key
               db_get_key(hDB, hikeyp, &key);

               if (equal_ustring(hpanel, key.name)) {
                  r->rsprintf("<option selected value=\"%s\">%s\n", key.name, key.name);
                  found = 1;
               } else
                  r->rsprintf("<option value=\"%s\">%s\n", key.name, key.name);
            }
         }

         if (found)
            r->rsprintf("<option value=\"\">- all -\n");
         else
            r->rsprintf("<option selected value=\"\">- all -\n");

         r->rsprintf("</select>\n");
      }

      r->rsprintf("<noscript>\n");
      r->rsprintf("<input type=submit value=\"Go\">\n");
      r->rsprintf("</noscript>\n");

      r->rsprintf("&nbsp;&nbsp;<input type=\"button\" name=\"New\" value=\"New\" ");
      r->rsprintf("onClick=\"window.location.href='?cmd=oldhistory&hcmd=New&group=%s'\">\n", hgroup);

      r->rsprintf("<input type=\"button\" name=\"Cmd\" value=\"Reset\" onClick=\"window.location.href='?cmd=oldhistory&hcmd=Reset&group=%s&panel=%s'\">\n", hgroup, hpanel);

      r->rsprintf("<input type=\"button\" name=\"Cmd\" value=\"Query\" onClick=\"window.location.href='?cmd=oldhistory&hcmd=Query&group=%s&panel=%s'\">\n", hgroup, hpanel);

      double xendtime = endtime;
      if (xendtime == 0)
         xendtime = now;
      double xstarttime = xendtime - scale;
      
      r->rsprintf("<input type=\"button\" name=\"Cmd\" value=\"New history\" onClick=\"window.location.href='?cmd=history&group=%s&panel=%s&A=%.0f&B=%.0f'\">\n", hgroup, hpanel, xstarttime, xendtime);

      r->rsprintf("</td></tr>\n");
   }

   //printf("hgroup [%s] hpanel [%s]\n", hgroup, hpanel);

   /* check if whole group should be displayed */
   if (hgroup[0] && !equal_ustring(hgroup, "ALL") && hpanel[0] == 0) {
      std::string strwidth = "Small";
      db_get_value_string(hDB, 0, "/History/Display Settings/Width Group", 0, &strwidth, TRUE);

      std::string path;
      //sprintf(str, "/History/Display/%s", hgroup); // FIXME: overflows str
      path += "/History/Display/";
      path += hgroup;
      db_find_key(hDB, 0, path.c_str(), &hkey);
      if (hkey) {
         for (i = 0 ;; i++) {     // scan group
            db_enum_link(hDB, hkey, i, &hikeyp);

            if (!hikeyp)
               break;

            db_get_key(hDB, hikeyp, &key);

            char enc_name[256];
            strlcpy(enc_name, key.name, sizeof(enc_name));
            urlEncode(enc_name, sizeof(enc_name));

            std::string ref;
            ref += "graph.gif?width=";
            ref += strwidth;
            ref += "&cmd=oldhistory&group=";
            ref += hgroup;
            ref += "&panel=";
            ref += enc_name;

            std::string ref2;
            ref2 += "?cmd=oldhistory&group=";
            ref2 += hgroup;
            ref2 += "&panel=";
            ref2 += enc_name;

            if (endtime != 0) {
               char tmp[256];
               sprintf(tmp, "time=%s&scale=%d", time_to_string(endtime).c_str(), scale);
               ref += "&";
               ref += tmp;
               ref2 += "?";
               ref2 += tmp;
            }

            if (i % 2 == 0)
               r->rsprintf("<tr><td><a href=\"%s\"><img src=\"%s\"></a>\n", ref2.c_str(), ref.c_str());
            else
               r->rsprintf("<td><a href=\"%s\"><img src=\"%s\"></a></tr>\n", ref2.c_str(), ref.c_str());
         }

      } else {
         r->rsprintf("Group \"%s\" not found", hgroup);
      }
   }

   /* image panel */
   else if (hpanel[0] && !equal_ustring(hpanel, "All")) {
      /* navigation links */
      r->rsprintf("<tr><td>\n");

      std::string path;
      //sprintf(str, "/History/Display/%s/%s/Buttons", hgroup, hpanel); // FIXME: overflow str
      path += "/History/Display/";
      path += hgroup;
      path += "/";
      path += hpanel;
      path += "/Buttons";
      db_find_key(hDB, 0, path.c_str(), &hkeybutton);
      if (hkeybutton == 0) {
         /* create default buttons */
         db_create_key(hDB, 0, path.c_str(), TID_STRING);
         status = db_find_key(hDB, 0, path.c_str(), &hkeybutton);
         if (status != DB_SUCCESS || !hkey) {
            cm_msg(MERROR, "show_hist_page", "Cannot create history panel with invalid ODB path \"%s\"", path.c_str());
            return;
         }
         db_set_data(hDB, hkeybutton, def_button, sizeof(def_button), 7, TID_STRING);
      }

      r->rsprintf("<script>\n");
      r->rsprintf("function histDisp(p) {\n");
      r->rsprintf("  var params = '?cmd=oldhistory&group=%s&panel=%s';\n", hgroup, hpanel);
      r->rsprintf("  params += '&'+p;\n");
      r->rsprintf("  if (document.getElementById(\'hscale\') !== null)\n");
      r->rsprintf("    params += '&hscale='+document.getElementById(\'hscale\').value;\n");
      r->rsprintf("  if (document.getElementById(\'htime\') !== null)\n");
      r->rsprintf("    params += '&htime='+document.getElementById(\'htime\').value;\n");
      r->rsprintf("  if (document.getElementById(\'hwdith\') !== null)\n");
      r->rsprintf("    params += '&hwidth='+document.getElementById(\'hwidth\').value;\n");
      r->rsprintf("  if (document.getElementById(\'hindex\') !== null)\n");
      r->rsprintf("    params += '&hindex='+document.getElementById(\'hindex\').value;\n");
      r->rsprintf("  window.location.search = params;\n");
      r->rsprintf("}\n\n");
      r->rsprintf("</script>\n");

      db_get_key(hDB, hkeybutton, &key);

      for (i = 0; i < key.num_values; i++) {
         char str[256];
         size = sizeof(str);
         db_get_data_index(hDB, hkeybutton, str, &size, i, TID_STRING);
         r->rsprintf("<input type=\"button\" title=\"display last %s\" value=%s onclick=\"histDisp('scale=%s')\">\n", str, str, str);
      }

      r->rsprintf("<input type=\"button\" value=\"<<<\" title=\"go back in time to last available data for all variables on the plot\" onclick=\"histDisp('shift=leftmaxall')\">");
      r->rsprintf("<input type=\"button\" value=\"<<\" title=\"go back in time to last available data\" onclick=\"histDisp('shift=leftmax')\">");
      r->rsprintf("<input type=\"button\" value=\"<\" title=\"go back in time\" onclick=\"histDisp('shift=left')\">");

      r->rsprintf("<input type=\"button\" value=\" + \" title=\"zoom in\" onclick=\"histDisp('shift=zoomin')\">");
      r->rsprintf("<input type=\"button\" value=\" - \" title=\"zoom out\" onclick=\"histDisp('shift=zoomout')\">");

      if (endtime != 0) {
         r->rsprintf("<input type=\"button\" value=\">\" title=\"go forward in time\" onclick=\"histDisp('shift=right')\">");
         r->rsprintf("<input type=\"button\" value=\">>\" title=\"go to currently updated fresh data\" onclick=\"histDisp('shift=rightmax')\">");
      }

      r->rsprintf("<td>\n");
      r->rsprintf("<input type=\"button\" value=\"Large\" title=\"large display\" onclick=\"histDisp('width=Large')\">\n");
      r->rsprintf("<input type=\"button\" value=\"Small\" title=\"large display\" onclick=\"histDisp('width=Small')\">\n");
      r->rsprintf("<input type=\"button\" value=\"Create Elog\" title=\"large display\" onclick=\"histDisp('hcmd=Create Elog')\">\n");
      r->rsprintf("<input type=\"button\" value=\"Config\" title=\"large display\" onclick=\"histDisp('hcmd=Config')\">\n");
      r->rsprintf("<input type=\"button\" value=\"Export\" title=\"large display\" onclick=\"histDisp('hcmd=Export')\">\n");
      r->rsprintf("</tr>\n");

      char paramstr[256];

      paramstr[0] = 0;
      sprintf(paramstr + strlen(paramstr), "&scale=%d", scale);
      if (endtime != 0)
         sprintf(paramstr + strlen(paramstr), "&time=%s", time_to_string(endtime).c_str());
      if (pwidth && *pwidth)
         sprintf(paramstr + strlen(paramstr), "&width=%s", pwidth);
      else {
         std::string wi = "640";
         db_get_value_string(hDB, 0, "/History/Display Settings/Width Individual", 0, &wi, TRUE);
         sprintf(paramstr + strlen(paramstr), "&width=%s", wi.c_str());
      }
      if (pheight && *pheight)
         sprintf(paramstr + strlen(paramstr), "&height=%s", pheight);

      /* define image map */
      r->rsprintf("<map name=\"%s\">\r\n", hpanel);

      if (!(pindex && *pindex)) {
         std::string path;
         //sprintf(str, "/History/Display/%s/%s/Variables", hgroup, hpanel); // FIXME: overflows str
         path += "/History/Display/";
         path += hgroup;
         path += "/";
         path += hpanel;
         path += "/Variables";
         db_find_key(hDB, 0, path.c_str(), &hkey);
         if (hkey) {
            db_get_key(hDB, hkey, &key);

            for (i = 0; i < key.num_values; i++) {
               std::string ref;
               //if (paramstr[0]) {
               //   sprintf(ref, "?cmd=oldhistory&group=%s&panel=%s&%s&index=%d", hgroup, hpanel, paramstr, i);
               //} else {
               //   sprintf(ref, "?cmd=oldhistory&group=%s&panel=%s&index=%d", hgroup, hpanel, i);
               //}

               ref += "?cmd=oldhistory&group=";
               ref += hgroup;
               ref += "&panel=";
               ref += hpanel;
               if (paramstr[0]) {
                  ref += "&";
                  ref += paramstr;
               }
               ref += "&index=";
               ref += toString(i);

               r->rsprintf("  <area shape=rect coords=\"%d,%d,%d,%d\" href=\"%s\">\r\n", 30, 31 + 23 * i, 150, 30 + 23 * i + 17, ref.c_str());
            }
         }
      } else {
         std::string ref = "?cmd=oldhistory&group=";
         ref += hgroup;
         ref += "&panel=";
         ref += hpanel;

         if (paramstr[0]) {
            ref += "&";
            ref += paramstr;
         }

         if (equal_ustring(pwidth, "Large"))
            width = 1024;
         else if (equal_ustring(pwidth, "Small"))
            width = 320;
         else if (atoi(pwidth) > 0)
            width = atoi(pwidth);
         else
            width = 640;

         r->rsprintf("  <area shape=rect coords=\"%d,%d,%d,%d\" href=\"%s\">\r\n", 0, 0, width, 20, ref.c_str());
      }

      r->rsprintf("</map>\r\n");

      /* Display individual panels */
      if (pindex && *pindex)
         sprintf(paramstr + strlen(paramstr), "&index=%s", pindex);

      std::string ref;
      //sprintf(ref, "graph.gif?cmd=oldhistory&group=%s&panel=%s%s", hgroup, hpanel, paramstr);
      ref += "graph.gif?cmd=oldhistory&group=";
      ref += hgroup;
      ref += "&panel=";
      ref += hpanel;
      ref += paramstr;

      /* put reference to graph */
      r->rsprintf("<tr><td colspan=2><img src=\"%s\" usemap=\"#%s\"></tr>\n", ref.c_str(), hpanel);
   }

   else if (equal_ustring(hgroup, "All")) {
      /* Display all panels */
      db_find_key(hDB, 0, "/History/Display", &hkey);
      if (hkey)
         for (i = 0, k = 0;; i++) {     // scan Groups
            db_enum_link(hDB, hkey, i, &hkeyp);

            if (!hkeyp)
               break;

            db_get_key(hDB, hkeyp, &key);

            char enc_group_name[256];
            strlcpy(enc_group_name, key.name, sizeof(enc_group_name));
            urlEncode(enc_group_name, sizeof(enc_group_name));

            for (j = 0;; j++, k++) {
               // scan items
               db_enum_link(hDB, hkeyp, j, &hikeyp);

               if (!hikeyp)
                  break;

               db_get_key(hDB, hikeyp, &ikey);

               char enc_panel_name[256];
               strlcpy(enc_panel_name, ikey.name, sizeof(enc_panel_name));
               urlEncode(enc_panel_name, sizeof(enc_panel_name));

               std::string ref;
               ref += "graph.gif?width=Small";
               ref += "&cmd=oldhistory&group=";
               ref += enc_group_name;
               ref += "&panel=";
               ref += enc_panel_name;

               std::string ref2;
               ref2 += "?cmd=oldhistory&group=";
               ref2 += enc_group_name;
               ref2 += "&panel=";
               ref2 += enc_panel_name;

               if (endtime != 0) {
                  char tmp[256];
                  sprintf(tmp, "time=%s&scale=%d", time_to_string(endtime).c_str(), scale);
                  ref += "&";
                  ref += tmp;
                  ref2 += "&";
                  ref2 += tmp;
               }

               if (k % 2 == 0)
                  r->rsprintf("<tr><td><a href=\"%s\"><img src=\"%s\"></a>\n", ref2.c_str(), ref.c_str());
               else
                  r->rsprintf("<td><a href=\"%s\"><img src=\"%s\"></a></tr>\n", ref2.c_str(), ref.c_str());
            }                   // items loop
         }                      // Groups loop
   }                            // All
   r->rsprintf("</table>\r\n");
   r->rsprintf("</div>\n"); // closing for <div id="mmain">
   r->rsprintf("</form>\n");
   r->rsprintf("</body></html>\r\n");
}


/*------------------------------------------------------------------*/

void send_icon(Return* r, const char *icon)
{
   int length;
   const unsigned char *picon;
   char str[256], format[256];
   time_t now;

   if (strstr(icon, "favicon.ico") != 0) {
      length = sizeof(favicon_ico);
      picon = favicon_ico;
   } else if (strstr(icon, "favicon.png") != 0) {
      length = sizeof(favicon_png);
      picon = favicon_png;
   } else
      return;

   r->rsprintf("HTTP/1.1 200 Document follows\r\n");
   r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
   r->rsprintf("Accept-Ranges: bytes\r\n");

   /* set expiration time to one day */
   time(&now);
   now += (int) (3600 * 24);
   struct tm gmt_tms;
   gmtime_r(&now, &gmt_tms);
   strcpy(format, "%A, %d-%b-%y %H:%M:%S GMT");
   strftime(str, sizeof(str), format, &gmt_tms);
   r->rsprintf("Expires: %s\r\n", str);

   if (equal_ustring(icon, "favicon.ico"))
      r->rsprintf("Content-Type: image/x-icon\r\n");
   else
      r->rsprintf("Content-Type: image/png\r\n");

   r->rsprintf("Content-Length: %d\r\n\r\n", length);

   r->rmemcpy(picon, length);
}

/*------------------------------------------------------------------*/

#define XNAME_LENGTH 256

static PMXML_NODE pnseq;

/*------------------------------------------------------------------*/

int strbreak(char *str, char list[][XNAME_LENGTH], int size, const char *brk, BOOL ignore_quotes)
/* break comma-separated list into char array, stripping leading
 and trailing blanks */
{
   int i, j;
   char *p;

   memset(list, 0, size * XNAME_LENGTH);
   p = str;
   if (!p || !*p)
      return 0;

   while (*p == ' ')
      p++;

   for (i = 0; *p && i < size; i++) {
      if (*p == '"' && !ignore_quotes) {
         p++;
         j = 0;
         memset(list[i], 0, XNAME_LENGTH);
         do {
            /* convert two '"' to one */
            if (*p == '"' && *(p + 1) == '"') {
               list[i][j++] = '"';
               p += 2;
            } else if (*p == '"') {
               break;
            } else
               list[i][j++] = *p++;

         } while (j < XNAME_LENGTH - 1);
         list[i][j] = 0;

         /* skip second '"' */
         p++;

         /* skip blanks and break character */
         while (*p == ' ')
            p++;
         if (*p && strchr(brk, *p))
            p++;
         while (*p == ' ')
            p++;

      } else {
         strlcpy(list[i], p, XNAME_LENGTH);

         for (j = 0; j < (int) strlen(list[i]); j++)
            if (strchr(brk, list[i][j])) {
               list[i][j] = 0;
               break;
            }

         p += strlen(list[i]);
         while (*p == ' ')
            p++;
         if (*p && strchr(brk, *p))
            p++;
         while (*p == ' ')
            p++;

         while (list[i][strlen(list[i]) - 1] == ' ')
            list[i][strlen(list[i]) - 1] = 0;
      }

      if (!*p)
         break;
   }

   if (i == size)
      return size;

   return i + 1;
}

/*------------------------------------------------------------------*/

void strsubst(char *string, int size, const char *pattern, const char *subst)
/* subsitute "pattern" with "subst" in "string" */
{
   char *tail, *p;
   int s;

   p = string;
   for (p = stristr(p, pattern); p != NULL; p = stristr(p, pattern)) {

      if (strlen(pattern) == strlen(subst)) {
         memcpy(p, subst, strlen(subst));
      } else if (strlen(pattern) > strlen(subst)) {
         memcpy(p, subst, strlen(subst));
         memmove(p + strlen(subst), p + strlen(pattern), strlen(p + strlen(pattern)) + 1);
      } else {
         tail = (char *) malloc(strlen(p) - strlen(pattern) + 1);
         strcpy(tail, p + strlen(pattern));
         s = size - (p - string);
         strlcpy(p, subst, s);
         strlcat(p, tail, s);
         free(tail);
         tail = NULL;
      }

      p += strlen(subst);
   }
}

/*------------------------------------------------------------------*/

BOOL msl_parse(const char *filename, char *error, int error_size, int *error_line)
{
   char str[256], *buf, *pl, *pe;
   char list[100][XNAME_LENGTH], list2[100][XNAME_LENGTH], **lines;
   int i, j, n, size, n_lines, endl, line, fhin, nest, incl, library;
   FILE *fout = NULL;
   char* msl_include, *include_error;
   int include_error_size;
   BOOL include_status;

   fhin = open(filename, O_RDONLY | O_TEXT);

   if (fhin < 0) {
      sprintf(error, "Cannot read sequencer file \"%s\", errno %d (%s)", filename, errno, strerror(errno)); // FIXME: overflows "error"
      return FALSE;
   }

   if (strchr(filename, '.')) {
      strlcpy(str, filename, sizeof(str));
      *strchr(str, '.') = 0;
      strlcat(str, ".xml", sizeof(str));
      fout = fopen(str, "wt");

      if (fout == NULL) {
         sprintf(error, "Cannot write to sequencer XML file \"%s\", errno %d (%s)", str, errno, strerror(errno)); // FIXME: overflows "error"
         return FALSE;
      }
   }

   if (fhin > 0 && fout) {
      size = (int)lseek(fhin, 0, SEEK_END);
      lseek(fhin, 0, SEEK_SET);
      buf = (char *)malloc(size+1);
      size = (int)read(fhin, buf, size);
      buf[size] = 0;
      close(fhin);

      /* look for any includes */
      lines = (char **)malloc(sizeof(char *));
      incl = 0;
      pl = buf;
      library = FALSE;
      for (n_lines=0 ; *pl ; n_lines++) {
         lines = (char **)realloc(lines, sizeof(char *)*n_lines+1);
         lines[n_lines] = pl;
         if (strchr(pl, '\n')) {
            pe = strchr(pl, '\n');
            *pe = 0;
            if (*(pe-1) == '\r') {
               *(pe-1) = 0;
            }
            pe++;
         } else
            pe = pl+strlen(pl);
         strlcpy(str, pl, sizeof(str));
         pl = pe;
         strbreak(str, list, 100, ", ", FALSE);
         if (equal_ustring(list[0], "include")) {
            if (!incl) {
               fprintf(fout, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n");
               fprintf(fout, "<!DOCTYPE RunSequence [\n");
               incl = 1;
            }

            //use filename as enity reference, not full path
            char *reference = strrchr(list[1], '/');
            if(reference)
               reference++;
            else
               reference = list[1];

            fprintf(fout, "  <!ENTITY %s SYSTEM \"%s.xml\">\n", reference, list[1]);
            //recurse
            size = strlen(list[1]) + 1 + 4;
            msl_include = (char*)malloc(size);
            strlcpy(msl_include, list[1], size);
            strlcat(msl_include, ".msl", size);

            strlcpy(error, "Including file ", error_size);
            strlcat(error, msl_include, error_size);
            strlcat(error, ", ", error_size);
            include_error = error + strlen(error);
            include_error_size = error_size - strlen(error);

            include_status = msl_parse(msl_include, include_error, include_error_size, error_line);
               free(msl_include);

            if(!include_status){
               //report the errror on CALL line instead of the one in included file
               *error_line = n_lines+1;
               return FALSE;
            }
         }
         if (equal_ustring(list[0], "library")) {
            fprintf(fout, "<Library name=\"%s\">\n", list[1]);
            library = TRUE;
         }
      }
      if (incl)
         fprintf(fout, "]>\n");
      else if (!library)
         fprintf(fout, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n");

      /* parse rest of file */
      if (!library)
         fprintf(fout, "<RunSequence xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:noNamespaceSchemaLocation=\"\">\n");
      for (line=0 ; line<n_lines ; line++) {
         n = strbreak(lines[line], list, 100, ", ", FALSE);

         /* remove any comment */
         for (i=0 ; i<n ; i++) {
            if (list[i][0] == '#') {
               for (j=i ; j<n ; j++)
                  list[j][0] = 0;
               break;
            }
         }

         if (equal_ustring(list[0], "library")) {

         } else if (equal_ustring(list[0], "include")) {
            //use filename as enity reference, not full path
            char *reference = strrchr(list[1], '/');
            if(reference)
               reference++;
            else
               reference = list[1];

            fprintf(fout, "&%s;\n", reference);

         } else if (equal_ustring(list[0], "call")) {
            fprintf(fout, "<Call l=\"%d\" name=\"%s\">", line+1, list[1]);
            for (i=2 ; i < 100 && list[i][0] ; i++) {
               if (i > 2)
                  fprintf(fout, ",");
               fprintf(fout, "%s", list[i]);
            }
            fprintf(fout, "</Call>\n");

         } else if (equal_ustring(list[0], "cat")) {
            fprintf(fout, "<Cat l=\"%d\" name=\"%s\">", line+1, list[1]);
            for (i=2 ; i < 100 && list[i][0] ; i++) {
               if (i > 2)
                  fprintf(fout, ",");
               fprintf(fout, "\"%s\"", list[i]);
            }
            fprintf(fout, "</Cat>\n");

         } else if (equal_ustring(list[0], "comment")) {
            fprintf(fout, "<Comment l=\"%d\">%s</Comment>\n", line+1, list[1]);

         } else if (equal_ustring(list[0], "goto")) {
            fprintf(fout, "<Goto l=\"%d\" sline=\"%s\" />\n", line+1, list[1]);

         } else if (equal_ustring(list[0], "if")) {
            fprintf(fout, "<If l=\"%d\" condition=\"", line+1);
            for (i=1 ; i<100 && list[i][0] && stricmp(list[i], "THEN") != 0 ; i++)
               fprintf(fout, "%s", list[i]);
            fprintf(fout, "\">\n");

         } else if (equal_ustring(list[0], "else")) {
            fprintf(fout, "<Else />\n");

         } else if (equal_ustring(list[0], "endif")) {
            fprintf(fout, "</If>\n");

         } else if (equal_ustring(list[0], "loop")) {
            /* find end of loop */
            for (i=line,nest=0 ; i<n_lines ; i++) {
               strbreak(lines[i], list2, 100, ", ", FALSE);
               if (equal_ustring(list2[0], "loop"))
                  nest++;
               if (equal_ustring(list2[0], "endloop")) {
                  nest--;
                  if (nest == 0)
                     break;
               }
            }
            if (i<n_lines)
               endl = i+1;
            else
               endl = line+1;
            if (list[2][0] == 0)
               fprintf(fout, "<Loop l=\"%d\" le=\"%d\" n=\"%s\">\n", line+1, endl, list[1]);
            else if (list[3][0] == 0){
               fprintf(fout, "<Loop l=\"%d\" le=\"%d\" var=\"%s\" n=\"%s\">\n", line+1, endl, list[1], list[2]);
            } else {
               fprintf(fout, "<Loop l=\"%d\" le=\"%d\" var=\"%s\" values=\"", line+1, endl, list[1]);
               for (i=2 ; i < 100 && list[i][0] ; i++) {
                  if (i > 2)
                     fprintf(fout, ",");
                  fprintf(fout, "%s", list[i]);
               }
               fprintf(fout, "\">\n");
            }
         } else if (equal_ustring(list[0], "endloop")) {
            fprintf(fout, "</Loop>\n");

         } else if (equal_ustring(list[0], "message")) {
            fprintf(fout, "<Message l=\"%d\"%s>%s</Message>\n", line+1,
                    list[2][0] == '1'? " wait=\"1\"" : "", list[1]);

         } else if (equal_ustring(list[0], "odbinc")) {
            if (list[2][0] == 0)
               strlcpy(list[2], "1", 2);
            fprintf(fout, "<ODBInc l=\"%d\" path=\"%s\">%s</ODBInc>\n", line+1, list[1], list[2]);

         } else if (equal_ustring(list[0], "odbset")) {
            if (list[3][0])
               fprintf(fout, "<ODBSet l=\"%d\" notify=\"%s\" path=\"%s\">%s</ODBSet>\n", line+1, list[3], list[1], list[2]);
            else
               fprintf(fout, "<ODBSet l=\"%d\" path=\"%s\">%s</ODBSet>\n", line+1, list[1], list[2]);

         } else if (equal_ustring(list[0], "odbload")) {
            if (list[2][0])
               fprintf(fout, "<ODBLoad l=\"%d\" path=\"%s\">%s</ODBLoad>\n", line+1, list[2], list[1]);
            else
               fprintf(fout, "<ODBLoad l=\"%d\">%s</ODBLoad>\n", line+1, list[1]);

         } else if (equal_ustring(list[0], "odbget")) {
            fprintf(fout, "<ODBGet l=\"%d\" path=\"%s\">%s</ODBGet>\n", line+1, list[1], list[2]);

         } else if (equal_ustring(list[0], "odbsubdir")) {
            if (list[2][0])
               fprintf(fout, "<ODBSubdir l=\"%d\" notify=\"%s\" path=\"%s\">\n", line+1, list[2], list[1]);
            else
               fprintf(fout, "<ODBSubdir l=\"%d\" path=\"%s\">\n", line+1, list[1]);
         } else if (equal_ustring(list[0], "endodbsubdir")) {
            fprintf(fout, "</ODBSubdir>\n");

         } else if (equal_ustring(list[0], "param")) {
            if (list[2][0] == 0)
               fprintf(fout, "<Param l=\"%d\" name=\"%s\" />\n", line+1, list[1]);
            else if (!list[3][0] && equal_ustring(list[2], "bool")) {
               fprintf(fout, "<Param l=\"%d\" name=\"%s\" type=\"bool\" />\n", line+1, list[1]);
            } else if (!list[3][0]) {
               fprintf(fout, "<Param l=\"%d\" name=\"%s\" comment=\"%s\" />\n", line+1, list[1], list[2]);
            } else {
               fprintf(fout, "<Param l=\"%d\" name=\"%s\" comment=\"%s\" options=\"", line+1, list[1], list[2]);
               for (i=3 ; i < 100 && list[i][0] ; i++) {
                  if (i > 3)
                     fprintf(fout, ",");
                  fprintf(fout, "%s", list[i]);
               }
               fprintf(fout, "\" />\n");
            }

         } else if (equal_ustring(list[0], "rundescription")) {
            fprintf(fout, "<RunDescription l=\"%d\">%s</RunDescription>\n", line+1, list[1]);

         } else if (equal_ustring(list[0], "script")) {
            if (list[2][0] == 0)
               fprintf(fout, "<Script l=\"%d\">%s</Script>\n", line+1, list[1]);
            else {
               fprintf(fout, "<Script l=\"%d\" params=\"", line+1);
               for (i=2 ; i < 100 && list[i][0] ; i++) {
                  if (i > 2)
                     fprintf(fout, ",");
                  fprintf(fout, "%s", list[i]);
               }
               fprintf(fout, "\">%s</Script>\n", list[1]);
            }

         } else if (equal_ustring(list[0], "set")) {
            fprintf(fout, "<Set l=\"%d\" name=\"%s\">%s</Set>\n", line+1, list[1], list[2]);

         } else if (equal_ustring(list[0], "subroutine")) {
            fprintf(fout, "\n<Subroutine l=\"%d\" name=\"%s\">\n", line+1, list[1]);
         } else if (equal_ustring(list[0], "endsubroutine")) {
            fprintf(fout, "</Subroutine>\n");

         } else if (equal_ustring(list[0], "transition")) {
            fprintf(fout, "<Transition l=\"%d\">%s</Transition>\n", line+1, list[1]);

         } else if (equal_ustring(list[0], "wait")) {
            if (!list[2][0])
               fprintf(fout, "<Wait l=\"%d\" for=\"seconds\">%s</Wait>\n", line+1, list[1]);
            else if (!list[3][0])
               fprintf(fout, "<Wait l=\"%d\" for=\"%s\">%s</Wait>\n", line+1, list[1], list[2]);
            else {
               fprintf(fout, "<Wait l=\"%d\" for=\"%s\" path=\"%s\" op=\"%s\">%s</Wait>\n",
                       line+1, list[1], list[2], list[3], list[4]);
            }

         } else if (list[0][0] == 0 || list[0][0] == '#'){
            /* skip empty or outcommented lines */
         } else {
            sprintf(error, "Invalid command \"%s\"", list[0]);
            *error_line = line + 1;
            return FALSE;
         }
      }

      free(lines);
      free(buf);
      if (library)
         fprintf(fout, "\n</Library>\n");
      else
         fprintf(fout, "</RunSequence>\n");
      fclose(fout);
   } else {
      // WE NEVER COME HERE
      abort();
      return FALSE;
   }

   return TRUE;
}

void seq_start_page(Param* p, Return* r)
{
   int line, i, n, no, size, last_line, status, maxlength;
   HNDLE hDB, hkey, hsubkey, hkeycomm, hkeyc;
   KEY key;
   char data[1000], str[256], name[32];
   char data_str[256], comment[1000], list[100][XNAME_LENGTH];
   MXML_NODE *pn;

   cm_get_experiment_database(&hDB, NULL);

   show_header(r, "Start sequence", "GET", "", 0);

   r->rsprintf("<table class=\"dialogTable\">");  //main table

   r->rsprintf("<tr><th colspan=2 class=\"subStatusTitle\" style=\"border:2px solid #FFFFFF\">Start script</th>\n");

   if (!pnseq) {
      r->rsprintf("<tr><td colspan=2 align=\"center\" class=\"redLight\"><b>Error in XML script</b></td></tr>\n");
      r->rsprintf("</table>\n");
      r->rsprintf("</div>\n"); // closing for <div id="mmain">
      r->rsprintf("</form>\n");
      r->rsprintf("</body></html>\r\n");
      return;
   }

   /* run parameters from ODB */
   db_find_key(hDB, 0, "/Experiment/Edit on sequence", &hkey);
   db_find_key(hDB, 0, "/Experiment/Parameter Comments", &hkeycomm);
   n = 0;
   if (hkey) {
      for (line = 0 ;; line++) {
         db_enum_link(hDB, hkey, line, &hsubkey);

         if (!hsubkey)
            break;

         db_get_link(hDB, hsubkey, &key);
         strlcpy(str, key.name, sizeof(str));

         if (equal_ustring(str, "Edit run number"))
            continue;

         db_enum_key(hDB, hkey, line, &hsubkey);
         db_get_key(hDB, hsubkey, &key);

         size = sizeof(data);
         status = db_get_data(hDB, hsubkey, data, &size, key.type);
         if (status != DB_SUCCESS)
            continue;

         for (i = 0; i < key.num_values; i++) {
            if (key.num_values > 1)
               r->rsprintf("<tr><td>%s [%d]", str, i);
            else
               r->rsprintf("<tr><td>%s", str);

            if (i == 0 && hkeycomm) {
               /* look for comment */
               if (db_find_key(hDB, hkeycomm, key.name, &hkeyc) == DB_SUCCESS) {
                  size = sizeof(comment);
                  if (db_get_data(hDB, hkeyc, comment, &size, TID_STRING) == DB_SUCCESS)
                     r->rsprintf("<br>%s\n", comment);
               }
            }

            db_sprintf(data_str, data, key.item_size, i, key.type);

            maxlength = 80;
            if (key.type == TID_STRING)
               maxlength = key.item_size;

            if (key.type == TID_BOOL) {
               if (((DWORD*)data)[i]) {
                  r->rsprintf("<td><input type=checkbox checked name=x%d value=1></td></tr>\n", n++);
               } else {
                  r->rsprintf("<td><input type=checkbox name=x%d value=1></td></tr>\n", n++);
               }
            } else {
               r->rsprintf("<td><input type=text size=%d maxlength=%d name=x%d value=\"%s\"></tr>\n",
                        (maxlength<80)?maxlength:80, maxlength-1, n++, data_str);
            }
         }
      }
   }

   /* parameters from script */
   pn = mxml_find_node(pnseq, "RunSequence");
   if (pn) {
      last_line = mxml_get_line_number_end(pn);

      for (line=1 ; line<last_line ; line++){
         pn = mxml_get_node_at_line(pnseq, line);
         if (!pn)
            continue;

         if (equal_ustring(mxml_get_name(pn), "Param")) {
            strlcpy(name, mxml_get_attribute(pn, "name"), sizeof(name));

            r->rsprintf("<tr><td>%s", name);
            if (mxml_get_attribute(pn, "comment")) {
               r->rsprintf("<br>%s\n", mxml_get_attribute(pn, "comment"));
            }

            size = sizeof(data_str);
            sprintf(str, "/Sequencer/Variables/%s", name);
            data_str[0] = 0;
            db_get_value(hDB, 0, str, data_str, &size, TID_STRING, FALSE);

            if (mxml_get_attribute(pn, "options")) {
               strlcpy(data, mxml_get_attribute(pn, "options"), sizeof(data));
               no = strbreak(mxml_get_attribute(pn, "options"), list, 100, ",", FALSE);
               r->rsprintf("<td><select name=x%d>\n", n++);
               for (i=0 ; i<no ; i++) {
                  if (stricmp(list[i], data_str)==0) {
                     r->rsprintf("<option selected>%s</option>\n", list[i]);
                  } else {
                     r->rsprintf("<option>%s</option>\n", list[i]);
                  }
               }
               r->rsprintf("</select></td></tr>\n");
               //printf("opt param [%s] option [%s] [%s]\n", name, list[0], data_str);
            } else if (mxml_get_attribute(pn, "type") && equal_ustring(mxml_get_attribute(pn, "type"), "bool")) {
               if (data_str[0] == '1') {
                  r->rsprintf("<td><input type=checkbox checked name=x%d value=1></tr>\n", n++);
                  //printf("bool param [%s] value true\n", name);
               } else {
                  r->rsprintf("<td><input type=checkbox name=x%d value=1></tr>\n", n++);
                  //printf("bool param [%s] value false\n", name);
               }
            } else {
               r->rsprintf("<td><input type=text name=x%d value=\"%s\"></tr>\n", n++, data_str);
               //printf("string param [%s] value [%s]\n", name, data_str);
            }
         }

      }
   }

   r->rsprintf("<tr><td align=center colspan=2>\n");
   r->rsprintf("<input type=submit name=cmd value=\"Start Script\">\n");
   r->rsprintf("<input type=hidden name=params value=1>\n");
   r->rsprintf("<input type=submit name=cmd value=\"Cancel Script\">\n");
   r->rsprintf("</tr>\n");
   r->rsprintf("</table>\n");

   if (p->isparam("redir"))
      r->rsprintf("<input type=hidden name=\"redir\" value=\"%s\">\n", p->getparam("redir"));

   r->rsprintf("</div>\n"); // closing for <div id="mmain">
   r->rsprintf("</form>\n");
   r->rsprintf("</body></html>\r\n");
}

/*------------------------------------------------------------------*/

#ifdef OLD_SEQUENCER

static const char* const bar_col[] = {"#B0B0FF", "#C0C0FF", "#D0D0FF", "#E0E0FF"};
static const char* const call_col[] = {"#B0FFB0", "#C0FFC0", "#D0FFD0", "#E0FFE0"};

static void seq_watch(HNDLE hDB, HNDLE hKeyChanged, int index, void* info)
{
   int status;
   HNDLE hKey;
   SEQUENCER seq;
   SEQUENCER_STR(sequencer_str);

   cm_get_experiment_database(&hDB, NULL);

   status = db_find_key(hDB, 0, "/Sequencer/State", &hKey);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "seq_watch", "Cannot find /Sequencer/State in ODB, db_find_key() status %d", status);
      return;
   }

   int size = sizeof(seq);
   status = db_get_record1(hDB, hKey, &seq, &size, 0, strcomb1(sequencer_str).c_str());
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "seq_watch", "Cannot get /Sequencer/State from ODB, db_get_record1() status %d", status);
      return;
   }
}

void init_sequencer(MVOdb* odb)
{
   int status;
   HNDLE hDB;
   HNDLE hKey;
   char str[256];
   SEQUENCER seq;
   SEQUENCER_STR(sequencer_str);

   cm_get_experiment_database(&hDB, NULL);

   status = db_check_record(hDB, 0, "/Sequencer/State", strcomb1(sequencer_str).c_str(), TRUE);
   if (status == DB_STRUCT_MISMATCH) {
      cm_msg(MERROR, "init_sequencer", "Sequencer error: mismatching /Sequencer/State structure, db_check_record() status %d", status);
      return;
   }

   bool b = false;
   odb->RB("Sequencer/Command/Start script", &b, true);
   b = false;
   odb->RB("Sequencer/Command/Stop immediately", &b, true);
   b = false;
   odb->RB("Sequencer/Command/Load new file", &b, true);
   std::string s;
   odb->RS("Sequencer/Command/Load filename", &s, true);

   status = db_find_key(hDB, 0, "/Sequencer/State", &hKey);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "init_sequencer", "Sequencer error: Cannot find /Sequencer/State, db_find_key() status %d", status);
      return;
   }

   int size = sizeof(seq);
   status = db_get_record1(hDB, hKey, &seq, &size, 0, strcomb1(sequencer_str).c_str());
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "init_sequencer", "Sequencer error: Cannot get /Sequencer/State, db_get_record1() status %d", status);
      return;
   }

   if (seq.path[0] == 0) {
      // NOTE: this code must match identical code in msequencer!
      const char* s = getenv("MIDASSYS");
      if (s) {
         strlcpy(seq.path, s, sizeof(seq.path));
         strlcat(seq.path, "/examples/sequencer/", sizeof(seq.path));
      } else {
         strlcpy(seq.path, cm_get_path().c_str(), sizeof(seq.path));
      }
   }

   if (strlen(seq.path)>0 && seq.path[strlen(seq.path)-1] != DIR_SEPARATOR) {
      strlcat(seq.path, DIR_SEPARATOR_STR, sizeof(seq.path));
   }

   if (seq.filename[0]) {
      strlcpy(str, seq.path, sizeof(str));
      strlcat(str, seq.filename, sizeof(str));
      seq.error[0] = 0;
      seq.error_line = 0;
      seq.serror_line = 0;
      if (pnseq) {
         mxml_free_tree(pnseq);
         pnseq = NULL;
      }
      if (stristr(str, ".msl")) {
         if (msl_parse(str, seq.error, sizeof(seq.error), &seq.serror_line)) {
            strsubst(str, sizeof(str), ".msl", ".xml");
            pnseq = mxml_parse_file(str, seq.error, sizeof(seq.error), &seq.error_line);
         }
      } else
         pnseq = mxml_parse_file(str, seq.error, sizeof(seq.error), &seq.error_line);
   }

   seq.transition_request = FALSE;

   db_set_record(hDB, hKey, &seq, sizeof(seq), 0);

   status = db_watch(hDB, hKey, seq_watch, NULL);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "init_sequencer", "Sequencer error: Cannot watch /Sequencer/State, db_watch() status %d", status);
      return;
   }
}

void seq_load(SEQUENCER &seq, HNDLE hDB, HNDLE hKey, const char* filename)
{
   strlcpy(seq.filename, filename, sizeof(seq.filename));

   char str[256];
   strlcpy(str, seq.path, sizeof(str));
   if (strlen(str)>1 && str[strlen(str)-1] != DIR_SEPARATOR)
      strlcat(str, DIR_SEPARATOR_STR, sizeof(str));
   strlcat(str, seq.filename, sizeof(str));
   seq.new_file = TRUE;
   seq.error[0] = 0;
   seq.error_line = 0;
   seq.serror_line = 0;
   if (pnseq) {
      mxml_free_tree(pnseq);
      pnseq = NULL;
   }
   if (stristr(str, ".msl")) {
      if (msl_parse(str, seq.error, sizeof(seq.error), &seq.serror_line)) {
         strsubst(str, sizeof(str), ".msl", ".xml");
         pnseq = mxml_parse_file(str, seq.error, sizeof(seq.error), &seq.error_line);
      }
   } else
      pnseq = mxml_parse_file(str, seq.error, sizeof(seq.error), &seq.error_line);

   seq.finished = FALSE;
   db_set_record(hDB, hKey, &seq, sizeof(seq), 0);
}

void seq_save(SEQUENCER &seq, HNDLE hDB, HNDLE hKey, char* str, int str_size)
{
   seq.error[0] = 0;
   if (pnseq) {
      mxml_free_tree(pnseq);
      pnseq = NULL;
   }
   seq.new_file = TRUE; // make sequencer load new file
   seq.error_line = 0;
   seq.serror_line = 0;
   if (stristr(str, ".msl")) {
      if (msl_parse(str, seq.error, sizeof(seq.error), &seq.serror_line)) {
         strsubst(str, str_size, ".msl", ".xml");
         pnseq = mxml_parse_file(str, seq.error, sizeof(seq.error), &seq.error_line);
      }
   } else
      pnseq = mxml_parse_file(str, seq.error, sizeof(seq.error), &seq.error_line);

   db_set_record(hDB, hKey, &seq, sizeof(SEQUENCER), 0);
}

void seq_start(SEQUENCER &seq)
{
   HNDLE hDB, hKey;

   cm_get_experiment_database(&hDB, NULL);
   db_find_key(hDB, 0, "/Sequencer/State", &hKey);

   /* start sequencer */
   seq.running = TRUE;
   seq.finished = FALSE;
   seq.paused = FALSE;
   seq.transition_request = FALSE;
   seq.wait_limit = 0;
   seq.wait_value = 0;
   seq.start_time = 0;
   seq.wait_type[0] = 0;
   for (int i=0 ; i<4 ; i++) {
      seq.loop_start_line[i] = 0;
      seq.sloop_start_line[i] = 0;
      seq.loop_end_line[i] = 0;
      seq.sloop_end_line[i] = 0;
      seq.loop_counter[i] = 0;
      seq.loop_n[i] = 0;
   }
   for (int i=0 ; i<4 ; i++) {
      seq.if_else_line[i] = 0;
      seq.if_endif_line[i] = 0;
      seq.subroutine_end_line[i] = 0;
      seq.subroutine_return_line[i] = 0;
      seq.subroutine_call_line[i] = 0;
      seq.ssubroutine_call_line[i] = 0;
      seq.subroutine_param[i][0] = 0;
   }
   seq.current_line_number = 1;
   seq.scurrent_line_number = 1;
   seq.if_index = 0;
   seq.stack_index = 0;
   seq.error[0] = 0;
   seq.error_line = 0;
   seq.serror_line = 0;
   seq.subdir[0] = 0;
   seq.subdir_end_line = 0;
   seq.subdir_not_notify = 0;
   db_set_record(hDB, hKey, &seq, sizeof(seq), 0);
}

void seq_stop(SEQUENCER &seq)
{
   HNDLE hDB, hKey;

   cm_get_experiment_database(&hDB, NULL);
   db_find_key(hDB, 0, "/Sequencer/State", &hKey);

   seq.running = FALSE;
   seq.finished = FALSE;
   seq.paused = FALSE;
   seq.wait_limit = 0;
   seq.wait_value = 0;
   seq.wait_type[0] = 0;
   for (int i=0 ; i<4 ; i++) {
      seq.loop_start_line[i] = 0;
      seq.loop_end_line[i] = 0;
      seq.loop_counter[i] = 0;
      seq.loop_n[i] = 0;
   }
   seq.stop_after_run = FALSE;
   seq.subdir[0] = 0;

   db_set_record(hDB, hKey, &seq, sizeof(seq), 0);

   /* stop run if not already stopped */
   char str[256];
   int state = 0;
   int size = sizeof(state);
   db_get_value(hDB, 0, "/Runinfo/State", &state, &size, TID_INT, FALSE);
   if (state != STATE_STOPPED)
      cm_transition(TR_STOP, 0, str, sizeof(str), TR_MTHREAD | TR_SYNC, FALSE);
}

int seq_loop_width(SEQUENCER &seq, int i)
{
   int width = 0;
   if (seq.loop_n[i] <= 0)
      width = 0;
   else
      width = (int)(((double)seq.loop_counter[i]/seq.loop_n[i])*100+0.5);
   return width;
}

int seq_wait_width(SEQUENCER &seq)
{
   int width = 0;
   if (seq.wait_value <= 0)
      width = 0;
   else
      width = (int)(((double)seq.wait_value/seq.wait_limit)*100+0.5);
   return width;
}

void seq_set_paused(SEQUENCER &seq, BOOL paused)
{
   HNDLE hDB, hKey;

   cm_get_experiment_database(&hDB, NULL);
   db_find_key(hDB, 0, "/Sequencer/State", &hKey);

   seq.paused = paused;
   db_set_record(hDB, hKey, &seq, sizeof(seq), 0);
}

void seq_set_stop_after_run(SEQUENCER &seq, BOOL stop_after_run)
{
   HNDLE hDB, hKey;

   cm_get_experiment_database(&hDB, NULL);
   db_find_key(hDB, 0, "/Sequencer/State", &hKey);

   seq.stop_after_run = stop_after_run;
   db_set_record(hDB, hKey, &seq, sizeof(seq), 0);
}

/*------------------------------------------------------------------*/

void show_seq_page(Param* p, Return* r)
{
   INT i, size, status, n,  width, eob, last_line, error_line, sectionEmpty;
   HNDLE hDB;
   char str[256], path[256], dir[256], error[256], data[256], buffer[10000], line[256], name[32];
   time_t now;
   char *flist = NULL, *pc, *pline, *buf;
   PMXML_NODE pn;
   int fh;
   FILE *f;
   HNDLE hKey, hparam, hsubkey;
   KEY key;

   SEQUENCER seq;
   SEQUENCER_STR(sequencer_str);

   cm_get_experiment_database(&hDB, NULL);
   db_find_key(hDB, 0, "/Sequencer/State", &hKey);
   if (hKey == 0) {
      show_error(r, "/Sequencer/State is missing in ODB");
      return;
   }

   size = sizeof(seq);
   status = db_get_record1(hDB, hKey, &seq, &size, 0, strcomb1(sequencer_str).c_str());
   if (status != DB_SUCCESS) {
      sprintf(str, "Cannot get /Sequencer/State from ODB, db_get_record1() status %d", status);
      show_error(r, str);
      return;
   }

   /*---- load XML file ----*/
   if (equal_ustring(p->getparam("xcmd"), "Load script file")) {
      if (p->isparam("dir"))
         strlcpy(str, p->getparam("dir"), sizeof(str));
      else
         str[0] = 0;
      if (p->isparam("fs"))
         strlcat(str, p->getparam("fs"), sizeof(str));

      seq_load(seq, hDB, hKey, str);
      redirect(r, "?cmd=sequencer");
      return;
   }

   /*---- start script ----*/
   if (equal_ustring(p->getparam("cmd"), "Start Script")) {
      if (p->isparam("params")) {

         /* set parameters from ODB */
         db_find_key(hDB, 0, "/Experiment/Edit on sequence", &hparam);
         n = 0;
         if (hparam) {
            for (i = 0 ;; i++) {
               db_enum_key(hDB, hparam, i, &hsubkey);

               if (!hsubkey)
                  break;

               db_get_key(hDB, hsubkey, &key);

               for (int j = 0; j < key.num_values; j++) {
                  size = key.item_size;
                  sprintf(str, "x%d", n++);
                  db_sscanf(p->getparam(str), data, &size, 0, key.type);
                  db_set_data_index(hDB, hsubkey, data, key.item_size, j, key.type);
               }
            }
         }

         /* set parameters from script */
         pn = mxml_find_node(pnseq, "RunSequence");
         if (pn) {
            last_line = mxml_get_line_number_end(pn);

            for (i=1 ; i<last_line ; i++){
               pn = mxml_get_node_at_line(pnseq, i);
               if (!pn)
                  continue;

               if (equal_ustring(mxml_get_name(pn), "Param")) {
                  strlcpy(name, mxml_get_attribute(pn, "name"), sizeof(name));
                  sprintf(str, "x%d", n++);
                  strlcpy(buffer, p->getparam(str), sizeof(buffer));
                  sprintf(str, "/Sequencer/Variables/%s", name);
                  size = strlen(buffer)+1;
                  if (size < 32)
                     size = 32;
                  db_set_value(hDB, 0, str, buffer, size, 1, TID_STRING);
               }
            }
         }

         seq_start(seq);
         cm_msg(MTALK, "show_seq_page", "Sequencer has been started.");
         redirect(r, "?cmd=sequencer");
         return;

      } else {

         seq_start_page(p, r);
         return;
      }
   }

   /*---- save script ----*/
   if (equal_ustring(p->getparam("cmd"), "Save script")) {
      strlcpy(str, seq.path, sizeof(str));
      if (strlen(str)>1 && str[strlen(str)-1] != DIR_SEPARATOR)
         strlcat(str, DIR_SEPARATOR_STR, sizeof(str));
      strlcat(str, seq.filename, sizeof(str));
      fh = open(str, O_RDWR | O_TRUNC | O_CREAT | O_TEXT, 0644);
      if (fh < 0) {
         cm_msg(MERROR, "show_seq_page", "Cannot save file \'%s\', open() errno %d (%s)", str, errno, strerror(errno));
      } else {
         if (p->isparam("scripttext")) {
            i = strlen(p->getparam("scripttext"));
            int wr = write(fh, p->getparam("scripttext"), i);
            if (wr != i) {
               cm_msg(MERROR, "show_seq_page", "Cannot save file \'%s\', write() errno %d (%s)", str, errno, strerror(errno));
            }
         }
         close(fh);
      }
      strlcpy(str, seq.path, sizeof(str));
      if (strlen(str)>1 && str[strlen(str)-1] != DIR_SEPARATOR)
         strlcat(str, DIR_SEPARATOR_STR, sizeof(str));
      strlcat(str, seq.filename, sizeof(str));

      seq_save(seq, hDB, hKey, str, sizeof(str));
      redirect(r, "?cmd=sequencer");
      return;
   }

   /*---- stop after current run ----*/
   if (equal_ustring(p->getparam("cmd"), "Stop after current run")) {
      seq_set_stop_after_run(seq, TRUE);
      redirect(r, "?cmd=sequencer");
      return;
   }
   if (equal_ustring(p->getparam("cmd"), "Cancel 'Stop after current run'")) {
      seq_set_stop_after_run(seq, FALSE);
      redirect(r, "?cmd=sequencer");
      return;
   }

   /*---- stop immediately ----*/
   if (equal_ustring(p->getparam("cmd"), "Stop immediately")) {
      seq_stop(seq);
      cm_msg(MTALK, "show_seq_page", "Sequencer is finished.");
      redirect(r, "?cmd=sequencer");
      return;
   }

   /*---- pause script ----*/
   if (equal_ustring(p->getparam("cmd"), "SPause")) {
      seq_set_paused(seq, TRUE);
      redirect(r, "?cmd=sequencer");
      return;
   }

   /*---- resume script ----*/
   if (equal_ustring(p->getparam("cmd"), "SResume")) {
      seq_set_paused(seq, FALSE);
      redirect(r, "?cmd=sequencer");
      return;
   }

   /* header */
   r->rsprintf("HTTP/1.0 200 Document follows\r\n");
   r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
   r->rsprintf("Content-Type: text/html; charset=iso-8859-1\r\n\r\n");

   r->rsprintf("<html><head>\n");
   r->rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
   r->rsprintf("<link rel=\"stylesheet\" href=\"midas.css\" type=\"text/css\" />\n");
   r->rsprintf("<link rel=\"stylesheet\" href=\"mhttpd.css\" type=\"text/css\" />\n");

   if (!equal_ustring(p->getparam("cmd"), "Load Script") && !p->isparam("fs") &&
       !equal_ustring(p->getparam("cmd"), "Edit Script") &&
       !equal_ustring(p->getparam("cmd"), "New Script"))
      r->rsprintf("<meta http-equiv=\"Refresh\" content=\"60\">\n");

   r->rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   r->rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   r->rsprintf("<script type=\"text/javascript\" src=\"controls.js\"></script>\n");
   r->rsprintf("<script type=\"text/javascript\" src=\"obsolete.js\"></script>\n");

   r->rsprintf("<script type=\"text/javascript\">\n");
   r->rsprintf("<!--\n");
   r->rsprintf("var show_all_lines = false;\n");
   r->rsprintf("var sshow_all_lines = false;\n");
   r->rsprintf("var last_msg = null;\n");
   r->rsprintf("var last_paused = null;\n");
   r->rsprintf("\n");
   r->rsprintf("function start_script()\n");
   r->rsprintf("{\n");
   r->rsprintf("  mjsonrpc_call('cm_exist', '{\"name\": \"sequencer\" }').then(function(rpc){;\n");
   r->rsprintf("    if (rpc.result.status === 1) {;\n");
   r->rsprintf("       window.location.href = '?cmd=Start+Script';\n");
   r->rsprintf("    } else {\n");
   r->rsprintf("       dlgAlert('Sequencer is not running, please start it');\n");
   r->rsprintf("    }\n");
   r->rsprintf("  }).catch(function(error) {\n");
   r->rsprintf("    mjsonrpc_error_alert(error); });\n");
   r->rsprintf("\n");
   r->rsprintf("  return false;\n");
   r->rsprintf("}\n");
   r->rsprintf("\n");
   r->rsprintf("function seq_refresh()\n");
   r->rsprintf("{\n");
   r->rsprintf("   seq = ODBGetRecord('/Sequencer/State');\n");
   r->rsprintf("   var current_line = ODBExtractRecord(seq, 'Current line number');\n");
   r->rsprintf("   var scurrent_line = ODBExtractRecord(seq, 'SCurrent line number');\n");
   r->rsprintf("   var subroutine_call_line = ODBExtractRecord(seq, 'Subroutine call line');\n");
   r->rsprintf("   var ssubroutine_call_line = ODBExtractRecord(seq, 'SSubroutine call line');\n");
   r->rsprintf("   var error_line = ODBExtractRecord(seq, 'Error line');\n");
   r->rsprintf("   var serror_line = ODBExtractRecord(seq, 'SError line');\n");
   r->rsprintf("   var message = ODBExtractRecord(seq, 'Message');\n");
   r->rsprintf("   var wait_value = ODBExtractRecord(seq, 'Wait value');\n");
   r->rsprintf("   var wait_limit = ODBExtractRecord(seq, 'Wait limit');\n");
   r->rsprintf("   var wait_type = ODBExtractRecord(seq, 'Wait type');\n");
   r->rsprintf("   var loop_n = ODBExtractRecord(seq, 'Loop n');\n");
   r->rsprintf("   var loop_counter = ODBExtractRecord(seq, 'Loop counter');\n");
   r->rsprintf("   var loop_start_line = ODBExtractRecord(seq, 'Loop start line');\n");
   r->rsprintf("   var sloop_start_line = ODBExtractRecord(seq, 'SLoop start line');\n");
   r->rsprintf("   var loop_end_line = ODBExtractRecord(seq, 'Loop end line');\n");
   r->rsprintf("   var sloop_end_line = ODBExtractRecord(seq, 'SLoop end line');\n");
   r->rsprintf("   var finished = ODBExtractRecord(seq, 'Finished');\n");
   r->rsprintf("   var paused = ODBExtractRecord(seq, 'Paused');\n");
   r->rsprintf("   var start_time = ODBExtractRecord(seq, 'Start time');\n");
   r->rsprintf("   var msg = ODBExtractRecord(seq, 'Last msg');\n");
   r->rsprintf("   \n");
   r->rsprintf("   if (last_msg == null)\n");
   r->rsprintf("      last_msg = msg;\n");
   r->rsprintf("   else if (last_msg != msg)\n");
   r->rsprintf("      window.location.reload();\n");
   r->rsprintf("   \n");
   r->rsprintf("   if (last_paused == null)\n");
   r->rsprintf("      last_paused = paused;\n");
   r->rsprintf("   else if (last_paused != paused)\n");
   r->rsprintf("      window.location.reload();\n");
   r->rsprintf("   \n");
   r->rsprintf("   for (var sl=1 ; ; sl++) {\n");
   r->rsprintf("      sline = document.getElementById('sline'+sl);\n");
   r->rsprintf("      if (sline == null) {\n");
   r->rsprintf("         var slast_line = sl-1;\n");
   r->rsprintf("         break;\n");
   r->rsprintf("      }\n");
   r->rsprintf("      if (Math.abs(sl - scurrent_line) > 10 && !sshow_all_lines)\n");
   r->rsprintf("         sline.style.display = 'none';\n");
   r->rsprintf("      else\n");
   r->rsprintf("         sline.style.display = 'inline';\n");
   r->rsprintf("      if (scurrent_line > 10) {\n");
   r->rsprintf("         document.getElementById('supperarrow').style.display = 'inline';\n");
   r->rsprintf("         if (sshow_all_lines)\n");
   r->rsprintf("            document.getElementById('supperarrow').style.display = '&#x25BC';\n");
   r->rsprintf("         else\n");
   r->rsprintf("            document.getElementById('supperarrow').style.display = '&#x25B2';\n");
   r->rsprintf("      } else\n");
   r->rsprintf("          document.getElementById('supperarrow').style.display = 'none';\n");
   r->rsprintf("      if (sl == serror_line)\n");
   r->rsprintf("         sline.style.backgroundColor = '#FF0000';\n");
   r->rsprintf("      else if (sl == scurrent_line)\n");
   r->rsprintf("         sline.style.backgroundColor = '#80FF80';\n");
   r->rsprintf("      else if (sl == ssubroutine_call_line[3])\n");
   r->rsprintf("         sline.style.backgroundColor = '%s';\n", call_col[3]);
   r->rsprintf("      else if (sl == ssubroutine_call_line[2])\n");
   r->rsprintf("         sline.style.backgroundColor = '%s';\n", call_col[2]);
   r->rsprintf("      else if (sl == ssubroutine_call_line[1])\n");
   r->rsprintf("         sline.style.backgroundColor = '%s';\n", call_col[1]);
   r->rsprintf("      else if (sl == ssubroutine_call_line[0])\n");
   r->rsprintf("         sline.style.backgroundColor = '%s';\n", call_col[0]);
   r->rsprintf("      else if (sl >= sloop_start_line[3] && sl <= sloop_end_line[3])\n");
   r->rsprintf("         sline.style.backgroundColor = '%s';\n", bar_col[3]);
   r->rsprintf("      else if (sl >= sloop_start_line[2] && sl <= sloop_end_line[2])\n");
   r->rsprintf("         sline.style.backgroundColor = '%s';\n", bar_col[2]);
   r->rsprintf("      else if (sl >= sloop_start_line[1] && sl <= sloop_end_line[1])\n");
   r->rsprintf("         sline.style.backgroundColor = '%s';\n", bar_col[1]);
   r->rsprintf("      else if (sl >= sloop_start_line[0] && sl <= sloop_end_line[0])\n");
   r->rsprintf("         sline.style.backgroundColor = '%s';\n", bar_col[0]);
   r->rsprintf("      else\n");
   r->rsprintf("         sline.style.backgroundColor = '#FFFFFF';\n");
   r->rsprintf("   }\n");
   r->rsprintf("   for (var l=1 ; ; l++) {\n");
   r->rsprintf("      line = document.getElementById('line'+l);\n");
   r->rsprintf("      if (line == null) {\n");
   r->rsprintf("         var last_line = l-1;\n");
   r->rsprintf("         break;\n");
   r->rsprintf("      }\n");
   r->rsprintf("      if (Math.abs(l - current_line) > 10 && !show_all_lines)\n");
   r->rsprintf("         line.style.display = 'none';\n");
   r->rsprintf("      else\n");
   r->rsprintf("         line.style.display = 'inline';\n");
   r->rsprintf("      if (current_line > 10) {\n");
   r->rsprintf("         document.getElementById('upperarrow').style.display = 'inline';\n");
   r->rsprintf("         if (show_all_lines)\n");
   r->rsprintf("            document.getElementById('upperarrow').style.display = '&#x25BC';\n");
   r->rsprintf("         else\n");
   r->rsprintf("            document.getElementById('upperarrow').style.display = '&#x25B2';\n");
   r->rsprintf("      } else\n");
   r->rsprintf("          document.getElementById('upperarrow').style.display = 'none';\n");
   r->rsprintf("      if (l == error_line)\n");
   r->rsprintf("         line.style.backgroundColor = '#FF0000';\n");
   r->rsprintf("      else if (l == current_line)\n");
   r->rsprintf("         line.style.backgroundColor = '#80FF80';\n");
   r->rsprintf("      else if (l == subroutine_call_line[3])\n");
   r->rsprintf("         line.style.backgroundColor = '%s';\n", call_col[3]);
   r->rsprintf("      else if (l == subroutine_call_line[2])\n");
   r->rsprintf("         line.style.backgroundColor = '%s';\n", call_col[2]);
   r->rsprintf("      else if (l == subroutine_call_line[1])\n");
   r->rsprintf("         line.style.backgroundColor = '%s';\n", call_col[1]);
   r->rsprintf("      else if (l == subroutine_call_line[0])\n");
   r->rsprintf("         line.style.backgroundColor = '%s';\n", call_col[0]);
   r->rsprintf("      else if (l >= loop_start_line[3] && l <= loop_end_line[3])\n");
   r->rsprintf("         line.style.backgroundColor = '%s';\n", bar_col[3]);
   r->rsprintf("      else if (l >= loop_start_line[2] && l <= loop_end_line[2])\n");
   r->rsprintf("         line.style.backgroundColor = '%s';\n", bar_col[2]);
   r->rsprintf("      else if (l >= loop_start_line[1] && l <= loop_end_line[1])\n");
   r->rsprintf("         line.style.backgroundColor = '%s';\n", bar_col[1]);
   r->rsprintf("      else if (l >= loop_start_line[0] && l <= loop_end_line[0])\n");
   r->rsprintf("         line.style.backgroundColor = '%s';\n", bar_col[0]);
   r->rsprintf("      else\n");
   r->rsprintf("         line.style.backgroundColor = '#FFFFFF';\n");
   r->rsprintf("   }\n");
   r->rsprintf("   \n");
   r->rsprintf("   if (document.getElementById('lowerarrow')) {\n");
   r->rsprintf("      if (current_line < last_line-10) {\n");
   r->rsprintf("         document.getElementById('lowerarrow').style.display = 'inline';\n");
   r->rsprintf("         if (show_all_lines)\n");
   r->rsprintf("            document.getElementById('lowerarrow').innerHTML = '&#x25B2';\n");
   r->rsprintf("         else\n");
   r->rsprintf("            document.getElementById('lowerarrow').innerHTML = '&#x25BC';\n");
   r->rsprintf("      } else\n");
   r->rsprintf("         document.getElementById('lowerarrow').style.display = 'none';\n");
   r->rsprintf("   }\n");
   r->rsprintf("   if (document.getElementById('slowerarrow')) {\n");
   r->rsprintf("      if (scurrent_line < slast_line-10) {\n");
   r->rsprintf("         document.getElementById('slowerarrow').style.display = 'inline';\n");
   r->rsprintf("         if (sshow_all_lines)\n");
   r->rsprintf("            document.getElementById('slowerarrow').innerHTML = '&#x25B2';\n");
   r->rsprintf("         else\n");
   r->rsprintf("            document.getElementById('slowerarrow').innerHTML = '&#x25BC';\n");
   r->rsprintf("      } else\n");
   r->rsprintf("         document.getElementById('slowerarrow').style.display = 'none';\n");
   r->rsprintf("   }\n");
   r->rsprintf("   \n");
   r->rsprintf("   var wl = document.getElementById('wait_label');\n");
   r->rsprintf("   if (wl != null) {\n");
   r->rsprintf("      if (wait_type == 'Seconds')\n");
   r->rsprintf("         wl.innerHTML = 'Wait:&nbsp['+wait_value+'/'+wait_limit+'&nbsp;s]';\n");
   r->rsprintf("      else if (wait_type == 'Events')\n");
   r->rsprintf("         wl.innerHTML = 'Run:&nbsp['+wait_value+'/'+wait_limit+'&nbsp;events]';\n");
   r->rsprintf("      else if (wait_type.substr(0, 3) == 'ODB') {\n");
   r->rsprintf("         op = wait_type.substr(3);\n");
   r->rsprintf("         if (op == '')\n");
   r->rsprintf("            op = '>=';\n");
   r->rsprintf("         op = op.replace(/>/g, '&gt;').replace(/</g, '&lt;');\n");
   r->rsprintf("         wl.innerHTML = 'ODB:&nbsp['+wait_value+'&nbsp;'+op+'&nbsp;'+wait_limit+']';\n");
   r->rsprintf("      } else {\n");
   r->rsprintf("         wl.innerHTML = '';\n");
   r->rsprintf("      }\n");
   r->rsprintf("      wr = document.getElementById('wait_row');\n");
   r->rsprintf("      if (wait_type == '')\n");
   r->rsprintf("         wr.style.display = 'none';\n");
   r->rsprintf("      else\n");
   r->rsprintf("         wr.style.display = 'table-row';\n");
   r->rsprintf("   }\n");
   r->rsprintf("   var rp = document.getElementById('runprgs');\n");
   r->rsprintf("   if (rp != null) {\n");
   r->rsprintf("      var width = Math.round(100.0*wait_value/wait_limit);\n");
   r->rsprintf("      if (width > 100)\n");
   r->rsprintf("         width = 100;\n");
   r->rsprintf("      rp.width = width+'%%';\n");
   r->rsprintf("   }\n");
   r->rsprintf("   \n");
   r->rsprintf("   for (var i=0 ; i<4 ; i++) {\n");
   r->rsprintf("      var l = document.getElementById('loop'+i);\n");
   r->rsprintf("      if (loop_start_line[i] > 0) {\n");
   r->rsprintf("         var ll = document.getElementById('loop_label'+i);\n");
   r->rsprintf("         if (loop_n[i] == -1)\n");
   r->rsprintf("            ll.innerHTML = 'Loop:&nbsp['+loop_counter[i]+']';\n");
   r->rsprintf("         else\n");
   r->rsprintf("            ll.innerHTML = 'Loop:&nbsp['+loop_counter[i]+'/'+loop_n[i]+']';\n");
   r->rsprintf("         l.style.display = 'table-row';\n");
   r->rsprintf("         var lp = document.getElementById('loopprgs'+i);\n");
   r->rsprintf("         if (loop_n[i] == -1)\n");
   r->rsprintf("            lp.style.display = 'none';\n");
   r->rsprintf("         else\n");
   r->rsprintf("            lp.width = Math.round(100.0*loop_counter[i]/loop_n[i])+'%%';\n");
   r->rsprintf("      } else\n");
   r->rsprintf("         l.style.display = 'none';\n");
   r->rsprintf("   }\n");
   r->rsprintf("   \n");
   r->rsprintf("   if (message != '') {\n");
   r->rsprintf("      alert(message);\n");
   r->rsprintf("      message = '';\n");
   r->rsprintf("      ODBSet('/Sequencer/State/Message', '');\n");
   r->rsprintf("      window.location.reload();\n");
   r->rsprintf("   }\n");
   r->rsprintf("   \n");
   r->rsprintf("   if (finished == 'y' || error_line > 0) {\n");
   r->rsprintf("      window.location.reload();\n");
   r->rsprintf("   }\n");
   r->rsprintf("\n");
   r->rsprintf("   refreshID = setTimeout('seq_refresh()', 1000);\n");
   r->rsprintf("}\n");
   r->rsprintf("\n");
   r->rsprintf("function show_lines()\n");
   r->rsprintf("{\n");
   r->rsprintf("   show_all_lines = !show_all_lines;\n");
   r->rsprintf("   if (show_all_lines)\n");
   r->rsprintf("      document.getElementById('upperarrow').innerHTML = '&#x25BC';\n");
   r->rsprintf("   else\n");
   r->rsprintf("      document.getElementById('upperarrow').innerHTML = '&#x25B2';\n");
   r->rsprintf("   seq_refresh();\n");
   r->rsprintf("}\n");
   r->rsprintf("\n");
   r->rsprintf("function sshow_lines()\n");
   r->rsprintf("{\n");
   r->rsprintf("   sshow_all_lines = !sshow_all_lines;\n");
   r->rsprintf("   if (sshow_all_lines)\n");
   r->rsprintf("      document.getElementById('supperarrow').innerHTML = '&#x25BC';\n");
   r->rsprintf("   else\n");
   r->rsprintf("      document.getElementById('supperarrow').innerHTML = '&#x25B2';\n");
   r->rsprintf("   seq_refresh();\n");
   r->rsprintf("}\n");
   r->rsprintf("\n");
   r->rsprintf("function load()\n");
   r->rsprintf("{\n");
   r->rsprintf("   if (document.getElementById('fs').selectedIndex == -1)\n");
   r->rsprintf("      dlgAlert('Please select a file to load');\n");
   r->rsprintf("   else {\n");
   r->rsprintf("      var o = document.createElement('input');\n");
   r->rsprintf("      o.type = 'hidden'; o.name='xcmd'; o.value='Load script file';\n");
   r->rsprintf("      document.form1.appendChild(o);\n");
   r->rsprintf("      document.form1.submit();\n");
   r->rsprintf("   }\n");
   r->rsprintf("}\n");
   r->rsprintf("\n");

   r->rsprintf("//-->\n");
   r->rsprintf("</script>\n");

   r->rsprintf("<title>Sequencer</title></head>\n");
   if (seq.running)
      r->rsprintf("<body onLoad=\"seq_refresh();\">\n");

   /* title row */
   size = sizeof(str);
   str[0] = 0;
   db_get_value(hDB, 0, "/Experiment/Name", str, &size, TID_STRING, TRUE);
   time(&now);

   r->rsprintf("<form name=\"form1\" method=\"GET\" action=\".\">\n");

   // body needs wrapper div to pin footer
   show_navigation_bar(r, "Sequencer");

   r->rsprintf("<table>");  //generic table for menu row

   /*---- menu buttons ----*/

   if (!equal_ustring(p->getparam("cmd"), "Load Script") && !p->isparam("fs")) {
      r->rsprintf("<tr>\n");
      r->rsprintf("<td colspan=2 style=\"text-align:center\">\n");

      if (seq.running) {
         if (seq.stop_after_run)
            r->rsprintf("<input type=submit name=cmd value=\"Cancel 'Stop after current run'\">\n");
         else
            r->rsprintf("<input type=submit name=cmd value=\"Stop after current run\">\n");

         r->rsprintf("<script type=\"text/javascript\">\n");
         r->rsprintf("<!--\n");
         r->rsprintf("function stop_immediately()\n");
         r->rsprintf("{\n");
         r->rsprintf("   flag = confirm('Are you sure to stop the script immediately?');\n");
         r->rsprintf("   if (flag == true)\n");
         r->rsprintf("      window.location.href = '?cmd=Stop immediately';\n");
         r->rsprintf("}\n");
         r->rsprintf("//-->\n");
         r->rsprintf("</script>\n");
         r->rsprintf("<input type=button onClick=\"stop_immediately()\" value=\"Stop immediately\">");

         if (!seq.paused) {
            r->rsprintf("<script type=\"text/javascript\">\n");
            r->rsprintf("<!--\n");
            r->rsprintf("function pause()\n");
            r->rsprintf("{\n");
            r->rsprintf("   flag = confirm('Are you sure to pause the script ?');\n");
            r->rsprintf("   if (flag == true)\n");
            r->rsprintf("      window.location.href = '?cmd=SPause';\n");
            r->rsprintf("}\n");
            r->rsprintf("//-->\n");
            r->rsprintf("</script>\n");
            r->rsprintf("<input type=button onClick=\"pause()\" value=\"Pause script\">");
         } else {
            r->rsprintf("<input type=button onClick=\"window.location.href=\'?cmd=SResume\';\" value=\"Resume script\">");
         }

      } else {
         r->rsprintf("<input type=submit name=cmd value=\"Load Script\">\n");
         r->rsprintf("<input type=submit onclick=\"return start_script();\" value=\"Start Script\">\n");
      }
      if (seq.filename[0] && !seq.running && !equal_ustring(p->getparam("cmd"), "Load Script") && !p->isparam("fs"))
         r->rsprintf("<input type=submit name=cmd value=\"Edit Script\">\n");

      r->rsprintf("</td></tr>\n");
   }

   r->rsprintf("</table>");  //end menu table

   r->rsprintf("<table><tr><td>"); //wrapper table to keep all sub-tables the same width
   r->rsprintf("<table id=\"topTable\" class=\"sequencerTable\" width=100%%>");  //first table ends up being different things depending on context; refactor.

   /*---- file selector ----*/

   if (equal_ustring(p->getparam("cmd"), "Load Script") || p->isparam("fs")) {
      r->rsprintf("<tr><th class=\"subStatusTitle\" colspan=2>\n");
      r->rsprintf("<b>Select a sequencer file:</b><br></th></tr>\n");
      r->rsprintf("<tr colspan=2><td>Directory: %s</td></tr>\n", seq.path);
      r->rsprintf("<tr><td><select name=\"fs\" id=\"fs\" size=20 style=\"width:300\">\n");

      if (p->isparam("dir"))
         strlcpy(dir, p->getparam("dir"), sizeof(dir));
      else
         dir[0] = 0;
      strlcpy(path, seq.path, sizeof(path));
      if (strlen(str)>1 && str[strlen(str)-1] != DIR_SEPARATOR)
         strlcat(str, DIR_SEPARATOR_STR, sizeof(str));

      if (p->isparam("fs")) {
         strlcpy(str, p->getparam("fs"), sizeof(str));
         if (str[0] == '[') {
            strlcpy(str, p->getparam("fs")+1, sizeof(str));
            str[strlen(str)-1] = 0;
         }
         if (equal_ustring(str, "..")) {
            pc = dir+strlen(dir)-1;
            if (pc > dir && *pc == '/')
               *(pc--) = 0;
            while (pc >= dir && *pc != '/')
               *(pc--) = 0;
         } else {
            strlcat(dir, str, sizeof(dir));
            strlcat(dir, DIR_SEPARATOR_STR, sizeof(dir));
         }
      }
      strlcat(path, dir, sizeof(path));

      /*---- go over subdirectories ----*/
      n = ss_dir_find(path, (char *)"*", &flist);
      if (dir[0])
         r->rsprintf("<option onDblClick=\"document.form1.submit()\">[..]</option>\n");
      for (int i=0 ; i<n ; i++) {
         if (flist[i*MAX_STRING_LENGTH] != '.')
            r->rsprintf("<option onDblClick=\"document.form1.submit()\">[%s]</option>\n", flist+i*MAX_STRING_LENGTH);
      }

      /*---- go over MSL files in sequencer directory ----*/
      n = ss_file_find(path, (char *)"*.msl", &flist);
      for (int i=0 ; i<n ; i++) {
         char comment[512];
         comment[0] = 0;
         strlcpy(str, path, sizeof(str));
         if (strlen(str)>1 && str[strlen(str)-1] != DIR_SEPARATOR)
            strlcat(str, DIR_SEPARATOR_STR, sizeof(str));
         strlcat(str, flist+i*MAX_STRING_LENGTH, sizeof(str));

         if (msl_parse(str, error, sizeof(error), &error_line)) {
            if (strchr(str, '.')) {
               *strchr(str, '.') = 0;
               strlcat(str, ".xml", sizeof(str));
            }
            comment[0] = 0;
            if (pnseq) {
               mxml_free_tree(pnseq);
               pnseq = NULL;
            }
            pnseq = mxml_parse_file(str, error, sizeof(error), &error_line);
            if (error[0]) {
               strlcpy(comment, error, sizeof(comment));
            } else {
               if (pnseq) {
                  pn = mxml_find_node(pnseq, "RunSequence/Comment");
                  if (pn)
                     strlcpy(comment, mxml_get_value(pn), sizeof(comment));
                  else
                     strcpy(comment, "<No description in XML file>");
               }
            }
            if (pnseq) {
               mxml_free_tree(pnseq);
               pnseq = NULL;
            }
         } else {
            sprintf(comment, "Error in MSL: %s", error);
         }

         strsubst(comment, sizeof(comment), "\"", "\\\'");
         r->rsprintf("<option onClick=\"document.getElementById('cmnt').innerHTML='%s'\"", comment);
         r->rsprintf(" onDblClick=\"load();\">%s</option>\n", flist+i*MAX_STRING_LENGTH);
      }

      free(flist);
      flist = NULL;
      r->rsprintf("</select>\n");
      r->rsprintf("<input type=hidden name=dir value=\"%s\">", dir);
      r->rsprintf("<input type=hidden name=cmd value=\"Load Script\">");
      r->rsprintf("</td></tr>\n");

      r->rsprintf("<tr><td style=\"text-align:center\" colspan=2 id=\"cmnt\">&nbsp;</td></tr>\n");
      r->rsprintf("<tr><td style=\"text-align:center\" colspan=2>\n");
      r->rsprintf("<input type=button onClick=\"load();\" value=\"Load Script\">\n");
      r->rsprintf("<input type=submit name=cmd value=\"Cancel Script\">\n");
      r->rsprintf("</td></tr>\n");
      r->rsprintf("</table>");
   }

   /*---- show XML file ----*/

   else {
      if (equal_ustring(p->getparam("cmd"), "New Script")) {
         r->rsprintf("<tr><th class=\"subStatusTitle\">Script Editor</th></tr>");
         r->rsprintf("<tr><td colspan=2>");
         r->rsprintf("<script type=\"text/javascript\">\n");
         r->rsprintf("function queryFilename()\n");
         r->rsprintf("{\n");
         r->rsprintf("  var f = prompt('Please enter filename');\n");
         r->rsprintf("  if (f != null && f != '') {\n");
         r->rsprintf("    if (f.indexOf('.') == -1)\n");
         r->rsprintf("       f = f+'.msl';\n");
         r->rsprintf("    ODBSet('/Sequencer/State/Filename', f);\n");
         r->rsprintf("    return true;\n");
         r->rsprintf("  } else\n");
         r->rsprintf("    return false;");
         r->rsprintf("}\n");
         r->rsprintf("</script>\n");
         r->rsprintf("<input type=submit name=cmd onClick=\"return queryFilename();\" value=\"Save Script\">\n");
         r->rsprintf("<input type=submit name=cmd value=\"Cancel Script\">\n");
         r->rsprintf("<div align=\"right\"><a target=\"_blank\" href=\"https://midas.triumf.ca/MidasWiki/index.php/Sequencer\">Syntax Help</a></div>");
         r->rsprintf("</td></tr>\n");
         r->rsprintf("<tr><td colspan=2><textarea rows=30 cols=80 name=\"scripttext\" style=\"font-family:monospace;font-size:medium;\">\n");
         r->rsprintf("</textarea></td></tr>\n");
         r->rsprintf("<tr><td style=\"text-align:center;\" colspan=2>\n");
         r->rsprintf("<input type=submit name=cmd onClick=\"return queryFilename();\" value=\"Save Script\">\n");
         r->rsprintf("<input type=submit name=cmd value=\"Cancel Script\">\n");
         r->rsprintf("</td></tr></table>\n");
      } else if (seq.filename[0]) {
         if (equal_ustring(p->getparam("cmd"), "Edit Script")) {
            r->rsprintf("<tr><th class=\"subStatusTitle\">Script Editor</th></tr>");
            r->rsprintf("<tr><td colspan=2>\n");
            r->rsprintf("<script type=\"text/javascript\">\n");
            r->rsprintf("function queryFilename()\n");
            r->rsprintf("{\n");
            r->rsprintf("  var f = prompt('Please enter new filename');\n");
            r->rsprintf("  if (f != null && f != '') {\n");
            r->rsprintf("    if (f.indexOf('.') == -1)\n");
            r->rsprintf("       f = f+'.msl';\n");
            r->rsprintf("    ODBSet('/Sequencer/State/Filename', f);\n");
            r->rsprintf("    var o=document.createElement('input');o.type='hidden';o.name='cmd';o.value='Save script';\n");
            r->rsprintf("    document.form1.appendChild(o);\n");
            r->rsprintf("    document.form1.submit();\n");
            r->rsprintf("  }\n");
            r->rsprintf("  return false;");
            r->rsprintf("}\n");
            r->rsprintf("</script>\n");
            r->rsprintf("Filename:<a onClick=\"return queryFilename();\" href=\"#\">%s</a>&nbsp;&nbsp;", seq.filename);
            r->rsprintf("<input type=submit name=cmd value=\"Save Script\">\n");
            r->rsprintf("<input type=submit name=cmd value=\"Cancel Script\">\n");
            r->rsprintf("<div align=\"right\"><a target=\"_blank\" href=\"https://midas.triumf.ca/MidasWiki/index.php/Sequencer\">Syntax Help</a></div>");
            r->rsprintf("</td></tr>\n");
            r->rsprintf("<tr><td colspan=2><textarea rows=30 cols=80 name=\"scripttext\" style=\"font-family:monospace;font-size:medium;\">\n");
            strlcpy(str, seq.path, sizeof(str));
            if (strlen(str)>1 && str[strlen(str)-1] != DIR_SEPARATOR)
               strlcat(str, DIR_SEPARATOR_STR, sizeof(str));
            strlcat(str, seq.filename, sizeof(str));
            f = fopen(str, "rt");
            if (f) {
               for (int line=0 ; !feof(f) ; line++) {
                  str[0] = 0;
                  if (fgets(str, sizeof(str), f)) {
                     r->rsputs(str);
                  }
               }
               fclose(f);
            }
            r->rsprintf("</textarea></td></tr>\n");
            r->rsprintf("<tr><td style=\"text-align:center;\" colspan=2>\n");
            r->rsprintf("<input type=submit name=cmd value=\"Save Script\">\n");
            r->rsprintf("<input type=submit name=cmd value=\"Cancel Script\">\n");
            r->rsprintf("</td></tr>\n");
         } else {
            sectionEmpty = 1;
            r->rsprintf("<tr><th class=\"subStatusTitle\">Progress</th></tr>");
            if (seq.stop_after_run){
               sectionEmpty = 0;
               r->rsprintf("<tr id=\"msg\" style=\"display:table-row\"><td colspan=2><b>Sequence will be stopped after current run</b></td></tr>\n");
            } else
               r->rsprintf("<tr id=\"msg\" style=\"display:none\"><td colspan=2><b>Sequence will be stopped after current run</b></td></tr>\n");

            for (i=0 ; i<4 ; i++) {
               r->rsprintf("<tr id=\"loop%d\" style=\"display:none\"><td colspan=2>\n", i);
               r->rsprintf("<table width=\"100%%\"><tr><td style=\"width:150px;\" id=\"loop_label%d\">Loop&nbsp;%d:</td>\n", i, i);
               width = seq_loop_width(seq, i);
               r->rsprintf("<td><table id=\"loopprgs%d\" width=\"%d%%\" height=\"25\">\n", i, width);
               r->rsprintf("<tr><td style=\"background-color:%s;", bar_col[i]);
               r->rsprintf("border:2px solid #000080;border-top:2px solid #E0E0FF;border-left:2px solid #E0E0FF;\">&nbsp;\n");
               r->rsprintf("</td></tr></table></td></tr></table></td></tr>\n");
            }
            if (seq.running) {
               r->rsprintf("<tr id=\"wait_row\" style=\"visible: none;\"><td colspan=2>\n");
               width = seq_wait_width(seq);
               r->rsprintf("<table width=\"100%%\"><tr><td style=\"width:150px\" id=\"wait_label\">Run:</td>\n");
               r->rsprintf("<td><table id=\"runprgs\" width=\"%d%%\" height=\"25\">\n", width);
               r->rsprintf("<tr><td style=\"background-color:#80FF80;border:2px solid #008000;border-top:2px solid #E0E0FF;border-left:2px solid #E0E0FF;\">&nbsp;\n");
               r->rsprintf("</td></tr></table></td></tr></table></td></tr>\n");
               sectionEmpty=0;
            }
            if (seq.paused) {
               r->rsprintf("<tr><td align=\"center\" colspan=2 style=\"background-color:#FFFF80;\"><b>Sequencer is paused</b>\n");
               r->rsprintf("</td></tr>\n");
               sectionEmpty=0;
            }
            if (seq.finished) {
               r->rsprintf("<tr><td colspan=2 style=\"background-color:#80FF80;\"><b>Sequence is finished</b>\n");
               r->rsprintf("</td></tr>\n");
               sectionEmpty=0;
            }
            r->rsprintf("</table>"); //end progress table
            //hide progress table if nothing in it:
            if(sectionEmpty == 1){
               r->rsprintf("<script type=\"text/JavaScript\">");
               r->rsprintf("var element = document.getElementById(\"topTable\");");
               r->rsprintf("element.parentNode.removeChild(element);");
               r->rsprintf("</script>");
            }

            r->rsprintf("<table class=\"mtable\" width=\"100%%\"><tr><th class=\"mtableheader\">Sequencer File</th></tr>");  //start file display table

            r->rsprintf("<tr><td colspan=2><table width=100%%><tr><td>Filename:<b>%s</b></td>", seq.filename);
            r->rsprintf("</td></tr></table></td></tr>\n");

            if (seq.error[0]) {
               r->rsprintf("<tr><td class=\"redLight\" colspan=2><b>");
               strencode(r, seq.error);
               r->rsprintf("</b></td></tr>\n");
            }

            r->rsprintf("<tr><td colspan=2><table width=100%%>");

            /*---- Left (MSL) pane ---- */

            if (stristr(seq.filename, ".msl")) {
               strlcpy(str, seq.path, sizeof(str));
               if (strlen(str)>1 && str[strlen(str)-1] != DIR_SEPARATOR)
                  strlcat(str, DIR_SEPARATOR_STR, sizeof(str));
               strlcat(str, seq.filename, sizeof(str));
               fh = open(str, O_RDONLY | O_TEXT, 0644);
               if (fh > 0) {
                  size = (int)lseek(fh, 0, SEEK_END);
                  lseek(fh, 0, SEEK_SET);
                  buf = (char *)malloc(size+1);
                  size = (int)read(fh, buf, size);
                  buf[size] = 0;
                  close(fh);

                  r->rsprintf("<tr><td style=\"background-color:#FFFFFF; text-align:left;\" colspan=2 valign=\"top\">\n");
                  r->rsprintf("<a onClick=\"sshow_lines();return false;\" href=\"#\" id=\"supperarrow\" style=\"display:none;\">&#x25B2</a><br>\n");

                  pline = buf;
                  for (int line=1 ; *pline ; line++) {
                     strlcpy(str, pline, sizeof(str));
                     if (strchr(str, '\n'))
                        *(strchr(str, '\n')+1) = 0;
                     if (str[0]) {
                        if (line == seq.serror_line)
                           r->rsprintf("<div id=\"sline%d\" style=\"font-family:monospace;background-color:red;\">", line);
                        else if (seq.running && line == seq.current_line_number)
                           r->rsprintf("<div id=\"sline%d\" style=\"font-family:monospace;background-color:#80FF00\">", line);
                        else
                           r->rsprintf("<div id=\"sline%d\" style=\"font-family:monospace\">", line);
                        if (line < 10)
                           r->rsprintf("&nbsp;");
                        if (line < 100)
                           r->rsprintf("&nbsp;");
                        r->rsprintf("%d&nbsp;", line);
                        strencode4(r, str);
                        r->rsprintf("</div>");
                     }
                     if (strchr(pline, '\n'))
                        pline = strchr(pline, '\n')+1;
                     else
                        pline += strlen(pline);
                     if (*pline == '\r')
                        pline++;
                  }
                  r->rsprintf("<a onClick=\"sshow_lines();return false;\" href=\"#\" id=\"slowerarrow\" style=\"display:none;\">&#x25BC</a><br>\n");
                  r->rsprintf("</td>\n");
                  free(buf);
                  buf = NULL;

               } else {
                  if (str[0]) {
                     r->rsprintf("<tr><td colspan=2><b>Cannot open file \"%s\"</td></tr>\n", str);
                  }
               }
            }
         }
         r->rsprintf("</table>"); //end sequencer file table

         /*---- show messages ----*/
         if (seq.running) {
            r->rsprintf("<table class=\"mtable\" width=100%%><tr><th class=\"mtableheader\">Messages</th></tr>");
            r->rsprintf("<tr><td colspan=2>\n");
            r->rsprintf("<div id=\"sequencerMessages\" style=\"font-family:monospace; text-align:left;\">\n");
            r->rsprintf("<a href=\"../?cmd=Messages\">...</a><br>\n");

            cm_msg_retrieve(10, buffer, sizeof(buffer));

            pline = buffer;
            eob = FALSE;

            do {
               strlcpy(line, pline, sizeof(line));

               /* extract single line */
               if (strchr(line, '\n'))
                  *strchr(line, '\n') = 0;
               if (strchr(line, '\r'))
                  *strchr(line, '\r') = 0;

               pline += strlen(line);

               while (*pline == '\r' || *pline == '\n')
                  pline++;

               strlcpy(str, line+11, sizeof(str));
               pc = strchr(line+25, ' ');
               if (pc)
                  strlcpy(str+8, pc, sizeof(str)-9);

               /* check for error */
               if (strstr(line, ",ERROR]"))
                  r->rsprintf("<div style=\"color:white;background-color:red;\" width=100%%>%s</div>", str);
               else
                  r->rsprintf("<div>%s</div>", str);

               r->rsprintf("<br>\n");
            } while (!eob && *pline);

            //some JS to reverse the order of messages, so latest appears at the top:
            r->rsprintf("<script type=\"text/JavaScript\">");
            r->rsprintf("var messages = document.getElementById(\"sequencerMessages\");");
            r->rsprintf("var i = messages.childNodes.length;");
            r->rsprintf("while (i--)");
            r->rsprintf("messages.appendChild(messages.childNodes[i]);");
            r->rsprintf("</script>");

            r->rsprintf("</div></td></tr>\n");
         }
         r->rsprintf("</table>\n");
      } else {
         r->rsprintf("<tr><td><div class=\"subStatusTitle\" style=\"text-align:center\">&nbsp;&nbsp;No script loaded&nbsp;&nbsp;</div></td></tr>\n");
         r->rsprintf("<tr><td style=\"text-align:center\"><input type=submit name=cmd value=\"New Script\"></td></tr></table>\n");
      }
   }

   r->rsprintf("</td></tr></table>"); //end wrapper table
   r->rsprintf("</div>\n"); // closing for <div id="mmain">
   r->rsprintf("</form>\n");
   r->rsprintf("</body></html>\r\n");
}

#endif // OLD_SEQUENCER

/*------------------------------------------------------------------*/

struct Cookies
{
   std::string cookie_pwd;
   std::string cookie_wpwd;
   std::string cookie_cpwd;
   int refresh = 0;
   //int expand_equipment = 0;
};

/*------------------------------------------------------------------*/

void Lock(RequestTrace* t)
{
   gMutex.lock();
   t->fTimeLocked = GetTimeSec();
}

void Unlock(RequestTrace* t)
{
   t->fTimeUnlocked = GetTimeSec();
   gMutex.unlock();
}

/*------------------------------------------------------------------*/

void interprete(Param* p, Return* r, Attachment* a, const Cookies* c, const char *dec_path, RequestTrace* t)
/********************************************************************\

 Routine: interprete

 Purpose: Main interpreter of web commands

 \********************************************************************/
{
   int status;
   HNDLE hkey, hDB;

   //printf("dec_path [%s]\n", dec_path);

   if (strstr(dec_path, "favicon.ico") != 0 ||
       strstr(dec_path, "favicon.png")) {
      send_icon(r, dec_path);
      return;
   }

   const char* password = p->getparam("pwd");
   const char* wpassword = p->getparam("wpwd");
   const char* command = p->getparam("cmd");

   //printf("interprete: dec_path [%s], command [%s]\n", dec_path, command);

   cm_get_experiment_database(&hDB, NULL);
   MVOdb* odb = gOdb;

   if (history_mode) {
      if (equal_ustring(command, "history")) {
         if (equal_ustring(command, "config")) {
            return;
         }

         Lock(t);
         show_hist_page(odb, p, r, dec_path, NULL, NULL, c->refresh);
         Unlock(t);
         return;
      }
      return;
   }

   /* check for password */
   db_find_key(hDB, 0, "/Experiment/Security/Password", &hkey);
   if (!password[0] && hkey) {
      char str[256];
      int size = sizeof(str);
      db_get_data(hDB, hkey, str, &size, TID_STRING);

      /* check for excemption */
      db_find_key(hDB, 0, "/Experiment/Security/Allowed programs/mhttpd", &hkey);
      if (hkey == 0 && strcmp(c->cookie_pwd.c_str(), str) != 0) {
         Lock(t);
         show_password_page(r, dec_path, "");
         Unlock(t);
         return;
      }
   }

   /*---- redirect with cookie if password given --------------------*/

   if (password[0]) {
      r->rsprintf("HTTP/1.1 302 Found\r\n");
      r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());

      time_t now;
      time(&now);

      now += 3600 * 24;

      struct tm gmt_tms;
      gmtime_r(&now, &gmt_tms);

      char str[256];
      strftime(str, sizeof(str), "%A, %d-%b-%Y %H:00:00 GMT", &gmt_tms);

      r->rsprintf("Set-Cookie: midas_pwd=%s; path=/; expires=%s\r\n",
               ss_crypt(password, "mi"), str);

      r->rsprintf("Location: ./\n\n<html>redir</html>\r\n");
      return;
   }

   if (wpassword[0]) {
      /* check if password correct */
      if (!check_web_password(r, hDB, dec_path, ss_crypt(wpassword, "mi"), p->getparam("redir")))
         return;

      r->rsprintf("HTTP/1.1 302 Found\r\n");
      r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());

      time_t now;
      time(&now);

      now += 3600 * 24;

      struct tm gmt_tms;
      gmtime_r(&now, &gmt_tms);

      char str[256];
      strftime(str, sizeof(str), "%A, %d-%b-%Y %H:%M:%S GMT", &gmt_tms);

      r->rsprintf("Set-Cookie: midas_wpwd=%s; path=/; expires=%s\r\n", ss_crypt(wpassword, "mi"), str);

      sprintf(str, "./%s", p->getparam("redir"));
      r->rsprintf("Location: %s\n\n<html>redir</html>\r\n", str);
      return;
   }

   /*---- send sound file -------------------------------------------*/

   if (strlen(dec_path) > 3 &&
       dec_path[strlen(dec_path)-3] == 'm' &&
       dec_path[strlen(dec_path)-2] == 'p' &&
       dec_path[strlen(dec_path)-1] == '3') {
      if (strrchr(dec_path, '/'))
         send_resource(r, strrchr(dec_path, '/')+1);
      else
         send_resource(r, dec_path);
      return;
   }

   /*---- send midas.js and midas.css -------------------------------*/

   if (strstr(dec_path, "midas.js")) {
      send_resource(r, "midas.js");
      return;
   }

   if (strstr(dec_path, "midas.css")) {
      send_resource(r, "midas.css");
      return;
   }

   /*---- send mhttpd.js --------------------------------------------*/

   if (strstr(dec_path, "mhttpd.js")) {
      send_resource(r, "mhttpd.js");
      return;
   }

   /*---- send obsolete.js ------------------------------------------*/

   if (strstr(dec_path, "obsolete.js")) {
      send_resource(r, "obsolete.js");
      return;
   }

   /*---- send the obsolete mhttpd.css ------------------------------*/

   if (strstr(dec_path, "mhttpd.css")) {
      send_resource(r, "mhttpd.css");
      return;
   }

   /*---- send controls.js ------------------------------------------*/

   if (strstr(dec_path, "controls.js")) {
      send_resource(r, "controls.js");
      return;
   }

   /*---- send example web page -------------------------------------*/

   if (equal_ustring(command, "example")) {
      send_resource(r, "example.html");
      return;
   }

   /*---- send example custom page -------------------------------------*/

   if (equal_ustring(command, "custom_example")) {
      send_resource(r, "custom_example.html");
      return;
   }

   /*---- script command --------------------------------------------*/

   if (p->getparam("script") && *p->getparam("script")) {
      char str[256];

      sprintf(str, "%s?script=%s", dec_path, p->getparam("script")); // FIXME: overflows str[]
      if (!check_web_password(r, hDB, dec_path, c->cookie_wpwd.c_str(), str))
         return;

      std::string path;
      path += "/Script/";
      path += p->getparam("script");

      Lock(t);

      cm_exec_script(path.c_str());

      Unlock(t);

      if (p->isparam("redir"))
         redirect2(r, p->getparam("redir"));
      else
         redirect2(r, "");

      return;
   }

   /*---- customscript command --------------------------------------*/

   if (p->getparam("customscript") && *p->getparam("customscript")) {
      char str[256];

      sprintf(str, "%s?customscript=%s", dec_path, p->getparam("customscript")); // FIXME: overflows str[]
      if (!check_web_password(r, hDB, dec_path, c->cookie_wpwd.c_str(), str))
         return;

      std::string path;
      path += "/CustomScript/";
      path += p->getparam("customscript");

      Lock(t);

      cm_exec_script(path.c_str());

      Unlock(t);

      if (p->isparam("redir"))
         redirect2(r, p->getparam("redir"));
      else
         redirect2(r, str);

      return;
   }

   /*---- send the new html pages -----------------------------------*/

   if (equal_ustring(command, "start")) {
      send_resource(r, "start.html");
      return;
   }

   if (equal_ustring(command, "") && strlen(dec_path) == 0) {
       send_resource(r, "status.html");
       return;
   }

   if (equal_ustring(command, "status")) {
       send_resource(r, "status.html");
       return;
   }

   if (equal_ustring(command, "newODB")) {
      send_resource(r, "odb.html");
      return;
   }

   if (equal_ustring(command, "programs")) {
      send_resource(r, "programs.html");
      return;
   }

   if (equal_ustring(command, "alarms")) {
      send_resource(r, "alarms.html");
      return;
   }

   if (equal_ustring(command, "transition")) {
      send_resource(r, "transition.html");
      return;
   }

   if (equal_ustring(command, "messages")) {
      send_resource(r, "messages.html");
      return;
   }

   if (equal_ustring(command, "config") &&
       !(dec_path[0] == 'H' && dec_path[1] == 'S' && dec_path[2] == '/')) {
      send_resource(r, "config.html");
      return;
   }

   if (equal_ustring(command, "chat")) {
      send_resource(r, "chat.html");
      return;
   }

   if (equal_ustring(command, "buffers")) {
      send_resource(r, "buffers.html");
      return;
   }

   if (equal_ustring(command, "Show elog")) {
      send_resource(r, "elog_show.html");
      return;
   }

   if (equal_ustring(command, "Query elog")) {
      send_resource(r, "elog_query_form.html");
      return;
   }

   if (equal_ustring(command, "New elog")) {
      send_resource(r, "elog_edit.html");
      return;
   }

   if (equal_ustring(command, "Edit elog")) {
      send_resource(r, "elog_edit.html");
      return;
   }

   if (equal_ustring(command, "Reply Elog")) {
      send_resource(r, "elog_edit.html");
      return;
   }

   if (equal_ustring(command, "Last elog")) {
      send_resource(r, "elog_show.html");
      return;
   }

   if (equal_ustring(command, "Submit Query")) {
      send_resource(r, "elog_query.html");
      return;
   }

   if (equal_ustring(dec_path, "spinning-wheel.gif")) {
      send_resource(r, "spinning-wheel.gif");
      return;
   }

   /*---- java script commands --------------------------------------*/

   if (equal_ustring(command, "jset") ||
       equal_ustring(command, "jget") ||
       equal_ustring(command, "jcopy") ||
       equal_ustring(command, "jpaste") ||
       equal_ustring(command, "jkey") ||
       equal_ustring(command, "jcreate") ||
       equal_ustring(command, "jresize") ||
       equal_ustring(command, "jlink") ||
       equal_ustring(command, "jrename") ||
       equal_ustring(command, "jreorder") ||
       equal_ustring(command, "jdelete") ||
       equal_ustring(command, "jmsg") ||
       equal_ustring(command, "jalm") ||
       equal_ustring(command, "jgenmsg") ||
       equal_ustring(command, "jrpc_rev0") ||
       equal_ustring(command, "jrpc_rev1") ||
       equal_ustring(command, "jrpc")) {
      Lock(t);
      javascript_commands(p, r, c->cookie_cpwd.c_str());
      Unlock(t);
      return;
   }

   /*---- history command -------------------------------------------*/

   if (equal_ustring(command, "oldhistory")) {
      Lock(t);
      show_hist_page(odb, p, r, dec_path, NULL, NULL, c->refresh);
      Unlock(t);
      return;
   }

   if (equal_ustring(command, "history")) {
      send_resource(r, "history.html");
      return;
   }

   /*---- MSCB command ----------------------------------------------*/

   if (equal_ustring(command, "MSCB")) {
      if (equal_ustring(command, "set")) {
         std::string str;
         str += dec_path;
         str += "?";
         str += add_param_to_url("cmd", command);
         if (!check_web_password(r, hDB, dec_path, c->cookie_wpwd.c_str(), str.c_str()))
            return;
      }

      Lock(t);

#ifdef HAVE_MSCB
      show_mscb_page(p, r, c->refresh);
#else
      show_error(r, "MSCB support not compiled into this version of mhttpd");
#endif

      Unlock(t);
      return;
   }

   /*---- help command ----------------------------------------------*/

   if (equal_ustring(command, "help")) {
      Lock(t);
      show_help_page(r, dec_path);
      Unlock(t);
      return;
   }

   /*---- trigger equipment readout ---------------------------*/

   if (strncmp(command, "Trigger", 7) == 0) {
      std::string cmd;
      cmd += "?cmd=";
      cmd += command;
      if (!check_web_password(r, hDB, dec_path, c->cookie_wpwd.c_str(), cmd.c_str())) {
         return;
      }

      Lock(t);

      /* extract equipment name */
      char eq_name[NAME_LENGTH];

      strlcpy(eq_name, command + 8, sizeof(eq_name));
      if (strchr(eq_name, ' '))
         *strchr(eq_name, ' ') = 0;

      /* get frontend name */
      std::string path;
      path += "/Equipment/";
      path += eq_name;
      path += "/Common/Frontend name";
      char fe_name[NAME_LENGTH];
      int size = NAME_LENGTH;
      db_get_value(hDB, 0, path.c_str(), fe_name, &size, TID_STRING, TRUE);

      /* and ID */
      path = "";
      path += "/Equipment/";
      path += eq_name;
      path += "/Common/Event ID";
      WORD event_id = 0;
      size = sizeof(event_id);
      db_get_value(hDB, 0, path.c_str(), &event_id, &size, TID_WORD, TRUE);

      if (cm_exist(fe_name, FALSE) != CM_SUCCESS) {
         std::string str;
         str += "Frontend \"";
         str += fe_name;
         str += "\" not running!";
         show_error(r, str.c_str());
      } else {
         HNDLE hconn;
         status = cm_connect_client(fe_name, &hconn);
         if (status != RPC_SUCCESS) {
            std::string str;
            str += "Cannot connect to frontend \"";
            str += fe_name;
            str +="\" !";
            show_error(r, str.c_str());
         } else {
            status = rpc_client_call(hconn, RPC_MANUAL_TRIG, event_id);
            if (status != CM_SUCCESS)
               show_error(r, "Error triggering event");
            else
               redirect(r, "");

            //cm_disconnect_client(hconn, FALSE);
         }
      }

      Unlock(t);

      return;
   }

   /*---- switch to next subrun -------------------------------------*/

   if (strncmp(command, "Next Subrun", 11) == 0) {
      int i = TRUE;
      db_set_value(hDB, 0, "/Logger/Next subrun", &i, sizeof(i), 1, TID_BOOL);
      redirect(r, "");
      return;
   }

   /*---- cancel command --------------------------------------------*/

   if (equal_ustring(command, "cancel")) {
      if (p->isparam("redir"))
         redirect(r, p->getparam("redir"));
      else
         redirect(r, "");
      return;
   }

   /*---- set command -----------------------------------------------*/

   if (equal_ustring(command, "set")) {
      char str[256];
      strlcpy(str, "?cmd=set", sizeof(str));
      if (!check_web_password(r, hDB, dec_path, c->cookie_wpwd.c_str(), str))
         return;

      const char* group = p->getparam("group");
      int index = atoi(p->getparam("index"));
      const char* value = p->getparam("value");

      Lock(t);
      show_set_page(p, r, group, index, value);
      Unlock(t);
      return;
   }

   /*---- find command ----------------------------------------------*/

   if (equal_ustring(command, "find")) {
      const char* value = p->getparam("value");
      Lock(t);
      show_find_page(r, value);
      Unlock(t);
      return;
   }

   /*---- CAMAC CNAF command ----------------------------------------*/

   if (equal_ustring(command, "CNAF") || strncmp(dec_path, "CNAF", 4) == 0) {
      if (!check_web_password(r, hDB, dec_path, c->cookie_wpwd.c_str(), "?cmd=CNAF"))
         return;

      Lock(t);
      show_cnaf_page(p, r);
      Unlock(t);
      return;
   }

   /*---- ELog command ----------------------------------------------*/

   if (equal_ustring(command, "elog")) {
      /* redirect to external ELOG if URL present */
      cm_get_experiment_database(&hDB, NULL);
      BOOL external_elog = FALSE;
      std::string external_elog_url;
      int size = sizeof(external_elog);
      status = db_get_value(hDB, 0, "/Elog/External Elog", &external_elog, &size, TID_BOOL, TRUE);
      status = db_get_value_string(hDB, 0, "/Elog/URL", 0, &external_elog_url, TRUE);
      if (external_elog && (external_elog_url.length() > 0)) {
         redirect(r, external_elog_url.c_str());
         return;
      }
      send_resource(r, "elog_show.html");
      return;
   }

   // special processing for "Elog last 7d", etc

   char cmdx[32];
   strlcpy(cmdx, command, sizeof(cmdx));
   cmdx[9] = 0;

   if (equal_ustring(cmdx, "Elog last")) {
      // "Elog last 7d", etc
      send_resource(r, "elog_query.html");
      return;
   }

   if (equal_ustring(command, "Create ELog from this page")) {
      std::string redir;
      redir += "?cmd=New+elog";
      redir += "&odb_path=";
      redir += p->getparam("odb_path");
      redirect(r, redir.c_str());
      return;
   }

   if (equal_ustring(command, "Submit elog")) {
      Lock(t);
      submit_elog(odb, p, r, a);
      Unlock(t);
      return;
   }

   if (equal_ustring(command, "elog_att")) {
      Lock(t);
      show_elog_attachment(p, r, dec_path);
      Unlock(t);
      return;
   }

   /*---- accept command --------------------------------------------*/

   if (equal_ustring(command, "accept")) {
      int refresh = atoi(p->getparam("refr"));

      /* redirect with cookie */
      r->rsprintf("HTTP/1.1 302 Found\r\n");
      r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
      r->rsprintf("Content-Type: text/html; charset=%s\r\n", HTTP_ENCODING);

      time_t now;
      time(&now);

      now += 3600 * 24 * 365;

      struct tm gmt_tms;
      gmtime_r(&now, &gmt_tms);

      char str[256];
      strftime(str, sizeof(str), "%A, %d-%b-%Y %H:00:00 GMT", &gmt_tms);

      r->rsprintf("Set-Cookie: midas_refr=%d; path=/; expires=%s\r\n", refresh, str);
      r->rsprintf("Location: ./\r\n\r\n<html>redir</html>\r\n");

      return;
   }

   /*---- slow control display --------------------------------------*/

   if (equal_ustring(command, "eqtable")) {
      Lock(t);
      show_eqtable_page(p, r, c->refresh);
      Unlock(t);
      return;
   }

   /*---- sequencer page --------------------------------------------*/

#ifdef OLD_SEQUENCER
   if (equal_ustring(command, "NewSequencer")) {
      send_resource(r, "sequencer.html");
      return;
   }
#else
   if (equal_ustring(command, "Sequencer")) {
      send_resource(r, "sequencer.html");
      return;
   }
#endif

   if (equal_ustring(command, "seq")) {
      send_resource(r, "sequencer.html");
      return;
   }

   if (equal_ustring(command, "start_script")) {
      send_resource(r, "start_script.html");
      return;
   }

   if (equal_ustring(command, "load_script")) {
      send_resource(r, "load_script.html");
      return;
   }

   if (equal_ustring(command, "edit_script")) {
      send_resource(r, "edit_script.html");
      return;
   }

#ifdef OLD_SEQUENCER
   if (equal_ustring(command, "Sequencer")) {
      Lock(t);
      show_seq_page(p, r);
      Unlock(t);
      return;
   }

   if (equal_ustring(command, "Start script")) {
      Lock(t);
      show_seq_page(p, r);
      Unlock(t);
      return;
   }

   if (equal_ustring(command, "Cancel script")) {
      Lock(t);
      show_seq_page(p, r);
      Unlock(t);
      return;
   }

   if (equal_ustring(command, "Load script")) {
      Lock(t);
      show_seq_page(p, r);
      Unlock(t);
      return;
   }

   if (equal_ustring(command, "New script")) {
      Lock(t);
      show_seq_page(p, r);
      Unlock(t);
      return;
   }

   if (equal_ustring(command, "Save script")) {
      Lock(t);
      show_seq_page(p, r);
      Unlock(t);
      return;
   }

   if (equal_ustring(command, "Edit script")) {
      Lock(t);
      show_seq_page(p, r);
      Unlock(t);
      return;
   }

   if (equal_ustring(command, "SPause")) {
      Lock(t);
      show_seq_page(p, r);
      Unlock(t);
      return;
   }

   if (equal_ustring(command, "SResume")) {
      Lock(t);
      show_seq_page(p, r);
      Unlock(t);
      return;
   }

   if (equal_ustring(command, "Stop immediately")) {
      Lock(t);
      show_seq_page(p, r);
      Unlock(t);
      return;
   }

   if (equal_ustring(command, "Stop after current run")) {
      Lock(t);
      show_seq_page(p, r);
      Unlock(t);
      return;
   }

   if (equal_ustring(command, "Cancel \'Stop after current run\'")) {
      Lock(t);
      show_seq_page(p, r);
      Unlock(t);
      return;
   }
#endif

   /*---- show ODB --------------------------------------------------*/

   if (equal_ustring(command, "odb")) {
      int write_access = TRUE;
      db_find_key(hDB, 0, "/Experiment/Security/Web Password", &hkey);
      if (hkey) {
         char str[256];
         int size = sizeof(str);
         db_get_data(hDB, hkey, str, &size, TID_STRING);
         if (strcmp(c->cookie_wpwd.c_str(), str) == 0)
            write_access = TRUE;
         else
            write_access = FALSE;
      }

      std::string odb_path;
      if (p->getparam("odb_path") && *p->getparam("odb_path"))
         odb_path = p->getparam("odb_path");

      Lock(t);
      show_odb_page(p, r, odb_path.c_str(), write_access);
      Unlock(t);
      return;
   }

   /*---- old ODB path ----------------------------------------------*/

   if ((command[0]==0) && dec_path[0]) {
      if (equal_ustring(dec_path, "root")) {
         std::string new_url = "./?cmd=odb";
         //printf("redirect old odb path url [%s] to [%s]\n", dec_path, new_url.c_str());
         redirect_307(r, new_url.c_str());
         return;
      }
   }

   if ((command[0]==0) && dec_path[0]) {
      HNDLE hkey;
      status = db_find_key(hDB, 0, dec_path, &hkey);
      //printf("try odb path [%s], status %d\n", dec_path, status);
      if (status == DB_SUCCESS) {
         int level = 0;
         for (const char* s = dec_path; *s; s++) {
            if (*s == '/')
               level++;
         }
         std::string new_url;
         if (level == 0) {
            // Top-level directory like /Logger, (which appears in dec_path as "Logger")
            new_url += "./";
         } else {
            for (int i=0; i<level; i++) {
               if (i>0)
                  new_url += "/";
               new_url += "..";
            }
         }
         new_url += "?cmd=odb";
         new_url += "&odb_path=";
         new_url += urlEncode(dec_path);
         //printf("redirect old odb path url [%s] to [%s]\n", dec_path, new_url.c_str());
         redirect_307(r, new_url.c_str());
         return;
      }
   }

   /*---- custom page -----------------------------------------------*/

   if (equal_ustring(command, "custom")) {
      Lock(t);
      show_custom_page(p, r, c->cookie_cpwd.c_str());
      Unlock(t);
      return;
   }

   /*---- custom page accessed by direct URL that used to be under /CS/... ----*/

   if (db_find_key(hDB, 0, "/Custom", &hkey) == DB_SUCCESS && dec_path[0]) {
      std::string odb_path;
      std::string value;
      int status;

      odb_path = "";
      odb_path += "/Custom/Images/";
      odb_path += dec_path;
      odb_path += "/Background";

      status = db_get_value_string(hDB, 0, odb_path.c_str(), 0, &value, FALSE);

      //printf("Try custom gif [%s] status %d\n", odb_path.c_str(), status);

      if (status == DB_SUCCESS) {
         if (strstr(dec_path, "..")) {
            std::string str;
            str += "Invalid custom gif name \'";
            str += dec_path;
            str += "\' contains \'..\'";
            show_error_404(r, str.c_str());
            return;
         }

         Lock(t);
         show_custom_gif(r, dec_path);
         Unlock(t);
         return;
      }

      bool found_custom = false;

      odb_path = "";
      odb_path += "/Custom/";
      odb_path += dec_path;

      status = db_get_value_string(hDB, 0, odb_path.c_str(), 0, &value, FALSE);

      //printf("Try [%s] status %d\n", odb_path.c_str(), status);

      if (status == DB_SUCCESS) {
         found_custom = true;
      } else {
         odb_path = "";
         odb_path += "/Custom/";
         odb_path += dec_path;
         odb_path += "&";

         status = db_get_value_string(hDB, 0, odb_path.c_str(), 0, &value, FALSE);

         //printf("Try [%s] status %d\n", odb_path.c_str(), status);

         if (status == DB_SUCCESS) {
            found_custom = true;
         } else {
            odb_path = "";
            odb_path += "/Custom/";
            odb_path += dec_path;
            odb_path += "!";

            status = db_get_value_string(hDB, 0, odb_path.c_str(), 0, &value, FALSE);

            //printf("Try [%s] status %d\n", odb_path.c_str(), status);

            if (status == DB_SUCCESS) {
               found_custom = true;
            }
         }
      }

      if (found_custom) {
         //printf("custom file: serving [%s] value [%s]\n", dec_path, value.c_str());
         if (strstr(dec_path, "..")) {
            std::string str;
            str += "Invalid custom page name \'";
            str += dec_path;
            str += "\' contains \'..\'";
            show_error_404(r, str.c_str());
            return;
         }

         p->setparam("page", dec_path);
         Lock(t);
         show_custom_page(p, r, c->cookie_cpwd.c_str());
         Unlock(t);
         return;
      }
   }

   /* new custom pages */
   if (db_find_key(hDB, 0, "/Custom", &hkey) == DB_SUCCESS && dec_path[0]) {
      std::string custom_path;
      status = db_get_value_string(hDB, 0, "/Custom/Path", 0, &custom_path, TRUE);
      if ((status == DB_SUCCESS) && (custom_path.length() > 0)) {
         if (strstr(dec_path, "..")) {
            std::string str;
            str += "Invalid custom file name \'";
            str += dec_path;
            str += "\' contains \'..\'";
            show_error_404(r, str.c_str());
            return;
         }

         std::string full_filename = add_custom_path(dec_path);

         // if custom file exists, send it (like normal web server)
         if (ss_file_exist(full_filename.c_str())) {
            send_file(r, full_filename);
            return;
         }
      }
   }

   /*---- redirect if web page --------------------------------------*/

   //if (strlen(command) > 0) {
   //   if (send_resource(r, std::string(command) + ".html", false))
   //      return;
   //}

   /*---- serve url as a resource file ------------------------------*/

   if (strlen(p->getparam("path")) > 0) {
      if (send_resource(r, p->getparam("path"), false)) {
         return;
      }
   }

   /*---- show status -----------------------------------------------*/

   if (elog_mode) {
      redirect(r, "EL/");
      return;
   }

   /* header */
   r->rsprintf("HTTP/1.1 400 Bad Request\r\n");
   r->rsprintf("Server: MIDAS HTTP %s\r\n", mhttpd_revision());
   r->rsprintf("Content-Type: text/plain; charset=%s\r\n", HTTP_ENCODING);
   r->rsprintf("\r\n");
   r->rsprintf("Error: Invalid URL \"%s\" or query \"%s\" or command \"%s\"\n", p->getparam("path"), p->getparam("query"), command);
}

/*------------------------------------------------------------------*/

void decode_query(Param* pp, const char *query_string)
{
   int len = strlen(query_string);
   char *buf = (char *)malloc(len+1);
   memcpy(buf, query_string, len+1);
   char* p = buf;
   p = strtok(p, "&");
   while (p != NULL) {
      char *pitem = p;
      p = strchr(p, '=');
      if (p != NULL) {
         *p++ = 0;
         urlDecode(pitem); // parameter name
         if (!equal_ustring(pitem, "format"))
            urlDecode(p); // parameter value

         pp->setparam(pitem, p); // decoded query parameters

         p = strtok(NULL, "&");
      }
   }
   free(buf);
}

void decode_get(Return* rr, char *string, const Cookies* c, const char* url, const char* query_string, RequestTrace* t)
{
   char path[256];

   //printf("decode_get: string [%s], decode_url %d, url [%s], query_string [%s]\n", string, decode_url, url, query_string);

   Param* param = new Param();

   param->initparam();

   if (url)
      strlcpy(path, url + 1, sizeof(path));     /* strip leading '/' */
   else {
      strlcpy(path, string + 1, sizeof(path));     /* strip leading '/' */

      if (strchr(path, '?'))
         *strchr(path, '?') = 0;
   }

   param->setparam("path", path);

   assert(query_string != NULL);

   decode_query(param, query_string);

   param->setparam("query", query_string);

   char dec_path[256];
   strlcpy(dec_path, path, sizeof(dec_path));

   interprete(param, rr, NULL, c, dec_path, t);

   param->freeparam();
   delete param;
}

/*------------------------------------------------------------------*/

void decode_post(Return* rr, const char *header, const char *string, const char *boundary, int length, const Cookies* c, const char* url, RequestTrace* t)
{
   bool debug_decode_post = false;
   
   Param* param = new Param;

   param->initparam();

   char path[256];

   if (url)
      strlcpy(path, url + 1, sizeof(path));     /* strip leading '/' */
   else {
      strlcpy(path, header + 1, sizeof(path));     /* strip leading '/' */
      if (strchr(path, '?'))
         *strchr(path, '?') = 0;
      if (strchr(path, ' '))
         *strchr(path, ' ') = 0;
   }
   param->setparam("path", path); // undecoded path

   Attachment* a = new Attachment;

   const char* pinit = string;

   /* return if no boundary defined */
   if (!boundary[0])
      return;

   if (strstr(string, boundary))
      string = strstr(string, boundary) + strlen(boundary);

   if (debug_decode_post)
      printf("decode_post: -->[%s]<--\n", string);

   do {
      //printf("decode_post: [%s]\n", string);
      if (strstr(string, "name=")) {
         const char* pitem = strstr(string, "name=") + 5;
         if (*pitem == '\"')
            pitem++;

         //printf("decode_post: pitem [%s]\n", pitem);

         if (strncmp(pitem, "attfile", 7) == 0) {
            int n = pitem[7] - '1';

            char file_name[256];
            file_name[0] = 0;

            /* evaluate file attachment */
            if (strstr(pitem, "filename=")) {
               const char* p = strstr(pitem, "filename=") + 9;
               if (*p == '\"')
                  p++;
               if (strstr(p, "\r\n\r\n"))
                  string = strstr(p, "\r\n\r\n") + 4;
               else if (strstr(p, "\r\r\n\r\r\n"))
                  string = strstr(p, "\r\r\n\r\r\n") + 6;

               strlcpy(file_name, p, sizeof(file_name));

               char* pp = file_name;
               if (strchr(pp, '\"'))
                  *strchr(pp, '\"') = 0;

               /* set attachment filename */
               char str[256];
               sprintf(str, "attachment%d", n);
               if (debug_decode_post)
                  printf("decode_post: [%s] = [%s]\n", str, file_name);
               param->setparam(str, file_name); // file_name should be decoded?
            }

            /* find next boundary */
            const char* ptmp = string;
            const char* p = NULL;
            do {
               while (*ptmp != '-')
                  ptmp++;

               p = strstr(ptmp, boundary);
               if (p != NULL) {
                  while (*p == '-')
                     p--;
                  if (*p == 10)
                     p--;
                  if (*p == 13)
                     p--;
                  p++;
                  break;
               } else
                  ptmp += strlen(ptmp);

            } while (TRUE);

            /* save pointer to file */
            if (file_name[0]) {
               size_t size = (POINTER_T) p - (POINTER_T) string;
               char* buf = (char*)malloc(size+1);
               if (!buf) {
                  return;
               }
               memcpy(buf, string, size);
               buf[size] = 0; // make sure string is NUL terminated
               a->attachment_buffer[n] = buf;
               a->attachment_size[n] = size;
               if (debug_decode_post)
                  printf("decode_post: attachment[%d] size %d data --->[%s]<---\n", n, (int)a->attachment_size[n], a->attachment_buffer[n]);
            }

            string = strstr(p, boundary) + strlen(boundary);
         } else {
            const char* p = pitem;
            if (strstr(p, "\r\n\r\n"))
               p = strstr(p, "\r\n\r\n") + 4;
            else if (strstr(p, "\r\r\n\r\r\n"))
               p = strstr(p, "\r\r\n\r\r\n") + 6;

            char* ppitem = (char*)strchr(pitem, '\"'); // NB: defeat "const char* string"
            if (ppitem)
               *ppitem = 0;

            char* pb = (char*)(strstr(p, boundary)); // NB: defeat "const char* string"
            if (pb) {
               string = pb + strlen(boundary);
               *pb = 0;
               char* ptmp = (char*)(p + (strlen(p) - 1)); // NB: defeat "const char* string"
               while (*ptmp == '-' || *ptmp == '\n' || *ptmp == '\r')
                  *ptmp-- = 0;
            } else {
               show_error(rr, "Invalid POST request");
               return;
            }
            if (debug_decode_post)
               printf("decode_post: [%s] = [%s]\n", pitem, p);
            param->setparam(pitem, p); // in decode_post()
         }

         while (*string == '-' || *string == '\n' || *string == '\r')
            string++;
      }

   } while ((POINTER_T) string - (POINTER_T) pinit < length);

   char dec_path[256];
   strlcpy(dec_path, path, sizeof(dec_path));

   interprete(param, rr, a, c, dec_path, t);

   delete a;
   delete param;
}

/*------------------------------------------------------------------*/

INT check_odb_records(MVOdb* odb)
{
   HNDLE hDB, hKeyEq, hKey;
   RUNINFO_STR(runinfo_str);
   int i, status;
   KEY key;

   /* check /Runinfo structure */
   status = cm_get_experiment_database(&hDB, NULL);
   assert(status == DB_SUCCESS);

   status = db_check_record(hDB, 0, "/Runinfo", strcomb1(runinfo_str).c_str(), FALSE);
   if (status == DB_STRUCT_MISMATCH) {
      status = db_check_record(hDB, 0, "/Runinfo", strcomb1(runinfo_str).c_str(), TRUE);
      if (status == DB_SUCCESS) {
         cm_msg(MINFO, "check_odb_records", "ODB subtree /Runinfo corrected successfully");
      } else {
         cm_msg(MERROR, "check_odb_records", "Cannot correct ODB subtree /Runinfo, db_check_record() status %d", status);
         return 0;
      }
   } else if (status == DB_NO_KEY) {
      cm_msg(MERROR, "check_odb_records", "ODB subtree /Runinfo does not exist");
      status = db_create_record(hDB, 0, "/Runinfo", strcomb1(runinfo_str).c_str());
      if (status == DB_SUCCESS) {
         cm_msg(MINFO, "check_odb_records", "ODB subtree /Runinfo created successfully");
      } else {
         cm_msg(MERROR, "check_odb_records", "Cannot create ODB subtree /Runinfo, db_create_record() status %d", status);
         return 0;
      }
   } else if (status != DB_SUCCESS) {
      cm_msg(MERROR, "check_odb_records", "Cannot correct ODB subtree /Runinfo, db_check_record() status %d", status);
      return 0;
   }

   /* check /Equipment/<name>/Common structures */
   if (db_find_key(hDB, 0, "/equipment", &hKeyEq) == DB_SUCCESS) {
      for (i = 0 ;; i++) {
         db_enum_key(hDB, hKeyEq, i, &hKey);
         if (!hKey)
            break;
         db_get_key(hDB, hKey, &key);

         status = db_check_record(hDB, hKey, "Common", EQUIPMENT_COMMON_STR, FALSE);
         if (status == DB_STRUCT_MISMATCH) {
            status = db_check_record(hDB, hKey, "Common", EQUIPMENT_COMMON_STR, TRUE);
            if (status == DB_SUCCESS) {
               cm_msg(MINFO, "check_odb_records", "ODB subtree /Equipment/%s/Common corrected successfully", key.name);
            } else {
               cm_msg(MERROR, "check_odb_records", "Cannot correct ODB subtree /Equipment/%s/Common, db_check_record() status %d", key.name, status);
            }
         } else if (status != DB_SUCCESS) {
            cm_msg(MERROR, "check_odb_records", "Cannot correct ODB subtree /Equipment/%s/Common, db_check_record() status %d", key.name, status);
         }
      }
   }

   return CM_SUCCESS;
}


/*------------------------------------------------------------------*/

std::atomic_bool _abort{false};

void ctrlc_handler(int sig)
{
   _abort = true;
}

/*------------------------------------------------------------------*/

#ifdef HAVE_MONGOOSE6
static std::vector<std::string> gUserAllowedHosts;
#endif
static std::vector<std::string> gAllowedHosts;
#ifdef HAVE_MONGOOSE6
static const std::string gOdbAllowedHosts = "/Experiment/Security/mhttpd hosts/Allowed hosts";
#endif

#ifdef HAVE_MONGOOSE6
static void load_allowed_hosts(HNDLE hDB, HNDLE hKey, int index, void* info)
{
   if (hKey != 0)
      cm_msg(MINFO, "load_allowed_hosts", "Reloading mhttpd hosts access control list via hotlink callback");

   gAllowedHosts.clear();

   // copy the user allowed hosts
   for (unsigned int i=0; i<gUserAllowedHosts.size(); i++)
      gAllowedHosts.push_back(gUserAllowedHosts[i]);

   int total = 0;
   int last = 0;
   for (int i=0; ; i++) {
      std::string s;
      int status = db_get_value_string(hDB, 0, gOdbAllowedHosts.c_str(), i, &s, FALSE);
      //printf("get %d, status %d, string [%s]\n", i, status, s.c_str());
      if (status != DB_SUCCESS) {
         total = i;
         break;
      }

      if (s.length() < 1) // skip emties
         continue;

      if (s[0] == '#') // skip commented-out entries
         continue;

      //printf("add allowed hosts %d [%s]\n", i, s.c_str());
      gAllowedHosts.push_back(s);
      last = i;
   }

   //printf("total %d, last %d\n", total, last);

   if (total - last < 5) {
      int new_size = last + 10;
      //printf("new size %d\n", new_size);
      int status = db_resize_string(hDB, 0, gOdbAllowedHosts.c_str(), new_size, 256);
      if (status != DB_SUCCESS) {
         cm_msg(MERROR, "load_allowed_hosts", "Cannot resize the allowed hosts access control list, db_resize_string(%d) status %d", new_size, status);
      }
   }
}

static int init_allowed_hosts()
{
   HNDLE hDB;
   HNDLE hKey;
   int status;

   cm_get_experiment_database(&hDB, NULL);

   // create "allowed hosts" so we can watch it

   std::string s;
   status = db_get_value_string(hDB, 0, gOdbAllowedHosts.c_str(), 0, &s, TRUE);

   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "init_allowed_hosts", "Cannot create the mhttpd hosts access control list, db_get_value_string() status %d", status);
      return status;
   }

   status = db_find_key(hDB, 0, gOdbAllowedHosts.c_str(), &hKey);

   if (status != DB_SUCCESS || hKey == 0) {
      cm_msg(MERROR, "init_allowed_hosts", "Cannot find the mhttpd hosts access control list, db_find_key() status %d", status);
      return status;
   }

   load_allowed_hosts(hDB, 0, 0, NULL);

   status = db_watch(hDB, hKey, load_allowed_hosts, NULL);

   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "init_allowed_hosts", "Cannot watch the mhttpd hosts access control list, db_watch() status %d", status);
      return status;
   }

   return SUCCESS;
}

   int check_midas_acl(const struct sockaddr *sa, int len) {
      // access control list is empty?
      if (gAllowedHosts.size() == 0)
         return 1;

      char hname[NI_MAXHOST];
      hname[0] = 0;

      int status;
      const char* status_string = "success";

      status = getnameinfo(sa, len, hname, sizeof(hname), NULL, 0, 0);

      if (status)
         status_string = gai_strerror(status);

      //printf("connection from [%s], status %d (%s)\n", hname, status, status_string);

      if (status != 0) {
         printf("Rejecting connection from \'%s\', getnameinfo() status %d (%s)\n", hname, status, status_string);
         return 0;
      }

      /* always permit localhost */
      if (strcmp(hname, "localhost.localdomain") == 0)
         return 1;
      if (strcmp(hname, "localhost") == 0)
         return 1;

      for (unsigned int i=0 ; i<gAllowedHosts.size() ; i++)
         if (gAllowedHosts[i] == hname) {
            return 1;
         }

      printf("Rejecting connection from \'%s\'\n", hname);
      return 0;
   }

int open_listening_socket(int port)
{
   int status;
   struct sockaddr_in bind_addr;

   /* create a new socket */
   int lsock = socket(AF_INET, SOCK_STREAM, 0);

   if (lsock == -1) {
      printf("Cannot create socket, socket() errno %d (%s)\n", errno, strerror(errno));
      return -1;
   }

   /* bind local node name and port to socket */
   memset(&bind_addr, 0, sizeof(bind_addr));
   bind_addr.sin_family = AF_INET;
   bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
   bind_addr.sin_port = htons((short) port);

   /* try reusing address */
   int flag = 1;
   status = setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, (char *) &flag, sizeof(INT));

   if (status < 0) {
      printf("Cannot setsockopt(SOL_SOCKET, SO_REUSEADDR), errno %d (%s)\n", errno, strerror(errno));
      return -1;
   }

   status = bind(lsock, (struct sockaddr *) &bind_addr, sizeof(bind_addr));

   if (status < 0) {
      printf("Cannot bind() to port %d, bind() errno %d (%s)\n", port, errno, strerror(errno));
      return -1;
   }

   /* listen for connection */
   status = listen(lsock, SOMAXCONN);
   if (status < 0) {
      printf("Cannot listen() on port %d, errno %d (%s), bye!\n", port, errno, strerror(errno));
      return -1;
   }

   printf("mhttpd is listening on port %d\n", port);

   return lsock;
}
#endif

/*------------------------------------------------------------------*/

int try_file_mg(const char* try_dir, const char* filename, std::string& path, FILE** fpp, bool trace)
{
   if (fpp)
      *fpp = NULL;
   if (!try_dir)
      return SS_FILE_ERROR;
   if (strlen(try_dir) < 1)
      return SS_FILE_ERROR;

   path = try_dir;
   if (path[path.length()-1] != DIR_SEPARATOR)
      path += DIR_SEPARATOR_STR;
   path += filename;

   FILE* fp = fopen(path.c_str(), "r");

   if (trace) {
      if (fp)
         printf("file \"%s\": OK!\n", path.c_str());
      else
         printf("file \"%s\": not found.\n", path.c_str());
   }

   if (!fp)
      return SS_FILE_ERROR;
   else if (fpp)
      *fpp = fp;
   else
      fclose(fp);

   return SUCCESS;
}

int find_file_mg(const char* filename, std::string& path, FILE** fpp, bool trace)
{
   char exptdir[256];
   cm_get_path(exptdir, sizeof(exptdir));

   if (try_file_mg(".", filename, path, fpp, trace) == SUCCESS)
      return SUCCESS;

   if (try_file_mg(getenv("MIDAS_DIR"), filename, path, fpp, trace) == SUCCESS)
      return SUCCESS;

   if (try_file_mg(exptdir, filename, path, fpp, trace) == SUCCESS)
      return SUCCESS;

   if (try_file_mg(getenv("MIDASSYS"), filename, path, fpp, trace) == SUCCESS)
      return SUCCESS;

   // setup default filename
   try_file_mg(exptdir, filename, path, NULL, false);
   return SS_FILE_ERROR;
}

#ifdef HAVE_MONGOOSE6
#include "mongoose6.h"
#endif

#ifdef HAVE_MONGOOSE616
#undef closesocket
#include "mongoose616.h"
// cs_md5() in not in mongoose.h
extern void cs_md5(char buf[33], ...);
#endif

static bool verbose_mg = false;
static bool trace_mg = false;
static bool trace_mg_recv = false;
static bool trace_mg_send = false;
#ifdef HAVE_MONGOOSE616
static bool multithread_mg = true;
#endif

#ifdef HAVE_MONGOOSE6
static struct mg_mgr mgr_mg;
#endif

struct AuthEntry {
   std::string username;
   std::string realm;
   std::string password;
};

class Auth {
public:
   std::string realm;
   std::string passwd_filename;
   std::vector<AuthEntry> passwords;
public:
   int Init();
};

static bool read_passwords(Auth* auth);

int Auth::Init()
{
   char exptname[256];
   exptname[0] = 0;
   cm_get_experiment_name(exptname, sizeof(exptname));
   
   if (strlen(exptname) > 0)
      realm = exptname;
   else
      realm = "midas";
   
   bool ok = read_passwords(this);
   if (!ok) {
      cm_msg(MERROR, "mongoose", "mongoose web server password file \"%s\" has no passwords for realm \"%s\"", passwd_filename.c_str(), realm.c_str());
      cm_msg(MERROR, "mongoose", "please add passwords by running: htdigest %s %s midas", passwd_filename.c_str(), realm.c_str());
      return SS_FILE_ERROR;
   }

   return SUCCESS;
}

static Auth *gAuthMg = NULL;

static void xmg_mkmd5resp(const char *method, size_t method_len, const char *uri,
                         size_t uri_len, const char *ha1, size_t ha1_len,
                         const char *nonce, size_t nonce_len, const char *nc,
                         size_t nc_len, const char *cnonce, size_t cnonce_len,
                         const char *qop, size_t qop_len, char *resp) {
  static const char colon[] = ":";
  static const size_t one = 1;
  char ha2[33];

  cs_md5(ha2, method, method_len, colon, one, uri, uri_len, NULL);
  cs_md5(resp, ha1, ha1_len, colon, one, nonce, nonce_len, colon, one, nc,
         nc_len, colon, one, cnonce, cnonce_len, colon, one, qop, qop_len,
         colon, one, ha2, sizeof(ha2) - 1, NULL);
}

/*
 * Check for authentication timeout.
 * Clients send time stamp encoded in nonce. Make sure it is not too old,
 * to prevent replay attacks.
 * Assumption: nonce is a hexadecimal number of seconds since 1970.
 */
static int xmg_check_nonce(const char *nonce) {
  unsigned long now = (unsigned long) time(NULL);
  unsigned long val = (unsigned long) strtoul(nonce, NULL, 16);
  return now < val || now - val < 3600;
}

/*
 * Authenticate HTTP request against opened passwords file.
 * Returns 1 if authenticated, 0 otherwise.
 */

static void xmg_http_send_digest_auth_request(struct mg_connection *c,
                                             const char *domain) {
  mg_printf(c,
            "HTTP/1.1 401 Unauthorized\r\n"
            "WWW-Authenticate: Digest qop=\"auth\", "
            "realm=\"%s\", nonce=\"%lu\"\r\n"
            "Content-Length: 0\r\n\r\n",
            domain, (unsigned long) time(NULL));
}

static bool read_passwords(Auth* auth)
{
   std::string path;
   FILE *fp;
   int status = find_file_mg("htpasswd.txt", path, &fp, trace_mg||verbose_mg);

   auth->passwd_filename = path;
   auth->passwords.clear();

   if (status != SUCCESS || fp == NULL) {
      cm_msg(MERROR, "mongoose", "mongoose web server cannot find password file \"%s\"", path.c_str());
      cm_msg(MERROR, "mongoose", "please create password file: touch %s", path.c_str());
      return false;
   }

   bool have_realm = false;
   char buf[256];

   /*
    * Read passwords file line by line. If should have htdigest format,
    * i.e. each line should be a colon-separated sequence:
    * USER_NAME:DOMAIN_NAME:HA1_HASH_OF_USER_DOMAIN_AND_PASSWORD
    */
   while (fgets(buf, sizeof(buf), fp) != NULL) {
      char f_user[256];
      char f_domain[256];
      char f_ha1[256];

      if (sscanf(buf, "%[^:]:%[^:]:%s", f_user, f_domain, f_ha1) == 3) {
         AuthEntry e;
         e.realm = f_domain;
         e.username = f_user;
         e.password = f_ha1;

         if (e.realm == auth->realm) {
            have_realm = true;
            auth->passwords.push_back(e);
         }
      }
   }

   fclose(fp);

   return have_realm;
}

#ifdef HAVE_MONGOOSE6
std::string find_var_mg(struct mg_str *hdr, const char* var_name)
{
   assert(!"this code is untested!");
   
   char* buf = NULL;
   int buf_size = 0;

   while (1) {
      if (buf_size == 0) {
         buf_size = 256;
         buf = (char*)malloc(buf_size);
         assert(buf != NULL);
      }
         
      int size = mg_http_parse_header(hdr, var_name, buf, buf_size);

      if (size <= 0) {
         free(buf);
         return "";
      }

      if (size < buf_size) {
         std::string s = buf;
         free(buf);
         return s;
      }

      buf_size = buf_size*2 + 16;
      buf = (char*)realloc(buf, buf_size);
      assert(buf != NULL);
   }
}
#endif

#ifdef HAVE_MONGOOSE616
std::string find_var_mg(struct mg_str *hdr, const char* var_name)
{
   char* buf = NULL;
   int buf_size = 0;
   int size = mg_http_parse_header2(hdr, var_name, &buf, buf_size);
   if (size <= 0)
      return "";
   assert(buf != NULL);
   std::string s = buf;
   free(buf);
   return s;
}
#endif

static std::string check_digest_auth(struct http_message *hm, Auth* auth)
{
   char expected_response[33];

   //printf("HereA!\n");

   /* Parse "Authorization:" header, fail fast on parse error */
   struct mg_str *hdr = mg_get_http_header(hm, "Authorization");

   if (!hdr)
      return "";

   //printf("HereB!\n");

   std::string user     = find_var_mg(hdr, "username");
   std::string cnonce   = find_var_mg(hdr, "cnonce");
   std::string response = find_var_mg(hdr, "response");
   std::string uri      = find_var_mg(hdr, "uri");
   std::string qop      = find_var_mg(hdr, "qop");
   std::string nc       = find_var_mg(hdr, "nc");
   std::string nonce    = find_var_mg(hdr, "nonce");

   if (user.length()<1) return "";
   if (cnonce.length()<1) return "";
   if (response.length()<1) return "";
   if (uri.length()<1) return "";
   if (qop.length()<1) return "";
   if (nc.length()<1) return "";
   if (nonce.length()<1) return "";

   if (xmg_check_nonce(nonce.c_str()) == 0) return "";
   //printf("HereB8!\n");

   //printf("HereC!\n");

   const char* uri_end = strchr(hm->uri.p, ' ');
   if (!uri_end) return "";

   size_t uri_length = uri_end - hm->uri.p;

   if (uri_length != uri.length())
      return "";

   int cmp = strncmp(hm->uri.p, uri.c_str(), uri_length);

   //printf("check URI: message %d %d [%d] authorization [%s]\n", (int)hm->uri.len, uri_length, cmp, uri);

   if (cmp != 0)
      return "";

   for (unsigned i=0; i<auth->passwords.size(); i++) {
      AuthEntry* e = &auth->passwords[i];
      if (e->username != user)
         continue;
      if (e->realm != auth->realm)
         continue;
      const char* f_ha1 = e->password.c_str();
      int uri_len = hm->uri.len;
      if (hm->uri.p[uri_len] == '?')
         uri_len += hm->query_string.len + 1; // "+1" accounts for the "?" character
      xmg_mkmd5resp(hm->method.p, hm->method.len,
                    hm->uri.p, uri_len,
                    f_ha1, strlen(f_ha1),
                    nonce.c_str(), nonce.length(),
                    nc.c_str(), nc.length(),
                    cnonce.c_str(), cnonce.length(),
                    qop.c_str(), qop.length(),
                    expected_response);
      int cmp = strcasecmp(response.c_str(), expected_response);
      //printf("digest_auth: expected %s, got %s, cmp %d\n", expected_response, response.c_str(), cmp);
      if (cmp == 0) {
         return e->username;
      }
    }

   return "";
}

#ifdef HAVE_MONGOOSE616

struct HostlistCacheEntry
{
   time_t time_created = 0;
   time_t time_last_used = 0;
   int count_used = 0;
   bool ipv4 = false;
   bool ipv6 = false;
   uint32_t ipv4addr = 0;
   struct in6_addr ipv6addr;
   std::string hostname;
   int gai_status = 0;
   std::string gai_strerror;
   bool ok = false;
};

static std::vector<HostlistCacheEntry*> gHostlistCache;

static void print_hostlist_cache()
{
   time_t now = time(NULL);
   
   for (unsigned i=0; i<gHostlistCache.size(); i++) {
      HostlistCacheEntry* e = gHostlistCache[i];
      if (!e) {
         // empty slot
         continue;
      }

      printf("%3d: %s \"%s\", ok %d, count_used %d, age created: %d, last_used %d",
             i,
             e->ipv4?"IPv4":(e->ipv6?"IPv6":"????"),
             e->hostname.c_str(),
             e->ok,
             e->count_used,
             (int)(now - e->time_created),
             (int)(now - e->time_last_used));

      if (e->gai_status) {
         printf(", getnameinfo() status %d (%s)", e->gai_status, e->gai_strerror.c_str());
      }

      printf("\n");
   }
}

static bool mongoose_check_hostlist(const union socket_address *sa)
{
   time_t now = time(NULL);
   bool ipv4 = false;
   bool ipv6 = false;
   uint32_t ipv4addr = 0;
   struct in6_addr ipv6addr;

   if (sa->sa.sa_family == AF_INET) {
      ipv4 = true;
      ipv4addr = sa->sin.sin_addr.s_addr;
   } else if (sa->sa.sa_family == AF_INET6) {
      ipv6 = true;
      memcpy(&ipv6addr, &sa->sin6.sin6_addr, sizeof(ipv6addr));
   } else {
      printf("Rejecting connection from unknown address family %d (AF_xxx)\n", sa->sa.sa_family);
      return false;
   }
   
   for (unsigned i=0; i<gHostlistCache.size(); i++) {
      HostlistCacheEntry* e = gHostlistCache[i];
      if (!e) {
         // empty slot
         continue;
      }
      
      if ((ipv4 == e->ipv4) && (ipv4addr == e->ipv4addr)) {
         // IPv4 address match
         e->time_last_used = now;
         e->count_used++;
         return e->ok;
      }

      if ((ipv6 == e->ipv6) && (memcmp(&ipv6addr, &e->ipv6addr, sizeof(ipv6addr)) == 0)) {
         // IPv6 address match
         e->time_last_used = now;
         e->count_used++;
         return e->ok;
      }

      // not this one. maybe expire old entries?

      if (e->time_last_used < now - 24*60*60) {
         printf("hostlist: expire \"%s\", ok %d, age %d, count_used: %d\n", e->hostname.c_str(), e->ok, (int)(now - e->time_last_used), e->count_used);
         gHostlistCache[i] = NULL;
         delete e;
      }
   }

   // not found in cache

   assert(ipv4 || ipv6);

   HostlistCacheEntry* e = new HostlistCacheEntry;

   bool found = false;
   for (unsigned i=0; i<gHostlistCache.size(); i++) {
      if (gHostlistCache[i] == NULL) {
         gHostlistCache[i] = e;
         found = true;
      }
   }
   if (!found) {
      gHostlistCache.push_back(e);
   }
   
   e->time_created = now;
   e->time_last_used = now;
   e->count_used = 1;
   e->ipv4 = ipv4;
   e->ipv6 = ipv6;
   if (ipv4)
      e->ipv4addr = ipv4addr;
   if (ipv6)
      memcpy(&e->ipv6addr, &ipv6addr, sizeof(ipv6addr));
   e->ok = false;

   char hname[NI_MAXHOST];
   hname[0] = 0;
   
   e->gai_status = getnameinfo(&sa->sa, sizeof(*sa), hname, sizeof(hname), NULL, 0, 0);
   
   if (e->gai_status) {
      e->gai_strerror = gai_strerror(e->gai_status);

      printf("Rejecting connection from \'%s\', getnameinfo() status %d (%s)\n", hname, e->gai_status, e->gai_strerror.c_str());

      e->ok = false;
      return e->ok;
   }
   
   printf("connection from \"%s\"\n", hname);
   
   e->hostname = hname;

   /* always permit localhost */
   if (e->hostname == "localhost.localdomain")
      e->ok = true;
   else if (e->hostname == "localhost")
      e->ok = true;
   else {
      for (unsigned int i=0 ; i<gAllowedHosts.size() ; i++) {
         if (e->hostname == gAllowedHosts[i]) {
            e->ok = true;
         }
      }
   }

   if (!e->ok) {
      printf("Rejecting connection from \'%s\'\n", hname);
   }

   print_hostlist_cache();

   return e->ok;
}

#endif

static std::string mgstr(const mg_str* s)
{
   return std::string(s->p, s->len);
}

static const std::string find_header_mg(const struct http_message *msg, const char* name)
{
   size_t nlen = strlen(name);
   for (int i=0; i<MG_MAX_HTTP_HEADERS; i++) {
      if (msg->header_names[i].len != nlen)
         continue;
      if (strncmp(msg->header_names[i].p, name, nlen) != 0)
         continue;
      return mgstr(&msg->header_values[i]);
   }
   return "";
}

static const std::string find_cookie_mg(const struct http_message *msg, const char* cookie_name)
{
   const std::string cookies = find_header_mg(msg, "Cookie");
   if (cookies.length() < 1)
      return "";
   const char* p = strstr(cookies.c_str(), cookie_name);
   if (!p)
      return "";
   const char* v = p+strlen(cookie_name);
   if (*v != '=')
      return "";
   v++;
   //printf("cookie [%s] value [%s]\n", cookie_name, v);
   return v;
}

// Generic event handler

static void handle_event_mg(struct mg_connection *nc, int ev, void *ev_data)
{
   struct mbuf *io = &nc->recv_mbuf;
   switch (ev) {
   case MG_EV_POLL: // periodic call from loop_mg() via mg_mgr_poll()
      break;
   case MG_EV_ACCEPT:
      if (trace_mg)
         printf("handle_event_mg: nc %p, ev %d, ev_data %p -> accept\n", nc, ev, ev_data);
      break;
   case MG_EV_RECV:
      if (trace_mg)
         printf("handle_event_mg: nc %p, ev %d, ev_data %p -> recv %d, buffered %d bytes\n", nc, ev, ev_data, *(int*)ev_data, (int)io->len);
#if 0
      // This event handler implements simple TCP echo server
      mg_send(nc, io->buf, io->len);  // Echo received data back
      mbuf_remove(io, io->len);      // Discard data from recv buffer
#endif
      break;
   case MG_EV_SEND:
      if (trace_mg)
         printf("handle_event_mg: nc %p, ev %d, ev_data %p -> send %d bytes\n", nc, ev, ev_data, *(int*)ev_data);
      break;
   case MG_EV_CLOSE:
      if (trace_mg)
         printf("handle_event_mg: nc %p, ev %d, ev_data %p -> close\n", nc, ev, ev_data);
      break;
   default:
      if (trace_mg)
         printf("handle_event_mg: nc %p, ev %d, ev_data %p\n", nc, ev, ev_data);
      break;
   }
}

void decode_cookies(Cookies *c, const http_message* msg)
{
   // extract password cookies

   char cookie_pwd[256]; // general access password
   char cookie_wpwd[256]; // "write mode" password
   char cookie_cpwd[256]; // custom page and javascript password

   cookie_pwd[0] = 0;
   cookie_wpwd[0] = 0;
   cookie_cpwd[0] = 0;

   std::string s = find_cookie_mg(msg, "midas_pwd");
   if (s.length() > 0) {
      STRLCPY(cookie_pwd, s.c_str());
      cookie_pwd[strcspn(cookie_pwd, " ;\r\n")] = 0;
   }

   s = find_cookie_mg(msg, "midas_wpwd");
   if (s.length()) {
      STRLCPY(cookie_wpwd, s.c_str());
      cookie_wpwd[strcspn(cookie_wpwd, " ;\r\n")] = 0;
   }

   s = find_cookie_mg(msg, "cpwd");
   if (s.length()) {
      STRLCPY(cookie_cpwd, s.c_str());
      cookie_cpwd[strcspn(cookie_cpwd, " ;\r\n")] = 0;
   }

   // extract refresh rate
   c->refresh = DEFAULT_REFRESH;
   s = find_cookie_mg(msg, "midas_refr");
   if (s.length() > 0)
      c->refresh = atoi(s.c_str());

   // extract equipment expand flag
   //c->expand_equipment = 0;
   //s = find_cookie_mg(msg, "midas_expeq");
   //if (s.length() > 0)
   //   c->expand_equipment = atoi(s.c_str());

   c->cookie_pwd  = cookie_pwd;
   c->cookie_wpwd = cookie_wpwd;
   c->cookie_cpwd = cookie_cpwd;
}

#define RESPONSE_SENT   1
#define RESPONSE_QUEUED 2
#define RESPONSE_501    3

static int handle_decode_get(struct mg_connection *nc, const http_message* msg, const char* uri, const char* query_string, RequestTrace* t)
{
   Cookies cookies;

   decode_cookies(&cookies, msg);

   // lock shared structures

#ifdef HAVE_MONGOOSE6
   int status = ss_mutex_wait_for(request_mutex, 0);
   assert(status == SS_SUCCESS);
#endif

   //t->fTimeLocked = GetTimeSec();

   // prepare return buffer

   Return *rr = new Return();

   rr->zero();

   // call midas

   decode_get(rr, NULL, &cookies, uri, query_string, t);

   if (trace_mg)
      printf("handle_decode_get: return buffer length %d bytes, strlen %d\n", rr->return_length, (int)strlen(rr->return_buffer));

   t->fTimeProcessed = GetTimeSec();

   if (rr->return_length == -1) {
      delete rr;
#ifdef HAVE_MONGOOSE6
      //t->fTimeUnlocked = GetTimeSec();
      ss_mutex_release(request_mutex);
#endif
      return RESPONSE_501;
   }

   if (rr->return_length == 0)
      rr->return_length = strlen(rr->return_buffer);

   //t->fTimeUnlocked = GetTimeSec();

#ifdef HAVE_MONGOOSE6
   ss_mutex_release(request_mutex);
#endif

   mg_send(nc, rr->return_buffer, rr->return_length);

   if (!strstr(rr->return_buffer, "Content-Length")) {
      // cannot do pipelined http if response generated by mhttpd
      // decode_get() has no Content-Length header.
      // must close the connection.
      nc->flags |= MG_F_SEND_AND_CLOSE;
   }

   t->fTimeSent = GetTimeSec();

   delete rr;

   return RESPONSE_SENT;
}

#ifdef HAVE_MONGOOSE616

static uint32_t s_mwo_seqno = 0;

struct MongooseWorkObject
{
   uint32_t seqno = 0;
   void* nc = NULL;
   int socket = -1;
   bool http_get  = false;
   bool http_post = false;
   bool mjsonrpc  = false;
   Cookies cookies;
   std::string origin;
   std::string uri;
   std::string query_string;
   std::string post_body;
   std::string post_boundary;
   RequestTrace* t = NULL;
   bool send_done = false;
};

struct MongooseThreadObject
{
   std::atomic_bool fIsRunning{false};
   std::thread* fThread = NULL; // thread
   void*        fNc     = NULL; // thread is attached to this network connection
   std::mutex   fMutex;
   std::deque<MongooseWorkObject*> fQueue;
   std::condition_variable fNotify;

   //MongooseThreadObject() // ctor
   //{
   //   printf("MongooseThreadObject %p created!\n", this);
   //}

   //~MongooseThreadObject() // dtor
   //{
   //   printf("MongooseThreadObject %p destroyed!\n", this);
   //}
};

static std::vector<MongooseThreadObject*> gMongooseThreads;

static void mongoose_thread(MongooseThreadObject*);

MongooseThreadObject* FindThread(void* nc)
{
   //printf("FindThread: nc %p, thread %s\n", nc, ss_tid_to_string(ss_gettid()).c_str());

   MongooseThreadObject* last_not_connected = NULL;
   
   for (auto it : gMongooseThreads) {
      MongooseThreadObject* to = it;
      if (to->fNc == nc) {
         //printf("to %p, nc %p: found thread\n", to, nc);
         return to;
      }
      if (to->fNc == NULL) {
         last_not_connected = to;
      }
   }

   if (last_not_connected) {
      MongooseThreadObject* to = last_not_connected;
      to->fNc = nc;
      //printf("to %p, nc %p: reusing thread\n", to, nc);
      return to;
   }

   MongooseThreadObject* to = new MongooseThreadObject();

   to->fNc = nc;

   //printf("to %p, nc %p: new thread\n", to, nc);

   gMongooseThreads.push_back(to);

   printf("Mongoose web server is using %d threads\n", (int)gMongooseThreads.size());

   to->fThread = new std::thread(mongoose_thread, to);

   return to;
}

void FreeThread(void* nc)
{
   //printf("FreeThread, nc %p\n", nc);

   for (auto it : gMongooseThreads) {
      MongooseThreadObject* to = it;
      if (to->fNc == nc) {
         //printf("to %p, nc %p: connection closed\n", to, nc);
         to->fNc = NULL;
         return;
      }
   }

   //printf("to %p, nc %p: connection closed, but no thread\n", nullptr, nc);
}

static void mongoose_queue(void* nc, MongooseWorkObject* w)
{
   w->nc = nc;
   MongooseThreadObject* to = FindThread(nc);
   assert(to->fNc == nc);
   to->fMutex.lock();
   to->fQueue.push_back(w);
   to->fMutex.unlock();
   to->fNotify.notify_one();
}

static void mongoose_send(void* nc, MongooseWorkObject* w, const char* p1, size_t s1, const char* p2, size_t s2, bool close_flag = false);

static int queue_decode_get(struct mg_connection *nc, const http_message* msg, const char* uri, const char* query_string, RequestTrace* t)
{
   MongooseWorkObject* w = new MongooseWorkObject();
   w->socket = nc->sock;
   w->seqno = s_mwo_seqno++;
   w->http_get = true;
   decode_cookies(&w->cookies, msg);
   w->uri = uri;
   w->query_string = query_string;
   w->t = t;

   mongoose_queue(nc, w);

   return RESPONSE_QUEUED;
}

static int queue_decode_post(struct mg_connection *nc, const http_message* msg, const char* boundary, const char* uri, const char* query_string, RequestTrace* t)
{
   MongooseWorkObject* w = new MongooseWorkObject();
   w->socket = nc->sock;
   w->seqno = s_mwo_seqno++;
   w->http_post = true;
   decode_cookies(&w->cookies, msg);
   w->uri = uri;
   w->query_string = query_string;
   w->post_body = mgstr(&msg->body);
   w->post_boundary = boundary;
   w->t = t;

   mongoose_queue(nc, w);

   return RESPONSE_QUEUED;
}

static int queue_mjsonrpc(struct mg_connection *nc, const std::string& origin, const std::string& post_body, RequestTrace* t)
{
   MongooseWorkObject* w = new MongooseWorkObject();
   w->socket = nc->sock;
   w->seqno = s_mwo_seqno++;
   w->mjsonrpc = true;
   w->origin = origin;
   w->post_body = post_body;
   w->t = t;

   mongoose_queue(nc, w);

   return RESPONSE_QUEUED;
}

static int thread_http_get(void *nc, MongooseWorkObject *w)
{
   // lock shared structures

   //int status = ss_mutex_wait_for(request_mutex, 0);
   //assert(status == SS_SUCCESS);

   //w->t->fTimeLocked = GetTimeSec();

   // prepare return buffer

   Return *rr = new Return();

   rr->zero();

   // call midas

   decode_get(rr, NULL, &w->cookies, w->uri.c_str(), w->query_string.c_str(), w->t);

   if (trace_mg)
      printf("handle_decode_get: return buffer length %d bytes, strlen %d\n", rr->return_length, (int)strlen(rr->return_buffer));

   w->t->fTimeProcessed = GetTimeSec();

   if (rr->return_length == -1) {
      delete rr;
      //w->t->fTimeUnlocked = GetTimeSec();
      //ss_mutex_release(request_mutex);
      return RESPONSE_501;
   }

   if (rr->return_length == 0)
      rr->return_length = strlen(rr->return_buffer);

   //w->t->fTimeUnlocked = GetTimeSec();

   //ss_mutex_release(request_mutex);

   bool close_flag = false;

   if (!strstr(rr->return_buffer, "Content-Length")) {
      // cannot do pipelined http if response generated by mhttpd
      // decode_get() has no Content-Length header.
      // must close the connection.
      close_flag = true;
   }

   mongoose_send(nc, w, rr->return_buffer, rr->return_length, NULL, 0, close_flag);

   w->t->fTimeSent = GetTimeSec();

   delete rr;

   return RESPONSE_SENT;
}

static int thread_http_post(void *nc, MongooseWorkObject *w)
{
   const char* post_data = w->post_body.c_str();
   int post_data_len = w->post_body.length();

   // lock shared strctures

   //int status = ss_mutex_wait_for(request_mutex, 0);
   //assert(status == SS_SUCCESS);

   // prepare return buffer

   Return* rr = new Return;

   rr->zero();

   //printf("post_data_len %d, data [%s], boundary [%s]\n", post_data_len, post_data, boundary);

   decode_post(rr, NULL, (char*)post_data, w->post_boundary.c_str(), post_data_len, &w->cookies, w->uri.c_str(), w->t);

   if (trace_mg)
      printf("handle_decode_post: return buffer length %d bytes, strlen %d\n", rr->return_length, (int)strlen(rr->return_buffer));

   if (rr->return_length == -1) {
      //ss_mutex_release(request_mutex);
      delete rr;
      return RESPONSE_501;
   }

   if (rr->return_length == 0)
      rr->return_length = strlen(rr->return_buffer);

   //ss_mutex_release(request_mutex);

   bool close_flag = false;
   if (!strstr(rr->return_buffer, "Content-Length")) {
      // cannot do pipelined http if response generated by mhttpd
      // decode_get() has no Content-Length header.
      // must close the connection.
      close_flag = true;
   }

   mongoose_send(nc, w, rr->return_buffer, rr->return_length, NULL, 0, close_flag);

   delete rr;

   return RESPONSE_SENT;

}

static int thread_mjsonrpc(void *nc, MongooseWorkObject *w)
{
   w->t->fRPC = w->post_body;

   //int status = ss_mutex_wait_for(request_mutex, 0);
   //assert(status == SS_SUCCESS);

   //gMutex.lock();
   //w->t->fTimeLocked = GetTimeSec();

   MJsonNode* reply = mjsonrpc_decode_post_data(w->post_body.c_str());

   //w->t->fTimeUnlocked = GetTimeSec();
   //gMutex.unlock();
   
   //ss_mutex_release(request_mutex);

   if (reply->GetType() == MJSON_ARRAYBUFFER) {
      const char* ptr;
      size_t size;
      reply->GetArrayBuffer(&ptr, &size);
      
      std::string headers;
      headers += "HTTP/1.1 200 OK\n";
      if (w->origin.length() > 0)
         headers += "Access-Control-Allow-Origin: " + w->origin + "\n";
      else
         headers += "Access-Control-Allow-Origin: *\n";
      headers += "Access-Control-Allow-Credentials: true\n";
      headers += "Content-Length: " + toString(size) + "\n";
      headers += "Content-Type: application/octet-stream\n";
      //headers += "Date: Sat, 08 Jul 2006 12:04:08 GMT\n";
      
      //printf("sending headers: %s\n", headers.c_str());
      //printf("sending reply: %s\n", reply_string.c_str());
      
      std::string send = headers + "\n";
      
      w->t->fTimeProcessed = GetTimeSec();
      
      mongoose_send(nc, w, send.c_str(), send.length(), ptr, size);
      
      w->t->fTimeSent = GetTimeSec();
      
      delete reply;
      
      return RESPONSE_SENT;
   }
   
   std::string reply_string = reply->Stringify();
   int reply_length = reply_string.length();
   
   std::string headers;
   headers += "HTTP/1.1 200 OK\n";
   if (w->origin.length() > 0)
      headers += "Access-Control-Allow-Origin: " + w->origin + "\n";
   else
      headers += "Access-Control-Allow-Origin: *\n";
   headers += "Access-Control-Allow-Credentials: true\n";
   headers += "Content-Length: " + toString(reply_length) + "\n";
   headers += "Content-Type: application/json\n";
   //headers += "Date: Sat, 08 Jul 2006 12:04:08 GMT\n";
   
   //printf("sending headers: %s\n", headers.c_str());
   //printf("sending reply: %s\n", reply_string.c_str());
   
   std::string send = headers + "\n" + reply_string;
   
   w->t->fTimeProcessed = GetTimeSec();
   
   mongoose_send(nc, w, send.c_str(), send.length(), NULL, 0);
   
   w->t->fTimeSent = GetTimeSec();
   
   delete reply;
   
   return RESPONSE_SENT;
}

static int thread_work_function(void *nc, MongooseWorkObject *w)
{
   if (w->http_get)
      return thread_http_get(nc, w);
   else if (w->http_post)
      return thread_http_post(nc, w);
   else if (w->mjsonrpc)
      return thread_mjsonrpc(nc, w);
   else
      return RESPONSE_501;
}

#endif

static int handle_decode_post(struct mg_connection *nc, const http_message* msg, const char* uri, const char* query_string, RequestTrace* t)
{

   char boundary[256];
   boundary[0] = 0;
   const std::string ct = find_header_mg(msg, "Content-Type");
   if (ct.length() > 0) {
      const char* s = strstr(ct.c_str(), "boundary=");
      if (s)
         strlcpy(boundary, s+9, sizeof(boundary));
   }

#ifdef HAVE_MONGOOSE616
   if (multithread_mg)
      return queue_decode_post(nc, msg, boundary, uri, query_string, t);
#endif

   Cookies cookies;

   decode_cookies(&cookies, msg);

   const char* post_data = msg->body.p;
   int post_data_len = msg->body.len;

   // lock shared strctures

#ifdef HAVE_MONGOOSE6
   int status = ss_mutex_wait_for(request_mutex, 0);
   assert(status == SS_SUCCESS);
#endif

   // prepare return buffer

   Return* rr = new Return;

   rr->zero();

   //printf("post_data_len %d, data [%s], boundary [%s]\n", post_data_len, post_data, boundary);

   decode_post(rr, NULL, (char*)post_data, boundary, post_data_len, &cookies, uri, t);

   if (trace_mg)
      printf("handle_decode_post: return buffer length %d bytes, strlen %d\n", rr->return_length, (int)strlen(rr->return_buffer));

   if (rr->return_length == -1) {
#ifdef HAVE_MONGOOSE6
      ss_mutex_release(request_mutex);
#endif
      delete rr;
      return RESPONSE_501;
   }

   if (rr->return_length == 0)
      rr->return_length = strlen(rr->return_buffer);

#ifdef HAVE_MONGOOSE6
   ss_mutex_release(request_mutex);
#endif

   mg_send(nc, rr->return_buffer, rr->return_length);

   if (!strstr(rr->return_buffer, "Content-Length")) {
      // cannot do pipelined http if response generated by mhttpd
      // decode_get() has no Content-Length header.
      // must close the connection.
      nc->flags |= MG_F_SEND_AND_CLOSE;
   }

   delete rr;

   return RESPONSE_SENT;
}

static int handle_http_get(struct mg_connection *nc, const http_message* msg, const char* uri, RequestTrace* t)
{
   std::string query_string = mgstr(&msg->query_string);

   if (trace_mg||verbose_mg)
      printf("handle_http_get: uri [%s], query [%s]\n", uri, query_string.c_str());

   if (query_string == "mjsonrpc_schema") {
      MJsonNode* s = mjsonrpc_get_schema();
      std::string reply = s->Stringify();
      delete s;

      int reply_length = reply.length();

      const std::string origin_header = find_header_mg(msg, "Origin");

      std::string headers;
      headers += "HTTP/1.1 200 OK\n";
      if (origin_header.length() > 0)
         headers += "Access-Control-Allow-Origin: " + std::string(origin_header) + "\n";
      else
         headers += "Access-Control-Allow-Origin: *\n";
      headers += "Access-Control-Allow-Credentials: true\n";
      headers += "Content-Length: " + toString(reply_length) + "\n";
      headers += "Content-Type: application/json\n";
      //headers += "Date: Sat, 08 Jul 2006 12:04:08 GMT\n";

      //printf("sending headers: %s\n", headers.c_str());
      //printf("sending reply: %s\n", reply.c_str());

      std::string send = headers + "\n" + reply;

      t->fTimeProcessed = GetTimeSec();

      mg_send(nc, send.c_str(), send.length());

      t->fTimeSent = GetTimeSec();

      return RESPONSE_SENT;
   }

   if (query_string == "mjsonrpc_schema_text") {
      MJsonNode* s = mjsonrpc_get_schema();
      std::string reply = mjsonrpc_schema_to_text(s);
      delete s;

      int reply_length = reply.length();

      const std::string origin_header = find_header_mg(msg, "Origin");

      std::string headers;
      headers += "HTTP/1.1 200 OK\n";
      if (origin_header.length() > 0)
         headers += "Access-Control-Allow-Origin: " + std::string(origin_header) + "\n";
      else
         headers += "Access-Control-Allow-Origin: *\n";
      headers += "Access-Control-Allow-Credentials: true\n";
      headers += "Content-Length: " + toString(reply_length) + "\n";
      headers += "Content-Type: text/plain\n";
      //headers += "Date: Sat, 08 Jul 2006 12:04:08 GMT\n";

      //printf("sending headers: %s\n", headers.c_str());
      //printf("sending reply: %s\n", reply.c_str());

      std::string send = headers + "\n" + reply;

      t->fTimeProcessed = GetTimeSec();

      mg_send(nc, send.c_str(), send.length());

      t->fTimeSent = GetTimeSec();

      return RESPONSE_SENT;
   }

#ifdef HAVE_MONGOOSE616
   if (multithread_mg)
      return queue_decode_get(nc, msg, uri, query_string.c_str(), t);
#endif

   return handle_decode_get(nc, msg, uri, query_string.c_str(), t);
}

static int handle_http_post(struct mg_connection *nc, const http_message* msg, const char* uri, RequestTrace* t)
{
   std::string query_string = mgstr(&msg->query_string);
   std::string post_data = mgstr(&msg->body);

   if (trace_mg||verbose_mg)
      printf("handle_http_post: uri [%s], query [%s], post data %d bytes\n", uri, query_string.c_str(), (int)post_data.length());

   if (query_string == "mjsonrpc") {
      const std::string origin_header = find_header_mg(msg, "Origin");
      const std::string ctype_header = find_header_mg(msg, "Content-Type");

      if (strstr(ctype_header.c_str(), "application/json") == NULL) {
         std::string headers;
         headers += "HTTP/1.1 415 Unsupported Media Type\n";
         //headers += "Date: Sat, 08 Jul 2006 12:04:08 GMT\n";

         //printf("sending headers: %s\n", headers.c_str());
         //printf("sending reply: %s\n", reply.c_str());

         std::string send = headers + "\n";

         t->fTimeProcessed = GetTimeSec();

         mg_send(nc, send.c_str(), send.length());

         t->fTimeSent = GetTimeSec();

         return RESPONSE_SENT;
      }

#ifdef HAVE_MONGOOSE616
      if (multithread_mg)
         return queue_mjsonrpc(nc, origin_header, post_data, t);
#endif

      //printf("post body: %s\n", post_data.c_str());

      t->fRPC = post_data;

#ifdef HAVE_MONGOOSE6
      int status = ss_mutex_wait_for(request_mutex, 0);
      assert(status == SS_SUCCESS);
#endif

      //t->fTimeLocked = GetTimeSec();

      MJsonNode* reply = mjsonrpc_decode_post_data(post_data.c_str());

      //t->fTimeUnlocked = GetTimeSec();
      
#ifdef HAVE_MONGOOSE6
      ss_mutex_release(request_mutex);
#endif

      if (reply->GetType() == MJSON_ARRAYBUFFER) {
         const char* ptr;
         size_t size;
         reply->GetArrayBuffer(&ptr, &size);

         std::string headers;
         headers += "HTTP/1.1 200 OK\n";
         if (origin_header.length() > 0)
            headers += "Access-Control-Allow-Origin: " + std::string(origin_header) + "\n";
         else
            headers += "Access-Control-Allow-Origin: *\n";
         headers += "Access-Control-Allow-Credentials: true\n";
         headers += "Content-Length: " + toString(size) + "\n";
         headers += "Content-Type: application/octet-stream\n";
         //headers += "Date: Sat, 08 Jul 2006 12:04:08 GMT\n";

         //printf("sending headers: %s\n", headers.c_str());
         //printf("sending reply: %s\n", reply_string.c_str());

         std::string send = headers + "\n";

         t->fTimeProcessed = GetTimeSec();

         mg_send(nc, send.c_str(), send.length());
         mg_send(nc, ptr, size);

         t->fTimeSent = GetTimeSec();

         delete reply;

         return RESPONSE_SENT;
      }

      std::string reply_string = reply->Stringify();
      int reply_length = reply_string.length();

      std::string headers;
      headers += "HTTP/1.1 200 OK\n";
      if (origin_header.length() > 0)
         headers += "Access-Control-Allow-Origin: " + std::string(origin_header) + "\n";
      else
         headers += "Access-Control-Allow-Origin: *\n";
      headers += "Access-Control-Allow-Credentials: true\n";
      headers += "Content-Length: " + toString(reply_length) + "\n";
      headers += "Content-Type: application/json\n";
      //headers += "Date: Sat, 08 Jul 2006 12:04:08 GMT\n";

      //printf("sending headers: %s\n", headers.c_str());
      //printf("sending reply: %s\n", reply_string.c_str());

      std::string send = headers + "\n" + reply_string;

      t->fTimeProcessed = GetTimeSec();

      mg_send(nc, send.c_str(), send.length());

      t->fTimeSent = GetTimeSec();

      delete reply;

      return RESPONSE_SENT;
   }

   return handle_decode_post(nc, msg, uri, query_string.c_str(), t);
}

static void handle_http_options_cors(struct mg_connection *nc, const http_message* msg, RequestTrace* t)
{
   //
   // JSON-RPC CORS pre-flight request, see
   // https://en.wikipedia.org/wiki/Cross-origin_resource_sharing
   //
   // OPTIONS /resources/post-here/ HTTP/1.1
   // Host: bar.other
   // User-Agent: Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9.1b3pre) Gecko/20081130 Minefield/3.1b3pre
   // Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8
   // Accept-Language: en-us,en;q=0.5
   // Accept-Encoding: gzip,deflate
   // Accept-Charset: ISO-8859-1,utf-8;q=0.7,*;q=0.7
   // Connection: keep-alive
   // Origin: http://foo.example
   // Access-Control-Request-Method: POST
   // Access-Control-Request-Headers: X-PINGOTHER
   //
   // HTTP/1.1 200 OK
   // Date: Mon, 01 Dec 2008 01:15:39 GMT
   // Server: Apache/2.0.61 (Unix)
   // Access-Control-Allow-Origin: http://foo.example
   // Access-Control-Allow-Methods: POST, GET, OPTIONS
   // Access-Control-Allow-Headers: X-PINGOTHER
   // Access-Control-Max-Age: 1728000
   // Vary: Accept-Encoding, Origin
   // Content-Encoding: gzip
   // Content-Length: 0
   // Keep-Alive: timeout=2, max=100
   // Connection: Keep-Alive
   // Content-Type: text/plain
   //

   const std::string origin_header = find_header_mg(msg, "Origin");

   if (trace_mg||verbose_mg)
      printf("handle_http_options_cors: origin [%s]\n", origin_header.c_str());

   std::string headers;
   headers += "HTTP/1.1 200 OK\n";
   //headers += "Date: Sat, 08 Jul 2006 12:04:08 GMT\n";
   if (origin_header.length() > 0)
      headers += "Access-Control-Allow-Origin: " + origin_header + "\n";
   else
      headers += "Access-Control-Allow-Origin: *\n";
   headers += "Access-Control-Allow-Headers: Content-Type\n";
   headers += "Access-Control-Allow-Credentials: true\n";
   headers += "Access-Control-Max-Age: 120\n";
   headers += "Content-Length: 0\n";
   headers += "Content-Type: text/plain\n";
   //printf("sending headers: %s\n", headers.c_str());
   //printf("sending reply: %s\n", reply.c_str());

   std::string send = headers + "\n";

   t->fTimeProcessed = GetTimeSec();

   mg_send(nc, send.c_str(), send.length());

   t->fTimeSent = GetTimeSec();
}

// HTTP event handler

static bool mongoose_passwords_enabled(const struct mg_connection *nc);

#ifdef HAVE_MONGOOSE616
static MVOdb* gProxyOdb = NULL;
#endif

static void handle_http_message(struct mg_connection *nc, http_message* msg)
{
   std::string method = mgstr(&msg->method);
   std::string query_string = mgstr(&msg->query_string);
   std::string uri_encoded = mgstr(&msg->uri);
   std::string uri = UrlDecode(uri_encoded.c_str());

   if (trace_mg)
      printf("handle_http_message: method [%s] uri [%s] proto [%s]\n", method.c_str(), uri.c_str(), mgstr(&msg->proto).c_str());

   RequestTrace* t = new RequestTrace;
   t->fTimeReceived = GetTimeSec();
   t->fMethod = method;
   t->fUri = uri;
   t->fQuery = query_string;

   // process OPTIONS for Cross-origin (CORS) preflight request
   // see https://developer.mozilla.org/en-US/docs/Web/HTTP/Access_control_CORS
   if (method == "OPTIONS" && query_string == "mjsonrpc" && mg_get_http_header(msg, "Access-Control-Request-Method") != NULL) {
      handle_http_options_cors(nc, msg, t);
      t->fCompleted = true;
      gTraceBuf->AddTraceMTS(t);
      return;
   }

   if (gAuthMg && mongoose_passwords_enabled(nc)) {
      std::string username = check_digest_auth(msg, gAuthMg);

      // Cannot re-read the password file - it is not thread safe to do so
      // unless I lock gAuthMg for each call check_digest_auth() and if I do so,
      // I will serialize (single-thread) all the http requests and defeat
      // the whole point of multithreading the web server. K.O.
      //
      //// if auth failed, reread password file - maybe user added or password changed
      //if (username.length() < 1) {
      //   bool ok = read_passwords(&gAuthMg);
      //   if (ok)
      //      username = check_digest_auth(msg, &gAuthMg);
      //}

      if (trace_mg)
         printf("handle_http_message: auth user: \"%s\"\n", username.c_str());

      if (username.length() == 0) {
         if (trace_mg||verbose_mg)
            printf("handle_http_message: method [%s] uri [%s] query [%s] proto [%s], sending auth request for realm \"%s\"\n", method.c_str(), uri.c_str(), query_string.c_str(), mgstr(&msg->proto).c_str(), gAuthMg->realm.c_str());

         xmg_http_send_digest_auth_request(nc, gAuthMg->realm.c_str());
         t->fCompleted = true;
         gTraceBuf->AddTraceMTS(t);
         return;
      }
      t->fAuthOk = true;
   } else {
      t->fAuthOk = true;
   }

#ifdef HAVE_MONGOOSE616
   if (gProxyOdb && starts_with(uri, "/proxy/")) {
      std::string::size_type p1 = uri.find("/", 1);
      if (p1 == uri.length()-1) {
         std::string response = "404 Not Found (Proxy name is missing)";
         mg_send_head(nc, 404, response.length(), NULL);
         mg_send(nc, response.c_str(), response.length());
         delete t;
         return;
      }
      std::string::size_type p2 = uri.find("/", p1+1);
      if (p2 == std::string::npos) {
         std::string response = "404 Not Found (Proxy URL should end with a slash)";
         mg_send_head(nc, 404, response.length(), NULL);
         mg_send(nc, response.c_str(), response.length());
         delete t;
         return;
      }
      std::string p = uri.substr(p1+1, p2-p1-1);
      //printf("uri [%s], p1: %d, p2: %d, substr: [%s]\n", uri.c_str(), (int)p1, (int)p2, p.c_str());
      if (p.length() < 1) {
         std::string response = "404 Not Found (Double-slash or Proxy name is too short)";
         mg_send_head(nc, 404, response.length(), NULL);
         mg_send(nc, response.c_str(), response.length());
         delete t;
         return;
      }
      std::string destination;
      gProxyOdb->RS(p.c_str(), &destination);
      if (destination.length() < 1) {
         std::string response = "404 Not Found (Proxy not found in ODB)";
         mg_send_head(nc, 404, response.length(), NULL);
         mg_send(nc, response.c_str(), response.length());
         delete t;
         return;
      } else if (destination[0] == '#') {
         std::string response = "404 Not Found (Proxy commented-out in ODB)";
         mg_send_head(nc, 404, response.length(), NULL);
         mg_send(nc, response.c_str(), response.length());
         delete t;
         return;
      } else if (ends_with_char(destination, '/')) {
         std::string response = "404 Not Found (Proxy address should not end with a slash)";
         mg_send_head(nc, 404, response.length(), NULL);
         mg_send(nc, response.c_str(), response.length());
         delete t;
         return;
      } else if (!starts_with(destination, "http")) {
         std::string response = "404 Not Found (Proxy address does not start with http";
         mg_send_head(nc, 404, response.length(), NULL);
         mg_send(nc, response.c_str(), response.length());
         delete t;
         return;
      } else {
         std::string m;
         m += "/proxy";
         m += "/";
         m += p;
         mg_str mount = mg_mk_str(m.c_str());
         mg_str upstream = mg_mk_str(destination.c_str());
         if (verbose_mg||trace_mg) {
            printf("proxy: uri [%s] mount [%s] upstream [%s]\n", uri.c_str(), mgstr(&mount).c_str(), mgstr(&upstream).c_str());
         }
         mg_http_reverse_proxy(nc, msg, mount, upstream);
         delete t;
         return;
      }
   }
#endif

   int response = RESPONSE_501;

   if (method == "GET")
      response = handle_http_get(nc, msg, uri.c_str(), t);
   else if (method == "POST")
      response = handle_http_post(nc, msg, uri.c_str(), t);

   if (response == RESPONSE_501) {
      if (trace_mg||verbose_mg)
         printf("handle_http_message: sending 501 Not Implemented error\n");

      std::string response = "501 Not Implemented";
      mg_send_head(nc, 501, response.length(), NULL); // 501 Not Implemented
      mg_send(nc, response.c_str(), response.length());
   }

   if (response != RESPONSE_QUEUED) {
      t->fCompleted = true;
      gTraceBuf->AddTraceMTS(t);
   }
}

#ifdef HAVE_MONGOOSE6

static void handle_http_event_mg(struct mg_connection *nc, int ev, void *ev_data)
{
   switch (ev) {
   case MG_EV_HTTP_REQUEST:
      if (trace_mg)
         printf("handle_http_event_mg: nc %p, ev %d, ev_data %p -> http request\n", nc, ev, ev_data);
      handle_http_message(nc, (http_message*)ev_data);
      break;
   default:
      if (trace_mg)
         printf("handle_http_event_mg: nc %p, ev %d, ev_data %p\n", nc, ev, ev_data);
      break;
   }
}

static void handle_http_redirect(struct mg_connection *nc, int ev, void *ev_data)
{
   switch (ev) {
   case MG_EV_HTTP_REQUEST:
      {
         http_message* msg = (http_message*)ev_data;
         if (trace_mg)
            printf("handle_http_redirect: nc %p, ev %d, ev_data %p -> http request\n", nc, ev, ev_data);

         mg_printf(nc, "HTTP/1.1 302 Found\r\nLocation: https://%s%s\r\n\r\n",
                   ((std::string*)(nc->user_data))->c_str(),
                   mgstr(&msg->uri).c_str());
         nc->flags |= MG_F_SEND_AND_CLOSE;
      }
      break;
   default:
      if (trace_mg)
         printf("handle_http_redirect: nc %p, ev %d, ev_data %p\n", nc, ev, ev_data);
   }
}

#endif

#ifdef HAVE_MONGOOSE616

// from mongoose examples/multithreaded/multithreaded.c

//static sock_t s_sock[2];
static std::atomic_bool s_shutdown{false};
static struct mg_mgr s_mgr;
static std::atomic_uint32_t s_seqno{0};
static std::mutex s_mg_broadcast_mutex;

#if 0
// This info is passed to the worker thread
struct work_request {
   void* nc;
   MongooseWorkObject* w;
};
#endif

// This info is passed by the worker thread to mg_broadcast
struct work_result {
   void* nc = NULL;
   uint32_t check = 0x12345678;
   uint32_t seqno = 0;
   MongooseWorkObject* w = NULL;
   const char* p1 = NULL;
   size_t s1 = 0;
   const char* p2 = NULL;
   size_t s2 = 0;
   bool close_flag = false;
   bool send_501 = false;
};

#if 0
static void mongoose_queue(void *nc, MongooseWorkObject *w)
{
   struct work_request req = {nc, w};

   //printf("nc: %p: w: %d, queue work object!\n", nc, w->seqno);
         
   if (write(s_sock[0], &req, sizeof(req)) < 0) {
      fprintf(stderr, "mongoose_queue: Error: write(s_sock(0)) error %d (%s)\n", errno, strerror(errno));
      abort();
   }
}
#endif

static void on_work_complete(struct mg_connection *nc, int ev, void *ev_data)
{
   (void) ev;
   struct work_result *res = (struct work_result *)ev_data;

   //printf("nc: %p: w: %d, seqno: %d, check 0x%08x, offered nc %p, flags 0x%08x\n", res->nc, res->w->seqno, res->seqno, res->check, nc, (int)nc->flags);

   if (res->nc != nc)
      return;

   //printf("nc: %p: w: %d, on_work_complete: seqno: %d, send_501: %d, s1 %d, s2: %d, close_flag: %d\n", res->nc, res->w->seqno, res->seqno, res->send_501, (int)res->s1, (int)res->s2, res->close_flag);

   if (!res->w) {
      cm_msg(MERROR, "on_work_complete", "no work object!");
   } else {
      if (res->w->socket != nc->sock) {
         cm_msg(MERROR, "on_work_complete", "Should not send response to request from socket %d to socket %d, abort!", res->w->socket, nc->sock);
         cm_msg_flush_buffer();
         abort();
      }
   }

   if (res->send_501) {
      std::string response = "501 Not Implemented";
      mg_send_head(nc, 501, response.length(), NULL); // 501 Not Implemented
      mg_send(nc, response.c_str(), response.length());
   }

   if (res->s1 > 0)
      mg_send(nc, res->p1, res->s1);

   if (res->s2 > 0)
      mg_send(nc, res->p2, res->s2);

   if (res->close_flag) {
      // cannot do pipelined http if response generated by mhttpd
      // decode_get() has no Content-Length header.
      // must close the connection.
      nc->flags |= MG_F_SEND_AND_CLOSE;
   }

   res->w->send_done = true;
}

static void mongoose_send(void* nc, MongooseWorkObject* w, const char* p1, size_t s1, const char* p2, size_t s2, bool close_flag)
{
   //printf("nc: %p: send %d and %d\n", nc, (int)s1, (int)s2);
   struct work_result res;
   res.nc = nc;
   res.w  = w;
   res.seqno = s_seqno++; // thread-asfe, s_seqno is std::atomic_int
   res.p1 = p1;
   res.s1 = s1;
   res.p2 = p2;
   res.s2 = s2;
   res.close_flag = close_flag;
   res.send_501 = false;
   //printf("nc: %p: call mg_broadcast()\n", nc);

   // NB: mg_broadcast() is advertised as thread-safe, but it is not.
   //
   // in mongoose 6.16, mg_brodacast() and mg_mgr_handle_ctl_sock() have several problems:
   //
   // a) "wrong thread" read from mgr->ctl[0], defeating the handshake
   //
   // b) "lost messages". if more than one message is written to mgr->ctl[0], the second message
   //    will be "eaten" by mg_mgr_handle_ctl_sock() because of mistatch between number of bytes read and written
   //    in the two functions. mg_mgr_handle_ctl_sock() always reads about 8000 bytes while mg_broadcast()
   //    writes 8 bytes per message, (per examples/multithreaded/multithreaded.c. mhttpd messages are a bit longer).
   //    So if multiple messages are present in the msg->ctl[0] pipe, the read call (of about 8000 bytes)
   //    in mg_mgr_handle_ctl_sock() will return several messages (last message may be truncated)
   //    but only the first message will be processed by the code. any additional messages are ignored.
   //
   // Problems (a) and (b) are easy to fix by using a mutex to serialize mg_broadcast().
   //
   // c) if the mg_broadcast() message contains pointers to the data buffer to be sent out,
   //    the caller of mg_broadcast() should not free these data buffers until mg_send() is called
   //    in "on_work_complete()". In theory, the caller of mg_broadcast() could wait until on_work_complete()
   //    sets a "done" flag. In practice, if the corresponding network connection is closed before
   //    mg_mgr_handle_ctl_sock() has looped over it, on_work_complete() will never run
   //    and the "done" flag will never be set. (Of course, network connections are permitted to close
   //    at any time without warning, but) the firefox browser closes the network connections "a lot"
   //    especially when user pressed the "page reload" button at the moment when HTTP transations
   //    are "in flight". (google-chrome tends to permit these "lame duck" transactions to complete and mongoose
   //    does not see unexpected socket closures, at least not as many).
   //
   // To fix problem (c) I need to know when mg_mgr_handle_ctl_sock()'s loop over network connections
   // has completed (two cases: (a) my on_work_complete() was hopefully called and finished,
   // and (b) the "right" network connection was already closed (for whatever reason) and my on_work_complete()
   // was never called).
   //
   // My solution is to change the handshake between mg_broadcast() and mg_mgr_handle_ctl_sock() by sending
   // the handshake reply after looping over the network connections instead of after reading the message
   // from msg->ctl[1].
   //
   // This requires a modification to the code in mongoose.c. If this change is lost/undone, nothing will work.
   //

   s_mg_broadcast_mutex.lock();
   mg_broadcast(&s_mgr, on_work_complete, (void *)&res, sizeof(res));
   s_mg_broadcast_mutex.unlock();
}

static void mongoose_send_501(void* nc, MongooseWorkObject* w)
{
   struct work_result res;
   res.nc = nc;
   res.w  = w;
   res.seqno = s_seqno++; // thread-asfe, s_seqno is std::atomic_int
   res.p1 = 0;
   res.s1 = 0;
   res.p2 = 0;
   res.s2 = 0;
   res.close_flag = false;
   res.send_501 = true;
   //printf("nc: %p, call mg_broadcast()\n", nc);

   s_mg_broadcast_mutex.lock();
   mg_broadcast(&s_mgr, on_work_complete, (void *)&res, sizeof(res));
   s_mg_broadcast_mutex.unlock();
}

#if 0
void *worker_thread_proc(void *param)
{
   //struct mg_mgr *mgr = (struct mg_mgr *) param;
   struct work_request req = {0};
   
   while ((! _abort) && (! s_shutdown)) {
      int rd = read(s_sock[1], &req, sizeof(req));
      if (rd == 0) {
         // socket closed, shutdown the thread
         break;
      }
      if (rd < 0) {
         if (_abort || s_shutdown) {
            return NULL;
         }
         fprintf(stderr, "worker_thread_proc: Error: read(s_sock(1)) returned %d, error %d (%s)\n", rd, errno, strerror(errno));
         abort();
         return NULL;
      }
      
      //printf("nc: %p: received request!\n", req.nc);

      int response = thread_work_function(req.nc, req.w);

      if (response == RESPONSE_501) {
         if (trace_mg||verbose_mg)
            printf("handle_http_message: sending 501 Not Implemented error\n");
         mongoose_send_501(req.nc, req.w);
      }

      req.w->t->fCompleted = true;
      gTraceBuf->AddTraceMTS(req.w->t);

      //printf("nc: %p: w: %d, delete work object!\n", req.nc, req.w->seqno);

      delete req.w;
      req.w = NULL;
   }
   return NULL;
}
#endif

static void mongoose_thread(MongooseThreadObject* to)
{
   //printf("to %p, nc %p: thread %p started!\n", to, to->fNc, to->fThread);

   std::unique_lock<std::mutex> ulm(to->fMutex, std::defer_lock);

   to->fIsRunning = true;
   
   while ((! _abort) && (! s_shutdown)) {
      MongooseWorkObject *w = NULL;

      ulm.lock();
      while (to->fQueue.empty()) {
         //printf("to %p, nc %p, thread %p: waiting!\n", to, to->fNc, to->fThread);
         to->fNotify.wait(ulm);
         if (_abort || s_shutdown) {
            break;
         }
      }

      if (_abort || s_shutdown) {
         break;
      }
      
      w = to->fQueue.front();
      to->fQueue.pop_front();
      ulm.unlock();

      //printf("to %p, nc %p: w: %d, received request!\n", to, w->nc, w->seqno);

      int response = thread_work_function(w->nc, w);

      if (response == RESPONSE_501) {
         if (trace_mg||verbose_mg)
            printf("handle_http_message: sending 501 Not Implemented error\n");
         mongoose_send_501(w->nc, w);
      }

      w->t->fCompleted = true;
      gTraceBuf->AddTraceMTS(w->t);

      //printf("nc: %p: w: %d, delete work object!\n", w->nc, w->seqno);

      delete w;
   }

   to->fIsRunning = false;

   //printf("to %p, nc %p: thread %p finished!\n", to, to->fNc, to->fThread);
}

static bool mongoose_hostlist_enabled(const struct mg_connection *nc);

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data)
{
   (void) nc;
   (void) ev_data;

   //if (trace_mg && ev != 0) {
   //   printf("ev_handler: connection %p, event %d\n", nc, ev);
   //}
   
   switch (ev) {
   case 0:
      break;
   default: { 
      if (trace_mg) {
         printf("ev_handler: connection %p, event %d\n", nc, ev);
      }
      break;
   }
   case MG_EV_ACCEPT:
      if (trace_mg) {
         printf("ev_handler: connection %p, MG_EV_ACCEPT\n", nc);
      }
      if (s_shutdown) {
         //printf("XXX nc %p!\n", nc);
         nc->flags |= MG_F_CLOSE_IMMEDIATELY;
      } else if (mongoose_hostlist_enabled(nc)) {
         if (!mongoose_check_hostlist(&nc->sa)) {
            nc->flags |= MG_F_CLOSE_IMMEDIATELY;
         }
      }
      break;
   case MG_EV_RECV:
      if (trace_mg_recv) {
         printf("ev_handler: connection %p, MG_EV_RECV, %d bytes\n", nc, *(int*)ev_data);
      }
      if (s_shutdown) {
         //printf("RRR nc %p!\n", nc);
         nc->flags |= MG_F_CLOSE_IMMEDIATELY;
      }
      break;
   case MG_EV_SEND:
      if (trace_mg_send) {
         printf("ev_handler: connection %p, MG_EV_SEND, %d bytes\n", nc, *(int*)ev_data);
      }
      break;
   case MG_EV_HTTP_CHUNK: {
      if (trace_mg) {
         printf("ev_handler: connection %p, MG_EV_HTTP_CHUNK\n", nc);
      }
      if (s_shutdown) {
         //printf("RRR1 nc %p!\n", nc);
         nc->flags |= MG_F_CLOSE_IMMEDIATELY;
      }
      break;
   }
   case MG_EV_HTTP_REQUEST: {
      struct http_message* msg = (struct http_message*)ev_data;
      if (trace_mg) {
         printf("ev_handler: connection %p, MG_EV_HTTP_REQUEST \"%s\" \"%s\"\n", nc, mgstr(&msg->method).c_str(), mgstr(&msg->uri).c_str());
      }
      if (s_shutdown) {
         //printf("RRR2 nc %p!\n", nc);
         nc->flags |= MG_F_CLOSE_IMMEDIATELY;
      } else {
         handle_http_message(nc, msg);
      }
      break;
   }
   case MG_EV_CLOSE: {
      if (trace_mg) {
         printf("ev_handler: connection %p, MG_EV_CLOSE\n", nc);
      }
      //printf("CCC nc %p!\n", nc);
      FreeThread(nc);
   }
   }
}

#define FLAG_HTTPS     MG_F_USER_1
#define FLAG_PASSWORDS MG_F_USER_2
#define FLAG_HOSTLIST  MG_F_USER_3

static bool mongoose_passwords_enabled(const struct mg_connection *nc)
{
   int flags = 0;
   if (nc && nc->listener) {
      flags = nc->listener->flags;
   }
   //printf("mongoose_passwords_enabled: nc %p, listener %p, flags 0x%lx, user_data %p, flags 0x%x\n", nc, nc->listener, nc->listener->flags, nc->listener->user_data, flags);
   return flags & FLAG_PASSWORDS;
}

static bool mongoose_hostlist_enabled(const struct mg_connection *nc)
{
   int flags = 0;
   if (nc && nc->listener) {
      flags = nc->listener->flags;
   }
   //printf("mongoose_hostlist_enabled: nc %p, listener %p, flags 0x%lx, user_data %p, flags 0x%x\n", nc, nc->listener, nc->listener->flags, nc->listener->user_data, flags);
   return flags & FLAG_HOSTLIST;
}

static int mongoose_listen(const char* address, int flags)
{
#if MG_ENABLE_SSL
#else
   if (flags & FLAG_HTTPS) {
      cm_msg(MERROR, "mongoose_listen", "https port \"%s\" requested, but mhttpd compiled without MG_ENABLE_SSL", address);
      return SS_SOCKET_ERROR;
   }
#endif

   struct mg_connection *nc = mg_bind(&s_mgr, address, ev_handler);
   if (nc == NULL) {
      cm_msg(MERROR, "mongoose_listen", "Cannot mg_bind address \"%s\"", address);
      return SS_SOCKET_ERROR;
   }

   if (flags & FLAG_HTTPS) {
#if MG_ENABLE_SSL
      std::string cert_file;

      int status = find_file_mg("ssl_cert.pem", cert_file, NULL, trace_mg);

      if (status != SUCCESS) {
         cm_msg(MERROR, "mongoose_listen", "cannot find SSL certificate file \"%s\"", cert_file.c_str());
         cm_msg(MERROR, "mongoose_listen", "please create SSL certificate file using openssl: cd $MIDASSYS; openssl req -new -nodes -newkey rsa:2048 -sha256 -out ssl_cert.csr -keyout ssl_cert.key -subj \"/C=/ST=/L=/O=midas/OU=mhttpd/CN=localhost\"; openssl x509 -req -days 365 -sha256 -in ssl_cert.csr -signkey ssl_cert.key -out ssl_cert.pem; cat ssl_cert.key >> ssl_cert.pem");
         cm_msg(MERROR, "mongoose_listen", "or using certbot (recommened): setup certbot per Let's Encrypt instructions, certificates are typically saved in /etc/letsencrypt/live/$HOSTNAME/, copy fullchain.pem and privkey.pem to $MIDASSYS; cd $MIDASSYS; cat fullchain.pem privkey.pem > ssl_cert.pem");
         return SS_FILE_ERROR;
      }

      printf("Mongoose web server will use https certificate file \"%s\"\n", cert_file.c_str());

      const char* errmsg = mg_set_ssl(nc, cert_file.c_str(), NULL);
      if (errmsg) {
         cm_msg(MERROR, "mongoose_listen", "Cannot enable https with certificate file \"%s\", error: %s", cert_file.c_str(), errmsg);
         return SS_SOCKET_ERROR;
      }

      // NB: where is the warning that the SSL certificate has expired?!? K.O.
#else
      abort(); // cannot happen!
#endif
   }

   mg_set_protocol_http_websocket(nc);

   nc->flags |= flags;

   printf("Mongoose web server listening on %s address \"%s\", passwords %s, hostlist %s\n", (flags&FLAG_HTTPS)?"https":"http", address, (flags&FLAG_PASSWORDS)?"enabled":"OFF", (flags&FLAG_HOSTLIST)?"enabled":"OFF");

   return SUCCESS;
}

static int mongoose_init(MVOdb* odb, bool no_passwords, bool no_hostlist, const std::vector<std::string>& user_hostlist)
{
   bool enable_localhost_port = true;
   int  localhost_port           = 8080;
   bool localhost_port_passwords = false;

   bool enable_insecure_port     = false;
   int  insecure_port            = 8081;
   bool insecure_port_passwords  = true;
   bool insecure_port_hostlist   = true;

   bool enable_https_port        = false;
   int  https_port               = 8443;
   bool https_port_passwords     = true;
   bool https_port_hostlist      = false;

   std::vector<std::string> hostlist;
   hostlist.push_back("localhost");

   bool enable_ipv6 = true;

   odb->RB("Enable localhost port", &enable_localhost_port, true);
   odb->RI("localhost port", &localhost_port, true);
   odb->RB("localhost port passwords", &localhost_port_passwords, true);
   odb->RB("Enable insecure port", &enable_insecure_port, true);
   odb->RI("insecure port", &insecure_port, true);
   odb->RB("insecure port passwords", &insecure_port_passwords, true);
   odb->RB("insecure port host list", &insecure_port_hostlist, true);
   odb->RB("Enable https port", &enable_https_port, true);
   odb->RI("https port", &https_port, true);
   odb->RB("https port passwords", &https_port_passwords, true);
   odb->RB("https port host list", &https_port_hostlist, true);
   odb->RSA("Host list", &hostlist, true, 10, 256);
   odb->RB("Enable IPv6", &enable_ipv6, true);

   // populate the MIME.types table
   gProxyOdb = odb->Chdir("Proxy", true);
   std::string proxy_example = "#http://localhost:8080";
   gProxyOdb->RS("example", &proxy_example, true);
   
   // populate the MIME.types table
   SaveMimetypes(odb->Chdir("mime.types", true));
   
   if (!no_passwords
       && ((enable_localhost_port && localhost_port_passwords)
           || (enable_insecure_port && insecure_port_passwords)
           || (enable_https_port && https_port_passwords))) {
      gAuthMg = new Auth();
      int status = gAuthMg->Init();
      if (status != SUCCESS) {
         printf("mongoose_init: Error: Cannot initialize authorization object!\n");
         return status;
      }
      printf("Mongoose web server will use HTTP Digest authentication with realm \"%s\" and password file \"%s\"\n", gAuthMg->realm.c_str(), gAuthMg->passwd_filename.c_str());
   } else {
      printf("Mongoose web server will not use password protection\n");
   }

   if (!no_hostlist
       && ((enable_insecure_port && insecure_port_hostlist)
           || (enable_https_port && https_port_hostlist))) {
      gAllowedHosts.clear();

      // copy the user allowed hosts
      for (unsigned int i=0; i<user_hostlist.size(); i++)
         gAllowedHosts.push_back(user_hostlist[i]);

      for (unsigned i=0; i<hostlist.size(); i++) {
         std::string s = hostlist[i];
         if (s.length() < 1) // skip emties
            continue;

         if (s[0] == '#') // skip commented-out entries
            continue;

         //printf("add allowed hosts %d [%s]\n", i, s.c_str());
         gAllowedHosts.push_back(s);
      }

      printf("Mongoose web server will use the hostlist, connections will be accepted only from: ");
      for (unsigned i=0; i<gAllowedHosts.size(); i++) {
         if (i>0)
            printf(", ");
         printf("%s", gAllowedHosts[i].c_str());
      }
      printf("\n");
   } else {
      printf("Mongoose web server will not use the hostlist, connections from anywhere will be accepted\n");
   }

   mg_mgr_init(&s_mgr, NULL);

   if (enable_localhost_port) {
      char str[256];
      sprintf(str, "localhost:%d", localhost_port);
      mongoose_listen(str, 0);
      if (enable_ipv6) {
         sprintf(str, "[::1]:%d", localhost_port);
         mongoose_listen(str, 0);
      }
   }

   if (enable_insecure_port) {
      char str[256];
      int flags = 0;
      if (insecure_port_passwords)
         flags |= FLAG_PASSWORDS;
      if (insecure_port_hostlist)
         flags |= FLAG_HOSTLIST;
      if (enable_ipv6) {
         sprintf(str, "[::]:%d", insecure_port);
         mongoose_listen(str, flags);
      } else {
         sprintf(str, "%d", insecure_port);
         mongoose_listen(str, flags);
      }
   }

   if (enable_https_port) {
      char str[256];
      int flags = 0;
      if (https_port_passwords)
         flags |= FLAG_PASSWORDS;
      if (https_port_hostlist)
         flags |= FLAG_HOSTLIST;
      flags |= FLAG_HTTPS;
      if (enable_ipv6) {
         sprintf(str, "[::]:%d", https_port);
         mongoose_listen(str, flags);
      } else {
         sprintf(str, "%d", https_port);
         mongoose_listen(str, flags);
      }
   }

   return SUCCESS;
}

static void mongoose_poll(int msec = 200)
{
   mg_mgr_poll(&s_mgr, msec);
}

static void mongoose_cleanup()
{
   printf("Mongoose web server shutting down\n");

   s_shutdown = true;

   // close listener sockets
   if (s_mgr.active_connections) {
      struct mg_connection* nc = s_mgr.active_connections;
      while (nc) {
         //printf("nc %p, next %p, user_data %p, listener %p, flags %lu\n", nc, nc->next, nc->user_data, nc->listener, nc->flags);
         if (nc->flags & MG_F_LISTENING) {
            nc->flags |= MG_F_CLOSE_IMMEDIATELY;
         }
         nc = nc->next;
      }
   }

   // tell threads to shut down
   for (auto it : gMongooseThreads) {
      MongooseThreadObject* to = it;
      to->fNotify.notify_one();
   }

   // wait until all threads stop
   for (int i=0; i<10; i++) {
      int count_running = 0;
      for (auto it : gMongooseThreads) {
         MongooseThreadObject* to = it;
         //printf("AAA6C %p thread %p running %d!\n", to, to->fThread, to->fIsRunning);
         if (to->fIsRunning) {
            count_running++;
         }
      }
      printf("Mongoose web server shutting down, %d threads still running\n", count_running);
      if (count_running == 0)
         break;
      mongoose_poll(1000);
   }

   // delete thread objects
   for (auto it : gMongooseThreads) {
      MongooseThreadObject* to = it;
      //printf("AAA7B %p thread %p running %d!\n", to, to->fThread, to->fIsRunning);
      if (to->fIsRunning) {
         cm_msg(MERROR, "mongoose", "thread failed to shut down");
         continue;
      }
      to->fThread->join();
      delete to->fThread;
      delete to;
   }
   gMongooseThreads.clear();

   mg_mgr_free(&s_mgr);
   
   //closesocket(s_sock[0]);
   //closesocket(s_sock[1]);

   // make leak sanitizer happy!
   for (auto e : gHostlistCache) {
      delete e;
   }
   gHostlistCache.clear();
   if (gProxyOdb) {
      delete gProxyOdb;
      gProxyOdb = NULL;
   }
   if (gMimeTypesOdb) {
      delete gMimeTypesOdb;
      gMimeTypesOdb = NULL;
   }

   printf("Mongoose web server shut down\n");
}

#endif

#ifdef HAVE_MONGOOSE6

static bool mongoose_passwords_enabled(const struct mg_connection *nc)
{
   return true;
}

int start_mg(int user_http_port, int user_https_port, int socket_priviledged_port, int verbose)
{
   HNDLE hDB;
   int size;
   int status;

   //if (verbose)
   //   trace_mg = true;

   if (verbose)
      verbose_mg = true;

   status = cm_get_experiment_database(&hDB, NULL);
   assert(status == CM_SUCCESS);

   int http_port = 8080;
   int https_port = 8443;
   int http_redirect_to_https = 1;

   size = sizeof(http_port);
   db_get_value(hDB, 0, "/Experiment/midas http port", &http_port, &size, TID_INT, TRUE);

   size = sizeof(https_port);
   db_get_value(hDB, 0, "/Experiment/midas https port", &https_port, &size, TID_INT, TRUE);

   size = sizeof(http_redirect_to_https);
   db_get_value(hDB, 0, "/Experiment/http redirect to https", &http_redirect_to_https, &size, TID_BOOL, TRUE);

   bool need_cert_file = false;
   bool need_password_file = false;

   if (user_http_port)
      http_port = user_http_port;

   if (user_https_port)
      https_port = user_https_port;

   if (https_port) {
      need_cert_file = true;
      need_password_file = true;
   }

   if (!https_port)
      http_redirect_to_https = 0;

   if (http_port && !http_redirect_to_https) {
      // no passwords serving over http unless
      // http is just a redict to https
      need_password_file = false;
   }

   if (socket_priviledged_port >= 0) {
      // no passwords if serving unencrypted http on port 80
      need_password_file = false;
      printf("Mongoose web server password portection is disabled: serving unencrypted http on port 80\n");
   }

   bool have_at_least_one_port = false;

   std::string cert_file;

   if (need_cert_file) {
      status = find_file_mg("ssl_cert.pem", cert_file, NULL, trace_mg);

      if (status != SUCCESS) {
         cm_msg(MERROR, "mongoose", "cannot find SSL certificate file \"%s\"", cert_file.c_str());
         cm_msg(MERROR, "mongoose", "please create SSL certificate file: cd $MIDASSYS; openssl req -new -nodes -newkey rsa:2048 -sha256 -out ssl_cert.csr -keyout ssl_cert.key -subj \"/C=/ST=/L=/O=midas/OU=mhttpd/CN=localhost\"; openssl x509 -req -days 365 -sha256 -in ssl_cert.csr -signkey ssl_cert.key -out ssl_cert.pem; cat ssl_cert.key >> ssl_cert.pem");
         return SS_FILE_ERROR;
      }

      printf("Mongoose web server will use SSL certificate file \"%s\"\n", cert_file.c_str());
   }

   if (need_password_file) {
      gAuthMg = new Auth();
      status = gAuthMg->Init();
      if (status != SUCCESS) {
         printf("Error: Cannot initialize authorization object!\n");
         return status;
      }
      printf("Mongoose web server will use authentication realm \"%s\", password file \"%s\"\n", gAuthMg->realm.c_str(), gAuthMg->passwd_filename.c_str());
   } else {
      printf("Mongoose web server will not use password protection\n");
   }

   if (trace_mg)
      printf("start_mg!\n");

#ifndef OS_WINNT
   signal(SIGPIPE, SIG_IGN);
#endif

   if (!gTraceBuf) {
      gTraceBuf = new RequestTraceBuf;
   }

   if (!request_mutex) {
      status = ss_mutex_create(&request_mutex, FALSE);
      assert(status==SS_SUCCESS || status==SS_CREATED);
   }

   mg_mgr_init(&mgr_mg, NULL);

   // use socket bound to priviledged port (setuid-mode)
   if (socket_priviledged_port >= 0) {
      struct mg_connection* nc = mg_add_sock(&mgr_mg, socket_priviledged_port, handle_event_mg);
      if (nc == NULL) {
         cm_msg(MERROR, "mongoose", "Cannot create mg_connection for set-uid-root privileged port");
         return SS_SOCKET_ERROR;
      }

      nc->flags |= MG_F_LISTENING;
#ifdef MG_ENABLE_THREADS
      mg_enable_multithreading(nc);
#endif
      mg_set_protocol_http_websocket(nc);
      mg_register_http_endpoint(nc, "/", handle_http_event_mg);

      have_at_least_one_port = true;
      printf("mongoose web server is listening on the set-uid-root privileged port\n");
   }

   if (http_port != 80) { // port 80 is already handled by socket_priviledged_port
      char str[256];
      sprintf(str, "%d", http_port);
      struct mg_connection* nc = mg_bind(&mgr_mg, str, handle_event_mg);
      if (nc == NULL) {
         cm_msg(MERROR, "mongoose", "Cannot bind to port %d", http_port);
         return SS_SOCKET_ERROR;
      }

#ifdef MG_ENABLE_THREADS
      mg_enable_multithreading(nc);
#endif
      mg_set_protocol_http_websocket(nc);

      if (http_redirect_to_https) {
         char hostname[256];
         ss_gethostname(hostname, sizeof(hostname));
         char str[256];
         sprintf(str, "%d", https_port);
         std::string s = std::string(hostname) + ":" + std::string(str);
         nc->user_data = new std::string(s);
         mg_register_http_endpoint(nc, "/", handle_http_redirect);
         printf("mongoose web server is redirecting HTTP port %d to https://%s\n", http_port, s.c_str());
      } else {
         mg_register_http_endpoint(nc, "/", handle_http_event_mg);
      }

      have_at_least_one_port = true;
      printf("mongoose web server is listening on the HTTP port %d\n", http_port);
   }

   if (https_port) {
#ifdef MG_ENABLE_SSL
      char str[256];
      sprintf(str, "%d", https_port);
      struct mg_connection* nc = mg_bind(&mgr_mg, str, handle_event_mg);
      if (nc == NULL) {
         cm_msg(MERROR, "mongoose", "Cannot bind to port %d", https_port);
         return SS_SOCKET_ERROR;
      }

      mg_set_ssl(nc, cert_file.c_str(), NULL);
#ifdef MG_ENABLE_THREADS
      mg_enable_multithreading(nc);
#endif
      mg_set_protocol_http_websocket(nc);
      mg_register_http_endpoint(nc, "/", handle_http_event_mg);

      have_at_least_one_port = true;
      printf("mongoose web server is listening on the HTTPS port %d\n", https_port);
#else
      cm_msg(MERROR, "mongoose", "https port %d requested, but mhttpd compiled without MG_ENABLE_SSL", https_port);
      return SS_SOCKET_ERROR;
#endif
   }

   if (!have_at_least_one_port) {
      cm_msg(MERROR, "mongoose", "cannot start: no ports defined");
      return SS_FILE_ERROR;
   }

   return SUCCESS;
}

int stop_mg()
{
   if (trace_mg)
      printf("stop_mg!\n");

   // Stop the server.
   mg_mgr_free(&mgr_mg);

   if (trace_mg)
      printf("stop_mg done!\n");
   return SUCCESS;
}

int loop_mg()
{
   int status = SUCCESS;

   /* establish Ctrl-C handler - will set _abort to TRUE */
   ss_ctrlc_handler(ctrlc_handler);

   while (!_abort) {

      /* cm_yield() is not thread safe, need to take a lock */

#ifdef HAVE_MONGOOSE6
      status = ss_mutex_wait_for(request_mutex, 0);
#endif
      gMutex.lock();

      /* check for shutdown message */
      status = cm_yield(0);
      if (status == RPC_SHUTDOWN)
         break;

      gMutex.unlock();
#ifdef HAVE_MONGOOSE6
      status = ss_mutex_release(request_mutex);
#endif

      //ss_sleep(10);

      mg_mgr_poll(&mgr_mg, 10);
   }

   return status;
}
#endif

static MJsonNode* get_http_trace(const MJsonNode* params)
{
   if (!params) {
      MJSO *doc = MJSO::I();
      doc->D("get current value of mhttpd http_trace");
      doc->P(NULL, 0, "there are no input parameters");
      doc->R(NULL, MJSON_INT, "current value of http_trace");
      return doc;
   }

   return mjsonrpc_make_result("http_trace", MJsonNode::MakeInt(http_trace));
}

static MJsonNode* set_http_trace(const MJsonNode* params)
{
   if (!params) {
      MJSO* doc = MJSO::I();
      doc->D("set new value of mhttpd http_trace");
      doc->P(NULL, MJSON_INT, "new value of http_trace");
      doc->R(NULL, MJSON_INT, "new value of http_trace");
      return doc;
   }

   http_trace = params->GetInt();
   return mjsonrpc_make_result("http_trace", MJsonNode::MakeInt(http_trace));
}

static void add_rpc_functions()
{
   mjsonrpc_add_handler("set_http_trace", set_http_trace);
   mjsonrpc_add_handler("get_http_trace", get_http_trace);
}

/*------------------------------------------------------------------*/

int main(int argc, const char *argv[])
{
   int status;
   int daemon = FALSE;
#ifdef HAVE_MONGOOSE6
   int user_http_port = 0;
   int user_https_port = 0;
#endif
#ifdef HAVE_MONGOOSE616
   bool no_passwords = false;
   bool no_hostlist  = false;
#endif
   const char *myname = "mhttpd";

   setbuf(stdout, NULL);
   setbuf(stderr, NULL);
#ifdef SIGPIPE
   /* avoid getting killed by "Broken pipe" signals */
   signal(SIGPIPE, SIG_IGN);
#endif

#ifdef HAVE_MONGOOSE6
   //
   // if running setuid-root, unconditionally bind to port 80.
   //

   int socket_priviledged_port = -1;

#ifdef OS_UNIX
   // in setuid-root mode bind to priviledged port
   if (getuid() != geteuid()) {
      int port80 = 80;

      printf("mhttpd is running in setuid-root mode.\n");

      socket_priviledged_port = open_listening_socket(port80);
      if (socket_priviledged_port < 0) {
         printf("Cannot open listening socket on TCP port %d, aborting.\n", port80);
         exit(1);
      }

      // give up root privilege
      status = setuid(getuid());
      if (status != 0) {
         printf("Cannot give up root privelege, aborting.\n");
         exit(1);
      }
      status = setuid(getuid());
      if (status != 0) {
         printf("Cannot give up root privelege, aborting.\n");
         exit(1);
      }
   }
#endif
#endif

   char midas_hostname[256];
   char midas_expt[256];

   /* get default from environment */
   cm_get_environment(midas_hostname, sizeof(midas_hostname), midas_expt, sizeof(midas_expt));

   /* parse command line parameters */
#ifdef HAVE_MONGOOSE6
   gUserAllowedHosts.clear();
#else
   std::vector<std::string> user_hostlist;
#endif
   for (int i = 1; i < argc; i++) {
      if (argv[i][0] == '-' && argv[i][1] == 'D')
         daemon = TRUE;
      else if (argv[i][0] == '-' && argv[i][1] == 'v')
         verbose = TRUE;
      else if (argv[i][0] == '-' && argv[i][1] == 'E')
         elog_mode = TRUE;
      else if (argv[i][0] == '-' && argv[i][1] == 'H') {
         history_mode = TRUE;
#ifdef HAVE_MONGOOSE6
      } else if (strcmp(argv[i], "--http") == 0) {
         if (argv[i+1]) {
            user_http_port = atoi(argv[i+1]);
         }
      } else if (strcmp(argv[i], "--https") == 0) {
         if (argv[i+1]) {
            user_https_port = atoi(argv[i+1]);
         }
#endif
      } else if (strcmp(argv[i], "--trace-mg") == 0) {
         trace_mg = true;
         trace_mg_recv = true;
         trace_mg_send = true;
      } else if (strcmp(argv[i], "--no-trace-mg-recv") == 0) {
         trace_mg_recv = false;
      } else if (strcmp(argv[i], "--no-trace-mg-send") == 0) {
         trace_mg_send = false;
      } else if (strcmp(argv[i], "--verbose-mg") == 0) {
         verbose_mg = true;
#ifdef HAVE_MONGOOSE616
      } else if (strcmp(argv[i], "--no-multithread") == 0) {
         multithread_mg = false;
      } else if (strcmp(argv[i], "--no-passwords") == 0) {
         no_passwords = true;
      } else if (strcmp(argv[i], "--no-hostlist") == 0) {
         no_hostlist = true;
#endif
      } else if (argv[i][0] == '-') {
         if (i + 1 >= argc || argv[i + 1][0] == '-')
            goto usage;
         if (argv[i][1] == 'h')
            strlcpy(midas_hostname, argv[++i], sizeof(midas_hostname));
         else if (argv[i][1] == 'e')
            strlcpy(midas_expt, argv[++i], sizeof(midas_hostname));
         else if (argv[i][1] == 'a') {
#ifdef HAVE_MONGOOSE6
            gUserAllowedHosts.push_back(argv[++i]);
#else
            user_hostlist.push_back(argv[++i]);
#endif
         } else if (argv[i][1] == 'p') {
            printf("Option \"-p port_number\" for the old web server is obsolete.\n");
            printf("mongoose web server is the new default, port number is set in ODB or with \"--http port_number\".\n");
            printf("To run the obsolete old web server, please use \"--oldserver\" switch.\n");
            return 1;
         } else {
          usage:
            printf("usage: %s [-h Hostname[:port]] [-e Experiment] [-v] [-D] [-a Hostname]\n\n", argv[0]);
            printf("       -a add hostname to the hostlist of hosts allowed to connect to mhttpd\n");
            printf("       -e experiment to connect to\n");
            printf("       -h connect to midas server (mserver) on given host\n");
            printf("       -v display verbose HTTP communication\n");
            printf("       -D become a daemon\n");
            printf("       -E only display ELog system\n");
            printf("       -H only display history plots\n");
#ifdef HAVE_MONGOOSE6
            printf("       --http port - bind to specified HTTP port (default is ODB \"/Experiment/midas http port\")\n");
            printf("       --https port - bind to specified HTTP port (default is ODB \"/Experiment/midas https port\")\n");
#endif
            printf("       --verbose-mg - trace mongoose web requests\n");
            printf("       --trace-mg - trace mongoose events\n");
            printf("       --no-trace-mg-recv - do not trace mongoose recv events\n");
            printf("       --no-trace-mg-send - dop not trace mongoose send events\n");
#ifdef HAVE_MONGOOSE616
            printf("       --no-multithread - disable mongoose multithreading\n");
            printf("       --no-passwords - disable password protection\n");
            printf("       --no-hostlist - disable access control host list\n");
#endif
            return 0;
         }
      }
   }

   if (daemon) {
      printf("Becoming a daemon...\n");
      ss_daemon_init(FALSE);
   }

#ifdef OS_LINUX
   /* write PID file */
   FILE *f = fopen("/var/run/mhttpd.pid", "w");
   if (f != NULL) {
      fprintf(f, "%d", ss_getpid());
      fclose(f);
   }
#endif

   if (history_mode)
      myname = "mhttpd_history";

   /*---- connect to experiment ----*/
   status = cm_connect_experiment1(midas_hostname, midas_expt, myname, NULL,
                                   DEFAULT_ODB_SIZE, DEFAULT_WATCHDOG_TIMEOUT);
   if (status == CM_WRONG_PASSWORD)
      return 1;
   else if (status == DB_INVALID_HANDLE) {
      std::string s = cm_get_error(status);
      puts(s.c_str());
   } else if (status != CM_SUCCESS) {
      std::string s = cm_get_error(status);
      puts(s.c_str());
      return 1;
   }

   /* mhttpd needs the watchdog thread until we are sure
    * we do not have any long sleeps anywhere in the mhttpd code.
    * this includes reads from the history files or databases,
    * that can take arbitrary long time */
   cm_start_watchdog_thread();

   /* Get ODB handles */

   HNDLE hDB;

   cm_get_experiment_database(&hDB, NULL);

   MVOdb *odb = MakeMidasOdb(hDB);
   gOdb = odb;

   /* do ODB record checking */
   if (!check_odb_records(odb)) {
      // check_odb_records() fails with nothing printed to the terminal
      // because mhttpd does not print cm_msg(MERROR, ...) messages to the terminal.
      // At least print something!
      printf("check_odb_records() failed, see messages and midas.log, bye!\n");
      cm_disconnect_experiment();
      return 1;
   }

#ifdef HAVE_MONGOOSE6
   if (init_allowed_hosts() != SUCCESS) {
      printf("init_allowed_hosts() failed, see messages and midas.log, bye!\n");
      cm_disconnect_experiment();
      return 1;
   }

   if (verbose) {
      if (gAllowedHosts.size() > 0) {
         printf("mhttpd allowed hosts list: ");
         for (unsigned int i=0; i<gAllowedHosts.size(); i++) {
            if (i>0)
               printf(", ");
            printf("%s", gAllowedHosts[i].c_str());
         }
         printf("\n");
      } else {
         printf("mhttpd allowed hosts list is empty\n");
      }
   }

   // populate the MIME.types table
   SaveMimetypes(odb->Chdir("WebServer/mime.types", true));
#endif

   /* initialize odb entries needed for mhttpd and midas web pages */
   init_mhttpd_odb(odb);

   /* initialize menu buttons */
   init_menu_buttons(odb);

#ifdef OLD_SEQUENCER
   /* initialize sequencer */
   init_sequencer(odb);
#endif

   /* initialize elog odb entries */
   init_elog_odb();

   /* initialize the JSON RPC handlers */
   mjsonrpc_init();
   mjsonrpc_set_std_mutex(&gMutex);

   add_rpc_functions();

#ifdef HAVE_MONGOOSE6
   status = start_mg(user_http_port, user_https_port, socket_priviledged_port, verbose);
   if (status != SUCCESS) {
      // At least print something!
      printf("could not start the mongoose web server, see messages and midas.log, bye!\n");
      cm_disconnect_experiment();
      return 1;
   }
#endif
   
#ifdef HAVE_MONGOOSE616

#ifdef SIGPIPE
#ifdef SIG_IGN
   signal(SIGPIPE, SIG_IGN);
#endif
#endif

   if (!gTraceBuf) {
      gTraceBuf = new RequestTraceBuf;
   }

   //if (!request_mutex) {
   //   status = ss_mutex_create(&request_mutex, FALSE);
   //   assert(status==SS_SUCCESS || status==SS_CREATED);
   //}

   /* establish Ctrl-C handler - will set _abort to TRUE */
   ss_ctrlc_handler(ctrlc_handler);

   MVOdb* o = odb->Chdir("WebServer", true);
   status = mongoose_init(o, no_passwords, no_hostlist, user_hostlist);
   if (status != SUCCESS) {
      // At least print something!
      printf("Error: Could not start the mongoose web server, see messages and midas.log, bye!\n");
      cm_disconnect_experiment();
      return 1;
   }

   delete o;
#endif

#ifdef HAVE_MONGOOSE6
   loop_mg();
   stop_mg();
#endif

#ifdef HAVE_MONGOOSE616
   while (!_abort) {

      /* cm_yield() is not thread safe, need to take a lock */

      //status = ss_mutex_wait_for(request_mutex, 0);
      gMutex.lock();

      /* check for shutdown message */
      status = cm_yield(0);
      if (status == RPC_SHUTDOWN)
         break;

      gMutex.unlock();
      //status = ss_mutex_release(request_mutex);

      //ss_sleep(10);

      mongoose_poll(10);
   }

   mongoose_cleanup();
#endif

   if (gMh) {
      delete gMh;
      gMh = NULL;
      gMhkey = 0;
   }

   mjsonrpc_exit();
   cm_disconnect_experiment();
   return 0;
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
