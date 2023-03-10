/********************************************************************\

  Name:         mhttpd.cxx
  Created by:   Stefan Ritt

  Contents:     Web server program for midas RPC calls

\********************************************************************/

#include <math.h>
#include <assert.h>
#include <float.h>
#include <algorithm>
#include "midas.h"
#include "msystem.h"
#include "mxml.h"
#ifndef HAVE_STRLCPY
#include "strlcpy.h"
#endif

extern "C" {
#include "mgd.h"
}
#include "history.h"

#ifdef HAVE_MSCB
#include "mscb.h"
#endif

#include "mjsonrpc.h"

#define STRLCPY(dst, src) strlcpy((dst), (src), sizeof(dst))
#define STRLCAT(dst, src) strlcat((dst), (src), sizeof(dst))

/* refresh times in seconds */
#define DEFAULT_REFRESH 60

/* time until mhttpd disconnects from MIDAS */
#define CONNECT_TIME  3600*24

static MUTEX_T* request_mutex = NULL;

/* size of buffer for incoming data, must fit sum of all attachments */
#define WEB_BUFFER_SIZE (6*1024*1024)

size_t return_size = WEB_BUFFER_SIZE;
char *return_buffer = (char*)malloc(return_size);

int strlen_retbuf;
int return_length;
char referer[256];

#define MAX_GROUPS    32
#define MAX_VARS     100
#define MAX_PARAM    500
#define PARAM_LENGTH 256
#define TEXT_SIZE  50000

char _param[MAX_PARAM][PARAM_LENGTH];
char *_value[MAX_PARAM];
char _text[TEXT_SIZE];
char *_attachment_buffer[3];
INT _attachment_size[3];
struct in_addr remote_addr;
INT _sock = -1;
BOOL elog_mode = FALSE;
BOOL history_mode = FALSE;
BOOL verbose = FALSE;
char midas_hostname[256];
char midas_expt[256];

extern const char *mname[];

char type_list[20][NAME_LENGTH] = {
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

char system_list[20][NAME_LENGTH] = {
   "General",
   "DAQ",
   "Detector",
   "Electronics",
   "Target",
   "Beamline"
};


struct Filetype {
   char ext[32];
   char type[32];
};

const Filetype filetype[] = {

   {
   ".JPG", "image/jpeg",}, {
   ".GIF", "image/gif",}, {
   ".PNG", "image/png",}, {
   ".SVG", "image/svg+xml",}, {
   ".PS",  "application/postscript",}, {
   ".EPS", "application/postscript",}, {
   ".HTML","text/html",}, {
   ".HTM", "text/html",}, {
   ".XLS", "application/x-msexcel",}, {
   ".DOC", "application/msword",}, {
   ".PDF", "application/pdf",}, {
   ".TXT", "text/plain",}, {
   ".ASC", "text/plain",}, {
   ".ZIP", "application/zip",}, {
   ".CSS", "text/css",}, {
   ".JS",  "application/javascript"}, {
""},};

#define HTTP_ENCODING "UTF-8"

typedef struct {
   char user[256];
   char msg[256];
   time_t last_time;
   time_t prev_time;
} LASTMSG;

/*------------------------------------------------------------------*/

const unsigned char favicon_png[] = {
   0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48,
   0x44, 0x52,
   0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90,
   0x91, 0x68,
   0x36, 0x00, 0x00, 0x00, 0x07, 0x74, 0x49, 0x4D, 0x45, 0x07, 0xD4, 0x0B, 0x1A, 0x08,
   0x37, 0x07,
   0x0D, 0x7F, 0x16, 0x5C, 0x00, 0x00, 0x00, 0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00,
   0x2E, 0x23,
   0x00, 0x00, 0x2E, 0x23, 0x01, 0x78, 0xA5, 0x3F, 0x76, 0x00, 0x00, 0x00, 0x04, 0x67,
   0x41, 0x4D,
   0x41, 0x00, 0x00, 0xB1, 0x8F, 0x0B, 0xFC, 0x61, 0x05, 0x00, 0x00, 0x01, 0x7D, 0x49,
   0x44, 0x41,
   0x54, 0x78, 0xDA, 0x63, 0xFC, 0xFF, 0xFF, 0x3F, 0x03, 0x29, 0x80, 0x09, 0xAB, 0xE8,
   0xD2, 0x65,
   0x77, 0x36, 0x6F, 0x7E, 0x8A, 0x5D, 0xC7, 0x7F, 0x0C, 0x30, 0x67, 0xEE, 0x0D, 0x56,
   0xCE, 0xCD,
   0x5C, 0xBC, 0x3B, 0xB6, 0x6D, 0x7F, 0x81, 0x29, 0xCB, 0x88, 0xE6, 0x24, 0x20, 0x57,
   0x50, 0x7C,
   0xDD, 0xCF, 0x1F, 0x6C, 0x40, 0xCB, 0xB5, 0xB5, 0x05, 0xCF, 0x1C, 0xB7, 0x42, 0xB3,
   0x80, 0x05,
   0x8D, 0xCF, 0xC8, 0xC8, 0x58, 0x5A, 0x2A, 0xFB, 0xF6, 0x4D, 0x37, 0x1B, 0xAB, 0xA0,
   0xB4, 0x4C,
   0x0A, 0x51, 0x4E, 0x02, 0x82, 0x85, 0xCB, 0x12, 0x0E, 0x1D, 0xAB, 0xC7, 0x2A, 0xC5,
   0x82, 0x69,
   0xC4, 0xAF, 0x5F, 0x7F, 0x1E, 0x3F, 0xF8, 0xCD, 0xCB, 0xF1, 0xF5, 0xEF, 0xDF, 0x7F,
   0xCC, 0xCC,
   0x4C, 0x84, 0x6D, 0x98, 0x59, 0xD5, 0xEB, 0xCF, 0xA5, 0x16, 0xC4, 0xAB, 0x71, 0x72,
   0xCB, 0x21,
   0x4C, 0x59, 0x74, 0x03, 0x5E, 0x3F, 0x7F, 0xB3, 0x6B, 0xD6, 0x22, 0x46, 0xA6, 0x7F,
   0x0C, 0x0C,
   0x7F, 0xD7, 0x75, 0x4D, 0xFB, 0xF1, 0xFD, 0x27, 0x81, 0x78, 0xB8, 0x7D, 0xE9, 0x0A,
   0xCB, 0xFF,
   0xDF, 0x4C, 0x8C, 0x8C, 0x40, 0xF6, 0xAD, 0x4B, 0x67, 0x1F, 0xDE, 0xBD, 0x8B, 0x45,
   0x03, 0x3C,
   0x60, 0x8F, 0x9D, 0xD8, 0xB3, 0xEB, 0x74, 0xB5, 0x90, 0x26, 0x07, 0x03, 0x48, 0xE4,
   0x3F, 0x8F,
   0xF6, 0xFF, 0x1B, 0x0F, 0x9A, 0x1E, 0x3E, 0x3A, 0xFB, 0xF3, 0xDB, 0x8F, 0xB7, 0x0F,
   0x9E, 0x43,
   0x83, 0xF1, 0xCF, 0xDF, 0x3F, 0x8A, 0x29, 0xCE, 0x3F, 0x7F, 0xFD, 0xFC, 0xCF, 0xF0,
   0xDF, 0x98,
   0xE9, 0xB5, 0x8F, 0xBD, 0x8A, 0x3C, 0x6F, 0xEC, 0xB9, 0x2D, 0x47, 0xFE, 0xFC, 0xFF,
   0x6F, 0x16,
   0x6C, 0xF3, 0xEC, 0xD3, 0x1C, 0x2E, 0x96, 0xEF, 0xBF, 0xAB, 0x7E, 0x32, 0x7D, 0xE2,
   0x10, 0xCE,
   0x88, 0xF4, 0x69, 0x2B, 0x60, 0xFC, 0xF4, 0xF5, 0x97, 0x78, 0x8A, 0x36, 0xD8, 0x44,
   0x86, 0x18,
   0x0D, 0xD7, 0x29, 0x95, 0x13, 0xD8, 0xD9, 0x58, 0xE1, 0x0E, 0xF8, 0xF1, 0xF3, 0xDB,
   0xC6, 0xD6,
   0xEC, 0x5F, 0x53, 0x8E, 0xBF, 0xFE, 0xC3, 0x70, 0x93, 0x8D, 0x6D, 0xDA, 0xCB, 0x0B,
   0x4C, 0x3F,
   0xFF, 0xFC, 0xFA, 0xCF, 0x0C, 0xB4, 0x09, 0x84, 0x54, 0xD5, 0x74, 0x91, 0x55, 0x03,
   0x01, 0x07,
   0x3B, 0x97, 0x96, 0x6E, 0xC8, 0x17, 0xFE, 0x7F, 0x4F, 0xF8, 0xFE, 0xBC, 0x95, 0x16,
   0x60, 0x62,
   0x62, 0x64, 0xE1, 0xE6, 0x60, 0x73, 0xD1, 0xB2, 0x7A, 0xFA, 0xE2, 0xF1, 0xDF, 0x3F,
   0xFF, 0xC4,
   0x78, 0x44, 0x31, 0xA3, 0x45, 0x2B, 0xD0, 0xE3, 0xF6, 0xD9, 0xE3, 0x2F, 0x2E, 0x9D,
   0x29, 0xA9,
   0xAC, 0x07, 0xA6, 0x03, 0xF4, 0xB4, 0x44, 0x10, 0x00, 0x00, 0x75, 0x65, 0x12, 0xB0,
   0x49, 0xFF,
   0x3F, 0x68, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
};

const unsigned char favicon_ico[] = {
   0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x10, 0x10, 0x10, 0x00, 0x01, 0x00, 0x04, 0x00,
   0x28, 0x01,
   0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
   0x20, 0x00,
   0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00,
   0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0xB4, 0x0F,
   0x0A, 0x00, 0x5C, 0x86, 0x4C, 0x00, 0x2F, 0x5E, 0x1A, 0x00, 0xBF, 0xD3, 0xD7, 0x00,
   0x29, 0x17,
   0x8D, 0x00, 0x50, 0xA7, 0xA4, 0x00, 0x59, 0x57, 0x7F, 0x00, 0xC6, 0xA3, 0xAC, 0x00,
   0xFC, 0xFE,
   0xFC, 0x00, 0x28, 0x12, 0x53, 0x00, 0x58, 0x7D, 0x72, 0x00, 0xC4, 0x3A, 0x34, 0x00,
   0x3C, 0x3D,
   0x69, 0x00, 0xC5, 0xB6, 0xB9, 0x00, 0x94, 0x92, 0x87, 0x00, 0x7E, 0x7A, 0xAA, 0x00,
   0x88, 0x88,
   0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x81, 0x22, 0xD8, 0x88, 0x88, 0x88, 0xF6, 0xD8,
   0x82, 0x22,
   0xE8, 0x88, 0x88, 0x8D, 0x44, 0x98, 0x82, 0x22, 0xA8, 0x88, 0x88, 0x8F, 0x44, 0x48,
   0x82, 0x22,
   0x25, 0x76, 0x67, 0x55, 0x44, 0xF8, 0x88, 0x88, 0x3A, 0xC9, 0x9C, 0x53, 0x83, 0x88,
   0x88, 0x88,
   0x8D, 0x99, 0x99, 0x38, 0x88, 0x88, 0x88, 0x88, 0x88, 0x99, 0x9C, 0x88, 0x88, 0x88,
   0x88, 0x88,
   0x88, 0xF9, 0x9D, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x8A, 0x58, 0x88, 0x88, 0x88,
   0x88, 0x88,
   0x88, 0x85, 0xD8, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0xEA, 0xAE, 0x88, 0x88, 0x88,
   0x88, 0x88,
   0x88, 0x00, 0x0B, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70, 0x0D, 0x88, 0x88, 0x88,
   0x88, 0x88,
   0x88, 0x87, 0xD8, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
   0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*------------------------------------------------------------------*/

void show_hist_page(const char *dec_path, const char* enc_path, char *buffer, int *buffer_size, int refresh);
int vaxis(gdImagePtr im, gdFont * font, int col, int gcol, int x1, int y1, int width,
          int minor, int major, int text, int label, int grid, double ymin, double ymax,
          BOOL logaxis);
void haxis(gdImagePtr im, gdFont * font, int col, int gcol, int x1, int y1, int width,
           int minor, int major, int text, int label, int grid, double xmin, double xmax);
void get_elog_url(char *url, int len);
void show_header(const char *title, const char *method, const char *path, int refresh);
void show_navigation_bar(const char *cur_page);
#ifdef OBSOLETE
char *get_js_filename();
#endif
const char *get_css_filename();

/* functions from sequencer.cxx */
extern void show_seq_page();
extern void sequencer();
extern void init_sequencer();

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

class auto_string
{
private:
   char* ptr;

public:
   auto_string(int size) // ctor
   {
      ptr = (char*)malloc(size);
   }

   ~auto_string() // dtor
   {
      if (ptr)
         free(ptr);
      ptr = NULL;
   }

   char* str() { return ptr; };
   char* c_str() { return ptr; };
};

/*------------------------------------------------------------------*/

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

void rsprintf(const char *format, ...)
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

/*------------------------------------------------------------------*/

/* Parameter handling functions similar to setenv/getenv */

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

/*------------------------------------------------------------------*/

static char _dec_path[256];

void set_dec_path(const char *path)
{
   strlcpy(_dec_path, path, sizeof(_dec_path));
}

char *get_dec_path()
{
   return _dec_path;
}

/*------------------------------------------------------------------*/

char *mhttpd_revision(void)
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
/********************************************************************\
   Encode mhttpd ODB path for embedding into HTML <a href="xxx"> elements.
   Encoding is intended to be compatible with RFC 3986 section 2 (adding of %XX escapes)
   Note 0: it is genarally safe to percent-escape everything
   Note 1: RFC 3986 specifies that '/' should be percent-escaped in path elements. But ODB path elements never contain '/' and the input if this function is '/'-separated paths, therefore this function does not escape '/'
   Note 2: do not use this function to encode query URLs that already contain the query separators '?' and '&'. The URL path and the individual query elements should be encoded separately, then concatenated.
\********************************************************************/
{
   char *pd, *p;
   int len = strlen(ps);
   char *str = (char*)malloc(len*3 + 10); // at worst, each input character is expanded into 3 output characters

   pd = str;
   p = ps;
   while (*p) {
      if (*p == '/') {
         *pd++ = *p++;
      } else if (*p == '.') {
         *pd++ = *p++;
      } else if (isalnum(*p)) {
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

/*------------------------------------------------------------------*/

LASTMSG lastMsg;
LASTMSG lastChatMsg;
LASTMSG lastTalkMsg;

INT print_message(const char *message)
{
   time_t tm;
   char str[80], line[256];

   /* prepare time */
   time(&tm);
   strcpy(str, ctime(&tm));
   str[19] = 0;

   /* print message text which comes after event header */
   strlcpy(line, str + 11, sizeof(line));
   strlcat(line, " ", sizeof(line));
   strlcat(line, (char *) message, sizeof(line));

   if (strstr(message, ",USER]") != NULL) {
      strlcpy(lastChatMsg.msg, line, sizeof(lastMsg.msg));
      lastChatMsg.prev_time = lastChatMsg.last_time;
      time(&lastChatMsg.last_time);
   } else if (strstr(message, ",TALK]") != NULL) {
      strlcpy(lastTalkMsg.msg, line, sizeof(lastMsg.msg));
      lastTalkMsg.prev_time = lastTalkMsg.last_time;
      time(&lastTalkMsg.last_time);
   } else {
      strlcpy(lastMsg.msg, line, sizeof(lastMsg.msg));
      lastMsg.prev_time = lastMsg.last_time;
      time(&lastMsg.last_time);
   }
   
   return SUCCESS;
}

void receive_message(HNDLE hBuf, HNDLE id, EVENT_HEADER * pheader, void *message)
{
   print_message((const char *)message);
}

/*-------------------------------------------------------------------*/

INT sendmail(const char* from_host, const char *smtp_host, const char *from, const char *to, const char *subject, const char *text)
{
   struct sockaddr_in bind_addr;
   struct hostent *phe;
   int i, s, strsize, offset;
   char *str, buf[256];
   time_t now;
   struct tm *ts;

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

   time(&now);
   ts = localtime(&now);
   strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S", ts);
   offset = (-(int) timezone);
   if (ts->tm_isdst)
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

void redirect(const char *path)
{
   char str[256];

   //printf("redirect to [%s]\n", path);

   strlcpy(str, path, sizeof(str));
   if (str[0] == 0)
      strcpy(str, "./");

   /* redirect */
   rsprintf("HTTP/1.1 302 Found\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
   rsprintf("Content-Type: text/html; charset=%s\r\n", HTTP_ENCODING);

   if (strncmp(path, "http:", 5) == 0)
      rsprintf("Location: %s\r\n\r\n<html>redir</html>\r\n", str);
   else if (strncmp(path, "https:", 6) == 0)
      rsprintf("Location: %s\r\n\r\n<html>redir</html>\r\n", str);
   else {
      rsprintf("Location: %s\r\n\r\n<html>redir</html>\r\n", str);
   }
}

void redirect2(const char *path)
{
   redirect(path);
   if (_sock != (-1)) {
      send_tcp(_sock, return_buffer, strlen(return_buffer) + 1, 0x10000);
      closesocket(_sock);
      _sock = -1;
      return_length = -1;
   }
}

/*------------------------------------------------------------------*/

INT search_callback(HNDLE hDB, HNDLE hKey, KEY * key, INT level, void *info)
{
   INT i, size, status;
   char *search_name, *p;
   static char data_str[MAX_ODB_PATH];
   static char str1[MAX_ODB_PATH], str2[MAX_ODB_PATH], ref[MAX_ODB_PATH];
   char path[MAX_ODB_PATH], data[10000];

   search_name = (char *) info;

   /* convert strings to uppercase */
   for (i = 0; key->name[i]; i++)
      str1[i] = toupper(key->name[i]);
   str1[i] = 0;
   for (i = 0; key->name[i]; i++)
      str2[i] = toupper(search_name[i]);
   str2[i] = 0;

   if (strstr(str1, str2) != NULL) {
      db_get_path(hDB, hKey, str1, MAX_ODB_PATH);
      strlcpy(path, str1 + 1, sizeof(path));    /* strip leading '/' */
      strlcpy(str1, path, sizeof(str1));
      urlEncode(str1, sizeof(str1));

      if (key->type == TID_KEY || key->type == TID_LINK) {
         /* for keys, don't display data value */
         rsprintf("<tr><td><a href=\"%s\">%s</a></tr>\n", str1, path);
      } else {
         /* strip variable name from path */
         p = path + strlen(path) - 1;
         while (*p && *p != '/')
            *p-- = 0;
         if (*p == '/')
            *p = 0;

         /* display single value */
         if (key->num_values == 1) {
            size = sizeof(data);
            status = db_get_data(hDB, hKey, data, &size, key->type);
            if (status == DB_NO_ACCESS)
               strcpy(data_str, "<no read access>");
            else
               db_sprintf(data_str, data, key->item_size, 0, key->type);

            sprintf(ref, "%s?cmd=Set", str1);

            rsprintf("<tr><td class=\"yellowLight\">");

            rsprintf("<a href=\"%s\">%s</a>/%s", path, path, key->name);

            rsprintf("<td><a href=\"%s\">%s</a></tr>\n", ref, data_str);
         } else {
            /* display first value */
            rsprintf("<tr><td rowspan=%d class=\"yellowLight\">%s\n", key->num_values, path);

            for (i = 0; i < key->num_values; i++) {
               size = sizeof(data);
               db_get_data(hDB, hKey, data, &size, key->type);
               db_sprintf(data_str, data, key->item_size, i, key->type);

               sprintf(ref, "%s?cmd=Set&index=%d", str1, i);

               if (i > 0)
                  rsprintf("<tr>");

               rsprintf("<td><a href=\"%s\">[%d] %s</a></tr>\n", ref, i, data_str);
            }
         }
      }
   }

   return SUCCESS;
}

/*------------------------------------------------------------------*/

void page_footer(BOOL bForm)  // wraps up body wrapper and inserts page footer
{
   time_t now;
   HNDLE hDB;
   char dec_path[256], path[256];

   /*---- spacer for footer ----*/
   rsprintf("<div class=\"push\"></div>\n");
   rsprintf("</div>\n"); //ends body wrapper

   /*---- footer div ----*/
   rsprintf("<div id=\"footerDiv\" class=\"footerDiv\">\n");
   cm_get_experiment_database(&hDB, NULL);
   std::string exptname;
   db_get_value_string(hDB, 0, "/Experiment/Name", 0, &exptname, TRUE);
   rsprintf("<div style=\"display:inline; float:left;\">Experiment %s</div>", exptname.c_str());
   rsprintf("<div style=\"display:inline;\">");
   
   /* add one "../" for each level */
   strlcpy(dec_path, get_dec_path(), sizeof(dec_path));
   path[0] = 0;
   char*p;
   for (p = dec_path ; *p ; p++)
      if (*p == '/')
         strlcat(path, "../", sizeof(path));
   if (path[strlen(path)-1] == '/')
      path[strlen(path)-1] = 0;

   // add speak JS code for chat messages
   time(&now);
   if (now < lastChatMsg.last_time + 60) {
      char usr[256];
      char msg[256];
      char tim[256];

      strlcpy(tim, ctime(&lastChatMsg.last_time)+11, sizeof(tim));
      tim[8] = 0;
      if (strchr(lastChatMsg.msg, '[')) {
         strlcpy(usr, strchr(lastChatMsg.msg, '[')+1, sizeof(usr));
         if (strchr(usr, ','))
            *strchr(usr, ',') = 0;
         if (strchr(lastChatMsg.msg, ']')) {
            strlcpy(msg, strchr(lastChatMsg.msg, ']')+2, sizeof(msg));
            rsprintf("<span class=\"chatBubbleFooter\">");
            rsprintf("<a href=\"./%s?cmd=Chat\">%s %s:%s</a>\n",
                     path, tim, usr, msg);
            rsprintf("</span>\n");
            
            rsprintf("<script>\n");
            rsprintf("  chat_maybeSpeak(\'%s\',\'%s\');\n", tim, msg);
            rsprintf("</script>\n");
         }
      }
   }

   // add speak JS code for talk messages
   time(&now);
   if (now < lastTalkMsg.last_time + 60) {
      char usr[256];
      char msg[256];
      char tim[256];
      
      strlcpy(tim, ctime(&lastTalkMsg.last_time)+11, sizeof(tim));
      tim[8] = 0;
      if (strchr(lastTalkMsg.msg, '[')) {
         strlcpy(usr, strchr(lastTalkMsg.msg, '[')+1, sizeof(usr));
         if (strchr(usr, ','))
            *strchr(usr, ',') = 0;
         if (strchr(lastTalkMsg.msg, ']')) {
            strlcpy(msg, strchr(lastTalkMsg.msg, ']')+2, sizeof(msg));
            rsprintf("<span class=\"chatBubbleFooter\">");
            rsprintf("<a href=\"./%s?cmd=Messages\">%s %s:%s</a>\n",
                     path, tim, usr, msg);
            rsprintf("</span>\n");
            
            rsprintf("<script>\n");
            rsprintf("  talk_maybeSpeak(\'%s\',\'%s\');\n", tim, msg);
            rsprintf("</script>\n");
         }
      }
   }

   rsprintf("<a href=\"./%s?cmd=Help\">Help</a>", path);
   
   rsprintf("</div>");
   rsprintf("<div style=\"display:inline; float:right;\">%s</div>", ctime(&now));
   rsprintf("</div>\n");
   
   /*---- top level form ----*/
   if (bForm)
      rsprintf("</form>\n");
   rsprintf("</body></html>\r\n");
}

FILE *open_resource_file(const char *filename, std::string* pfilename);

void show_help_page()
{
   const char *s;
   char str[256];
   int status;

   show_header("Help", "", "./", 0);
   rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   show_navigation_bar("Help");

   rsprintf("<table class=\"ODBTable\">\n");
   rsprintf("  <tr>\n");
   rsprintf("    <td class=\"subStatusTitle\">MIDAS Help Page</td>\n");
   rsprintf("  </tr>\n");
   rsprintf("  <tr>\n");
   rsprintf("    <td>\n");
   rsprintf("      <table>\n");

   rsprintf("        <tr>\n");
   rsprintf("          <td style=\"text-align:right;\">Documentation:</td>\n");
   rsprintf("          <td style=\"text-align:left;\"><a href=\"https://midas.triumf.ca\">https://midas.triumf.ca</a></td>\n");
   rsprintf("        </tr>\n");
   rsprintf("        <tr>\n");
   rsprintf("          <td style=\"text-align:right;\">Discussion Forum:</td>\n");
   rsprintf("          <td style=\"text-align:left;\"><a href=\"https://midas.triumf.ca/elog/Midas/\">https://midas.triumf.ca/elog/Midas/</a></td>\n");
   rsprintf("        </tr>\n");
   rsprintf("        <tr>\n");
   rsprintf("          <td style=\"text-align:right;\">Code:</td>\n");
   rsprintf("          <td style=\"text-align:left;\"><a href=\"https://bitbucket.org/tmidas/midas/\">https://bitbucket.org/tmidas/midas/</a></td>\n");
   rsprintf("        </tr>\n");

   rsprintf("        <tr>\n");
   rsprintf("          <td style=\"text-align:right;\">Version:</td>\n");
   rsprintf("          <td style=\"text-align:left;\">%s</td>\n", cm_get_version());
   rsprintf("        </tr>\n");
   rsprintf("        <tr>\n");
   rsprintf("          <td style=\"text-align:right;\">Revision:</td>\n");
   strlcpy(str, "https://bitbucket.org/tmidas/midas/commits/all?search=", sizeof(str));
   if (strrchr(cm_get_revision(), '-'))
      strlcat(str, strrchr(cm_get_revision(), '-')+2, sizeof(str));
   rsprintf("          <td style=\"text-align:left;\"><a href=\"%s\">%s</a></td>\n", str, cm_get_revision());
   rsprintf("        </tr>\n");

   rsprintf("        <tr>\n");
   rsprintf("          <td style=\"text-align:right;\">Experiment:</td>\n");
   cm_get_experiment_name(str, sizeof(str));
   rsprintf("          <td style=\"text-align:left;\">%s</td>\n", str);
   rsprintf("        </tr>\n");

   rsprintf("        <tr>\n");
   rsprintf("          <td style=\"text-align:right;\">MIDAS_EXPTAB:</td>\n");
   s = getenv("MIDAS_EXPTAB");
   if (!s) s = "";
   strlcpy(str, s, sizeof(str));
   rsprintf("          <td style=\"text-align:left;\">%s</td>\n", str);
   rsprintf("        </tr>\n");

   rsprintf("        <tr>\n");
   rsprintf("          <td style=\"text-align:right;\">MIDAS_DIR:</td>\n");
   s = getenv("MIDAS_DIR");
   if (!s) s = "";
   strlcpy(str, s, sizeof(str));
   rsprintf("          <td style=\"text-align:left;\">%s</td>\n", str);
   rsprintf("        </tr>\n");

   rsprintf("        <tr>\n");
   rsprintf("          <td style=\"text-align:right;\">MIDASSYS:</td>\n");
   s = getenv("MIDASSYS");
   if (!s) s = "";
   strlcpy(str, s, sizeof(str));
   rsprintf("          <td style=\"text-align:left;\">%s</td>\n", str);
   rsprintf("        </tr>\n");

   rsprintf("        <tr>\n");
   rsprintf("          <td style=\"text-align:right;\">CWD:</td>\n");
   if (!getcwd(str, sizeof(str)))
      str[0] = 0;
   rsprintf("          <td style=\"text-align:left;\">%s</td>\n", str);
   rsprintf("        </tr>\n");

   STRING_LIST list;
   status = cm_msg_facilities(&list);

   if (status == CM_SUCCESS) {
      if (list.size() == 1) {
         rsprintf("        <tr>\n");
         rsprintf("          <td style=\"text-align:right;\">System logfile:</td>\n");
         cm_msg_get_logfile("midas", 0, str, sizeof(str), NULL, 0);
         rsprintf("          <td style=\"text-align:left;\">%s</td>\n", str);
         rsprintf("        </tr>\n");
      } else {
         rsprintf("        <tr>\n");
         rsprintf("          <td style=\"text-align:right;\">Logfiles:</td>\n");
         rsprintf("          <td style=\"text-align:left;\">\n", str);
         for (unsigned i=0 ; i<list.size() ; i++) {
            if (i>0)
               rsputs("<br />\n");
            cm_msg_get_logfile(list[i].c_str(), 0, str, sizeof(str), NULL, 0);
            rsputs(str);
         }
         rsprintf("\n          </td>\n");
         rsprintf("        </tr>\n");
      }
   }

   rsprintf("        <tr>\n");
   rsprintf("          <td style=\"text-align:right;\">CSS File:</td>\n");
   std::string f;
   FILE *fp = open_resource_file(get_css_filename(), &f);
   if (fp) {
      fclose(fp);
      rsprintf("          <td style=\"text-align:left;\">%s</td>\n", f.c_str());
   } else
      rsprintf("          <td style=\"text-align:left;\">NOT FOUND</td>\n");
   rsprintf("        </tr>\n");

   rsprintf("        <tr>\n");
   rsprintf("          <td style=\"text-align:right;\">midas.css:</td>\n");
   fp = open_resource_file("midas.css", &f);
   if (fp) {
      fclose(fp);
      rsprintf("          <td style=\"text-align:left;\">%s</td>\n", f.c_str());
   } else
      rsprintf("          <td style=\"text-align:left;\">NOT FOUND</td>\n");
   rsprintf("        </tr>\n");

   rsprintf("        <tr>\n");
   rsprintf("          <td style=\"text-align:right;\">midas.js:</td>\n");
   fp = open_resource_file("midas.js", &f);
   if (fp) {
      fclose(fp);
      rsprintf("          <td style=\"text-align:left;\">%s</td>\n", f.c_str());
   } else
      rsprintf("          <td style=\"text-align:left;\">NOT FOUND</td>\n");
   rsprintf("        </tr>\n");

   rsprintf("        <tr>\n");
   rsprintf("          <td style=\"text-align:right;\">mhttpd.js:</td>\n");
   fp = open_resource_file("mhttpd.js", &f);
   if (fp) {
      fclose(fp);
      rsprintf("          <td style=\"text-align:left;\">%s</td>\n", f.c_str());
   } else
      rsprintf("          <td style=\"text-align:left;\">NOT FOUND</td>\n");
   rsprintf("        </tr>\n");

   rsprintf("        <tr>\n");
   rsprintf("          <td style=\"text-align:right;\">obsolete.js:</td>\n");
   fp = open_resource_file("obsolete.js", &f);
   if (fp) {
      fclose(fp);
      rsprintf("          <td style=\"text-align:left;\">%s</td>\n", f.c_str());
   } else
      rsprintf("          <td style=\"text-align:left;\">NOT FOUND</td>\n");
   rsprintf("        </tr>\n");

   rsprintf("        <tr>\n");
   rsprintf("          <td style=\"text-align:right;\">JSON-RPC schema:</td>\n");
   rsprintf("          <td style=\"text-align:left;\"><a href=\"?mjsonrpc_schema\">json format</a> or <a href=\"?mjsonrpc_schema_text\">text table format</a></td>\n");
   rsprintf("        </tr>\n");

   rsprintf("        <tr>\n");
   rsprintf("          <td style=\"text-align:right;\">JavaScript examples:</td>\n");
   rsprintf("          <td style=\"text-align:left;\"><a href=\"?cmd=example\">example.html</a></td>\n");
   rsprintf("        </tr>\n");

   rsprintf("      </table>\n");
   rsprintf("    </td>\n");
   rsprintf("  </tr>\n");
   rsprintf("</table>\n");

   rsprintf("<div id=\"helpPush\" class=\"push\" style=\"height:50px;\"></div>\n");
   rsprintf("</div>\n");
   rsprintf("<div id=\"helpFooter\" class=\"footerDiv\" style=\"font-size:10pt;height:50px;\">\n");
   rsprintf("<div id=\"contribList\" style=\"display:inline;\">\n");
   rsprintf("Contributions: Pierre-Andre Amaudruz - Sergio Ballestrero - Suzannah Daviel - Peter Green - Qing Gu - Greg Hackman - Gertjan Hofman - Paul Knowles - Exaos Lee - Rudi Meier - Bill Mills - Glenn Moloney - Dave Morris - John M O'Donnell - Konstantin Olchanski - Chris Pearson - Renee Poutissou - Stefan Ritt - Ryu Sawada - Tamsen Schurman - Andreas Suter - Jan M.Wouters - Piotr Adam Zolnierczuk\n");
   rsprintf("</div></div>\n");

   rsprintf("</form>\n");

   rsprintf("<script type=\"text/javascript\">\n");
   rsprintf("window.onresize = function(){");
   rsprintf("var footerHeight = parseInt(document.getElementById(\"contribList\").offsetHeight,10)+25;");
   rsprintf("console.log(footerHeight);");
   rsprintf("document.getElementById(\"helpPush\").style.height = footerHeight+\"px\";");
   rsprintf("document.getElementById(\"helpFooter\").style.height=footerHeight+\"px\";");
   rsprintf("document.getElementById(\"wrapper\").style.margin= \"0 auto -\"+parseFloat(footerHeight)+\"px\";");
   rsprintf("};");
   rsprintf("window.onresize();");
   rsprintf("</script>");

   rsprintf("</body></html>\r\n");
}

/*------------------------------------------------------------------*/

void show_header(const char *title, const char *method, const char *path, int refresh)
{
   HNDLE hDB;
   time_t now;
   char str[256];

   cm_get_experiment_database(&hDB, NULL);

   /* header */
   rsprintf("HTTP/1.1 200 Document follows\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
   rsprintf("Cache-control: private, max-age=0, no-cache\r\n");
   rsprintf("Expires: Fri, 01 Jan 1983 00:00:00 GMT\r\n");
   rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

   rsprintf("<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\n");
   rsprintf("<html><head>\n");

   /* style sheet */
   rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
   rsprintf("<link rel=\"stylesheet\" href=\"%s\" type=\"text/css\" />\n", get_css_filename());

   /* auto refresh */
   if (refresh > 0)
      rsprintf("<meta http-equiv=\"Refresh\" content=\"%02d\">\n", refresh);

   rsprintf("<title>%s</title></head>\n", title);

   strlcpy(str, path, sizeof(str));
   if (str[0] == 0)
      strcpy(str, "./");

   urlEncode(str, sizeof(str));

   if (equal_ustring(method, "POST"))
      rsprintf
          ("<body><form name=\"form1\" method=\"POST\" action=\"%s\" enctype=\"multipart/form-data\">\n\n",
           str);
   else if (equal_ustring(method, "GET"))
      rsprintf("<body><form name=\"form1\" method=\"GET\" action=\"%s\">\n\n", str);

   /* title row */

   std::string exptname;
   db_get_value_string(hDB, 0, "/Experiment/Name", 0, &exptname, TRUE);
   time(&now);

   /*---- body needs wrapper div to pin footer ----*/
   rsprintf("<div id=\"wrapper\" class=\"wrapper\">\n");
}

/*------------------------------------------------------------------*/

void show_text_header()
{
   rsprintf("HTTP/1.1 200 Document follows\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
   rsprintf("Access-Control-Allow-Origin: *\r\n");
   rsprintf("Cache-control: private, max-age=0, no-cache\r\n");
   rsprintf("Expires: Fri, 01 Jan 1983 00:00:00 GMT\r\n");
   rsprintf("Content-Type: text/plain; charset=%s\r\n\r\n", HTTP_ENCODING);
}

/*------------------------------------------------------------------*/

void show_error(const char *error)
{
   /* header */
   rsprintf("HTTP/1.1 200 Document follows\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
   rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

   rsprintf("<html><head>\n");
   rsprintf("<link rel=\"stylesheet\" href=\"%s\" type=\"text/css\" />\n", get_css_filename());
   rsprintf("<title>MIDAS error</title></head>\n");
   rsprintf("<body><H1>%s</H1></body></html>\n", error);
}

/*------------------------------------------------------------------*/

int exec_script(HNDLE hkey)
/********************************************************************\

  Routine: exec_script

  Purpose: Execute script from /Script tree

  exec_script is enabled by the tree /Script
  The /Script struct is composed of list of keys
  from which the name of the key is the button name
  and the sub-structure is a record as follow:

  /Script/<button_name> = <script command> (TID_STRING)

  The "Script command", containing possible arguements,
  is directly executed.

  /Script/<button_name>/<script command>
                        <soft link1>|<arg1>
                        <soft link2>|<arg2>
                           ...

  The arguments for the script are derived from the
  subtree below <button_name>, where <button_name> must be
  TID_KEY. The subtree may then contain arguments or links
  to other values in the ODB, like run number etc.

\********************************************************************/
{
   HNDLE hDB;
   KEY key;
   std::string command;

   cm_get_experiment_database(&hDB, NULL);
   db_get_key(hDB, hkey, &key);

   if (key.type == TID_STRING) {
      int size = key.item_size;
      auto_string data(size);
      int status = db_get_data(hDB, hkey, data.str(), &size, TID_STRING);
      if (status != DB_SUCCESS) {
         cm_msg(MERROR, "exec_script", "key \"%s\" of type TID_STRING, db_get_data() error %d", key.name, status);
         return status;
      }
      command = data.c_str();
   } else if (key.type == TID_KEY) {
      for (int i = 0;; i++) {
         HNDLE hsubkey;
         KEY subkey;
         db_enum_key(hDB, hkey, i, &hsubkey);
         if (!hsubkey)
            break;
         db_get_key(hDB, hsubkey, &subkey);

         if (i > 0)
            command += " ";

         if (subkey.type == TID_KEY) {
            cm_msg(MERROR, "exec_script", "key \"%s/%s\" should not be TID_KEY", key.name, subkey.name);
            return DB_TYPE_MISMATCH;
         } else if (subkey.type == TID_STRING) {
            int size = subkey.item_size;
            auto_string data(size);
            int status = db_get_data(hDB, hsubkey, data.str(), &size, TID_STRING);
            if (status != DB_SUCCESS) {
               cm_msg(MERROR, "exec_script", "key \"%s/%s\" of type TID_STRING, db_get_data() error %d", key.name, subkey.name, status);
               return status;
            }
            command += data.c_str();
         } else {
            char str[256];
            int size = subkey.item_size;
            auto_string data(size);
            int status = db_get_data(hDB, hsubkey, data.str(), &size, subkey.type);
            if (status != DB_SUCCESS) {
               cm_msg(MERROR, "exec_script", "key \"%s/%s\" of type %d, db_get_data() error %d", key.name, subkey.name, subkey.type, status);
               return status;
            }
            db_sprintf(str, data.c_str(), subkey.item_size, 0, subkey.type);
            command += str;
         }
      }
   } else {
      cm_msg(MERROR, "exec_script", "key \"%s\" has invalid type %d, should be TID_STRING or TID_KEY", key.name, key.type);
      return DB_TYPE_MISMATCH;
   }

   // printf("exec_script: %s\n", command.c_str());

   if (command.length() > 0)
      ss_system(command.c_str());

   return SUCCESS;
}

/*------------------------------------------------------------------*/

void show_navigation_bar(const char *cur_page)
{
   char dec_path[256], path[256];

   /* add one "../" for each level */
   strlcpy(dec_path, get_dec_path(), sizeof(dec_path));
   path[0] = 0;

   for (char* p = dec_path ; *p ; p++)
      if (*p == '/')
         strlcat(path, "../", sizeof(path));
   if (path[strlen(path)-1] == '/')
      path[strlen(path)-1] = 0;

   //printf("dec_path [%s], path [%s]\n", dec_path, path);

   rsprintf("<script>\n");
   rsprintf("mhttpd_navigation_bar(\"%s\", \"%s\");\n", cur_page, path);
   rsprintf("</script>\n");
}

void init_menu_buttons()
{
   int status;
   HNDLE hDB;
   BOOL value = TRUE;
   int size = sizeof(value);
   cm_get_experiment_database(&hDB, NULL);
   db_get_value(hDB, 0, "/Experiment/Menu/Status", &value, &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Start", &value, &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Transition", &value, &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/ODB", &value, &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Messages", &value, &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Chat", &value, &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Elog", &value, &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Alarms", &value, &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Programs", &value, &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/History", &value, &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/MSCB", &value, &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Sequencer", &value, &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Config", &value, &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Example", &value, &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Experiment/Menu/Help", &value, &size, TID_BOOL, TRUE);
   //strlcpy(str, "Status, ODB, Messages, Chat, ELog, Alarms, Programs, History, MSCB, Sequencer, Config, Example, Help", sizeof(str));

   std::string buf;
   status = db_get_value_string(hDB, 0, "/Experiment/Menu buttons", 0, &buf, FALSE);
   if (status == DB_SUCCESS) {
      cm_msg(MERROR, "init_menu_buttons", "ODB \"/Experiment/Menu buttons\" is obsolete, please delete it.");
   }
}

#ifdef OBSOLETE
void xshow_navigation_bar(const char *cur_page)
{
   HNDLE hDB;
   char str[1000], dec_path[256], path[256], filename[256];
   int fh, size;

   cm_get_experiment_database(&hDB, NULL);

   /*---- display optional custom header ----*/
   size = sizeof(str);
   if (db_get_value(hDB, 0, "/Custom/Header", str, &size, TID_STRING, FALSE) == DB_SUCCESS) {
      size = sizeof(path);
      path[0] = 0;
      db_get_value(hDB, 0, "/Custom/Path", path, &size, TID_STRING, FALSE);
      if (path[0] && path[strlen(path)-1] != DIR_SEPARATOR)
         strlcat(path, DIR_SEPARATOR_STR, sizeof(path));
      strlcpy(filename, path, sizeof(filename));
      strlcat(filename, str, sizeof(filename));
      fh = open(filename, O_RDONLY | O_BINARY);
      if (fh > 0) {
         // show file contents
         size = lseek(fh, 0, SEEK_END) + 1;
         lseek(fh, 0, SEEK_SET);
         char* p = (char*)malloc(size+1);
         int rd = read(fh, p, size);
         if (rd > 0) {
            p[rd] = 0; // make sure string is zero-terminated
            rsputs(p);
         }
         close(fh);
         free(p);
      } else {
         // show HTML text directly
         rsputs(filename);
      }
   }

   rsprintf("<table class=\"navigationTable\">\n");
   rsprintf("<tr><td>\n");

   /*---- menu buttons ----*/

#ifdef HAVE_MSCB
   strlcpy(str, "Status, ODB, Messages, Chat, ELog, Alarms, Programs, History, MSCB, Sequencer, Config, Example, Help", sizeof(str));
#else
   strlcpy(str, "Status, ODB, Messages, Chat, ELog, Alarms, Programs, History, Sequencer, Config, Example, Help", sizeof(str));
#endif
   size = sizeof(str);
   db_get_value(hDB, 0, "/Experiment/Menu Buttons", str, &size, TID_STRING, TRUE);

   /* add one "../" for each level */
   strlcpy(dec_path, get_dec_path(), sizeof(dec_path));
   path[0] = 0;

   char*p;
   for (p = dec_path ; *p ; p++)
      if (*p == '/')
         strlcat(path, "../", sizeof(path));
   if (path[strlen(path)-1] == '/')
      path[strlen(path)-1] = 0;

   //printf("dec_path [%s], path [%s]\n", dec_path, path);
   
   p = strtok(str, ",");
   while (p) {

      while (*p == ' ')
         p++;
      strlcpy(str, p, sizeof(str));
      while (str[strlen(str)-1] == ' ')
         str[strlen(str)-1] = 0;

      if (equal_ustring(str, cur_page))
         rsprintf("<input type=button name=cmd value=\"%s\" class=\"navButtonSel\" onclick=\"window.location.href=\'./%s?cmd=%s\';return false;\">\n",
                  str, path, str);
      else
         rsprintf("<input type=button name=cmd value=\"%s\" class=\"navButton\" onclick=\"window.location.href=\'./%s?cmd=%s\';return false;\">\n",
                  str, path, str);

      p = strtok(NULL, ",");
   }

   rsprintf("</td></tr></table>\n\n");
}
#endif

/*------------------------------------------------------------------*/

int requested_transition = 0;
int requested_old_state = 0;

void show_status_page(int refresh, const char *cookie_wpwd, int expand_equipment)
{
   int i, j, k, h, m, s, status, size, type, n_items, n_hidden;
   BOOL flag, first, expand;
   char name[32], ref[MAX_STRING_LENGTH],
      value_str[MAX_STRING_LENGTH], status_data[MAX_STRING_LENGTH];
   const char *trans_name[] = { "Start", "Stop", "Pause", "Resume" };
   time_t now;
   DWORD difftime;
   double d, value;
   HNDLE hDB, hkey, hLKey, hsubkey, hkeytmp;
   KEY key;
   int  ftp_mode, previous_mode;
   char client_name[NAME_LENGTH];
   struct tm *gmt;
   BOOL new_window;

   RUNINFO runinfo;

   cm_get_experiment_database(&hDB, NULL);

   expand = FALSE;
   if (isparam("expand")) {
      expand = (BOOL)atoi(getparam("expand"));
      rsprintf("HTTP/1.1 302 Found\r\n");
      rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());

      time(&now);
      now += 3600 * 24;
      gmt = gmtime(&now);

      char str[256];
      strftime(str, sizeof(str), "%A, %d-%b-%Y %H:00:00 GMT", gmt);

      if (expand)
         rsprintf("Set-Cookie: midas_expeq=1; path=/; expires=%s\r\n", str);
      else
         rsprintf("Set-Cookie: midas_expeq=; path=/;\r\n");
      rsprintf("Location: ./\n\n<html>redir</html>\r\n");
      return;
   }

   db_find_key(hDB, 0, "/Runinfo", &hkey);
   assert(hkey);

   RUNINFO_STR(runinfo_str);

   size = sizeof(runinfo);
   status = db_get_record1(hDB, hkey, &runinfo, &size, 0, strcomb(runinfo_str));
   assert(status == DB_SUCCESS);

   /* header */
   rsprintf("HTTP/1.1 200 OK\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
   rsprintf("Content-Type: text/html; charset=%s\r\n", HTTP_ENCODING);
   rsprintf("Cache-control: private, max-age=0, no-cache\r\n");
   rsprintf("Expires: Fri, 01-Jan-1983 00:00:00 GMT\r\n");
   if (cookie_wpwd[0]) {
      time(&now);
      now += 3600 * 24;
      gmt = gmtime(&now);

      char str[256];
      strftime(str, sizeof(str), "%A, %d-%b-%Y %H:%M:%S GMT", gmt);

      rsprintf("Set-Cookie: midas_wpwd=%s; path=/; expires=%s\r\n", cookie_wpwd, str);
   }

   rsprintf("\r\n<html>\n");

#define NEW_START_STOP 1

#ifndef NEW_START_STOP
   /* auto refresh */
   i = 0;
   size = sizeof(i);
   db_get_value(hDB, 0, "/Runinfo/Transition in progress", &i, &size, TID_INT, FALSE);
   if (i > 0)
      rsprintf("<head><meta http-equiv=\"Refresh\" content=\"1\">\n");
   else {
#endif
      if (refresh > 0)
         rsprintf("<head><meta http-equiv=\"Refresh\" content=\"%02d\">\n", refresh);
#ifndef NEW_START_STOP
   }
#endif

   rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
   rsprintf("<link rel=\"stylesheet\" href=\"%s\" type=\"text/css\" />\n", get_css_filename());

   std::string exptname;
   db_get_value_string(hDB, 0, "/Experiment/Name", 0, &exptname, TRUE);

   time(&now);

   rsprintf("<title>%s status</title>\n", exptname.c_str());

   rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"obsolete.js\"></script>\n");

   rsprintf("</head>\n");

   rsprintf("<body><form method=\"GET\" action=\".\">\n");

   rsprintf("<div id=\"wrapper\" class=\"wrapper\">\n");

   /*---- navigation bar ----*/

   show_navigation_bar("Status");

   /*---- script buttons ----*/

   rsprintf("<table class=\"headerTable\">\n");

   status = db_find_key(hDB, 0, "Script", &hkey);
   if (status == DB_SUCCESS) {
      rsprintf("<tr><td>\n");

      for (i = 0;; i++) {
         db_enum_link(hDB, hkey, i, &hsubkey);
         if (!hsubkey)
            break;
         db_get_key(hDB, hsubkey, &key);
         rsprintf("<input type=submit name=script value=\"%s\">\n", key.name);
      }
      rsprintf("</td></tr>\n\n");
   }

   /*---- manual triggered equipment ----*/

   if (db_find_key(hDB, 0, "/equipment", &hkey) == DB_SUCCESS) {
      first = TRUE;
      for (i = 0;; i++) {
         db_enum_key(hDB, hkey, i, &hsubkey);
         if (!hsubkey)
            break;

         db_get_key(hDB, hsubkey, &key);

         db_find_key(hDB, hsubkey, "Common", &hkeytmp);

         if (hkeytmp) {
            size = sizeof(type);
            db_get_value(hDB, hkeytmp, "Type", &type, &size, TID_INT, TRUE);
            if (type & EQ_MANUAL_TRIG) {
               if (first)
                  rsprintf("<tr><td colspan=6>\n");

               first = FALSE;

               char str[256];
               sprintf(str, "Trigger %s event", key.name);
               rsprintf("<input type=submit name=cmd value=\"%s\">\n", str);
            }
         }
      }
      if (!first)
         rsprintf("</tr>\n\n");
   }

   /*---- aliases ----*/

   first = TRUE;

   db_find_key(hDB, 0, "/Alias", &hkey);
   if (hkey) {
      if (first) {
         rsprintf("<tr><td colspan=6>\n");
         first = FALSE;
      }
      for (i = 0;; i++) {
         db_enum_link(hDB, hkey, i, &hsubkey);
         if (!hsubkey)
            break;

         db_get_key(hDB, hsubkey, &key);

         strlcpy(name, key.name, sizeof(name));
         new_window = (name[strlen(name) - 1] != '&');
         if (!new_window)
            name[strlen(name) - 1] = 0;

         if (key.type == TID_STRING) {
            /* html link */
            size = sizeof(ref);
            db_get_data(hDB, hsubkey, ref, &size, TID_STRING);
            if (new_window)
               rsprintf("<button type=\"button\" onclick=\"window.open('%s');\">%s</button>\n", ref, name);
            else
               rsprintf("<button type=\"button\" onclick=\"document.location.href='%s';\">%s</button>\n", ref, name);
         } else if (key.type == TID_LINK) {
            /* odb link */
            sprintf(ref, "./Alias/%s", key.name);

            if (new_window)
               rsprintf("<button type=\"button\" onclick=\"window.open('%s');\">%s</button>\n", ref, name);
            else
               rsprintf("<button type=\"button\" onclick=\"document.location.href='%s';\">%s</button>\n", ref, name);
         }
      }
   }

   /*---- custom pages ----*/

   db_find_key(hDB, 0, "/Custom", &hkey);
   if (hkey) {
      for (i = 0;; i++) {
         db_enum_link(hDB, hkey, i, &hsubkey);
         if (!hsubkey)
            break;

         db_get_key(hDB, hsubkey, &key);

         /* skip "Images" */
         if (key.type != TID_STRING)
            continue;

         /* skip "Path" */
         if (equal_ustring(key.name, "Path"))
            continue;

         /* skip "Header" */
         if (equal_ustring(key.name, "Header"))
            continue;

         strlcpy(name, key.name, sizeof(name));

         /* check if hidden page */
         if (name[strlen(name) - 1] == '!')
            continue;

         if (first) {
            rsprintf("<tr><td colspan=6>\n");
            first = FALSE;
         }

         new_window = (name[strlen(name) - 1] != '&');
         if (!new_window)
            name[strlen(name) - 1] = 0;

         sprintf(ref, "./CS/%s", name);

         if (new_window)
            rsprintf("<button type=\"button\" onclick=\"window.open('%s');\">%s</button>\n", ref, name);
         else
            rsprintf("<button type=\"button\" onclick=\"document.location.href='%s';\">%s</button>\n", ref, name);
       }
   }
   rsprintf("</table>\n");

   /*---- begin main status reporting ----*/
   rsprintf("<table id=\"statusTable\">\n");

   /*---- alarms ----*/

   /* go through all triggered alarms */
   db_find_key(hDB, 0, "/Alarms/Alarms", &hkey);
   if (hkey) {
      bool first_alarm = true;
      /* check global alarm flag */
      flag = TRUE;
      size = sizeof(flag);
      db_get_value(hDB, 0, "/Alarms/Alarm System active", &flag, &size, TID_BOOL, TRUE);
      if (flag) {
         for (i = 0;; i++) {
            db_enum_link(hDB, hkey, i, &hsubkey);

            if (!hsubkey)
               break;

            size = sizeof(flag);
            db_get_value(hDB, hsubkey, "Triggered", &flag, &size, TID_INT, TRUE);
            if (flag) {
               std::string alarm_class;
               db_get_value_string(hDB, hsubkey, "Alarm Class", 0, &alarm_class, TRUE);

               std::string path;
               path  = "/Alarms/Classes/";
               path += alarm_class;
               path += "/Display BGColor";

               std::string bgcol = "red";
               db_get_value_string(hDB, 0, path.c_str(), 0, &bgcol, TRUE);

               path  = "/Alarms/Classes/";
               path += alarm_class;
               path += "/Display FGColor";

               std::string fgcol = "black";
               db_get_value_string(hDB, 0, path.c_str(), 0, &fgcol, TRUE);

               std::string msg;
               db_get_value_string(hDB, hsubkey, "Alarm Message", 0, &msg, TRUE);

               size = sizeof(j);
               db_get_value(hDB, hsubkey, "Type", &j, &size, TID_INT, TRUE);

               std::string text;

               if (j == AT_EVALUATED) {
                  std::string cond;
                  db_get_value_string(hDB, hsubkey, "Condition", 0, &cond, TRUE);

                  /* retrieve value */
                  al_evaluate_condition(cond.c_str(), value_str);
                  char str[MAX_STRING_LENGTH];
                  sprintf(str, msg.c_str(), value_str); // FIXME: overflows str!
                  text = str;
               } else {
                  text = msg;
               }

               db_get_key(hDB, hsubkey, &key);

               rsprintf("<tr>\n");

               rsprintf("<td colspan=6 style=\"background-color:%s;border-radius:12px;\" align=center>", bgcol.c_str());
               rsprintf("<table width=\"100%%\"><tr>\n");
               rsprintf("<td align=center width=\"99%%\" style=\"border:0px;\"><font color=\"%s\" size=+3>%s: %s</font></td>\n", fgcol.c_str(), alarm_class.c_str(), text.c_str());
               rsprintf("<td width=\"1%%\" style=\"border:0px;\">\n");
               rsprintf("<button type=\"button\" onclick=\"mhttpd_reset_alarm(\'%s\');\">Reset</button>\n", key.name);
               rsprintf("</td>\n");
               rsprintf("</tr></table>\n");
               rsprintf("</td>\n");

               std::string spk;
               spk = alarm_class;
               spk +=  ". ";
               spk += text;

               if (first_alarm) {
                  first_alarm = false;
                  std::string filename = "alarm.mp3";
                  db_get_value_string(hDB, 0, "/Alarms/Sound", 0, &filename, TRUE);
                  rsprintf("<script>mhttpd_alarm_play(\"%s\");</script>\n", filename.c_str());
               }

               rsprintf("<script type=\"text/javascript\">mhttpd_alarm_speak(\"%s\");</script>\n", spk.c_str());

               rsprintf("</tr>\n");
            }
         }
      }
   }

   /*---- Summary Table ----*/
   rsprintf("<tr><td colspan=6><table class=\"subStatusTable\" width=100%%>\n");

   /*---- Run status ----*/
   rsprintf("<tr><th colspan=6 class=\"subStatusTitle\">Run Status</th></tr>\n");

   /*---- Run number & buttons ----*/

   rsprintf("<tr align=center><td rowspan=3 id=\"runNumberCell\" ");

   if (runinfo.state == STATE_STOPPED)
      rsprintf("class=\"redLight\">Run<br>%d<br>", runinfo.run_number);
   else if (runinfo.state == STATE_PAUSED)
      rsprintf(" class=\"yellowLight\">Run<br>%d<br>", runinfo.run_number);
   else if (runinfo.state == STATE_RUNNING)
      rsprintf("class=\"greenLight\">Run<br>%d<br>", runinfo.run_number);

   if (runinfo.transition_in_progress)
      requested_transition = 0;
   if (runinfo.state != requested_old_state)
      requested_transition = 0;

   if (requested_transition == TR_STOP)
      rsprintf("<p id=\"transitionMessage\">Run stop requested</p>");
   else if (requested_transition == TR_START)
      rsprintf("<p id=\"transitionMessage\">Run start requested</p>");
   else if (requested_transition == TR_PAUSE)
      rsprintf("<p id=\"transitionMessage\">Run pause requested</p>");
   else if (requested_transition == TR_RESUME)
      rsprintf("<p id=\"transitionMessage\">Run resume requested</p>");
   else if (runinfo.transition_in_progress == TR_STOP)
      rsprintf("<p id=\"transitionMessage\">Stopping run</p>");
   else if (runinfo.transition_in_progress == TR_START)
      rsprintf("<p id=\"transitionMessage\">Starting run</p>");
   else if (runinfo.transition_in_progress == TR_PAUSE)
      rsprintf("<p id=\"transitionMessage\">Pausing run</p>");
   else if (runinfo.transition_in_progress == TR_RESUME)
      rsprintf("<p id=\"transitionMessage\">Resuming run</p>");
   else if (runinfo.requested_transition) {
      for (i = 0; i < 4; i++)
         if (runinfo.requested_transition & (1 << i))
            rsprintf("<br><b>%s requested</b>", trans_name[i]);
   } else {
      if (runinfo.state == STATE_STOPPED)
         rsprintf("Stopped");
      else if (runinfo.state == STATE_PAUSED)
         rsprintf("Paused");
      else if (runinfo.state == STATE_RUNNING)
         rsprintf("Running");
      rsprintf("<br>");
   }

   flag = TRUE;
   size = sizeof(flag);
   db_get_value(hDB, 0, "/Experiment/Start-Stop Buttons", &flag, &size, TID_BOOL, TRUE);
#ifdef NEW_START_STOP
   if (flag && !runinfo.transition_in_progress) {
      if (runinfo.state == STATE_STOPPED)
         rsprintf("<input type=button %s value=Start onClick=\"mhttpd_start_run();\">\n", runinfo.transition_in_progress?"disabled":"");
      else {
         if (runinfo.state == STATE_PAUSED || runinfo.state == STATE_RUNNING)
            rsprintf("<input type=button %s value=Stop onClick=\"mhttpd_stop_run();\">\n", runinfo.transition_in_progress?"disabled":"");
      }
   }
#else
   if (flag) {
      if (runinfo.state == STATE_STOPPED)
         rsprintf("<input type=submit name=cmd %s value=Start>\n", runinfo.transition_in_progress?"disabled":"");
      else {
         rsprintf("<script type=\"text/javascript\">\n");
         rsprintf("function stop()\n");
         rsprintf("{\n");
         rsprintf("   flag = confirm('Are you sure to stop the run?');\n");
         rsprintf("   if (flag == true)\n");
         rsprintf("      window.location.href = '?cmd=Stop';\n");
         rsprintf("}\n");
         if (runinfo.state == STATE_PAUSED || runinfo.state == STATE_RUNNING)
            rsprintf("document.write('<input type=button %s value=Stop onClick=\"stop();\">\\n');\n", runinfo.transition_in_progress?"disabled":"");
         rsprintf("</script>\n");
      }
   }
#endif

   flag = FALSE;
   size = sizeof(flag);
   db_get_value(hDB, 0, "/Experiment/Pause-Resume Buttons", &flag, &size, TID_BOOL, TRUE);
#ifdef NEW_START_STOP
   if (flag && !runinfo.transition_in_progress) {
      if (runinfo.state != STATE_STOPPED) {
         if (runinfo.state == STATE_RUNNING)
            rsprintf("<input type=button %s value=Pause onClick=\"mhttpd_pause_run();\">\n", runinfo.transition_in_progress?"disabled":"");
         if (runinfo.state == STATE_PAUSED)
            rsprintf("<input type=button %s value=Resume onClick=\"mhttpd_resume_run();\">\n", runinfo.transition_in_progress?"disabled":"");
      }
   }
#else
   if (flag) {
      if (runinfo.state != STATE_STOPPED) {
         rsprintf("<script type=\"text/javascript\">\n");
         rsprintf("function pause()\n");
         rsprintf("{\n");
         rsprintf("   flag = confirm('Are you sure to pause the run?');\n");
         rsprintf("   if (flag == true)\n");
         rsprintf("      window.location.href = '?cmd=Pause';\n");
         rsprintf("}\n");
         rsprintf("function resume()\n");
         rsprintf("{\n");
         rsprintf("   flag = confirm('Are you sure to resume the run?');\n");
         rsprintf("   if (flag == true)\n");
         rsprintf("      window.location.href = '?cmd=Resume';\n");
         rsprintf("}\n");
         if (runinfo.state == STATE_RUNNING)
            rsprintf("document.write('<input type=button %s value=Pause onClick=\"pause();\"\\n>');\n", runinfo.transition_in_progress?"disabled":"");
         if (runinfo.state == STATE_PAUSED)
            rsprintf("document.write('<input type=button %s value=Resume onClick=\"resume();\"\\n>');\n", runinfo.transition_in_progress?"disabled":"");
         rsprintf("</script>\n");
      }
   }
#endif

   if (runinfo.transition_in_progress) {
      rsprintf("<input type=button value=Cancel onClick=\"mhttpd_cancel_transition();\">\n");
   }

   /*---- time ----*/

   rsprintf("<td colspan=2>Start: %s", runinfo.start_time);

   difftime = (DWORD) (now - runinfo.start_time_binary);
   h = difftime / 3600;
   m = difftime % 3600 / 60;
   s = difftime % 60;

   if (runinfo.state == STATE_STOPPED)
      rsprintf("<td colspan=2>Stop: %s</tr>\n", runinfo.stop_time);
   else
      rsprintf("<td colspan=2>Running time: %dh%02dm%02ds</tr>\n", h, m, s);

   /*---- run info ----*/

   sprintf(ref, "Alarms/Alarm system active?cmd=set");

   size = sizeof(flag);
   db_get_value(hDB, 0, "Alarms/Alarm system active", &flag, &size, TID_BOOL, TRUE);

   {
      char str[256];
      strlcpy(str, flag ? "class=\"greenLight\"" : "class=\"redLight\"", sizeof(str));
      rsprintf("<td %s><a href=\"%s\">Alarms: %s</a>", str, ref, flag ? "On" : "Off");
   }

   sprintf(ref, "Logger/Auto restart?cmd=set");

   size = sizeof(flag);
   db_get_value(hDB, 0, "/Sequencer/State/Running", &flag, &size, TID_BOOL, FALSE);
   if (flag)
      rsprintf("<td class=\"greenLight\">Restart: Sequencer");
   else if (cm_exist("RunSubmit", FALSE) == CM_SUCCESS)
      rsprintf("<td class=\"greenLight\">Restart: RunSubmit");
   else {
     size = sizeof(flag);
     db_get_value(hDB, 0, "Logger/Auto restart", &flag, &size, TID_BOOL, TRUE);
     char str[256];
     strlcpy(str, flag ? "greenLight" : "yellowLight", sizeof(str));
     rsprintf("<td class=%s><a href=\"%s\">Restart: %s</a>", str, ref, flag ? "Yes" : "No");
   }

   if (cm_exist("Logger", FALSE) != CM_SUCCESS && cm_exist("FAL", FALSE) != CM_SUCCESS)
      rsprintf("<td colspan=2 class=\"redLight\">Logger not running</tr>\n");
   else {
      /* write data flag */
      size = sizeof(flag);
      db_get_value(hDB, 0, "/Logger/Write data", &flag, &size, TID_BOOL, TRUE);

      if (!flag)
         rsprintf("<td colspan=2 class=\"yellowLight\">Logging disabled</tr>\n");
      else {
         std::string data_dir;
         db_get_value_string(hDB, 0, "/Logger/Data dir", 0, &data_dir, TRUE);
         rsprintf("<td colspan=2>Data dir: %s</tr>\n", data_dir.c_str());
      }
   }

   /*---- if no status items present, create one to run comment ----*/

   if (db_find_key(hDB, 0, "/Experiment/Status items", &hkey) != DB_SUCCESS)
      db_create_link(hDB, 0, "/Experiment/Status items/Experiment Name", "/Experiment/Name");

   /*---- Status items ----*/

   n_items = 0;
   if (db_find_key(hDB, 0, "/Experiment/Status items", &hkey) == DB_SUCCESS) {
      for (i = 0;; i++) {
         db_enum_link(hDB, hkey, i, &hsubkey);
         if (!hsubkey)
            break;

         if (n_items++ == 0)
            rsprintf("<tr><td colspan=6><table class=\"genericStripe\" width=100%%>\n");

         db_get_key(hDB, hsubkey, &key);
         rsprintf("<tr><td style=\"text-align:left;\" width=30%% class=\"titleCell\">%s:</td>", key.name);

         db_enum_key(hDB, hkey, i, &hsubkey);
         db_get_key(hDB, hsubkey, &key);
         size = sizeof(status_data);
         if (db_get_data(hDB, hsubkey, status_data, &size, key.type) == DB_SUCCESS) {
            char str[MAX_STRING_LENGTH]; // for db_sprintf()
            db_sprintf(str, status_data, key.item_size, 0, key.type);
            rsprintf("<td style=\"text-align:left;\">%s</td></tr>\n", str);
         }
      }
      if (n_items)
         rsprintf("</table></td></tr>\n");
   }

   /*---- Messages ----*/

   time(&now);
   if (now < lastChatMsg.last_time + 60) {
      rsprintf("<tr><td colspan=6 class=\"msgService\">");

      /*
      if (strstr(lastMsg.msg, ",ERROR]") || strstr(lastMsg.msg, ",TALK]"))
         rsprintf("<span style=\"color:#EEEEEE;background-color:#c0392b\"><b>%s</b></span>",
                  lastMsg.msg);
      */
      
      // add speak JS code for chat messages
      char usr[256];
      char msg[256];
      char tim[256];
      
      strlcpy(tim, ctime(&lastChatMsg.last_time)+11, sizeof(tim));
      tim[8] = 0;
      if (strchr(lastChatMsg.msg, '[')) {
         strlcpy(usr, strchr(lastChatMsg.msg, '[')+1, sizeof(usr));
         if (strchr(usr, ','))
            *strchr(usr, ',') = 0;
         if (strchr(lastChatMsg.msg, ']')) {
            strlcpy(msg, strchr(lastChatMsg.msg, ']')+2, sizeof(msg));
            rsprintf("<span class=\"chatBubbleFooter\">");
            rsprintf("<a href=\"?cmd=Chat\">%s %s:%s</a>\n",
                     tim, usr, msg);
            rsprintf("</span>\n");
         }
      }
      rsprintf("</tr>");
   }

   if (now < lastTalkMsg.last_time + 60) {
      rsprintf("<tr><td colspan=6 class=\"msgServiceTalk\">");
      
      char usr[256];
      char msg[256];
      char tim[256];
      
      strlcpy(tim, ctime(&lastTalkMsg.last_time)+11, sizeof(tim));
      tim[8] = 0;
      if (strchr(lastTalkMsg.msg, '[')) {
         strlcpy(usr, strchr(lastTalkMsg.msg, '[')+1, sizeof(usr));
         if (strchr(usr, ','))
            *strchr(usr, ',') = 0;
         if (strchr(lastTalkMsg.msg, ']')) {
            strlcpy(msg, strchr(lastTalkMsg.msg, ']')+2, sizeof(msg));
            rsprintf("%s %s:%s\n", tim, usr, msg);
         }
      }
      rsprintf("</tr>");
   }

   if (now < lastMsg.last_time + 600) {
      if (strstr(lastMsg.msg, ",ERROR]") != NULL)
         rsprintf("<tr><td colspan=6 class=\"msgServiceErr\">");
      else
         rsprintf("<tr><td colspan=6 class=\"msgService\">");
      rsprintf("%s\n", lastMsg.msg);
      rsprintf("</tr>");
   }

   rsprintf("</table></td></tr>\n");  //end summary table

   /*---- Equipment list ----*/

   /* count hidden equipments */
   n_hidden = 0;
#ifdef USE_HIDDEN_EQ
   if (db_find_key(hDB, 0, "/equipment", &hkey) == DB_SUCCESS) {
      for (i = 0 ;; i++) {
         db_enum_key(hDB, hkey, i, &hsubkey);
         if (!hsubkey)
            break;

         db_get_key(hDB, hsubkey, &key);
         db_find_key(hDB, hsubkey, "Common", &hkeytmp);
         if (hkeytmp) {
            BOOL hidden = false;
            size = sizeof(hidden);
            db_get_value(hDB, hkeytmp, "hidden", &hidden, &size, TID_BOOL, FALSE);
            if (hidden)
               n_hidden++;
         }
      }
   }
#endif

   rsprintf("<tr><td colspan=6><table class=\"subStatusTable\" id=\"stripeList\" width=100%%>\n");
   rsprintf("<tr><th colspan=6 class=\"subStatusTitle\">Equipment</th><tr>\n");

   rsprintf("<tr class=\"titleRow\"><th>Equipment");
   if (n_hidden) {
      if (expand_equipment)
         rsprintf("&nbsp;<a href=\"?expand=0\">-</a>");
      else
         rsprintf("&nbsp;<a href=\"?expand=1\">+</a>");
   }
   rsprintf("<th>Status<th>Events");
   rsprintf("<th>Events[/s]<th>Data[MB/s]\n");

   if (db_find_key(hDB, 0, "/equipment", &hkey) == DB_SUCCESS) {
      for (i = 0;; i++) {
         db_enum_key(hDB, hkey, i, &hsubkey);
         if (!hsubkey)
            break;

         db_get_key(hDB, hsubkey, &key);

         db_find_key(hDB, hsubkey, "Common", &hkeytmp);

         if (!hkeytmp)
            continue;

         int equipment_enabled = false;
         int equipment_hidden  = false;
         std::string equipment_frontend_name;
         std::string equipment_frontend_host;
         std::string equipment_status;
         std::string equipment_status_color;

         size = sizeof(BOOL);
         db_get_value(hDB, hkeytmp, "enabled", &equipment_enabled, &size, TID_BOOL, FALSE);
         size = sizeof(BOOL);
         db_get_value(hDB, hkeytmp, "hidden", &equipment_hidden, &size, TID_BOOL, FALSE);
         db_get_value_string(hDB, hkeytmp, "frontend name", 0, &equipment_frontend_name, FALSE);
         db_get_value_string(hDB, hkeytmp, "frontend host", 0, &equipment_frontend_host, FALSE);
         db_get_value_string(hDB, hkeytmp, "status", 0, &equipment_status, FALSE);
         db_get_value_string(hDB, hkeytmp, "status color", 0, &equipment_status_color, FALSE);

         //printf("eq [%s] enabled %d, hidden %d, fe [%s] host [%s]\n", key.name, equipment_enabled, equipment_hidden, equipment_frontend_name.c_str(), equipment_frontend_host.c_str());

         if (equipment_hidden && !expand_equipment)
            continue;

         sprintf(ref, "SC/%s", key.name);

         /* check if client running this equipment is present */
         if (cm_exist(equipment_frontend_name.c_str(), TRUE) != CM_SUCCESS
             && cm_exist("FAL", TRUE) != CM_SUCCESS)
            rsprintf("<tr><td><a href=\"%s\">%s</a><td align=center class=\"redLight\">Frontend stopped",
                 ref, key.name);
         else {
            if (equipment_enabled) {
               if (equipment_status.length() < 1)
                  rsprintf("<tr><td><a href=\"%s\">%s</a><td align=center class=\"greenLight\">%s@%s", ref, key.name,
                           equipment_frontend_name.c_str(), equipment_frontend_host.c_str());
               else {
                  if (stristr(equipment_status_color.c_str(), "Light"))
                     rsprintf("<tr><td><a href=\"%s\">%s</a><td align=center class=\"%s\">%s", ref, key.name,
                              equipment_status_color.c_str(), equipment_status.c_str());
                  else
                     rsprintf("<tr><td><a href=\"%s\">%s</a><td align=center class=\"Light\" style=\"background-color:%s\">%s",
                              ref, key.name, equipment_status_color.c_str(), equipment_status.c_str());
               }
            } else
               rsprintf("<tr><td><a href=\"%s\">%s</a><td align=center class=\"yellowLight\">Disabled", ref, key.name);
         }

         char str[256];

         /* event statistics */
         double equipment_stats_events_sent = 0;
         double equipment_stats_events_per_sec = 0;
         double equipment_stats_kbytes_per_sec = 0;

         size = sizeof(double);
         db_get_value(hDB, hsubkey, "Statistics/events sent", &equipment_stats_events_sent, &size, TID_DOUBLE, FALSE);
         size = sizeof(double);
         db_get_value(hDB, hsubkey, "Statistics/events per sec.", &equipment_stats_events_per_sec, &size, TID_DOUBLE, FALSE);
         size = sizeof(double);
         db_get_value(hDB, hsubkey, "Statistics/kBytes per sec.", &equipment_stats_kbytes_per_sec, &size, TID_DOUBLE, FALSE);

         d = equipment_stats_events_sent;
         if (d > 1E9)
            sprintf(str, "%1.3lfG", d / 1E9);
         else if (d > 1E6)
            sprintf(str, "%1.3lfM", d / 1E6);
         else
            sprintf(str, "%1.0lf", d);

         rsprintf("<td align=center>%s<td align=center>%1.1lf<td align=center>%1.3lf\n",
                  str, equipment_stats_events_per_sec, equipment_stats_kbytes_per_sec/1024.0);
      }
   }

   rsprintf("</table></td></tr>\n"); //end equipment table

   /*---- Logging Table ----*/
   rsprintf("<tr><td colspan=6><table class=\"subStatusTable\" width=100%%>\n");
   rsprintf("<tr><th colspan=6 class=\"subStatusTitle\">Logging Channels</th><tr>\n");

   /*---- Logging channels ----*/

   rsprintf("<tr class=\"titleRow\"><th colspan=2>Channel<th>Events<th>MiB written<th>Compr.<th>Disk level</tr>\n");

   if (db_find_key(hDB, 0, "/Logger/Channels", &hkey) == DB_SUCCESS) {
      for (i = 0;; i++) {
         db_enum_key(hDB, hkey, i, &hsubkey);
         if (!hsubkey)
            break;

         db_get_key(hDB, hsubkey, &key);

         HNDLE hSet;
         status = db_find_key(hDB, hsubkey, "Settings", &hSet);
         if (status != DB_SUCCESS || !hSet)
            continue;

         HNDLE hStat;
         status = db_find_key(hDB, hsubkey, "Statistics", &hStat);
         if (status != DB_SUCCESS || !hStat)
            continue;

         // read channel settings

         std::string chn_current_filename;
         status = db_get_value_string(hDB, hSet, "current filename", 0, &chn_current_filename, FALSE);
         if (status != DB_SUCCESS)
            continue;

         std::string chn_type;
         status = db_get_value_string(hDB, hSet, "type", 0, &chn_type, FALSE);
         if (status != DB_SUCCESS)
            continue;

         BOOL chn_active = 0;
         size = sizeof(chn_active);
         status = db_get_value(hDB, hSet, "active", &chn_active, &size, TID_BOOL, FALSE);
         if (status != DB_SUCCESS)
            continue;

         int chn_compression = 0;
         size = sizeof(chn_compression);
         status = db_get_value(hDB, hSet, "compression", &chn_compression, &size, TID_INT, FALSE);
         if (status != DB_SUCCESS)
            continue;

         //printf("current_filename [%s] type [%s] active [%d] compression [%d]\n", chn_current_filename, chn_type, chn_active, chn_compression);

         // read channel statistics

         double chn_events_written = 0;
         size = sizeof(chn_events_written);
         status = db_get_value(hDB, hStat, "events written", &chn_events_written, &size, TID_DOUBLE, FALSE);
         if (status != DB_SUCCESS)
            continue;
         
         double chn_bytes_written = 0;
         size = sizeof(chn_bytes_written);
         status = db_get_value(hDB, hStat, "bytes written", &chn_bytes_written, &size, TID_DOUBLE, FALSE);
         if (status != DB_SUCCESS)
            continue;
         
         double chn_bytes_written_uncompressed = 0;
         size = sizeof(chn_bytes_written_uncompressed);
         status = db_get_value(hDB, hStat, "bytes written uncompressed", &chn_bytes_written_uncompressed, &size, TID_DOUBLE, FALSE);
         if (status != DB_SUCCESS)
            continue;

         double chn_disk_level = 0;
         size = sizeof(chn_disk_level);
         status = db_get_value(hDB, hStat, "disk level", &chn_disk_level, &size, TID_DOUBLE, FALSE);
         if (status != DB_SUCCESS)
            continue;

         /* filename */

         std::string xfilename = chn_current_filename;

         if (equal_ustring(chn_type.c_str(), "FTP")) {
            char *token, orig[256];

            strlcpy(orig, chn_current_filename.c_str(), sizeof(orig));

            std::string str;
            str = "ftp://";
            token = strtok(orig, ", ");
            if (token) {
               str += token;
               token = strtok(NULL, ", ");
               token = strtok(NULL, ", ");
               token = strtok(NULL, ", ");
               token = strtok(NULL, ", ");
               if (token) {
                  str += "/";
                  str += token;
                  str += "/";
                  token = strtok(NULL, ", ");
                  str += token;
               }
            }

            xfilename = str;
         }

         sprintf(ref, "Logger/Channels/%s/Settings", key.name);

         if (cm_exist("Logger", FALSE) != CM_SUCCESS
             && cm_exist("FAL", FALSE) != CM_SUCCESS)
            rsprintf("<tr><td colspan=2 class=\"redLight\">");
         else if (!flag)
            rsprintf("<tr><td colspan=2 class=\"yellowLight\">");
         else if (chn_active)
            rsprintf("<tr><td colspan=2 class=\"greenLight\">");
         else
            rsprintf("<tr><td colspan=2 class=\"yellowLight\">");

         rsprintf("<B><a href=\"%s\">#%s:</a></B> %s", ref, key.name, xfilename.c_str());

         /* statistics */

         rsprintf("<td align=center>%1.0lf</td>\n", chn_events_written);
         rsprintf("<td align=center>%1.3lf</td>\n", chn_bytes_written / 1024 / 1024);
         
         if (chn_compression > 0) {
            double compression_ratio;
            if (chn_bytes_written_uncompressed > 0)
               compression_ratio = 1 - chn_bytes_written / chn_bytes_written_uncompressed;
            else
               compression_ratio = 0;
            rsprintf("<td align=center>%4.1lf%%</td>", compression_ratio * 100);
         } else {
            rsprintf("<td align=center>N/A</td>");
         }

         char col[80];
         if (chn_disk_level >= 0.9)
            strcpy(col, "#c0392b");
         else if (chn_disk_level >= 0.7)
            strcpy(col, "#f1c40f");
         else
            strcpy(col, "#00E600");

         rsprintf("<td class=\"meterCell\">\n");
         rsprintf("<div style=\"display:block; width:90%%; height:100%%; position:relative; border:1px solid black;\">");  //wrapper to fill table cell
         rsprintf("<div style=\"background-color:%s;width:%d%%;height:100%%; position:relative; display:inline-block; padding-top:2px;\">&nbsp;%1.1lf&nbsp;%%</div>\n", col, (int)(chn_disk_level*100), chn_disk_level*100);
         rsprintf("</td>\n");
         rsprintf("</tr>\n");
      }
   }
   
   /*---- Lazy Logger ----*/

   if (db_find_key(hDB, 0, "/Lazy", &hkey) == DB_SUCCESS) {
      status = db_find_key(hDB, 0, "System/Clients", &hkey);
      if (status != DB_SUCCESS)
         return;

      k = 0;
      previous_mode = -1;
      /* loop over all clients */
      for (j = 0;; j++) {
         status = db_enum_key(hDB, hkey, j, &hsubkey);
         if (status == DB_NO_MORE_SUBKEYS)
            break;

         if (status == DB_SUCCESS) {
            /* get client name */
            size = sizeof(client_name);
            db_get_value(hDB, hsubkey, "Name", client_name, &size, TID_STRING, TRUE);
            client_name[4] = 0; /* search only for the 4 first char */
            if (equal_ustring(client_name, "Lazy")) {
               char str[MAX_ODB_PATH];
               sprintf(str, "/Lazy/%s", &client_name[5]);
               status = db_find_key(hDB, 0, str, &hLKey);
               if (status == DB_SUCCESS) {
                  size = sizeof(str);
                  db_get_value(hDB, hLKey, "Settings/Backup Type", str, &size, TID_STRING, TRUE);
                  ftp_mode = equal_ustring(str, "FTP");

                  if (previous_mode != ftp_mode)
                     k = 0;
                  if (k == 0) {
                     if (ftp_mode)
                        rsprintf
                            ("<tr style=\"font-weight:bold;\" class=\"titleRow\"><th colspan=2>Lazy Destination<th>Progress<th>File Name<th>Speed [MB/s]<th>Total</tr>\n");
                     else
                        rsprintf
                            ("<tr style=\"font-weight:bold;\" class=\"titleRow\"><th colspan=2>Lazy Label<th>Progress<th>File Name<th># Files<th>Total</tr>\n");
                  }
                  previous_mode = ftp_mode;
                  if (ftp_mode) {
                     size = sizeof(str);
                     db_get_value(hDB, hLKey, "Settings/Path", str, &size, TID_STRING, TRUE);
                     if (strchr(str, ','))
                        *strchr(str, ',') = 0;
                  } else {
                     size = sizeof(str);
                     db_get_value(hDB, hLKey, "Settings/List Label", str, &size, TID_STRING, TRUE);
                     if (str[0] == 0)
                        strcpy(str, "(empty)");
                  }

                  sprintf(ref, "Lazy/%s/Settings", &client_name[5]);

                  rsprintf("<tr><td colspan=2><B><a href=\"%s\">%s</a></B>", ref, str);

                  size = sizeof(value);
                  db_get_value(hDB, hLKey, "Statistics/Copy progress (%)", &value, &size, TID_DOUBLE, TRUE);
                  rsprintf("<td align=center>%1.0f %%", value);

                  size = sizeof(str);
                  db_get_value(hDB, hLKey, "Statistics/Backup File", str, &size, TID_STRING, TRUE);
                  rsprintf("<td align=center>%s", str);

                  if (ftp_mode) {
                     size = sizeof(value);
                     db_get_value(hDB, hLKey, "Statistics/Copy Rate (Bytes per s)", &value, &size, TID_DOUBLE, TRUE);
                     rsprintf("<td align=center>%1.1f", value / 1024.0 / 1024.0);
                  } else {
                     size = sizeof(i);
                     db_get_value(hDB, hLKey, "/Statistics/Number of files", &i, &size, TID_INT, TRUE);
                     rsprintf("<td align=center>%d", i);
                  }

                  size = sizeof(value);
                  db_get_value(hDB, hLKey, "Statistics/Backup status (%)", &value, &size, TID_DOUBLE, TRUE);
                  rsprintf("<td align=center>%1.1f %%", value);
                  k++;
               }
            }
         }
      }

      rsprintf("</tr>\n");
   }

   rsprintf("</table></td></tr>\n"); //end logging table

   /*---- Clients ----*/

   if (db_find_key(hDB, 0, "/System/Clients", &hkey) == DB_SUCCESS) {

      /*---- Client Table ----*/
      rsprintf("<tr><td colspan=6><table class=\"subStatusTable\" id=\"clientsTable\" width=100%%>\n");
      rsprintf("<tr><th colspan=6 class=\"subStatusTitle\">Clients</th><tr>\n");

      for (i = 0;; i++) {
         db_enum_key(hDB, hkey, i, &hsubkey);
         if (!hsubkey)
            break;

         if (i % 3 == 0)
            rsprintf("<tr>");

         std::string name;
         db_get_value_string(hDB, hsubkey, "Name", 0, &name, TRUE);
         std::string host;
         db_get_value_string(hDB, hsubkey, "Host", 0, &host, TRUE);

         rsprintf("<td colspan=2 align=center>%s [%s]", name.c_str(), host.c_str());

         if (i % 3 == 2)
            rsprintf("</tr>\n");
      }

      if (i % 3 != 0)
         rsprintf("</tr>\n");

      rsprintf("</table></td></tr>\n"); //end client table
   }

   rsprintf("</table>\n");

   page_footer(TRUE);

}

/*------------------------------------------------------------------*/

void show_messages_page()
{
   int status;
   char bclass[256], facility[256];
   time_t now;
   HNDLE hDB;

   cm_get_experiment_database(&hDB, NULL);

   std::string exptname;
   db_get_value_string(hDB, 0, "/Experiment/Name", 0, &exptname, TRUE);
   time(&now);

   show_header("Messages", "GET", "./", 0);
   rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"obsolete.js\"></script>\n");
   show_navigation_bar("Messages");

   /*---- facilities button bar ----*/

   if (getparam("facility") && *getparam("facility"))
      strlcpy(facility, getparam("facility"), sizeof(facility));
   else
      strlcpy(facility, "midas", sizeof(facility));

   STRING_LIST list;
   status = cm_msg_facilities(&list);
   
   if ((status == CM_SUCCESS) && (list.size() > 0)) {
      rsprintf("<table class=\"navigationTable\"><tr><td>\n");
      for (unsigned i=0 ; i<list.size() ; i++) {
         char str[1024];
         strlcpy(str, list[i].c_str(), sizeof(str));
         if (equal_ustring(str, facility))
            strlcpy(bclass, "navButtonSel", sizeof(bclass));
         else
            strlcpy(bclass, "navButton", sizeof(bclass));
         rsprintf("<input type=\"button\" name=\"facility\" value=\"%s\" class=\"%s\" ", str, bclass);
         rsprintf("onclick=\"window.location.href='./?cmd=Messages&facility=%s';return false;\">\n", str);
      }
      rsprintf("</td></tr></table>\n");
   }

   /*---- messages will be dynamically loaded via JS ----*/

   rsprintf("<div class=\"messageBox\" id=\"messageFrame\">\n");
   rsprintf("<h1 class=\"subStatusTitle\">Messages</h1>");
   rsprintf("</div>\n");
   
   rsprintf("<script type=\"text/javascript\">msg_load('%s');</script>\n", facility);

   rsprintf("</form>\n");
   rsprintf("</body></html>\n");
}

/*------------------------------------------------------------------*/

void show_chat_page()
{
   show_header("Chat", "GET", "./", 0);
   rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"obsolete.js\"></script>\n");
   show_navigation_bar("Chat");
   
   /*---- messages will be dynamically loaded via JS ----*/

   rsprintf("<div class=\"chatInput\" id=\"chatInput\">\n");
   rsprintf("  <table width=\"100%%\" border=\"0\"><tr>\n");
   rsprintf("    <td><input style=\"width:100%%\" type=\"text\" id=\"text\" autofocus=\"autofocus\" onkeypress=\"return chat_kp(event)\"></td>\n");
   rsprintf("    <td nowrap width=\"10%%\"><input type=\"button\" name=\"send\" value=\"Send\" onClick=\"chat_send()\">");
   rsprintf("&nbsp;&nbsp;Your name: <input type=\"text\" id=\"name\" size=\"10\" onkeypress=\"return chat_kp(event)\">\n");
   rsprintf("    <input type=\"checkbox\" name=\"speak\" id=\"speak\" onClick=\"return speak_click(this);\"><span id=\"speakLabel\">Audio</span></td>");
   rsprintf("  </tr></table>");
   rsprintf("</div>\n");
   
   rsprintf("<div class=\"chatBox\" id=\"messageFrame\">\n");
   rsprintf("<h1 class=\"chatTitle\">Chat messages</h1>");
   rsprintf("</div>\n");
   
   rsprintf("<script type=\"text/javascript\">chat_load();</script>\n");
   
   rsprintf("</form>\n");
   rsprintf("</body></html>\n");
}

/*------------------------------------------------------------------*/

void strencode(char *text)
{
   int i;

   for (i = 0; i < (int) strlen(text); i++) {
      switch (text[i]) {
      case '\n':
         rsprintf("<br>\n");
         break;
      case '<':
         rsprintf("&lt;");
         break;
      case '>':
         rsprintf("&gt;");
         break;
      case '&':
         rsprintf("&amp;");
         break;
      case '\"':
         rsprintf("&quot;");
         break;
      default:
         rsprintf("%c", text[i]);
      }
   }
}

/*------------------------------------------------------------------*/

void strencode2(char *b, char *text)
{
   int i;

   for (i = 0; i < (int) strlen(text); b++, i++) {
      switch (text[i]) {
      case '\n':
         sprintf(b, "<br>\n");
         break;
      case '<':
         sprintf(b, "&lt;");
         break;
      case '>':
         sprintf(b, "&gt;");
         break;
      case '&':
         sprintf(b, "&amp;");
         break;
      case '\"':
         sprintf(b, "&quot;");
         break;
      default:
         sprintf(b, "%c", text[i]);
      }
   }
   *b = 0;
}

/*------------------------------------------------------------------*/

void strencode3(char *text)
{
   int i;

   for (i = 0; i < (int) strlen(text); i++) {
      switch (text[i]) {
      case '<':
         rsprintf("&lt;");
         break;
      case '>':
         rsprintf("&gt;");
         break;
      case '&':
         rsprintf("&amp;");
         break;
      case '\"':
         rsprintf("&quot;");
         break;
      default:
         rsprintf("%c", text[i]);
      }
   }
}

/*------------------------------------------------------------------*/

void strencode4(char *text)
{
   int i;

   for (i = 0; i < (int) strlen(text); i++) {
      switch (text[i]) {
         case '\n':
            rsprintf("<br>\n");
            break;
         case '<':
            rsprintf("&lt;");
            break;
         case '>':
            rsprintf("&gt;");
            break;
         case '&':
            rsprintf("&amp;");
            break;
         case '\"':
            rsprintf("&quot;");
            break;
         case ' ':
            rsprintf("&nbsp;");
            break;
         default:
            rsprintf("%c", text[i]);
      }
   }
}

/*------------------------------------------------------------------*/

void show_elog_new(const char *path, BOOL bedit, const char *odb_att, const char *action_path)
{
   int i, j, size, run_number, wrap, status;
   char str[256], ref[256], *p;
   char date[80], author[80], type[80], system[80], subject[256], text[10000],
       orig_tag[80], reply_tag[80], att1[256], att2[256], att3[256], encoding[80];
   time_t now;
   HNDLE hDB, hkey, hsubkey;
   BOOL display_run_number;
   KEY key;

   //printf("show_elog_new, path [%s], action_path [%s], att [%s]\n", path, action_path, odb_att);

   if (!action_path)
     action_path = "./";

   cm_get_experiment_database(&hDB, NULL);
   display_run_number = TRUE;
   size = sizeof(BOOL);
   db_get_value(hDB, 0, "/Elog/Display run number", &display_run_number, &size, TID_BOOL, TRUE);

   /* get message for reply */
   type[0] = system[0] = 0;
   att1[0] = att2[0] = att3[0] = 0;
   subject[0] = 0;
   run_number = 0;

   if (path) {
      strlcpy(str, path, sizeof(str));
      size = sizeof(text);
      el_retrieve(str, date, &run_number, author, type, system, subject,
                  text, &size, orig_tag, reply_tag, att1, att2, att3, encoding);
   }

   if (run_number < 0) {
      cm_msg(MERROR, "show_elog_new", "aborting on attempt to use invalid run number %d", run_number);
      abort();
   }

   /* header */
   rsprintf("HTTP/1.1 200 Document follows\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
   rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

   rsprintf("<html><head>\n");
   rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
   rsprintf("<link rel=\"stylesheet\" href=\"%s\" type=\"text/css\" />\n", get_css_filename());
   rsprintf("<title>MIDAS ELog</title></head>\n");
   rsprintf
       ("<body><form method=\"POST\" action=\"%s\" enctype=\"multipart/form-data\">\n", action_path);

   /*---- body needs wrapper div to pin footer ----*/
   rsprintf("<div class=\"wrapper\">\n");
   /*---- begin page header ----*/
   rsprintf("<table class=\"headerTable\">\n");

   /*---- title row ----*/

   //size = sizeof(str);
   //str[0] = 0;
   //db_get_value(hDB, 0, "/Experiment/Name", str, &size, TID_STRING, TRUE);
   rsprintf("<tr><td></td></tr>\n");
/*
   rsprintf("<tr><th>MIDAS Electronic Logbook");
   if (elog_mode)
      rsprintf("<th>Logbook \"%s\"</tr>\n", str);
   else
      rsprintf("<th>Experiment \"%s\"</tr>\n", str);
*/
   //end header
   rsprintf("</table>");

   //main table
   rsprintf("<table class=\"dialogTable\">");
   /*---- menu buttons ----*/

   rsprintf("<tr><td colspan=2 class=\"subStatusTitle\">Create E-Log</td></tr>");

   rsprintf("<tr><td colspan=2>\n");

   rsprintf("<input type=submit name=cmd value=Submit>\n");
   rsprintf("</tr>\n\n");

   /*---- entry form ----*/

   if (display_run_number) {
      if (bedit) {
         rsprintf("<tr><td>Entry date: %s<br>", date);
         time(&now);
         rsprintf("Revision date: %s", ctime(&now));
      } else {
         time(&now);
         rsprintf("<tr><td>Entry date: %s", ctime(&now));
      }

      if (!bedit) {
         run_number = 0;
         size = sizeof(run_number);
         status = db_get_value(hDB, 0, "/Runinfo/Run number", &run_number, &size, TID_INT, TRUE);
         assert(status == SUCCESS);
      }

      if (run_number < 0) {
         cm_msg(MERROR, "show_elog_new", "aborting on attempt to use invalid run number %d", run_number);
         abort();
      }

      rsprintf("<td>Run number: ");
      rsprintf("<input type=\"text\" size=10 maxlength=10 name=\"run\" value=\"%d\"</tr>", run_number);
   } else {
      if (bedit) {
         rsprintf("<tr><td colspan=2>Entry date: %s<br>", date);
         time(&now);
         rsprintf("Revision date: %s", ctime(&now));
      } else {
         time(&now);
         rsprintf("<tr><td colspan=2>Entry date: %s", ctime(&now));
      }
   }

   if (bedit) {
      strlcpy(str, author, sizeof(str));
      if (strchr(str, '@'))
         *strchr(str, '@') = 0;
   } else
      str[0] = 0;

   rsprintf
       ("<tr><td>Author: <input type=\"text\" size=\"15\" maxlength=\"80\" name=\"Author\" value=\"%s\">\n",
        str);

   /* get type list from ODB */
   size = 20 * NAME_LENGTH;
   if (db_find_key(hDB, 0, "/Elog/Types", &hkey) != DB_SUCCESS)
      db_set_value(hDB, 0, "/Elog/Types", type_list, NAME_LENGTH * 20, 20, TID_STRING);
   db_find_key(hDB, 0, "/Elog/Types", &hkey);
   if (hkey)
      db_get_data(hDB, hkey, type_list, &size, TID_STRING);

   /* add types from forms */
   for (j = 0; j < 20 && type_list[j][0]; j++);
   db_find_key(hDB, 0, "/Elog/Forms", &hkey);
   if (hkey)
      for (i = 0; j < 20; i++) {
         db_enum_link(hDB, hkey, i, &hsubkey);
         if (!hsubkey)
            break;

         db_get_key(hDB, hsubkey, &key);
         strlcpy(type_list[j++], key.name, NAME_LENGTH);
      }

   /* get system list from ODB */
   size = 20 * NAME_LENGTH;
   if (db_find_key(hDB, 0, "/Elog/Systems", &hkey) != DB_SUCCESS)
      db_set_value(hDB, 0, "/Elog/Systems", system_list, NAME_LENGTH * 20, 20,
                   TID_STRING);
   db_find_key(hDB, 0, "/Elog/Systems", &hkey);
   if (hkey)
      db_get_data(hDB, hkey, system_list, &size, TID_STRING);

   sprintf(ref, "/ELog/");

   rsprintf
       ("<td><a href=\"%s\" target=\"_blank\">Type:</a> <select name=\"type\">\n",
        ref);
   for (i = 0; i < 20 && type_list[i][0]; i++)
      if ((path && !bedit && equal_ustring(type_list[i], "reply")) ||
          (bedit && equal_ustring(type_list[i], type)))
         rsprintf("<option selected value=\"%s\">%s\n", type_list[i], type_list[i]);
      else
         rsprintf("<option value=\"%s\">%s\n", type_list[i], type_list[i]);
   rsprintf("</select></tr>\n");

   rsprintf
       ("<tr><td><a href=\"%s\" target=\"_blank\">  System:</a> <select name=\"system\">\n",
        ref);
   for (i = 0; i < 20 && system_list[i][0]; i++)
      if (path && equal_ustring(system_list[i], system))
         rsprintf("<option selected value=\"%s\">%s\n", system_list[i], system_list[i]);
      else
         rsprintf("<option value=\"%s\">%s\n", system_list[i], system_list[i]);
   rsprintf("</select>\n");

   str[0] = 0;
   if (path && !bedit)
      sprintf(str, "Re: %s", subject);
   else
      sprintf(str, "%s", subject);
   rsprintf
       ("<td>Subject: <input type=text size=20 maxlength=\"80\" name=Subject value=\"%s\"></tr>\n",
        str);

   if (path) {
      /* hidden text for original message */
      rsprintf("<input type=hidden name=orig value=\"%s\">\n", path);

      if (bedit)
         rsprintf("<input type=hidden name=edit value=1>\n");
   }

   /* increased wrapping for replys (leave space for '> ' */
   wrap = (path && !bedit) ? 78 : 76;

   rsprintf("<tr><td colspan=2>Text:<br>\n");
   rsprintf("<textarea rows=10 cols=%d wrap=hard name=Text>", wrap);

   if (path) {
      if (bedit) {
         rsputs(text);
      } else {
         p = text;
         do {
            if (strchr(p, '\r')) {
               *strchr(p, '\r') = 0;
               rsprintf("> %s\n", p);
               p += strlen(p) + 1;
               if (*p == '\n')
                  p++;
            } else {
               rsprintf("> %s\n\n", p);
               break;
            }

         } while (TRUE);
      }
   }

   rsprintf("</textarea><br>\n");

   /* HTML check box */
   if (bedit && encoding[0] == 'H')
      rsprintf
          ("<input type=checkbox checked name=html value=1>Submit as HTML text</tr>\n");
   else
      rsprintf("<input type=checkbox name=html value=1>Submit as HTML text</tr>\n");

   if (bedit && att1[0])
      rsprintf
          ("<tr><td colspan=2 align=center>If no attachment are resubmitted, the original ones are kept</tr>\n");

   /* attachment */
   rsprintf
       ("<tr><td colspan=2 align=center>Enter attachment filename(s) or ODB tree(s), use \"\\\" as an ODB directory separator:</tr>");

   if (odb_att) {
      str[0] = 0;
      if (odb_att[0] != '\\' && odb_att[0] != '/')
         strlcpy(str, "\\", sizeof(str));
      strlcat(str, odb_att, sizeof(str));
      rsprintf
          ("<tr><td colspan=2>Attachment 1: <input type=hidden name=attachment0 value=\"%s\"><b>%s</b></tr>\n",
           str, str);
   } else
      rsprintf
          ("<tr><td colspan=2>Attachment 1: <input type=\"file\" size=\"60\" maxlength=\"256\" name=\"attfile1\" value=\"%s\" accept=\"filetype/*\"></tr>\n",
           att1);

   rsprintf
       ("<tr><td colspan=2>Attachment 2: <input type=\"file\" size=\"60\" maxlength=\"256\" name=\"attfile2\" value=\"%s\" accept=\"filetype/*\"></tr>\n",
        att2);
   rsprintf
       ("<tr><td colspan=2>Attachment 3: <input type=\"file\" size=\"60\" maxlength=\"256\" name=\"attfile3\" value=\"%s\" accept=\"filetype/*\"></tr>\n",
        att3);

   rsprintf("</table>\n");
   page_footer(TRUE);
}

/*------------------------------------------------------------------*/

void show_elog_query()
{
   int i, size;
   time_t now;
   struct tm *tms;
   HNDLE hDB, hkey, hkeyroot;
   KEY key;
   BOOL display_run_number;

   /* get flag for displaying run number */
   cm_get_experiment_database(&hDB, NULL);
   display_run_number = TRUE;
   size = sizeof(BOOL);
   db_get_value(hDB, 0, "/Elog/Display run number", &display_run_number, &size, TID_BOOL, TRUE);

   /* header */
   rsprintf("HTTP/1.1 200 Document follows\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
   rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

   rsprintf("<html><head>\n");
   rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
   rsprintf("<link rel=\"stylesheet\" href=\"%s\" type=\"text/css\" />\n", get_css_filename());
   rsprintf("<title>MIDAS ELog</title></head>\n");
   rsprintf("<body><form method=\"GET\" action=\"./\">\n");

   /*---- body needs wrapper div to pin footer ----*/
   rsprintf("<div class=\"wrapper\">\n");
   /*---- begin page header ----*/
   rsprintf("<table class=\"headerTable\">\n");

  /*---- title row ----*/

   //size = sizeof(str);
   //str[0] = 0;
   //db_get_value(hDB, 0, "/Experiment/Name", str, &size, TID_STRING, TRUE);
   rsprintf("<tr><td></td></tr>\n");
/*
   rsprintf("<tr><th colspan=2>MIDAS Electronic Logbook");
   if (elog_mode)
      rsprintf("<th colspan=2>Logbook \"%s\"</tr>\n", str);
   else
      rsprintf("<th colspan=2>Experiment \"%s\"</tr>\n", str);
*/
   //end header
   rsprintf("</table>");

   //main table
   rsprintf("<table class=\"dialogTable\">");
   rsprintf("<tr><td colspan=4 class=\"subStatusTitle\">E-Log Query</td></tr>");
  /*---- menu buttons ----*/

   rsprintf("<tr><td colspan=4>\n");

   rsprintf("<input type=submit name=cmd value=\"Submit Query\">\n");
   rsprintf("<input type=reset value=\"Reset Form\">\n");
   rsprintf("</tr>\n\n");

  /*---- entry form ----*/

   rsprintf("<tr><td colspan=2>");
   rsprintf("<input type=checkbox name=mode value=\"summary\">Summary only\n");
   rsprintf("<td colspan=2>");
   rsprintf("<input type=checkbox name=attach value=1>Show attachments</tr>\n");

   time(&now);
   now -= 3600 * 24;
   tms = localtime(&now);
   tms->tm_year += 1900;

   rsprintf("<tr><td>Start date: ");
   rsprintf("<td colspan=3><select name=\"m1\">\n");

   for (i = 0; i < 12; i++)
      if (i == tms->tm_mon)
         rsprintf("<option selected value=\"%s\">%s\n", mname[i], mname[i]);
      else
         rsprintf("<option value=\"%s\">%s\n", mname[i], mname[i]);
   rsprintf("</select>\n");

   rsprintf("<select name=\"d1\">");
   for (i = 0; i < 31; i++)
      if (i + 1 == tms->tm_mday)
         rsprintf("<option selected value=%d>%d\n", i + 1, i + 1);
      else
         rsprintf("<option value=%d>%d\n", i + 1, i + 1);
   rsprintf("</select>\n");

   rsprintf(" <input type=\"text\" size=5 maxlength=5 name=\"y1\" value=\"%d\">",
            tms->tm_year);
   rsprintf("</tr>\n");

   rsprintf("<tr><td>End date: ");
   rsprintf("<td colspan=3><select name=\"m2\" value=\"%s\">\n",
            mname[tms->tm_mon]);

   rsprintf("<option value=\"\">\n");
   for (i = 0; i < 12; i++)
      rsprintf("<option value=\"%s\">%s\n", mname[i], mname[i]);
   rsprintf("</select>\n");

   rsprintf("<select name=\"d2\">");
   rsprintf("<option selected value=\"\">\n");
   for (i = 0; i < 31; i++)
      rsprintf("<option value=%d>%d\n", i + 1, i + 1);
   rsprintf("</select>\n");

   rsprintf(" <input type=\"text\" size=5 maxlength=5 name=\"y2\">");
   rsprintf("</tr>\n");

   if (display_run_number) {
      rsprintf("<tr><td>Start run: ");
      rsprintf("<td><input type=\"text\" size=\"10\" maxlength=\"10\" name=\"r1\">\n");
      rsprintf("<td>End run: ");
      rsprintf("<td><input type=\"text\" size=\"10\" maxlength=\"10\" name=\"r2\">\n");
      rsprintf("</tr>\n");
   }

   /* get type list from ODB */
   size = 20 * NAME_LENGTH;
   if (db_find_key(hDB, 0, "/Elog/Types", &hkey) != DB_SUCCESS)
      db_set_value(hDB, 0, "/Elog/Types", type_list, NAME_LENGTH * 20, 20, TID_STRING);
   db_find_key(hDB, 0, "/Elog/Types", &hkey);
   if (hkey)
      db_get_data(hDB, hkey, type_list, &size, TID_STRING);

   /* get system list from ODB */
   size = 20 * NAME_LENGTH;
   if (db_find_key(hDB, 0, "/Elog/Systems", &hkey) != DB_SUCCESS)
      db_set_value(hDB, 0, "/Elog/Systems", system_list, NAME_LENGTH * 20, 20,
                   TID_STRING);
   db_find_key(hDB, 0, "/Elog/Systems", &hkey);
   if (hkey)
      db_get_data(hDB, hkey, system_list, &size, TID_STRING);

   rsprintf("<tr><td colspan=2>Author: ");
   rsprintf("<input type=\"test\" size=\"15\" maxlength=\"80\" name=\"author\">\n");

   rsprintf("<td colspan=2>Type: ");
   rsprintf("<select name=\"type\">\n");
   rsprintf("<option value=\"\">\n");
   for (i = 0; i < 20 && type_list[i][0]; i++)
      rsprintf("<option value=\"%s\">%s\n", type_list[i], type_list[i]);
   /* add forms as types */
   db_find_key(hDB, 0, "/Elog/Forms", &hkeyroot);
   if (hkeyroot)
      for (i = 0;; i++) {
         db_enum_link(hDB, hkeyroot, i, &hkey);
         if (!hkey)
            break;
         db_get_key(hDB, hkey, &key);
         rsprintf("<option value=\"%s\">%s\n", key.name, key.name);
      }
   rsprintf("</select></tr>\n");

   rsprintf("<tr><td colspan=2>System: ");
   rsprintf("<select name=\"system\">\n");
   rsprintf("<option value=\"\">\n");
   for (i = 0; i < 20 && system_list[i][0]; i++)
      rsprintf("<option value=\"%s\">%s\n", system_list[i], system_list[i]);
   rsprintf("</select>\n");

   rsprintf("<td colspan=2>Subject: ");
   rsprintf("<input type=\"text\" size=\"15\" maxlength=\"80\" name=\"subject\"></tr>\n");

   rsprintf("<tr><td colspan=4>Text: ");
   rsprintf("<input type=\"text\" size=\"15\" maxlength=\"80\" name=\"subtext\">\n");
   rsprintf("<i>(case insensitive substring)</i><tr>\n");

   rsprintf("</tr></table>\n");
   page_footer(TRUE);
}

/*------------------------------------------------------------------*/

void show_elog_delete(const char *path)
{
   HNDLE hDB;
   int size, status;
   char str[256];
   BOOL allow_delete;

   /* get flag for allowing delete */
   cm_get_experiment_database(&hDB, NULL);
   allow_delete = FALSE;
   size = sizeof(BOOL);
   db_get_value(hDB, 0, "/Elog/Allow delete", &allow_delete, &size, TID_BOOL, TRUE);

   /* redirect if confirm = NO */
   if (getparam("confirm") && *getparam("confirm")
       && strcmp(getparam("confirm"), "No") == 0) {
      sprintf(str, "../EL/%s", path);
      redirect(str);
      return;
   }

   /* header */
   sprintf(str, "../EL/%s", path);
   show_header("Delete ELog entry", "GET", str, 0);
   rsprintf("</table>"); //end header

   rsprintf("<table class=\"dialogTable\">"); //main table

   if (!allow_delete) {
      rsprintf
          ("<tr><td colspan=2 class=\"redLight\" align=center><h1>Message deletion disabled in ODB</h1>\n");
   } else {
      if (getparam("confirm") && *getparam("confirm")) {
         if (strcmp(getparam("confirm"), "Yes") == 0) {
            /* delete message */
            status = el_delete_message(path);
            rsprintf("<tr><td colspan=2 class=\"greenLight\" align=center>");
            if (status == EL_SUCCESS)
               rsprintf("<b>Message successfully deleted</b></tr>\n");
            else
               rsprintf("<b>Error deleting message: status = %d</b></tr>\n", status);

            rsprintf("<input type=hidden name=cmd value=last>\n");
            rsprintf
                ("<tr><td colspan=2 align=center><input type=submit value=\"Goto last message\"></tr>\n");
         }
      } else {
         /* define hidden field for command */
         rsprintf("<input type=hidden name=cmd value=delete>\n");

         rsprintf("<tr><td colspan=2 class=\"redLight\" align=center>");
         rsprintf("<b>Are you sure to delete this message?</b></tr>\n");

         rsprintf("<tr><td align=center><input type=submit name=confirm value=Yes>\n");
         rsprintf("<td align=center><input type=submit name=confirm value=No>\n");
         rsprintf("</tr>\n\n");
      }
   }

   rsprintf("</table>\n");
   page_footer(TRUE);
}

/*------------------------------------------------------------------*/

void show_elog_submit_query(INT last_n)
{
   int i, size, run, status, m1, d2, m2, y2, index, colspan;
   char date[80], author[80], type[80], system[80], subject[256], text[10000],
       orig_tag[80], reply_tag[80], attachment[3][256], encoding[80];
   char str[256], str2[10000], tag[256], ref[256], *pc;
   HNDLE hDB;
   BOOL full, show_attachments, display_run_number;
   time_t ltime_start, ltime_end, ltime_current, now;
   struct tm tms, *ptms;
   FILE *f;

   /* get flag for displaying run number */
   cm_get_experiment_database(&hDB, NULL);
   display_run_number = TRUE;
   size = sizeof(BOOL);
   db_get_value(hDB, 0, "/Elog/Display run number", &display_run_number, &size, TID_BOOL, TRUE);

#if 0
   /* header */
   rsprintf("HTTP/1.1 200 Document follows\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
   rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

   rsprintf("<html><head>\n");
   rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
   rsprintf("<link rel=\"stylesheet\" href=\"%s\" type=\"text/css\" />\n", get_css_filename());
   rsprintf("<title>MIDAS ELog</title></head>\n");
   rsprintf("<body><form method=\"GET\" action=\"./\">\n");
#endif

   show_header("ELog", "GET", "./", 0);
   rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   show_navigation_bar("ELog");

   /*---- body needs wrapper div to pin footer ----*/
   rsprintf("<div class=\"wrapper\">\n");
   /*---- begin page header ----*/
   rsprintf("<table class=\"headerTable\">\n");

   /* get mode */
   if (last_n) {
      full = TRUE;
      show_attachments = FALSE;
   } else {
      full = !(*getparam("mode"));
      show_attachments = (*getparam("attach") > 0);
   }

   /*---- title row ----*/

   //size = sizeof(str);
   //str[0] = 0;
   //db_get_value(hDB, 0, "/Experiment/Name", str, &size, TID_STRING, TRUE);

   colspan = full ? 3 : 4;
   if (!display_run_number)
      colspan--;
   rsprintf("<tr><td></td></tr>\n");
/*
   rsprintf("<tr><th colspan=3>MIDAS Electronic Logbook");
   if (elog_mode)
      rsprintf("<th colspan=%d>Logbook \"%s\"</tr>\n", colspan, str);
   else
      rsprintf("<th colspan=%d>Experiment \"%s\"</tr>\n", colspan, str);
*/
   /*---- menu buttons ----*/

   if (!full) {
      colspan = display_run_number ? 7 : 6;
      rsprintf("<tr><td colspan=%d>\n", colspan);

      rsprintf("<input type=submit name=cmd value=\"Query\">\n");
      rsprintf("<input type=submit name=cmd value=\"ELog\">\n");
      rsprintf("<input type=submit name=cmd value=\"Status\">\n");
      rsprintf("</tr>\n\n");
   }

   /*---- convert end date to ltime ----*/

   ltime_end = ltime_start = 0;
   m1 = m2 = d2 = y2 = 0;

   if (!last_n) {
      strlcpy(str, getparam("m1"), sizeof(str));
      for (m1 = 0; m1 < 12; m1++)
         if (equal_ustring(str, mname[m1]))
            break;
      if (m1 == 12)
         m1 = 0;

      if (*getparam("m2") || *getparam("y2") || *getparam("d2")) {
         if (*getparam("m2")) {
            strlcpy(str, getparam("m2"), sizeof(str));
            for (m2 = 0; m2 < 12; m2++)
               if (equal_ustring(str, mname[m2]))
                  break;
            if (m2 == 12)
               m2 = 0;
         } else
            m2 = m1;

         if (*getparam("y2"))
            y2 = atoi(getparam("y2"));
         else
            y2 = atoi(getparam("y1"));

         if (*getparam("d2"))
            d2 = atoi(getparam("d2"));
         else
            d2 = atoi(getparam("d1"));

         memset(&tms, 0, sizeof(struct tm));
         tms.tm_year = y2 % 100;
         tms.tm_mon = m2;
         tms.tm_mday = d2;
         tms.tm_hour = 24;

         if (tms.tm_year < 90)
            tms.tm_year += 100;
         ltime_end = mktime(&tms);
      }
   }

  /*---- title row ----*/

   colspan = full ? 6 : 7;
   if (!display_run_number)
      colspan--;

#if 0
   /* menu buttons */
   rsprintf("<tr><td colspan=%d>\n", colspan);
   rsprintf("<input type=submit name=cmd value=Query>\n");
   rsprintf("<input type=submit name=cmd value=Last>\n");
   if (!elog_mode)
      rsprintf("<input type=submit name=cmd value=Status>\n");
   rsprintf("</tr>\n");
#endif

   rsprintf("</table>");  //end header

   rsprintf("<table id=\"elogContent\" class=\"dialogTable\">");  //main table
   rsprintf("<tr><th class=\"subStatusTitle\" colspan=6>E-Log</th><tr>");

   if (*getparam("r1")) {
      if (*getparam("r2"))
         rsprintf("<tr><td colspan=%d class=\"yellowLight\"><b>Query result between runs %s and %s</b></tr>\n", colspan, getparam("r1"), getparam("r2"));
      else
         rsprintf("<tr><td colspan=%d class=\"yellowLight\"><b>Query result between run %s and today</b></tr>\n", colspan, getparam("r1"));
   } else {
      if (last_n) {
         if (last_n < 24) {
            rsprintf("<tr><td colspan=6><a href=\"last%d\">Last %d hours</a></tr>\n",
                        last_n * 2, last_n * 2);

            rsprintf("<tr><td colspan=6 class=\"yellowLight\"><b>Last %d hours</b></tr>\n",
                     last_n);
         } else {
            rsprintf("<tr><td colspan=6><a href=\"last%d\">Last %d days</a></tr>\n",
                        last_n * 2, last_n / 24 * 2);

            rsprintf("<tr><td colspan=6 class=\"yellowLight\"><b>Last %d days</b></tr>\n",
                     last_n / 24);
         }
      }

      else if (*getparam("m2") || *getparam("y2") || *getparam("d2"))
         rsprintf
             ("<tr><td colspan=%d class=\"yellowLight\"><b>Query result between %s %s %s and %s %d %d</b></tr>\n",
              colspan, getparam("m1"), getparam("d1"), getparam("y1"), mname[m2], d2, y2);
      else {
         time(&now);
         ptms = localtime(&now);
         ptms->tm_year += 1900;

         rsprintf
             ("<tr><td colspan=%d class=\"yellowLight\"><b>Query result between %s %s %s and %s %d %d</b></tr>\n",
              colspan, getparam("m1"), getparam("d1"), getparam("y1"),
              mname[ptms->tm_mon], ptms->tm_mday, ptms->tm_year);
      }
   }

   rsprintf("</tr>\n<tr class=\"titleRow\">");

   //rsprintf("<td colspan=%d bgcolor=#FFA0A0>\n", colspan);

   if (*getparam("author"))
      rsprintf("Author: <b>%s</b>   ", getparam("author"));

   if (*getparam("type"))
      rsprintf("Type: <b>%s</b>   ", getparam("type"));

   if (*getparam("system"))
      rsprintf("System: <b>%s</b>   ", getparam("system"));

   if (*getparam("subject"))
      rsprintf("Subject: <b>%s</b>   ", getparam("subject"));

   if (*getparam("subtext"))
      rsprintf("Text: <b>%s</b>   ", getparam("subtext"));

   rsprintf("</tr>\n");

  /*---- table titles ----*/

   if (display_run_number) {
      if (full)
         rsprintf("<tr class=\"titleRow\"><th>Date<th>Run<th>Author<th>Type<th>System<th>Subject</tr>\n");
      else
         rsprintf
             ("<tr class=\"titleRow\"><th>Date<th>Run<th>Author<th>Type<th>System<th>Subject<th>Text</tr>\n");
   } else {
      if (full)
         rsprintf("<tr class=\"titleRow\"><th>Date<th>Author<th>Type<th>System<th>Subject</tr>\n");
      else
         rsprintf("<tr class=\"titleRow\"><th>Date<th>Author<th>Type<th>System<th>Subject<th>Text</tr>\n");
   }

  /*---- do query ----*/

   if (last_n) {
      time(&now);
      ltime_start = now - 3600 * last_n;
      ptms = localtime(&ltime_start);
      sprintf(tag, "%02d%02d%02d.0", ptms->tm_year % 100, ptms->tm_mon + 1,
              ptms->tm_mday);
   } else if (*getparam("r1")) {
      /* do run query */
      el_search_run(atoi(getparam("r1")), tag);
   } else {
      /* do date-date query */
      sprintf(tag, "%02d%02d%02d.0", atoi(getparam("y1")) % 100, m1 + 1,
              atoi(getparam("d1")));
   }

   do {
      size = sizeof(text);
      status = el_retrieve(tag, date, &run, author, type, system, subject,
                           text, &size, orig_tag, reply_tag,
                           attachment[0], attachment[1], attachment[2], encoding);
      strlcat(tag, "+1", sizeof(tag));

      /* check for end run */
      if (*getparam("r2") && atoi(getparam("r2")) < run)
         break;

      /* convert date to unix format */
      memset(&tms, 0, sizeof(struct tm));
      tms.tm_year = (tag[0] - '0') * 10 + (tag[1] - '0');
      tms.tm_mon = (tag[2] - '0') * 10 + (tag[3] - '0') - 1;
      tms.tm_mday = (tag[4] - '0') * 10 + (tag[5] - '0');
      tms.tm_hour = (date[11] - '0') * 10 + (date[12] - '0');
      tms.tm_min = (date[14] - '0') * 10 + (date[15] - '0');
      tms.tm_sec = (date[17] - '0') * 10 + (date[18] - '0');

      if (tms.tm_year < 90)
         tms.tm_year += 100;
      ltime_current = mktime(&tms);

      /* check for start date */
      if (ltime_start > 0)
         if (ltime_current < ltime_start)
            continue;

      /* check for end date */
      if (ltime_end > 0) {
         if (ltime_current > ltime_end)
            break;
      }

      if (status == EL_SUCCESS) {
         /* do filtering */
         if (*getparam("type") && !equal_ustring(getparam("type"), type))
            continue;
         if (*getparam("system") && !equal_ustring(getparam("system"), system))
            continue;

         if (*getparam("author")) {
            strlcpy(str, getparam("author"), sizeof(str));
            for (i = 0; i < (int) strlen(str); i++)
               str[i] = toupper(str[i]);
            str[i] = 0;
            for (i = 0; i < (int) strlen(author) && author[i] != '@'; i++)
               str2[i] = toupper(author[i]);
            str2[i] = 0;

            if (strstr(str2, str) == NULL)
               continue;
         }

         if (*getparam("subject")) {
            strlcpy(str, getparam("subject"), sizeof(str));
            for (i = 0; i < (int) strlen(str); i++)
               str[i] = toupper(str[i]);
            str[i] = 0;
            for (i = 0; i < (int) strlen(subject); i++)
               str2[i] = toupper(subject[i]);
            str2[i] = 0;

            if (strstr(str2, str) == NULL)
               continue;
         }

         if (*getparam("subtext")) {
            strlcpy(str, getparam("subtext"), sizeof(str));
            for (i = 0; i < (int) strlen(str); i++)
               str[i] = toupper(str[i]);
            str[i] = 0;
            for (i = 0; i < (int) strlen(text); i++)
               str2[i] = toupper(text[i]);
            str2[i] = 0;

            if (strstr(str2, str) == NULL)
               continue;
         }

         /* filter passed: display line */

         strlcpy(str, tag, sizeof(str));
         if (strchr(str, '+'))
            *strchr(str, '+') = 0;
         sprintf(ref, "/EL/%s", str);

         strlcpy(str, text, sizeof(str));

         if (full) {
            if (display_run_number) {
               rsprintf("<tr><td><a href=%s>%s</a><td>%d<td>%s<td>%s<td>%s<td>%s</tr>\n",
                        ref, date, run, author, type, system, subject);
               rsprintf("<tr><td colspan=6>");
            } else {
               rsprintf("<tr><td><a href=%s>%s</a><td>%s<td>%s<td>%s<td>%s</tr>\n", ref,
                        date, author, type, system, subject);
               rsprintf("<tr><td colspan=5>");
            }

            if (equal_ustring(encoding, "plain")) {
               rsputs("<pre class=\"elogText\">");
               rsputs2(text);
               rsputs("</pre>");
            } else
               rsputs(text);

            if (!show_attachments && attachment[0][0]) {
               if (attachment[1][0])
                  rsprintf("Attachments: ");
               else
                  rsprintf("Attachment: ");
            } else
               rsprintf("</tr>\n");

            for (index = 0; index < 3; index++) {
               if (attachment[index][0]) {
                  char ref1[256];

                  for (i = 0; i < (int) strlen(attachment[index]); i++)
                     str[i] = toupper(attachment[index][i]);
                  str[i] = 0;

                  strlcpy(ref1, attachment[index], sizeof(ref1));
                  urlEncode(ref1, sizeof(ref1));

                  sprintf(ref, "/EL/%s", ref1);

                  if (!show_attachments) {
                     rsprintf("<a href=\"%s\"><b>%s</b></a> ", ref,
                              attachment[index] + 14);
                  } else {
                     colspan = display_run_number ? 6 : 5;
                     if (strstr(str, ".GIF") || strstr(str, ".PNG")
                         || strstr(str, ".JPG")) {
                        rsprintf
                            ("<tr><td colspan=%d>Attachment: <a href=\"%s\"><b>%s</b></a><br>\n",
                             colspan, ref, attachment[index] + 14);
                        if (show_attachments)
                           rsprintf("<img src=\"%s\"></tr>", ref);
                     } else {
                        rsprintf
                            ("<tr><td colspan=%d>Attachment: <a href=\"%s\"><b>%s</b></a>\n",
                             colspan, ref, attachment[index] + 14);

                        if ((strstr(str, ".TXT") ||
                             strstr(str, ".ASC") || strchr(str, '.') == NULL)
                            && show_attachments) {
                           /* display attachment */
                           rsprintf("<br><pre class=\"elogText\">");

                           std::string file_name;
                           db_get_value_string(hDB, 0, "/Logger/Data dir", 0, &file_name, TRUE);
                           if (file_name.length() > 0)
                              if (file_name[file_name.length() - 1] != DIR_SEPARATOR)
                                 file_name += DIR_SEPARATOR_STR;
                           file_name += attachment[index];

                           f = fopen(file_name.c_str(), "rt");
                           if (f != NULL) {
                              while (!feof(f)) {
                                 str[0] = 0;
                                 pc = fgets(str, sizeof(str), f);
                                 if (pc == NULL)
                                    break;
                                 rsputs2(str);
                              }
                              fclose(f);
                           }

                           rsprintf("</pre>\n");
                        }
                        rsprintf("</tr>\n");
                     }
                  }
               }
            }

            if (!show_attachments && attachment[0][0])
               rsprintf("</tr>\n");

         } else {
            if (display_run_number)
               rsprintf("<tr><td><a href=%s>%s</a><td>%d<td>%s<td>%s<td>%s<td>%s\n", ref,
                        date, run, author, type, system, subject);
            else
               rsprintf("<tr><td><a href=%s>%s</a><td>%s<td>%s<td>%s<td>%s\n", ref, date,
                        author, type, system, subject);

            if (equal_ustring(encoding, "HTML"))
               rsputs(text);
            else
               strencode(text);

            rsprintf("</tr>\n");
         }
      }

   } while (status == EL_SUCCESS);

   rsprintf("</table>\n");
   page_footer(TRUE);
}

/*------------------------------------------------------------------*/

void show_rawfile(const char *path)
{
   int size, lines, i, buf_size, offset;
   char *p;
   FILE *f;
   char file_name[256], buffer[100000];
   HNDLE hDB;

   cm_get_experiment_database(&hDB, NULL);

   lines = 10;
   if (*getparam("lines"))
      lines = atoi(getparam("lines"));

   if (*getparam("cmd") && equal_ustring(getparam("cmd"), "More lines"))
      lines *= 2;

   /* header */
   rsprintf("HTTP/1.1 200 Document follows\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
   rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

   rsprintf("<html><head>\n");
   rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
   rsprintf("<link rel=\"stylesheet\" href=\"%s\" type=\"text/css\" />\n", get_css_filename());
   rsprintf("<title>MIDAS File Display %s</title></head>\n", path);
   rsprintf("<body><form method=\"GET\" action=\"./%s\">\n", path);

   rsprintf("<input type=hidden name=lines value=%d>\n", lines);

   /*---- body needs wrapper div to pin footer ----*/
   rsprintf("<div class=\"wrapper\">\n");
   /*---- begin page header ----*/
   rsprintf("<table class=\"headerTable\">\n");

   /*---- title row ----*/

   //size = sizeof(str);
   //str[0] = 0;
   //db_get_value(hDB, 0, "/Experiment/Name", str, &size, TID_STRING, TRUE);

   if (!elog_mode)
      rsprintf("<tr><td colspan=2><input type=submit name=cmd value=\"Status\"></td></tr>");
   else
      rsprintf("<tr><td></td></tr>\n");

   //end header
   rsprintf("</table>");

   //main table:
   rsprintf("<table class=\"runlogTable\">");

   /*---- menu buttons ----*/
   rsprintf("<tr><td colspan=2>\n");
   rsprintf("<input type=submit name=cmd value=\"ELog\">\n");
   rsprintf("<input type=submit name=cmd value=\"More lines\">\n");
   rsprintf("</tr>\n");

   /*---- open file ----*/

   cm_get_experiment_database(&hDB, NULL);
   file_name[0] = 0;
   if (hDB > 0) {
      size = sizeof(file_name);
      memset(file_name, 0, size);
      db_get_value(hDB, 0, "/Logger/Data dir", file_name, &size, TID_STRING, TRUE);
      if (file_name[0] != 0)
         if (file_name[strlen(file_name) - 1] != DIR_SEPARATOR)
            strlcat(file_name, DIR_SEPARATOR_STR, sizeof(file_name));
   }
   strlcat(file_name, path, sizeof(file_name));

   f = fopen(file_name, "r");
   if (f == NULL) {
      rsprintf("<tr><td><h3>Cannot find file \"%s\"</h3></td></tr>\n", file_name);
      rsprintf("</table>\n");
      page_footer(TRUE);
      return;
   }

   /*---- file contents ----*/

   rsprintf("<tr><td colspan=2>\n");
   rsprintf("<pre style='font-family:monospace; text-align:left'>\n");

   buf_size = sizeof(buffer);

   /* position buf_size bytes before the EOF */
   fseek(f, -(buf_size - 1), SEEK_END);
   offset = ftell(f);
   if (offset != 0) {
      /* go to end of line */
      char* s = fgets(buffer, buf_size - 1, f);
      if (s) {
         *s = 0; // avoid compiler warning about unused "s"
      }

      offset = ftell(f) - offset;
      buf_size -= offset;
   }

   memset(buffer, 0, buf_size);
   int rd = fread(buffer, 1, buf_size - 1, f);
   if (rd > 0)
      buffer[rd] = 0;
   buffer[buf_size - 1] = 0;
   fclose(f);

   p = buffer + (buf_size - 2);

   /* goto end of buffer */
   while (p != buffer && *p == 0)
      p--;

   /* strip line break */
   while (p != buffer && (*p == '\n' || *p == '\r'))
      *(p--) = 0;

   /* trim buffer so that last lines remain */
   for (i = 0; i < lines; i++) {
      while (p != buffer && *p != '\n')
         p--;

      while (p != buffer && (*p == '\n' || *p == '\r'))
         p--;
   }
   if (p != buffer) {
      p++;
      while (*p == '\n' || *p == '\r')
         p++;
   }

   buf_size = (buf_size - 1) - ((POINTER_T) p - (POINTER_T) buffer);

   memmove(buffer, p, buf_size);
   buffer[buf_size] = 0;

   rsputs(buffer);

   rsprintf("</pre>\n");

   rsprintf("</td></tr></table>\r\n");
   page_footer(TRUE);
}

/*------------------------------------------------------------------*/

void show_form_query()
{
   int i = 0, size, run_number, status;
   char str[256];
   time_t now;
   HNDLE hDB, hkey, hkeyroot;
   KEY key;

   cm_get_experiment_database(&hDB, NULL);

   /* header */
   rsprintf("HTTP/1.1 200 Document follows\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
   rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

   rsprintf("<html><head>\n");
   rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
   rsprintf("<link rel=\"stylesheet\" href=\"%s\" type=\"text/css\" />\n", get_css_filename());
   rsprintf("<title>MIDAS ELog</title></head>\n");
   rsprintf("<body><form method=\"GET\" action=\"./\">\n");

   if (*getparam("form") == 0)
      return;

   /* hidden field for form */
   rsprintf("<input type=hidden name=form value=\"%s\">\n", getparam("form"));

   /*---- body needs wrapper div to pin footer ----*/
   rsprintf("<div class=\"wrapper\">\n");
   /*---- begin page header ----*/
   rsprintf("<table class=\"headerTable\">\n");

   /*---- title row ----*/
   rsprintf("<tr><td></td></tr>\n");
   //rsprintf("<tr><th colspan=2>MIDAS Electronic Logbook");
   //rsprintf("<th colspan=2>Form \"%s\"</tr>\n", getparam("form"));

   rsprintf("</table>");  //close header
   rsprintf("<table class=\"dialogTable\">");  //main table

   /*---- menu buttons ----*/

   rsprintf("<tr><td colspan=4>\n");

   rsprintf("<input type=submit name=cmd value=\"Submit\">\n");
   rsprintf("<input type=reset value=\"Reset Form\">\n");
   rsprintf("</tr>\n\n");

   /*---- entry form ----*/

   time(&now);
   rsprintf("<tr><td colspan=2 class=\"yellowLight\">Entry date: %s", ctime(&now));

   run_number = 0;
   size = sizeof(run_number);
   status = db_get_value(hDB, 0, "/Runinfo/Run number", &run_number, &size, TID_INT, TRUE);
   assert(status == SUCCESS);

   if (run_number < 0) {
      cm_msg(MERROR, "show_form_query",
             "aborting on attempt to use invalid run number %d", run_number);
      abort();
   }

   rsprintf("<td class=\"yellowLight\">Run number: ");
   rsprintf("<input type=\"text\" size=10 maxlength=10 name=\"run\" value=\"%d\"</tr>",
            run_number);

   rsprintf
       ("<tr><td colspan=2>Author: <input type=\"text\" size=\"15\" maxlength=\"80\" name=\"Author\">\n");

   rsprintf
       ("<tr><th>Item<th>Checked<th colspan=2>Comment</tr>\n");

   sprintf(str, "/Elog/Forms/%s", getparam("form"));
   db_find_key(hDB, 0, str, &hkeyroot);
   i = 0;
   if (hkeyroot)
      for (i = 0;; i++) {
         db_enum_link(hDB, hkeyroot, i, &hkey);
         if (!hkey)
            break;

         db_get_key(hDB, hkey, &key);

         strlcpy(str, key.name, sizeof(str));
         if (str[0])
            str[strlen(str) - 1] = 0;
         if (equal_ustring(str, "attachment")) {
            size = sizeof(str);
            db_get_data(hDB, hkey, str, &size, TID_STRING);
            rsprintf("<tr><td colspan=2 align=center><b>%s:</b>",
                     key.name);
            rsprintf
                ("<td colspan=2><input type=text size=30 maxlength=255 name=c%d value=\"%s\"></tr>\n",
                 i, str);
         } else {
            rsprintf("<tr><td>%d <b>%s</b>", i + 1, key.name);
            rsprintf
                ("<td align=center><input type=checkbox name=x%d value=1>",
                 i);
            rsprintf
                ("<td colspan=2><input type=text size=30 maxlength=255 name=c%d></tr>\n",
                 i);
         }
      }


   /*---- menu buttons at bottom ----*/

   if (i > 10) {
      rsprintf("<tr><td colspan=4>\n");

      rsprintf("<input type=submit name=cmd value=\"Submit\">\n");
      rsprintf("</tr>\n\n");
   }

   rsprintf("</tr></table>\n");
   page_footer(TRUE);
}

/*------------------------------------------------------------------*/

void gen_odb_attachment(const char *path, char *b)
{
   HNDLE hDB, hkeyroot, hkey;
   KEY key;
   INT i, j, size;
   char data_str[256], hex_str[256];
   char data[10000];
   time_t now;

   cm_get_experiment_database(&hDB, NULL);
   db_find_key(hDB, 0, path, &hkeyroot);
   assert(hkeyroot);

   /* title row */
   //size = sizeof(str);
   //str[0] = 0;
   //db_get_value(hDB, 0, "/Experiment/Name", str, &size, TID_STRING, TRUE);
   time(&now);

   sprintf(b, "<table border=3 cellpadding=1 class=\"dialogTable\">\n");
   sprintf(b + strlen(b), "<tr><th colspan=2>%s</tr>\n", ctime(&now));
   sprintf(b + strlen(b), "<tr><th colspan=2>%s</tr>\n", path);

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
         sprintf(b + strlen(b), "<tr><td colspan=2>%s</td></tr>\n",
                 key.name);
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
               sprintf(b + strlen(b),
                       "<tr><td>%s</td><td>%s (%s)</td></tr>\n",
                       key.name, data_str, hex_str);
            else {
               sprintf(b + strlen(b),
                       "<tr><td>%s</td><td>", key.name);
               strencode2(b + strlen(b), data_str);
               sprintf(b + strlen(b), "</td></tr>\n");
            }
         } else {
            /* display first value */
            sprintf(b + strlen(b), "<tr><td rowspan=%d>%s</td>\n",
                    key.num_values, key.name);

            for (j = 0; j < key.num_values; j++) {
               size = sizeof(data);
               db_get_data_index(hDB, hkey, data, &size, j, key.type);
               db_sprintf(data_str, data, key.item_size, 0, key.type);
               db_sprintfh(hex_str, data, key.item_size, 0, key.type);

               if (data_str[0] == 0 || equal_ustring(data_str, "<NULL>")) {
                  strcpy(data_str, "(empty)");
                  hex_str[0] = 0;
               }

               if (j > 0)
                  sprintf(b + strlen(b), "<tr>");

               if (strcmp(data_str, hex_str) != 0 && hex_str[0])
                  sprintf(b + strlen(b),
                          "<td>[%d] %s (%s)<br></td></tr>\n", j, data_str,
                          hex_str);
               else
                  sprintf(b + strlen(b), "<td>[%d] %s<br></td></tr>\n", j,
                          data_str);
            }
         }
      }
   }

   sprintf(b + strlen(b), "</table>\n");
}

/*------------------------------------------------------------------*/

void submit_elog()
{
   char author[256], path[256], path1[256];
   char mail_to[256], mail_from[256], mail_text[10000], mail_list[256],
       smtp_host[256], tag[80], mail_param[1000];
   char *buffer[3], *p, *pitem;
   HNDLE hDB, hkey;
   char att_file[3][256];
   int i, fh, size, n_mail, index;
   struct hostent *phe;
   char mhttpd_full_url[256];

   cm_get_experiment_database(&hDB, NULL);
   strlcpy(att_file[0], getparam("attachment0"), sizeof(att_file[0]));
   strlcpy(att_file[1], getparam("attachment1"), sizeof(att_file[1]));
   strlcpy(att_file[2], getparam("attachment2"), sizeof(att_file[2]));

   /* check for author */
   if (*getparam("author") == 0) {
      rsprintf("HTTP/1.1 200 Document follows\r\n");
      rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
      rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

      rsprintf("<html><head>\n");
      rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
      rsprintf("<link rel=\"stylesheet\" href=\"%s\" type=\"text/css\" />\n", get_css_filename());
      rsprintf("<title>ELog Error</title></head>\n");
      rsprintf("<i>Error: No author supplied.</i><p>\n");
      rsprintf("Please go back and enter your name in the <i>author</i> field.\n");
      rsprintf("<body></body></html>\n");
      return;
   }

   /* check for valid attachment files */
   for (i = 0; i < 3; i++) {
      buffer[i] = NULL;
      char str[256];
      sprintf(str, "attachment%d", i);
      if (getparam(str) && *getparam(str) && _attachment_size[i] == 0) {
         /* replace '\' by '/' */
         strlcpy(path, getparam(str), sizeof(path));
         strlcpy(path1, path, sizeof(path1));
         while (strchr(path, '\\'))
            *strchr(path, '\\') = '/';

         /* check if valid ODB tree */
         if (db_find_key(hDB, 0, path, &hkey) == DB_SUCCESS) {
           buffer[i] = (char*)M_MALLOC(100000);
            gen_odb_attachment(path, buffer[i]);
            strlcpy(att_file[i], path, sizeof(att_file[0]));
            strlcat(att_file[i], ".html", sizeof(att_file[0]));
            _attachment_buffer[i] = buffer[i];
            _attachment_size[i] = strlen(buffer[i]) + 1;
         }
         /* check if local file */
         else if ((fh = open(path1, O_RDONLY | O_BINARY)) >= 0) {
            size = lseek(fh, 0, SEEK_END);
            buffer[i] = (char*)M_MALLOC(size);
            lseek(fh, 0, SEEK_SET);
            int rd = read(fh, buffer[i], size);
            if (rd < 0)
               rd = 0;
            close(fh);
            strlcpy(att_file[i], path, sizeof(att_file[0]));
            _attachment_buffer[i] = buffer[i];
            _attachment_size[i] = rd;
         } else if (strncmp(path, "/HS/", 4) == 0) {
           buffer[i] = (char*)M_MALLOC(100000);
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

                     setparam(pitem, p);

                     p = strtok(NULL, "&");
                  }
               }
               *strchr(str, '?') = 0;
            }
            show_hist_page(str, str, buffer[i], &size, 0);
            strlcpy(att_file[i], str, sizeof(att_file[0]));
            _attachment_buffer[i] = buffer[i];
            _attachment_size[i] = size;
            unsetparam("scale");
            unsetparam("offset");
            unsetparam("width");
            unsetparam("index");
         } else {
            rsprintf("HTTP/1.1 200 Document follows\r\n");
            rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
            rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

            rsprintf("<html><head>\n");
            rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
            rsprintf("<link rel=\"stylesheet\" href=\"%s\" type=\"text/css\" />\n", get_css_filename());
            rsprintf("<title>ELog Error</title></head>\n");
            rsprintf("<i>Error: Attachment file <i>%s</i> not valid.</i><p>\n",
                     getparam(str));
            rsprintf
                ("Please go back and enter a proper filename (use the <b>Browse</b> button).\n");
            rsprintf("<body></body></html>\n");
            return;
         }
      }
   }

   {
      char str[256];
      /* add remote host name to author */
      phe = gethostbyaddr((char *) &remote_addr, 4, PF_INET);
      if (phe == NULL) {
         /* use IP number instead */
         strlcpy(str, (char *) inet_ntoa(remote_addr), sizeof(str));
      } else
         strlcpy(str, phe->h_name, sizeof(str));
      
      strlcpy(author, getparam("author"), sizeof(author));
      strlcat(author, "@", sizeof(author));
      strlcat(author, str, sizeof(author));
   }
      
   tag[0] = 0;
   if (*getparam("edit"))
      strlcpy(tag, getparam("orig"), sizeof(tag));

   el_submit(atoi(getparam("run")), author, getparam("type"),
             getparam("system"), getparam("subject"), getparam("text"),
             getparam("orig"), *getparam("html") ? "HTML" : "plain",
             att_file[0], _attachment_buffer[0], _attachment_size[0],
             att_file[1], _attachment_buffer[1], _attachment_size[1],
             att_file[2], _attachment_buffer[2], _attachment_size[2], tag, sizeof(tag));

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

   for (index = 0; index <= 1; index++) {
      char str[256];
      if (index == 0)
         sprintf(str, "/Elog/Email %s", getparam("type")); // FIXME: string overrun
      else
         sprintf(str, "/Elog/Email %s", getparam("system")); // FIXME: string overrun

      if (db_find_key(hDB, 0, str, &hkey) == DB_SUCCESS) {
         size = sizeof(mail_list);
         db_get_data(hDB, hkey, mail_list, &size, TID_STRING);

         if (db_find_key(hDB, 0, "/Elog/SMTP host", &hkey) != DB_SUCCESS) {
            show_error("No SMTP host defined under /Elog/SMTP host");
            return;
         }
         size = sizeof(smtp_host);
         db_get_data(hDB, hkey, smtp_host, &size, TID_STRING);

         p = strtok(mail_list, ",");
         for (i = 0; p; i++) {
            strlcpy(mail_to, p, sizeof(mail_to));

            std::string exptname;
            db_get_value_string(hDB, 0, "/Experiment/Name", 0, &exptname, TRUE);

            sprintf(mail_from, "MIDAS %s <MIDAS@%s>", exptname.c_str(), elog_host_name.c_str());

            sprintf(mail_text, "A new entry has been submitted by %s:\n\n", author);
            sprintf(mail_text + strlen(mail_text), "Experiment : %s\n", exptname.c_str());
            sprintf(mail_text + strlen(mail_text), "Type       : %s\n", getparam("type"));
            sprintf(mail_text + strlen(mail_text), "System     : %s\n", getparam("system"));
            sprintf(mail_text + strlen(mail_text), "Subject    : %s\n", getparam("subject"));

            sprintf(mail_text + strlen(mail_text), "Link       : %sEL/%s\n", mhttpd_full_url, tag);

            assert(strlen(mail_text) + 100 < sizeof(mail_text));        // bomb out on array overrun.

            strlcat(mail_text + strlen(mail_text), "\n", sizeof(mail_text));
            strlcat(mail_text + strlen(mail_text), getparam("text"),
                    sizeof(mail_text) - strlen(mail_text) - 50);
            strlcat(mail_text + strlen(mail_text), "\n", sizeof(mail_text));

            assert(strlen(mail_text) < sizeof(mail_text));      // bomb out on array overrun.

            sendmail(elog_host_name.c_str(), smtp_host, mail_from, mail_to, getparam("type"), mail_text);

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

   for (i = 0; i < 3; i++)
      if (buffer[i]) {
         M_FREE(buffer[i]);
         buffer[i] = NULL;
      }

   rsprintf("HTTP/1.1 302 Found\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());

   if (mail_param[0])
      rsprintf("Location: ../EL/%s?%s\n\n<html>redir</html>\r\n", tag, mail_param + 1);
   else
      rsprintf("Location: ../EL/%s\n\n<html>redir</html>\r\n", tag);
}

/*------------------------------------------------------------------*/

void submit_form()
{
   char str[256], att_name[256];
   char text[10000];
   int i, n_att, size;
   HNDLE hDB, hkey, hkeyroot;
   KEY key;

   /* check for author */
   if (*getparam("author") == 0) {
      rsprintf("HTTP/1.1 200 Document follows\r\n");
      rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
      rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

      rsprintf("<html><head>\n");
      rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
      rsprintf("<link rel=\"stylesheet\" href=\"%s\" type=\"text/css\" />\n", get_css_filename());
      rsprintf("<title>ELog Error</title></head>\n");
      rsprintf("<i>Error: No author supplied.</i><p>\n");
      rsprintf("Please go back and enter your name in the <i>author</i> field.\n");
      rsprintf("<body></body></html>\n");
      return;
   }

   /* assemble text from form */
   cm_get_experiment_database(&hDB, NULL);
   sprintf(str, "/Elog/Forms/%s", getparam("form"));
   db_find_key(hDB, 0, str, &hkeyroot);
   text[0] = 0;
   n_att = 0;
   if (hkeyroot)
      for (i = 0;; i++) {
         db_enum_link(hDB, hkeyroot, i, &hkey);
         if (!hkey)
            break;

         db_get_key(hDB, hkey, &key);

         strlcpy(str, key.name, sizeof(str));
         if (str[0])
            str[strlen(str) - 1] = 0;
         if (equal_ustring(str, "attachment")) {
            /* generate attachments */
            size = sizeof(str);
            db_get_data(hDB, hkey, str, &size, TID_STRING);
            _attachment_size[n_att] = 0;
            sprintf(att_name, "attachment%d", n_att++);

            sprintf(str, "c%d", i);
            setparam(att_name, getparam(str));
         } else {
            sprintf(str, "x%d", i);
            sprintf(text + strlen(text), "%d %s : [%c]  ", i + 1, key.name,
                    *getparam(str) == '1' ? 'X' : ' ');
            sprintf(str, "c%d", i);
            sprintf(text + strlen(text), "%s\n", getparam(str));
         }
      }

   /* set parameters for submit_elog() */
   setparam("type", getparam("form"));
   setparam("system", "General");
   setparam("subject", getparam("form"));
   setparam("text", text);
   setparam("orig", "");
   setparam("html", "");

   submit_elog();
}

/*------------------------------------------------------------------*/

void show_elog_page(char *path, int path_size)
{
   int size, i, run, msg_status, status, fh, length, first_message, last_message, index,
      fsize;
   char str[256], orig_path[256], command[80], ref[256], file_name[256], dir[256], *fbuffer;
   char date[80], author[80], type[80], system[80], subject[256], text[10000],
        orig_tag[80], reply_tag[80], attachment[3][256], encoding[80], att[256], url[256],
        action[80];
   HNDLE hDB, hkey, hkeyroot, hkeybutton;
   KEY key;
   FILE *f;
   BOOL display_run_number, allow_delete;
   time_t now;
   struct tm *tms;
   char def_button[][NAME_LENGTH] = { "8h", "24h", "7d" };

   /* get flag for displaying run number and allow delete */
   cm_get_experiment_database(&hDB, NULL);
   display_run_number = TRUE;
   allow_delete = FALSE;
   size = sizeof(BOOL);
   db_get_value(hDB, 0, "/Elog/Display run number", &display_run_number, &size, TID_BOOL, TRUE);
   db_get_value(hDB, 0, "/Elog/Allow delete", &allow_delete, &size, TID_BOOL, TRUE);

   /*---- interprete commands ---------------------------------------*/

   strlcpy(command, getparam("cmd"), sizeof(command));

   if (*getparam("form")) {
      if (*getparam("type")) {
         sprintf(str, "EL/?form=%s", getparam("form"));
         redirect(str);
         return;
      }
      if (equal_ustring(command, "submit"))
         submit_form();
      else
         show_form_query();
      return;
   }

   if (equal_ustring(command, "new")) {
      if (*getparam("file"))
         show_elog_new(NULL, FALSE, getparam("file"), NULL);
      else
         show_elog_new(NULL, FALSE, NULL, NULL);
      return;
   }

   if (equal_ustring(command, "Create ELog from this page")) {

      size = sizeof(url);
      if (db_get_value(hDB, 0, "/Elog/URL", url, &size, TID_STRING, FALSE) == DB_SUCCESS) {

         get_elog_url(url, sizeof(url));

         /*---- use external ELOG ----*/
         fsize = 100000;
         fbuffer = (char*)M_MALLOC(fsize);
         assert(fbuffer != NULL);

         /* write ODB contents to buffer */
         gen_odb_attachment(path, fbuffer);
         fsize = strlen(fbuffer);

         /* save temporary file */
         size = sizeof(dir);
         dir[0] = 0;
         db_get_value(hDB, 0, "/Elog/Logbook Dir", dir, &size, TID_STRING, TRUE);
         if (strlen(dir) > 0 && dir[strlen(dir)-1] != DIR_SEPARATOR)
            strlcat(dir, DIR_SEPARATOR_STR, sizeof(dir));

         time(&now);
         tms = localtime(&now);

         if (strchr(path, '/'))
            strlcpy(str, strrchr(path, '/') + 1, sizeof(str));
         else
            strlcpy(str, path, sizeof(str));
         sprintf(file_name, "%02d%02d%02d_%02d%02d%02d_%s.html",
                  tms->tm_year % 100, tms->tm_mon + 1, tms->tm_mday,
                  tms->tm_hour, tms->tm_min, tms->tm_sec, str);
         sprintf(str, "%s%s", dir, file_name);

         /* save attachment */
         fh = open(str, O_CREAT | O_RDWR | O_BINARY, 0644);
         if (fh < 0) {
            cm_msg(MERROR, "show_hist_page", "Cannot write attachment file \"%s\", open() errno %d (%s)", str, errno, strerror(errno));
         } else {
            int wr = write(fh, fbuffer, fsize);
            if (wr != fsize) {
               cm_msg(MERROR, "show_hist_page", "Cannot write attachment file \"%s\", write(%d) returned %d, errno %d (%s)", str, fsize, wr, errno, strerror(errno));
            }
            close(fh);
         }

         /* redirect to ELOG */
         if (strlen(url) > 1 && url[strlen(url)-1] != '/')
            strlcat(url, "/", sizeof(url));
         strlcat(url, "?cmd=New&fa=", sizeof(url));
         strlcat(url, file_name, sizeof(url));
         redirect(url);

         M_FREE(fbuffer);
         return;

      } else {

         char action_path[256];

         action_path[0] = 0;

         strlcpy(str, path, sizeof(str));
         while (strchr(path, '/')) {
            *strchr(path, '/') = '\\';
            strlcat(action_path, "../", sizeof(action_path));
         }

         strlcat(action_path, "EL/", sizeof(action_path));

         show_elog_new(NULL, FALSE, path, action_path);
         return;
      }
   }

   if (equal_ustring(command, "edit")) {
      show_elog_new(path, TRUE, NULL, NULL);
      return;
   }

   if (equal_ustring(command, "reply")) {
      show_elog_new(path, FALSE, NULL, NULL);
      return;
   }

   if (equal_ustring(command, "submit")) {
      submit_elog();
      return;
   }

   if (equal_ustring(command, "query")) {
      show_elog_query();
      return;
   }

   if (equal_ustring(command, "submit query")) {
      show_elog_submit_query(0);
      return;
   }

   if (strncmp(command, "Last ", 5) == 0) {
      if (command[strlen(command) - 1] == 'h')
         sprintf(str, "last%d", atoi(command + 5));
      else if (command[strlen(command) - 1] == 'd')
         sprintf(str, "last%d", atoi(command + 5) * 24);

      redirect(str);
      return;
   }

   if (equal_ustring(command, "delete")) {
      show_elog_delete(path);
      return;
   }

   if (strncmp(path, "last", 4) == 0) {
      show_elog_submit_query(atoi(path + 4));
      return;
   }

   if (equal_ustring(command, "runlog")) {
      sprintf(str, "runlog.txt");
      redirect(str);
      return;
   }

  /*---- check if file requested -----------------------------------*/

   if (strlen(path) > 13 && path[6] == '_' && path[13] == '_') {
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

      fh = open(file_name, O_RDONLY | O_BINARY);
      if (fh > 0) {
         lseek(fh, 0, SEEK_END);
         length = TELL(fh);
         lseek(fh, 0, SEEK_SET);

         rsprintf("HTTP/1.1 200 Document follows\r\n");
         rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
         rsprintf("Accept-Ranges: bytes\r\n");

         /* return proper header for file type */
         for (i = 0; i < (int) strlen(path); i++)
            str[i] = toupper(path[i]);
         str[i] = 0;

         for (i = 0; filetype[i].ext[0]; i++)
            if (strstr(str, filetype[i].ext))
               break;

         if (filetype[i].ext[0])
            rsprintf("Content-Type: %s\r\n", filetype[i].type);
         else if (strchr(str, '.') == NULL)
            rsprintf("Content-Type: text/plain\r\n");
         else
            rsprintf("Content-Type: application/octet-stream\r\n");

         rsprintf("Content-Length: %d\r\n\r\n", length);

         rread(file_name, fh, length);

         close(fh);
      }

      return;
   }

  /*---- check if runlog is requested ------------------------------*/

   if (path[0] > '9') {
      show_rawfile(path);
      return;
   }

  /*---- check next/previous message -------------------------------*/

   last_message = first_message = FALSE;
   if (equal_ustring(command, "next") || equal_ustring(command, "previous")
       || equal_ustring(command, "last")) {
      strlcpy(orig_path, path, sizeof(orig_path));

      if (equal_ustring(command, "last"))
         path[0] = 0;

      do {
         strlcat(path, equal_ustring(command, "next") ? "+1" : "-1", path_size);
         status = el_search_message(path, &fh, TRUE, NULL, 0);
         close(fh);
         if (status != EL_SUCCESS) {
            if (equal_ustring(command, "next"))
               last_message = TRUE;
            else
               first_message = TRUE;
            strlcpy(path, orig_path, path_size);
            break;
         }

         size = sizeof(text);
         el_retrieve(path, date, &run, author, type, system, subject,
                     text, &size, orig_tag, reply_tag, attachment[0], attachment[1],
                     attachment[2], encoding);

         if (strchr(author, '@'))
            *strchr(author, '@') = 0;
         if (*getparam("lauthor") == '1' && !equal_ustring(getparam("author"), author))
            continue;
         if (*getparam("ltype") == '1' && !equal_ustring(getparam("type"), type))
            continue;
         if (*getparam("lsystem") == '1' && !equal_ustring(getparam("system"), system))
            continue;
         if (*getparam("lsubject") == '1') {
            strlcpy(str, getparam("subject"), sizeof(str));
            for (i = 0; i < (int) strlen(str); i++)
               str[i] = toupper(str[i]);
            for (i = 0; i < (int) strlen(subject); i++)
               subject[i] = toupper(subject[i]);

            if (strstr(subject, str) == NULL)
               continue;
         }

         sprintf(str, "%s", path);

         if (*getparam("lauthor") == '1') {
            if (strchr(str, '?') == NULL)
               strlcat(str, "?lauthor=1", sizeof(str));
            else
               strlcat(str, "&lauthor=1", sizeof(str));
         }

         if (*getparam("ltype") == '1') {
            if (strchr(str, '?') == NULL)
               strlcat(str, "?ltype=1", sizeof(str));
            else
               strlcat(str, "&ltype=1", sizeof(str));
         }

         if (*getparam("lsystem") == '1') {
            if (strchr(str, '?') == NULL)
               strlcat(str, "?lsystem=1", sizeof(str));
            else
               strlcat(str, "&lsystem=1", sizeof(str));
         }

         if (*getparam("lsubject") == '1') {
            if (strchr(str, '?') == NULL)
               strlcat(str, "?lsubject=1", sizeof(str));
            else
               strlcat(str, "&lsubject=1", sizeof(str));
         }

         redirect(str);
         return;

      } while (TRUE);
   }

   /*---- get current message ---------------------------------------*/

   size = sizeof(text);
   strlcpy(str, path, sizeof(str));
   subject[0] = 0;
   msg_status = el_retrieve(str, date, &run, author, type, system, subject,
                            text, &size, orig_tag, reply_tag,
                            attachment[0], attachment[1], attachment[2], encoding);

   sprintf(action, "../EL/%s", str);
   show_header("ELog", "GET", action, 0);
   rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   show_navigation_bar("Elog");

   /*---- begin page header ----*/
   rsprintf("<table class=\"headerTable\">\n");

   /*---- title row ----*/

   //size = sizeof(str);
   //str[0] = 0;
   //db_get_value(hDB, 0, "/Experiment/Name", str, &size, TID_STRING, TRUE);
/*
   rsprintf("<tr><th>MIDAS Electronic Logbook");
   if (elog_mode)
      rsprintf("<th>Logbook \"%s\"</tr>\n", str);
   else
      rsprintf("<th>Experiment \"%s\"</tr>\n", str);
*/
   /*---- menu buttons ----*/
   rsprintf("<tr><td colspan=2>\n");
   /* check forms from ODB */
   db_find_key(hDB, 0, "/Elog/Forms", &hkeyroot);
   if (hkeyroot)
      for (i = 0;; i++) {
         db_enum_link(hDB, hkeyroot, i, &hkey);
         if (!hkey)
            break;

         db_get_key(hDB, hkey, &key);

         rsprintf("<input type=submit name=form value=\"%s\">\n", key.name);
      }
   rsprintf("<input type=submit name=cmd value=Runlog>\n");
   rsprintf("</tr>\n");

   /* "last x" button row */
   //rsprintf("<tr><td colspan=2>\n");

   db_find_key(hDB, 0, "/Elog/Buttons", &hkeybutton);
   if (hkeybutton == 0) {
      /* create default buttons */
      db_create_key(hDB, 0, "/Elog/Buttons", TID_STRING);
      db_find_key(hDB, 0, "/Elog/Buttons", &hkeybutton);
      assert(hkeybutton);
      db_set_data(hDB, hkeybutton, def_button, sizeof(def_button), 3, TID_STRING);
   }

   db_get_key(hDB, hkeybutton, &key);

   //rsprintf("</tr>\n");
   rsprintf("</table>\n"); //ends header table

   rsprintf("<table class=\"dialogTable\">\n"); //main table
   rsprintf("<tr><th class=\"subStatusTitle\" colspan=2>E-Log</th></tr>");

   //local buttons
   rsprintf("<tr><td colspan=2>\n");
   rsprintf("<input type=submit name=cmd value=New>\n");
   rsprintf("<input type=submit name=cmd value=Edit>\n");
   if (allow_delete)
      rsprintf("<input type=submit name=cmd value=Delete>\n");
   rsprintf("<input type=submit name=cmd value=Reply>\n");
   rsprintf("<input type=submit name=cmd value=Query></td></tr>\n");

   //period buttons
   rsprintf("<tr><td colspan=2>");
   for (i = 0; i < key.num_values; i++) {
      size = sizeof(str);
      db_get_data_index(hDB, hkeybutton, str, &size, i, TID_STRING);
      rsprintf("<input type=submit name=cmd value=\"Last %s\">\n", str);
   }

   rsprintf("<tr><td colspan=2><i>Check a category to browse only entries from that category</i></td>");
   rsprintf("<tr><td colspan=2><input type=submit name=cmd value=Next>\n");
   rsprintf("<input type=submit name=cmd value=Previous>\n");
   rsprintf("<input type=submit name=cmd value=Last>\n");
   rsprintf("</td></tr>\n\n");

   if (msg_status != EL_FILE_ERROR && (reply_tag[0] || orig_tag[0])) {
      rsprintf("<tr><td colspan=2>");
      if (orig_tag[0]) {
         sprintf(ref, "/EL/%s", orig_tag);
         rsprintf("  <a href=\"%s\">Original message</a>  ", ref);
      }
      if (reply_tag[0]) {
         sprintf(ref, "/EL/%s", reply_tag);
         rsprintf("  <a href=\"%s\">Reply to this message</a>  ", ref);
      }
      rsprintf("</tr>\n");
   }

  /*---- message ----*/

   if (msg_status == EL_FILE_ERROR)
      rsprintf
          ("<tr><td class='redLight' colspan=2 align=center><h1>No message available</h1></tr>\n");
   else {
      if (last_message)
         rsprintf
             ("<tr><td class='redLight' colspan=2 align=center><b>This is the last message in the ELog</b></tr>\n");

      if (first_message)
         rsprintf
             ("<tr><td class='redLight' colspan=2 align=center><b>This is the first message in the ELog</b></tr>\n");

      /* check for mail submissions */
      for (i = 0;; i++) {
         sprintf(str, "mail%d", i);
         if (*getparam(str)) {
            if (i == 0)
               rsprintf("<tr><td colspan=2>");
            rsprintf("Mail sent to <b>%s</b><br>\n", getparam(str));
         } else
            break;
      }
      if (i > 0)
         rsprintf("</tr>\n");


      if (display_run_number) {
         rsprintf("<tr><td>Entry date: <b>%s</b>", date);

         rsprintf("<td>Run number: <b>%d</b></tr>\n\n", run);
      } else
         rsprintf("<tr><td colspan=2>Entry date: <b>%s</b></tr>\n\n",
                  date);


      /* define hidded fields */
      strlcpy(str, author, sizeof(str));
      if (strchr(str, '@'))
         *strchr(str, '@') = 0;
      rsprintf("<input type=hidden name=author  value=\"%s\">\n", str);
      rsprintf("<input type=hidden name=type    value=\"%s\">\n", type);
      rsprintf("<input type=hidden name=system  value=\"%s\">\n", system);
      rsprintf("<input type=hidden name=subject value=\"%s\">\n\n", subject);

      if (*getparam("lauthor") == '1')
         rsprintf
             ("<tr><td><input type=\"checkbox\" checked name=\"lauthor\" value=\"1\">");
      else
         rsprintf
             ("<tr><td><input type=\"checkbox\" name=\"lauthor\" value=\"1\">");
      rsprintf("  Author: <b>%s</b>\n", author);

      if (*getparam("ltype") == '1')
         rsprintf
             ("<td><input type=\"checkbox\" checked name=\"ltype\" value=\"1\">");
      else
         rsprintf
             ("<td><input type=\"checkbox\" name=\"ltype\" value=\"1\">");
      rsprintf("  Type: <b>%s</b></tr>\n", type);

      if (*getparam("lsystem") == '1')
         rsprintf
             ("<tr><td><input type=\"checkbox\" checked name=\"lsystem\" value=\"1\">");
      else
         rsprintf
             ("<tr><td><input type=\"checkbox\" name=\"lsystem\" value=\"1\">");

      rsprintf("  System: <b>%s</b>\n", system);

      if (*getparam("lsubject") == '1')
         rsprintf
             ("<td><input type=\"checkbox\" checked name=\"lsubject\" value=\"1\">");
      else
         rsprintf
             ("<td><input type=\"checkbox\" name=\"lsubject\" value=\"1\">");
      rsprintf("  Subject: <b>%s</b></tr>\n", subject);


      /* message text */
      rsprintf("<tr><td colspan=2>\n");
      if (equal_ustring(encoding, "plain")) {
         rsputs("<pre class=\"elogText\">");
         rsputs2(text);
         rsputs("</pre>");
      } else
         rsputs(text);
      rsputs("</tr>\n");

      for (index = 0; index < 3; index++) {
         if (attachment[index][0]) {
            char ref1[256];

            for (i = 0; i < (int) strlen(attachment[index]); i++)
               att[i] = toupper(attachment[index][i]);
            att[i] = 0;

            strlcpy(ref1, attachment[index], sizeof(ref1));
            urlEncode(ref1, sizeof(ref1));

            sprintf(ref, "/EL/%s", ref1);

            if (strstr(att, ".GIF") || strstr(att, ".PNG") || strstr(att, ".JPG")) {
               rsprintf
                   ("<tr><td colspan=2>Attachment: <a href=\"%s\"><b>%s</b></a><br>\n",
                    ref, attachment[index] + 14);
               rsprintf("<img src=\"%s\"></tr>", ref);
            } else {
               rsprintf
                   ("<tr><td colspan=2>Attachment: <a href=\"%s\"><b>%s</b></a>\n",
                    ref, attachment[index] + 14);
               if (strstr(att, ".TXT") || strstr(att, ".ASC") || strchr(att, '.') == NULL) {
                  /* display attachment */
                  rsprintf("<br>");
                  if (!strstr(att, ".HTML"))
                     rsprintf("<pre class=\"elogText\">");

                  file_name[0] = 0;
                  size = sizeof(file_name);
                  memset(file_name, 0, size);
                  db_get_value(hDB, 0, "/Logger/Data dir", file_name, &size, TID_STRING, TRUE);
                  if (file_name[0] != 0)
                     if (file_name[strlen(file_name) - 1] != DIR_SEPARATOR)
                        strlcat(file_name, DIR_SEPARATOR_STR, sizeof(file_name));
                  strlcat(file_name, attachment[index], sizeof(file_name));

                  f = fopen(file_name, "rt");
                  if (f != NULL) {
                     while (!feof(f)) {
                        str[0] = 0;
                        if (!fgets(str, sizeof(str), f))
                           str[0] = 0;
                        if (!strstr(att, ".HTML"))
                           rsputs2(str);
                        else
                           rsputs(str);
                     }
                     fclose(f);
                  }

                  if (!strstr(att, ".HTML"))
                     rsprintf("</pre>");
                  rsprintf("\n");
               }
               rsprintf("</tr>\n");
            }
         }
      }
   }

   rsprintf("</table>\n");
   page_footer(TRUE);
}

/*------------------------------------------------------------------*/

void get_elog_url(char *url, int len)
{
   HNDLE hDB;
   char str[256], str2[256], *p;
   int size;

   /* redirect to external ELOG if URL present */
   cm_get_experiment_database(&hDB, NULL);
   size = sizeof(str);
   if (db_get_value(hDB, 0, "/Elog/URL", str, &size, TID_STRING, FALSE) == DB_SUCCESS) {
      if (str[0] == ':') {
         strcpy(str2, referer);
         while ((p = strrchr(str2, '/')) != NULL && p > str2 && *(p-1) != '/')
            *p = 0;
         if (strrchr(str2+5, ':'))
            *strrchr(str2+5, ':') = 0;
         if (str2[strlen(str2)-1] == '/')
            str2[strlen(str2)-1] = 0;
         sprintf(url, "%s%s", str2, str);
      } else
         strlcpy(url, str, len);
   } else
      strlcpy(url, "EL/", len);
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

void show_sc_page(const char *path, int refresh)
{
   int i, j, k, colspan, size, n_var, i_edit, i_set, line;
   char str[256], eq_name[32], group[32], name[32], ref[256];
   char group_name[MAX_GROUPS][32], data[256], back_path[256], style[80];
   const char *p;
   HNDLE hDB, hkey, hkeyeq, hkeyset, hkeynames, hkeyvar, hkeyroot;
   KEY eqkey, key, varkey;
   char data_str[256], hex_str[256], odb_path[256];

   cm_get_experiment_database(&hDB, NULL);

   /* check if variable to edit */
   i_edit = -1;
   if (equal_ustring(getparam("cmd"), "Edit"))
      i_edit = atoi(getparam("index"));

   /* check if variable to set */
   i_set = -1;
   if (equal_ustring(getparam("cmd"), "Set"))
      i_set = atoi(getparam("index"));

   /* split path into equipment and group */
   strlcpy(eq_name, path, sizeof(eq_name));
   strlcpy(group, "All", sizeof(group));
   if (strchr(eq_name, '/')) {
      strlcpy(group, strchr(eq_name, '/') + 1, sizeof(group));
      *strchr(eq_name, '/') = 0;
   }

   back_path[0] = 0;
   for (p=path ; *p ; p++)
      if (*p == '/')
         strlcat(back_path, "../", sizeof(back_path));

   /* check for "names" in settings */
   if (eq_name[0]) {
      sprintf(str, "/Equipment/%s/Settings", eq_name);
      db_find_key(hDB, 0, str, &hkeyset);
      hkeynames = 0;
      if (hkeyset) {
         for (i = 0;; i++) {
            db_enum_link(hDB, hkeyset, i, &hkeynames);

            if (!hkeynames)
               break;

            db_get_key(hDB, hkeynames, &key);

            if (strncmp(key.name, "Names", 5) == 0)
               break;
         }
      }

      /* redirect if no names found */
      if (!hkeyset || !hkeynames) {
         /* redirect */
         sprintf(str, "../Equipment/%s/Variables", eq_name);
         redirect(str);
         return;
      }
   }

   sprintf(str, "%s", group);
   show_header("MIDAS slow control", "", str, i_edit == -1 ? refresh : 0);
   rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"obsolete.js\"></script>\n");
   show_navigation_bar("SC");

   /*---- menu buttons ----*/

   rsprintf("<tr><td colspan=15>\n");

   if (equal_ustring(getparam("cmd"), "Edit"))
      rsprintf("<input type=submit name=cmd value=Set>\n");

   rsprintf("</tr>\n\n");
   rsprintf("</table>");  //end header table

   rsprintf("<table class=\"ODBtable\">");  //body table

   /*---- enumerate SC equipment ----*/

   rsprintf("<tr><td class=\"subStatusTitle\" colspan=15><i>Equipment:</i> &nbsp;&nbsp;\n");

   db_find_key(hDB, 0, "/Equipment", &hkey);
   if (hkey)
      for (i = 0;; i++) {
         db_enum_link(hDB, hkey, i, &hkeyeq);

         if (!hkeyeq)
            break;

         db_get_key(hDB, hkeyeq, &eqkey);

         db_find_key(hDB, hkeyeq, "Settings", &hkeyset);
         if (hkeyset) {
            for (j = 0;; j++) {
               db_enum_link(hDB, hkeyset, j, &hkeynames);

               if (!hkeynames)
                  break;

               db_get_key(hDB, hkeynames, &key);

               if (strncmp(key.name, "Names", 5) == 0) {
                  if (equal_ustring(eq_name, eqkey.name))
                     rsprintf("<b>%s</b> &nbsp;&nbsp;", eqkey.name);
                  else {
                     rsprintf("<a href=\"%s%s\">%s</a> &nbsp;&nbsp;",
                                 back_path, eqkey.name, eqkey.name);
                  }
                  break;
               }
            }
         }
      }
   rsprintf("</tr>\n");

   if (!eq_name[0]) {
      rsprintf("</table>");
      return;
   }

   /*---- display SC ----*/

   n_var = 0;
   sprintf(str, "/Equipment/%s/Settings/Names", eq_name);
   db_find_key(hDB, 0, str, &hkey);

   if (hkey) {

      /*---- single name array ----*/
      rsprintf("<tr><td colspan=15><i>Groups:</i> &nbsp;&nbsp;");

      /* "all" group */
      if (equal_ustring(group, "All"))
         rsprintf("<b>All</b> &nbsp;&nbsp;");
      else
         rsprintf("<a href=\"%s%s/All\">All</a> &nbsp;&nbsp;", back_path, eq_name);

      /* collect groups */

      memset(group_name, 0, sizeof(group_name));
      db_get_key(hDB, hkey, &key);

      for (int level = 0; ; level++) {
         bool next_level = false;
         for (i = 0; i < key.num_values; i++) {
            size = sizeof(str);
            db_get_data_index(hDB, hkey, str, &size, i, TID_STRING);

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
            rsprintf("<b>%s</b> &nbsp;&nbsp;", group_name[i]);
         else {
            char s[256];
            strlcpy(s, group_name[i], sizeof(s));
            urlEncode(s, sizeof(s));
            rsprintf("<a href=\"%s%s/%s\">%s</a> &nbsp;&nbsp;", back_path, eq_name,
                        s, group_name[i]);
         }
      }
      rsprintf("</tr>\n");

      /* count variables */
      sprintf(str, "/Equipment/%s/Variables", eq_name);
      db_find_key(hDB, 0, str, &hkeyvar);
      if (!hkeyvar) {
         rsprintf("</table>");
         return;
      }
      for (i = 0;; i++) {
         db_enum_link(hDB, hkeyvar, i, &hkey);
         if (!hkey)
            break;
      }

      if (i == 0 || i > 15) {
         rsprintf("</table>");
         return;
      }

      /* title row */
      colspan = 15 - i;
      rsprintf("<tr class=\"subStatusTitle\"><th colspan=%d>Names", colspan);

      /* display entries for this group */
      for (i = 0;; i++) {
         db_enum_link(hDB, hkeyvar, i, &hkey);

         if (!hkey)
            break;

         db_get_key(hDB, hkey, &key);
         rsprintf("<th>%s", key.name);
      }

      rsprintf("</tr>\n");

      /* data for current group */
      sprintf(str, "/Equipment/%s/Settings/Names", eq_name);
      db_find_key(hDB, 0, str, &hkeyset);
      assert(hkeyset);
      db_get_key(hDB, hkeyset, &key);
      for (i = 0; i < key.num_values; i++) {
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
            rsprintf("<tr class=\"ODBtableEven\"><td colspan=%d>%s", colspan, name);
         else
            rsprintf("<tr class=\"ODBtableOdd\"><td colspan=%d>%s", colspan, name);

         for (j = 0;; j++) {
            db_enum_link(hDB, hkeyvar, j, &hkey);
            if (!hkey)
               break;
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
                  strlcpy(str, getparam("value"), sizeof(str));
                  db_sscanf(str, data, &size, 0, varkey.type);
                  db_set_data_index(hDB, hkey, data, size, i, varkey.type);

                  /* redirect (so that 'reload' does not reset value) */
                  strlen_retbuf = 0;
                  sprintf(str, "%s", group);
                  redirect(str);
                  return;
               }
               if (n_var == i_edit) {
                  rsprintf("<td align=center>");
                  rsprintf("<input type=text size=10 maxlenth=80 name=value value=\"%s\">\n", str);
                  rsprintf("<input type=submit size=20 name=cmd value=Set>\n");
                  rsprintf("<input type=hidden name=index value=%d>\n", i_edit);
                  n_var++;
               } else {
                  sprintf(ref, "%s%s/%s?cmd=Edit&index=%d", back_path, eq_name, group, n_var);
                  sprintf(odb_path, "Equipment/%s/Variables/%s[%d]", eq_name, varkey.name, i);

                  rsprintf("<td align=center>");
                  rsprintf("<a href=\"%s\" onClick=\"ODBInlineEdit(this.parentNode,\'%s\', 0);return false;\" >%s</a>", ref, odb_path, str);
                  n_var++;
               }
            } else
               rsprintf("<td align=center>%s", str);
         }

         rsprintf("</tr>\n");
      }
   } else {
      /*---- multiple name arrays ----*/
      rsprintf("<tr><td colspan=15><i>Groups:</i> ");

      /* "all" group */
      if (equal_ustring(group, "All"))
         rsprintf("<b>All</b> &nbsp;&nbsp;");
      else
         rsprintf("<a href=\"%s%s\">All</a> &nbsp;&nbsp;\n", back_path, eq_name);

      /* groups from Variables tree */

      sprintf(str, "/Equipment/%s/Variables", eq_name);
      db_find_key(hDB, 0, str, &hkeyvar);
      assert(hkeyvar);

      for (i = 0;; i++) {
         db_enum_link(hDB, hkeyvar, i, &hkey);

         if (!hkey)
            break;

         db_get_key(hDB, hkey, &key);

         if (equal_ustring(key.name, group))
            rsprintf("<b>%s</b> &nbsp;&nbsp;", key.name);
         else
            rsprintf("<a href=\"%s%s/%s\">%s</a> &nbsp;&nbsp;\n", back_path,
                        eq_name, key.name, key.name);
      }

      rsprintf("</tr>\n");

      /* enumerate variable arrays */
      line = 0;
      for (i = 0;; i++) {
         db_enum_link(hDB, hkeyvar, i, &hkey);

         if (line % 2 == 0)
            strlcpy(style, "ODBtableEven", sizeof(style));
         else
            strlcpy(style, "ODBtableOdd", sizeof(style));

         if (!hkey)
            break;

         db_get_key(hDB, hkey, &varkey);

         if (!equal_ustring(group, "All") && !equal_ustring(varkey.name, group))
            continue;

         /* title row */
         rsprintf("<tr class=\"subStatusTitle\"><th colspan=9>Names<th>%s</tr>\n", varkey.name);

         if (varkey.type == TID_KEY) {
            hkeyroot = hkey;

            /* enumerate subkeys */
            for (j = 0;; j++) {
               db_enum_key(hDB, hkeyroot, j, &hkey);
               if (!hkey)
                  break;
               db_get_key(hDB, hkey, &key);

               if (key.type == TID_KEY) {
                  /* for keys, don't display data value */
                  rsprintf("<tr class=\"%s\"><td colspan=9>%s<br></tr>\n", style, key.name);
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
                        rsprintf
                            ("<tr class=\"%s\" ><td colspan=9>%s<td align=center>%s (%s)<br></tr>\n",
                             style, key.name, data_str, hex_str);
                     else
                        rsprintf("<tr class=\"%s\"><td colspan=9>%s<td align=center>%s<br></tr>\n",
                                 style, key.name, data_str);
                     line++;
                  } else {
                     /* display first value */
                     rsprintf("<tr class=\"%s\"><td colspan=9 rowspan=%d>%s\n", style, key.num_values,
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
                           rsprintf("<tr>");

                        if (strcmp(data_str, hex_str) != 0 && hex_str[0])
                           rsprintf("<td>[%d] %s (%s)<br></tr>\n", k, data_str, hex_str);
                        else
                           rsprintf("<td>[%d] %s<br></tr>\n", k, data_str);
                        line++;
                     }
                  }
               }
            }
         } else {
            /* data for current group */
            sprintf(str, "/Equipment/%s/Settings/Names %s", eq_name, varkey.name);
            db_find_key(hDB, 0, str, &hkeyset);
            if (hkeyset)
               db_get_key(hDB, hkeyset, &key);

            if (varkey.num_values > 1000)
               rsprintf("<tr class=\"%s\"><td colspan=9>%s<td align=center><i>... %d values ...</i>",
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
                  } else
                     sprintf(name, "%s[%d]", varkey.name, j);

                  if (strlen(name) < 1)
                     sprintf(name, "%s[%d]", varkey.name, j);

                  rsprintf("<tr class=\"%s\"><td colspan=9>%s", style, name);

                  size = sizeof(data);
                  db_get_data_index(hDB, hkey, data, &size, j, varkey.type);
                  db_sprintf(str, data, varkey.item_size, 0, varkey.type);

                  if (is_editable(eq_name, varkey.name)) {
                     if (n_var == i_set) {
                        /* set value */
                        strlcpy(str, getparam("value"), sizeof(str));
                        db_sscanf(str, data, &size, 0, varkey.type);
                        db_set_data_index(hDB, hkey, data, size, j, varkey.type);

                        /* redirect (so that 'reload' does not reset value) */
                        strlen_retbuf = 0;
                        sprintf(str, "%s", group);
                        redirect(str);
                        return;
                     }
                     if (n_var == i_edit) {
                        rsprintf
                            ("<td align=center><input type=text size=10 maxlenth=80 name=value value=\"%s\">\n",
                             str);
                        rsprintf("<input type=submit size=20 name=cmd value=Set></tr>\n");
                        rsprintf("<input type=hidden name=index value=%d>\n", i_edit);
                        rsprintf("<input type=hidden name=cmd value=Set>\n");
                        n_var++;
                     } else {
                        sprintf(ref, "%s%s/%s?cmd=Edit&index=%d", back_path,
                                eq_name, group, n_var);
                        sprintf(odb_path, "Equipment/%s/Variables/%s[%d]", eq_name, varkey.name, j);

                        rsprintf("<td align=cernter>");
                        rsprintf("<a href=\"%s\" onClick=\"ODBInlineEdit(this.parentNode,\'%s\', 0);return false;\" >%s</a>", ref, odb_path, str);
                        n_var++;
                     }

                  } else
                     rsprintf("<td align=center>%s\n", str);
                  rsprintf("</tr>\n");
                  line++;
               }
            }

            rsprintf("</tr>\n");
         }
      }
   }

   rsprintf("</table>\n");
   page_footer(TRUE);
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

void show_odb_tag(const char *path, const char *keypath1, const char *format, int n_var, int edit, char *type, char *pwd, char *tail)
{
   int size, index, i_edit, i_set;
   char str[TEXT_SIZE], data[TEXT_SIZE], options[1000], full_keypath[256], keypath[256], *p;
   HNDLE hDB, hkey;
   KEY key;

   /* check if variable to edit */
   i_edit = -1;
   if (equal_ustring(getparam("cmd"), "Edit"))
      i_edit = atoi(getparam("index"));

   /* check if variable to set */
   i_set = -1;
   if (equal_ustring(getparam("cmd"), "Set"))
      i_set = atoi(getparam("index"));

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
      rsprintf("<b>Key \"%s\" not found in ODB</b>\n", keypath);
   else {
      db_get_key(hDB, hkey, &key);
      size = sizeof(data);
      db_get_data_index(hDB, hkey, data, &size, index, key.type);

      if (format && strlen(format)>0)
         db_sprintff(str, format, data, key.item_size, 0, key.type);
      else
         db_sprintf(str, data, key.item_size, 0, key.type);

      if (equal_ustring(type, "checkbox")) {

         if (isparam("cbi"))
            i_set = atoi(getparam("cbi"));
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

         rsprintf("<input type=\"checkbox\" %s>\n", options);

      } else { // checkbox

         if (edit == 1) {
            if (n_var == i_set) {
               /* set value */
               strlcpy(str, getparam("value"), sizeof(str));
               db_sscanf(str, data, &size, 0, key.type);
               db_set_data_index(hDB, hkey, data, size, index, key.type);

               /* read back value */
               size = sizeof(data);
               db_get_data_index(hDB, hkey, data, &size, index, key.type);
               db_sprintf(str, data, key.item_size, 0, key.type);
            }

            if (n_var == i_edit) {
               rsprintf("<input type=text size=10 maxlength=80 name=value value=\"%s\">\n",
                        str);
               rsprintf("<input type=submit size=20 name=cmd value=Set>\n");
               rsprintf("<input type=hidden name=index value=%d>\n", n_var);
               rsprintf("<input type=hidden name=cmd value=Set>\n");
            } else {
               if (edit == 2) {
                  /* edit handling through user supplied JavaScript */
                  rsprintf("<a href=\"#\" %s>", tail);
               } else {
                  /* edit handling through form submission */
                  if (pwd[0]) {
                     rsprintf("<a onClick=\"promptpwd('%s?cmd=Edit&index=%d&pnam=%s')\" href=\"#\">", path, n_var, pwd);
                  } else {
                     rsprintf("<a href=\"%s?cmd=Edit&index=%d\" %s>", path, n_var, tail);
                  }
               }

               rsputs(str);
               rsprintf("</a>");
            }
         } else if (edit == 2) {
            rsprintf("<a href=\"#\" onclick=\"ODBEdit('%s')\">\n", full_keypath);
            rsputs(str);
            rsprintf("</a>");
         }
           else
            rsputs(str);
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

const char *cgif_label_str[] = {
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

const char *cgif_bar_str[] = {
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

void show_custom_file(const char *name)
{
   char str[256];
   std::string filename;
   std::string custom_path;
   int i, fh, size;
   HNDLE hDB, hkey;
   KEY key;

   cm_get_experiment_database(&hDB, NULL);

   // Get custom page value
   db_get_value_string(hDB, 0, "/Custom/Path", 0, &custom_path, FALSE);

   /* check for PATH variable */
   if (custom_path.length() > 0) {
      filename = custom_path;
      if (filename[filename.length()-1] != DIR_SEPARATOR)
         filename += DIR_SEPARATOR_STR;
      filename += name;
   } else {
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
         sprintf(str,"Invalid custom page: /Custom/%s not found in ODB",name);
         show_error(str);
         return;
      }
      
      char* ctext;
      int status;
      
      status = db_get_key(hDB, hkey, &key);
      assert(status == DB_SUCCESS);
      size = key.total_size;
      ctext = (char*)malloc(size);
      status = db_get_data(hDB, hkey, ctext, &size, TID_STRING);
      if (status != DB_SUCCESS) {
         sprintf(str, "Error: db_get_data() status %d", status);
         show_error(str);
         free(ctext);
         return;
      }      
      filename = ctext;
   }


   fh = open(filename.c_str(), O_RDONLY | O_BINARY);
   if (fh < 0) {
      sprintf(str, "Cannot open file \"%s\" ", filename.c_str());
      show_error(str);
      return;
   }

   size = lseek(fh, 0, SEEK_END);
   lseek(fh, 0, SEEK_SET);

   /* return audio file */
   rsprintf("HTTP/1.1 200 Document follows\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());

   /* return proper header for file type */
   for (i = 0; i < (int) strlen(name); i++)
      str[i] = toupper(name[i]);
   str[i] = 0;

   for (i = 0; filetype[i].ext[0]; i++)
      if (strstr(str, filetype[i].ext))
         break;

   if (filetype[i].ext[0])
      rsprintf("Content-Type: %s\r\n", filetype[i].type);
   else if (strchr(str, '.') == NULL)
      rsprintf("Content-Type: text/plain\r\n");
   else
      rsprintf("Content-Type: application/octet-stream\r\n");

   rsprintf("Content-Length: %d\r\n\r\n", size);

   rread(filename.c_str(), fh, size);

   close(fh);
   return;
}

/*------------------------------------------------------------------*/

void show_custom_gif(const char *name)
{
   char str[256], filename[256], data[256], value[256], src[256], custom_path[256],
      full_filename[256];
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

   custom_path[0] = 0;
   size = sizeof(custom_path);
   db_get_value(hDB, 0, "/Custom/Path", custom_path, &size, TID_STRING, FALSE);

   /* find image description in ODB */
   sprintf(str, "/Custom/Images/%s", name);
   db_find_key(hDB, 0, str, &hkeygif);
   if (!hkeygif) {

      // If we don't have Images directory, 
      // then just treat this like any other custom file.
      show_custom_file(name);
      return;
   }

   /* load background image */
   size = sizeof(filename);
   db_get_value(hDB, hkeygif, "Background", filename, &size, TID_STRING, FALSE);

   strlcpy(full_filename, custom_path, sizeof(str));
   if (full_filename[strlen(full_filename)-1] != DIR_SEPARATOR)
      strlcat(full_filename, DIR_SEPARATOR_STR, sizeof(full_filename));
   strlcat(full_filename, filename, sizeof(full_filename));

   f = fopen(full_filename, "rb");
   if (f == NULL) {
      sprintf(str, "Cannot open file \"%s\"", full_filename);
      show_error(str);
      return;
   }

   im = gdImageCreateFromGif(f);
   fclose(f);

   if (im == NULL) {
      sprintf(str, "File \"%s\" is not a GIF image", filename);
      show_error(str);
      return;
   }

   /*---- draw labels ----------------------------------------------*/

   db_find_key(hDB, hkeygif, "Labels", &hkeyroot);
   if (hkeyroot) {
      for (index = 0;; index++) {
         db_enum_key(hDB, hkeyroot, index, &hkey);
         if (!hkey)
            break;
         db_get_key(hDB, hkey, &key);

         size = sizeof(label);
         status = db_get_record1(hDB, hkey, &label, &size, 0, strcomb(cgif_label_str));
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
         status = db_get_record1(hDB, hkey, &bar, &size, 0, strcomb(cgif_bar_str));
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

   rsprintf("HTTP/1.1 200 Document follows\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());

   rsprintf("Content-Type: image/gif\r\n");
   rsprintf("Content-Length: %d\r\n", length);
   rsprintf("Cache-control: private, max-age=0, no-cache\r\n");
   rsprintf("Expires: Fri, 01-Jan-1983 00:00:00 GMT\r\n\r\n");

   rmemcpy(gb.data, length);
}



/*------------------------------------------------------------------*/

void do_jrpc_rev0()
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

   const char *xname   = getparam("name");
   const char *srpc    = getparam("rpc");

   if (!srpc || !xname) {
      show_text_header();
      rsprintf("<INVALID_ARGUMENTS>");
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
      show_text_header();
      rsprintf("<INVALID_RPC_ID>");
      return;
   }

   rpc_list[0].id = rpc;
   rpc_register_functions(rpc_list, NULL);

   show_text_header();
   rsprintf("calling rpc %d | ", rpc);

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

               rsprintf("client %s", client_name);

               status = cm_connect_client(client_name, &hconn);
               rsprintf(" %d", status);

               if (status == RPC_SUCCESS) {
                  status = rpc_client_call(hconn, rpc,
                                           getparam("arg0"),
                                           getparam("arg1"),
                                           getparam("arg2"),
                                           getparam("arg3"),
                                           getparam("arg4"),
                                           getparam("arg5"),
                                           getparam("arg6"),
                                           getparam("arg7"),
                                           getparam("arg8"),
                                           getparam("arg9")
                                           );
                  rsprintf(" %d", status);

                  status = cm_disconnect_client(hconn, FALSE);
                  rsprintf(" %d", status);
               }

               rsprintf(" | ");
            }
         }
      }
   }

   rsprintf("rpc %d, called %d clients\n", rpc, count);
}

/*------------------------------------------------------------------*/

void do_jrpc_rev1()
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

   const char *xname   = getparam("name");
   const char *srpc    = getparam("rpc");

   if (!srpc || !xname) {
      show_text_header();
      rsprintf("<INVALID_ARGUMENTS>");
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
      show_text_header();
      rsprintf("<INVALID_RPC_ID>");
      return;
   }

   rpc_list[0].id = rpc;
   status = rpc_register_functions(rpc_list, NULL);

   //printf("cm_register_functions() for format \'%s\' status %d\n", sformat, status);

   show_text_header();

   std::string reply_header;
   std::string reply_body;

   //rsprintf("<?xml version=\"1.0\" encoding=\"%s\"?>\n", HTTP_ENCODING);
   //rsprintf("<!-- created by MHTTPD on (timestamp) -->\n");
   //rsprintf("<jrpc_rev1>\n");
   //rsprintf("  <rpc>%d</rpc>\n", rpc);

   if (1) {
      HNDLE hDB, hrootkey, hsubkey, hkey;

      cm_get_experiment_database(&hDB, NULL);

      int buf_length = 1024;

      int max_reply_length = atoi(getparam("max_reply_length"));
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

               //rsprintf("  <client>\n");
               //rsprintf("    <name>%s</name>\n", client_name);

               int connect_status = -1;
               int call_status = -1;
               int call_length = 0;
               int disconnect_status = -1;

               connect_status = cm_connect_client(client_name, &hconn);

               //rsprintf("    <connect_status>%d</connect_status>\n", status);

               if (connect_status == RPC_SUCCESS) {
                  buf[0] = 0;

                  call_status = rpc_client_call(hconn, rpc,
                                                buf,
                                                buf_length,
                                                getparam("arg0"),
                                                getparam("arg1"),
                                                getparam("arg2"),
                                                getparam("arg3"),
                                                getparam("arg4"),
                                                getparam("arg5"),
                                                getparam("arg6"),
                                                getparam("arg7"),
                                                getparam("arg8"),
                                                getparam("arg9")
                                                );

                  //rsprintf("    <rpc_status>%d</rpc_status>\n", status);
                  ////rsprintf("    <data>%s</data>\n", buf);
                  //rsputs("<data>");
                  //rsputs(buf);
                  //rsputs("</data>\n");

                  if (call_status == RPC_SUCCESS) {
                     call_length = strlen(buf);
                     reply_body += buf;
                  }

                  disconnect_status = cm_disconnect_client(hconn, FALSE);
                  //rsprintf("    <disconnect_status>%d</disconnect_status>\n", status);
               }

               //rsprintf("  </client>\n");

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

   //rsprintf("  <called_clients>%d</called_clients>\n", count);
   //rsprintf("</jrpc_rev1>\n");

   if (reply_header.length() > 0) {
      rsputs(reply_header.c_str());
      rsputs(" || ");
      rsputs(reply_body.c_str());
      rsputs("\n");
   }
}

/*------------------------------------------------------------------*/

void do_jrpc()
{
   int status;

   const char *name   = getparam("name");
   const char *cmd    = getparam("rcmd");
   const char *args   = getparam("rarg");

   if (!name || !cmd || !args) {
      show_text_header();
      rsprintf("<INVALID_ARGUMENTS>");
      return;
   }

   show_text_header();

   int buf_length = 1024;

   int max_reply_length = atoi(getparam("max_reply_length"));
   if (max_reply_length > buf_length)
      buf_length = max_reply_length;

   char* buf = (char*)malloc(buf_length);
   buf[0] = 0;

   HNDLE hconn;

   status = cm_connect_client(name, &hconn);

   if (status != RPC_SUCCESS) {
      rsprintf("<RPC_CONNECT_ERROR>%d</RPC_CONNECT_ERROR>", status);
      return;
   }

   status = rpc_client_call(hconn, RPC_JRPC, cmd, args, buf, buf_length);

   if (status != RPC_SUCCESS) {
      rsprintf("<RPC_CALL_ERROR>%d</RPC_CALL_ERROR>", status);
      return;
   }

   rsprintf("%s", buf);

   status = cm_disconnect_client(hconn, FALSE);

   free(buf);
}

/*------------------------------------------------------------------*/

void output_key(HNDLE hkey, int index, const char *format)
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
         output_key(hsubkey, -1, format);
      }
   } else {
      if (key.item_size <= (int)sizeof(data)) {
         size = sizeof(data);
         db_get_data(hDB, hkey, data, &size, key.type);
         if (index == -1) {
            for (i=0 ; i<key.num_values ; i++) {
               if (isparam("name") && atoi(getparam("name")) == 1) {
                  if (key.num_values == 1)
                     rsprintf("%s:", key.name);
                  else
                     rsprintf("%s[%d]:", key.name, i);
               }
               if (format && format[0])
                  db_sprintff(str, format, data, key.item_size, i, key.type);
               else
                  db_sprintf(str, data, key.item_size, i, key.type);
               rsputs(str);
               if (i<key.num_values-1)
                  rsputs("\n");
            }
         } else {
            if (isparam("name") && atoi(getparam("name")) == 1)
               rsprintf("%s[%d]:", key.name, index);
            if (index >= key.num_values)
               rsputs("<DB_OUT_OF_RANGE>");
            else {
               if (isparam("format"))
                  db_sprintff(str, getparam("format"), data, key.item_size, index, key.type);
               else
                  db_sprintf(str, data, key.item_size, index, key.type);
               rsputs(str);
            }
         }
         rsputs("\n");
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

/*------------------------------------------------------------------*/

void javascript_commands(const char *cookie_cpwd)
{
   int status;
   int size, i, n, index, type;
   unsigned int t;
   char str[TEXT_SIZE], ppath[256], format[256], facility[256], user[256];
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

   if (isparam("cmd")) {
      cmd_parameter = getparam("cmd");
   }

   if (isparam("encoding")) {
      encoding_parameter = getparam("encoding");
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
      if (isparam("callback")) {
         jsonp = true;
         jsonp_callback = getparam("callback");
      }
   }

   if (isparam("odb")) {
      single = true;
      odb.push_back(getparam("odb"));
   }

   if (isparam("odb0")) {
      multiple = true;
      for (int i=0 ; ; i++) {
         char ppath[256];
         sprintf(ppath, "odb%d", i);
         if (!isparam(ppath))
            break;
         odb.push_back(getparam(ppath));
      }
   }

   if (/* DISABLES CODE */ (0)) {
      printf("command [%s], encoding %d [%s], jsonp %d, single %d, multiple %d, odb array size %d\n", cmd_parameter.c_str(), encoding, encoding_parameter.c_str(), jsonp, single, multiple, (int)odb.size());
   }

   /* process "jset" command */
   if (equal_ustring(getparam("cmd"), "jset")) {

      if (*getparam("pnam")) {
         sprintf(ppath, "/Custom/Pwd/%s", getparam("pnam"));
         str[0] = 0;
         db_get_value(hDB, 0, ppath, str, &size, TID_STRING, TRUE);
         if (!equal_ustring(cookie_cpwd, str)) {
            show_text_header();
            rsprintf("Invalid password!");
            return;
         }
      }
      strlcpy(str, getparam("odb"), sizeof(str));
      if (strchr(str, '[')) {
         if (*(strchr(str, '[')+1) == '*')
            index = -1;
         else
            index = atoi(strchr(str, '[')+1);
         *strchr(str, '[') = 0;
      } else
         index = 0;

      if (db_find_key(hDB, 0, str, &hkey) == DB_SUCCESS && isparam("value")) {
         db_get_key(hDB, hkey, &key);
         memset(data, 0, sizeof(data));
         if (key.item_size <= (int)sizeof(data)) {
            if (index == -1) {
               const char* p = getparam("value");
               for (i=0 ; p != NULL ; i++) {
                  size = sizeof(data);
                  db_sscanf(p, data, &size, 0, key.type);
                  if (strchr(data, ','))
                     *strchr(data, ',') = 0;
                  db_set_data_index(hDB, hkey, data, key.item_size, i, key.type);
                  p = strchr(p, ',');
                  if (p != NULL)
                     p++;
               }
            } else {
               size = sizeof(data);
               db_sscanf(getparam("value"), data, &size, 0, key.type);

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
         if (isparam("value") && isparam("type") && isparam("len")) {
            int type = atoi(getparam("type"));
            if (type == 0) {
               show_text_header();
               rsprintf("Invalid type %d!", type);
               return;
            }
            db_create_key(hDB, 0, str, type);
            db_find_key(hDB, 0, str, &hkey);
            if (!hkey) {
               show_text_header();
               rsprintf("Cannot create \'%s\' type %d", str, type);
               return;
            }
            db_get_key(hDB, hkey, &key);
            memset(data, 0, sizeof(data));
            size = sizeof(data);
            db_sscanf(getparam("value"), data, &size, 0, key.type);
            if (key.type == TID_STRING)
               db_set_data(hDB, hkey, data, atoi(getparam("len")), 1, TID_STRING);
            else {
               for (i=0 ; i<atoi(getparam("len")) ; i++)
                  db_set_data_index(hDB, hkey, data, rpc_tid_size(key.type), i, key.type);
            }
         }
      }

      show_text_header();
      rsprintf("OK");
      return;
   }

   /* process "jget" command */
   if (equal_ustring(getparam("cmd"), "jget")) {

      if (isparam("odb")) {
         strlcpy(str, getparam("odb"), sizeof(str));
         if (strchr(str, '[')) {
            if (*(strchr(str, '[')+1) == '*')
               index = -1;
            else
               index = atoi(strchr(str, '[')+1);
            *strchr(str, '[') = 0;
         } else
            index = 0;

         show_text_header();

         status = db_find_key(hDB, 0, str, &hkey);

         if (status == DB_SUCCESS)
            output_key(hkey, index, getparam("format"));
         else
            rsputs("<DB_NO_KEY>");
      }

      if (isparam("odb0")) {
         show_text_header();
         for (i=0 ; ; i++) {
            sprintf(ppath, "odb%d", i);
            sprintf(format, "format%d", i);
            if (isparam(ppath)) {
               strlcpy(str, getparam(ppath), sizeof(str));
               if (strchr(str, '[')) {
                  if (*(strchr(str, '[')+1) == '*')
                     index = -1;
                  else
                     index = atoi(strchr(str, '[')+1);
                  *strchr(str, '[') = 0;
               } else
                  index = 0;
               if (i > 0)
                  rsputs("$#----#$\n");
               if (db_find_key(hDB, 0, str, &hkey) == DB_SUCCESS)
                  output_key(hkey, index, getparam(format));
               else
                  rsputs("<DB_NO_KEY>");

            } else
               break;
         }
      }

      return;
   }

   /* process "jcopy" command */
   if (equal_ustring(getparam("cmd"), "jcopy")) {

      bool fmt_odb  = false;
      bool fmt_xml  = false;
      bool fmt_json = true;
      bool fmt_jsonp = false;
      int follow_links = 1;
      int save_keys = 1;
      int recurse = 1;
      const char* fmt = NULL;
      const char* jsonp_callback = "callback";

      if (isparam("encoding")) {
         fmt = getparam("encoding");
      } else if (isparam("format")) {
         fmt = getparam("format");
      }

      if (fmt) {
         fmt_odb = equal_ustring(fmt, "odb") > 0;
         fmt_xml = equal_ustring(fmt, "xml") > 0;
         fmt_json = strstr(fmt, "json") > 0;

         if (fmt_odb)
            fmt_xml = fmt_json = false;
         if (fmt_xml)
            fmt_odb = fmt_json = false;
         if (fmt_json)
            fmt_odb = fmt_xml = false;

         if (fmt_json)
            fmt_jsonp = strstr(fmt, "-p") > 0;
         if (fmt_jsonp && isparam("callback"))
            jsonp_callback = getparam("callback");
         if (fmt_json && strstr(fmt, "-nofollowlinks"))
            follow_links = 0;
         if (fmt_json && strstr(fmt, "-nokeys"))
            save_keys = 2;
         if (fmt_json && strstr(fmt, "-nolastwritten"))
            save_keys = 0;
         if (fmt_json && strstr(fmt, "-norecurse"))
            recurse = 0;
      }

      if (isparam("odb")) {
         strlcpy(str, getparam("odb"), sizeof(str));

         show_text_header();

         if (fmt_json)
            status = db_find_link(hDB, 0, str, &hkey);
         else
            status = db_find_key(hDB, 0, str, &hkey);
         if (status == DB_SUCCESS) {

            if (fmt_jsonp) {
               rsputs(jsonp_callback);
               rsputs("(");
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

            rsputs(buf);
            free(buf);

            if (fmt_jsonp) {
               rsputs(");\n");
            }
         } else
            rsputs("<DB_NO_KEY>");
      }

      if (isparam("odb0")) {
         show_text_header();
         if (fmt_jsonp) {
            rsputs(jsonp_callback);
            rsputs("(");
         }
         if (fmt_xml) {
            rsprintf("<?xml version=\"1.0\" encoding=\"%s\"?>\n", HTTP_ENCODING);
            rsputs("<jcopy>\n");
            rsputs("<data>\n");
         } else if (fmt_json)
            rsputs("[\n");
         else
            rsputs("");
         for (int i=0 ; ; i++) {
            char ppath[256];
            sprintf(ppath, "odb%d", i);
            if (!isparam(ppath))
               break;
            strlcpy(str, getparam(ppath), sizeof(str));

            if (i > 0) {
               if (fmt_xml)
                  rsputs("</data>\n<data>\n");
               else if (fmt_json)
                  rsputs(",\n");
               else
                  rsputs("$#----#$\n");
            }

            if (fmt_json)
               status = db_find_link(hDB, 0, str, &hkey);
            else
               status = db_find_key(hDB, 0, str, &hkey);
            if (status != DB_SUCCESS) {
               if (fmt_xml)
                  rsputs("<DB_NO_KEY/>\n");
               else if (fmt_json) {
                  char tmp[256];
                  sprintf(tmp, "{ \"/error\" : %d }\n", status);
                  rsputs(tmp);
               } else
                  rsputs("<DB_NO_KEY>\n");
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
               rsputs(s);
            } else if (fmt_json) {
               db_copy_json_obsolete(hDB, hkey, &buf, &bufsize, &end, save_keys, follow_links, recurse);
               rsputs(buf);
            } else {
               db_copy(hDB, hkey, buf, &bufsize, (char *)"");
               rsputs(buf);
            }

            free(buf);
         }

         if (fmt_xml)
            rsputs("</data>\n</jcopy>\n");
         else if (fmt_json)
            rsputs("]\n");
         else
            rsputs("");

         if (fmt_jsonp) {
            rsputs(");\n");
         }
      }
      return;
   }

   /* process "jkey" command */
   if (equal_ustring(getparam("cmd"), "jkey")) {

      // test:
      // curl "http://localhost:8080?cmd=jkey&odb0=/runinfo/run+number&odb1=/nonexistant&odb2=/&encoding=json&callback=aaa"

      show_text_header();

      if (jsonp) {
         rsputs(jsonp_callback.c_str());
         rsputs("(");
      }

      if (multiple) {
         switch (encoding) {
         default:
            break;
         case ENCODING_JSON:
            rsprintf("[ ");
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
               rsputs("$#----#$\n");
            if (status == DB_SUCCESS) {
               rsprintf("%s\n", key.name);
               rsprintf("TID_%s\n", rpc_tid_name(key.type));
               rsprintf("%d\n", key.num_values);
               rsprintf("%d\n", key.item_size);
               rsprintf("%d\n", key.last_written);
            } else {
               rsputs("<DB_NO_KEY>\n");
            }
            break;
         case ENCODING_JSON:
            if (multiple && i>0)
               rsprintf(", ");
            if (status == DB_SUCCESS) {
               rsprintf("{ ");
               rsprintf("\"name\":\"%s\",", key.name);
               rsprintf("\"type\":%d,", key.type);
               rsprintf("\"type_name\":\"TID_%s\",", rpc_tid_name(key.type));
               rsprintf("\"num_values\":%d,", key.num_values);
               rsprintf("\"item_size\":%d,", key.item_size);
               rsprintf("\"last_written\":%d", key.last_written);
               rsprintf(" }");
            } else {
               rsprintf("{ \"/error\":%d }", status);
            }
            break;
         }
      }

      if (multiple) {
         switch (encoding) {
         default:
            break;
         case ENCODING_JSON:
            rsprintf(" ]");
            break;
         }
      }

      if (jsonp) {
         rsputs(");\n");
      }

      return;
   }

   /* process "jcreate" command */
   if (equal_ustring(getparam("cmd"), "jcreate")) {

      // test:
      // curl "http://localhost:8080?cmd=jcreate&odb0=/test/foo&type0=7&odb1=/nonexistant&type1=100&odb2=/test/bar&type2=12&encoding=json&callback=aaa"
      // curl "http://localhost:8080?cmd=jcreate&odb=/test/foo&type=7"
      // curl "http://localhost:8080?cmd=jcreate&odb=/test/foo70&type=7&arraylen=10"
      // curl "http://localhost:8080?cmd=jcreate&odb=/test/foo12s&type=12&strlen=32"
      // curl "http://localhost:8080?cmd=jcreate&odb=/test/foo12s5&type=12&strlen=32&arraylen=5"
      // curl "http://localhost:8080?cmd=jcreate&odb0=/test/foo12s5x&type0=12&strlen0=32&arraylen0=5"


      show_text_header();

      if (jsonp) {
         rsputs(jsonp_callback.c_str());
         rsputs("(");
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            rsprintf("[ ");
            break;
         }
      }

      for (unsigned i=0; i<odb.size(); i++) {
         HNDLE hkey = 0;
         int type = 0;
         int arraylength = 0;
         int strlength = 0;

         if (single) {
            type = atoi(getparam("type"));
            arraylength = atoi(getparam("arraylen"));
            strlength = atoi(getparam("strlen"));
         }
         else if (multiple) {
            char p[256];
            sprintf(p, "type%d", i);
            type = atoi(getparam(p));
            sprintf(p, "arraylen%d", i);
            arraylength = atoi(getparam(p));
            sprintf(p, "strlen%d", i);
            strlength = atoi(getparam(p));
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
               rsprintf(", ");
            rsprintf("%d", status);
            break;
         }
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            rsprintf(" ]");
            break;
         }
      }

      if (jsonp) {
         rsputs(");\n");
      }

      return;
   }

   /* process "jresize" command */
   if (equal_ustring(getparam("cmd"), "jresize")) {

      // test:

      // curl "http://localhost:8080?cmd=jresize&odb=/test/foo70&arraylen=5"
      // curl "http://localhost:8080?cmd=jresize&odb=/test/foo12s5&arraylen=5"
      // curl "http://localhost:8080?cmd=jresize&odb=/test/foo12s5&strlen=16"
      // curl "http://localhost:8080?cmd=jresize&odb=/test/foo12s5&strlen=30&arraylen=10"

      show_text_header();

      if (jsonp) {
         rsputs(jsonp_callback.c_str());
         rsputs("(");
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            rsprintf("[ ");
            break;
         }
      }

      for (unsigned i=0; i<odb.size(); i++) {
         HNDLE hkey;
         KEY key;
         int arraylength = 0;
         int strlength = 0;

         if (single) {
            arraylength = atoi(getparam("arraylen"));
            strlength = atoi(getparam("strlen"));
         }
         else if (multiple) {
            char p[256];
            sprintf(p, "arraylen%d", i);
            arraylength = atoi(getparam(p));
            sprintf(p, "strlen%d", i);
            strlength = atoi(getparam(p));
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
               rsprintf(", ");
            rsprintf("%d", status);
            break;
         }
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            rsprintf(" ]");
            break;
         }
      }

      if (jsonp) {
         rsputs(");\n");
      }

      return;
   }

   /* process "jrename" command */
   if (equal_ustring(getparam("cmd"), "jrename")) {

      // test:
      // curl "http://localhost:8080?cmd=jrename&odb0=/test/foo&type0=7&odb1=/nonexistant&type1=100&odb2=/test/bar&type2=12&encoding=json&callback=aaa"
      // curl "http://localhost:8080?cmd=jrename&odb=/test/foo&name=foofoo"

      show_text_header();

      if (jsonp) {
         rsputs(jsonp_callback.c_str());
         rsputs("(");
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            rsprintf("[ ");
            break;
         }
      }

      for (unsigned i=0; i<odb.size(); i++) {
         const char* name = NULL;
         if (single)
            name = getparam("name");
         else if (multiple) {
            char p[256];
            sprintf(p, "name%d", i);
            name = getparam(p);
         }
         status = db_find_key(hDB, 0, odb[i].c_str(), &hkey);
         if (status == DB_SUCCESS) {
            status = db_rename_key(hDB, hkey, name);
         }
         switch (encoding) {
         default:
         case ENCODING_JSON:
            if (multiple && i>0)
               rsprintf(", ");
            rsprintf("%d", status);
            break;
         }
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            rsprintf(" ]");
            break;
         }
      }

      if (jsonp) {
         rsputs(");\n");
      }

      return;
   }

   /* process "jlink" command */
   if (equal_ustring(getparam("cmd"), "jlink")) {

      // test:
      // curl "http://localhost:8080?cmd=jlink&odb=/test/link&dest=/test/foo"
      // curl "http://localhost:8080?cmd=jlink&odb0=/test/link0&dest0=/test/foo&odb1=/test/link1&dest1=/test/foo"

      show_text_header();

      if (jsonp) {
         rsputs(jsonp_callback.c_str());
         rsputs("(");
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            rsprintf("[ ");
            break;
         }
      }

      for (unsigned i=0; i<odb.size(); i++) {
         const char* dest = NULL;
         if (single)
            dest = getparam("dest");
         else if (multiple) {
            char p[256];
            sprintf(p, "dest%d", i);
            dest = getparam(p);
         }

         status = db_create_link(hDB, 0, odb[i].c_str(), dest);

         switch (encoding) {
         default:
         case ENCODING_JSON:
            if (multiple && i>0)
               rsprintf(", ");
            rsprintf("%d", status);
            break;
         }
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            rsprintf(" ]");
            break;
         }
      }

      if (jsonp) {
         rsputs(");\n");
      }

      return;
   }

   /* process "jreorder" command */
   if (equal_ustring(getparam("cmd"), "jreorder")) {

      // test:
      // curl "http://localhost:8080?cmd=jreorder&odb0=/test/foo&index0=0&odb1=/test/bar&index1=1"
      // curl "http://localhost:8080?cmd=jreorder&odb=/test/bar&index=0"

      show_text_header();

      if (jsonp) {
         rsputs(jsonp_callback.c_str());
         rsputs("(");
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            rsprintf("[ ");
            break;
         }
      }

      for (unsigned i=0; i<odb.size(); i++) {
         int index = 0;
         if (single)
            index = atoi(getparam("index"));
         else if (multiple) {
            char p[256];
            sprintf(p, "index%d", i);
            index = atoi(getparam(p));
         }

         status = db_find_key(hDB, 0, odb[i].c_str(), &hkey);
         if (status == DB_SUCCESS) {
            status = db_reorder_key(hDB, hkey, index);
         }

         switch (encoding) {
         default:
         case ENCODING_JSON:
            if (multiple && i>0)
               rsprintf(", ");
            rsprintf("%d", status);
            break;
         }
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            rsprintf(" ]");
            break;
         }
      }

      if (jsonp) {
         rsputs(");\n");
      }

      return;
   }

   /* process "jdelete" command */
   if (equal_ustring(getparam("cmd"), "jdelete")) {

      // test:
      // curl "http://localhost:8080?cmd=jdelete&odb0=/test/foo&odb1=/nonexistant&odb2=/test/bar&encoding=json&callback=aaa"
      // curl "http://localhost:8080?cmd=jdelete&odb=/test/foo"

      show_text_header();

      if (jsonp) {
         rsputs(jsonp_callback.c_str());
         rsputs("(");
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            rsprintf("[ ");
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
               rsprintf(", ");
            rsprintf("%d", status);
            break;
         }
      }

      if (multiple) {
         switch (encoding) {
         default:
         case ENCODING_JSON:
            rsprintf(" ]");
            break;
         }
      }

      if (jsonp) {
         rsputs(");\n");
      }

      return;
   }

   /* process "jmsg" command */
   if (equal_ustring(getparam("cmd"), "jmsg")) {

      if (getparam("f") && *getparam("f"))
         strlcpy(facility, getparam("f"), sizeof(facility));
      else
         strlcpy(facility, "midas", sizeof(facility));

      n = 1;
      if (getparam("n") && *getparam("n"))
         n = atoi(getparam("n"));

      t = 0;
      if (getparam("t") && getparam("t"))
         t = atoi(getparam("t"));

      show_text_header();
      char* messages = NULL;
      int num_messages = 0;
      cm_msg_retrieve2(facility, t, n, &messages, &num_messages);
      if (messages) {
         rsputs(messages);
         free(messages);
      }
      return;
   }

   /* process "jgenmsg" command */
   if (equal_ustring(getparam("cmd"), "jgenmsg")) {

      if (getparam("facility") && *getparam("facility"))
         strlcpy(facility, getparam("facility"), sizeof(facility));
      else
         strlcpy(facility, "midas", sizeof(facility));
      
      if (getparam("user") && *getparam("user"))
         strlcpy(user, getparam("user"), sizeof(user));
      else
         strlcpy(user, "javascript_commands", sizeof(user));
      
      if (getparam("type") && *getparam("type"))
         type = atoi(getparam("type"));
      else
         type = MT_INFO;

      if (getparam("msg") && *getparam("msg")) {
         cm_msg1(type, __FILE__, __LINE__, facility, user, "%s", getparam("msg"));
      }

      show_text_header();
      rsputs("Message successfully created\n");
      return;
   }

   /* process "jalm" command */
   if (equal_ustring(getparam("cmd"), "jalm")) {

      show_text_header();
      al_get_alarms(str, sizeof(str));
      rsputs(str);
      return;
   }

   /* process "jrpc" command */
   if (equal_ustring(getparam("cmd"), "jrpc_rev0")) {
      do_jrpc_rev0();
      return;
   }

   /* process "jrpc" command */
   if (equal_ustring(getparam("cmd"), "jrpc_rev1")) {
      do_jrpc_rev1();
      return;
   }

   /* process "jrpc" command */
   if (equal_ustring(getparam("cmd"), "jrpc")) {
      do_jrpc();
      return;
   }
}

/*------------------------------------------------------------------*/

void show_custom_page(const char *path, const char *cookie_cpwd)
{
   int size, n_var, fh, index, edit;
   char str[TEXT_SIZE], keypath[256], type[32], *p, *ps, custom_path[256],
      filename[256], pwd[256], ppath[256], tail[256];
   HNDLE hDB, hkey;
   KEY key;
   char data[TEXT_SIZE];

   if (strstr(path, ".gif")) {
      show_custom_gif(path);
      return;
   }

   if (strchr(path, '.')) {
      show_custom_file(path);
      return;
   }

   cm_get_experiment_database(&hDB, NULL);

   if (path[0] == 0) {
      show_error("Invalid custom page: NULL path");
      return;
   }
   sprintf(str, "/Custom/%s", path);

   custom_path[0] = 0;
   size = sizeof(custom_path);
   db_get_value(hDB, 0, "/Custom/Path", custom_path, &size, TID_STRING, FALSE);
   db_find_key(hDB, 0, str, &hkey);
   if (!hkey) {
      sprintf(str, "/Custom/%s&", path);
      db_find_key(hDB, 0, str, &hkey);
      if (!hkey) {
         sprintf(str, "/Custom/%s!", path);
         db_find_key(hDB, 0, str, &hkey);
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
         sprintf(str, "Error: db_get_data() status %d", status);
         show_error(str);
         free(ctext);
         return;
      }

      /* check if filename */
      if (strchr(ctext, '\n') == 0) {
         if (custom_path[0]) {
            strlcpy(filename, custom_path, sizeof(filename));
            if (filename[strlen(filename)-1] != DIR_SEPARATOR)
               strlcat(filename, DIR_SEPARATOR_STR, sizeof(filename));
            strlcat(filename, ctext, sizeof(filename));
         } else {
            strlcpy(filename, ctext, sizeof(filename));
         }
         fh = open(filename, O_RDONLY | O_BINARY);
         if (fh < 0) {
            sprintf(str, "Cannot open file \"%s\", errno %d (%s)", filename, errno, strerror(errno));
            show_error(str);
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
      }

      /* check for valid password */
      if (equal_ustring(getparam("cmd"), "Edit")) {
         p = ps = ctext;
         n_var = 0;
         do {
            char format[256];

            p = find_odb_tag(ps, keypath, format, &edit, type, pwd, tail);
            if (p == NULL)
               break;
            ps = strchr(p, '>') + 1;

            if (pwd[0] && n_var == atoi(getparam("index"))) {
               size = NAME_LENGTH;
               strlcpy(str, path, sizeof(str));
               if (strlen(str)>0 && str[strlen(str)-1] == '&')
                  str[strlen(str)-1] = 0;
               if (getparam("pnam") && *getparam("pnam"))
                  sprintf(ppath, "/Custom/Pwd/%s", getparam("pnam"));
               else
                  sprintf(ppath, "/Custom/Pwd/%s", str);
               str[0] = 0;
               db_get_value(hDB, 0, ppath, str, &size, TID_STRING, TRUE);
               if (!equal_ustring(cookie_cpwd, str)) {
                  show_error("Invalid password!");
                  free(ctext);
                  return;
               } else
                  break;
            }

            n_var++;
         } while (p != NULL);
      }

      /* process toggle command */
      if (equal_ustring(getparam("cmd"), "Toggle")) {

         if (getparam("pnam") && *getparam("pnam")) {
            sprintf(ppath, "/Custom/Pwd/%s", getparam("pnam"));
            str[0] = 0;
            db_get_value(hDB, 0, ppath, str, &size, TID_STRING, TRUE);
            if (!equal_ustring(cookie_cpwd, str)) {
               show_error("Invalid password!");
               free(ctext);
               return;
            }
         }
         strlcpy(str, getparam("odb"), sizeof(str));
         if (strchr(str, '[')) {
            index = atoi(strchr(str, '[')+1);
            *strchr(str, '[') = 0;
         } else
            index = 0;

         if (db_find_key(hDB, 0, str, &hkey)) {
            db_get_key(hDB, hkey, &key);
            memset(data, 0, sizeof(data));
            if (key.item_size <= (int)sizeof(data)) {
               size = sizeof(data);
               db_get_data_index(hDB, hkey, data, &size, index, key.type);
               db_sprintf(str, data, size, 0, key.type);
               if (atoi(str) == 0)
                  db_sscanf("1", data, &size, 0, key.type);
               else
                  db_sscanf("0", data, &size, 0, key.type);
               db_set_data_index(hDB, hkey, data, key.item_size, index, key.type);
            }
         }

         /* redirect (so that 'reload' does not toggle again) */
         redirect(path);
         free(ctext);
         return;
      }

      /* HTTP header */
      rsprintf("HTTP/1.1 200 Document follows\r\n");
      rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
      rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

      /* interprete text, replace <odb> tags with ODB values */
      p = ps = ctext;
      n_var = 0;
      do {
         char format[256];
         p = find_odb_tag(ps, keypath, format, &edit, type, pwd, tail);
         if (p != NULL)
            *p = 0;
         rsputs(ps);

         if (p == NULL)
            break;
         ps = strchr(p + 1, '>') + 1;

         show_odb_tag(path, keypath, format, n_var, edit, type, pwd, tail);
         n_var++;

      } while (p != NULL);

      if (equal_ustring(getparam("cmd"), "Set") || isparam("cbi")) {
         /* redirect (so that 'reload' does not change value) */
         strlen_retbuf = 0;
         sprintf(str, "%s", path);
         redirect(str);
      }

      free(ctext);
      ctext = NULL;
   } else {
      show_error("Invalid custom page: Page not found in ODB");
      return;
   }
}

/*------------------------------------------------------------------*/

void show_cnaf_page()
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
   rsprintf("HTTP/1.1 200 Document follows\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
   rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

   rsprintf("<html><head>\n");
   rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
   rsprintf("<link rel=\"stylesheet\" href=\"%s\" type=\"text/css\" />\n", get_css_filename());
   rsprintf("<title>MIDAS CAMAC interface</title></head>\n");
   rsprintf("<body><form method=\"GET\" action=\"CNAF\">\n\n");

   /* title row */

   size = sizeof(str);
   str[0] = 0;
   db_get_value(hDB, 0, "/Experiment/Name", str, &size, TID_STRING, TRUE);

   rsprintf("<table border=3 cellpadding=1>\n");
   rsprintf("<tr><th colspan=3>MIDAS experiment \"%s\"", str);

   if (client_name[0] == 0)
      rsprintf("<th colspan=3 class=\"redLight\">No CAMAC server running</tr>\n");
   else if (hconn == 0)
      rsprintf("<th colspan=3 class=\"redLight\">Cannot connect to %s</tr>\n", client_name);
   else
      rsprintf("<th colspan=3>CAMAC server: %s</tr>\n", client_name);

   /* default values */
   c = n = 1;
   a = f = d = q = x = 0;
   r = 1;
   ia = id = w = 0;

   /*---- menu buttons ----*/

   rsprintf("<tr><td colspan=3>\n");
   rsprintf("<input type=submit name=cmd value=Execute>\n");

   rsprintf("<td colspan=3>\n");
   rsprintf("<input type=submit name=cmd value=ODB>\n");
   rsprintf("<input type=submit name=cmd value=Status>\n");
   rsprintf("<input type=submit name=cmd value=Help>\n");
   rsprintf("</tr>\n\n");

   /* header */
   rsprintf("<tr><th>N");
   rsprintf("<th>A");
   rsprintf("<th>F");
   rsprintf("<th colspan=3>Data");

   /* execute commands */
   size = sizeof(d);

   const char* cmd = getparam("cmd");
   if (equal_ustring(cmd, "C cycle")) {
      rpc_client_call(hconn, RPC_CNAF16, CNAF_CRATE_CLEAR, 0, 0, 0, 0, 0, &d, &size, &x,
                      &q);

      rsprintf("<tr><td colspan=6 class=\"greenLight\">C cycle executed sucessfully</tr>\n");
   } else if (equal_ustring(cmd, "Z cycle")) {
      rpc_client_call(hconn, RPC_CNAF16, CNAF_CRATE_ZINIT, 0, 0, 0, 0, 0, &d, &size, &x,
                      &q);

      rsprintf("<tr><td colspan=6 class=\"greenLight\">Z cycle executed sucessfully</tr>\n");
   } else if (equal_ustring(cmd, "Clear inhibit")) {
      rpc_client_call(hconn, RPC_CNAF16, CNAF_INHIBIT_CLEAR, 0, 0, 0, 0, 0, &d, &size, &x,
                      &q);

      rsprintf
          ("<tr><td colspan=6 class=\"greenLight\">Clear inhibit executed sucessfully</tr>\n");
   } else if (equal_ustring(cmd, "Set inhibit")) {
      rpc_client_call(hconn, RPC_CNAF16, CNAF_INHIBIT_SET, 0, 0, 0, 0, 0, &d, &size, &x,
                      &q);

      rsprintf
          ("<tr><td colspan=6 class=\"greenLight\">Set inhibit executed sucessfully</tr>\n");
   } else if (equal_ustring(cmd, "Execute")) {
      c = atoi(getparam("C"));
      n = atoi(getparam("N"));
      a = atoi(getparam("A"));
      f = atoi(getparam("F"));
      r = atoi(getparam("R"));
      w = atoi(getparam("W"));
      id = atoi(getparam("ID"));
      ia = atoi(getparam("IA"));

      const char* pd = getparam("D");
      if (strncmp(pd, "0x", 2) == 0)
         sscanf(pd + 2, "%x", &d);
      else
         d = atoi(getparam("D"));

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
               cm_disconnect_client(hconn, FALSE);
               status = cm_connect_client(client_name, &hconn);
               if (status != RPC_SUCCESS) {
                  hconn = 0;
                  client_name[0] = 0;
               }

               if (hconn)
                  status =
                      rpc_client_call(hconn, RPC_CNAF24, CNAF, 0, c, n, a, f, &d, &size,
                                      &x, &q);
            }
         }

         if (status != SUCCESS) {
            rsprintf
                ("<tr><td colspan=6 class=\"redLight\">Error executing function, code = %d</tr>",
                 status);
         } else {
            rsprintf("<tr align=center><td>%d", n);
            rsprintf("<td>%d", a);
            rsprintf("<td>%d", f);
            rsprintf("<td colspan=3>%d / 0x%04X  Q%d X%d", d, d, q, x);
         }

         d += id;
         a += ia;

         if (w > 0)
            ss_sleep(w);
      }
   }

   /* input fields */
   rsprintf
       ("<tr align=center><td><input type=text size=3 name=N value=%d>\n",
        n);
   rsprintf("<td><input type=text size=3 name=A value=%d>\n", a);
   rsprintf("<td><input type=text size=3 name=F value=%d>\n", f);
   rsprintf
       ("<td colspan=3><input type=text size=8 name=D value=%d></tr>\n",
        d);

   /* control fields */
   rsprintf("<tr><td colspan=2>Repeat");
   rsprintf("<td><input type=text size=3 name=R value=%d>\n", r);

   rsprintf
       ("<td align=center colspan=3><input type=submit name=cmd value=\"C cycle\">\n");
   rsprintf("<input type=submit name=cmd value=\"Z cycle\">\n");

   rsprintf("<tr><td colspan=2>Repeat delay [ms]");
   rsprintf("<td><input type=text size=3 name=W value=%d>\n", w);

   rsprintf
       ("<td align=center colspan=3><input type=submit name=cmd value=\"Set inhibit\">\n");
   rsprintf("<input type=submit name=cmd value=\"Clear inhibit\">\n");

   rsprintf("<tr><td colspan=2>Data increment");
   rsprintf("<td><input type=text size=3 name=ID value=%d>\n", id);

   rsprintf
       ("<td colspan=3 align=center>Branch <input type=text size=3 name=B value=0>\n");

   rsprintf("<tr><td colspan=2>A increment");
   rsprintf("<td><input type=text size=3 name=IA value=%d>\n", ia);

   rsprintf
       ("<td colspan=3 align=center>Crate <input type=text size=3 name=C value=%d>\n",
        c);

   rsprintf("</table></body>\r\n");
}

/*------------------------------------------------------------------*/

#ifdef HAVE_MSCB

typedef struct {
   signed char id;
   char name[32];
} NAME_TABLE;

NAME_TABLE prefix_table[] = {
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

NAME_TABLE unit_table[] = {

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

void show_mscb_page(const char *path, int refresh)
{
   int i, j, n, ind, fi, fd, status, size, n_addr, *addr, cur_node, adr, show_hidden;
   unsigned int uptime;
   BOOL comment_created;
   float fvalue;
   char str[256], comment[256], *pd, dbuf[256], value[256], evalue[256], unit[256], cur_subm_name[256];
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

   if (strstr(path, "favicon") != NULL)
      return;

   strlcpy(cur_subm_name, getparam("subm"), sizeof(cur_subm_name));
   if (cur_subm_name[0] == 0) {
      db_enum_key(hDB, hKeySubm, 0, &hKeyCurSubm);
      if (!hKeyCurSubm) {
         sprintf(str, "No submaster defined under /MSCB/Submaster");
         show_error(str);
         return;
      }
      db_get_key(hDB, hKeyCurSubm, &key);
      strcpy(cur_subm_name, key.name);
   } else
      db_find_key(hDB, hKeySubm, cur_subm_name, &hKeyCurSubm);

   /* perform MSCB rescan */
   if (isparam("cmd") && equal_ustring(getparam("cmd"), "Rescan") && isparam("subm")) {
      /* create Pwd and Comment if not there */
      size = 32;
      str[0] = 0;
      db_get_value(hDB, hKeyCurSubm, "Pwd", (void *)str, &size, TID_STRING, true);
      str[0] = 0;
      db_get_value(hDB, hKeyCurSubm, "Comment", (void *)str, &size, TID_STRING, true);

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

            status = mscb_ping(fd, (unsigned short) ind, 1);
            if (status == MSCB_SUCCESS) {

               /* node found, search next 100 as well */
               for (j=ind; j<ind+100 && j<0x10000 ; j++)
                  if (j >= 0)
                     ping_addr[j] = 1;

               status = mscb_info(fd, (unsigned short) ind, &info);
               strncpy(str, info.node_name, sizeof(info.node_name));
               str[16] = 0;

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

         if (path[0])
            sprintf(str, "../%s", cur_subm_name);
         else
            sprintf(str, "%s", cur_subm_name);
         redirect(str);
         return;

      } else {
         sprintf(str, "Cannot talk to submaster \"%s\"", cur_subm_name);
         show_error(str);
         return;
      }
   }

   if (isparam("subm") && isparam("node")) {
      strlcpy(cur_subm_name, getparam("subm"), sizeof(cur_subm_name));
      cur_node = atoi(getparam("node"));

      /* write data to node */
      if (isparam("idx") && isparam("value")) {
         i = atoi(getparam("idx"));
         strlcpy(value, getparam("value"), sizeof(value));

         fd = mscb_init(cur_subm_name, 0, "", FALSE);
         if (fd >= 0) {
            status = mscb_info_variable(fd,
                       (unsigned short) cur_node, (unsigned char) i, &info_var);
            if (status == MSCB_SUCCESS) {
               if (info_var.unit == UNIT_STRING) {
                  memset(str, 0, sizeof(str));
                  strncpy(str, value, info_var.width);
                  if (strlen(str) > 0 && str[strlen(str) - 1] == '\n')
                     str[strlen(str) - 1] = 0;

                  status = mscb_write(fd, (unsigned short) cur_node,
                                    (unsigned char) i, str, strlen(str) + 1);
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
      }

      if (path[0])
         sprintf(str, "../%s/%d", cur_subm_name, cur_node);
      else
         sprintf(str, "%s/%d", cur_subm_name, cur_node);
      if (isparam("hidden"))
         strlcat(str, "h", sizeof(str));
      redirect(str);
      return;
   }

   if (path[0]) {
      strlcpy(cur_subm_name, path, sizeof(cur_subm_name));
      if (strchr(cur_subm_name, '/'))
         *strchr(cur_subm_name, '/') = 0;
      if (strchr(cur_subm_name, '?'))
         *strchr(cur_subm_name, '?') = 0;
      if (strchr(path, '/'))
         cur_node = atoi(strchr(path, '/')+1);
      else
         cur_node = -1;
   } else {
      cur_subm_name[0] = 0;
      cur_node = -1;
   }

   if (path[0] && path[strlen(path)-1] == 'h')
      show_hidden = TRUE;
   else
      show_hidden = FALSE;

   show_header("MSCB", "GET", "./", refresh);
   rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   show_navigation_bar("MSCB");

   /* style sheet */
   rsprintf("<style type=\"text/css\">\r\n");
   rsprintf("select { width:150px; background-color:#FFFFE0; font-size:12px; }\r\n");
   rsprintf(".subm {\r\n");
   rsprintf("  background-color:#E0E0E0; text-align:center; font-weight:bold;\r\n");
   rsprintf("  padding:5px;\r\n");
   rsprintf("  vertical-align:top;\r\n");
   rsprintf("  font-size:16px;\r\n");
   rsprintf("  border-right:1px solid #808080;\r\n");
   rsprintf("}\r\n");
   rsprintf(".node {\r\n");
   rsprintf("  background-color:#E0E0E0; text-align:center; font-weight:bold;\r\n");
   rsprintf("  padding:5px;\r\n");
   rsprintf("  vertical-align:top;\r\n");
   rsprintf("  font-size:16px;\r\n");
   rsprintf("  border-right:1px solid #808080;\r\n");
   rsprintf("}\r\n");
   rsprintf(".vars {\r\n");
   rsprintf("  background-color:#E0E0E0; text-align:center; font-weight:bold;\r\n");
   rsprintf("  padding:5px;\r\n");
   rsprintf("  vertical-align:top;\r\n");
   rsprintf("  font-size:10px;\r\n");
   rsprintf("}\r\n");
   rsprintf(".v1 {\r\n");
   rsprintf("  padding:3px;\r\n");
   rsprintf("  font-weight:bold;\r\n");
   rsprintf("  font-size:12px;\r\n");
   rsprintf("}\r\n");
   rsprintf(".v2 {\r\n");
   rsprintf("  background-color:#F0F0F0;\r\n");
   rsprintf("  padding:3px;\r\n");
   rsprintf("  font-size:12px;\r\n");
   rsprintf("  border:1px solid #808080;\r\n");
   rsprintf("  border-right:1px solid #FFFFFF;\r\n");
   rsprintf("  border-bottom:1px solid #FFFFFF;\r\n");
   rsprintf("}\r\n");
   rsprintf(".v3 {\r\n");
   rsprintf("  padding:3px;\r\n");
   rsprintf("  font-size:12px;\r\n");
   rsprintf("}\r\n");
   rsprintf("</style>\r\n\r\n");

   /* javascript */
   rsprintf("<script type=\"text/javascript\">\r\n");
   rsprintf("function mscb_edit(index, value)\r\n");
   rsprintf("{\r\n");
   rsprintf("   var new_value = prompt('Please enter new value', value);\r\n");
   rsprintf("   if (new_value != undefined) {\r\n");
   rsprintf("     o = document.createElement('input');\r\n");
   rsprintf("     o.type = 'hidden';\r\n");
   rsprintf("     o.name = 'idx';\r\n");
   rsprintf("     o.value = index;\r\n");
   rsprintf("     document.form1.appendChild(o);\r\n");
   rsprintf("     o = document.createElement('input');\r\n");
   rsprintf("     o.type = 'hidden';\r\n");
   rsprintf("     o.name = 'value';\r\n");
   rsprintf("     o.value = new_value;\r\n");
   rsprintf("     document.form1.appendChild(o);\r\n");
   rsprintf("     document.form1.submit()\r\n");
   rsprintf("   }\n");
   rsprintf("}\r\n");
   rsprintf("</script>\r\n\r\n");

   rsprintf("<table class=\"dialogTable\">");  //main table
   rsprintf("<tr><th class=\"subStatusTitle\" colspan=2>MSCB</th><tr>");
   /*---- menu buttons ----*/

   rsprintf("<tr><td colspan=2>\n");
   rsprintf("<table width=100%%><tr>\n");
   rsprintf("<td><input type=submit name=cmd value=Reload></td>\n");

   rsprintf("<tr><td colspan=\"2\" cellpadding=\"0\" cellspacing=\"0\">\r\n");

   status = db_find_key(hDB, 0, "MSCB/Submaster", &hKeySubm);
   if (status != DB_SUCCESS) {
      rsprintf("<h1>No MSCB Submasters defined in ODB</h1>\r\n");
      rsprintf("</td></tr>\r\n");
      rsprintf("</table>\r\n"); //submaster table
      rsprintf("</td></tr>\r\n");
      rsprintf("</table>\r\n");  //main table
      page_footer(TRUE);
      return;
   }

   rsprintf("<table width=\"100%%\" cellpadding=\"0\" cellspacing=\"0\">");

   /*---- submaster list ----*/
   rsprintf("<tr><td class=\"subm\">\r\n");
   rsprintf("Submaster<hr>\r\n");

   /* count submasters */
   for (i = 0;;i++) {
      db_enum_key(hDB, hKeySubm, i, &hKey);
      if (!hKey)
         break;
   }
   if (i<2)
      i = 2;

   rsprintf("<select name=\"subm\" size=%d onChange=\"document.form1.submit();\">\r\n", i);
   hKeyCurSubm = 0;
   for (i = 0;;i++) {
      db_enum_key(hDB, hKeySubm, i, &hKey);
      if (!hKey)
         break;
      db_get_key(hDB, hKey, &key);
      strcpy(str, key.name);
      size = sizeof(comment);
      if (db_get_value(hDB, hKey, "Comment", comment, &size, TID_STRING, FALSE) == DB_SUCCESS) {
         strcat(str, ": ");
         strlcat(str, comment, sizeof(str));
      }

      if ((cur_subm_name[0] && equal_ustring(cur_subm_name, key.name)) ||
          (cur_subm_name[0] == 0 && i == 0)) {
         rsprintf("<option value=\"%s\" selected>%s</option>\r\n", key.name, str);
         hKeyCurSubm = hKey;
      } else
         rsprintf("<option value=\"%s\">%s</option>\r\n", key.name, str);
   }
   rsprintf("</select>\r\n");

   /*---- node list ----*/
   rsprintf("<td class=\"node\">\r\n");
   rsprintf("Node ");

   rsprintf("<script type=\"text/javascript\">\n");
   rsprintf("<!--\n");
   rsprintf("function rescan()\n");
   rsprintf("{\n");
   rsprintf("   flag = confirm('Rescan can take up to one minute.');\n");
   rsprintf("   if (flag == true)\n");
   rsprintf("      window.location.href = '?cmd=Rescan&subm=%s';\n", cur_subm_name);
   rsprintf("}\n");
   rsprintf("//-->\n");
   rsprintf("</script>\n");

   rsprintf("<input type=button name=cmd value=\"Rescan\" onClick=\"rescan();\">");
   rsprintf("<hr>\r\n");

   if (!hKeyCurSubm) {
      rsprintf("No submaster found in ODB\r\n");
      rsprintf("</td></tr>\r\n");
      rsprintf("</table>\r\n");  //inner submaster table
      rsprintf("</td></tr>\r\n");
      rsprintf("</table>\r\n");  //submaster table
      rsprintf("</td></tr>\r\n");
      rsprintf("</table>\r\n"); //main table
      page_footer(TRUE);
      return;
   }

   db_find_key(hDB, hKeyCurSubm, "Address", &hKeyAddr);
   db_find_key(hDB, hKeyCurSubm, "Node comment", &hKeyComm);

   i = 10;
   if (hKeyAddr) {
      db_get_key(hDB, hKeyAddr, &key);
      i = key.num_values;
   }
   if (i < 2)
      i = 2;
   rsprintf("<select name=\"node\" size=%d onChange=\"document.form1.submit();\">\r\n", i);

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
         size = sizeof(adr);
         db_get_data_index(hDB, hKeyAddr, &adr, &size, i, TID_INT);
         if (hKeyComm) {
            size = sizeof(comment);
            db_get_data_index(hDB, hKeyComm, comment, &size, i, TID_STRING);
            sprintf(str, "%d: %s", adr, comment);
         } else
            sprintf(str, "%d", adr);
         if (cur_node == 0 && i == 0)
            cur_node = adr;
         if (adr == cur_node)
            rsprintf("<option selected>%s</option>\r\n", str);
         else
            rsprintf("<option>%s</option>\r\n", str);
      }
   }
   rsprintf("</select>\r\n");

   /*---- node contents ----*/
   rsprintf("<td class=\"vars\">\r\n");
   rsprintf("<table>\r\n");
   db_get_key(hDB, hKeyCurSubm, &key);
   if (cur_node != -1)
      rsprintf("<tr><td colspan=3 align=center><b>%s:%d</b>", key.name, cur_node);
   else
      rsprintf("<tr><td colspan=3 align=center><b>%s</b>", key.name);
   rsprintf("<hr></td></tr>\r\n");
   str[0] = 0;
   size = 32;
   db_get_value(hDB, hKeyCurSubm, "Pwd", str, &size, TID_STRING, TRUE);

   fd = mscb_init(key.name, 0, str, FALSE);
   if (fd < 0) {
      if (fd == EMSCB_WRONG_PASSWORD)
         rsprintf("<tr><td colspan=3><b>Invalid password</b></td>");
      else
         rsprintf("<tr><td colspan=3><b>Submaster does not respond</b></td>");
      goto mscb_error;
   }
   mscb_set_eth_max_retry(fd, 3);
   mscb_set_max_retry(1);

   status = mscb_ping(fd, cur_node, TRUE);
   if (status != MSCB_SUCCESS) {
      rsprintf("<tr><td colspan=3><b>No response from node</b></td>");
      goto mscb_error;
   }
   status = mscb_info(fd, (unsigned short) cur_node, &info);
   if (status != MSCB_SUCCESS) {
      rsprintf("<tr><td colspan=3><b>No response from node</b></td>");
      goto mscb_error;
   }
   strncpy(str, info.node_name, sizeof(info.node_name));
   str[16] = 0;
   rsprintf("<tr><td class=\"v1\">Node name<td colspan=2 class=\"v2\">%s</tr>\n", str);
   rsprintf("<tr><td class=\"v1\">GIT revision<td colspan=2 class=\"v2\">%d</tr>\n", info.revision);

   if (info.rtc[0] && info.rtc[0] != 0xFF) {
      for (i=0 ; i<6 ; i++)
         info.rtc[i] = (info.rtc[i] / 0x10) * 10 + info.rtc[i] % 0x10;
      rsprintf("<tr><td class=\"v1\">Real Time Clock<td colspan=2 class=\"v2\">%02d-%02d-%02d %02d:%02d:%02d</td>\n",
         info.rtc[0], info.rtc[1], info.rtc[2],
         info.rtc[3], info.rtc[4], info.rtc[5]);
   }

   status = mscb_uptime(fd, (unsigned short) cur_node, &uptime);
    if (status == MSCB_SUCCESS)
      rsprintf("<tr><td class=\"v1\">Uptime<td colspan=2 class=\"v2\">%dd %02dh %02dm %02ds</tr>\n",
             uptime / (3600 * 24),
             (uptime % (3600 * 24)) / 3600, (uptime % 3600) / 60,
             (uptime % 60));

   rsprintf("<tr><td colspan=3><hr></td></tr>\r\n");

   /* check for hidden variables */
   for (i=0 ; i < info.n_variables ; i++) {
      mscb_info_variable(fd, cur_node, i, &info_var);
      if (info_var.flags & MSCBF_HIDDEN)
         break;
   }
   if (i < info.n_variables) {
      strcpy(str, show_hidden ? " checked" : "");
      rsprintf("<tr><td colspan=3><input type=checkbox%s name=\"hidden\" value=\"1\"", str);
      rsprintf("onChange=\"document.form1.submit();\">Display hidden variables<hr></td></tr>\r\n");
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
         rsprintf("<tr><td colspan=3><b>Error reading data from node</b></td>");
         goto mscb_error;
      }
      pd = dbuf;

      for (j=fi ; j<i ; j++) {
         status = mscb_info_variable(fd, cur_node, j, &info_var);
         if ((info_var.flags & MSCBF_HIDDEN) == 0 || show_hidden) {
            memcpy(str, info_var.name, 8);
            str[8] = 0;
            rsprintf("<tr><td class=\"v1\">%s</td>\r\n", str);
            rsprintf("<td class=\"v2\">\r\n");
            print_mscb_var(value, evalue, unit, &info_var, pd);
            rsprintf("<a href=\"#\" onClick=\"mscb_edit(%d,'%s')\">%s</a>",
               j, evalue, value);
            rsprintf("</td><td class=\"v3\">%s</td>", unit);
            rsprintf("</tr>\r\n");
         }
         pd += info_var.width;
      }

      fi = i;
   }

mscb_error:
   rsprintf("</tr></table>\r\n");
   rsprintf("</td></tr></table>\r\n");
   rsprintf("</td></tr></table>\r\n");
   rsprintf("</td></tr></table>\r\n");
   page_footer(TRUE);
}

#endif // HAVE_MSCB

/*------------------------------------------------------------------*/

void show_password_page(const char *password, const char *experiment)
{
   rsprintf("HTTP/1.1 200 Document follows\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
   rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

   rsprintf("<html><head>\n");
   rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
   rsprintf("<link rel=\"stylesheet\" href=\"%s\" type=\"text/css\" />\n", get_css_filename());
   rsprintf("<title>Enter password</title></head><body>\n\n");

   rsprintf("<form method=\"GET\" action=\".\">\n\n");

   /* define hidden fields for current experiment */
   if (experiment[0])
      rsprintf("<input type=hidden name=exp value=\"%s\">\n", experiment);

   /*---- body needs wrapper div to pin footer ----*/
   rsprintf("<div class=\"wrapper\">\n");
   /*---- page header ----*/
   rsprintf("<table class=\"headerTable\"><tr><td></td><tr></table>\n");

   rsprintf("<table class=\"dialogTable\">\n");  //main table
   if (password[0])
      rsprintf("<tr><th class=\"redLight\">Wrong password!</tr>\n");

   rsprintf("<tr><th>Please enter password</tr>\n");
   rsprintf("<tr><td align=center><input type=password name=pwd></tr>\n");
   rsprintf("<tr><td align=center><input type=submit value=Submit></tr>");

   rsprintf("</table>\n");

   page_footer(TRUE);
}

/*------------------------------------------------------------------*/

BOOL check_web_password(const char *password, const char *redir, const char *experiment)
{
   HNDLE hDB, hkey;
   INT size;
   char str[256];

   cm_get_experiment_database(&hDB, NULL);

   /* check for password */
   db_find_key(hDB, 0, "/Experiment/Security/Web Password", &hkey);
   if (hkey) {
      size = sizeof(str);
      db_get_data(hDB, hkey, str, &size, TID_STRING);
      if (strcmp(password, str) == 0)
         return TRUE;

      /* show web password page */
      rsprintf("HTTP/1.1 200 Document follows\r\n");
      rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
      rsprintf("Content-Type: text/html; charset=%s\r\n\r\n", HTTP_ENCODING);

      rsprintf("<html><head>\n");
      rsprintf("<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\" />\n");
      rsprintf("<link rel=\"stylesheet\" href=\"%s\" type=\"text/css\" />\n", get_css_filename());
      rsprintf("<title>Enter password</title></head><body>\n\n");

      rsprintf("<form method=\"GET\" action=\".\">\n\n");

      /* define hidden fields for current experiment and destination */
      if (experiment[0])
         rsprintf("<input type=hidden name=exp value=\"%s\">\n", experiment);
      if (redir[0])
         rsprintf("<input type=hidden name=redir value=\"%s\">\n", redir);

      /*---- body needs wrapper div to pin footer ----*/
      rsprintf("<div class=\"wrapper\">\n");
      /*---- page header ----*/
      rsprintf("<table class=\"headerTable\"><tr><td></td><tr></table>\n");

      rsprintf("<table class=\"dialogTable\">\n");  //main table

      if (password[0])
         rsprintf("<tr><th class=\"redLight\">Wrong password!</tr>\n");

      rsprintf
          ("<tr><th>Please enter password to obtain write access</tr>\n");
      rsprintf("<tr><td align=center><input type=password name=wpwd></tr>\n");
      rsprintf("<tr><td align=center><input type=submit value=Submit></tr>");

      rsprintf("</table>\n");

      page_footer(TRUE);

      return FALSE;
   } else
      return TRUE;
}

/*------------------------------------------------------------------*/

void show_start_page(int script)
{
   int rn, i, j, n, size, status, maxlength;
   HNDLE hDB, hkey, hsubkey, hkeycomm, hkeyc;
   KEY key;
   char data[1000], str[32];
   char data_str[256], comment[1000];

   cm_get_experiment_database(&hDB, NULL);

   if (script) {
      show_header("Start sequence", "GET", "", 0);
      //begin start menu dialog table:
      rsprintf("<table class=\"ODBTable\">\n");
      rsprintf("<tr><th colspan=2>Start script</th>\n");
   } else {
      show_header("Start run", "GET", "", 0);
      //begin start menu dialog table:
      rsprintf("<table class=\"ODBTable\">\n");
      rsprintf("<tr><th colspan=2 class=\"subStatusTitle\">Start new run</tr>\n");
      rsprintf("<tr><td>Run number");

      /* run number */
      size = sizeof(rn);
      status = db_get_value(hDB, 0, "/Runinfo/Run number", &rn, &size, TID_INT, TRUE);
      assert(status == SUCCESS);

      if (rn < 0) { // value "zero" is ok
         cm_msg(MERROR, "show_start_page",
                "aborting on attempt to use invalid run number %d", rn);
         abort();
      }

      size = sizeof(i);
      if (db_find_key(hDB, 0, "/Experiment/Edit on start/Edit Run number", &hkey) ==
          DB_SUCCESS && db_get_data(hDB, hkey, &i, &size, TID_BOOL) && i == 0)
         rsprintf("<td><input type=hidden name=value value=%d>%d</tr>\n", rn + 1, rn + 1);
      else
         rsprintf("<td><input type=text size=20 maxlength=80 name=value value=%d></tr>\n",
                  rn + 1);
   }
   /* run parameters */
   if (script)
      db_find_key(hDB, 0, "/Experiment/Edit on sequence", &hkey);
   else
      db_find_key(hDB, 0, "/Experiment/Edit on start", &hkey);
   db_find_key(hDB, 0, "/Experiment/Parameter Comments", &hkeycomm);
   if (hkey) {
      for (i = 0, n = 0;; i++) {
         db_enum_link(hDB, hkey, i, &hsubkey);

         if (!hsubkey)
            break;

         db_get_link(hDB, hsubkey, &key);
         strlcpy(str, key.name, sizeof(str));

         if (equal_ustring(str, "Edit run number"))
            continue;

         db_enum_key(hDB, hkey, i, &hsubkey);
         db_get_key(hDB, hsubkey, &key);

         size = sizeof(data);
         status = db_get_data(hDB, hsubkey, data, &size, key.type);
         if (status != DB_SUCCESS)
            continue;

         for (j = 0; j < key.num_values; j++) {
            if (key.num_values > 1)
               rsprintf("<tr><td>%s [%d]", str, j);
            else
               rsprintf("<tr><td>%s", str);

            if (j == 0 && hkeycomm) {
               /* look for comment */
               if (db_find_key(hDB, hkeycomm, key.name, &hkeyc) == DB_SUCCESS) {
                  size = sizeof(comment);
                  if (db_get_data(hDB, hkeyc, comment, &size, TID_STRING) == DB_SUCCESS)
                     rsprintf("<br>%s\n", comment);
               }
            }

            db_sprintf(data_str, data, key.item_size, j, key.type);

            maxlength = 80;
            if (key.type == TID_STRING)
               maxlength = key.item_size;

            if (key.type == TID_BOOL) {
               if (((DWORD*)data)[j])
                  rsprintf("<td><input type=checkbox checked name=x%d value=1></td></tr>\n", n++);
               else
                  rsprintf("<td><input type=checkbox name=x%d value=1></td></tr>\n", n++);
            } else
               rsprintf("<td><input type=text size=%d maxlength=%d name=x%d value=\"%s\"></tr>\n",
                        (maxlength<80)?maxlength:80, maxlength-1, n++, data_str);
         }
      }
   }

   rsprintf("<tr><td align=center colspan=2 style=\"background-color:#EEEEEE;\">\n");
   if (script) {
      rsprintf("<input type=submit name=cmd value=\"Start Script\">\n");
      rsprintf("<input type=hidden name=params value=1>\n");
   } else
      rsprintf("<input type=submit name=cmd value=Start>\n");
   rsprintf("<input type=submit name=cmd value=Cancel>\n");
   rsprintf("</tr>\n");
   rsprintf("</table>\n");

   if (isparam("redir"))
      rsprintf("<input type=hidden name=\"redir\" value=\"%s\">\n", getparam("redir"));

   page_footer(TRUE);

}

/*------------------------------------------------------------------*/

void show_odb_page(char *enc_path, int enc_path_size, char *dec_path, int write_access)
{
   int i, j, keyPresent, scan, size, status, line;
   char str[256], tmp_path[256], url_path[256],
      hex_str[256], ref[256], keyname[32], link_name[256], link_ref[256],
      full_path[256], root_path[256], odb_path[256], colspan, style[32];
   char *p;
   char *pd;
   HNDLE hDB, hkey, hkeyroot;
   KEY key;
   DWORD delta;

   cm_get_experiment_database(&hDB, NULL);

   //printf("enc_path [%s] dec_path [%s]\n", enc_path, dec_path);

   if (strcmp(enc_path, "root") == 0) {
      strcpy(enc_path, "");
      strcpy(dec_path, "");
   }

   strlcpy(str, dec_path, sizeof(str));
   if (strrchr(str, '/'))
      strlcpy(str, strrchr(str, '/')+1, sizeof(str));
   if (str[0] == 0)
      strlcpy(str, "root", sizeof(str));
   show_header("MIDAS online database", "", str, 0);

#if 0
   /* add one "../" for each level */
   tmp_path[0] = 0;
   for (p = dec_path ; *p ; p++)
      if (*p == '/')
         strlcat(tmp_path, "../", sizeof(tmp_path));
   strlcat(tmp_path, "../", sizeof(tmp_path));
   strlcat(tmp_path, get_js_filename(), sizeof(tmp_path));
   rsprintf("<script type=\"text/javascript\" src=\"%s\"></script>\n", tmp_path);
#endif

   /* use javascript file */
   rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"obsolete.js\"></script>\n");

   /* find key via path */
   status = db_find_key(hDB, 0, dec_path, &hkeyroot);
   if (status != DB_SUCCESS) {
      rsprintf("Error: cannot find key %s<P>\n", dec_path);
      rsprintf("</body></html>\r\n");
      return;
   }

   /* if key is not of type TID_KEY, cut off key name */
   db_get_key(hDB, hkeyroot, &key);
   if (key.type != TID_KEY) {
      /* strip variable name from path */
      p = dec_path + strlen(dec_path) - 1;
      while (*p && *p != '/')
         *p-- = 0;
      if (*p == '/')
         *p = 0;

      strlcpy(enc_path, dec_path, enc_path_size);
      urlEncode(enc_path, enc_path_size);

      status = db_find_key(hDB, 0, dec_path, &hkeyroot);
      if (status != DB_SUCCESS) {
         rsprintf("Error: cannot find key %s<P>\n", dec_path);
         rsprintf("</body></html>\r\n");
         return;
      }
   }

   /*---- navigation bar ----*/

   colspan = 7;

   if (elog_mode) {
      rsprintf("<table class=\"headerTable\">\n");
      rsprintf("<tr><td colspan=%d>\n", colspan);
      rsprintf("<input type=button value=ELog onclick=\"self.location=\'?cmd=Alarms\';\">\n");
      rsprintf("</td></tr></table>\n\n");
   } else
      show_navigation_bar("ODB");

   /*---- begin ODB directory table ----*/

   rsprintf("<table class=\"ODBtable\" style=\"border-spacing:0px;\">\n");
   rsprintf("<tr><th colspan=%d class=\"subStatusTitle\">Online Database Browser</tr>\n", colspan);
   //buttons:
   if(!elog_mode){
      rsprintf("<tr><td colspan=%d>\n", colspan);
      rsprintf("<input type=button value=Find onclick=\"self.location=\'?cmd=Find\';\">\n");
      rsprintf("<input type=button value=Create onclick=\"self.location=\'?cmd=Create\';\">\n");
      rsprintf("<input type=button value=Delete onclick=\"self.location=\'?cmd=Delete\';\">\n");
      rsprintf("<input type=button value=\"Create Elog from this page\" onclick=\"self.location=\'?cmd=Create Elog from this page\';\"></td></tr>\n");
   }

   /*---- ODB display -----------------------------------------------*/

   /* add one "../" for each level */
   tmp_path[0] = 0;
   for (p = dec_path ; *p ; p++)
      if (*p == '/')
         strlcat(tmp_path, "../", sizeof(tmp_path));

   p = dec_path;
   if (*p == '/')
      p++;

   /* display root key */
   rsprintf("<tr><td colspan=%d class='ODBpath'><b>", colspan);
   rsprintf("<a href=\"%sroot\">/</a> \n", tmp_path);
   strlcpy(root_path, tmp_path, sizeof(root_path));

   /*---- display path ----*/
   while (*p) {
      pd = str;
      while (*p && *p != '/')
         *pd++ = *p++;
      *pd = 0;

      strlcat(tmp_path, str, sizeof(tmp_path));
      strlcpy(url_path, tmp_path, sizeof(url_path));
      urlEncode(url_path, sizeof(url_path));

      rsprintf("<a href=\"%s\">%s</a>\n / ", url_path, str);

      strlcat(tmp_path, "/", sizeof(tmp_path));
      if (*p == '/')
         p++;
   }
   rsprintf("</b></tr>\n");

   /* enumerate subkeys */
   keyPresent = 0;
   for(scan=0; scan<2; scan++){
      if(scan==1 && keyPresent==1) {
         rsprintf("<tr class=\"titleRow\">\n");
         rsprintf("<th class=\"ODBkey\">Key</th>\n");
         rsprintf("<th class=\"ODBvalue\">Value&nbsp;");
         rsprintf("<script type=\"text/javascript\">\n");
         rsprintf("function expand()\n");
         rsprintf("{\n");
         rsprintf("  var n = document.getElementsByName('ext');\n");
         rsprintf("  for (i=0 ; i<n.length ; i++) {\n");
         rsprintf("    if (n[i].style.display == 'none')\n");
         rsprintf("       n[i].style.display = 'table-cell';\n");
         rsprintf("    else\n");
         rsprintf("       n[i].style.display = 'none';\n");
         rsprintf("  }\n");
         rsprintf("  if (document.getElementById('expp').innerHTML == '-')\n");
         rsprintf("    document.getElementById('expp').innerHTML = '+';\n");
         rsprintf("  else\n");
         rsprintf("    document.getElementById('expp').innerHTML = '-';\n");
         rsprintf("}\n");
         rsprintf("</script>");
         rsprintf("<div style=\"display:inline;float:right\"><a id=\"expp\"href=\"#\" onClick=\"expand();return false;\">+</div>");
         rsprintf("</th>\n");
         rsprintf("<th class=\"ODBvalue\" name=\"ext\" style=\"display:none\">Type</th>\n");
         rsprintf("<th class=\"ODBvalue\" name=\"ext\" style=\"display:none\">#Val</th>\n");
         rsprintf("<th class=\"ODBvalue\" name=\"ext\" style=\"display:none\">Size</th>\n");
         rsprintf("<th class=\"ODBvalue\" name=\"ext\" style=\"display:none\">Written</th>\n");
         rsprintf("<th class=\"ODBvalue\" name=\"ext\" style=\"display:none\">Mode</th>\n");
         rsprintf("</tr>\n");
      }
      line = 0;
      for (i = 0;; i++) {
         db_enum_link(hDB, hkeyroot, i, &hkey);
         if (!hkey)
            break;
         db_get_link(hDB, hkey, &key);

         if (line % 2 == 0)
            strlcpy(style, "ODBtableEven", sizeof(style));
         else
            strlcpy(style, "ODBtableOdd", sizeof(style));

         if (strrchr(dec_path, '/'))
            strlcpy(str, strrchr(dec_path, '/')+1, sizeof(str));
         else
            strlcpy(str, dec_path, sizeof(str));
         if (str[0] && str[strlen(str) - 1] != '/')
            strlcat(str, "/", sizeof(str));
         strlcat(str, key.name, sizeof(str));
         strlcpy(full_path, str, sizeof(full_path));
         urlEncode(full_path, sizeof(full_path));
         strlcpy(keyname, key.name, sizeof(keyname));
         strlcpy(odb_path, dec_path, sizeof(odb_path));
         if (odb_path[0] && odb_path[strlen(odb_path) - 1] != '/')
            strlcat(odb_path, "/", sizeof(odb_path));
         strlcat(odb_path, key.name, sizeof(odb_path));

         /* resolve links */
         link_name[0] = 0;
         status = DB_SUCCESS;
         if (key.type == TID_LINK) {
            size = sizeof(link_name);
            db_get_link_data(hDB, hkey, link_name, &size, TID_LINK);

            status = db_find_key(hDB, 0, link_name, &hkey);

            if (status == DB_SUCCESS)
               db_get_key(hDB, hkey, &key);

            sprintf(link_ref, "%s?cmd=Set", full_path);

            if (status == DB_SUCCESS && link_name[0] == 0) {
               // fake the case when an empty link somehow resolves
               sprintf(link_name, "%s", "(empty)");
            }
         }

         if (link_name[0]) {
            if (root_path[strlen(root_path)-1] == '/' && link_name[0] == '/')
               sprintf(ref, "%s%s?cmd=Set", root_path, link_name+1);
            else
               sprintf(ref, "%s%s?cmd=Set", root_path, link_name);
         } else
            sprintf(ref, "%s?cmd=Set", full_path);

         if (status != DB_SUCCESS) {
            if (scan == 1) {
               rsprintf("<tr><td class=\"yellowLight\">");
               rsprintf("%s <i>-> <a href=\"%s\">%s</a></i><td><b><font color=\"red\">&lt;cannot resolve link&gt;</font></b></tr>\n", keyname, link_ref, link_name[0]?link_name:"(empty)");
            }
         } else {

            if (key.type == TID_KEY && scan == 0) {
               /* for keys, don't display data value */
               rsprintf("<tr><td colspan=%d class=\"ODBdirectory\"><a href=\"%s\">&#x25B6 %s</a>\n", colspan, full_path, keyname);
               if (link_name[0])
                  rsprintf("<i>-> <a href=\"%s\">%s</a></i>", link_ref, link_name);
               rsprintf("</tr>\n");
            } else if(key.type != TID_KEY && scan == 1) {
               /* display single value */
               if (key.num_values == 1) {
                  char data[TEXT_SIZE];
                  char data_str[TEXT_SIZE];
                  size = sizeof(data);
                  db_get_data(hDB, hkey, data, &size, key.type);
                  db_sprintf(data_str, data, key.item_size, 0, key.type);

                  if (key.type == TID_STRING) {
                     if (strlen(data_str) >= MAX_STRING_LENGTH-1) {
                        strlcat(data_str, "...(truncated)", sizeof(data_str));
                     }
                  }

                  if (key.type != TID_STRING)
                     db_sprintfh(hex_str, data, key.item_size, 0, key.type);
                  else
                     hex_str[0] = 0;

                  if (data_str[0] == 0 || equal_ustring(data_str, "<NULL>")) {
                     strcpy(data_str, "(empty)");
                     hex_str[0] = 0;
                  }

                  rsprintf("<tr>\n");
                  if (strcmp(data_str, hex_str) != 0 && hex_str[0]) {
                     if (link_name[0]) {
                        rsprintf("<td class=\"ODBkey\">\n");
                        rsprintf("%s <i>-> ", keyname);
                        rsprintf("<a href=\"%s\">%s</a></i>\n", link_ref, link_name);
                        rsprintf("<td class=\"%s\">\n", style);
                        if (!write_access)
                           rsprintf("<a href=\"%s\" ", ref, odb_path);
                        else {
                           rsprintf("<a href=\"%s\" onClick=\"ODBInlineEdit(this.parentNode,\'%s\');return false;\" ", ref, odb_path);
                           rsprintf("onFocus=\"ODBInlineEdit(this.parentNode,\'%s\');\">%s (%s)</a>\n", odb_path, data_str, hex_str);
                        }
                     } else {
                        rsprintf("<td class=\"ODBkey\">\n");
                        rsprintf("%s<td class=\"%s\">", keyname, style);
                        if (!write_access)
                           rsprintf("<a href=\"%s\">%s (%s)</a> ", ref, data_str, hex_str);
                        else {
                           rsprintf("<a href=\"%s\" onClick=\"ODBInlineEdit(this.parentNode,\'%s\');return false;\" ", ref, odb_path);
                           rsprintf("onFocus=\"ODBInlineEdit(this.parentNode,\'%s\');\">%s (%s)</a>\n", odb_path, data_str, hex_str);
                        }
                     }
                  } else {
                     if (strchr(data_str, '\n')) {
                        if (link_name[0]) {
                           rsprintf("<td class=\"ODBkey\">");
                           rsprintf("%s <i>-> <a href=\"%s\">%s</a></i><td class=\"ODBvalue\">", keyname, link_ref, link_name);
                        } else
                           rsprintf("<td class=\"ODBkey\">%s<td class=\"%s\">", keyname, style);
                        rsprintf("\n<pre>");
                        strencode3(data_str);
                        rsprintf("</pre>");
                        if (strlen(data) > strlen(data_str))
                           rsprintf("<i>... (%d bytes total)<p>\n", strlen(data));

                        rsprintf("<a href=\"%s\">Edit</a>\n", ref);
                     } else {
                        if (link_name[0]) {
                           rsprintf("<td class=\"ODBkey\">\n");
                           rsprintf("%s <i>-> <a href=\"%s\">%s</a></i><td class=\"%s\">", keyname, link_ref, link_name, style);
                           if (!write_access)
                              rsprintf("<a href=\"%s\">", ref);
                           else {
                              rsprintf("<a href=\"%s\" onClick=\"ODBInlineEdit(this.parentNode,\'%s\');return false;\" ", ref, odb_path);
                              rsprintf("onFocus=\"ODBInlineEdit(this.parentNode,\'%s\');\">", odb_path);
                           }
                        } else {
                           rsprintf("<td class=\"ODBkey\">%s<td class=\"%s\">", keyname, style);
                           if (!write_access)
                              rsprintf("<a href=\"%s\">", ref);
                           else {
                              rsprintf("<a href=\"%s\" onClick=\"ODBInlineEdit(this.parentNode,\'%s\');return false;\" ", ref, odb_path);
                              rsprintf("onFocus=\"ODBInlineEdit(this.parentNode,\'%s\');\">", odb_path);
                           }
                        }
                        strencode(data_str);
                        rsprintf("</a>\n");
                     }
                  }

                  /* extended key information */
                  rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\">");
                  rsprintf("%s", rpc_tid_name(key.type));
                  rsprintf("</td>\n");

                  rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\">");
                  rsprintf("%d", key.num_values);
                  rsprintf("</td>\n");

                  rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\">");
                  rsprintf("%d", key.item_size);
                  rsprintf("</td>\n");

                  rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\">");
                  db_get_key_time(hDB, hkey, &delta);
                  if (delta < 60)
                     rsprintf("%ds", delta);
                  else if (delta < 3600)
                     rsprintf("%1.0lfm", delta / 60.0);
                  else if (delta < 86400)
                     rsprintf("%1.0lfh", delta / 3600.0);
                  else if (delta < 86400 * 99)
                     rsprintf("%1.0lfh", delta / 86400.0);
                  else
                     rsprintf(">99d");
                  rsprintf("</td>\n");

                  rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\">");
                  if (key.access_mode & MODE_READ)
                     rsprintf("R");
                  if (key.access_mode & MODE_WRITE)
                     rsprintf("W");
                  if (key.access_mode & MODE_DELETE)
                     rsprintf("D");
                  if (key.access_mode & MODE_EXCLUSIVE)
                     rsprintf("E");
                  rsprintf("</td>\n");

                  line++;
                  rsprintf("</tr>\n");
               } else { /* display array value */
                  /* check for exceeding length */
                  if (key.num_values > 1000 && !isparam("all"))
                     rsprintf("<tr><td class=\"ODBkey\">%s<td class=\"%s\"><span style=\"font-style: italic\"><a href=\"?all=1\">... %d values ...</a></span>\n",
                              keyname, style, key.num_values);
                  else {
                     /* display first value */
                     if (link_name[0])
                        rsprintf("<tr><td class=\"ODBkey\" rowspan=%d>%s<br><i>-> %s</i>\n",
                                 key.num_values, keyname, link_name);
                     else
                        rsprintf("<tr><td class=\"ODBkey\" rowspan=%d>%s\n",
                                 key.num_values, keyname);

                     for (j = 0; j < key.num_values; j++) {
                        char data[TEXT_SIZE];
                        char data_str[TEXT_SIZE];

                        if (line % 2 == 0)
                           strlcpy(style, "ODBtableEven", sizeof(style));
                        else
                           strlcpy(style, "ODBtableOdd", sizeof(style));

                        size = sizeof(data);
                        db_get_data_index(hDB, hkey, data, &size, j, key.type);
                        db_sprintf(data_str, data, key.item_size, 0, key.type);
                        db_sprintfh(hex_str, data, key.item_size, 0, key.type);

                        if (key.type == TID_STRING) {
                           hex_str[0] = 0;
                           if (strlen(data_str) >= MAX_STRING_LENGTH-1) {
                              strlcat(data_str, "...(truncated)", sizeof(data_str));
                           }
                        }

                        if (data_str[0] == 0 || equal_ustring(data_str, "<NULL>")) {
                           strcpy(data_str, "(empty)");
                           hex_str[0] = 0;
                        }

                        sprintf(ref, "%s?cmd=Set&index=%d", full_path, j);
                        sprintf(str, "%s[%d]", odb_path, j);

                        if (j > 0)
                           rsprintf("<tr>");

                        rsprintf("<td class=\"%s\">[%d]&nbsp;", style, j);
                        if (!write_access)
                           rsprintf("<a href=\"%s\">", ref);
                        else {
                           rsprintf("<a href=\"%s\" onClick=\"ODBInlineEdit(this.parentNode,\'%s\');return false;\" ", ref, str);
                           rsprintf("onFocus=\"ODBInlineEdit(this.parentNode,\'%s\');\">", str);
                        }
                        if (strcmp(data_str, hex_str) != 0 && hex_str[0])
                           rsprintf("%s (%s)</a>\n", data_str, hex_str);
                        else
                           rsprintf("%s</a>\n", data_str);

                        if (j == 0) {
                           /* extended key information */
                           rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\" rowspan=%d>", key.num_values);
                           rsprintf("%s", rpc_tid_name(key.type));
                           rsprintf("</td>\n");

                           rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\" rowspan=%d>", key.num_values);
                           rsprintf("%d", key.num_values);
                           rsprintf("</td>\n");

                           rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\" rowspan=%d>", key.num_values);
                           rsprintf("%d", key.item_size);
                           rsprintf("</td>\n");

                           rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\" rowspan=%d>", key.num_values);
                           db_get_key_time(hDB, hkey, &delta);
                           if (delta < 60)
                              rsprintf("%ds", delta);
                           else if (delta < 3600)
                              rsprintf("%1.0lfm", delta / 60.0);
                           else if (delta < 86400)
                              rsprintf("%1.0lfh", delta / 3600.0);
                           else if (delta < 86400 * 99)
                              rsprintf("%1.0lfh", delta / 86400.0);
                           else
                              rsprintf(">99d");
                           rsprintf("</td>\n");

                           rsprintf("<td class=\"ODBkey\" name=\"ext\" style=\"display:none\" rowspan=%d>", key.num_values);
                           if (key.access_mode & MODE_READ)
                              rsprintf("R");
                           if (key.access_mode & MODE_WRITE)
                              rsprintf("W");
                           if (key.access_mode & MODE_DELETE)
                              rsprintf("D");
                           if (key.access_mode & MODE_EXCLUSIVE)
                              rsprintf("E");
                           rsprintf("</td>\n");
                        }
                        line++;
                     }

                     rsprintf("</tr>\n");
                  }
               }
            } else if(key.type != TID_KEY){
               keyPresent = 1;  //flag that we've seen a key on the first pass, and should therefore write the Key / Value headline
            }
         }
      }
   }
   rsprintf("</table>\n");
   page_footer(FALSE);
}

/*------------------------------------------------------------------*/

void show_set_page(char *enc_path, int enc_path_size,
                   char *dec_path, const char *group,
                   int index, const char *value)
{
   int status, size;
   HNDLE hDB, hkey;
   KEY key;
   char* p;
   char data_str[TEXT_SIZE], str[256], eq_name[NAME_LENGTH];
   char data[TEXT_SIZE];

   cm_get_experiment_database(&hDB, NULL);

   /* show set page if no value is given */
   if (!isparam("value") && !*getparam("text")) {
      status = db_find_link(hDB, 0, dec_path, &hkey);
      if (status != DB_SUCCESS) {
         rsprintf("Error: cannot find key %s<P>\n", dec_path);
         return;
      }
      db_get_key(hDB, hkey, &key);

      strlcpy(str, dec_path, sizeof(str));
      if (strrchr(str, '/'))
         strlcpy(str, strrchr(str, '/')+1, sizeof(str));
      show_header("Set value", "POST", str, 0);
      //close header:
      rsprintf("</table>");

      //main table:
      rsprintf("<table class=\"dialogTable\">");

      if (index > 0)
         rsprintf("<input type=hidden name=index value=\"%d\">\n", index);
      else
         index = 0;

      if (group[0])
         rsprintf("<input type=hidden name=group value=\"%s\">\n", group);

      strlcpy(data_str, rpc_tid_name(key.type), sizeof(data_str));
      if (key.num_values > 1) {
         sprintf(str, "[%d]", key.num_values);
         strlcat(data_str, str, sizeof(data_str));

         sprintf(str, "%s[%d]", dec_path, index);
      } else
         strlcpy(str, dec_path, sizeof(str));

      rsprintf("<tr><th colspan=2>Set new value - type = %s</tr>\n",
               data_str);
      rsprintf("<tr><td>%s<td>\n", str);

      /* set current value as default */
      size = sizeof(data);
      db_get_data(hDB, hkey, data, &size, key.type);
      db_sprintf(data_str, data, key.item_size, index, key.type);

      if (equal_ustring(data_str, "<NULL>"))
         data_str[0] = 0;

      if (strchr(data_str, '\n') != NULL) {
         rsprintf("<textarea rows=20 cols=80 name=\"text\">\n");
         strencode3(data);
         rsprintf("</textarea>\n");
      } else {
         size = 20;
         if ((int) strlen(data_str) > size)
            size = strlen(data_str) + 3;
         if (size > 80)
            size = 80;

         rsprintf("<input type=\"text\" size=%d maxlength=256 name=\"value\" value=\"", size);
         strencode(data_str);
         rsprintf("\">\n");
      }

      rsprintf("</tr>\n");

      rsprintf("<tr><td align=center colspan=2>");
      rsprintf("<input type=submit name=cmd value=Set>");
      rsprintf("<input type=submit name=cmd value=Cancel>");
      rsprintf("</tr>");
      rsprintf("</table>");

      rsprintf("<input type=hidden name=cmd value=Set>\n");

      page_footer(TRUE);
      return;
   } else {
      /* set value */

      status = db_find_link(hDB, 0, dec_path, &hkey);
      if (status != DB_SUCCESS) {
         rsprintf("Error: cannot find key %s<P>\n", dec_path);
         return;
      }
      db_get_key(hDB, hkey, &key);

      memset(data, 0, sizeof(data));

      if (getparam("text") && *getparam("text"))
         strlcpy(data, getparam("text"), sizeof(data));
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
         rsprintf("<h2>Write access not allowed</h2>\n");

      /* strip variable name from path */
      p = dec_path + strlen(dec_path) - 1;
      while (*p && *p != '/')
         *p-- = 0;
      if (*p == '/')
         *p = 0;

      //strlcpy(enc_path, dec_path, enc_path_size);
      //urlEncode(enc_path, enc_path_size);
      enc_path[0] = 0;

      /* redirect */

      if (group[0]) {
         /* extract equipment name */
         eq_name[0] = 0;
         if (strncmp(enc_path, "Equipment/", 10) == 0) {
            strlcpy(eq_name, enc_path + 10, sizeof(eq_name));
            if (strchr(eq_name, '/'))
               *strchr(eq_name, '/') = 0;
         }

         /* back to SC display */
         sprintf(str, "SC/%s/%s", eq_name, group);
         redirect(str);
      } else
         redirect(enc_path);

      return;
   }

}

/*------------------------------------------------------------------*/

void show_find_page(const char *enc_path, const char *value)
{
   HNDLE hDB, hkey;
   char str[256];

   cm_get_experiment_database(&hDB, NULL);

   if (value[0] == 0) {
      /* without value, show find dialog */
      str[0] = 0;
      for (const char* p=enc_path ; *p ; p++)
         if (*p == '/')
            strlcat(str, "../", sizeof(str));
      show_header("Find value", "GET", str, 0);

      //end header:
      rsprintf("</table>");

      //find dialog:
      rsprintf("<table class=\"dialogTable\">");

      rsprintf("<tr><th colspan=2>Find string in Online Database</tr>\n");
      rsprintf("<tr><td>Enter substring (case insensitive)\n");

      rsprintf("<td><input type=\"text\" size=\"20\" maxlength=\"80\" name=\"value\">\n");
      rsprintf("</tr>");

      rsprintf("<tr><td align=center colspan=2>");
      rsprintf("<input type=submit name=cmd value=Find>");
      rsprintf("<input type=submit name=cmd value=Cancel>");
      rsprintf("</tr>");
      rsprintf("</table>");

      rsprintf("<input type=hidden name=cmd value=Find>");

      page_footer(TRUE);
   } else {
      strlcpy(str, enc_path, sizeof(str));
      if (strrchr(str, '/'))
         strlcpy(str, strrchr(str, '/')+1, sizeof(str));
      show_header("Search results", "GET", str, 0);

      rsprintf("<tr><td colspan=2>\n");
      rsprintf("<input type=submit name=cmd value=Find>\n");
      rsprintf("<input type=submit name=cmd value=ODB>\n");
      rsprintf("<input type=submit name=cmd value=Help>\n");
      rsprintf("</tr>\n\n");

      rsprintf("<tr><th colspan=2>");
      rsprintf("Results of search for substring \"%s\"</tr>\n", value);
      rsprintf("<tr><th>Key<th>Value</tr>\n");

      /* start from root */
      db_find_key(hDB, 0, "", &hkey);
      assert(hkey);

      /* scan tree, call "search_callback" for each key */
      db_scan_tree(hDB, hkey, 0, search_callback, (void *) value);

      rsprintf("</table>");
      page_footer(TRUE);
   }
}

/*------------------------------------------------------------------*/

void show_create_page(const char *enc_path, const char *dec_path, const char *value, int index, int type)
{
   char str[256], link[256], error[256], *p;
   char data[10000];
   int status;
   HNDLE hDB, hkey;
   KEY key;

   cm_get_experiment_database(&hDB, NULL);

   if (value[0] == 0) {
      /* without value, show create dialog */

      strlcpy(str, enc_path, sizeof(str));
      if (strrchr(str, '/'))
         strlcpy(str, strrchr(str, '/')+1, sizeof(str));
      show_header("Create ODB entry", "GET", str, 0);
      //close header:
      rsprintf("</table>");

      rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
      rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
      rsprintf("<script type=\"text/javascript\" src=\"obsolete.js\"></script>\n");

      rsprintf("<table class=\"dialogTable\">");
      rsprintf("<tr><th colspan=2>Create ODB entry</tr>\n");

      rsprintf("<tr><td>Type");
      rsprintf("<td><select type=text size=1 name=type>\n");

      rsprintf("<option value=7> Integer (32-bit)\n");
      rsprintf("<option value=9> Float (4 Bytes)\n");
      rsprintf("<option value=12> String\n");
      //rsprintf("<option value=13> Multi-line String\n");
      rsprintf("<option value=15> Subdirectory\n");

      rsprintf("<option value=1> Byte\n");
      rsprintf("<option value=2> Signed byte\n");
      rsprintf("<option value=3> Character (8-bit)\n");
      rsprintf("<option value=4> Word (16-bit)\n");
      rsprintf("<option value=5> Short integer(16-bit)\n");
      rsprintf("<option value=6> Double Word (32-bit)\n");
      rsprintf("<option value=8> Boolean\n");
      rsprintf("<option value=10> Double float(8 Bytes)\n");
      rsprintf("<option value=16> Symbolic link\n");

      rsprintf("</select></tr>\n");

      rsprintf("<tr><td>Name");
      rsprintf("<td><input type=text size=20 maxlength=80 name=value>\n");
      rsprintf("</tr>");

      rsprintf("<tr><td>Array size");
      rsprintf("<td><input type=text size=20 maxlength=80 name=index value=1>\n");
      rsprintf("</tr>");

      rsprintf("<tr><td>String size");
      rsprintf("<td><input type=text size=20 maxlength=80 name=strlen value=32>\n");
      rsprintf("</tr>");

      rsprintf("<tr><td align=center colspan=2>");

      if (1) {
         char str[256];

         if (strcmp(dec_path, "root") == 0) {
            strcpy(str, "");
         } else {
            if (dec_path[0] != '/')
               strcpy(str, "/");
            else
               str[0] = 0;
            strlcat(str, dec_path, sizeof(str));
         }
         rsprintf("<input type=hidden name=odb value=\"%s\">\n", str);
      }

      //rsprintf("<input type=submit name=cmd value=Create>\n");
      rsprintf("<input type=button value=Create onClick=\'mhttpd_create_page_handle_create(event);\'>\n");
      //rsprintf("<input type=submit name=cmd value=Cancel>\n");
      rsprintf("<input type=button value=Cancel onClick=\'mhttpd_create_page_handle_cancel(event);\'>\n");
      rsprintf("</tr>");
      rsprintf("</table>");

      page_footer(TRUE);
   } else {
      if (type == TID_LINK) {
         /* check if destination exists */
         status = db_find_key(hDB, 0, value, &hkey);
         if (status != DB_SUCCESS) {
            rsprintf("<h1>Error: Link destination \"%s\" does not exist!</h1>", value);
            return;
         }

         /* extract key name from destination */
         strlcpy(str, value, sizeof(str));
         p = str + strlen(str) - 1;
         while (*p && *p != '/')
            p--;
         p++;

         /* use it as link name */
         strlcpy(link, p, sizeof(link));

         strlcpy(str, dec_path, sizeof(str));
         if (str[strlen(str) - 1] != '/')
            strlcat(str, "/", sizeof(str));
         strlcat(str, link, sizeof(str));

         status = db_create_link(hDB, 0, str, value);
         if (status != DB_SUCCESS) {
            sprintf(error, "Cannot create key %s</h1>\n", str);
            show_error(error);
            return;
         }

      } else {
         if (dec_path[0] != '/')
            strcpy(str, "/");
         else
            str[0] = 0;
         strlcat(str, dec_path, sizeof(str));
         if (str[strlen(str) - 1] != '/')
            strlcat(str, "/", sizeof(str));
         strlcat(str, value, sizeof(str));

         if (type == TID_ARRAY)
            /* multi-line string */
            status = db_create_key(hDB, 0, str, TID_STRING);
         else
            status = db_create_key(hDB, 0, str, type);
         if (status != DB_SUCCESS) {
            sprintf(error, "Cannot create key %s</h1>\n", str);
            show_error(error);
            return;
         }

         db_find_key(hDB, 0, str, &hkey);
         assert(hkey);
         db_get_key(hDB, hkey, &key);
         memset(data, 0, sizeof(data));
         if (key.type == TID_STRING || key.type == TID_LINK)
            key.item_size = NAME_LENGTH;
         if (type == TID_ARRAY)
            strcpy(data, "\n");

         if (index > 1)
            db_set_data_index(hDB, hkey, data, key.item_size, index - 1, key.type);
         else if (key.type == TID_STRING || key.type == TID_LINK)
            db_set_data(hDB, hkey, data, key.item_size, 1, key.type);
      }

      /* redirect */
      strlcpy(str, enc_path, sizeof(str));
      if (strrchr(str, '/'))
         strlcpy(str, strrchr(str, '/')+1, sizeof(str));
      redirect(str);
      return;
   }
}

/*------------------------------------------------------------------*/

void show_delete_page(const char *enc_path, const char *dec_path, const char *value, int index)
{
   char str[256];
   char path[256];
   int i, status;
   HNDLE hDB, hkeyroot, hkey;
   KEY key;

   cm_get_experiment_database(&hDB, NULL);

   if (value[0] == 0) {
      /* without value, show delete dialog */

      strlcpy(str, enc_path, sizeof(str));
      if (strrchr(str, '/'))
         strlcpy(str, strrchr(str, '/')+1, sizeof(str));
      show_header("Delete ODB entry", "GET", str, 0);
      //close header
      rsprintf("</table>");

      rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
      rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
      rsprintf("<script type=\"text/javascript\" src=\"obsolete.js\"></script>\n");

      rsprintf("<table class=\"dialogTable\">");
      rsprintf("<tr><th colspan=2>Delete ODB entries:</tr>\n");

      if (strcmp(dec_path, "root") == 0) {
         strlcpy(path, "/", sizeof(path));
      } else {
         if (dec_path[0] != '/')
            strcpy(path, "/");
         else
            path[0] = 0;
         strlcat(path, dec_path, sizeof(str));
      }

      /* find key via from */
      status = db_find_key(hDB, 0, path, &hkeyroot);
      if (status != DB_SUCCESS) {
         rsprintf("Error: cannot find key \'%s\'<p>\n", path);
         page_footer(TRUE);
         return;
      }

      int count = 0;

      /* enumerate subkeys */
      for (i = 0;; i++) {
         db_enum_link(hDB, hkeyroot, i, &hkey);
         if (!hkey)
            break;
         db_get_link(hDB, hkey, &key);

         rsprintf("<tr><td style=\"text-align:left;\" align=left><input align=left type=checkbox name=\"name%d\" value=\"%s\">%s</input></td></tr>\n", i, key.name, key.name);
         count ++;
      }

      rsprintf("</select></tr>\n");

      if (count == 0) {
         rsprintf("<tr><td>This directory is empty, nothing to delete</td></tr>\n");
      }

      rsprintf("<tr><td align=center colspan=2>");

      rsprintf("<input type=hidden name=odb value=\"%s\">\n", path);

      if (count != 0) {
         rsprintf("<input type=button value=Delete onClick=\'mhttpd_delete_page_handle_delete(event);\'>\n");
      }
      rsprintf("<input type=button value=Cancel onClick=\'mhttpd_delete_page_handle_cancel(event);\'>\n");

      rsprintf("</tr>");
      rsprintf("</table>");

      page_footer(TRUE);
   } else {
      strlcpy(str, dec_path, sizeof(str));
      if (str[strlen(str) - 1] != '/')
         strlcat(str, "/", sizeof(str));
      strlcat(str, value, sizeof(str));

      status = db_find_link(hDB, 0, str, &hkey);
      if (status != DB_SUCCESS) {
         rsprintf("<h1>Cannot find key %s</h1>\n", str);
         return;
      }

      status = db_delete_key(hDB, hkey, FALSE);
      if (status != DB_SUCCESS) {
         rsprintf("<h1>Cannot delete key %s</h1>\n", str);
         return;
      }

      /* redirect */
      strlcpy(str, enc_path, sizeof(str));
      if (strrchr(str, '/'))
         strlcpy(str, strrchr(str, '/')+1, sizeof(str));
      redirect(str);
      return;
   }
}

/*------------------------------------------------------------------*/

#ifdef OBSOLETE
void show_alarm_page()
{
   INT i, size, triggered, type, index, ai;
   BOOL active;
   HNDLE hDB, hkeyroot, hkey;
   KEY key;
   char str[256], ref[256], condition[256], value[256];
   time_t last, interval;
   INT al_list[] = { AT_EVALUATED, AT_PROGRAM, AT_INTERNAL, AT_PERIODIC };

   cm_get_experiment_database(&hDB, NULL);

   show_header("Alarms", "GET", "./", 0);
   rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"obsolete.js\"></script>\n");
   show_navigation_bar("Alarms");

   /*---- menu buttons ----*/
   rsprintf("<table>");   //main table
   rsprintf("<tr>\n");
   rsprintf("<td colspan=7 style=\"margin:0px; padding:0px;\">\n");

   rsprintf("<input type=submit name=cmd value=\"Reset all alarms\">\n");
   rsprintf("<input type=submit name=cmd value=\"Alarms on/off\">\n");
   rsprintf("<input type=\"checkbox\" name=\"caspeak\" id=\"aspeak\" onClick=\"return aspeak_click(this);\"><span id=\"aspeakLabel\">Audio</span></td>");

   rsprintf("</tr></table>\n\n");  //used to end with an extra form closure tag, messes up the footer.
   rsprintf("<script type=\"text/javascript\">alarm_load();</script>\n");

   /*---- global flag ----*/
   active = TRUE;
   rsprintf("<table id=\"statusTable\" style=\"padding-top:0px;\">\n");
   size = sizeof(active);
   db_get_value(hDB, 0, "/Alarms/Alarm System active", &active, &size, TID_BOOL, TRUE);
   if (!active) {
      sprintf(ref, "Alarms/Alarm System active?cmd=set");
      rsprintf("<tr><td align=center colspan=7 class=\"redLight\"><a href=\"%s\"><h1>Alarm system disabled</h1></a></tr>",
           ref);
   }

   /*---- alarms ----*/

   for (ai = 0; ai < AT_LAST; ai++) {
      index = al_list[ai];

      if (index == AT_EVALUATED) {
         rsprintf("<tr><td colspan=7><table class=\"alarmTable\" width=100%%><tr><th align=center colspan=7 class=\"subStatusTitle\">Evaluated alarms</tr>\n");
         rsprintf("<tr class=\"titleRow\"><th>Alarm<th>State<th>First triggered<th>Class<th>Condition<th>Current value<th></tr>\n");
      } else if (index == AT_PROGRAM) {
         rsprintf("<tr><td colspan=7><table class=\"alarmTable\" width=100%%><tr><th align=center colspan=7 class=\"subStatusTitle\">Program alarms</tr>\n");
         rsprintf("<tr class=\"titleRow\"><th>Alarm<th>State<th>First triggered<th>Class<th colspan=2>Condition<th></tr>\n");
      } else if (index == AT_INTERNAL) {
         rsprintf("<tr><td colspan=7><table class=\"alarmTable\" width=100%%><tr><th align=center colspan=7 class=\"subStatusTitle\">Internal alarms</tr>\n");
         rsprintf("<tr class=\"titleRow\"><th>Alarm<th>State<th>First triggered<th>Class<th colspan=2>Condition/Message<th></tr>\n");
      } else if (index == AT_PERIODIC) {
         rsprintf("<tr><td colspan=7><table class=\"alarmTable\" width=100%%><tr><th align=center colspan=7 class=\"subStatusTitle\">Periodic alarms</tr>\n");
         rsprintf("<tr class=\"titleRow\"><th>Alarm<th>State<th>First triggered<th>Class<th colspan=2>Time/Message<th></tr>\n");
      }

      /* go through all alarms */
      db_find_key(hDB, 0, "/Alarms/Alarms", &hkeyroot);
      if (hkeyroot) {
         for (i = 0;; i++) {
            db_enum_link(hDB, hkeyroot, i, &hkey);

            if (!hkey)
               break;

            db_get_key(hDB, hkey, &key);

            /* type */
            size = sizeof(INT);
            db_get_value(hDB, hkey, "Type", &type, &size, TID_INT, TRUE);
            if (type != index)
               continue;

            /* start form for each alarm to make "reset" button work */
            sprintf(ref, "%s", key.name);

            rsprintf("<form method=\"GET\" action=\"%s\">\n", ref);

            /* alarm name */
            sprintf(ref, "Alarms/Alarms/%s", key.name);
            rsprintf("<tr><td><a href=\"%s\"><b>%s</b></a>", ref,
                     key.name);

            /* state */
            size = sizeof(BOOL);
            db_get_value(hDB, hkey, "Active", &active, &size, TID_BOOL, TRUE);
            size = sizeof(INT);
            db_get_value(hDB, hkey, "Triggered", &triggered, &size, TID_INT, TRUE);
            if (!active)
               rsprintf("<td class=\"yellowLight\" align=center>Disabled");
            else {
               if (!triggered)
                  rsprintf("<td class=\"greenLight\" align=center>OK");
               else
                  rsprintf("<td class=\"redLight\" align=center>Triggered");
            }

            /* time */
            size = sizeof(str);
            db_get_value(hDB, hkey, "Time triggered first", str, &size, TID_STRING, TRUE);
            if (!triggered)
               strcpy(str, "-");
            rsprintf("<td align=center>%s", str);

            /* class */
            size = sizeof(str);
            db_get_value(hDB, hkey, "Alarm Class", str, &size, TID_STRING, TRUE);

            sprintf(ref, "Alarms/Classes/%s", str);
            rsprintf("<td align=center><a href=\"%s\">%s</a>", ref, str);

            /* condition */
            size = sizeof(condition);
            db_get_value(hDB, hkey, "Condition", condition, &size, TID_STRING, TRUE);

            if (index == AT_EVALUATED) {
               /* print condition */
               rsprintf("<td>");
               strencode(condition);

               /* retrieve value */
               al_evaluate_condition(condition, value);
               rsprintf("<td align=center>%s", value);
            } else if (index == AT_PROGRAM) {
               /* print condition */
               rsprintf("<td colspan=2>");
               strencode(condition);
            } else if (index == AT_INTERNAL) {
               size = sizeof(str);
               if (triggered)
                  db_get_value(hDB, hkey, "Alarm message", str, &size, TID_STRING, TRUE);
               else
                  db_get_value(hDB, hkey, "Condition", str, &size, TID_STRING, TRUE);

               rsprintf("<td colspan=2>%s", str);
            } else if (index == AT_PERIODIC) {
               size = sizeof(str);
               if (triggered)
                  db_get_value(hDB, hkey, "Alarm message", str, &size, TID_STRING, TRUE);
               else {
                  size = sizeof(last);
                  db_get_value(hDB, hkey, "Checked last", &last, &size, TID_DWORD, TRUE);
                  if (last == 0) {
                     last = ss_time();
                     db_set_value(hDB, hkey, "Checked last", &last, size, 1, TID_DWORD);
                  }

                  size = sizeof(interval);
                  db_get_value(hDB, hkey, "Check interval", &interval, &size, TID_INT,
                               TRUE);
                  last += interval;

                  if (ctime(&last) == NULL)
                     strcpy(value, "<invalid>");
                  else
                     strcpy(value, ctime(&last));
                  value[16] = 0;

                  sprintf(str, "Alarm triggers at %s", value);
               }

               rsprintf("<td colspan=2>%s", str);
            }

            rsprintf("<td>\n");
            if (triggered)
               rsprintf("<input type=submit name=cmd value=\"Reset\">\n");
            else
               rsprintf(" &nbsp;&nbsp;&nbsp;&nbsp;");

            rsprintf("</tr>\n");
            rsprintf("</form>\n");

         }
         rsprintf("</table></td></tr>\n"); //closes subTables
      }
   }

   rsprintf("</table>\n"); //closes main table
   page_footer(TRUE);

   //something is closing the top level form with the footer div outside of it; force it back in for now,
   //until the proper closing tag can be chased down:
   //rsprintf("<script>\n");
   //   rsprintf("document.getElementById('wrapper').parentNode.insertBefore(document.getElementsByName('footerDiv'), document.getElementById('wrapper').nextSibling)");
   //rsprintf("</script>\n");

}
#endif

/*------------------------------------------------------------------*/

#ifdef OBSOLETE
void show_programs_page()
{
   INT i, j, k, size, count, status;
   BOOL restart, first, required, client;
   HNDLE hDB, hkeyroot, hkey, hkey_rc, hkeycl;
   KEY key, keycl;
   char str[256], ref[256], command[256], name[80], name_addon[80];

   cm_get_experiment_database(&hDB, NULL);

   /* stop command */
   if (getparam("Stop") && *getparam("Stop")) {
      status = cm_shutdown(getparam("Stop") + 5, FALSE);

      if (status == CM_SUCCESS)
         redirect("./?cmd=programs");
      else {
         sprintf(str,
                 "Cannot shut down client \"%s\", please kill manually and do an ODB cleanup",
                 getparam("Stop") + 5);
         show_error(str);
      }

      return;
   }

   /* start command */
   if (getparam("Start") && *getparam("Start")) {
      /* for NT: close reply socket before starting subprocess */
      redirect2("./?cmd=programs");

      strlcpy(name, getparam("Start") + 6, sizeof(name));
      if (strchr(name, '?'))
         *strchr(name, '?') = 0;
      strlcpy(str, "/Programs/", sizeof(str));
      strlcat(str, name, sizeof(str));
      strlcat(str, "/Start command", sizeof(str));
      command[0] = 0;
      size = sizeof(command);
      db_get_value(hDB, 0, str, command, &size, TID_STRING, FALSE);
      if (command[0]) {
         ss_system(command);
         for (i = 0; i < 50; i++) {
            if (cm_exist(name, FALSE) == CM_SUCCESS)
               break;
            ss_sleep(100);
         }
      }

      return;
   }

   show_header("Programs", "GET", "", 0);
   show_navigation_bar("Programs");

   /* use javascript file */
   rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"obsolete.js\"></script>\n");

   rsprintf("<input type=hidden name=cmd value=Programs>\n");

   /*---- programs ----*/
   rsprintf("<table class=\"subStatusTable\" id=\"stripeList\"><tr><td colspan=5 class=\"subStatusTitle\">Programs</td></tr>");
   rsprintf("<tr class=\"titleRow\"><th>Program<th>Running on host<th>Alarm class<th>Autorestart</tr>\n");

   /* go through all programs */
   db_find_key(hDB, 0, "/Programs", &hkeyroot);
   if (hkeyroot) {
      for (i = 0;; i++) {
         db_enum_key(hDB, hkeyroot, i, &hkey);

         if (!hkey)
            break;

         db_get_key(hDB, hkey, &key);

         /* skip "execute on xxxx" */
         if (key.type != TID_KEY)
            continue;

         sprintf(ref, "Programs/%s", key.name);

         /* required? */
         size = sizeof(required);
         db_get_value(hDB, hkey, "Required", &required, &size, TID_BOOL, TRUE);

         /* running */
         count = 0;
         if (db_find_key(hDB, 0, "/System/Clients", &hkey_rc) == DB_SUCCESS) {
            first = TRUE;
            for (j = 0;; j++) {
               db_enum_key(hDB, hkey_rc, j, &hkeycl);
               if (!hkeycl)
                  break;

               size = sizeof(name);
               memset(name, 0, size);
               db_get_value(hDB, hkeycl, "Name", name, &size, TID_STRING, TRUE);

               db_get_key(hDB, hkeycl, &keycl);

               // check if client name is longer than the program name
               // necessary for multiple started processes which will have names
               // as client_name, client_name1, ...
               client = TRUE;
               if (strlen(name) > strlen(key.name)) {
                  size = strlen(name)-strlen(key.name);
                  memcpy(name_addon, name+strlen(key.name), size);
                  name_addon[size] = 0;
                  for (k=0; k<size; k++) {
                     if (!isdigit(name_addon[k])) {
                        client = FALSE;
                        break;
                     }
                  }
               }
               name[strlen(key.name)] = 0;

               if (equal_ustring(name, key.name) && client) {
                  size = sizeof(str);
                  db_get_value(hDB, hkeycl, "Host", str, &size, TID_STRING, TRUE);

                  if (first) {
                     rsprintf("<tr><td><a href=\"%s\"><b>%s</b></a>", ref,
                              key.name);
                     rsprintf("<td align=center class=\"greenLight\">");
                  }
                  if (!first)
                     rsprintf("<br>");
                  rsprintf(str);

                  first = FALSE;
                  count++;
               }
            }
         }

         if (count == 0 && required) {
            rsprintf("<tr><td><a href=\"%s\"><b>%s</b></a>", ref,
                     key.name);
            rsprintf("<td align=center class=\"redLight\">Not running");
         }

         /* dont display non-running programs which are not required */
         if (count == 0 && !required)
            continue;

         /* Alarm */
         size = sizeof(str);
         db_get_value(hDB, hkey, "Alarm Class", str, &size, TID_STRING, TRUE);
         if (str[0]) {
            sprintf(ref, "Alarms/Classes/%s", str);
            rsprintf("<td class=\"yellowLight\" align=center><a href=\"%s\">%s</a>", ref, str);
         } else
            rsprintf("<td align=center>-");

         /* auto restart */
         size = sizeof(restart);
         db_get_value(hDB, hkey, "Auto restart", &restart, &size, TID_BOOL, TRUE);

         if (restart)
            rsprintf("<td align=center>Yes\n");
         else
            rsprintf("<td align=center>No\n");

         /* start/stop button */
         size = sizeof(str);
         db_get_value(hDB, hkey, "Start Command", str, &size, TID_STRING, TRUE);
         if (str[0] && count == 0) {
            sprintf(str, "Start %s", key.name);
            rsprintf("<td align=center><input type=submit name=\"Start\" value=\"%s\">\n",
                     str);
         }

         if (count > 0 && strncmp(key.name, "mhttpd", 6) != 0) {
            sprintf(str, "Stop %s", key.name);
            rsprintf("<td align=center><input type=submit name=\"Stop\" value=\"%s\">\n",
                     str);
         }

         rsprintf("</tr>\n");
      }
   }

   rsprintf("</table>\n");

   page_footer(TRUE);
}
#endif

/*------------------------------------------------------------------*/

void show_config_page(int refresh)
{
   char str[80];
   HNDLE hDB;

   cm_get_experiment_database(&hDB, NULL);

   show_header("Configure", "GET", "", 0);
   rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   show_navigation_bar("Config");

   //main table
   rsprintf("<table class=\"dialogTable\">");
   rsprintf("<tr><th colspan=2 class=\"subStatusTitle\">Configure</tr>\n");

   rsprintf("<tr><td>Update period\n");

   sprintf(str, "5");
   rsprintf("<td><input type=text size=5 maxlength=5 name=refr value=%d>\n", refresh);
   rsprintf("</tr>\n");

   rsprintf("<tr><td align=center colspan=2>\n");
   rsprintf("<input type=submit name=cmd value=Accept>\n");
   rsprintf("<input type=submit name=cmd value=Cancel>\n");
   rsprintf("<input type=hidden name=cmd value=Accept>\n");
   rsprintf("</tr>\n");
   rsprintf("</table>\n");

   page_footer(TRUE);
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
   struct tm *tms;
   time_t t_sec;

   t_sec = (time_t) sec;
   tms = localtime(&t_sec);
   strcpy(mon, mname[tms->tm_mon]);
   mon[3] = 0;

   if (force_date) {
      if (base < 600)
         sprintf(result, "%02d %s %02d %02d:%02d:%02d",
                 tms->tm_mday, mon, tms->tm_year % 100, tms->tm_hour, tms->tm_min,
                 tms->tm_sec);
      else if (base < 3600 * 24)
         sprintf(result, "%02d %s %02d %02d:%02d",
                 tms->tm_mday, mon, tms->tm_year % 100, tms->tm_hour, tms->tm_min);
      else
         sprintf(result, "%02d %s %02d", tms->tm_mday, mon, tms->tm_year % 100);
   } else {
      if (base < 600)
         sprintf(result, "%02d:%02d:%02d", tms->tm_hour, tms->tm_min, tms->tm_sec);
      else if (base < 3600 * 3)
         sprintf(result, "%02d:%02d", tms->tm_hour, tms->tm_min);
      else if (base < 3600 * 24)
         sprintf(result, "%02d %s %02d %02d:%02d",
                 tms->tm_mday, mon, tms->tm_year % 100, tms->tm_hour, tms->tm_min);
      else
         sprintf(result, "%02d %s %02d", tms->tm_mday, mon, tms->tm_year % 100);
   }
}

void taxis(gdImagePtr im, gdFont * font, int col, int gcol,
           int x1, int y1, int width, int xr,
           int minor, int major, int text, int label, int grid, double xmin, double xmax)
{
   int dx, x_act, label_dx, major_dx, x_screen, maxwidth;
   int tick_base, major_base, label_base, xs, xl;
   char str[80];
   int base[] = { 1, 5, 10, 60, 300, 600, 1800, 3600, 3600 * 6, 3600 * 12, 3600 * 24, 0 };
   time_t ltime;
   int force_date, d1, d2;
   struct tm *ptms;

   if (xmax <= xmin || width <= 0)
      return;

   /* force date display if xmax not today */
   ltime = ss_time();
   ptms = localtime(&ltime);
   d1 = ptms->tm_mday;
   ltime = (time_t) xmax;
   ptms = localtime(&ltime);
   d2 = ptms->tm_mday;
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
   double base[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000 };

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

const char* time_to_string(time_t t)
{
   static char buf[256];
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

/*------------------------------------------------------------------*/

static MidasHistoryInterface* get_history(bool reset = false)
{
   int status;
   HNDLE hDB;
   static MidasHistoryInterface* mh = NULL;
   static HNDLE mhkey = 0;

   // history reconnect requested by watch callback?

   if (gDoReloadHistory) {
      gDoReloadHistory = false;
      reset = true;
   }

   // disconnect from previous history

   if (reset && mh) {
      mh->hs_disconnect();
      delete mh;
      mh = NULL;
      mhkey = 0;
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
      return mh;

   //printf("mh %p, hKey %d, mhkey %d\n", mh, hKey, mhkey);

   if (mh && hKey == mhkey) // same channel as before
      return mh;

   status = hs_get_history(hDB, hKey, HS_GET_READER|HS_GET_INACTIVE, verbose, &mh);
   if (status != HS_SUCCESS || mh==NULL) {
      cm_msg(MERROR, "get_history", "Cannot configure history, hs_get_history() status %d", status);
      mh = NULL;
      return NULL;
   }

   mhkey = hKey;

   cm_msg(MINFO, "get_history", "Reading history from channel \'%s\' type \'%s\'", mh->name, mh->type);

   return mh;
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
      if (nvars > 0)
         Free();
   }
};

#define READ_HISTORY_DATA         0x1
#define READ_HISTORY_RUNMARKER    0x2
#define READ_HISTORY_LAST_WRITTEN 0x4

int read_history(HNDLE hDB, const char *path, int index, int flags, time_t tstart, time_t tend, time_t scale, HistoryData *data)
{
   HNDLE hkeypanel, hkeydvar, hkey;
   KEY key;
   int n_vars, status;
   int debug = 0;

   //printf("read_history, path %s, index %d, flags 0x%x, start %d, end %d, scale %d, data %p\n", path, index, flags, (int)tstart, (int)tend, (int)scale, data);

   /* connect to history */
   MidasHistoryInterface* mh = get_history();
   if (mh == NULL) {
      //rsprintf(str, "History is not configured\n");
      return HS_FILE_ERROR;
   }

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

   data->Allocate(n_vars+2);

   data->tstart = tstart;
   data->tend = tend;
   data->scale = scale;

   for (int i=0; i<n_vars; i++) {
      if (index != -1 && index != i)
         continue;

      char str[256];
      int size = sizeof(str);
      status = db_get_data_index(hDB, hkeydvar, str, &size, i, TID_STRING);
      if (status != DB_SUCCESS) {
         cm_msg(MERROR, "read_history", "Cannot read tag %d in panel %s, status %d", i, path, status);
         continue;
      }

      /* split varname in event, variable and index: "event/tag[index]" */

      char *p = strchr(str, ':');
      if (!p)
         p = strchr(str, '/');

      if (!p) {
         cm_msg(MERROR, "read_history", "Tag \"%s\" has wrong format in panel \"%s\"", str, path);
         continue;
      }

      *p = 0;

      data->odb_index[data->nvars] = i;
      data->event_names[data->nvars] = STRDUP(str);
      data->var_index[data->nvars] = 0;

      char *q = strchr(p+1, '[');
      if (q) {
         data->var_index[data->nvars] = atoi(q+1);
         *q = 0;
      }

      data->var_names[data->nvars] = STRDUP(p+1);

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

int get_hist_last_written(const char *path, time_t endtime, int index, int want_all, time_t *plastwritten)
{
   HNDLE hDB;
   int status;

   time_t now = ss_time();

   if (endtime == 0)
      endtime = now;

   HistoryData  hsxxx;
   HistoryData* hsdata = &hsxxx;

   cm_get_experiment_database(&hDB, NULL);

   char panel[256];
   strlcpy(panel, path, sizeof(panel));
   if (strstr(panel, ".gif"))
      *strstr(panel, ".gif") = 0;

   double tstart = ss_millitime();

   int flags = READ_HISTORY_LAST_WRITTEN;

   status = read_history(hDB, panel, index, flags, endtime, endtime, 0, hsdata);

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

void generate_hist_graph(const char *path, char *buffer, int *buffer_size,
                         int width, int height,
                         time_t xendtime,
                         int scale,
                         int index,
                         int labels, const char *bgcolor, const char *fgcolor, const char *gridcolor)
{
   HNDLE hDB, hkey, hkeypanel, hkeyeq, hkeydvar, hkeyvars, hkeyroot, hkeynames;
   KEY key;
   gdImagePtr im;
   gdGifBuffer gb;
   int i, j, k, l, n_vars, size, status, r, g, b;
   //int x_marker;
   int length;
   int white, grey, red;
   //int black, ltgrey, green, blue;
   int fgcol, bgcol, gridcol;
   int curve_col[MAX_VARS], state_col[3];
   char str[256], panel[256], *p, odbpath[256];
   INT var_index[MAX_VARS];
   char event_name[MAX_VARS][NAME_LENGTH];
   char tag_name[MAX_VARS][64], var_name[MAX_VARS][NAME_LENGTH], varname[64], key_name[256];
   float factor[MAX_VARS], offset[MAX_VARS];
   BOOL logaxis, runmarker;
   //double xmin, xrange;
   double ymin, ymax;
   double upper_limit[MAX_VARS], lower_limit[MAX_VARS];
   float minvalue = (float) -HUGE_VAL;
   float maxvalue = (float) +HUGE_VAL;
   int show_values = 0;
   int sort_vars = 0;
   int old_vars = 0;
   time_t starttime, endtime;
   int flags;

   time_t now = ss_time();

   if (xendtime == 0)
      xendtime = now;

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
   strlcpy(panel, path, sizeof(panel));
   if (strstr(panel, ".gif"))
      *strstr(panel, ".gif") = 0;
   gdImageString(im, gdFontGiant, width / 2 - (strlen(panel) * gdFontGiant->w) / 2, 2, panel, fgcol);

   /* connect to history */
   MidasHistoryInterface *mh = get_history();
   if (mh == NULL) {
      sprintf(str, "History is not configured, see messages");
      gdImageString(im, gdFontSmall, width / 2 - (strlen(str) * gdFontSmall->w) / 2, height / 2, str, red);
      goto error;
   }

   /* check panel name in ODB */
   sprintf(str, "/History/Display/%s", panel);
   db_find_key(hDB, 0, str, &hkeypanel);
   if (!hkeypanel) {
      sprintf(str, "Cannot find /History/Display/%s in ODB", panel);
      gdImageString(im, gdFontSmall, width / 2 - (strlen(str) * gdFontSmall->w) / 2, height / 2, str, red);
      goto error;
   }

   db_find_key(hDB, hkeypanel, "Variables", &hkeydvar);
   if (!hkeydvar) {
      sprintf(str, "Cannot find /History/Display/%s/Variables in ODB", panel);
      gdImageString(im, gdFontSmall, width / 2 - (strlen(str) * gdFontSmall->w) / 2, height / 2, str, red);
      goto error;
   }

   db_get_key(hDB, hkeydvar, &key);
   n_vars = key.num_values;

   if (n_vars > MAX_VARS) {
      sprintf(str, "Too many variables in panel %s", panel);
      gdImageString(im, gdFontSmall, width / 2 - (strlen(str) * gdFontSmall->w) / 2, height / 2, str, red);
      goto error;
   }

   ymin = ymax = 0;
   logaxis = runmarker = 0;

   for (i = 0; i < n_vars; i++) {
      if (index != -1 && index != i)
         continue;

      size = sizeof(str);
      status = db_get_data_index(hDB, hkeydvar, str, &size, i, TID_STRING);
      if (status != DB_SUCCESS) {
         sprintf(str, "Cannot read tag %d in panel %s, status %d", i, panel, status);
         gdImageString(im, gdFontSmall, width / 2 - (strlen(str) * gdFontSmall->w) / 2, height / 2, str, red);
         goto error;
      }

      strlcpy(tag_name[i], str, sizeof(tag_name[0]));

      /* split varname in event, variable and index */
      char *tp = strchr(tag_name[i], ':');
      if (tp) {
         strlcpy(event_name[i], tag_name[i], sizeof(event_name[0]));
         char *ep = strchr(event_name[i], ':');
         if (ep)
            *ep = 0;
         strlcpy(var_name[i], tp+1, sizeof(var_name[0]));
         var_index[i] = 0;
         char *vp = strchr(var_name[i], '[');
         if (vp) {
            var_index[i] = atoi(vp + 1);
            *vp = 0;
         }
      } else {
         sprintf(str, "Tag \"%s\" has wrong format in panel \"%s\"", tag_name[i], panel);
         gdImageString(im, gdFontSmall, width / 2 - (strlen(str) * gdFontSmall->w) / 2, height / 2, str, red);
         goto error;
      }

      db_find_key(hDB, hkeypanel, "Colour", &hkey);
      if (hkey) {
         size = sizeof(str);
         status = db_get_data_index(hDB, hkey, str, &size, i, TID_STRING);
         if (status == DB_SUCCESS) {
            if (str[0] == '#') {
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
	 }
      }

      /* get timescale */
      if (scale == 0) {
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

      for (j = 0; j < MAX_VARS; j++) {
         factor[j] = 1;
         offset[j] = 0;
      }

      /* get factors */
      size = sizeof(float) * n_vars;
      db_get_value(hDB, hkeypanel, "Factor", factor, &size, TID_FLOAT, TRUE);

      /* get offsets */
      size = sizeof(float) * n_vars;
      db_get_value(hDB, hkeypanel, "Offset", offset, &size, TID_FLOAT, TRUE);

      /* get axis type */
      size = sizeof(logaxis);
      logaxis = 0;
      db_get_value(hDB, hkeypanel, "Log axis", &logaxis, &size, TID_BOOL, TRUE);

      /* get show_values type */
      size = sizeof(show_values);
      show_values = 0;
      db_get_value(hDB, hkeypanel, "Show values", &show_values, &size, TID_BOOL, TRUE);

      /* get sort_vars type */
      size = sizeof(sort_vars);
      sort_vars = 0;
      db_get_value(hDB, hkeypanel, "Sort vars", &sort_vars, &size, TID_BOOL, TRUE);

      /* get old_vars type */
      size = sizeof(old_vars);
      old_vars = 0;
      db_get_value(hDB, hkeypanel, "Show old vars", &old_vars, &size, TID_BOOL, TRUE);

      /* get min value */
      size = sizeof(minvalue);
      minvalue = (float) -HUGE_VAL;
      db_get_value(hDB, hkeypanel, "Minimum", &minvalue, &size, TID_FLOAT, TRUE);

      /* get max value */
      size = sizeof(maxvalue);
      maxvalue = (float) +HUGE_VAL;
      db_get_value(hDB, hkeypanel, "Maximum", &maxvalue, &size, TID_FLOAT, TRUE);

      if ((minvalue == 0) && (maxvalue == 0)) {
         minvalue = (float) -HUGE_VAL;
         maxvalue = (float) +HUGE_VAL;
      }

      /* get runmarker flag */
      size = sizeof(runmarker);
      runmarker = 1;
      db_get_value(hDB, hkeypanel, "Show run markers", &runmarker, &size, TID_BOOL, TRUE);

      /* make ODB path from tag name */
      odbpath[0] = 0;
      db_find_key(hDB, 0, "/Equipment", &hkeyroot);
      if (hkeyroot) {
         for (j = 0;; j++) {
            db_enum_key(hDB, hkeyroot, j, &hkeyeq);

            if (!hkeyeq)
               break;

            db_get_key(hDB, hkeyeq, &key);
            if (equal_ustring(key.name, event_name[i])) {
               /* check if variable is individual key under variables/ */
               sprintf(str, "Variables/%s", var_name[i]);
               db_find_key(hDB, hkeyeq, str, &hkey);
               if (hkey) {
                  sprintf(odbpath, "/Equipment/%s/Variables/%s", event_name[i], var_name[i]);
                  break;
               }

               /* check if variable is in setttins/names array */
               db_find_key(hDB, hkeyeq, "Settings/Names", &hkeynames);
               if (hkeynames) {
                  /* extract variable name and Variables/<key> */
                  strlcpy(str, var_name[i], sizeof(str));
                  p = str + strlen(str) - 1;
                  while (p > str && *p != ' ')
                     p--;
                  strlcpy(key_name, p + 1, sizeof(key_name));
                  *p = 0;
                  strlcpy(varname, str, sizeof(varname));

                  /* find key in single name array */
                  db_get_key(hDB, hkeynames, &key);
                  for (k = 0; k < key.num_values; k++) {
                     size = sizeof(str);
                     db_get_data_index(hDB, hkeynames, str, &size, k, TID_STRING);
                     if (equal_ustring(str, varname)) {
                        sprintf(odbpath, "/Equipment/%s/Variables/%s[%d]", event_name[i], key_name, k);
                        break;
                     }
                  }
               } else {
                  /* go through /variables/<name> entries */
                  db_find_key(hDB, hkeyeq, "Variables", &hkeyvars);
                  if (hkeyvars) {
                     for (k = 0;; k++) {
                        db_enum_key(hDB, hkeyvars, k, &hkey);

                        if (!hkey)
                           break;

                        /* find "settins/names <key>" for this key */
                        db_get_key(hDB, hkey, &key);

                        /* find key in key_name array */
                        strlcpy(key_name, key.name, sizeof(key_name));
                        sprintf(str, "Settings/Names %s", key_name);

                        db_find_key(hDB, hkeyeq, str, &hkeynames);
                        if (hkeynames) {
                           db_get_key(hDB, hkeynames, &key);
                           for (l = 0; l < key.num_values; l++) {
                              size = sizeof(str);
                              db_get_data_index(hDB, hkeynames, str, &size, l, TID_STRING);
                              if (equal_ustring(str, var_name[i])) {
                                 sprintf(odbpath, "/Equipment/%s/Variables/%s[%d]", event_name[i], key_name, l);
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
                  db_enum_link(hDB, hkeyroot, j, &hkey);

                  if (!hkey)
                     break;

                  db_get_key(hDB, hkey, &key);
                  if (equal_ustring(key.name, event_name[i])) {
                     db_enum_key(hDB, hkeyroot, j, &hkey);
                     db_find_key(hDB, hkey, var_name[i], &hkey);
                     if (hkey) {
                        db_get_key(hDB, hkey, &key);
                        db_get_path(hDB, hkey, odbpath, sizeof(odbpath));
                        if (key.num_values > 1)
                           sprintf(odbpath + strlen(odbpath), "[%d]", var_index[i]);
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
      if (odbpath[0] && hkeyroot) {
         for (j = 0;; j++) {
            db_enum_key(hDB, hkeyroot, j, &hkey);

            if (!hkey)
               break;

            size = sizeof(str);
            db_get_value(hDB, hkey, "Condition", str, &size, TID_STRING, TRUE);

            if (strstr(str, odbpath)) {
               if (strchr(str, '<')) {
                  p = strchr(str, '<') + 1;
                  if (*p == '=')
                     p++;
                  lower_limit[i] = (factor[i] * atof(p) + offset[i]);
               }
               if (strchr(str, '>')) {
                  p = strchr(str, '>') + 1;
                  if (*p == '=')
                     p++;
                  upper_limit[i] = (factor[i] * atof(p) + offset[i]);
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
   if (runmarker)
      flags |= READ_HISTORY_RUNMARKER;

   status = read_history(hDB, panel, index, flags, starttime, endtime, scale/1000+1, hsdata);

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
         yy = yy * factor[i] + offset[i];
         
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

   int flag;
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
      flag = 0;
      size = sizeof(flag);
      db_get_value(hDB, hkeypanel, "Zero ylow", &flag, &size, TID_BOOL, TRUE);
      if (flag && ymin > 0)
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
         ymin = (float) (floor(ymin / ybase) * ybase);
         ybase = pow(10, floor(log(ymax) / LN10));
         ymax = (float) ((floor(ymax / ybase) + 1) * ybase);
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
   if (/* DISABLES CODE */ (0) && runmarker) {

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
   if (runmarker) {

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

   for (i = 0; i < n_vars; i++) {
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
      for (i = 0; i < n_vars; i++) {
         if (index != -1 && index != i)
            continue;

         str[0] = 0;
         status = db_find_key(hDB, hkeypanel, "Label", &hkeydvar);
         if (status == DB_SUCCESS) {
            size = sizeof(str);
            status = db_get_data_index(hDB, hkeydvar, str, &size, i, TID_STRING);
         }

         if (status != DB_SUCCESS || strlen(str) < 1) {
            if (factor[i] != 1) {
               if (offset[i] == 0)
                  sprintf(str, "%s * %1.2lG", strchr(tag_name[i], ':') + 1, factor[i]);
               else
                  sprintf(str, "%s * %1.2lG %c %1.5lG", strchr(tag_name[i], ':') + 1,
                          factor[i], offset[i] < 0 ? '-' : '+', fabs(offset[i]));
            } else {
               if (offset[i] == 0)
                  sprintf(str, "%s", strchr(tag_name[i], ':') + 1);
               else
                  sprintf(str, "%s %c %1.5lG", strchr(tag_name[i], ':') + 1,
                          offset[i] < 0 ? '-' : '+', fabs(offset[i]));
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

         if (show_values) {
            char xstr[256];
            if (n_point[i] > 0) {
               sprintf(xstr," = %g", y[i][n_point[i]-1]);
            } else if (hsdata->num_entries[k] > 0) {
               sprintf(xstr," = all data is NaN or INF");
            } else if (hsdata->have_last_written) {
               if (hsdata->last_written[k]) {
                  sprintf(xstr," = last data %s", ctime(&hsdata->last_written[k]));
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
            strlcat(str, xstr, sizeof(str));
         }

         if (strlen(var_status[i]) > 1) {
            char xstr[256];
            sprintf(xstr," (%s)", var_status[i]);
            strlcat(str, xstr, sizeof(str));
         }

         row = index == -1 ? i : 0;

         gdImageFilledRectangle(im,
                                x1 + 10,
                                y2 + 10 + row * (gdFontMediumBold->h + 10),
                                x1 + 10 + strlen(str) * gdFontMediumBold->w + 10,
                                y2 + 10 + row * (gdFontMediumBold->h + 10) +
                                gdFontMediumBold->h + 2 + 2, white);
         gdImageRectangle(im, x1 + 10, y2 + 10 + row * (gdFontMediumBold->h + 10),
                          x1 + 10 + strlen(str) * gdFontMediumBold->w + 10,
                          y2 + 10 + row * (gdFontMediumBold->h + 10) +
                          gdFontMediumBold->h + 2 + 2, curve_col[i]);

         gdImageString(im, gdFontMediumBold,
                       x1 + 10 + 5, y2 + 10 + 2 + row * (gdFontMediumBold->h + 10), str,
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
      rsprintf("HTTP/1.1 200 Document follows\r\n");
      rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());

      rsprintf("Content-Type: image/gif\r\n");
      rsprintf("Content-Length: %d\r\n", length);
      rsprintf("Cache-control: private, max-age=0, no-cache\r\n");
      rsprintf("Expires: Fri, 01-Jan-1983 00:00:00 GMT\r\n\r\n");

      rmemcpy(gb.data, length);
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
   // and we do not know how to initialize the value of tms->tm_isdst.
   // This can cause the output of mktime() to be off by one hour.
   // (Rules for daylight savings time are set by national and local govt and in some locations, changes yearly)
   // (There are no locations with 2 hour or half-hour daylight savings that I know of)
   // (Yes, "man mktime" talks about using "tms->tm_isdst = -1")
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
   time_t t1 = mktime(&tms);
#ifdef OS_WINNT
   struct tm *ptms2 = localtime(&t1);
   memcpy(&tms2, ptms2, sizeof(tms2));
#else
   localtime_r(&t1, &tms2);
#endif
   tms2.tm_year = ptms->tm_year;
   tms2.tm_mon  = ptms->tm_mon;
   tms2.tm_mday = ptms->tm_mday;
   tms2.tm_hour = ptms->tm_hour;
   tms2.tm_min  = ptms->tm_min;
   time_t t2 = mktime(&tms2);
   //printf("t1 %.0f, t2 %.0f, diff %d\n", (double)t1, (double)t2, (int)(t1-t2));
   return t2;
}

/*------------------------------------------------------------------*/

void add_param_to_url(char* buf, int bufsize, const char* name, const char* value)
{
   if (strstr(buf, "?"))
      strlcat(buf, "&", bufsize);
   else
      strlcat(buf, "?", bufsize);
   strlcat(buf, name, bufsize); // FIXME: should be URI-encoded
   strlcat(buf, "=", bufsize);
   strlcat(buf, value, bufsize); // FIXME: should be URI-encoded
}

/*------------------------------------------------------------------*/

void show_query_page(const char *path)
{
   int i;
   HNDLE hDB;
   char str[256], redir[256];

   if (getparam("m1") && *getparam("m1")) {
      struct tm tms;
      memset(&tms, 0, sizeof(struct tm));

      tms.tm_year = atoi(getparam("y1")) % 100;

      strlcpy(str, getparam("m1"), sizeof(str));
      for (i = 0; i < 12; i++)
         if (equal_ustring(str, mname[i]))
            break;
      if (i == 12)
         i = 0;

      tms.tm_mon = i;
      tms.tm_mday = atoi(getparam("d1"));
      tms.tm_hour = atoi(getparam("h1"));

      if (tms.tm_year < 90)
         tms.tm_year += 100;

      time_t ltime_start = mktime_with_dst(&tms);

      memset(&tms, 0, sizeof(struct tm));
      tms.tm_year = atoi(getparam("y2")) % 100;

      strlcpy(str, getparam("m2"), sizeof(str));
      for (i = 0; i < 12; i++)
         if (equal_ustring(str, mname[i]))
            break;
      if (i == 12)
         i = 0;

      tms.tm_mon = i;
      tms.tm_mday = atoi(getparam("d2"));
      tms.tm_hour = atoi(getparam("h2"));

      if (tms.tm_year < 90)
         tms.tm_year += 100;

      time_t ltime_end = mktime_with_dst(&tms);

      if (ltime_end == ltime_start)
         ltime_end += 3600 * 24;

      strcpy(str, path);
      if (strrchr(str, '/'))
         strcpy(str, strrchr(str, '/')+1);
      //sprintf(redir, "%s?scale=%d&offset=%d", str, (int) (ltime_end - ltime_start), MIN((int) (ltime_end - ss_time()), 0));
      sprintf(redir, "%s?scale=%d&time=%s", str, (int) (ltime_end - ltime_start), time_to_string(ltime_end));
      if (isparam("hindex"))
         add_param_to_url(redir, sizeof(redir), "index", getparam("hindex"));
      redirect(redir);
      return;
   }

   cm_get_experiment_database(&hDB, NULL);

   strcpy(str, path);
   if (strrchr(str, '/'))
      strcpy(str, strrchr(str, '/')+1);
   show_header("History", "GET", str, 0);

   /* set the times */

   struct tm *ptms;

   time_t now = time(NULL);

   time_t starttime = now - 3600 * 24;
   time_t endtime = now;
   bool full_day = true;

   if (isparam("htime")) {
      endtime = string_to_time(getparam("htime"));

      if (isparam("hscale")) {
         starttime = endtime - atoi(getparam("hscale"));
         full_day = false;
      } else {
         starttime = endtime - 3600 * 24;
         full_day = false;
      }
   }

   /* menu buttons */
   rsprintf("<tr><td colspan=2>\n");
   rsprintf("<input type=submit name=cmd value=Query>\n");
   rsprintf("<input type=submit name=cmd value=Cancel>\n");
   if (isparam("htime"))
      rsprintf("<input type=hidden name=htime value=%s>\n", getparam("htime"));
   if (isparam("hscale"))
      rsprintf("<input type=hidden name=hscale value=%s>\n", getparam("hscale"));
   if (isparam("hindex"))
      rsprintf("<input type=hidden name=hindex value=%s>\n", getparam("hindex"));
   rsprintf("</tr>\n\n");
   rsprintf("</table>");  //end header

   rsprintf("<table class=\"dialogTable\">");  //main table

   ptms = localtime(&starttime);
   ptms->tm_year += 1900;

   rsprintf("<tr><td nowrap>Start date:</td>", "Start date");

   rsprintf("<td>Month: <select name=\"m1\">\n");
   rsprintf("<option value=\"\">\n");
   for (i = 0; i < 12; i++)
      if (i == ptms->tm_mon)
         rsprintf("<option selected value=\"%s\">%s\n", mname[i], mname[i]);
      else
         rsprintf("<option value=\"%s\">%s\n", mname[i], mname[i]);
   rsprintf("</select>\n");

   rsprintf("&nbsp;Day: <select name=\"d1\">");
   rsprintf("<option selected value=\"\">\n");
   for (i = 0; i < 31; i++)
      if (i + 1 == ptms->tm_mday)
         rsprintf("<option selected value=%d>%d\n", i + 1, i + 1);
      else
         rsprintf("<option value=%d>%d\n", i + 1, i + 1);
   rsprintf("</select>\n");

   int start_hour = ptms->tm_hour;
   if (full_day)
      start_hour = 0;

   rsprintf("&nbsp;Hour: <input type=\"text\" size=5 maxlength=5 name=\"h1\" value=\"%d\">", start_hour);

   rsprintf("&nbsp;Year: <input type=\"text\" size=5 maxlength=5 name=\"y1\" value=\"%d\">", ptms->tm_year);
   rsprintf("</td></tr>\n");

   rsprintf("<tr><td nowrap>End date:</td>");

   ptms = localtime(&endtime);
   ptms->tm_year += 1900;

   rsprintf("<td>Month: <select name=\"m2\">\n");
   rsprintf("<option value=\"\">\n");
   for (i = 0; i < 12; i++)
      if (i == ptms->tm_mon)
         rsprintf("<option selected value=\"%s\">%s\n", mname[i], mname[i]);
      else
         rsprintf("<option value=\"%s\">%s\n", mname[i], mname[i]);
   rsprintf("</select>\n");

   rsprintf("&nbsp;Day: <select name=\"d2\">");
   rsprintf("<option selected value=\"\">\n");
   for (i = 0; i < 31; i++)
      if (i + 1 == ptms->tm_mday)
         rsprintf("<option selected value=%d>%d\n", i + 1, i + 1);
      else
         rsprintf("<option value=%d>%d\n", i + 1, i + 1);
   rsprintf("</select>\n");

   int end_hour = ptms->tm_hour;
   if (full_day)
      end_hour = 24;

   rsprintf("&nbsp;Hour: <input type=\"text\" size=5 maxlength=5 name=\"h2\" value=\"%d\">", end_hour);

   rsprintf("&nbsp;Year: <input type=\"text\" size=5 maxlength=5 name=\"y2\" value=\"%d\">", ptms->tm_year);
   rsprintf("</td></tr>\n");

   rsprintf("</table>\n");
   page_footer(TRUE);
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

struct hist_var_t
{
   std::string event_name;
   std::string tag_name;
   float hist_factor;
   float hist_offset;
   std::string hist_col;
   std::string hist_label;
   int  hist_order;

   hist_var_t() // ctor
   {
      hist_factor = 1;
      hist_offset = 0;
      hist_order = -1;
   }
};

typedef std::vector<hist_var_t> hist_var_list_t;

bool cmp_vars(const hist_var_t &a, const hist_var_t &b)
{
   return a.hist_order < b.hist_order;
}

struct hist_plot_t
{
   std::string timescale;
   float minimum;
   float maximum;
   bool zero_ylow;
   bool log_axis;
   bool show_run_markers;
   bool show_values;

   hist_var_list_t vars;

   hist_plot_t() // ctor
   {
      timescale = "1h";
      minimum = 0;
      maximum = 0;
      zero_ylow = true;
      log_axis = false;
      show_run_markers = true;
      show_values = true;
   }

   void Print() const
   {
      printf("hist plot:\n");
      printf("timescale: %s, minimum: %f, maximum: %f, zero_ylow: %d, log_axis: %d, show_run_markers: %d, show_values: %d\n", timescale.c_str(), minimum, maximum, zero_ylow, log_axis, show_run_markers, show_values);

      for (unsigned i=0; i<vars.size(); i++) {
         printf("var[%d] event [%s][%s] factor %f, offset %f, color [%s] label [%s] order %d\n", i, vars[i].event_name.c_str(), vars[i].tag_name.c_str(), vars[i].hist_factor, vars[i].hist_offset, vars[i].hist_col.c_str(), vars[i].hist_label.c_str(), vars[i].hist_order);
      }
   }

   void LoadFromOdb(HNDLE hDB, const char* path)
   {
      int status;
      HNDLE hDir;

      status = db_find_key(hDB, 0, "/History/Display", &hDir);
      if (status != DB_SUCCESS || !hDir) {
         return;
      }

      status = db_find_key(hDB, hDir, path, &hDir);
      if (status != DB_SUCCESS || !hDir) {
         return;
      }

      int size;
      float val;
      BOOL flag;

      std::string ts = timescale;
      status = db_get_value_string(hDB, hDir, "Timescale", 0, &ts, FALSE);
      if (status == DB_SUCCESS)
         timescale = ts;

      val = minimum;
      size = sizeof(val);
      status = db_get_value(hDB, hDir, "Minimum", &val, &size, TID_FLOAT, FALSE);
      if (status == DB_SUCCESS)
         minimum = val;

      val = maximum;
      size = sizeof(val);
      status = db_get_value(hDB, hDir, "Maximum", &val, &size, TID_FLOAT, FALSE);
      if (status == DB_SUCCESS)
         maximum = val;

      flag = zero_ylow;
      size = sizeof(flag);
      status = db_get_value(hDB, hDir, "Zero ylow", &flag, &size, TID_BOOL, FALSE);
      if (status == DB_SUCCESS)
         zero_ylow = flag;

      flag = log_axis;
      size = sizeof(flag);
      status = db_get_value(hDB, hDir, "Log axis", &flag, &size, TID_BOOL, FALSE);
      if (status == DB_SUCCESS)
         log_axis = flag;

      flag = show_run_markers;
      size = sizeof(flag);
      status = db_get_value(hDB, hDir, "Show run markers", &flag, &size, TID_BOOL, FALSE);
      if (status == DB_SUCCESS)
         show_run_markers = flag;

      flag = show_values;
      size = sizeof(flag);
      status = db_get_value(hDB, hDir, "Show values", &flag, &size, TID_BOOL, FALSE);
      if (status == DB_SUCCESS)
         show_values = flag;

      for (unsigned index=0; ; index++) {
         hist_var_t v;

         char str[256];
         char var_name_odb[256];

         var_name_odb[0] = 0;

         sprintf(str, "/History/Display/%s/Variables", path);
         xdb_get_data_index(hDB, str, var_name_odb, sizeof(var_name_odb), index, TID_STRING);

         if (var_name_odb[0] == 0)
            break;

         //printf("index %d, var_name_odb %s\n", index, var_name_odb);

         char* s = strchr(var_name_odb, ':');

         //printf("index %d, var_name_odb [%s] %p [%s]\n", index, var_name_odb, s, s?s:"");
         
         if (s)
            *s = 0;
         v.event_name = var_name_odb;
         if (s)
            v.tag_name = s+1;
         
         //printf("index %d, var_name_odb [%s] %p [%s]\n", index, var_name_odb, s, s?(s+1):"");
         
         v.hist_factor = 1;
         
         sprintf(str, "/History/Display/%s/Factor", path);
         xdb_get_data_index(hDB, str, &v.hist_factor, sizeof(float), index, TID_FLOAT);
         
         v.hist_offset = 0;
         
         sprintf(str, "/History/Display/%s/Offset", path);
         xdb_get_data_index(hDB, str, &v.hist_offset, sizeof(float), index, TID_FLOAT);
         
         char buf[256];
         
         sprintf(str, "/History/Display/%s/Colour", path);
         xdb_get_data_index(hDB, str, buf, sizeof(buf), index, TID_STRING);
         v.hist_col = buf;
         
         sprintf(str, "/History/Display/%s/Label", path);
         xdb_get_data_index(hDB, str, buf, sizeof(buf), index, TID_STRING);
         v.hist_label = buf;
         
         v.hist_order = NextOrder();
         
         vars.push_back(v);
      }

      //printf("Load from ODB:\n");
      //Print();
   }

   void LoadFromParam()
   {
      timescale        = getparam("timescale");
      minimum = (float) strtod(getparam("minimum"),NULL);
      maximum = (float) strtod(getparam("maximum"),NULL);
      zero_ylow        = *getparam("zero_ylow");
      log_axis         = *getparam("log_axis");
      show_run_markers = *getparam("run_markers");
      show_values      = *getparam("show_values");

      for (unsigned index=0; ; index++) {
         char str[256];
         sprintf(str, "event%d", index);

         //printf("param event %d: [%s] [%s] [%d]\n", index, str, getparam(str), *getparam(str));

         if (!getparam(str) || !*getparam(str))
            break;

         if (*getparam(str) == '/') // "/empty"
            continue;

         hist_var_t v;
         v.event_name = getparam(str);
         
         sprintf(str, "var%d", index);
         v.tag_name = getparam(str);
         
         sprintf(str, "fac%d", index);
         if (getparam(str) && *getparam(str))
            v.hist_factor = (float) atof(getparam(str));
         
         sprintf(str, "ofs%d", index);
         if (getparam(str) && *getparam(str))
            v.hist_offset = (float) atof(getparam(str));
         
         sprintf(str, "col%d", index);
         if (getparam(str) && *getparam(str))
            v.hist_col = getparam(str);
         
         sprintf(str, "lab%d", index);
         if (getparam(str) && *getparam(str))
            v.hist_label = getparam(str);
         
         sprintf(str, "ord%d", index);
         if (getparam(str) && *getparam(str))
            v.hist_order = atoi(getparam(str));
         
         vars.push_back(v);
      }

      /* correctly number newly added variables */
      for (unsigned index=0; index<vars.size(); index++) {
         if (vars[index].hist_order < 0)
            vars[index].hist_order = NextOrder();
      }

      //printf("Load from param:\n");
      //Print();
   }

   void AddSelectedParam()
   {
      int seln = atoi(getparam("seln"));
      for (int i=0; i<seln; i++) {
         char str[256];
         sprintf(str, "sel%d", i);

         std::string par = getparam(str);
	 if (par.length() < 1)
            continue;

         std::string::size_type pos = par.find(':');
         if (pos == std::string::npos)
            continue;

         hist_var_t v;

         v.event_name = par.substr(0, pos);
         v.tag_name = par.substr(pos+1);
         v.hist_factor = 1;
         v.hist_order = NextOrder();

         vars.push_back(v);
      }
   }

   void SaveToOdb(HNDLE hDB, const char* path)
   {
      int status;
      HNDLE hDir;
      BOOL flag;
      float val;
      char str[256];

      status = db_find_key(hDB, 0, "/History/Display", &hDir);
      if (status != DB_SUCCESS || !hDir) {
         return;
      }

      status = db_find_key(hDB, hDir, path, &hDir);
      if (status != DB_SUCCESS || !hDir) {
         status = db_create_key(hDB, 0, path, TID_KEY);
         status = db_find_key(hDB, 0, path, &hDir);
         if (status != DB_SUCCESS || !hDir)
            return;
      }

      STRLCPY(str, timescale.c_str());
      db_set_value(hDB, hDir, "Timescale", str, NAME_LENGTH, 1, TID_STRING);

      val = minimum;
      db_set_value(hDB, hDir, "Minimum", &val, sizeof(val), 1, TID_FLOAT);

      val = maximum;
      db_set_value(hDB, hDir, "Maximum", &val, sizeof(val), 1, TID_FLOAT);

      flag = zero_ylow;
      db_set_value(hDB, hDir, "Zero ylow", &flag, sizeof(flag), 1, TID_BOOL);

      flag = log_axis;
      db_set_value(hDB, hDir, "Log axis", &flag, sizeof(flag), 1, TID_BOOL);

      flag = show_run_markers;
      db_set_value(hDB, hDir, "Show run markers", &flag, sizeof(flag), 1, TID_BOOL);

      flag = show_values;
      db_set_value(hDB, hDir, "Show values", &flag, sizeof(flag), 1, TID_BOOL);

      int index = vars.size();

      if (index == 0) {
         index = 1;
      }

      HNDLE hKey;

      xdb_find_key(hDB, hDir, "Variables", &hKey, TID_STRING, 2*NAME_LENGTH);
      status = db_set_num_values(hDB, hKey, index);
      assert(status == DB_SUCCESS);

      xdb_find_key(hDB, hDir, "Label", &hKey, TID_STRING, NAME_LENGTH);
      status = db_set_num_values(hDB, hKey, index);
      assert(status == DB_SUCCESS);

      xdb_find_key(hDB, hDir, "Colour", &hKey, TID_STRING, NAME_LENGTH);
      status = db_set_num_values(hDB, hKey, index);
      assert(status == DB_SUCCESS);

      xdb_find_key(hDB, hDir, "Factor", &hKey, TID_FLOAT, 0);
      status = db_set_num_values(hDB, hKey, index);
      assert(status == DB_SUCCESS);

      xdb_find_key(hDB, hDir, "Offset", &hKey, TID_FLOAT, 0);
      status = db_set_num_values(hDB, hKey, index);
      assert(status == DB_SUCCESS);

      for (unsigned index=0; index<vars.size(); index++) {
         HNDLE hKey;
         std::string var_name = vars[index].event_name + ":" + vars[index].tag_name;

         sprintf(str, "/History/Display/%s/Variables", path);
         STRLCPY(str, var_name.c_str());
         xdb_find_key(hDB, hDir, "Variables", &hKey, TID_STRING, 2*NAME_LENGTH);
         db_set_data_index(hDB, hKey, str, 2 * NAME_LENGTH, index, TID_STRING);
      
         xdb_find_key(hDB, hDir, "Factor", &hKey, TID_FLOAT, 0);
         db_set_data_index(hDB, hKey, &vars[index].hist_factor, sizeof(float), index, TID_FLOAT);

         xdb_find_key(hDB, hDir, "Offset", &hKey, TID_FLOAT, 0);
         db_set_data_index(hDB, hKey, &vars[index].hist_offset, sizeof(float), index, TID_FLOAT);

         xdb_find_key(hDB, hDir, "Colour", &hKey, TID_STRING, NAME_LENGTH);
         db_set_data_index(hDB, hKey, vars[index].hist_col.c_str(), NAME_LENGTH, index, TID_STRING);

         xdb_find_key(hDB, hDir, "Label", &hKey, TID_STRING, NAME_LENGTH);
         db_set_data_index(hDB, hKey, vars[index].hist_label.c_str(), NAME_LENGTH, index, TID_STRING);
      }
   }

   void DeleteDeleted()
   {
      /* delete variables according to "hist_order" */

      while (1) {
         bool something_deleted = false;
         for (unsigned i=0; i<vars.size(); i++) {
            if (vars[i].hist_order <= 0) {
               vars.erase(vars.begin() + i);
               something_deleted = true;
            }
         }
         if (!something_deleted)
            break;
      }
   }

   void SortVars()
   {
      /* sort variables according to "hist_order" */

      bool need_sort = false;
      for (unsigned i=1; i<vars.size(); i++) {
         if (vars[i-1].hist_order >= vars[i].hist_order) {
            need_sort = true;
         }
      }
      
      if (need_sort) {
         /* sort variables by order */
         std::sort(vars.begin(), vars.end(), cmp_vars);

         /* renumber the variables according to the new sorted order */
         for (unsigned index=0; index<vars.size(); index++)
            vars[index].hist_order = (index+1)*10;
      }
   }

   std::string NextColour()
   {
      const char* colour[] = {
         "#0000FF", "#00C000", "#FF0000", "#00C0C0", "#FF00FF",
         "#C0C000", "#808080", "#80FF80", "#FF8080", "#8080FF", NULL };

      for (int i=0; colour[i]; i++) {
         bool in_use = false;

         for (unsigned j=0; j<vars.size(); j++)
            if (vars[j].hist_col == colour[i]) {
               in_use = true;
               break;
            }

         if (!in_use)
            return colour[i];
      }

      return "#808080";
   }

   int NextOrder()
   {
      int order = 0;
      for (unsigned i=0; i<vars.size(); i++)
         if (vars[i].hist_order > order)
            order = vars[i].hist_order;
      return order + 10;
   }
};

void show_hist_config_page(const char *path, const char *hgroup, const char *panel)
{
   int status, size;
   HNDLE hDB;
   unsigned max_display_events = 20;
   unsigned max_display_tags = 200;
   char str[256], cmd[256];
   hist_plot_t plot;

   cm_get_experiment_database(&hDB, NULL);

   size = sizeof(max_display_events);
   db_get_value(hDB, 0, "/History/MaxDisplayEvents", &max_display_events, &size, TID_INT, TRUE);

   size = sizeof(max_display_tags);
   db_get_value(hDB, 0, "/History/MaxDisplayTags", &max_display_tags, &size, TID_INT, TRUE);

   strlcpy(cmd, getparam("cmd"), sizeof(cmd));

   if (equal_ustring(cmd, "Clear history cache")) {
      strcpy(cmd, "Refresh");
      MidasHistoryInterface* mh = get_history();
      if (mh)
         mh->hs_clear_cache();
   }

   //printf("cmd [%s]\n", cmd);
   //printf("cmdx [%s]\n", getparam("cmdx"));

   if (equal_ustring(cmd, "refresh") || equal_ustring(cmd, "save")) {
      plot.LoadFromParam();
      plot.DeleteDeleted();
   } else {
      plot.LoadFromOdb(hDB, path);
   }

   plot.SortVars();

   if (strlen(getparam("seln")) > 0)
      plot.AddSelectedParam();

   //plot.Print();

   if (cmd[0] && equal_ustring(cmd, "save")) {
      plot.SaveToOdb(hDB, path);

      strlcpy(str, path, sizeof(str));
      if (strrchr(str, '/'))
         strlcpy(str, strrchr(str, '/')+1, sizeof(str));
      redirect(str);
      return;
   }

   if (panel[0]) {
      str[0] = 0;
      for (const char* p=path ; *p ; p++)
         if (*p == '/')
            strlcat(str, "../", sizeof(str));
      strlcat(str, hgroup, sizeof(str));
      strlcat(str, "/", sizeof(str));
      strlcat(str, panel, sizeof(str));
   } else {
      strlcpy(str, path, sizeof(str));
      if (strrchr(str, '/'))
         strlcpy(str, strrchr(str, '/')+1, sizeof(str));
   }
   show_header("History Config", "GET", str, 0);
   rsprintf("</table>");  //close header table

   rsprintf("<table class=\"historyConfigTable\">"); //open main table

   rsprintf("<tr><th colspan=8 class=\"subStatusTitle\">History Panel \"%s / %s\"</th></tr>\n", hgroup, panel);

   /* menu buttons */
   rsprintf("<tr><td colspan=8>\n");
   rsprintf("<input type=submit name=cmd value=Refresh>\n");
   rsprintf("<input type=submit name=cmd value=Save>\n");
   rsprintf("<input type=submit name=cmd value=Cancel>\n");
   rsprintf("<input type=submit name=cmd value=\"Clear history cache\">\n");
   rsprintf("<input type=submit name=cmd value=\"Delete Panel\">\n");
   rsprintf("</td></tr>\n");

   rsprintf("<tr><td colspan=8>\n");

   /* sort_vars */
   int sort_vars = *getparam("sort_vars");
   rsprintf("<input type=checkbox %s name=sort_vars value=1 onclick=\"this.form.submit();\">Sort variable names", sort_vars?"checked":"");

   rsprintf("</td></tr>\n");
   rsprintf("<tr><td colspan=8>\n");

   /* old_vars */
   int old_vars = *getparam("old_vars");
   rsprintf("<input type=checkbox %s name=old_vars value=1 onclick=\"this.form.submit();\">Show deleted and renamed variables", old_vars?"checked":"");

   rsprintf("</td></tr>\n");

   rsprintf("<tr><td colspan=8>\n");
   /* hidden command for refresh */
   rsprintf("<input type=hidden name=cmd value=Refresh>\n");
   rsprintf("<input type=hidden name=panel value=\"%s\">\n", panel);
   rsprintf("<input type=hidden name=group value=\"%s\">\n", hgroup);
   rsprintf("</td></tr>\n");

   rsprintf("<tr><td colspan=8>Time scale: &nbsp;&nbsp;");
   rsprintf("<input type=text name=timescale value=%s></td></tr>\n", plot.timescale.c_str());

   if (plot.zero_ylow)
      rsprintf("<tr><td colspan=8><input type=checkbox checked name=zero_ylow value=1>");
   else
      rsprintf("<tr><td colspan=8><input type=checkbox name=zero_ylow value=1>");
   rsprintf("&nbsp;&nbsp;Zero Ylow</td></tr>\n");

   rsprintf("<tr><td colspan=8>Minimum: &nbsp;&nbsp;<input type=text name=minimum value=%f></td></tr>\n", plot.minimum);
   rsprintf("<tr><td colspan=8>Maximum: &nbsp;&nbsp;<input type=text name=maximum value=%f></td></tr>\n", plot.maximum);

   if (plot.log_axis)
      rsprintf("<tr><td colspan=8><input type=checkbox checked name=log_axis value=1>");
   else
      rsprintf("<tr><td colspan=8><input type=checkbox name=log_axis value=1>");
   rsprintf("&nbsp;&nbsp;Logarithmic Y axis</td></tr>\n");

   if (plot.show_run_markers)
      rsprintf("<tr><td colspan=8><input type=checkbox checked name=run_markers value=1>");
   else
      rsprintf("<tr><td colspan=8><input type=checkbox name=run_markers value=1>");
   rsprintf("&nbsp;&nbsp;Show run markers</td></tr>\n");

   if (plot.show_values)
      rsprintf("<tr><td colspan=8><input type=checkbox checked name=show_values value=1>");
   else
      rsprintf("<tr><td colspan=8><input type=checkbox name=show_values value=1>");
   rsprintf("&nbsp;&nbsp;Show values of variables</td></tr>\n");

   /*---- events and variables ----*/

   /* get display event name */

   MidasHistoryInterface* mh = get_history();
   if (mh == NULL) {
      rsprintf(str, "History is not configured\n");
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

   if (strlen(getparam("cmdx")) > 0) {
      rsprintf("<tr><th colspan=8 class=\"subStatusTitle\">List of available history variables</th></tr>\n");
      rsprintf("<tr><th colspan=1>Sel<th colspan=1>Equipment<th colspan=1>Event<th colspan=1>Variable</tr>\n");

      std::string cmdx = xgetparam("cmdx");
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

         //printf("param [%s] is [%s]\n", qcmd.c_str(), getparam(qcmd.c_str()));

         bool collapsed = true;

         if (cmdx == qcmd)
            collapsed = false;

         if (strlen(getparam(qcmd.c_str())) > 0)
            collapsed = false;

         if (collapsed) {
            if (eqname == xeqname)
               continue;

            rsprintf("<tr align=left>\n");
            rsprintf("<td></td>\n");
            rsprintf("<td>%s</td>\n", eqname.c_str());
            rsprintf("<td><input type=submit name=cmdx value=\"%s\"></td>\n", qcmd.c_str());
            rsprintf("<td>%s</td>\n", "");
            rsprintf("</tr>\n");
            xeqname = eqname;
            continue;
         }

         if (once)
            rsprintf("<tr><input type=hidden name=\"%s\" value=%d></tr>\n", qcmd.c_str(), 1);

         std::string rcmd = "Expand " + events[e];

         //printf("param [%s] is [%s]\n", rcmd.c_str(), getparam(rcmd.c_str()));

         collapsed = true;

         if (cmdx == rcmd)
            collapsed = false;

         if (strlen(getparam(rcmd.c_str())) > 0)
            collapsed = false;

         if (collapsed) {
            rsprintf("<tr align=left>\n");
            rsprintf("<td></td>\n");
            rsprintf("<td>%s</td>\n", eqname.c_str());
            rsprintf("<td>%s</td>\n", events[e].c_str());
            rsprintf("<td><input type=submit name=cmdx value=\"%s\"></td>\n", rcmd.c_str());
            rsprintf("</tr>\n");
            continue;
         }

         rsprintf("<tr><input type=hidden name=\"%s\" value=%d></tr>\n", rcmd.c_str(), 1);

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

		  rsprintf("<tr align=left>\n");
		  rsprintf("<td><input type=checkbox %s name=\"sel%d\" value=\"%s:%s\"></td>\n", checked?"checked":"", i++, events[e].c_str(), tagname);
		  rsprintf("<td>%s</td>\n", eqname.c_str());
		  rsprintf("<td>%s</td>\n", events[e].c_str());
		  rsprintf("<td>%s</td>\n", tagname);
		  rsprintf("</tr>\n");
	       }
	    }
	 }
      }

      rsprintf("<tr>\n");
      rsprintf("<td></td>\n");
      rsprintf("<td>\n");
      rsprintf("<input type=hidden name=seln value=%d>\n", i);
      rsprintf("<input type=submit value=\"Add Selected\">\n");
      rsprintf("</td>\n");
      rsprintf("</tr>\n");
   }

   rsprintf("<tr><th>Col<th>Event<th>Variable<th>Factor<th>Offset<th>Colour<th>Label<th>Order</tr>\n");

   //print_vars(vars);

   unsigned nvars = plot.vars.size();
   for (unsigned index = 0; index <= nvars; index++) {

      rsprintf("<tr>");

      if (index < nvars) {
         if (plot.vars[index].hist_col.length() < 1)
            plot.vars[index].hist_col = plot.NextColour();
         rsprintf("<td style=\"background-color:%s\">&nbsp;<td>\n", plot.vars[index].hist_col.c_str());
      } else {
         rsprintf("<td>&nbsp;<td>\n");
      }

      /* event and variable selection */

      rsprintf("<select name=\"event%d\" size=1 onChange=\"document.form1.submit()\">\n", index);

      /* enumerate events */

      /* empty option */
      rsprintf("<option value=\"/empty\">&lt;empty&gt;\n");

      if (index==nvars) { // last "empty" entry
         for (unsigned e=0; e<events.size(); e++) {
            const char *p = events[e].c_str();
            rsprintf("<option value=\"%s\">%s\n", p, p);
         }
      } else if (events.size() > max_display_events) { // too many events
         rsprintf("<option selected value=\"%s\">%s\n", plot.vars[index].event_name.c_str(), plot.vars[index].event_name.c_str());
         rsprintf("<option>(%d events omitted)\n", events.size());
      } else { // show all events
         bool found = false;
         for (unsigned e=0; e<events.size(); e++) {
            const char *s = "";
            const char *p = events[e].c_str();
            if (equal_ustring(plot.vars[index].event_name.c_str(), p)) {
               s = "selected";
               found = true;
            }
            rsprintf("<option %s value=\"%s\">%s\n", s, p, p);
         }
         if (!found) {
            const char *p = plot.vars[index].event_name.c_str();
            rsprintf("<option selected value=\"%s\">%s\n", p, p);
         }
      }

      rsprintf("</select></td>\n");

      //if (vars[index].hist_order <= 0)
      //vars[index].hist_order = (index+1)*10;

      if (index < nvars) {
         bool found_tag = false;
         std::string selected_tag = plot.vars[index].tag_name;

         rsprintf("<td><select name=\"var%d\">\n", index);

         std::vector<TAG> tags;

         status = mh->hs_get_tags(plot.vars[index].event_name.c_str(), t, &tags);

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
               printf("Event [%s] %d tags\n", plot.vars[index].event_name.c_str(), (int)tags.size());

               for (unsigned v=0; v<tags.size(); v++) {
                 printf("tag[%d] [%s]\n", v, tags[v].name);
               }
            }

            unsigned count_tags = 0;
            for (unsigned v=0; v<tags.size(); v++)
               count_tags += tags[v].n_data;

            //printf("output %d option tags\n", count_tags);

            if (count_tags < max_display_tags) {
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
                       rsprintf("<option selected value=\"%s\">%s\n", tagname.c_str(), tagname.c_str());
                       found_tag = true;
                    }
                    else
                       rsprintf("<option value=\"%s\">%s\n", tagname.c_str(), tagname.c_str());

                    //printf("%d [%s] [%s] [%s][%s] %d\n", index, vars[index].event_name, tagname, vars[index].var_name, selected_var, found_var);
                 }
               }
            }
         }

         if (!found_tag)
            if (plot.vars[index].tag_name.length() > 0)
               rsprintf("<option selected value=\"%s\">%s\n", plot.vars[index].tag_name.c_str(), plot.vars[index].tag_name.c_str());

         rsprintf("</select></td>\n");
         rsprintf("<td><input type=text size=10 maxlength=10 name=\"fac%d\" value=%g></td>\n", index, plot.vars[index].hist_factor);
         rsprintf("<td><input type=text size=10 maxlength=10 name=\"ofs%d\" value=%g></td>\n", index, plot.vars[index].hist_offset);
         rsprintf("<td><input type=text size=10 maxlength=10 name=\"col%d\" value=%s></td>\n", index, plot.vars[index].hist_col.c_str());
         rsprintf("<td><input type=text size=10 maxlength=%d name=\"lab%d\" value=\"%s\"></td>\n", NAME_LENGTH, index, plot.vars[index].hist_label.c_str());
         rsprintf("<td><input type=text size=5 maxlength=10 name=\"ord%d\" value=\"%d\"></td>\n", index, plot.vars[index].hist_order);
      } else {
         rsprintf("<td><input type=submit name=cmdx value=\"List all variables\"></td>\n");
      }

      rsprintf("</tr>\n");
   }

   rsprintf("</table>\n");
   //rsprintf("</form>\n");
   page_footer(TRUE);
}

/*------------------------------------------------------------------*/

void export_hist(const char *path, time_t endtime, int scale, int index, int labels)
{
   HNDLE hDB, hkey, hkeypanel;
   int size, status;
   char str[256];

   int debug = 0;

   cm_get_experiment_database(&hDB, NULL);

   /* check panel name in ODB */
   sprintf(str, "/History/Display/%s", path);
   db_find_key(hDB, 0, str, &hkeypanel);
   if (!hkeypanel) {
      sprintf(str, "Cannot find /History/Display/%s in ODB\n", path);
      show_error(str);
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

   time_t now = ss_time();

   if (endtime == 0)
      endtime = now;

   HistoryData hsxxx;
   HistoryData* hsdata = &hsxxx;

   time_t starttime = endtime - scale;

   //printf("start %.0f, end %.0f, scale %.0f\n", (double)starttime, (double)endtime, (double)scale);

   status = read_history(hDB, path, index, runmarker, starttime, endtime, 0, hsdata);
   if (status != HS_SUCCESS) {
      sprintf(str, "History error, status %d\n", status);
      show_error(str);
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
      show_error("No history available for choosen period");
      free(i_var);
      return;
   }

   int run_index = -1;
   int state_index = -1;
   int n_run_number = 0;
   time_t* t_run_number = NULL;
   if (runmarker)
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
   rsprintf("HTTP/1.1 200 Document follows\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
   rsprintf("Accept-Ranges: bytes\r\n");
   rsprintf("Cache-control: private, max-age=0, no-cache\r\n");
   rsprintf("Expires: Fri, 01 Jan 1983 00:00:00 GMT\r\n");
   rsprintf("Content-Type: text/plain\r\n");
   rsprintf("Content-disposition: attachment; filename=\"export.csv\"\r\n");
   rsprintf("\r\n");
   
   /* output header line with variable names */
   if (runmarker && t_run_number)
      rsprintf("Time, Timestamp, Run, Run State, ");
   else
      rsprintf("Time, Timestamp, ");

   for (int i = 0, first = 1; i < hsdata->nvars; i++) {
      if (hsdata->odb_index[i] < 0)
         continue;
      if (hsdata->num_entries[i] <= 0)
         continue;
      if (!first)
         rsprintf(", ");
      first = 0;
      rsprintf("%s", hsdata->var_names[i]);
   }
   rsprintf("\n");

   int i_run = 0;

   do {

      if (debug)
         printf("hsdata %p, t %d, irun %d\n", hsdata, (int)t, i_run);

      /* find run number/state which is valid for t */
      if (runmarker && t_run_number)
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

      struct tm* tms = localtime(&t);

      char fmt[256];
      //strcpy(fmt, "%c");
      strcpy(fmt, "%Y.%m.%d %H:%M:%S");
      strftime(str, sizeof(str), fmt, tms);

      if (t_run_number && run_index>=0 && state_index>=0) {
         if (t_run_number[i_run] <= t)
            rsprintf("%s, %d, %.0f, %.0f, ", str, (int)t, hsdata->v[run_index][i_run], hsdata->v[state_index][i_run]);
         else
            rsprintf("%s, %d, N/A, N/A, ", str, (int)t);
      } else
         rsprintf("%s, %d, ", str, (int)t);

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
            rsprintf(", ");
         first = 0;
         //rsprintf("(%d %g)", i_var[i], hsdata->v[i][i_var[i]]);
         rsprintf("%g", hsdata->v[i][i_var[i]]);
      }
      rsprintf("\n");

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

void show_hist_page(const char *dec_path, const char* enc_path, char *buffer, int *buffer_size, int refresh)
{
   const char *p;
   HNDLE hDB, hkey, hikeyp, hkeyp, hkeybutton;
   KEY key, ikey;
   int i, j, k, scale, index, width, size, status, labels, fh, fsize;
   float factor[2];
   char def_button[][NAME_LENGTH] = { "10m", "1h", "3h", "12h", "24h", "3d", "7d" };
   struct tm *tms;

   cm_get_experiment_database(&hDB, NULL);

   //printf("show_hist_page: path [%s], enc_path [%s]\n", dec_path, enc_path);

   if (equal_ustring(getparam("cmd"), "Reset")) {
      char str[MAX_STRING_LENGTH];
      strlcpy(str, dec_path, sizeof(str));
      if (strrchr(str, '/'))
         strlcpy(str, strrchr(str, '/')+1, sizeof(str));
      redirect(str);
      return;
   }

   if (equal_ustring(getparam("cmd"), "Query")) {
      show_query_page(dec_path);
      return;
   }

   if (equal_ustring(getparam("cmd"), "Cancel")) {
      char str[MAX_STRING_LENGTH];
      strlcpy(str, dec_path, sizeof(str));
      if (strrchr(str, '/'))
         strlcpy(str, strrchr(str, '/')+1, sizeof(str));
      if (isparam("hscale"))
         add_param_to_url(str, sizeof(str), "scale", getparam("hscale"));
      if (isparam("htime"))
         add_param_to_url(str, sizeof(str), "time", getparam("htime"));
      if (isparam("hindex"))
         add_param_to_url(str, sizeof(str), "index", getparam("hindex"));
      redirect(str);
      return;
   }

   if (equal_ustring(getparam("cmd"), "Config") ||
       equal_ustring(getparam("cmd"), "Save")
       || equal_ustring(getparam("cmd"), "Clear history cache")
       || equal_ustring(getparam("cmd"), "Refresh")) {

      std::string hgroup = xgetparam("group");
      std::string panel = xgetparam("panel");
      
      /* get group and panel from path if not given */
      if (!isparam("group")) {
         hgroup = dec_path;
         std::string::size_type pos = hgroup.find("/");
         if (pos != std::string::npos)
            hgroup.resize(pos);
         panel = "";
         if (strrchr(dec_path, '/'))
            panel = strrchr(dec_path, '/')+1;
      }

      show_hist_config_page(dec_path, hgroup.c_str(), panel.c_str());
      return;
   }

   /* evaluate path pointing back to /HS */
   std::string back_path;
   for (p=enc_path ; *p ; p++)
      if (*p == '/')
         back_path += "../";

   if (isparam("fpanel") && isparam("fgroup") &&
      !isparam("scale")  && !isparam("shift") && !isparam("width") && !isparam("cmd")) {

      std::string hgroup;
      if (strchr(dec_path, '/')) {
         hgroup = dec_path;
         std::string::size_type pos = hgroup.find("/");
         if (pos != std::string::npos)
            hgroup.resize(pos);
      } else {
         hgroup = dec_path;
      }

      /* rewrite path if parameters come from a form submission */

      char path[256];
      /* check if group changed */
      if (!equal_ustring(getparam("fgroup"), hgroup.c_str()))
         sprintf(path, "%s%s", back_path.c_str(), getparam("fgroup"));
      else if (*getparam("fpanel"))
         sprintf(path, "%s%s/%s", back_path.c_str(), getparam("fgroup"), getparam("fpanel"));
      else
         sprintf(path, "%s%s", back_path.c_str(), getparam("fgroup"));

      if (isparam("hscale"))
         add_param_to_url(path, sizeof(path), "scale", getparam("hscale"));
      if (isparam("htime"))
         add_param_to_url(path, sizeof(path), "time", getparam("htime"));

      redirect(path);
      return;
   }

   if (equal_ustring(getparam("cmd"), "New")) {
      char str[MAX_STRING_LENGTH];
      strlcpy(str, dec_path, sizeof(str));
      if (strrchr(str, '/'))
         strlcpy(str, strrchr(str, '/')+1, sizeof(str));
      show_header("History", "GET", str, 0);

      rsprintf("<table class=\"dialogTable\">");
      rsprintf("<tr><th class=\"subStatusTitle\" colspan=2>New History Item</th><tr>");
      rsprintf("<tr><td align=center colspan=2>\n");
      rsprintf("Select group: &nbsp;&nbsp;");
      rsprintf("<select name=\"group\">\n");

      /* list existing groups */
      db_find_key(hDB, 0, "/History/Display", &hkey);
      if (hkey) {
         for (i = 0;; i++) {
            db_enum_link(hDB, hkey, i, &hkeyp);

            if (!hkeyp)
               break;

            db_get_key(hDB, hkeyp, &key);
            if (equal_ustring(dec_path, key.name))
               rsprintf("<option selected>%s</option>\n", key.name);
            else
               rsprintf("<option>%s</option>\n", key.name);
         }
      }
      if (!hkey || i == 0)
         rsprintf("<option>Default</option>\n");
      rsprintf("</select><p>\n");

      rsprintf("Or enter new group name: &nbsp;&nbsp;");
      rsprintf("<input type=text size=15 maxlength=31 name=new_group>\n");

      rsprintf("<tr><td align=center colspan=2>\n");
      rsprintf("<br>Panel name: &nbsp;&nbsp;");
      rsprintf("<input type=text size=15 maxlength=31 name=panel><br><br>\n");
      rsprintf("</td></tr>\n");

      rsprintf("<tr><td align=center colspan=2>");
      rsprintf("<input type=submit value=Submit>\n");
      rsprintf("</td></tr>\n");

      rsprintf("</table>\r\n");
      page_footer(TRUE);
      return;
   }

   if (equal_ustring(getparam("cmd"), "Delete Panel")) {
      char str[MAX_ODB_PATH];
      strlcpy(str, "/History/Display/", sizeof(str));
      strlcat(str, dec_path, sizeof(str));
      if (db_find_key(hDB, 0, str, &hkey)==DB_SUCCESS)
         db_delete_key(hDB, hkey, FALSE);

      redirect("../");
      return;
   }

   if (getparam("panel") && *getparam("panel")) {
      char panel[256];
      strlcpy(panel, getparam("panel"), sizeof(panel));

      /* strip leading/trailing spaces */
      while (*panel == ' ') {
         char str[256];
         strlcpy(str, panel+1, sizeof(str));
         strlcpy(panel, str, sizeof(panel));
      }
      while (strlen(panel)> 1 && panel[strlen(panel)-1] == ' ')
         panel[strlen(panel)-1] = 0;

      char hgroup[256];
      strlcpy(hgroup, getparam("group"), sizeof(hgroup));
      /* use new group if present */
      if (isparam("new_group") && *getparam("new_group"))
         strlcpy(hgroup, getparam("new_group"), sizeof(hgroup));

      char str[MAX_ODB_PATH];

      /* create new panel */
      sprintf(str, "/History/Display/%s/%s", hgroup, panel); // FIXME: overflow str
      db_create_key(hDB, 0, str, TID_KEY);
      status = db_find_key(hDB, 0, str, &hkey);
      if (status != DB_SUCCESS || !hkey) {
         cm_msg(MERROR, "show_hist_page", "Cannot create history panel with invalid ODB path \"%s\"", str);
         return;
      }
      db_set_value(hDB, hkey, "Timescale", "1h", NAME_LENGTH, 1, TID_STRING);
      i = 1;
      db_set_value(hDB, hkey, "Zero ylow", &i, sizeof(BOOL), 1, TID_BOOL);
      db_set_value(hDB, hkey, "Show run markers", &i, sizeof(BOOL), 1, TID_BOOL);
      i = 0;
      db_set_value(hDB, hkey, "Show values", &i, sizeof(BOOL), 1, TID_BOOL);
      i = 0;
      db_set_value(hDB, hkey, "Sort Vars", &i, sizeof(BOOL), 1, TID_BOOL);
      i = 0;
      db_set_value(hDB, hkey, "Log axis", &i, sizeof(BOOL), 1, TID_BOOL);

      /* configure that panel */
      show_hist_config_page(dec_path, hgroup, panel);
      return;
   }

   const char* pscale = getparam("scale");
   if (pscale == NULL || *pscale == 0)
      pscale = getparam("hscale");
   const char* pmag = getparam("width");
   if (pmag == NULL || *pmag == 0)
      pmag = getparam("hwidth");
   const char* pindex = getparam("index");
   if (pindex == NULL || *pindex == 0)
      pindex = getparam("hindex");

   labels = 1;
   if (*getparam("labels") && atoi(getparam("labels")) == 0)
      labels = 0;

   std::string bgcolor = "FFFFFF";
   if (*getparam("bgcolor"))
      bgcolor = xgetparam("bgcolor");

   std::string fgcolor = "000000";
   if (*getparam("fgcolor"))
      fgcolor = xgetparam("fgcolor");

   std::string gridcolor = "A0A0A0";
   if (*getparam("gcolor"))
      gridcolor = xgetparam("gcolor");

   /* evaluate scale and offset */

   time_t endtime = 0;
   if (isparam("time"))
      endtime = string_to_time(getparam("time"));
   else if (isparam("htime"))
      endtime = string_to_time(getparam("htime"));

   if (pscale && *pscale)
      scale = time_to_sec(pscale);
   else
      scale = 0;

   index = -1;
   if (pindex && *pindex)
      index = atoi(pindex);

   /* use contents of "/History/URL" to access history images (*images only*)
    * otherwise, use relative addresses from "back_path" */

   std::string hurl;
   status = db_get_value_string(hDB, 0, "/History/URL", 0, &hurl, FALSE);
   if (status != DB_SUCCESS)
      hurl = back_path;

   if (equal_ustring(getparam("cmd"), "Create ELog")) {
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
         generate_hist_graph(dec_path, fbuffer, &fsize, width, height, endtime, scale, index, labels, bgcolor.c_str(), fgcolor.c_str(), gridcolor.c_str());

         /* save temporary file */
         std::string dir;
         db_get_value_string(hDB, 0, "/Elog/Logbook Dir", 0, &dir, TRUE);
         if (dir.length() > 0 && dir[dir.length()-1] != DIR_SEPARATOR)
            dir += DIR_SEPARATOR_STR;

         time_t now = time(NULL);
         tms = localtime(&now);

         char str[MAX_STRING_LENGTH];

         if (strchr(dec_path, '/'))
            strlcpy(str, strchr(dec_path, '/') + 1, sizeof(str));
         else
            strlcpy(str, dec_path, sizeof(str));

         char file_name[256];
         sprintf(file_name, "%02d%02d%02d_%02d%02d%02d_%s.gif",
                  tms->tm_year % 100, tms->tm_mon + 1, tms->tm_mday,
                  tms->tm_hour, tms->tm_min, tms->tm_sec, str); // FIXME: overflows file_name
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
         redirect(url);

         M_FREE(fbuffer);
         return;

      } else {
         char str[MAX_STRING_LENGTH];
         /*---- use internal ELOG ----*/
         sprintf(str, "\\HS\\%s.gif", dec_path); // FIXME: overflows str
         if (getparam("hscale") && *getparam("hscale"))
            sprintf(str + strlen(str), "?scale=%s", getparam("hscale"));
         if (getparam("htime") && *getparam("htime")) {
            if (strchr(str, '?'))
               strlcat(str, "&", sizeof(str));
            else
               strlcat(str, "?", sizeof(str));
            sprintf(str + strlen(str), "time=%s", getparam("htime"));
         }
         //if (getparam("hoffset") && *getparam("hoffset")) {
         //   if (strchr(str, '?'))
         //      strlcat(str, "&", sizeof(str));
         //   else
         //      strlcat(str, "?", sizeof(str));
         //   sprintf(str + strlen(str), "offset=%s", getparam("hoffset"));
         //}
         if (getparam("hwidth") && *getparam("hwidth")) {
            if (strchr(str, '?'))
               strlcat(str, "&", sizeof(str));
            else
               strlcat(str, "?", sizeof(str));
            sprintf(str + strlen(str), "width=%s", getparam("hwidth"));
         }
         if (getparam("hindex") && *getparam("hindex")) {
            if (strchr(str, '?'))
               strlcat(str, "&", sizeof(str));
            else
               strlcat(str, "?", sizeof(str));
            sprintf(str + strlen(str), "index=%s", getparam("hindex"));
         }

         show_elog_new(NULL, FALSE, str, "../../EL/");
         return;
      }
   }

   if (equal_ustring(getparam("cmd"), "Export")) {
      export_hist(dec_path, endtime, scale, index, labels);
      return;
   }

   if (strstr(dec_path, ".gif")) {
      int width =  640;
      int height = 400;
      if (equal_ustring(pmag, "Large")) {
         width = 1024;
         height = 768;
      } else if (equal_ustring(pmag, "Small")) {
         width = 320;
         height = 200;
      } else if (atoi(pmag) > 0) {
         width = atoi(pmag);
         height = (int)(0.625 * width);
      }

      //printf("dec_path [%s], buf %p, %p, width %d, height %d, endtime %ld, scale %d, index %d, labels %d\n", dec_path, buffer, buffer_size, width, height, endtime, scale, index, labels);

      generate_hist_graph(dec_path, buffer, buffer_size, width, height, endtime, scale, index, labels, bgcolor.c_str(), fgcolor.c_str(), gridcolor.c_str());

      return;
   }

   if (history_mode && index < 0)
      return;

   time_t now = time(NULL);

   /* evaluate offset shift */
   if (equal_ustring(getparam("shift"), "<<<")) {
      if (endtime == 0)
         endtime = now;
      time_t last_written = 0;
      status = get_hist_last_written(dec_path, endtime, index, 1, &last_written);
      if (status == HS_SUCCESS)
         endtime = last_written + scale/2;
   }

   if (equal_ustring(getparam("shift"), "<<")) {
      if (endtime == 0)
         endtime = now;
      time_t last_written = 0;
      status = get_hist_last_written(dec_path, endtime, index, 0, &last_written);
      if (status == HS_SUCCESS)
         if (last_written != endtime)
            endtime = last_written + scale/2;
   }

   if (equal_ustring(getparam("shift"), "<")) {
      if (endtime == 0)
         endtime = now;
      endtime -= scale/2;
      //offset -= scale / 2;
   }

   if (equal_ustring(getparam("shift"), ">")) {
      if (endtime == 0)
         endtime = now;
      endtime += scale/2;
      if (endtime > now)
         endtime = now;

      //offset += scale / 2;
      //if (offset > 0)
      //   offset = 0;
   }

   if (equal_ustring(getparam("shift"), ">>")) {
      endtime = 0;
      //offset = 0;
   }

   if (equal_ustring(getparam("shift"), " + ")) {
      if (endtime == 0)
         endtime = now;
      endtime -= scale / 4;
      //offset -= scale / 4;
      scale /= 2;
   }

   if (equal_ustring(getparam("shift"), " - ")) {
      if (endtime == 0)
         endtime = now;
      endtime += scale / 2;
      if (endtime > now)
         endtime = now;
      //offset += scale / 2;
      //if (offset > 0)
      //   offset = 0;
      scale *= 2;
   }

   {
      char str[256];
      strlcpy(str, dec_path, sizeof(str));
      if (strrchr(str, '/'))
         strlcpy(str, strrchr(str, '/')+1, sizeof(str));
      int xrefresh = refresh;
      if (endtime != 0)
         xrefresh = 0;
      //if (offset != 0)
      //   xrefresh = 0;
      show_header(str, "GET", str, xrefresh);
   }
   
   rsprintf("<script type=\"text/javascript\" src=\"midas.js\"></script>\n");
   rsprintf("<script type=\"text/javascript\" src=\"mhttpd.js\"></script>\n");
   show_navigation_bar("History");

   rsprintf("<table class=\"genericTable\">");
   rsprintf("<tr><th class=\"subStatusTitle\" colspan=2>History</th></tr>");

   {
      char str[MAX_ODB_PATH];
      /* check if panel exists */
      sprintf(str, "/History/Display/%s", dec_path);
      status = db_find_key(hDB, 0, str, &hkey);
      if (status != DB_SUCCESS && !equal_ustring(dec_path, "All") && !equal_ustring(dec_path,"")) {
         rsprintf("<h1>Error: History panel \"%s\" does not exist</h1>\n", dec_path);
         rsprintf("</table>\r\n");
         page_footer(TRUE);
         return;
      }
   }

   /* define hidden field for parameters */
   if (pscale && *pscale)
      rsprintf("<input type=hidden name=hscale value=%d>\n", scale);
   else {
      /* if no scale and offset given, get it from default */
      if (dec_path[0] && !equal_ustring(dec_path, "All") && strchr(dec_path, '/') != NULL) {
         char str[MAX_ODB_PATH];
         sprintf(str, "/History/Display/%s/Timescale", dec_path); // FIXME: overflows str

         std::string scalestr = "1h";
         status = db_get_value_string(hDB, 0, str, 0, &scalestr, TRUE);
         if (status != DB_SUCCESS) {
            /* delete old integer key */
            db_find_key(hDB, 0, str, &hkey);
            if (hkey)
               db_delete_key(hDB, hkey, FALSE);

            scalestr = "1h";
            db_get_value_string(hDB, 0, str, 0, &scalestr, TRUE);
         }

         rsprintf("<input type=hidden name=hscale value=%s>\n", scalestr.c_str());
         scale = time_to_sec(scalestr.c_str());
      }
   }

   if (endtime != 0)
      rsprintf("<input type=hidden name=htime value=%s>\n", time_to_string(endtime));
   //if (offset != 0)
   //   rsprintf("<input type=hidden name=hoffset value=%d>\n", offset);
   if (pmag && *pmag)
      rsprintf("<input type=hidden name=hwidth value=%s>\n", pmag);
   if (pindex && *pindex)
      rsprintf("<input type=hidden name=hindex value=%s>\n", pindex);

   rsprintf("</td></tr>\n");

   if (dec_path[0] == 0) {
      /* "New" button */
      rsprintf("<tr><td colspan=2><input type=submit name=cmd value=New></td></tr>\n");

      /* links for history panels */
      rsprintf("<tr><td colspan=2 style=\"text-align:left;\">\n");
      if (!dec_path[0])
         rsprintf("<b>Please select panel:</b><br>\n");

      /* table for panel selection */
      rsprintf("<table class=\"historyTable\">");

      /* "All" link */
      rsprintf("<tr><td colspan=2 class=\"titleCell\">\n");
      if (equal_ustring(dec_path, "All"))
         rsprintf("All &nbsp;&nbsp;");
      else
         rsprintf("<a href=\"%sAll\">ALL</a>\n", back_path.c_str());
      rsprintf("</td></tr>\n");

      /* Setup History table links */
      db_find_key(hDB, 0, "/History/Display", &hkey);
      if (!hkey) {
         /* create default panel */
         char str[256];
         strcpy(str, "System:Trigger per sec.");
         strcpy(str + 2 * NAME_LENGTH, "System:Trigger kB per sec.");
         db_set_value(hDB, 0, "/History/Display/Default/Trigger rate/Variables", str, NAME_LENGTH * 4, 2, TID_STRING);
         strcpy(str, "1h");
         db_set_value(hDB, 0, "/History/Display/Default/Trigger rate/Time Scale", str, NAME_LENGTH, 1, TID_STRING);

         factor[0] = 1;
         factor[1] = 1;
         db_set_value(hDB, 0, "/History/Display/Default/Trigger rate/Factor", factor, 2 * sizeof(float), 2, TID_FLOAT);
         factor[0] = 0;
         factor[1] = 0;
         db_set_value(hDB, 0, "/History/Display/Default/Trigger rate/Offset", factor, 2 * sizeof(float), 2, TID_FLOAT);
         strcpy(str, "1h");
         db_set_value(hDB, 0, "/History/Display/Default/Trigger rate/Timescale", str, NAME_LENGTH, 1, TID_STRING);
         i = 1;
         db_set_value(hDB, 0, "/History/Display/Default/Trigger rate/Zero ylow", &i, sizeof(BOOL), 1, TID_BOOL);
         i = 1;
         db_set_value(hDB, 0, "/History/Display/Default/Trigger rate/Show run markers", &i, sizeof(BOOL), 1, TID_BOOL);
      }

      db_find_key(hDB, 0, "/History/Display", &hkey);
      if (hkey) {
         for (i = 0;; i++) {
            db_enum_link(hDB, hkey, i, &hkeyp);

            if (!hkeyp)
               break;

            // Group key
            db_get_key(hDB, hkeyp, &key);

            char str[256];

            if (strchr(dec_path, '/'))
               strlcpy(str, strchr(dec_path, '/') + 1, sizeof(str));
            else
               strlcpy(str, dec_path, sizeof(str));

            char enc_name[256];
            strlcpy(enc_name, key.name, sizeof(enc_name));
            urlEncode(enc_name, sizeof(enc_name));

            if (equal_ustring(str, key.name))
               rsprintf("<tr><td class=\"titleCell\">%s</td>\n<td>", key.name);
            else
               rsprintf("<tr><td class=\"titleCell\"><a href=\"%s%s\">%s</a></td>\n<td>", back_path.c_str(), enc_name, key.name);

            for (j = 0;; j++) {
               // scan items
               db_enum_link(hDB, hkeyp, j, &hikeyp);

               if (!hikeyp) {
                  rsprintf("</tr>");
                  break;
               }
               // Item key
               db_get_key(hDB, hikeyp, &ikey);

               if (strchr(dec_path, '/'))
                  strlcpy(str, strchr(dec_path, '/') + 1, sizeof(str));
               else
                  strlcpy(str, dec_path, sizeof(str));

               char enc_iname[256];
               strlcpy(enc_iname, ikey.name, sizeof(enc_iname));
               urlEncode(enc_iname, sizeof(enc_iname));

               if (equal_ustring(str, ikey.name))
                  rsprintf("<small><b>%s</b></small> &nbsp;", ikey.name);
               else
                  rsprintf("<small><a href=\"%s%s/%s\">%s</a></small> &nbsp;\n", back_path.c_str(), enc_name, enc_iname, ikey.name);
            }
         }
      }

      rsprintf("</table></tr>\n");

   } else {
      int found = 0;

      /* show drop-down selectors */
      rsprintf("<tr><td colspan=2>\n");

      rsprintf("Group:\n");

      rsprintf("<select title=\"Select group\" name=\"fgroup\" onChange=\"document.form1.submit()\">\n");

      db_find_key(hDB, 0, "/History/Display", &hkey);
      if (hkey) {
         hkeyp = 0;
         char hgroup[256];
         hgroup[0] = 0;
         for (i = 0;; i++) {
            db_enum_link(hDB, hkey, i, &hikeyp);

            if (!hikeyp)
               break;

            if (i == 0)
               hkeyp = hikeyp;

            // Group key
            db_get_key(hDB, hikeyp, &key);

            if (strchr(dec_path, '/')) {
               strlcpy(hgroup, dec_path, sizeof(hgroup));
               *strchr(hgroup, '/') = 0;
            } else {
               strlcpy(hgroup, dec_path, sizeof(hgroup));
            }

            if (equal_ustring(key.name, hgroup)) {
               rsprintf("<option selected value=\"%s\">%s\n", key.name, key.name);
               hkeyp = hikeyp;
            } else
               rsprintf("<option value=\"%s\">%s\n", key.name, key.name);
         }

         if (equal_ustring("ALL", hgroup)) {
            rsprintf("<option selected value=\"%s\">%s\n", "ALL", "ALL");
         } else {
            rsprintf("<option value=\"%s\">%s\n", "ALL", "ALL");
         }

         rsprintf("</select>\n");
         rsprintf("&nbsp;&nbsp;Panel:\n");
         rsprintf("<select title=\"Select panel\" name=\"fpanel\" onChange=\"document.form1.submit()\">\n");

         found = 0;
         if (hkeyp) {
            for (i = 0;; i++) {
               // scan panels
               db_enum_link(hDB, hkeyp, i, &hikeyp);

               if (!hikeyp)
                  break;

               // Item key
               db_get_key(hDB, hikeyp, &key);

               char str[256];

               if (strchr(dec_path, '/'))
                  strlcpy(str, strchr(dec_path, '/') + 1, sizeof(str));
               else
                  strlcpy(str, dec_path, sizeof(str));

               if (equal_ustring(str, key.name)) {
                  rsprintf("<option selected value=\"%s\">%s\n", key.name, key.name);
                  found = 1;
               } else
                  rsprintf("<option value=\"%s\">%s\n", key.name, key.name);
            }
         }

         if (found)
            rsprintf("<option value=\"\">- all -\n");
         else
            rsprintf("<option selected value=\"\">- all -\n");

         rsprintf("</select>\n");
      }

      rsprintf("<noscript>\n");
      rsprintf("<input type=submit value=\"Go\">\n");
      rsprintf("</noscript>\n");

      rsprintf("&nbsp;&nbsp;<input type=\"button\" name=\"New\" value=\"New\" ");

      if (found)
         rsprintf("onClick=\"window.location.href='../?cmd=New'\">\n");
      else
         rsprintf("onClick=\"window.location.href='?cmd=New'\">\n");

      rsprintf("<input type=\"submit\" name=\"Cmd\" value=\"Reset\" onClick=\"document.form1.submit()\">\n");

      rsprintf("<input type=\"submit\" name=\"Cmd\" value=\"Query\" onClick=\"document.form1.submit()\">\n");

      rsprintf("</td></tr>\n");
   }


   /* check if whole group should be displayed */
   if (dec_path[0] && !equal_ustring(dec_path, "ALL") && strchr(dec_path, '/') == NULL) {
      std::string strwidth = "Small";
      db_get_value_string(hDB, 0, "/History/Display Settings/Width Group", 0, &strwidth, TRUE);

      char str[MAX_ODB_PATH];
      sprintf(str, "/History/Display/%s", dec_path); // FIXME: overflows str
      db_find_key(hDB, 0, str, &hkey);
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
            ref += hurl;
            ref += enc_path;
            ref += "/";
            ref += enc_name;
            ref += ".gif?width=";
            ref += strwidth;

            std::string ref2;
            ref2 += enc_path;
            ref2 += "/";
            ref2 += enc_name;

            if (endtime != 0) {
               char tmp[256];
               sprintf(tmp, "time=%s&scale=%d", time_to_string(endtime), scale);
               ref += "&";
               ref += tmp;
               ref2 += "?";
               ref2 += tmp;
            }

            if (i % 2 == 0)
               rsprintf("<tr><td><a href=\"%s%s\"><img src=\"%s\" alt=\"%s.gif\"></a>\n", back_path.c_str(), ref2.c_str(), ref.c_str(), key.name);
            else
               rsprintf("<td><a href=\"%s%s\"><img src=\"%s\" alt=\"%s.gif\"></a></tr>\n", back_path.c_str(), ref2.c_str(), ref.c_str(), key.name);
         }

      } else {
         rsprintf("Group \"%s\" not found", dec_path);
      }
   }

   /* image panel */
   else if (dec_path[0] && !equal_ustring(dec_path, "All")) {
      /* navigation links */
      rsprintf("<tr><td>\n");

      char str[MAX_ODB_PATH];
      sprintf(str, "/History/Display/%s/Buttons", dec_path); // FIXME: overflow str
      db_find_key(hDB, 0, str, &hkeybutton);
      if (hkeybutton == 0) {
         /* create default buttons */
         db_create_key(hDB, 0, str, TID_STRING);
         status = db_find_key(hDB, 0, str, &hkeybutton);
         if (status != DB_SUCCESS || !hkey) {
            cm_msg(MERROR, "show_hist_page", "Cannot create history panel with invalid ODB path \"%s\"", str);
            return;
         }
         db_set_data(hDB, hkeybutton, def_button, sizeof(def_button), 7, TID_STRING);
      }

      db_get_key(hDB, hkeybutton, &key);

      for (i = 0; i < key.num_values; i++) {
         size = sizeof(str);
         db_get_data_index(hDB, hkeybutton, str, &size, i, TID_STRING);
         rsprintf("<input type=submit name=scale value=%s>\n", str);
      }

      rsprintf("<input type=submit name=shift value=\"<<<\" title=\"go back in time to last available data for all variables on the plot\">\n");
      rsprintf("<input type=submit name=shift value=\"<<\"  title=\"go back in time to last available data\">\n");
      rsprintf("<input type=submit name=shift value=\"<\"   title=\"go back in time\">\n");
      rsprintf("<input type=submit name=shift value=\" + \" title=\"zoom in\">\n");
      rsprintf("<input type=submit name=shift value=\" - \" title=\"zoom out\">\n");
      //if (offset != 0) {
      //   rsprintf("<input type=submit name=shift value=\">\">\n");
      //   rsprintf("<input type=submit name=shift value=\">>\">\n");
      //}
      if (endtime != 0) {
         rsprintf("<input type=submit name=shift value=\">\" title=\"go forward in time\">\n");
         rsprintf("<input type=submit name=shift value=\">>\" title=\"go to currently updated fresh data\">\n");
      }

      rsprintf("<td>\n");
      rsprintf("<input type=submit name=width value=Large>\n");
      rsprintf("<input type=submit name=width value=Small>\n");
      rsprintf("<input type=submit name=cmd value=\"Create ELog\">\n");
      rsprintf("<input type=submit name=cmd value=Config>\n");
      rsprintf("<input type=submit name=cmd value=Export>\n");
      //rsprintf("<input type=submit name=cmd value=Query>\n");
      //rsprintf("<input type=submit name=cmd value=Reset>\n");

      rsprintf("</tr>\n");

      char paramstr[256];
      
      paramstr[0] = 0;
      sprintf(paramstr + strlen(paramstr), "&scale=%d", scale);
      //if (offset != 0)
      //   sprintf(paramstr + strlen(paramstr), "&offset=%d", offset);
      if (endtime != 0)
         sprintf(paramstr + strlen(paramstr), "&time=%s", time_to_string(endtime));
      if (pmag && *pmag)
         sprintf(paramstr + strlen(paramstr), "&width=%s", pmag);
      else {
         std::string wi = "640";
         db_get_value_string(hDB, 0, "/History/Display Settings/Width Individual", 0, &wi, TRUE);
         sprintf(paramstr + strlen(paramstr), "&width=%s", wi.c_str());
      }

      /* define image map */
      rsprintf("<map name=\"%s\">\r\n", enc_path);

      if (!(pindex && *pindex)) {
         char str[MAX_ODB_PATH];
         sprintf(str, "/History/Display/%s/Variables", dec_path); // FIXME: overflows str
         db_find_key(hDB, 0, str, &hkey);
         if (hkey) {
            db_get_key(hDB, hkey, &key);

            for (i = 0; i < key.num_values; i++) {
               char ref[256];
               if (paramstr[0])
                  sprintf(ref, "%s?%s&index=%d", enc_path, paramstr, i);
               else
                  sprintf(ref, "%s?index=%d", enc_path, i);

               rsprintf("  <area shape=rect coords=\"%d,%d,%d,%d\" href=\"%s%s\">\r\n",
                        30, 31 + 23 * i, 150, 30 + 23 * i + 17, back_path.c_str(), ref);
            }
         }
      } else {
         std::string ref;
         
         if (paramstr[0]) {
            ref += enc_path;
            ref += "?";
            ref += paramstr;
         } else {
            ref = enc_path;
         }

         if (equal_ustring(pmag, "Large"))
            width = 1024;
         else if (equal_ustring(pmag, "Small"))
            width = 320;
         else if (atoi(pmag) > 0)
            width = atoi(pmag);
         else
            width = 640;

         rsprintf("  <area shape=rect coords=\"%d,%d,%d,%d\" href=\"%s%s\">\r\n", 0, 0, width, 20, back_path.c_str(), ref.c_str());
      }

      rsprintf("</map>\r\n");

      /* Display individual panels */
      if (pindex && *pindex)
         sprintf(paramstr + strlen(paramstr), "&index=%s", pindex);

      char ref[256];
      if (paramstr[0])
         sprintf(ref, "%s%s.gif?%s", hurl.c_str(), enc_path, paramstr);
      else
         sprintf(ref, "%s%s.gif", hurl.c_str(), enc_path);

      /* put reference to graph */
      rsprintf("<tr><td colspan=2><img src=\"%s\" alt=\"%s.gif\" usemap=\"#%s\"></tr>\n", ref, dec_path, enc_path);
   }

   else if (equal_ustring(dec_path, "All")) {
      /* Display all panels */
      db_find_key(hDB, 0, "/History/Display", &hkey);
      if (hkey)
         for (i = 0, k = 0;; i++) {     // scan Groups
            db_enum_link(hDB, hkey, i, &hkeyp);

            if (!hkeyp)
               break;

            db_get_key(hDB, hkeyp, &key);

            char enc_name[256];
            strlcpy(enc_name, key.name, sizeof(enc_name));
            urlEncode(enc_name, sizeof(enc_name));

            for (j = 0;; j++, k++) {
               // scan items
               db_enum_link(hDB, hkeyp, j, &hikeyp);

               if (!hikeyp)
                  break;

               db_get_key(hDB, hikeyp, &ikey);

               char enc_iname[256];
               strlcpy(enc_iname, ikey.name, sizeof(enc_iname));
               urlEncode(enc_iname, sizeof(enc_iname));

               std::string ref;
               ref += hurl;
               ref += enc_name;
               ref += "/";
               ref += enc_iname;
               ref += ".gif?width=Small";

               std::string ref2;
               ref2 += enc_name;
               ref2 += "/";
               ref2 += enc_iname;

               if (endtime != 0) {
                  char tmp[256];
                  sprintf(tmp, "time=%s&scale=%d", time_to_string(endtime), scale);
                  ref += "&";
                  ref += tmp;
                  ref2 += "?";
                  ref2 += tmp;
               }

               if (k % 2 == 0)
                  rsprintf("<tr><td><a href=\"%s%s\"><img src=\"%s\" alt=\"%s.gif\"></a>\n", back_path.c_str(), ref2.c_str(), ref.c_str(), ikey.name);
               else
                  rsprintf("<td><a href=\"%s%s\"><img src=\"%s\" alt=\"%s.gif\"></a></tr>\n", back_path.c_str(), ref2.c_str(), ref.c_str(), ikey.name);
            }                   // items loop
         }                      // Groups loop
   }                            // All
   rsprintf("</table>\r\n");
   //rsprintf("</form>\r\n");
   page_footer(TRUE);
}


/*------------------------------------------------------------------*/

void get_password(char *password)
{
   static char last_password[32];

   if (strncmp(password, "set=", 4) == 0)
      strlcpy(last_password, password + 4, sizeof(last_password));
   else
      strcpy(password, last_password);  // unsafe: do not know size of password string, has to be this way because of cm_connect_experiment() KO 27-Jul-2006
}

/*------------------------------------------------------------------*/

void send_icon(const char *icon)
{
   int length;
   const unsigned char *picon;
   char str[256], format[256];
   time_t now;
   struct tm *gmt;

   if (strstr(icon, "favicon.ico") != 0) {
      length = sizeof(favicon_ico);
      picon = favicon_ico;
   } else if (strstr(icon, "favicon.png") != 0) {
      length = sizeof(favicon_png);
      picon = favicon_png;
   } else
      return;

   rsprintf("HTTP/1.1 200 Document follows\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
   rsprintf("Accept-Ranges: bytes\r\n");

   /* set expiration time to one day */
   time(&now);
   now += (int) (3600 * 24);
   gmt = gmtime(&now);
   strcpy(format, "%A, %d-%b-%y %H:%M:%S GMT");
   strftime(str, sizeof(str), format, gmt);
   rsprintf("Expires: %s\r\n", str);

   if (equal_ustring(icon, "favicon.ico"))
      rsprintf("Content-Type: image/x-icon\r\n");
   else
      rsprintf("Content-Type: image/png\r\n");

   rsprintf("Content-Length: %d\r\n\r\n", length);

   rmemcpy(picon, length);
}

/*------------------------------------------------------------------*/

FILE *open_resource_file(const char *filename, std::string* pfilename)
{
   int status;
   HNDLE hDB;
   char* env;
   std::string path;
   FILE *fp = NULL;

   cm_get_experiment_database(&hDB, NULL);

   do { // THIS IS NOT A LOOP

      std::string buf;
      status = db_get_value_string(hDB, 0, "/Experiment/Resources", 0, &buf, FALSE);
      if (status == DB_SUCCESS && buf.length() > 0) {
         path = buf;
         if (path[path.length()-1] != DIR_SEPARATOR)
            path += DIR_SEPARATOR_STR;
         path += filename;
         fp = fopen(path.c_str(), "r");
         if (fp)
            break;
      }

      path = filename;
      fp = fopen(path.c_str(), "r");
      if (fp)
         break;

      path = std::string("resources") + DIR_SEPARATOR_STR + filename;
      fp = fopen(path.c_str(), "r");
      if (fp)
         break;

      env = getenv("MIDAS_DIR");
      if (env && strlen(env) > 0) {
         path = env;
         if (path[path.length()-1] != DIR_SEPARATOR)
            path += DIR_SEPARATOR_STR;
         path += filename;
         fp = fopen(path.c_str(), "r");
         if (fp)
            break;
      }

      env = getenv("MIDAS_DIR");
      if (env && strlen(env) > 0) {
         path = env;
         if (path[path.length()-1] != DIR_SEPARATOR)
            path += DIR_SEPARATOR_STR;
         path += "resources";
         path += DIR_SEPARATOR_STR;
         path += filename;
         fp = fopen(path.c_str(), "r");
         if (fp)
            break;
      }

      env = getenv("MIDASSYS");
      if (env && strlen(env) > 0) {
         path = env;
         if (path[path.length()-1] != DIR_SEPARATOR)
            path += std::string(DIR_SEPARATOR_STR);
         path += "resources";
         path += DIR_SEPARATOR_STR;
         path += filename;
         fp = fopen(path.c_str(), "r");
         if (fp)
            break;
      }

      break;
   } while (false); // THIS IS NOT A LOOP

   if (fp) {
      if (pfilename)
         *pfilename = path;
      //cm_msg(MINFO, "open_resource_file", "Resource file \'%s\' is \'%s\'", filename, path.c_str());
      return fp;
   }

   cm_msg(MERROR, "open_resource_file", "Cannot find resource file \'%s\' in ODB /Experiment/Resources, in $MIDASSYS/resources, in $MIDAS_DIR/resources or in local directory", filename);
   return NULL;
}

/*------------------------------------------------------------------*/

static std::string css_file = "mhttpd.css";

const char *get_css_filename()
{
   HNDLE hDB;
   cm_get_experiment_database(&hDB, NULL);
   db_get_value_string(hDB, 0, "/Experiment/CSS File", 0, &css_file, TRUE);
   return css_file.c_str();
}

/*------------------------------------------------------------------*/

#ifdef OBSOLETE

static char _js_file[MAX_STRING_LENGTH];

char *get_js_filename()
{
   HNDLE hDB;

   cm_get_experiment_database(&hDB, NULL);
   char filename[MAX_STRING_LENGTH];
   int size = sizeof(filename);
   strcpy(filename, "mhttpd.js");
   db_get_value(hDB, 0, "/Experiment/JS File", filename, &size, TID_STRING, TRUE);
   strlcpy(_js_file, filename, sizeof(_js_file));
   return _js_file;
}

#endif

/*------------------------------------------------------------------*/

void send_css()
{
   char str[MAX_STRING_LENGTH], format[MAX_STRING_LENGTH];
   time_t now;
   struct tm *gmt;

   rsprintf("HTTP/1.1 200 Document follows\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
   rsprintf("Accept-Ranges: bytes\r\n");

   /* set expiration time to one day */
   time(&now);
   now += (int) (3600 * 24);
   gmt = gmtime(&now);
   strcpy(format, "%A, %d-%b-%y %H:%M:%S GMT");
   strftime(str, sizeof(str), format, gmt);
   rsprintf("Expires: %s\r\n", str);
   rsprintf("Content-Type: text/css\r\n");

   /* look for external CSS file */

   std::string filename;
   FILE *fp = open_resource_file(get_css_filename(), &filename);

   if (fp) {
      struct stat stat_buf;
      fstat(fileno(fp), &stat_buf);
      int length = stat_buf.st_size;
      rsprintf("Content-Length: %d\r\n\r\n", length);

      rread(filename.c_str(), fileno(fp), length);

      fclose(fp);
      return;
   }

   int length = 0;
   rsprintf("Content-Length: %d\r\n\r\n", length);
}

/*------------------------------------------------------------------*/

bool send_resource(const std::string& name)
{
   std::string filename;
   FILE *fp = open_resource_file(name.c_str(), &filename);

   if (!fp) {
      return false;
   }

   // send HTTP headers
   
   rsprintf("HTTP/1.1 200 Document follows\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
   rsprintf("Accept-Ranges: bytes\r\n");

   // send HTTP cache control headers

   time_t now = time(NULL);
   now += (int) (3600 * 24);
   struct tm* gmt = gmtime(&now);
   const char* format = "%A, %d-%b-%y %H:%M:%S GMT";
   char str[256];
   strftime(str, sizeof(str), format, gmt);
   rsprintf("Expires: %s\r\n", str);

   // send Content-Type header

   const char* type = "text/plain";

   if (name.rfind(".css") != std::string::npos)
      type = "text/css";
   else if (name.rfind(".html") != std::string::npos)
      type = "text/html";
   else if (name.rfind(".js") != std::string::npos)
      type = "application/javascript";
   else if (name.rfind(".mp3") != std::string::npos)
      type = "audio/mpeg";

   rsprintf("Content-Type: %s\r\n", type);

   // send Content-Length header

   struct stat stat_buf;
   fstat(fileno(fp), &stat_buf);
   int length = stat_buf.st_size;
   rsprintf("Content-Length: %d\r\n\r\n", length);

   // send file data
   
   rread(filename.c_str(), fileno(fp), length);
   
   fclose(fp);

   return true;
}

/*------------------------------------------------------------------*/

#ifdef OBSOLETE

const char *mhttpd_js =
"/* MIDAS type definitions */\n"
"var TID_BYTE = 1;\n"
"var TID_SBYTE = 2;\n"
"var TID_CHAR = 3;\n"
"var TID_WORD = 4;\n"
"var TID_SHORT = 5;\n"
"var TID_DWORD = 6;\n"
"var TID_INT = 7;\n"
"var TID_BOOL = 8;\n"
"var TID_FLOAT = 9;\n"
"var TID_DOUBLE = 10;\n"
"var TID_BITFIELD = 11;\n"
"var TID_STRING = 12;\n"
"var TID_ARRAY = 13;\n"
"var TID_STRUCT = 14;\n"
"var TID_KEY = 15;\n"
"var TID_LINK = 16;\n"
"\n"
"document.onmousemove = getMouseXY;\n"
"\n"
"function getMouseXY(e)\n"
"{\n"
"   try {\n"
"      var x = e.pageX;\n"
"      var y = e.pageY;\n"
"      var p = 'abs: ' + x + '/' + y;\n"
"      i = document.getElementById('refimg');\n"
"      if (i == null)\n"
"         return false;\n"
"      document.body.style.cursor = 'crosshair';\n"
"      x -= i.offsetLeft;\n"
"      y -= i.offsetTop;\n"
"      while (i = i.offsetParent) {\n"
"         x -= i.offsetLeft;\n"
"         y -= i.offsetTop;\n"
"      }\n"
"      p += '   rel: ' + x + '/' + y;\n"
"      window.status = p;\n"
"      return true;\n"
"      }\n"
"   catch (e) {\n"
"      return false;\n"
"   }\n"
"}\n"
"\n"
"function XMLHttpRequestGeneric()\n"
"{\n"
"   var request;\n"
"   try {\n"
"      request = new XMLHttpRequest(); // Firefox, Opera 8.0+, Safari\n"
"   }\n"
"   catch (e) {\n"
"      try {\n"
"         request = new ActiveXObject('Msxml2.XMLHTTP'); // Internet Explorer\n"
"      }\n"
"      catch (e) {\n"
"         try {\n"
"            request = new ActiveXObject('Microsoft.XMLHTTP');\n"
"         }\n"
"         catch (e) {\n"
"           alert('Your browser does not support AJAX!');\n"
"           return undefined;\n"
"         }\n"
"      }\n"
"   }\n"
"\n"
"   return request;\n"
"}\n"
"\n"
"function ODBSet(path, value, pwdname)\n"
"{\n"
"   var value, request, url;\n"
"\n"
"   if (pwdname != undefined)\n"
"      pwd = prompt('Please enter password', '');\n"
"   else\n"
"      pwd = '';\n"
"\n"
"   var request = XMLHttpRequestGeneric();\n"
"\n"
"   url = '?cmd=jset&odb=' + path + '&value=' + value;\n"
"\n"
"   if (pwdname != undefined)\n"
"      url += '&pnam=' + pwdname;\n"
"\n"
"   request.open('GET', url, false);\n"
"\n"
"   if (pwdname != undefined)\n"
"      request.setRequestHeader('Cookie', 'cpwd='+pwd);\n"
"\n"
"   request.send(null);\n"
"\n"
"   if (request.status != 200 || request.responseText != 'OK') \n"
"      alert('ODBSet error:\\nPath: '+path+'\\nHTTP Status: '+request.status+'\\nMessage: '+request.responseText+'\\n'+document.location) ;\n"
"}\n"
"\n"
"function ODBGet(path, format, defval, len, type)\n"
"{\n"
"   var request = XMLHttpRequestGeneric();\n"
"\n"
"   var url = '?cmd=jget&odb=' + path;\n"
"   if (format != undefined && format != '')\n"
"      url += '&format=' + format;\n"
"   request.open('GET', url, false);\n"
"   request.send(null);\n"
"\n"
"   if (path.match(/[*]/)) {\n"
"      if (request.responseText == null)\n"
"         return null;\n"
"      if (request.responseText == '<DB_NO_KEY>') {\n"
"         url = '?cmd=jset&odb=' + path + '&value=' + defval + '&len=' + len + '&type=' + type;\n"
"\n"
"         request.open('GET', url, false);\n"
"         request.send(null);\n"
"         return defval;\n"
"      } else {\n"
"         var array = request.responseText.split('\\n');\n"
"         return array;\n"
"      }\n"
"   } else {\n"
"      if ((request.responseText == '<DB_NO_KEY>' ||\n"
"           request.responseText == '<DB_OUT_OF_RANGE>') && defval != undefined) {\n"
"         url = '?cmd=jset&odb=' + path + '&value=' + defval + '&len=' + len + '&type=' + type;\n"
"\n"
"         request.open('GET', url, false);\n"
"         request.send(null);\n"
"         return defval;\n"
"      }\n"
"      return request.responseText.split('\\n')[0];\n"
"   }\n"
"}\n"
"\n"
"function ODBMGet(paths, callback, formats)\n"
"{\n"
"   var request = XMLHttpRequestGeneric();\n"
"\n"
"   var url = '?cmd=jget';\n"
"   for (var i=0 ; i<paths.length ; i++) {\n"
"      url += '&odb'+i+'='+paths[i];\n"
"      if (formats != undefined && formats != '')\n"
"         url += '&format'+i+'=' + formats[i];\n"
"   }\n"
"\n"
"   if (callback != undefined) {\n"
"      request.onreadystatechange = function() \n"
"         {\n"
"         if (request.readyState == 4) {\n"
"            if (request.status == 200) {\n"
"               var array = request.responseText.split('$#----#$\\n');\n"
"               for (var i=0 ; i<array.length ; i++)\n"
"                  if (paths[i].match(/[*]/)) {\n"
"                     array[i] = array[i].split('\\n');\n"
"                     array[i].length--;\n"
"                  } else\n"
"                     array[i] = array[i].split('\\n')[0];\n"
"               callback(array);\n"
"            }\n"
"         }\n"
"      }\n"
"      request.open('GET', url, true);\n"
"   } else\n"
"      request.open('GET', url, false);\n"
"   request.send(null);\n"
"\n"
"   if (callback == undefined) {\n"
"      var array = request.responseText.split('$#----#$\\n');\n"
"      for (var i=0 ; i<array.length ; i++) {\n"
"         if (paths[i].match(/[*]/)) {\n"
"            array[i] = array[i].split('\\n');\n"
"            array[i].length--;\n"
"         } else\n"
"            array[i] = array[i].split('\\n')[0];\n"
"      }\n"
"      return array;\n"
"   }\n"
"}\n"
"\n"
"function ODBGetRecord(path)\n"
"{\n"
"   var request = XMLHttpRequestGeneric();\n"
"\n"
"   var url = '?cmd=jget&odb=' + path + '&name=1';\n"
"   request.open('GET', url, false);\n"
"   request.send(null);\n"
"   return request.responseText;\n"
"}\n"
"\n"
"function ODBExtractRecord(record, key)\n"
"{\n"
"   var array = record.split('\\n');\n"
"   for (var i=0 ; i<array.length ; i++) {\n"
"      var ind = array[i].indexOf(':');\n"
"      if (ind > 0) {\n"
"         var k = array[i].substr(0, ind);\n"
"         if (k == key)\n"
"            return array[i].substr(ind+1, array[i].length);\n"
"      }\n"
"      var ind = array[i].indexOf('[');\n"
"      if (ind > 0) {\n"
"         var k = array[i].substr(0, ind);\n"
"         if (k == key) {\n"
"            var a = new Array();\n"
"            for (var j=0 ; ; j++,i++) {\n"
"               if (array[i].substr(0, ind) != key)\n"
"                  break;\n"
"               var k = array[i].indexOf(':');\n"
"               a[j] = array[i].substr(k+1, array[i].length);\n"
"            }\n"
"            return a;\n"
"         }\n"
"      }\n"
"   }\n"
"   return null;\n"
"}\n"
"\n"
"function ODBKey(path)\n"
"{\n"
"   var request = XMLHttpRequestGeneric();\n"
"\n"
"   var url = '?cmd=jkey&odb=' + path;\n"
"   request.open('GET', url, false);\n"
"   request.send(null);\n"
"   if (request.responseText == null)\n"
"      return null;\n"
"   var res = request.responseText.split('\\n');\n"
"   this.name = res[0];\n"
"   this.type = res[1];\n"
"   this.num_values = res[2];\n"
"   this.item_size = res[3];\n"
"   this.last_written = res[4];\n"
"}\n"
"\n"
"function ODBCopy(path, format)\n"
"{\n"
"   var request = XMLHttpRequestGeneric();\n"
"\n"
"   var url = '?cmd=jcopy&odb=' + path;\n"
"   if (format != undefined && format != '')\n"
"      url += '&format=' + format;\n"
"   request.open('GET', url, false);\n"
"   request.send(null);\n"
"   return request.responseText;\n"
"}\n"
"\n"
"function ODBRpc_rev0(name, rpc, args)\n"
"{\n"
"   var request = XMLHttpRequestGeneric();\n"
"\n"
"   var url = '?cmd=jrpc_rev0&name=' + name + '&rpc=' + rpc;\n"
"   for (var i = 2; i < arguments.length; i++) {\n"
"     url += '&arg'+(i-2)+'='+arguments[i];\n"
"   };\n"
"   request.open('GET', url, false);\n"
"   request.send(null);\n"
"   if (request.responseText == null)\n"
"      return null;\n"
"   this.reply = request.responseText.split('\\n');\n"
"}\n"
"\n"
"function ODBRpc_rev1(name, rpc, max_reply_length, args)\n"
"{\n"
"   var request = XMLHttpRequestGeneric();\n"
"\n"
"   var url = '?cmd=jrpc_rev1&name=' + name + '&rpc=' + rpc + '&max_reply_length=' + max_reply_length;\n"
"   for (var i = 3; i < arguments.length; i++) {\n"
"     url += '&arg'+(i-3)+'='+arguments[i];\n"
"   };\n"
"   request.open('GET', url, false);\n"
"   request.send(null);\n"
"   if (request.responseText == null)\n"
"      return null;\n"
"   return request.responseText;\n"
"}\n"
"\n"
"function ODBGetMsg(n)\n"
"{\n"
"   var request = XMLHttpRequestGeneric();\n"
"\n"
"   var url = '?cmd=jmsg&n=' + n;\n"
"   request.open('GET', url, false);\n"
"   request.send(null);\n"
"\n"
"   if (n > 1) {\n"
"      var array = request.responseText.split('\\n');\n"
"      return array;\n"
"   } else\n"
"      return request.responseText;\n"
"}\n"
"\n"
"function ODBGenerateMsg(m)\n"
"{\n"
"   var request = XMLHttpRequestGeneric();\n"
"\n"
"   var url = '?cmd=jgenmsg&msg=' + m;\n"
"   request.open('GET', url, false);\n"
"   request.send(null);\n"
"   return request.responseText;\n"
"}\n"
"\n"
"function ODBGetAlarms()\n"
"{\n"
"   var request = XMLHttpRequestGeneric();\n"
"   request.open('GET', '?cmd=jalm', false);\n"
"   request.send(null);\n"
"   var a = request.responseText.split('\\n');\n"
"   a.length = a.length-1;\n"
"   return a;\n"
"}\n"
"\n"
"function ODBEdit(path)\n"
"{\n"
"   var value = ODBGet(path);\n"
"   var new_value = prompt('Please enter new value', value);\n"
"   if (new_value != undefined) {\n"
"      ODBSet(path, new_value);\n"
"      window.location.reload();\n"
"   }\n"
"}\n"
"";

#endif

#ifdef OBSOLETE

void send_js()
{
   int length;
   char str[256], format[256];
   time_t now;
   struct tm *gmt;

   rsprintf("HTTP/1.1 200 Document follows\r\n");
   rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
   rsprintf("Accept-Ranges: bytes\r\n");

   /* set expiration time to one day */
   time(&now);
   now += (int) (3600 * 24);
   gmt = gmtime(&now);
   strcpy(format, "%A, %d-%b-%y %H:%M:%S GMT");
   strftime(str, sizeof(str), format, gmt);
   rsprintf("Expires: %s\r\n", str);
   rsprintf("Content-Type: text/javascript\r\n");

   /* look for external JS file */
   std::string filename;
   FILE *fp = open_resource_file(get_js_filename(), &filename);

   if (fp) {
      struct stat stat_buf;
      fstat(fileno(fp), &stat_buf);
      length = stat_buf.st_size;
      rsprintf("Content-Length: %d\r\n\r\n", length);
      rread(filename.c_str(), fileno(fp), length);
      fclose(fp);
      return;
   }

   length = strlen(mhttpd_js);
   rsprintf("Content-Length: %d\r\n\r\n", length);

   rmemcpy(mhttpd_js, length);
}

#endif

/*------------------------------------------------------------------*/

void interprete(const char *cookie_pwd, const char *cookie_wpwd, const char *cookie_cpwd, const char *dec_path, int refresh, int expand_equipment)
/********************************************************************\

 Routine: interprete

 Purpose: Interprete parametersand generate HTML output from odb.

 Input:
 char *cookie_pwd        Cookie containing encrypted password
 char *path              ODB path "/dir/subdir/key"

 <implicit>
 _param/_value array accessible via getparam()

 \********************************************************************/
{
   int i, j, n, status, size, run_state, index, write_access;
   WORD event_id;
   HNDLE hkey, hsubkey, hDB, hconn;
   KEY key;
   const char *p;
   char str[256];
   char enc_path[256], eq_name[NAME_LENGTH], fe_name[NAME_LENGTH];
   char data[TEXT_SIZE];
   time_t now;
   struct tm *gmt;

   //printf("dec_path [%s]\n", dec_path);

   if (strstr(dec_path, "favicon.ico") != 0 ||
       strstr(dec_path, "favicon.png")) {
      send_icon(dec_path);
      return;
   }

   if (strstr(dec_path, get_css_filename())) {
      send_css();
      return;
   }

#ifdef OBSOLETE
   if (strstr(dec_path, get_js_filename())) {
      send_js();
      return;
   }
#endif

   strlcpy(enc_path, dec_path, sizeof(enc_path));
   urlEncode(enc_path, sizeof(enc_path));
   set_dec_path(dec_path);

   const char* experiment = getparam("exp");
   const char* password = getparam("pwd");
   const char* wpassword = getparam("wpwd");
   const char* command = getparam("cmd");
   const char* value = getparam("value");
   const char* group = getparam("group");
   index = atoi(getparam("index"));

   //printf("interprete: dec_path [%s], command [%s] value [%s]\n", dec_path, command, value);

   cm_get_experiment_database(&hDB, NULL);

   if (history_mode) {
      if (strncmp(dec_path, "HS/", 3) == 0) {
         if (equal_ustring(command, "config")) {
            return;
         }

         show_hist_page(dec_path + 3, enc_path + 3, NULL, NULL, refresh);
         return;
      }

      return;
   }

   /* check for password */
   db_find_key(hDB, 0, "/Experiment/Security/Password", &hkey);
   if (!password[0] && hkey) {
      size = sizeof(str);
      db_get_data(hDB, hkey, str, &size, TID_STRING);

      /* check for excemption */
      db_find_key(hDB, 0, "/Experiment/Security/Allowed programs/mhttpd", &hkey);
      if (hkey == 0 && strcmp(cookie_pwd, str) != 0) {
         show_password_page("", experiment);
         return;
      }
   }

   /* get run state */
   run_state = STATE_STOPPED;
   size = sizeof(run_state);
   db_get_value(hDB, 0, "/Runinfo/State", &run_state, &size, TID_INT, TRUE);

   /*---- redirect with cookie if password given --------------------*/

   if (password[0]) {
      rsprintf("HTTP/1.1 302 Found\r\n");
      rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());

      time(&now);
      now += 3600 * 24;
      gmt = gmtime(&now);
      strftime(str, sizeof(str), "%A, %d-%b-%Y %H:00:00 GMT", gmt);

      rsprintf("Set-Cookie: midas_pwd=%s; path=/; expires=%s\r\n",
               ss_crypt(password, "mi"), str);

      rsprintf("Location: ./\n\n<html>redir</html>\r\n");
      return;
   }

   if (wpassword[0]) {
      /* check if password correct */
      if (!check_web_password(ss_crypt(wpassword, "mi"), getparam("redir"), experiment))
         return;

      rsprintf("HTTP/1.1 302 Found\r\n");
      rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());

      time(&now);
      now += 3600 * 24;
      gmt = gmtime(&now);
      strftime(str, sizeof(str), "%A, %d-%b-%Y %H:%M:%S GMT", gmt);

      rsprintf("Set-Cookie: midas_wpwd=%s; path=/; expires=%s\r\n",
               ss_crypt(wpassword, "mi"), str);

      sprintf(str, "./%s", getparam("redir"));
      rsprintf("Location: %s\n\n<html>redir</html>\r\n", str);
      return;
   }

   /*---- redirect if ODB command -----------------------------------*/

   if (equal_ustring(command, "ODB")) {
      str[0] = 0;
      for (p=dec_path ; *p ; p++)
         if (*p == '/')
            strlcat(str, "../", sizeof(str));
      strlcat(str, "root", sizeof(str));
      redirect(str);
      return;
   }

   /*---- send sound file -------------------------------------------*/

   if (strlen(dec_path) > 3 &&
       dec_path[strlen(dec_path)-3] == 'm' &&
       dec_path[strlen(dec_path)-2] == 'p' &&
       dec_path[strlen(dec_path)-1] == '3') {
      send_resource(dec_path);
      return;
   }

   /*---- send midas.js and midas.css -------------------------------*/

   if (strstr(dec_path, "midas.js")) {
      send_resource("midas.js");
      return;
   }

   if (strstr(dec_path, "midas.css")) {
      send_resource("midas.css");
      return;
   }

   /*---- send mhttpd.js --------------------------------------------*/

   if (strstr(dec_path, "mhttpd.js")) {
      send_resource("mhttpd.js");
      return;
   }

   /*---- send obsolete.js ------------------------------------------*/

   if (strstr(dec_path, "obsolete.js")) {
      send_resource("obsolete.js");
      return;
   }

   /*---- send example web page -------------------------------------*/

   if (equal_ustring(command, "example")) {
      send_resource("example.html");
      return;
   }

   /*---- send the new html pages -----------------------------------*/

#ifdef NEW_START_STOP
   if (equal_ustring(command, "start")) {
      send_resource("start.html");
      return;
   }
#endif

   if (equal_ustring(command, "programs")) {
      send_resource("programs.html");
      return;
   }

   if (equal_ustring(command, "alarms")) {
      send_resource("alarms.html");
      return;
   }

   if (equal_ustring(command, "transition")) {
      send_resource("transition.html");
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
      javascript_commands(cookie_cpwd);
      return;
   }

   /*---- redirect if SC command ------------------------------------*/

   if (equal_ustring(command, "SC")) {
      redirect("SC/");
      return;
   }

   /*---- redirect if web page --------------------------------------*/

   //if (send_resource(std::string(command) + ".html"))
   //   return;

   /*---- redirect if status command --------------------------------*/

   if (equal_ustring(command, "status")) {
      str[0] = 0;
      for (p=dec_path ; *p ; p++)
         if (*p == '/')
            strlcat(str, "../", sizeof(str));
      redirect(str);
      return;
   }

   /*---- script command --------------------------------------------*/

   if (getparam("script") && *getparam("script")) {
      sprintf(str, "%s?script=%s", dec_path, getparam("script"));
      if (!check_web_password(cookie_wpwd, str, experiment))
         return;

      sprintf(str, "/Script/%s", getparam("script"));

      db_find_key(hDB, 0, str, &hkey);

      if (hkey) {
         /* for NT: close reply socket before starting subprocess */
         if (isparam("redir"))
            redirect2(getparam("redir"));
         else
            redirect2("");
         exec_script(hkey);
      } else {
         if (isparam("redir"))
            redirect2(getparam("redir"));
         else
            redirect2("");
      }

      return;
   }

   /*---- customscript command --------------------------------------*/

   if (getparam("customscript") && *getparam("customscript")) {
      sprintf(str, "%s?customscript=%s", dec_path, getparam("customscript"));
      if (!check_web_password(cookie_wpwd, str, experiment))
         return;

      sprintf(str, "/CustomScript/%s", getparam("customscript"));

      db_find_key(hDB, 0, str, &hkey);

      if (hkey) {
         /* for NT: close reply socket before starting subprocess */
         if (isparam("redir"))
            redirect2(getparam("redir"));
         else
            redirect2("");
         exec_script(hkey);
      } else {
         if (isparam("redir"))
            redirect(getparam("redir"));
         else
            redirect("");
      }

      return;
   }

#ifdef OBSOLETE
   /*---- alarms command --------------------------------------------*/

   if (equal_ustring(command, "alarms")) {
      str[0] = 0;
      for (p=dec_path ; *p ; p++)
         if (*p == '/')
            strlcat(str, "../", sizeof(str));
      if (str[0]) {
         strlcat(str, "./?cmd=alarms", sizeof(str));
         redirect(str);
         return;
      }
      show_alarm_page();
      return;
   }

   /*---- alarms reset command --------------------------------------*/

   if (equal_ustring(command, "alrst")) {
      if (getparam("name") && *getparam("name"))
         al_reset_alarm(getparam("name"));
      redirect("");
      return;
   }
#endif

   /*---- history command -------------------------------------------*/

   if (equal_ustring(command, "history")) {
      str[0] = 0;
      for (p=dec_path ; *p ; p++)
         if (*p == '/')
            strlcat(str, "../", sizeof(str));
      strlcat(str, "HS/", sizeof(str));
      redirect(str);
      return;
   }

   if (strncmp(dec_path, "HS/", 3) == 0) {
      if (equal_ustring(command, "config")) {
         sprintf(str, "%s?cmd=%s", dec_path, command);
         if (!check_web_password(cookie_wpwd, str, experiment))
            return;
      }

      show_hist_page(dec_path + 3, enc_path + 3, NULL, NULL, refresh);
      return;
   }

   /*---- MSCB command ----------------------------------------------*/

   if (equal_ustring(command, "MSCB")) {
      str[0] = 0;
      for (p=dec_path ; *p ; p++)
         if (*p == '/')
            strlcat(str, "../", sizeof(str));
      strlcat(str, "MS/", sizeof(str));
      redirect(str);
      return;
   }

   if (strncmp(dec_path, "MS/", 3) == 0) {
      if (equal_ustring(command, "set")) {
         sprintf(str, "%s?cmd=%s", dec_path, command);
         if (!check_web_password(cookie_wpwd, str, experiment))
            return;
      }

#ifdef HAVE_MSCB
      show_mscb_page(dec_path + 3, refresh);
#else
      show_error("MSCB support not compiled into this version of mhttpd");
#endif
      return;
   }

   /*---- help command ----------------------------------------------*/

   if (equal_ustring(command, "help")) {
      show_help_page();
      return;
   }

   /*---- pause run -------------------------------------------*/

   if (equal_ustring(command, "pause")) {
      if (run_state != STATE_RUNNING) {
         show_error("Run is not running");
         return;
      }

      if (!check_web_password(cookie_wpwd, "?cmd=pause", experiment))
         return;

      status = cm_transition(TR_PAUSE, 0, str, sizeof(str), TR_MTHREAD | TR_ASYNC, FALSE);
      if (status != CM_SUCCESS && status != CM_DEFERRED_TRANSITION)
         show_error(str);
      else if (isparam("redir"))
         redirect(getparam("redir"));
      else
         redirect("");

      requested_old_state = run_state;
      if (status == SUCCESS)
         requested_transition = TR_PAUSE;

      return;
   }

   /*---- resume run ------------------------------------------*/

   if (equal_ustring(command, "resume")) {
      if (run_state != STATE_PAUSED) {
         show_error("Run is not paused");
         return;
      }

      if (!check_web_password(cookie_wpwd, "?cmd=resume", experiment))
         return;

      status = cm_transition(TR_RESUME, 0, str, sizeof(str), TR_MTHREAD | TR_ASYNC, FALSE);
      if (status != CM_SUCCESS && status != CM_DEFERRED_TRANSITION)
         show_error(str);
      else if (isparam("redir"))
         redirect(getparam("redir"));
      else
         redirect("");

      requested_old_state = run_state;
      if (status == SUCCESS)
         requested_transition = TR_RESUME;

      return;
   }

   /*---- start dialog --------------------------------------------*/

   if (equal_ustring(command, "start")) {
      if (run_state == STATE_RUNNING) {
         show_error("Run is already started");
         return;
      }

      if (value[0] == 0) {
         if (!check_web_password(cookie_wpwd, "?cmd=start", experiment))
            return;
         show_start_page(FALSE);
      } else {
         /* set run parameters */
         db_find_key(hDB, 0, "/Experiment/Edit on start", &hkey);
         if (hkey) {
            for (i = 0, n = 0;; i++) {
               db_enum_key(hDB, hkey, i, &hsubkey);

               if (!hsubkey)
                  break;

               db_get_key(hDB, hsubkey, &key);

               for (j = 0; j < key.num_values; j++) {
                  size = key.item_size;
                  sprintf(str, "x%d", n++);
                  db_sscanf(getparam(str), data, &size, 0, key.type);
                  db_set_data_index(hDB, hsubkey, data, key.item_size, j, key.type);
               }
            }
         }

         i = atoi(value);
         if (i <= 0) {
            cm_msg(MERROR, "interprete", "Start run: invalid run number %d", i);
            sprintf(str, "Invalid run number %d", i);
            show_error(str);
            return;
         }

         status = cm_transition(TR_START, i, str, sizeof(str), TR_MTHREAD | TR_ASYNC, FALSE);
         if (status != CM_SUCCESS && status != CM_DEFERRED_TRANSITION) {
            show_error(str);
         } else {

            requested_old_state = run_state;
            requested_transition = TR_START;

            if (isparam("redir"))
               redirect(getparam("redir"));
            else
               redirect("");
         }
      }
      return;
   }

   /*---- stop run --------------------------------------------*/

   if (equal_ustring(command, "stop")) {
      if (run_state != STATE_RUNNING && run_state != STATE_PAUSED) {
         show_error("Run is not running");
         return;
      }

      if (!check_web_password(cookie_wpwd, "?cmd=stop", experiment))
         return;

      status = cm_transition(TR_STOP, 0, str, sizeof(str), TR_MTHREAD | TR_ASYNC, FALSE);
      if (status != CM_SUCCESS && status != CM_DEFERRED_TRANSITION)
         show_error(str);
      else if (isparam("redir"))
         redirect(getparam("redir"));
      else
         redirect("");

      requested_old_state = run_state;
      if (status == CM_SUCCESS)
         requested_transition = TR_STOP;

      return;
   }

   /*---- trigger equipment readout ---------------------------*/

   if (strncmp(command, "Trigger", 7) == 0) {
      sprintf(str, "?cmd=%s", command);
      if (!check_web_password(cookie_wpwd, str, experiment))
         return;

      /* extract equipment name */
      strlcpy(eq_name, command + 8, sizeof(eq_name));
      if (strchr(eq_name, ' '))
         *strchr(eq_name, ' ') = 0;

      /* get frontend name */
      sprintf(str, "/Equipment/%s/Common/Frontend name", eq_name);
      size = NAME_LENGTH;
      db_get_value(hDB, 0, str, fe_name, &size, TID_STRING, TRUE);

      /* and ID */
      sprintf(str, "/Equipment/%s/Common/Event ID", eq_name);
      size = sizeof(event_id);
      db_get_value(hDB, 0, str, &event_id, &size, TID_WORD, TRUE);

      if (cm_exist(fe_name, FALSE) != CM_SUCCESS) {
         sprintf(str, "Frontend \"%s\" not running!", fe_name);
         show_error(str);
      } else {
         status = cm_connect_client(fe_name, &hconn);
         if (status != RPC_SUCCESS) {
            sprintf(str, "Cannot connect to frontend \"%s\" !", fe_name);
            show_error(str);
         } else {
            status = rpc_client_call(hconn, RPC_MANUAL_TRIG, event_id);
            if (status != CM_SUCCESS)
               show_error("Error triggering event");
            else
               redirect("");

            cm_disconnect_client(hconn, FALSE);
         }
      }

      return;
   }

   /*---- switch to next subrun -------------------------------------*/

   if (strncmp(command, "Next Subrun", 11) == 0) {
      i = TRUE;
      db_set_value(hDB, 0, "/Logger/Next subrun", &i, sizeof(i), 1, TID_BOOL);
      redirect("");
      return;
   }

   /*---- cancel command --------------------------------------------*/

   if (equal_ustring(command, "cancel")) {

      if (group[0]) {
         /* extract equipment name */
         eq_name[0] = 0;
         if (strncmp(enc_path, "Equipment/", 10) == 0) {
            strlcpy(eq_name, enc_path + 10, sizeof(eq_name));
            if (strchr(eq_name, '/'))
               *strchr(eq_name, '/') = 0;
         }

         /* back to SC display */
         sprintf(str, "SC/%s/%s", eq_name, group);
         redirect(str);
      } else {
         if (isparam("redir"))
            redirect(getparam("redir"));
         else
            redirect("./");
      }

      return;
   }

   /*---- set command -----------------------------------------------*/

   if (equal_ustring(command, "set") && strncmp(dec_path, "SC/", 3) != 0
       && strncmp(dec_path, "CS/", 3) != 0) {

      if (strchr(enc_path, '/'))
         strlcpy(str, strrchr(enc_path, '/') + 1, sizeof(str));
      else
         strlcpy(str, enc_path, sizeof(str));
      strlcat(str, "?cmd=set", sizeof(str));
      if (!check_web_password(cookie_wpwd, str, experiment))
         return;

      strlcpy(str, dec_path, sizeof(str));
      show_set_page(enc_path, sizeof(enc_path), str, group, index, value);
      return;
   }

   /*---- find command ----------------------------------------------*/

   if (equal_ustring(command, "find")) {
      show_find_page(enc_path, value);
      return;
   }

   /*---- create command --------------------------------------------*/

   if (equal_ustring(command, "create")) {
      sprintf(str, "%s?cmd=create", enc_path);
      if (!check_web_password(cookie_wpwd, str, experiment))
         return;

      show_create_page(enc_path, dec_path, value, index, atoi(getparam("type")));
      return;
   }

   /*---- CAMAC CNAF command ----------------------------------------*/

   if (equal_ustring(command, "CNAF") || strncmp(dec_path, "CNAF", 4) == 0) {
      if (!check_web_password(cookie_wpwd, "?cmd=CNAF", experiment))
         return;

      show_cnaf_page();
      return;
   }

#ifdef OBSOLETE
   /*---- alarms command --------------------------------------------*/

   if (equal_ustring(command, "reset all alarms")) {
      if (!check_web_password(cookie_wpwd, "?cmd=reset%20all%20alarms", experiment))
         return;

      al_reset_alarm(NULL);
      redirect("./?cmd=alarms");
      return;
   }

   if (equal_ustring(command, "reset")) {
      if (!check_web_password(cookie_wpwd, "?cmd=reset%20all%20alarms", experiment))
         return;

      al_reset_alarm(dec_path);
      redirect("./?cmd=alarms");
      return;
   }

   if (equal_ustring(command, "Alarms on/off")) {
      redirect("Alarms/Alarm system active?cmd=set");
      return;
   }
#endif

#ifdef OBSOLETE
   /*---- programs command ------------------------------------------*/

   if (equal_ustring(command, "programs")) {
      str[0] = 0;
      for (p=dec_path ; *p ; p++)
         if (*p == '/')
            strlcat(str, "../", sizeof(str));
      if (str[0]) {
         strlcat(str, "./?cmd=programs", sizeof(str));
         redirect(str);
         return;
      }

      str[0] = 0;
      if (getparam("Start") && *getparam("Start"))
         sprintf(str, "?cmd=programs&Start=%s", getparam("Start"));
      if (getparam("Stop") && *getparam("Stop"))
         sprintf(str, "?cmd=programs&Stop=%s", getparam("Stop"));

      if (str[0])
         if (!check_web_password(cookie_wpwd, str, experiment))
            return;

      show_programs_page();
      return;
   }
#endif

   /*---- config command --------------------------------------------*/

   if (equal_ustring(command, "config")) {
      show_config_page(refresh);
      return;
   }

   /*---- Messages command ------------------------------------------*/

   if (equal_ustring(command, "messages")) {
      show_messages_page();
      return;
   }

   /*---- Chat command ------------------------------------------*/
   
   if (equal_ustring(command, "chat")) {
      show_chat_page();
      return;
   }
   
   /*---- ELog command ----------------------------------------------*/

   if (equal_ustring(command, "elog")) {
      get_elog_url(str, sizeof(str));
      redirect(str);
      return;
   }

   if (strncmp(dec_path, "EL/", 3) == 0) {
      if (equal_ustring(command, "new") || equal_ustring(command, "edit")
          || equal_ustring(command, "reply")) {
         sprintf(str, "%s?cmd=%s", dec_path, command);
         if (!check_web_password(cookie_wpwd, str, experiment))
            return;
      }

      strlcpy(str, dec_path + 3, sizeof(str));
      show_elog_page(str, sizeof(str));
      return;
   }

   if (equal_ustring(command, "Create ELog from this page")) {
      strlcpy(str, dec_path, sizeof(str));
      show_elog_page(str, sizeof(str));
      return;
   }

   /*---- accept command --------------------------------------------*/

   if (equal_ustring(command, "accept")) {
      refresh = atoi(getparam("refr"));

      /* redirect with cookie */
      rsprintf("HTTP/1.1 302 Found\r\n");
      rsprintf("Server: MIDAS HTTP %d\r\n", mhttpd_revision());
      rsprintf("Content-Type: text/html; charset=%s\r\n", HTTP_ENCODING);

      time(&now);
      now += 3600 * 24 * 365;
      gmt = gmtime(&now);
      strftime(str, sizeof(str), "%A, %d-%b-%Y %H:00:00 GMT", gmt);

      rsprintf("Set-Cookie: midas_refr=%d; path=/; expires=%s\r\n", refresh, str);
      rsprintf("Location: ./\r\n\r\n<html>redir</html>\r\n");

      return;
   }

   /*---- delete command --------------------------------------------*/

   if (equal_ustring(command, "delete")) {
      sprintf(str, "%s?cmd=delete", enc_path);
      if (!check_web_password(cookie_wpwd, str, experiment))
         return;

      show_delete_page(enc_path, dec_path, value, index);
      return;
   }

   /*---- slow control display --------------------------------------*/

   if (strncmp(dec_path, "SC/", 3) == 0) {
      if (equal_ustring(command, "edit")) {
         sprintf(str, "%s?cmd=Edit&index=%d", dec_path, index);
         if (!check_web_password(cookie_wpwd, str, experiment))
            return;
      }

      show_sc_page(dec_path + 3, refresh);
      return;
   }

   /*---- sequencer page --------------------------------------------*/

   if (equal_ustring(command, "sequencer")) {
      str[0] = 0;
      for (p=dec_path ; *p ; p++)
         if (*p == '/')
            strlcat(str, "../", sizeof(str));
      strlcat(str, "SEQ/", sizeof(str));
      redirect(str);
      return;
   }

   if (strncmp(dec_path, "SEQ/", 4) == 0) {
      show_seq_page();
      return;
   }

   /*---- custom page -----------------------------------------------*/

   if (strncmp(dec_path, "CS/", 3) == 0) {
      if (equal_ustring(command, "edit")) {
         sprintf(str, "%s?cmd=Edit&index=%d", dec_path+3, index);
         if (!check_web_password(cookie_wpwd, str, experiment))
            return;
      }

      show_custom_page(dec_path + 3, cookie_cpwd);
      return;
   }

   if (db_find_key(hDB, 0, "/Custom/Status", &hkey) == DB_SUCCESS && dec_path[0] == 0) {
      if (equal_ustring(command, "edit")) {
         sprintf(str, "%s?cmd=Edit&index=%d", dec_path, index);
         if (!check_web_password(cookie_wpwd, str, experiment))
            return;
      }

      show_custom_page("Status", cookie_cpwd);
      return;
   }

   /*---- show status -----------------------------------------------*/

   if (dec_path[0] == 0) {
      if (elog_mode) {
         redirect("EL/");
         return;
      }

      show_status_page(refresh, cookie_wpwd, expand_equipment);
      return;
   }

   /*---- show ODB --------------------------------------------------*/

   if (dec_path[0]) {
      write_access = TRUE;
      db_find_key(hDB, 0, "/Experiment/Security/Web Password", &hkey);
      if (hkey) {
         size = sizeof(str);
         db_get_data(hDB, hkey, str, &size, TID_STRING);
         if (strcmp(cookie_wpwd, str) == 0)
            write_access = TRUE;
         else
            write_access = FALSE;
      }

      strlcpy(str, dec_path, sizeof(str));
      show_odb_page(enc_path, sizeof(enc_path), str, write_access);
      return;
   }
}

/*------------------------------------------------------------------*/

void decode_query(const char *query_string)
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

         setparam(pitem, p); // decoded query parameters

         p = strtok(NULL, "&");
      }
   }
   free(buf);
}

void decode_get(char *string, const char *cookie_pwd, const char *cookie_wpwd, const char *cookie_cpwd, int refresh, int expand_equipment, bool decode_url, const char* url, const char* query_string)
{
   char path[256];

   //printf("decode_get: string [%s], decode_url %d, url [%s], query_string [%s]\n", string, decode_url, url, query_string);

   initparam();
   if (url)
      strlcpy(path, url + 1, sizeof(path));     /* strip leading '/' */
   else {
      strlcpy(path, string + 1, sizeof(path));     /* strip leading '/' */

      if (strchr(path, '?'))
         *strchr(path, '?') = 0;
   }
   setparam("path", path); // undecoded path, is this used anywhere?

   if (query_string)
      decode_query(query_string);
   else if (string && strchr(string, '?')) {
      char* p = strchr(string, '?') + 1;

      /* cut trailing "/" from netscape */
      if (p[strlen(p) - 1] == '/')
         p[strlen(p) - 1] = 0;

      decode_query(p);
   }

   char dec_path[256];
   strlcpy(dec_path, path, sizeof(dec_path));
   if (decode_url)
      urlDecode(dec_path);

   interprete(cookie_pwd, cookie_wpwd, cookie_cpwd, dec_path, refresh, expand_equipment);

   freeparam();
}

/*------------------------------------------------------------------*/

void decode_post(const char *header, char *string, const char *boundary, int length,
                 const char *cookie_pwd, const char *cookie_wpwd, int refresh, int expand_equipment, bool decode_url, const char* url)
{
   char *pinit, *p, *pitem, *ptmp, file_name[256], str[256], path[256];
   int n;

   initparam();

   if (url)
      strlcpy(path, url + 1, sizeof(path));     /* strip leading '/' */
   else {
      strlcpy(path, header + 1, sizeof(path));     /* strip leading '/' */
      if (strchr(path, '?'))
         *strchr(path, '?') = 0;
      if (strchr(path, ' '))
         *strchr(path, ' ') = 0;
   }
   setparam("path", path); // undecoded path

   _attachment_size[0] = _attachment_size[1] = _attachment_size[2] = 0;
   pinit = string;

   /* return if no boundary defined */
   if (!boundary[0])
      return;

   if (strstr(string, boundary))
      string = strstr(string, boundary) + strlen(boundary);

   do {
      if (strstr(string, "name=")) {
         pitem = strstr(string, "name=") + 5;
         if (*pitem == '\"')
            pitem++;

         if (strncmp(pitem, "attfile", 7) == 0) {
            n = pitem[7] - '1';

            /* evaluate file attachment */
            if (strstr(pitem, "filename=")) {
               p = strstr(pitem, "filename=") + 9;
               if (*p == '\"')
                  p++;
               if (strstr(p, "\r\n\r\n"))
                  string = strstr(p, "\r\n\r\n") + 4;
               else if (strstr(p, "\r\r\n\r\r\n"))
                  string = strstr(p, "\r\r\n\r\r\n") + 6;
               if (strchr(p, '\"'))
                  *strchr(p, '\"') = 0;

               /* set attachment filename */
               strlcpy(file_name, p, sizeof(file_name));
               sprintf(str, "attachment%d", n);
               setparam(str, file_name); // file_name should be decoded?
            } else
               file_name[0] = 0;

            /* find next boundary */
            ptmp = string;
            do {
               while (*ptmp != '-')
                  ptmp++;

               if ((p = strstr(ptmp, boundary)) != NULL) {
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
               _attachment_buffer[n] = string;
               _attachment_size[n] = (POINTER_T) p - (POINTER_T) string;
            }

            string = strstr(p, boundary) + strlen(boundary);
         } else {
            p = pitem;
            if (strstr(p, "\r\n\r\n"))
               p = strstr(p, "\r\n\r\n") + 4;
            else if (strstr(p, "\r\r\n\r\r\n"))
               p = strstr(p, "\r\r\n\r\r\n") + 6;

            if (strchr(pitem, '\"'))
               *strchr(pitem, '\"') = 0;

            if (strstr(p, boundary)) {
               string = strstr(p, boundary) + strlen(boundary);
               *strstr(p, boundary) = 0;
               ptmp = p + (strlen(p) - 1);
               while (*ptmp == '-' || *ptmp == '\n' || *ptmp == '\r')
                  *ptmp-- = 0;
            }
            setparam(pitem, p); // in decode_post()
         }

         while (*string == '-' || *string == '\n' || *string == '\r')
            string++;
      }

   } while ((POINTER_T) string - (POINTER_T) pinit < length);

   char dec_path[256];
   strlcpy(dec_path, path, sizeof(dec_path));
   if (decode_url)
      urlDecode(dec_path);

   interprete(cookie_pwd, cookie_wpwd, "", dec_path, refresh, expand_equipment);
}

/*------------------------------------------------------------------*/

INT check_odb_records(void)
{
   HNDLE hDB, hKeyEq, hKey;
   RUNINFO_STR(runinfo_str);
   int i, status;
   KEY key;

   /* check /Runinfo structure */
   status = cm_get_experiment_database(&hDB, NULL);
   assert(status == DB_SUCCESS);

   status = db_check_record(hDB, 0, "/Runinfo", strcomb(runinfo_str), FALSE);
   if (status == DB_STRUCT_MISMATCH) {
      status = db_check_record(hDB, 0, "/Runinfo", strcomb(runinfo_str), TRUE);
      if (status == DB_SUCCESS) {
         cm_msg(MINFO, "check_odb_records", "ODB subtree /Runinfo corrected successfully");
      } else {
         cm_msg(MERROR, "check_odb_records", "Cannot correct ODB subtree /Runinfo, db_check_record() status %d", status);
         return 0;
      }
   } else if (status == DB_NO_KEY) {
      cm_msg(MERROR, "check_odb_records", "ODB subtree /Runinfo does not exist");
      status = db_create_record(hDB, 0, "/Runinfo", strcomb(runinfo_str));
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
               return 0;
            }
         } else if (status != DB_SUCCESS) {
            cm_msg(MERROR, "check_odb_records", "Cannot correct ODB subtree /Equipment/%s/Common, db_check_record() status %d", key.name, status);
            return 0;
         }
      }
   }

   return CM_SUCCESS;
}


/*------------------------------------------------------------------*/

BOOL _abort = FALSE;

void ctrlc_handler(int sig)
{
   _abort = TRUE;
}

/*------------------------------------------------------------------*/

static std::string toString(int i)
{
   char buf[256];
   sprintf(buf, "%d", i);
   return buf;
}

/*------------------------------------------------------------------*/


static std::vector<std::string> gUserAllowedHosts;
static std::vector<std::string> gAllowedHosts;
static const std::string gOdbAllowedHosts = "/Experiment/Security/mhttpd hosts/Allowed hosts";

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

extern "C" {
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
         printf("Rejecting http connection from \'%s\', getnameinfo() status %d (%s)\n", hname, status, status_string);
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

      printf("Rejecting http connection from \'%s\'\n", hname);
      return 0;
   }
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

#ifdef HAVE_OLDSERVER

char net_buffer[WEB_BUFFER_SIZE];

void server_loop(int tcp_port, int port80_socket)
{
   int status, i, refresh, n_error;
   struct sockaddr_in bind_addr, acc_addr;
   char cookie_pwd[256], cookie_wpwd[256], cookie_cpwd[256], boundary[256], *p;
   int lsock, flag, content_length, header_length;
   unsigned int len;
   struct hostent *local_phe = NULL;
   fd_set readfds;
   struct timeval timeout;

   /* establish Ctrl-C handler */
   ss_ctrlc_handler(ctrlc_handler);

#ifdef OS_WINNT
   {
      WSADATA WSAData;

      /* Start windows sockets */
      if (WSAStartup(MAKEWORD(1, 1), &WSAData) != 0)
         return;
   }
#endif

   if (port80_socket >= 0) {
      lsock = port80_socket;
   } else {

      /* create a new socket */
      lsock = socket(AF_INET, SOCK_STREAM, 0);
      
      if (lsock == -1) {
         printf("Cannot create socket\n");
         return;
      }
      
      /* bind local node name and port to socket */
      memset(&bind_addr, 0, sizeof(bind_addr));
      bind_addr.sin_family = AF_INET;
      bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
      bind_addr.sin_port = htons((short) tcp_port);
      
      /* try reusing address */
      flag = 1;
      setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, (char *) &flag, sizeof(INT));
      status = bind(lsock, (struct sockaddr *) &bind_addr, sizeof(bind_addr));
      
      if (status < 0) {
         printf("Cannot bind to port %d, bind() errno %d (%s)\n", tcp_port, errno, strerror(errno));
         printf("Please try later or use the \"-p\" flag to specify a different port\n");
         return;
      }
   }
      
   /* get host name for mail notification */
   ss_gethostname(host_name, sizeof(host_name));
   
   local_phe = gethostbyname(host_name);
   if (local_phe != NULL)
      local_phe = gethostbyaddr(local_phe->h_addr, sizeof(int), AF_INET);
   
   /* if domain name is not in host name, hope to get it from phe */
   if (local_phe != NULL && strchr(host_name, '.') == NULL)
      strlcpy(host_name, local_phe->h_name, sizeof(host_name));
   
#ifdef OS_UNIX
   /* give up root privilege */
   assert(setuid(getuid()) == 0);
   assert(setgid(getgid()) == 0);
#endif

   /* listen for connection */
   status = listen(lsock, SOMAXCONN);
   if (status < 0) {
      printf("listen() errno %d (%s), bye!\n", errno, strerror(errno));
      return;
   }

   if (port80_socket >= 0) {
      printf("Server listening on port 80 in setuid-root mode\n");
   } else {
      printf("Server listening on port %d...\n", tcp_port);
   }

   do {
      FD_ZERO(&readfds);
      FD_SET(lsock, &readfds);

      timeout.tv_sec = 0;
      timeout.tv_usec = 100000;

#ifdef OS_UNIX
      do {
         status = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);
         /* if an alarm signal was cought, restart with reduced timeout */
      } while (status == -1 && errno == EINTR);
#else
      status = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);
#endif

      if (FD_ISSET(lsock, &readfds)) {

         len = sizeof(acc_addr);
#ifdef OS_WINNT
         _sock = accept(lsock, (struct sockaddr *) &acc_addr, (int *)&len);
#else
         _sock = accept(lsock, (struct sockaddr *) &acc_addr, (socklen_t *)&len);
#endif

         /* save remote host address */
         memcpy(&remote_addr, &(acc_addr.sin_addr), sizeof(remote_addr));

         /* check access control list */
         if (!check_midas_acl((struct sockaddr *) &acc_addr, len)) {
            closesocket(_sock);
            _sock = -1;
            continue;
         }

         /* save remote host address */
         memcpy(&remote_addr, &(acc_addr.sin_addr), sizeof(remote_addr));
         if (verbose) {
            struct hostent *remote_phe;
            char str[256];

            printf("=========== Received request from ");

            remote_phe = gethostbyaddr((char *) &remote_addr, 4, PF_INET);
            if (remote_phe == NULL) {
               /* use IP number instead */
               strlcpy(str, (char *) inet_ntoa(remote_addr), sizeof(str));
            } else
               strlcpy(str, remote_phe->h_name, sizeof(str));

            printf("%s at %s ===========\n", str, ss_asctime());
            fflush(stdout);
         }

         memset(net_buffer, 0, sizeof(net_buffer));
         len = 0;
         header_length = 0;
         content_length = 0;
         n_error = 0;
         bool locked = false;
         do {
            FD_ZERO(&readfds);
            FD_SET(_sock, &readfds);

            timeout.tv_sec = 6;
            timeout.tv_usec = 0;

#ifdef OS_UNIX
            int loop = 0;
            do {
               status = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);
               /* if an alarm signal was cought, restart with reduced timeout */
            } while (status == -1 && errno == EINTR && (++loop < 2));
#else
            status = select(FD_SETSIZE, (fd_set *) &readfds, NULL, NULL, (const timeval *) &timeout);
#endif

            //printf("select status %d, errno %d, isset %d\n", status, errno, FD_ISSET(_sock, &readfds));

            if (status > 0 && FD_ISSET(_sock, &readfds))
               i = recv(_sock, net_buffer + len, sizeof(net_buffer) - len, 0);
            else
               goto error;

            //printf("recv status %d, errno %d, len %d\n", i, errno, len);

            /* abort if connection got broken */
            if (i < 0)
               goto error;

            if (i > 0) {
               len += i;
               net_buffer[len] = 0; // we later use strstr() on net_buffer - have to make sure it is zero-terminated
            }

            /* check if net_buffer too small */
            if (len >= sizeof(net_buffer)) {
               /* drain incoming remaining data */
               do {
                  FD_ZERO(&readfds);
                  FD_SET(_sock, &readfds);

                  timeout.tv_sec = 2;
                  timeout.tv_usec = 0;

                  int loop = 0;
                  do {
                     status = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);
                     /* if an alarm signal was cought, restart with reduced timeout */
                  } while (status == -1 && errno == EINTR && (++loop < 2));

                  if (status <= 0)
                     break;

                  if (!FD_ISSET(_sock, &readfds))
                     break;

                  i = recv(_sock, net_buffer, sizeof(net_buffer), 0);

               } while (i > 0);

               memset(return_buffer, 0, return_size);
               strlen_retbuf = 0;
               return_length = 0;

               show_error("Submitted attachment too large, please increase WEB_BUFFER_SIZE in mhttpd.c and recompile");

               if (return_length == 0)
                  return_length = strlen(return_buffer);

               i = send_tcp(_sock, return_buffer, return_length, 0x10000);

               if (verbose) {
                  printf("########## Return error info %i bytes ##########\n", return_length);
                  puts(return_buffer);
                  printf("\n\n");
               }

               goto error;
            }

            if (i == 0) {
               n_error++;
               if (n_error == 100)
                  goto error;
            }

            /* finish when empty line received (fragmented TCP packets) */
            if (strstr(net_buffer, "\r\n\r\n")) {
               if (strstr(net_buffer, "GET") != NULL && strstr(net_buffer, "POST") == NULL) {
                  if (len > 4 && strcmp(&net_buffer[len - 4], "\r\n\r\n") == 0)
                     break;
                  if (len > 6 && strcmp(&net_buffer[len - 6], "\r\r\n\r\r\n") == 0)
                     break;
               } else if (strstr(net_buffer, "POST") != NULL) {
                  if (header_length == 0) {
                     /* extract header and content length */
                     if (strstr(net_buffer, "Content-Length:"))
                        content_length = atoi(strstr(net_buffer, "Content-Length:") + 15);
                     else if (strstr(net_buffer, "Content-length:"))
                        content_length = atoi(strstr(net_buffer, "Content-length:") + 15);

                     //printf("content-length %d\n", content_length);

                     boundary[0] = 0;
                     if (strstr(net_buffer, "boundary=")) {
                        strlcpy(boundary, strstr(net_buffer, "boundary=") + 9, sizeof(boundary));
                        if (strchr(boundary, '\r'))
                           *strchr(boundary, '\r') = 0;
                     }

                     if (strstr(net_buffer, "\r\n\r\n"))
                        header_length = (POINTER_T) strstr(net_buffer, "\r\n\r\n") - (POINTER_T) net_buffer + 4;

                     if (strstr(net_buffer, "\r\r\n\r\r\n"))
                        header_length = (POINTER_T) strstr(net_buffer, "\r\r\n\r\r\n") - (POINTER_T) net_buffer + 6;
                  }

                  //printf("header_length %d, len %d, header+contents %d\n", header_length, len, header_length + content_length);

                  if (header_length > 0 && (int) len >= header_length + content_length) {
                     if (header_length)
                        net_buffer[header_length - 1] = 0;
                     break;
                  }
               } else if (strstr(net_buffer, "OPTIONS") != NULL)
                  goto error;
               else {
                  printf("%s", net_buffer);
                  goto error;
               }
            }

         } while (1);

         if (!strchr(net_buffer, '\r'))
            goto error;

         /* extract cookies */
         cookie_pwd[0] = 0;
         cookie_wpwd[0] = 0;
         if (strstr(net_buffer, "midas_pwd=") != NULL) {
            strlcpy(cookie_pwd, strstr(net_buffer, "midas_pwd=") + 10,
                    sizeof(cookie_pwd));
            cookie_pwd[strcspn(cookie_pwd, " ;\r\n")] = 0;
         }
         if (strstr(net_buffer, "midas_wpwd=") != NULL) {
            strlcpy(cookie_wpwd, strstr(net_buffer, "midas_wpwd=") + 11,
                    sizeof(cookie_wpwd));
            cookie_wpwd[strcspn(cookie_wpwd, " ;\r\n")] = 0;
         }
         if (strstr(net_buffer, "cpwd=") != NULL) {
            strlcpy(cookie_cpwd, strstr(net_buffer, "cpwd=") + 5,
                    sizeof(cookie_cpwd));
            cookie_cpwd[strcspn(cookie_cpwd, " ;\r\n")] = 0;
         }

         refresh = 0;
         if (strstr(net_buffer, "midas_refr=") != NULL)
            refresh = atoi(strstr(net_buffer, "midas_refr=") + 11);
         else
            refresh = DEFAULT_REFRESH;

         if (strstr(net_buffer, "midas_expeq=") != NULL)
            expand_equipment = atoi(strstr(net_buffer, "midas_expeq=") + 6);
         else
            expand_equipment = FALSE;

         /* extract referer */
         referer[0] = 0;
         if ((p = strstr(net_buffer, "Referer:")) != NULL) {
            p += 9;
            while (*p && *p == ' ')
               p++;
            strlcpy(referer, p, sizeof(referer));
            if (strchr(referer, '\r'))
               *strchr(referer, '\r') = 0;
            if (strchr(referer, '?'))
               *strchr(referer, '?') = 0;
            for (p = referer + strlen(referer) - 1; p > referer && *p != '/'; p--)
               *p = 0;
         }

         memset(return_buffer, 0, return_size);
         strlen_retbuf = 0;

         if (strncmp(net_buffer, "GET", 3) != 0 && strncmp(net_buffer, "POST", 4) != 0)
            goto error;

         return_length = 0;

         if (verbose) {
            INT temp;
            printf("Received buffer of %i bytes :\n%s\n", len, net_buffer);
            fflush(stdout);
            if (strncmp(net_buffer, "POST", 4) == 0) {
               printf("Contents of POST has %i bytes:\n", content_length);
               if (content_length > 2000) {
                  printf("--- Dumping first 2000 bytes only\n");
                  temp = net_buffer[header_length + 2000];
                  net_buffer[header_length + 2000] = 0;
                  puts(net_buffer + header_length);
                  net_buffer[header_length + 2000] = temp;
               } else
                  puts(net_buffer + header_length);
               printf("\n\n");
               fflush(stdout);
            }
         }

         if (strncmp(net_buffer, "GET", 3) == 0) {
            /* extract path and commands */
            *strchr(net_buffer, '\r') = 0;

            if (!strstr(net_buffer, "HTTP"))
               goto error;
            *(strstr(net_buffer, "HTTP") - 1) = 0;

            if (request_mutex) {
               status = ss_mutex_wait_for(request_mutex, 0);
               assert(status == SS_SUCCESS);
               locked = true;
            }
            /* decode command and return answer */
            decode_get(net_buffer + 4, cookie_pwd, cookie_wpwd, cookie_cpwd, refresh, true, NULL, NULL);
         } else {
            if (request_mutex) {
               status = ss_mutex_wait_for(request_mutex, 0);
               assert(status == SS_SUCCESS);
               locked = true;
            }
            decode_post(net_buffer + 5, net_buffer + header_length, boundary,
                        content_length, cookie_pwd, cookie_wpwd, refresh, true, NULL);
         }

         if (return_length != -1) {
            if (return_length == 0)
               return_length = strlen(return_buffer);

            if (verbose) {
               printf("########## Return buffer %i bytes at %s ##########\n", return_length, ss_asctime());
               printf("\n\n");
            }

            i = send_tcp(_sock, return_buffer, return_length, 0x10000);

            if (verbose) {
               if (return_length > 200) {
                  printf("--- Dumping first 200 bytes only\n");
                  return_buffer[200] = 0;
               }
               puts(return_buffer);
               printf("\n\n");
            }

          error:

            closesocket(_sock);
            _sock = -1;
         }

         if (locked) {
            ss_mutex_release(request_mutex);
         }
      }

      /* re-establish ctrl-c handler */
      ss_ctrlc_handler(ctrlc_handler);

      /* check for shutdown message */
      status = cm_yield(0);
      if (status == RPC_SHUTDOWN)
         break;

      /* call sequencer periodically */
      sequencer();

   } while (!_abort);
}

#endif // HAVE_OLDSERVER

/*------------------------------------------------------------------*/

//#define HAVE_MG 1
//#define HAVE_MG4 1
//#define HAVE_MG6 1

#ifdef HAVE_MG

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

#endif

#ifdef HAVE_MG4

#include "mongoose4.h"

static int debug_mg = 0;

static const char* find_header_mg(const struct mg_event *event, const char* name)
{
   for (int i=0; i<event->request_info->num_headers; i++) {
      if (strcmp(event->request_info->http_headers[i].name, name) == 0)
         return event->request_info->http_headers[i].value;
   }
   return NULL;
}

static const char* find_cookie_mg(const struct mg_event *event, const char* cookie_name)
{
   const char* cookies = find_header_mg(event, "Cookie");
   if (!cookies)
      return NULL;
   const char* p = strstr(cookies, cookie_name);
   if (!p)
      return NULL;
   const char* v = p+strlen(cookie_name);
   if (*v != '=')
      return NULL;
   v++;
   //printf("cookie [%s] value [%s]\n", cookie_name, v);
   return v;
}

// This function will be called by mongoose on every new request.
static int event_handler_mg(struct mg_event *event)
{
   int status;
   if (debug_mg)
      printf("mongoose event %d: ", event->type);

   switch (event->type) {
   case MG_REQUEST_BEGIN: {
      if (debug_mg) {
         printf("MG_REQUEST_BEGIN, method [%s], uri [%s], query [%s]\n", event->request_info->request_method, event->request_info->uri, event->request_info->query_string);

         for (int i=0; i<event->request_info->num_headers; i++) {
            printf("Header %d: [%s] = [%s]\n", i, event->request_info->http_headers[i].name, event->request_info->http_headers[i].value);
         }
      }

      // prepare return buffer
      memset(return_buffer, 0, return_size);
      strlen_retbuf = 0;
      return_length = 0;

      const char* p;

      // extract password cookies

      char cookie_pwd[256]; // general access password
      char cookie_wpwd[256]; // "write mode" password
      char cookie_cpwd[256]; // custom page and javascript password

      cookie_pwd[0] = 0;
      cookie_wpwd[0] = 0;
      cookie_cpwd[0] = 0;

      p = find_cookie_mg(event, "midas_pwd");
      if (p) {
         STRLCPY(cookie_pwd, p);
         cookie_pwd[strcspn(cookie_pwd, " ;\r\n")] = 0;
      }

      p = find_cookie_mg(event, "midas_wpwd");
      if (p) {
         STRLCPY(cookie_wpwd, p);
         cookie_wpwd[strcspn(cookie_wpwd, " ;\r\n")] = 0;
      }

      p = find_cookie_mg(event, "cpwd");
      if (p) {
         STRLCPY(cookie_cpwd, p);
         cookie_cpwd[strcspn(cookie_cpwd, " ;\r\n")] = 0;
      }

      // extract refresh rate
      int refresh = DEFAULT_REFRESH;
      p = find_cookie_mg(event, "midas_refr");
      if (p)
         refresh = atoi(p);

      // extract equipment expand flag
      int expand_equipment = 0;
      p = find_cookie_mg(event, "midas_expeq");
      if (p)
         expand_equipment = atoi(p);

      if ((strcmp(event->request_info->request_method, "OPTIONS") == 0) &&
          (event->request_info->query_string != NULL) &&
          (strcmp(event->request_info->query_string, "mjsonrpc") == 0)) {
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

         const char* origin_header = find_header_mg(event, "Origin");

         std::string headers;
         headers += "HTTP/1.1 200 OK\n";
         //headers += "Date: Sat, 08 Jul 2006 12:04:08 GMT\n";
         if (origin_header)
            headers += "Access-Control-Allow-Origin: " + std::string(origin_header) + "\n";
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

         mg_write(event->conn, send.c_str(), send.length());

         return 1;
      }

      if ((strcmp(event->request_info->request_method, "GET") == 0) &&
          (event->request_info->query_string != NULL) &&
          (strcmp(event->request_info->query_string, "mjsonrpc_schema") == 0)) {

         MJsonNode* s = mjsonrpc_get_schema();
         std::string reply = s->Stringify();
         delete s;

         int reply_length = reply.length();

         const char* origin_header = find_header_mg(event, "Origin");

         std::string headers;
         headers += "HTTP/1.1 200 OK\n";
         //headers += "Connection: close\n";
         if (origin_header)
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
         
         mg_write(event->conn, send.c_str(), send.length());
         
         return 1;
      }

      if ((strcmp(event->request_info->request_method, "GET") == 0) &&
          (event->request_info->query_string != NULL) &&
          (strcmp(event->request_info->query_string, "mjsonrpc_schema_text") == 0)) {

         MJsonNode* s = mjsonrpc_get_schema();
         std::string reply = mjsonrpc_schema_to_text(s);
         delete s;

         int reply_length = reply.length();

         const char* origin_header = find_header_mg(event, "Origin");

         std::string headers;
         headers += "HTTP/1.1 200 OK\n";
         //headers += "Connection: close\n";
         if (origin_header)
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

         mg_write(event->conn, send.c_str(), send.length());

         return 1;
      }

      if ((strcmp(event->request_info->request_method, "POST") == 0) &&
          (event->request_info->query_string != NULL) &&
          (strcmp(event->request_info->query_string, "mjsonrpc") == 0)) {
         const char* ctype_header = find_header_mg(event, "Content-Type");

         if (strstr(ctype_header, "application/json") == NULL) {
            std::string headers;
            headers += "HTTP/1.1 415 Unsupported Media Type\n";
            //headers += "Connection: close\n";
            //headers += "Date: Sat, 08 Jul 2006 12:04:08 GMT\n";

            //printf("sending headers: %s\n", headers.c_str());
            //printf("sending reply: %s\n", reply.c_str());

            std::string send = headers + "\n";

            mg_write(event->conn, send.c_str(), send.length());

            return 1;
         }

         const char* clength_header = find_header_mg(event, "Content-Length");
         if (clength_header) {
            int clength = atoi(clength_header);

            char *post_data = (char *)malloc(clength+1);

            int len = mg_read(event->conn, post_data, clength);

            // make sure we have a zero-terminated string
            if (len > 0) {
               post_data[len] = 0;

               status = ss_mutex_wait_for(request_mutex, 0);
               assert(status == SS_SUCCESS);

               std::string reply = mjsonrpc_decode_post_data(post_data);

               ss_mutex_release(request_mutex);

               free(post_data);
               post_data = NULL;

               int reply_length = reply.length();

               const char* origin_header = find_header_mg(event, "Origin");

               std::string headers;
               headers += "HTTP/1.1 200 OK\n";
               //headers += "Connection: close\n";
               if (origin_header)
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

               mg_write(event->conn, send.c_str(), send.length());

               return 1;
            }

            free(post_data);
         }

         return 0;
      }

      bool locked = false;

      if (strcmp( event->request_info->request_method, "GET") == 0) {
         status = ss_mutex_wait_for(request_mutex, 0);
         assert(status == SS_SUCCESS);
         locked = true;
         decode_get(NULL, cookie_pwd, cookie_wpwd, cookie_cpwd, refresh, false, event->request_info->uri, event->request_info->query_string);
      } else if (strcmp( event->request_info->request_method, "POST") == 0) {

         int max_post_data = 1024*1024;
         char *post_data = (char *)malloc(max_post_data);
         // User has submitted a form, show submitted data and a variable value
         int post_data_len = mg_read(event->conn, post_data, max_post_data);

         char boundary[256];
         boundary[0] = 0;
         const char* ct = find_header_mg(event, "Content-Type");
         if (ct) {
            const char* s = strstr(ct, "boundary=");
            if (s)
               strlcpy(boundary, s+9, sizeof(boundary));
         }

         //printf("post_data_len %d, data [%s], boundary [%s]\n", post_data_len, post_data, boundary);

         status = ss_mutex_wait_for(request_mutex, 0);
         assert(status == SS_SUCCESS);
         locked = true;
         decode_post(NULL, post_data, boundary, post_data_len, cookie_pwd, cookie_wpwd, refresh, false, event->request_info->uri);
         free(post_data);
      }

      if (debug_mg)
         printf("mongoose: return buffer length %d bytes (%d)\n", return_length, (int)strlen(return_buffer));

      if (return_length != -1) {
         if (return_length == 0)
            return_length = strlen(return_buffer);

         if (debug_mg)
            printf("mongoose: corrected return buffer length %d bytes\n", return_length);

         char* buf = (char*)malloc(return_length);
         assert(buf != NULL);

         memcpy(buf, return_buffer, return_length);

         if (locked)
            ss_mutex_release(request_mutex);

         mg_write(event->conn, buf, return_length);

         free(buf);
         buf = NULL;

         return 1;
      }

      if (locked)
         ss_mutex_release(request_mutex);

      return 0;

      // Returning non-zero tells mongoose that our function has replied to
      // the client, and mongoose should not send client any more data.
      // return 1; // return value "1" means we send reply to client. return value "0" means we do not know what to do with this.
   }
   case MG_REQUEST_END:
      if (debug_mg)
         printf("MG_REQUEST_END\n");
      return 0; // return value ignored
   case MG_HTTP_ERROR:
      // NOTE: messages with code 500 and no other information are generated when then client closes the connection
      if (debug_mg) {
         printf("MG_HTTP_ERROR, error code %d", (int)(long)event->event_param);
         if (event->request_info)
            printf(", method [%s], uri [%s]", event->request_info->request_method, event->request_info->uri);
         printf("\n");
      }
      return 0; // return value "1" means we have sent our own custon error response, value "0" means mongoose sends it's default response
   case MG_EVENT_LOG:
      if (debug_mg)
         printf("MG_EVENT_LOG, message: %s\n", (const char*)event->event_param);
      cm_msg(MERROR, "mongoose", "mongoose web server error: %s", (const char*)event->event_param);
      return 1; // return value "1" means we logged the message, value "0" means mongoose logs the message somewhere we do not know where
   case MG_THREAD_BEGIN:
      if (debug_mg)
         printf("MG_THREAD_BEGIN\n");
      return 0; // return value ignored
   case MG_THREAD_END:
      if (debug_mg)
         printf("MG_THREAD_END\n");
      return 0; // return value ignored
   default:
      if (debug_mg)
         printf("unknown request, event->type=%d\n", event->type);
      return 0; // not handled by us
   }

   // NOT REACHED
   // We do not handle any other event
   return 0;
}

static struct mg_context *ctx_mg = NULL;

#include <vector>
#include <string>

static std::vector<std::string> options_mg;

void add_option_mg(const char* name, const char* value)
{
   options_mg.push_back(name);
   options_mg.push_back(value);
}

const char** get_options_mg()
{
   int size = options_mg.size();
   const char** s = (const char**)malloc(sizeof(char*)*(size+1));
   for (int i=0; i<size; i++)
      s[i] = options_mg[i].c_str();
   s[size] = NULL;
   return s;
}

int start_mg(int user_http_port, int user_https_port, int socket_priviledged_port, int verbose)
{
   HNDLE hDB;
   int size;
   int status;

   if (socket_priviledged_port >= 0) {
      printf("Mongoose version 4 cannot listen to port 80 in setuid mode. Please use mongoose version 6. Sorry, bye!\n");
      exit(1);
   }

   if (verbose)
      debug_mg = 1;

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

   {
      HNDLE hKey;

      status = db_find_key(hDB, 0, "/Experiment/Mongoose listening_port", &hKey);
      if (status == DB_SUCCESS)
         cm_msg(MERROR, "mongoose", "ODB \"/Experiment/Mongoose listening_port\" is obsolete, please delete it");

      status = db_find_key(hDB, 0, "/Experiment/Mongoose access_control_list", &hKey);
      if (status == DB_SUCCESS)
         cm_msg(MERROR, "mongoose", "ODB \"/Experiment/Mongoose access_control_list\" is obsolete, please delete it");
   }

   bool need_cert_file = false;
   bool need_password_file = false;

   add_option_mg("num_threads", "1");

   std::string listening_ports;

   //STRLCPY(listening_ports, "8080r,8443s");

   if (user_http_port || user_https_port) { // use user ports
      if (user_http_port)
         listening_ports += toString(user_http_port);
      if (user_https_port) {
         if (listening_ports.length() > 0)
            listening_ports += ",";
         listening_ports += toString(user_https_port);
         listening_ports += "s";
         if (!user_http_port)
            need_password_file = true; // passwords only if non-https port is disabled
      }
   } else {
      if (http_port) {
         listening_ports += toString(http_port);
         if (https_port && http_redirect_to_https)
            listening_ports += "r";
      }
      if (https_port) {
         if (listening_ports.length() > 0)
            listening_ports += ",";
         listening_ports += toString(https_port);
         listening_ports += "s";
         if (!http_port || http_redirect_to_https)
            need_password_file = true; // passwords only if non-https port is disabled or redirects to https
      }
   }

   printf("Mongoose web server will listen on ports \"%s\"\n", listening_ports.c_str());

   if (listening_ports.length() < 1) {
      cm_msg(MERROR, "mongoose", "cannot start: no ports defined");
      return SS_FILE_ERROR;
   }

   add_option_mg("listening_ports", listening_ports.c_str());

   if (https_port || user_https_port) {
      need_cert_file = true;
   }

   if (need_cert_file) {
      std::string path;
      status = find_file_mg("ssl_cert.pem", path, NULL, debug_mg>0);

      if (status != SUCCESS) {
         cm_msg(MERROR, "mongoose", "cannot find SSL certificate file \"%s\"", path.c_str());
         cm_msg(MERROR, "mongoose", "please create SSL certificate file: cd $MIDASSYS; openssl req -new -nodes -newkey rsa:2048 -sha256 -out ssl_cert.csr -keyout ssl_cert.key; openssl x509 -req -days 365 -sha256 -in ssl_cert.csr -signkey ssl_cert.key -out ssl_cert.pem; cat ssl_cert.key >> ssl_cert.pem");
         return SS_FILE_ERROR;
      }

      printf("Mongoose web server will use SSL certificate file \"%s\"\n", path.c_str());
      add_option_mg("ssl_certificate", path.c_str());
   }

   if (need_password_file) {
      char realm[256];
      realm[0] = 0;
      cm_get_experiment_name(realm, sizeof(realm));

      if (strlen(realm) < 1)
         STRLCPY(realm, "midas");

      std::string path;
      FILE *fp;
      status = find_file_mg("htpasswd.txt", path, &fp, debug_mg>0);

      bool realm_ok = false;

      if (fp) { // check that the password file has our realm name
         while (1) {
            char buf[256];
            char* s = fgets(buf, sizeof(buf), fp);
            if (!s)
               break; // end of file
            // format is: user:realm:password
            //printf("[%s]\n", s);
            char* ss = strchr(s, ':');
            if (ss) {
               ss++;
               char* sss = strchr(ss, ':');
               if (sss) {
                  //printf("[%s] ss [%s] sss [%s]\n", s, ss, sss);
                  *sss = 0;
                  if (strcmp(ss, realm) == 0) {
                     // found at least one entry with matching realm name
                     realm_ok = true;
                     break;
                  }
               }
            }
         }
         fclose(fp);
         fp = NULL;
      }

      if (status != SUCCESS) {
         cm_msg(MERROR, "mongoose", "mongoose web server cannot find password file \"%s\"", path.c_str());
         cm_msg(MERROR, "mongoose", "please create password file: htdigest -c %s %s midas", path.c_str(), realm);
         return SS_FILE_ERROR;
      }

      if (!realm_ok) {
         cm_msg(MERROR, "mongoose", "mongoose web server password file \"%s\" has no passwords for realm \"%s\"", path.c_str(), realm);
         cm_msg(MERROR, "mongoose", "please add passwords: htdigest %s %s midas", path.c_str(), realm);
         return SS_FILE_ERROR;
      }

      // create or overwrite exiting password file: htdigest -c htpasswd.txt expt midas
      add_option_mg("authentication_domain", realm);
      add_option_mg("global_auth_file", path.c_str());

      printf("Mongoose web server will use authentication realm \"%s\", password file \"%s\"\n", realm, path.c_str());
   }

   //if (strlen(mongoose_acl) > 0) {
   //   printf("Web server access control list: \"%s\"\n", mongoose_acl);
   //   add_option_mg("access_control_list", mongoose_acl);
   //}

   const char** options = get_options_mg();

   if (debug_mg)
      printf("start_mg!\n");

#ifndef OS_WINNT
   signal(SIGPIPE, SIG_IGN);
#endif

   if (!request_mutex) {
      status = ss_mutex_create(&request_mutex);
      assert(status==SS_SUCCESS || status==SS_CREATED);
   }

   // Start the web server.
   ctx_mg = mg_start(options, &event_handler_mg, NULL);

   if (debug_mg)
      printf("start_mg: ctx %p\n", ctx_mg);

   return SUCCESS;
}

int stop_mg()
{
   if (debug_mg)
      printf("stop_mg!\n");
   // Stop the server.
   if (ctx_mg)
      mg_stop(ctx_mg);
   ctx_mg = NULL;
   if (debug_mg)
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

      status = ss_mutex_wait_for(request_mutex, 0);

      /* check for shutdown message */
      status = cm_yield(0);
      if (status == RPC_SHUTDOWN)
         break;

      /* call sequencer periodically */
      sequencer();

      status = ss_mutex_release(request_mutex);

      ss_sleep(10);
   }

   return status;
}

#endif

#ifdef HAVE_MG6

#include "mongoose6.h"

static bool verbose_mg = false;
static bool trace_mg = false;
static struct mg_mgr mgr_mg;

struct AuthEntry {
   std::string username;
   std::string realm;
   std::string password;
};

struct Auth {
   bool active;
   std::string realm;
   std::string passwd_filename;
   std::vector<AuthEntry> passwords;
};

static Auth auth_mg;

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

static std::string check_digest_auth(struct http_message *hm, Auth* auth)
{
   char user[255], cnonce[33], response[40], uri[4000], qop[20], nc[20], nonce[30];
   char expected_response[33];

   //printf("HereA!\n");
   
   /* Parse "Authorization:" header, fail fast on parse error */
   struct mg_str *hdr = mg_get_http_header(hm, "Authorization");
 
   if (!hdr)
      return "";

   //printf("HereB!\n");

   if (mg_http_parse_header(hdr, "username", user, sizeof(user)) == 0) return "";
   //printf("HereB1!\n");
   if (mg_http_parse_header(hdr, "cnonce", cnonce, sizeof(cnonce)) == 0) return "";
   //printf("HereB2!\n");
   if (mg_http_parse_header(hdr, "response", response, sizeof(response)) == 0) return "";
   //printf("HereB3!\n");
   if (mg_http_parse_header(hdr, "uri", uri, sizeof(uri)) == 0) return "";
   //printf("HereB4!\n");
   if (mg_http_parse_header(hdr, "qop", qop, sizeof(qop)) == 0) return "";
   //printf("HereB5!\n");
   if (mg_http_parse_header(hdr, "nc", nc, sizeof(nc)) == 0) return "";
   //printf("HereB6!\n");
   if (mg_http_parse_header(hdr, "nonce", nonce, sizeof(nonce)) == 0) return "";
   //printf("HereB7!\n");
   if (xmg_check_nonce(nonce) == 0) return "";
   //printf("HereB8!\n");

   //printf("HereC!\n");

   const char* uri_end = strchr(hm->uri.p, ' ');
   if (!uri_end) return "";

   int uri_length = uri_end - hm->uri.p;

   if (uri_length != (int)strlen(uri))
      return "";

   int cmp = strncmp(hm->uri.p, uri, uri_length);

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
                    nonce, strlen(nonce),
                    nc, strlen(nc),
                    cnonce, strlen(cnonce),
                    qop, strlen(qop),
                    expected_response);
      //printf("digest_auth: expected %s, got %s\n", expected_response, response);
      if (mg_casecmp(response, expected_response) == 0) {
         return e->username;
      }
    }

   return "";
}

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

static bool handle_decode_get(struct mg_connection *nc, const http_message* msg, const char* uri, const char* query_string)
{
   int status;
   
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
   int refresh = DEFAULT_REFRESH;
   s = find_cookie_mg(msg, "midas_refr");
   if (s.length() > 0)
      refresh = atoi(s.c_str());

   // extract expand flag
   int expand = 0;
   s = find_cookie_mg(msg, "midas_expeq");
   if (s.length() > 0)
      expand = atoi(s.c_str());
   
   // lock shared structures
   
   status = ss_mutex_wait_for(request_mutex, 0);
   assert(status == SS_SUCCESS);

   // prepare return buffer

   memset(return_buffer, 0, return_size);
   strlen_retbuf = 0;
   return_length = 0;

   // call midas
   
   decode_get(NULL, cookie_pwd, cookie_wpwd, cookie_cpwd, refresh, expand, false, uri, query_string);

   if (trace_mg)
      printf("handle_decode_get: return buffer length %d bytes, strlen %d\n", return_length, (int)strlen(return_buffer));

   if (return_length == -1) {
      ss_mutex_release(request_mutex);
      return false;
   }

   if (return_length == 0)
      return_length = strlen(return_buffer);
   
   char* buf = (char*)malloc(return_length);
   assert(buf != NULL);
   
   memcpy(buf, return_buffer, return_length);
   
   ss_mutex_release(request_mutex);
   
   mg_send(nc, buf, return_length);

   if (!strstr(buf, "Content-Length")) {
      // cannot do pipelined http if response generated by mhttpd
      // decode_get() has no Content-Length header.
      // must close the connection.
      nc->flags |= MG_F_SEND_AND_CLOSE;
   }
   
   free(buf);
   buf = NULL;
   
   return true;
}

static bool handle_decode_post(struct mg_connection *nc, const http_message* msg, const char* uri, const char* query_string)
{
   int status;
   
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
   int refresh = DEFAULT_REFRESH;
   s = find_cookie_mg(msg, "midas_refr");
   if (s.length() > 0)
      refresh = atoi(s.c_str());

   // extract equipment expand flag
   int expand_equipment = 0;
   s = find_cookie_mg(msg, "midas_expeq");
   if (s.length() > 0)
      expand_equipment = atoi(s.c_str());

   char boundary[256];
   boundary[0] = 0;
   const std::string ct = find_header_mg(msg, "Content-Type");
   if (ct.length() > 0) {
      const char* s = strstr(ct.c_str(), "boundary=");
      if (s)
         strlcpy(boundary, s+9, sizeof(boundary));
   }

   const char* post_data = msg->body.p;
   int post_data_len = msg->body.len;

   // lock shared strctures
   
   status = ss_mutex_wait_for(request_mutex, 0);
   assert(status == SS_SUCCESS);

   // prepare return buffer

   memset(return_buffer, 0, return_size);
   strlen_retbuf = 0;
   return_length = 0;
   
   //printf("post_data_len %d, data [%s], boundary [%s]\n", post_data_len, post_data, boundary);

   decode_post(NULL, (char*)post_data, boundary, post_data_len, cookie_pwd, cookie_wpwd,
               refresh, expand_equipment, false, uri);

   if (trace_mg)
      printf("handle_decode_post: return buffer length %d bytes, strlen %d\n", return_length, (int)strlen(return_buffer));

   if (return_length == -1) {
      ss_mutex_release(request_mutex);
      return false;
   }

   if (return_length == 0)
      return_length = strlen(return_buffer);
   
   char* buf = (char*)malloc(return_length);
   assert(buf != NULL);
   
   memcpy(buf, return_buffer, return_length);
   
   ss_mutex_release(request_mutex);
   
   mg_send(nc, buf, return_length);

   if (!strstr(buf, "Content-Length")) {
      // cannot do pipelined http if response generated by mhttpd
      // decode_get() has no Content-Length header.
      // must close the connection.
      nc->flags |= MG_F_SEND_AND_CLOSE;
   }
   
   free(buf);
   buf = NULL;
   
   return true;
}

static bool handle_http_get(struct mg_connection *nc, const http_message* msg, const char* uri)
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
      
      mg_send(nc, send.c_str(), send.length());
      
      return true;
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
      
      mg_send(nc, send.c_str(), send.length());
      
      return true;
   }
   
   return handle_decode_get(nc, msg, uri, query_string.c_str());
}

static bool handle_http_post(struct mg_connection *nc, const http_message* msg, const char* uri)
{
   std::string query_string = mgstr(&msg->query_string);
   std::string post_data = mgstr(&msg->body);

   if (trace_mg||verbose_mg)
      printf("handle_http_post: uri [%s], query [%s], post data %d bytes\n", uri, query_string.c_str(), (int)post_data.length());

   if (query_string == "mjsonrpc") {
      const std::string ctype_header = find_header_mg(msg, "Content-Type");
      
      if (strstr(ctype_header.c_str(), "application/json") == NULL) {
         std::string headers;
         headers += "HTTP/1.1 415 Unsupported Media Type\n";
         //headers += "Date: Sat, 08 Jul 2006 12:04:08 GMT\n";
         
         //printf("sending headers: %s\n", headers.c_str());
         //printf("sending reply: %s\n", reply.c_str());
         
         std::string send = headers + "\n";
         
         mg_send(nc, send.c_str(), send.length());
         
         return true;
      }

      //printf("post body: %s\n", post_data.c_str());
      
      int status = ss_mutex_wait_for(request_mutex, 0);
      assert(status == SS_SUCCESS);
         
      std::string reply = mjsonrpc_decode_post_data(post_data.c_str());
         
      ss_mutex_release(request_mutex);
         
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
      
      mg_send(nc, send.c_str(), send.length());
      
      return true;
   }

   return handle_decode_post(nc, msg, uri, query_string.c_str());
}

static bool handle_http_options_cors(struct mg_connection *nc, const http_message* msg)
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
   
   mg_send(nc, send.c_str(), send.length());
   
   return true;
}

// HTTP event handler

static void handle_http_message(struct mg_connection *nc, http_message* msg)
{
   std::string method = mgstr(&msg->method);
   std::string query_string = mgstr(&msg->query_string);
   std::string uri_encoded = mgstr(&msg->uri);
   std::string uri = UrlDecode(uri_encoded.c_str());
   
   if (trace_mg)
      printf("handle_http_message: method [%s] uri [%s] proto [%s]\n", method.c_str(), uri.c_str(), mgstr(&msg->proto).c_str());

   bool response_sent = false;

   // process OPTIONS for Cross-origin (CORS) preflight request
   // see https://developer.mozilla.org/en-US/docs/Web/HTTP/Access_control_CORS
   if (method == "OPTIONS" && query_string == "mjsonrpc" && mg_get_http_header(msg, "Access-Control-Request-Method") != NULL) {
      handle_http_options_cors(nc, msg);
      return;
   }

   if (auth_mg.active) {
      std::string username = check_digest_auth(msg, &auth_mg);
      
      // if auth failed, reread password file - maybe user added or password changed
      if (username.length() < 1) {
         bool ok = read_passwords(&auth_mg);
         if (ok)
            username = check_digest_auth(msg, &auth_mg);
      }

      if (trace_mg)
         printf("handle_http_message: auth user: \"%s\"\n", username.c_str());
      
      if (username.length() == 0) {
         if (trace_mg||verbose_mg)
            printf("handle_http_message: method [%s] uri [%s] query [%s] proto [%s], sending auth request for realm \"%s\"\n", method.c_str(), uri.c_str(), query_string.c_str(), mgstr(&msg->proto).c_str(), auth_mg.realm.c_str());

         xmg_http_send_digest_auth_request(nc, auth_mg.realm.c_str());
         return;
      }
   }

   if (method == "GET")
      response_sent = handle_http_get(nc, msg, uri.c_str());
   else if (method == "POST")
      response_sent = handle_http_post(nc, msg, uri.c_str());

   if (!response_sent) {
      if (trace_mg||verbose_mg)
         printf("handle_http_message: sending 501 Not Implemented error\n");

      std::string response = "501 Not Implemented";
      mg_send_head(nc, 501, response.length(), NULL); // 501 Not Implemented
      mg_send(nc, response.c_str(), response.length());
   }
}

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
         cm_msg(MERROR, "mongoose", "please create SSL certificate file: cd $MIDASSYS; openssl req -new -nodes -newkey rsa:2048 -sha256 -out ssl_cert.csr -keyout ssl_cert.key; openssl x509 -req -days 365 -sha256 -in ssl_cert.csr -signkey ssl_cert.key -out ssl_cert.pem; cat ssl_cert.key >> ssl_cert.pem");
         return SS_FILE_ERROR;
      }

      printf("Mongoose web server will use SSL certificate file \"%s\"\n", cert_file.c_str());
   }

   auth_mg.active = false;

   if (need_password_file) {
      char exptname[256];
      exptname[0] = 0;
      cm_get_experiment_name(exptname, sizeof(exptname));

      if (strlen(exptname) > 0)
         auth_mg.realm = exptname;
      else
         auth_mg.realm = "midas";

      bool ok = read_passwords(&auth_mg);
      if (!ok) {
         cm_msg(MERROR, "mongoose", "mongoose web server password file \"%s\" has no passwords for realm \"%s\"", auth_mg.passwd_filename.c_str(), auth_mg.realm.c_str());
         cm_msg(MERROR, "mongoose", "please add passwords by running: htdigest %s %s midas", auth_mg.passwd_filename.c_str(), auth_mg.realm.c_str());
         return SS_FILE_ERROR;
      }

      auth_mg.active = true;

      printf("Mongoose web server will use authentication realm \"%s\", password file \"%s\"\n", auth_mg.realm.c_str(), auth_mg.passwd_filename.c_str());
   }

   if (trace_mg)
      printf("start_mg!\n");

#ifndef OS_WINNT
   signal(SIGPIPE, SIG_IGN);
#endif

   if (!request_mutex) {
      status = ss_mutex_create(&request_mutex);
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

      status = ss_mutex_wait_for(request_mutex, 0);

      /* check for shutdown message */
      status = cm_yield(0);
      if (status == RPC_SHUTDOWN)
         break;

      /* call sequencer periodically */
      sequencer();

      status = ss_mutex_release(request_mutex);

      //ss_sleep(10);

      mg_mgr_poll(&mgr_mg, 10);
   }

   return status;
}

#endif

/*------------------------------------------------------------------*/

int main(int argc, const char *argv[])
{
   int status;
   int daemon = FALSE;
   char str[256];
   int user_http_port = 0;
   int user_https_port = 0;
   int use_mg = 1;
#ifdef HAVE_OLDSERVER
   int use_oldserver = 0;
   int use_oldserver_port = 80;
#endif
   const char *myname = "mhttpd";
   
   setbuf(stdout, NULL);
   setbuf(stderr, NULL);
#ifdef SIGPIPE
   /* avoid getting killed by "Broken pipe" signals */
   signal(SIGPIPE, SIG_IGN);
#endif

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
   
   /* get default from environment */
   cm_get_environment(midas_hostname, sizeof(midas_hostname), midas_expt, sizeof(midas_expt));

   /* parse command line parameters */
   gUserAllowedHosts.clear();
   for (int i = 1; i < argc; i++) {
      if (argv[i][0] == '-' && argv[i][1] == 'D')
         daemon = TRUE;
      else if (argv[i][0] == '-' && argv[i][1] == 'v')
         verbose = TRUE;
      else if (argv[i][0] == '-' && argv[i][1] == 'E')
         elog_mode = TRUE;
      else if (argv[i][0] == '-' && argv[i][1] == 'H') {
         history_mode = TRUE;
      } else if (strcmp(argv[i], "--http") == 0) {
         if (argv[i+1]) {
            user_http_port = atoi(argv[i+1]);
         }
      } else if (strcmp(argv[i], "--https") == 0) {
         if (argv[i+1]) {
            user_https_port = atoi(argv[i+1]);
         }
      } else if (strcmp(argv[i], "--nomg") == 0) {
         use_mg = 0;
#ifdef HAVE_OLDSERVER
      } else if (strcmp(argv[i], "--oldserver") == 0) {
         use_oldserver = 1;
         if (argv[i+1]) {
            int port = atoi(argv[i+1]);
            if (port > 0) {
               i++;
               use_oldserver_port = port;
            }
         }
      } else if (strcmp(argv[i], "--nooldserver") == 0) {
         use_oldserver = 0;
#endif
      } else if (argv[i][0] == '-') {
         if (i + 1 >= argc || argv[i + 1][0] == '-')
            goto usage;
         if (argv[i][1] == 'h')
            strlcpy(midas_hostname, argv[++i], sizeof(midas_hostname));
         else if (argv[i][1] == 'e')
            strlcpy(midas_expt, argv[++i], sizeof(midas_hostname));
         else if (argv[i][1] == 'a') {
            gUserAllowedHosts.push_back(argv[++i]);
         } else if (argv[i][1] == 'p') {
            printf("Option \"-p port_number\" for the old web server is obsolete.\n");
            printf("mongoose web server is the new default, port number is set in ODB or with \"--http port_number\".\n");
            printf("To run the obsolete old web server, please use \"--oldserver\" switch.\n");
            return 1;
         } else {
          usage:
            printf("usage: %s [-h Hostname[:port]] [-e Experiment] [-v] [-D] [-a Hostname]\n\n", argv[0]);
            printf("       -a only allow access for specific host(s), several [-a Hostname] statements might be given (default list is ODB \"/Experiment/security/mhttpd hosts/allowed hosts\")\n");
            printf("       -e experiment to connect to\n");
            printf("       -h connect to midas server (mserver) on given host\n");
            printf("       -v display verbose HTTP communication\n");
            printf("       -D become a daemon\n");
            printf("       -E only display ELog system\n");
            printf("       -H only display history plots\n");
            printf("       --http port - bind to specified HTTP port (default is ODB \"/Experiment/midas http port\")\n");
            printf("       --https port - bind to specified HTTP port (default is ODB \"/Experiment/midas https port\")\n");
#ifdef HAVE_MG
            printf("       --nomg use the old mhttpd web server\n");
#endif
#ifdef HAVE_OLDSERVER
            printf("       --oldserver [port] - use the old web server on given port\n");
            printf("       --nooldserver - do not use the old mhttpd web server\n");
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
      cm_get_error(status, str);
      puts(str);
   } else if (status != CM_SUCCESS) {
      cm_get_error(status, str);
      puts(str);
      return 1;
   }

   /* do ODB record checking */
   if (!check_odb_records()) {
      // check_odb_records() fails with nothing printed to the terminal
      // because mhttpd does not print cm_msg(MERROR, ...) messages to the terminal.
      // At least print something!
      printf("check_odb_records() failed, see messages and midas.log, bye!\n");
      cm_disconnect_experiment();
      return 1;
   }

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

   /* initialize menu buttons */
   init_menu_buttons();

   /* initialize sequencer */
   init_sequencer();

   /* initialize the JSON RPC handlers */
   mjsonrpc_init();

#ifdef HAVE_MG
   if (use_mg) {
      status = start_mg(user_http_port, user_https_port, socket_priviledged_port, verbose);
      if (status != SUCCESS) {
         // At least print something!
         printf("could not start the mongoose web server, see messages and midas.log, bye!\n");
         cm_disconnect_experiment();
         return 1;
      }
   }
#endif

   /* place a request for system messages */
   cm_msg_register(receive_message);

   /* redirect message display, turn on message logging */
   cm_set_msg_print(MT_ALL, MT_ALL, print_message);

#if defined(HAVE_MG) && defined(HAVE_OLDSERVER)
   if (use_oldserver)
      server_loop(use_oldserver_port, socket_priviledged_port);
   else if (use_mg)
      loop_mg();
#elif defined(HAVE_MG)
   if (use_mg)
      loop_mg();
#elif defined(HAVE_OLDSERVER)
   if (use_oldserver)
      server_loop(use_oldserver_port, socket_priviledged_port);
#else
#error Have neither mongoose web server nor old web server. Please define HAVE_MG or HAVE_OLDSERVER or both
#endif

#ifdef HAVE_MG
   if (use_mg)
      stop_mg();
#endif

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
