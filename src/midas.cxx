/********************************************************************\

  Name:         MIDAS.C
  Created by:   Stefan Ritt

  Contents:     MIDAS main library funcitons

  $Id$

\********************************************************************/

#undef NDEBUG // midas required assert() to be always enabled

#include "midas.h"
#include "msystem.h"
#include "git-revision.h"

#include <assert.h>
#include <signal.h>
#include <sys/resource.h>

#ifndef HAVE_STRLCPY
#include "strlcpy.h"
#endif

#include <mutex>
#include <deque>
#include <thread>
#include <atomic>
#include <algorithm>

/**dox***************************************************************/
/** @file midas.c
The main core C-code for Midas.
*/

/**
\mainpage MIDAS code documentation

Welcome to the doxygen-generated documentation for the MIDAS source code.

This documentation is intended to be used as reference by MIDAS developers
and advanced users.

Documentation for new users, general information on MIDAS, examples,
user discussion, mailing lists and forums,
can be found through the MIDAS Wiki at http://midas.triumf.ca
*/

/** @defgroup cmfunctionc Common Functions (cm_xxx)
 */
/** @defgroup bmfunctionc Event Buffer Functions (bm_xxx)
 */
/** @defgroup msgfunctionc Message Functions (msg_xxx)
 */
/** @defgroup bkfunctionc Data Bank Functions (bk_xxx)
 */
/** @defgroup rpc_xxx RPC Functions (rpc_xxx)
 */
/** @defgroup rbfunctionc Ring Buffer Functions (rb_xxx)
 */

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/********************************************************************/
/* data type sizes */
static const int tid_size[] = {
        0,   /* tid == 0 not defined                               */
        1,   /* TID_UINT8     unsigned byte         0       255    */
        1,   /* TID_INT8      signed byte         -128      127    */
        1,   /* TID_CHAR      single character      0       255    */
        2,   /* TID_UINT16    two bytes             0      65535   */
        2,   /* TID_INT16     signed word        -32768    32767   */
        4,   /* TID_UINT32    four bytes            0      2^32-1  */
        4,   /* TID_INT32     signed dword        -2^31    2^31-1  */
        4,   /* TID_BOOL      four bytes bool       0        1     */
        4,   /* TID_FLOAT     4 Byte float format                  */
        8,   /* TID_DOUBLE    8 Byte float format                  */
        1,   /* TID_BITFIELD  8 Bits Bitfield    00000000 11111111 */
        0,   /* TID_STRING    zero terminated string               */
        0,   /* TID_ARRAY     variable length array of unkown type */
        0,   /* TID_STRUCT    C structure                          */
        0,   /* TID_KEY       key in online database               */
        0,   /* TID_LINK      link in online database              */
        8,   /* TID_INT64     8 bytes int          -2^63   2^63-1  */
        8    /* TID_UINT64    8 bytes unsigned int  0      2^64-1  */
};

/* data type names */
static const char *tid_name_old[] = {
        "NULL",
        "BYTE",
        "SBYTE",
        "CHAR",
        "WORD",
        "SHORT",
        "DWORD",
        "INT",
        "BOOL",
        "FLOAT",
        "DOUBLE",
        "BITFIELD",
        "STRING",
        "ARRAY",
        "STRUCT",
        "KEY",
        "LINK",
        "INT64",
        "UINT64"
};

static const char *tid_name[] = {
        "NULL",
        "UINT8",
        "INT8",
        "CHAR",
        "UINT16",
        "INT16",
        "UINT32",
        "INT32",
        "BOOL",
        "FLOAT",
        "DOUBLE",
        "BITFIELD",
        "STRING",
        "ARRAY",
        "STRUCT",
        "KEY",
        "LINK",
        "INT64",
        "UINT64"
};

std::string cm_transition_name(int transition)
{
   if (transition == TR_START) return "START";
   if (transition == TR_STOP)  return "STOP";
   if (transition == TR_PAUSE) return "PAUSE";
   if (transition == TR_RESUME) return "RESUME";
   if (transition == TR_STARTABORT) return "STARTABORT";
   if (transition == TR_DEFERRED) return "DEFERRED";
   return msprintf("UNKNOWN TRANSITION %d", transition);
}

const char *mname[] = {
        "January",
        "February",
        "March",
        "April",
        "May",
        "June",
        "July",
        "August",
        "September",
        "October",
        "November",
        "December"
};

/* Globals */
#ifdef OS_MSDOS
extern unsigned _stklen = 60000U;
#endif

extern DATABASE *_database;
extern INT _database_entries;

//
// locking rules for gBuffers and gBuffersMutex:
//
// - all access to gBuffers must be done while holding gBufferMutex
// - while holding gBufferMutex:
// - taking additional locks not permitted (no calling odb, no locking event buffers, etc)
// - calling functions that can take additional locks not permitted (no calling db_xxx(), bm_xxx(), etc)
// - calling functions that can come back recursively not permitted
//
// after obtaining a BUFFER*pbuf pointer from gBuffers:
//
// - holding gBuffersMutex is not required
// - to access pbuf data, must hold buffer_mutex or call bm_lock_buffer()
// - except for:
//     pbuf->attached - no need to hold a lock (std::atomic)
//     pbuf->buffer_name - no need to hold a lock (constant data, only changed by bm_open_buffer())
//
// object life time:
//
// - gBuffers never shrinks
// - new BUFFER objects are created by bm_open_buffer(), added to gBuffers when ready for use, pbuf->attached set to true
// - bm_close_buffer() sets pbuf->attached to false
// - BUFFER objects are never deleted to avoid race between delete and bm_send_event() & co
// - BUFFER objects are never reused, bm_open_buffer() always creates a new object
// - gBuffers[i] set to NULL are empty slots available for reuse
// - closed buffers have corresponding gBuffers[i]->attached set to false
// 

static std::mutex gBuffersMutex; // protects gBuffers vector itself, but not it's contents!
static std::vector<BUFFER*> gBuffers;

static INT _msg_buffer = 0;
static EVENT_HANDLER *_msg_dispatch = NULL;

static REQUEST_LIST *_request_list;
static INT _request_list_entries = 0;

//static char *_tcp_buffer = NULL;
//static INT _tcp_wp = 0;
//static INT _tcp_rp = 0;
//static INT _tcp_sock = 0;

static MUTEX_T *_mutex_rpc = NULL; // mutex to protect RPC calls

static void (*_debug_print)(const char *) = NULL;

static INT _debug_mode = 0;

static int _rpc_connect_timeout = 10000;

// for use on a single machine it is best to restrict RPC access to localhost
// by binding the RPC listener socket to the localhost IP address.
static int disable_bind_rpc_to_localhost = 0;

/* table for transition functions */

struct TRANS_TABLE {
   INT transition;
   INT sequence_number;
   INT (*func)(INT, char *);
};

static std::mutex _trans_table_mutex;
static std::vector<TRANS_TABLE> _trans_table;

static TRANS_TABLE _deferred_trans_table[] = {
        {TR_START,  0, NULL},
        {TR_STOP,   0, NULL},
        {TR_PAUSE,  0, NULL},
        {TR_RESUME, 0, NULL},
        {0,         0, NULL}
};

static BOOL _rpc_registered = FALSE;
static int _rpc_listen_socket = 0;

static INT rpc_transition_dispatch(INT idx, void *prpc_param[]);

void cm_ctrlc_handler(int sig);

typedef struct {
   INT code;
   const char *string;
} ERROR_TABLE;

static const ERROR_TABLE _error_table[] = {
        {CM_WRONG_PASSWORD, "Wrong password"},
        {CM_UNDEF_EXP,      "Experiment not defined"},
        {CM_UNDEF_ENVIRON,
                            "\"exptab\" file not found and MIDAS_DIR or MIDAS_EXPTAB environment variable is not defined"},
        {RPC_NET_ERROR,     "Cannot connect to remote host"},
        {0, NULL}
};

typedef struct {
   void *adr;
   int size;
   char file[80];
   int line;
} DBG_MEM_LOC;

static DBG_MEM_LOC *_mem_loc = NULL;
static INT _n_mem = 0;

struct TR_PARAM
{
   INT transition;
   INT run_number;
   char *errstr;
   INT errstr_size;
   INT async_flag;
   INT debug_flag;
   std::atomic_int status{0};
   std::atomic_bool finished{false};
   std::atomic<std::thread*> thread{NULL};
};

static TR_PARAM _trp;

/*------------------------------------------------------------------*/

void *dbg_malloc(unsigned int size, char *file, int line) {
   FILE *f;
   void *adr;
   int i;

   adr = malloc(size);

   /* search for deleted entry */
   for (i = 0; i < _n_mem; i++)
      if (_mem_loc[i].adr == NULL)
         break;

   if (i == _n_mem) {
      _n_mem++;
      if (!_mem_loc)
         _mem_loc = (DBG_MEM_LOC *) malloc(sizeof(DBG_MEM_LOC));
      else
         _mem_loc = (DBG_MEM_LOC *) realloc(_mem_loc, sizeof(DBG_MEM_LOC) * _n_mem);
   }

   _mem_loc[i].adr = adr;
   _mem_loc[i].size = size;
   strcpy(_mem_loc[i].file, file);
   _mem_loc[i].line = line;

   f = fopen("mem.txt", "w");
   for (i = 0; i < _n_mem; i++)
      if (_mem_loc[i].adr)
         fprintf(f, "%s:%d size=%d adr=%p\n", _mem_loc[i].file, _mem_loc[i].line, _mem_loc[i].size,
                 _mem_loc[i].adr);
   fclose(f);

   return adr;
}

void *dbg_calloc(unsigned int size, unsigned int count, char *file, int line) {
   void *adr;

   adr = dbg_malloc(size * count, file, line);
   if (adr)
      memset(adr, 0, size * count);

   return adr;
}

void dbg_free(void *adr, char *file, int line) {
   FILE *f;
   int i;

   free(adr);

   for (i = 0; i < _n_mem; i++)
      if (_mem_loc[i].adr == adr)
         break;

   if (i < _n_mem)
      _mem_loc[i].adr = NULL;

   f = fopen("mem.txt", "w");
   for (i = 0; i < _n_mem; i++)
      if (_mem_loc[i].adr)
         fprintf(f, "%s:%d %s:%d size=%d adr=%p\n", _mem_loc[i].file, _mem_loc[i].line,
                 file, line, _mem_loc[i].size, _mem_loc[i].adr);
   fclose(f);
}

static std::vector<std::string> split(const char* sep, const std::string& s)
{
   unsigned sep_len = strlen(sep);
   std::vector<std::string> v;
   std::string::size_type pos = 0;
   while (1) {
      std::string::size_type next = s.find(sep, pos);
      if (next == std::string::npos) {
         v.push_back(s.substr(pos));
         break;
      }
      v.push_back(s.substr(pos, next-pos));
      pos = next+sep_len;
   }
   return v;
}

static std::string join(const char* sep, const std::vector<std::string>& v)
{
   std::string s;

   for (unsigned i=0; i<v.size(); i++) {
      if (i>0) {
         s += sep;
      }
      s += v[i];
   }

   return s;
}

bool ends_with_char(const std::string& s, char c)
{
   if (s.length() < 1)
      return false;
   return s[s.length()-1] == c;
}

std::string msprintf(const char *format, ...) {
   va_list ap, ap1;
   va_start(ap, format);
   va_copy(ap1, ap);
   size_t size = vsnprintf(nullptr, 0, format, ap1) + 1;
   char *buffer = (char *)malloc(size);
   if (!buffer)
      return "";
   vsnprintf(buffer, size, format, ap);
   va_end(ap);
   std::string s(buffer);
   free(buffer);
   return s;
}

/********************************************************************\
*                                                                    *
*              Common message functions                              *
*                                                                    *
\********************************************************************/

typedef int (*MessagePrintCallback)(const char *);

static std::atomic<MessagePrintCallback> _message_print{puts};

static std::atomic_int _message_mask_system{MT_ALL};
static std::atomic_int _message_mask_user{MT_ALL};


/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/**dox***************************************************************/
/** @addtogroup msgfunctionc
 *
 *  @{  */

/********************************************************************/
/**
Convert error code to string. Used after cm_connect_experiment to print
error string in command line programs or windows programs.
@param code Error code as defined in midas.h
@param string Error string
@return CM_SUCCESS
*/
std::string cm_get_error(INT code)
{
   for (int i = 0; _error_table[i].code; i++) {
      if (_error_table[i].code == code) {
         return _error_table[i].string;
      }
   }

   return msprintf("unlisted status code %d", code);
}

/********************************************************************/
int cm_msg_early_init(void) {

   return CM_SUCCESS;
}

/********************************************************************/

int cm_msg_open_buffer(void) {
   //printf("cm_msg_open_buffer!\n");
   if (_msg_buffer == 0) {
      int status = bm_open_buffer(MESSAGE_BUFFER_NAME, MESSAGE_BUFFER_SIZE, &_msg_buffer);
      if (status != BM_SUCCESS && status != BM_CREATED) {
         return status;
      }
   }
   return CM_SUCCESS;
}

/********************************************************************/

int cm_msg_close_buffer(void) {
   //printf("cm_msg_close_buffer!\n");
   if (_msg_buffer) {
      bm_close_buffer(_msg_buffer);
      _msg_buffer = 0;
   }
   return CM_SUCCESS;
}

/********************************************************************/

/**
 Retrieve list of message facilities by searching logfiles on disk
 @param  list             List of facilities
 @return status           SUCCESS
 */

INT EXPRT cm_msg_facilities(STRING_LIST *list) {
   std::string path;

   cm_msg_get_logfile("midas", 0, &path, NULL, NULL);

   /* extract directory name from full path name of midas.log */
   size_t pos = path.rfind(DIR_SEPARATOR);
   if (pos != std::string::npos) {
      path.resize(pos);
   } else {
      path = "";
   }

   //printf("cm_msg_facilities: path [%s]\n", path.c_str());

   STRING_LIST flist;
   
   ss_file_find(path.c_str(), "*.log", &flist);

   for (size_t i = 0; i < flist.size(); i++) {
      const char *p = flist[i].c_str();
      if (strchr(p, '_') == NULL && !(p[0] >= '0' && p[0] <= '9')) {
         size_t pos = flist[i].rfind('.');
         if (pos != std::string::npos) {
            flist[i].resize(pos);
         }
         list->push_back(flist[i]);
      }
   }

   return SUCCESS;
}

/********************************************************************/

void cm_msg_get_logfile(const char *fac, time_t t, std::string* filename, std::string* linkname, std::string* linktarget) {
   HNDLE hDB;
   int status;

   status = cm_get_experiment_database(&hDB, NULL);

   // check for call to cm_msg() before MIDAS is fully initialized
   // or after MIDAS is partially shutdown.
   if (status != CM_SUCCESS) {
      if (filename)
         *filename = std::string(fac) + ".log";
      if (linkname)
         *linkname = "";
      if (linktarget)
         *linktarget = "";
      return;
   }

   if (filename)
      *filename = "";
   if (linkname)
      *linkname = "";
   if (linktarget)
      *linktarget = "";

   std::string facility;
   if (fac && fac[0])
      facility = fac;
   else
      facility = "midas";

   std::string message_format;
   db_get_value_string(hDB, 0, "/Logger/Message file date format", 0, &message_format, TRUE);
   if (message_format.find('%') != std::string::npos) {
      /* replace stings such as %y%m%d with current date */
      struct tm tms;

      ss_tzset();
      if (t == 0)
         time(&t);
      localtime_r(&t, &tms);

      char de[256];
      de[0] = '_';
      strftime(de + 1, sizeof(de)-1, strchr(message_format.c_str(), '%'), &tms);
      message_format = de;
   }

   std::string message_dir;
   db_get_value_string(hDB, 0, "/Logger/Message dir", 0, &message_dir, TRUE);
   if (message_dir.empty()) {
      db_get_value_string(hDB, 0, "/Logger/Data dir", 0, &message_dir, FALSE);
      if (message_dir.empty()) {
         message_dir = cm_get_path();
         if (message_dir.empty()) {
            message_dir = ss_getcwd();
         }
      }
   }

   // prepend experiment directory
   if (message_dir[0] != DIR_SEPARATOR)
      message_dir = cm_get_path() + message_dir;

   if (message_dir.back() != DIR_SEPARATOR)
      message_dir.push_back(DIR_SEPARATOR);

   if (filename)
      *filename = message_dir + facility + message_format + ".log";
   if (!message_format.empty()) {
      if (linkname)
         *linkname = message_dir + facility  + ".log";
      if (linktarget)
         *linktarget = facility  + message_format + ".log";
   }
}

/********************************************************************/
/**
Set message masks. When a message is generated by calling cm_msg(),
it can got to two destinatinons. First a user defined callback routine
and second to the "SYSMSG" buffer.

A user defined callback receives all messages which satisfy the user_mask.

\code
int message_print(const char *msg)
{
  char str[160];

  memset(str, ' ', 159);
  str[159] = 0;
  if (msg[0] == '[')
    msg = strchr(msg, ']')+2;
  memcpy(str, msg, strlen(msg));
  ss_printf(0, 20, str);
  return 0;
}
...
  cm_set_msg_print(MT_ALL, MT_ALL, message_print);
...
\endcode
@param system_mask Bit masks for MERROR, MINFO etc. to send system messages.
@param user_mask Bit masks for MERROR, MINFO etc. to send messages to the user callback.
@param func Function which receives all printout. By setting "puts",
       messages are just printed to the screen.
@return CM_SUCCESS
*/
INT cm_set_msg_print(INT system_mask, INT user_mask, int (*func)(const char *)) {
   _message_mask_system = system_mask;
   _message_mask_user = user_mask;
   _message_print = func;

   return BM_SUCCESS;
}

/********************************************************************/
/**
Write message to logging file. Called by cm_msg.
@attention May burn your fingers
@param message_type     Message type
@param message          Message string
@param facility         Message facility, filename in which messages will be written
@return CM_SUCCESS
*/
INT cm_msg_log(INT message_type, const char *facility, const char *message) {
   INT status;

   if (rpc_is_remote()) {
      status = rpc_call(RPC_CM_MSG_LOG, message_type, facility, message);
      if (status != RPC_SUCCESS) {
         fprintf(stderr,
                 "cm_msg_log: Message \"%s\" not written to midas.log because rpc_call(RPC_CM_MSG_LOG) failed with status %d\n",
                 message, status);
      }
      return status;
   }

   if (message_type != MT_DEBUG) {
      std::string filename, linkname, linktarget;

      cm_msg_get_logfile(facility, 0, &filename, &linkname, &linktarget);

#ifdef OS_LINUX
      if (!linkname.empty()) {
         //printf("cm_msg_log: filename [%s] linkname [%s] linktarget [%s]\n", filename.c_str(), linkname.c_str(), linktarget.c_str());
         // If filename does not exist, user just switched from non-date format to date format.
         // In that case we must copy linkname to filename, otherwise messages might get lost.
         if (ss_file_exist(linkname.c_str()) && !ss_file_link_exist(linkname.c_str())) {
            ss_file_copy(linkname.c_str(), filename.c_str(), true);
         }

         unlink(linkname.c_str());
         status = symlink(linktarget.c_str(), linkname.c_str());
         if (status != 0) {
            fprintf(stderr,
                    "cm_msg_log: Error: Cannot symlink message log file \'%s' to \'%s\', symlink() errno: %d (%s)\n",
                    linktarget.c_str(), linkname.c_str(), errno, strerror(errno));
         }
      }
#endif

      int fh = open(filename.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_LARGEFILE, 0644);
      if (fh < 0) {
         fprintf(stderr,
                 "cm_msg_log: Message \"%s\" not written to midas.log because open(%s) failed with errno %d (%s)\n",
                 message, filename.c_str(), errno, strerror(errno));
      } else {

         struct timeval tv;
         struct tm tms;

         ss_tzset();
         gettimeofday(&tv, NULL);
         localtime_r(&tv.tv_sec, &tms);

         char str[256];
         strftime(str, sizeof(str), "%H:%M:%S", &tms);
         sprintf(str + strlen(str), ".%03d ", (int) (tv.tv_usec / 1000));
         strftime(str + strlen(str), sizeof(str), "%G/%m/%d", &tms);

         std::string msg;
         msg += str;
         msg += " ";
         msg += message;
         msg += "\n";

         /* avoid c++ complaint about comparison between
            unsigned size_t returned by msg.length() and
            signed ssize_t returned by write() */
         ssize_t len = msg.length();

         /* atomic write, no need to take a semaphore */
         ssize_t wr = write(fh, msg.c_str(), len);

         if (wr < 0) {
            fprintf(stderr, "cm_msg_log: Message \"%s\" not written to \"%s\", write() error, errno %d (%s)\n", message, filename.c_str(), errno, strerror(errno));
         } else if (wr != len) {
            fprintf(stderr, "cm_msg_log: Message \"%s\" not written to \"%s\", short write() wrote %d instead of %d bytes\n", message, filename.c_str(), (int)wr, (int)len);
         }

         close(fh);
      }
   }

   return CM_SUCCESS;
}


static std::string cm_msg_format(INT message_type, const char *filename, INT line, const char *routine, const char *format, va_list *argptr)
{
   /* strip path */
   const char* pc = filename + strlen(filename);
   while (*pc != '\\' && *pc != '/' && pc != filename)
      pc--;
   if (pc != filename)
      pc++;

   /* convert type to string */
   std::string type_str;
   if (message_type & MT_ERROR)
      type_str += MT_ERROR_STR;
   if (message_type & MT_INFO)
      type_str += MT_INFO_STR;
   if (message_type & MT_DEBUG)
      type_str += MT_DEBUG_STR;
   if (message_type & MT_USER)
      type_str += MT_USER_STR;
   if (message_type & MT_LOG)
      type_str += MT_LOG_STR;
   if (message_type & MT_TALK)
      type_str += MT_TALK_STR;

   std::string message;

   /* print client name into string */
   if (message_type == MT_USER)
      message = msprintf("[%s] ", routine);
   else {
      std::string name = rpc_get_name();
      if (name.length() > 0)
         message = msprintf("[%s,%s] ", name.c_str(), type_str.c_str());
      else
         message = "";
   }

   /* preceed error messages with file and line info */
   if (message_type == MT_ERROR) {
      message += msprintf("[%s:%d:%s,%s] ", pc, line, routine, type_str.c_str());
   } else if (message_type == MT_USER) {
      message = msprintf("[%s,%s] ", routine, type_str.c_str());
   }

   int bufsize = 1024;
   char* buf = (char*)malloc(bufsize);
   assert(buf);

   for (int i=0; i<10; i++) {
      va_list ap;
      va_copy(ap, *argptr);
      
      /* print argument list into message */
      int n = vsnprintf(buf, bufsize-1, format, ap);

      //printf("vsnprintf [%s] %d %d\n", format, bufsize, n);

      if (n < bufsize) {
         break;
      }

      bufsize += 100;
      bufsize *= 2;
      buf = (char*)realloc(buf, bufsize);
      assert(buf);
   }

   message += buf;
   free(buf);

   return message;
}

static INT cm_msg_send_event(DWORD ts, INT message_type, const char *send_message) {
   //printf("cm_msg_send: ts %d, type %d, message [%s]\n", ts, message_type, send_message);

   /* send event if not of type MLOG */
   if (message_type != MT_LOG) {
      if (_msg_buffer) {
         /* copy message to event */
         size_t len = strlen(send_message);
         int event_length = sizeof(EVENT_HEADER) + len + 1;
         char event[event_length];
         EVENT_HEADER *pevent = (EVENT_HEADER *) event;

         memcpy(event + sizeof(EVENT_HEADER), send_message, len + 1);

         /* setup the event header and send the message */
         bm_compose_event(pevent, EVENTID_MESSAGE, (WORD) message_type, len + 1, 0);
         if (ts)
            pevent->time_stamp = ts;
         //printf("cm_msg_send_event: len %d, header %d, allocated %d, data_size %d, bm_send_event %p+%d\n", (int)len, (int)sizeof(EVENT_HEADER), event_length, pevent->data_size, pevent, (int)(pevent->data_size + sizeof(EVENT_HEADER)));
         bm_send_event(_msg_buffer, pevent, 0, BM_WAIT);
      }
   }

   return CM_SUCCESS;
}

struct msg_buffer_entry {
   DWORD ts;
   int message_type;
   std::string message;
};

static std::deque<msg_buffer_entry> gMsgBuf;
static std::mutex gMsgBufMutex;

/********************************************************************/
/**
This routine can be called to process messages buffered by cm_msg(). Normally
it is called from cm_yield() and cm_disconnect_experiment() to make sure
all accumulated messages are processed.
*/
INT cm_msg_flush_buffer() {
   int i;

   //printf("cm_msg_flush_buffer!\n");

   for (i = 0; i < 100; i++) {
      msg_buffer_entry e;
      {
         std::lock_guard<std::mutex> lock(gMsgBufMutex);
         if (gMsgBuf.empty())
            break;
         e = gMsgBuf.front();
         gMsgBuf.pop_front();
         // implicit unlock
      }

      /* log message */
      cm_msg_log(e.message_type, "midas", e.message.c_str());

      /* send message to SYSMSG */
      int status = cm_msg_send_event(e.ts, e.message_type, e.message.c_str());
      if (status != CM_SUCCESS)
         return status;
   }

   return CM_SUCCESS;
}

/********************************************************************/
/**
This routine can be called whenever an internal error occurs
or an informative message is produced. Different message
types can be enabled or disabled by setting the type bits
via cm_set_msg_print().
@attention Do not add the "\n" escape carriage control at the end of the
formated line as it is already added by the client on the receiving side.
\code
   ...
   cm_msg(MINFO, "my program", "This is a information message only);
   cm_msg(MERROR, "my program", "This is an error message with status:%d", my_status);
   cm_msg(MTALK, "my_program", My program is Done!");
   ...
\endcode
@param message_type (See @ref midas_macro).
@param filename Name of source file where error occured
@param line Line number where error occured
@param routine Routine name.
@param format message to printout, ... Parameters like for printf()
@return CM_SUCCESS
*/
INT cm_msg(INT message_type, const char *filename, INT line, const char *routine, const char *format, ...)
{
   DWORD ts = ss_time();

   /* print argument list into message */
   std::string message;
   va_list argptr;
   va_start(argptr, format);
   message = cm_msg_format(message_type, filename, line, routine, format, &argptr);
   va_end(argptr);

   //printf("message [%s]\n", message);

   /* call user function if set via cm_set_msg_print */
   MessagePrintCallback f = _message_print.load();
   if (f != NULL && (message_type & _message_mask_user) != 0) {
      if (message_type != MT_LOG) { // do not print MLOG messages
         (*f)(message.c_str());
      }
   }

   /* return if system mask is not set */
   if ((message_type & _message_mask_system) == 0) {
      return CM_SUCCESS;
   }

   gMsgBufMutex.lock();
   gMsgBuf.push_back(msg_buffer_entry{ts, message_type, message});
   gMsgBufMutex.unlock();

   return CM_SUCCESS;
}

/********************************************************************/
/**
This routine is similar to @ref cm_msg().
It differs from cm_msg() only by the logging destination being a file
given through the argument list i.e:\b facility
@internal
@attention Do not add the "\n" escape carriage control at the end of the
formated line as it is already added by the client on the receiving side.
The first arg in the following example uses the predefined
macro MINFO which handles automatically the first 3 arguments of the function
(see @ref midas_macro).
\code   ...
   cm_msg1(MINFO, "my_log_file", "my_program"," My message status:%d", status);
   ...
//----- File my_log_file.log
Thu Nov  8 17:59:28 2001 [my_program] My message status:1
\endcode
@param message_type See @ref midas_macro.
@param filename Name of source file where error occured
@param line Line number where error occured
@param facility Logging file name
@param routine Routine name
@param format message to printout, ... Parameters like for printf()
@return CM_SUCCESS
*/
INT cm_msg1(INT message_type, const char *filename, INT line,
            const char *facility, const char *routine, const char *format, ...) {
   va_list argptr;
   std::string message;
   static BOOL in_routine = FALSE;

   /* avoid recursive calles */
   if (in_routine)
      return 0;

   in_routine = TRUE;

   /* print argument list into message */
   va_start(argptr, format);
   message = cm_msg_format(message_type, filename, line, routine, format, &argptr);
   va_end(argptr);

   /* call user function if set via cm_set_msg_print */
   MessagePrintCallback f = _message_print.load();
   if (f != NULL && (message_type & _message_mask_user) != 0)
      (*f)(message.c_str());

   /* return if system mask is not set */
   if ((message_type & _message_mask_system) == 0) {
      in_routine = FALSE;
      return CM_SUCCESS;
   }

   /* send message to SYSMSG */
   cm_msg_send_event(0, message_type, message.c_str());

   /* log message */
   cm_msg_log(message_type, facility, message.c_str());

   in_routine = FALSE;

   return CM_SUCCESS;
}

/********************************************************************/
/**
Register a dispatch function for receiving system messages.
- example code from mlxspeaker.c
\code
void receive_message(HNDLE hBuf, HNDLE id, EVENT_HEADER *header, void *message)
{
  char str[256], *pc, *sp;
  // print message
  printf("%s\n", (char *)(message));

  printf("evID:%x Mask:%x Serial:%i Size:%d\n"
                 ,header->event_id
                 ,header->trigger_mask
                 ,header->serial_number
                 ,header->data_size);
  pc = strchr((char *)(message),']')+2;
  ...
  // skip none talking message
  if (header->trigger_mask == MT_TALK ||
      header->trigger_mask == MT_USER)
   ...
}

int main(int argc, char *argv[])
{
  ...
  // now connect to server
  status = cm_connect_experiment(host_name, exp_name, "Speaker", NULL);
  if (status != CM_SUCCESS)
    return 1;
  // Register callback for messages
  cm_msg_register(receive_message);
  ...
}
\endcode
@param func Dispatch function.
@return CM_SUCCESS or bm_open_buffer and bm_request_event return status
*/
INT cm_msg_register(EVENT_HANDLER *func) {
   INT status, id;

   // we should only come here after the message buffer
   // was opened by cm_connect_experiment()
   assert(_msg_buffer);

   _msg_dispatch = func;

   status = bm_request_event(_msg_buffer, EVENTID_ALL, TRIGGER_ALL, GET_NONBLOCKING, &id, func);

   return status;
}

static void add_message(char **messages, int *length, int *allocated, time_t tstamp, const char *new_message) {
   int new_message_length = strlen(new_message);
   int new_allocated = 1024 + 2 * ((*allocated) + new_message_length);
   char buf[100];
   int buf_length;

   //printf("add_message: new message %d, length %d, new end: %d, allocated: %d, maybe reallocate size %d\n", new_message_length, *length, *length + new_message_length, *allocated, new_allocated);

   if (*length + new_message_length + 100 > *allocated) {
      *messages = (char *) realloc(*messages, new_allocated);
      assert(*messages != NULL);
      *allocated = new_allocated;
   }

   if (*length > 0)
      if ((*messages)[(*length) - 1] != '\n') {
         (*messages)[*length] = '\n'; // separator between messages
         (*length) += 1;
      }

   sprintf(buf, "%ld ", tstamp);
   buf_length = strlen(buf);
   memcpy(&((*messages)[*length]), buf, buf_length);
   (*length) += buf_length;

   memcpy(&((*messages)[*length]), new_message, new_message_length);
   (*length) += new_message_length;
   (*messages)[*length] = 0; // make sure string is NUL terminated
}

/* Retrieve message from an individual file. Internal use only */
static int cm_msg_retrieve1(const char *filename, time_t t, INT n_messages, char **messages, int *length, int *allocated,
                            int *num_messages) {
   BOOL stop;
   int fh;
   char *p, str[1000];
   struct stat stat_buf;
   time_t tstamp, tstamp_valid, tstamp_last;

   ss_tzset(); // required by localtime_r()

   *num_messages = 0;

   fh = open(filename, O_RDONLY | O_TEXT, 0644);
   if (fh < 0) {
      cm_msg(MERROR, "cm_msg_retrieve1", "Cannot open log file \"%s\", errno %d (%s)", filename, errno,
             strerror(errno));
      return SS_FILE_ERROR;
   }

   /* read whole file into memory */
   fstat(fh, &stat_buf);
   ssize_t size = stat_buf.st_size;

   /* if file is too big, only read tail of file */
   ssize_t maxsize = 10 * 1024 * 1024;
   if (size > maxsize) {
      lseek(fh, -maxsize, SEEK_END);
      //printf("lseek status %d, errno %d (%s)\n", status, errno, strerror(errno));
      size = maxsize;
   }

   char *buffer = (char *) malloc(size + 1);

   if (buffer == NULL) {
      cm_msg(MERROR, "cm_msg_retrieve1", "Cannot malloc %d bytes to read log file \"%s\", errno %d (%s)", (int) size,
             filename, errno, strerror(errno));
      close(fh);
      return SS_FILE_ERROR;
   }

   ssize_t rd = read(fh, buffer, size);

   if (rd != size) {
      cm_msg(MERROR, "cm_msg_retrieve1", "Cannot read %d bytes from log file \"%s\", read() returned %d, errno %d (%s)",
             (int) size, filename, (int) rd, errno, strerror(errno));
      close(fh);
      return SS_FILE_ERROR;
   }

   buffer[size] = 0;
   close(fh);

   p = buffer + size - 1;
   tstamp_last = tstamp_valid = 0;
   stop = FALSE;

   while (*p == '\n' || *p == '\r')
      p--;

   int n;
   for (n = 0; !stop && p > buffer;) {

      /* go to beginning of line */
      int i;
      for (i = 0; p != buffer && (*p != '\n' && *p != '\r'); i++)
         p--;

      /* limit line length to sizeof(str) */
      if (i >= (int) sizeof(str))
         i = sizeof(str) - 1;

      if (p == buffer) {
         i++;
         memcpy(str, p, i);
      } else
         memcpy(str, p + 1, i);
      str[i] = 0;
      if (strchr(str, '\n'))
         *strchr(str, '\n') = 0;
      if (strchr(str, '\r'))
         *strchr(str, '\r') = 0;
      strlcat(str, "\n", sizeof(str));

      // extract time tag
      time_t now;
      time(&now);

      struct tm tms;
      localtime_r(&now, &tms); // must call tzset() beforehand!

      if (str[0] >= '0' && str[0] <= '9') {
         // new format
         tms.tm_hour = atoi(str);
         tms.tm_min = atoi(str + 3);
         tms.tm_sec = atoi(str + 6);
         tms.tm_year = atoi(str + 13) - 1900;
         tms.tm_mon = atoi(str + 18) - 1;
         tms.tm_mday = atoi(str + 21);
      } else {
         // old format
         tms.tm_hour = atoi(str + 11);
         tms.tm_min = atoi(str + 14);
         tms.tm_sec = atoi(str + 17);
         tms.tm_year = atoi(str + 20) - 1900;
         for (i = 0; i < 12; i++)
            if (strncmp(str + 4, mname[i], 3) == 0)
               break;
         tms.tm_mon = i;
         tms.tm_mday = atoi(str + 8);
      }
      tstamp = ss_mktime(&tms);
      if (tstamp != -1)
         tstamp_valid = tstamp;

      // for new messages (n=0!), stop when t reached
      if (n_messages == 0) {
         if (tstamp_valid < t)
            break;
      }

      // for old messages, stop when all messages belonging to tstamp_last are sent
      if (n_messages != 0) {
         if (tstamp_last > 0 && tstamp_valid < tstamp_last)
            break;
      }

      if (t == 0 || tstamp == -1 ||
          (n_messages > 0 && tstamp <= t) ||
          (n_messages == 0 && tstamp >= t)) {

         n++;

         add_message(messages, length, allocated, tstamp, str);
      }

      while (*p == '\n' || *p == '\r')
         p--;

      if (n_messages == 1)
         stop = TRUE;
      else if (n_messages > 1) {
         // continue collecting messages until time stamp differs from current one
         if (n == n_messages)
            tstamp_last = tstamp_valid;

         // if all messages without time tags, just return after n
         if (n == n_messages && tstamp_valid == 0)
            break;
      }
   }

   free(buffer);

   *num_messages = n;

   return CM_SUCCESS;
}

/********************************************************************/
/**
Retrieve old messages from log file
@param  facility         Logging facility ("midas", "chat", "lazy", ...)
@param  t                Return messages logged before and including time t, value 0 means start with newest messages
@param  min_messages     Minimum number of messages to return
@param  messages         messages, newest first, separated by \n characters. caller should free() this buffer at the end.
@param  num_messages     Number of messages returned
@return CM_SUCCESS
*/
INT cm_msg_retrieve2(const char *facility, time_t t, INT n_message, char **messages, int *num_messages) {
   std::string filename, linkname;
   INT n, i;
   time_t filedate;
   int length = 0;
   int allocated = 0;

   time(&filedate);
   cm_msg_get_logfile(facility, filedate, &filename, &linkname, NULL);

   //printf("facility %s, filename \"%s\" \"%s\"\n", facility, filename, linkname);

   // see if file exists, use linkname if not
   if (!linkname.empty()) {
      if (!ss_file_exist(filename.c_str()))
         filename = linkname;
   }

   if (ss_file_exist(filename.c_str())) {
      cm_msg_retrieve1(filename.c_str(), t, n_message, messages, &length, &allocated, &n);
   } else {
      n = 0;
   }

   /* if there is no symlink, then there is no additional log files to read */
   if (linkname.empty()) {
      return CM_SUCCESS;
   }

   //printf("read more messages %d %d!\n", n, n_message);

   int missing = 0;
   while (n < n_message) {
      filedate -= 3600 * 24;         // go one day back

      cm_msg_get_logfile(facility, filedate, &filename, NULL, NULL);

      //printf("read [%s] for time %d!\n", filename.c_str(), filedate);

      if (ss_file_exist(filename.c_str())) {
         cm_msg_retrieve1(filename.c_str(), t, n_message - n, messages, &length, &allocated, &i);
         n += i;
         missing = 0;
      } else {
         missing++;
      }

      // stop if ten consecutive files are not found
      if (missing > 10)
         break;
   }

   *num_messages = n;

   return CM_SUCCESS;
}

/********************************************************************/
/**
Retrieve newest messages from "midas" facility log file
@param  n_message        Number of messages to retrieve
@param  message          buf_size bytes of messages, separated
                         by \n characters. The returned number
                         of bytes is normally smaller than the
                         initial buf_size, since only full
                         lines are returned.
@param *buf_size         Size of message buffer to fill
@return CM_SUCCESS, CM_TRUNCATED
*/
INT cm_msg_retrieve(INT n_message, char *message, INT buf_size) {
   int status;
   char *messages = NULL;
   int num_messages = 0;

   if (rpc_is_remote())
      return rpc_call(RPC_CM_MSG_RETRIEVE, n_message, message, buf_size);

   status = cm_msg_retrieve2("midas", 0, n_message, &messages, &num_messages);

   if (messages) {
      strlcpy(message, messages, buf_size);
      int len = strlen(messages);
      if (len > buf_size)
         status = CM_TRUNCATED;
      free(messages);
   }

   return status;
}

/**dox***************************************************************/
/** @} *//* end of msgfunctionc */

/**dox***************************************************************/
/** @addtogroup cmfunctionc
 *
 *  @{  */

/********************************************************************/
/**
Get time from MIDAS server and set local time.
@param    seconds         Time in seconds
@return CM_SUCCESS
*/
INT cm_synchronize(DWORD *seconds) {
   INT sec, status;

   /* if connected to server, get time from there */
   if (rpc_is_remote()) {
      status = rpc_call(RPC_CM_SYNCHRONIZE, &sec);

      /* set local time */
      if (status == CM_SUCCESS)
         ss_settime(sec);
   }

   /* return time to caller */
   if (seconds != NULL) {
      *seconds = ss_time();
   }

   return CM_SUCCESS;
}

/********************************************************************/
/**
Get time from MIDAS server and set local time.
@param    str            return time string
@param    buf_size       Maximum size of str
@return   CM_SUCCESS
*/
INT cm_asctime(char *str, INT buf_size) {
   /* if connected to server, get time from there */
   if (rpc_is_remote())
      return rpc_call(RPC_CM_ASCTIME, str, buf_size);

   /* return local time */
   strlcpy(str, ss_asctime().c_str(), buf_size);

   return CM_SUCCESS;
}

/********************************************************************/
/**
Get time from MIDAS server and set local time.
@return   return time string
*/
std::string cm_asctime() {
   /* if connected to server, get time from there */
   if (rpc_is_remote()) {
      char buf[256];
      int status = rpc_call(RPC_CM_ASCTIME, buf, sizeof(buf));
      if (status == CM_SUCCESS) {
         return buf;
      } else {
         return "";
      }
   }

   /* return local time */
   return ss_asctime();
}

/********************************************************************/
/**
Get time from ss_time on server.
@param    t string
@return   CM_SUCCESS
*/
INT cm_time(DWORD *t) {
   /* if connected to server, get time from there */
   if (rpc_is_remote())
      return rpc_call(RPC_CM_TIME, t);

   /* return local time */
   *t = ss_time();

   return CM_SUCCESS;
}

/**dox***************************************************************/
/** @} *//* end of cmfunctionc */

/********************************************************************\
*                                                                    *
*           cm_xxx  -  Common Functions to buffer & database         *
*                                                                    *
\********************************************************************/

/* Globals */

static HNDLE _hKeyClient = 0;   /* key handle for client in ODB */
static HNDLE _hDB = 0;          /* Database handle */
static std::string _experiment_name;
static std::string _client_name;
static std::string _path_name;
static INT _watchdog_timeout = DEFAULT_WATCHDOG_TIMEOUT;
INT _semaphore_alarm = -1;
INT _semaphore_elog = -1;
INT _semaphore_history = -1;
//INT _semaphore_msg = -1;

/**dox***************************************************************/
/** @addtogroup cmfunctionc
 *
 *  @{  */

/**
Return version number of current MIDAS library as a string
@return version number
*/
const char *cm_get_version() {
   return MIDAS_VERSION;
}

/**
Return git revision number of current MIDAS library as a string
@return revision number
*/
const char *cm_get_revision() {
   return GIT_REVISION;
}

/********************************************************************/
/**
Set path to actual experiment. This function gets called
by cm_connect_experiment if the connection is established
to a local experiment (not through the TCP/IP server).
The path is then used for all shared memory routines.
@param  path             Pathname
@return CM_SUCCESS
*/
INT cm_set_path(const char *path) {
   assert(path);
   assert(path[0] != 0);

   _path_name = path;

   if (_path_name.back() != DIR_SEPARATOR) {
      _path_name += DIR_SEPARATOR_STR;
   }

   //printf("cm_set_path [%s]\n", _path_name.c_str());

   return CM_SUCCESS;
}

/********************************************************************/
/**
Return the path name previously set with cm_set_path.
@param  path             Pathname
@return CM_SUCCESS
*/
INT cm_get_path(char *path, int path_size) {
   // check that we were not accidentally called
   // with the size of the pointer to a string
   // instead of the size of the string buffer
   assert(path_size != sizeof(char *));
   assert(path);
   assert(_path_name.length() > 0);

   strlcpy(path, _path_name.c_str(), path_size);

   return CM_SUCCESS;
}

/********************************************************************/
/**
Return the path name previously set with cm_set_path.
@param  path             Pathname
@return CM_SUCCESS
*/
std::string cm_get_path() {
   assert(_path_name.length() > 0);
   return _path_name;
}

/********************************************************************/
/* C++ wrapper for cm_get_path */

INT EXPRT cm_get_path_string(std::string *path) {
   assert(path != NULL);
   assert(_path_name.length() > 0);
   *path = _path_name;
   return CM_SUCCESS;
}

/********************************************************************/
/**
Set name of the experiment
@param  name             Experiment name
@return CM_SUCCESS
*/
INT cm_set_experiment_name(const char *name) {
   _experiment_name = name;
   return CM_SUCCESS;
}

/********************************************************************/
/**
Return the experiment name
@param  name             Pointer to user string, size should be at least NAME_LENGTH
@param  name_size        Size of user string
@return CM_SUCCESS
*/
INT cm_get_experiment_name(char *name, int name_length) {
   strlcpy(name, _experiment_name.c_str(), name_length);
   return CM_SUCCESS;
}

/********************************************************************/
/**
Return the experiment name
@return experiment name
*/
std::string cm_get_experiment_name() {
   return _experiment_name;
}

/**dox***************************************************************/
/** @} *//* end of cmfunctionc */

/**dox***************************************************************/
/** @addtogroup cmfunctionc
 *
 *  @{  */

#ifdef LOCAL_ROUTINES

struct exptab_entry {
   std::string name;
   std::string directory;
   std::string user;
};

struct exptab_struct {
   std::string filename;
   std::vector<exptab_entry> exptab;
};

static exptab_struct _exptab; // contents of exptab file

/**
Scan the "exptab" file for MIDAS experiment names and save them
for later use by rpc_server_accept(). The file is first searched
under $MIDAS/exptab if present, then the directory from argv[0] is probed.
@return CM_SUCCESS<br>
        CM_UNDEF_EXP exptab not found and MIDAS_DIR not set
*/
INT cm_read_exptab(exptab_struct *exptab) {
   exptab->exptab.clear();

   /* MIDAS_DIR overrides exptab */
   if (getenv("MIDAS_DIR")) {
      exptab->filename = "";

      exptab_entry e;

      e.name = "Default";
      e.directory = getenv("MIDAS_DIR");
      e.user = "";

      exptab->exptab.push_back(e);

      return CM_SUCCESS;
   }

   /* default directory for different OSes */
#if defined (OS_WINNT)
   std::string str;
   if (getenv("SystemRoot"))
      str = getenv("SystemRoot");
   else if (getenv("windir"))
      str = getenv("windir");
   else
      str = "";

   std::string alt_str = str;
   str += "\\system32\\exptab";
   alt_str += "\\system\\exptab";
#elif defined (OS_UNIX)
   std::string str = "/etc/exptab";
   std::string alt_str = "/exptab";
#else
   std::strint str = "exptab";
   std::string alt_str = "exptab";
#endif

   /* MIDAS_EXPTAB overrides default directory */
   if (getenv("MIDAS_EXPTAB")) {
      str = getenv("MIDAS_EXPTAB");
      alt_str = getenv("MIDAS_EXPTAB");
   }
   
   exptab->filename = str;

   /* read list of available experiments */
   FILE* f = fopen(str.c_str(), "r");
   if (f == NULL) {
      f = fopen(alt_str.c_str(), "r");
      if (f == NULL)
         return CM_UNDEF_ENVIRON;
      exptab->filename = alt_str;
   }

   if (f != NULL) {
      do {
         char buf[256];
         memset(buf, 0, sizeof(buf));
         char* str = fgets(buf, sizeof(buf)-1, f);
         if (str == NULL)
            break;
         if (str[0] == 0) continue; // empty line
         if (str[0] == '#') continue; // comment line

         exptab_entry e;

         // following code emulates the function of this sprintf():
         //sscanf(str, "%s %s %s", exptab[i].name, exptab[i].directory, exptab[i].user);

         // skip leading spaces
         while (*str && isspace(*str))
            str++;

         char* p1 = str;
         char* p2 = str;

         while (*p2 && !isspace(*p2))
            p2++;

         ssize_t len = p2-p1;

         if (len<1)
            continue;
         
         //printf("str %d [%s] p1 [%s] p2 %d [%s] len %d\n", *str, str, p1, *p2, p2, (int)len);

         e.name = std::string(p1, len);

         if (*p2 == 0)
            continue;

         str = p2;

         // skip leading spaces
         while (*str && isspace(*str))
            str++;
         
         p1 = str;
         p2 = str;

         while (*p2 && !isspace(*p2))
            p2++;

         len = p2-p1;

         if (len<1)
            continue;
         
         //printf("str %d [%s] p1 [%s] p2 %d [%s] len %d\n", *str, str, p1, *p2, p2, (int)len);

         e.directory = std::string(p1, len);

         if (*p2 == 0)
            continue;

         str = p2;

         // skip leading spaces
         while (*str && isspace(*str))
            str++;
         
         p1 = str;
         p2 = str;

         while (*p2 && !isspace(*p2))
            p2++;

         len = p2-p1;

         //printf("str %d [%s] p1 [%s] p2 %d [%s] len %d\n", *str, str, p1, *p2, p2, (int)len);

         e.user = std::string(p1, len);
         
         /* check for trailing directory separator */
         if (!ends_with_char(e.directory, DIR_SEPARATOR)) {
            e.directory += DIR_SEPARATOR_STR;
         }
         
         exptab->exptab.push_back(e);
      } while (!feof(f));
      fclose(f);
   }

#if 0
   cm_msg(MINFO, "cm_read_exptab", "Read exptab \"%s\":", exptab->filename.c_str()); 
   for (unsigned j=0; j<exptab->exptab.size(); j++) {
      cm_msg(MINFO, "cm_read_exptab", "entry %d, experiment \"%s\", directory \"%s\", user \"%s\"", j, exptab->exptab[j].name.c_str(), exptab->exptab[j].directory.c_str(), exptab->exptab[j].user.c_str());
   }
#endif

   return CM_SUCCESS;
}

/********************************************************************/
/**
Return location of exptab file
@param s               Pointer to string buffer
@param size            Size of string buffer
@return CM_SUCCESS
*/
int cm_get_exptab_filename(char *s, int size) {
   strlcpy(s, _exptab.filename.c_str(), size);
   return CM_SUCCESS;
}

std::string cm_get_exptab_filename() {
   return _exptab.filename;
}

/********************************************************************/
/**
Return exptab information for given experiment
@param s               Pointer to string buffer
@param size            Size of string buffer
@return CM_SUCCESS
*/
int cm_get_exptab(const char *expname, std::string* dir, std::string* user) {

   if (_exptab.exptab.size() == 0) {
      int status = cm_read_exptab(&_exptab);
      if (status != CM_SUCCESS)
         return status;
   }

   for (unsigned i = 0; i < _exptab.exptab.size(); i++) {
      if (_exptab.exptab[i].name == expname) {
         if (dir)
            *dir = _exptab.exptab[i].directory;
         if (user)
            *user = _exptab.exptab[i].user;
         return CM_SUCCESS;
      }
   }
   if (dir)
      *dir = "";
   if (user)
      *user = "";
   return CM_UNDEF_EXP;
}

/********************************************************************/
/**
Return exptab information for given experiment
@param s               Pointer to string buffer
@param size            Size of string buffer
@return CM_SUCCESS
*/
int cm_get_exptab(const char *expname, char *dir, int dir_size, char *user, int user_size) {
   std::string sdir, suser;
   int status = cm_get_exptab(expname, &sdir, &suser);
   if (status == CM_SUCCESS) {
      if (dir)
         strlcpy(dir, sdir.c_str(), dir_size);
      if (user)
         strlcpy(user, suser.c_str(), user_size);
      return CM_SUCCESS;
   }
   return CM_UNDEF_EXP;
}

#endif // LOCAL_ROUTINES

/********************************************************************/
/**
Delete client info from database
@param hDB               Database handle
@param pid               PID of entry to delete, zero for this process.
@return CM_SUCCESS
*/
INT cm_delete_client_info(HNDLE hDB, INT pid) {
   /* only do it if local */
   if (!rpc_is_remote()) {
      db_delete_client_info(hDB, pid);
   }
   return CM_SUCCESS;
}

/********************************************************************/
/**
Check if a client with a /system/client/xxx entry has
a valid entry in the ODB client table. If not, remove
that client from the /system/client tree.
@param   hDB               Handle to online database
@param   hKeyClient        Handle to client key
@return  CM_SUCCESS, CM_NO_CLIENT
*/
INT cm_check_client(HNDLE hDB, HNDLE hKeyClient) {
   if (rpc_is_remote())
      return rpc_call(RPC_CM_CHECK_CLIENT, hDB, hKeyClient);

#ifdef LOCAL_ROUTINES
   return db_check_client(hDB, hKeyClient);
#endif                          /*LOCAL_ROUTINES */
   return CM_SUCCESS;
}

/********************************************************************/
/**
Set client information in online database and return handle
@param  hDB              Handle to online database
@param  hKeyClient       returned key
@param  host_name        server name
@param  client_name      Name of this program as it will be seen
                         by other clients.
@param  hw_type          Type of byte order
@param  password         MIDAS password
@param  watchdog_timeout Default watchdog timeout, can be overwritten
                         by ODB setting /programs/\<name\>/Watchdog timeout
@return   CM_SUCCESS
*/
INT cm_set_client_info(HNDLE hDB, HNDLE *hKeyClient, const char *host_name,
                       char *client_name, INT hw_type, const char *password, DWORD watchdog_timeout) {
   if (rpc_is_remote())
      return rpc_call(RPC_CM_SET_CLIENT_INFO, hDB, hKeyClient,
                      host_name, client_name, hw_type, password, watchdog_timeout);

#ifdef LOCAL_ROUTINES
   {
      INT status, pid, data, i, idx, size;
      HNDLE hKey, hSubkey;
      char str[256], name[NAME_LENGTH], orig_name[NAME_LENGTH], pwd[NAME_LENGTH];
      BOOL call_watchdog, allow;
      PROGRAM_INFO_STR(program_info_str);

      /* check security if password is present */
      status = db_find_key(hDB, 0, "/Experiment/Security/Password", &hKey);
      if (hKey) {
         /* get password */
         size = sizeof(pwd);
         db_get_data(hDB, hKey, pwd, &size, TID_STRING);

         /* first check allowed hosts list */
         allow = FALSE;
         db_find_key(hDB, 0, "/Experiment/Security/Allowed hosts", &hKey);
         if (hKey && db_find_key(hDB, hKey, host_name, &hKey) == DB_SUCCESS)
            allow = TRUE;

         /* check allowed programs list */
         db_find_key(hDB, 0, "/Experiment/Security/Allowed programs", &hKey);
         if (hKey && db_find_key(hDB, hKey, client_name, &hKey) == DB_SUCCESS)
            allow = TRUE;

         /* now check password */
         if (!allow && strcmp(password, pwd) != 0) {
            if (password[0])
               cm_msg(MINFO, "cm_set_client_info", "Wrong password for host %s", host_name);
            return CM_WRONG_PASSWORD;
         }
      }

      /* make following operation atomic by locking database */
      db_lock_database(hDB);

      /* check if entry with this pid exists already */
      pid = ss_getpid();

      sprintf(str, "System/Clients/%0d", pid);
      status = db_find_key(hDB, 0, str, &hKey);
      if (status == DB_SUCCESS) {
         db_set_mode(hDB, hKey, MODE_READ | MODE_WRITE | MODE_DELETE, TRUE);
         db_delete_key(hDB, hKey, TRUE);
      }

      if (strlen(client_name) >= NAME_LENGTH)
         client_name[NAME_LENGTH] = 0;

      strcpy(name, client_name);
      strcpy(orig_name, client_name);

      /* check if client name already exists */
      status = db_find_key(hDB, 0, "System/Clients", &hKey);

      for (idx = 1; status != DB_NO_MORE_SUBKEYS; idx++) {
         for (i = 0;; i++) {
            status = db_enum_key(hDB, hKey, i, &hSubkey);
            if (status == DB_NO_MORE_SUBKEYS)
               break;

            if (status == DB_SUCCESS) {
               size = sizeof(str);
               status = db_get_value(hDB, hSubkey, "Name", str, &size, TID_STRING, FALSE);
               if (status != DB_SUCCESS)
                  continue;
            }

            /* check if client is living */
            if (cm_check_client(hDB, hSubkey) == CM_NO_CLIENT)
               continue;

            if (equal_ustring(str, name)) {
               sprintf(name, "%s%d", client_name, idx);
               break;
            }
         }
      }

      /* set name */
      sprintf(str, "System/Clients/%0d/Name", pid);
      status = db_set_value(hDB, 0, str, name, NAME_LENGTH, 1, TID_STRING);
      if (status != DB_SUCCESS) {
         db_unlock_database(hDB);
         cm_msg(MERROR, "cm_set_client_info", "cannot set client name, db_set_value(%s) status %d", str, status);
         return status;
      }

      /* copy new client name */
      strcpy(client_name, name);
      db_set_client_name(hDB, client_name);

      /* set also as rpc name */
      rpc_set_name(client_name);

      /* use /system/clients/PID as root */
      sprintf(str, "System/Clients/%0d", pid);
      db_find_key(hDB, 0, str, &hKey);

      /* set host name */
      status = db_set_value(hDB, hKey, "Host", host_name, HOST_NAME_LENGTH, 1, TID_STRING);
      if (status != DB_SUCCESS) {
         db_unlock_database(hDB);
         return status;
      }

      /* set computer id */
      status = db_set_value(hDB, hKey, "Hardware type", &hw_type, sizeof(hw_type), 1, TID_INT32);
      if (status != DB_SUCCESS) {
         db_unlock_database(hDB);
         return status;
      }

      /* set server port */
      data = 0;
      status = db_set_value(hDB, hKey, "Server Port", &data, sizeof(INT), 1, TID_INT32);
      if (status != DB_SUCCESS) {
         db_unlock_database(hDB);
         return status;
      }

      /* lock client entry */
      db_set_mode(hDB, hKey, MODE_READ, TRUE);

      /* get (set) default watchdog timeout */
      size = sizeof(watchdog_timeout);
      sprintf(str, "/Programs/%s/Watchdog Timeout", orig_name);
      db_get_value(hDB, 0, str, &watchdog_timeout, &size, TID_INT32, TRUE);

      /* define /programs entry */
      sprintf(str, "/Programs/%s", orig_name);
      db_create_record(hDB, 0, str, strcomb1(program_info_str).c_str());

      /* save handle for ODB and client */
      cm_set_experiment_database(hDB, hKey);

      /* save watchdog timeout */
      cm_get_watchdog_params(&call_watchdog, NULL);
      cm_set_watchdog_params(call_watchdog, watchdog_timeout);

      /* end of atomic operations */
      db_unlock_database(hDB);

      /* touch notify key to inform others */
      data = 0;
      db_set_value(hDB, 0, "/System/Client Notify", &data, sizeof(data), 1, TID_INT32);

      *hKeyClient = hKey;
   }
#endif                          /* LOCAL_ROUTINES */

   return CM_SUCCESS;
}

/********************************************************************/
/**
Get current client name
@return   current client name
*/
std::string cm_get_client_name()
{
   INT status;
   HNDLE hDB, hKey;

   /* get root key of client */
   cm_get_experiment_database(&hDB, &hKey);
   if (!hDB) {
      return "unknown";
   }

   std::string name;

   status = db_get_value_string(hDB, hKey, "Name", 0, &name);
   if (status != DB_SUCCESS) {
      return "unknown";
   }

   //printf("get client name: [%s]\n", name.c_str());

   return name;
}

/********************************************************************/
/**
Returns MIDAS environment variables.
@attention This function can be used to evaluate the standard MIDAS
           environment variables before connecting to an experiment
           (see @ref Environment_variables).
           The usual way is that the host name and experiment name are first derived
           from the environment variables MIDAS_SERVER_HOST and MIDAS_EXPT_NAME.
           They can then be superseded by command line parameters with -h and -e flags.
\code
#include <stdio.h>
#include <midas.h>
main(int argc, char *argv[])
{
  INT  status, i;
  char host_name[256],exp_name[32];

  // get default values from environment
  cm_get_environment(host_name, exp_name);

  // parse command line parameters
  for (i=1 ; i<argc ; i++)
    {
    if (argv[i][0] == '-')
      {
      if (i+1 >= argc || argv[i+1][0] == '-')
        goto usage;
      if (argv[i][1] == 'e')
        strcpy(exp_name, argv[++i]);
      else if (argv[i][1] == 'h')
        strcpy(host_name, argv[++i]);
      else
        {
usage:
        printf("usage: test [-h Hostname] [-e Experiment]\n\n");
        return 1;
        }
      }
    }
  status = cm_connect_experiment(host_name, exp_name, "Test", NULL);
  if (status != CM_SUCCESS)
    return 1;
    ...do anyting...
  cm_disconnect_experiment();
}
\endcode
@param host_name          Contents of MIDAS_SERVER_HOST environment variable.
@param host_name_size     string length
@param exp_name           Contents of MIDAS_EXPT_NAME environment variable.
@param exp_name_size      string length
@return CM_SUCCESS
*/
INT cm_get_environment(char *host_name, int host_name_size, char *exp_name, int exp_name_size) {
   if (host_name)
      host_name[0] = 0;
   if (exp_name)
      exp_name[0] = 0;

   if (host_name && getenv("MIDAS_SERVER_HOST"))
      strlcpy(host_name, getenv("MIDAS_SERVER_HOST"), host_name_size);

   if (exp_name && getenv("MIDAS_EXPT_NAME"))
      strlcpy(exp_name, getenv("MIDAS_EXPT_NAME"), exp_name_size);

   return CM_SUCCESS;
}

INT cm_get_environment(std::string *host_name, std::string *exp_name) {
   if (host_name)
      *host_name = "";
   if (exp_name)
      *exp_name = "";

   if (host_name && getenv("MIDAS_SERVER_HOST"))
      *host_name = getenv("MIDAS_SERVER_HOST");

   if (exp_name && getenv("MIDAS_EXPT_NAME"))
      *exp_name = getenv("MIDAS_EXPT_NAME");

   return CM_SUCCESS;
}

#ifdef LOCAL_ROUTINES

int cm_set_experiment_local(const char* exp_name)
{
   std::string exp_name1;

   if ((exp_name != NULL) && (strlen(exp_name) > 0)) {
      exp_name1 = exp_name;
   } else {
      int status = cm_select_experiment_local(&exp_name1);
      if (status != CM_SUCCESS)
         return status;
   }

   std::string expdir, expuser;
   
   int status = cm_get_exptab(exp_name1.c_str(), &expdir, &expuser);
   
   if (status != CM_SUCCESS) {
      cm_msg(MERROR, "cm_set_experiment_local", "Experiment \"%s\" not found in exptab file \"%s\"", exp_name1.c_str(), cm_get_exptab_filename().c_str());
      return CM_UNDEF_EXP;
   }

   if (!ss_dir_exist(expdir.c_str())) {
      cm_msg(MERROR, "cm_set_experiment_local", "Experiment \"%s\" directory \"%s\" does not exist", exp_name1.c_str(), expdir.c_str());
      return CM_UNDEF_EXP;
   }
   
   cm_set_experiment_name(exp_name1.c_str());
   cm_set_path(expdir.c_str());

   return CM_SUCCESS;
}

#endif // LOCAL_ROUTINES
   
/********************************************************************/
void cm_check_connect(void) {
   if (_hKeyClient) {
      cm_msg(MERROR, "cm_check_connect", "cm_disconnect_experiment not called at end of program");
      cm_msg_flush_buffer();
   }
}

/********************************************************************/
/**
This function connects to an existing MIDAS experiment.
This must be the first call in a MIDAS application.
It opens three TCP connection to the remote host (one for RPC calls,
one to send events and one for hot-link notifications from the remote host)
and writes client information into the ODB under /System/Clients.
@attention All MIDAS applications should evaluate the MIDAS_SERVER_HOST
and MIDAS_EXPT_NAME environment variables as defaults to the host name and
experiment name (see @ref Environment_variables).
For that purpose, the function cm_get_environment()
should be called prior to cm_connect_experiment(). If command line
parameters -h and -e are used, the evaluation should be done between
cm_get_environment() and cm_connect_experiment(). The function
cm_disconnect_experiment() must be called before a MIDAS application exits.
\code
#include <stdio.h>
#include <midas.h>
main(int argc, char *argv[])
{
  INT  status, i;
  char host_name[256],exp_name[32];

  // get default values from environment
  cm_get_environment(host_name, exp_name);

  // parse command line parameters
  for (i=1 ; i<argc ; i++)
    {
    if (argv[i][0] == '-')
      {
      if (i+1 >= argc || argv[i+1][0] == '-')
        goto usage;
      if (argv[i][1] == 'e')
        strcpy(exp_name, argv[++i]);
      else if (argv[i][1] == 'h')
        strcpy(host_name, argv[++i]);
      else
        {
usage:
        printf("usage: test [-h Hostname] [-e Experiment]\n\n");
        return 1;
        }
      }
    }
  status = cm_connect_experiment(host_name, exp_name, "Test", NULL);
  if (status != CM_SUCCESS)
    return 1;
  ...do operations...
  cm_disconnect_experiment();
}
\endcode
@param host_name Specifies host to connect to. Must be a valid IP host name.
  The string can be empty ("") if to connect to the local computer.
@param exp_name Specifies the experiment to connect to.
  If this string is empty, the number of defined experiments in exptab is checked.
  If only one experiment is defined, the function automatically connects to this
  one. If more than one experiment is defined, a list is presented and the user
  can interactively select one experiment.
@param client_name Client name of the calling program as it can be seen by
  others (like the scl command in ODBEdit).
@param func Callback function to read in a password if security has
  been enabled. In all command line applications this function is NULL which
  invokes an internal ss_gets() function to read in a password.
  In windows environments (MS Windows, X Windows) a function can be supplied to
  open a dialog box and read in the password. The argument of this function must
  be the returned password.
@return CM_SUCCESS, CM_UNDEF_EXP, CM_SET_ERROR, RPC_NET_ERROR <br>
CM_VERSION_MISMATCH MIDAS library version different on local and remote computer
*/
INT cm_connect_experiment(const char *host_name, const char *exp_name, const char *client_name, void (*func)(char *)) {
   INT status;

   status = cm_connect_experiment1(host_name, exp_name, client_name, func, DEFAULT_ODB_SIZE, DEFAULT_WATCHDOG_TIMEOUT);
   cm_msg_flush_buffer();
   if (status != CM_SUCCESS) {
      std::string s = cm_get_error(status);
      puts(s.c_str());
   }

   return status;
}

/********************************************************************/
/**
Connect to a MIDAS experiment (to the online database) on
           a specific host.
@internal
*/
INT cm_connect_experiment1(const char *host_name, const char *exp_name,
                           const char *client_name, void (*func)(char *), INT odb_size, DWORD watchdog_timeout) {
   INT status, size;
   char local_host_name[HOST_NAME_LENGTH];
   char client_name1[NAME_LENGTH];
   char password[NAME_LENGTH], str[256];
   HNDLE hDB = 0, hKeyClient = 0;
   BOOL call_watchdog;

   ss_tzset(); // required for localtime_r()

   if (_hKeyClient)
      cm_disconnect_experiment();

   cm_msg_early_init();

   //cm_msg(MERROR, "cm_connect_experiment", "test cm_msg before connecting to experiment");
   //cm_msg_flush_buffer();

   rpc_set_name(client_name);

   /* check for local host */
   if (equal_ustring(host_name, "local"))
      host_name = NULL;

#ifdef OS_WINNT
   {
      WSADATA WSAData;

      /* Start windows sockets */
      if (WSAStartup(MAKEWORD(1, 1), &WSAData) != 0)
         return RPC_NET_ERROR;
   }
#endif

   /* search for experiment name in exptab */
   if (exp_name == NULL)
      exp_name = "";

   std::string exp_name1 = exp_name;

   /* connect to MIDAS server */
   if (host_name && host_name[0]) {
      if (exp_name1.length() == 0) {
         status = cm_select_experiment_remote(host_name, &exp_name1);
         if (status != CM_SUCCESS)
            return status;
      }

      status = rpc_server_connect(host_name, exp_name1.c_str());
      if (status != RPC_SUCCESS)
         return status;

      /* register MIDAS library functions */
      status = rpc_register_functions(rpc_get_internal_list(1), NULL);
      if (status != RPC_SUCCESS)
         return status;
   } else {
      /* lookup path for *SHM files and save it */

#ifdef LOCAL_ROUTINES
      status = cm_set_experiment_local(exp_name1.c_str());
      if (status != CM_SUCCESS)
         return status;

      exp_name1 = cm_get_experiment_name();

      ss_suspend_init_odb_port();

      INT semaphore_elog, semaphore_alarm, semaphore_history, semaphore_msg;

      /* create alarm and elog semaphores */
      status = ss_semaphore_create("ALARM", &semaphore_alarm);
      if (status != SS_CREATED && status != SS_SUCCESS) {
         cm_msg(MERROR, "cm_connect_experiment", "Cannot create alarm semaphore");
         return status;
      }
      status = ss_semaphore_create("ELOG", &semaphore_elog);
      if (status != SS_CREATED && status != SS_SUCCESS) {
         cm_msg(MERROR, "cm_connect_experiment", "Cannot create elog semaphore");
         return status;
      }
      status = ss_semaphore_create("HISTORY", &semaphore_history);
      if (status != SS_CREATED && status != SS_SUCCESS) {
         cm_msg(MERROR, "cm_connect_experiment", "Cannot create history semaphore");
         return status;
      }
      status = ss_semaphore_create("MSG", &semaphore_msg);
      if (status != SS_CREATED && status != SS_SUCCESS) {
         cm_msg(MERROR, "cm_connect_experiment", "Cannot create message semaphore");
         return status;
      }

      cm_set_experiment_semaphore(semaphore_alarm, semaphore_elog, semaphore_history, semaphore_msg);
#else
      return CM_UNDEF_EXP;
#endif
   }

   //cm_msg(MERROR, "cm_connect_experiment", "test cm_msg before open ODB");
   //cm_msg_flush_buffer();

   /* open ODB */
   if (odb_size == 0)
      odb_size = DEFAULT_ODB_SIZE;

   status = db_open_database("ODB", odb_size, &hDB, client_name);
   if (status != DB_SUCCESS && status != DB_CREATED) {
      cm_msg(MERROR, "cm_connect_experiment1", "cannot open database, db_open_database() status %d", status);
      return status;
   }

   //cm_msg(MERROR, "cm_connect_experiment", "test cm_msg after open ODB");
   //cm_msg_flush_buffer();

   int odb_timeout = db_set_lock_timeout(hDB, 0);
   size = sizeof(odb_timeout);
   status = db_get_value(hDB, 0, "/Experiment/ODB timeout", &odb_timeout, &size, TID_INT32, TRUE);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "cm_connect_experiment1", "cannot get ODB /Experiment/ODB timeout, status %d", status);
   }

   if (odb_timeout > 0) {
      db_set_lock_timeout(hDB, odb_timeout);
   }

   BOOL protect_odb = FALSE;
   size = sizeof(protect_odb);
   status = db_get_value(hDB, 0, "/Experiment/Protect ODB", &protect_odb, &size, TID_BOOL, TRUE);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "cm_connect_experiment1", "cannot get ODB /Experiment/Protect ODB, status %d", status);
   }

   if (protect_odb) {
      db_protect_database(hDB);
   }

   BOOL enable_core_dumps = FALSE;
   size = sizeof(enable_core_dumps);
   status = db_get_value(hDB, 0, "/Experiment/Enable core dumps", &enable_core_dumps, &size, TID_BOOL, TRUE);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "cm_connect_experiment1", "cannot get ODB /Experiment/Enable core dumps, status %d", status);
   }

   if (enable_core_dumps) {
#ifdef RLIMIT_CORE
      struct rlimit limit;
      limit.rlim_cur = RLIM_INFINITY;
      limit.rlim_max = RLIM_INFINITY;
      status = setrlimit(RLIMIT_CORE, &limit);
      if (status != 0) {
         cm_msg(MERROR, "cm_connect_experiment", "Cannot setrlimit(RLIMIT_CORE, RLIM_INFINITY), errno %d (%s)", errno,
                strerror(errno));
      }
#else
#warning setrlimit(RLIMIT_CORE) is not available
#endif
   }

   size = sizeof(disable_bind_rpc_to_localhost);
   status = db_get_value(hDB, 0, "/Experiment/Security/Enable non-localhost RPC", &disable_bind_rpc_to_localhost, &size,
                         TID_BOOL, TRUE);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "cm_connect_experiment1",
             "cannot get ODB /Experiment/Security/Enable non-localhost RPC, status %d", status);
   }

   /* now setup client info */
   if (!disable_bind_rpc_to_localhost)
      strlcpy(local_host_name, "localhost", sizeof(local_host_name));
   else
      ss_gethostname(local_host_name, sizeof(local_host_name));

   /* check watchdog timeout */
   if (watchdog_timeout == 0)
      watchdog_timeout = DEFAULT_WATCHDOG_TIMEOUT;

   strcpy(client_name1, client_name);
   password[0] = 0;
   status = cm_set_client_info(hDB, &hKeyClient, local_host_name, client_name1, rpc_get_hw_type(), password, watchdog_timeout);

   if (status == CM_WRONG_PASSWORD) {
      if (func == NULL)
         strcpy(str, ss_getpass("Password: "));
      else
         func(str);

      strcpy(password, ss_crypt(str, "mi"));
      status = cm_set_client_info(hDB, &hKeyClient, local_host_name, client_name1, rpc_get_hw_type(), password, watchdog_timeout);
      if (status != CM_SUCCESS) {
         /* disconnect */
         if (rpc_is_remote())
            rpc_server_disconnect();
         cm_disconnect_experiment();

         return status;
      }
   }

   //cm_msg(MERROR, "cm_connect_experiment", "test cm_msg after set client info");
   //cm_msg_flush_buffer();

   /* tell the rest of MIDAS that ODB is open for business */

   cm_set_experiment_database(hDB, hKeyClient);

   //cm_msg(MERROR, "cm_connect_experiment", "test cm_msg after set experiment database");
   //cm_msg_flush_buffer();

   /* cm_msg_open_buffer() calls bm_open_buffer() calls ODB function
    * to get event buffer size, etc */

   status = cm_msg_open_buffer();
   if (status != CM_SUCCESS) {
      cm_msg(MERROR, "cm_connect_experiment1", "cannot open message buffer, cm_msg_open_buffer() status %d", status);
      return status;
   }

   //cm_msg(MERROR, "cm_connect_experiment", "test cm_msg after message system is ready");
   //cm_msg_flush_buffer();

   /* set experiment name in ODB */
   db_set_value_string(hDB, 0, "/Experiment/Name", &exp_name1);

   if (!rpc_is_remote()) {
      /* experiment path is only set for local connections */
      /* set data dir in ODB */
      std::string path = cm_get_path();
      db_get_value_string(hDB, 0, "/Logger/Data dir", 0, &path, TRUE);
   }

   /* register server to be able to be called by other clients */
   status = cm_register_server();
   if (status != CM_SUCCESS) {
      cm_msg(MERROR, "cm_connect_experiment", "Cannot register RPC server, cm_register_server() status %d", status);
      if (!equal_ustring(client_name, "odbedit")) {
         return status;
      }
   }

   /* set watchdog timeout */
   cm_get_watchdog_params(&call_watchdog, &watchdog_timeout);
   size = sizeof(watchdog_timeout);
   sprintf(str, "/Programs/%s/Watchdog Timeout", client_name);
   db_get_value(hDB, 0, str, &watchdog_timeout, &size, TID_INT32, TRUE);
   cm_set_watchdog_params(call_watchdog, watchdog_timeout);

   /* send startup notification */
   if (strchr(local_host_name, '.'))
      *strchr(local_host_name, '.') = 0;

   /* get final client name */
   std::string xclient_name = rpc_get_name();

   /* startup message is not displayed */
   cm_msg(MLOG, "cm_connect_experiment", "Program %s on host %s started", xclient_name.c_str(), local_host_name);

   /* enable system and user messages to stdout as default */
   cm_set_msg_print(MT_ALL, MT_ALL, puts);

   /* call cm_check_connect when exiting */
   atexit((void (*)(void)) cm_check_connect);

   /* register ctrl-c handler */
   ss_ctrlc_handler(cm_ctrlc_handler);

   //cm_msg(MERROR, "cm_connect_experiment", "test cm_msg after connect to experiment is complete");
   //cm_msg_flush_buffer();

   return CM_SUCCESS;
}

#ifdef LOCAL_ROUTINES
/********************************************************************/
/**
Read exptab and return all defined experiments in *exp_name[MAX_EXPERIMENTS]
@param  host_name         Internet host name.
@param  exp_name          list of experiment names
@return CM_SUCCESS, RPC_NET_ERROR
*/
INT cm_list_experiments_local(STRING_LIST *exp_names) {
   assert(exp_names != NULL);
   exp_names->clear();

   if (_exptab.exptab.size() == 0) {
      int status = cm_read_exptab(&_exptab);
      if (status != CM_SUCCESS)
         return status;
   }
   
   for (unsigned i=0; i<_exptab.exptab.size(); i++) {
      exp_names->push_back(_exptab.exptab[i].name);
   }
   
   return CM_SUCCESS;
}
#endif // LOCAL_ROUTINES

/********************************************************************/
/**
Connect to a MIDAS server and return all defined
           experiments in *exp_name[MAX_EXPERIMENTS]
@param  host_name         Internet host name.
@param  exp_name          list of experiment names
@return CM_SUCCESS, RPC_NET_ERROR
*/
INT cm_list_experiments_remote(const char *host_name, STRING_LIST *exp_names) {
   INT status;
   struct sockaddr_in bind_addr;
   INT sock;
   struct hostent *phe;
   int port = MIDAS_TCP_PORT;
   char hname[256];
   char *s;

   assert(exp_names != NULL);
   exp_names->clear();

#ifdef OS_WINNT
   {
      WSADATA WSAData;

      /* Start windows sockets */
      if (WSAStartup(MAKEWORD(1, 1), &WSAData) != 0)
         return RPC_NET_ERROR;
   }
#endif

   // NB: this will not work with IPv6

   /* create a new socket for connecting to remote server */
   sock = socket(AF_INET, SOCK_STREAM, 0);
   if (sock == -1) {
      cm_msg(MERROR, "cm_list_experiments_remote", "cannot create socket, errno %d (%s)", errno, strerror(errno));
      return RPC_NET_ERROR;
   }

   /* extract port number from host_name */
   strlcpy(hname, host_name, sizeof(hname));
   s = strchr(hname, ':');
   if (s) {
      *s = 0;
      port = strtoul(s + 1, NULL, 0);
   }

   /* connect to remote node */
   memset(&bind_addr, 0, sizeof(bind_addr));
   bind_addr.sin_family = AF_INET;
   bind_addr.sin_addr.s_addr = 0;
   bind_addr.sin_port = htons(port);

#ifdef OS_VXWORKS
   {
      INT host_addr;

      host_addr = hostGetByName(hname);
      memcpy((char *) &(bind_addr.sin_addr), &host_addr, 4);
   }
#else
   phe = gethostbyname(hname);
   if (phe == NULL) {
      cm_msg(MERROR, "cm_list_experiments_remote", "cannot resolve host name \'%s\'", hname);
      return RPC_NET_ERROR;
   }
   memcpy((char *) &(bind_addr.sin_addr), phe->h_addr, phe->h_length);
#endif

#ifdef OS_UNIX
   do {
      status = connect(sock, (struct sockaddr *) &bind_addr, sizeof(bind_addr));

      /* don't return if an alarm signal was cought */
   } while (status == -1 && errno == EINTR);
#else
   status = connect(sock, (struct sockaddr *) &bind_addr, sizeof(bind_addr));
#endif

   if (status != 0) {
      cm_msg(MERROR, "cm_list_experiments_remote", "Cannot connect to \"%s\" port %d, errno %d (%s)", hname, port, errno, strerror(errno));
      return RPC_NET_ERROR;
   }

   /* request experiment list */
   send(sock, "I", 2, 0);

   while (1) {
      char str[256];

      status = recv_string(sock, str, sizeof(str), _rpc_connect_timeout);

      if (status < 0)
         return RPC_NET_ERROR;

      if (status == 0)
         break;

      exp_names->push_back(str);
   }

   closesocket(sock);

   return CM_SUCCESS;
}

#ifdef LOCAL_ROUTINES
/********************************************************************/
/**
Read exptab and select an experiment
           from the experiments available on this server
@internal
@param  exp_name          selected experiment name
@return CM_SUCCESS
*/
INT cm_select_experiment_local(std::string *exp_name) {
   INT status;
   STRING_LIST expts;

   assert(exp_name != NULL);

   /* retrieve list of experiments and make selection */
   status = cm_list_experiments_local(&expts);
   if (status != CM_SUCCESS)
      return status;

   if (expts.size() == 1) {
      *exp_name = expts[0];
   } else if (expts.size() > 1) {
      printf("Available experiments on local computer:\n");

      for (unsigned i = 0; i < expts.size(); i++) {
         printf("%d : %s\n", i, expts[i].c_str());
      }

      while (1) {
         printf("Select number from 0 to %d: ", ((int)expts.size())-1);
         char str[32];
         ss_gets(str, 32);
         int isel = atoi(str);
         if (isel < 0)
            continue;
         if (isel >= (int)expts.size())
            continue;
         *exp_name = expts[isel];
         break;
      }
   } else {
      return CM_UNDEF_EXP;
   }

   return CM_SUCCESS;
}
#endif // LOCAL_ROUTINES

/********************************************************************/
/**
Connect to a MIDAS server and select an experiment
           from the experiments available on this server
@internal
@param  host_name         Internet host name.
@param  exp_name          selected experiment name
@return CM_SUCCESS, RPC_NET_ERROR
*/
INT cm_select_experiment_remote(const char *host_name, std::string *exp_name) {
   INT status;
   STRING_LIST expts;

   assert(exp_name != NULL);

   /* retrieve list of experiments and make selection */
   status = cm_list_experiments_remote(host_name, &expts);
   if (status != CM_SUCCESS)
      return status;

   if (expts.size() > 1) {
      printf("Available experiments on server %s:\n", host_name);

      for (unsigned i = 0; i < expts.size(); i++) {
         printf("%d : %s\n", i, expts[i].c_str());
      }

      while (1) {
         printf("Select number from 0 to %d: ", ((int)expts.size())-1);
         char str[32];
         ss_gets(str, 32);
         int isel = atoi(str);
         if (isel < 0)
            continue;
         if (isel >= (int)expts.size())
            continue;
         *exp_name = expts[isel];
         break;
      }
   } else {
      *exp_name = expts[0];
   }

   return CM_SUCCESS;
}

/********************************************************************/
/**
Connect to a MIDAS client of the current experiment
@internal
@param  client_name       Name of client to connect to. This name
                            is set by the other client via the
                            cm_connect_experiment call.
@param  hConn            Connection handle
@return CM_SUCCESS, CM_NO_CLIENT
*/
INT cm_connect_client(const char *client_name, HNDLE *hConn) {
   HNDLE hDB, hKeyRoot, hSubkey, hKey;
   INT status, i, length, port;
   char name[NAME_LENGTH], host_name[HOST_NAME_LENGTH];

   /* find client entry in ODB */
   cm_get_experiment_database(&hDB, &hKey);

   status = db_find_key(hDB, 0, "System/Clients", &hKeyRoot);
   if (status != DB_SUCCESS)
      return status;

   i = 0;
   do {
      /* search for client with specific name */
      status = db_enum_key(hDB, hKeyRoot, i++, &hSubkey);
      if (status == DB_NO_MORE_SUBKEYS)
         return CM_NO_CLIENT;

      status = db_find_key(hDB, hSubkey, "Name", &hKey);
      if (status != DB_SUCCESS)
         return status;

      length = NAME_LENGTH;
      status = db_get_data(hDB, hKey, name, &length, TID_STRING);
      if (status != DB_SUCCESS)
         return status;

      if (equal_ustring(name, client_name)) {
         status = db_find_key(hDB, hSubkey, "Server Port", &hKey);
         if (status != DB_SUCCESS)
            return status;

         length = sizeof(INT);
         status = db_get_data(hDB, hKey, &port, &length, TID_INT32);
         if (status != DB_SUCCESS)
            return status;

         status = db_find_key(hDB, hSubkey, "Host", &hKey);
         if (status != DB_SUCCESS)
            return status;

         length = sizeof(host_name);
         status = db_get_data(hDB, hKey, host_name, &length, TID_STRING);
         if (status != DB_SUCCESS)
            return status;

         /* client found -> connect to its server port */
         return rpc_client_connect(host_name, port, client_name, hConn);
      }


   } while (TRUE);
}

static void rpc_client_shutdown();

/********************************************************************/
/**
Disconnect from a MIDAS client
@param   hConn             Connection handle obtained via
                             cm_connect_client()
@param   bShutdown         If TRUE, disconnect from client and
                             shut it down (exit the client program)
                             by sending a RPC_SHUTDOWN message
@return   see rpc_client_disconnect()
*/
INT cm_disconnect_client(HNDLE hConn, BOOL bShutdown) {
   return rpc_client_disconnect(hConn, bShutdown);
}

/********************************************************************/
/**
Disconnect from a MIDAS experiment.
@attention Should be the last call to a MIDAS library function in an
application before it exits. This function removes the client information
from the ODB, disconnects all TCP connections and frees all internal
allocated memory. See cm_connect_experiment() for example.
@return CM_SUCCESS
*/
INT cm_disconnect_experiment(void) {
   HNDLE hDB, hKey;
   char local_host_name[HOST_NAME_LENGTH];

   //cm_msg(MERROR, "cm_disconnect_experiment", "test cm_msg before disconnect from experiment");
   //cm_msg_flush_buffer();

   /* wait on any transition thread */
   if (_trp.transition && !_trp.finished) {
      printf("Waiting for transition to finish...\n");
      do {
         ss_sleep(10);
      } while (!_trp.finished);
   }

   /* stop the watchdog thread */
   cm_stop_watchdog_thread();

   /* send shutdown notification */
   std::string client_name = rpc_get_name();

   if (!disable_bind_rpc_to_localhost)
      strlcpy(local_host_name, "localhost", sizeof(local_host_name));
   else {
      ss_gethostname(local_host_name, sizeof(local_host_name));
      if (strchr(local_host_name, '.'))
         *strchr(local_host_name, '.') = 0;
   }

   /* disconnect message not displayed */
   cm_msg(MLOG, "cm_disconnect_experiment", "Program %s on host %s stopped", client_name.c_str(), local_host_name);
   cm_msg_flush_buffer();

   if (rpc_is_remote()) {
      /* close open records */
      db_close_all_records();

      cm_msg_close_buffer();

      rpc_client_shutdown();
      rpc_server_disconnect();

      cm_set_experiment_database(0, 0);
   } else {
      rpc_client_shutdown();

      /* delete client info */
      cm_get_experiment_database(&hDB, &hKey);

      if (hDB)
         cm_delete_client_info(hDB, 0);

      //cm_msg(MERROR, "cm_disconnect_experiment", "test cm_msg before close all buffers, close all databases");
      //cm_msg_flush_buffer();

      cm_msg_close_buffer();
      bm_close_all_buffers();
      db_close_all_databases();

      cm_set_experiment_database(0, 0);

      //cm_msg(MERROR, "cm_disconnect_experiment", "test cm_msg after close all buffers, close all databases");
      //cm_msg_flush_buffer();
   }

   if (!rpc_is_mserver())
      rpc_server_shutdown();

   /* free RPC list */
   rpc_deregister_functions();

   //cm_msg(MERROR, "cm_disconnect_experiment", "test cm_msg before deleting the message ring buffer");
   //cm_msg_flush_buffer();

   /* last flush before we delete the message ring buffer */
   cm_msg_flush_buffer();

   //cm_msg(MERROR, "cm_disconnect_experiment", "test cm_msg after disconnect is completed");
   //cm_msg_flush_buffer();

   return CM_SUCCESS;
}

/********************************************************************/
/**
Set the handle to the ODB for the currently connected experiment
@param hDB              Database handle
@param hKeyClient       Key handle of client structure
@return CM_SUCCESS
*/
INT cm_set_experiment_database(HNDLE hDB, HNDLE hKeyClient) {
   //printf("cm_set_experiment_database: hDB %d, hKeyClient %d\n", hDB, hKeyClient);

   _hDB = hDB;
   _hKeyClient = hKeyClient;

   //if (hDB == 0) {
   //   rpc_set_server_option(RPC_ODB_HANDLE, 0);
   //}

   return CM_SUCCESS;
}



/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/********************************************************************/
INT cm_set_experiment_semaphore(INT semaphore_alarm, INT semaphore_elog, INT semaphore_history, INT semaphore_msg)
/********************************************************************\

  Routine: cm_set_experiment_semaphore

  Purpose: Set the handle to the experiment wide semaphorees

  Input:
    INT    semaphore_alarm      Alarm semaphore
    INT    semaphore_elog       Elog semaphore
    INT    semaphore_history    History semaphore
    INT    semaphore_msg        Message semaphore

  Output:
    none

  Function value:
    CM_SUCCESS              Successful completion

\********************************************************************/
{
   _semaphore_alarm = semaphore_alarm;
   _semaphore_elog = semaphore_elog;
   _semaphore_history = semaphore_history;
   //_semaphore_msg = semaphore_msg;

   return CM_SUCCESS;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Get the handle to the ODB from the currently connected experiment.

@attention This function returns the handle of the online database (ODB) which
can be used in future db_xxx() calls. The hkeyclient key handle can be used
to access the client information in the ODB. If the client key handle is not needed,
the parameter can be NULL.
\code
HNDLE hDB, hkeyclient;
 char  name[32];
 int   size;
 db_get_experiment_database(&hdb, &hkeyclient);
 size = sizeof(name);
 db_get_value(hdb, hkeyclient, "Name", name, &size, TID_STRING, TRUE);
 printf("My name is %s\n", name);
\endcode
@param hDB Database handle.
@param hKeyClient Handle for key where search starts, zero for root.
@return CM_SUCCESS
*/
INT cm_get_experiment_database(HNDLE *hDB, HNDLE *hKeyClient) {
   if (_hDB) {
      //printf("cm_get_experiment_database %d %d\n", _hDB, _hKeyClient);
      if (hDB != NULL)
         *hDB = _hDB;
      if (hKeyClient != NULL)
         *hKeyClient = _hKeyClient;
      return CM_SUCCESS;
   } else {
      //printf("cm_get_experiment_database no init\n");
      if (hDB != NULL)
         *hDB = 0;
      if (hKeyClient != NULL)
         *hKeyClient = 0;
      return CM_DB_ERROR;
   }
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/********************************************************************/
INT cm_get_experiment_semaphore(INT *semaphore_alarm, INT *semaphore_elog, INT *semaphore_history, INT *semaphore_msg)
/********************************************************************\

  Routine: cm_get_experiment_semaphore

  Purpose: Get the handle to the experiment wide semaphores

  Input:
    none

  Output:
    INT    semaphore_alarm      Alarm semaphore
    INT    semaphore_elog       Elog semaphore
    INT    semaphore_history    History semaphore
    INT    semaphore_msg        Message semaphore

  Function value:
    CM_SUCCESS              Successful completion

\********************************************************************/
{
   if (semaphore_alarm)
      *semaphore_alarm = _semaphore_alarm;
   if (semaphore_elog)
      *semaphore_elog = _semaphore_elog;
   if (semaphore_history)
      *semaphore_history = _semaphore_history;
   //if (semaphore_msg)
   //   *semaphore_msg = _semaphore_msg;
   if (semaphore_msg)
      *semaphore_msg = -1;

   return CM_SUCCESS;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

static int bm_validate_client_index(const BUFFER *buf, BOOL abort_if_invalid);

static BUFFER_CLIENT *bm_get_my_client(BUFFER *pbuf, BUFFER_HEADER *pheader);

#ifdef LOCAL_ROUTINES
static BUFFER* bm_get_buffer(const char *who, INT buffer_handle, int *pstatus);
static int bm_lock_buffer_read_cache(BUFFER *pbuf);
static int bm_lock_buffer_write_cache(BUFFER *pbuf);
static int bm_lock_buffer_mutex(BUFFER *pbuf);
static int xbm_lock_buffer(BUFFER *pbuf);
static void xbm_unlock_buffer(BUFFER *pbuf);

class bm_lock_buffer_guard
{
public:
   bool fDebug = false;
   
public:
   bm_lock_buffer_guard(BUFFER* pbuf, bool do_not_lock=false) // ctor
   {
      assert(pbuf != NULL);
      fBuf = pbuf;
      if (do_not_lock) {
         if (fDebug)
            printf("lock_buffer_guard(%s) ctor without lock\n", fBuf->buffer_name);
         return;
      }
      if (fDebug)
         printf("lock_buffer_guard(%s) ctor\n", fBuf->buffer_name);
      int status = xbm_lock_buffer(fBuf);
      if (status != BM_SUCCESS) {
         fLocked = false;
         fError  = true;
         fStatus = status;
      } else {
         fLocked = true;
      }
   }

   ~bm_lock_buffer_guard() // dtor
   {
      if (fInvalid) {
         if (fDebug)
            printf("lock_buffer_guard(invalid) dtor\n");
      } else {
         assert(fBuf != NULL);
         if (fDebug)
            printf("lock_buffer_guard(%s) dtor, locked %d, error %d\n", fBuf->buffer_name, fLocked, fError);
         if (fLocked) {
            xbm_unlock_buffer(fBuf);
            fLocked = false;
            fError = false;
         }
         fBuf = NULL;
      }
   }

   // make object uncopyable
   bm_lock_buffer_guard(const bm_lock_buffer_guard&) = delete;
   bm_lock_buffer_guard& operator=(const bm_lock_buffer_guard&) = delete;

   void unlock()
   {
      assert(fBuf != NULL);
      if (fDebug)
         printf("lock_buffer_guard(%s) unlock, locked %d, error %d\n", fBuf->buffer_name, fLocked, fError);
      assert(fLocked);
      xbm_unlock_buffer(fBuf);
      fLocked = false;
      fError = false;
   }

   bool relock()
   {
      assert(fBuf != NULL);
      if (fDebug)
         printf("lock_buffer_guard(%s) relock, locked %d, error %d\n", fBuf->buffer_name, fLocked, fError);
      assert(!fLocked);
      int status = xbm_lock_buffer(fBuf);
      if (status != BM_SUCCESS) {
         fLocked = false;
         fError  = true;
         fStatus = status;
      } else {
         fLocked = true;
      }
      return fLocked;
   }

   void invalidate()
   {
      assert(fBuf != NULL);
      if (fDebug)
         printf("lock_buffer_guard(%s) invalidate, locked %d, error %d\n", fBuf->buffer_name, fLocked, fError);
      assert(!fLocked);
      fInvalid = true;
      fBuf = NULL;
   }

   bool is_locked() const
   {
      return fLocked;
   }

   bool is_error() const
   {
      return fError;
   }

   int get_status() const
   {
      return fStatus;
   }

   BUFFER* get_pbuf() const
   {
      assert(!fInvalid); // pbuf was deleted
      assert(fBuf); // we do not return NULL
      return fBuf;
   }
   
private:
   BUFFER* fBuf    = NULL;
   bool    fLocked = false;
   bool    fError  = false;
   bool    fInvalid = false;
   int     fStatus = 0;
};
#endif

static INT bm_notify_client(const char *buffer_name, int s);

static INT bm_push_event(const char *buffer_name);

static void bm_defragment_event(HNDLE buffer_handle, HNDLE request_id,
                                EVENT_HEADER *pevent, void *pdata,
                                EVENT_HANDLER *dispatcher);

/********************************************************************/
/**
Sets the internal watchdog flags and the own timeout.
If call_watchdog is TRUE, the cm_watchdog routine is called
periodically from the system to show other clients that
this application is "alive". On UNIX systems, the
alarm() timer is used which is then not available for
user purposes.

The timeout specifies the time, after which the calling
application should be considered "dead" by other clients.
Normally, the cm_watchdog() routines is called periodically.
If a client crashes, this does not occur any more. Then
other clients can detect this and clear all buffer and
database entries of this application so they are not
blocked any more. If this application should not checked
by others, the timeout can be specified as zero.
It might be useful for debugging purposes to do so,
because if a debugger comes to a breakpoint and stops
the application, the periodic call of cm_watchdog
is disabled and the client looks like dead.

If the timeout is not zero, but the watchdog is not
called (call_watchdog == FALSE), the user must ensure
to call cm_watchdog periodically with a period of
WATCHDOG_INTERVAL milliseconds or less.

An application which calles system routines which block
the alarm signal for some time, might increase the
timeout to the maximum expected blocking time before
issuing the calls. One example is the logger doing
Exabyte tape IO, which can take up to one minute.
@param    call_watchdog   Call the cm_watchdog routine periodically
@param    timeout         Timeout for this application in ms
@return   CM_SUCCESS
*/
INT cm_set_watchdog_params_local(BOOL call_watchdog, DWORD timeout)
{
#ifdef LOCAL_ROUTINES
   _watchdog_timeout = timeout;

   std::vector<BUFFER*> mybuffers;

   gBuffersMutex.lock();
   mybuffers = gBuffers;
   gBuffersMutex.unlock();

   /* set watchdog timeout of all open buffers */
   for (BUFFER* pbuf : mybuffers) {
      
      if (!pbuf || !pbuf->attached)
         continue;

      bm_lock_buffer_guard pbuf_guard(pbuf);

      if (!pbuf_guard.is_locked())
         continue;
      
      BUFFER_HEADER *pheader = pbuf->buffer_header;
      BUFFER_CLIENT *pclient = bm_get_my_client(pbuf, pheader);
      
      /* clear entry from client structure in buffer header */
      pclient->watchdog_timeout = timeout;
      
      /* show activity */
      pclient->last_activity = ss_millitime();
   }

   /* set watchdog timeout for ODB */
   db_set_watchdog_params(timeout);

#endif /* LOCAL_ROUTINES */

   return CM_SUCCESS;
}

INT cm_set_watchdog_params(BOOL call_watchdog, DWORD timeout)
{
   /* set also local timeout to requested value (needed by cm_enable_watchdog()) */
   _watchdog_timeout = timeout;

   if (rpc_is_remote()) { // we are connected remotely
      return rpc_call(RPC_CM_SET_WATCHDOG_PARAMS, call_watchdog, timeout);
   } else if (rpc_is_mserver()) { // we are the mserver
      RPC_SERVER_ACCEPTION* sa = rpc_get_mserver_acception();
      if (sa)
         sa->watchdog_timeout = timeout;
      
      /* write timeout value to client enty in ODB */
      HNDLE hDB, hKey;
      cm_get_experiment_database(&hDB, &hKey);
      
      if (hDB) {
         db_set_mode(hDB, hKey, MODE_READ | MODE_WRITE, TRUE);
         db_set_value(hDB, hKey, "Link timeout", &timeout, sizeof(timeout), 1, TID_INT32);
         db_set_mode(hDB, hKey, MODE_READ, TRUE);
      }
      return DB_SUCCESS;
   } else {
      return cm_set_watchdog_params_local(call_watchdog, timeout);
   }
}

/********************************************************************/
/**
Return the current watchdog parameters
@param call_watchdog   Call the cm_watchdog routine periodically
@param timeout         Timeout for this application in seconds
@return   CM_SUCCESS
*/
INT cm_get_watchdog_params(BOOL *call_watchdog, DWORD *timeout) {
   if (call_watchdog)
      *call_watchdog = FALSE;
   if (timeout)
      *timeout = _watchdog_timeout;

   return CM_SUCCESS;
}

/********************************************************************/
/**
Return watchdog information about specific client
@param    hDB              ODB handle
@param    client_name     ODB client name
@param    timeout         Timeout for this application in seconds
@param    last            Last time watchdog was called in msec
@return   CM_SUCCESS, CM_NO_CLIENT, DB_INVALID_HANDLE
*/

INT cm_get_watchdog_info(HNDLE hDB, const char *client_name, DWORD *timeout, DWORD *last) {
   if (rpc_is_remote())
      return rpc_call(RPC_CM_GET_WATCHDOG_INFO, hDB, client_name, timeout, last);

#ifdef LOCAL_ROUTINES
   return db_get_watchdog_info(hDB, client_name, timeout, last);
#else                           /* LOCAL_ROUTINES */
   return CM_SUCCESS;
#endif                          /* LOCAL_ROUTINES */
}


/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/********************************************************************/

static void load_rpc_hosts(HNDLE hDB, HNDLE hKey, int index, void *info) {
   int status;
   int i, last;
   KEY key;
   int max_size;
   char *str;

   if (index != -99)
      cm_msg(MINFO, "load_rpc_hosts", "Reloading RPC hosts access control list via hotlink callback");

   status = db_get_key(hDB, hKey, &key);

   if (status != DB_SUCCESS)
      return;

   //printf("clear rpc hosts!\n");
   rpc_clear_allowed_hosts();

   max_size = key.item_size;
   str = (char *) malloc(max_size);

   last = 0;
   for (i = 0; i < key.num_values; i++) {
      int size = max_size;
      status = db_get_data_index(hDB, hKey, str, &size, i, TID_STRING);
      if (status != DB_SUCCESS)
         break;

      if (strlen(str) < 1) // skip emties
         continue;

      if (str[0] == '#') // skip commented-out entries
         continue;

      //printf("add rpc hosts %d [%s]\n", i, str);
      rpc_add_allowed_host(str);
      last = i;
   }

   if (key.num_values - last < 10) {
      int new_size = last + 10;
      status = db_set_num_values(hDB, hKey, new_size);
      if (status != DB_SUCCESS) {
         cm_msg(MERROR, "load_rpc_hosts",
                "Cannot resize the RPC hosts access control list, db_set_num_values(%d) status %d", new_size, status);
      }
   }

   free(str);
}

static void init_rpc_hosts(HNDLE hDB) {
   int status;
   char buf[256];
   int size, i;
   HNDLE hKey;

   strcpy(buf, "localhost");
   size = sizeof(buf);

   status = db_get_value(hDB, 0, "/Experiment/Security/RPC hosts/Allowed hosts[0]", buf, &size, TID_STRING, TRUE);

   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "init_rpc_hosts", "Cannot create the RPC hosts access control list, db_get_value() status %d",
             status);
      return;
   }

   size = sizeof(i);
   i = 0;
   status = db_get_value(hDB, 0, "/Experiment/Security/Disable RPC hosts check", &i, &size, TID_BOOL, TRUE);

   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "init_rpc_hosts", "Cannot create \"Disable RPC hosts check\", db_get_value() status %d", status);
      return;
   }

   if (i != 0) // RPC hosts check is disabled
      return;

   status = db_find_key(hDB, 0, "/Experiment/Security/RPC hosts/Allowed hosts", &hKey);

   if (status != DB_SUCCESS || hKey == 0) {
      cm_msg(MERROR, "init_rpc_hosts", "Cannot find the RPC hosts access control list, db_find_key() status %d",
             status);
      return;
   }

   load_rpc_hosts(hDB, hKey, -99, NULL);

   status = db_watch(hDB, hKey, load_rpc_hosts, NULL);

   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "init_rpc_hosts", "Cannot watch the RPC hosts access control list, db_watch() status %d", status);
      return;
   }
}

/********************************************************************/
INT cm_register_server(void)
/********************************************************************\

  Routine: cm_register_server

  Purpose: Register a server which can be called from other clients
           of a specific experiment.

  Input:
    none

  Output:
    none

  Function value:
    CM_SUCCESS              Successful completion

\********************************************************************/
{
   if (!_rpc_registered) {
      INT status;
      int size;
      HNDLE hDB, hKey;
      char name[NAME_LENGTH];
      char str[256];
      int port = 0;

      cm_get_experiment_database(&hDB, &hKey);

      size = sizeof(name);
      status = db_get_value(hDB, hKey, "Name", &name, &size, TID_STRING, FALSE);

      if (status != DB_SUCCESS) {
         cm_msg(MERROR, "cm_register_server", "cannot get client name, db_get_value() status %d", status);
         return status;
      }

      strlcpy(str, "/Experiment/Security/RPC ports/", sizeof(str));
      strlcat(str, name, sizeof(str));

      size = sizeof(port);
      status = db_get_value(hDB, 0, str, &port, &size, TID_UINT32, TRUE);

      if (status != DB_SUCCESS) {
         cm_msg(MERROR, "cm_register_server", "cannot get RPC port number, db_get_value(%s) status %d", str, status);
         return status;
      }

      int lport = 0; // actual port number assigned to us by the OS

      status = rpc_register_server(port, &_rpc_listen_socket, &lport);
      if (status != RPC_SUCCESS) {
         cm_msg(MERROR, "cm_register_server", "error, rpc_register_server(port=%d) status %d", port, status);
         return status;
      }

      _rpc_registered = TRUE;

      /* register MIDAS library functions */
      rpc_register_functions(rpc_get_internal_list(1), NULL);

      /* store port number in ODB */

      status = db_find_key(hDB, hKey, "Server Port", &hKey);
      if (status != DB_SUCCESS) {
         cm_msg(MERROR, "cm_register_server", "error, db_find_key(\"Server Port\") status %d", status);
         return status;
      }

      /* unlock database */
      db_set_mode(hDB, hKey, MODE_READ | MODE_WRITE, TRUE);

      /* set value */
      status = db_set_data(hDB, hKey, &lport, sizeof(INT), 1, TID_INT32);
      if (status != DB_SUCCESS) {
         cm_msg(MERROR, "cm_register_server", "error, db_set_data(\"Server Port\"=%d) status %d", port, status);
         return status;
      }

      /* lock database */
      db_set_mode(hDB, hKey, MODE_READ, TRUE);

      init_rpc_hosts(hDB);
   }

   return CM_SUCCESS;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Registers a callback function for run transitions.
This function internally registers the transition callback
function and publishes its request for transition notification by writing
a transition request to /System/Clients/\<pid\>/Transition XXX.
Other clients making a transition scan the transition requests of all clients
and call their transition callbacks via RPC.

Clients can register for transitions (Start/Stop/Pause/Resume) in a given
sequence. All sequence numbers given in the registration are sorted on
a transition and the clients are contacted in ascending order. By default,
all programs register with a sequence number of 500. The logger however
uses 200 for start, so that it can open files before the other clients
are contacted, and 800 for stop, so that the files get closed when all
other clients have gone already through the stop trantition.

The callback function returns CM_SUCCESS if it can perform the transition or
a value larger than one in case of error. An error string can be copied
into the error variable.
@attention The callback function will be called on transitions from inside the
    cm_yield() function which therefore must be contained in the main program loop.
\code
INT start(INT run_number, char *error)
{
  if (<not ok>)
    {
    strcpy(error, "Cannot start because ...");
    return 2;
    }
  printf("Starting run %d\n", run_number);
  return CM_SUCCESS;
}
main()
{
  ...
  cm_register_transition(TR_START, start, 500);
  do
    {
    status = cm_yield(1000);
    } while (status != RPC_SHUTDOWN &&
             status != SS_ABORT);
  ...
}
\endcode
@param transition Transition to register for (see @ref state_transition)
@param func Callback function.
@param sequence_number Sequence number for that transition (1..1000)
@return CM_SUCCESS
*/
INT cm_register_transition(INT transition, INT(*func)(INT, char *), INT sequence_number) {
   INT status;
   HNDLE hDB, hKey, hKeyTrans;
   KEY key;
   char str[256];

   /* check for valid transition */
   if (transition != TR_START && transition != TR_STOP && transition != TR_PAUSE && transition != TR_RESUME && transition != TR_STARTABORT) {
      cm_msg(MERROR, "cm_register_transition", "Invalid transition request \"%d\"", transition);
      return CM_INVALID_TRANSITION;
   }

   cm_get_experiment_database(&hDB, &hKey);

   rpc_register_function(RPC_RC_TRANSITION, rpc_transition_dispatch);

   /* register new transition request */

   {
      std::lock_guard<std::mutex> guard(_trans_table_mutex);

      for (size_t i = 0; i < _trans_table.size(); i++) {
         if (_trans_table[i].transition == transition && _trans_table[i].sequence_number == sequence_number) {
            cm_msg(MERROR, "cm_register_transition", "transition %s with sequence number %d is already registered", cm_transition_name(transition).c_str(), sequence_number);
            return CM_INVALID_TRANSITION;
         }
      }

      bool found = false;
      for (size_t i = 0; i < _trans_table.size(); i++) {
         if (!_trans_table[i].transition) {
            _trans_table[i].transition = transition;
            _trans_table[i].sequence_number = sequence_number;
            _trans_table[i].func = func;
            found = true;
            break;
         }
      }

      if (!found) {
         TRANS_TABLE tt;
         tt.transition = transition;
         tt.sequence_number = sequence_number;
         tt.func = func;
         _trans_table.push_back(tt);
      }

      // implicit unlock
   }

   sprintf(str, "Transition %s", cm_transition_name(transition).c_str());

   /* unlock database */
   db_set_mode(hDB, hKey, MODE_READ | MODE_WRITE | MODE_DELETE, TRUE);

   /* set value */
   status = db_find_key(hDB, hKey, str, &hKeyTrans);
   if (!hKeyTrans) {
      status = db_set_value(hDB, hKey, str, &sequence_number, sizeof(INT), 1, TID_INT32);
      if (status != DB_SUCCESS)
         return status;
   } else {
      status = db_get_key(hDB, hKeyTrans, &key);
      if (status != DB_SUCCESS)
         return status;
      status = db_set_data_index(hDB, hKeyTrans, &sequence_number, sizeof(INT), key.num_values, TID_INT32);
      if (status != DB_SUCCESS)
         return status;
   }

   /* re-lock database */
   db_set_mode(hDB, hKey, MODE_READ, TRUE);

   return CM_SUCCESS;
}

INT cm_deregister_transition(INT transition) {
   INT status;
   HNDLE hDB, hKey, hKeyTrans;
   char str[256];

   /* check for valid transition */
   if (transition != TR_START && transition != TR_STOP && transition != TR_PAUSE && transition != TR_RESUME && transition != TR_STARTABORT) {
      cm_msg(MERROR, "cm_deregister_transition", "Invalid transition request \"%d\"", transition);
      return CM_INVALID_TRANSITION;
   }

   cm_get_experiment_database(&hDB, &hKey);

   {
      std::lock_guard<std::mutex> guard(_trans_table_mutex);

      /* remove existing transition request */
      for (size_t i = 0; i < _trans_table.size(); i++) {
         if (_trans_table[i].transition == transition) {
            _trans_table[i].transition = 0;
            _trans_table[i].sequence_number = 0;
            _trans_table[i].func = NULL;
         }
      }

      // implicit unlock
   }
      
   sprintf(str, "Transition %s", cm_transition_name(transition).c_str());

   /* unlock database */
   db_set_mode(hDB, hKey, MODE_READ | MODE_WRITE | MODE_DELETE, TRUE);

   /* set value */
   status = db_find_key(hDB, hKey, str, &hKeyTrans);
   if (hKeyTrans) {
      status = db_delete_key(hDB, hKeyTrans, FALSE);
      if (status != DB_SUCCESS)
         return status;
   }

   /* re-lock database */
   db_set_mode(hDB, hKey, MODE_READ, TRUE);

   return CM_SUCCESS;
}

/********************************************************************/
/**
Change the transition sequence for the calling program.
@param transition TR_START, TR_PAUSE, TR_RESUME or TR_STOP.
@param sequence_number New sequence number, should be between 1 and 1000
@return     CM_SUCCESS
*/
INT cm_set_transition_sequence(INT transition, INT sequence_number) {
   INT status;
   HNDLE hDB, hKey;
   char str[256];

   /* check for valid transition */
   if (transition != TR_START && transition != TR_STOP && transition != TR_PAUSE && transition != TR_RESUME) {
      cm_msg(MERROR, "cm_set_transition_sequence", "Invalid transition request \"%d\"", transition);
      return CM_INVALID_TRANSITION;
   }

   {
      std::lock_guard<std::mutex> guard(_trans_table_mutex);

      int count = 0;
      for (size_t i = 0; i < _trans_table.size(); i++) {
         if (_trans_table[i].transition == transition) {
            _trans_table[i].sequence_number = sequence_number;
            count++;
         }
      }

      if (count == 0) {
         cm_msg(MERROR, "cm_set_transition_sequence", "transition %s is not registered", cm_transition_name(transition).c_str());
         return CM_INVALID_TRANSITION;
      } else if (count > 1) {
         cm_msg(MERROR, "cm_set_transition_sequence", "cannot change sequence number, transition %s is registered %d times", cm_transition_name(transition).c_str(), count);
         return CM_INVALID_TRANSITION;
      }

      /* Change local sequence number for this transition type */

      for (size_t i = 0; i < _trans_table.size(); i++) {
         if (_trans_table[i].transition == transition) {
            _trans_table[i].sequence_number = sequence_number;
         }
      }

      // implicit unlock
   }

   cm_get_experiment_database(&hDB, &hKey);

   /* unlock database */
   db_set_mode(hDB, hKey, MODE_READ | MODE_WRITE, TRUE);

   sprintf(str, "Transition %s", cm_transition_name(transition).c_str());

   /* set value */
   status = db_set_value(hDB, hKey, str, &sequence_number, sizeof(INT), 1, TID_INT32);
   if (status != DB_SUCCESS)
      return status;

   /* re-lock database */
   db_set_mode(hDB, hKey, MODE_READ, TRUE);

   return CM_SUCCESS;

}

INT cm_set_client_run_state(INT state) {
   INT status;
   HNDLE hDB, hKey;
   KEY key;

   cm_get_experiment_database(&hDB, &hKey);

   /* check that hKey is still valid */
   status = db_get_key(hDB, hKey, &key);

   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "cm_set_client_run_state",
             "Cannot set client run state, client hKey %d into /System/Clients is not valid, maybe this client was removed by a watchdog timeout",
             hKey);
      return status;
   }

   /* unlock database */
   db_set_mode(hDB, hKey, MODE_READ | MODE_WRITE, TRUE);

   /* set value */
   status = db_set_value(hDB, hKey, "Run state", &state, sizeof(INT), 1, TID_INT32);
   if (status != DB_SUCCESS)
      return status;

   /* re-lock database */
   db_set_mode(hDB, hKey, MODE_READ, TRUE);

   return CM_SUCCESS;

}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

static INT _requested_transition;
static DWORD _deferred_transition_mask;

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Register a deferred transition handler. If a client is
registered as a deferred transition handler, it may defer
a requested transition by returning FALSE until a certain
condition (like a motor reaches its end position) is
reached.
@param transition      One of TR_xxx
@param (*func)         Function which gets called whenever
                       a transition is requested. If it returns
                       FALSE, the transition is not performed.
@return CM_SUCCESS,    \<error\> Error from ODB access
*/
INT cm_register_deferred_transition(INT transition, BOOL(*func)(INT, BOOL)) {
   INT status, size;
   char tr_key_name[256];
   HNDLE hDB, hKey;

   cm_get_experiment_database(&hDB, &hKey);

   for (int i = 0; _deferred_trans_table[i].transition; i++)
      if (_deferred_trans_table[i].transition == transition)
         _deferred_trans_table[i].func = (int (*)(int, char *)) func;

   /* set new transition mask */
   _deferred_transition_mask |= transition;

   sprintf(tr_key_name, "Transition %s DEFERRED", cm_transition_name(transition).c_str());

   /* unlock database */
   db_set_mode(hDB, hKey, MODE_READ | MODE_WRITE, TRUE);

   /* set value */
   int i = 0;
   status = db_set_value(hDB, hKey, tr_key_name, &i, sizeof(INT), 1, TID_INT32);
   if (status != DB_SUCCESS)
      return status;

   /* re-lock database */
   db_set_mode(hDB, hKey, MODE_READ, TRUE);

   /* hot link requested transition */
   size = sizeof(_requested_transition);
   db_get_value(hDB, 0, "/Runinfo/Requested Transition", &_requested_transition, &size, TID_INT32, TRUE);
   db_find_key(hDB, 0, "/Runinfo/Requested Transition", &hKey);
   status = db_open_record(hDB, hKey, &_requested_transition, sizeof(INT), MODE_READ, NULL, NULL);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "cm_register_deferred_transition", "Cannot hotlink /Runinfo/Requested Transition");
      return status;
   }

   return CM_SUCCESS;
}

/********************************************************************/
/**
Check for any deferred transition. If a deferred transition
handler has been registered via the
cm_register_deferred_transition function, this routine
should be called regularly. It checks if a transition
request is pending. If so, it calld the registered handler
if the transition should be done and then actually does
the transition.
@return     CM_SUCCESS, \<error\>  Error from cm_transition()
*/
INT cm_check_deferred_transition() {
   INT i, status;
   char str[256];
   static BOOL first;

   if (_requested_transition == 0)
      first = TRUE;

   if (_requested_transition & _deferred_transition_mask) {
      for (i = 0; _deferred_trans_table[i].transition; i++)
         if (_deferred_trans_table[i].transition == _requested_transition)
            break;

      if (_deferred_trans_table[i].transition == _requested_transition) {
         if (((BOOL(*)(INT, BOOL)) _deferred_trans_table[i].func)(_requested_transition, first)) {
            status = cm_transition(_requested_transition | TR_DEFERRED, 0, str, sizeof(str), TR_SYNC, FALSE);
            if (status != CM_SUCCESS)
               cm_msg(MERROR, "cm_check_deferred_transition", "Cannot perform deferred transition: %s", str);

            /* bypass hotlink and set _requested_transition directly to zero */
            _requested_transition = 0;

            return status;
         }
         first = FALSE;
      }
   }

   return SUCCESS;
}


/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/********************************************************************/

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

struct TrClient {
   int transition = 0;
   int run_number = 0;
   int async_flag = 0;
   int debug_flag = 0;
   int sequence_number = 0;
   std::vector<int> wait_for_index;
   std::string host_name;
   std::string client_name;
   int port = 0;
   std::string key_name; /* this client key name in /System/Clients */
   std::atomic_int status{0};
   std::thread* thread = NULL;
   std::string errorstr;
   DWORD init_time = 0;    // time when tr_client created
   std::string waiting_for_client; // name of client we are waiting for
   DWORD connect_timeout = 0;
   DWORD connect_start_time = 0; // time when client rpc connection is started
   DWORD connect_end_time = 0;   // time when client rpc connection is finished
   DWORD rpc_timeout = 0;
   DWORD rpc_start_time = 0;     // time client rpc call is started
   DWORD rpc_end_time = 0;       // time client rpc call is finished
   DWORD end_time = 0;           // time client thread is finished

   TrClient() // ctor
   {
      // empty
   }

   ~TrClient() // dtor
   {
      //printf("TrClient::dtor: client \"%s\"\n", client_name);
      assert(thread == NULL);
   }

   void Print() const
   {
      printf("client \"%s\", transition %d, seqno %d, status %d", client_name.c_str(), transition, sequence_number, int(status));
      if (wait_for_index.size() > 0) {
         printf(", wait for:");
         for (size_t i=0; i<wait_for_index.size(); i++) {
            printf(" %d", wait_for_index[i]);
         }
      }
   }
};

static bool tr_compare(const std::unique_ptr<TrClient>& arg1, const std::unique_ptr<TrClient>& arg2) {
   return arg1->sequence_number < arg2->sequence_number;
}

/*------------------------------------------------------------------*/

struct TrState {
   int transition = 0;
   int run_number = 0;
   int async_flag = 0;
   int debug_flag = 0;
   int status     = 0;
   std::string errorstr;
   DWORD start_time = 0;
   DWORD end_time   = 0;
   std::vector<std::unique_ptr<TrClient>> clients;
};

/*------------------------------------------------------------------*/

static int tr_finish(HNDLE hDB, TrState* tr, int transition, int status, const char *errorstr)
{
   DWORD end_time = ss_millitime();

   if (transition != TR_STARTABORT) {
      db_set_value(hDB, 0, "/System/Transition/end_time", &end_time, sizeof(DWORD), 1, TID_UINT32);
      db_set_value(hDB, 0, "/System/Transition/status", &status, sizeof(INT), 1, TID_INT32);

      if (errorstr) {
         db_set_value(hDB, 0, "/System/Transition/error", errorstr, strlen(errorstr) + 1, 1, TID_STRING);
      } else if (status == CM_SUCCESS) {
         const char *buf = "Success";
         db_set_value(hDB, 0, "/System/Transition/error", buf, strlen(buf) + 1, 1, TID_STRING);
      } else {
         char buf[256];
         sprintf(buf, "status %d", status);
         db_set_value(hDB, 0, "/System/Transition/error", buf, strlen(buf) + 1, 1, TID_STRING);
      }
   }

   tr->status = status;
   tr->end_time = end_time;
   if (errorstr) {
      tr->errorstr = errorstr;
   } else {
      tr->errorstr = "(null)";
   }

   return status;
}

/*------------------------------------------------------------------*/

static void write_tr_client_to_odb(HNDLE hDB, const TrClient *tr_client) {
   //printf("Writing client [%s] to ODB\n", tr_client->client_name.c_str());

   int status;
   HNDLE hKey;

   if (tr_client->transition == TR_STARTABORT) {
      status = db_create_key(hDB, 0, "/System/Transition/TR_STARTABORT", TID_KEY);
      status = db_find_key(hDB, 0, "/System/Transition/TR_STARTABORT", &hKey);
      if (status != DB_SUCCESS)
         return;
   } else {
      status = db_create_key(hDB, 0, "/System/Transition/Clients", TID_KEY);
      status = db_find_key(hDB, 0, "/System/Transition/Clients", &hKey);
      if (status != DB_SUCCESS)
         return;
   }

   // same client_name can exist with different sequence numbers!
   std::string keyname = msprintf("%s_%d", tr_client->client_name.c_str(), tr_client->sequence_number);

   status = db_create_key(hDB, hKey, keyname.c_str(), TID_KEY);
   status = db_find_key(hDB, hKey, keyname.c_str(), &hKey);
   if (status != DB_SUCCESS)
      return;
   
   DWORD now = ss_millitime();

   //int   transition;
   //int   run_number;
   //int   async_flag;
   //int   debug_flag;
   status = db_set_value(hDB, hKey, "sequence_number", &tr_client->sequence_number, sizeof(INT), 1, TID_INT32);
   status = db_set_value(hDB, hKey, "client_name", tr_client->client_name.c_str(), tr_client->client_name.length() + 1, 1, TID_STRING);
   status = db_set_value(hDB, hKey, "host_name", tr_client->host_name.c_str(), tr_client->host_name.length() + 1, 1, TID_STRING);
   status = db_set_value(hDB, hKey, "port", &tr_client->port, sizeof(INT), 1, TID_INT32);
   status = db_set_value(hDB, hKey, "init_time", &tr_client->init_time, sizeof(DWORD), 1, TID_UINT32);
   status = db_set_value(hDB, hKey, "waiting_for_client", tr_client->waiting_for_client.c_str(), tr_client->waiting_for_client.length() + 1, 1, TID_STRING);
   status = db_set_value(hDB, hKey, "connect_timeout", &tr_client->connect_timeout, sizeof(DWORD), 1, TID_UINT32);
   status = db_set_value(hDB, hKey, "connect_start_time", &tr_client->connect_start_time, sizeof(DWORD), 1, TID_UINT32);
   status = db_set_value(hDB, hKey, "connect_end_time", &tr_client->connect_end_time, sizeof(DWORD), 1, TID_UINT32);
   status = db_set_value(hDB, hKey, "rpc_timeout", &tr_client->rpc_timeout, sizeof(DWORD), 1, TID_UINT32);
   status = db_set_value(hDB, hKey, "rpc_start_time", &tr_client->rpc_start_time, sizeof(DWORD), 1, TID_UINT32);
   status = db_set_value(hDB, hKey, "rpc_end_time", &tr_client->rpc_end_time, sizeof(DWORD), 1, TID_UINT32);
   status = db_set_value(hDB, hKey, "end_time", &tr_client->end_time, sizeof(DWORD), 1, TID_UINT32);
   status = db_set_value(hDB, hKey, "status", &tr_client->status, sizeof(INT), 1, TID_INT32);
   status = db_set_value(hDB, hKey, "error", tr_client->errorstr.c_str(), tr_client->errorstr.length() + 1, 1, TID_STRING);
   status = db_set_value(hDB, hKey, "last_updated", &now, sizeof(DWORD), 1, TID_UINT32);
}

/*------------------------------------------------------------------*/

/* Perform a detached transition through the external "mtransition" program */
static int cm_transition_detach(INT transition, INT run_number, char *errstr, INT errstr_size, INT async_flag, INT debug_flag) {
   HNDLE hDB;
   int status;
   const char *args[100];
   std::string path;
   char debug_arg[256];
   char start_arg[256];
   std::string expt_name;
   std::string mserver_hostname;

   int iarg = 0;

   cm_get_experiment_database(&hDB, NULL);

   const char *midassys = getenv("MIDASSYS");
   if (midassys) {
      path += midassys;
      path += DIR_SEPARATOR_STR;
      path += "bin";
      path += DIR_SEPARATOR_STR;
   }
   path += "mtransition";

   args[iarg++] = path.c_str();

   if (rpc_is_remote()) {
      /* if connected to mserver, pass connection info to mtransition */
      mserver_hostname = rpc_get_mserver_hostname();
      args[iarg++] = "-h";
      args[iarg++] = mserver_hostname.c_str();
   }

   /* get experiment name from ODB */
   db_get_value_string(hDB, 0, "/Experiment/Name", 0, &expt_name, FALSE);

   if (expt_name.length() > 0) {
      args[iarg++] = "-e";
      args[iarg++] = expt_name.c_str();
   }

   if (debug_flag) {
      args[iarg++] = "-d";

      sprintf(debug_arg, "%d", debug_flag);
      args[iarg++] = debug_arg;
   }

   if (transition == TR_STOP)
      args[iarg++] = "STOP";
   else if (transition == TR_PAUSE)
      args[iarg++] = "PAUSE";
   else if (transition == TR_RESUME)
      args[iarg++] = "RESUME";
   else if (transition == TR_START) {
      args[iarg++] = "START";

      sprintf(start_arg, "%d", run_number);
      args[iarg++] = start_arg;
   }

   args[iarg++] = NULL;

#if 0
   for (iarg = 0; args[iarg] != NULL; iarg++) {
      printf("arg[%d] [%s]\n", iarg, args[iarg]);
   }
#endif

   status = ss_spawnv(P_DETACH, args[0], args);

   if (status != SS_SUCCESS) {
      if (errstr != NULL) {
         sprintf(errstr, "Cannot execute mtransition, ss_spawnv() returned %d", status);
      }
      return CM_SET_ERROR;
   }

   return CM_SUCCESS;
}

/*------------------------------------------------------------------*/

/* contact a client via RPC and execute the remote transition */
static int cm_transition_call(TrState* s, int idx) {
   INT old_timeout, status, i, t1, t0, size;
   HNDLE hDB;
   HNDLE hConn = -1;
   int connect_timeout = 10000;
   int timeout = 120000;

   cm_get_experiment_database(&hDB, NULL);
   assert(hDB);

   TrClient *tr_client = s->clients[idx].get();

   tr_client->errorstr = "";
   //tr_client->init_time = ss_millitime();
   tr_client->waiting_for_client = "";
   tr_client->connect_timeout = 0;
   tr_client->connect_start_time = 0;
   tr_client->connect_end_time = 0;
   tr_client->rpc_timeout = 0;
   tr_client->rpc_start_time = 0;
   tr_client->rpc_end_time = 0;
   tr_client->end_time = 0;

   write_tr_client_to_odb(hDB, tr_client);

   /* wait for predecessor if set */
   if (tr_client->async_flag & TR_MTHREAD && !tr_client->wait_for_index.empty()) {
      while (1) {
         TrClient* wait_for = NULL;

         for (size_t i = 0; i < tr_client->wait_for_index.size(); i++) {
            int wait_for_index = tr_client->wait_for_index[i];

            assert(wait_for_index >= 0);
            assert(wait_for_index < (int)s->clients.size());

            TrClient *t = s->clients[wait_for_index].get();

            if (!t)
               continue;

            if (t->status == 0) {
               wait_for = t;
               break;
            }

            if (t->status != SUCCESS && tr_client->transition != TR_STOP) {
               cm_msg(MERROR, "cm_transition_call", "Transition %d aborted: client \"%s\" returned status %d", tr_client->transition, t->client_name.c_str(), int(t->status));
               tr_client->status = -1;
               tr_client->errorstr = msprintf("Aborted by failure of client \"%s\"", t->client_name.c_str());
               tr_client->end_time = ss_millitime();
               write_tr_client_to_odb(hDB, tr_client);
               return CM_SUCCESS;
            }
         }

         if (wait_for == NULL)
            break;

         tr_client->waiting_for_client = wait_for->client_name;
         write_tr_client_to_odb(hDB, tr_client);

         if (tr_client->debug_flag == 1)
            printf("Client \"%s\" waits for client \"%s\"\n", tr_client->client_name.c_str(), wait_for->client_name.c_str());

         i = 0;
         size = sizeof(i);
         status = db_get_value(hDB, 0, "/Runinfo/Transition in progress", &i, &size, TID_INT32, FALSE);

         if (status == DB_SUCCESS && i == 0) {
            cm_msg(MERROR, "cm_transition_call", "Client \"%s\" transition %d aborted while waiting for client \"%s\": \"/Runinfo/Transition in progress\" was cleared", tr_client->client_name.c_str(), tr_client->transition, wait_for->client_name.c_str());
            tr_client->status = -1;
            tr_client->errorstr = "Canceled";
            tr_client->end_time = ss_millitime();
            write_tr_client_to_odb(hDB, tr_client);
            return CM_SUCCESS;
         }

         ss_sleep(100);
      };
   }

   tr_client->waiting_for_client[0] = 0;

   /* contact client if transition mask set */
   if (tr_client->debug_flag == 1)
      printf("Connecting to client \"%s\" on host %s...\n", tr_client->client_name.c_str(), tr_client->host_name.c_str());
   if (tr_client->debug_flag == 2)
      cm_msg(MINFO, "cm_transition_call", "cm_transition_call: Connecting to client \"%s\" on host %s...", tr_client->client_name.c_str(), tr_client->host_name.c_str());

   /* get transition timeout for rpc connect */
   size = sizeof(timeout);
   db_get_value(hDB, 0, "/Experiment/Transition connect timeout", &connect_timeout, &size, TID_INT32, TRUE);

   if (connect_timeout < 1000)
      connect_timeout = 1000;

   /* get transition timeout */
   size = sizeof(timeout);
   db_get_value(hDB, 0, "/Experiment/Transition timeout", &timeout, &size, TID_INT32, TRUE);

   if (timeout < 1000)
      timeout = 1000;

   /* set our timeout for rpc_client_connect() */
   //old_timeout = rpc_get_timeout(RPC_HNDLE_CONNECT);
   rpc_set_timeout(RPC_HNDLE_CONNECT, connect_timeout, &old_timeout);

   tr_client->connect_timeout = connect_timeout;
   tr_client->connect_start_time = ss_millitime();

   write_tr_client_to_odb(hDB, tr_client);

   /* client found -> connect to its server port */
   status = rpc_client_connect(tr_client->host_name.c_str(), tr_client->port, tr_client->client_name.c_str(), &hConn);

   rpc_set_timeout(RPC_HNDLE_CONNECT, old_timeout);

   tr_client->connect_end_time = ss_millitime();
   write_tr_client_to_odb(hDB, tr_client);

   if (status != RPC_SUCCESS) {
      cm_msg(MERROR, "cm_transition_call",
             "cannot connect to client \"%s\" on host %s, port %d, status %d",
             tr_client->client_name.c_str(), tr_client->host_name.c_str(), tr_client->port, status);
      tr_client->errorstr = msprintf("Cannot connect to client \"%s\"", tr_client->client_name.c_str());

      /* clients that do not respond to transitions are dead or defective, get rid of them. K.O. */
      cm_shutdown(tr_client->client_name.c_str(), TRUE);
      cm_cleanup(tr_client->client_name.c_str(), TRUE);

      if (tr_client->transition != TR_STOP) {
         /* indicate abort */
         i = 1;
         db_set_value(hDB, 0, "/Runinfo/Start abort", &i, sizeof(INT), 1, TID_INT32);
         i = 0;
         db_set_value(hDB, 0, "/Runinfo/Transition in progress", &i, sizeof(INT), 1, TID_INT32);
      }

      tr_client->status = status;
      tr_client->end_time = ss_millitime();

      write_tr_client_to_odb(hDB, tr_client);
      return status;
   }

   if (tr_client->debug_flag == 1)
      printf("Connection established to client \"%s\" on host %s\n", tr_client->client_name.c_str(), tr_client->host_name.c_str());
   if (tr_client->debug_flag == 2)
      cm_msg(MINFO, "cm_transition_call",
             "cm_transition: Connection established to client \"%s\" on host %s",
             tr_client->client_name.c_str(), tr_client->host_name.c_str());

   /* call RC_TRANSITION on remote client with increased timeout */
   //old_timeout = rpc_get_timeout(hConn);
   rpc_set_timeout(hConn, timeout, &old_timeout);

   tr_client->rpc_timeout = timeout;
   tr_client->rpc_start_time = ss_millitime();
   write_tr_client_to_odb(hDB, tr_client);

   if (tr_client->debug_flag == 1)
      printf("Executing RPC transition client \"%s\" on host %s...\n",
             tr_client->client_name.c_str(), tr_client->host_name.c_str());
   if (tr_client->debug_flag == 2)
      cm_msg(MINFO, "cm_transition_call",
             "cm_transition: Executing RPC transition client \"%s\" on host %s...",
             tr_client->client_name.c_str(), tr_client->host_name.c_str());

   t0 = ss_millitime();

   char errorstr[TRANSITION_ERROR_STRING_LENGTH];
   errorstr[0] = 0;

   status = rpc_client_call(hConn, RPC_RC_TRANSITION, tr_client->transition, tr_client->run_number, errorstr, sizeof(errorstr), tr_client->sequence_number);

   tr_client->errorstr = errorstr;

   t1 = ss_millitime();

   tr_client->rpc_end_time = ss_millitime();

   write_tr_client_to_odb(hDB, tr_client);

   /* fix for clients returning 0 as error code */
   if (status == 0)
      status = FE_ERR_HW;

   /* reset timeout */
   rpc_set_timeout(hConn, old_timeout);

   //DWORD t2 = ss_millitime();

   if (tr_client->debug_flag == 1)
      printf("RPC transition finished client \"%s\" on host \"%s\" in %d ms with status %d\n",
             tr_client->client_name.c_str(), tr_client->host_name.c_str(), t1 - t0, status);
   if (tr_client->debug_flag == 2)
      cm_msg(MINFO, "cm_transition_call",
             "cm_transition: RPC transition finished client \"%s\" on host \"%s\" in %d ms with status %d",
             tr_client->client_name.c_str(), tr_client->host_name.c_str(), t1 - t0, status);

   if (status == RPC_NET_ERROR || status == RPC_TIMEOUT) {
      tr_client->errorstr = msprintf("RPC network error or timeout from client \'%s\' on host \"%s\"", tr_client->client_name.c_str(), tr_client->host_name.c_str());
      /* clients that do not respond to transitions are dead or defective, get rid of them. K.O. */
      cm_shutdown(tr_client->client_name.c_str(), TRUE);
      cm_cleanup(tr_client->client_name.c_str(), TRUE);
   } else if (status != CM_SUCCESS && tr_client->errorstr.empty()) {
      tr_client->errorstr = msprintf("Unknown error %d from client \'%s\' on host \"%s\"", status, tr_client->client_name.c_str(), tr_client->host_name.c_str());
   }

   tr_client->status = status;
   tr_client->end_time = ss_millitime();

   // write updated status and end_time to ODB

   write_tr_client_to_odb(hDB, tr_client);

#if 0
   printf("hconn %d cm_transition_call(%s) finished init %d connect %d end %d rpc %d end %d xxx %d end %d\n",
          hConn,
          tr_client->client_name.c_str(),
          tr_client->init_time - tr_client->init_time,
          tr_client->connect_start_time - tr_client->init_time,
          tr_client->connect_end_time - tr_client->init_time,
          tr_client->rpc_start_time - tr_client->init_time,
          tr_client->rpc_end_time - tr_client->init_time,
          t2 - tr_client->init_time,
          tr_client->end_time - tr_client->init_time);
#endif

   return CM_SUCCESS;
}

/*------------------------------------------------------------------*/

static int cm_transition_call_direct(TrClient *tr_client)
{
   HNDLE hDB;

   cm_get_experiment_database(&hDB, NULL);

   DWORD now = ss_millitime();

   tr_client->errorstr = "";
   //tr_client->init_time = now;
   tr_client->waiting_for_client = "";
   tr_client->connect_timeout = 0;
   tr_client->connect_start_time = now;
   tr_client->connect_end_time = now;
   tr_client->rpc_timeout = 0;
   tr_client->rpc_start_time = 0;
   tr_client->rpc_end_time = 0;
   tr_client->end_time = 0;

   write_tr_client_to_odb(hDB, tr_client);

   // find registered handler
   // NB: this code should match same code in rpc_transition_dispatch()
   // NB: only use the first handler, this is how MIDAS always worked
   // NB: we could run all handlers, but we can return the status and error string of only one of them.

   _trans_table_mutex.lock();
   size_t n = _trans_table.size();
   _trans_table_mutex.unlock();

   for (size_t i = 0; i < n; i++) {
      _trans_table_mutex.lock();
      TRANS_TABLE tt = _trans_table[i];
      _trans_table_mutex.unlock();
      if (tt.transition == tr_client->transition && tt.sequence_number == tr_client->sequence_number) {
         /* call registered function */
         if (tt.func) {
            if (tr_client->debug_flag == 1)
               printf("Calling local transition callback\n");
            if (tr_client->debug_flag == 2)
               cm_msg(MINFO, "cm_transition_call_direct", "cm_transition: Calling local transition callback");
            
            tr_client->rpc_start_time = ss_millitime();

            write_tr_client_to_odb(hDB, tr_client);

            char errorstr[TRANSITION_ERROR_STRING_LENGTH];
            errorstr[0] = 0;
            
            tr_client->status = tt.func(tr_client->run_number, errorstr);

            tr_client->errorstr = errorstr;
            
            tr_client->rpc_end_time = ss_millitime();
            
            if (tr_client->debug_flag == 1)
               printf("Local transition callback finished, status %d\n", int(tr_client->status));
            if (tr_client->debug_flag == 2)
               cm_msg(MINFO, "cm_transition_call_direct", "cm_transition: Local transition callback finished, status %d", int(tr_client->status));

            tr_client->end_time = ss_millitime();

            // write status and end_time to ODB

            write_tr_client_to_odb(hDB, tr_client);

            return tr_client->status;
         }
      }
   }

   cm_msg(MERROR, "cm_transition_call_direct", "no handler for transition %d with sequence number %d", tr_client->transition, tr_client->sequence_number);

   tr_client->status = CM_SUCCESS;
   tr_client->end_time = ss_millitime();

   // write status and end_time to ODB

   write_tr_client_to_odb(hDB, tr_client);

   return CM_SUCCESS;
}

/********************************************************************/
/**
Performs a run transition (Start/Stop/Pause/Resume).

Synchronous/Asynchronous flag.
If set to TR_ASYNC, the transition is done
asynchronously, meaning that clients are connected and told to execute their
callback routine, but no result is awaited. The return value is
specified by the transition callback function on the remote clients. If all callbacks
can perform the transition, CM_SUCCESS is returned. If one callback cannot
perform the transition, the return value of this callback is returned from
cm_transition().
The async_flag is usually FALSE so that transition callbacks can block a
run transition in case of problems and return an error string. The only exception are
situations where a run transition is performed automatically by a program which
cannot block in a transition. For example the logger can cause a run stop when a
disk is nearly full but it cannot block in the cm_transition() function since it
has its own run stop callback which must flush buffers and close disk files and
tapes.
\code
...
    i = 1;
    db_set_value(hDB, 0, "/Runinfo/Transition in progress", &i, sizeof(INT), 1, TID_INT32);

      status = cm_transition(TR_START, new_run_number, str, sizeof(str), SYNC, debug_flag);
      if (status != CM_SUCCESS)
      {
        // in case of error
        printf("Error: %s\n", str);
      }
    ...
\endcode
@param transition TR_START, TR_PAUSE, TR_RESUME or TR_STOP.
@param run_number New run number. If zero, use current run number plus one.
@param errstr returned error string.
@param errstr_size Size of error string.
@param async_flag TR_SYNC: synchronization flag (TR_SYNC:wait completion, TR_ASYNC: retun immediately)
@param debug_flag If 1 output debugging information, if 2 output via cm_msg().
@return CM_SUCCESS, \<error\> error code from remote client
*/
static INT cm_transition2(INT transition, INT run_number, char *errstr, INT errstr_size, INT async_flag, INT debug_flag)
{
   INT i, status, size, sequence_number, port, state;
   HNDLE hDB, hRootKey, hSubkey, hKey, hKeylocal, hKeyTrans;
   DWORD seconds;
   char str[256], tr_key_name[256];
   KEY key;
   BOOL deferred;
   char xerrstr[TRANSITION_ERROR_STRING_LENGTH];

   //printf("cm_transition2: transition %d, run_number %d, errstr %p, errstr_size %d, async_flag %d, debug_flag %d\n", transition, run_number, errstr, errstr_size, async_flag, debug_flag);

   /* if needed, use internal error string */
   if (!errstr) {
      errstr = xerrstr;
      errstr_size = sizeof(xerrstr);
   }

   /* erase error string */
   errstr[0] = 0;

   /* get key of local client */
   cm_get_experiment_database(&hDB, &hKeylocal);

   deferred = (transition & TR_DEFERRED) > 0;
   transition &= ~TR_DEFERRED;

   /* check for valid transition */
   if (transition != TR_START && transition != TR_STOP && transition != TR_PAUSE && transition != TR_RESUME
       && transition != TR_STARTABORT) {
      cm_msg(MERROR, "cm_transition", "Invalid transition request \"%d\"", transition);
      strlcpy(errstr, "Invalid transition request", errstr_size);
      return CM_INVALID_TRANSITION;
   }

   /* check if transition in progress */
   if (!deferred) {
      i = 0;
      size = sizeof(i);
      db_get_value(hDB, 0, "/Runinfo/Transition in progress", &i, &size, TID_INT32, TRUE);
      if (i == 1) {
         if (errstr) {
            sprintf(errstr, "Start/Stop transition %d already in progress, please try again later\n", i);
            strlcat(errstr, "or set \"/Runinfo/Transition in progress\" manually to zero.\n", errstr_size);
         }
         cm_msg(MERROR, "cm_transition", "another transition is already in progress");
         return CM_TRANSITION_IN_PROGRESS;
      }
   }

   /* indicate transition in progress */
   i = transition;
   db_set_value(hDB, 0, "/Runinfo/Transition in progress", &i, sizeof(INT), 1, TID_INT32);

   /* clear run abort flag */
   i = 0;
   db_set_value(hDB, 0, "/Runinfo/Start abort", &i, sizeof(INT), 1, TID_INT32);

   /* construct new transition state */

   TrState s;
   
   s.transition = transition;
   s.run_number = run_number;
   s.async_flag = async_flag;
   s.debug_flag = debug_flag;
   s.status = 0;
   s.errorstr[0] = 0;
   s.start_time = ss_millitime();
   s.end_time = 0;

   /* construct the ODB tree /System/Transition */

   status = db_find_key(hDB, 0, "/System/Transition/TR_STARTABORT", &hKey);
   if (status == DB_SUCCESS) {
      db_delete_key(hDB, hKey, FALSE);
   }

   if (transition != TR_STARTABORT) {
      status = db_find_key(hDB, 0, "/System/Transition/Clients", &hKey);
      if (status == DB_SUCCESS) {
         db_delete_key(hDB, hKey, FALSE);
      }
   }

   if (transition != TR_STARTABORT) {
      db_set_value(hDB, 0, "/System/Transition/transition", &transition, sizeof(INT), 1, TID_INT32);
      db_set_value(hDB, 0, "/System/Transition/run_number", &run_number, sizeof(INT), 1, TID_INT32);
      db_set_value(hDB, 0, "/System/Transition/start_time", &s.start_time, sizeof(DWORD), 1, TID_UINT32);
      db_set_value(hDB, 0, "/System/Transition/end_time", &s.end_time, sizeof(DWORD), 1, TID_UINT32);
      status = 0;
      db_set_value(hDB, 0, "/System/Transition/status", &status, sizeof(INT), 1, TID_INT32);
      db_set_value(hDB, 0, "/System/Transition/error", "", 1, 1, TID_STRING);
      db_set_value(hDB, 0, "/System/Transition/deferred", "", 1, 1, TID_STRING);
   }

   /* check for alarms */
   i = 0;
   size = sizeof(i);
   db_get_value(hDB, 0, "/Experiment/Prevent start on alarms", &i, &size, TID_BOOL, TRUE);
   if (i == TRUE && transition == TR_START) {
      al_check();
      if (al_get_alarms(str, sizeof(str)) > 0) {
         cm_msg(MERROR, "cm_transition", "Run start abort due to alarms: %s", str);
         sprintf(errstr, "Cannot start run due to alarms: ");
         strlcat(errstr, str, errstr_size);
         return tr_finish(hDB, &s, transition, AL_TRIGGERED, errstr);
      }
   }

   /* check for required programs */
   i = 0;
   size = sizeof(i);
   db_get_value(hDB, 0, "/Experiment/Prevent start on required progs", &i, &size, TID_BOOL, TRUE);
   if (i == TRUE && transition == TR_START) {

      HNDLE hkeyroot, hkey;

      /* check /programs alarms */
      db_find_key(hDB, 0, "/Programs", &hkeyroot);
      if (hkeyroot) {
         for (i = 0;; i++) {
            BOOL program_info_required = FALSE;
            status = db_enum_key(hDB, hkeyroot, i, &hkey);
            if (status == DB_NO_MORE_SUBKEYS)
               break;

            db_get_key(hDB, hkey, &key);

            /* don't check "execute on xxx" */
            if (key.type != TID_KEY)
               continue;

            size = sizeof(program_info_required);
            status = db_get_value(hDB, hkey, "Required", &program_info_required, &size, TID_BOOL, TRUE);
            if (status != DB_SUCCESS) {
               cm_msg(MERROR, "cm_transition", "Cannot get program info required, status %d", status);
               continue;
            }

            if (program_info_required) {
               std::string name = rpc_get_name();
               strlcpy(str, name.c_str(), sizeof(str));
               str[strlen(key.name)] = 0;
               if (!equal_ustring(str, key.name) && cm_exist(key.name, FALSE) == CM_NO_CLIENT) {
                  cm_msg(MERROR, "cm_transition", "Run start abort due to program \"%s\" not running", key.name);
                  sprintf(errstr, "Run start abort due to program \"%s\" not running", key.name);
                  return tr_finish(hDB, &s, transition, AL_TRIGGERED, errstr);
               }
            }
         }
      }
   }

   /* do detached transition via mtransition tool */
   if (async_flag & TR_DETACH) {
      status = cm_transition_detach(transition, run_number, errstr, errstr_size, async_flag, debug_flag);
      return tr_finish(hDB, &s, transition, status, errstr);
   }

   strlcpy(errstr, "Unknown error", errstr_size);

   if (debug_flag == 0) {
      size = sizeof(i);
      db_get_value(hDB, 0, "/Experiment/Transition debug flag", &debug_flag, &size, TID_INT32, TRUE);
   }

   /* if no run number is given, get it from ODB and increment it */
   if (run_number == 0) {
      size = sizeof(run_number);
      status = db_get_value(hDB, 0, "Runinfo/Run number", &run_number, &size, TID_INT32, TRUE);
      assert(status == SUCCESS);
      if (transition == TR_START) {
         run_number++;
      }
      s.run_number = run_number;

      if (transition != TR_STARTABORT) {
         db_set_value(hDB, 0, "/System/Transition/run_number", &run_number, sizeof(INT), 1, TID_INT32);
      }
   }

   if (run_number <= 0) {
      cm_msg(MERROR, "cm_transition", "aborting on attempt to use invalid run number %d", run_number);
      abort();
   }

   /* Set new run number in ODB */
   if (transition == TR_START) {
      if (debug_flag == 1)
         printf("Setting run number %d in ODB\n", run_number);
      if (debug_flag == 2)
         cm_msg(MINFO, "cm_transition", "cm_transition: Setting run number %d in ODB", run_number);

      status = db_set_value(hDB, 0, "Runinfo/Run number", &run_number, sizeof(run_number), 1, TID_INT32);
      if (status != DB_SUCCESS) {
         cm_msg(MERROR, "cm_transition", "cannot set Runinfo/Run number in database, status %d", status);
         abort();
      }
   }

   if (deferred) {
      if (debug_flag == 1)
         printf("Clearing /Runinfo/Requested transition\n");
      if (debug_flag == 2)
         cm_msg(MINFO, "cm_transition", "cm_transition: Clearing /Runinfo/Requested transition");

      /* remove transition request */
      i = 0;
      db_set_value(hDB, 0, "/Runinfo/Requested transition", &i, sizeof(int), 1, TID_INT32);
   } else {
      status = db_find_key(hDB, 0, "System/Clients", &hRootKey);
      if (status != DB_SUCCESS) {
         cm_msg(MERROR, "cm_transition", "cannot find System/Clients entry in database");
         if (errstr)
            strlcpy(errstr, "Cannot find /System/Clients in ODB", errstr_size);
         return tr_finish(hDB, &s, transition, status, errstr);
      }

      /* check if deferred transition already in progress */
      size = sizeof(i);
      db_get_value(hDB, 0, "/Runinfo/Requested transition", &i, &size, TID_INT32, TRUE);
      if (i) {
         if (errstr) {
            strlcpy(errstr, "Deferred transition already in progress", errstr_size);
            strlcat(errstr, ", to cancel, set \"/Runinfo/Requested transition\" to zero", errstr_size);
         }
         return tr_finish(hDB, &s, transition, CM_TRANSITION_IN_PROGRESS, errstr);
      }

      std::string trname = cm_transition_name(transition);

      sprintf(tr_key_name, "Transition %s DEFERRED", trname.c_str());

      /* search database for clients with deferred transition request */
      for (i = 0, status = 0;; i++) {
         status = db_enum_key(hDB, hRootKey, i, &hSubkey);
         if (status == DB_NO_MORE_SUBKEYS)
            break;

         if (status == DB_SUCCESS) {
            size = sizeof(sequence_number);
            status = db_get_value(hDB, hSubkey, tr_key_name, &sequence_number, &size, TID_INT32, FALSE);

            /* if registered for deferred transition, set flag in ODB and return */
            if (status == DB_SUCCESS) {
               size = NAME_LENGTH;
               db_get_value(hDB, hSubkey, "Name", str, &size, TID_STRING, TRUE);

               if (debug_flag == 1)
                  printf("---- Transition %s deferred by client \"%s\" ----\n", trname.c_str(), str);
               if (debug_flag == 2)
                  cm_msg(MINFO, "cm_transition", "cm_transition: ---- Transition %s deferred by client \"%s\" ----", trname.c_str(), str);

               if (debug_flag == 1)
                  printf("Setting /Runinfo/Requested transition\n");
               if (debug_flag == 2)
                  cm_msg(MINFO, "cm_transition", "cm_transition: Setting /Runinfo/Requested transition");

               /* /Runinfo/Requested transition is hot-linked by mfe.c and writing to it
                * will activate the deferred transition code in the frontend.
                * the transition itself will be run from the frontend via cm_transition(TR_DEFERRED) */

               db_set_value(hDB, 0, "/Runinfo/Requested transition", &transition, sizeof(int), 1, TID_INT32);

               db_set_value(hDB, 0, "/System/Transition/deferred", str, strlen(str) + 1, 1, TID_STRING);

               if (errstr)
                  sprintf(errstr, "Transition %s deferred by client \"%s\"", trname.c_str(), str);

               return tr_finish(hDB, &s, transition, CM_DEFERRED_TRANSITION, errstr);
            }
         }
      }
   }

   /* execute programs on start */
   if (transition == TR_START) {
      str[0] = 0;
      size = sizeof(str);
      db_get_value(hDB, 0, "/Programs/Execute on start run", str, &size, TID_STRING, TRUE);
      if (str[0])
         ss_system(str);

      db_find_key(hDB, 0, "/Programs", &hRootKey);
      if (hRootKey) {
         for (i = 0;; i++) {
            BOOL program_info_auto_start = FALSE;
            status = db_enum_key(hDB, hRootKey, i, &hKey);
            if (status == DB_NO_MORE_SUBKEYS)
               break;

            db_get_key(hDB, hKey, &key);

            /* don't check "execute on xxx" */
            if (key.type != TID_KEY)
               continue;

            size = sizeof(program_info_auto_start);
            status = db_get_value(hDB, hKey, "Auto start", &program_info_auto_start, &size, TID_BOOL, TRUE);
            if (status != DB_SUCCESS) {
               cm_msg(MERROR, "cm_transition", "Cannot get program info auto start, status %d", status);
               continue;
            }

            if (program_info_auto_start) {
               char start_command[MAX_STRING_LENGTH];
               start_command[0] = 0;

               size = sizeof(start_command);
               status = db_get_value(hDB, hKey, "Start command", &start_command, &size, TID_STRING, TRUE);
               if (status != DB_SUCCESS) {
                  cm_msg(MERROR, "cm_transition", "Cannot get program info start command, status %d", status);
                  continue;
               }

               if (start_command[0]) {
                  cm_msg(MINFO, "cm_transition", "Auto Starting program \"%s\", command \"%s\"", key.name,
                         start_command);
                  ss_system(start_command);
               }
            }
         }
      }
   }

   /* make sure odb entry is always created, otherwise we only see it after the first aborted run start, maybe never */
   str[0] = 0;
   size = sizeof(str);
   db_get_value(hDB, 0, "/Programs/Execute on start abort", str, &size, TID_STRING, TRUE);

   /* execute programs on startabort */
   if (transition == TR_STARTABORT) {
      if (str[0])
         ss_system(str);
   }

   /* set new start time in database */
   if (transition == TR_START) {
      /* ASCII format */
      cm_asctime(str, sizeof(str));
      db_set_value(hDB, 0, "Runinfo/Start Time", str, 32, 1, TID_STRING);

      /* reset stop time */
      seconds = 0;
      db_set_value(hDB, 0, "Runinfo/Stop Time binary", &seconds, sizeof(seconds), 1, TID_UINT32);

      /* Seconds since 1.1.1970 */
      cm_time(&seconds);
      db_set_value(hDB, 0, "Runinfo/Start Time binary", &seconds, sizeof(seconds), 1, TID_UINT32);
   }

   size = sizeof(state);
   status = db_get_value(hDB, 0, "Runinfo/State", &state, &size, TID_INT32, TRUE);

   /* set stop time in database */
   if (transition == TR_STOP) {
      if (status != DB_SUCCESS)
         cm_msg(MERROR, "cm_transition", "cannot get Runinfo/State in database");

      if (state != STATE_STOPPED) {
         /* stop time binary */
         cm_time(&seconds);
         status = db_set_value(hDB, 0, "Runinfo/Stop Time binary", &seconds, sizeof(seconds), 1, TID_UINT32);
         if (status != DB_SUCCESS)
            cm_msg(MERROR, "cm_transition", "cannot set \"Runinfo/Stop Time binary\" in database");

         /* stop time ascii */
         cm_asctime(str, sizeof(str));
         status = db_set_value(hDB, 0, "Runinfo/Stop Time", str, 32, 1, TID_STRING);
         if (status != DB_SUCCESS)
            cm_msg(MERROR, "cm_transition", "cannot set \"Runinfo/Stop Time\" in database");
      }
   }

   status = db_find_key(hDB, 0, "System/Clients", &hRootKey);
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "cm_transition", "cannot find System/Clients entry in database");
      if (errstr)
         strlcpy(errstr, "Cannot find /System/Clients in ODB", errstr_size);
      return tr_finish(hDB, &s, transition, status, errstr);
   }

   std::string trname = cm_transition_name(transition);

   /* check that all transition clients are alive */
   for (int i = 0;;) {
      status = db_enum_key(hDB, hRootKey, i, &hSubkey);
      if (status != DB_SUCCESS)
         break;

      status = cm_check_client(hDB, hSubkey);

      if (status == DB_SUCCESS) {
         /* this client is alive. Check next one! */
         i++;
         continue;
      }

      assert(status == CM_NO_CLIENT);

      /* start from scratch: removing odb entries as we iterate over them
       * does strange things to db_enum_key() */
      i = 0;
   }

   /* check for broken RPC connections */
   rpc_client_check();

   if (debug_flag == 1)
      printf("---- Transition %s started ----\n", trname.c_str());
   if (debug_flag == 2)
      cm_msg(MINFO, "cm_transition", "cm_transition: ---- Transition %s started ----", trname.c_str());

   sprintf(tr_key_name, "Transition %s", trname.c_str());

   /* search database for clients which registered for transition */

   for (int i = 0, status = 0;; i++) {
      KEY subkey;
      status = db_enum_key(hDB, hRootKey, i, &hSubkey);
      if (status == DB_NO_MORE_SUBKEYS)
         break;

      status = db_get_key(hDB, hSubkey, &subkey);
      assert(status == DB_SUCCESS);

      if (status == DB_SUCCESS) {
         status = db_find_key(hDB, hSubkey, tr_key_name, &hKeyTrans);

         if (status == DB_SUCCESS) {

            db_get_key(hDB, hKeyTrans, &key);

            for (int j = 0; j < key.num_values; j++) {
               size = sizeof(sequence_number);
               status = db_get_data_index(hDB, hKeyTrans, &sequence_number, &size, j, TID_INT32);
               assert(status == DB_SUCCESS);

               TrClient *c = new TrClient;

               c->init_time  = ss_millitime();
               c->transition = transition;
               c->run_number = run_number;
               c->async_flag = async_flag;
               c->debug_flag = debug_flag;
               c->sequence_number = sequence_number;
               c->status = 0;
               c->key_name = subkey.name;

               /* get client info */
               char client_name[NAME_LENGTH];
               size = sizeof(client_name);
               db_get_value(hDB, hSubkey, "Name", client_name, &size, TID_STRING, TRUE);
               c->client_name = client_name;

               char host_name[HOST_NAME_LENGTH];
               size = sizeof(host_name);
               db_get_value(hDB, hSubkey, "Host", host_name, &size, TID_STRING, TRUE);
               c->host_name = host_name;

               //printf("Found client [%s] name [%s] transition [%s], i=%d, j=%d\n", subkey.name, client_name, tr_key_name, i, j);

               if (hSubkey == hKeylocal && ((async_flag & TR_MTHREAD) == 0)) {
                  /* remember own client */
                  c->port = 0;
               } else {
                  size = sizeof(port);
                  db_get_value(hDB, hSubkey, "Server Port", &port, &size, TID_INT32, TRUE);
                  c->port = port;
               }

               /* check for duplicates */

               bool found = false;
               for (size_t k=0; k<s.clients.size(); k++) {
                  TrClient* cc = s.clients[k].get();
                  if (cc->client_name == c->client_name)
                     if (cc->host_name == c->host_name)
                        if (cc->port == c->port)
                           if (cc->sequence_number == c->sequence_number)
                              found = true;
               }

               if (!found) {
                  s.clients.push_back(std::unique_ptr<TrClient>(c));
                  c = NULL;
               } else {
                  cm_msg(MERROR, "cm_transition", "transition %s: client \"%s\" is registered with sequence number %d more than once", trname.c_str(), c->client_name.c_str(), c->sequence_number);
                  delete c;
                  c = NULL;
               }
            }
         }
      }
   }

   std::sort(s.clients.begin(), s.clients.end(), tr_compare);

   /* set predecessor for multi-threaded transitions */
   for (size_t idx = 0; idx < s.clients.size(); idx++) {
      if (s.clients[idx]->sequence_number == 0) {
         // sequence number 0 means "don't care"
      } else {
         /* find clients with smaller sequence number */
         if (idx > 0) {
            for (size_t i = idx - 1; ; i--) {
               if (s.clients[i]->sequence_number < s.clients[idx]->sequence_number) {
                  if (s.clients[i]->sequence_number > 0) {
                     s.clients[idx]->wait_for_index.push_back(i);
                  }
               }
               if (i==0)
                  break;
            }
         }
      }
   }

   for (size_t idx = 0; idx < s.clients.size(); idx++) {
      write_tr_client_to_odb(hDB, s.clients[idx].get());
   }

#if 0
   for (size_t idx = 0; idx < s.clients.size(); idx++) {
      printf("TrClient[%d]: ", int(idx));
      s.clients[idx]->Print();
      printf("\n");
   }
#endif

   /* contact ordered clients for transition -----------------------*/
   status = CM_SUCCESS;
   for (size_t idx = 0; idx < s.clients.size(); idx++) {
      if (debug_flag == 1)
         printf("\n==== Found client \"%s\" with sequence number %d\n",
                s.clients[idx]->client_name.c_str(), s.clients[idx]->sequence_number);
      if (debug_flag == 2)
         cm_msg(MINFO, "cm_transition",
                "cm_transition: ==== Found client \"%s\" with sequence number %d",
                s.clients[idx]->client_name.c_str(), s.clients[idx]->sequence_number);

      if (async_flag & TR_MTHREAD) {
         status = CM_SUCCESS;
         assert(s.clients[idx]->thread == NULL);
         s.clients[idx]->thread = new std::thread(cm_transition_call, &s, idx);
      } else {
         if (s.clients[idx]->port == 0) {
            /* if own client call transition callback directly */
            status = cm_transition_call_direct(s.clients[idx].get());
         } else {
            /* if other client call transition via RPC layer */
            status = cm_transition_call(&s, idx);
         }

         if (status == CM_SUCCESS && transition != TR_STOP)
            if (s.clients[idx]->status != SUCCESS) {
               cm_msg(MERROR, "cm_transition", "transition %s aborted: client \"%s\" returned status %d", trname.c_str(),
                      s.clients[idx]->client_name.c_str(), int(s.clients[idx]->status));
               break;
            }
      }

      if (status != CM_SUCCESS)
         break;
   }

   if (async_flag & TR_MTHREAD) {
      /* wait until all threads have finished */
      while (1) {
         int all_done = 1;

         for (size_t idx = 0; idx < s.clients.size(); idx++) {
            if (s.clients[idx]->status == 0) {
               all_done = 0;
               break;
            }

            if (s.clients[idx]->thread) {
               s.clients[idx]->thread->join();
               delete s.clients[idx]->thread;
               s.clients[idx]->thread = NULL;
            }
         }

         if (all_done)
            break;

         i = 0;
         size = sizeof(i);
         status = db_get_value(hDB, 0, "/Runinfo/Transition in progress", &i, &size, TID_INT32, FALSE);

         if (status == DB_SUCCESS && i == 0) {
            cm_msg(MERROR, "cm_transition", "transition %s aborted: \"/Runinfo/Transition in progress\" was cleared", trname.c_str());

            if (errstr != NULL)
               strlcpy(errstr, "Canceled", errstr_size);

            return tr_finish(hDB, &s, transition, CM_TRANSITION_CANCELED, "Canceled");
         }

         ss_sleep(100);
      }
   }

   /* search for any error */
   for (size_t idx = 0; idx < s.clients.size(); idx++)
      if (s.clients[idx]->status != CM_SUCCESS) {
         status = s.clients[idx]->status;
         if (errstr)
            strlcpy(errstr, s.clients[idx]->errorstr.c_str(), errstr_size);
         s.errorstr = msprintf("Aborted by client \"%s\"", s.clients[idx]->client_name.c_str());
         break;
      }

   if (transition != TR_STOP && status != CM_SUCCESS) {
      /* indicate abort */
      i = 1;
      db_set_value(hDB, 0, "/Runinfo/Start abort", &i, sizeof(INT), 1, TID_INT32);
      i = 0;
      db_set_value(hDB, 0, "/Runinfo/Transition in progress", &i, sizeof(INT), 1, TID_INT32);

      return tr_finish(hDB, &s, transition, status, errstr);
   }

   if (debug_flag == 1)
      printf("\n---- Transition %s finished ----\n", trname.c_str());
   if (debug_flag == 2)
      cm_msg(MINFO, "cm_transition", "cm_transition: ---- Transition %s finished ----", trname.c_str());

   /* set new run state in database */
   if (transition == TR_START || transition == TR_RESUME)
      state = STATE_RUNNING;

   if (transition == TR_PAUSE)
      state = STATE_PAUSED;

   if (transition == TR_STOP)
      state = STATE_STOPPED;

   if (transition == TR_STARTABORT)
      state = STATE_STOPPED;

   size = sizeof(state);
   status = db_set_value(hDB, 0, "Runinfo/State", &state, size, 1, TID_INT32);
   if (status != DB_SUCCESS)
      cm_msg(MERROR, "cm_transition", "cannot set Runinfo/State in database, db_set_value() status %d", status);

   /* send notification message */
   str[0] = 0;
   if (transition == TR_START)
      sprintf(str, "Run #%d started", run_number);
   if (transition == TR_STOP)
      sprintf(str, "Run #%d stopped", run_number);
   if (transition == TR_PAUSE)
      sprintf(str, "Run #%d paused", run_number);
   if (transition == TR_RESUME)
      sprintf(str, "Run #%d resumed", run_number);
   if (transition == TR_STARTABORT)
      sprintf(str, "Run #%d start aborted", run_number);

   if (str[0])
      cm_msg(MINFO, "cm_transition", "%s", str);

   /* lock/unlock ODB values if present */
   db_find_key(hDB, 0, "/Experiment/Lock when running", &hKey);
   if (hKey) {
      if (state == STATE_STOPPED)
         db_set_mode(hDB, hKey, MODE_READ | MODE_WRITE | MODE_DELETE, TRUE);
      else
         db_set_mode(hDB, hKey, MODE_READ, TRUE);
   }

   /* flush online database */
   if (transition == TR_STOP)
      db_flush_database(hDB);

   /* execute/stop programs on stop */
   if (transition == TR_STOP) {
      str[0] = 0;
      size = sizeof(str);
      db_get_value(hDB, 0, "/Programs/Execute on stop run", str, &size, TID_STRING, TRUE);
      if (str[0])
         ss_system(str);

      db_find_key(hDB, 0, "/Programs", &hRootKey);
      if (hRootKey) {
         for (i = 0;; i++) {
            BOOL program_info_auto_stop = FALSE;
            status = db_enum_key(hDB, hRootKey, i, &hKey);
            if (status == DB_NO_MORE_SUBKEYS)
               break;

            db_get_key(hDB, hKey, &key);

            /* don't check "execute on xxx" */
            if (key.type != TID_KEY)
               continue;

            size = sizeof(program_info_auto_stop);
            status = db_get_value(hDB, hKey, "Auto stop", &program_info_auto_stop, &size, TID_BOOL, TRUE);
            if (status != DB_SUCCESS) {
               cm_msg(MERROR, "cm_transition", "Cannot get program info auto stop, status %d", status);
               continue;
            }

            if (program_info_auto_stop) {
               cm_msg(MINFO, "cm_transition", "Auto Stopping program \"%s\"", key.name);
               cm_shutdown(key.name, FALSE);
            }
         }
      }
   }


   /* indicate success */
   i = 0;
   db_set_value(hDB, 0, "/Runinfo/Transition in progress", &i, sizeof(INT), 1, TID_INT32);

   if (errstr != NULL)
      strlcpy(errstr, "Success", errstr_size);

   return tr_finish(hDB, &s, transition, CM_SUCCESS, "Success");
}

/*------------------------------------------------------------------*/

/* wrapper around cm_transition2() to send a TR_STARTABORT in case of failure */
static INT cm_transition1(INT transition, INT run_number, char *errstr, INT errstr_size, INT async_flag, INT debug_flag) {
   int status;

   status = cm_transition2(transition, run_number, errstr, errstr_size, async_flag, debug_flag);

   if (transition == TR_START && status != CM_SUCCESS) {
      cm_msg(MERROR, "cm_transition", "Could not start a run: cm_transition() status %d, message \'%s\'", status,
             errstr);
      cm_transition2(TR_STARTABORT, run_number, NULL, 0, async_flag, debug_flag);
   }

   return status;
}

/*------------------------------------------------------------------*/

static INT tr_main_thread(void *param) {
   INT status;
   TR_PARAM *trp;

   trp = (TR_PARAM *) param;
   status = cm_transition1(trp->transition, trp->run_number, trp->errstr, trp->errstr_size, trp->async_flag, trp->debug_flag);

   trp->status = status;
   trp->finished = TRUE;

   return 0;
}

INT cm_transition_cleanup()
{
   if (_trp.thread && !_trp.finished) {
      //printf("main transition thread did not finish yet!\n");
      return CM_TRANSITION_IN_PROGRESS;
   }

   std::thread* t = _trp.thread.exchange(NULL);

   if (t) {
      t->join();
      delete t;
      t = NULL;
   }

   return CM_SUCCESS;
}

/* wrapper around cm_transition1() for detached multi-threaded transitions */
INT cm_transition(INT transition, INT run_number, char *errstr, INT errstr_size, INT async_flag, INT debug_flag) {
   int mflag = async_flag & TR_MTHREAD;
   int sflag = async_flag & TR_SYNC;

   int status = cm_transition_cleanup();

   if (status != CM_SUCCESS) {
      cm_msg(MERROR, "cm_transition", "previous transition did not finish yet");
      return CM_TRANSITION_IN_PROGRESS;
   }

   /* get key of local client */
   HNDLE hDB;
   cm_get_experiment_database(&hDB, NULL);

   bool deferred = (transition & TR_DEFERRED) > 0;
   INT trans_raw = (transition & ~TR_DEFERRED);

   /* check for valid transition */
   if (trans_raw != TR_START && trans_raw != TR_STOP && trans_raw != TR_PAUSE && trans_raw != TR_RESUME && trans_raw != TR_STARTABORT) {
      cm_msg(MERROR, "cm_transition", "Invalid transition request \"%d\"", transition);
      if (errstr) {
         strlcpy(errstr, "Invalid transition request", errstr_size);
      }
      return CM_INVALID_TRANSITION;
   }

   /* check if transition in progress */
   if (!deferred) {
      int i = 0;
      int size = sizeof(i);
      db_get_value(hDB, 0, "/Runinfo/Transition in progress", &i, &size, TID_INT32, TRUE);
      if (i == 1) {
         if (errstr) {
            sprintf(errstr, "Start/Stop transition %d already in progress, please try again later\n", i);
            strlcat(errstr, "or set \"/Runinfo/Transition in progress\" manually to zero.\n", errstr_size);
         }
         cm_msg(MERROR, "cm_transition", "another transition is already in progress");
         return CM_TRANSITION_IN_PROGRESS;
      }
   }

   if (mflag) {
      _trp.transition = transition;
      _trp.run_number = run_number;
      if (sflag) {
         /* in MTHREAD|SYNC mode, we wait until the main thread finishes and it is safe for it to write into errstr */
         _trp.errstr = errstr;
         _trp.errstr_size = errstr_size;
      } else {
         /* in normal MTHREAD mode, we return right away and
          * if errstr is a local variable in the caller and they return too,
          * errstr becomes a stale reference and writing into it will corrupt the stack
          * in the mlogger, errstr is a local variable in "start_the_run", "stop_the_run"
          * and we definitely corrupt mlogger memory with out this: */
         _trp.errstr = NULL;
         _trp.errstr_size = 0;
      }
      _trp.async_flag = async_flag;
      _trp.debug_flag = debug_flag;
      _trp.status = 0;
      _trp.finished = FALSE;

      if (errstr)
         *errstr = 0; // null error string

      //ss_thread_create(tr_main_thread, &_trp);

      std::thread* t = _trp.thread.exchange(new std::thread(tr_main_thread, &_trp));

      assert(t==NULL); // previous thread should have been reaped by cm_transition_cleanup()

      if (sflag) {

         /* wait until main thread has finished */
         do {
            ss_sleep(10);
         } while (!_trp.finished);

         std::thread* t = _trp.thread.exchange(NULL);

         if (t) {
            t->join();
            delete t;
            t = NULL;
         }

         return _trp.status;
      }
   } else
      return cm_transition1(transition, run_number, errstr, errstr_size, async_flag, debug_flag);

   return CM_SUCCESS;
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/********************************************************************/
INT cm_dispatch_ipc(const char *message, int message_size, int client_socket)
/********************************************************************\

  Routine: cm_dispatch_ipc

  Purpose: Called from ss_suspend if an IPC message arrives

  Input:
    INT   msg               IPC message we got, MSG_ODB/MSG_BM
    INT   p1, p2            Optional parameters
    int   s                 Optional server socket

  Output:
    none

  Function value:
    CM_SUCCESS              Successful completion

\********************************************************************/
{
   if (message[0] == 'O') {
      HNDLE hDB, hKey, hKeyRoot;
      INT index;
      index = 0;
      sscanf(message + 2, "%d %d %d %d", &hDB, &hKeyRoot, &hKey, &index);
      if (client_socket) {
         return db_update_record_mserver(hDB, hKeyRoot, hKey, index, client_socket);
      } else {
         return db_update_record_local(hDB, hKeyRoot, hKey, index);
      }
   }

   /* message == "B" means "resume event sender" */
   if (message[0] == 'B' && message[2] != ' ') {
      char str[NAME_LENGTH];

      //printf("cm_dispatch_ipc: message [%s], s=%d\n", message, s);

      strlcpy(str, message + 2, sizeof(str));
      if (strchr(str, ' '))
         *strchr(str, ' ') = 0;

      if (client_socket)
         return bm_notify_client(str, client_socket);
      else
         return bm_push_event(str);
   }

   //printf("cm_dispatch_ipc: message [%s] ignored\n", message);

   return CM_SUCCESS;
}

/********************************************************************/
static BOOL _ctrlc_pressed = FALSE;

void cm_ctrlc_handler(int sig) {
   if (_ctrlc_pressed) {
      printf("Received 2nd Ctrl-C, hard abort\n");
      exit(0);
   }
   printf("Received Ctrl-C, aborting...\n");
   _ctrlc_pressed = TRUE;

   ss_ctrlc_handler(cm_ctrlc_handler);
}

BOOL cm_is_ctrlc_pressed() {
   return _ctrlc_pressed;
}

void cm_ack_ctrlc_pressed() {
   _ctrlc_pressed = FALSE;
}

/********************************************************************/
int cm_exec_script(const char *odb_path_to_script)
/********************************************************************\

  Routine: cm_exec_script

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
   HNDLE hDB, hkey;
   KEY key;
   int status;

   status = cm_get_experiment_database(&hDB, NULL);
   if (status != DB_SUCCESS)
      return status;

   status = db_find_key(hDB, 0, odb_path_to_script, &hkey);
   if (status != DB_SUCCESS)
      return status;

   status = db_get_key(hDB, hkey, &key);
   if (status != DB_SUCCESS)
      return status;

   std::string command;

   if (key.type == TID_STRING) {
      int status = db_get_value_string(hDB, 0, odb_path_to_script, 0, &command, FALSE);
      if (status != DB_SUCCESS) {
         cm_msg(MERROR, "cm_exec_script", "Script ODB \"%s\" of type TID_STRING, db_get_value_string() error %d",
                odb_path_to_script, status);
         return status;
      }
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
            cm_msg(MERROR, "cm_exec_script", "Script ODB \"%s/%s\" should not be TID_KEY", odb_path_to_script,
                   subkey.name);
            return DB_TYPE_MISMATCH;
         } else {
            int size = subkey.item_size;
            char *buf = (char *) malloc(size);
            assert(buf != NULL);
            int status = db_get_data(hDB, hsubkey, buf, &size, subkey.type);
            if (status != DB_SUCCESS) {
               cm_msg(MERROR, "cm_exec_script", "Script ODB \"%s/%s\" of type %d, db_get_data() error %d",
                      odb_path_to_script, subkey.name, subkey.type, status);
               free(buf);
               return status;
            }
            if (subkey.type == TID_STRING) {
               command += buf;
            } else {
               command += db_sprintf(buf, subkey.item_size, 0, subkey.type);
            }
            free(buf);
         }
      }
   } else {
      cm_msg(MERROR, "cm_exec_script", "Script ODB \"%s\" has invalid type %d, should be TID_STRING or TID_KEY",
             odb_path_to_script, key.type);
      return DB_TYPE_MISMATCH;
   }

   // printf("exec_script: %s\n", command.c_str());

   if (command.length() > 0) {
      cm_msg(MINFO, "cm_exec_script", "Executing script \"%s\" from ODB \"%s\"", command.c_str(), odb_path_to_script);
      ss_system(command.c_str());
   }

   return SUCCESS;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

static void bm_cleanup(const char *who, DWORD actual_time, BOOL wrong_interval);

/********************************************************************/
/**
Perform midas periodic tasks - check alarms, update and check
timeouts on odb and on event buffers, etc. Normally called by cm_yield().
Programs that do not use cm_yield(), i.e. the mserver,
should call this function periodically, every 1 or 2 seconds.
@return CM_SUCCESS
*/
INT cm_periodic_tasks() {
   static DWORD alarm_last_checked_sec = 0;
   DWORD now_sec = ss_time();

   DWORD now_millitime = ss_millitime();
   static DWORD last_millitime = 0;
   DWORD tdiff_millitime = now_millitime - last_millitime;
   const DWORD kPeriod = 1000;
   if (last_millitime == 0) {
      last_millitime = now_millitime;
      tdiff_millitime = kPeriod; // make sure first time we come here we do something.
   }

   //printf("cm_periodic_tasks! tdiff_millitime %d\n", (int)tdiff_millitime);

   //if (now_millitime < last_millitime) {
   //   printf("millitime wraparound 0x%08x -> 0x%08x\n", last_millitime, now_millitime);
   //}

   /* check alarms once every 10 seconds */
   if (now_sec - alarm_last_checked_sec > 10) {
      al_check();
      alarm_last_checked_sec = now_sec;
   }

   /* run periodic checks previously done by cm_watchdog */

   if (tdiff_millitime >= kPeriod) {
      BOOL wrong_interval = FALSE;
      if (tdiff_millitime > 60000)
         wrong_interval = TRUE;

      //printf("millitime %u, diff %u, wrong_interval %d\n", now_millitime, tdiff_millitime, wrong_interval);

      bm_cleanup("cm_periodic_tasks", now_millitime, wrong_interval);
      db_cleanup("cm_periodic_tasks", now_millitime, wrong_interval);

      bm_write_statistics_to_odb();

      last_millitime = now_millitime;
   }

   /* reap transition thread */

   cm_transition_cleanup();

   return CM_SUCCESS;
}

/********************************************************************/
/**
Central yield functions for clients. This routine should
be called in an infinite loop by a client in order to
give the MIDAS system the opportunity to receive commands
over RPC channels, update database records and receive
events.
@param millisec         Timeout in millisec. If no message is
                        received during the specified timeout,
                        the routine returns. If millisec=-1,
                        it only returns when receiving an
                        RPC_SHUTDOWN message.
@return CM_SUCCESS, RPC_SHUTDOWN
*/
INT cm_yield(INT millisec) {
   INT status;
   INT bMore;
   //static DWORD last_yield = 0;
   //static DWORD last_yield_time = 0;
   //DWORD start_yield = ss_millitime();

   /* check for ctrl-c */
   if (_ctrlc_pressed)
      return RPC_SHUTDOWN;

   /* flush the cm_msg buffer */
   cm_msg_flush_buffer();

   /* check for available events */
   if (rpc_is_remote()) {
      //printf("cm_yield() calling bm_poll_event()\n");
      status = bm_poll_event();

      if (status == SS_ABORT) {
         return status;
      }

      if (status == BM_SUCCESS) {
         /* one or more events received by bm_poll_event() */
         status = ss_suspend(0, 0);
      } else {
         status = ss_suspend(millisec, 0);
      }

      return status;
   }

   status = cm_periodic_tasks();

   if (status != CM_SUCCESS)
      return status;

   //DWORD start_check = ss_millitime();

   bMore = bm_check_buffers();

   //DWORD end_check = ss_millitime();
   //printf("cm_yield: timeout %4d, yield period %4d, last yield time %4d, bm_check_buffers() elapsed %4d, returned %d\n", millisec, start_yield - last_yield, last_yield_time, end_check - start_check, bMore);
   //fflush(stdout);

   if (bMore == BM_CORRUPTED) {
      status = SS_ABORT;
   } else if (bMore) {
      /* if events available, quickly check other IPC channels */
      status = ss_suspend(0, 0);
   } else {
      status = ss_suspend(millisec, 0);
   }

   /* flush the cm_msg buffer */
   cm_msg_flush_buffer();

   //DWORD end_yield = ss_millitime();
   //last_yield_time = end_yield - start_yield;
   //last_yield = start_yield;

   return status;
}

/********************************************************************/
/**
Executes command via system() call
@param    command          Command string to execute
@param    result           stdout of command
@param    bufsize          string size in byte
@return   CM_SUCCESS
*/
INT cm_execute(const char *command, char *result, INT bufsize) {
   char str[256];
   INT n;
   int fh;
   int status = 0;
   static int check_cm_execute = 1;
   static int enable_cm_execute = 0;

   if (rpc_is_remote())
      return rpc_call(RPC_CM_EXECUTE, command, result, bufsize);

   if (check_cm_execute) {
      int status;
      int size;
      HNDLE hDB;
      check_cm_execute = 0;

      status = cm_get_experiment_database(&hDB, NULL);
      assert(status == DB_SUCCESS);

      size = sizeof(enable_cm_execute);
      status = db_get_value(hDB, 0, "/Experiment/Enable cm_execute", &enable_cm_execute, &size, TID_BOOL, TRUE);
      assert(status == DB_SUCCESS);

      //printf("enable_cm_execute %d\n", enable_cm_execute);
   }

   if (!enable_cm_execute) {
      char buf[32];
      strlcpy(buf, command, sizeof(buf));
      cm_msg(MERROR, "cm_execute", "cm_execute(%s...) is disabled by ODB \"/Experiment/Enable cm_execute\"", buf);
      return CM_WRONG_PASSWORD;
   }

   if (bufsize > 0) {
      strcpy(str, command);
      sprintf(str, "%s > %d.tmp", command, ss_getpid());

      status = system(str);

      sprintf(str, "%d.tmp", ss_getpid());
      fh = open(str, O_RDONLY, 0644);
      result[0] = 0;
      if (fh) {
         n = read(fh, result, bufsize - 1);
         result[MAX(0, n)] = 0;
         close(fh);
      }
      remove(str);
   } else {
      status = system(command);
   }

   if (status < 0) {
      cm_msg(MERROR, "cm_execute", "cm_execute(%s) error %d", command, status);
      return CM_SET_ERROR;
   }

   return CM_SUCCESS;
}



/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/********************************************************************/
INT cm_register_function(INT id, INT(*func)(INT, void **))
/********************************************************************\

  Routine: cm_register_function

  Purpose: Call rpc_register_function and publish the registered
           function under system/clients/<pid>/RPC

  Input:
    INT      id             RPC ID
    INT      *func          New dispatch function

  Output:
   <implicit: func gets copied to rpc_list>

  Function value:
   CM_SUCCESS               Successful completion
   RPC_INVALID_ID           RPC ID not found

\********************************************************************/
{
   HNDLE hDB, hKey;
   INT status;
   char str[80];

   status = rpc_register_function(id, func);
   if (status != RPC_SUCCESS)
      return status;

   cm_get_experiment_database(&hDB, &hKey);

   /* create new key for this id */
   status = 1;
   sprintf(str, "RPC/%d", id);

   db_set_mode(hDB, hKey, MODE_READ | MODE_WRITE, TRUE);
   status = db_set_value(hDB, hKey, str, &status, sizeof(BOOL), 1, TID_BOOL);
   db_set_mode(hDB, hKey, MODE_READ, TRUE);

   if (status != DB_SUCCESS)
      return status;

   return CM_SUCCESS;
}


/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

//
// Return "/"-terminated file path for given history channel
//

std::string cm_get_history_path(const char* history_channel)
{
   int status;
   HNDLE hDB;
   std::string path;

   cm_get_experiment_database(&hDB, NULL);

   if (history_channel && (strlen(history_channel) > 0)) {
      std::string p;
      p += "/Logger/History/";
      p += history_channel;
      p += "/History dir";

      // NB: be careful to avoid creating odb entries under /logger
      // for whatever values of "history_channel" we get called with!
      status = db_get_value_string(hDB, 0, p.c_str(), 0, &path, FALSE);
      if (status == DB_SUCCESS && path.length() > 0) {
         // if not absolute path, prepend with experiment directory
         if (path[0] != DIR_SEPARATOR)
            path = cm_get_path() + path;
         // append directory separator
         if (path.back() != DIR_SEPARATOR)
            path += DIR_SEPARATOR_STR;
         //printf("for [%s] returning [%s] from [%s]\n", history_channel, path.c_str(), p.c_str());
         return path;
      }
   }

   status = db_get_value_string(hDB, 0, "/Logger/History dir", 0, &path, TRUE);
   if (status == DB_SUCCESS && path.length() > 0) {
      // if not absolute path, prepend with experiment directory
      if (path[0] != DIR_SEPARATOR)
         path = cm_get_path() + path;
      // append directory separator
      if (path.back() != DIR_SEPARATOR)
         path += DIR_SEPARATOR_STR;
      //printf("for [%s] returning /Logger/History dir [%s]\n", history_channel, path.c_str());
      return path;
   }

   status = db_get_value_string(hDB, 0, "/Logger/Data dir", 0, &path, FALSE);
   if (status == DB_SUCCESS && path.length() > 0) {
      // if not absolute path, prepend with experiment directory
      if (path[0] != DIR_SEPARATOR)
         path = cm_get_path() + path;
      // append directory separator
      if (path.back() != DIR_SEPARATOR)
         path += DIR_SEPARATOR_STR;
      //printf("for [%s] returning /Logger/Data dir [%s]\n", history_channel, path.c_str());
      return path;
   }

   //printf("for [%s] returning experiment dir [%s]\n", history_channel, cm_get_path().c_str());
   return cm_get_path();
}

/**dox***************************************************************/
/** @} *//* end of cmfunctionc */

/**dox***************************************************************/
/** @addtogroup bmfunctionc
 *
 *  @{  */

/********************************************************************\
*                                                                    *
*                 bm_xxx  -  Buffer Manager Functions                *
*                                                                    *
\********************************************************************/

static DWORD _bm_max_event_size = 0;

#ifdef LOCAL_ROUTINES

// see locking code in xbm_lock_buffer()
static int _bm_lock_timeout = 5 * 60 * 1000;
static double _bm_mutex_timeout_sec = _bm_lock_timeout/1000 + 15.000;

static int bm_validate_client_index(const BUFFER *buf, BOOL abort_if_invalid) {
   static int prevent_recursion = 1;
   int badindex = 0;
   BUFFER_CLIENT *bcl = buf->buffer_header->client;

   if (buf->client_index < 0)
      badindex = 1;
   else if (buf->client_index > buf->buffer_header->max_client_index)
      badindex = 1;
   else {
      bcl = &(buf->buffer_header->client[buf->client_index]);
      if (bcl->name[0] == 0)
         badindex = 1;
      else if (bcl->pid != ss_getpid())
         badindex = 1;
   }

#if 0
   printf("bm_validate_client_index: badindex=%d, buf=%p, client_index=%d, max_client_index=%d, client_name=\'%s\', client_pid=%d, pid=%d\n",
        badindex, buf, buf->client_index, buf->buffer_header->max_client_index,
        buf->buffer_header->client[buf->client_index].name, buf->buffer_header->client[buf->client_index].pid,
        ss_getpid());
#endif

   if (badindex) {

      if (!abort_if_invalid)
         return -1;

      if (prevent_recursion) {
         prevent_recursion = 0;
         cm_msg(MERROR, "bm_validate_client_index",
                "My client index %d in buffer \'%s\' is invalid: client name \'%s\', pid %d should be my pid %d",
                buf->client_index, buf->buffer_header->name, bcl->name, bcl->pid, ss_getpid());
         cm_msg(MERROR, "bm_validate_client_index",
                "Maybe this client was removed by a timeout. See midas.log. Cannot continue, aborting...");
      }

      abort();
   }

   return buf->client_index;
}

static BUFFER_CLIENT *bm_get_my_client(BUFFER *pbuf, BUFFER_HEADER *pheader) {
   int my_client_index = bm_validate_client_index(pbuf, TRUE);
   return pheader->client + my_client_index;
}

#endif // LOCAL_ROUTINES

/********************************************************************/
/**
Check if an event matches a given event request by the
event id and trigger mask
@param event_id      Event ID of request
@param trigger_mask  Trigger mask of request
@param pevent    Pointer to event to check
@return TRUE      if event matches request
*/
INT bm_match_event(short int event_id, short int trigger_mask, const EVENT_HEADER *pevent) {
   // NB: cast everything to unsigned 16 bit to avoid bitwise comparison failure
   // because of mismatch in sign-extension between signed 16-bit event_id and
   // unsigned 16-bit constants. K.O.

   if (((uint16_t(pevent->event_id) & uint16_t(0xF000)) == uint16_t(EVENTID_FRAG1)) || ((uint16_t(pevent->event_id) & uint16_t(0xF000)) == uint16_t(EVENTID_FRAG)))
      /* fragmented event */
      return (((uint16_t(event_id) == uint16_t(EVENTID_ALL)) || (uint16_t(event_id) == (uint16_t(pevent->event_id) & uint16_t(0x0FFF))))
              && ((uint16_t(trigger_mask) == uint16_t(TRIGGER_ALL)) || ((uint16_t(trigger_mask) & uint16_t(pevent->trigger_mask)))));

   return (((uint16_t(event_id) == uint16_t(EVENTID_ALL)) || (uint16_t(event_id) == uint16_t(pevent->event_id)))
           && ((uint16_t(trigger_mask) == uint16_t(TRIGGER_ALL)) || ((uint16_t(trigger_mask) & uint16_t(pevent->trigger_mask)))));
}

#ifdef LOCAL_ROUTINES

/********************************************************************/
/**
Called to forcibly disconnect given client from a data buffer
*/
void bm_remove_client_locked(BUFFER_HEADER *pheader, int j) {
   int k, nc;
   BUFFER_CLIENT *pbctmp;

   /* clear entry from client structure in buffer header */
   memset(&(pheader->client[j]), 0, sizeof(BUFFER_CLIENT));

   /* calculate new max_client_index entry */
   for (k = MAX_CLIENTS - 1; k >= 0; k--)
      if (pheader->client[k].pid != 0)
         break;
   pheader->max_client_index = k + 1;

   /* count new number of clients */
   for (k = MAX_CLIENTS - 1, nc = 0; k >= 0; k--)
      if (pheader->client[k].pid != 0)
         nc++;
   pheader->num_clients = nc;

   /* check if anyone is waiting and wake him up */
   pbctmp = pheader->client;

   for (k = 0; k < pheader->max_client_index; k++, pbctmp++)
      if (pbctmp->pid && (pbctmp->write_wait || pbctmp->read_wait))
         ss_resume(pbctmp->port, "B  ");
}

/********************************************************************/
/**
Check all clients on buffer, remove invalid clients
*/
static void bm_cleanup_buffer_locked(BUFFER* pbuf, const char *who, DWORD actual_time) {
   BUFFER_HEADER *pheader;
   BUFFER_CLIENT *pbclient;
   int j;

   pheader = pbuf->buffer_header;
   pbclient = pheader->client;

   /* now check other clients */
   for (j = 0; j < pheader->max_client_index; j++, pbclient++) {
      if (pbclient->pid) {
         if (!ss_pid_exists(pbclient->pid)) {
            cm_msg(MINFO, "bm_cleanup",
                   "Client \'%s\' on buffer \'%s\' removed by %s because process pid %d does not exist", pbclient->name,
                   pheader->name, who, pbclient->pid);

            bm_remove_client_locked(pheader, j);
            continue;
         }
      }

      /* If client process has no activity, clear its buffer entry. */
      if (pbclient->pid && pbclient->watchdog_timeout > 0) {
         DWORD tdiff = actual_time - pbclient->last_activity;
#if 0
         printf("buffer [%s] client [%-32s] times 0x%08x 0x%08x, diff 0x%08x %5d, timeout %d\n",
                pheader->name,
                pbclient->name,
                pbclient->last_activity,
                actual_time,
                tdiff,
                tdiff,
                pbclient->watchdog_timeout);
#endif
         if (actual_time > pbclient->last_activity &&
             tdiff > pbclient->watchdog_timeout) {

            cm_msg(MINFO, "bm_cleanup", "Client \'%s\' on buffer \'%s\' removed by %s (idle %1.1lfs, timeout %1.0lfs)",
                   pbclient->name, pheader->name, who,
                   tdiff / 1000.0,
                   pbclient->watchdog_timeout / 1000.0);

            bm_remove_client_locked(pheader, j);
         }
      }
   }
}

/**
Update last activity time
*/
static void bm_update_last_activity(DWORD millitime) {
   int pid = ss_getpid();

   std::vector<BUFFER*> mybuffers;

   gBuffersMutex.lock();
   mybuffers = gBuffers;
   gBuffersMutex.unlock();

   for (BUFFER* pbuf : mybuffers) {
      if (!pbuf)
         continue;
      if (pbuf->attached) {

         bm_lock_buffer_guard pbuf_guard(pbuf);

         if (!pbuf_guard.is_locked())
            continue;

         BUFFER_HEADER *pheader = pbuf->buffer_header;
         for (int j = 0; j < pheader->max_client_index; j++) {
            BUFFER_CLIENT *pclient = pheader->client + j;
            if (pclient->pid == pid) {
               pclient->last_activity = millitime;
            }
         }
      }
   }
}

#endif // LOCAL_ROUTINES

/**
Check all clients on all buffers, remove invalid clients
*/
static void bm_cleanup(const char *who, DWORD actual_time, BOOL wrong_interval)
{
#ifdef LOCAL_ROUTINES

   //printf("bm_cleanup: called by %s, actual_time %d, wrong_interval %d\n", who, actual_time, wrong_interval);

   std::vector<BUFFER*> mybuffers;

   gBuffersMutex.lock();
   mybuffers = gBuffers;
   gBuffersMutex.unlock();

   /* check buffers */
   for (BUFFER* pbuf : mybuffers) {
      if (!pbuf)
         continue;
      if (pbuf->attached) {
         /* update the last_activity entry to show that we are alive */

         bm_lock_buffer_guard pbuf_guard(pbuf);
         
         if (!pbuf_guard.is_locked())
            continue;

         BUFFER_HEADER *pheader = pbuf->buffer_header;
         BUFFER_CLIENT *pclient = bm_get_my_client(pbuf, pheader);
         pclient->last_activity = actual_time;

         /* don't check other clients if interval is strange */
         if (!wrong_interval)
            bm_cleanup_buffer_locked(pbuf, who, actual_time);
      }
   }
#endif // LOCAL_ROUTINES
}

#ifdef LOCAL_ROUTINES

static BOOL bm_validate_rp(const char *who, const BUFFER_HEADER *pheader, int rp) {
   if (rp < 0 || rp > pheader->size) {
      cm_msg(MERROR, "bm_validate_rp",
             "error: buffer \"%s\" is corrupted: rp %d is invalid. buffer read_pointer %d, write_pointer %d, size %d, called from %s",
             pheader->name,
             rp,
             pheader->read_pointer,
             pheader->write_pointer,
             pheader->size,
             who);
      return FALSE;
   }

   if ((rp + (int) sizeof(EVENT_HEADER)) > pheader->size) {
      // note ">" here, has to match bm_incr_rp() and bm_write_to_buffer()
      cm_msg(MERROR, "bm_validate_rp",
             "error: buffer \"%s\" is corrupted: rp %d plus event header point beyond the end of buffer by %d bytes. buffer read_pointer %d, write_pointer %d, size %d, called from %s",
             pheader->name,
             rp,
             (int) (rp + sizeof(EVENT_HEADER) - pheader->size),
             pheader->read_pointer,
             pheader->write_pointer,
             pheader->size,
             who);
      return FALSE;
   }

   return TRUE;
}

#if 0
static FILE* gRpLog = NULL;
#endif

static int bm_incr_rp_no_check(const BUFFER_HEADER *pheader, int rp, int total_size)
{
#if 0
   if (gRpLog == NULL) {
      gRpLog = fopen("rp.log", "a");
   }
   if (gRpLog && (total_size < 16)) {
      const char *pdata = (const char *) (pheader + 1);
      const DWORD *pevent = (const DWORD*) (pdata + rp);
      fprintf(gRpLog, "%s: rp %d, total_size %d, at rp 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n", pheader->name, rp, total_size,
              pevent[0], pevent[1], pevent[2], pevent[3], pevent[4], pevent[5]);
   }
#endif

   // these checks are already done before we come here.
   // but we check again as last-ressort protection. K.O.
   assert(total_size > 0);
   assert(total_size >= (int)sizeof(EVENT_HEADER));

   rp += total_size;
   if (rp >= pheader->size) {
      rp -= pheader->size;
   } else if ((rp + (int) sizeof(EVENT_HEADER)) > pheader->size) {
      // note: ">" here to match bm_write_to_buffer_locked() and bm_validate_rp().
      // if at the end of the buffer, the remaining free space is exactly
      // equal to the size of an event header, the event header
      // is written there, the pointer is wrapped and the event data
      // is written to the beginning of the buffer.
      rp = 0;
   }
   return rp;
}

static int bm_next_rp(const char *who, const BUFFER_HEADER *pheader, const char *pdata, int rp) {
   const EVENT_HEADER *pevent = (const EVENT_HEADER *) (pdata + rp);
   int event_size = pevent->data_size + sizeof(EVENT_HEADER);
   int total_size = ALIGN8(event_size);

   if (pevent->data_size <= 0 || total_size <= 0 || total_size > pheader->size) {
      cm_msg(MERROR, "bm_next_rp",
             "error: buffer \"%s\" is corrupted: rp %d points to an invalid event: data_size %d, event size %d, total_size %d, buffer read_pointer %d, write_pointer %d, size %d, called from %s",
             pheader->name,
             rp,
             pevent->data_size,
             event_size,
             total_size,
             pheader->read_pointer,
             pheader->write_pointer,
             pheader->size,
             who);
      return -1;
   }

   int remaining = 0;
   if (rp < pheader->write_pointer) {
      remaining = pheader->write_pointer - rp;
   } else {
      remaining = pheader->size - rp;
      remaining += pheader->write_pointer;
   }

   //printf("bm_next_rp: total_size %d, remaining %d, rp %d, wp %d, size %d\n", total_size, remaining, rp, pheader->write_pointer, pheader->size);

   if (total_size > remaining) {
      cm_msg(MERROR, "bm_next_rp",
             "error: buffer \"%s\" is corrupted: rp %d points to an invalid event: data_size %d, event size %d, total_size %d, buffer read_pointer %d, write_pointer %d, size %d, remaining %d, called from %s",
             pheader->name,
             rp,
             pevent->data_size,
             event_size,
             total_size,
             pheader->read_pointer,
             pheader->write_pointer,
             pheader->size,
             remaining,
             who);
      return -1;
   }

   rp = bm_incr_rp_no_check(pheader, rp, total_size);

   return rp;
}

static int bm_validate_buffer_locked(const BUFFER *pbuf) {
   const BUFFER_HEADER *pheader = pbuf->buffer_header;
   const char *pdata = (const char *) (pheader + 1);

   //printf("bm_validate_buffer: buffer \"%s\"\n", pheader->name);

   //printf("size: %d, rp: %d, wp: %d\n", pheader->size, pheader->read_pointer, pheader->write_pointer);

   //printf("clients: max: %d, num: %d, MAX_CLIENTS: %d\n", pheader->max_client_index, pheader->num_clients, MAX_CLIENTS);

   if (pheader->read_pointer < 0 || pheader->read_pointer >= pheader->size) {
      cm_msg(MERROR, "bm_validate_buffer",
             "buffer \"%s\" is corrupted: invalid read pointer %d. Size %d, write pointer %d", pheader->name,
             pheader->read_pointer, pheader->size, pheader->write_pointer);
      return BM_CORRUPTED;
   }

   if (pheader->write_pointer < 0 || pheader->write_pointer >= pheader->size) {
      cm_msg(MERROR, "bm_validate_buffer",
             "buffer \"%s\" is corrupted: invalid write pointer %d. Size %d, read pointer %d", pheader->name,
             pheader->write_pointer, pheader->size, pheader->read_pointer);
      return BM_CORRUPTED;
   }

   if (!bm_validate_rp("bm_validate_buffer_locked", pheader, pheader->read_pointer)) {
      cm_msg(MERROR, "bm_validate_buffer", "buffer \"%s\" is corrupted: read pointer %d is invalid", pheader->name,
             pheader->read_pointer);
      return BM_CORRUPTED;
   }

   int event_count = 0;
   int rp = pheader->read_pointer;
   int rp0 = -1;
   while (rp != pheader->write_pointer) {
      if (!bm_validate_rp("bm_validate_buffer_locked", pheader, rp)) {
         cm_msg(MERROR, "bm_validate_buffer", "buffer \"%s\" is corrupted: invalid rp %d, last good event at rp %d",
                pheader->name, rp, rp0);
         return BM_CORRUPTED;
      }
      //bm_print_event(pdata, rp);
      int rp1 = bm_next_rp("bm_validate_buffer_locked", pheader, pdata, rp);
      if (rp1 < 0) {
         cm_msg(MERROR, "bm_validate_buffer",
                "buffer \"%s\" is corrupted: invalid event at rp %d, last good event at rp %d", pheader->name, rp, rp0);
         return BM_CORRUPTED;
      }
      event_count++;
      rp0 = rp;
      rp = rp1;
   }

   //printf("buffered events: %d\n", event_count);

   int i;
   for (i = 0; i < MAX_CLIENTS; i++) {
      const BUFFER_CLIENT *c = &pheader->client[i];
      if (c->pid == 0)
         continue;
      BOOL get_all = FALSE;
      int j;
      for (j = 0; j < MAX_EVENT_REQUESTS; j++) {
         const EVENT_REQUEST *r = &c->event_request[j];
         if (!r->valid)
            continue;
         BOOL xget_all = r->sampling_type == GET_ALL;
         get_all = (get_all || xget_all);
         //printf("client slot %d: pid %d, name \"%s\", request %d: id %d, valid %d, sampling_type %d, get_all %d\n", i, c->pid, c->name, j, r->id, r->valid, r->sampling_type, xget_all);
      }

      int event_count = 0;
      int rp = c->read_pointer;
      int rp0 = -1;
      while (rp != pheader->write_pointer) {
         //bm_print_event(pdata, rp);
         int rp1 = bm_next_rp("bm_validate_buffer_locked", pheader, pdata, rp);
         if (rp1 < 0) {
            cm_msg(MERROR, "bm_validate_buffer",
                   "buffer \"%s\" is corrupted for client \"%s\" rp %d: invalid event at rp %d, last good event at rp %d",
                   pheader->name, c->name, c->read_pointer, rp, rp0);
            return BM_CORRUPTED;
         }
         event_count++;
         rp0 = rp;
         rp = rp1;
      }

      //printf("client slot %d: pid %d, name \"%s\", port %d, rp: %d, get_all %d, %d events\n", i, c->pid, c->name, c->port, c->read_pointer, get_all, event_count);
   }

   return BM_SUCCESS;
}

static void bm_reset_buffer_locked(BUFFER *pbuf) {
   BUFFER_HEADER *pheader = pbuf->buffer_header;

   //printf("bm_reset_buffer: buffer \"%s\"\n", pheader->name);

   pheader->read_pointer = 0;
   pheader->write_pointer = 0;

   int i;
   for (i = 0; i < pheader->max_client_index; i++) {
      BUFFER_CLIENT *pc = pheader->client + i;
      if (pc->pid) {
         pc->read_pointer = 0;
      }
   }
}

static void bm_clear_buffer_statistics(HNDLE hDB, BUFFER *pbuf) {
   HNDLE hKey;
   int status;

   char str[256 + 2 * NAME_LENGTH];
   sprintf(str, "/System/buffers/%s/Clients/%s/writes_blocked_by", pbuf->buffer_name, pbuf->client_name);
   //printf("delete [%s]\n", str);
   status = db_find_key(hDB, 0, str, &hKey);
   if (status == DB_SUCCESS) {
      status = db_delete_key(hDB, hKey, FALSE);
   }
}

struct BUFFER_INFO
{
   BOOL get_all_flag = false;         /**< this is a get_all reader     */

   /* buffer statistics */
   int count_lock = 0;                /**< count how many times we locked the buffer */
   int count_sent = 0;                /**< count how many events we sent */
   double bytes_sent = 0;             /**< count how many bytes we sent */
   int count_write_wait = 0;          /**< count how many times we waited for free space */
   DWORD time_write_wait = 0;         /**< count for how long we waited for free space, in units of ss_millitime() */
   int last_count_lock = 0;           /**< avoid writing statistics to odb if lock count did not change */
   DWORD wait_start_time = 0;         /**< time when we started the wait */
   int wait_client_index = 0;         /**< waiting for which client */
   int max_requested_space = 0;       /**< waiting for this many bytes of free space */
   int count_read = 0;                /**< count how many events we read */
   double bytes_read = 0;             /**< count how many bytes we read */
   int client_count_write_wait[MAX_CLIENTS]; /**< per-client count_write_wait */
   DWORD client_time_write_wait[MAX_CLIENTS]; /**< per-client time_write_wait */

   BUFFER_INFO(BUFFER* pbuf)
   {
      get_all_flag = pbuf->get_all_flag;

      /* buffer statistics */
      count_lock        = pbuf->count_lock;
      count_sent        = pbuf->count_sent;
      bytes_sent        = pbuf->bytes_sent;
      count_write_wait  = pbuf->count_write_wait;
      time_write_wait   = pbuf->time_write_wait;
      last_count_lock   = pbuf->last_count_lock;
      wait_start_time   = pbuf->wait_start_time;
      wait_client_index = pbuf->wait_client_index;
      max_requested_space = pbuf->max_requested_space;
      count_read        = pbuf->count_read;
      bytes_read        = pbuf->bytes_read;

      for (int i=0; i<MAX_CLIENTS; i++) {
         client_count_write_wait[i] = pbuf->client_count_write_wait[i];
         client_time_write_wait[i] = pbuf->client_time_write_wait[i];
      }
   };
};

static void bm_write_buffer_statistics_to_odb_copy(HNDLE hDB, const char* buffer_name, const char* client_name, int client_index, BUFFER_INFO *pbuf, BUFFER_HEADER* pheader)
{
   int status;

   DWORD now = ss_millitime();

   HNDLE hKey;
   status = db_find_key(hDB, 0, "/System/Buffers", &hKey);
   if (status != DB_SUCCESS) {
      db_create_key(hDB, 0, "/System/Buffers", TID_KEY);
      status = db_find_key(hDB, 0, "/System/Buffers", &hKey);
      if (status != DB_SUCCESS)
         return;
   }

   HNDLE hKeyBuffer;
   status = db_find_key(hDB, hKey, buffer_name, &hKeyBuffer);
   if (status != DB_SUCCESS) {
      db_create_key(hDB, hKey, buffer_name, TID_KEY);
      status = db_find_key(hDB, hKey, buffer_name, &hKeyBuffer);
      if (status != DB_SUCCESS)
         return;
   }

   double buf_size = pheader->size;
   double buf_rptr = pheader->read_pointer;
   double buf_wptr = pheader->write_pointer;

   double buf_fill = 0;
   double buf_cptr = 0;
   double buf_cused = 0;
   double buf_cused_pct = 0;

   if (client_index >= 0 && client_index <= pheader->max_client_index) {
      buf_cptr = pheader->client[client_index].read_pointer;

      if (buf_wptr == buf_cptr) {
         buf_cused = 0;
      } else if (buf_wptr > buf_cptr) {
         buf_cused = buf_wptr - buf_cptr;
      } else {
         buf_cused = (buf_size - buf_cptr) + buf_wptr;
      }
      
      buf_cused_pct = buf_cused / buf_size * 100.0;
      
      // we cannot write buf_cused and buf_cused_pct into the buffer statistics
      // because some other GET_ALL client may have different buf_cused & etc,
      // so they must be written into the per-client statistics
      // and the web page should look at all the GET_ALL clients and used
      // the biggest buf_cused as the whole-buffer "bytes used" value.
   }

   if (buf_wptr == buf_rptr) {
      buf_fill = 0;
   } else if (buf_wptr > buf_rptr) {
      buf_fill = buf_wptr - buf_rptr;
   } else {
      buf_fill = (buf_size - buf_rptr) + buf_wptr;
   }

   double buf_fill_pct = buf_fill / buf_size * 100.0;

   db_set_value(hDB, hKeyBuffer, "Size", &buf_size, sizeof(double), 1, TID_DOUBLE);
   db_set_value(hDB, hKeyBuffer, "Write pointer", &buf_wptr, sizeof(double), 1, TID_DOUBLE);
   db_set_value(hDB, hKeyBuffer, "Read pointer", &buf_rptr, sizeof(double), 1, TID_DOUBLE);
   db_set_value(hDB, hKeyBuffer, "Filled", &buf_fill, sizeof(double), 1, TID_DOUBLE);
   db_set_value(hDB, hKeyBuffer, "Filled pct", &buf_fill_pct, sizeof(double), 1, TID_DOUBLE);

   status = db_find_key(hDB, hKeyBuffer, "Clients", &hKey);
   if (status != DB_SUCCESS) {
      db_create_key(hDB, hKeyBuffer, "Clients", TID_KEY);
      status = db_find_key(hDB, hKeyBuffer, "Clients", &hKey);
      if (status != DB_SUCCESS)
         return;
   }

   HNDLE hKeyClient;
   status = db_find_key(hDB, hKey, client_name, &hKeyClient);
   if (status != DB_SUCCESS) {
      db_create_key(hDB, hKey, client_name, TID_KEY);
      status = db_find_key(hDB, hKey, client_name, &hKeyClient);
      if (status != DB_SUCCESS)
         return;
   }

   db_set_value(hDB, hKeyClient, "count_lock", &pbuf->count_lock, sizeof(int), 1, TID_INT32);
   db_set_value(hDB, hKeyClient, "count_sent", &pbuf->count_sent, sizeof(int), 1, TID_INT32);
   db_set_value(hDB, hKeyClient, "bytes_sent", &pbuf->bytes_sent, sizeof(double), 1, TID_DOUBLE);
   db_set_value(hDB, hKeyClient, "count_write_wait", &pbuf->count_write_wait, sizeof(int), 1, TID_INT32);
   db_set_value(hDB, hKeyClient, "time_write_wait", &pbuf->time_write_wait, sizeof(DWORD), 1, TID_UINT32);
   db_set_value(hDB, hKeyClient, "max_bytes_write_wait", &pbuf->max_requested_space, sizeof(INT), 1, TID_INT32);
   db_set_value(hDB, hKeyClient, "count_read", &pbuf->count_read, sizeof(int), 1, TID_INT32);
   db_set_value(hDB, hKeyClient, "bytes_read", &pbuf->bytes_read, sizeof(double), 1, TID_DOUBLE);
   db_set_value(hDB, hKeyClient, "get_all_flag", &pbuf->get_all_flag, sizeof(BOOL), 1, TID_BOOL);
   db_set_value(hDB, hKeyClient, "read_pointer", &buf_cptr, sizeof(double), 1, TID_DOUBLE);
   db_set_value(hDB, hKeyClient, "bytes_used", &buf_cused, sizeof(double), 1, TID_DOUBLE);
   db_set_value(hDB, hKeyClient, "pct_used", &buf_cused_pct, sizeof(double), 1, TID_DOUBLE);

   for (int i = 0; i < MAX_CLIENTS; i++) {
      if (!pbuf->client_count_write_wait[i])
         continue;
      
      if (pheader->client[i].pid == 0)
         continue;
      
      if (pheader->client[i].name[0] == 0)
         continue;
      
      char str[100 + NAME_LENGTH];
      
      sprintf(str, "writes_blocked_by/%s/count_write_wait", pheader->client[i].name);
      db_set_value(hDB, hKeyClient, str, &pbuf->client_count_write_wait[i], sizeof(int), 1, TID_INT32);
      
      sprintf(str, "writes_blocked_by/%s/time_write_wait", pheader->client[i].name);
      db_set_value(hDB, hKeyClient, str, &pbuf->client_time_write_wait[i], sizeof(DWORD), 1, TID_UINT32);
   }

   db_set_value(hDB, hKeyBuffer, "Last updated", &now, sizeof(DWORD), 1, TID_UINT32);
   db_set_value(hDB, hKeyClient, "last_updated", &now, sizeof(DWORD), 1, TID_UINT32);
}

static void bm_write_buffer_statistics_to_odb(HNDLE hDB, BUFFER *pbuf, BOOL force)
{
   //printf("bm_buffer_write_statistics_to_odb: buffer [%s] client [%s], lock count %d -> %d, force %d\n", pbuf->buffer_name, pbuf->client_name, pbuf->last_count_lock, pbuf->count_lock, force);

   bm_lock_buffer_guard pbuf_guard(pbuf);

   if (!pbuf_guard.is_locked())
      return;

   if (!force) {
      if (pbuf->count_lock == pbuf->last_count_lock) {
         return;
      }
   }

   std::string buffer_name = pbuf->buffer_name;
   std::string client_name = pbuf->client_name;

   if ((strlen(buffer_name.c_str()) < 1) || (strlen(client_name.c_str()) < 1)) {
      // do not call cm_msg() while holding buffer lock, if we are SYSMSG, we will deadlock. K.O.
      pbuf_guard.unlock(); // unlock before cm_msg()
      cm_msg(MERROR, "bm_write_buffer_statistics_to_odb", "Invalid empty buffer name \"%s\" or client name \"%s\"", buffer_name.c_str(), client_name.c_str());
      return;
   }

   pbuf->last_count_lock = pbuf->count_lock;

   BUFFER_INFO xbuf(pbuf);
   BUFFER_HEADER xheader = *pbuf->buffer_header;
   int client_index = pbuf->client_index;

   pbuf_guard.unlock();

   bm_write_buffer_statistics_to_odb_copy(hDB, buffer_name.c_str(), client_name.c_str(), client_index, &xbuf, &xheader);
}

static BUFFER* bm_get_buffer(const char* who, int buffer_handle, int* pstatus)
{
   size_t sbuffer_handle = buffer_handle;

   size_t  nbuf = 0;
   BUFFER* pbuf = NULL;

   gBuffersMutex.lock();

   nbuf = gBuffers.size();
   if (buffer_handle >=1 && sbuffer_handle <= nbuf) {
      pbuf = gBuffers[buffer_handle-1];
   }

   gBuffersMutex.unlock();

   if (sbuffer_handle > nbuf || buffer_handle <= 0) {
      if (who)
         cm_msg(MERROR, who, "invalid buffer handle %d: out of range [1..%d]", buffer_handle, (int)nbuf);
      if (pstatus)
         *pstatus = BM_INVALID_HANDLE;
      return NULL;
   }
   
   if (!pbuf) {
      if (who)
         cm_msg(MERROR, who, "invalid buffer handle %d: empty slot", buffer_handle);
      if (pstatus)
         *pstatus = BM_INVALID_HANDLE;
      return NULL;
   }
   
   if (!pbuf->attached) {
      if (who)
         cm_msg(MERROR, who, "invalid buffer handle %d: not attached", buffer_handle);
      if (pstatus)
         *pstatus = BM_INVALID_HANDLE;
      return NULL;
   }
   
   if (pstatus)
      *pstatus = BM_SUCCESS;

   return pbuf;
}

#endif // LOCAL_ROUTINES

/********************************************************************/
/**
Open an event buffer.
Two default buffers are created by the system.
The "SYSTEM" buffer is used to
exchange events and the "SYSMSG" buffer is used to exchange system messages.
The name and size of the event buffers is defined in midas.h as
EVENT_BUFFER_NAME and DEFAULT_BUFFER_SIZE.
Following example opens the "SYSTEM" buffer, requests events with ID 1 and
enters a main loop. Events are then received in process_event()
\code
#include <stdio.h>
#include "midas.h"
void process_event(HNDLE hbuf, HNDLE request_id, EVENT_HEADER *pheader, void *pevent)
{
  printf("Received event #%d\r",
  pheader->serial_number);
}
main()
{
  INT status, request_id;
  HNDLE hbuf;
  status = cm_connect_experiment("pc810", "Sample", "Simple Analyzer", NULL);
  if (status != CM_SUCCESS)
  return 1;
  bm_open_buffer(EVENT_BUFFER_NAME, DEFAULT_BUFFER_SIZE, &hbuf);
  bm_request_event(hbuf, 1, TRIGGER_ALL, GET_ALL, request_id, process_event);

  do
  {
   status = cm_yield(1000);
  } while (status != RPC_SHUTDOWN && status != SS_ABORT);
  cm_disconnect_experiment();
  return 0;
}
\endcode
@param buffer_name Name of buffer
@param buffer_size Default size of buffer in bytes. Can by overwritten with ODB value
@param buffer_handle Buffer handle returned by function
@return BM_SUCCESS, BM_CREATED <br>
BM_NO_SHM Shared memory cannot be created <br>
BM_NO_SEMAPHORE Semaphore cannot be created <br>
BM_NO_MEMORY Not enough memory to create buffer descriptor <br>
BM_MEMSIZE_MISMATCH Buffer size conflicts with an existing buffer of
different size <br>
BM_INVALID_PARAM Invalid parameter
*/
INT bm_open_buffer(const char *buffer_name, INT buffer_size, INT *buffer_handle) {
   INT status;

   if (rpc_is_remote()) {
      status = rpc_call(RPC_BM_OPEN_BUFFER, buffer_name, buffer_size, buffer_handle);

      HNDLE hDB;
      status = cm_get_experiment_database(&hDB, NULL);
      if (status != SUCCESS || hDB == 0) {
         cm_msg(MERROR, "bm_open_buffer", "cannot open buffer \'%s\' - not connected to ODB", buffer_name);
         return BM_NO_SHM;
      }

      _bm_max_event_size = DEFAULT_MAX_EVENT_SIZE;

      int size = sizeof(INT);
      status = db_get_value(hDB, 0, "/Experiment/MAX_EVENT_SIZE", &_bm_max_event_size, &size, TID_UINT32, TRUE);

      if (status != DB_SUCCESS) {
         cm_msg(MERROR, "bm_open_buffer", "Cannot get ODB /Experiment/MAX_EVENT_SIZE, db_get_value() status %d",
                status);
         return status;
      }

      return status;
   }
#ifdef LOCAL_ROUTINES
   {
      HNDLE shm_handle;
      size_t shm_size;
      HNDLE hDB;
      const int max_buffer_size = 2 * 1000 * 1024 * 1024; // limited by 32-bit integers in the buffer header

      bm_cleanup("bm_open_buffer", ss_millitime(), FALSE);

      if (!buffer_name || !buffer_name[0]) {
         cm_msg(MERROR, "bm_open_buffer", "cannot open buffer with zero name");
         return BM_INVALID_PARAM;
      }

      if (strlen(buffer_name) >= NAME_LENGTH) {
         cm_msg(MERROR, "bm_open_buffer", "buffer name \"%s\" is longer than %d bytes", buffer_name, NAME_LENGTH);
         return BM_INVALID_PARAM;
      }

      status = cm_get_experiment_database(&hDB, NULL);

      if (status != SUCCESS || hDB == 0) {
         //cm_msg(MERROR, "bm_open_buffer", "cannot open buffer \'%s\' - not connected to ODB", buffer_name);
         return BM_NO_SHM;
      }

      /* get buffer size from ODB, user parameter as default if not present in ODB */
      std::string odb_path;
      odb_path += "/Experiment/Buffer sizes/";
      odb_path += buffer_name;

      int size = sizeof(INT);
      status = db_get_value(hDB, 0, odb_path.c_str(), &buffer_size, &size, TID_UINT32, TRUE);

      if (buffer_size <= 0 || buffer_size > max_buffer_size) {
         cm_msg(MERROR, "bm_open_buffer",
                "Cannot open buffer \"%s\", invalid buffer size %d in ODB \"%s\", maximum buffer size is %d",
                buffer_name, buffer_size, odb_path.c_str(), max_buffer_size);
         return BM_INVALID_PARAM;
      }

      _bm_max_event_size = DEFAULT_MAX_EVENT_SIZE;

      size = sizeof(INT);
      status = db_get_value(hDB, 0, "/Experiment/MAX_EVENT_SIZE", &_bm_max_event_size, &size, TID_UINT32, TRUE);

      if (status != DB_SUCCESS) {
         cm_msg(MERROR, "bm_open_buffer", "Cannot get ODB /Experiment/MAX_EVENT_SIZE, db_get_value() status %d",
                status);
         return status;
      }

      /* check if buffer already is open */
      gBuffersMutex.lock();
      for (size_t i = 0; i < gBuffers.size(); i++) {
         BUFFER* pbuf = gBuffers[i];
         if (pbuf && pbuf->attached && equal_ustring(pbuf->buffer_name, buffer_name)) {
            *buffer_handle = i + 1;
            gBuffersMutex.unlock();
            return BM_SUCCESS;
         }
      }
      gBuffersMutex.unlock();

      // only one thread at a time should create new buffers

      static std::mutex gNewBufferMutex;
      std::lock_guard<std::mutex> guard(gNewBufferMutex);

      // if we had a race against another thread
      // and while we were waiting for gNewBufferMutex
      // the other thread created this buffer, we return it.

      gBuffersMutex.lock();
      for (size_t i = 0; i < gBuffers.size(); i++) {
         BUFFER* pbuf = gBuffers[i];
         if (pbuf && pbuf->attached && equal_ustring(pbuf->buffer_name, buffer_name)) {
            *buffer_handle = i + 1;
            gBuffersMutex.unlock();
            return BM_SUCCESS;
         }
      }
      gBuffersMutex.unlock();

      /* allocate new BUFFER object */

      BUFFER* pbuf = new BUFFER;

      /* there is no constructor for BUFFER object, we have to zero the arrays manually */

      for (int i=0; i<MAX_CLIENTS; i++) {
         pbuf->client_count_write_wait[i] = 0;
         pbuf->client_time_write_wait[i] = 0;
      }

      /* create buffer semaphore */

      status = ss_semaphore_create(buffer_name, &(pbuf->semaphore));

      if (status != SS_CREATED && status != SS_SUCCESS) {
         *buffer_handle = 0;
         delete pbuf;
         return BM_NO_SEMAPHORE;
      }

      std::string client_name = cm_get_client_name();

      /* store client name */
      strlcpy(pbuf->client_name, client_name.c_str(), sizeof(pbuf->client_name));

      /* store buffer name */
      strlcpy(pbuf->buffer_name, buffer_name, sizeof(pbuf->buffer_name));

      /* lock buffer semaphore to avoid race with bm_open_buffer() in a different program */

      pbuf->attached = true; // required by bm_lock_buffer()

      bm_lock_buffer_guard pbuf_guard(pbuf);

      if (!pbuf_guard.is_locked()) {
         // cannot happen, no other thread can see this pbuf
         abort();
         return BM_NO_SEMAPHORE;
      }

      /* open shared memory */

      void *p = NULL;
      status = ss_shm_open(buffer_name, sizeof(BUFFER_HEADER) + buffer_size, &p, &shm_size, &shm_handle, FALSE);

      if (status != SS_SUCCESS && status != SS_CREATED) {
         *buffer_handle = 0;
         pbuf_guard.unlock();
         pbuf_guard.invalidate(); // destructor will see a deleted pbuf
         delete pbuf;
         return BM_NO_SHM;
      }

      pbuf->buffer_header = (BUFFER_HEADER *) p;

      BUFFER_HEADER *pheader = pbuf->buffer_header;

      bool shm_created = (status == SS_CREATED);

      if (shm_created) {
         /* initialize newly created shared memory */

         memset(pheader, 0, sizeof(BUFFER_HEADER) + buffer_size);

         strlcpy(pheader->name, buffer_name, sizeof(pheader->name));
         pheader->size = buffer_size;

      } else {
         /* validate existing shared memory */

         if (!equal_ustring(pheader->name, buffer_name)) {
            // unlock before calling cm_msg(). if we are SYSMSG, we wil ldeadlock. K.O.
            pbuf_guard.unlock();
            pbuf_guard.invalidate(); // destructor will see a deleted pbuf
            cm_msg(MERROR, "bm_open_buffer",
                   "Buffer \"%s\" is corrupted, mismatch of buffer name in shared memory \"%s\"", buffer_name,
                   pheader->name);
            *buffer_handle = 0;
            delete pbuf;
            return BM_CORRUPTED;
         }

         if ((pheader->num_clients < 0) || (pheader->num_clients > MAX_CLIENTS)) {
            // unlock before calling cm_msg(). if we are SYSMSG, we wil ldeadlock. K.O.
            pbuf_guard.unlock();
            pbuf_guard.invalidate(); // destructor will see a deleted pbuf
            cm_msg(MERROR, "bm_open_buffer", "Buffer \"%s\" is corrupted, num_clients %d exceeds MAX_CLIENTS %d",
                   buffer_name, pheader->num_clients, MAX_CLIENTS);
            *buffer_handle = 0;
            delete pbuf;
            return BM_CORRUPTED;
         }

         if ((pheader->max_client_index < 0) || (pheader->max_client_index > MAX_CLIENTS)) {
            // unlock before calling cm_msg(). if we are SYSMSG, we wil ldeadlock. K.O.
            pbuf_guard.unlock();
            pbuf_guard.invalidate(); // destructor will see a deleted pbuf
            cm_msg(MERROR, "bm_open_buffer", "Buffer \"%s\" is corrupted, max_client_index %d exceeds MAX_CLIENTS %d",
                   buffer_name, pheader->max_client_index, MAX_CLIENTS);
            *buffer_handle = 0;
            delete pbuf;
            return BM_CORRUPTED;
         }

         /* check if buffer size is identical */
         if (pheader->size != buffer_size) {
            cm_msg(MINFO, "bm_open_buffer", "Buffer \"%s\" requested size %d differs from existing size %d",
                   buffer_name, buffer_size, pheader->size);

            buffer_size = pheader->size;

            ss_shm_close(buffer_name, p, shm_size, shm_handle, FALSE);

            status = ss_shm_open(buffer_name, sizeof(BUFFER_HEADER) + buffer_size, &p, &shm_size, &shm_handle, FALSE);

            if (status != SS_SUCCESS) {
               *buffer_handle = 0;
               pbuf_guard.unlock();
               pbuf_guard.invalidate(); // destructor will see a deleted pbuf
               delete pbuf;
               return BM_NO_SHM;
            }

            pbuf->buffer_header = (BUFFER_HEADER *) p;
            pheader = pbuf->buffer_header;
         }
      }

      /* shared memory is good from here down */

      pbuf->attached = true;

      pbuf->shm_handle = shm_handle;
      pbuf->shm_size = shm_size;
      pbuf->callback = FALSE;

      bm_cleanup_buffer_locked(pbuf, "bm_open_buffer", ss_millitime());

      status = bm_validate_buffer_locked(pbuf);
      if (status != BM_SUCCESS) {
         cm_msg(MERROR, "bm_open_buffer",
                "buffer \'%s\' is corrupted, bm_validate_buffer() status %d, calling bm_reset_buffer()...", buffer_name,
                status);
         bm_reset_buffer_locked(pbuf);
         cm_msg(MINFO, "bm_open_buffer", "buffer \'%s\' was reset, all buffered events were lost", buffer_name);
      }

      /* add our client BUFFER_HEADER */

      int iclient = 0;
      for (; iclient < MAX_CLIENTS; iclient++)
         if (pheader->client[iclient].pid == 0)
            break;

      if (iclient == MAX_CLIENTS) {
         *buffer_handle = 0;
         // unlock before calling cm_msg(). if we are SYSMSG, we wil ldeadlock. K.O.
         pbuf_guard.unlock();
         pbuf_guard.invalidate(); // destructor will see a deleted pbuf
         delete pbuf;
         cm_msg(MERROR, "bm_open_buffer", "buffer \'%s\' maximum number of clients %d exceeded", buffer_name, MAX_CLIENTS);
         return BM_NO_SLOT;
      }

      /* store slot index in _buffer structure */
      pbuf->client_index = iclient;

      /*
         Save the index of the last client of that buffer so that later only
         the clients 0..max_client_index-1 have to be searched through.
       */
      pheader->num_clients++;
      if (iclient + 1 > pheader->max_client_index)
         pheader->max_client_index = iclient + 1;

      /* setup buffer header and client structure */
      BUFFER_CLIENT *pclient = &pheader->client[iclient];

      memset(pclient, 0, sizeof(BUFFER_CLIENT));

      strlcpy(pclient->name, client_name.c_str(), sizeof(pclient->name));

      pclient->pid = ss_getpid();

      ss_suspend_get_buffer_port(ss_gettid(), &pclient->port);

      pclient->read_pointer = pheader->write_pointer;
      pclient->last_activity = ss_millitime();

      cm_get_watchdog_params(NULL, &pclient->watchdog_timeout);

      pbuf_guard.unlock();

      /* shared memory is not locked from here down, do not touch pheader and pbuf->buffer_header! */

      pheader = NULL;

      /* we are not holding any locks from here down, but other threads cannot see this pbuf yet */

      bm_clear_buffer_statistics(hDB, pbuf);
      bm_write_buffer_statistics_to_odb(hDB, pbuf, true);

      /* add pbuf to buffer list */

      gBuffersMutex.lock();

      bool added = false;
      for (size_t i=0; i<gBuffers.size(); i++) {
         if (gBuffers[i] == NULL) {
            gBuffers[i] = pbuf;
            added = true;
            *buffer_handle = i+1;
            break;
         }
      }
      if (!added) {
         *buffer_handle = gBuffers.size() + 1;
         gBuffers.push_back(pbuf);
      }

      /* from here down we should not touch pbuf without locking it */

      pbuf = NULL;

      gBuffersMutex.unlock();

      /* new buffer is now ready for use */

      /* initialize buffer counters */
      bm_init_buffer_counters(*buffer_handle);

      bm_cleanup("bm_open_buffer", ss_millitime(), FALSE);

      if (shm_created)
         return BM_CREATED;
   }
#endif                          /* LOCAL_ROUTINES */

   return BM_SUCCESS;
}

/********************************************************************/
/**
If buffer is already open, return it's handle
@param buffer_name buffer name
@return BM_SUCCESS, BM_NOT_FOUND
*/
INT bm_get_buffer_handle(const char* buffer_name, INT *buffer_handle)
{
   gBuffersMutex.lock();
   for (size_t i = 0; i < gBuffers.size(); i++) {
      BUFFER* pbuf = gBuffers[i];
      if (pbuf && pbuf->attached && equal_ustring(pbuf->buffer_name, buffer_name)) {
         *buffer_handle = i + 1;
         gBuffersMutex.unlock();
         return BM_SUCCESS;
      }
   }
   gBuffersMutex.unlock();
   return BM_NOT_FOUND;
}

/********************************************************************/
/**
Closes an event buffer previously opened with bm_open_buffer().
@param buffer_handle buffer handle
@return BM_SUCCESS, BM_INVALID_HANDLE
*/
INT bm_close_buffer(INT buffer_handle) {
   //printf("bm_close_buffer: handle %d\n", buffer_handle);

   if (rpc_is_remote())
      return rpc_call(RPC_BM_CLOSE_BUFFER, buffer_handle);

#ifdef LOCAL_ROUTINES
   {
      int status = 0;

      BUFFER *pbuf = bm_get_buffer(NULL, buffer_handle, &status);

      if (!pbuf)
         return status;

      //printf("bm_close_buffer: handle %d, name [%s]\n", buffer_handle, pheader->name);

      int i;

      /* delete all requests for this buffer */
      for (i = 0; i < _request_list_entries; i++)
         if (_request_list[i].buffer_handle == buffer_handle)
            bm_delete_request(i);

      HNDLE hDB;
      cm_get_experiment_database(&hDB, NULL);

      if (hDB) {
         /* write statistics to odb */
         bm_write_buffer_statistics_to_odb(hDB, pbuf, TRUE);
      }

      /* lock buffer in correct order */

      status = bm_lock_buffer_read_cache(pbuf);

      if (status != BM_SUCCESS) {
         return status;
      }

      status = bm_lock_buffer_write_cache(pbuf);

      if (status != BM_SUCCESS) {
         pbuf->read_cache_mutex.unlock();
         return status;
      }

      bm_lock_buffer_guard pbuf_guard(pbuf);

      if (!pbuf_guard.is_locked()) {
         pbuf->write_cache_mutex.unlock();
         pbuf->read_cache_mutex.unlock();
         return pbuf_guard.get_status();
      }

      BUFFER_HEADER *pheader = pbuf->buffer_header;

      /* mark entry in _buffer as empty */
      pbuf->attached = false;

      BUFFER_CLIENT* pclient = bm_get_my_client(pbuf, pheader);

      if (pclient) {
         /* clear entry from client structure in buffer header */
         memset(pclient, 0, sizeof(BUFFER_CLIENT));
      }

      /* calculate new max_client_index entry */
      for (i = MAX_CLIENTS - 1; i >= 0; i--)
         if (pheader->client[i].pid != 0)
            break;
      pheader->max_client_index = i + 1;

      /* count new number of clients */
      int j = 0;
      for (i = MAX_CLIENTS - 1; i >= 0; i--)
         if (pheader->client[i].pid != 0)
            j++;
      pheader->num_clients = j;

      int destroy_flag = (pheader->num_clients == 0);

      // we hold the locks on the read cache and the write cache.

      /* free cache */
      if (pbuf->read_cache_size > 0) {
         free(pbuf->read_cache);
         pbuf->read_cache = NULL;
         pbuf->read_cache_size = 0;
         pbuf->read_cache_rp = 0;
         pbuf->read_cache_wp = 0;
      }

      if (pbuf->write_cache_size > 0) {
         free(pbuf->write_cache);
         pbuf->write_cache = NULL;
         pbuf->write_cache_size = 0;
         pbuf->write_cache_rp = 0;
         pbuf->write_cache_wp = 0;
      }

      /* check if anyone is waiting and wake him up */

      for (int i = 0; i < pheader->max_client_index; i++) {
         BUFFER_CLIENT *pclient = pheader->client + i;
         if (pclient->pid && (pclient->write_wait || pclient->read_wait))
            ss_resume(pclient->port, "B  ");
      }

      /* unmap shared memory, delete it if we are the last */

      ss_shm_close(pbuf->buffer_name, pbuf->buffer_header, pbuf->shm_size, pbuf->shm_handle, destroy_flag);

      /* after ss_shm_close() these are invalid: */

      pheader = NULL;
      pbuf->buffer_header = NULL;
      pbuf->shm_size = 0;
      pbuf->shm_handle = 0;

      /* unlock buffer in correct order */

      pbuf_guard.unlock();

      pbuf->write_cache_mutex.unlock();
      pbuf->read_cache_mutex.unlock();

      /* delete semaphore */

      ss_semaphore_delete(pbuf->semaphore, destroy_flag);
   }
#endif                          /* LOCAL_ROUTINES */

   return BM_SUCCESS;
}

/********************************************************************/
/**
Close all open buffers
@return BM_SUCCESS
*/
INT bm_close_all_buffers(void) {
   if (rpc_is_remote())
      return rpc_call(RPC_BM_CLOSE_ALL_BUFFERS);

#ifdef LOCAL_ROUTINES
   {
      cm_msg_close_buffer();

      gBuffersMutex.lock();
      size_t nbuf = gBuffers.size();
      gBuffersMutex.unlock();

      for (size_t i = nbuf; i > 0; i--) {
         bm_close_buffer(i);
      }

      gBuffersMutex.lock();
      for (size_t i=0; i< gBuffers.size(); i++) {
         BUFFER* pbuf = gBuffers[i];
         if (!pbuf)
            continue;
         delete pbuf;
         pbuf = NULL;
         gBuffers[i] = NULL;
      }
      gBuffersMutex.unlock();
   }
#endif                          /* LOCAL_ROUTINES */

   return BM_SUCCESS;
}

/********************************************************************/
/**
Close all open buffers
@return BM_SUCCESS
*/
INT bm_write_statistics_to_odb(void) {
#ifdef LOCAL_ROUTINES
   {
      int status;
      HNDLE hDB;

      status = cm_get_experiment_database(&hDB, NULL);

      if (status != CM_SUCCESS) {
         //printf("bm_write_statistics_to_odb: cannot get ODB handle!\n");
         return BM_SUCCESS;
      }

      std::vector<BUFFER*> mybuffers;
      
      gBuffersMutex.lock();
      mybuffers = gBuffers;
      gBuffersMutex.unlock();

      for (BUFFER* pbuf : mybuffers) {
         if (!pbuf || !pbuf->attached)
            continue;
         bm_write_buffer_statistics_to_odb(hDB, pbuf, FALSE);
      }
   }
#endif /* LOCAL_ROUTINES */

   return BM_SUCCESS;
}

/**dox***************************************************************/
/** @} *//* end of bmfunctionc */

/**dox***************************************************************/
/** @addtogroup cmfunctionc
 *
 *  @{  */

/*-- Watchdog routines ---------------------------------------------*/
#ifdef LOCAL_ROUTINES

static std::atomic<bool> _watchdog_thread_run{false}; // set by main thread
static std::atomic<bool> _watchdog_thread_is_running{false}; // set by watchdog thread
static std::atomic<std::thread*> _watchdog_thread{NULL};

/********************************************************************/
/**
Watchdog thread to maintain the watchdog timeout timestamps for this client
*/
INT cm_watchdog_thread(void *unused) {
   _watchdog_thread_is_running = true;
   //printf("cm_watchdog_thread started!\n");
   while (_watchdog_thread_run) {
      //printf("cm_watchdog_thread runs!\n");
      DWORD now = ss_millitime();
      bm_update_last_activity(now);
      db_update_last_activity(now);
      int i;
      for (i = 0; i < 20; i++) {
         ss_sleep(100);
         if (!_watchdog_thread_run)
            break;
      }
   }
   //printf("cm_watchdog_thread stopped!\n");
   _watchdog_thread_is_running = false;
   return 0;
}

static void xcm_watchdog_thread() {
   cm_watchdog_thread(NULL);
}

#endif

INT cm_start_watchdog_thread() {
   /* watchdog does not run inside remote clients.
    * watchdog timeout timers are maintained by the mserver */
   if (rpc_is_remote())
      return CM_SUCCESS;
#ifdef LOCAL_ROUTINES
   /* only start once */
   if (_watchdog_thread)
      return CM_SUCCESS;
   _watchdog_thread_run = true;
   _watchdog_thread.store(new std::thread(xcm_watchdog_thread));
#endif
   return CM_SUCCESS;
}

INT cm_stop_watchdog_thread() {
   /* watchdog does not run inside remote clients.
    * watchdog timeout timers are maintained by the mserver */
   if (rpc_is_remote())
      return CM_SUCCESS;
#ifdef LOCAL_ROUTINES
   _watchdog_thread_run = false;
   while (_watchdog_thread_is_running) {
      //printf("waiting for watchdog thread to shut down\n");
      ss_sleep(10);
   }
   if (_watchdog_thread != NULL) {
      _watchdog_thread.load()->join();
      delete static_cast<std::thread *>(_watchdog_thread);
      _watchdog_thread = NULL;
   }
#endif
   return CM_SUCCESS;
}

/********************************************************************/
/**
Shutdown (exit) other MIDAS client
@param name           Client name or "all" for all clients
@param bUnique        If true, look for the exact client name.
                      If false, look for namexxx where xxx is
                      a any number.

@return CM_SUCCESS, CM_NO_CLIENT, DB_NO_KEY
*/
INT cm_shutdown(const char *name, BOOL bUnique) {
   INT status, return_status, i, size;
   HNDLE hDB, hKeyClient, hKey, hSubkey, hKeyTmp, hConn;
   KEY key;
   char client_name[NAME_LENGTH], remote_host[HOST_NAME_LENGTH];
   INT port;
   DWORD start_time;
   DWORD timeout;
   DWORD last;

   cm_get_experiment_database(&hDB, &hKeyClient);

   status = db_find_key(hDB, 0, "System/Clients", &hKey);
   if (status != DB_SUCCESS)
      return DB_NO_KEY;

   return_status = CM_NO_CLIENT;

   /* loop over all clients */
   for (i = 0;; i++) {
      status = db_enum_key(hDB, hKey, i, &hSubkey);
      if (status == DB_NO_MORE_SUBKEYS)
         break;

      /* don't shutdown ourselves */
      if (hSubkey == hKeyClient)
         continue;

      if (status == DB_SUCCESS) {
         db_get_key(hDB, hSubkey, &key);

         /* contact client */
         size = sizeof(client_name);
         status = db_get_value(hDB, hSubkey, "Name", client_name, &size, TID_STRING, FALSE);
         if (status != DB_SUCCESS)
            continue;

         if (!bUnique)
            client_name[strlen(name)] = 0;      /* strip number */

         /* check if individual client */
         if (!equal_ustring("all", name) && !equal_ustring(client_name, name))
            continue;

         size = sizeof(port);
         db_get_value(hDB, hSubkey, "Server Port", &port, &size, TID_INT32, TRUE);

         size = sizeof(remote_host);
         db_get_value(hDB, hSubkey, "Host", remote_host, &size, TID_STRING, TRUE);

         cm_get_watchdog_info(hDB, name, &timeout, &last);
         if (timeout == 0)
            timeout = 5000;

         /* client found -> connect to its server port */
         status = rpc_client_connect(remote_host, port, client_name, &hConn);
         if (status != RPC_SUCCESS) {
            int client_pid = atoi(key.name);
            return_status = CM_NO_CLIENT;
            cm_msg(MERROR, "cm_shutdown", "Cannot connect to client \'%s\' on host \'%s\', port %d",
                   client_name, remote_host, port);
#ifdef SIGKILL
            cm_msg(MERROR, "cm_shutdown", "Killing and Deleting client \'%s\' pid %d", client_name,
                   client_pid);
            kill(client_pid, SIGKILL);
            return_status = CM_SUCCESS;
            status = cm_delete_client_info(hDB, client_pid);
            if (status != CM_SUCCESS)
               cm_msg(MERROR, "cm_shutdown", "Cannot delete client info for client \'%s\', pid %d, status %d",
                      name, client_pid, status);
#endif
         } else {
            /* call disconnect with shutdown=TRUE */
            rpc_client_disconnect(hConn, TRUE);

            /* wait until client has shut down */
            start_time = ss_millitime();
            do {
               ss_sleep(100);
               status = db_find_key(hDB, hKey, key.name, &hKeyTmp);
            } while (status == DB_SUCCESS && (ss_millitime() - start_time < timeout));

            if (status == DB_SUCCESS) {
               int client_pid = atoi(key.name);
               return_status = CM_NO_CLIENT;
               cm_msg(MERROR, "cm_shutdown", "Client \'%s\' not responding to shutdown command", client_name);
#ifdef SIGKILL
               cm_msg(MERROR, "cm_shutdown", "Killing and Deleting client \'%s\' pid %d", client_name,
                      client_pid);
               kill(client_pid, SIGKILL);
               status = cm_delete_client_info(hDB, client_pid);
               if (status != CM_SUCCESS)
                  cm_msg(MERROR, "cm_shutdown",
                         "Cannot delete client info for client \'%s\', pid %d, status %d", name, client_pid,
                         status);
#endif
               return_status = CM_NO_CLIENT;
            } else {
               return_status = CM_SUCCESS;
               i--;
            }
         }
      }

      /* display any message created during each shutdown */
      cm_msg_flush_buffer();
   }

   return return_status;
}

/********************************************************************/
/**
Check if a MIDAS client exists in current experiment
@param    name            Client name
@param    bUnique         If true, look for the exact client name.
                          If false, look for namexxx where xxx is
                          a any number
@return   CM_SUCCESS, CM_NO_CLIENT
*/
INT cm_exist(const char *name, BOOL bUnique) {
   INT status, i, size;
   HNDLE hDB, hKeyClient, hKey, hSubkey;
   char client_name[NAME_LENGTH];

   if (rpc_is_remote())
      return rpc_call(RPC_CM_EXIST, name, bUnique);

   cm_get_experiment_database(&hDB, &hKeyClient);

   status = db_find_key(hDB, 0, "System/Clients", &hKey);
   if (status != DB_SUCCESS)
      return DB_NO_KEY;

   db_lock_database(hDB);

   /* loop over all clients */
   for (i = 0;; i++) {
      status = db_enum_key(hDB, hKey, i, &hSubkey);
      if (status == DB_NO_MORE_SUBKEYS)
         break;

      if (hSubkey == hKeyClient)
         continue;

      if (status == DB_SUCCESS) {
         /* get client name */
         size = sizeof(client_name);
         status = db_get_value(hDB, hSubkey, "Name", client_name, &size, TID_STRING, FALSE);

         if (status != DB_SUCCESS) {
            //fprintf(stderr, "cm_exist: name %s, i=%d, hSubkey=%d, status %d, client_name %s, my name %s\n", name, i, hSubkey, status, client_name, _client_name);
            continue;
         }

         if (equal_ustring(client_name, name)) {
            db_unlock_database(hDB);
            return CM_SUCCESS;
         }

         if (!bUnique) {
            client_name[strlen(name)] = 0;      /* strip number */
            if (equal_ustring(client_name, name)) {
               db_unlock_database(hDB);
               return CM_SUCCESS;
            }
         }
      }
   }

   db_unlock_database(hDB);

   return CM_NO_CLIENT;
}

/********************************************************************/
/**
Remove hanging clients independent of their watchdog
           timeout.

Since this function does not obey the client watchdog
timeout, it should be only called to remove clients which
have their watchdog checking turned off or which are
known to be dead. The normal client removement is done
via cm_watchdog().

Currently (Sept. 02) there are two applications for that:
-# The ODBEdit command "cleanup", which can be used to
remove clients which have their watchdog checking off,
like the analyzer started with the "-d" flag for a
debugging session.
-# The frontend init code to remove previous frontends.
This can be helpful if a frontend dies. Normally,
one would have to wait 60 sec. for a crashed frontend
to be removed. Only then one can start again the
frontend. Since the frontend init code contains a
call to cm_cleanup(<frontend_name>), one can restart
a frontend immediately.

Added ignore_timeout on Nov.03. A logger might have an
increased tiemout of up to 60 sec. because of tape
operations. If ignore_timeout is FALSE, the logger is
then not killed if its inactivity is less than 60 sec.,
while in the previous implementation it was always
killed after 2*WATCHDOG_INTERVAL.
@param    client_name      Client name, if zero check all clients
@param    ignore_timeout   If TRUE, ignore a possible increased
                           timeout defined by each client.
@return   CM_SUCCESS
*/
INT cm_cleanup(const char *client_name, BOOL ignore_timeout) {
   if (rpc_is_remote())
      return rpc_call(RPC_CM_CLEANUP, client_name);

#ifdef LOCAL_ROUTINES
   {
      DWORD interval;
      DWORD now = ss_millitime();

      std::vector<BUFFER*> mybuffers;
      
      gBuffersMutex.lock();
      mybuffers = gBuffers;
      gBuffersMutex.unlock();

      /* check buffers */
      for (BUFFER* pbuf : mybuffers) {
         if (!pbuf)
            continue;
         if (pbuf->attached) {
            std::string msg;

            bm_lock_buffer_guard pbuf_guard(pbuf);

            if (!pbuf_guard.is_locked())
               continue;

            /* update the last_activity entry to show that we are alive */
            BUFFER_HEADER *pheader = pbuf->buffer_header;
            BUFFER_CLIENT *pbclient = pheader->client;
            int idx = bm_validate_client_index(pbuf, FALSE);
            if (idx >= 0)
               pbclient[idx].last_activity = ss_millitime();

            /* now check other clients */
            for (int j = 0; j < pheader->max_client_index; j++, pbclient++) {
               if (j != pbuf->client_index && pbclient->pid &&
                   (client_name == NULL || client_name[0] == 0
                    || strncmp(pbclient->name, client_name, strlen(client_name)) == 0)) {
                  if (ignore_timeout)
                     interval = 2 * WATCHDOG_INTERVAL;
                  else
                     interval = pbclient->watchdog_timeout;

                  /* If client process has no activity, clear its buffer entry. */
                  if (interval > 0
                      && now > pbclient->last_activity && now - pbclient->last_activity > interval) {

                     /* now make again the check with the buffer locked */
                     if (interval > 0
                         && now > pbclient->last_activity && now - pbclient->last_activity > interval) {
                        msg = msprintf(
                                "Client \'%s\' on \'%s\' removed by cm_cleanup (idle %1.1lfs, timeout %1.0lfs)",
                                pbclient->name, pheader->name,
                                (ss_millitime() - pbclient->last_activity) / 1000.0,
                                interval / 1000.0);

                        bm_remove_client_locked(pheader, j);
                     }

                     /* go again through whole list */
                     j = 0;
                  }
               }
            }

            // unlock buffer before calling cm_msg(), if we are SYSMSG, we will deadlock.
            pbuf_guard.unlock();

            /* display info message after unlocking buffer */
            if (!msg.empty())
               cm_msg(MINFO, "cm_cleanup", "%s", msg.c_str());
         }
      }

      db_cleanup2(client_name, ignore_timeout, now, "cm_cleanup");
   }
#endif                          /* LOCAL_ROUTINES */

   return CM_SUCCESS;
}

/********************************************************************/
/**
Expand environment variables in filesystem file path names

Examples of expansion: $FOO=foo, $BAR=bar, $UNDEF is undefined (undefined, not empty)

   ok &= test_cm_expand_env1("aaa", "aaa");
   ok &= test_cm_expand_env1("$FOO", "foo");
   ok &= test_cm_expand_env1("/$FOO", "/foo");
   ok &= test_cm_expand_env1("/$FOO/", "/foo/");
   ok &= test_cm_expand_env1("$FOO/$BAR", "foo/bar");
   ok &= test_cm_expand_env1("$FOO1", "$FOO1");
   ok &= test_cm_expand_env1("1$FOO", "1foo");
   ok &= test_cm_expand_env1("$UNDEF", "$UNDEF");
   ok &= test_cm_expand_env1("/$UNDEF/", "/$UNDEF/");

@param    str     Input file path
@return   expanded file path
*/
std::string cm_expand_env(const char *str) {
   const char *s = str;
   std::string r;
   for (; *s;) {
      if (*s == '$') {
         s++;
         std::string envname;
         for (; *s;) {
            if (*s == DIR_SEPARATOR)
               break;
            envname += *s;
            s++;
         }
         const char *e = getenv(envname.c_str());
         //printf("expanding [%s] at [%s] envname [%s] value [%s]\n", filename, s, envname.c_str(), e);
         if (!e) {
            //cm_msg(MERROR, "expand_env", "Env.variable \"%s\" cannot be expanded in \"%s\"", envname.c_str(), filename);
            r += '$';
            r += envname;
         } else {
            r += e;
            //if (r[r.length()-1] != DIR_SEPARATOR)
            //r += DIR_SEPARATOR_STR;
         }
      } else {
         r += *s;
         s++;
      }
   }
   return r;
}

static bool test_cm_expand_env1(const char *str, const char *expected) {
   std::string s = cm_expand_env(str);
   printf("test_expand_env: [%s] -> [%s] expected [%s]",
          str,
          s.c_str(),
          expected);
   if (s != expected) {
      printf(", MISMATCH!\n");
      return false;
   }

   printf("\n");
   return true;
}

void cm_test_expand_env() {
   printf("Test expand_end()\n");
   setenv("FOO", "foo", 1);
   setenv("BAR", "bar", 1);
   setenv("EMPTY", "", 1);
   unsetenv("UNDEF");

   bool ok = true;

   ok &= test_cm_expand_env1("aaa", "aaa");
   ok &= test_cm_expand_env1("$FOO", "foo");
   ok &= test_cm_expand_env1("/$FOO", "/foo");
   ok &= test_cm_expand_env1("/$FOO/", "/foo/");
   ok &= test_cm_expand_env1("$FOO/$BAR", "foo/bar");
   ok &= test_cm_expand_env1("$FOO1", "$FOO1");
   ok &= test_cm_expand_env1("1$FOO", "1foo");
   ok &= test_cm_expand_env1("$UNDEF", "$UNDEF");
   ok &= test_cm_expand_env1("/$UNDEF/", "/$UNDEF/");

   if (ok) {
      printf("test_expand_env: all tests passed!\n");
   } else {
      printf("test_expand_env: test FAILED!\n");
   }
}

/**dox***************************************************************/

/** @} *//* end of cmfunctionc */

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/********************************************************************/
INT bm_get_buffer_info(INT buffer_handle, BUFFER_HEADER *buffer_header)
/********************************************************************\

  Routine: bm_buffer_info

  Purpose: Copies the current buffer header referenced by buffer_handle
           into the *buffer_header structure which must be supplied
           by the calling routine.

  Input:
    INT buffer_handle       Handle of the buffer to get the header from

  Output:
    BUFFER_HEADER *buffer_header   Destination address which gets a copy
                                   of the buffer header structure.

  Function value:
    BM_SUCCESS              Successful completion
    BM_INVALID_HANDLE       Buffer handle is invalid
    RPC_NET_ERROR           Network error

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_BM_GET_BUFFER_INFO, buffer_handle, buffer_header);

#ifdef LOCAL_ROUTINES

   int status = 0;
   BUFFER *pbuf = bm_get_buffer("bm_get_buffer_info", buffer_handle, &status);

   if (!pbuf)
      return status;

   bm_lock_buffer_guard pbuf_guard(pbuf);

   if (!pbuf_guard.is_locked())
      return pbuf_guard.get_status();

   memcpy(buffer_header, pbuf->buffer_header, sizeof(BUFFER_HEADER));

#endif                          /* LOCAL_ROUTINES */

   return BM_SUCCESS;
}

/********************************************************************/
INT bm_get_buffer_level(INT buffer_handle, INT *n_bytes)
/********************************************************************\

  Routine: bm_get_buffer_level

  Purpose: Return number of bytes in buffer or in cache

  Input:
    INT buffer_handle       Handle of the buffer to get the info

  Output:
    INT *n_bytes              Number of bytes in buffer

  Function value:
    BM_SUCCESS              Successful completion
    BM_INVALID_HANDLE       Buffer handle is invalid
    RPC_NET_ERROR           Network error

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_BM_GET_BUFFER_LEVEL, buffer_handle, n_bytes);

#ifdef LOCAL_ROUTINES
   {
      int status = 0;

      BUFFER *pbuf = bm_get_buffer("bm_get_buffer_level", buffer_handle, &status);

      if (!pbuf)
         return status;

      bm_lock_buffer_guard pbuf_guard(pbuf);

      if (!pbuf_guard.is_locked())
         return pbuf_guard.get_status();

      BUFFER_HEADER *pheader = pbuf->buffer_header;

      BUFFER_CLIENT *pclient = bm_get_my_client(pbuf, pheader);

      *n_bytes = pheader->write_pointer - pclient->read_pointer;
      if (*n_bytes < 0)
         *n_bytes += pheader->size;

      pbuf_guard.unlock();

      if (pbuf->read_cache_size) {
         status = bm_lock_buffer_read_cache(pbuf);
         if (status == BM_SUCCESS) {
            /* add bytes in cache */
            if (pbuf->read_cache_wp > pbuf->read_cache_rp)
               *n_bytes += pbuf->read_cache_wp - pbuf->read_cache_rp;
            pbuf->read_cache_mutex.unlock();
         }
      }
   }
#endif                          /* LOCAL_ROUTINES */

   return BM_SUCCESS;
}


#ifdef LOCAL_ROUTINES

/********************************************************************/
static int bm_lock_buffer_read_cache(BUFFER *pbuf)
{
   bool locked = ss_timed_mutex_wait_for_sec(pbuf->read_cache_mutex, "buffer read cache", _bm_mutex_timeout_sec);

   if (!locked) {
      fprintf(stderr, "bm_lock_buffer_read_cache: Error: Cannot lock read cache of buffer \"%s\", ss_timed_mutex_wait_for_sec() timeout, aborting...\n", pbuf->buffer_name);
      cm_msg(MERROR, "bm_lock_buffer_read_cache", "Cannot lock read cache of buffer \"%s\", ss_timed_mutex_wait_for_sec() timeout, aborting...", pbuf->buffer_name);
      abort();
      /* DOES NOT RETURN */
   }

   if (!pbuf->attached) {
      pbuf->read_cache_mutex.unlock();
      fprintf(stderr, "bm_lock_buffer_read_cache: Error: Cannot lock read cache of buffer \"%s\", buffer was closed while we waited for the buffer_mutex\n", pbuf->buffer_name);
      return BM_INVALID_HANDLE;
   }

   return BM_SUCCESS;
}

/********************************************************************/
static int bm_lock_buffer_write_cache(BUFFER *pbuf)
{
   bool locked = ss_timed_mutex_wait_for_sec(pbuf->write_cache_mutex, "buffer write cache", _bm_mutex_timeout_sec);

   if (!locked) {
      fprintf(stderr, "bm_lock_buffer_write_cache: Error: Cannot lock write cache of buffer \"%s\", ss_timed_mutex_wait_for_sec() timeout, aborting...\n", pbuf->buffer_name);
      cm_msg(MERROR, "bm_lock_buffer_write_cache", "Cannot lock write cache of buffer \"%s\", ss_timed_mutex_wait_for_sec() timeout, aborting...", pbuf->buffer_name);
      abort();
      /* DOES NOT RETURN */
   }

   if (!pbuf->attached) {
      pbuf->write_cache_mutex.unlock();
      fprintf(stderr, "bm_lock_buffer_write_cache: Error: Cannot lock write cache of buffer \"%s\", buffer was closed while we waited for the buffer_mutex\n", pbuf->buffer_name);
      return BM_INVALID_HANDLE;
   }

   return BM_SUCCESS;
}

/********************************************************************/
static int bm_lock_buffer_mutex(BUFFER *pbuf)
{
   //printf("bm_lock_buffer_mutex %s!\n", pbuf->buffer_name);

   bool locked = ss_timed_mutex_wait_for_sec(pbuf->buffer_mutex, "buffer mutex", _bm_mutex_timeout_sec);

   if (!locked) {
      fprintf(stderr, "bm_lock_buffer_mutex: Error: Cannot lock buffer \"%s\", ss_timed_mutex_wait_for_sec() timeout, aborting...\n", pbuf->buffer_name);
      cm_msg(MERROR, "bm_lock_buffer_mutex", "Cannot lock buffer \"%s\", ss_timed_mutex_wait_for_sec() timeout, aborting...", pbuf->buffer_name);
      abort();
      /* DOES NOT RETURN */
   }

   if (!pbuf->attached) {
      pbuf->buffer_mutex.unlock();
      fprintf(stderr, "bm_lock_buffer_mutex: Error: Cannot lock buffer \"%s\", buffer was closed while we waited for the buffer_mutex\n", pbuf->buffer_name);
      return BM_INVALID_HANDLE;
   }

   //static int counter = 0;
   //counter++;
   //printf("locked %d!\n", counter);
   //if (counter > 50)
   //   ::sleep(3);

   return BM_SUCCESS;
}

/********************************************************************/
static int xbm_lock_buffer(BUFFER *pbuf)
{
   int status;

   // NB: locking order: 1st buffer mutex, 2nd buffer semaphore. Unlock in reverse order.

   //if (pbuf->locked) {
   //   fprintf(stderr, "double lock, abort!\n");
   //   abort();
   //}

   status = bm_lock_buffer_mutex(pbuf);

   if (status != BM_SUCCESS)
      return status;

   status = ss_semaphore_wait_for(pbuf->semaphore, 1000);
 
   if (status != SS_SUCCESS) {
      fprintf(stderr, "bm_lock_buffer: Lock buffer \"%s\" is taking longer than 1 second!\n", pbuf->buffer_name);

      status = ss_semaphore_wait_for(pbuf->semaphore, 10000);

      if (status != SS_SUCCESS) {
         fprintf(stderr, "bm_lock_buffer: Lock buffer \"%s\" is taking longer than 10 seconds, buffer semaphore is probably stuck, delete %s.SHM and try again!\n", pbuf->buffer_name, pbuf->buffer_name);

         if (pbuf->buffer_header) {
            for (int i=0; i<MAX_CLIENTS; i++) {
               fprintf(stderr, "bm_lock_buffer: Buffer \"%s\" client %d \"%s\" pid %d\n", pbuf->buffer_name, i, pbuf->buffer_header->client[i].name, pbuf->buffer_header->client[i].pid);
            }
         }
            
         status = ss_semaphore_wait_for(pbuf->semaphore, _bm_lock_timeout);
         
         if (status != SS_SUCCESS) {
            fprintf(stderr, "bm_lock_buffer: Error: Cannot lock buffer \"%s\", ss_semaphore_wait_for() status %d, aborting...\n", pbuf->buffer_name, status);
            cm_msg(MERROR, "bm_lock_buffer", "Cannot lock buffer \"%s\", ss_semaphore_wait_for() status %d, aborting...", pbuf->buffer_name, status);
            abort();
            /* DOES NOT RETURN */
         }
      }
   }

   // protect against double lock
   assert(!pbuf->locked);
   pbuf->locked = TRUE;

#if 0
   int x = MAX_CLIENTS - 1;
   if (pbuf->buffer_header->client[x].unused1 != 0) {
      printf("lllock [%s] unused1 %d pid %d\n", pbuf->buffer_name, pbuf->buffer_header->client[x].unused1, getpid());
   }
   //assert(pbuf->buffer_header->client[x].unused1 == 0);
   pbuf->buffer_header->client[x].unused1 = getpid();
#endif

   pbuf->count_lock++;

   return BM_SUCCESS;
}

/********************************************************************/
static void xbm_unlock_buffer(BUFFER *pbuf) {
   // NB: locking order: 1st buffer mutex, 2nd buffer semaphore. Unlock in reverse order.

#if 0
   int x = MAX_CLIENTS-1;
   if (pbuf->attached) {
      if (pbuf->buffer_header->client[x].unused1 != getpid()) {
         printf("unlock [%s] unused1 %d pid %d\n", pbuf->buffer_header->name, pbuf->buffer_header->client[x].unused1, getpid());
      }
      pbuf->buffer_header->client[x].unused1 = 0;
   } else {
      printf("unlock [??????] unused1 ????? pid %d\n", getpid());
   }
#endif

   // protect against double unlock
   assert(pbuf->locked);
   pbuf->locked = FALSE;

   ss_semaphore_release(pbuf->semaphore);
   pbuf->buffer_mutex.unlock();
}

#endif                          /* LOCAL_ROUTINES */

/********************************************************************/
INT bm_init_buffer_counters(INT buffer_handle)
/********************************************************************\

  Routine: bm_init_event_counters

  Purpose: Initialize counters for a specific buffer. This routine
           should be called at the beginning of a run.

  Input:
    INT    buffer_handle    Handle to the buffer to be
                            initialized.
  Output:
    none

  Function value:
    BM_SUCCESS              Successful completion
    BM_INVALID_HANDLE       Buffer handle is invalid

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_BM_INIT_BUFFER_COUNTERS, buffer_handle);

#ifdef LOCAL_ROUTINES

   int status = 0;

   BUFFER* pbuf = bm_get_buffer("bm_init_buffer_counters", buffer_handle, &status);

   if (!pbuf)
      return status;

   bm_lock_buffer_guard pbuf_guard(pbuf);

   if (!pbuf_guard.is_locked())
      return pbuf_guard.get_status();

   pbuf->buffer_header->num_in_events = 0;
   pbuf->buffer_header->num_out_events = 0;

#endif                          /* LOCAL_ROUTINES */

   return BM_SUCCESS;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/**dox***************************************************************/
/** @addtogroup bmfunctionc
 *
 *  @{  */

/********************************************************************/
/**
Modifies buffer cache size.
Without a buffer cache, events are copied to/from the shared
memory event by event.

To protect processed from accessing the shared memory simultaneously,
semaphores are used. Since semaphore operations are CPU consuming (typically
50-100us) this can slow down the data transfer especially for small events.
By using a cache the number of semaphore operations is reduced dramatically.
Instead writing directly to the shared memory, the events are copied to a
local cache buffer. When this buffer is full, it is copied to the shared
memory in one operation. The same technique can be used when receiving events.

The drawback of this method is that the events have to be copied twice, once to the
cache and once from the cache to the shared memory. Therefore it can happen that the
usage of a cache even slows down data throughput on a given environment (computer
type, OS type, event size).
The cache size has therefore be optimized manually to maximize data throughput.
@param buffer_handle buffer handle obtained via bm_open_buffer()
@param read_size cache size for reading events in bytes, zero for no cache
@param write_size cache size for writing events in bytes, zero for no cache
@return BM_SUCCESS, BM_INVALID_HANDLE, BM_NO_MEMORY, BM_INVALID_PARAM
*/
INT bm_set_cache_size(INT buffer_handle, INT read_size, INT write_size)
/*------------------------------------------------------------------*/
{
   if (rpc_is_remote())
      return rpc_call(RPC_BM_SET_CACHE_SIZE, buffer_handle, read_size, write_size);

#ifdef LOCAL_ROUTINES
   {
      int status = 0;

      BUFFER *pbuf = bm_get_buffer("bm_set_cache_size", buffer_handle, &status);

      if (!pbuf)
         return status;

      /* lock pbuf for local access. we do not lock buffer semaphore because we do not touch the shared memory */

      status = bm_lock_buffer_mutex(pbuf);

      if (status != BM_SUCCESS)
         return status;

      if (write_size < 0)
         write_size = 0;

      if (write_size > pbuf->buffer_header->size/4) {
         int new_write_size = pbuf->buffer_header->size/4;
         cm_msg(MERROR, "bm_set_cache_size", "requested write cache size %d on buffer \"%s\" is too big: buffer size is %d, write cache size will be %d bytes", write_size, pbuf->buffer_header->name, pbuf->buffer_header->size, new_write_size);
         write_size = new_write_size;
      }

      pbuf->buffer_mutex.unlock();

      /* resize read cache */

      status = bm_lock_buffer_read_cache(pbuf);

      if (status != BM_SUCCESS) {
         return status;
      }

      if (pbuf->read_cache_size > 0) {
         free(pbuf->read_cache);
         pbuf->read_cache = NULL;
      }

      if (read_size > 0) {
         pbuf->read_cache = (char *) malloc(read_size);
         if (pbuf->read_cache == NULL) {
            pbuf->read_cache_mutex.unlock();
            cm_msg(MERROR, "bm_set_cache_size", "not enough memory to allocate cache buffer, malloc(%d) failed", read_size);
            return BM_NO_MEMORY;
         }
      }

      pbuf->read_cache_size = read_size;
      pbuf->read_cache_rp = 0;
      pbuf->read_cache_wp = 0;

      pbuf->read_cache_mutex.unlock();

      /* resize the write cache */

      status = bm_lock_buffer_write_cache(pbuf);

      if (status != BM_SUCCESS)
         return status;

      // FIXME: should flush the write cache!
      if (pbuf->write_cache_size && pbuf->write_cache_wp > 0) {
         cm_msg(MERROR, "bm_set_cache_size", "buffer \"%s\" lost %d bytes from the write cache", pbuf->buffer_name, (int)pbuf->write_cache_wp);
      }

      /* manage write cache */
      if (pbuf->write_cache_size > 0) {
         free(pbuf->write_cache);
         pbuf->write_cache = NULL;
      }

      if (write_size > 0) {
         pbuf->write_cache = (char *) M_MALLOC(write_size);
         if (pbuf->write_cache == NULL) {
            pbuf->write_cache_mutex.unlock();
            cm_msg(MERROR, "bm_set_cache_size", "not enough memory to allocate cache buffer, malloc(%d) failed", write_size);
            return BM_NO_MEMORY;
         }
      }

      pbuf->write_cache_size = write_size;
      pbuf->write_cache_rp = 0;
      pbuf->write_cache_wp = 0;

      pbuf->write_cache_mutex.unlock();
   }
#endif                          /* LOCAL_ROUTINES */

   return BM_SUCCESS;
}

/********************************************************************/
/**
Compose a Midas event header.
An event header can usually be set-up manually or
through this routine. If the data size of the event is not known when
the header is composed, it can be set later with event_header->data-size = <...>
Following structure is created at the beginning of an event
\code
typedef struct {
 short int     event_id;
 short int     trigger_mask;
 DWORD         serial_number;
 DWORD         time_stamp;
 DWORD         data_size;
} EVENT_HEADER;

char event[1000];
 bm_compose_event((EVENT_HEADER *)event, 1, 0, 100, 1);
 *(event+sizeof(EVENT_HEADER)) = <...>
\endcode
@param event_header pointer to the event header
@param event_id event ID of the event
@param trigger_mask trigger mask of the event
@param data_size size if the data part of the event in bytes
@param serial serial number
@return BM_SUCCESS
*/
INT bm_compose_event(EVENT_HEADER *event_header, short int event_id, short int trigger_mask, DWORD data_size, DWORD serial)
{
   event_header->event_id = event_id;
   event_header->trigger_mask = trigger_mask;
   event_header->data_size = data_size;
   event_header->time_stamp = ss_time();
   event_header->serial_number = serial;

   return BM_SUCCESS;
}

INT bm_compose_event_threadsafe(EVENT_HEADER *event_header, short int event_id, short int trigger_mask, DWORD data_size, DWORD *serial)
{
   static std::mutex mutex;

   event_header->event_id = event_id;
   event_header->trigger_mask = trigger_mask;
   event_header->data_size = data_size;
   event_header->time_stamp = ss_time();
   {
      std::lock_guard<std::mutex> lock(mutex);
      event_header->serial_number = *serial;
      *serial = *serial + 1;
      // implicit unlock
   }

   return BM_SUCCESS;
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/********************************************************************/
INT bm_add_event_request(INT buffer_handle, short int event_id,
                         short int trigger_mask,
                         INT sampling_type,
                         EVENT_HANDLER *func,
                         INT request_id)
/********************************************************************\

  Routine:  bm_add_event_request

  Purpose:  Place a request for a specific event type in the client
            structure of the buffer refereced by buffer_handle.

  Input:
    INT          buffer_handle  Handle to the buffer where the re-
                                quest should be placed in

    short int    event_id       Event ID      \
    short int    trigger_mask   Trigger mask  / Event specification

    INT          sampling_type  One of GET_ALL, GET_NONBLOCKING or GET_RECENT


                 Note: to request all types of events, use
                   event_id = 0 (all others should be !=0 !)
                   trigger_mask = TRIGGER_ALL
                   sampling_typ = GET_ALL


    void         *func          Callback function
    INT          request_id     Request id (unique number assigned
                                by bm_request_event)

  Output:
    none

  Function value:
    BM_SUCCESS              Successful completion
    BM_NO_MEMORY            Too much request. MAX_EVENT_REQUESTS in
                            MIDAS.H should be increased.
    BM_INVALID_HANDLE       Buffer handle is invalid
    BM_INVALID_PARAM        GET_RECENT is used with non-zero cache size
    RPC_NET_ERROR           Network error

\********************************************************************/
{
   if (rpc_is_remote())
      return rpc_call(RPC_BM_ADD_EVENT_REQUEST, buffer_handle, event_id,
                      trigger_mask, sampling_type, (INT) (POINTER_T) func, request_id);

#ifdef LOCAL_ROUTINES
   {
      int status = 0;

      BUFFER *pbuf = bm_get_buffer("bm_add_event_request", buffer_handle, &status);

      if (!pbuf)
         return status;

      /* lock buffer */
      bm_lock_buffer_guard pbuf_guard(pbuf);

      if (!pbuf_guard.is_locked())
         return pbuf_guard.get_status();

      /* avoid callback/non callback requests */
      if (func == NULL && pbuf->callback) {
         pbuf_guard.unlock(); // unlock before cm_msg()
         cm_msg(MERROR, "bm_add_event_request", "mixing callback/non callback requests not possible");
         return BM_INVALID_MIXING;
      }

      /* do not allow GET_RECENT with nonzero cache size */
      if (sampling_type == GET_RECENT && pbuf->read_cache_size > 0) {
         pbuf_guard.unlock(); // unlock before cm_msg()
         cm_msg(MERROR, "bm_add_event_request", "GET_RECENT request not possible if read cache is enabled");
         return BM_INVALID_PARAM;
      }

      /* get a pointer to the proper client structure */
      BUFFER_HEADER *pheader = pbuf->buffer_header;
      BUFFER_CLIENT *pclient = bm_get_my_client(pbuf, pheader);

      /* look for a empty request entry */
      int i;
      for (i = 0; i < MAX_EVENT_REQUESTS; i++)
         if (!pclient->event_request[i].valid)
            break;

      if (i == MAX_EVENT_REQUESTS) {
         // implicit unlock
         return BM_NO_MEMORY;
      }

      /* setup event_request structure */
      pclient->event_request[i].id = request_id;
      pclient->event_request[i].valid = TRUE;
      pclient->event_request[i].event_id = event_id;
      pclient->event_request[i].trigger_mask = trigger_mask;
      pclient->event_request[i].sampling_type = sampling_type;

      pclient->all_flag = pclient->all_flag || (sampling_type & GET_ALL);

      pbuf->get_all_flag = pclient->all_flag;

      /* set callback flag in buffer structure */
      if (func != NULL)
         pbuf->callback = TRUE;

      /*
         Save the index of the last request in the list so that later only the
         requests 0..max_request_index-1 have to be searched through.
       */

      if (i + 1 > pclient->max_request_index)
         pclient->max_request_index = i + 1;
   }
#endif                          /* LOCAL_ROUTINES */

   return BM_SUCCESS;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Place an event request based on certain characteristics.
Multiple event requests can be placed for each buffer, which
are later identified by their request ID. They can contain different callback
routines. Example see bm_open_buffer() and bm_receive_event()
@param buffer_handle buffer handle obtained via bm_open_buffer()
@param event_id event ID for requested events. Use EVENTID_ALL
to receive events with any ID.
@param trigger_mask trigger mask for requested events.
The requested events must have at least one bit in its
trigger mask common with the requested trigger mask. Use TRIGGER_ALL to
receive events with any trigger mask.
@param sampling_type specifies how many events to receive.
A value of GET_ALL receives all events which
match the specified event ID and trigger mask. If the events are consumed slower
than produced, the producer is automatically slowed down. A value of GET_NONBLOCKING
receives as much events as possible without slowing down the producer. GET_ALL is
typically used by the logger, while GET_NONBLOCKING is typically used by analyzers.
@param request_id request ID returned by the function.
This ID is passed to the callback routine and must
be used in the bm_delete_request() routine.
@param func allback routine which gets called when an event of the
specified type is received.
@return BM_SUCCESS, BM_INVALID_HANDLE <br>
BM_NO_MEMORY  too many requests. The value MAX_EVENT_REQUESTS in midas.h
should be increased.
*/
INT bm_request_event(HNDLE buffer_handle, short int event_id,
                     short int trigger_mask,
                     INT sampling_type, HNDLE *request_id,
                     EVENT_HANDLER *func) {
   INT idx, status;

   /* allocate new space for the local request list */
   if (_request_list_entries == 0) {
      _request_list = (REQUEST_LIST *) M_MALLOC(sizeof(REQUEST_LIST));
      memset(_request_list, 0, sizeof(REQUEST_LIST));
      if (_request_list == NULL) {
         cm_msg(MERROR, "bm_request_event", "not enough memory to allocate request list buffer");
         return BM_NO_MEMORY;
      }

      _request_list_entries = 1;
      idx = 0;
   } else {
      /* check for a deleted entry */
      for (idx = 0; idx < _request_list_entries; idx++)
         if (!_request_list[idx].buffer_handle)
            break;

      /* if not found, create new one */
      if (idx == _request_list_entries) {
         _request_list =
                 (REQUEST_LIST *) realloc(_request_list, sizeof(REQUEST_LIST) * (_request_list_entries + 1));
         if (_request_list == NULL) {
            cm_msg(MERROR, "bm_request_event", "not enough memory to allocate request list buffer");
            return BM_NO_MEMORY;
         }

         memset(&_request_list[_request_list_entries], 0, sizeof(REQUEST_LIST));

         _request_list_entries++;
      }
   }

   /* initialize request list */
   _request_list[idx].buffer_handle = buffer_handle;
   _request_list[idx].event_id = event_id;
   _request_list[idx].trigger_mask = trigger_mask;
   _request_list[idx].dispatcher = func;

   *request_id = idx;

   /* add request in buffer structure */
   status = bm_add_event_request(buffer_handle, event_id, trigger_mask, sampling_type, func, idx);
   if (status != BM_SUCCESS)
      return status;

   return BM_SUCCESS;
}

/********************************************************************/
/**
Delete a previously placed request for a specific event
type in the client structure of the buffer refereced by buffer_handle.
@param buffer_handle  Handle to the buffer where the re-
                                quest should be placed in
@param request_id     Request id returned by bm_request_event
@return BM_SUCCESS, BM_INVALID_HANDLE, BM_NOT_FOUND, RPC_NET_ERROR
*/
INT bm_remove_event_request(INT buffer_handle, INT request_id) {
   if (rpc_is_remote())
      return rpc_call(RPC_BM_REMOVE_EVENT_REQUEST, buffer_handle, request_id);

#ifdef LOCAL_ROUTINES
   {
      int status = 0;

      BUFFER *pbuf = bm_get_buffer("bm_remove_event_request", buffer_handle, &status);

      if (!pbuf)
         return status;

      /* lock buffer */
      bm_lock_buffer_guard pbuf_guard(pbuf);

      if (!pbuf_guard.is_locked())
         return pbuf_guard.get_status();

      INT i, deleted;

      /* get a pointer to the proper client structure */
      BUFFER_HEADER *pheader = pbuf->buffer_header;
      BUFFER_CLIENT *pclient = bm_get_my_client(pbuf, pheader);

      /* check all requests and set to zero if matching */
      for (i = 0, deleted = 0; i < pclient->max_request_index; i++)
         if (pclient->event_request[i].valid && pclient->event_request[i].id == request_id) {
            memset(&pclient->event_request[i], 0, sizeof(EVENT_REQUEST));
            deleted++;
         }

      /* calculate new max_request_index entry */
      for (i = MAX_EVENT_REQUESTS - 1; i >= 0; i--)
         if (pclient->event_request[i].valid)
            break;

      pclient->max_request_index = i + 1;

      /* calculate new all_flag */
      pclient->all_flag = FALSE;

      for (i = 0; i < pclient->max_request_index; i++)
         if (pclient->event_request[i].valid && (pclient->event_request[i].sampling_type & GET_ALL)) {
            pclient->all_flag = TRUE;
            break;
         }

      pbuf->get_all_flag = pclient->all_flag;

      if (!deleted)
         return BM_NOT_FOUND;
   }
#endif                          /* LOCAL_ROUTINES */

   return BM_SUCCESS;
}

/********************************************************************/
/**
Deletes an event request previously done with bm_request_event().
When an event request gets deleted, events of that requested type are
not received any more. When a buffer is closed via bm_close_buffer(), all
event requests from that buffer are deleted automatically
@param request_id request identifier given by bm_request_event()
@return BM_SUCCESS, BM_INVALID_HANDLE
*/
INT bm_delete_request(INT request_id) {
   if (request_id < 0 || request_id >= _request_list_entries)
      return BM_INVALID_HANDLE;

   /* remove request entry from buffer */
   int status = bm_remove_event_request(_request_list[request_id].buffer_handle, request_id);

   memset(&_request_list[request_id], 0, sizeof(REQUEST_LIST));

   return status;
}

#if 0                           // currently not used
static void bm_show_pointers(const BUFFER_HEADER * pheader)
{
   int i;
   const BUFFER_CLIENT *pclient;

   pclient = pheader->client;

   printf("buffer \'%s\', rptr: %d, wptr: %d, size: %d\n", pheader->name, pheader->read_pointer,
          pheader->write_pointer, pheader->size);
   for (i = 0; i < pheader->max_client_index; i++)
      if (pclient[i].pid) {
         printf("pointers: client %d \'%s\', rptr %d\n", i, pclient[i].name, pclient[i].read_pointer);
      }

   printf("done\n");
}
#endif

static void bm_validate_client_pointers_locked(const BUFFER_HEADER *pheader, BUFFER_CLIENT *pclient) {
   assert(pheader->read_pointer >= 0 && pheader->read_pointer <= pheader->size);
   assert(pclient->read_pointer >= 0 && pclient->read_pointer <= pheader->size);

   if (pheader->read_pointer <= pheader->write_pointer) {

      if (pclient->read_pointer < pheader->read_pointer) {
         cm_msg(MINFO, "bm_validate_client_pointers",
                "Corrected read pointer for client \'%s\' on buffer \'%s\' from %d to %d, write pointer %d, size %d",
                pclient->name,
                pheader->name, pclient->read_pointer, pheader->read_pointer, pheader->write_pointer, pheader->size);

         pclient->read_pointer = pheader->read_pointer;
      }

      if (pclient->read_pointer > pheader->write_pointer) {
         cm_msg(MINFO, "bm_validate_client_pointers",
                "Corrected read pointer for client \'%s\' on buffer \'%s\' from %d to %d, read pointer %d, size %d",
                pclient->name,
                pheader->name, pclient->read_pointer, pheader->write_pointer, pheader->read_pointer, pheader->size);

         pclient->read_pointer = pheader->write_pointer;
      }

   } else {

      if (pclient->read_pointer < 0) {
         cm_msg(MINFO, "bm_validate_client_pointers",
                "Corrected read pointer for client \'%s\' on buffer \'%s\' from %d to %d, write pointer %d, size %d",
                pclient->name,
                pheader->name, pclient->read_pointer, pheader->read_pointer, pheader->write_pointer, pheader->size);

         pclient->read_pointer = pheader->read_pointer;
      }

      if (pclient->read_pointer >= pheader->size) {
         cm_msg(MINFO, "bm_validate_client_pointers",
                "Corrected read pointer for client \'%s\' on buffer \'%s\' from %d to %d, write pointer %d, size %d",
                pclient->name,
                pheader->name, pclient->read_pointer, pheader->read_pointer, pheader->write_pointer, pheader->size);

         pclient->read_pointer = pheader->read_pointer;
      }

      if (pclient->read_pointer > pheader->write_pointer && pclient->read_pointer < pheader->read_pointer) {
         cm_msg(MINFO, "bm_validate_client_pointers",
                "Corrected read pointer for client \'%s\' on buffer \'%s\' from %d to %d, write pointer %d, size %d",
                pclient->name,
                pheader->name, pclient->read_pointer, pheader->read_pointer, pheader->write_pointer, pheader->size);

         pclient->read_pointer = pheader->read_pointer;
      }
   }
}

#if 0                           // currently not used
static void bm_validate_pointers(BUFFER_HEADER * pheader)
{
   BUFFER_CLIENT *pclient = pheader->client;
   int i;

   for (i = 0; i < pheader->max_client_index; i++)
      if (pclient[i].pid) {
         bm_validate_client_pointers(pheader, &pclient[i]);
      }
}
#endif

//
// Buffer pointers
//
// normal:
//
// zero -->
// ... free space
// read_pointer -->
// client1 rp -->
// client2 rp -->
// ... buffered data
// write_pointer -->
// ... free space
// pheader->size -->
//
// inverted:
//
// zero -->
// client3 rp -->
// ... buffered data
// client4 rp -->
// write_pointer -->
// ... free space
// read_pointer -->
// client1 rp -->
// client2 rp -->
// ... buffered data
// pheader->size -->
//

static BOOL bm_update_read_pointer_locked(const char *caller_name, BUFFER_HEADER *pheader) {
   assert(caller_name);

   /* calculate global read pointer as "minimum" of client read pointers */
   int min_rp = pheader->write_pointer;

   int i;
   for (i = 0; i < pheader->max_client_index; i++) {
      BUFFER_CLIENT *pc = pheader->client + i;
      if (pc->pid) {
         bm_validate_client_pointers_locked(pheader, pc);

#if 0
         printf("bm_update_read_pointer: [%s] rp %d, wp %d, size %d, min_rp %d, client [%s] rp %d\n",
                pheader->name,
                pheader->read_pointer,
                pheader->write_pointer,
                pheader->size,
                min_rp,
                pc->name,
                pc->read_pointer);
#endif

         if (pheader->read_pointer <= pheader->write_pointer) {
            // normal pointers
            if (pc->read_pointer < min_rp)
               min_rp = pc->read_pointer;
         } else {
            // inverted pointers
            if (pc->read_pointer <= pheader->write_pointer) {
               // clients 3 and 4
               if (pc->read_pointer < min_rp)
                  min_rp = pc->read_pointer;
            } else {
               // clients 1 and 2
               int xptr = pc->read_pointer - pheader->size;
               if (xptr < min_rp)
                  min_rp = xptr;
            }
         }
      }
   }

   if (min_rp < 0)
      min_rp += pheader->size;

   assert(min_rp >= 0);
   assert(min_rp < pheader->size);

   if (min_rp == pheader->read_pointer) {
      return FALSE;
   }

#if 0
   printf("bm_update_read_pointer: [%s] rp %d, wp %d, size %d, new_rp %d, moved\n",
          pheader->name,
          pheader->read_pointer,
          pheader->write_pointer,
          pheader->size,
          min_rp);
#endif

   pheader->read_pointer = min_rp;

   return TRUE;
}

static void bm_wakeup_producers_locked(const BUFFER_HEADER *pheader, const BUFFER_CLIENT *pc) {
   int i;
   int have_get_all_requests = 0;

   for (i = 0; i < pc->max_request_index; i++)
      if (pc->event_request[i].valid)
         have_get_all_requests |= (pc->event_request[i].sampling_type == GET_ALL);

   /* only GET_ALL requests actually free space in the event buffer */
   if (!have_get_all_requests)
      return;

   /*
      If read pointer has been changed, it may have freed up some space
      for waiting producers. So check if free space is now more than 50%
      of the buffer size and wake waiting producers.
    */

   int free_space = pc->read_pointer - pheader->write_pointer;
   if (free_space <= 0)
      free_space += pheader->size;

   if (free_space >= pheader->size * 0.5) {
      for (i = 0; i < pheader->max_client_index; i++) {
         const BUFFER_CLIENT *pc = pheader->client + i;
         if (pc->pid && pc->write_wait) {
            BOOL send_wakeup = (pc->write_wait < free_space);
            //printf("bm_wakeup_producers: buffer [%s] client [%s] write_wait %d, free_space %d, sending wakeup message %d\n", pheader->name, pc->name, pc->write_wait, free_space, send_wakeup);
            if (send_wakeup) {
               ss_resume(pc->port, "B  ");
            }
         }
      }
   }
}

static void bm_dispatch_event(int buffer_handle, EVENT_HEADER *pevent) {
   int i;

   /* call dispatcher */
   for (i = 0; i < _request_list_entries; i++)
      if (_request_list[i].buffer_handle == buffer_handle &&
          bm_match_event(_request_list[i].event_id, _request_list[i].trigger_mask, pevent)) {
         /* if event is fragmented, call defragmenter */
         if (((uint16_t(pevent->event_id) & uint16_t(0xF000)) == uint16_t(EVENTID_FRAG1)) || ((uint16_t(pevent->event_id) & uint16_t(0xF000)) == uint16_t(EVENTID_FRAG)))
            bm_defragment_event(buffer_handle, i, pevent, (void *) (pevent + 1), _request_list[i].dispatcher);
         else
            _request_list[i].dispatcher(buffer_handle, i, pevent, (void *) (pevent + 1));
      }
}

#ifdef LOCAL_ROUTINES

static void bm_incr_read_cache_locked(BUFFER *pbuf, int total_size) {
   /* increment read cache read pointer */
   pbuf->read_cache_rp += total_size;

   if (pbuf->read_cache_rp == pbuf->read_cache_wp) {
      pbuf->read_cache_rp = 0;
      pbuf->read_cache_wp = 0;
   }
}

static BOOL bm_peek_read_cache_locked(BUFFER *pbuf, EVENT_HEADER **ppevent, int *pevent_size, int *ptotal_size)
{
   if (pbuf->read_cache_rp == pbuf->read_cache_wp)
      return FALSE;

   EVENT_HEADER *pevent = (EVENT_HEADER *) (pbuf->read_cache + pbuf->read_cache_rp);
   int event_size = pevent->data_size + sizeof(EVENT_HEADER);
   int total_size = ALIGN8(event_size);

   if (ppevent)
      *ppevent = pevent;
   if (pevent_size)
      *pevent_size = event_size;
   if (ptotal_size)
      *ptotal_size = total_size;

   return TRUE;
}

//
// return values:
// BM_SUCCESS - have an event, fill ppevent, ppevent_size & co
// BM_ASYNC_RETURN - buffer is empty
// BM_CORRUPTED - buffer is corrupted
//

static int bm_peek_buffer_locked(BUFFER *pbuf, BUFFER_HEADER *pheader, BUFFER_CLIENT *pc, EVENT_HEADER **ppevent, int *pevent_size, int *ptotal_size)
{
   if (pc->read_pointer == pheader->write_pointer) {
      /* no more events buffered for this client */
      if (!pc->read_wait) {
         //printf("bm_peek_buffer_locked: buffer [%s] client [%s], set read_wait!\n", pheader->name, pc->name);
         pc->read_wait = TRUE;
      }
      return BM_ASYNC_RETURN;
   }

   if (pc->read_wait) {
      //printf("bm_peek_buffer_locked: buffer [%s] client [%s], clear read_wait!\n", pheader->name, pc->name);
      pc->read_wait = FALSE;
   }

   if ((pc->read_pointer < 0) || (pc->read_pointer >= pheader->size)) {
      cm_msg(MERROR, "bm_peek_buffer_locked", "event buffer \"%s\" is corrupted: client \"%s\" read pointer %d is invalid. buffer read pointer %d, write pointer %d, size %d", pheader->name, pc->name, pc->read_pointer, pheader->read_pointer, pheader->write_pointer, pheader->size);
      return BM_CORRUPTED;
   }

   char *pdata = (char *) (pheader + 1);

   EVENT_HEADER *pevent = (EVENT_HEADER *) (pdata + pc->read_pointer);
   int event_size = pevent->data_size + sizeof(EVENT_HEADER);
   int total_size = ALIGN8(event_size);

   if ((total_size <= 0) || (total_size > pheader->size)) {
      cm_msg(MERROR, "bm_peek_buffer_locked", "event buffer \"%s\" is corrupted: client \"%s\" read pointer %d points to invalid event: data_size %d, event_size %d, total_size %d. buffer size: %d, read_pointer: %d, write_pointer: %d", pheader->name, pc->name, pc->read_pointer, pevent->data_size, event_size, total_size, pheader->size, pheader->read_pointer, pheader->write_pointer);
      return BM_CORRUPTED;
   }

   assert(total_size > 0);
   assert(total_size <= pheader->size);

   if (ppevent)
      *ppevent = pevent;
   if (pevent_size)
      *pevent_size = event_size;
   if (ptotal_size)
      *ptotal_size = total_size;

   return BM_SUCCESS;
}

static void bm_read_from_buffer_locked(const BUFFER_HEADER *pheader, int rp, char *buf, int event_size)
{
   const char *pdata = (const char *) (pheader + 1);

   if (rp + event_size <= pheader->size) {
      /* copy event to cache */
      memcpy(buf, pdata + rp, event_size);
   } else {
      /* event is splitted */
      int size = pheader->size - rp;
      memcpy(buf, pdata + rp, size);
      memcpy(buf + size, pdata, event_size - size);
   }
}

static void bm_read_from_buffer_locked(const BUFFER_HEADER *pheader, int rp, std::vector<char> *vecptr, int event_size)
{
   const char *pdata = (const char *) (pheader + 1);

   if (rp + event_size <= pheader->size) {
      /* copy event to cache */
      vecptr->assign(pdata + rp, pdata + rp + event_size);
   } else {
      /* event is splitted */
      int size = pheader->size - rp;
      vecptr->assign(pdata + rp, pdata + rp + size);
      vecptr->insert(vecptr->end(), pdata, pdata + event_size - size);
   }
}

static BOOL bm_check_requests(const BUFFER_CLIENT *pc, const EVENT_HEADER *pevent) {

   BOOL is_requested = FALSE;
   int i;
   for (i = 0; i < pc->max_request_index; i++) {
      const EVENT_REQUEST *prequest = pc->event_request + i;
      if (prequest->valid) {
         if (bm_match_event(prequest->event_id, prequest->trigger_mask, pevent)) {
            /* check if this is a recent event */
            if (prequest->sampling_type == GET_RECENT) {
               if (ss_time() - pevent->time_stamp > 1) {
                  /* skip that event */
                  continue;
               }
            }

            is_requested = TRUE;
            break;
         }
      }
   }
   return is_requested;
}

static int bm_wait_for_more_events_locked(bm_lock_buffer_guard& pbuf_guard, BUFFER_CLIENT *pc, int timeout_msec, BOOL unlock_read_cache);

static int bm_fill_read_cache_locked(bm_lock_buffer_guard& pbuf_guard, int timeout_msec)
{
   BUFFER* pbuf = pbuf_guard.get_pbuf();
   BUFFER_HEADER* pheader = pbuf->buffer_header;
   BUFFER_CLIENT *pc = bm_get_my_client(pbuf, pheader);
   BOOL need_wakeup = FALSE;
   int count_events = 0;

   //printf("bm_fill_read_cache: [%s] timeout %d, size %d, rp %d, wp %d\n", pheader->name, timeout_msec, pbuf->read_cache_size, pbuf->read_cache_rp, pbuf->read_cache_wp);

   /* loop over all events in the buffer */

   while (1) {
      EVENT_HEADER *pevent = NULL;
      int event_size = 3; // poison value
      int total_size = 3; // poison value

      int status = bm_peek_buffer_locked(pbuf, pheader, pc, &pevent, &event_size, &total_size);
      if (status == BM_CORRUPTED) {
         return status;
      } else if (status != BM_SUCCESS) {
         /* event buffer is empty */
         if (timeout_msec == BM_NO_WAIT) {
            if (need_wakeup)
               bm_wakeup_producers_locked(pheader, pc);
            //printf("bm_fill_read_cache: [%s] async %d, size %d, rp %d, wp %d, events %d, buffer is empty\n", pheader->name, async_flag, pbuf->read_cache_size, pbuf->read_cache_rp, pbuf->read_cache_wp, count_events);
            if (pbuf->read_cache_rp == pbuf->read_cache_wp) {
               // read cache is empty
               return BM_ASYNC_RETURN;
            }
            return BM_SUCCESS;
         }

         int status = bm_wait_for_more_events_locked(pbuf_guard, pc, timeout_msec, TRUE);

         if (status != BM_SUCCESS) {
            // we only come here with SS_ABORT & co
            //printf("bm_fill_read_cache: [%s] async %d, size %d, rp %d, wp %d, events %d, bm_wait_for_more_events() status %d\n", pheader->name, async_flag, pbuf->read_cache_size, pbuf->read_cache_rp, pbuf->read_cache_wp, count_events, status);
            return status;
         }

         // make sure we wait for new event only once
         timeout_msec = BM_NO_WAIT;
         // go back to bm_peek_buffer_locked
         continue;
      }

      /* loop over all requests: if this event matches a request,
       * copy it to the read cache */

      BOOL is_requested = bm_check_requests(pc, pevent);

      if (is_requested) {
         if (pbuf->read_cache_wp + total_size > pbuf->read_cache_size) {
            /* read cache is full */
            if (need_wakeup)
               bm_wakeup_producers_locked(pheader, pc);
            //printf("bm_fill_read_cache: [%s] async %d, size %d, rp %d, wp %d, events %d, event total_size %d, cache full\n", pheader->name, async_flag, pbuf->read_cache_size, pbuf->read_cache_rp, pbuf->read_cache_wp, count_events, total_size);
            return BM_SUCCESS;
         }

         bm_read_from_buffer_locked(pheader, pc->read_pointer, pbuf->read_cache + pbuf->read_cache_wp, event_size);

         pbuf->read_cache_wp += total_size;
         count_events++;

         /* update statistics */
         pheader->num_out_events++;
         pbuf->count_read++;
         pbuf->bytes_read += event_size;
      }

      /* shift read pointer */

      int new_read_pointer = bm_incr_rp_no_check(pheader, pc->read_pointer, total_size);
      pc->read_pointer = new_read_pointer;

      need_wakeup = TRUE;
   }
   /* NOT REACHED */
}

static void bm_convert_event_header(EVENT_HEADER *pevent, int convert_flags) {
   /* now convert event header */
   if (convert_flags) {
      rpc_convert_single(&pevent->event_id, TID_INT16, RPC_OUTGOING, convert_flags);
      rpc_convert_single(&pevent->trigger_mask, TID_INT16, RPC_OUTGOING, convert_flags);
      rpc_convert_single(&pevent->serial_number, TID_UINT32, RPC_OUTGOING, convert_flags);
      rpc_convert_single(&pevent->time_stamp, TID_UINT32, RPC_OUTGOING, convert_flags);
      rpc_convert_single(&pevent->data_size, TID_UINT32, RPC_OUTGOING, convert_flags);
   }
}

static int bm_wait_for_free_space_locked(bm_lock_buffer_guard& pbuf_guard, int timeout_msec, int requested_space, bool unlock_write_cache)
{
   // return values:
   // BM_SUCCESS - have "requested_space" bytes free in the buffer
   // BM_CORRUPTED - shared memory is corrupted
   // BM_NO_MEMORY - asked for more than buffer size
   // BM_ASYNC_RETURN - timeout waiting for free space
   // BM_INVALID_HANDLE - buffer was closed (locks released) (via bm_clock_xxx())
   // SS_ABORT - we are told to shutdown (locks releases)

   int status;
   BUFFER* pbuf = pbuf_guard.get_pbuf();
   BUFFER_HEADER *pheader = pbuf->buffer_header;
   char *pdata = (char *) (pheader + 1);

   /* make sure the buffer never completely full:
    * read pointer and write pointer would coincide
    * and the code cannot tell if it means the
    * buffer is 100% full or 100% empty. It will explode
    * or lose events */
   requested_space += 100;

   if (requested_space >= pheader->size)
      return BM_NO_MEMORY;

   DWORD time_start = ss_millitime();
   DWORD time_end = time_start + timeout_msec;

   //DWORD blocking_time = 0;
   int blocking_loops = 0;
   int blocking_client_index = -1;
   char blocking_client_name[NAME_LENGTH];
   blocking_client_name[0] = 0;

   while (1) {
      while (1) {
         /* check if enough space in buffer */

         int free = pheader->read_pointer - pheader->write_pointer;
         if (free <= 0)
            free += pheader->size;

         //printf("bm_wait_for_free_space: buffer pointers: read: %d, write: %d, free space: %d, bufsize: %d, event size: %d, timeout %d\n", pheader->read_pointer, pheader->write_pointer, free, pheader->size, requested_space, timeout_msec);

         if (requested_space < free) { /* note the '<' to avoid 100% filling */
            //if (blocking_loops) {
            //   DWORD wait_time = ss_millitime() - blocking_time;
            //   printf("blocking client \"%s\", time %d ms, loops %d\n", blocking_client_name, wait_time, blocking_loops);
            //}

            if (pbuf->wait_start_time != 0) {
               DWORD now = ss_millitime();
               DWORD wait_time = now - pbuf->wait_start_time;
               pbuf->time_write_wait += wait_time;
               pbuf->wait_start_time = 0;
               int iclient = pbuf->wait_client_index;
               //printf("bm_wait_for_free_space: wait ended: wait time %d ms, blocking client index %d\n", wait_time, iclient);
               if (iclient >= 0 && iclient < MAX_CLIENTS) {
                  pbuf->client_count_write_wait[iclient] += 1;
                  pbuf->client_time_write_wait[iclient] += wait_time;
               }
            }

            //if (blocking_loops > 0) {
            //   printf("bm_wait_for_free_space: buffer pointers: read: %d, write: %d, free space: %d, bufsize: %d, event size: %d, timeout %d, found space after %d waits\n", pheader->read_pointer, pheader->write_pointer, free, pheader->size, requested_space, timeout_msec, blocking_loops);
            //}

            return BM_SUCCESS;
         }

         if (!bm_validate_rp("bm_wait_for_free_space_locked", pheader, pheader->read_pointer)) {
            cm_msg(MERROR, "bm_wait_for_free_space",
                   "error: buffer \"%s\" is corrupted: read_pointer %d, write_pointer %d, size %d, free %d, waiting for %d bytes: read pointer is invalid",
                   pheader->name,
                   pheader->read_pointer,
                   pheader->write_pointer,
                   pheader->size,
                   free,
                   requested_space);
            return BM_CORRUPTED;
         }

         const EVENT_HEADER *pevent = (const EVENT_HEADER *) (pdata + pheader->read_pointer);
         int event_size = pevent->data_size + sizeof(EVENT_HEADER);
         int total_size = ALIGN8(event_size);

#if 0
         printf("bm_wait_for_free_space: buffer pointers: read: %d, write: %d, free space: %d, bufsize: %d, event size: %d, blocking event size %d/%d\n", pheader->read_pointer, pheader->write_pointer, free, pheader->size, requested_space, event_size, total_size);
#endif

         if (pevent->data_size <= 0 || total_size <= 0 || total_size > pheader->size) {
            cm_msg(MERROR, "bm_wait_for_free_space",
                   "error: buffer \"%s\" is corrupted: read_pointer %d, write_pointer %d, size %d, free %d, waiting for %d bytes: read pointer points to an invalid event: data_size %d, event size %d, total_size %d",
                   pheader->name,
                   pheader->read_pointer,
                   pheader->write_pointer,
                   pheader->size,
                   free,
                   requested_space,
                   pevent->data_size,
                   event_size,
                   total_size);
            return BM_CORRUPTED;
         }

         int blocking_client = -1;

         int i;
         for (i = 0; i < pheader->max_client_index; i++) {
            BUFFER_CLIENT *pc = pheader->client + i;
            if (pc->pid) {
               if (pc->read_pointer == pheader->read_pointer) {
                  /*
                    First assume that the client with the "minimum" read pointer
                    is not really blocking due to a GET_ALL request.
                  */
                  BOOL blocking = FALSE;
                  //int blocking_request_id = -1;

                  int j;
                  for (j = 0; j < pc->max_request_index; j++) {
                     const EVENT_REQUEST *prequest = pc->event_request + j;
                     if (prequest->valid
                         && bm_match_event(prequest->event_id, prequest->trigger_mask, pevent)) {
                        if (prequest->sampling_type & GET_ALL) {
                           blocking = TRUE;
                           //blocking_request_id = prequest->id;
                           break;
                        }
                     }
                  }

                  //printf("client [%s] blocking %d, request %d\n", pc->name, blocking, blocking_request_id);

                  if (blocking) {
                     blocking_client = i;
                     break;
                  }

                  pc->read_pointer = bm_incr_rp_no_check(pheader, pc->read_pointer, total_size);
               }
            }
         } /* client loop */

         if (blocking_client >= 0) {
            blocking_client_index = blocking_client;
            strlcpy(blocking_client_name, pheader->client[blocking_client].name, sizeof(blocking_client_name));
            //if (!blocking_time) {
            //   blocking_time = ss_millitime();
            //}

            //printf("bm_wait_for_free_space: buffer pointers: read: %d, write: %d, free space: %d, bufsize: %d, event size: %d, timeout %d, must wait for more space!\n", pheader->read_pointer, pheader->write_pointer, free, pheader->size, requested_space, timeout_msec);

            // from this "break" we go into timeout check and sleep/wait.
            break;
         }

         /* no blocking clients. move the read pointer and again check for free space */

         BOOL moved = bm_update_read_pointer_locked("bm_wait_for_free_space", pheader);

         if (!moved) {
            cm_msg(MERROR, "bm_wait_for_free_space",
                   "error: buffer \"%s\" is corrupted: read_pointer %d, write_pointer %d, size %d, free %d, waiting for %d bytes: read pointer did not move as expected",
                   pheader->name,
                   pheader->read_pointer,
                   pheader->write_pointer,
                   pheader->size,
                   free,
                   requested_space);
            return BM_CORRUPTED;
         }

         /* we freed one event, loop back to the check for free space */
      }

      blocking_loops++;

      /* at least one client is blocking */

      BUFFER_CLIENT *pc = bm_get_my_client(pbuf, pheader);
      pc->write_wait = requested_space;

      if (pbuf->wait_start_time == 0) {
         pbuf->wait_start_time = ss_millitime();
         pbuf->count_write_wait++;
         if (requested_space > pbuf->max_requested_space)
            pbuf->max_requested_space = requested_space;
         pbuf->wait_client_index = blocking_client_index;
      }

      DWORD now = ss_millitime();

      //printf("bm_wait_for_free_space: start 0x%08x, now 0x%08x, end 0x%08x, timeout %d, wait %d\n", time_start, now, time_end, timeout_msec, time_end - now);

      int sleep_time_msec = 1000;

      if (timeout_msec == BM_WAIT) {
         // wait forever
      } else if (timeout_msec == BM_NO_WAIT) {
         // no wait
         return BM_ASYNC_RETURN;
      } else {
         // check timeout
         if (now >= time_end) {
            // timeout!
            return BM_ASYNC_RETURN;
         }

         sleep_time_msec = time_end - now;

         if (sleep_time_msec <= 0) {
            sleep_time_msec = 10;
         } else if (sleep_time_msec > 1000) {
            sleep_time_msec = 1000;
         }
      }

      ss_suspend_get_buffer_port(ss_gettid(), &pc->port);

      /* before waiting, unlock everything in the correct order */

      pbuf_guard.unlock();

      if (unlock_write_cache)
         pbuf->write_cache_mutex.unlock();

      //printf("bm_wait_for_free_space: blocking client \"%s\"\n", blocking_client_name);

#ifdef DEBUG_MSG
      cm_msg(MDEBUG, "Send sleep: rp=%d, wp=%d, level=%1.1lf", pheader->read_pointer, pheader->write_pointer, 100 - 100.0 * size / pheader->size);
#endif

      ///* signal other clients wait mode */
      //int idx = bm_validate_client_index(pbuf, FALSE);
      //if (idx >= 0)
      //   pheader->client[idx].write_wait = requested_space;

      //bm_cleanup("bm_wait_for_free_space", ss_millitime(), FALSE);

      status = ss_suspend(sleep_time_msec, MSG_BM);

      /* we are told to shutdown */
      if (status == SS_ABORT) {
         // NB: buffer is locked!
         return SS_ABORT;
      }

      /* make sure we do sleep in this loop:
       * if we are the mserver receiving data on the event
       * socket and the data buffer is full, ss_suspend() will
       * never sleep: it will detect data on the event channel,
       * call rpc_server_receive() (recursively, we already *are* in
       * rpc_server_receive()) and return without sleeping. Result
       * is a busy loop waiting for free space in data buffer */

      /* update May 2021: ss_suspend(MSG_BM) no longer looks at
       * the event socket, and should sleep now, so this sleep below
       * maybe is not needed now. but for safety, I keep it. K.O. */

      if (status != SS_TIMEOUT) {
         //printf("ss_suspend: status %d\n", status);
         ss_sleep(1);
      }

      /* we may be stuck in this loop for an arbitrary long time,
       * depending on how other buffer clients read the accumulated data
       * so we should update all the timeouts & etc. K.O. */

      cm_periodic_tasks();

      /* lock things again in the correct order */

      if (unlock_write_cache) {
         status = bm_lock_buffer_write_cache(pbuf);

         if (status != BM_SUCCESS) {
            // bail out with all locks released
            return status;
         }
      }

      if (!pbuf_guard.relock()) {
         if (unlock_write_cache) {
            pbuf->write_cache_mutex.unlock();
         }

         // bail out with all locks released
         return pbuf_guard.get_status();
      }

      /* revalidate the client index: we could have been removed from the buffer while sleeping */
      pc = bm_get_my_client(pbuf, pheader);

      pc->write_wait = 0;

      ///* validate client index: we could have been removed from the buffer */
      //idx = bm_validate_client_index(pbuf, FALSE);
      //if (idx >= 0)
      //   pheader->client[idx].write_wait = 0;
      //else {
      //   cm_msg(MERROR, "bm_wait_for_free_space", "our client index is no longer valid, exiting...");
      //   status = SS_ABORT;
      //}

#ifdef DEBUG_MSG
      cm_msg(MDEBUG, "Send woke up: rp=%d, wp=%d, level=%1.1lf", pheader->read_pointer, pheader->write_pointer, 100 - 100.0 * size / pheader->size);
#endif

   }
}

static int bm_wait_for_more_events_locked(bm_lock_buffer_guard& pbuf_guard, BUFFER_CLIENT *pc, int timeout_msec, BOOL unlock_read_cache)
{
   BUFFER* pbuf = pbuf_guard.get_pbuf();
   BUFFER_HEADER* pheader = pbuf->buffer_header;
   
   //printf("bm_wait_for_more_events_locked: [%s] timeout %d\n", pheader->name, timeout_msec);
   
   if (pc->read_pointer != pheader->write_pointer) {
      // buffer has data
      return BM_SUCCESS;
   }

   if (timeout_msec == BM_NO_WAIT) {
      /* event buffer is empty and we are told to not wait */
      if (!pc->read_wait) {
         //printf("bm_wait_for_more_events: buffer [%s] client [%s] set read_wait in BM_NO_WAIT!\n", pheader->name, pc->name);
         pc->read_wait = TRUE;
      }
      return BM_ASYNC_RETURN;
   }

   DWORD time_start = ss_millitime();
   DWORD time_wait  = time_start + timeout_msec;
   DWORD sleep_time = 1000;
   if (timeout_msec == BM_NO_WAIT) {
      // default sleep time
   } else if (timeout_msec == BM_WAIT) {
      // default sleep time
   } else {
      if (sleep_time > (DWORD)timeout_msec)
         sleep_time = timeout_msec;
   }

   //printf("time start 0x%08x, end 0x%08x, sleep %d\n", time_start, time_wait, sleep_time);

   while (pc->read_pointer == pheader->write_pointer) {
      /* wait until there is data in the buffer (write pointer moves) */

      if (!pc->read_wait) {
         //printf("bm_wait_for_more_events: buffer [%s] client [%s] set read_wait!\n", pheader->name, pc->name);
         pc->read_wait = TRUE;
      }

      pc->last_activity = ss_millitime();

      ss_suspend_get_buffer_port(ss_gettid(), &pc->port);

      // NB: locking order is: 1st read cache lock, 2nd buffer lock, unlock in reverse order

      pbuf_guard.unlock();

      if (unlock_read_cache)
         pbuf->read_cache_mutex.unlock();

      int status = ss_suspend(sleep_time, MSG_BM);

      if (timeout_msec == BM_NO_WAIT) {
         // return immediately
      } else if (timeout_msec == BM_WAIT) {
         // wait forever
      } else {
         DWORD now = ss_millitime();
         //printf("check timeout: now 0x%08x, end 0x%08x, diff %d\n", now, time_wait, time_wait - now);
         if (now >= time_wait) {
            timeout_msec = BM_NO_WAIT; // cause immediate return
         } else {
            sleep_time = time_wait - now;
            if (sleep_time > 1000)
               sleep_time = 1000;
            //printf("time start 0x%08x, now 0x%08x, end 0x%08x, sleep %d\n", time_start, now, time_wait, sleep_time);
         }
      }

      // NB: locking order is: 1st read cache lock, 2nd buffer lock, unlock in reverse order

      if (unlock_read_cache) {
         status = bm_lock_buffer_read_cache(pbuf);
         if (status != BM_SUCCESS) {
            // bail out with all locks released
            return status;
         }
      }

      if (!pbuf_guard.relock()) {
         if (unlock_read_cache) {
            pbuf->read_cache_mutex.unlock();
         }
         // bail out with all locks released
         return pbuf_guard.get_status();
      }
      
      /* need to revalidate our BUFFER_CLIENT after releasing the buffer lock
       * because we may have been removed from the buffer by bm_cleanup() & co
       * due to a timeout or whatever. */
      pc = bm_get_my_client(pbuf, pheader);

      /* return if TCP connection broken */
      if (status == SS_ABORT)
         return SS_ABORT;

      if (timeout_msec == BM_NO_WAIT)
         return BM_ASYNC_RETURN;
   }

   if (pc->read_wait) {
      //printf("bm_wait_for_more_events: buffer [%s] client [%s] clear read_wait!\n", pheader->name, pc->name);
      pc->read_wait = FALSE;
   }

   return BM_SUCCESS;
}

static void bm_write_to_buffer_locked(BUFFER_HEADER *pheader, int sg_n, const char* const sg_ptr[], const size_t sg_len[], size_t total_size)
{
   char *pdata = (char *) (pheader + 1);

   //int old_write_pointer = pheader->write_pointer;

   /* new event fits into the remaining space? */
   if ((size_t)pheader->write_pointer + total_size <= (size_t)pheader->size) {
      //memcpy(pdata + pheader->write_pointer, pevent, event_size);
      char* wptr = pdata + pheader->write_pointer;
      for (int i=0; i<sg_n; i++) {
         //printf("memcpy %p+%d\n", sg_ptr[i], (int)sg_len[i]);
         memcpy(wptr, sg_ptr[i], sg_len[i]);
         wptr += sg_len[i];
      }
      pheader->write_pointer = pheader->write_pointer + total_size;
      assert(pheader->write_pointer <= pheader->size);
      /* remaining space is smaller than size of an event header? */
      if ((pheader->write_pointer + (int) sizeof(EVENT_HEADER)) > pheader->size) {
         // note: ">" here to match "bm_incr_rp". If remaining space is exactly
         // equal to the event header size, we will write the next event header here,
         // then wrap the pointer and write the event data at the beginning of the buffer.
         //printf("bm_write_to_buffer_locked: truncate wp %d. buffer size %d, remaining %d, event header size %d, event size %d, total size %d\n", pheader->write_pointer, pheader->size, pheader->size-pheader->write_pointer, (int)sizeof(EVENT_HEADER), event_size, total_size);
         pheader->write_pointer = 0;
      }
   } else {
      /* split event */
      size_t size = pheader->size - pheader->write_pointer;

      //printf("split: wp %d, size %d, avail %d\n", pheader->write_pointer, pheader->size, size);

      //memcpy(pdata + pheader->write_pointer, pevent, size);
      //memcpy(pdata, ((const char *) pevent) + size, event_size - size);

      char* wptr = pdata + pheader->write_pointer;
      size_t count = 0;

      // copy first part

      int i = 0;
      for (; i<sg_n; i++) {
         if (count + sg_len[i] > size)
            break;
         memcpy(wptr, sg_ptr[i], sg_len[i]);
         wptr  += sg_len[i];
         count += sg_len[i];
      }

      //printf("wptr %d, count %d\n", wptr-pdata, count);

      // split segment

      size_t first = size - count;
      size_t second = sg_len[i] - first;
      assert(first + second == sg_len[i]);
      assert(count + first == size);
      
      //printf("first %d, second %d\n", first, second);
      
      memcpy(wptr, sg_ptr[i], first);
      wptr = pdata + 0;
      count += first;
      memcpy(wptr, sg_ptr[i] + first, second);
      wptr  += second;
      count += second;
      i++;

      // copy remaining

      for (; i<sg_n; i++) {
         memcpy(wptr, sg_ptr[i], sg_len[i]);
         wptr  += sg_len[i];
         count += sg_len[i];
      }

      //printf("wptr %d, count %d\n", wptr-pdata, count);

      //printf("bm_write_to_buffer_locked: wrap wp %d -> %d. buffer size %d, available %d, wrote %d, remaining %d, event size %d, total size %d\n", pheader->write_pointer, total_size-size, pheader->size, pheader->size-pheader->write_pointer, size, pheader->size - (pheader->write_pointer+size), event_size, total_size);

      pheader->write_pointer = total_size - size;
   }

   //printf("bm_write_to_buffer_locked: buf [%s] size %d, wrote %d/%d, wp %d -> %d\n", pheader->name, pheader->size, event_size, total_size, old_write_pointer, pheader->write_pointer);
}

static int bm_find_first_request_locked(BUFFER_CLIENT *pc, const EVENT_HEADER *pevent) {
   if (pc->pid) {
      int j;
      for (j = 0; j < pc->max_request_index; j++) {
         const EVENT_REQUEST *prequest = pc->event_request + j;
         if (prequest->valid && bm_match_event(prequest->event_id, prequest->trigger_mask, pevent)) {
            return prequest->id;
         }
      }
   }

   return -1;
}

static void bm_notify_reader_locked(BUFFER_HEADER *pheader, BUFFER_CLIENT *pc, int old_write_pointer, int request_id) {
   if (request_id >= 0) {
      /* if that client has a request and is suspended, wake it up */
      if (pc->read_wait) {
         char str[80];
         sprintf(str, "B %s %d", pheader->name, request_id);
         ss_resume(pc->port, str);
         //printf("bm_notify_reader_locked: buffer [%s] client [%s] request_id %d, port %d, message [%s]\n", pheader->name, pc->name, request_id, pc->port, str);
         //printf("bm_notify_reader_locked: buffer [%s] client [%s] clear read_wait!\n", pheader->name, pc->name);
         pc->read_wait = FALSE;
      }
   }
}

#endif // LOCAL_ROUTINES

#if 0
INT bm_send_event_rpc(INT buffer_handle, const EVENT_HEADER *pevent, int event_size, int timeout_msec)
{
   //printf("bm_send_event_rpc: handle %d, size %d, timeout %d\n", buffer_handle, event_size, timeout_msec);

   DWORD time_start = ss_millitime();
   DWORD time_end = time_start + timeout_msec;
   
   int xtimeout_msec = timeout_msec;

   while (1) {
      if (timeout_msec == BM_WAIT) {
         xtimeout_msec = 1000;
      } else if (timeout_msec == BM_NO_WAIT) {
         xtimeout_msec = BM_NO_WAIT;
      } else {
         if (xtimeout_msec > 1000) {
            xtimeout_msec = 1000;
         }
      }
   
      int status = rpc_call(RPC_BM_SEND_EVENT, buffer_handle, pevent, event_size, xtimeout_msec);

      //printf("bm_send_event_rpc: handle %d, size %d, timeout %d, status %d\n", buffer_handle, event_size, xtimeout_msec, status);

      if (status == BM_ASYNC_RETURN) {
         if (timeout_msec == BM_WAIT) {
            // BM_WAIT means wait forever
            continue;
         } else if (timeout_msec == BM_NO_WAIT) {
            // BM_NO_WAIT means do not wait
            return status;
         } else {
            DWORD now = ss_millitime();
            if (now >= time_end) {
               // timeout, return BM_ASYNC_RETURN
               return status;
            }

            DWORD remain = time_end - now;

            if (remain < xtimeout_msec) {
               xtimeout_msec = remain;
            }

            // keep asking for event...
            continue;
         }
      } else if (status == BM_SUCCESS) {
         // success, return BM_SUCCESS
         return status;
      } else {
         // error
         return status;
      }
   }
}
#endif

INT bm_send_event(INT buffer_handle, const EVENT_HEADER *pevent, int unused, int timeout_msec)
{
   const DWORD MAX_DATA_SIZE = (0x7FFFFFF0 - 16); // event size computations are not 32-bit clean, limit event size to 2GB. K.O.
   const DWORD data_size = pevent->data_size; // 32-bit unsigned value

   if (data_size == 0) {
      cm_msg(MERROR, "bm_send_event", "invalid event data size zero");
      return BM_INVALID_SIZE;
   }

   if (data_size > MAX_DATA_SIZE) {
      cm_msg(MERROR, "bm_send_event", "invalid event data size %d (0x%x) maximum is %d (0x%x)", data_size, data_size, MAX_DATA_SIZE, MAX_DATA_SIZE);
      return BM_INVALID_SIZE;
   }

   const size_t event_size = sizeof(EVENT_HEADER) + data_size;

   //printf("bm_send_event: pevent %p, data_size %d, event_size %d, buf_size %d\n", pevent, data_size, event_size, unused);

   if (rpc_is_remote()) {
      //return bm_send_event_rpc(buffer_handle, pevent, event_size, timeout_msec);
      return rpc_send_event_sg(buffer_handle, 1, (char**)&pevent, &event_size);
   } else {
      return bm_send_event_sg(buffer_handle, 1, (char**)&pevent, &event_size, timeout_msec);
   }
}

int bm_send_event_vec(int buffer_handle, const std::vector<char>& event, int timeout_msec)
{
   const char* cptr = event.data();
   size_t clen = event.size();
   return bm_send_event_sg(buffer_handle, 1, &cptr, &clen, timeout_msec);
}

int bm_send_event_vec(int buffer_handle, const std::vector<std::vector<char>>& event, int timeout_msec)
{
   int sg_n = event.size();
   const char* sg_ptr[sg_n];
   size_t sg_len[sg_n];
   for (int i=0; i<sg_n; i++) {
      sg_ptr[i] = event[i].data();
      sg_len[i] = event[i].size();
   }
   return bm_send_event_sg(buffer_handle, sg_n, sg_ptr, sg_len, timeout_msec);
}

static INT bm_flush_cache_locked(bm_lock_buffer_guard& pbuf_guard, int timeout_msec);

/********************************************************************/
/**
Sends an event to a buffer.
This function check if the buffer has enough space for the
event, then copies the event to the buffer in shared memory.
If clients have requests for the event, they are notified via an UDP packet.
\code
char event[1000];
// create event with ID 1, trigger mask 0, size 100 bytes and serial number 1
bm_compose_event((EVENT_HEADER *) event, 1, 0, 100, 1);

// set first byte of event
*(event+sizeof(EVENT_HEADER)) = <...>
#include <stdio.h>
#include "midas.h"
main()
{
 INT status, i;
 HNDLE hbuf;
 char event[1000];
 status = cm_connect_experiment("", "Sample", "Producer", NULL);
 if (status != CM_SUCCESS)
 return 1;
 bm_open_buffer(EVENT_BUFFER_NAME, DEFAULT_BUFFER_SIZE, &hbuf);

 // create event with ID 1, trigger mask 0, size 100 bytes and serial number 1
 bm_compose_event((EVENT_HEADER *) event, 1, 0, 100, 1);

 // set event data
 for (i=0 ; i<100 ; i++)
 *(event+sizeof(EVENT_HEADER)+i) = i;
 // send event
 bm_send_event(hbuf, event, 100+sizeof(EVENT_HEADER), BM_WAIT);
 cm_disconnect_experiment();
 return 0;
}
\endcode
@param buffer_handle Buffer handle obtained via bm_open_buffer()
@param source Address of event buffer
@param buf_size Size of event including event header in bytes
@param timeout_msec Timeout waiting for free space in the event buffer. If BM_WAIT, wait forever.
If BM_NO_WAIT, the function returns immediately with a
value of BM_ASYNC_RETURN without writing the event to the buffer
@return BM_SUCCESS, BM_INVALID_HANDLE, BM_INVALID_PARAM<br>
BM_ASYNC_RETURN Routine called with timeout_msec == BM_NO_WAIT and
buffer has not enough space to receive event<br>
BM_NO_MEMORY   Event is too large for network buffer or event buffer.
One has to increase the event buffer size "/Experiment/Buffer sizes/SYSTEM"
and/or /Experiment/MAX_EVENT_SIZE in ODB.
*/
int bm_send_event_sg(int buffer_handle, int sg_n, const char* const sg_ptr[], const size_t sg_len[], int timeout_msec)
{
   if (rpc_is_remote())
      return rpc_send_event_sg(buffer_handle, sg_n, sg_ptr, sg_len);

   if (sg_n < 1) {
      cm_msg(MERROR, "bm_send_event", "invalid sg_n %d", sg_n);
      return BM_INVALID_SIZE;
   }

   if (sg_ptr[0] == NULL) {
      cm_msg(MERROR, "bm_send_event", "invalid sg_ptr[0] is NULL");
      return BM_INVALID_SIZE;
   }

   if (sg_len[0] < sizeof(EVENT_HEADER)) {
      cm_msg(MERROR, "bm_send_event", "invalid sg_len[0] value %d is smaller than event header size %d", (int)sg_len[0], (int)sizeof(EVENT_HEADER));
      return BM_INVALID_SIZE;
   }

   const EVENT_HEADER* pevent = (const EVENT_HEADER*)sg_ptr[0];
   
   const DWORD MAX_DATA_SIZE = (0x7FFFFFF0 - 16); // event size computations are not 32-bit clean, limit event size to 2GB. K.O.
   const DWORD data_size = pevent->data_size; // 32-bit unsigned value

   if (data_size == 0) {
      cm_msg(MERROR, "bm_send_event", "invalid event data size zero");
      return BM_INVALID_SIZE;
   }

   if (data_size > MAX_DATA_SIZE) {
      cm_msg(MERROR, "bm_send_event", "invalid event data size %d (0x%x) maximum is %d (0x%x)", data_size, data_size, MAX_DATA_SIZE, MAX_DATA_SIZE);
      return BM_INVALID_SIZE;
   }

   const size_t event_size = sizeof(EVENT_HEADER) + data_size;
   const size_t total_size = ALIGN8(event_size);

   size_t count = 0;
   for (int i=0; i<sg_n; i++) {
      count += sg_len[i];
   }

   if (count != event_size) {
      cm_msg(MERROR, "bm_send_event", "data size mismatch: event data_size %d, event_size %d not same as sum of sg_len %d", (int)data_size, (int)event_size, (int)count);
      return BM_INVALID_SIZE;
   }

   //printf("bm_send_event_sg: pevent %p, event_id 0x%04x, serial 0x%08x, data_size %d, event_size %d, total_size %d\n", pevent, pevent->event_id, pevent->serial_number, (int)pevent->data_size, (int)event_size, (int)total_size);

#ifdef LOCAL_ROUTINES
   {
      int status = 0;

      BUFFER *pbuf = bm_get_buffer("bm_send_event_sg", buffer_handle, &status);

      if (!pbuf)
         return status;

      /* round up total_size to next DWORD boundary */
      //int total_size = ALIGN8(event_size);

      /* look if there is space in the cache */
      if (pbuf->write_cache_size) {
         status = bm_lock_buffer_write_cache(pbuf);

         if (status != BM_SUCCESS)
            return status;
         
         if (pbuf->write_cache_size) {
            int status = BM_SUCCESS;

            /* if this event does not fit into the write cache, flush the write cache */
            if (pbuf->write_cache_wp > 0 && pbuf->write_cache_wp + total_size > pbuf->write_cache_size) {
               //printf("bm_send_event: write %d/%d but cache is full, size %d, wp %d\n", (int)event_size, (int)total_size, int(pbuf->write_cache_size), int(pbuf->write_cache_wp));

               bm_lock_buffer_guard pbuf_guard(pbuf);

               if (!pbuf_guard.is_locked()) {
                  pbuf->write_cache_mutex.unlock();
                  return pbuf_guard.get_status();
               }

               status = bm_flush_cache_locked(pbuf_guard, timeout_msec);

               if (pbuf_guard.is_locked()) {
                  // check if bm_wait_for_free_space() failed to relock the buffer
                  pbuf_guard.unlock();
               }

               if (status != BM_SUCCESS) {
                  pbuf->write_cache_mutex.unlock();
                  // bm_flush_cache() failed: timeout in bm_wait_for_free_space() or write cache size is bigger than buffer size or buffer was closed.
                  if (status == BM_NO_MEMORY)
                     cm_msg(MERROR, "bm_send_event", "write cache size is bigger than buffer size");
                  return status;
               }

               // write cache must be empty here
               assert(pbuf->write_cache_wp == 0);
            }

            /* write this event into the write cache, if it fits */
            if (pbuf->write_cache_wp + total_size <= pbuf->write_cache_size) {
               //printf("bm_send_event: write %d/%d to cache size %d, wp %d\n", (int)event_size, (int)total_size, (int)pbuf->write_cache_size, (int)pbuf->write_cache_wp);
               
               char* wptr = pbuf->write_cache + pbuf->write_cache_wp;
               
               for (int i=0; i<sg_n; i++) {
                  memcpy(wptr, sg_ptr[i], sg_len[i]);
                  wptr += sg_len[i];
               }

               pbuf->write_cache_wp += total_size;

               pbuf->write_cache_mutex.unlock();
               return BM_SUCCESS;
            }
         }

         /* event did not fit into the write cache, send it directly to shared memory */
         pbuf->write_cache_mutex.unlock();
      }

      /* we come here only for events that are too big to fit into the cache */

      /* lock the buffer */
      bm_lock_buffer_guard pbuf_guard(pbuf);

      if (!pbuf_guard.is_locked()) {
         return pbuf_guard.get_status();
      }

      /* calculate some shorthands */
      BUFFER_HEADER *pheader = pbuf->buffer_header;

#if 0
      status = bm_validate_buffer_locked(pbuf);
      if (status != BM_SUCCESS) {
         printf("bm_send_event: corrupted 111!\n");
         abort();
      }
#endif

      /* check if buffer is large enough */
      if (total_size >= (size_t)pheader->size) {
         pbuf_guard.unlock(); // unlock before cm_msg()
         cm_msg(MERROR, "bm_send_event", "total event size (%d) larger than size (%d) of buffer \'%s\'", (int)total_size, pheader->size, pheader->name);
         return BM_NO_MEMORY;
      }

      status = bm_wait_for_free_space_locked(pbuf_guard, timeout_msec, total_size, false);

      if (status != BM_SUCCESS) {
         // implicit unlock
         return status;
      }

#if 0
      status = bm_validate_buffer_locked(pbuf);
      if (status != BM_SUCCESS) {
         printf("bm_send_event: corrupted 222!\n");
         abort();
      }
#endif

      int old_write_pointer = pheader->write_pointer;

      bm_write_to_buffer_locked(pheader, sg_n, sg_ptr, sg_len, total_size);

      /* write pointer was incremented, but there should
       * always be some free space in the buffer and the
       * write pointer should never cacth up to the read pointer:
       * the rest of the code gets confused this happens (buffer 100% full)
       * as it is write_pointer == read_pointer can be either
       * 100% full or 100% empty. My solution: never fill
       * the buffer to 100% */
      assert(pheader->write_pointer != pheader->read_pointer);

      /* send wake up messages to all clients that want this event */
      int i;
      for (i = 0; i < pheader->max_client_index; i++) {
         BUFFER_CLIENT *pc = pheader->client + i;
         int request_id = bm_find_first_request_locked(pc, pevent);
         bm_notify_reader_locked(pheader, pc, old_write_pointer, request_id);
      }

#if 0
      status = bm_validate_buffer_locked(pbuf);
      if (status != BM_SUCCESS) {
         printf("bm_send_event: corrupted 333!\n");
         abort();
      }
#endif

      /* update statistics */
      pheader->num_in_events++;
      pbuf->count_sent += 1;
      pbuf->bytes_sent += total_size;
   }
#endif                          /* LOCAL_ROUTINES */

   return BM_SUCCESS;
}

static int bm_flush_cache_rpc(int buffer_handle, int timeout_msec)
{
   //printf("bm_flush_cache_rpc: handle %d, timeout %d\n", buffer_handle, timeout_msec);

   DWORD time_start = ss_millitime();
   DWORD time_end = time_start + timeout_msec;
   
   int xtimeout_msec = timeout_msec;

   while (1) {
      if (timeout_msec == BM_WAIT) {
         xtimeout_msec = 1000;
      } else if (timeout_msec == BM_NO_WAIT) {
         xtimeout_msec = BM_NO_WAIT;
      } else {
         if (xtimeout_msec > 1000) {
            xtimeout_msec = 1000;
         }
      }

      int status = rpc_call(RPC_BM_FLUSH_CACHE, buffer_handle, xtimeout_msec);
      
      //printf("bm_flush_cache_rpc: handle %d, timeout %d, status %d\n", buffer_handle, xtimeout_msec, status);

      if (status == BM_ASYNC_RETURN) {
         if (timeout_msec == BM_WAIT) {
            // BM_WAIT means wait forever
            continue;
         } else if (timeout_msec == BM_NO_WAIT) {
            // BM_NO_WAIT means do not wait
            return status;
         } else {
            DWORD now = ss_millitime();
            if (now >= time_end) {
               // timeout, return BM_ASYNC_RETURN
               return status;
            }

            DWORD remain = time_end - now;

            if (remain < (DWORD)xtimeout_msec) {
               xtimeout_msec = remain;
            }

            // keep asking for event...
            continue;
         }
      } else if (status == BM_SUCCESS) {
         // success, return BM_SUCCESS
         return status;
      } else {
         // error
         return status;
      }
   }
}

/********************************************************************/
/**
Empty write cache.
This function should be used if events in the write cache
should be visible to the consumers immediately. It should be called at the
end of each run, otherwise events could be kept in the write buffer and will
flow to the data of the next run.
@param buffer_handle Buffer handle obtained via bm_open_buffer() or 0 to flush data in the mserver event socket
@param timeout_msec Timeout waiting for free space in the event buffer.
If BM_WAIT, wait forever. If BM_NO_WAIT, the function returns
immediately with a value of BM_ASYNC_RETURN without writing the cache.
@return BM_SUCCESS, BM_INVALID_HANDLE<br>
BM_ASYNC_RETURN Routine called with async_flag == BM_NO_WAIT
and buffer has not enough space to receive cache<br>
BM_NO_MEMORY Event is too large for network buffer or event buffer.
One has to increase the event buffer size "/Experiment/Buffer sizes/SYSTEM"
and/or /Experiment/MAX_EVENT_SIZE in ODB.
*/
#ifdef LOCAL_ROUTINES
static INT bm_flush_cache_locked(bm_lock_buffer_guard& pbuf_guard, int timeout_msec)
{
   // NB we come here with write cache locked and buffer locked.

   {
      INT status = 0;

      //printf("bm_flush_cache_locked!\n");

      BUFFER* pbuf = pbuf_guard.get_pbuf();
      BUFFER_HEADER* pheader = pbuf->buffer_header;

      //printf("bm_flush_cache_locked: buffer %s, cache rp %zu, wp %zu, timeout %d msec\n", pbuf->buffer_name, pbuf->write_cache_rp, pbuf->write_cache_wp, timeout_msec);

      int old_write_pointer = pheader->write_pointer;

      int request_id[MAX_CLIENTS];
      for (int i = 0; i < pheader->max_client_index; i++) {
         request_id[i] = -1;
      }
         
      size_t ask_rp = pbuf->write_cache_rp;
      size_t ask_wp = pbuf->write_cache_wp;

      if (ask_wp == 0) { // nothing to do
         return BM_SUCCESS;
      }

      if (ask_rp == ask_wp) { // nothing to do
         return BM_SUCCESS;
      }

      assert(ask_rp < ask_wp);

      size_t ask_free = ALIGN8(ask_wp - ask_rp);

      if (ask_free == 0) { // nothing to do
         return BM_SUCCESS;
      }

#if 0
      status = bm_validate_buffer_locked(pbuf);
      if (status != BM_SUCCESS) {
         printf("bm_flush_cache: corrupted 111!\n");
         abort();
      }
#endif

      status = bm_wait_for_free_space_locked(pbuf_guard, timeout_msec, ask_free, true);

      if (status != BM_SUCCESS) {
         return status;
      }

      // NB: ask_rp, ask_wp and ask_free are invalid after calling bm_wait_for_free_space():
      //
      // wait_for_free_space() will sleep with all locks released,
      // during this time, another thread may call bm_send_event() that will
      // add one or more events to the write cache and after wait_for_free_space()
      // returns, size of data in cache will be bigger than the amount
      // of free space we requested. so we need to keep track of how
      // much data we write to the buffer and ask for more data
      // if we run short. This is the reason for the big loop
      // around wait_for_free_space(). We ask for slightly too little free
      // space to make sure all this code is always used and does work. K.O.

      if (pbuf->write_cache_wp == 0) {
         /* somebody emptied the cache while we were inside bm_wait_for_free_space */
         return BM_SUCCESS;
      }

      //size_t written = 0;
      while (pbuf->write_cache_rp < pbuf->write_cache_wp) {
         /* loop over all events in cache */

         const EVENT_HEADER *pevent = (const EVENT_HEADER *) (pbuf->write_cache + pbuf->write_cache_rp);
         size_t event_size = (pevent->data_size + sizeof(EVENT_HEADER));
         size_t total_size = ALIGN8(event_size);

#if 0
         printf("bm_flush_cache: cache size %d, wp %d, rp %d, event data_size %d, event_size %d, total_size %d, free %d, written %d\n",
                int(pbuf->write_cache_size),
                int(pbuf->write_cache_wp),
                int(pbuf->write_cache_rp),
                int(pevent->data_size),
                int(event_size),
                int(total_size),
                int(ask_free),
                int(written));
#endif

         // check for crazy event size
         assert(total_size >= sizeof(EVENT_HEADER));
         assert(total_size <= (size_t)pheader->size);

         bm_write_to_buffer_locked(pheader, 1, (char**)&pevent, &event_size, total_size);

         /* update statistics */
         pheader->num_in_events++;
         pbuf->count_sent += 1;
         pbuf->bytes_sent += total_size;

         /* see comment for the same code in bm_send_event().
          * We make sure the buffer is never 100% full */
         assert(pheader->write_pointer != pheader->read_pointer);

         /* check if anybody has a request for this event */
         for (int i = 0; i < pheader->max_client_index; i++) {
            BUFFER_CLIENT *pc = pheader->client + i;
            int r = bm_find_first_request_locked(pc, pevent);
            if (r >= 0) {
               request_id[i] = r;
            }
         }
            
         /* this loop does not loop forever because rp
          * is monotonously incremented here. write_cache_wp does
          * not change */

         pbuf->write_cache_rp += total_size;
         //written += total_size;

         assert(pbuf->write_cache_rp > 0);
         assert(pbuf->write_cache_rp <= pbuf->write_cache_size);
         assert(pbuf->write_cache_rp <= pbuf->write_cache_wp);
      }

      /* the write cache is now empty */
      assert(pbuf->write_cache_wp == pbuf->write_cache_rp);
      pbuf->write_cache_wp = 0;
      pbuf->write_cache_rp = 0;

      /* check which clients are waiting */
      for (int i = 0; i < pheader->max_client_index; i++) {
         BUFFER_CLIENT *pc = pheader->client + i;
         bm_notify_reader_locked(pheader, pc, old_write_pointer, request_id[i]);
      }
   }

   return BM_SUCCESS;
}

#endif /* LOCAL_ROUTINES */

INT bm_flush_cache(int buffer_handle, int timeout_msec)
{
   if (rpc_is_remote()) {
      return bm_flush_cache_rpc(buffer_handle, timeout_msec);
   }

#ifdef LOCAL_ROUTINES
   {
      INT status = 0;

      //printf("bm_flush_cache!\n");

      BUFFER *pbuf = bm_get_buffer("bm_flush_cache", buffer_handle, &status);

      if (!pbuf)
         return status;

      if (pbuf->write_cache_size == 0)
         return BM_SUCCESS;

      status = bm_lock_buffer_write_cache(pbuf);

      if (status != BM_SUCCESS)
         return status;

      /* check if anything needs to be flushed */
      if (pbuf->write_cache_wp == 0) {
         pbuf->write_cache_mutex.unlock();
         return BM_SUCCESS;
      }

      /* lock the buffer */
      bm_lock_buffer_guard pbuf_guard(pbuf);

      if (!pbuf_guard.is_locked())
         return pbuf_guard.get_status();

      status = bm_flush_cache_locked(pbuf_guard, timeout_msec);

      /* unlock in correct order */

      if (pbuf_guard.is_locked()) {
         // check if bm_wait_for_free_space() failed to relock the buffer
         pbuf_guard.unlock();
      }

      pbuf->write_cache_mutex.unlock();

      return status;
   }
#endif                          /* LOCAL_ROUTINES */

   return BM_SUCCESS;
}

#ifdef LOCAL_ROUTINES

static INT bm_read_buffer(BUFFER *pbuf, INT buffer_handle, void **bufptr, void *buf, INT *buf_size, std::vector<char> *vecptr, int timeout_msec, int convert_flags, BOOL dispatch) {
   INT status = BM_SUCCESS;

   int max_size = 0;
   if (buf_size) {
      max_size = *buf_size;
      *buf_size = 0;
   }

   //printf("bm_read_buffer: [%s] timeout %d, conv %d, ptr %p, buf %p, disp %d\n", pbuf->buffer_name, timeout_msec, convert_flags, bufptr, buf, dispatch);

   bm_lock_buffer_guard pbuf_guard(pbuf, true); // buffer is not locked

   // NB: locking order is: 1st read cache lock, 2nd buffer lock, unlock in reverse order

   /* look if there is anything in the cache */
   if (pbuf->read_cache_size > 0) {

      status = bm_lock_buffer_read_cache(pbuf);

      if (status != BM_SUCCESS)
         return status;

      if (pbuf->read_cache_wp == 0) {

         // lock buffer for the first time

         if (!pbuf_guard.relock()) {
            pbuf->read_cache_mutex.unlock();
            return pbuf_guard.get_status();
         }

         status = bm_fill_read_cache_locked(pbuf_guard, timeout_msec);
         if (status != BM_SUCCESS) {
            // unlock in correct order
            if (pbuf_guard.is_locked()) {
               // check if bm_wait_for_more_events() failed to relock the buffer
               pbuf_guard.unlock();
            }
            pbuf->read_cache_mutex.unlock();
            return status;
         }

         // buffer remains locked here
      }
      EVENT_HEADER *pevent;
      int event_size;
      int total_size;
      if (bm_peek_read_cache_locked(pbuf, &pevent, &event_size, &total_size)) {
         if (pbuf_guard.is_locked()) {
            // do not need to keep the event buffer locked
            // when reading from the read cache
            pbuf_guard.unlock();
         }
         //printf("bm_read_buffer: [%s] async %d, conv %d, ptr %p, buf %p, disp %d, total_size %d, read from cache %d %d %d\n", pbuf->buffer_name, async_flag, convert_flags, bufptr, buf, dispatch, total_size, pbuf->read_cache_size, pbuf->read_cache_rp, pbuf->read_cache_wp);
         status = BM_SUCCESS;
         if (buf) {
            if (event_size > max_size) {
               cm_msg(MERROR, "bm_read_buffer", "buffer size %d is smaller than event size %d, event truncated. buffer \"%s\"", max_size, event_size, pbuf->buffer_name);
               event_size = max_size;
               status = BM_TRUNCATED;
            }

            memcpy(buf, pevent, event_size);

            if (buf_size) {
               *buf_size = event_size;
            }
            if (convert_flags) {
               bm_convert_event_header((EVENT_HEADER *) buf, convert_flags);
            }
         } else if (bufptr) {
            *bufptr = malloc(event_size);
            memcpy(*bufptr, pevent, event_size);
            status = BM_SUCCESS;
         } else if (vecptr) {
            vecptr->resize(0);
            char* cptr = (char*)pevent;
            vecptr->assign(cptr, cptr+event_size);
         }
         bm_incr_read_cache_locked(pbuf, total_size);
         pbuf->read_cache_mutex.unlock();
         if (dispatch) {
            // FIXME need to protect currently dispatched event against
            // another thread overwriting it by refilling the read cache
            bm_dispatch_event(buffer_handle, pevent);
            return BM_MORE_EVENTS;
         }
         // buffer is unlocked here
         return status;
      }
      pbuf->read_cache_mutex.unlock();
   }

   /* we come here if the read cache is disabled */
   /* we come here if the next event is too big to fit into the read cache */

   if (!pbuf_guard.is_locked()) {
      if (!pbuf_guard.relock())
         return pbuf_guard.get_status();
   }

   EVENT_HEADER *event_buffer = NULL;

   BUFFER_HEADER *pheader = pbuf->buffer_header;

   BUFFER_CLIENT *pc = bm_get_my_client(pbuf, pheader);

   while (1) {
      /* loop over events in the event buffer */

      status = bm_wait_for_more_events_locked(pbuf_guard, pc, timeout_msec, FALSE);

      if (status != BM_SUCCESS) {
         // implicit unlock
         return status;
      }

      /* check if event at current read pointer matches a request */

      EVENT_HEADER *pevent;
      int event_size;
      int total_size;

      status = bm_peek_buffer_locked(pbuf, pheader, pc, &pevent, &event_size, &total_size);
      if (status == BM_CORRUPTED) {
         // implicit unlock
         return status;
      } else if (status != BM_SUCCESS) {
         /* event buffer is empty */
         break;
      }

      BOOL is_requested = bm_check_requests(pc, pevent);

      if (is_requested) {
         //printf("bm_read_buffer: [%s] async %d, conv %d, ptr %p, buf %p, disp %d, total_size %d, read from buffer, cache %d %d %d\n", pheader->name, async_flag, convert_flags, bufptr, buf, dispatch, total_size, pbuf->read_cache_size, pbuf->read_cache_rp, pbuf->read_cache_wp);

         status = BM_SUCCESS;

         if (buf) {
            if (event_size > max_size) {
               cm_msg(MERROR, "bm_read_buffer",
                      "buffer size %d is smaller than event size %d, event truncated. buffer \"%s\"", max_size,
                      event_size, pheader->name);
               event_size = max_size;
               status = BM_TRUNCATED;
            }

            bm_read_from_buffer_locked(pheader, pc->read_pointer, (char *) buf, event_size);

            if (buf_size) {
               *buf_size = event_size;
            }

            if (convert_flags) {
               bm_convert_event_header((EVENT_HEADER *) buf, convert_flags);
            }

            pbuf->count_read++;
            pbuf->bytes_read += event_size;
         } else if (dispatch || bufptr) {
            assert(event_buffer == NULL); // make sure we only come here once
            event_buffer = (EVENT_HEADER *) malloc(event_size);
            bm_read_from_buffer_locked(pheader, pc->read_pointer, (char *) event_buffer, event_size);
            pbuf->count_read++;
            pbuf->bytes_read += event_size;
         } else if (vecptr) {
            bm_read_from_buffer_locked(pheader, pc->read_pointer, vecptr, event_size);
            pbuf->count_read++;
            pbuf->bytes_read += event_size;
         }

         int new_read_pointer = bm_incr_rp_no_check(pheader, pc->read_pointer, total_size);
         pc->read_pointer = new_read_pointer;

         pheader->num_out_events++;
         /* exit loop over events */
         break;
      }

      int new_read_pointer = bm_incr_rp_no_check(pheader, pc->read_pointer, total_size);
      pc->read_pointer = new_read_pointer;
      pheader->num_out_events++;
   }

   /*
     If read pointer has been changed, it may have freed up some space
     for waiting producers. So check if free space is now more than 50%
     of the buffer size and wake waiting producers.
   */

   bm_wakeup_producers_locked(pheader, pc);

   pbuf_guard.unlock();

   if (dispatch && event_buffer) {
      bm_dispatch_event(buffer_handle, event_buffer);
      free(event_buffer);
      event_buffer = NULL;
      return BM_MORE_EVENTS;
   }

   if (bufptr && event_buffer) {
      *bufptr = event_buffer;
      event_buffer = NULL;
      status = BM_SUCCESS;
   }

   if (event_buffer) {
      free(event_buffer);
      event_buffer = NULL;
   }

   return status;
}

#endif

static INT bm_receive_event_rpc(INT buffer_handle, void *buf, int *buf_size, EVENT_HEADER** ppevent, std::vector<char>* pvec, int timeout_msec)
{
   //printf("bm_receive_event_rpc: handle %d, buf %p, pevent %p, pvec %p, timeout %d, max_event_size %d\n", buffer_handle, buf, ppevent, pvec, timeout_msec, _bm_max_event_size);

   assert(_bm_max_event_size > sizeof(EVENT_HEADER));

   void *xbuf = NULL;
   int xbuf_size = 0;

   if (buf) {
      xbuf = buf;
      xbuf_size = *buf_size;
   } else if (ppevent) {
      *ppevent = (EVENT_HEADER*)malloc(_bm_max_event_size);
      xbuf_size = _bm_max_event_size;
   } else if (pvec) {
      pvec->resize(_bm_max_event_size);
      xbuf = pvec->data();
      xbuf_size = pvec->size();
   } else {
      assert(!"incorrect call to bm_receivent_event_rpc()");
   }

   int status;
   DWORD time_start = ss_millitime();
   DWORD time_end = time_start + timeout_msec;
   
   int xtimeout_msec = timeout_msec;

   int zbuf_size = xbuf_size;

   while (1) {
      if (timeout_msec == BM_WAIT) {
         xtimeout_msec = 1000;
      } else if (timeout_msec == BM_NO_WAIT) {
         xtimeout_msec = BM_NO_WAIT;
      } else {
         if (xtimeout_msec > 1000) {
            xtimeout_msec = 1000;
         }
      }

      zbuf_size = xbuf_size;

      status = rpc_call(RPC_BM_RECEIVE_EVENT, buffer_handle, xbuf, &zbuf_size, xtimeout_msec);
      
      //printf("bm_receive_event_rpc: handle %d, timeout %d, status %d, size %d in, %d out, via RPC_BM_RECEIVE_EVENT\n", buffer_handle, xtimeout_msec, status, xbuf_size, zbuf_size);

      if (status == BM_ASYNC_RETURN) {
         if (timeout_msec == BM_WAIT) {
            // BM_WAIT means wait forever
            continue;
         } else if (timeout_msec == BM_NO_WAIT) {
            // BM_NO_WAIT means do not wait
            break;
         } else {
            DWORD now = ss_millitime();
            if (now >= time_end) {
               // timeout, return BM_ASYNC_RETURN
               break;
            }

            DWORD remain = time_end - now;

            if (remain < (DWORD)xtimeout_msec) {
               xtimeout_msec = remain;
            }

            // keep asking for event...
            continue;
         }
      } else if (status == BM_SUCCESS) {
         // success, return BM_SUCCESS
         break;
      }

      // RPC error
         
      if (buf) {
         *buf_size = 0;
      } else if (ppevent) {
         free(*ppevent);
         *ppevent = NULL;
      } else if (pvec) {
         pvec->resize(0);
      } else {
         assert(!"incorrect call to bm_receivent_event_rpc()");
      }
      
      return status;
   }

   // status is BM_SUCCESS or BM_ASYNC_RETURN

   if (buf) {
      *buf_size = zbuf_size;
   } else if (ppevent) {
      // nothing to do
      // ppevent = realloc(ppevent, xbuf_size); // shrink memory allocation
   } else if (pvec) {
      pvec->resize(zbuf_size);
   } else {
      assert(!"incorrect call to bm_receivent_event_rpc()");
   }

   return status;
}

/********************************************************************/
/**
Receives events directly.
This function is an alternative way to receive events without
a main loop.

It can be used in analysis systems which actively receive events,
rather than using callbacks. A analysis package could for example contain its own
command line interface. A command
like "receive 1000 events" could make it necessary to call bm_receive_event()
1000 times in a row to receive these events and then return back to the
command line prompt.
The according bm_request_event() call contains NULL as the
callback routine to indicate that bm_receive_event() is called to receive
events.
\code
#include <stdio.h>
#include "midas.h"
void process_event(EVENT_HEADER *pheader)
{
 printf("Received event #%d\r",
 pheader->serial_number);
}
main()
{
  INT status, request_id;
  HNDLE hbuf;
  char event_buffer[1000];
  status = cm_connect_experiment("", "Sample",
  "Simple Analyzer", NULL);
  if (status != CM_SUCCESS)
   return 1;
  bm_open_buffer(EVENT_BUFFER_NAME, DEFAULT_BUFFER_SIZE, &hbuf);
  bm_request_event(hbuf, 1, TRIGGER_ALL, GET_ALL, request_id, NULL);

  do
  {
   size = sizeof(event_buffer);
   status = bm_receive_event(hbuf, event_buffer, &size, BM_NO_WAIT);
  if (status == CM_SUCCESS)
   process_event((EVENT_HEADER *) event_buffer);
   <...do something else...>
   status = cm_yield(0);
  } while (status != RPC_SHUTDOWN &&
  status != SS_ABORT);
  cm_disconnect_experiment();
  return 0;
}
\endcode
@param buffer_handle buffer handle
@param destination destination address where event is written to
@param buf_size size of destination buffer on input, size of event plus
header on return.
@param timeout_msec Wait so many millisecond for new data. Special values: BM_WAIT: wait forever, BM_NO_WAIT: do not wait, return BM_ASYNC_RETURN if no data is immediately available
@return BM_SUCCESS, BM_INVALID_HANDLE <br>
BM_TRUNCATED   The event is larger than the destination buffer and was
               therefore truncated <br>
BM_ASYNC_RETURN No event available
*/
INT bm_receive_event(INT buffer_handle, void *destination, INT *buf_size, int timeout_msec) {
   //printf("bm_receive_event: handle %d, async %d\n", buffer_handle, async_flag);
   if (rpc_is_remote()) {
      return bm_receive_event_rpc(buffer_handle, destination, buf_size, NULL, NULL, timeout_msec);
   }
#ifdef LOCAL_ROUTINES
   {
      INT status = BM_SUCCESS;

      BUFFER *pbuf = bm_get_buffer("bm_receive_event", buffer_handle, &status);

      if (!pbuf)
         return status;

      int convert_flags = rpc_get_convert_flags();

      status = bm_read_buffer(pbuf, buffer_handle, NULL, destination, buf_size, NULL, timeout_msec, convert_flags, FALSE);
      //printf("bm_receive_event: handle %d, async %d, status %d, size %d\n", buffer_handle, async_flag, status, *buf_size);
      return status;
   }
#else                           /* LOCAL_ROUTINES */

   return BM_SUCCESS;
#endif
}

/********************************************************************/
/**
Receives events directly.
This function is an alternative way to receive events without
a main loop.

It can be used in analysis systems which actively receive events,
rather than using callbacks. A analysis package could for example contain its own
command line interface. A command
like "receive 1000 events" could make it necessary to call bm_receive_event()
1000 times in a row to receive these events and then return back to the
command line prompt.
The according bm_request_event() call contains NULL as the
callback routine to indicate that bm_receive_event() is called to receive
events.
\code
#include <stdio.h>
#include "midas.h"
void process_event(EVENT_HEADER *pheader)
{
 printf("Received event #%d\r",
 pheader->serial_number);
}
main()
{
  INT status, request_id;
  HNDLE hbuf;
  char event_buffer[1000];
  status = cm_connect_experiment("", "Sample",
  "Simple Analyzer", NULL);
  if (status != CM_SUCCESS)
   return 1;
  bm_open_buffer(EVENT_BUFFER_NAME, DEFAULT_BUFFER_SIZE, &hbuf);
  bm_request_event(hbuf, 1, TRIGGER_ALL, GET_ALL, request_id, NULL);

  do
  {
   size = sizeof(event_buffer);
   status = bm_receive_event(hbuf, event_buffer, &size, BM_NO_WAIT);
  if (status == CM_SUCCESS)
   process_event((EVENT_HEADER *) event_buffer);
   <...do something else...>
   status = cm_yield(0);
  } while (status != RPC_SHUTDOWN &&
  status != SS_ABORT);
  cm_disconnect_experiment();
  return 0;
}
\endcode
@param buffer_handle buffer handle
@param ppevent pointer to the received event pointer, event pointer should be free()ed to avoid memory leak
@param timeout_msec Wait so many millisecond for new data. Special values: BM_WAIT: wait forever, BM_NO_WAIT: do not wait, return BM_ASYNC_RETURN if no data is immediately available
@return BM_SUCCESS, BM_INVALID_HANDLE <br>
BM_ASYNC_RETURN No event available
*/
INT bm_receive_event_alloc(INT buffer_handle, EVENT_HEADER **ppevent, int timeout_msec) {
   if (rpc_is_remote()) {
      return bm_receive_event_rpc(buffer_handle, NULL, NULL, ppevent, NULL, timeout_msec);
   }
#ifdef LOCAL_ROUTINES
   {
      INT status = BM_SUCCESS;

      BUFFER *pbuf = bm_get_buffer("bm_receive_event_alloc", buffer_handle, &status);

      if (!pbuf)
         return status;

      int convert_flags = rpc_get_convert_flags();

      return bm_read_buffer(pbuf, buffer_handle, (void **) ppevent, NULL, NULL, NULL, timeout_msec, convert_flags, FALSE);
   }
#else                           /* LOCAL_ROUTINES */

   return BM_SUCCESS;
#endif
}

/********************************************************************/
/**
Receives events directly.
This function is an alternative way to receive events without
a main loop.

It can be used in analysis systems which actively receive events,
rather than using callbacks. A analysis package could for example contain its own
command line interface. A command
like "receive 1000 events" could make it necessary to call bm_receive_event()
1000 times in a row to receive these events and then return back to the
command line prompt.
The according bm_request_event() call contains NULL as the
callback routine to indicate that bm_receive_event() is called to receive
events.
\code
#include <stdio.h>
#include "midas.h"
void process_event(EVENT_HEADER *pheader)
{
 printf("Received event #%d\r",
 pheader->serial_number);
}
main()
{
  INT status, request_id;
  HNDLE hbuf;
  char event_buffer[1000];
  status = cm_connect_experiment("", "Sample",
  "Simple Analyzer", NULL);
  if (status != CM_SUCCESS)
   return 1;
  bm_open_buffer(EVENT_BUFFER_NAME, DEFAULT_BUFFER_SIZE, &hbuf);
  bm_request_event(hbuf, 1, TRIGGER_ALL, GET_ALL, request_id, NULL);

  do
  {
   size = sizeof(event_buffer);
   status = bm_receive_event(hbuf, event_buffer, &size, BM_NO_WAIT);
  if (status == CM_SUCCESS)
   process_event((EVENT_HEADER *) event_buffer);
   <...do something else...>
   status = cm_yield(0);
  } while (status != RPC_SHUTDOWN &&
  status != SS_ABORT);
  cm_disconnect_experiment();
  return 0;
}
\endcode
@param buffer_handle buffer handle
@param ppevent pointer to the received event pointer, event pointer should be free()ed to avoid memory leak
@param timeout_msec Wait so many millisecond for new data. Special values: BM_WAIT: wait forever, BM_NO_WAIT: do not wait, return BM_ASYNC_RETURN if no data is immediately available
@return BM_SUCCESS, BM_INVALID_HANDLE <br>
BM_ASYNC_RETURN No event available
*/
INT bm_receive_event_vec(INT buffer_handle, std::vector<char> *pvec, int timeout_msec) {
   if (rpc_is_remote()) {
      return bm_receive_event_rpc(buffer_handle, NULL, NULL, NULL, pvec, timeout_msec);
   }
#ifdef LOCAL_ROUTINES
   {
      INT status = BM_SUCCESS;

      BUFFER *pbuf = bm_get_buffer("bm_receive_event_vec", buffer_handle, &status);

      if (!pbuf)
         return status;

      int convert_flags = rpc_get_convert_flags();

      return bm_read_buffer(pbuf, buffer_handle, NULL, NULL, NULL, pvec, timeout_msec, convert_flags, FALSE);
   }
#else /* LOCAL_ROUTINES */
   return BM_SUCCESS;
#endif
}

#ifdef LOCAL_ROUTINES

static int bm_skip_event(BUFFER* pbuf)
{
   /* clear read cache */
   if (pbuf->read_cache_size > 0) {

      int status = bm_lock_buffer_read_cache(pbuf);

      if (status != BM_SUCCESS)
         return status;

      pbuf->read_cache_rp = 0;
      pbuf->read_cache_wp = 0;

      pbuf->read_cache_mutex.unlock();
   }
   
   bm_lock_buffer_guard pbuf_guard(pbuf);

   if (!pbuf_guard.is_locked())
      return pbuf_guard.get_status();
   
   BUFFER_HEADER *pheader = pbuf->buffer_header;
   
   /* forward read pointer to global write pointer */
   BUFFER_CLIENT *pclient = bm_get_my_client(pbuf, pheader);
   pclient->read_pointer = pheader->write_pointer;
   
   return BM_SUCCESS;
}

#endif /* LOCAL_ROUTINES */

/********************************************************************/
/**
Skip all events in current buffer.

Useful for single event displays to see the newest events
@param buffer_handle      Handle of the buffer. Must be obtained
                          via bm_open_buffer.
@return BM_SUCCESS, BM_INVALID_HANDLE, RPC_NET_ERROR
*/
INT bm_skip_event(INT buffer_handle) {
   if (rpc_is_remote())
      return rpc_call(RPC_BM_SKIP_EVENT, buffer_handle);

#ifdef LOCAL_ROUTINES
   {
      int status = 0;

      BUFFER *pbuf = bm_get_buffer("bm_skip_event", buffer_handle, &status);

      if (!pbuf)
         return status;

      return bm_skip_event(pbuf);
   }
#endif

   return BM_SUCCESS;
}

#ifdef LOCAL_ROUTINES
/********************************************************************/
/**
Check a buffer if an event is available and call the dispatch function if found.
@param buffer_name       Name of buffer
@return BM_SUCCESS, BM_INVALID_HANDLE, BM_TRUNCATED, BM_ASYNC_RETURN,
                    RPC_NET_ERROR
*/
static INT bm_push_buffer(BUFFER *pbuf, int buffer_handle) {
   //printf("bm_push_buffer: buffer [%s], handle %d, callback %d\n", pbuf->buffer_header->name, buffer_handle, pbuf->callback);

   /* return immediately if no callback routine is defined */
   if (!pbuf->callback)
      return BM_SUCCESS;

   return bm_read_buffer(pbuf, buffer_handle, NULL, NULL, NULL, NULL, BM_NO_WAIT, 0, TRUE);
}

/********************************************************************/
/**
Check a buffer if an event is available and call the dispatch function if found.
@param buffer_name       Name of buffer
@return BM_SUCCESS, BM_INVALID_HANDLE, BM_TRUNCATED, BM_ASYNC_RETURN, BM_CORRUPTED, RPC_NET_ERROR
*/
static INT bm_push_event(const char *buffer_name)
{
   std::vector<BUFFER*> mybuffers;
   
   gBuffersMutex.lock();
   mybuffers = gBuffers;
   gBuffersMutex.unlock();
   
   for (size_t i = 0; i < mybuffers.size(); i++) {
      BUFFER *pbuf = mybuffers[i];
      if (!pbuf || !pbuf->attached)
         continue;
      // FIXME: unlocked read access to pbuf->buffer_name!
      if (strcmp(buffer_name, pbuf->buffer_name) == 0) {
         return bm_push_buffer(pbuf, i + 1);
      }
   }

   return BM_INVALID_HANDLE;
}

#else

static INT bm_push_event(const char *buffer_name)
{
   return BM_SUCCESS;                                                  
}

#endif /* LOCAL_ROUTINES */

/********************************************************************/
/**
Check if any requested event is waiting in a buffer
@return TRUE             More events are waiting<br>
        FALSE            No more events are waiting
*/
INT bm_check_buffers() {
#ifdef LOCAL_ROUTINES
   {
      INT status = 0;
      BOOL bMore;
      DWORD start_time;
      //static DWORD last_time = 0;

      /* if running as a server, buffer checking is done by client
         via ASYNC bm_receive_event */
      if (rpc_is_mserver()) {
         return FALSE;
      }

      bMore = FALSE;
      start_time = ss_millitime();

      std::vector<BUFFER*> mybuffers;
      
      gBuffersMutex.lock();
      mybuffers = gBuffers;
      gBuffersMutex.unlock();

      /* go through all buffers */
      for (size_t idx = 0; idx < mybuffers.size(); idx++) {
         BUFFER* pbuf = mybuffers[idx];

         if (!pbuf || !pbuf->attached)
            continue;

         int count_loops = 0;
         while (1) {
            if (pbuf->attached) {
               /* one bm_push_event could cause a run stop and a buffer close, which
                * would crash the next call to bm_push_event(). So check for valid
                * buffer on each call */

               /* this is what happens:
                * bm_push_buffer() may call a user callback function
                * user callback function may indirectly call bm_close() of this buffer,
                * i.e. if it stops the run,
                * bm_close() will set pbuf->attached to false, but will not delete pbuf or touch gBuffers
                * here we will see pbuf->attched is false and quit this loop
                */

               status = bm_push_buffer(pbuf, idx + 1);

               if (status == BM_CORRUPTED) {
                  return status;
               }

               //printf("bm_check_buffers: bm_push_buffer() returned %d, loop %d, time %d\n", status, count_loops, ss_millitime() - start_time);

               if (status != BM_MORE_EVENTS) {
                  //DWORD t = ss_millitime() - start_time;
                  //printf("bm_check_buffers: index %d, period %d, elapsed %d, loop %d, no more events\n", idx, start_time - last_time, t, count_loops);
                  break;
               }

               count_loops++;
            }

            // NB: this code has a logic error: if 2 buffers always have data,
            // this timeout will cause us to exit reading the 1st buffer
            // after 1000 msec, then we read the 2nd buffer exactly once,
            // and exit the loop because the timeout is still active -
            // we did not reset "start_time" when we started reading
            // from the 2nd buffer. Result is that we always read all
            // the data in a loop from the 1st buffer, but read just
            // one event from the 2nd buffer, resulting in severe unfairness.

            /* stop after one second */
            DWORD t = ss_millitime() - start_time;
            if (t > 1000) {
               //printf("bm_check_buffers: index %d, period %d, elapsed %d, loop %d, timeout.\n", idx, start_time - last_time, t, count_loops);
               bMore = TRUE;
               break;
            }
         }
      }

      //last_time = start_time;

      return bMore;

   }
#else                           /* LOCAL_ROUTINES */

   return FALSE;

#endif
}

/********************************************************************/
static INT bm_notify_client(const char *buffer_name, int client_socket)
/********************************************************************\

  Routine: bm_notify_client

  Purpose: Called by cm_dispatch_ipc. Send an event notification to
           the connected client. Used by mserver to relay the BM_MSG
           buffer message from local UDP socket to the remote connected client.

  Input:
    char  *buffer_name      Name of buffer
    int   client_socket     Network socket to client

  Output:
    none

  Function value:
    BM_SUCCESS              Successful completion

\********************************************************************/
{
   static DWORD last_time = 0;
   DWORD now = ss_millitime();

   //printf("bm_notify_client: buffer [%s], socket %d, time %d\n", buffer_name, client_socket, now - last_time);

   BUFFER* fbuf = NULL;

   gBuffersMutex.lock();

   for (size_t i = 0; i < gBuffers.size(); i++) {
      BUFFER* pbuf = gBuffers[i];
      if (!pbuf || !pbuf->attached)
         continue;
      if (strcmp(buffer_name, pbuf->buffer_header->name) == 0) {
         fbuf = pbuf;
         break;
      }
   }

   gBuffersMutex.unlock();

   if (!fbuf)
      return BM_INVALID_HANDLE;

   /* don't send notification if client has no callback defined
      to receive events -> client calls bm_receive_event manually */
   if (!fbuf->callback)
      return DB_SUCCESS;

   int convert_flags = rpc_get_convert_flags();

   /* only send notification once each 500ms */
   if (now - last_time < 500)
      return DB_SUCCESS;

   last_time = now;

   char buffer[32];
   NET_COMMAND *nc = (NET_COMMAND *) buffer;

   nc->header.routine_id = MSG_BM;
   nc->header.param_size = 0;

   if (convert_flags) {
      rpc_convert_single(&nc->header.routine_id, TID_UINT32, RPC_OUTGOING, convert_flags);
      rpc_convert_single(&nc->header.param_size, TID_UINT32, RPC_OUTGOING, convert_flags);
   }

   //printf("bm_notify_client: Sending MSG_BM! buffer [%s]\n", buffer_name);

   /* send the update notification to the client */
   send_tcp(client_socket, (char *) buffer, sizeof(NET_COMMAND_HEADER), 0);

   return BM_SUCCESS;
}

/********************************************************************/
INT bm_poll_event()
/********************************************************************\

  Routine: bm_poll_event

  Purpose: Poll an event from a remote server. Gets called by
           rpc_client_dispatch() and by cm_yield()

  Function value:
    BM_SUCCESS       At least one event was received and dispatched
    BM_ASYNC_RETURN  No events received
    SS_ABORT         Network connection broken

\********************************************************************/
{
   INT status;
   DWORD start_time;
   BOOL dispatched_something = FALSE;

   //printf("bm_poll_event!\n");

   start_time = ss_millitime();

   std::vector<char> vec;

   /* loop over all requests */
   int request_id;
   for (request_id = 0; request_id < _request_list_entries; request_id++) {
      /* continue if no dispatcher set (manual bm_receive_event) */
      if (_request_list[request_id].dispatcher == NULL)
         continue;

      do {
         /* receive event */
         status = bm_receive_event_vec(_request_list[request_id].buffer_handle, &vec, BM_NO_WAIT);

         //printf("bm_poll_event: request_id %d, buffer_handle %d, bm_receive_event(BM_NO_WAIT) status %d, vec size %d, capacity %d\n", request_id, _request_list[request_id].buffer_handle, status, (int)vec.size(), (int)vec.capacity());

         /* call user function if successful */
         if (status == BM_SUCCESS) {
            bm_dispatch_event(_request_list[request_id].buffer_handle, (EVENT_HEADER*)vec.data());
            dispatched_something = TRUE;
         }

         /* break if no more events */
         if (status == BM_ASYNC_RETURN)
            break;

         /* break if corrupted event buffer */
         if (status == BM_TRUNCATED) {
            cm_msg(MERROR, "bm_poll_event", "received event was truncated, buffer size %d is too small, see messages and increase /Experiment/MAX_EVENT_SIZE in ODB", (int)vec.size());
         }

         /* break if corrupted event buffer */
         if (status == BM_CORRUPTED)
            return SS_ABORT;

         /* break if server died */
         if (status == RPC_NET_ERROR) {
            return SS_ABORT;
         }

         /* stop after one second */
         if (ss_millitime() - start_time > 1000) {
            break;
         }

      } while (TRUE);
   }

   if (dispatched_something)
      return BM_SUCCESS;
   else
      return BM_ASYNC_RETURN;
}

/********************************************************************/
/**
Clears event buffer and cache.
If an event buffer is large and a consumer is slow in analyzing
events, events are usually received some time after they are produced.
This effect is even more experienced if a read cache is used
(via bm_set_cache_size()).
When changes to the hardware are made in the experience, the consumer will then
still analyze old events before any new event which reflects the hardware change.
Users can be fooled by looking at histograms which reflect the hardware change
many seconds after they have been made.

To overcome this potential problem, the analyzer can call
bm_empty_buffers() just after the hardware change has been made which
skips all old events contained in event buffers and read caches.
Technically this is done by forwarding the read pointer of the client.
No events are really deleted, they are still visible to other clients like
the logger.

Note that the front-end also contains write buffers which can delay the
delivery of events.
The standard front-end framework mfe.c reduces this effect by flushing
all buffers once every second.
@return BM_SUCCESS
*/
INT bm_empty_buffers() {
   if (rpc_is_remote())
      return rpc_call(RPC_BM_EMPTY_BUFFERS);

#ifdef LOCAL_ROUTINES
   {
      std::vector<BUFFER*> mybuffers;
      
      gBuffersMutex.lock();
      mybuffers = gBuffers;
      gBuffersMutex.unlock();

      /* go through all buffers */
      for (BUFFER* pbuf : mybuffers) {
         if (!pbuf)
            continue;
         if (!pbuf->attached)
            continue;

         int status = bm_skip_event(pbuf);
         if (status != BM_SUCCESS)
            return status;
      }
   }
#endif                          /* LOCAL_ROUTINES */

   return BM_SUCCESS;
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

#define MAX_DEFRAG_EVENTS 10

typedef struct {
   WORD event_id;
   DWORD data_size;
   DWORD received;
   EVENT_HEADER *pevent;
} EVENT_DEFRAG_BUFFER;

static EVENT_DEFRAG_BUFFER defrag_buffer[MAX_DEFRAG_EVENTS];

/********************************************************************/
static void bm_defragment_event(HNDLE buffer_handle, HNDLE request_id,
                                EVENT_HEADER *pevent, void *pdata,
                                EVENT_HANDLER *dispatcher)
/********************************************************************\

  Routine: bm_defragment_event

  Purpose: Called internally from the event receiving routines
           bm_push_event and bm_poll_event to recombine event
           fragments and call the user callback routine upon
           completion.

  Input:
    HNDLE buffer_handle  Handle for the buffer containing event
    HNDLE request_id     Handle for event request
    EVENT_HEADER *pevent Pointer to event header
    void *pata           Pointer to event data
    dispatcher()         User callback routine

  Output:
    <calls dispatcher() after successfull recombination of event>

  Function value:
    void

\********************************************************************/
{
   INT i;

   if ((uint16_t(pevent->event_id) & uint16_t(0xF000)) == uint16_t(EVENTID_FRAG1)) {
      /*---- start new event ----*/

      //printf("First Frag detected : Ser#:%d ID=0x%x \n", pevent->serial_number, pevent->event_id);

      /* check if fragments already stored */
      for (i = 0; i < MAX_DEFRAG_EVENTS; i++)
         if (defrag_buffer[i].event_id == (pevent->event_id & 0x0FFF))
            break;

      if (i < MAX_DEFRAG_EVENTS) {
         free(defrag_buffer[i].pevent);
         defrag_buffer[i].pevent = NULL;
         memset(&defrag_buffer[i].event_id, 0, sizeof(EVENT_DEFRAG_BUFFER));
         cm_msg(MERROR, "bm_defragement_event",
                "Received new event with ID %d while old fragments were not completed",
                (pevent->event_id & 0x0FFF));
      }

      /* search new slot */
      for (i = 0; i < MAX_DEFRAG_EVENTS; i++)
         if (defrag_buffer[i].event_id == 0)
            break;

      if (i == MAX_DEFRAG_EVENTS) {
         cm_msg(MERROR, "bm_defragment_event",
                "Not enough defragment buffers, please increase MAX_DEFRAG_EVENTS and recompile");
         return;
      }

      /* check event size */
      if (pevent->data_size != sizeof(DWORD)) {
         cm_msg(MERROR, "bm_defragment_event",
                "Received first event fragment with %d bytes instead of %d bytes, event ignored",
                pevent->data_size, (int) sizeof(DWORD));
         return;
      }

      /* setup defragment buffer */
      defrag_buffer[i].event_id = (pevent->event_id & 0x0FFF);
      defrag_buffer[i].data_size = *(DWORD *) pdata;
      defrag_buffer[i].received = 0;
      defrag_buffer[i].pevent = (EVENT_HEADER *) malloc(sizeof(EVENT_HEADER) + defrag_buffer[i].data_size);

      if (defrag_buffer[i].pevent == NULL) {
         memset(&defrag_buffer[i].event_id, 0, sizeof(EVENT_DEFRAG_BUFFER));
         cm_msg(MERROR, "bm_defragement_event", "Not enough memory to allocate event defragment buffer");
         return;
      }

      memcpy(defrag_buffer[i].pevent, pevent, sizeof(EVENT_HEADER));
      defrag_buffer[i].pevent->event_id = defrag_buffer[i].event_id;
      defrag_buffer[i].pevent->data_size = defrag_buffer[i].data_size;

      // printf("First frag[%d] (ID %d) Ser#:%d sz:%d\n", i, defrag_buffer[i].event_id,
      //       pevent->serial_number, defrag_buffer[i].data_size);

      return;
   }

   /* search buffer for that event */
   for (i = 0; i < MAX_DEFRAG_EVENTS; i++)
      if (defrag_buffer[i].event_id == (pevent->event_id & 0xFFF))
         break;

   if (i == MAX_DEFRAG_EVENTS) {
      /* no buffer available -> no first fragment received */
      cm_msg(MERROR, "bm_defragement_event",
             "Received fragment without first fragment (ID %d) Ser#:%d",
             pevent->event_id & 0x0FFF, pevent->serial_number);
      return;
   }

   /* add fragment to buffer */
   if (pevent->data_size + defrag_buffer[i].received > defrag_buffer[i].data_size) {
      free(defrag_buffer[i].pevent);
      defrag_buffer[i].pevent = NULL;
      memset(&defrag_buffer[i].event_id, 0, sizeof(EVENT_DEFRAG_BUFFER));
      cm_msg(MERROR, "bm_defragement_event",
             "Received fragments with more data (%d) than event size (%d)",
             pevent->data_size + defrag_buffer[i].received, defrag_buffer[i].data_size);
      return;
   }

   memcpy(((char *) defrag_buffer[i].pevent) + sizeof(EVENT_HEADER) +
          defrag_buffer[i].received, pdata, pevent->data_size);

   defrag_buffer[i].received += pevent->data_size;

   //printf("Other frag[%d][%d] (ID %d) Ser#:%d sz:%d\n", i, j++,
   //       defrag_buffer[i].event_id, pevent->serial_number, pevent->data_size);

   if (defrag_buffer[i].received == defrag_buffer[i].data_size) {
      /* event complete */
      dispatcher(buffer_handle, request_id, defrag_buffer[i].pevent, defrag_buffer[i].pevent + 1);
      free(defrag_buffer[i].pevent);
      defrag_buffer[i].pevent = NULL;
      memset(&defrag_buffer[i].event_id, 0, sizeof(EVENT_DEFRAG_BUFFER));
   }
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/**dox***************************************************************/
/** @} *//* end of bmfunctionc */

/**dox***************************************************************/
/** @addtogroup rpc_xxx
 *
 *  @{  */

/********************************************************************\
*                                                                    *
*                         RPC functions                              *
*                                                                    *
\********************************************************************/

class RPC_CLIENT_CONNECTION
{
public:
   std::atomic_bool connected{false}; /*  socket is connected */
   std::string host_name;       /*  server name             */
   int port = 0;                /*  server port             */
   std::mutex mutex;            /*  connection lock           */
   int index = 0;               /* index in the connection array */
   std::string client_name;     /* name of remote client    */
   int send_sock = 0;           /*  tcp socket              */
   int remote_hw_type = 0;      /*  remote hardware type    */
   int rpc_timeout = 0;         /*  timeout in milliseconds */

   void print() {
      printf("index %d, client \"%s\", host \"%s\", port %d, socket %d, connected %d, timeout %d",
             index,
             client_name.c_str(),
             host_name.c_str(),
             port,
             send_sock,
             int(connected),
             rpc_timeout);
   }

   void close_locked() {
      if (send_sock > 0) {
         closesocket(send_sock);
         send_sock = 0;
      }
      connected = false;
   }
};

/* globals */

//
// locking rules for client connections:
//
// lock _client_connections_mutex, look at _client_connections vector and c->connected, unlock _client_connections_mutex
// lock _client_connections_mutex, look at _client_connections vector and c->connected, lock individual connection, recheck c->connected, work on the connection, unlock the connection, unlock _client_connections_mutex
// lock individual connection, check c->connected, work on the connection, unlock connection
//
// ok to access without locking client connection:
//
// - c->connected (std::atomic, but must recheck it after taking the lock)
// - only inside rpc_client_connect() under protection of gHostnameMutex: c->host_name and c->port
//
// this will deadlock, wrong locking order: lock individual connection, lock of _client_connections_mutex
// this will deadlock, wrong unlocking order: unlock of _client_connections_mutex, unlock individual connection
//
// lifetime of client connections:
//
// - client connection slots are allocated by rpc_client_connect()
// - client connection slots are deleted by rpc_client_shutdown() called from cm_disconnect_experiment()
// - client slots marked NULL are free and will be reused by rpc_client_connect()
// - client slots marked c->connected == false are free and will be reused by rpc_client_connect()
// - rpc_client_check() will close connections that have dead tcp sockets, set c->connected = FALSE to mark the slot free for reuse
// - rpc_client_disconnect() will close the connection and set c->connected = FALSE to mark the slot free for reuse
// - rpc_client_call() can race rpc_client_disconnect() running in another thread, if disconnect happens first,
//   client call will see an empty slot and return an error
// - rpc_client_call() can race a disconnect()/connect() pair, if disconnect and connect happen first,
//   client call will be made to the wrong connection. for this reason, one should call rpc_client_disconnect()
//   only when one is sure no other threads are running concurrent rpc client calls.
//

static std::mutex _client_connections_mutex;
static std::vector<RPC_CLIENT_CONNECTION*> _client_connections;

static RPC_SERVER_CONNECTION _server_connection; // connection to the mserver
static BOOL _rpc_is_remote = FALSE;
   
//static RPC_SERVER_ACCEPTION _server_acception[MAX_RPC_CONNECTION];
static std::vector<RPC_SERVER_ACCEPTION*> _server_acceptions;
static RPC_SERVER_ACCEPTION* _mserver_acception = NULL; // mserver acception

static RPC_SERVER_ACCEPTION* rpc_get_server_acception(int idx)
{
   assert(idx >= 0);
   assert(idx < (int)_server_acceptions.size());
   assert(_server_acceptions[idx] != NULL);
   return _server_acceptions[idx];
}

RPC_SERVER_ACCEPTION* rpc_get_mserver_acception()
{
   return _mserver_acception;
}

static RPC_SERVER_ACCEPTION* rpc_new_server_acception()
{
   for (unsigned idx = 0; idx < _server_acceptions.size(); idx++) {
      if (_server_acceptions[idx] && (_server_acceptions[idx]->recv_sock == 0)) {
         //printf("rpc_new_server_acception: reuse acception in slot %d\n", idx);
         return _server_acceptions[idx];
      }
   }

   RPC_SERVER_ACCEPTION* sa = new RPC_SERVER_ACCEPTION;

   for (unsigned idx = 0; idx < _server_acceptions.size(); idx++) {
      if (_server_acceptions[idx] == NULL) {
         //printf("rpc_new_server_acception: new acception, reuse slot %d\n", idx);
         _server_acceptions[idx] = sa;
         return _server_acceptions[idx];
      }
   }

   //printf("rpc_new_server_acception: new acception, array size %d, push_back\n", (int)_server_acceptions.size());
   _server_acceptions.push_back(sa);
   
   return sa;
}

void RPC_SERVER_ACCEPTION::close()
{
   //printf("RPC_SERVER_ACCEPTION::close: connection from %s program %s mserver %d\n", host_name.c_str(), prog_name.c_str(), is_mserver);

   if (is_mserver) {
      assert(_mserver_acception == this);
      _mserver_acception = NULL;
      is_mserver = false;
   }
   
   /* close server connection */
   if (recv_sock)
      closesocket(recv_sock);
   if (send_sock)
      closesocket(send_sock);
   if (event_sock)
      closesocket(event_sock);

   recv_sock = 0;
   send_sock = 0;
   event_sock = 0;

   /* free TCP cache */
   if (net_buffer) {
      //printf("free net_buffer %p+%d\n", net_buffer, net_buffer_size);
      free(net_buffer);
      net_buffer = NULL;
      net_buffer_size = 0;
   }

   /* mark this entry as invalid */
   clear();
}

static RPC_LIST *rpc_list = NULL;

static int _opt_tcp_size = OPT_TCP_SIZE;


/********************************************************************\
*                       conversion functions                         *
\********************************************************************/

void rpc_calc_convert_flags(INT hw_type, INT remote_hw_type, INT *convert_flags) {
   *convert_flags = 0;

   /* big/little endian conversion */
   if (((remote_hw_type & DRI_BIG_ENDIAN) &&
        (hw_type & DRI_LITTLE_ENDIAN)) || ((remote_hw_type & DRI_LITTLE_ENDIAN)
                                           && (hw_type & DRI_BIG_ENDIAN)))
      *convert_flags |= CF_ENDIAN;

   /* float conversion between IEEE and VAX G */
   if ((remote_hw_type & DRF_G_FLOAT) && (hw_type & DRF_IEEE))
      *convert_flags |= CF_VAX2IEEE;

   /* float conversion between VAX G and IEEE */
   if ((remote_hw_type & DRF_IEEE) && (hw_type & DRF_G_FLOAT))
      *convert_flags |= CF_IEEE2VAX;

   ///* ASCII format */
   //if (remote_hw_type & DR_ASCII)
   //   *convert_flags |= CF_ASCII;
}

/********************************************************************/
void rpc_get_convert_flags(INT *convert_flags) {
   rpc_calc_convert_flags(rpc_get_hw_type(), _server_connection.remote_hw_type, convert_flags);
}

/********************************************************************/
void rpc_ieee2vax_float(float *var) {
   unsigned short int lo, hi;

   /* swap hi and lo word */
   lo = *((short int *) (var) + 1);
   hi = *((short int *) (var));

   /* correct exponent */
   if (lo != 0)
      lo += 0x100;

   *((short int *) (var) + 1) = hi;
   *((short int *) (var)) = lo;
}

void rpc_vax2ieee_float(float *var) {
   unsigned short int lo, hi;

   /* swap hi and lo word */
   lo = *((short int *) (var) + 1);
   hi = *((short int *) (var));

   /* correct exponent */
   if (hi != 0)
      hi -= 0x100;

   *((short int *) (var) + 1) = hi;
   *((short int *) (var)) = lo;

}

void rpc_vax2ieee_double(double *var) {
   unsigned short int i1, i2, i3, i4;

   /* swap words */
   i1 = *((short int *) (var) + 3);
   i2 = *((short int *) (var) + 2);
   i3 = *((short int *) (var) + 1);
   i4 = *((short int *) (var));

   /* correct exponent */
   if (i4 != 0)
      i4 -= 0x20;

   *((short int *) (var) + 3) = i4;
   *((short int *) (var) + 2) = i3;
   *((short int *) (var) + 1) = i2;
   *((short int *) (var)) = i1;
}

void rpc_ieee2vax_double(double *var) {
   unsigned short int i1, i2, i3, i4;

   /* swap words */
   i1 = *((short int *) (var) + 3);
   i2 = *((short int *) (var) + 2);
   i3 = *((short int *) (var) + 1);
   i4 = *((short int *) (var));

   /* correct exponent */
   if (i1 != 0)
      i1 += 0x20;

   *((short int *) (var) + 3) = i4;
   *((short int *) (var) + 2) = i3;
   *((short int *) (var) + 1) = i2;
   *((short int *) (var)) = i1;
}

/********************************************************************/
void rpc_convert_single(void *data, INT tid, INT flags, INT convert_flags) {

   if (convert_flags & CF_ENDIAN) {
      if (tid == TID_UINT16 || tid == TID_INT16) WORD_SWAP(data);
      if (tid == TID_UINT32 || tid == TID_INT32 || tid == TID_BOOL || tid == TID_FLOAT) DWORD_SWAP(data);
      if (tid == TID_DOUBLE) QWORD_SWAP(data);
   }

   if (((convert_flags & CF_IEEE2VAX) && !(flags & RPC_OUTGOING)) ||
       ((convert_flags & CF_VAX2IEEE) && (flags & RPC_OUTGOING))) {
      if (tid == TID_FLOAT)
         rpc_ieee2vax_float((float *) data);
      if (tid == TID_DOUBLE)
         rpc_ieee2vax_double((double *) data);
   }

   if (((convert_flags & CF_IEEE2VAX) && (flags & RPC_OUTGOING)) ||
       ((convert_flags & CF_VAX2IEEE) && !(flags & RPC_OUTGOING))) {
      if (tid == TID_FLOAT)
         rpc_vax2ieee_float((float *) data);
      if (tid == TID_DOUBLE)
         rpc_vax2ieee_double((double *) data);
   }
}

void rpc_convert_data(void *data, INT tid, INT flags, INT total_size, INT convert_flags)
/********************************************************************\

  Routine: rpc_convert_data

  Purpose: Convert data format between differenct computers

  Input:
    void   *data            Pointer to data
    INT    tid              Type ID of data, one of TID_xxx
    INT    flags            Combination of following flags:
                              RPC_IN: data is input parameter
                              RPC_OUT: data is output variable
                              RPC_FIXARRAY, RPC_VARARRAY: data is array
                                of "size" bytes (see next param.)
                              RPC_OUTGOING: data is outgoing
    INT    total_size       Size of bytes of data. Used for variable
                            length arrays.
    INT    convert_flags    Flags for data conversion

  Output:
    void   *data            Is converted according to _convert_flag
                            value

  Function value:
    RPC_SUCCESS             Successful completion

\********************************************************************/
{
   /* convert array */
   if (flags & (RPC_FIXARRAY | RPC_VARARRAY)) {
      int single_size = rpc_tid_size(tid);
      /* don't convert TID_ARRAY & TID_STRUCT */
      if (single_size == 0)
         return;

      int n = total_size / single_size;

      for (int i = 0; i < n; i++) {
         char* p = (char *) data + (i * single_size);
         rpc_convert_single(p, tid, flags, convert_flags);
      }
   } else {
      rpc_convert_single(data, tid, flags, convert_flags);
   }
}

/********************************************************************\
*                       type ID functions                            *
\********************************************************************/

INT rpc_tid_size(INT id) {
   if (id >= 0 && id < TID_LAST)
      return tid_size[id];

   return 0;
}

const char *rpc_tid_name(INT id) {
   if (id >= 0 && id < TID_LAST)
      return tid_name[id];
   else
      return "<unknown>";
}

const char *rpc_tid_name_old(INT id) {
   if (id >= 0 && id < TID_LAST)
      return tid_name_old[id];
   else
      return "<unknown>";
}

int rpc_name_tid(const char* name) // inverse of rpc_tid_name()
{
   for (int i=0; i<TID_LAST; i++) {
      if (strcmp(name, tid_name[i]) == 0)
         return i;
   }

   for (int i=0; i<TID_LAST; i++) {
      if (strcmp(name, tid_name_old[i]) == 0)
         return i;
   }

   return 0;
}

/********************************************************************\
*                        client functions                            *
\********************************************************************/

/********************************************************************/
/**
Register RPC client for standalone mode (without standard
           midas server)
@param list           Array of RPC_LIST structures containing
                            function IDs and parameter definitions.
                            The end of the list must be indicated by
                            a function ID of zero.
@param name          Name of this client
@return RPC_SUCCESS
*/
INT rpc_register_client(const char *name, RPC_LIST *list) {
   rpc_set_name(name);
   rpc_register_functions(rpc_get_internal_list(0), NULL);
   rpc_register_functions(list, NULL);

   return RPC_SUCCESS;
}

/********************************************************************/
/**
Register a set of RPC functions (both as clients or servers)
@param new_list       Array of RPC_LIST structures containing
                            function IDs and parameter definitions.
                            The end of the list must be indicated by
                            a function ID of zero.
@param func          Default dispatch function

@return RPC_SUCCESS, RPC_NO_MEMORY, RPC_DOUBLE_DEFINED
*/
INT rpc_register_functions(const RPC_LIST *new_list, RPC_HANDLER func) {
   INT i, j, iold, inew;

   /* count number of new functions */
   for (i = 0; new_list[i].id != 0; i++) {
      /* check double defined functions */
      for (j = 0; rpc_list != NULL && rpc_list[j].id != 0; j++)
         if (rpc_list[j].id == new_list[i].id)
            return RPC_DOUBLE_DEFINED;
   }
   inew = i;

   /* count number of existing functions */
   for (i = 0; rpc_list != NULL && rpc_list[i].id != 0; i++);
   iold = i;

   /* allocate new memory for rpc_list */
   if (rpc_list == NULL)
      rpc_list = (RPC_LIST *) M_MALLOC(sizeof(RPC_LIST) * (inew + 1));
   else
      rpc_list = (RPC_LIST *) realloc(rpc_list, sizeof(RPC_LIST) * (iold + inew + 1));

   if (rpc_list == NULL) {
      cm_msg(MERROR, "rpc_register_functions", "out of memory");
      return RPC_NO_MEMORY;
   }

   /* append new functions */
   for (i = iold; i < iold + inew; i++) {
      memmove(rpc_list + i, new_list + i - iold, sizeof(RPC_LIST));

      /* set default dispatcher */
      if (rpc_list[i].dispatch == NULL)
         rpc_list[i].dispatch = func;

      /* check valid ID for user functions */
      if (new_list != rpc_get_internal_list(0) &&
          new_list != rpc_get_internal_list(1) && (rpc_list[i].id < RPC_MIN_ID
                                                   || rpc_list[i].id > RPC_MAX_ID))
         cm_msg(MERROR, "rpc_register_functions", "registered RPC function with invalid ID");
   }

   /* mark end of list */
   rpc_list[i].id = 0;

   return RPC_SUCCESS;
}



/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/********************************************************************/
INT rpc_deregister_functions()
/********************************************************************\

  Routine: rpc_deregister_functions

  Purpose: Free memory of previously registered functions

  Input:
    none

  Output:
    none

  Function value:
    RPC_SUCCESS              Successful completion

\********************************************************************/
{
   if (rpc_list)
      M_FREE(rpc_list);
   rpc_list = NULL;

   return RPC_SUCCESS;
}


/********************************************************************/
INT rpc_register_function(INT id, INT(*func)(INT, void **))
/********************************************************************\

  Routine: rpc_register_function

  Purpose: Replace a dispatch function for a specific rpc routine

  Input:
    INT      id             RPC ID
    INT      *func          New dispatch function

  Output:
   <implicit: func gets copied to rpc_list>

  Function value:
   RPC_SUCCESS              Successful completion
   RPC_INVALID_ID           RPC ID not found

\********************************************************************/
{
   INT i;

   for (i = 0; rpc_list != NULL && rpc_list[i].id != 0; i++)
      if (rpc_list[i].id == id)
         break;

   if (rpc_list[i].id == id)
      rpc_list[i].dispatch = func;
   else
      return RPC_INVALID_ID;

   return RPC_SUCCESS;
}

/********************************************************************/

static int handle_msg_odb(int n, const NET_COMMAND *nc) {
   //printf("rpc_client_dispatch: MSG_ODB: packet size %d, expected %d\n", n, (int)(sizeof(NET_COMMAND_HEADER) + 4 * sizeof(INT)));
   if (n == sizeof(NET_COMMAND_HEADER) + 4 * sizeof(INT)) {
      /* update a changed record */
      HNDLE hDB = *((INT *) nc->param);
      HNDLE hKeyRoot = *((INT *) nc->param + 1);
      HNDLE hKey = *((INT *) nc->param + 2);
      int index = *((INT *) nc->param + 3);
      return db_update_record_local(hDB, hKeyRoot, hKey, index);
   }
   return CM_VERSION_MISMATCH;
}

/********************************************************************/
INT rpc_client_dispatch(int sock)
/********************************************************************\

  Routine: rpc_client_dispatch

  Purpose: Receive data from the mserver: watchdog and buffer notification messages

\********************************************************************/
{
   INT status = 0;
   char net_buffer[256];

   int n = recv_tcp(sock, net_buffer, sizeof(net_buffer), 0);
   if (n <= 0)
      return SS_ABORT;

   NET_COMMAND *nc = (NET_COMMAND *) net_buffer;

   if (nc->header.routine_id == MSG_ODB) {
      status = handle_msg_odb(n, nc);
   } else if (nc->header.routine_id == MSG_WATCHDOG) {
      nc->header.routine_id = 1;
      nc->header.param_size = 0;
      send_tcp(sock, net_buffer, sizeof(NET_COMMAND_HEADER), 0);
      status = RPC_SUCCESS;
   } else if (nc->header.routine_id == MSG_BM) {
      fd_set readfds;
      struct timeval timeout;

      //printf("rpc_client_dispatch: received MSG_BM!\n");

      /* receive further messages to empty TCP queue */
      do {
         FD_ZERO(&readfds);
         FD_SET(sock, &readfds);

         timeout.tv_sec = 0;
         timeout.tv_usec = 0;

         select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);

         if (FD_ISSET(sock, &readfds)) {
            n = recv_tcp(sock, net_buffer, sizeof(net_buffer), 0);
            if (n <= 0)
               return SS_ABORT;

            if (nc->header.routine_id == MSG_ODB) {
               status = handle_msg_odb(n, nc);
            } else if (nc->header.routine_id == MSG_WATCHDOG) {
               nc->header.routine_id = 1;
               nc->header.param_size = 0;
               send_tcp(sock, net_buffer, sizeof(NET_COMMAND_HEADER), 0);
               status = RPC_SUCCESS;
            }
         }

      } while (FD_ISSET(sock, &readfds));

      /* poll event from server */
      status = bm_poll_event();
   }

   return status;
}


/********************************************************************/
INT rpc_client_connect(const char *host_name, INT port, const char *client_name, HNDLE *hConnection)
/********************************************************************\

  Routine: rpc_client_connect

  Purpose: Establish a network connection to a remote client

  Input:
    char *host_name          IP address of host to connect to.
    INT  port                TPC port to connect to.
    char *clinet_name        Client program name

  Output:
    HNDLE *hConnection       Handle for new connection which can be used
                             in future rpc_call(hConnection....) calls

  Function value:
    RPC_SUCCESS              Successful completion
    RPC_NET_ERROR            Error in socket call
    RPC_NO_CONNECTION        Maximum number of connections reached
    RPC_NOT_REGISTERED       cm_connect_experiment was not called properly

\********************************************************************/
{
   INT i, status;
   bool debug = false;

#ifdef OS_WINNT
   {
      WSADATA WSAData;

      /* Start windows sockets */
      if (WSAStartup(MAKEWORD(1, 1), &WSAData) != 0)
         return RPC_NET_ERROR;
   }
#endif

   /* check if cm_connect_experiment was called */
   if (_client_name.length() == 0) {
      cm_msg(MERROR, "rpc_client_connect", "cm_connect_experiment/rpc_set_name not called");
      return RPC_NOT_REGISTERED;
   }

   /* refuse connection to port 0 */
   if (port == 0) {
      cm_msg(MERROR, "rpc_client_connect", "invalid port %d", port);
      return RPC_NET_ERROR;
   }

   RPC_CLIENT_CONNECTION* c = NULL;

   static std::mutex gHostnameMutex;

   {
      std::lock_guard<std::mutex> guard(_client_connections_mutex);

      if (debug) {
         printf("rpc_client_connect: host \"%s\", port %d, client \"%s\"\n", host_name, port, client_name);
         for (size_t i = 0; i < _client_connections.size(); i++) {
            if (_client_connections[i]) {
               printf("client connection %d: ", (int)i);
               _client_connections[i]->print();
               printf("\n");
            }
         }
      }

      // slot with index 0 is not used, fill it with a NULL
      
      if (_client_connections.empty()) {
         _client_connections.push_back(NULL);
      }

      bool hostname_locked = false;

      /* check if connection already exists */
      for (size_t i = 1; i < _client_connections.size(); i++) {
         RPC_CLIENT_CONNECTION* c = _client_connections[i];
         if (c && c->connected) {

            if (!hostname_locked) {
               gHostnameMutex.lock();
               hostname_locked = true;
            }

            if ((c->host_name == host_name) && (c->port == port)) {
               // NB: we must release the hostname lock before taking
               // c->mutex to avoid a locking order inversion deadlock:
               // later on we lock the hostname mutex while holding the c->mutex
               gHostnameMutex.unlock();
               hostname_locked = false;
               std::lock_guard<std::mutex> cguard(c->mutex);
               // check if socket is still connected
               if (c->connected) {
                  // found connection slot with matching hostname and port number
                  status = ss_socket_wait(c->send_sock, 0);
                  if (status == SS_TIMEOUT) { // yes, still connected and empty
                     // so reuse it connection
                     *hConnection = c->index;
                     if (debug) {
                        printf("already connected: ");
                        c->print();
                        printf("\n");
                     }
                     // implicit unlock of c->mutex
                     // gHostnameLock is not locked here
                     return RPC_SUCCESS;
                  }
                  //cm_msg(MINFO, "rpc_client_connect", "Stale connection to \"%s\" on host %s is closed", _client_connection[i].client_name, _client_connection[i].host_name);
                  c->close_locked();
               }
               // implicit unlock of c->mutex
            }
         }
      }

      if (hostname_locked) {
         gHostnameMutex.unlock();
         hostname_locked = false;
      }
      
      // only start reusing connections once we have
      // a good number of slots allocated.
      if (_client_connections.size() > 10) {
         static int last_reused = 0;

         int size = _client_connections.size();
         for (int j = 1; j < size; j++) {
            int i = (last_reused + j) % size;
            if (_client_connections[i] && !_client_connections[i]->connected) {
               c = _client_connections[i];
               if (debug) {
                  printf("last reused %d, reusing slot %d: ", last_reused, (int)i);
                  c->print();
                  printf("\n");
               }
               last_reused = i;
               break;
            }
         }
      }

      // no slots to reuse, allocate a new slot.
      if (!c) {
         c = new RPC_CLIENT_CONNECTION;

         // if empty slot not found, add to end of array
         c->index = _client_connections.size();
         _client_connections.push_back(c);

         if (debug) {
            printf("new connection appended to array: ");
            c->print();
            printf("\n");
         }
      }

      c->mutex.lock();
      c->connected = true; // rpc_client_connect() in another thread may try to grab this slot

      // done with the array of connections
      // implicit unlock of _client_connections_mutex
   }

   // locked connection slot for new connection
   assert(c != NULL);

   /* create a new socket for connecting to remote server */
   c->send_sock = socket(AF_INET, SOCK_STREAM, 0);
   if (c->send_sock == -1) {
      cm_msg(MERROR, "rpc_client_connect", "cannot create socket, socket() errno %d (%s)", errno, strerror(errno));
      c->mutex.unlock();
      return RPC_NET_ERROR;
   }

   gHostnameMutex.lock();

   c->host_name   = host_name;
   c->port        = port;

   gHostnameMutex.unlock();

   c->client_name = client_name;
   c->rpc_timeout = DEFAULT_RPC_TIMEOUT;

   /* connect to remote node */
   struct sockaddr_in bind_addr;
   memset(&bind_addr, 0, sizeof(bind_addr));
   bind_addr.sin_family = AF_INET;
   bind_addr.sin_addr.s_addr = 0;
   bind_addr.sin_port = htons((short) port);

#ifdef OS_VXWORKS
   {
      INT host_addr;

      host_addr = hostGetByName(host_name);
      memcpy((char *) &(bind_addr.sin_addr), &host_addr, 4);
   }
#else
   struct hostent *phe = gethostbyname(host_name);
   if (phe == NULL) {
      cm_msg(MERROR, "rpc_client_connect", "cannot lookup host name \'%s\'", host_name);
      c->close_locked();
      c->mutex.unlock();
      return RPC_NET_ERROR;
   }
   memcpy((char *) &(bind_addr.sin_addr), phe->h_addr, phe->h_length);
#endif

#ifdef OS_UNIX
   do {
      status = connect(c->send_sock, (struct sockaddr *) &bind_addr, sizeof(bind_addr));

      /* don't return if an alarm signal was cought */
   } while (status == -1 && errno == EINTR);
#else
   status = connect(c->send_sock, (struct sockaddr *) &bind_addr, sizeof(bind_addr));
#endif

   if (status != 0) {
      cm_msg(MERROR, "rpc_client_connect",
             "cannot connect to host \"%s\", port %d: connect() returned %d, errno %d (%s)", host_name, port, status,
             errno, strerror(errno));
      c->close_locked();
      c->mutex.unlock();
      return RPC_NET_ERROR;
   }

   /* set TCP_NODELAY option for better performance */
   i = 1;
   setsockopt(c->send_sock, IPPROTO_TCP, TCP_NODELAY, (char *) &i, sizeof(i));

   /* send local computer info */
   std::string local_prog_name = rpc_get_name();
   std::string local_host_name = ss_gethostname();

   int hw_type = rpc_get_hw_type();

   std::string cstr = msprintf("%d %s %s %s", hw_type, cm_get_version(), local_prog_name.c_str(), local_host_name.c_str());

   int size = cstr.length() + 1;
   i = send(c->send_sock, cstr.c_str(), size, 0);
   if (i < 0 || i != size) {
      cm_msg(MERROR, "rpc_client_connect", "cannot send %d bytes, send() returned %d, errno %d (%s)", size, i, errno, strerror(errno));
      c->mutex.unlock();
      return RPC_NET_ERROR;
   }

   bool restore_watchdog_timeout = false;
   BOOL watchdog_call;
   DWORD watchdog_timeout;
   cm_get_watchdog_params(&watchdog_call, &watchdog_timeout);

   //printf("watchdog timeout: %d, rpc_connect_timeout: %d\n", watchdog_timeout, _rpc_connect_timeout);

   if (_rpc_connect_timeout >= (int) watchdog_timeout) {
      restore_watchdog_timeout = true;
      cm_set_watchdog_params(watchdog_call, _rpc_connect_timeout + 1000);
   }

   char str[256];

   /* receive remote computer info */
   i = recv_string(c->send_sock, str, sizeof(str), _rpc_connect_timeout);

   if (restore_watchdog_timeout) {
      cm_set_watchdog_params(watchdog_call, watchdog_timeout);
   }

   if (i <= 0) {
      cm_msg(MERROR, "rpc_client_connect", "timeout waiting for server reply");
      c->close_locked();
      c->mutex.unlock();
      return RPC_NET_ERROR;
   }

   int remote_hw_type = 0;
   char remote_version[32];
   remote_version[0] = 0;
   sscanf(str, "%d %s", &remote_hw_type, remote_version);

   c->remote_hw_type = remote_hw_type;

   /* print warning if version patch level doesn't agree */
   char v1[32];
   strlcpy(v1, remote_version, sizeof(v1));
   if (strchr(v1, '.'))
      if (strchr(strchr(v1, '.') + 1, '.'))
         *strchr(strchr(v1, '.') + 1, '.') = 0;

   strlcpy(str, cm_get_version(), sizeof(str));
   if (strchr(str, '.'))
      if (strchr(strchr(str, '.') + 1, '.'))
         *strchr(strchr(str, '.') + 1, '.') = 0;

   if (strcmp(v1, str) != 0) {
      cm_msg(MERROR, "rpc_client_connect", "remote MIDAS version \'%s\' differs from local version \'%s\'", remote_version, cm_get_version());
   }

   c->connected = true;

   *hConnection = c->index;

   c->mutex.unlock();

   return RPC_SUCCESS;
}

/********************************************************************/
void rpc_client_check()
/********************************************************************\

  Routine: rpc_client_check

  Purpose: Check all client connections if remote client closed link

\********************************************************************/
{
#if 0
   for (i = 0; i < MAX_RPC_CONNECTION; i++)
      if (_client_connection[i].send_sock != 0)
         printf("slot %d, checking client %s socket %d, connected %d\n", i, _client_connection[i].client_name, _client_connection[i].send_sock, _client_connection[i].connected);
#endif

   std::lock_guard<std::mutex> guard(_client_connections_mutex);

   /* check for broken connections */
   for (unsigned i = 0; i < _client_connections.size(); i++) {
      RPC_CLIENT_CONNECTION* c = _client_connections[i];
      if (c && c->connected) {
         std::lock_guard<std::mutex> cguard(c->mutex);

         if (!c->connected) {
            // implicit unlock
            continue;
         }

         //printf("rpc_client_check: connection %d: ", i);
         //c->print();
         //printf("\n");

         int ok = 0;

         fd_set readfds;
         FD_ZERO(&readfds);
         FD_SET(c->send_sock, &readfds);

         struct timeval timeout;
         timeout.tv_sec = 0;
         timeout.tv_usec = 0;

         int status;

#ifdef OS_WINNT
         status = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);
#else
         do {
            status = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);
         } while (status == -1 && errno == EINTR); /* dont return if an alarm signal was cought */
#endif

         if (!FD_ISSET(c->send_sock, &readfds)) {
            // implicit unlock
            continue;
         }

         char buffer[64];

         status = recv(c->send_sock, (char *) buffer, sizeof(buffer), MSG_PEEK);
         //printf("recv %d status %d, errno %d (%s)\n", sock, status, errno, strerror(errno));

         if (status < 0) {
#ifndef OS_WINNT
            if (errno == EAGAIN) { // still connected
               ok = 1;
            } else
#endif
            {
               // connection error
               cm_msg(MERROR, "rpc_client_check",
                      "RPC client connection to \"%s\" on host \"%s\" is broken, recv() errno %d (%s)",
                      c->client_name.c_str(),
                      c->host_name.c_str(),
                      errno, strerror(errno));
               ok = 0;
            }
         } else if (status == 0) {
            // connection closed by remote end without sending an EXIT message
            // this can happen if the remote end has crashed, so this message
            // is still necessary as a useful diagnostic for unexpected crashes
            // of midas programs. K.O.
            cm_msg(MINFO, "rpc_client_check", "RPC client connection to \"%s\" on host \"%s\" unexpectedly closed", c->client_name.c_str(), c->host_name.c_str());
            ok = 0;
         } else {
            // read some data
            ok = 1;
            if (equal_ustring(buffer, "EXIT")) {
               /* normal exit */
               ok = 0;
            }
         }

         if (!ok) {
            //printf("rpc_client_check: closing connection %d: ", i);
            //c->print();
            //printf("\n");

            // connection lost, close the socket
            c->close_locked();
         }

         // implicit unlock
      }
   }

   // implicit unlock of _client_connections_mutex
}


/********************************************************************/
INT rpc_server_connect(const char *host_name, const char *exp_name)
/********************************************************************\

  Routine: rpc_server_connect

  Purpose: Extablish a network connection to a remote MIDAS
           server using a callback scheme.

  Input:
    char *host_name         IP address of host to connect to.

    INT  port               TPC port to connect to.

    char *exp_name          Name of experiment to connect to. By using
                            this name, several experiments (e.g. online
                            DAQ and offline analysis) can run simultan-
                            eously on the same host.

  Output:
    none

  Function value:
    RPC_SUCCESS              Successful completion
    RPC_NET_ERROR            Error in socket call
    RPC_NOT_REGISTERED       cm_connect_experiment was not called properly
    CM_UNDEF_EXP             Undefined experiment on server

\********************************************************************/
{
   INT i, status, flag;
   struct sockaddr_in bind_addr;
   INT sock, lsock1, lsock2, lsock3;
   INT listen_port1, listen_port2, listen_port3;
   INT remote_hw_type, hw_type;
   unsigned int size;
   char str[200], version[32], v1[32];
   struct hostent *phe;
   fd_set readfds;
   struct timeval timeout;
   int port = MIDAS_TCP_PORT;
   char *s;

#ifdef OS_WINNT
   {
      WSADATA WSAData;

      /* Start windows sockets */
      if (WSAStartup(MAKEWORD(1, 1), &WSAData) != 0)
         return RPC_NET_ERROR;
   }
#endif

   /* check if local connection */
   if (host_name[0] == 0)
      return RPC_SUCCESS;

   /* register system functions */
   rpc_register_functions(rpc_get_internal_list(0), NULL);

   /* check if cm_connect_experiment was called */
   if (_client_name.length() == 0) {
      cm_msg(MERROR, "rpc_server_connect", "cm_connect_experiment/rpc_set_name not called");
      return RPC_NOT_REGISTERED;
   }

   /* check if connection already exists */
   if (_server_connection.send_sock != 0)
      return RPC_SUCCESS;

   _server_connection.host_name = host_name;
   _server_connection.exp_name = exp_name;
   _server_connection.rpc_timeout = DEFAULT_RPC_TIMEOUT;

   /* create new TCP sockets for listening */
   lsock1 = socket(AF_INET, SOCK_STREAM, 0);
   lsock2 = socket(AF_INET, SOCK_STREAM, 0);
   lsock3 = socket(AF_INET, SOCK_STREAM, 0);
   if (lsock3 == -1) {
      cm_msg(MERROR, "rpc_server_connect", "cannot create socket");
      return RPC_NET_ERROR;
   }

   flag = 1;
   setsockopt(lsock1, SOL_SOCKET, SO_REUSEADDR, (char *) &flag, sizeof(INT));
   setsockopt(lsock2, SOL_SOCKET, SO_REUSEADDR, (char *) &flag, sizeof(INT));
   setsockopt(lsock3, SOL_SOCKET, SO_REUSEADDR, (char *) &flag, sizeof(INT));

   /* let OS choose any port number */
   memset(&bind_addr, 0, sizeof(bind_addr));
   bind_addr.sin_family = AF_INET;
   bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

   bind_addr.sin_port = 0;
   status = bind(lsock1, (struct sockaddr *) &bind_addr, sizeof(bind_addr));
   if (status < 0) {
      cm_msg(MERROR, "rpc_server_connect", "cannot bind, errno %d (%s)", errno, strerror(errno));
      return RPC_NET_ERROR;
   }

   bind_addr.sin_port = 0;
   status = bind(lsock2, (struct sockaddr *) &bind_addr, sizeof(bind_addr));
   if (status < 0) {
      cm_msg(MERROR, "rpc_server_connect", "cannot bind, errno %d (%s)", errno, strerror(errno));
      return RPC_NET_ERROR;
   }

   bind_addr.sin_port = 0;
   status = bind(lsock3, (struct sockaddr *) &bind_addr, sizeof(bind_addr));
   if (status < 0) {
      cm_msg(MERROR, "rpc_server_connect", "cannot bind, errno %d (%s)", errno, strerror(errno));
      return RPC_NET_ERROR;
   }

   /* listen for connection */
   status = listen(lsock1, 1);
   status = listen(lsock2, 1);
   status = listen(lsock3, 1);
   if (status < 0) {
      cm_msg(MERROR, "rpc_server_connect", "cannot listen, errno %d (%s)", errno, strerror(errno));
      return RPC_NET_ERROR;
   }

   /* find out which port OS has chosen */
   size = sizeof(bind_addr);
#ifdef OS_WINNT
   getsockname(lsock1, (struct sockaddr *) &bind_addr, (int *) &size);
   listen_port1 = ntohs(bind_addr.sin_port);
   getsockname(lsock2, (struct sockaddr *) &bind_addr, (int *) &size);
   listen_port2 = ntohs(bind_addr.sin_port);
   getsockname(lsock3, (struct sockaddr *) &bind_addr, (int *) &size);
   listen_port3 = ntohs(bind_addr.sin_port);
#else
   getsockname(lsock1, (struct sockaddr *) &bind_addr, &size);
   listen_port1 = ntohs(bind_addr.sin_port);
   getsockname(lsock2, (struct sockaddr *) &bind_addr, &size);
   listen_port2 = ntohs(bind_addr.sin_port);
   getsockname(lsock3, (struct sockaddr *) &bind_addr, &size);
   listen_port3 = ntohs(bind_addr.sin_port);
#endif

   /* create a new socket for connecting to remote server */
   sock = socket(AF_INET, SOCK_STREAM, 0);
   if (sock == -1) {
      cm_msg(MERROR, "rpc_server_connect", "cannot create socket");
      return RPC_NET_ERROR;
   }

   /* extract port number from host_name */
   strlcpy(str, host_name, sizeof(str));
   s = strchr(str, ':');
   if (s) {
      *s = 0;
      port = strtoul(s + 1, NULL, 0);
   }

   /* connect to remote node */
   memset(&bind_addr, 0, sizeof(bind_addr));
   bind_addr.sin_family = AF_INET;
   bind_addr.sin_addr.s_addr = 0;
   bind_addr.sin_port = htons(port);

#ifdef OS_VXWORKS
   {
      INT host_addr;

      host_addr = hostGetByName(str);
      memcpy((char *) &(bind_addr.sin_addr), &host_addr, 4);
   }
#else
   phe = gethostbyname(str);
   if (phe == NULL) {
      cm_msg(MERROR, "rpc_server_connect", "cannot resolve host name \'%s\'", str);
      return RPC_NET_ERROR;
   }
   memcpy((char *) &(bind_addr.sin_addr), phe->h_addr, phe->h_length);
#endif

#ifdef OS_UNIX
   do {
      status = connect(sock, (struct sockaddr *) &bind_addr, sizeof(bind_addr));

      /* don't return if an alarm signal was cought */
   } while (status == -1 && errno == EINTR);
#else
   status = connect(sock, (struct sockaddr *) &bind_addr, sizeof(bind_addr));
#endif

   if (status != 0) {
/*    cm_msg(MERROR, "rpc_server_connect", "cannot connect"); message should be displayed by application */
      return RPC_NET_ERROR;
   }

   /* connect to experiment */
   if (exp_name[0] == 0)
      sprintf(str, "C %d %d %d %s Default", listen_port1, listen_port2, listen_port3, cm_get_version());
   else
      sprintf(str, "C %d %d %d %s %s", listen_port1, listen_port2, listen_port3, cm_get_version(), exp_name);

   send(sock, str, strlen(str) + 1, 0);
   i = recv_string(sock, str, sizeof(str), _rpc_connect_timeout);
   closesocket(sock);
   if (i <= 0) {
      cm_msg(MERROR, "rpc_server_connect", "timeout on receive status from server");
      return RPC_NET_ERROR;
   }

   status = version[0] = 0;
   sscanf(str, "%d %s", &status, version);

   if (status == 2) {
/*  message "undefined experiment" should be displayed by application */
      return CM_UNDEF_EXP;
   }

   /* print warning if version patch level doesn't agree */
   strcpy(v1, version);
   if (strchr(v1, '.'))
      if (strchr(strchr(v1, '.') + 1, '.'))
         *strchr(strchr(v1, '.') + 1, '.') = 0;

   strcpy(str, cm_get_version());
   if (strchr(str, '.'))
      if (strchr(strchr(str, '.') + 1, '.'))
         *strchr(strchr(str, '.') + 1, '.') = 0;

   if (strcmp(v1, str) != 0) {
      cm_msg(MERROR, "rpc_server_connect", "remote MIDAS version \'%s\' differs from local version \'%s\'", version,
             cm_get_version());
   }

   /* wait for callback on send and recv socket with timeout */
   FD_ZERO(&readfds);
   FD_SET(lsock1, &readfds);
   FD_SET(lsock2, &readfds);
   FD_SET(lsock3, &readfds);

   timeout.tv_sec = _rpc_connect_timeout / 1000;
   timeout.tv_usec = 0;

   do {
      status = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);

      /* if an alarm signal was cought, restart select with reduced timeout */
      if (status == -1 && timeout.tv_sec >= WATCHDOG_INTERVAL / 1000)
         timeout.tv_sec -= WATCHDOG_INTERVAL / 1000;

   } while (status == -1);      /* dont return if an alarm signal was cought */

   if (!FD_ISSET(lsock1, &readfds)) {
      cm_msg(MERROR, "rpc_server_connect", "mserver subprocess could not be started (check path)");
      closesocket(lsock1);
      closesocket(lsock2);
      closesocket(lsock3);
      return RPC_NET_ERROR;
   }

   size = sizeof(bind_addr);

#ifdef OS_WINNT
   _server_connection.send_sock = accept(lsock1, (struct sockaddr *) &bind_addr, (int *) &size);
   _server_connection.recv_sock = accept(lsock2, (struct sockaddr *) &bind_addr, (int *) &size);
   _server_connection.event_sock = accept(lsock3, (struct sockaddr *) &bind_addr, (int *) &size);
#else
   _server_connection.send_sock = accept(lsock1, (struct sockaddr *) &bind_addr, &size);
   _server_connection.recv_sock = accept(lsock2, (struct sockaddr *) &bind_addr, &size);
   _server_connection.event_sock = accept(lsock3, (struct sockaddr *) &bind_addr, &size);
#endif

   if (_server_connection.send_sock == -1 || _server_connection.recv_sock == -1
       || _server_connection.event_sock == -1) {
      cm_msg(MERROR, "rpc_server_connect", "accept() failed");
      return RPC_NET_ERROR;
   }

   closesocket(lsock1);
   closesocket(lsock2);
   closesocket(lsock3);

   /* set TCP_NODELAY option for better performance */
   flag = 1;
   setsockopt(_server_connection.send_sock, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(flag));
   setsockopt(_server_connection.event_sock, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(flag));

   /* increase send buffer size to 2 Mbytes, on Linux also limited by sysctl net.ipv4.tcp_rmem and net.ipv4.tcp_wmem */
   flag = 2 * 1024 * 1024;
   status = setsockopt(_server_connection.event_sock, SOL_SOCKET, SO_SNDBUF, (char *) &flag, sizeof(flag));
   if (status != 0)
      cm_msg(MERROR, "rpc_server_connect", "cannot setsockopt(SOL_SOCKET, SO_SNDBUF), errno %d (%s)", errno, strerror(errno));

   /* send local computer info */
   std::string local_prog_name = rpc_get_name();
   hw_type = rpc_get_hw_type();
   sprintf(str, "%d %s", hw_type, local_prog_name.c_str());

   send(_server_connection.send_sock, str, strlen(str) + 1, 0);

   /* receive remote computer info */
   i = recv_string(_server_connection.send_sock, str, sizeof(str), _rpc_connect_timeout);
   if (i <= 0) {
      cm_msg(MERROR, "rpc_server_connect", "timeout on receive remote computer info");
      return RPC_NET_ERROR;
   }

   sscanf(str, "%d", &remote_hw_type);
   _server_connection.remote_hw_type = remote_hw_type;

   ss_suspend_set_client_connection(&_server_connection);

   _rpc_is_remote = TRUE;

   return RPC_SUCCESS;
}

/********************************************************************/

static RPC_CLIENT_CONNECTION* rpc_get_locked_client_connection(HNDLE hConn)
{
   _client_connections_mutex.lock();
   if (hConn >= 0 && hConn < (int)_client_connections.size()) {
      RPC_CLIENT_CONNECTION* c = _client_connections[hConn];
      if (c && c->connected) {
         _client_connections_mutex.unlock();
         c->mutex.lock();
         if (!c->connected) {
            // disconnected while we were waiting for the lock
            c->mutex.unlock();
            return NULL;
         }
         return c;
      }
   }
   _client_connections_mutex.unlock();
   return NULL;
}

static void rpc_client_shutdown()
{
   /* close all open connections */

   _client_connections_mutex.lock();

   for (unsigned i = 0; i < _client_connections.size(); i++) {
      RPC_CLIENT_CONNECTION* c = _client_connections[i];
      if (c && c->connected) {
         int index = c->index;
         // must unlock the array, otherwise we hang -
         // rpc_client_disconnect() will do rpc_call_client()
         // which needs to lock the array to convert handle
         // to connection pointer. Ouch! K.O. Dec 2020.
         _client_connections_mutex.unlock();
         rpc_client_disconnect(index, FALSE);
         _client_connections_mutex.lock();
      }
   }

   for (unsigned i = 0; i < _client_connections.size(); i++) {
      RPC_CLIENT_CONNECTION* c = _client_connections[i];
      //printf("client connection %d %p\n", i, c);
      if (c) {
         //printf("client connection %d %p connected %d\n", i, c, c->connected);
         if (!c->connected) {
            delete c;
            _client_connections[i] = NULL;
         }
      }
   }

   _client_connections_mutex.unlock();

   /* close server connection from other clients */
   for (unsigned i = 0; i < _server_acceptions.size(); i++) {
      if (_server_acceptions[i] && _server_acceptions[i]->recv_sock) {
         send(_server_acceptions[i]->recv_sock, "EXIT", 5, 0);
         _server_acceptions[i]->close();
      }
   }
}

/********************************************************************/
INT rpc_client_disconnect(HNDLE hConn, BOOL bShutdown)
/********************************************************************\

  Routine: rpc_client_disconnect

  Purpose: Close a rpc connection to a MIDAS client

  Input:
    HNDLE  hConn           Handle of connection
    BOOL   bShutdown       Shut down remote server if TRUE

  Output:
    none

  Function value:
   RPC_SUCCESS             Successful completion

\********************************************************************/
{
   /* notify server about exit */
   
   /* call exit and shutdown with RPC_NO_REPLY because client will exit immediately without possibility of replying */
   
   rpc_client_call(hConn, bShutdown ? (RPC_ID_SHUTDOWN | RPC_NO_REPLY) : (RPC_ID_EXIT | RPC_NO_REPLY));

   return RPC_SUCCESS;
}

/********************************************************************/
INT rpc_server_disconnect()
/********************************************************************\

  Routine: rpc_server_disconnect

  Purpose: Close a rpc connection to a MIDAS server and close all
           server connections from other clients

  Input:
    none

  Output:
    none

  Function value:
   RPC_SUCCESS             Successful completion
   RPC_NET_ERROR           Error in socket call
   RPC_NO_CONNECTION       Maximum number of connections reached

\********************************************************************/
{
   static int rpc_server_disconnect_recursion_level = 0;

   if (rpc_server_disconnect_recursion_level)
      return RPC_SUCCESS;

   rpc_server_disconnect_recursion_level = 1;

   /* flush remaining events */
   rpc_flush_event();

   /* notify server about exit */
   rpc_call(RPC_ID_EXIT);

   /* close sockets */
   closesocket(_server_connection.send_sock);
   closesocket(_server_connection.recv_sock);
   closesocket(_server_connection.event_sock);

   _server_connection.send_sock = 0;
   _server_connection.recv_sock = 0;
   _server_connection.event_sock = 0;

   _server_connection.clear();

   /* remove semaphore */
   if (_mutex_rpc)
      ss_mutex_delete(_mutex_rpc);
   _mutex_rpc = NULL;

   rpc_server_disconnect_recursion_level = 0;
   return RPC_SUCCESS;
}

/********************************************************************/
INT rpc_is_remote(void)
/********************************************************************\

  Routine: rpc_is_remote

  Purpose: Return true if program is connected to a remote server

  Input:
   none

  Output:
    none

  Function value:
    INT    RPC connection index

\********************************************************************/
{
   return _rpc_is_remote;
}

/********************************************************************/
std::string rpc_get_mserver_hostname(void)
/********************************************************************\

  Routine: rpc_get_mserver_hostname

  Purpose: Return the hostname of the mserver connection (host:port format)

\********************************************************************/
{
   return _server_connection.host_name;
}

/********************************************************************/
bool rpc_is_mserver(void)
/********************************************************************\

  Routine: rpc_is_mserver

  Purpose: Return true if we are the mserver

  Function value:
    INT    "true" if we are the mserver

\********************************************************************/
{
   return _mserver_acception != NULL;
}

/********************************************************************/
INT rpc_get_hw_type()
/********************************************************************\

  Routine: rpc_get_hw_type

  Purpose: get hardware information

  Function value:
    INT                     combination of DRI_xxx bits

\********************************************************************/
{
   {
      {
         INT tmp_type, size;
         DWORD dummy;
         unsigned char *p;
         float f;
         double d;

         tmp_type = 0;

         /* test pointer size */
         size = sizeof(p);
         if (size == 2)
            tmp_type |= DRI_16;
         if (size == 4)
            tmp_type |= DRI_32;
         if (size == 8)
            tmp_type |= DRI_64;

         /* test if little or big endian machine */
         dummy = 0x12345678;
         p = (unsigned char *) &dummy;
         if (*p == 0x78)
            tmp_type |= DRI_LITTLE_ENDIAN;
         else if (*p == 0x12)
            tmp_type |= DRI_BIG_ENDIAN;
         else
            cm_msg(MERROR, "rpc_get_option", "unknown byte order format");

         /* floating point format */
         f = (float) 1.2345;
         dummy = 0;
         memcpy(&dummy, &f, sizeof(f));
         if ((dummy & 0xFF) == 0x19 &&
             ((dummy >> 8) & 0xFF) == 0x04 && ((dummy >> 16) & 0xFF) == 0x9E
             && ((dummy >> 24) & 0xFF) == 0x3F)
            tmp_type |= DRF_IEEE;
         else if ((dummy & 0xFF) == 0x9E &&
                  ((dummy >> 8) & 0xFF) == 0x40 && ((dummy >> 16) & 0xFF) == 0x19
                  && ((dummy >> 24) & 0xFF) == 0x04)
            tmp_type |= DRF_G_FLOAT;
         else
            cm_msg(MERROR, "rpc_get_option", "unknown floating point format");

         d = (double) 1.2345;
         dummy = 0;
         memcpy(&dummy, &d, sizeof(f));
         if ((dummy & 0xFF) == 0x8D &&  /* little endian */
             ((dummy >> 8) & 0xFF) == 0x97 && ((dummy >> 16) & 0xFF) == 0x6E
             && ((dummy >> 24) & 0xFF) == 0x12)
            tmp_type |= DRF_IEEE;
         else if ((dummy & 0xFF) == 0x83 &&     /* big endian */
                  ((dummy >> 8) & 0xFF) == 0xC0 && ((dummy >> 16) & 0xFF) == 0xF3
                  && ((dummy >> 24) & 0xFF) == 0x3F)
            tmp_type |= DRF_IEEE;
         else if ((dummy & 0xFF) == 0x13 &&
                  ((dummy >> 8) & 0xFF) == 0x40 && ((dummy >> 16) & 0xFF) == 0x83
                  && ((dummy >> 24) & 0xFF) == 0xC0)
            tmp_type |= DRF_G_FLOAT;
         else if ((dummy & 0xFF) == 0x9E &&
                  ((dummy >> 8) & 0xFF) == 0x40 && ((dummy >> 16) & 0xFF) == 0x18
                  && ((dummy >> 24) & 0xFF) == 0x04)
            cm_msg(MERROR, "rpc_get_option",
                   "MIDAS cannot handle VAX D FLOAT format. Please compile with the /g_float flag");
         else
            cm_msg(MERROR, "rpc_get_option", "unknown floating point format");

         return tmp_type;
      }
   }
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Set RPC option
@param hConn              RPC connection handle, -1 for server connection, -2 for rpc connect timeout
@param item               One of RPC_Oxxx
@param value              Value to set
@return RPC_SUCCESS
*/
#if 0
INT rpc_set_option(HNDLE hConn, INT item, INT value) {
   switch (item) {
      case RPC_OTIMEOUT:
         if (hConn == -1)
            _server_connection.rpc_timeout = value;
         else if (hConn == -2)
            _rpc_connect_timeout = value;
         else {
            RPC_CLIENT_CONNECTION* c = rpc_get_locked_client_connection(hConn);
            if (c) {
               c->rpc_timeout = value;
               c->mutex.unlock();
            }
         }
         break;

      case RPC_NODELAY:
         if (hConn == -1) {
            setsockopt(_server_connection.send_sock, IPPROTO_TCP, TCP_NODELAY, (char *) &value, sizeof(value));
         } else {
            RPC_CLIENT_CONNECTION* c = rpc_get_locked_client_connection(hConn);
            if (c) {
               setsockopt(c->send_sock, IPPROTO_TCP, TCP_NODELAY, (char *) &value, sizeof(value));
               c->mutex.unlock();
            }
         }
         break;

      default:
         cm_msg(MERROR, "rpc_set_option", "invalid argument");
         break;
   }

   return 0;
}
#endif

/********************************************************************/
/**
Get RPC timeout
@param hConn              RPC connection handle, RPC_HNDLE_MSERVER for mserver connection, RPC_HNDLE_CONNECT for rpc connect timeout
@return timeout value
*/
INT rpc_get_timeout(HNDLE hConn)
{
   if (hConn == RPC_HNDLE_MSERVER) {
      return _server_connection.rpc_timeout;
   } else if (hConn == RPC_HNDLE_CONNECT) {
      return _rpc_connect_timeout;
   } else {
      RPC_CLIENT_CONNECTION* c = rpc_get_locked_client_connection(hConn);
      if (c) {
         int timeout = c->rpc_timeout;
         c->mutex.unlock();
         return timeout;
      }
   }
   return 0;
}

/********************************************************************/
/**
Set RPC timeout
@param hConn              RPC connection handle, RPC_HNDLE_MSERVER for mserver connection, RPC_HNDLE_CONNECT for rpc connect timeout
@param timeout_msec       RPC timeout in milliseconds
@param old_timeout_msec   returns old value of RPC timeout in milliseconds
@return RPC_SUCCESS
*/
INT rpc_set_timeout(HNDLE hConn, int timeout_msec, int* old_timeout_msec)
{
   //printf("rpc_set_timeout: hConn %d, timeout_msec %d\n", hConn, timeout_msec);

   if (hConn == RPC_HNDLE_MSERVER) {
      if (old_timeout_msec)
         *old_timeout_msec = _server_connection.rpc_timeout;
      _server_connection.rpc_timeout = timeout_msec;
   } else if (hConn == RPC_HNDLE_CONNECT) {
      if (old_timeout_msec)
         *old_timeout_msec = _rpc_connect_timeout;
      _rpc_connect_timeout = timeout_msec;
   } else {
      RPC_CLIENT_CONNECTION* c = rpc_get_locked_client_connection(hConn);
      if (c) {
         if (old_timeout_msec)
            *old_timeout_msec = c->rpc_timeout;
         c->rpc_timeout = timeout_msec;
         c->mutex.unlock();
      } else {
         if (old_timeout_msec)
            *old_timeout_msec = 0;
      }
   }
   return RPC_SUCCESS;
}


/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/********************************************************************/
INT rpc_get_convert_flags(void)
/********************************************************************\

  Routine: rpc_get_convert_flags

  Purpose: Get RPC convert_flags for the mserver connection

  Function value:
    INT                     Actual option

\********************************************************************/
{
   if (_mserver_acception)
      return _mserver_acception->convert_flags;
   else
      return 0;
}

static std::string _mserver_path;

/********************************************************************/
const char *rpc_get_mserver_path()
/********************************************************************\

  Routine: rpc_get_mserver_path()

  Purpose: Get path of the mserver executable

\********************************************************************/
{
   return _mserver_path.c_str();
}

/********************************************************************/
INT rpc_set_mserver_path(const char *path)
/********************************************************************\

  Routine: rpc_set_mserver_path

  Purpose: Remember the path of the mserver executable

  Input:
   char *path               Full path of the mserver executable

  Function value:
    RPC_SUCCESS             Successful completion

\********************************************************************/
{
   _mserver_path = path;
   return RPC_SUCCESS;
}

/********************************************************************/
std::string rpc_get_name()
/********************************************************************\

  Routine: rpc_get_name

  Purpose: Get name set by rpc_set_name

  Input:
    none

  Output:
    char*  name             The location pointed by *name receives a
                            copy of the _prog_name

  Function value:
    RPC_SUCCESS             Successful completion

\********************************************************************/
{
   return _client_name;
}


/********************************************************************/
INT rpc_set_name(const char *name)
/********************************************************************\

  Routine: rpc_set_name

  Purpose: Set name of actual program for further rpc connections

  Input:
   char *name               Program name, up to NAME_LENGTH chars,
                            no blanks

  Output:
    none

  Function value:
    RPC_SUCCESS             Successful completion

\********************************************************************/
{
   _client_name = name;

   return RPC_SUCCESS;
}


/********************************************************************/
INT rpc_set_debug(void (*func)(const char *), INT mode)
/********************************************************************\

  Routine: rpc_set_debug

  Purpose: Set a function which is called on every RPC call to
           display the function name and parameters of the RPC
           call.

  Input:
   void *func(char*)        Pointer to function.
   INT  mode                Debug mode

  Output:
    none

  Function value:
    RPC_SUCCESS             Successful completion

\********************************************************************/
{
   _debug_print = func;
   _debug_mode = mode;
   return RPC_SUCCESS;
}

/********************************************************************/
void rpc_debug_printf(const char *format, ...)
/********************************************************************\

  Routine: rpc_debug_print

  Purpose: Calls function set via rpc_set_debug to output a string.

  Input:
   char *str                Debug string

  Output:
    none

\********************************************************************/
{
   va_list argptr;
   char str[1000];

   if (_debug_mode) {
      va_start(argptr, format);
      vsprintf(str, (char *) format, argptr);
      va_end(argptr);

      if (_debug_print) {
         strcat(str, "\n");
         _debug_print(str);
      } else
         puts(str);
   }
}

/********************************************************************/
void rpc_va_arg(va_list *arg_ptr, INT arg_type, void *arg) {
   switch (arg_type) {
      /* On the stack, the minimum parameter size is sizeof(int).
         To avoid problems on little endian systems, treat all
         smaller parameters as int's */
      case TID_UINT8:
      case TID_INT8:
      case TID_CHAR:
      case TID_UINT16:
      case TID_INT16:
         *((int *) arg) = va_arg(*arg_ptr, int);
         break;

      case TID_INT32:
      case TID_BOOL:
         *((INT *) arg) = va_arg(*arg_ptr, INT);
         break;

      case TID_UINT32:
         *((DWORD *) arg) = va_arg(*arg_ptr, DWORD);
         break;

         /* float variables are passed as double by the compiler */
      case TID_FLOAT:
         *((float *) arg) = (float) va_arg(*arg_ptr, double);
         break;

      case TID_DOUBLE:
         *((double *) arg) = va_arg(*arg_ptr, double);
         break;

      case TID_ARRAY:
         *((char **) arg) = va_arg(*arg_ptr, char *);
         break;
   }
}

/********************************************************************/
static void rpc_call_encode(va_list& ap, int idx, const char* rpc_name, NET_COMMAND** nc)
{
   bool debug = false;

   if (debug)
      printf("encode rpc %d \"%s\"\n", idx, rpc_name);

   size_t buf_size = sizeof(NET_COMMAND) + 4 * 1024;
   char* buf = (char *)malloc(buf_size);
   assert(buf);

   (*nc) = (NET_COMMAND*) buf;

   /* find out if we are on a big endian system */
   bool bbig = ((rpc_get_hw_type() & DRI_BIG_ENDIAN) > 0);

   char* param_ptr = (*nc)->param;

   for (int i=0; rpc_list[idx].param[i].tid != 0; i++) {
      int tid = rpc_list[idx].param[i].tid;
      int flags = rpc_list[idx].param[i].flags;
      int arg_type = 0;

      bool bpointer = (flags & RPC_POINTER) || (flags & RPC_OUT) ||
                 (flags & RPC_FIXARRAY) || (flags & RPC_VARARRAY) ||
                 tid == TID_STRING || tid == TID_ARRAY || tid == TID_STRUCT || tid == TID_LINK;

      if (bpointer)
         arg_type = TID_ARRAY;
      else
         arg_type = tid;

      /* floats are passed as doubles, at least under NT */
      if (tid == TID_FLOAT && !bpointer)
         arg_type = TID_DOUBLE;

      /* get pointer to argument */
      char arg[8];
      rpc_va_arg(&ap, arg_type, arg);

      /* shift 1- and 2-byte parameters to the LSB on big endian systems */
      if (bbig) {
         if (tid == TID_UINT8 || tid == TID_CHAR || tid == TID_INT8) {
            arg[0] = arg[3];
         }
         if (tid == TID_UINT16 || tid == TID_INT16) {
            arg[0] = arg[2];
            arg[1] = arg[3];
         }
      }

      if (flags & RPC_IN) {
         int arg_size = 0;

         if (bpointer)
            arg_size = rpc_tid_size(tid);
         else
            arg_size = rpc_tid_size(arg_type);

         /* for strings, the argument size depends on the string length */
         if (tid == TID_STRING || tid == TID_LINK)
            arg_size = 1 + strlen((char *) *((char **) arg));

         /* for varibale length arrays, the size is given by
            the next parameter on the stack */
         if (flags & RPC_VARARRAY) {
            va_list aptmp;
            memcpy(&aptmp, &ap, sizeof(ap));

            char arg_tmp[8];
            rpc_va_arg(&aptmp, TID_ARRAY, arg_tmp);

            /* for (RPC_IN+RPC_OUT) parameters, size argument is a pointer */
            if (flags & RPC_OUT)
               arg_size = *((INT *) *((void **) arg_tmp));
            else
               arg_size = *((INT *) arg_tmp);

            *((INT *) param_ptr) = ALIGN8(arg_size);
            param_ptr += ALIGN8(sizeof(INT));
         }

         if (tid == TID_STRUCT || (flags & RPC_FIXARRAY))
            arg_size = rpc_list[idx].param[i].n;

         /* always align parameter size */
         int param_size = ALIGN8(arg_size);

         {
            size_t param_offset = (char *) param_ptr - (char *)(*nc);

            if (param_offset + param_size + 16 > buf_size) {
               size_t new_size = param_offset + param_size + 1024;
               buf = (char *) realloc(buf, new_size);
               assert(buf);
               buf_size = new_size;
               (*nc) = (NET_COMMAND*) buf;
               param_ptr = buf + param_offset;
            }
         }

         if (bpointer) {
            if (debug) {
               printf("encode param %d, flags 0x%x, tid %d, arg_type %d, arg_size %d, param_size %d, memcpy pointer %d\n", i, flags, tid, arg_type, arg_size, param_size, arg_size);
            }
            memcpy(param_ptr, (void *) *((void **) arg), arg_size);
         } else if (tid == TID_FLOAT) {
            if (debug) {
               printf("encode param %d, flags 0x%x, tid %d, arg_type %d, arg_size %d, param_size %d, double->float\n", i, flags, tid, arg_type, arg_size, param_size);
            }
            /* floats are passed as doubles on most systems */
            *((float *) param_ptr) = (float) *((double *) arg);
         } else {
            if (debug) {
               printf("encode param %d, flags 0x%x, tid %d, arg_type %d, arg_size %d, param_size %d, memcpy %d\n", i, flags, tid, arg_type, arg_size, param_size, arg_size);
            }
            memcpy(param_ptr, arg, arg_size);
         }

         param_ptr += param_size;
      }
   }

   (*nc)->header.param_size = (POINTER_T) param_ptr - (POINTER_T) (*nc)->param;

   if (debug)
      printf("encode rpc %d \"%s\" buf_size %d, param_size %d\n", idx, rpc_name, (int)buf_size, (*nc)->header.param_size);
}

/********************************************************************/
static int rpc_call_decode(va_list& ap, int idx, const char* rpc_name, const char* buf, size_t buf_size)
{
   bool debug = false;

   if (debug)
      printf("decode reply to rpc %d \"%s\" has %d bytes\n", idx, rpc_name, (int)buf_size);

   /* extract result variables and place it to argument list */

   const char* param_ptr = buf;

   for (int i = 0; rpc_list[idx].param[i].tid != 0; i++) {
      int tid = rpc_list[idx].param[i].tid;
      int flags = rpc_list[idx].param[i].flags;
      int arg_type = 0;

      bool bpointer = (flags & RPC_POINTER) || (flags & RPC_OUT) ||
                 (flags & RPC_FIXARRAY) || (flags & RPC_VARARRAY) ||
                 tid == TID_STRING || tid == TID_ARRAY || tid == TID_STRUCT || tid == TID_LINK;

      if (bpointer)
         arg_type = TID_ARRAY;
      else
         arg_type = rpc_list[idx].param[i].tid;

      if (tid == TID_FLOAT && !bpointer)
         arg_type = TID_DOUBLE;

      char arg[8];
      rpc_va_arg(&ap, arg_type, arg);

      if (rpc_list[idx].param[i].flags & RPC_OUT) {

         if (param_ptr == NULL) {
            cm_msg(MERROR, "rpc_call_decode", "routine \"%s\": no data in RPC reply, needed to decode an RPC_OUT parameter. param_ptr is NULL", rpc_list[idx].name);
            return RPC_NET_ERROR;
         }

         tid = rpc_list[idx].param[i].tid;
         int arg_size = rpc_tid_size(tid);

         if (tid == TID_STRING || tid == TID_LINK)
            arg_size = strlen((char *) (param_ptr)) + 1;

         if (flags & RPC_VARARRAY) {
            arg_size = *((INT *) param_ptr);
            param_ptr += ALIGN8(sizeof(INT));
         }

         if (tid == TID_STRUCT || (flags & RPC_FIXARRAY))
            arg_size = rpc_list[idx].param[i].n;

         /* parameter size is always aligned */
         int param_size = ALIGN8(arg_size);

         /* return parameters are always pointers */
         if (*((char **) arg)) {
            if (debug)
               printf("decode param %d, flags 0x%x, tid %d, arg_type %d, arg_size %d, param_size %d, memcpy %d\n", i, flags, tid, arg_type, arg_size, param_size, arg_size);
            memcpy((void *) *((char **) arg), param_ptr, arg_size);
         }

         param_ptr += param_size;
      }
   }

   return RPC_SUCCESS;
}

/********************************************************************/
INT rpc_client_call(HNDLE hConn, DWORD routine_id, ...)
/********************************************************************\

  Routine: rpc_client_call

  Purpose: Call a function on a MIDAS client

  Input:
    INT  hConn              Client connection
    INT  routine_id         routine ID as defined in RPC.H (RPC_xxx)

    ...                     variable argument list

  Output:
    (depends on argument list)

  Function value:
    RPC_SUCCESS             Successful completion
    RPC_NET_ERROR           Error in socket call
    RPC_NO_CONNECTION       No active connection
    RPC_TIMEOUT             Timeout in RPC call
    RPC_INVALID_ID          Invalid routine_id (not in rpc_list)
    RPC_EXCEED_BUFFER       Paramters don't fit in network buffer

\********************************************************************/
{
   RPC_CLIENT_CONNECTION* c = rpc_get_locked_client_connection(hConn);

   if (!c) {
      cm_msg(MERROR, "rpc_client_call", "invalid rpc connection handle %d", hConn);
      return RPC_NO_CONNECTION;
   }

   //printf("rpc_client_call: handle %d, connection: ", hConn);
   //c->print();
   //printf("\n");

   INT i, status;

   BOOL rpc_no_reply = routine_id & RPC_NO_REPLY;
   routine_id &= ~RPC_NO_REPLY;

   //if (rpc_no_reply)
   //   printf("rpc_client_call: routine_id %d, RPC_NO_REPLY\n", routine_id);

   // make local copy of the client name just in case _client_connection is erased by another thread

   /* find rpc_index */

   for (i = 0;; i++)
      if ((rpc_list[i].id == (int) routine_id) || (rpc_list[i].id == 0))
         break;

   int rpc_index = i;

   if (rpc_list[rpc_index].id == 0) {
      cm_msg(MERROR, "rpc_client_call", "call to \"%s\" on \"%s\" with invalid RPC ID %d", c->client_name.c_str(), c->host_name.c_str(), routine_id);
      c->mutex.unlock();
      return RPC_INVALID_ID;
   }

   const char *rpc_name = rpc_list[rpc_index].name;

   NET_COMMAND *nc = NULL;

   /* examine variable argument list and convert it to parameter array */
   va_list ap;
   va_start(ap, routine_id);

   rpc_call_encode(ap, rpc_index, rpc_name, &nc);

   va_end(ap);

   nc->header.routine_id = routine_id;

   if (rpc_no_reply)
      nc->header.routine_id |= RPC_NO_REPLY;

   int send_size = nc->header.param_size + sizeof(NET_COMMAND_HEADER);

   /* in FAST TCP mode, only send call and return immediately */
   if (rpc_no_reply) {
      i = send_tcp(c->send_sock, (char *) nc, send_size, 0);

      if (i != send_size) {
         cm_msg(MERROR, "rpc_client_call", "call to \"%s\" on \"%s\" RPC \"%s\": send_tcp() failed", c->client_name.c_str(), c->host_name.c_str(), rpc_name);
         free(nc);
         c->mutex.unlock();
         return RPC_NET_ERROR;
      }

      free(nc);

      if (routine_id == RPC_ID_EXIT || routine_id == RPC_ID_SHUTDOWN) {
         //printf("rpc_client_call: routine_id %d is RPC_ID_EXIT %d or RPC_ID_SHUTDOWN %d, closing connection: ", routine_id, RPC_ID_EXIT, RPC_ID_SHUTDOWN);
         //c->print();
         //printf("\n");
         c->close_locked();
      }

      c->mutex.unlock();
      return RPC_SUCCESS;
   }

   /* in TCP mode, send and wait for reply on send socket */
   i = send_tcp(c->send_sock, (char *) nc, send_size, 0);
   if (i != send_size) {
      cm_msg(MERROR, "rpc_client_call", "call to \"%s\" on \"%s\" RPC \"%s\": send_tcp() failed", c->client_name.c_str(), c->host_name.c_str(), rpc_name);
      c->mutex.unlock();
      return RPC_NET_ERROR;
   }

   free(nc);
   nc = NULL;

   bool restore_watchdog_timeout = false;
   BOOL watchdog_call;
   DWORD watchdog_timeout;
   cm_get_watchdog_params(&watchdog_call, &watchdog_timeout);

   //printf("watchdog timeout: %d, rpc_timeout: %d\n", watchdog_timeout, c->rpc_timeout);

   if (c->rpc_timeout >= (int) watchdog_timeout) {
      restore_watchdog_timeout = true;
      cm_set_watchdog_params(watchdog_call, c->rpc_timeout + 1000);
   }

   DWORD rpc_status = 0;
   DWORD buf_size = 0;
   char* buf = NULL;

   /* receive result on send socket */
   status = ss_recv_net_command(c->send_sock, &rpc_status, &buf_size, &buf, c->rpc_timeout);

   if (restore_watchdog_timeout) {
      cm_set_watchdog_params(watchdog_call, watchdog_timeout);
   }

   if (status == SS_TIMEOUT) {
      cm_msg(MERROR, "rpc_client_call", "call to \"%s\" on \"%s\" RPC \"%s\": timeout waiting for reply", c->client_name.c_str(), c->host_name.c_str(), rpc_name);
      if (buf)
         free(buf);
      c->mutex.unlock();
      return RPC_TIMEOUT;
   }

   if (status != SS_SUCCESS) {
      cm_msg(MERROR, "rpc_client_call", "call to \"%s\" on \"%s\" RPC \"%s\": error, ss_recv_net_command() status %d", c->client_name.c_str(), c->host_name.c_str(), rpc_name, status);
      if (buf)
         free(buf);
      c->mutex.unlock();
      return RPC_NET_ERROR;
   }

   c->mutex.unlock();

   /* extract result variables and place it to argument list */

   va_start(ap, routine_id);

   status = rpc_call_decode(ap, rpc_index, rpc_name, buf, buf_size);

   if (status != RPC_SUCCESS) {
      rpc_status = status;
   }

   va_end(ap);

   if (buf)
      free(buf);
   buf = NULL;
   buf_size = 0;

   return rpc_status;
}

/********************************************************************/
INT rpc_call(DWORD routine_id, ...)
/********************************************************************\

  Routine: rpc_call

  Purpose: Call a function on a MIDAS server

  Input:
    INT  routine_id         routine ID as defined in RPC.H (RPC_xxx)

    ...                     variable argument list

  Output:
    (depends on argument list)

  Function value:
    RPC_SUCCESS             Successful completion
    RPC_NET_ERROR           Error in socket call
    RPC_NO_CONNECTION       No active connection
    RPC_TIMEOUT             Timeout in RPC call
    RPC_INVALID_ID          Invalid routine_id (not in rpc_list)
    RPC_EXCEED_BUFFER       Paramters don't fit in network buffer

\********************************************************************/
{
   va_list ap;
   INT i, idx, status;

   BOOL rpc_no_reply = routine_id & RPC_NO_REPLY;
   routine_id &= ~RPC_NO_REPLY;

   //if (rpc_no_reply)
   //   printf("rpc_call: routine_id %d, RPC_NO_REPLY\n", routine_id);

   int send_sock = _server_connection.send_sock;
   int rpc_timeout = _server_connection.rpc_timeout;

   if (!send_sock) {
      fprintf(stderr, "rpc_call(routine_id=%d) failed, no connection to mserver.\n", routine_id);
      return RPC_NET_ERROR;
   }

   if (!_mutex_rpc) {
      /* create a local mutex for multi-threaded applications */
      ss_mutex_create(&_mutex_rpc, FALSE);
   }

   status = ss_mutex_wait_for(_mutex_rpc, 10000 + rpc_timeout);
   if (status != SS_SUCCESS) {
      cm_msg(MERROR, "rpc_call", "Mutex timeout");
      return RPC_MUTEX_TIMEOUT;
   }

   /* find rpc definition */

   if (rpc_list == NULL) {
      ss_mutex_release(_mutex_rpc);
      // cannot call cm_msg() here as it can cause recursive message loop after RPC connection was shut down. K.O.
      //cm_msg(MERROR, "rpc_call", "cannot call rpc ID (%d), rpc_list is NULL", routine_id);
      return RPC_INVALID_ID;
   }

   for (i = 0;; i++)
      if ((rpc_list[i].id == (int) routine_id) || (rpc_list[i].id == 0))
         break;
   idx = i;
   if (rpc_list[i].id == 0) {
      ss_mutex_release(_mutex_rpc);
      cm_msg(MERROR, "rpc_call", "invalid rpc ID (%d)", routine_id);
      return RPC_INVALID_ID;
   }

   const char* rpc_name = rpc_list[idx].name;

   /* prepare output buffer */

   NET_COMMAND* nc = NULL;

   /* examine variable argument list and convert it to parameter array */
   va_start(ap, routine_id);

   rpc_call_encode(ap, idx, rpc_name, &nc);

   va_end(ap);

   nc->header.routine_id = routine_id;

   if (rpc_no_reply)
      nc->header.routine_id |= RPC_NO_REPLY;

   int send_size = nc->header.param_size + sizeof(NET_COMMAND_HEADER);

   /* do not wait for reply if requested RPC_NO_REPLY */
   if (rpc_no_reply) {
      i = send_tcp(send_sock, (char *) nc, send_size, 0);

      if (i != send_size) {
         ss_mutex_release(_mutex_rpc);
         cm_msg(MERROR, "rpc_call", "rpc \"%s\" error: send_tcp() failed", rpc_name);
         free(nc);
         return RPC_NET_ERROR;
      }

      ss_mutex_release(_mutex_rpc);
      free(nc);
      return RPC_SUCCESS;
   }

   /* in TCP mode, send and wait for reply on send socket */
   i = send_tcp(send_sock, (char *) nc, send_size, 0);
   if (i != send_size) {
      ss_mutex_release(_mutex_rpc);
      cm_msg(MERROR, "rpc_call", "rpc \"%s\" error: send_tcp() failed", rpc_name);
      free(nc);
      return RPC_NET_ERROR;
   }

   free(nc);
   nc = NULL;

   bool restore_watchdog_timeout = false;
   BOOL watchdog_call;
   DWORD watchdog_timeout;
   cm_get_watchdog_params(&watchdog_call, &watchdog_timeout);

   //printf("watchdog timeout: %d, rpc_timeout: %d\n", watchdog_timeout, rpc_timeout);

   if (!rpc_is_remote()) {
      // if RPC is remote, we are connected to an mserver,
      // the mserver takes care of watchdog timeouts.
      // otherwise we should make sure the watchdog timeout
      // is longer than the RPC timeout. K.O.
      if (rpc_timeout >= (int) watchdog_timeout) {
         restore_watchdog_timeout = true;
         cm_set_watchdog_params_local(watchdog_call, rpc_timeout + 1000);
      }
   }

   DWORD rpc_status = 0;
   DWORD buf_size = 0;
   char* buf = NULL;

   status = ss_recv_net_command(send_sock, &rpc_status, &buf_size, &buf, rpc_timeout);

   if (restore_watchdog_timeout) {
      cm_set_watchdog_params_local(watchdog_call, watchdog_timeout);
   }

   /* drop the mutex, we are done with the socket, argument unpacking is done from our own buffer */

   ss_mutex_release(_mutex_rpc);

   /* check for reply errors */

   if (status == SS_TIMEOUT) {
      cm_msg(MERROR, "rpc_call", "routine \"%s\": timeout waiting for reply, program abort", rpc_list[idx].name);
      if (buf)
         free(buf);
      abort(); // cannot continue - our mserver is not talking to us!
      return RPC_TIMEOUT;
   }

   if (status != SS_SUCCESS) {
      cm_msg(MERROR, "rpc_call", "routine \"%s\": error, ss_recv_net_command() status %d, program abort",
             rpc_list[idx].name, status);
      if (buf)
         free(buf);
      abort(); // cannot continue - something is wrong with our mserver connection
      return RPC_NET_ERROR;
   }

   /* extract result variables and place it to argument list */

   va_start(ap, routine_id);

   status = rpc_call_decode(ap, idx, rpc_name, buf, buf_size);

   if (status != RPC_SUCCESS) {
      rpc_status = status;
   }

   va_end(ap);

   if (buf)
      free(buf);

   return rpc_status;
}


/********************************************************************/
INT rpc_set_opt_tcp_size(INT tcp_size) {
   INT old;

   old = _opt_tcp_size;
   _opt_tcp_size = tcp_size;
   return old;
}

INT rpc_get_opt_tcp_size() {
   return _opt_tcp_size;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Fast send_event routine which bypasses the RPC layer and
           sends the event directly at the TCP level.
@param buffer_handle      Handle of the buffer to send the event to.
                          Must be obtained via bm_open_buffer.
@param source             Address of the event to send. It must have
                          a proper event header.
@param buf_size           Size of event in bytes with header.
@param async_flag         BM_WAIT / BM_NO_WAIT flag. In BM_NO_WAIT mode, the
                          function returns immediately if it cannot
                          send the event over the network. In BM_WAIT
                          mode, it waits until the packet is sent
                          (blocking).
@param mode               Determines in which mode the event is sent.
                          If zero, use RPC socket, if one, use special
                          event socket to bypass RPC layer on the
                          server side.

@return BM_INVALID_PARAM, BM_ASYNC_RETURN, RPC_SUCCESS, RPC_NET_ERROR,
        RPC_NO_CONNECTION, RPC_EXCEED_BUFFER
*/
INT rpc_send_event(INT buffer_handle, const EVENT_HEADER *pevent, int unused, INT async_flag, INT mode)
{
   if (rpc_is_remote()) {
      return rpc_send_event1(buffer_handle, pevent);
   } else {
      return bm_send_event(buffer_handle, pevent, unused, async_flag);
   }
}

/********************************************************************/
/**
Send event to mserver using the event socket connection, bypassing the RPC layer
@param buffer_handle      Handle of the buffer to send the event to.
                          Must be obtained via bm_open_buffer.
@param event              Pointer to event header

@return RPC_SUCCESS, RPC_NET_ERROR, RPC_NO_CONNECTION
*/
INT rpc_send_event1(INT buffer_handle, const EVENT_HEADER *pevent)
{
   const size_t event_size = sizeof(EVENT_HEADER) + pevent->data_size;
   return rpc_send_event_sg(buffer_handle, 1, (char**)&pevent, &event_size);
}

INT rpc_send_event_sg(INT buffer_handle, int sg_n, const char* const sg_ptr[], const size_t sg_len[])
{
   if (sg_n < 1) {
      cm_msg(MERROR, "rpc_send_event_sg", "invalid sg_n %d", sg_n);
      return BM_INVALID_SIZE;
   }

   if (sg_ptr[0] == NULL) {
      cm_msg(MERROR, "rpc_send_event_sg", "invalid sg_ptr[0] is NULL");
      return BM_INVALID_SIZE;
   }

   if (sg_len[0] < sizeof(EVENT_HEADER)) {
      cm_msg(MERROR, "rpc_send_event_sg", "invalid sg_len[0] value %d is smaller than event header size %d", (int)sg_len[0], (int)sizeof(EVENT_HEADER));
      return BM_INVALID_SIZE;
   }

   const EVENT_HEADER* pevent = (const EVENT_HEADER*)sg_ptr[0];
   
   const DWORD MAX_DATA_SIZE = (0x7FFFFFF0 - 16); // event size computations are not 32-bit clean, limit event size to 2GB. K.O.
   const DWORD data_size = pevent->data_size; // 32-bit unsigned value

   if (data_size == 0) {
      cm_msg(MERROR, "rpc_send_event_sg", "invalid event data size zero");
      return BM_INVALID_SIZE;
   }

   if (data_size > MAX_DATA_SIZE) {
      cm_msg(MERROR, "rpc_send_event_sg", "invalid event data size %d (0x%x) maximum is %d (0x%x)", data_size, data_size, MAX_DATA_SIZE, MAX_DATA_SIZE);
      return BM_INVALID_SIZE;
   }

   const size_t event_size = sizeof(EVENT_HEADER) + data_size;
   const size_t total_size = ALIGN8(event_size);

   size_t count = 0;
   for (int i=0; i<sg_n; i++) {
      count += sg_len[i];
   }

   if (count != event_size) {
      cm_msg(MERROR, "rpc_send_event_sg", "data size mismatch: event data_size %d, event_size %d not same as sum of sg_len %d", (int)data_size, (int)event_size, (int)count);
      return BM_INVALID_SIZE;
   }

   // protect non-atomic access to _server_connection.event_sock. K.O.
   
   std::lock_guard<std::mutex> guard(_server_connection.event_sock_mutex);

   //printf("rpc_send_event_sg: pevent %p, event_id 0x%04x, serial 0x%08x, data_size %d, event_size %d, total_size %d\n", pevent, pevent->event_id, pevent->serial_number, (int)data_size, (int)event_size, (int)total_size);

   if (_server_connection.event_sock == 0) {
      return RPC_NO_CONNECTION;
   }

   //
   // event socket wire protocol: (see also rpc_server_receive_event() and recv_event_server_realloc())
   //
   // 4 bytes of buffer handle
   // 16 bytes of event header, includes data_size
   // ALIGN8(data_size) bytes of event data
   // 

   int status;

   /* send buffer handle */

   assert(sizeof(DWORD) == 4);
   DWORD bh_buf = buffer_handle;
   
   status = ss_write_tcp(_server_connection.event_sock, (const char *) &bh_buf, sizeof(DWORD));
   if (status != SS_SUCCESS) {
      closesocket(_server_connection.event_sock);
      _server_connection.event_sock = 0;
      cm_msg(MERROR, "rpc_send_event_sg", "ss_write_tcp(buffer handle) failed, event socket is now closed");
      return RPC_NET_ERROR;
   }

   /* send data */

   for (int i=0; i<sg_n; i++) {
      status = ss_write_tcp(_server_connection.event_sock, sg_ptr[i], sg_len[i]);
      if (status != SS_SUCCESS) {
         closesocket(_server_connection.event_sock);
         _server_connection.event_sock = 0;
         cm_msg(MERROR, "rpc_send_event_sg", "ss_write_tcp(event data) failed, event socket is now closed");
         return RPC_NET_ERROR;
      }
   }

   /* send padding */

   if (count < total_size) {
      char padding[8] = { 0,0,0,0,0,0,0,0 };
      size_t padlen = total_size - count;
      assert(padlen < 8);
      status = ss_write_tcp(_server_connection.event_sock, padding, padlen);
      if (status != SS_SUCCESS) {
         closesocket(_server_connection.event_sock);
         _server_connection.event_sock = 0;
         cm_msg(MERROR, "rpc_send_event_sg", "ss_write_tcp(padding) failed, event socket is now closed");
         return RPC_NET_ERROR;
      }
   }

   return RPC_SUCCESS;
}

/********************************************************************/
/**
Send event residing in the TCP cache buffer filled by
           rpc_send_event. This routine should be called when a
           run is stopped.

@return RPC_SUCCESS, RPC_NET_ERROR
*/
INT rpc_flush_event() {
   return RPC_SUCCESS;
}

/********************************************************************/

struct TR_FIFO {
   int transition = 0;
   int run_number = 0;
   time_t trans_time = 0;
   int sequence_number = 0;
};

static std::mutex _tr_fifo_mutex;
static TR_FIFO _tr_fifo[10];
static int _tr_fifo_wp = 0;
static int _tr_fifo_rp = 0;

static INT rpc_transition_dispatch(INT idx, void *prpc_param[])
/********************************************************************\

  Routine: rpc_transition_dispatch

  Purpose: Gets called when a transition function was registered and
           a transition occured. Internal use only.

  Input:
    INT    idx              RPC function ID
    void   *prpc_param      RPC parameters

  Output:
    none

  Function value:
    INT    return value from called user routine

\********************************************************************/
{
   /* erase error string */
   *(CSTRING(2)) = 0;

   if (idx == RPC_RC_TRANSITION) {
      // find registered handler
      // NB: this code should match same code in cm_transition_call_direct()
      // NB: only use the first handler, this is how MIDAS always worked
      // NB: we could run all handlers, but we can return the status and error string of only one of them.
      _trans_table_mutex.lock();
      size_t n = _trans_table.size();
      _trans_table_mutex.unlock();
         
      for (size_t i = 0; i < n; i++) {
         _trans_table_mutex.lock();
         TRANS_TABLE tt = _trans_table[i];
         _trans_table_mutex.unlock();
         
         if (tt.transition == CINT(0) && tt.sequence_number == CINT(4)) {
            if (tt.func) {
               /* execute callback if defined */
               return tt.func(CINT(1), CSTRING(2));
            } else {
               std::lock_guard<std::mutex> guard(_tr_fifo_mutex);
               /* store transition in FIFO */
               _tr_fifo[_tr_fifo_wp].transition = CINT(0);
               _tr_fifo[_tr_fifo_wp].run_number = CINT(1);
               _tr_fifo[_tr_fifo_wp].trans_time = time(NULL);
               _tr_fifo[_tr_fifo_wp].sequence_number = CINT(4);
               _tr_fifo_wp = (_tr_fifo_wp + 1) % 10;
               // implicit unlock
               return RPC_SUCCESS;
            }
         }
      }
      // no handler for this transition
      cm_msg(MERROR, "rpc_transition_dispatch", "no handler for transition %d with sequence number %d", CINT(0), CINT(4));
      return CM_SUCCESS;
   } else {
      cm_msg(MERROR, "rpc_transition_dispatch", "received unrecognized command %d", idx);
      return RPC_INVALID_ID;
   }
}

/********************************************************************/
int cm_query_transition(int *transition, int *run_number, int *trans_time)
/********************************************************************\

  Routine: cm_query_transition

  Purpose: Query system if transition has occured. Normally, one
           registers callbacks for transitions via
           cm_register_transition. In some environments however,
           callbacks are not possible. In that case one spciefies
           a NULL pointer as the callback routine and can query
           transitions "manually" by calling this functions. A small
           FIFO takes care that no transition is lost if this functions
           did not get called between some transitions.

  Output:
    INT   *transition        Type of transition, one of TR_xxx
    INT   *run_nuber         Run number for transition
    time_t *trans_time       Time (in UNIX time) of transition

  Function value:
    FALSE  No transition occured since last call
    TRUE   Transition occured

\********************************************************************/
{
   std::lock_guard<std::mutex> guard(_tr_fifo_mutex);

   if (_tr_fifo_wp == _tr_fifo_rp)
      return FALSE;

   if (transition)
      *transition = _tr_fifo[_tr_fifo_rp].transition;

   if (run_number)
      *run_number = _tr_fifo[_tr_fifo_rp].run_number;

   if (trans_time)
      *trans_time = (int) _tr_fifo[_tr_fifo_rp].trans_time;

   _tr_fifo_rp = (_tr_fifo_rp + 1) % 10;

   // implicit unlock
   return TRUE;
}

/********************************************************************\
*                        server functions                            *
\********************************************************************/

#if 0
void debug_dump(unsigned char *p, int size)
{
   int i, j;
   unsigned char c;

   for (i = 0; i < (size - 1) / 16 + 1; i++) {
      printf("%p ", p + i * 16);
      for (j = 0; j < 16; j++)
         if (i * 16 + j < size)
            printf("%02X ", p[i * 16 + j]);
         else
            printf("   ");
      printf(" ");

      for (j = 0; j < 16; j++) {
         c = p[i * 16 + j];
         if (i * 16 + j < size)
            printf("%c", (c >= 32 && c < 128) ? p[i * 16 + j] : '.');
      }
      printf("\n");
   }

   printf("\n");
}
#endif

/********************************************************************/
static int recv_net_command_realloc(INT idx, char **pbuf, int *pbufsize, INT *remaining)
/********************************************************************\

  Routine: recv_net_command

  Purpose: TCP receive routine with local cache. To speed up network
           performance, a 64k buffer is read in at once and split into
           several RPC command on successive calls to recv_net_command.
           Therefore, the number of recv() calls is minimized.

           This routine is ment to be called by the server process.
           Clients should call recv_tcp instead.

  Input:
    INT   idx                Index of server connection
    DWORD buffer_size        Size of the buffer in bytes.
    INT   flags              Flags passed to recv()
    INT   convert_flags      Convert flags needed for big/little
                             endian conversion

  Output:
    char  *buffer            Network receive buffer.
    INT   *remaining         Remaining data in cache

  Function value:
    INT                      Same as recv()

\********************************************************************/
{
   char *buffer = NULL; // buffer is changed to point to *pbuf when we receive the NET_COMMAND header

   RPC_SERVER_ACCEPTION* sa = rpc_get_server_acception(idx);

   int sock = sa->recv_sock;

   if (!sa->net_buffer) {
      if (sa->is_mserver)
         sa->net_buffer_size = NET_TCP_SIZE;
      else
         sa->net_buffer_size = NET_BUFFER_SIZE;

      sa->net_buffer = (char *) malloc(sa->net_buffer_size);
      //printf("sa %p idx %d, net_buffer %p+%d\n", sa, idx, sa->net_buffer, sa->net_buffer_size);
      sa->write_ptr = 0;
      sa->read_ptr = 0;
      sa->misalign = 0;
   }
   if (!sa->net_buffer) {
      cm_msg(MERROR, "recv_net_command", "Cannot allocate %d bytes for network buffer", sa->net_buffer_size);
      return -1;
   }

   int copied = 0;
   int param_size = -1;

   int write_ptr = sa->write_ptr;
   int read_ptr = sa->read_ptr;
   int misalign = sa->misalign;
   char *net_buffer = sa->net_buffer;

   do {
      if (write_ptr - read_ptr >= (INT) sizeof(NET_COMMAND_HEADER) - copied) {
         if (param_size == -1) {
            if (copied > 0) {
               /* assemble split header */
               memcpy(buffer + copied, net_buffer + read_ptr, (INT) sizeof(NET_COMMAND_HEADER) - copied);
               NET_COMMAND *nc = (NET_COMMAND *) (buffer);
               param_size = (INT) nc->header.param_size;
            } else {
               NET_COMMAND *nc = (NET_COMMAND *) (net_buffer + read_ptr);
               param_size = (INT) nc->header.param_size;
            }

            if (sa->convert_flags)
               rpc_convert_single(&param_size, TID_UINT32, 0, sa->convert_flags);
         }

         //printf("recv_net_command: param_size %d, NET_COMMAND_HEADER %d, buffer_size %d\n", param_size, (int)sizeof(NET_COMMAND_HEADER), *pbufsize);

         /* check if parameters fit in buffer */
         if (*pbufsize < (param_size + (int) sizeof(NET_COMMAND_HEADER))) {
            int new_size = param_size + sizeof(NET_COMMAND_HEADER) + 1024;
            char *p = (char *) realloc(*pbuf, new_size);
            //printf("recv_net_command: reallocate buffer %d -> %d, %p\n", *pbufsize, new_size, p);
            if (p == NULL) {
               cm_msg(MERROR, "recv_net_command", "cannot reallocate buffer from %d bytes to %d bytes", *pbufsize, new_size);
               sa->read_ptr = 0;
               sa->write_ptr = 0;
               return -1;
            }
            *pbuf = p;
            *pbufsize = new_size;
         }

         buffer = *pbuf;

         /* check if we have all parameters in buffer */
         if (write_ptr - read_ptr >= param_size + (INT) sizeof(NET_COMMAND_HEADER) - copied)
            break;
      }

      /* not enough data, so copy partially and get new */
      int size = write_ptr - read_ptr;

      if (size > 0) {
         memcpy(buffer + copied, net_buffer + read_ptr, size);
         copied += size;
         read_ptr = write_ptr;
      }
#ifdef OS_UNIX
      do {
         write_ptr = recv(sock, net_buffer + misalign, sa->net_buffer_size - 8, 0);

         /* don't return if an alarm signal was cought */
      } while (write_ptr == -1 && errno == EINTR);
#else
      write_ptr = recv(sock, net_buffer + misalign, sa->net_buffer_size - 8, 0);
#endif

      /* abort if connection broken */
      if (write_ptr <= 0) {
         if (write_ptr == 0)
            cm_msg(MERROR, "recv_net_command", "rpc connection from \'%s\' on \'%s\' unexpectedly closed", sa->prog_name.c_str(), sa->host_name.c_str());
         else
            cm_msg(MERROR, "recv_net_command", "recv() returned %d, errno: %d (%s)", write_ptr, errno, strerror(errno));

         if (remaining)
            *remaining = 0;

         return write_ptr;
      }

      read_ptr = misalign;
      write_ptr += misalign;

      misalign = write_ptr % 8;
   } while (TRUE);

   /* copy rest of parameters */
   int size = param_size + sizeof(NET_COMMAND_HEADER) - copied;
   memcpy(buffer + copied, net_buffer + read_ptr, size);
   read_ptr += size;

   if (remaining) {
      /* don't keep rpc_server_receive in an infinite loop */
      if (write_ptr - read_ptr < param_size)
         *remaining = 0;
      else
         *remaining = write_ptr - read_ptr;
   }

   sa->write_ptr = write_ptr;
   sa->read_ptr = read_ptr;
   sa->misalign = misalign;

   return size + copied;
}


/********************************************************************/
INT recv_tcp_check(int sock)
/********************************************************************\

  Routine: recv_tcp_check

  Purpose: Check if in TCP receive buffer associated with sock is
           some data. Called by ss_suspend.

  Input:
    INT   sock               TCP receive socket

  Output:
    none

  Function value:
    INT   count              Number of bytes remaining in TCP buffer

\********************************************************************/
{
   /* figure out to which connection socket belongs */
   for (unsigned idx = 0; idx < _server_acceptions.size(); idx++)
      if (_server_acceptions[idx] && _server_acceptions[idx]->recv_sock == sock) {
         return _server_acceptions[idx]->write_ptr - _server_acceptions[idx]->read_ptr;
      }

   return 0;
}


/********************************************************************/
static int recv_event_server_realloc(INT idx, RPC_SERVER_ACCEPTION* psa, char **pbuffer, int *pbuffer_size)
/********************************************************************\

  Routine: recv_event_server_realloc

  Purpose: receive events sent by rpc_send_event()

  Input:
    INT   idx                Index of server connection
    DWORD buffer_size        Size of the buffer in bytes.
    INT   flags              Flags passed to recv()
    INT   convert_flags      Convert flags needed for big/little
                             endian conversion

  Output:
    char  *buffer            Network receive buffer.
    INT   *remaining         Remaining data in cache

  Function value:
    INT                      Same as recv()

\********************************************************************/
{
   int sock = psa->event_sock;

   //printf("recv_event_server: idx %d, buffer %p, buffer_size %d\n", idx, buffer, buffer_size);

   const size_t header_size = (sizeof(EVENT_HEADER) + sizeof(INT));

   char header_buf[header_size];

   // First read the header.
   //
   // Data format is:
   // INT buffer handle (4 bytes)
   // EVENT_HEADER (16 bytes)
   // event data
   // ALIGN8() padding
   // ...next event

   int hrd = recv_tcp2(sock, header_buf, header_size, 1);

   if (hrd == 0) {
      // timeout waiting for data
      return 0;
   }

   /* abort if connection broken */
   if (hrd < 0) {
      cm_msg(MERROR, "recv_event_server", "recv_tcp2(header) returned %d", hrd);
      return -1;
   }

   if (hrd < (int) header_size) {
      int hrd1 = recv_tcp2(sock, header_buf + hrd, header_size - hrd, 0);

      /* abort if connection broken */
      if (hrd1 <= 0) {
         cm_msg(MERROR, "recv_event_server", "recv_tcp2(more header) returned %d", hrd1);
         return -1;
      }

      hrd += hrd1;
   }

   /* abort if connection broken */
   if (hrd != (int) header_size) {
      cm_msg(MERROR, "recv_event_server", "recv_tcp2(header) returned %d instead of %d", hrd, (int) header_size);
      return -1;
   }

   INT *pbh = (INT *) header_buf;
   EVENT_HEADER *pevent = (EVENT_HEADER *) (((INT *) header_buf) + 1);

   /* convert header little endian/big endian */
   if (psa->convert_flags) {
      rpc_convert_single(&pbh, TID_INT32, 0, psa->convert_flags);
      rpc_convert_single(&pevent->event_id, TID_INT16, 0, psa->convert_flags);
      rpc_convert_single(&pevent->trigger_mask, TID_INT16, 0, psa->convert_flags);
      rpc_convert_single(&pevent->serial_number, TID_UINT32, 0, psa->convert_flags);
      rpc_convert_single(&pevent->time_stamp, TID_UINT32, 0, psa->convert_flags);
      rpc_convert_single(&pevent->data_size, TID_UINT32, 0, psa->convert_flags);
   }

   int event_size = pevent->data_size + sizeof(EVENT_HEADER);
   int total_size = ALIGN8(event_size);

   //printf("recv_event_server: buffer_handle %d, event_id 0x%04x, serial 0x%08x, data_size %d, event_size %d, total_size %d\n", *pbh, pevent->event_id, pevent->serial_number, pevent->data_size, event_size, total_size);

   if (pevent->data_size == 0) {
      for (int i=0; i<5; i++) {
         printf("recv_event_server: header[%d]: 0x%08x\n", i, pbh[i]);
      }
      abort();
   }

   /* check for sane event size */
   if (event_size <= 0 || total_size <= 0) {
      cm_msg(MERROR, "recv_event_server",
             "received event header with invalid data_size %d: event_size %d, total_size %d", pevent->data_size,
             event_size, total_size);
      return -1;
   }

   //printf("recv_event_server: idx %d, bh %d, event header: id %d, mask %d, serial %d, data_size %d, event_size %d, total_size %d\n", idx, *pbh, pevent->event_id, pevent->trigger_mask, pevent->serial_number, pevent->data_size, event_size, total_size);


   int bufsize = sizeof(INT) + total_size;

   // Second, check that output buffer is big enough

   /* check if data part fits in buffer */
   if (*pbuffer_size < bufsize) {
      int newsize = 1024 + ALIGN8(bufsize);

      //printf("recv_event_server: buffer realloc %d -> %d\n", *pbuffer_size, newsize);

      char *newbuf = (char *) realloc(*pbuffer, newsize);
      if (newbuf == NULL) {
         cm_msg(MERROR, "recv_event_server", "cannot realloc() event buffer from %d to %d bytes", *pbuffer_size,
                newsize);
         return -1;
      }
      *pbuffer = newbuf;
      *pbuffer_size = newsize;
   }

   // Third, copy header into output buffer

   memcpy(*pbuffer, header_buf, header_size);

   // Forth, read the event data

   int to_read = sizeof(INT) + total_size - header_size;
   int rptr = header_size;

   if (to_read > 0) {
      int drd = recv_tcp2(sock, (*pbuffer) + rptr, to_read, 0);

      /* abort if connection broken */
      if (drd <= 0) {
         cm_msg(MERROR, "recv_event_server", "recv_tcp2(data) returned %d instead of %d", drd, to_read);
         return -1;
      }
   }

   return bufsize;
}


/********************************************************************/
INT rpc_register_server(int port, int *plsock, int *pport)
/********************************************************************\

  Routine: rpc_register_listener

  Purpose: Register the calling process as a MIDAS RPC server. Note
           that cm_connnect_experiment must be called prior to any call of
           rpc_register_server.

  Input:
    INT   port              TCP port for listen. If port==0, the OS chooses a free port and returns it in *pport

  Output:
    int   *plsock           Listener socket, can be NULL
    int   *pport            Port under which server is listening, can be NULL

  Function value:
    RPC_SUCCESS             Successful completion
    RPC_NET_ERROR           Error in socket call
    RPC_NOT_REGISTERED      cm_connect_experiment was not called

\********************************************************************/
{
   int status;
   int lsock;

   status = rpc_register_listener(port, NULL, &lsock, pport);
   if (status != RPC_SUCCESS)
      return status;

   status = ss_suspend_set_client_listener(lsock);
   if (status != SS_SUCCESS)
      return status;

   if (plsock)
      *plsock = lsock;

   return RPC_SUCCESS;
}

/********************************************************************/
INT rpc_register_listener(int port, RPC_HANDLER func, int *plsock, int *pport)
/********************************************************************\

  Routine: rpc_register_listener

  Purpose: Register the calling process as a MIDAS RPC server. Note
           that cm_connnect_experiment must be called prior to any call of
           rpc_register_listener.

  Input:
    INT   port              TCP port for listen. If port==0, the OS chooses a free port and returns it in *pport
    INT   *func             Default dispatch function

  Output:
    int   *plsock           Listener socket, should not be NULL
    int   *pport            Port under which server is listening, can be NULL

  Function value:
    RPC_SUCCESS             Successful completion
    RPC_NET_ERROR           Error in socket call
    RPC_NOT_REGISTERED      cm_connect_experiment was not called

\********************************************************************/
{
   struct sockaddr_in bind_addr;
   INT status, flag;

#ifdef OS_WINNT
   {
      WSADATA WSAData;

      /* Start windows sockets */
      if (WSAStartup(MAKEWORD(1, 1), &WSAData) != 0)
         return RPC_NET_ERROR;
   }
#endif

   /* register system functions: RPC_ID_EXIT, RPC_ID_SHUTDOWN, RPC_ID_WATCHDOG */
   rpc_register_functions(rpc_get_internal_list(0), func);

   /* create a socket for listening */
   int lsock = socket(AF_INET, SOCK_STREAM, 0);
   if (lsock == -1) {
      cm_msg(MERROR, "rpc_register_server", "socket(AF_INET, SOCK_STREAM) failed, errno %d (%s)", errno,
             strerror(errno));
      return RPC_NET_ERROR;
   }

   /* set close-on-exec flag to prevent child mserver processes from inheriting the listen socket */
#if defined(F_SETFD) && defined(FD_CLOEXEC)
   status = fcntl(lsock, F_SETFD, fcntl(lsock, F_GETFD) | FD_CLOEXEC);
   if (status < 0) {
      cm_msg(MERROR, "rpc_register_server", "fcntl(F_SETFD, FD_CLOEXEC) failed, errno %d (%s)", errno, strerror(errno));
      return RPC_NET_ERROR;
   }
#endif

   /* reuse address, needed if previous server stopped (30s timeout!) */
   flag = 1;
   status = setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, (char *) &flag, sizeof(INT));
   if (status < 0) {
      cm_msg(MERROR, "rpc_register_server", "setsockopt(SO_REUSEADDR) failed, errno %d (%s)", errno, strerror(errno));
      return RPC_NET_ERROR;
   }

   /* bind local node name and port to socket */
   memset(&bind_addr, 0, sizeof(bind_addr));
   bind_addr.sin_family = AF_INET;

   if (!disable_bind_rpc_to_localhost) {
      bind_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   } else {
      bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
   }

   if (!port)
      bind_addr.sin_port = htons(0); // OS will allocate a port number for us
   else
      bind_addr.sin_port = htons((short) port);

   status = bind(lsock, (struct sockaddr *) &bind_addr, sizeof(bind_addr));
   if (status < 0) {
      cm_msg(MERROR, "rpc_register_server", "bind() to port %d failed, errno %d (%s)", port, errno, strerror(errno));
      return RPC_NET_ERROR;
   }

   /* listen for connection */
#ifdef OS_MSDOS
   status = listen(lsock, 1);
#else
   status = listen(lsock, SOMAXCONN);
#endif
   if (status < 0) {
      cm_msg(MERROR, "rpc_register_server", "listen() failed, errno %d (%s)", errno, strerror(errno));
      return RPC_NET_ERROR;
   }

   /* return port wich OS has choosen */
   if (pport) {
      socklen_t sosize = sizeof(bind_addr);
#ifdef OS_WINNT
      getsockname(lsock, (struct sockaddr *) &bind_addr, (int *) &sosize);
#else
      getsockname(lsock, (struct sockaddr *) &bind_addr, &sosize);
#endif
      *pport = ntohs(bind_addr.sin_port);
   }

   assert(plsock); // must be able to return the new listener socket, otherwise it's a socket leak

   *plsock = lsock;

   //printf("rpc_register_server: requested port %d, actual port %d, socket %d\n", port, *pport, *plsock);

   return RPC_SUCCESS;
}

typedef struct {
   midas_thread_t thread_id;
   int buffer_size;
   char *buffer;
} TLS_POINTER;

static TLS_POINTER *tls_buffer = NULL;
static int tls_size = 0;

/********************************************************************/
INT rpc_execute(INT sock, char *buffer, INT convert_flags)
/********************************************************************\

  Routine: rpc_execute

  Purpose: Execute a RPC command received over the network

  Input:
    INT  sock               TCP socket to which the result should be
                            send back

    char *buffer            Command buffer
    INT  convert_flags      Flags for data conversion

  Output:
    none

  Function value:
    RPC_SUCCESS             Successful completion
    RPC_INVALID_ID          Invalid routine_id received
    RPC_NET_ERROR           Error in socket call
    RPC_EXCEED_BUFFER       Not enough memory for network buffer
    RPC_SHUTDOWN            Shutdown requested
    SS_ABORT                TCP connection broken
    SS_EXIT                 TCP connection closed

\********************************************************************/
{
   INT i, idx, routine_id, status;
   char *in_param_ptr, *out_param_ptr, *last_param_ptr;
   INT tid, flags;
   NET_COMMAND *nc_in, *nc_out;
   INT param_size, max_size;
   void *prpc_param[20];
   char str[1024], debug_line[1024], *return_buffer;
   int return_buffer_size;
   int return_buffer_tls;
#ifdef FIXED_BUFFER
   int initial_buffer_size = NET_BUFFER_SIZE;
#else
   int initial_buffer_size = 1024;
#endif

   /* return buffer must must use thread local storage multi-thread servers */
   if (!tls_size) {
      tls_buffer = (TLS_POINTER *) malloc(sizeof(TLS_POINTER));
      tls_buffer[tls_size].thread_id = ss_gettid();
      tls_buffer[tls_size].buffer_size = initial_buffer_size;
      tls_buffer[tls_size].buffer = (char *) malloc(tls_buffer[tls_size].buffer_size);
      tls_size = 1;
   }
   for (i = 0; i < tls_size; i++)
      if (tls_buffer[i].thread_id == ss_gettid())
         break;
   if (i == tls_size) {
      /* new thread -> allocate new buffer */
      tls_buffer = (TLS_POINTER *) realloc(tls_buffer, (tls_size + 1) * sizeof(TLS_POINTER));
      tls_buffer[tls_size].thread_id = ss_gettid();
      tls_buffer[tls_size].buffer_size = initial_buffer_size;
      tls_buffer[tls_size].buffer = (char *) malloc(tls_buffer[tls_size].buffer_size);
      tls_size++;
   }

   return_buffer_tls = i;
   return_buffer_size = tls_buffer[i].buffer_size;
   return_buffer = tls_buffer[i].buffer;
   assert(return_buffer);

   // make valgrind happy - the RPC parameter encoder skips the alignement padding bytes
   // and valgrind complains that we transmit uninitialized data
   //memset(return_buffer, 0, return_buffer_size);

   /* extract pointer array to parameters */
   nc_in = (NET_COMMAND *) buffer;

   /* convert header format (byte swapping) */
   if (convert_flags) {
      rpc_convert_single(&nc_in->header.routine_id, TID_UINT32, 0, convert_flags);
      rpc_convert_single(&nc_in->header.param_size, TID_UINT32, 0, convert_flags);
   }

   //if (nc_in->header.routine_id & RPC_NO_REPLY) {
   //   printf("rpc_execute: routine_id %d, RPC_NO_REPLY\n", (int)(nc_in->header.routine_id & ~RPC_NO_REPLY));
   //}

   /* no result return as requested */
   if (nc_in->header.routine_id & RPC_NO_REPLY)
      sock = 0;

   /* find entry in rpc_list */
   routine_id = nc_in->header.routine_id & ~RPC_NO_REPLY;

   assert(rpc_list != NULL);

   for (i = 0;; i++)
      if (rpc_list[i].id == 0 || rpc_list[i].id == routine_id)
         break;
   idx = i;
   if (rpc_list[i].id == 0) {
      cm_msg(MERROR, "rpc_execute", "Invalid rpc ID (%d)", routine_id);
      return RPC_INVALID_ID;
   }

   again:

   in_param_ptr = nc_in->param;

   nc_out = (NET_COMMAND *) return_buffer;
   out_param_ptr = nc_out->param;

   sprintf(debug_line, "%s(", rpc_list[idx].name);

   for (i = 0; rpc_list[idx].param[i].tid != 0; i++) {
      tid = rpc_list[idx].param[i].tid;
      flags = rpc_list[idx].param[i].flags;

      if (flags & RPC_IN) {
         param_size = ALIGN8(rpc_tid_size(tid));

         if (tid == TID_STRING || tid == TID_LINK)
            param_size = ALIGN8(1 + strlen((char *) (in_param_ptr)));

         if (flags & RPC_VARARRAY) {
            /* for arrays, the size is stored as a INT in front of the array */
            param_size = *((INT *) in_param_ptr);
            if (convert_flags)
               rpc_convert_single(&param_size, TID_INT32, 0, convert_flags);
            param_size = ALIGN8(param_size);

            in_param_ptr += ALIGN8(sizeof(INT));
         }

         if (tid == TID_STRUCT)
            param_size = ALIGN8(rpc_list[idx].param[i].n);

         prpc_param[i] = in_param_ptr;

         /* convert data format */
         if (convert_flags) {
            if (flags & RPC_VARARRAY)
               rpc_convert_data(in_param_ptr, tid, flags, param_size, convert_flags);
            else
               rpc_convert_data(in_param_ptr, tid, flags, rpc_list[idx].param[i].n * rpc_tid_size(tid),
                                convert_flags);
         }

         db_sprintf(str, in_param_ptr, param_size, 0, rpc_list[idx].param[i].tid);
         if (rpc_list[idx].param[i].tid == TID_STRING) {
            /* check for long strings (db_create_record...) */
            if (strlen(debug_line) + strlen(str) + 2 < sizeof(debug_line)) {
               strcat(debug_line, "\"");
               strcat(debug_line, str);
               strcat(debug_line, "\"");
            } else
               strcat(debug_line, "...");
         } else
            strcat(debug_line, str);

         in_param_ptr += param_size;
      }

      if (flags & RPC_OUT) {
         param_size = ALIGN8(rpc_tid_size(tid));

         if (flags & RPC_VARARRAY || tid == TID_STRING) {

            /* save maximum array length from the value of the next argument.
             * this means RPC_OUT arrays and strings should always be passed like this:
             * rpc_call(..., array_ptr, array_max_size, ...); */

            max_size = *((INT *) in_param_ptr);

            if (convert_flags)
               rpc_convert_single(&max_size, TID_INT32, 0, convert_flags);
            max_size = ALIGN8(max_size);

            *((INT *) out_param_ptr) = max_size;

            /* save space for return array length */
            out_param_ptr += ALIGN8(sizeof(INT));

            /* use maximum array length from input */
            param_size += max_size;
         }

         if (rpc_list[idx].param[i].tid == TID_STRUCT)
            param_size = ALIGN8(rpc_list[idx].param[i].n);

         if ((POINTER_T) out_param_ptr - (POINTER_T) nc_out + param_size > return_buffer_size) {
#ifdef FIXED_BUFFER
            cm_msg(MERROR, "rpc_execute",
                   "return parameters (%d) too large for network buffer (%d)",
                   (POINTER_T) out_param_ptr - (POINTER_T) nc_out + param_size, return_buffer_size);

            return RPC_EXCEED_BUFFER;
#else
            int itls;
            int new_size = (POINTER_T) out_param_ptr - (POINTER_T) nc_out + param_size + 1024;

#if 0
            cm_msg(MINFO, "rpc_execute",
                      "rpc_execute: return parameters (%d) too large for network buffer (%d), new buffer size (%d)",
                      (int)((POINTER_T) out_param_ptr - (POINTER_T) nc_out + param_size), return_buffer_size, new_size);
#endif

            itls = return_buffer_tls;

            tls_buffer[itls].buffer_size = new_size;
            tls_buffer[itls].buffer = (char *) realloc(tls_buffer[itls].buffer, new_size);

            if (!tls_buffer[itls].buffer) {
               cm_msg(MERROR, "rpc_execute", "Cannot allocate return buffer of size %d", new_size);
               return RPC_EXCEED_BUFFER;
            }

            return_buffer_size = tls_buffer[itls].buffer_size;
            return_buffer = tls_buffer[itls].buffer;
            assert(return_buffer);

            goto again;
#endif
         }

         /* if parameter goes both directions, copy input to output */
         if (rpc_list[idx].param[i].flags & RPC_IN)
            memcpy(out_param_ptr, prpc_param[i], param_size);

         if (_debug_print && !(flags & RPC_IN))
            strcat(debug_line, "-");

         prpc_param[i] = out_param_ptr;
         out_param_ptr += param_size;
      }

      if (rpc_list[idx].param[i + 1].tid)
         strcat(debug_line, ", ");
   }

   //printf("predicted return size %d\n", (POINTER_T) out_param_ptr - (POINTER_T) nc_out);

   strcat(debug_line, ")");
   rpc_debug_printf(debug_line);

   last_param_ptr = out_param_ptr;

   /*********************************\
   *   call dispatch function        *
   \*********************************/
   if (rpc_list[idx].dispatch)
      status = rpc_list[idx].dispatch(routine_id, prpc_param);
   else
      status = RPC_INVALID_ID;

   if (routine_id == RPC_ID_EXIT || routine_id == RPC_ID_SHUTDOWN || routine_id == RPC_ID_WATCHDOG)
      status = RPC_SUCCESS;

   /* return immediately for closed down client connections */
   if (!sock && routine_id == RPC_ID_EXIT)
      return SS_EXIT;

   if (!sock && routine_id == RPC_ID_SHUTDOWN)
      return RPC_SHUTDOWN;

   /* Return if TCP connection broken */
   if (status == SS_ABORT)
      return SS_ABORT;

   /* if sock == 0, we are in FTCP mode and may not sent results */
   if (!sock)
      return RPC_SUCCESS;

   /* compress variable length arrays */
   out_param_ptr = nc_out->param;
   for (i = 0; rpc_list[idx].param[i].tid != 0; i++)
      if (rpc_list[idx].param[i].flags & RPC_OUT) {
         tid = rpc_list[idx].param[i].tid;
         flags = rpc_list[idx].param[i].flags;
         param_size = ALIGN8(rpc_tid_size(tid));

         if (tid == TID_STRING) {
            max_size = *((INT *) out_param_ptr);
            param_size = strlen((char *) prpc_param[i]) + 1;
            param_size = ALIGN8(param_size);

            /* move string ALIGN8(sizeof(INT)) left */
            memmove(out_param_ptr, out_param_ptr + ALIGN8(sizeof(INT)), param_size);

            /* move remaining parameters to end of string */
            memmove(out_param_ptr + param_size,
                    out_param_ptr + max_size + ALIGN8(sizeof(INT)),
                    (POINTER_T) last_param_ptr - ((POINTER_T) out_param_ptr + max_size + ALIGN8(sizeof(INT))));
         }

         if (flags & RPC_VARARRAY) {
            /* store array length at current out_param_ptr */
            max_size = *((INT *) out_param_ptr);
            param_size = *((INT *) prpc_param[i + 1]);
            *((INT *) out_param_ptr) = param_size;      // store new array size
            if (convert_flags)
               rpc_convert_single(out_param_ptr, TID_INT32, RPC_OUTGOING, convert_flags);

            out_param_ptr += ALIGN8(sizeof(INT));       // step over array size

            param_size = ALIGN8(param_size);

            /* move remaining parameters to end of array */
            memmove(out_param_ptr + param_size,
                    out_param_ptr + max_size,
                    (POINTER_T) last_param_ptr - ((POINTER_T) out_param_ptr + max_size));
         }

         if (tid == TID_STRUCT)
            param_size = ALIGN8(rpc_list[idx].param[i].n);

         /* convert data format */
         if (convert_flags) {
            if (flags & RPC_VARARRAY)
               rpc_convert_data(out_param_ptr, tid,
                                rpc_list[idx].param[i].flags | RPC_OUTGOING, param_size, convert_flags);
            else
               rpc_convert_data(out_param_ptr, tid,
                                rpc_list[idx].param[i].flags | RPC_OUTGOING,
                                rpc_list[idx].param[i].n * rpc_tid_size(tid), convert_flags);
         }

         out_param_ptr += param_size;
      }

   /* send return parameters */
   param_size = (POINTER_T) out_param_ptr - (POINTER_T) nc_out->param;
   nc_out->header.routine_id = status;
   nc_out->header.param_size = param_size;

   //printf("actual return size %d, buffer used %d\n", (POINTER_T) out_param_ptr - (POINTER_T) nc_out, sizeof(NET_COMMAND_HEADER) + param_size);

   /* convert header format (byte swapping) if necessary */
   if (convert_flags) {
      rpc_convert_single(&nc_out->header.routine_id, TID_UINT32, RPC_OUTGOING, convert_flags);
      rpc_convert_single(&nc_out->header.param_size, TID_UINT32, RPC_OUTGOING, convert_flags);
   }

   // valgrind complains about sending uninitialized data, if you care about this, uncomment
   // the memset(return_buffer,0) call above (search for "valgrind"). K.O.

   status = send_tcp(sock, return_buffer, sizeof(NET_COMMAND_HEADER) + param_size, 0);

   if (status < 0) {
      cm_msg(MERROR, "rpc_execute", "send_tcp() failed");
      return RPC_NET_ERROR;
   }

   /* print return buffer */
/*
  printf("Return buffer, ID %d:\n", routine_id);
  for (i=0; i<param_size ; i++)
    {
    status = (char) nc_out->param[i];
    printf("%02X ", status);
    if (i%8 == 7)
      printf("\n");
    }
*/
   /* return SS_EXIT if RPC_EXIT is called */
   if (routine_id == RPC_ID_EXIT)
      return SS_EXIT;

   /* return SS_SHUTDOWN if RPC_SHUTDOWN is called */
   if (routine_id == RPC_ID_SHUTDOWN)
      return RPC_SHUTDOWN;

   return RPC_SUCCESS;
}

#define MAX_N_ALLOWED_HOSTS 100
static char allowed_host[MAX_N_ALLOWED_HOSTS][256];
static int n_allowed_hosts = 0;

/********************************************************************/
INT rpc_clear_allowed_hosts()
/********************************************************************\
  Routine: rpc_clear_allowed_hosts

  Purpose: Clear list of allowed hosts and permit connections from anybody

  Input:
    none

  Output:
    none

  Function value:
    RPC_SUCCESS             Successful completion

\********************************************************************/
{
   n_allowed_hosts = 0;
   return RPC_SUCCESS;
}

/********************************************************************/
INT rpc_add_allowed_host(const char *hostname)
/********************************************************************\
  Routine: rpc_add_allowed_host

  Purpose: Permit connections from listed hosts only

  Input:
    none

  Output:
    none

  Function value:
    RPC_SUCCESS             Successful completion
    RPC_NO_MEMORY           Too many allowed hosts

\********************************************************************/
{
   if (n_allowed_hosts >= MAX_N_ALLOWED_HOSTS)
      return RPC_NO_MEMORY;

   //cm_msg(MINFO, "rpc_add_allowed_host", "Adding allowed host \'%s\'", hostname); 

   strlcpy(allowed_host[n_allowed_hosts++], hostname, sizeof(allowed_host[0]));
   return RPC_SUCCESS;
}

/********************************************************************/
INT rpc_server_accept(int lsock)
/********************************************************************\

  Routine: rpc_server_accept

  Purpose: Accept new incoming connections

  Input:
    INT    lscok            Listen socket

  Output:
    none

  Function value:
    RPC_SUCCESS             Successful completion
    RPC_NET_ERROR           Error in socket call
    RPC_CONNCLOSED          Connection was closed
    RPC_SHUTDOWN            Listener shutdown
    RPC_EXCEED_BUFFER       Not enough memory for network buffer

\********************************************************************/
{
   INT i;
   unsigned int size;
   INT sock;
   char version[NAME_LENGTH], v1[32];
   char experiment[NAME_LENGTH];
   INT port1, port2, port3;
   char *ptr;
   struct sockaddr_in acc_addr;
   struct hostent *phe;
   char net_buffer[256];
   struct linger ling;

   static struct callback_addr callback;

   if (lsock > 0) {
      size = sizeof(acc_addr);
#ifdef OS_WINNT
      sock = accept(lsock, (struct sockaddr *) &acc_addr, (int *) &size);
#else
      sock = accept(lsock, (struct sockaddr *) &acc_addr, &size);
#endif

      if (sock == -1)
         return RPC_NET_ERROR;
   } else {
      /* lsock is stdin -> already connected from inetd */

      size = sizeof(acc_addr);
      sock = lsock;
#ifdef OS_WINNT
      getpeername(sock, (struct sockaddr *) &acc_addr, (int *) &size);
#else
      getpeername(sock, (struct sockaddr *) &acc_addr, &size);
#endif
   }

   /* check access control list */
   if (n_allowed_hosts > 0) {
      int allowed = FALSE;
      struct hostent *remote_phe;
      char hname[256];
      struct in_addr remote_addr;

      /* save remote host address */
      memcpy(&remote_addr, &(acc_addr.sin_addr), sizeof(remote_addr));

      remote_phe = gethostbyaddr((char *) &remote_addr, 4, PF_INET);

      if (remote_phe == NULL) {
         /* use IP number instead */
         strlcpy(hname, (char *) inet_ntoa(remote_addr), sizeof(hname));
      } else
         strlcpy(hname, remote_phe->h_name, sizeof(hname));

      /* always permit localhost */
      if (strcmp(hname, "localhost.localdomain") == 0)
         allowed = TRUE;
      if (strcmp(hname, "localhost") == 0)
         allowed = TRUE;

      if (!allowed) {
         for (i = 0; i < n_allowed_hosts; i++)
            if (strcmp(hname, allowed_host[i]) == 0) {
               allowed = TRUE;
               break;
            }
      }

      if (!allowed) {
         static int max_report = 10;
         if (max_report > 0) {
            max_report--;
            if (max_report == 0)
               cm_msg(MERROR, "rpc_server_accept",
                      "rejecting connection from unallowed host \'%s\', this message will no longer be reported",
                      hname);
            else
               cm_msg(MERROR, "rpc_server_accept",
                      "rejecting connection from unallowed host \'%s\'. Add this host to \"/Experiment/Security/RPC hosts/Allowed hosts\"",
                      hname);
         }
         closesocket(sock);
         return RPC_NET_ERROR;
      }
   }

   /* receive string with timeout */
   i = recv_string(sock, net_buffer, 256, 10000);
   rpc_debug_printf("Received command: %s", net_buffer);

   if (i > 0) {
      char command = (char) toupper(net_buffer[0]);

      //printf("rpc_server_accept: command [%c]\n", command);

      switch (command) {
         case 'S': {

            /*----------- shutdown listener ----------------------*/
            closesocket(sock);
            return RPC_SHUTDOWN;
         }
         case 'I': {

            /*----------- return available experiments -----------*/
#ifdef LOCAL_ROUTINES
            exptab_struct exptab;
            cm_read_exptab(&exptab); // thread safe!
            for (unsigned i=0; i<exptab.exptab.size(); i++) {
               rpc_debug_printf("Return experiment: %s", exptab.exptab[i].name.c_str());
               const char* str = exptab.exptab[i].name.c_str();
               send(sock, str, strlen(str) + 1, 0);
            }
            send(sock, "", 1, 0);
#endif
            closesocket(sock);
            break;
         }
         case 'C': {

            /*----------- connect to experiment -----------*/

            /* get callback information */
            callback.experiment[0] = 0;
            port1 = port2 = version[0] = 0;

            //printf("rpc_server_accept: net buffer \'%s\'\n", net_buffer);

            /* parse string in format "C port1 port2 port3 version expt" */
            /* example: C 51046 45838 56832 2.0.0 alpha */

            port1 = strtoul(net_buffer + 2, &ptr, 0);
            port2 = strtoul(ptr, &ptr, 0);
            port3 = strtoul(ptr, &ptr, 0);

            while (*ptr == ' ')
               ptr++;

            i = 0;
            for (; *ptr != 0 && *ptr != ' ' && i < (int) sizeof(version) - 1;)
               version[i++] = *ptr++;

            // ensure that we do not overwrite buffer "version"
            assert(i < (int) sizeof(version));
            version[i] = 0;

            // skip wjatever is left from the "version" string
            for (; *ptr != 0 && *ptr != ' ';)
               ptr++;

            while (*ptr == ' ')
               ptr++;

            i = 0;
            for (; *ptr != 0 && *ptr != ' ' && *ptr != '\n' && *ptr != '\r' && i < (int) sizeof(experiment) - 1;)
               experiment[i++] = *ptr++;

            // ensure that we do not overwrite buffer "experiment"
            assert(i < (int) sizeof(experiment));
            experiment[i] = 0;

            callback.experiment = experiment;

            /* print warning if version patch level doesn't agree */
            strlcpy(v1, version, sizeof(v1));
            if (strchr(v1, '.'))
               if (strchr(strchr(v1, '.') + 1, '.'))
                  *strchr(strchr(v1, '.') + 1, '.') = 0;

            char str[100];
            strlcpy(str, cm_get_version(), sizeof(str));
            if (strchr(str, '.'))
               if (strchr(strchr(str, '.') + 1, '.'))
                  *strchr(strchr(str, '.') + 1, '.') = 0;

            if (strcmp(v1, str) != 0) {
               sprintf(str, "client MIDAS version %s differs from local version %s", version, cm_get_version());
               cm_msg(MERROR, "rpc_server_accept", "%s", str);

               sprintf(str, "received string: %s", net_buffer + 2);
               cm_msg(MERROR, "rpc_server_accept", "%s", str);
            }

            callback.host_port1 = (short) port1;
            callback.host_port2 = (short) port2;
            callback.host_port3 = (short) port3;
            callback.debug = _debug_mode;

            /* get the name of the remote host */
#ifdef OS_VXWORKS
            {
               INT status;
               status = hostGetByAddr(acc_addr.sin_addr.s_addr, callback.host_name);
               if (status != 0) {
                  cm_msg(MERROR, "rpc_server_accept", "cannot get host name for IP address");
                  break;
               }
            }
#else
            phe = gethostbyaddr((char *) &acc_addr.sin_addr, 4, PF_INET);
            if (phe == NULL) {
               /* use IP number instead */
               callback.host_name = (char *) inet_ntoa(acc_addr.sin_addr);
            } else {
               callback.host_name = phe->h_name;
            }
#endif

#ifdef LOCAL_ROUTINES
            /* update experiment definition */
            exptab_struct exptab;
            cm_read_exptab(&exptab); // thread safe!

            unsigned idx = 0;
            bool found = false;
            /* lookup experiment */
            if (equal_ustring(callback.experiment.c_str(), "Default")) {
               found = true;
               idx = 0;
            } else {
               for (idx = 0; idx < exptab.exptab.size(); idx++) {
                  if (exptab.exptab[idx].name == callback.experiment) {
                     if (ss_dir_exist(exptab.exptab[idx].directory.c_str())) {
                        found = true;
                        break;
                     }
                  }
               }
            }

            if (!found) {
               cm_msg(MERROR, "rpc_server_accept", "experiment \'%s\' not defined in exptab file \'%s\'", callback.experiment.c_str(), exptab.filename.c_str());

               send(sock, "2", 2, 0);   /* 2 means exp. not found */
               closesocket(sock);
               break;
            }

            callback.directory = exptab.exptab[idx].directory;
            callback.user = exptab.exptab[idx].user;

            /* create a new process */
            char host_port1_str[30], host_port2_str[30], host_port3_str[30];
            char debug_str[30];

            sprintf(host_port1_str, "%d", callback.host_port1);
            sprintf(host_port2_str, "%d", callback.host_port2);
            sprintf(host_port3_str, "%d", callback.host_port3);
            sprintf(debug_str, "%d", callback.debug);

            const char *mserver_path = rpc_get_mserver_path();

            const char *argv[10];
            argv[0] = mserver_path;
            argv[1] = callback.host_name.c_str();
            argv[2] = host_port1_str;
            argv[3] = host_port2_str;
            argv[4] = host_port3_str;
            argv[5] = debug_str;
            argv[6] = callback.experiment.c_str();
            argv[7] = callback.directory.c_str();
            argv[8] = callback.user.c_str();
            argv[9] = NULL;

            rpc_debug_printf("Spawn: %s %s %s %s %s %s %s %s %s %s",
                             argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8],
                             argv[9]);

            int status = ss_spawnv(P_NOWAIT, mserver_path, argv);

            if (status != SS_SUCCESS) {
               rpc_debug_printf("Cannot spawn subprocess: %s\n", strerror(errno));

               sprintf(str, "3");       /* 3 means cannot spawn subprocess */
               send(sock, str, strlen(str) + 1, 0);
               closesocket(sock);
               break;
            }

            sprintf(str, "1 %s", cm_get_version());     /* 1 means ok */
            send(sock, str, strlen(str) + 1, 0);
#endif // LOCAL_ROUTINES
            closesocket(sock);

            break;
         }
         default: {
            cm_msg(MERROR, "rpc_server_accept", "received unknown command '%c' code %d", command, command);
            closesocket(sock);
            break;
         }
      }
   } else {                     /* if i>0 */

      /* lingering needed for PCTCP */
      ling.l_onoff = 1;
      ling.l_linger = 0;
      setsockopt(sock, SOL_SOCKET, SO_LINGER, (char *) &ling, sizeof(ling));
      closesocket(sock);
   }

   return RPC_SUCCESS;
}

/********************************************************************/
INT rpc_client_accept(int lsock)
/********************************************************************\

  Routine: rpc_client_accept

  Purpose: midas program accept new RPC connection (run transitions, etc)

  Input:
    INT    lsock            Listen socket

  Output:
    none

  Function value:
    RPC_SUCCESS             Successful completion
    RPC_NET_ERROR           Error in socket call
    RPC_CONNCLOSED          Connection was closed
    RPC_SHUTDOWN            Listener shutdown
    RPC_EXCEED_BUFFER       Not enough memory for network buffer

\********************************************************************/
{
   INT i, status;
   //int version;
   unsigned int size;
   int sock;
   struct sockaddr_in acc_addr;
   INT client_hw_type = 0, hw_type;
   std::string client_program;
   std::string host_name;
   INT convert_flags;
   char net_buffer[256], *p;

   size = sizeof(acc_addr);
#ifdef OS_WINNT
   sock = accept(lsock, (struct sockaddr *) &acc_addr, (int *) &size);
#else
   sock = accept(lsock, (struct sockaddr *) &acc_addr, &size);
#endif

   if (sock == -1)
      return RPC_NET_ERROR;

   /* get the name of the calling host */
/* outcommented for speed reasons SR 7.10.98
#ifdef OS_VXWORKS
  {
  status = hostGetByAddr(acc_addr.sin_addr.s_addr, host_name);
  if (status != 0)
    strcpy(host_name, "unknown");
  }
#else
  phe = gethostbyaddr((char *) &acc_addr.sin_addr, 4, PF_INET);
  if (phe == NULL)
    strcpy(host_name, "unknown");
  strcpy(host_name, phe->h_name);
#endif
*/

   /* check access control list */
   if (n_allowed_hosts > 0) {
      int allowed = FALSE;
      struct hostent *remote_phe;
      char hname[256];
      struct in_addr remote_addr;

      /* save remote host address */
      memcpy(&remote_addr, &(acc_addr.sin_addr), sizeof(remote_addr));

      remote_phe = gethostbyaddr((char *) &remote_addr, 4, PF_INET);

      if (remote_phe == NULL) {
         /* use IP number instead */
         strlcpy(hname, (char *) inet_ntoa(remote_addr), sizeof(hname));
      } else
         strlcpy(hname, remote_phe->h_name, sizeof(hname));

      /* always permit localhost */
      if (strcmp(hname, "localhost.localdomain") == 0)
         allowed = TRUE;
      if (strcmp(hname, "localhost") == 0)
         allowed = TRUE;

      if (!allowed) {
         for (i = 0; i < n_allowed_hosts; i++)
            if (strcmp(hname, allowed_host[i]) == 0) {
               allowed = TRUE;
               break;
            }
      }

      if (!allowed) {
         static int max_report = 10;
         if (max_report > 0) {
            max_report--;
            if (max_report == 0)
               cm_msg(MERROR, "rpc_client_accept",
                      "rejecting connection from unallowed host \'%s\', this message will no longer be reported",
                      hname);
            else
               cm_msg(MERROR, "rpc_client_accept",
                      "rejecting connection from unallowed host \'%s\'. Add this host to \"/Experiment/Security/RPC hosts/Allowed hosts\"",
                      hname);
         }
         closesocket(sock);
         return RPC_NET_ERROR;
      }
   }

   host_name = "(unknown)";
   client_program = "(unknown)";

   /* receive string with timeout */
   i = recv_string(sock, net_buffer, sizeof(net_buffer), 10000);
   if (i <= 0) {
      closesocket(sock);
      return RPC_NET_ERROR;
   }

   /* get remote computer info */
   p = strtok(net_buffer, " ");
   if (p != NULL) {
      client_hw_type = atoi(p);
      p = strtok(NULL, " ");
   }
   if (p != NULL) {
      //version = atoi(p);
      p = strtok(NULL, " ");
   }
   if (p != NULL) {
      client_program = p;
      p = strtok(NULL, " ");
   }
   if (p != NULL) {
      host_name = p;
      p = strtok(NULL, " ");
   }

   //printf("rpc_client_accept: client_hw_type %d, version %d, client_name \'%s\', hostname \'%s\'\n", client_hw_type, version, client_program, host_name);

   RPC_SERVER_ACCEPTION* sa = rpc_new_server_acception();

   /* save information in _server_acception structure */
   sa->recv_sock = sock;
   sa->send_sock = 0;
   sa->event_sock = 0;
   sa->remote_hw_type = client_hw_type;
   sa->host_name = host_name;
   sa->prog_name = client_program;
   sa->last_activity = ss_millitime();
   sa->watchdog_timeout = 0;
   sa->is_mserver = FALSE;

   /* send my own computer id */
   hw_type = rpc_get_hw_type();
   std::string str = msprintf("%d %s", hw_type, cm_get_version());
   status = send(sock, str.c_str(), str.length() + 1, 0);
   if (status != (INT) str.length() + 1)
      return RPC_NET_ERROR;

   rpc_calc_convert_flags(hw_type, client_hw_type, &convert_flags);
   sa->convert_flags = convert_flags;

   ss_suspend_set_server_acceptions(&_server_acceptions);

   return RPC_SUCCESS;
}

/********************************************************************/
INT rpc_server_callback(struct callback_addr *pcallback)
/********************************************************************\

  Routine: rpc_server_callback

  Purpose: Callback a remote client. Setup _server_acception entry
           with optional conversion flags and establish two-way
           TCP connection.

  Input:
    callback_addr pcallback Pointer to a callback structure

  Output:
    none

  Function value:
    RPC_SUCCESS             Successful completion

\********************************************************************/
{
   INT status;
   int recv_sock, send_sock, event_sock;
   struct sockaddr_in bind_addr;
   struct hostent *phe;
   char str[100];
   std::string client_program;
   std::string host_name;
   INT client_hw_type, hw_type;
   INT convert_flags;
   char net_buffer[256];
   char *p;
   int flag;

   /* copy callback information */
   struct callback_addr callback = *pcallback;
   //idx = callback.index;

   /* create new sockets for TCP */
   recv_sock = socket(AF_INET, SOCK_STREAM, 0);
   send_sock = socket(AF_INET, SOCK_STREAM, 0);
   event_sock = socket(AF_INET, SOCK_STREAM, 0);
   if (event_sock == -1)
      return RPC_NET_ERROR;

   /* callback to remote node */
   memset(&bind_addr, 0, sizeof(bind_addr));
   bind_addr.sin_family = AF_INET;
   bind_addr.sin_port = htons(callback.host_port1);

#ifdef OS_VXWORKS
   {
      INT host_addr;

      host_addr = hostGetByName(callback.host_name.c_str());
      memcpy((char *) &(bind_addr.sin_addr), &host_addr, 4);
   }
#else
   phe = gethostbyname(callback.host_name.c_str());
   if (phe == NULL) {
      cm_msg(MERROR, "rpc_server_callback", "cannot lookup host name \'%s\'", callback.host_name.c_str());
      return RPC_NET_ERROR;
   }
   memcpy((char *) &(bind_addr.sin_addr), phe->h_addr, phe->h_length);
#endif

   /* connect receive socket */
#ifdef OS_UNIX
   do {
      status = connect(recv_sock, (struct sockaddr *) &bind_addr, sizeof(bind_addr));

      /* don't return if an alarm signal was cought */
   } while (status == -1 && errno == EINTR);
#else
   status = connect(recv_sock, (struct sockaddr *) &bind_addr, sizeof(bind_addr));
#endif

   if (status != 0) {
      cm_msg(MERROR, "rpc_server_callback", "cannot connect receive socket, host \"%s\", port %d, errno %d (%s)",
             callback.host_name.c_str(), callback.host_port1, errno, strerror(errno));
      closesocket(recv_sock);
      //closesocket(send_sock);
      //closesocket(event_sock);
      return RPC_NET_ERROR;
   }

   bind_addr.sin_port = htons(callback.host_port2);

   /* connect send socket */
#ifdef OS_UNIX
   do {
      status = connect(send_sock, (struct sockaddr *) &bind_addr, sizeof(bind_addr));

      /* don't return if an alarm signal was cought */
   } while (status == -1 && errno == EINTR);
#else
   status = connect(send_sock, (struct sockaddr *) &bind_addr, sizeof(bind_addr));
#endif

   if (status != 0) {
      cm_msg(MERROR, "rpc_server_callback", "cannot connect send socket");
      closesocket(recv_sock);
      closesocket(send_sock);
      //closesocket(event_sock);
      return RPC_NET_ERROR;
   }

   bind_addr.sin_port = htons(callback.host_port3);

   /* connect event socket */
#ifdef OS_UNIX
   do {
      status = connect(event_sock, (struct sockaddr *) &bind_addr, sizeof(bind_addr));

      /* don't return if an alarm signal was cought */
   } while (status == -1 && errno == EINTR);
#else
   status = connect(event_sock, (struct sockaddr *) &bind_addr, sizeof(bind_addr));
#endif

   if (status != 0) {
      cm_msg(MERROR, "rpc_server_callback", "cannot connect event socket");
      closesocket(recv_sock);
      closesocket(send_sock);
      closesocket(event_sock);
      return RPC_NET_ERROR;
   }
#ifndef OS_ULTRIX               /* crashes ULTRIX... */
   /* increase send buffer size to 2 Mbytes, on Linux also limited by sysctl net.ipv4.tcp_rmem and net.ipv4.tcp_wmem */
   flag = 2 * 1024 * 1024;
   status = setsockopt(event_sock, SOL_SOCKET, SO_RCVBUF, (char *) &flag, sizeof(INT));
   if (status != 0)
      cm_msg(MERROR, "rpc_server_callback", "cannot setsockopt(SOL_SOCKET, SO_RCVBUF), errno %d (%s)", errno,
             strerror(errno));
#endif

   if (recv_string(recv_sock, net_buffer, 256, _rpc_connect_timeout) <= 0) {
      cm_msg(MERROR, "rpc_server_callback", "timeout on receive remote computer info");
      closesocket(recv_sock);
      closesocket(send_sock);
      closesocket(event_sock);
      return RPC_NET_ERROR;
   }
   //printf("rpc_server_callback: \'%s\'\n", net_buffer);

   /* get remote computer info */
   client_hw_type = strtoul(net_buffer, &p, 0);

   while (*p == ' ')
      p++;

   client_program = p;

   //printf("hw type %d, name \'%s\'\n", client_hw_type, client_program);

   /* get the name of the remote host */
#ifdef OS_VXWORKS
   status = hostGetByAddr(bind_addr.sin_addr.s_addr, host_name);
   if (status != 0)
      strcpy(host_name, "unknown");
#else
   phe = gethostbyaddr((char *) &bind_addr.sin_addr, 4, PF_INET);
   if (phe == NULL)
      host_name = "unknown";
   else
      host_name = phe->h_name;
#endif

   //printf("rpc_server_callback: mserver acception\n");

   RPC_SERVER_ACCEPTION* sa = rpc_new_server_acception();

   /* save information in _server_acception structure */
   sa->recv_sock = recv_sock;
   sa->send_sock = send_sock;
   sa->event_sock = event_sock;
   sa->remote_hw_type = client_hw_type;
   sa->host_name = host_name;
   sa->prog_name = client_program;
   sa->last_activity = ss_millitime();
   sa->watchdog_timeout = 0;
   sa->is_mserver = TRUE;

   assert(_mserver_acception == NULL);

   _mserver_acception = sa;

   //printf("rpc_server_callback: _mserver_acception %p\n", _mserver_acception);

   /* send my own computer id */
   hw_type = rpc_get_hw_type();
   sprintf(str, "%d", hw_type);
   send(recv_sock, str, strlen(str) + 1, 0);

   rpc_calc_convert_flags(hw_type, client_hw_type, &convert_flags);
   sa->convert_flags = convert_flags;

   ss_suspend_set_server_acceptions(&_server_acceptions);

   if (rpc_is_mserver()) {
      rpc_debug_printf("Connection to %s:%s established\n", sa->host_name.c_str(), sa->prog_name.c_str());
   }

   return RPC_SUCCESS;
}


/********************************************************************/
INT rpc_server_loop(void)
/********************************************************************\

  Routine: rpc_server_loop

  Purpose: mserver main event loop

\********************************************************************/
{
   while (1) {
      int status = ss_suspend(1000, 0);

      if (status == SS_ABORT || status == SS_EXIT)
         break;

      if (rpc_check_channels() == RPC_NET_ERROR)
         break;

      /* check alarms, etc */
      status = cm_periodic_tasks();

      cm_msg_flush_buffer();
   }

   return RPC_SUCCESS;
}

/********************************************************************/
INT rpc_server_receive_rpc(int idx, RPC_SERVER_ACCEPTION* sa)
/********************************************************************\

  Routine: rpc_server_receive_rpc

  Purpose: Receive rpc commands and execute them. Close the connection
           if client has broken TCP pipe.

  Function value:
    RPC_SUCCESS             Successful completion
    RPC_EXCEED_BUFFER       Not enough memeory to allocate buffer
    SS_EXIT                 Server connection was closed
    SS_ABORT                Server connection was broken

\********************************************************************/
{
   int status = 0;
   int remaining = 0;

   char *buf = NULL;
   int bufsize = 0;
   
   do {
      int n_received = recv_net_command_realloc(idx, &buf, &bufsize, &remaining);
      
      if (n_received <= 0) {
         status = SS_ABORT;
         cm_msg(MERROR, "rpc_server_receive_rpc", "recv_net_command() returned %d, abort", n_received);
         goto error;
      }
      
      status = rpc_execute(sa->recv_sock, buf, sa->convert_flags);
      
      if (status == SS_ABORT) {
         cm_msg(MERROR, "rpc_server_receive_rpc", "rpc_execute() returned %d, abort", status);
         goto error;
      }
      
         if (status == SS_EXIT || status == RPC_SHUTDOWN) {
            if (rpc_is_mserver())
               rpc_debug_printf("Connection to %s:%s closed\n", sa->host_name.c_str(), sa->prog_name.c_str());
            goto exit;
         }
         
   } while (remaining);
   
   if (buf) {
      free(buf);
      buf = NULL;
      bufsize = 0;
   }

   return RPC_SUCCESS;

   error:

   {
      char str[80];
      strlcpy(str, sa->host_name.c_str(), sizeof(str));
      if (strchr(str, '.'))
         *strchr(str, '.') = 0;
      cm_msg(MTALK, "rpc_server_receive_rpc", "Program \'%s\' on host \'%s\' aborted", sa->prog_name.c_str(), str);
   }

   exit:

   cm_msg_flush_buffer();

   if (buf) {
      free(buf);
      buf = NULL;
      bufsize = 0;
   }

   /* disconnect from experiment as MIDAS server */
   if (rpc_is_mserver()) {
      HNDLE hDB, hKey;

      cm_get_experiment_database(&hDB, &hKey);

      /* only disconnect from experiment if previously connected.
         Necessary for pure RPC servers (RPC_SRVR) */
      if (hDB) {
         bm_close_all_buffers();
         cm_delete_client_info(hDB, 0);
         db_close_all_databases();

         rpc_deregister_functions();

         cm_set_experiment_database(0, 0);
      }
   }

   bool is_mserver = sa->is_mserver;

   sa->close();

   /* signal caller a shutdonw */
   if (status == RPC_SHUTDOWN)
      return status;

   /* only the mserver should stop on server connection closure */
   if (!is_mserver) {
      return SS_SUCCESS;
   }

   return status;
}

/********************************************************************/
INT rpc_server_receive_event(int idx, RPC_SERVER_ACCEPTION* sa, int timeout_msec)
/********************************************************************\

  Routine: rpc_server_receive_event

  Purpose: Receive event and dispatch it

  Function value:
    RPC_SUCCESS             Successful completion
    RPC_EXCEED_BUFFER       Not enough memeory to allocate buffer
    SS_EXIT                 Server connection was closed
    SS_ABORT                Server connection was broken

\********************************************************************/
{
   int status = 0;

   DWORD start_time = ss_millitime();

   //
   // THIS IS NOT THREAD SAFE!!!
   //
   // IT IS ONLY USED BY THE MSERVER
   // MSERVER IS SINGLE-THREADED!!!
   //

   static char *xbuf = NULL;
   static int   xbufsize = 0;
   static bool  xbufempty = true;

   // short cut
   if (sa == NULL && xbufempty)
      return RPC_SUCCESS;

   static bool  recurse = false;

   if (recurse) {
      cm_msg(MERROR, "rpc_server_receive_event", "internal error: called recursively");
      // do not do anything if we are called recursively
      // via recursive ss_suspend() or otherwise. K.O.
      if (xbufempty)
         return RPC_SUCCESS;
      else
         return BM_ASYNC_RETURN;
   }

   recurse = true;
   
   do {
      if (xbufempty && sa) {
         int n_received = recv_event_server_realloc(idx, sa, &xbuf, &xbufsize);
      
         if (n_received < 0) {
            status = SS_ABORT;
            cm_msg(MERROR, "rpc_server_receive_event", "recv_event_server_realloc() returned %d, abort", n_received);
            goto error;
         }

         if (n_received == 0) {
            // no more data in the tcp socket
            recurse = false;
            return RPC_SUCCESS;
         }

         xbufempty = false;
      }

      if (xbufempty) {
         // no event in xbuf buffer
         recurse = false;
         return RPC_SUCCESS;
      }

      /* send event to buffer */
      INT *pbh = (INT *) xbuf;
      EVENT_HEADER *pevent = (EVENT_HEADER *) (pbh + 1);
      
      status = bm_send_event(*pbh, pevent, 0, timeout_msec);

      //printf("rpc_server_receiv: buffer_handle %d, event_id 0x%04x, serial 0x%08x, data_size %d, status %d\n", *pbh, pevent->event_id, pevent->serial_number, pevent->data_size, status);
      
      if (status == SS_ABORT) {
         cm_msg(MERROR, "rpc_server_receive_event", "bm_send_event() error %d (SS_ABORT), abort", status);
         goto error;
      }
      
      if (status == BM_ASYNC_RETURN) {
         //cm_msg(MERROR, "rpc_server_receive_event", "bm_send_event() error %d, event buffer is full", status);
         recurse = false;
         return status;
      }
      
      if (status != BM_SUCCESS) {
         cm_msg(MERROR, "rpc_server_receive_event", "bm_send_event() error %d, mserver dropped this event", status);
      }
      
      xbufempty = true;

      /* repeat for maximum 0.5 sec */
   } while (ss_millitime() - start_time < 500);
   
   recurse = false;
   return RPC_SUCCESS;

   error:

   {
      char str[80];
      strlcpy(str, sa->host_name.c_str(), sizeof(str));
      if (strchr(str, '.'))
         *strchr(str, '.') = 0;
      cm_msg(MTALK, "rpc_server_receive_event", "Program \'%s\' on host \'%s\' aborted", sa->prog_name.c_str(), str);
   }

   //exit:

   cm_msg_flush_buffer();

   /* disconnect from experiment as MIDAS server */
   if (rpc_is_mserver()) {
      HNDLE hDB, hKey;

      cm_get_experiment_database(&hDB, &hKey);

      /* only disconnect from experiment if previously connected.
         Necessary for pure RPC servers (RPC_SRVR) */
      if (hDB) {
         bm_close_all_buffers();
         cm_delete_client_info(hDB, 0);
         db_close_all_databases();

         rpc_deregister_functions();

         cm_set_experiment_database(0, 0);
      }
   }

   bool is_mserver = sa->is_mserver;

   sa->close();

   /* signal caller a shutdonw */
   if (status == RPC_SHUTDOWN)
      return status;

   /* only the mserver should stop on server connection closure */
   if (!is_mserver) {
      return SS_SUCCESS;
   }

   return status;
}


/********************************************************************/
int rpc_flush_event_socket(int timeout_msec)
/********************************************************************\

  Routine: rpc_flush_event_socket

  Purpose: Receive and en-buffer events from the mserver event socket

  Function value:
    BM_SUCCESS              Event socket is empty, all data was read an en-buffered
    BM_ASYNC_RETURN         Event socket has unread data or event buffer is full and rpc_server_receive_event() has an un-buffered event.
    SS_EXIT                 Server connection was closed
    SS_ABORT                Server connection was broken

\********************************************************************/
{
   bool has_data = ss_event_socket_has_data();
   
   //printf("ss_event_socket_has_data() returned %d\n", has_data);

   if (has_data) {
      if (timeout_msec == BM_NO_WAIT) {
         return BM_ASYNC_RETURN;
      } else if (timeout_msec == BM_WAIT) {
         return BM_ASYNC_RETURN;
      } else {
         int status = ss_suspend(timeout_msec, MSG_BM);
         if (status == SS_ABORT || status == SS_EXIT)
            return status;
         return BM_ASYNC_RETURN;
      }
   }

   int status = rpc_server_receive_event(0, NULL, timeout_msec);
   
   //printf("rpc_server_receive_event() status %d\n", status);

   if (status == BM_ASYNC_RETURN) {
      return BM_ASYNC_RETURN;
   }

   if (status == SS_ABORT || status == SS_EXIT)
      return status;
   
   return BM_SUCCESS;
}

/********************************************************************/
INT rpc_server_shutdown(void)
/********************************************************************\

  Routine: rpc_server_shutdown

  Purpose: Shutdown RPC server, abort all connections

  Input:
    none

  Output:
    none

  Function value:
    RPC_SUCCESS             Successful completion

\********************************************************************/
{
   //printf("rpc_server_shutdown!\n");

   struct linger ling;

   /* close all open connections */
   for (unsigned idx = 0; idx < _server_acceptions.size(); idx++) {
      if (_server_acceptions[idx] && _server_acceptions[idx]->recv_sock != 0) {
         RPC_SERVER_ACCEPTION* sa = _server_acceptions[idx];
         /* lingering needed for PCTCP */
         ling.l_onoff = 1;
         ling.l_linger = 0;
         setsockopt(sa->recv_sock, SOL_SOCKET, SO_LINGER, (char *) &ling, sizeof(ling));
         closesocket(sa->recv_sock);

         if (sa->send_sock) {
            setsockopt(sa->send_sock, SOL_SOCKET, SO_LINGER, (char *) &ling, sizeof(ling));
            closesocket(sa->send_sock);
         }

         if (sa->event_sock) {
            setsockopt(sa->event_sock, SOL_SOCKET, SO_LINGER, (char *) &ling, sizeof(ling));
            closesocket(sa->event_sock);
         }

         sa->recv_sock = 0;
         sa->send_sock = 0;
         sa->event_sock = 0;
      }
   }

   /* avoid memory leak */
   for (unsigned idx = 0; idx < _server_acceptions.size(); idx++) {
      RPC_SERVER_ACCEPTION* sa = _server_acceptions[idx];
      if (sa) {
         //printf("rpc_server_shutdown: %d %p %p\n", idx, sa, _mserver_acception);
         if (sa == _mserver_acception) {
            // do not leave behind a stale pointer!
            _mserver_acception = NULL;
         }
         delete sa;
         _server_acceptions[idx] = NULL;
      }
   }

   if (_rpc_registered) {
      closesocket(_rpc_listen_socket);
      _rpc_listen_socket = 0;
      _rpc_registered = FALSE;
   }

   /* free suspend structures */
   ss_suspend_exit();

   return RPC_SUCCESS;
}


/********************************************************************/
INT rpc_check_channels(void)
/********************************************************************\

  Routine: rpc_check_channels

  Purpose: Check open rpc channels by sending watchdog messages

  Input:
    none

  Output:
    none

  Function value:
    RPC_SUCCESS             Channel is still alive
    RPC_NET_ERROR           Connection is broken

\********************************************************************/
{
   INT status;
   NET_COMMAND nc;
   fd_set readfds;
   struct timeval timeout;

   for (unsigned idx = 0; idx < _server_acceptions.size(); idx++) {
      if (_server_acceptions[idx] && _server_acceptions[idx]->recv_sock) {
         RPC_SERVER_ACCEPTION* sa = _server_acceptions[idx];

         if (sa->watchdog_timeout == 0) {
            continue;
         }

         DWORD elapsed = ss_millitime() - sa->last_activity;
         //printf("rpc_check_channels: idx %d, watchdog_timeout %d, last_activity %d, elapsed %d\n", idx, sa->watchdog_timeout, sa->last_activity, elapsed);

         if (sa->watchdog_timeout && (elapsed > (DWORD)sa->watchdog_timeout)) {
         
            //printf("rpc_check_channels: send watchdog message to %s on %s\n", sa->prog_name.c_str(), sa->host_name.c_str());

            /* send a watchdog message */
            nc.header.routine_id = MSG_WATCHDOG;
            nc.header.param_size = 0;

            int convert_flags = sa->convert_flags;
            if (convert_flags) {
               rpc_convert_single(&nc.header.routine_id, TID_UINT32, RPC_OUTGOING, convert_flags);
               rpc_convert_single(&nc.header.param_size, TID_UINT32, RPC_OUTGOING, convert_flags);
            }

            /* send the header to the client */
            int i = send_tcp(sa->send_sock, (char *) &nc, sizeof(NET_COMMAND_HEADER), 0);

            if (i < 0) {
               cm_msg(MINFO, "rpc_check_channels", "client \"%s\" on host \"%s\" failed watchdog test after %d sec, send_tcp() returned %d",
                      sa->prog_name.c_str(),
                      sa->host_name.c_str(),
                      sa->watchdog_timeout / 1000,
                      i);
               
               /* disconnect from experiment */
               if (rpc_is_mserver())
                  cm_disconnect_experiment();
               
               sa->close();
               return RPC_NET_ERROR;
            }

            /* make some timeout checking */
            FD_ZERO(&readfds);
            FD_SET(sa->send_sock, &readfds);
            FD_SET(sa->recv_sock, &readfds);

            timeout.tv_sec = sa->watchdog_timeout / 1000;
            timeout.tv_usec = (sa->watchdog_timeout % 1000) * 1000;

            do {
               status = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);

               /* if an alarm signal was cought, restart select with reduced timeout */
               if (status == -1 && timeout.tv_sec >= WATCHDOG_INTERVAL / 1000)
                  timeout.tv_sec -= WATCHDOG_INTERVAL / 1000;

            } while (status == -1);        /* dont return if an alarm signal was cought */

            if (!FD_ISSET(sa->send_sock, &readfds) &&
                !FD_ISSET(sa->recv_sock, &readfds)) {

               cm_msg(MINFO, "rpc_check_channels", "client \"%s\" on host \"%s\" failed watchdog test after %d sec",
                      sa->prog_name.c_str(),
                      sa->host_name.c_str(),
                      sa->watchdog_timeout / 1000);
               
               /* disconnect from experiment */
               if (rpc_is_mserver())
                  cm_disconnect_experiment();
               
               sa->close();
               return RPC_NET_ERROR;
            }

            /* receive result on send socket */
            if (FD_ISSET(sa->send_sock, &readfds)) {
               i = recv_tcp(sa->send_sock, (char *) &nc, sizeof(nc), 0);
               if (i <= 0) {
                  cm_msg(MINFO, "rpc_check_channels", "client \"%s\" on host \"%s\" failed watchdog test after %d sec, recv_tcp() returned %d",
                         sa->prog_name.c_str(),
                         sa->host_name.c_str(),
                         sa->watchdog_timeout / 1000,
                         i);
                  
                  /* disconnect from experiment */
                  if (rpc_is_mserver())
                     cm_disconnect_experiment();
                  
                  sa->close();
                  return RPC_NET_ERROR;
               }
            }
         }
      }
   }

   return RPC_SUCCESS;
}

/** @} */

/**dox***************************************************************/
/** @addtogroup bkfunctionc
 *
 *  @{  */

/********************************************************************\
*                                                                    *
*                 Bank functions                                     *
*                                                                    *
\********************************************************************/

/********************************************************************/
/**
Initializes an event for Midas banks structure.
Before banks can be created in an event, bk_init() has to be called first.
@param event pointer to the area of event
*/
void bk_init(void *event) {
   ((BANK_HEADER *) event)->data_size = 0;
   ((BANK_HEADER *) event)->flags = BANK_FORMAT_VERSION;
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/********************************************************************/
BOOL bk_is32(const void *event)
/********************************************************************\

  Routine: bk_is32

  Purpose: Return true if banks inside event are 32-bit banks

  Input:
    void   *event           pointer to the event

  Output:
    none

  Function value:
    none

\********************************************************************/
{
   return ((((BANK_HEADER *) event)->flags & BANK_FORMAT_32BIT) > 0);
}

/********************************************************************/
BOOL bk_is32a(const void *event)
/********************************************************************\

  Routine: bk_is32a

  Purpose: Return true if banks inside event are 32-bit banks
           and banks are 64-bit aligned

  Input:
    void   *event           pointer to the event

  Output:
    none

  Function value:
    none

\********************************************************************/
{
   return ((((BANK_HEADER *) event)->flags & BANK_FORMAT_64BIT_ALIGNED) > 0);
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Initializes an event for Midas banks structure for large bank size (> 32KBytes)
Before banks can be created in an event, bk_init32() has to be called first.
@param event pointer to the area of event
@return void
*/
void bk_init32(void *event) {
   ((BANK_HEADER *) event)->data_size = 0;
   ((BANK_HEADER *) event)->flags = BANK_FORMAT_VERSION | BANK_FORMAT_32BIT;
}

/********************************************************************/
/**
Initializes an event for Midas banks structure for large bank size (> 32KBytes)
which are aligned on 64-bit boundaries.
Before banks can be created in an event, bk_init32a() has to be called first.
@param event pointer to the area of event
@return void
*/
void bk_init32a(void *event) {
   ((BANK_HEADER *) event)->data_size = 0;
   ((BANK_HEADER *) event)->flags = BANK_FORMAT_VERSION | BANK_FORMAT_32BIT | BANK_FORMAT_64BIT_ALIGNED;
}

/********************************************************************/
/**
Returns the size of an event containing banks.
The total size of an event is the value returned by bk_size() plus the size
of the event header (sizeof(EVENT_HEADER)).
@param event pointer to the area of event
@return number of bytes contained in data area of event
*/
INT bk_size(const void *event) {
   return ((BANK_HEADER *) event)->data_size + sizeof(BANK_HEADER);
}

static void copy_bk_name(char* dst, const char* src)
{
   // copy 4 byte bank name from "src" to "dst", set unused bytes of "dst" to NUL.

   if (src[0] == 0) {
      // invalid empty name
      dst[0] = 0;
      dst[1] = 0;
      dst[2] = 0;
      dst[3] = 0;
      return;
   }

   dst[0] = src[0];

   if (src[1] == 0) {
      dst[1] = 0;
      dst[2] = 0;
      dst[3] = 0;
      return;
   }

   dst[1] = src[1];

   if (src[2] == 0) {
      dst[2] = 0;
      dst[3] = 0;
      return;
   }

   dst[2] = src[2];

   if (src[3] == 0) {
      dst[3] = 0;
      return;
   }

   dst[3] = src[3];
}

/********************************************************************/
/**
Create a Midas bank.
The data pointer pdata must be used as an address to fill a
bank. It is incremented with every value written to the bank and finally points
to a location just after the last byte of the bank. It is then passed to
the function bk_close() to finish the bank creation.
\code
INT *pdata;
bk_init(pevent);
bk_create(pevent, "ADC0", TID_INT32, &pdata);
*pdata++ = 123
*pdata++ = 456
bk_close(pevent, pdata);
\endcode
@param event pointer to the data area
@param name of the bank, must be exactly 4 charaters
@param type type of bank, one of the @ref Midas_Data_Types values defined in
midas.h
@param pdata pointer to the data area of the newly created bank
@return void
*/
void bk_create(void *event, const char *name, WORD type, void **pdata) {
   if (bk_is32a((BANK_HEADER *) event)) {
      if (((PTYPE) event & 0x07) != 0) {
         cm_msg(MERROR, "bk_create", "Bank %s created with unaligned event pointer", name);
         return;
      }
      BANK32A *pbk32a;

      pbk32a = (BANK32A *) ((char *) (((BANK_HEADER *) event) + 1) + ((BANK_HEADER *) event)->data_size);
      copy_bk_name(pbk32a->name, name);
      pbk32a->type = type;
      pbk32a->data_size = 0;
      *pdata = pbk32a + 1;
   } else if (bk_is32((BANK_HEADER *) event)) {
      BANK32 *pbk32;

      pbk32 = (BANK32 *) ((char *) (((BANK_HEADER *) event) + 1) + ((BANK_HEADER *) event)->data_size);
      copy_bk_name(pbk32->name, name);
      pbk32->type = type;
      pbk32->data_size = 0;
      *pdata = pbk32 + 1;
   } else {
      BANK *pbk;

      pbk = (BANK *) ((char *) (((BANK_HEADER *) event) + 1) + ((BANK_HEADER *) event)->data_size);
      copy_bk_name(pbk->name, name);
      pbk->type = type;
      pbk->data_size = 0;
      *pdata = pbk + 1;
   }
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/********************************************************************/
/**
Copy a bank given by name if found from a buffer source to a destination buffer.
@param * pevent pointing after the EVENT_HEADER (as in FE)
@param * psrce  pointing to EVENT_HEADER in this case (for ebch[i].pfragment)
@param bkname  Bank to be found and copied from psrce to pevent
@return EB_SUCCESS if bank found, 0 if not found (pdest untouched)
*/
INT bk_copy(char *pevent, char *psrce, const char *bkname) {

   INT status;
   DWORD bklen, bktype, bksze;
   BANK_HEADER *psBkh;
   BANK *psbkh;
   char *pdest;
   void *psdata;

   // source pointing on the BANKxx
   psBkh = (BANK_HEADER *) ((EVENT_HEADER *) psrce + 1);
   // Find requested bank
   status = bk_find(psBkh, bkname, &bklen, &bktype, &psdata);
   // Return 0 if not found
   if (status != SUCCESS) return 0;

   // Check bank type...
   // You cannot mix BANK and BANK32 so make sure all the FE use either
   // bk_init(pevent) or bk_init32(pevent).
   if (bk_is32a(psBkh)) {

      // pointer to the source bank header
      BANK32A *psbkh32a = ((BANK32A *) psdata - 1);
      // Data size in the bank
      bksze = psbkh32a->data_size;

      // Get to the end of the event
      pdest = (char *) (((BANK_HEADER *) pevent) + 1) + ((BANK_HEADER *) pevent)->data_size;
      // Copy from BANK32 to end of Data
      memmove(pdest, (char *) psbkh32a, ALIGN8(bksze) + sizeof(BANK32A));
      // Bring pointer to the next free location
      pdest += ALIGN8(bksze) + sizeof(BANK32A);

   } else if (bk_is32(psBkh)) {

      // pointer to the source bank header
      BANK32 *psbkh32 = ((BANK32 *) psdata - 1);
      // Data size in the bank
      bksze = psbkh32->data_size;

      // Get to the end of the event
      pdest = (char *) (((BANK_HEADER *) pevent) + 1) + ((BANK_HEADER *) pevent)->data_size;
      // Copy from BANK32 to end of Data
      memmove(pdest, (char *) psbkh32, ALIGN8(bksze) + sizeof(BANK32));
      // Bring pointer to the next free location
      pdest += ALIGN8(bksze) + sizeof(BANK32);

   } else {

      // pointer to the source bank header
      psbkh = ((BANK *) psdata - 1);
      // Data size in the bank
      bksze = psbkh->data_size;

      // Get to the end of the event
      pdest = (char *) (((BANK_HEADER *) pevent) + 1) + ((BANK_HEADER *) pevent)->data_size;
      // Copy from BANK to end of Data
      memmove(pdest, (char *) psbkh, ALIGN8(bksze) + sizeof(BANK));
      // Bring pointer to the next free location
      pdest += ALIGN8(bksze) + sizeof(BANK);
   }

   // Close bank (adjust BANK_HEADER size)
   bk_close(pevent, pdest);
   // Adjust EVENT_HEADER size
   ((EVENT_HEADER *) pevent - 1)->data_size = ((BANK_HEADER *) pevent)->data_size + sizeof(BANK_HEADER);
   return SUCCESS;
}

/********************************************************************/
int bk_delete(void *event, const char *name)
/********************************************************************\

  Routine: bk_delete

  Purpose: Delete a MIDAS bank inside an event

  Input:
    void   *event           pointer to the event
    char   *name            Name of bank (exactly four letters)

  Function value:
    CM_SUCCESS              Bank has been deleted
    0                       Bank has not been found

\********************************************************************/
{
   BANK *pbk;
   DWORD dname;
   int remaining;

   if (bk_is32a((BANK_HEADER *) event)) {
      /* locate bank */
      BANK32A *pbk32a = (BANK32A *) (((BANK_HEADER *) event) + 1);
      copy_bk_name((char *) &dname, name);
      do {
         if (*((DWORD *) pbk32a->name) == dname) {
            /* bank found, delete it */
            remaining = ((char *) event + ((BANK_HEADER *) event)->data_size +
                         sizeof(BANK_HEADER)) - ((char *) (pbk32a + 1) + ALIGN8(pbk32a->data_size));

            /* reduce total event size */
            ((BANK_HEADER *) event)->data_size -= sizeof(BANK32) + ALIGN8(pbk32a->data_size);

            /* copy remaining bytes */
            if (remaining > 0)
               memmove(pbk32a, (char *) (pbk32a + 1) + ALIGN8(pbk32a->data_size), remaining);
            return CM_SUCCESS;
         }

         pbk32a = (BANK32A *) ((char *) (pbk32a + 1) + ALIGN8(pbk32a->data_size));
      } while ((DWORD) ((char *) pbk32a - (char *) event) <
               ((BANK_HEADER *) event)->data_size + sizeof(BANK_HEADER));
   } else if (bk_is32((BANK_HEADER *) event)) {
      /* locate bank */
      BANK32 *pbk32 = (BANK32 *) (((BANK_HEADER *) event) + 1);
      copy_bk_name((char *) &dname, name);
      do {
         if (*((DWORD *) pbk32->name) == dname) {
            /* bank found, delete it */
            remaining = ((char *) event + ((BANK_HEADER *) event)->data_size +
                         sizeof(BANK_HEADER)) - ((char *) (pbk32 + 1) + ALIGN8(pbk32->data_size));

            /* reduce total event size */
            ((BANK_HEADER *) event)->data_size -= sizeof(BANK32) + ALIGN8(pbk32->data_size);

            /* copy remaining bytes */
            if (remaining > 0)
               memmove(pbk32, (char *) (pbk32 + 1) + ALIGN8(pbk32->data_size), remaining);
            return CM_SUCCESS;
         }

         pbk32 = (BANK32 *) ((char *) (pbk32 + 1) + ALIGN8(pbk32->data_size));
      } while ((DWORD) ((char *) pbk32 - (char *) event) <
               ((BANK_HEADER *) event)->data_size + sizeof(BANK_HEADER));
   } else {
      /* locate bank */
      pbk = (BANK *) (((BANK_HEADER *) event) + 1);
      copy_bk_name((char *) &dname, name);
      do {
         if (*((DWORD *) pbk->name) == dname) {
            /* bank found, delete it */
            remaining = ((char *) event + ((BANK_HEADER *) event)->data_size +
                         sizeof(BANK_HEADER)) - ((char *) (pbk + 1) + ALIGN8(pbk->data_size));

            /* reduce total event size */
            ((BANK_HEADER *) event)->data_size -= sizeof(BANK) + ALIGN8(pbk->data_size);

            /* copy remaining bytes */
            if (remaining > 0)
               memmove(pbk, (char *) (pbk + 1) + ALIGN8(pbk->data_size), remaining);
            return CM_SUCCESS;
         }

         pbk = (BANK *) ((char *) (pbk + 1) + ALIGN8(pbk->data_size));
      } while ((DWORD) ((char *) pbk - (char *) event) <
               ((BANK_HEADER *) event)->data_size + sizeof(BANK_HEADER));
   }

   return 0;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Close the Midas bank priviously created by bk_create().
The data pointer pdata must be obtained by bk_create() and
used as an address to fill a bank. It is incremented with every value written
to the bank and finally points to a location just after the last byte of the
bank. It is then passed to bk_close() to finish the bank creation
@param event pointer to current composed event
@param pdata  pointer to the data
@return number of bytes contained in bank
*/
INT bk_close(void *event, void *pdata) {
   if (bk_is32a((BANK_HEADER *) event)) {
      BANK32A *pbk32a = (BANK32A *) ((char *) (((BANK_HEADER *) event) + 1) + ((BANK_HEADER *) event)->data_size);
      pbk32a->data_size = (DWORD) ((char *) pdata - (char *) (pbk32a + 1));
      if (pbk32a->type == TID_STRUCT && pbk32a->data_size == 0)
         printf("Warning: TID_STRUCT bank %c%c%c%c has zero size\n", pbk32a->name[0], pbk32a->name[1], pbk32a->name[2], pbk32a->name[3]);
      ((BANK_HEADER *) event)->data_size += sizeof(BANK32A) + ALIGN8(pbk32a->data_size);
      return pbk32a->data_size;
   } else if (bk_is32((BANK_HEADER *) event)) {
      BANK32 *pbk32 = (BANK32 *) ((char *) (((BANK_HEADER *) event) + 1) + ((BANK_HEADER *) event)->data_size);
      pbk32->data_size = (DWORD) ((char *) pdata - (char *) (pbk32 + 1));
      if (pbk32->type == TID_STRUCT && pbk32->data_size == 0)
         printf("Warning: TID_STRUCT bank %c%c%c%c has zero size\n", pbk32->name[0], pbk32->name[1], pbk32->name[2], pbk32->name[3]);
      ((BANK_HEADER *) event)->data_size += sizeof(BANK32) + ALIGN8(pbk32->data_size);
      return pbk32->data_size;
   } else {
      BANK *pbk = (BANK *) ((char *) (((BANK_HEADER *) event) + 1) + ((BANK_HEADER *) event)->data_size);
      uint32_t size = (uint32_t) ((char *) pdata - (char *) (pbk + 1));
      if (size > 0xFFFF) {
         printf("Error: Bank size %d exceeds 16-bit limit of 65526, please use bk_init32() to create a 32-bit bank\n", size);
         size = 0;
      }
      pbk->data_size = (WORD) (size);
      if (pbk->type == TID_STRUCT && pbk->data_size == 0)
         printf("Warning: TID_STRUCT bank %c%c%c%c has zero size\n", pbk->name[0], pbk->name[1], pbk->name[2], pbk->name[3]);
      size = ((BANK_HEADER *) event)->data_size + sizeof(BANK) + ALIGN8(pbk->data_size);
      if (size > 0xFFFF) {
         printf("Error: Bank size %d exceeds 16-bit limit of 65526, please use bk_init32() to create a 32-bit bank\n", size);
         size = 0;
      }
      ((BANK_HEADER *) event)->data_size = size;
      return pbk->data_size;
   }
}

/********************************************************************/
/**
Extract the MIDAS bank name listing of an event.
The bklist should be dimensioned with STRING_BANKLIST_MAX
which corresponds to a max of BANKLIST_MAX banks (midas.h: 32 banks max).
\code
INT adc_calib(EVENT_HEADER *pheader, void *pevent)
{
  INT    n_adc, nbanks;
  WORD   *pdata;
  char   banklist[STRING_BANKLIST_MAX];

  // Display # of banks and list of banks in the event
  nbanks = bk_list(pevent, banklist);
  printf("#banks:%d List:%s\n", nbanks, banklist);

  // look for ADC0 bank, return if not present
  n_adc = bk_locate(pevent, "ADC0", &pdata);
  ...
}
\endcode
@param event pointer to current composed event
@param bklist returned ASCII string, has to be booked with STRING_BANKLIST_MAX.
@return number of bank found in this event.
*/
INT bk_list(const void *event, char *bklist) {                               /* Full event */
   INT nbk;
   BANK *pmbk = NULL;
   BANK32 *pmbk32 = NULL;
   BANK32A *pmbk32a = NULL;
   char *pdata;

   /* compose bank list */
   bklist[0] = 0;
   nbk = 0;
   do {
      /* scan all banks for bank name only */
      if (bk_is32a(event)) {
         bk_iterate32a(event, &pmbk32a, &pdata);
         if (pmbk32a == NULL)
            break;
      } else if (bk_is32(event)) {
         bk_iterate32(event, &pmbk32, &pdata);
         if (pmbk32 == NULL)
            break;
      } else {
         bk_iterate(event, &pmbk, &pdata);
         if (pmbk == NULL)
            break;
      }
      nbk++;

      if (nbk > BANKLIST_MAX) {
         cm_msg(MINFO, "bk_list", "over %i banks -> truncated", BANKLIST_MAX);
         return (nbk - 1);
      }
      if (bk_is32a(event))
         strncat(bklist, (char *) pmbk32a->name, 4);
      else if (bk_is32(event))
         strncat(bklist, (char *) pmbk32->name, 4);
      else
         strncat(bklist, (char *) pmbk->name, 4);
   } while (1);
   return (nbk);
}

/********************************************************************/
/**
Locates a MIDAS bank of given name inside an event.
@param event pointer to current composed event
@param name bank name to look for
@param pdata pointer to data area of bank, NULL if bank not found
@return number of values inside the bank
*/
INT bk_locate(const void *event, const char *name, void *pdata) {
   BANK *pbk;
   BANK32 *pbk32;
   BANK32A *pbk32a;
   DWORD dname;

   if (bk_is32a(event)) {
      pbk32a = (BANK32A *) (((BANK_HEADER *) event) + 1);
      copy_bk_name((char *) &dname, name);
      while ((DWORD) ((char *) pbk32a - (char *) event) <
             ((BANK_HEADER *) event)->data_size + sizeof(BANK_HEADER)) {
         if (*((DWORD *) pbk32a->name) == dname) {
            *((void **) pdata) = pbk32a + 1;
            if (tid_size[pbk32a->type & 0xFF] == 0)
               return pbk32a->data_size;
            return pbk32a->data_size / tid_size[pbk32a->type & 0xFF];
         }
         pbk32a = (BANK32A *) ((char *) (pbk32a + 1) + ALIGN8(pbk32a->data_size));
      }
   } else if (bk_is32(event)) {
      pbk32 = (BANK32 *) (((BANK_HEADER *) event) + 1);
      copy_bk_name((char *) &dname, name);
      while ((DWORD) ((char *) pbk32 - (char *) event) <
             ((BANK_HEADER *) event)->data_size + sizeof(BANK_HEADER)) {
         if (*((DWORD *) pbk32->name) == dname) {
            *((void **) pdata) = pbk32 + 1;
            if (tid_size[pbk32->type & 0xFF] == 0)
               return pbk32->data_size;
            return pbk32->data_size / tid_size[pbk32->type & 0xFF];
         }
         pbk32 = (BANK32 *) ((char *) (pbk32 + 1) + ALIGN8(pbk32->data_size));
      }
   } else {
      pbk = (BANK *) (((BANK_HEADER *) event) + 1);
      copy_bk_name((char *) &dname, name);
      while ((DWORD) ((char *) pbk - (char *) event) <
             ((BANK_HEADER *) event)->data_size + sizeof(BANK_HEADER)) {
         if (*((DWORD *) pbk->name) == dname) {
            *((void **) pdata) = pbk + 1;
            if (tid_size[pbk->type & 0xFF] == 0)
               return pbk->data_size;
            return pbk->data_size / tid_size[pbk->type & 0xFF];
         }
         pbk = (BANK *) ((char *) (pbk + 1) + ALIGN8(pbk->data_size));
      }

   }

   /* bank not found */
   *((void **) pdata) = NULL;
   return 0;
}

/********************************************************************/
/**
Finds a MIDAS bank of given name inside an event.
@param pbkh pointer to current composed event
@param name bank name to look for
@param bklen number of elemtents in bank
@param bktype bank type, one of TID_xxx
@param pdata pointer to data area of bank, NULL if bank not found
@return 1 if bank found, 0 otherwise
*/
INT bk_find(const BANK_HEADER *pbkh, const char *name, DWORD *bklen, DWORD *bktype, void **pdata) {
   DWORD dname;

   if (bk_is32a(pbkh)) {
      BANK32A *pbk32a = (BANK32A *) (pbkh + 1);
      copy_bk_name((char *) &dname, name);
      do {
         if (*((DWORD *) pbk32a->name) == dname) {
            *((void **) pdata) = pbk32a + 1;
            if (tid_size[pbk32a->type & 0xFF] == 0)
               *bklen = pbk32a->data_size;
            else
               *bklen = pbk32a->data_size / tid_size[pbk32a->type & 0xFF];

            *bktype = pbk32a->type;
            return 1;
         }
         pbk32a = (BANK32A *) ((char *) (pbk32a + 1) + ALIGN8(pbk32a->data_size));
      } while ((DWORD) ((char *) pbk32a - (char *) pbkh) < pbkh->data_size + sizeof(BANK_HEADER));
   } else if (bk_is32(pbkh)) {
         BANK32 *pbk32 = (BANK32 *) (pbkh + 1);
      copy_bk_name((char *) &dname, name);
      do {
         if (*((DWORD *) pbk32->name) == dname) {
            *((void **) pdata) = pbk32 + 1;
            if (tid_size[pbk32->type & 0xFF] == 0)
               *bklen = pbk32->data_size;
            else
               *bklen = pbk32->data_size / tid_size[pbk32->type & 0xFF];

            *bktype = pbk32->type;
            return 1;
         }
         pbk32 = (BANK32 *) ((char *) (pbk32 + 1) + ALIGN8(pbk32->data_size));
      } while ((DWORD) ((char *) pbk32 - (char *) pbkh) < pbkh->data_size + sizeof(BANK_HEADER));
   } else {
      BANK *pbk = (BANK *) (pbkh + 1);
      copy_bk_name((char *) &dname, name);
      do {
         if (*((DWORD *) pbk->name) == dname) {
            *((void **) pdata) = pbk + 1;
            if (tid_size[pbk->type & 0xFF] == 0)
               *bklen = pbk->data_size;
            else
               *bklen = pbk->data_size / tid_size[pbk->type & 0xFF];

            *bktype = pbk->type;
            return 1;
         }
         pbk = (BANK *) ((char *) (pbk + 1) + ALIGN8(pbk->data_size));
      } while ((DWORD) ((char *) pbk - (char *) pbkh) < pbkh->data_size + sizeof(BANK_HEADER));
   }

   /* bank not found */
   *((void **) pdata) = NULL;
   return 0;
}

/********************************************************************/
/**
Iterates through banks inside an event.
The function can be used to enumerate all banks of an event.
The returned pointer to the bank header has following structure:
\code
typedef struct {
char   name[4];
WORD   type;
WORD   data_size;
} BANK;
\endcode
where type is a TID_xxx value and data_size the size of the bank in bytes.
\code
BANK *pbk;
INT  size;
void *pdata;
char name[5];
pbk = NULL;
do
{
 size = bk_iterate(event, &pbk, &pdata);
 if (pbk == NULL)
  break;
 *((DWORD *)name) = *((DWORD *)(pbk)->name);
 name[4] = 0;
 printf("bank %s found\n", name);
} while(TRUE);
\endcode
@param event Pointer to data area of event.
@param pbk pointer to the bank header, must be NULL for the first call to
this function.
@param pdata Pointer to the bank header, must be NULL for the first
call to this function
@return Size of bank in bytes
*/
INT bk_iterate(const void *event, BANK **pbk, void *pdata) {
   if (*pbk == NULL)
      *pbk = (BANK *) (((BANK_HEADER *) event) + 1);
   else
      *pbk = (BANK *) ((char *) (*pbk + 1) + ALIGN8((*pbk)->data_size));

   *((void **) pdata) = (*pbk) + 1;

   if ((DWORD) ((char *) *pbk - (char *) event) >= ((BANK_HEADER *) event)->data_size + sizeof(BANK_HEADER)) {
      *pbk = *((BANK **) pdata) = NULL;
      return 0;
   }

   return (*pbk)->data_size;
}


/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/********************************************************************/
INT bk_iterate32(const void *event, BANK32 **pbk, void *pdata)
/********************************************************************\

  Routine: bk_iterate32

  Purpose: Iterate through 32 bit MIDAS banks inside an event

  Input:
    void   *event           pointer to the event
    BANK32 **pbk32          must be NULL for the first call to bk_iterate

  Output:
    BANK32 **pbk32            pointer to the bank header
    void   *pdata           pointer to data area of the bank

  Function value:
    INT    size of the bank in bytes

\********************************************************************/
{
   if (*pbk == NULL)
      *pbk = (BANK32 *) (((BANK_HEADER *) event) + 1);
   else
      *pbk = (BANK32 *) ((char *) (*pbk + 1) + ALIGN8((*pbk)->data_size));

   *((void **) pdata) = (*pbk) + 1;

   if ((DWORD) ((char *) *pbk - (char *) event) >= ((BANK_HEADER *) event)->data_size + sizeof(BANK_HEADER)) {
      *pbk = NULL;
      pdata = NULL;
      return 0;
   }

   return (*pbk)->data_size;
}

INT bk_iterate32a(const void *event, BANK32A **pbk32a, void *pdata)
/********************************************************************\

  Routine: bk_iterate32a

  Purpose: Iterate through 64-bit aliggned 32 bit MIDAS banks inside an event

  Input:
    void   *event           pointer to the event
    BANK32A **pbk32a        must be NULL for the first call to bk_iterate

  Output:
    BANK32A **pbk32         pointer to the bank header
    void   *pdata           pointer to data area of the bank

  Function value:
    INT    size of the bank in bytes

\********************************************************************/
{
   if (*pbk32a == NULL)
      *pbk32a = (BANK32A *) (((BANK_HEADER *) event) + 1);
   else
      *pbk32a = (BANK32A *) ((char *) (*pbk32a + 1) + ALIGN8((*pbk32a)->data_size));

   *((void **) pdata) = (*pbk32a) + 1;

   if ((DWORD) ((char *) *pbk32a - (char *) event) >= ((BANK_HEADER *) event)->data_size + sizeof(BANK_HEADER)) {
      *pbk32a = NULL;
      pdata = NULL;
      return 0;
   }

   return (*pbk32a)->data_size;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Swaps bytes from little endian to big endian or vice versa for a whole event.

An event contains a flag which is set by bk_init() to identify
the endian format of an event. If force is FALSE, this flag is evaluated and
the event is only swapped if it is in the "wrong" format for this system.
An event can be swapped to the "wrong" format on purpose for example by a
front-end which wants to produce events in a "right" format for a back-end
analyzer which has different byte ordering.
@param event pointer to data area of event
@param force If TRUE, the event is always swapped, if FALSE, the event
is only swapped if it is in the wrong format.
@return 1==event has been swap, 0==event has not been swapped.
*/
INT bk_swap(void *event, BOOL force) {
   BANK_HEADER *pbh;
   BANK *pbk;
   BANK32 *pbk32;
   BANK32A *pbk32a;
   void *pdata;
   WORD type;

   pbh = (BANK_HEADER *) event;

   /* only swap if flags in high 16-bit */
   if (pbh->flags < 0x10000 && !force)
      return 0;

   /* swap bank header */
   DWORD_SWAP(&pbh->data_size);
   DWORD_SWAP(&pbh->flags);

   pbk = (BANK *) (pbh + 1);
   pbk32 = (BANK32 *) pbk;
   pbk32a = (BANK32A *) pbk;

   /* scan event */
   while ((char *) pbk - (char *) pbh < (INT) pbh->data_size + (INT) sizeof(BANK_HEADER)) {
      /* swap bank header */
      if (bk_is32a(event)) {
         DWORD_SWAP(&pbk32a->type);
         DWORD_SWAP(&pbk32a->data_size);
         pdata = pbk32a + 1;
         type = (WORD) pbk32a->type;
      } else if (bk_is32(event)) {
         DWORD_SWAP(&pbk32->type);
         DWORD_SWAP(&pbk32->data_size);
         pdata = pbk32 + 1;
         type = (WORD) pbk32->type;
      } else {
         WORD_SWAP(&pbk->type);
         WORD_SWAP(&pbk->data_size);
         pdata = pbk + 1;
         type = pbk->type;
      }

      /* pbk points to next bank */
      if (bk_is32a(event)) {
         pbk32a = (BANK32A *) ((char *) (pbk32a + 1) + ALIGN8(pbk32a->data_size));
         pbk = (BANK *) pbk32a;
      } else if (bk_is32(event)) {
         pbk32 = (BANK32 *) ((char *) (pbk32 + 1) + ALIGN8(pbk32->data_size));
         pbk = (BANK *) pbk32;
      } else {
         pbk = (BANK *) ((char *) (pbk + 1) + ALIGN8(pbk->data_size));
         pbk32 = (BANK32 *) pbk;
      }

      switch (type) {
         case TID_UINT16:
         case TID_INT16:
            while ((char *) pdata < (char *) pbk) {
               WORD_SWAP(pdata);
               pdata = (void *) (((WORD *) pdata) + 1);
            }
            break;

         case TID_UINT32:
         case TID_INT32:
         case TID_BOOL:
         case TID_FLOAT:
            while ((char *) pdata < (char *) pbk) {
               DWORD_SWAP(pdata);
               pdata = (void *) (((DWORD *) pdata) + 1);
            }
            break;

         case TID_DOUBLE:
         case TID_INT64:
         case TID_UINT64:
            while ((char *) pdata < (char *) pbk) {
               QWORD_SWAP(pdata);
               pdata = (void *) (((double *) pdata) + 1);
            }
            break;
      }
   }

   return CM_SUCCESS;
}

/**dox***************************************************************/

/** @} *//* end of bkfunctionc */


/**dox***************************************************************/
/** @addtogroup rbfunctionc
 *
 *  @{  */

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS
/********************************************************************/

/********************************************************************\
*                                                                    *
*                 Ring buffer functions                              *
*                                                                    *
* Provide an inter-thread buffer scheme for handling front-end       *
* events. This code allows concurrent data acquisition, calibration  *
* and network transfer on a multi-CPU machine. One thread reads      *
* out the data, passes it vis the ring buffer functions              *
* to another thread running on the other CPU, which can then         *
* calibrate and/or send the data over the network.                   *
*                                                                    *
\********************************************************************/

typedef struct {
   unsigned char *buffer;
   unsigned int size;
   unsigned int max_event_size;
   unsigned char *rp;
   unsigned char *wp;
   unsigned char *ep;
} RING_BUFFER;

#define MAX_RING_BUFFER 100

static RING_BUFFER rb[MAX_RING_BUFFER];

static volatile int _rb_nonblocking = 0;

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Set all rb_get_xx to nonblocking. Needed in multi-thread
environments for stopping all theads without deadlock
@return DB_SUCCESS
*/
int rb_set_nonblocking()
/********************************************************************\

  Routine: rb_set_nonblocking

  Purpose: Set all rb_get_xx to nonblocking. Needed in multi-thread
           environments for stopping all theads without deadlock

  Input:
    NONE

  Output:
    NONE

  Function value:
    DB_SUCCESS       Successful completion

\********************************************************************/
{
   _rb_nonblocking = 1;

   return DB_SUCCESS;
}

/********************************************************************/
/**
Create a ring buffer with a given size

Provide an inter-thread buffer scheme for handling front-end
events. This code allows concurrent data acquisition, calibration
and network transfer on a multi-CPU machine. One thread reads
out the data, passes it via the ring buffer functions
to another thread running on the other CPU, which can then
calibrate and/or send the data over the network.

@param size             Size of ring buffer, must be larger than
                         2*max_event_size
@param max_event_size   Maximum event size to be placed into
@param *handle          Handle to ring buffer
@return DB_SUCCESS, DB_NO_MEMORY, DB_INVALID_PARAM
*/
int rb_create(int size, int max_event_size, int *handle)
/********************************************************************\

  Routine: rb_create

  Purpose: Create a ring buffer with a given size

  Input:
    int size             Size of ring buffer, must be larger than
                         2*max_event_size
    int max_event_size   Maximum event size to be placed into
                         ring buffer
  Output:
    int *handle          Handle to ring buffer

  Function value:
    DB_SUCCESS           Successful completion
    DB_NO_MEMORY         Maximum number of ring buffers exceeded
    DB_INVALID_PARAM     Invalid event size specified

\********************************************************************/
{
   int i;

   for (i = 0; i < MAX_RING_BUFFER; i++)
      if (rb[i].buffer == NULL)
         break;

   if (i == MAX_RING_BUFFER)
      return DB_NO_MEMORY;

   if (size < max_event_size * 2)
      return DB_INVALID_PARAM;

   memset(&rb[i], 0, sizeof(RING_BUFFER));
   rb[i].buffer = (unsigned char *) M_MALLOC(size);
   assert(rb[i].buffer);
   rb[i].size = size;
   rb[i].max_event_size = max_event_size;
   rb[i].rp = rb[i].buffer;
   rb[i].wp = rb[i].buffer;
   rb[i].ep = rb[i].buffer;

   *handle = i + 1;

   return DB_SUCCESS;
}

/********************************************************************/
/**
Delete a ring buffer
@param handle  Handle of the ring buffer
@return  DB_SUCCESS
*/
int rb_delete(int handle)
/********************************************************************\

  Routine: rb_delete

  Purpose: Delete a ring buffer

  Input:
    none
  Output:
    int handle       Handle to ring buffer

  Function value:
    DB_SUCCESS       Successful completion

\********************************************************************/
{
   if (handle < 0 || handle >= MAX_RING_BUFFER || rb[handle - 1].buffer == NULL)
      return DB_INVALID_HANDLE;

   M_FREE(rb[handle - 1].buffer);
   rb[handle - 1].buffer = NULL;
   memset(&rb[handle - 1], 0, sizeof(RING_BUFFER));

   return DB_SUCCESS;
}

/********************************************************************/
/**
Retrieve write pointer where new data can be written
@param handle               Ring buffer handle
@param millisec             Optional timeout in milliseconds if
                              buffer is full. Zero to not wait at
                              all (non-blocking)
@param  **p                  Write pointer
@return DB_SUCCESS, DB_TIMEOUT, DB_INVALID_HANDLE
*/
int rb_get_wp(int handle, void **p, int millisec)
/********************************************************************\

Routine: rb_get_wp

  Purpose: Retrieve write pointer where new data can be written

  Input:
     int handle               Ring buffer handle
     int millisec             Optional timeout in milliseconds if
                              buffer is full. Zero to not wait at
                              all (non-blocking)

  Output:
    char **p                  Write pointer

  Function value:
    DB_SUCCESS       Successful completion

\********************************************************************/
{
   int h, i;
   unsigned char *rp;

   if (handle < 1 || handle > MAX_RING_BUFFER || rb[handle - 1].buffer == NULL)
      return DB_INVALID_HANDLE;

   h = handle - 1;

   for (i = 0; i <= millisec / 10; i++) {

      rp = rb[h].rp;            // keep local copy for convenience

      /* check if enough size for wp >= rp without wrap-around */
      if (rb[h].wp >= rp
          && rb[h].wp + rb[h].max_event_size <= rb[h].buffer + rb[h].size - rb[h].max_event_size) {
         *p = rb[h].wp;
         return DB_SUCCESS;
      }

      /* check if enough size for wp >= rp with wrap-around */
      if (rb[h].wp >= rp && rb[h].wp + rb[h].max_event_size > rb[h].buffer + rb[h].size - rb[h].max_event_size &&
          rp > rb[h].buffer) {    // next increment of wp wraps around, so need space at beginning
         *p = rb[h].wp;
         return DB_SUCCESS;
      }

      /* check if enough size for wp < rp */
      if (rb[h].wp < rp && rb[h].wp + rb[h].max_event_size < rp) {
         *p = rb[h].wp;
         return DB_SUCCESS;
      }

      if (millisec == 0)
         return DB_TIMEOUT;

      if (_rb_nonblocking)
         return DB_TIMEOUT;

      /* wait one time slice */
      ss_sleep(10);
   }

   return DB_TIMEOUT;
}

/********************************************************************/
/** rb_increment_wp

Increment current write pointer, making the data at
the write pointer available to the receiving thread
@param handle               Ring buffer handle
@param size                 Number of bytes placed at the WP
@return DB_SUCCESS, DB_INVALID_PARAM, DB_INVALID_HANDLE
*/
int rb_increment_wp(int handle, int size)
/********************************************************************\

  Routine: rb_increment_wp

  Purpose: Increment current write pointer, making the data at
           the write pointer available to the receiving thread

  Input:
     int handle               Ring buffer handle
     int size                 Number of bytes placed at the WP

  Output:
    NONE

  Function value:
    DB_SUCCESS                Successful completion
    DB_INVALID_PARAM          Event size too large or invalid handle
\********************************************************************/
{
   int h;
   unsigned char *new_wp;

   if (handle < 1 || handle > MAX_RING_BUFFER || rb[handle - 1].buffer == NULL)
      return DB_INVALID_HANDLE;

   h = handle - 1;

   if ((DWORD) size > rb[h].max_event_size) {
      cm_msg(MERROR, "rb_increment_wp", "event size of %d MB larger than max_event_size of %d MB",
             size/1024/1024, rb[h].max_event_size/1024/1024);
      abort();
   }

   new_wp = rb[h].wp + size;

   /* wrap around wp if not enough space */
   if (new_wp > rb[h].buffer + rb[h].size - rb[h].max_event_size) {
      rb[h].ep = new_wp;
      new_wp = rb[h].buffer;
      assert(rb[h].rp != rb[h].buffer);
   } else
      if (new_wp > rb[h].ep)
         rb[h].ep = new_wp;

   rb[h].wp = new_wp;

   return DB_SUCCESS;
}

/********************************************************************/
/**
Obtain the current read pointer at which new data is
available with optional timeout

@param  handle               Ring buffer handle
@param  millisec             Optional timeout in milliseconds if
                             buffer is full. Zero to not wait at
                             all (non-blocking)

@param **p                 Address of pointer pointing to newly
                             available data. If p == NULL, only
                             return status.
@return  DB_SUCCESS, DB_TIEMOUT, DB_INVALID_HANDLE

*/
int rb_get_rp(int handle, void **p, int millisec)
/********************************************************************\

  Routine: rb_get_rp

  Purpose: Obtain the current read pointer at which new data is
           available with optional timeout

  Input:
    int handle               Ring buffer handle
    int millisec             Optional timeout in milliseconds if
                             buffer is full. Zero to not wait at
                             all (non-blocking)

  Output:
    char **p                 Address of pointer pointing to newly
                             available data. If p == NULL, only
                             return status.

  Function value:
    DB_SUCCESS       Successful completion

\********************************************************************/
{
   int i, h;

   if (handle < 1 || handle > MAX_RING_BUFFER || rb[handle - 1].buffer == NULL)
      return DB_INVALID_HANDLE;

   h = handle - 1;

   for (i = 0; i <= millisec / 10; i++) {

      if (rb[h].wp != rb[h].rp) {
         if (p != NULL)
            *p = rb[handle - 1].rp;
         return DB_SUCCESS;
      }

      if (millisec == 0)
         return DB_TIMEOUT;

      if (_rb_nonblocking)
         return DB_TIMEOUT;

      /* wait one time slice */
      ss_sleep(10);
   }

   return DB_TIMEOUT;
}

/********************************************************************/
/**
Increment current read pointer, freeing up space for the writing thread.

@param handle               Ring buffer handle
@param size                 Number of bytes to free up at current
                              read pointer
@return  DB_SUCCESS, DB_INVALID_PARAM

*/
int rb_increment_rp(int handle, int size)
/********************************************************************\

  Routine: rb_increment_rp

  Purpose: Increment current read pointer, freeing up space for
           the writing thread.

  Input:
     int handle               Ring buffer handle
     int size                 Number of bytes to free up at current
                              read pointer

  Output:
    NONE

  Function value:
    DB_SUCCESS                Successful completion
    DB_INVALID_PARAM          Event size too large or invalid handle

\********************************************************************/
{
   int h;

   unsigned char *new_rp;
   unsigned char *ep;

   if (handle < 1 || handle > MAX_RING_BUFFER || rb[handle - 1].buffer == NULL)
      return DB_INVALID_HANDLE;

   h = handle - 1;

   if ((DWORD) size > rb[h].max_event_size)
      return DB_INVALID_PARAM;

   new_rp = rb[h].rp + size;
   ep = rb[h].ep; // keep local copy of end pointer, rb[h].ep might be changed by other thread

   /* wrap around if end pointer reached */
   if (new_rp >= ep && rb[h].wp < ep)
      new_rp = rb[h].buffer;

   rb[handle - 1].rp = new_rp;

   return DB_SUCCESS;
}

/********************************************************************/
/**
Return number of bytes in a ring buffer

@param handle              Handle of the buffer to get the info
@param *n_bytes            Number of bytes in buffer
@return DB_SUCCESS, DB_INVALID_HANDLE
*/
int rb_get_buffer_level(int handle, int *n_bytes)
/********************************************************************\

  Routine: rb_get_buffer_level

  Purpose: Return number of bytes in a ring buffer

  Input:
    int handle              Handle of the buffer to get the info

  Output:
    int *n_bytes            Number of bytes in buffer

  Function value:
    DB_SUCCESS              Successful completion
    DB_INVALID_HANDLE       Buffer handle is invalid

\********************************************************************/
{
   int h;

   if (handle < 1 || handle > MAX_RING_BUFFER || rb[handle - 1].buffer == NULL)
      return DB_INVALID_HANDLE;

   h = handle - 1;

   if (rb[h].wp >= rb[h].rp)
      *n_bytes = (POINTER_T) rb[h].wp - (POINTER_T) rb[h].rp;
   else
      *n_bytes =
              (POINTER_T) rb[h].ep - (POINTER_T) rb[h].rp + (POINTER_T) rb[h].wp - (POINTER_T) rb[h].buffer;

   return DB_SUCCESS;
}

/** @} *//* end of rbfunctionc */


int cm_write_event_to_odb(HNDLE hDB, HNDLE hKey, const EVENT_HEADER* pevent, INT format)
{
   if (format == FORMAT_FIXED) {
      int status;
      status = db_set_record(hDB, hKey, (char *) (pevent + 1), pevent->data_size, 0);
      if (status != DB_SUCCESS) {
         cm_msg(MERROR, "cm_write_event_to_odb", "event %d ODB record size mismatch, db_set_record() status %d", pevent->event_id, status);
         return status;
      }
      return SUCCESS;
   } else if (format == FORMAT_MIDAS) {
      INT size, i, status, n_data;
      int n;
      char *pdata, *pdata0;

      char name[5];
      BANK_HEADER *pbh;
      BANK *pbk;
      BANK32 *pbk32;
      BANK32A *pbk32a;
      DWORD bkname;
      WORD bktype;
      HNDLE hKeyRoot, hKeyl, *hKeys;
      KEY key;

      pbh = (BANK_HEADER *) (pevent + 1);
      pbk = NULL;
      pbk32 = NULL;
      pbk32a = NULL;

      /* count number of banks */
      for (n=0 ; ; n++) {
         if (bk_is32a(pbh)) {
            bk_iterate32a(pbh, &pbk32a, &pdata);
            if (pbk32a == NULL)
               break;
         } else if (bk_is32(pbh)) {
            bk_iterate32(pbh, &pbk32, &pdata);
            if (pbk32 == NULL)
               break;
         } else {
            bk_iterate(pbh, &pbk, &pdata);
            if (pbk == NULL)
               break;
         }
      }

      /* build array of keys */
      hKeys = (HNDLE *)malloc(sizeof(HNDLE) * n);

      pbk = NULL;
      pbk32 = NULL;
      n = 0;
      do {
         /* scan all banks */
         if (bk_is32a(pbh)) {
            size = bk_iterate32a(pbh, &pbk32a, &pdata);
            if (pbk32a == NULL)
               break;
            bkname = *((DWORD *) pbk32a->name);
            bktype = (WORD) pbk32a->type;
         } else if (bk_is32(pbh)) {
            size = bk_iterate32(pbh, &pbk32, &pdata);
            if (pbk32 == NULL)
               break;
            bkname = *((DWORD *) pbk32->name);
            bktype = (WORD) pbk32->type;
         } else {
            size = bk_iterate(pbh, &pbk, &pdata);
            if (pbk == NULL)
               break;
            bkname = *((DWORD *) pbk->name);
            bktype = (WORD) pbk->type;
         }

         n_data = size;
         if (rpc_tid_size(bktype & 0xFF))
            n_data /= rpc_tid_size(bktype & 0xFF);

         /* get bank key */
         *((DWORD *) name) = bkname;
         name[4] = 0;
         /* record the start of the data in case it is struct */
         pdata0 = pdata;
         if (bktype == TID_STRUCT) {
            status = db_find_key(hDB, hKey, name, &hKeyRoot);
            if (status != DB_SUCCESS) {
               cm_msg(MERROR, "cm_write_event_to_odb", "please define bank \"%s\" in BANK_LIST in frontend", name);
               continue;
            }

            /* write structured bank */
            for (i = 0;; i++) {
               status = db_enum_key(hDB, hKeyRoot, i, &hKeyl);
               if (status == DB_NO_MORE_SUBKEYS)
                  break;

               db_get_key(hDB, hKeyl, &key);

               /* adjust for alignment */
               if (key.type != TID_STRING && key.type != TID_LINK)
                  pdata = (pdata0 + VALIGN(pdata-pdata0, MIN(ss_get_struct_align(), key.item_size)));

               status = db_set_data1(hDB, hKeyl, pdata, key.item_size * key.num_values, key.num_values, key.type);
               if (status != DB_SUCCESS) {
                  cm_msg(MERROR, "cm_write_event_to_odb", "cannot write bank \"%s\" to ODB, db_set_data1() status %d", name, status);
                  continue;
               }
               hKeys[n++] = hKeyl;

               /* shift data pointer to next item */
               pdata += key.item_size * key.num_values;
            }
         } else {
            /* write variable length bank  */
            status = db_find_key(hDB, hKey, name, &hKeyRoot);
            if (status != DB_SUCCESS) {
               status = db_create_key(hDB, hKey, name, bktype);
               if (status != DB_SUCCESS) {
                  cm_msg(MERROR, "cm_write_event_to_odb", "cannot create key for bank \"%s\" with tid %d in ODB, db_create_key() status %d", name, bktype, status);
                  continue;
               }
               status = db_find_key(hDB, hKey, name, &hKeyRoot);
               if (status != DB_SUCCESS) {
                  cm_msg(MERROR, "cm_write_event_to_odb", "cannot find key for bank \"%s\" in ODB, after db_create_key(), db_find_key() status %d", name, status);
                  continue;
               }
            }
            if (n_data > 0) {
               status = db_set_data1(hDB, hKeyRoot, pdata, size, n_data, bktype & 0xFF);
               if (status != DB_SUCCESS) {
                  cm_msg(MERROR, "cm_write_event_to_odb", "cannot write bank \"%s\" to ODB, db_set_data1() status %d", name, status);
               }
               hKeys[n++] = hKeyRoot;
            }
         }
      } while (1);

      /* notify all hot-lined clients in one go */
      db_notify_clients_array(hDB, hKeys, n*sizeof(INT));

      free(hKeys);

      return SUCCESS;
   } else {
      cm_msg(MERROR, "cm_write_event_to_odb", "event format %d is not supported (see midas.h definitions of FORMAT_xxx)", format);
      return CM_DB_ERROR;
   }
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
