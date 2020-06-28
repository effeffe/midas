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


static struct {
   int transition;
   char name[32];
} trans_name[] = {
        {
                TR_START,      "START",},
        {
                TR_STOP,       "STOP",},
        {
                TR_PAUSE,      "PAUSE",},
        {
                TR_RESUME,     "RESUME",},
        {
                TR_STARTABORT, "STARTABORT",},
        {
                TR_DEFERRED,   "DEFERRED",},
        {
                0,             "",},};

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

static BUFFER *_buffer;
static INT _buffer_entries = 0;

static INT _msg_buffer = 0;
static INT _msg_rb = 0;
static MUTEX_T *_msg_mutex = NULL;
static EVENT_HANDLER *_msg_dispatch = NULL;

static REQUEST_LIST *_request_list;
static INT _request_list_entries = 0;

static EVENT_HEADER *_event_buffer;
static INT _event_buffer_size = 0;

static char *_tcp_buffer = NULL;
static INT _tcp_wp = 0;
static INT _tcp_rp = 0;
static INT _tcp_sock = 0;

static MUTEX_T *_mutex_rpc = NULL; // mutex to protect RPC calls

static void (*_debug_print)(const char *) = NULL;

static INT _debug_mode = 0;

static int _rpc_connect_timeout = 10000;

// for use on a single machine it is best to restrict RPC access to localhost
// by binding the RPC listener socket to the localhost IP address.
static int disable_bind_rpc_to_localhost = 0;

/* table for transition functions */

typedef struct {
   INT transition;
   INT sequence_number;

   INT (*func)(INT, char *);
} TRANS_TABLE;

#define MAX_TRANSITIONS 20

static TRANS_TABLE _trans_table[MAX_TRANSITIONS];

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
                            "\"exptab\" file not found and MIDAS_DIR environment variable not defined"},
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

typedef struct {
   INT transition;
   INT run_number;
   char *errstr;
   INT errstr_size;
   INT async_flag;
   INT debug_flag;
   INT status;
   BOOL finished;
} TR_PARAM;

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

static void xwrite(const char *filename, int fd, const void *data, int size) {
   int wr = write(fd, data, size);
   if (wr != size) {
      printf("xwrite: cannot write to \'%s\', write(%d) returned %d, errno %d (%s)\n", filename, size, wr, errno,
             strerror(errno));
   }
}

static std::vector<std::string> split(const char* sep, const std::string& s)
{
   unsigned sep_len = strlen(sep);
   std::vector<std::string> v;
   std::string::size_type pos = 0;
   while (1) {
      std::string::size_type next = s.find(sep, pos);
      //printf("pos %d, next %d\n", (int)pos, (int)next);
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

/********************************************************************\
*                                                                    *
*              Common message functions                              *
*                                                                    *
\********************************************************************/

static int (*_message_print)(const char *) = puts;

static INT _message_mask_system = MT_ALL;
static INT _message_mask_user = MT_ALL;


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
INT cm_get_error(INT code, char *string) {
   INT i;

   for (i = 0; _error_table[i].code; i++)
      if (_error_table[i].code == code) {
         strcpy(string, _error_table[i].string);
         return CM_SUCCESS;
      }

   sprintf(string, "Unexpected error #%d", code);
   return CM_SUCCESS;
}

/********************************************************************/
int cm_msg_early_init(void) {
   int status;

   if (!_msg_rb) {
      status = rb_create(100 * 1024, 1024, &_msg_rb);
      assert(status == SUCCESS);
   }

   if (!_msg_mutex) {
      status = ss_mutex_create(&_msg_mutex, FALSE);
      assert(status == SS_SUCCESS || status == SS_CREATED);
   }

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
   char path[256], *flist;

   cm_msg_get_logfile("midas", 0, path, sizeof(path), NULL, 0);

   if (strrchr(path, DIR_SEPARATOR))
      *strrchr(path, DIR_SEPARATOR) = 0;
   else
      path[0] = 0;

   int n = ss_file_find(path, "*.log", &flist);

   for (int i = 0; i < n; i++) {
      char *p = flist + i * MAX_STRING_LENGTH;
      if (strchr(p, '_') == NULL && !(p[0] >= '0' && p[0] <= '9')) {
         char *s = strchr(p, '.');
         if (s)
            *s = 0;
         list->push_back(p);
      }
   }

   if (n > 0) {
      free(flist);
   }

   return SUCCESS;
}

/********************************************************************/

int
cm_msg_get_logfile(const char *fac, time_t t, char *filename, int filename_size, char *linkname, int linkname_size) {
   HNDLE hDB, hKey;
   char dir[256];
   char str[256];
   char date_ext[256];
   char facility[256];
   int status, size, flag;

   status = cm_get_experiment_database(&hDB, NULL);

   if (status != CM_SUCCESS)
      return -1;
   if (!hDB)
      return -1;

   if (linkname)
      linkname[0] = 0;
   flag = 0;

   if (fac && fac[0])
      strlcpy(facility, fac, sizeof(facility));
   else
      strlcpy(facility, "midas", sizeof(facility));

   strlcpy(str, "midas.log", sizeof(str));
   size = sizeof(str);
   status = db_get_value(hDB, 0, "/Logger/Message file", str, &size, TID_STRING, TRUE);

   if (status != DB_SUCCESS)
      return -1;

   /* extension must be .log and will be added later */
   if (strchr(str, '.'))
      *strchr(str, '.') = 0;

   if (strchr(str, '%')) {
      /* replace stings such as %y%m%d with current date */
      struct tm *tms;

      flag = 1;
      tzset();
      if (t == 0)
         time(&t);
      tms = localtime(&t);

      date_ext[0] = '_';
      strftime(date_ext + 1, sizeof(date_ext), strchr(str, '%'), tms);
   } else {
      date_ext[0] = 0;
   }

   if (strchr(str, DIR_SEPARATOR) == NULL) {
      status = db_find_key(hDB, 0, "/Logger/Data dir", &hKey);
      if (status == DB_SUCCESS) {
         size = sizeof(dir);
         memset(dir, 0, size);
         status = db_get_value(hDB, 0, "/Logger/Data dir", dir, &size, TID_STRING, TRUE);
         if (status != DB_SUCCESS)
            return -1;
         if (dir[0] != 0) {
            if (dir[strlen(dir) - 1] != DIR_SEPARATOR)
               strlcat(dir, DIR_SEPARATOR_STR, sizeof(dir));
         } else {
            cm_get_path(dir, sizeof(dir));
            if (dir[0] == 0) {
               const char *s = getcwd(dir, sizeof(dir));
               // per "man getcwd" we must check the return value and if it is NULL, contents of "dir" may be undefined and we must fix it.
               if (s == NULL)
                  dir[0] = 0;
            }
            if (dir[strlen(dir) - 1] != DIR_SEPARATOR)
               strlcat(dir, DIR_SEPARATOR_STR, sizeof(dir));
         }
      } else {
         cm_get_path(dir, sizeof(dir));
         if (dir[0] != 0)
            if (dir[strlen(dir) - 1] != DIR_SEPARATOR)
               strlcat(dir, DIR_SEPARATOR_STR, sizeof(dir));
      }
   } else {
      strlcpy(dir, str, sizeof(dir));
      *(strrchr(dir, DIR_SEPARATOR) + 1) = 0;
   }

   strlcpy(filename, dir, filename_size);
   strlcat(filename, facility, filename_size);
   strlcat(filename, date_ext, filename_size);
   strlcat(filename, ".log", filename_size);

   if (date_ext[0] && linkname) {
      strlcpy(linkname, dir, filename_size);
      strlcat(linkname, facility, filename_size);
      strlcat(linkname, ".log", filename_size);
   }

   return flag;
}

int
cm_msg_get_logfile1(const char *fac, time_t t, char *filename, int filename_size, char *linkname, int linkname_size) {
   static int first_time = 1;
   static int prev_flag = 0;
   static char prev_filename[256];
   static char prev_linkname[256];

   if (first_time) {
      first_time = 0;
      if (fac && fac[0]) {
         strlcpy(prev_filename, fac, sizeof(prev_filename));
      } else {
         strlcpy(prev_filename, "midas", sizeof(prev_filename));
      }
      strlcat(prev_filename, ".log", sizeof(prev_filename));
      prev_linkname[0] = 0;
   }

   if (filename)
      filename[0] = 0;
   if (linkname)
      linkname[0] = 0;

   int flag = cm_msg_get_logfile(fac, t, filename, filename_size, linkname, linkname_size);

   //printf("cm_msg_get_logfile1: flag %d prev %d, filename [%s] prev [%s], linkname [%s] prev [%s]\n", flag, prev_flag, filename, prev_filename, linkname, prev_linkname);

   if (flag >= 0) {
      prev_flag = flag;
      if (filename)
         strlcpy(prev_filename, filename, sizeof(prev_filename));
      if (linkname)
         strlcpy(prev_linkname, linkname, sizeof(prev_linkname));
      return flag;
   }

   if (filename)
      strlcpy(filename, prev_filename, filename_size);
   if (linkname)
      strlcpy(linkname, prev_linkname, linkname_size);
   return prev_flag;
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
   char filename[256], linkname[256];
   INT status, fh, semaphore;

   filename[0] = 0;

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
      int flag = cm_msg_get_logfile1(facility, 0, filename, sizeof(filename), linkname, sizeof(linkname));

      if (flag < 0) {
         fprintf(stderr,
                 "cm_msg_log: Message \"%s\" not written to midas.log because cm_msg_get_logfile1() failed with flag %d\n",
                 message, flag);
         return CM_SUCCESS;
      }

      fh = open(filename, O_WRONLY | O_CREAT | O_APPEND | O_LARGEFILE, 0644);
      if (fh < 0) {
         fprintf(stderr,
                 "cm_msg_log: Message \"%s\" not written to midas.log because open(%s) failed with errno %d (%s)\n",
                 message, filename, errno, strerror(errno));
      } else {
         char str[256];

         cm_get_experiment_semaphore(NULL, NULL, NULL, &semaphore);

         if (semaphore == -1) {
            fprintf(stderr,
                    "cm_msg_log: Message \"%s\" not written to midas.log (%s) because the message semaphore is not initialized yet.\n",
                    message, filename);
            return CM_SUCCESS;
         }

         status = ss_semaphore_wait_for(semaphore, 5 * 1000);
         if (status != SS_SUCCESS) {
            fprintf(stderr,
                    "cm_msg_log: Something is wrong with our semaphore, ss_semaphore_wait_for() returned %d, aborting.\n",
                    status);
            //abort(); // DOES NOT RETURN
            // NOT REACHED
            fprintf(stderr,
                    "cm_msg_log: Cannot abort - this will lock you out of odb. From this point, MIDAS will not work correctly. Please read the discussion at https://midas.triumf.ca/elog/Midas/945\n");
            return status;
         }

         struct timeval tv;
         struct tm *tms;

         tzset();
         gettimeofday(&tv, NULL);
         tms = localtime(&tv.tv_sec);

         strftime(str, sizeof(str), "%H:%M:%S", tms);
         sprintf(str + strlen(str), ".%03d ", (int) (tv.tv_usec / 1000));
         strftime(str + strlen(str), sizeof(str), "%G/%m/%d", tms);

         xwrite(filename, fh, str, strlen(str));
         xwrite(filename, fh, " ", 1);
         xwrite(filename, fh, message, strlen(message));
         xwrite(filename, fh, "\n", 1);
         close(fh);

#ifdef OS_LINUX
         if (linkname[0]) {
            unlink(linkname);
            status = symlink(filename, linkname);
            if (status != 0) {
               fprintf(stderr,
                       "cm_msg_log: Error: Cannot symlink message log file \'%s' to \'%s\', symlink() errno: %d (%s)\n",
                       filename, linkname, errno, strerror(errno));
            }
         }
#endif

         status = ss_semaphore_release(semaphore);
      }
   }

   return CM_SUCCESS;
}


static INT
cm_msg_format(char *message, int sizeof_message, INT message_type, const char *filename, INT line, const char *routine,
              const char *format, va_list *argptr) {
   char type_str[256], str[1000], format_cpy[900];
   const char *pc;

   /* strip path */
   pc = filename + strlen(filename);
   while (*pc != '\\' && *pc != '/' && pc != filename)
      pc--;
   if (pc != filename)
      pc++;

   /* convert type to string */
   type_str[0] = 0;
   if (message_type & MT_ERROR)
      strlcat(type_str, MT_ERROR_STR, sizeof(type_str));
   if (message_type & MT_INFO)
      strlcat(type_str, MT_INFO_STR, sizeof(type_str));
   if (message_type & MT_DEBUG)
      strlcat(type_str, MT_DEBUG_STR, sizeof(type_str));
   if (message_type & MT_USER)
      strlcat(type_str, MT_USER_STR, sizeof(type_str));
   if (message_type & MT_LOG)
      strlcat(type_str, MT_LOG_STR, sizeof(type_str));
   if (message_type & MT_TALK)
      strlcat(type_str, MT_TALK_STR, sizeof(type_str));

   /* print client name into string */
   if (message_type == MT_USER)
      sprintf(message, "[%s] ", routine);
   else {
      std::string name = rpc_get_name();
      strlcpy(str, name.c_str(), sizeof(str));
      if (str[0])
         sprintf(message, "[%s,%s] ", str, type_str);
      else
         message[0] = 0;
   }

   /* preceed error messages with file and line info */
   if (message_type == MT_ERROR) {
      sprintf(str, "[%s:%d:%s,%s] ", pc, line, routine, type_str);
      strlcat(message, str, sizeof_message);
   } else if (message_type == MT_USER)
      sprintf(message, "[%s,%s] ", routine, type_str);

   /* limit length of format */
   strlcpy(format_cpy, format, sizeof(format_cpy));

   /* print argument list into message */
   vsprintf(str, (char *) format, *argptr);

   strlcat(message, str, sizeof_message);

   return CM_SUCCESS;
}

static INT cm_msg_send_event(INT ts, INT message_type, const char *send_message) {
   //printf("cm_msg_send: ts %d, type %d, message [%s]\n", ts, message_type, send_message);

   /* send event if not of type MLOG */
   if (message_type != MT_LOG) {
      if (_msg_buffer) {
         /* copy message to event */
         char event[1000];
         EVENT_HEADER *pevent = (EVENT_HEADER *) event;

         strlcpy(event + sizeof(EVENT_HEADER), send_message, sizeof(event) - sizeof(EVENT_HEADER));

         /* setup the event header and send the message */
         bm_compose_event(pevent, EVENTID_MESSAGE, (WORD) message_type, strlen(event + sizeof(EVENT_HEADER)) + 1, 0);
         pevent->time_stamp = ts;
         bm_send_event(_msg_buffer, pevent, pevent->data_size + sizeof(EVENT_HEADER), BM_WAIT);
      }
   }

   return CM_SUCCESS;
}

static INT cm_msg_buffer(int ts, int message_type, const char *message) {
   int status;
   int len;
   char *wp;
   void *vp;

   //printf("cm_msg_buffer ts %d, type %d, message [%s]!\n", ts, message_type, message);

   if (!_msg_rb) {
      fprintf(stderr, "cm_msg_buffer: Error: dropped message [%s] because message ring buffer is not initialized\n",
              message);
      return CM_SUCCESS;
   }

   len = strlen(message) + 1;

   // lock
   status = ss_mutex_wait_for(_msg_mutex, 0);
   assert(status == SS_SUCCESS);

   status = rb_get_wp(_msg_rb, &vp, 1000);
   wp = (char *) vp;

   if (status != SUCCESS || wp == NULL) {
      // unlock
      ss_mutex_release(_msg_mutex);
      return SS_NO_MEMORY;
   }

   *wp++ = 'M';
   *wp++ = 'S';
   *wp++ = 'G';
   *wp++ = '_';
   *(int *) wp = ts;
   wp += sizeof(int);
   *(int *) wp = message_type;
   wp += sizeof(int);
   *(int *) wp = len;
   wp += sizeof(int);
   memcpy(wp, message, len);
   rb_increment_wp(_msg_rb, 4 + 3 * sizeof(int) + len);

   // unlock
   ss_mutex_release(_msg_mutex);

   return CM_SUCCESS;
}

/********************************************************************/
/**
This routine can be called to process messages buffered by cm_msg(). Normally
it is called from cm_yield() and cm_disconnect_experiment() to make sure
all accumulated messages are processed.
*/
INT cm_msg_flush_buffer() {
   int i;

   //printf("cm_msg_flush_buffer!\n");

   if (!_msg_rb)
      return CM_SUCCESS;

   for (i = 0; i < 100; i++) {
      int status;
      int ts;
      int message_type;
      char message[1024];
      int n_bytes;
      char *rp;
      void *vp;
      int len;

      status = rb_get_buffer_level(_msg_rb, &n_bytes);

      if (status != SUCCESS || n_bytes <= 0)
         break;

      // lock
      status = ss_mutex_wait_for(_msg_mutex, 0);
      assert(status == SS_SUCCESS);

      status = rb_get_rp(_msg_rb, &vp, 0);
      rp = (char *) vp;
      if (status != SUCCESS || rp == NULL) {
         // unlock
         ss_mutex_release(_msg_mutex);
         return SS_NO_MEMORY;
      }

      assert(rp);
      assert(rp[0] == 'M');
      assert(rp[1] == 'S');
      assert(rp[2] == 'G');
      assert(rp[3] == '_');
      rp += 4;

      ts = *(int *) rp;
      rp += sizeof(int);

      message_type = *(int *) rp;
      rp += sizeof(int);

      len = *(int *) rp;
      rp += sizeof(int);

      strlcpy(message, rp, sizeof(message));

      rb_increment_rp(_msg_rb, 4 + 3 * sizeof(int) + len);

      // unlock
      ss_mutex_release(_msg_mutex);

      /* log message */
      cm_msg_log(message_type, "midas", message);

      /* send message to SYSMSG */
      status = cm_msg_send_event(ts, message_type, message);
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
INT cm_msg(INT message_type, const char *filename, INT line, const char *routine, const char *format, ...) {
   va_list argptr;
   char message[1000];
   INT status;
   static BOOL in_routine = FALSE;
   int ts = ss_time();

   /* print argument list into message */
   va_start(argptr, format);
   cm_msg_format(message, sizeof(message), message_type, filename, line, routine, format, &argptr);
   va_end(argptr);

   //printf("message [%s]\n", message);

   /* avoid recursive calls */
   if (in_routine) {
      fprintf(stderr, "cm_msg: Error: dropped message [%s] to break recursion\n", message);
      return CM_SUCCESS;
   }

   in_routine = TRUE;

   /* call user function if set via cm_set_msg_print */
   if (_message_print != NULL && (message_type & _message_mask_user) != 0)
      _message_print(message);

   /* return if system mask is not set */
   if ((message_type & _message_mask_system) == 0) {
      in_routine = FALSE;
      return CM_SUCCESS;
   }

   status = cm_msg_buffer(ts, message_type, message);

   in_routine = FALSE;

   return status;
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
   char message[256];
   static BOOL in_routine = FALSE;

   /* avoid recursive calles */
   if (in_routine)
      return 0;

   in_routine = TRUE;

   /* print argument list into message */
   va_start(argptr, format);
   cm_msg_format(message, sizeof(message), message_type, filename, line, routine, format, &argptr);
   va_end(argptr);

   /* call user function if set via cm_set_msg_print */
   if (_message_print != NULL && (message_type & _message_mask_user) != 0)
      _message_print(message);

   /* return if system mask is not set */
   if ((message_type & _message_mask_system) == 0) {
      in_routine = FALSE;
      return CM_SUCCESS;
   }

   /* send event if not of type MLOG */
   if (message_type != MT_LOG) {
      /* if no message buffer already opened, do so now */
      if (_msg_buffer) {
         /* copy message to event */
         char event[1000];
         EVENT_HEADER *pevent = (EVENT_HEADER *) event;

         strlcpy(event + sizeof(EVENT_HEADER), message, sizeof(event) - sizeof(EVENT_HEADER));

         /* setup the event header and send the message */
         bm_compose_event(pevent, EVENTID_MESSAGE, (WORD) message_type, strlen(event + sizeof(EVENT_HEADER)) + 1, 0);
         bm_send_event(_msg_buffer, pevent, pevent->data_size + sizeof(EVENT_HEADER), BM_WAIT);
      }
   }

   /* log message */
   cm_msg_log(message_type, facility, message);

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
static int cm_msg_retrieve1(char *filename, time_t t, INT n_messages, char **messages, int *length, int *allocated,
                            int *num_messages) {
   BOOL stop;
   int fh;
   char *p, str[1000];
   struct stat stat_buf;
   struct tm tms;
   time_t tstamp, tstamp_valid, tstamp_last;

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
      memcpy(&tms, localtime(&now), sizeof(tms));

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
      tstamp = mktime(&tms);
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
   char filename[256], linkname[256];
   INT n, i, flag;
   time_t filedate;
   int length = 0;
   int allocated = 0;

   time(&filedate);
   flag = cm_msg_get_logfile(facility, filedate, filename, sizeof(filename), linkname, sizeof(linkname));

   if (flag < 0) {
      *num_messages = 0;
      return CM_SUCCESS;
   }

   //printf("facility %s, filename \"%s\" \"%s\"\n", facility, filename, linkname);

   // see if file exists, use linkname if not
   if (strlen(linkname) > 0) {
      if (!ss_file_exist(filename))
         strlcpy(filename, linkname, sizeof(filename));
   }

   if (ss_file_exist(filename))
      cm_msg_retrieve1(filename, t, n_message, messages, &length, &allocated, &n);
   else
      n = 0;

   int missing = 0;
   while (n < n_message && flag) {
      filedate -= 3600 * 24;         // go one day back

      int xflag = cm_msg_get_logfile(facility, filedate, filename, sizeof(filename), NULL, 0);

      if ((xflag >= 0) && ss_file_exist(filename)) {
         cm_msg_retrieve1(filename, t, n_message - n, messages, &length, &allocated, &i);
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
   strcpy(str, ss_asctime());

   return CM_SUCCESS;
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
INT _semaphore_msg = -1;

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
         char* str = fgets(buf, sizeof(buf)-1, f);
         if (str == NULL)
            break;
         if (str[0] && str[0] != '#' && str[0] != ' ' && str[0] != '\t'
             && (strchr(str, ' ') || strchr(str, '\t'))) {
            //sscanf(str, "%s %s %s", exptab[i].name, exptab[i].directory, exptab[i].user);
            std::vector<std::string> str_split = split(" ", str);
            if (str_split.size() != 3)
               continue;

            exptab_entry e;
            e.name = str_split[0];
            e.directory = str_split[1];
            e.user = str_split[2];

            /* check for trailing directory separator */
            if (!ends_with_char(e.directory, DIR_SEPARATOR)) {
               e.directory += DIR_SEPARATOR_STR;
            }

            exptab->exptab.push_back(e);
         }
      } while (!feof(f));
      fclose(f);
   }

   cm_msg(MINFO, "cm_read_exptab", "Read exptab \"%s\":", exptab->filename.c_str()); 
   for (unsigned j=0; j<exptab->exptab.size(); j++) {
      cm_msg(MINFO, "cm_read_exptab", "entry %d, experiment \"%s\", directory \"%s\", user \"%s\"", j, exptab->exptab[j].name.c_str(), exptab->exptab[j].directory.c_str(), exptab->exptab[j].name.c_str());
   }

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
#ifdef LOCAL_ROUTINES

   /* only do it if local */
   if (!rpc_is_remote()) {
      INT status;
      HNDLE hKey;
      char str[256];

      if (!pid)
         pid = ss_getpid();

      /* make operation atomic by locking database */
      db_lock_database(hDB);

      sprintf(str, "System/Clients/%0d", pid);
      status = db_find_key1(hDB, 0, str, &hKey);

      if (status == DB_NO_KEY) {
         db_unlock_database(hDB);
         return DB_SUCCESS;
      }

      if (status != DB_SUCCESS) {
         db_unlock_database(hDB);
         return status;
      }

      /* unlock client entry and delete it without locking DB */
      db_set_mode(hDB, hKey, MODE_READ | MODE_WRITE | MODE_DELETE, 2);
      db_delete_key1(hDB, hKey, 1, TRUE);

      db_unlock_database(hDB);

      /* touch notify key to inform others */
      status = 0;
      db_set_value(hDB, 0, "/System/Client Notify", &status, sizeof(status), 1, TID_INT32);
   }
#endif                          /*LOCAL_ROUTINES */

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
      db_create_record(hDB, 0, str, strcomb(program_info_str));

      /* save handle for ODB and client */
      rpc_set_server_option(RPC_ODB_HANDLE, hDB);
      rpc_set_server_option(RPC_CLIENT_HANDLE, hKey);

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
Get info about the current client
@param  *client_name       Client name.
@return   CM_SUCCESS, CM_UNDEF_EXP
*/
INT cm_get_client_info(char *client_name) {
   INT status, length;
   HNDLE hDB, hKey;

   /* get root key of client */
   cm_get_experiment_database(&hDB, &hKey);
   if (!hDB) {
      client_name[0] = 0;
      return CM_UNDEF_EXP;
   }

   status = db_find_key(hDB, hKey, "Name", &hKey);
   if (status != DB_SUCCESS) {
      client_name[0] = 0;
      return status;
   }

   length = NAME_LENGTH;
   status = db_get_data(hDB, hKey, client_name, &length, TID_STRING);
   if (status != DB_SUCCESS) {
      client_name[0] = 0;
      return status;
   }

   return CM_SUCCESS;
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
   char str[256];

   status = cm_connect_experiment1(host_name, exp_name, client_name, func, DEFAULT_ODB_SIZE, DEFAULT_WATCHDOG_TIMEOUT);
   cm_msg_flush_buffer();
   if (status != CM_SUCCESS) {
      cm_get_error(status, str);
      puts(str);
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
   status = cm_set_client_info(hDB, &hKeyClient, local_host_name, client_name1, rpc_get_option(0, RPC_OHW_TYPE),
                               password, watchdog_timeout);

   if (status == CM_WRONG_PASSWORD) {
      if (func == NULL)
         strcpy(str, ss_getpass("Password: "));
      else
         func(str);

      strcpy(password, ss_crypt(str, "mi"));
      status = cm_set_client_info(hDB, &hKeyClient, local_host_name, client_name1, rpc_get_option(0, RPC_OHW_TYPE),
                                  password, watchdog_timeout);
      if (status != CM_SUCCESS) {
         /* disconnect */
         if (rpc_is_remote())
            rpc_server_disconnect();
         cm_disconnect_experiment();

         return status;
      }
   }

   //cm_msg(MERROR, "cm_connect_experiment", "test cm_msg after open ODB");
   //cm_msg_flush_buffer();

   /* tell the rest of MIDAS that ODB is open for business */

   cm_set_experiment_database(hDB, hKeyClient);

   /* save the filename of midas.log */
   {
      const char *facility = "midas";
      char filename[256];
      char linkname[256];
      cm_msg_get_logfile1(facility, 0, filename, sizeof(filename), linkname, sizeof(linkname));
   }


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
      cm_get_path(str, sizeof(str));
      size = sizeof(str);
      db_get_value(hDB, 0, "/Logger/Data dir", str, &size, TID_STRING, TRUE);
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
   _message_print = NULL;

   cm_msg(MINFO, "cm_connect_experiment", "Program %s on host %s started", xclient_name.c_str(), local_host_name);

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
      cm_msg(MERROR, "Cannot connect to \"%s\" port %d, errno %d (%s)", hname, port, errno, strerror(errno));
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

   if (expts.size() > 1) {
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
      *exp_name = expts[0];
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
   _message_print = NULL;

   cm_msg(MINFO, "cm_disconnect_experiment", "Program %s on host %s stopped", client_name.c_str(), local_host_name);

   cm_msg_flush_buffer();

   if (rpc_is_remote()) {
      /* close open records */
      db_close_all_records();

      cm_msg_close_buffer();

      rpc_client_disconnect(-1, FALSE);
      rpc_server_disconnect();

      cm_set_experiment_database(0, 0);
   } else {
      rpc_client_disconnect(-1, FALSE);

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
   }

   if (!rpc_is_mserver())
      rpc_server_shutdown();

   /* free RPC list */
   rpc_deregister_functions();

   //cm_msg(MERROR, "cm_disconnect_experiment", "test cm_msg before deleting the message ring buffer");
   //cm_msg_flush_buffer();

   /* last flush before we delete the message ring buffer */
   cm_msg_flush_buffer();

   /* delete the message ring buffer and semaphore */
   if (_msg_mutex)
      ss_mutex_delete(_msg_mutex);
   _msg_mutex = 0;
   if (_msg_rb)
      rb_delete(_msg_rb);
   _msg_rb = 0;

   //cm_msg(MERROR, "cm_disconnect_experiment", "test cm_msg after deleting message ring buffer");
   //cm_msg_flush_buffer();

   /* free memory buffers */
   if (_event_buffer_size > 0) {
      M_FREE(_event_buffer);
      _event_buffer = NULL;
      _event_buffer_size = 0;
   }

   if (_tcp_buffer != NULL) {
      M_FREE(_tcp_buffer);
      _tcp_buffer = NULL;
   }

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

   if (hDB == 0) {
      rpc_set_server_option(RPC_ODB_HANDLE, 0);
   }

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
   _semaphore_msg = semaphore_msg;

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
      if (hDB != NULL)
         *hDB = _hDB;
      if (hKeyClient != NULL)
         *hKeyClient = _hKeyClient;
   } else {
      if (hDB != NULL)
         *hDB = rpc_get_server_option(RPC_ODB_HANDLE);
      if (hKeyClient != NULL)
         *hKeyClient = rpc_get_server_option(RPC_CLIENT_HANDLE);
   }

   return CM_SUCCESS;
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
   if (semaphore_msg)
      *semaphore_msg = _semaphore_msg;

   return CM_SUCCESS;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

static int bm_validate_client_index(const BUFFER *buf, BOOL abort_if_invalid);

static BUFFER_CLIENT *bm_get_my_client(BUFFER *pbuf, BUFFER_HEADER *pheader);

static INT bm_get_buffer(const char *who, INT buffer_handle, BUFFER **pbuf);

#ifdef LOCAL_ROUTINES
static void bm_lock_buffer(BUFFER *pbuf);
static void bm_unlock_buffer(BUFFER *pbuf);
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
INT cm_set_watchdog_params(BOOL call_watchdog, DWORD timeout)
{
   /* set also local timeout to requested value (needed by cm_enable_watchdog()) */
   _watchdog_timeout = timeout;

   if (rpc_is_remote())
      return rpc_call(RPC_CM_SET_WATCHDOG_PARAMS, call_watchdog, timeout);

#ifdef LOCAL_ROUTINES

   if (rpc_is_mserver()) {
      HNDLE hDB, hKey;

      rpc_set_server_option(RPC_WATCHDOG_TIMEOUT, timeout);

      /* write timeout value to client enty in ODB */
      cm_get_experiment_database(&hDB, &hKey);

      if (hDB) {
         db_set_mode(hDB, hKey, MODE_READ | MODE_WRITE, TRUE);
         db_set_value(hDB, hKey, "Link timeout", &timeout, sizeof(timeout), 1, TID_INT32);
         db_set_mode(hDB, hKey, MODE_READ, TRUE);
      }
   } else {
      _watchdog_timeout = timeout;

      /* set watchdog flag of all open buffers */
      for (int i = _buffer_entries; i > 0; i--) {
         BUFFER *pbuf = &_buffer[i - 1];

         if (!pbuf->attached)
            continue;

         BUFFER_HEADER *pheader = pbuf->buffer_header;
         BUFFER_CLIENT *pclient = bm_get_my_client(pbuf, pheader);

         /* clear entry from client structure in buffer header */
         pclient->watchdog_timeout = timeout;

         /* show activity */
         pclient->last_activity = ss_millitime();
      }

      db_set_watchdog_params(timeout);
   }

#endif                          /* LOCAL_ROUTINES */

   return CM_SUCCESS;
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

INT cm_get_watchdog_info(HNDLE hDB, char *client_name, DWORD *timeout, DWORD *last) {
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
   INT status, i;
   HNDLE hDB, hKey, hKeyTrans;
   KEY key;
   char str[256];

   /* check for valid transition */
   if (transition != TR_START && transition != TR_STOP && transition != TR_PAUSE && transition != TR_RESUME
       && transition != TR_STARTABORT) {
      cm_msg(MERROR, "cm_register_transition", "Invalid transition request \"%d\"", transition);
      return CM_INVALID_TRANSITION;
   }

   cm_get_experiment_database(&hDB, &hKey);

   rpc_register_function(RPC_RC_TRANSITION, rpc_transition_dispatch);

   /* register new transition request */

   /* find empty slot */
   for (i = 0; i < MAX_TRANSITIONS; i++)
      if (!_trans_table[i].transition)
         break;

   if (i == MAX_TRANSITIONS) {
      cm_msg(MERROR, "cm_register_transition",
             "To many transition registrations. Please increase MAX_TRANSITIONS and recompile");
      return CM_TOO_MANY_REQUESTS;
   }

   _trans_table[i].transition = transition;
   _trans_table[i].func = func;
   _trans_table[i].sequence_number = sequence_number;

   for (i = 0;; i++)
      if (trans_name[i].name[0] == 0 || trans_name[i].transition == transition)
         break;

   sprintf(str, "Transition %s", trans_name[i].name);

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
   INT status, i;
   HNDLE hDB, hKey, hKeyTrans;
   char str[256];

   /* check for valid transition */
   if (transition != TR_START && transition != TR_STOP && transition != TR_PAUSE && transition != TR_RESUME) {
      cm_msg(MERROR, "cm_deregister_transition", "Invalid transition request \"%d\"", transition);
      return CM_INVALID_TRANSITION;
   }

   cm_get_experiment_database(&hDB, &hKey);

   /* remove existing transition request */
   for (i = 0; i < MAX_TRANSITIONS; i++)
      if (_trans_table[i].transition == transition)
         break;

   if (i == MAX_TRANSITIONS) {
      cm_msg(MERROR, "cm_register_transition",
             "Cannot de-register transition registration, request not found");
      return CM_INVALID_TRANSITION;
   }

   _trans_table[i].transition = 0;
   _trans_table[i].func = NULL;
   _trans_table[i].sequence_number = 0;

   for (i = 0;; i++)
      if (trans_name[i].name[0] == 0 || trans_name[i].transition == transition)
         break;

   sprintf(str, "Transition %s", trans_name[i].name);

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
   INT status, i;
   HNDLE hDB, hKey;
   char str[256];

   /* check for valid transition */
   if (transition != TR_START && transition != TR_STOP && transition != TR_PAUSE && transition != TR_RESUME) {
      cm_msg(MERROR, "cm_set_transition_sequence", "Invalid transition request \"%d\"", transition);
      return CM_INVALID_TRANSITION;
   }

   cm_get_experiment_database(&hDB, &hKey);

   /* Find the transition type from the list */
   for (i = 0;; i++)
      if (trans_name[i].name[0] == 0 || trans_name[i].transition == transition)
         break;
   sprintf(str, "Transition %s", trans_name[i].name);

   /* Change local sequence number for this transition type */
   for (i = 0; i < MAX_TRANSITIONS; i++)
      if (_trans_table[i].transition == transition) {
         _trans_table[i].sequence_number = sequence_number;
         break;
      }

   /* unlock database */
   db_set_mode(hDB, hKey, MODE_READ | MODE_WRITE, TRUE);

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
   INT status, i, size;
   char tr_key_name[256];
   HNDLE hDB, hKey;

   cm_get_experiment_database(&hDB, &hKey);

   for (i = 0; _deferred_trans_table[i].transition; i++)
      if (_deferred_trans_table[i].transition == transition)
         _deferred_trans_table[i].func = (int (*)(int, char *)) func;

   /* set new transition mask */
   _deferred_transition_mask |= transition;

   for (i = 0;; i++)
      if (trans_name[i].name[0] == 0 || trans_name[i].transition == transition)
         break;

   sprintf(tr_key_name, "Transition %s DEFERRED", trans_name[i].name);

   /* unlock database */
   db_set_mode(hDB, hKey, MODE_READ | MODE_WRITE, TRUE);

   /* set value */
   i = 0;
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

typedef struct tr_client *PTR_CLIENT;

typedef struct tr_client {
   int transition;
   int run_number;
   int async_flag;
   int debug_flag;
   int sequence_number;
   PTR_CLIENT *pred;
   int n_pred;
   char host_name[HOST_NAME_LENGTH];
   char client_name[NAME_LENGTH];
   int port;
   char key_name[NAME_LENGTH]; /* this client key name in /System/Clients */
   int status;
   char errorstr[1024];
   DWORD init_time;    // time when tr_client created
   char waiting_for_client[NAME_LENGTH]; // name of client we are waiting for
   DWORD connect_timeout;
   DWORD connect_start_time; // time when client rpc connection is started
   DWORD connect_end_time;   // time when client rpc connection is finished
   DWORD rpc_timeout;
   DWORD rpc_start_time;     // time client rpc call is started
   DWORD rpc_end_time;       // time client rpc call is finished
   DWORD end_time;           // time client thread is finished
} TR_CLIENT;

static int tr_compare(const void *arg1, const void *arg2) {
   return ((TR_CLIENT *) arg1)->sequence_number - ((TR_CLIENT *) arg2)->sequence_number;
}

/*------------------------------------------------------------------*/

typedef struct {
   int transition;
   int run_number;
   int async_flag;
   int debug_flag;
   int status;
   char errorstr[256];
   DWORD start_time;
   DWORD end_time;
   int num_clients;
   TR_CLIENT *clients;
} TR_STATE;

static TR_STATE *tr_previous_transition = NULL;
static TR_STATE *tr_current_transition = NULL;

/*------------------------------------------------------------------*/

static int tr_finish(HNDLE hDB, int transition, int status, const char *errorstr) {
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

   if (tr_current_transition) {
      tr_current_transition->status = status;
      tr_current_transition->end_time = end_time;
      if (errorstr) {
         strlcpy(tr_current_transition->errorstr, errorstr, sizeof(tr_current_transition->errorstr));
      } else {
         strlcpy(tr_current_transition->errorstr, "(null)", sizeof(tr_current_transition->errorstr));
      }
   }

   return status;
}

/*------------------------------------------------------------------*/

static void write_tr_client_to_odb(HNDLE hDB, const TR_CLIENT *tr_client) {
   //printf("Writing client [%s] to ODB\n", tr_client->client_name);

   int status;
   HNDLE hKey;

   if (tr_client->transition == TR_STARTABORT) {
      status = db_create_key(hDB, 0, "/System/Transition/TR_STARTABORT", TID_KEY);
      status = db_find_key(hDB, 0, "/System/Transition/TR_STARTABORT", &hKey);
      assert(status == DB_SUCCESS);
   } else {
      status = db_create_key(hDB, 0, "/System/Transition/Clients", TID_KEY);
      status = db_find_key(hDB, 0, "/System/Transition/Clients", &hKey);
      assert(status == DB_SUCCESS);
   }

   status = db_create_key(hDB, hKey, tr_client->client_name, TID_KEY);
   status = db_find_key(hDB, hKey, tr_client->client_name, &hKey);
   assert(status == DB_SUCCESS);

   DWORD now = ss_millitime();

   //int   transition;
   //int   run_number;
   //int   async_flag;
   //int   debug_flag;
   status = db_set_value(hDB, hKey, "sequence_number", &tr_client->sequence_number, sizeof(INT), 1, TID_INT32);
   status = db_set_value(hDB, hKey, "client_name", &tr_client->client_name, strlen(tr_client->client_name) + 1, 1,
                         TID_STRING);
   status = db_set_value(hDB, hKey, "host_name", &tr_client->host_name, strlen(tr_client->host_name) + 1, 1,
                         TID_STRING);
   status = db_set_value(hDB, hKey, "port", &tr_client->port, sizeof(INT), 1, TID_INT32);
   status = db_set_value(hDB, hKey, "init_time", &tr_client->init_time, sizeof(DWORD), 1, TID_UINT32);
   status = db_set_value(hDB, hKey, "waiting_for_client", &tr_client->waiting_for_client,
                         strlen(tr_client->waiting_for_client) + 1, 1, TID_STRING);
   status = db_set_value(hDB, hKey, "connect_timeout", &tr_client->connect_timeout, sizeof(DWORD), 1, TID_UINT32);
   status = db_set_value(hDB, hKey, "connect_start_time", &tr_client->connect_start_time, sizeof(DWORD), 1, TID_UINT32);
   status = db_set_value(hDB, hKey, "connect_end_time", &tr_client->connect_end_time, sizeof(DWORD), 1, TID_UINT32);
   status = db_set_value(hDB, hKey, "rpc_timeout", &tr_client->rpc_timeout, sizeof(DWORD), 1, TID_UINT32);
   status = db_set_value(hDB, hKey, "rpc_start_time", &tr_client->rpc_start_time, sizeof(DWORD), 1, TID_UINT32);
   status = db_set_value(hDB, hKey, "rpc_end_time", &tr_client->rpc_end_time, sizeof(DWORD), 1, TID_UINT32);
   status = db_set_value(hDB, hKey, "end_time", &tr_client->end_time, sizeof(DWORD), 1, TID_UINT32);
   status = db_set_value(hDB, hKey, "status", &tr_client->status, sizeof(INT), 1, TID_INT32);
   status = db_set_value(hDB, hKey, "error", &tr_client->errorstr, strlen(tr_client->errorstr) + 1, 1, TID_STRING);
   status = db_set_value(hDB, hKey, "last_updated", &now, sizeof(DWORD), 1, TID_UINT32);
}

/*------------------------------------------------------------------*/

/* Perform a detached transition through the external "mtransition" program */
int
cm_transition_detach(INT transition, INT run_number, char *errstr, INT errstr_size, INT async_flag, INT debug_flag) {
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
int cm_transition_call(void *param) {
   INT old_timeout, status, i, t1, t0, size;
   HNDLE hDB, hConn;
   int connect_timeout = 10000;
   int timeout = 120000;
   TR_CLIENT *tr_client;

   cm_get_experiment_database(&hDB, NULL);
   assert(hDB);

   tr_client = (TR_CLIENT *) param;

   tr_client->errorstr[0] = 0;
   tr_client->init_time = ss_millitime();
   tr_client->waiting_for_client[0] = 0;
   tr_client->connect_timeout = 0;
   tr_client->connect_start_time = 0;
   tr_client->connect_end_time = 0;
   tr_client->rpc_timeout = 0;
   tr_client->rpc_start_time = 0;
   tr_client->rpc_end_time = 0;
   tr_client->end_time = 0;

   write_tr_client_to_odb(hDB, tr_client);

   /* wait for predecessor if set */
   if (tr_client->async_flag & TR_MTHREAD && tr_client->pred) {
      while (1) {
         int wait_for = -1;

         for (i = 0; i < tr_client->n_pred; i++) {
            if (tr_client->pred[i]->status == 0) {
               wait_for = i;
               break;
            }

            if (tr_client->pred[i]->status != SUCCESS && tr_client->transition != TR_STOP) {
               cm_msg(MERROR, "cm_transition_call", "Transition %d aborted: client \"%s\" returned status %d",
                      tr_client->transition, tr_client->pred[i]->client_name, tr_client->pred[i]->status);
               tr_client->status = -1;
               sprintf(tr_client->errorstr, "Aborted by failure of client \"%s\"", tr_client->pred[i]->client_name);
               tr_client->end_time = ss_millitime();
               write_tr_client_to_odb(hDB, tr_client);
               return CM_SUCCESS;
            }
         }

         if (wait_for < 0)
            break;

         strlcpy(tr_client->waiting_for_client, tr_client->pred[wait_for]->client_name,
                 sizeof(tr_client->waiting_for_client));
         write_tr_client_to_odb(hDB, tr_client);

         if (tr_client->debug_flag == 1)
            printf("Client \"%s\" waits for client \"%s\"\n", tr_client->client_name,
                   tr_client->pred[wait_for]->client_name);

         i = 0;
         size = sizeof(i);
         status = db_get_value(hDB, 0, "/Runinfo/Transition in progress", &i, &size, TID_INT32, FALSE);

         if (status == DB_SUCCESS && i == 0) {
            cm_msg(MERROR, "cm_transition_call",
                   "Client \"%s\" transition %d aborted while waiting for client \"%s\": \"/Runinfo/Transition in progress\" was cleared",
                   tr_client->client_name, tr_client->transition, tr_client->pred[wait_for]->client_name);
            tr_client->status = -1;
            sprintf(tr_client->errorstr, "Canceled");
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
      printf("Connecting to client \"%s\" on host %s...\n", tr_client->client_name,
             tr_client->host_name);
   if (tr_client->debug_flag == 2)
      cm_msg(MINFO, "cm_transition_call",
             "cm_transition_call: Connecting to client \"%s\" on host %s...",
             tr_client->client_name, tr_client->host_name);

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
   old_timeout = rpc_get_option(-2, RPC_OTIMEOUT);
   rpc_set_option(-2, RPC_OTIMEOUT, connect_timeout);

   tr_client->connect_timeout = connect_timeout;
   tr_client->connect_start_time = ss_millitime();

   write_tr_client_to_odb(hDB, tr_client);

   /* client found -> connect to its server port */
   status = rpc_client_connect(tr_client->host_name, tr_client->port, tr_client->client_name, &hConn);

   rpc_set_option(-2, RPC_OTIMEOUT, old_timeout);

   tr_client->connect_end_time = ss_millitime();
   write_tr_client_to_odb(hDB, tr_client);

   if (status != RPC_SUCCESS) {
      cm_msg(MERROR, "cm_transition_call",
             "cannot connect to client \"%s\" on host %s, port %d, status %d",
             tr_client->client_name, tr_client->host_name, tr_client->port, status);
      strlcpy(tr_client->errorstr, "Cannot connect to client \'", sizeof(tr_client->errorstr));
      strlcat(tr_client->errorstr, tr_client->client_name, sizeof(tr_client->errorstr));
      strlcat(tr_client->errorstr, "\'", sizeof(tr_client->errorstr));

      /* clients that do not respond to transitions are dead or defective, get rid of them. K.O. */
      cm_shutdown(tr_client->client_name, TRUE);
      cm_cleanup(tr_client->client_name, TRUE);

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
      printf("Connection established to client \"%s\" on host %s\n",
             tr_client->client_name, tr_client->host_name);
   if (tr_client->debug_flag == 2)
      cm_msg(MINFO, "cm_transition_call",
             "cm_transition: Connection established to client \"%s\" on host %s",
             tr_client->client_name, tr_client->host_name);

   /* call RC_TRANSITION on remote client with increased timeout */
   old_timeout = rpc_get_option(hConn, RPC_OTIMEOUT);
   rpc_set_option(hConn, RPC_OTIMEOUT, timeout);

   tr_client->rpc_timeout = timeout;
   tr_client->rpc_start_time = ss_millitime();
   write_tr_client_to_odb(hDB, tr_client);

   if (tr_client->debug_flag == 1)
      printf("Executing RPC transition client \"%s\" on host %s...\n",
             tr_client->client_name, tr_client->host_name);
   if (tr_client->debug_flag == 2)
      cm_msg(MINFO, "cm_transition_call",
             "cm_transition: Executing RPC transition client \"%s\" on host %s...",
             tr_client->client_name, tr_client->host_name);

   t0 = ss_millitime();

   status = rpc_client_call(hConn, RPC_RC_TRANSITION, tr_client->transition, tr_client->run_number, tr_client->errorstr,
                            sizeof(tr_client->errorstr), tr_client->sequence_number);

   t1 = ss_millitime();

   tr_client->rpc_end_time = ss_millitime();

   write_tr_client_to_odb(hDB, tr_client);

   /* fix for clients returning 0 as error code */
   if (status == 0)
      status = FE_ERR_HW;

   /* reset timeout */
   rpc_set_option(hConn, RPC_OTIMEOUT, old_timeout);

   if (tr_client->debug_flag == 1)
      printf("RPC transition finished client \"%s\" on host \"%s\" in %d ms with status %d\n",
             tr_client->client_name, tr_client->host_name, t1 - t0, status);
   if (tr_client->debug_flag == 2)
      cm_msg(MINFO, "cm_transition_call",
             "cm_transition: RPC transition finished client \"%s\" on host \"%s\" in %d ms with status %d",
             tr_client->client_name, tr_client->host_name, t1 - t0, status);

   if (status == RPC_NET_ERROR || status == RPC_TIMEOUT) {
      sprintf(tr_client->errorstr, "RPC network error or timeout from client \'%s\' on host \"%s\"",
              tr_client->client_name, tr_client->host_name);
      /* clients that do not respond to transitions are dead or defective, get rid of them. K.O. */
      cm_shutdown(tr_client->client_name, TRUE);
      cm_cleanup(tr_client->client_name, TRUE);
   } else if (status != CM_SUCCESS && strlen(tr_client->errorstr) < 2) {
      sprintf(tr_client->errorstr, "Unknown error %d from client \'%s\' on host \"%s\"", status, tr_client->client_name,
              tr_client->host_name);
   }

   tr_client->status = status;
   tr_client->end_time = ss_millitime();

   write_tr_client_to_odb(hDB, tr_client);

   return CM_SUCCESS;
}

/*------------------------------------------------------------------*/

int cm_transition_call_direct(TR_CLIENT *tr_client) {
   int i;
   int transition_status = CM_SUCCESS;
   HNDLE hDB;

   cm_get_experiment_database(&hDB, NULL);

   DWORD now = ss_millitime();

   tr_client->errorstr[0] = 0;
   tr_client->init_time = now;
   tr_client->waiting_for_client[0] = 0;
   tr_client->connect_timeout = 0;
   tr_client->connect_start_time = now;
   tr_client->connect_end_time = now;
   tr_client->rpc_timeout = 0;
   tr_client->rpc_start_time = 0;
   tr_client->rpc_end_time = 0;
   tr_client->end_time = 0;

   write_tr_client_to_odb(hDB, tr_client);

   for (i = 0; _trans_table[i].transition; i++)
      if (_trans_table[i].transition == tr_client->transition)
         break;

   /* call registered function */
   if (_trans_table[i].transition == tr_client->transition && _trans_table[i].func) {
      if (tr_client->debug_flag == 1)
         printf("Calling local transition callback\n");
      if (tr_client->debug_flag == 2)
         cm_msg(MINFO, "cm_transition_call_direct", "cm_transition: Calling local transition callback");

      tr_client->rpc_start_time = ss_millitime();

      transition_status = _trans_table[i].func(tr_client->run_number, tr_client->errorstr);

      tr_client->rpc_end_time = ss_millitime();

      if (tr_client->debug_flag == 1)
         printf("Local transition callback finished, status %d\n", transition_status);
      if (tr_client->debug_flag == 2)
         cm_msg(MINFO, "cm_transition_call_direct", "cm_transition: Local transition callback finished, status %d",
                transition_status);
   }

   tr_client->status = transition_status;
   tr_client->end_time = ss_millitime();

   write_tr_client_to_odb(hDB, tr_client);

   return transition_status;
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
INT cm_transition2(INT transition, INT run_number, char *errstr, INT errstr_size, INT async_flag, INT debug_flag) {
   INT i, j, status, idx, size, sequence_number, port, state, n_tr_clients;
   HNDLE hDB, hRootKey, hSubkey, hKey, hKeylocal, hKeyTrans;
   DWORD seconds;
   char host_name[HOST_NAME_LENGTH], client_name[NAME_LENGTH], str[256], tr_key_name[256];
   const char *trname = "unknown";
   KEY key;
   BOOL deferred;
   TR_CLIENT *tr_client;
   char xerrstr[256];

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

   /* delete previous transition state */

   if (tr_previous_transition) {
      TR_STATE *s = tr_previous_transition;
      int n = s->num_clients;
      TR_CLIENT *t = s->clients;

      for (i = 0; i < n; i++)
         if (t[i].pred)
            free(t[i].pred);

      free(s->clients);
      s->clients = NULL;
      free(s);
      tr_previous_transition = NULL;
   }

   if (tr_current_transition) {
      tr_previous_transition = tr_current_transition;
   }

   /* construct new transition state */

   if (1) {
      TR_STATE *s = (TR_STATE *) calloc(1, sizeof(TR_STATE));

      s->transition = transition;
      s->run_number = run_number;
      s->async_flag = async_flag;
      s->debug_flag = debug_flag;
      s->status = 0;
      s->errorstr[0] = 0;
      s->start_time = ss_millitime();
      s->end_time = 0;
      s->num_clients = 0;
      s->clients = NULL;

      tr_current_transition = s;
   }

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

   DWORD start_time = ss_millitime();
   DWORD end_time = 0;

   if (transition != TR_STARTABORT) {
      db_set_value(hDB, 0, "/System/Transition/transition", &transition, sizeof(INT), 1, TID_INT32);
      db_set_value(hDB, 0, "/System/Transition/run_number", &run_number, sizeof(INT), 1, TID_INT32);
      db_set_value(hDB, 0, "/System/Transition/start_time", &start_time, sizeof(DWORD), 1, TID_UINT32);
      db_set_value(hDB, 0, "/System/Transition/end_time", &end_time, sizeof(DWORD), 1, TID_UINT32);
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
         return tr_finish(hDB, transition, AL_TRIGGERED, errstr);
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
                  return tr_finish(hDB, transition, AL_TRIGGERED, errstr);
               }
            }
         }
      }
   }

   /* do detached transition via mtransition tool */
   if (async_flag & TR_DETACH) {
      status = cm_transition_detach(transition, run_number, errstr, errstr_size, async_flag, debug_flag);
      return tr_finish(hDB, transition, status, errstr);
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
      tr_current_transition->run_number = run_number;

      if (transition != TR_STARTABORT) {
         db_set_value(hDB, 0, "/System/Transition/run_number", &run_number, sizeof(INT), 1, TID_INT32);
      }
   }

   if (run_number <= 0) {
      cm_msg(MERROR, "cm_transition", "aborting on attempt to use invalid run number %d", run_number);
      abort();
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
         return tr_finish(hDB, transition, CM_TRANSITION_IN_PROGRESS, "Transition already in progress, see messages");
      }
   }

   /* indicate transition in progress */
   i = transition;
   db_set_value(hDB, 0, "/Runinfo/Transition in progress", &i, sizeof(INT), 1, TID_INT32);

   /* clear run abort flag */
   i = 0;
   db_set_value(hDB, 0, "/Runinfo/Start abort", &i, sizeof(INT), 1, TID_INT32);

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
         return tr_finish(hDB, transition, status, errstr);
      }

      /* check if deferred transition already in progress */
      size = sizeof(i);
      db_get_value(hDB, 0, "/Runinfo/Requested transition", &i, &size, TID_INT32, TRUE);
      if (i) {
         if (errstr) {
            strlcpy(errstr, "Deferred transition already in progress", errstr_size);
            strlcat(errstr, ", to cancel, set \"/Runinfo/Requested transition\" to zero", errstr_size);
         }
         return tr_finish(hDB, transition, CM_TRANSITION_IN_PROGRESS, errstr);
      }

      for (i = 0; trans_name[i].name[0] != 0; i++)
         if (trans_name[i].transition == transition) {
            trname = trans_name[i].name;
            break;
         }

      sprintf(tr_key_name, "Transition %s DEFERRED", trname);

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
                  printf("---- Transition %s deferred by client \"%s\" ----\n", trname, str);
               if (debug_flag == 2)
                  cm_msg(MINFO, "cm_transition", "cm_transition: ---- Transition %s deferred by client \"%s\" ----",
                         trname, str);

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
                  sprintf(errstr, "Transition %s deferred by client \"%s\"", trname, str);

               return tr_finish(hDB, transition, CM_DEFERRED_TRANSITION, errstr);
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
      return tr_finish(hDB, transition, status, errstr);
   }

   for (i = 0; trans_name[i].name[0] != 0; i++)
      if (trans_name[i].transition == transition) {
         trname = trans_name[i].name;
         break;
      }

   /* check that all transition clients are alive */
   for (i = 0;;) {
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
      printf("---- Transition %s started ----\n", trname);
   if (debug_flag == 2)
      cm_msg(MINFO, "cm_transition", "cm_transition: ---- Transition %s started ----", trname);

   sprintf(tr_key_name, "Transition %s", trname);

   /* search database for clients which registered for transition */
   n_tr_clients = 0;
   tr_client = NULL;

   for (i = 0, status = 0;; i++) {
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

            for (j = 0; j < key.num_values; j++) {
               size = sizeof(sequence_number);
               status = db_get_data_index(hDB, hKeyTrans, &sequence_number, &size, j, TID_INT32);
               assert(status == DB_SUCCESS);

               if (tr_client == NULL)
                  tr_client = (TR_CLIENT *) malloc(sizeof(TR_CLIENT));
               else
                  tr_client = (TR_CLIENT *) realloc(tr_client, sizeof(TR_CLIENT) * (n_tr_clients + 1));
               assert(tr_client);

               TR_CLIENT *c = &tr_client[n_tr_clients];

               memset(c, 0, sizeof(TR_CLIENT));

               c->transition = transition;
               c->run_number = run_number;
               c->async_flag = async_flag;
               c->debug_flag = debug_flag;
               c->sequence_number = sequence_number;
               c->status = 0;
               c->n_pred = 0;
               c->pred = NULL;
               strlcpy(c->key_name, subkey.name, sizeof(c->key_name));

               /* get client info */
               size = sizeof(client_name);
               db_get_value(hDB, hSubkey, "Name", client_name, &size, TID_STRING, TRUE);
               strlcpy(c->client_name, client_name, sizeof(c->client_name));

               size = sizeof(host_name);
               db_get_value(hDB, hSubkey, "Host", host_name, &size, TID_STRING, TRUE);
               strlcpy(c->host_name, host_name, sizeof(c->host_name));

               if (hSubkey == hKeylocal && ((async_flag & TR_MTHREAD) == 0)) {
                  /* remember own client */
                  c->port = 0;
               } else {
                  size = sizeof(port);
                  db_get_value(hDB, hSubkey, "Server Port", &port, &size, TID_INT32, TRUE);
                  c->port = port;
               }

               n_tr_clients++;
            }
         }
      }
   }

   /* sort clients according to sequence number */
   if (n_tr_clients > 1)
      qsort(tr_client, n_tr_clients, sizeof(TR_CLIENT), tr_compare);

   /* set predecessor for multi-threaded transitions */
   for (idx = 0; idx < n_tr_clients; idx++) {
      if (tr_client[idx].sequence_number == 0)
         tr_client[idx].n_pred = 0; // sequence number 0 means "don't care"
      else {
         /* find clients with smaller sequence number */
         tr_client[idx].n_pred = 0;
         for (i = idx - 1; i >= 0; i--) {
            if (tr_client[i].sequence_number < tr_client[idx].sequence_number) {
               if (i >= 0 && tr_client[i].sequence_number > 0) {
                  if (tr_client[idx].n_pred == 0)
                     tr_client[idx].pred = (PTR_CLIENT *) malloc(sizeof(PTR_CLIENT));
                  else
                     tr_client[idx].pred = (PTR_CLIENT *) realloc(tr_client[idx].pred,
                                                                  (tr_client[idx].n_pred + 1) * sizeof(PTR_CLIENT));
                  tr_client[idx].pred[tr_client[idx].n_pred] = &tr_client[i];
                  tr_client[idx].n_pred++;
               }
            }
         }
      }
   }

   /* construction of tr_client is complete, export it to outside watchers */

   tr_current_transition->num_clients = n_tr_clients;
   tr_current_transition->clients = tr_client;

   for (idx = 0; idx < n_tr_clients; idx++) {
      write_tr_client_to_odb(hDB, &tr_client[idx]);
   }

   /* contact ordered clients for transition -----------------------*/
   status = CM_SUCCESS;
   for (idx = 0; idx < n_tr_clients; idx++) {
      if (debug_flag == 1)
         printf("\n==== Found client \"%s\" with sequence number %d\n",
                tr_client[idx].client_name, tr_client[idx].sequence_number);
      if (debug_flag == 2)
         cm_msg(MINFO, "cm_transition",
                "cm_transition: ==== Found client \"%s\" with sequence number %d",
                tr_client[idx].client_name, tr_client[idx].sequence_number);

      if (async_flag & TR_MTHREAD) {
         status = CM_SUCCESS;
         ss_thread_create(cm_transition_call, &tr_client[idx]);
      } else {
         if (tr_client[idx].port == 0) {
            /* if own client call transition callback directly */
            status = cm_transition_call_direct(&tr_client[idx]);
         } else {
            /* if other client call transition via RPC layer */
            status = cm_transition_call(&tr_client[idx]);
         }

         if (status == CM_SUCCESS && transition != TR_STOP)
            if (tr_client[idx].status != SUCCESS) {
               cm_msg(MERROR, "cm_transition", "transition %s aborted: client \"%s\" returned status %d", trname,
                      tr_client[idx].client_name, tr_client[idx].status);
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

         for (idx = 0; idx < tr_current_transition->num_clients; idx++)
            if (tr_current_transition->clients[idx].status == 0) {
               all_done = 0;
               break;
            }

         if (all_done)
            break;

         i = 0;
         size = sizeof(i);
         status = db_get_value(hDB, 0, "/Runinfo/Transition in progress", &i, &size, TID_INT32, FALSE);

         if (status == DB_SUCCESS && i == 0) {
            cm_msg(MERROR, "cm_transition", "transition %s aborted: \"/Runinfo/Transition in progress\" was cleared",
                   trname);

            if (errstr != NULL)
               strlcpy(errstr, "Canceled", errstr_size);

            return tr_finish(hDB, transition, CM_TRANSITION_CANCELED, "Canceled");
         }

         ss_sleep(100);
      }
   }

   /* search for any error */
   for (idx = 0; idx < n_tr_clients; idx++)
      if (tr_client[idx].status != CM_SUCCESS) {
         status = tr_client[idx].status;
         if (errstr)
            strlcpy(errstr, tr_client[idx].errorstr, errstr_size);
         strlcpy(tr_current_transition->errorstr, "Aborted by client \"", sizeof(tr_current_transition->errorstr));
         strlcat(tr_current_transition->errorstr, tr_client[idx].client_name, sizeof(tr_current_transition->errorstr));
         strlcat(tr_current_transition->errorstr, "\"", sizeof(tr_current_transition->errorstr));
         break;
      }

   if (transition != TR_STOP && status != CM_SUCCESS) {
      /* indicate abort */
      i = 1;
      db_set_value(hDB, 0, "/Runinfo/Start abort", &i, sizeof(INT), 1, TID_INT32);
      i = 0;
      db_set_value(hDB, 0, "/Runinfo/Transition in progress", &i, sizeof(INT), 1, TID_INT32);

      return tr_finish(hDB, transition, status, errstr);
   }

   if (debug_flag == 1)
      printf("\n---- Transition %s finished ----\n", trname);
   if (debug_flag == 2)
      cm_msg(MINFO, "cm_transition", "cm_transition: ---- Transition %s finished ----", trname);

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

   return tr_finish(hDB, transition, CM_SUCCESS, "Success");
}

/*------------------------------------------------------------------*/

/* wrapper around cm_transition2() to send a TR_STARTABORT in case of failure */
INT cm_transition1(INT transition, INT run_number, char *errstr, INT errstr_size, INT async_flag, INT debug_flag) {
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

INT tr_main_thread(void *param) {
   INT status;
   TR_PARAM *trp;

   trp = (TR_PARAM *) param;
   status = cm_transition1(trp->transition, trp->run_number, trp->errstr, trp->errstr_size, trp->async_flag,
                           trp->debug_flag);

   trp->status = status;
   trp->finished = TRUE;
   return 0;
}

/* wrapper around cm_transition1() for detached multi-threaded transitions */
INT cm_transition(INT transition, INT run_number, char *errstr, INT errstr_size, INT async_flag, INT debug_flag) {
   int mflag = async_flag & TR_MTHREAD;
   int sflag = async_flag & TR_SYNC;

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

      ss_thread_create(tr_main_thread, &_trp);
      if (sflag) {

         /* wait until main thread has finished */
         do {
            ss_sleep(10);
         } while (!_trp.finished);

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
      printf("Received 2nd break. Hard abort.\n");
      exit(0);
   }
   printf("Received break. Aborting...\n");
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
               char str[256];
               db_sprintf(str, buf, subkey.item_size, 0, subkey.type);
               command += str;
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
   static DWORD alarm_last_checked = 0;
   DWORD now = ss_time();

   DWORD now_millitime = ss_millitime();
   static DWORD last_millitime = 0;
   DWORD tdiff_millitime = now_millitime - last_millitime;
   if (last_millitime == 0) {
      last_millitime = now_millitime;
      tdiff_millitime = 0;
   }

   //if (now_millitime < last_millitime) {
   //   printf("millitime wraparound 0x%08x -> 0x%08x\n", last_millitime, now_millitime);
   //}

   /* check alarms once every 10 seconds */
   if (now - alarm_last_checked > 10) {
      al_check();
      alarm_last_checked = now;
   }

   /* run periodic checks previously done by cm_watchdog */

   if (tdiff_millitime > 1000) {
      BOOL wrong_interval = FALSE;
      if (tdiff_millitime > 60000)
         wrong_interval = TRUE;
      // printf("millitime %u, diff %u, wrong_interval %d\n", now_millitime, tdiff_millitime, wrong_interval);

      bm_cleanup("cm_periodic_tasks", now_millitime, wrong_interval);
      db_cleanup("cm_periodic_tasks", now_millitime, wrong_interval);

      bm_write_statistics_to_odb();

      last_millitime = now_millitime;
   }

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

static int _bm_mutex_timeout = 10000;
static int _bm_lock_timeout = 5 * 60 * 1000;

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
   if ((pevent->event_id & 0xF000) == EVENTID_FRAG1 || (pevent->event_id & 0xF000) == EVENTID_FRAG)
      /* fragmented event */
      return ((event_id == EVENTID_ALL || event_id == (pevent->event_id & 0x0FFF))
              && (trigger_mask == TRIGGER_ALL || (trigger_mask & pevent->trigger_mask)));

   return ((event_id == EVENTID_ALL || event_id == pevent->event_id)
           && (trigger_mask == TRIGGER_ALL || (trigger_mask & pevent->trigger_mask)));
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
static void bm_cleanup_buffer_locked(int i, const char *who, DWORD actual_time) {
   BUFFER_HEADER *pheader;
   BUFFER_CLIENT *pbclient;
   int j;

   pheader = _buffer[i].buffer_header;
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
   int i;
   for (i = 0; i < _buffer_entries; i++) {
      if (_buffer[i].attached) {
         BUFFER_HEADER *pheader = _buffer[i].buffer_header;
         int j;
         for (j = 0; j < pheader->max_client_index; j++) {
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
   int i;

   //printf("bm_cleanup: called by %s, actual_time %d, wrong_interval %d\n", who, actual_time, wrong_interval);

   /* check buffers */
   for (i = 0; i < _buffer_entries; i++)
      if (_buffer[i].attached) {
         /* update the last_activity entry to show that we are alive */

         BUFFER *pbuf;

         bm_get_buffer("bm_cleanup", i + 1, &pbuf);

         bm_lock_buffer(pbuf);

         BUFFER_HEADER *pheader = pbuf->buffer_header;
         BUFFER_CLIENT *pclient = bm_get_my_client(pbuf, pheader);
         pclient->last_activity = actual_time;

         /* don't check other clients if interval is strange */
         if (!wrong_interval)
            bm_cleanup_buffer_locked(i, who, actual_time);

         bm_unlock_buffer(pbuf);
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

static int bm_incr_rp_no_check(const BUFFER_HEADER *pheader, int rp, int total_size) {
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
         get_all |= xget_all;
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

static void bm_write_buffer_statistics_to_odb(HNDLE hDB, BUFFER *pbuf, BOOL force) {
   //printf("bm_buffer_write_statistics_to_odb: buffer [%s] client [%s], lock count %d -> %d, force %d\n", pbuf->buffer_name, pbuf->client_name, pbuf->last_count_lock, pbuf->count_lock, force);

   if (!force)
      if (pbuf->count_lock == pbuf->last_count_lock)
         return;

   HNDLE hKey, hKeyBuffer, hKeyClient;
   int status;

   DWORD now = ss_millitime();

   status = db_find_key(hDB, 0, "/System/Buffers", &hKey);
   if (status != DB_SUCCESS) {
      db_create_key(hDB, 0, "/System/Buffers", TID_KEY);
      status = db_find_key(hDB, 0, "/System/Buffers", &hKey);
      if (status != DB_SUCCESS)
         return;
   }

   status = db_find_key(hDB, hKey, pbuf->buffer_name, &hKeyBuffer);
   if (status != DB_SUCCESS) {
      db_create_key(hDB, hKey, pbuf->buffer_name, TID_KEY);
      status = db_find_key(hDB, hKey, pbuf->buffer_name, &hKeyBuffer);
      if (status != DB_SUCCESS)
         return;
   }

   double buf_size = 0;
   double buf_rptr = 0;
   double buf_wptr = 0;
   double buf_fill = 0;
   double buf_cptr = 0;
   double buf_cused = 0;
   double buf_cused_pct = 0;

   if (pbuf->attached && pbuf->buffer_header) {
      buf_size = pbuf->buffer_header->size;
      buf_rptr = pbuf->buffer_header->read_pointer;
      buf_wptr = pbuf->buffer_header->write_pointer;
      if (pbuf->client_index >= 0 && pbuf->client_index <= pbuf->buffer_header->max_client_index) {
         buf_cptr = pbuf->buffer_header->client[pbuf->client_index].read_pointer;

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
   }

   status = db_find_key(hDB, hKeyBuffer, "Clients", &hKey);
   if (status != DB_SUCCESS) {
      db_create_key(hDB, hKeyBuffer, "Clients", TID_KEY);
      status = db_find_key(hDB, hKeyBuffer, "Clients", &hKey);
      if (status != DB_SUCCESS)
         return;
   }

   status = db_find_key(hDB, hKey, pbuf->client_name, &hKeyClient);
   if (status != DB_SUCCESS) {
      db_create_key(hDB, hKey, pbuf->client_name, TID_KEY);
      status = db_find_key(hDB, hKey, pbuf->client_name, &hKeyClient);
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

   if (pbuf->attached && pbuf->buffer_header) {
      int i;
      for (i = 0; i < MAX_CLIENTS; i++) {
         if (!pbuf->client_count_write_wait[i])
            continue;

         if (pbuf->buffer_header->client[i].pid == 0)
            continue;

         if (pbuf->buffer_header->client[i].name[0] == 0)
            continue;

         char str[100 + NAME_LENGTH];

         sprintf(str, "writes_blocked_by/%s/count_write_wait", pbuf->buffer_header->client[i].name);
         db_set_value(hDB, hKeyClient, str, &pbuf->client_count_write_wait[i], sizeof(int), 1, TID_INT32);

         sprintf(str, "writes_blocked_by/%s/time_write_wait", pbuf->buffer_header->client[i].name);
         db_set_value(hDB, hKeyClient, str, &pbuf->client_time_write_wait[i], sizeof(DWORD), 1, TID_UINT32);
      }
   }

   db_set_value(hDB, hKeyBuffer, "Last updated", &now, sizeof(DWORD), 1, TID_UINT32);
   db_set_value(hDB, hKeyClient, "last_updated", &now, sizeof(DWORD), 1, TID_UINT32);

   pbuf->last_count_lock = pbuf->count_lock;
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
      INT i, handle, size;
      BOOL shm_created;
      HNDLE shm_handle;
      size_t shm_size;
      BUFFER_HEADER *pheader;
      HNDLE hDB, odb_key;
      char odb_path[256];
      void *p;
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

      status = cm_get_experiment_database(&hDB, &odb_key);

      if (status != SUCCESS || hDB == 0) {
         //cm_msg(MERROR, "bm_open_buffer", "cannot open buffer \'%s\' - not connected to ODB", buffer_name);
         return BM_NO_SHM;
      }

      /* get buffer size from ODB, user parameter as default if not present in ODB */
      strlcpy(odb_path, "/Experiment/Buffer sizes/", sizeof(odb_path));
      strlcat(odb_path, buffer_name, sizeof(odb_path));

      size = sizeof(INT);
      status = db_get_value(hDB, 0, odb_path, &buffer_size, &size, TID_UINT32, TRUE);

      if (buffer_size <= 0 || buffer_size > max_buffer_size) {
         cm_msg(MERROR, "bm_open_buffer",
                "Cannot open buffer \"%s\", invalid buffer size %d in ODB \"%s\", maximum buffer size is %d",
                buffer_name, buffer_size, odb_path, max_buffer_size);
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

      /* allocate new space for the new buffer descriptor */
      if (_buffer_entries == 0) {
         _buffer = (BUFFER *) M_MALLOC(sizeof(BUFFER));
         memset(_buffer, 0, sizeof(BUFFER));
         if (_buffer == NULL) {
            *buffer_handle = 0;
            return BM_NO_MEMORY;
         }

         _buffer_entries = 1;
         i = 0;
      } else {
         /* check if buffer alreay is open */
         for (i = 0; i < _buffer_entries; i++)
            if (_buffer[i].attached && equal_ustring(_buffer[i].buffer_header->name, buffer_name)) {
               *buffer_handle = i + 1;
               return BM_SUCCESS;
            }

         /* check for a deleted entry */
         for (i = 0; i < _buffer_entries; i++)
            if (!_buffer[i].attached)
               break;

         /* if not found, create new one */
         if (i == _buffer_entries) {
            _buffer = (BUFFER *) realloc(_buffer, sizeof(BUFFER) * (_buffer_entries + 1));
            memset(&_buffer[_buffer_entries], 0, sizeof(BUFFER));

            _buffer_entries++;
            if (_buffer == NULL) {
               _buffer_entries--;
               *buffer_handle = 0;
               return BM_NO_MEMORY;
            }
         }
      }

      handle = i;

      /* open shared memory region */
      status = ss_shm_open(buffer_name, sizeof(BUFFER_HEADER) + buffer_size, &p, &shm_size, &shm_handle, FALSE);
      _buffer[handle].buffer_header = (BUFFER_HEADER *) p;

      if (status != SS_SUCCESS && status != SS_CREATED) {
         *buffer_handle = 0;
         _buffer_entries--;
         return BM_NO_SHM;
      }

      pheader = _buffer[handle].buffer_header;

      shm_created = (status == SS_CREATED);

      if (shm_created) {
         /* setup header info if buffer was created */
         memset(pheader, 0, sizeof(BUFFER_HEADER) + buffer_size);

         strcpy(pheader->name, buffer_name);
         pheader->size = buffer_size;
      } else {
         if (!equal_ustring(pheader->name, buffer_name)) {
            cm_msg(MERROR, "bm_open_buffer",
                   "Buffer \"%s\" is corrupted, mismatch of buffer name in shared memory \"%s\"", buffer_name,
                   pheader->name);
            *buffer_handle = 0;
            _buffer_entries--;
            return BM_CORRUPTED;
         }

         if ((pheader->num_clients < 0) || (pheader->num_clients > MAX_CLIENTS)) {
            cm_msg(MERROR, "bm_open_buffer", "Buffer \"%s\" is corrupted, num_clients %d exceeds MAX_CLIENTS %d",
                   buffer_name, pheader->num_clients, MAX_CLIENTS);
            *buffer_handle = 0;
            _buffer_entries--;
            return BM_CORRUPTED;
         }

         if ((pheader->max_client_index < 0) || (pheader->max_client_index > MAX_CLIENTS)) {
            cm_msg(MERROR, "bm_open_buffer", "Buffer \"%s\" is corrupted, max_client_index %d exceeds MAX_CLIENTS %d",
                   buffer_name, pheader->max_client_index, MAX_CLIENTS);
            *buffer_handle = 0;
            _buffer_entries--;
            return BM_CORRUPTED;
         }

         /* check if buffer size is identical */
         if (pheader->size != buffer_size) {
            cm_msg(MINFO, "bm_open_buffer", "Buffer \"%s\" requested size %d differs from existing size %d",
                   buffer_name, buffer_size, pheader->size);

            buffer_size = pheader->size;

            ss_shm_close(buffer_name, p, shm_size, shm_handle, FALSE);

            status = ss_shm_open(buffer_name, sizeof(BUFFER_HEADER) + buffer_size, &p, &shm_size, &shm_handle, FALSE);
            _buffer[handle].buffer_header = (BUFFER_HEADER *) p;

            if (status != SS_SUCCESS) {
               *buffer_handle = 0;
               return BM_NO_SHM;
            }

            pheader = _buffer[handle].buffer_header;
         }
      }

      /* create semaphore for the buffer */
      status = ss_semaphore_create(buffer_name, &(_buffer[handle].semaphore));
      if (status != SS_CREATED && status != SS_SUCCESS) {
         *buffer_handle = 0;
         _buffer_entries--;
         return BM_NO_SEMAPHORE;
      }

      /* create mutex for the buffer */
      ss_mutex_create(&_buffer[handle].buffer_mutex, FALSE);

      /* first lock buffer */
      BUFFER *pbuf = &_buffer[handle];

      bm_lock_buffer(pbuf);

      bm_cleanup_buffer_locked(handle, "bm_open_buffer", ss_millitime());

      status = bm_validate_buffer_locked(pbuf);
      if (status != BM_SUCCESS) {
         cm_msg(MERROR, "bm_open_buffer",
                "buffer \'%s\' is corrupted, bm_validate_buffer() status %d, calling bm_reset_buffer()...", buffer_name,
                status);
         bm_reset_buffer_locked(pbuf);
         cm_msg(MINFO, "bm_open_buffer", "buffer \'%s\' was reset, all buffered events were lost", buffer_name);
      }

      /*
         Now we have a BUFFER_HEADER, so let's setup a CLIENT
         structure in that buffer. The information there can also
         be seen by other processes.
       */

      for (i = 0; i < MAX_CLIENTS; i++)
         if (pheader->client[i].pid == 0)
            break;

      if (i == MAX_CLIENTS) {
         bm_unlock_buffer(pbuf);
         *buffer_handle = 0;
         cm_msg(MERROR, "bm_open_buffer", "buffer \'%s\' maximum number of clients exceeded", buffer_name);
         return BM_NO_SLOT;
      }

      /* get our client name previously set by bm_set_name */

      char client_name[NAME_LENGTH];

      cm_get_client_info(client_name);
      if (client_name[0] == 0)
         strcpy(client_name, "unknown");

      /* store slot index in _buffer structure */
      pbuf->client_index = i;

      /* store client name */
      strlcpy(pbuf->client_name, client_name, sizeof(pbuf->client_name));

      /* store buffer name */
      strlcpy(pbuf->buffer_name, buffer_name, sizeof(pbuf->buffer_name));

      /*
         Save the index of the last client of that buffer so that later only
         the clients 0..max_client_index-1 have to be searched through.
       */
      pheader->num_clients++;
      if (i + 1 > pheader->max_client_index)
         pheader->max_client_index = i + 1;

      /* setup buffer header and client structure */
      BUFFER_CLIENT *pclient = &pheader->client[i];

      memset(pclient, 0, sizeof(BUFFER_CLIENT));

      /* use client name previously set by bm_set_name */
      strlcpy(pclient->name, client_name, sizeof(pclient->name));

      pclient->pid = ss_getpid();

      ss_suspend_get_buffer_port(ss_gettid(), &pclient->port);

      pclient->read_pointer = pheader->write_pointer;
      pclient->last_activity = ss_millitime();

      cm_get_watchdog_params(NULL, &pclient->watchdog_timeout);

      bm_unlock_buffer(pbuf);

      /* setup _buffer entry */
      pbuf->attached = TRUE;
      pbuf->shm_handle = shm_handle;
      pbuf->shm_size = shm_size;
      pbuf->callback = FALSE;
      ss_mutex_create(&pbuf->write_cache_mutex, FALSE);
      ss_mutex_create(&pbuf->read_cache_mutex, FALSE);

      bm_clear_buffer_statistics(hDB, pbuf);

      *buffer_handle = (handle + 1);

      /* initialize buffer counters */
      bm_init_buffer_counters(handle + 1);

      bm_cleanup("bm_open_buffer", ss_millitime(), FALSE);

      if (shm_created)
         return BM_CREATED;
   }
#endif                          /* LOCAL_ROUTINES */

   return BM_SUCCESS;
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
      if (buffer_handle > _buffer_entries || buffer_handle <= 0) {
         // silently fail, maybe this buffer was already closed.
         //cm_msg(MERROR, who, "invalid buffer handle %d: out of range, _buffer_entries is %d", buffer_handle, _buffer_entries);
         return BM_INVALID_HANDLE;
      }

      if (!_buffer[buffer_handle - 1].attached) {
         // silently fail, maybe this buffer was already closed.
         //cm_msg(MERROR, who, "invalid buffer handle %d: not attached", buffer_handle);
         return BM_INVALID_HANDLE;
      }

      BUFFER *pbuf = &_buffer[buffer_handle - 1];
      BUFFER_HEADER *pheader = pbuf->buffer_header;

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

      /* first lock buffer */
      bm_lock_buffer(pbuf);

      /* mark entry in _buffer as empty */
      pbuf->attached = FALSE;

      int idx = bm_validate_client_index(&_buffer[buffer_handle - 1], FALSE);

      if (idx >= 0) {
         /* clear entry from client structure in buffer header */
         memset(&(pheader->client[idx]), 0, sizeof(BUFFER_CLIENT));
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

      /* free cache */
      if (pbuf->read_cache_size > 0) {
         M_FREE(pbuf->read_cache);
         pbuf->read_cache = NULL;
         pbuf->read_cache_size = 0;
         pbuf->read_cache_rp = 0;
         pbuf->read_cache_wp = 0;
      }

      if (pbuf->write_cache_size > 0) {
         M_FREE(pbuf->write_cache);
         pbuf->write_cache = NULL;
         pbuf->write_cache_size = 0;
         pbuf->write_cache_wp = 0;
      }

      if (pbuf->read_cache_mutex) {
         ss_mutex_delete(pbuf->read_cache_mutex);
         pbuf->read_cache_mutex = NULL;
      }

      if (pbuf->write_cache_mutex) {
         ss_mutex_delete(pbuf->write_cache_mutex);
         pbuf->write_cache_mutex = NULL;
      }

      /* check if anyone is waiting and wake him up */
      BUFFER_CLIENT *pclient = pheader->client;

      for (i = 0; i < pheader->max_client_index; i++, pclient++)
         if (pclient->pid && (pclient->write_wait || pclient->read_wait))
            ss_resume(pclient->port, "B  ");

      char xname[256];
      strlcpy(xname, pheader->name, sizeof(xname));

      pheader = NULL; // after ss_shm_close(), pheader points nowhere

      /* unmap shared memory, delete it if we are the last */
      ss_shm_close(xname, _buffer[buffer_handle - 1].buffer_header, _buffer[buffer_handle - 1].shm_size,
                   _buffer[buffer_handle - 1].shm_handle, destroy_flag);

      /* unlock buffer */
      bm_unlock_buffer(pbuf);

      /* delete semaphore */
      ss_semaphore_delete(pbuf->semaphore, destroy_flag);

      /* delete mutex */
      if (pbuf->buffer_mutex) {
         ss_mutex_delete(pbuf->buffer_mutex);
         pbuf->buffer_mutex = NULL;
      }

      /* update _buffer_entries */
      if (buffer_handle == _buffer_entries)
         _buffer_entries--;

      if (_buffer_entries > 0)
         _buffer = (BUFFER *) realloc(_buffer, sizeof(BUFFER) * (_buffer_entries));
      else {
         M_FREE(_buffer);
         _buffer = NULL;
      }
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
      INT i;

      cm_msg_close_buffer();

      for (i = _buffer_entries; i > 0; i--)
         bm_close_buffer(i);
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
      assert(status == DB_SUCCESS);

      int i;
      for (i = 0; i < _buffer_entries; i++) {
         bm_write_buffer_statistics_to_odb(hDB, &_buffer[i], FALSE);
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

static BOOL _watchdog_thread_run = FALSE; // set by main process
static int _watchdog_thread_pid = 0; // set by watchdog thread

/********************************************************************/
/**
Watchdog thread to maintain the watchdog timeout timestamps for this client
*/
INT cm_watchdog_thread(void *unused) {
   _watchdog_thread_pid = ss_getpid();
   //printf("cm_watchdog_thread started, pid %d!\n", _watchdog_thread_pid);
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
   _watchdog_thread_pid = 0;
   return 0;
}

#endif

INT cm_start_watchdog_thread() {
   /* watchdog does not run inside remote clients.
    * watchdog timeout timers are maintained by the mserver */
   if (rpc_is_remote())
      return CM_SUCCESS;
#ifdef LOCAL_ROUTINES
   /* only start once */
   if (_watchdog_thread_run)
      return CM_SUCCESS;
   if (_watchdog_thread_pid)
      return CM_SUCCESS;
   _watchdog_thread_run = TRUE;
   ss_thread_create(cm_watchdog_thread, NULL);
#endif
   return CM_SUCCESS;
}

INT cm_stop_watchdog_thread() {
   /* watchdog does not run inside remote clients.
    * watchdog timeout timers are maintained by the mserver */
   if (rpc_is_remote())
      return CM_SUCCESS;
#ifdef LOCAL_ROUTINES
   _watchdog_thread_run = FALSE;
   while (_watchdog_thread_pid) {
      //printf("waiting for pid %d\n", _watchdog_thread_pid);
      ss_sleep(10);
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
            } while (status == DB_SUCCESS && (ss_millitime() - start_time < 5000));

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
      INT i, j;
      char str[256];
      DWORD interval;
      DWORD now = ss_millitime();

      /* check buffers */
      for (i = 0; i < _buffer_entries; i++)
         if (_buffer[i].attached) {
            /* update the last_activity entry to show that we are alive */
            BUFFER *pbuf = &_buffer[i];
            BUFFER_HEADER *pheader = pbuf->buffer_header;
            BUFFER_CLIENT *pbclient = pheader->client;
            int idx = bm_validate_client_index(&_buffer[i], FALSE);
            if (idx >= 0)
               pbclient[idx].last_activity = ss_millitime();

            /* now check other clients */
            for (j = 0; j < pheader->max_client_index; j++, pbclient++)
               if (j != _buffer[i].client_index && pbclient->pid &&
                   (client_name == NULL || client_name[0] == 0
                    || strncmp(pbclient->name, client_name, strlen(client_name)) == 0)) {
                  if (ignore_timeout)
                     interval = 2 * WATCHDOG_INTERVAL;
                  else
                     interval = pbclient->watchdog_timeout;

                  /* If client process has no activity, clear its buffer entry. */
                  if (interval > 0
                      && now > pbclient->last_activity && now - pbclient->last_activity > interval) {

                     bm_lock_buffer(pbuf);

                     str[0] = 0;

                     /* now make again the check with the buffer locked */
                     if (interval > 0
                         && now > pbclient->last_activity && now - pbclient->last_activity > interval) {
                        sprintf(str,
                                "Client \'%s\' on \'%s\' removed by cm_cleanup (idle %1.1lfs, timeout %1.0lfs)",
                                pbclient->name, pheader->name,
                                (ss_millitime() - pbclient->last_activity) / 1000.0,
                                interval / 1000.0);

                        bm_remove_client_locked(pheader, j);
                     }

                     bm_unlock_buffer(pbuf);

                     /* display info message after unlocking buffer */
                     if (str[0])
                        cm_msg(MINFO, "cm_cleanup", "%s", str);

                     /* go again through whole list */
                     j = 0;
                  }
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

   BUFFER *pbuf;

   int status = bm_get_buffer("bm_get_buffer_info", buffer_handle, &pbuf);

   if (status != BM_SUCCESS)
      return status;

   bm_lock_buffer(pbuf);

   memcpy(buffer_header, pbuf->buffer_header, sizeof(BUFFER_HEADER));

   bm_unlock_buffer(pbuf);

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
      BUFFER *pbuf;

      int status = bm_get_buffer("bm_get_buffer_level", buffer_handle, &pbuf);

      if (status != BM_SUCCESS)
         return status;

      BUFFER_HEADER *pheader = pbuf->buffer_header;

      bm_lock_buffer(pbuf);

      BUFFER_CLIENT *pclient = bm_get_my_client(pbuf, pheader);

      *n_bytes = pheader->write_pointer - pclient->read_pointer;
      if (*n_bytes < 0)
         *n_bytes += pheader->size;

      bm_unlock_buffer(pbuf);

      /* add bytes in cache */
      if (pbuf->read_cache_wp > pbuf->read_cache_rp)
         *n_bytes += pbuf->read_cache_wp - pbuf->read_cache_rp;
   }
#endif                          /* LOCAL_ROUTINES */

   return BM_SUCCESS;
}


#ifdef LOCAL_ROUTINES

/********************************************************************/
static INT bm_get_buffer(const char *who, int buffer_handle, BUFFER **pbuf) {
   assert(pbuf);
   *pbuf = NULL;

   if (buffer_handle > _buffer_entries || buffer_handle <= 0) {
      cm_msg(MERROR, who, "invalid buffer handle %d: out of range, _buffer_entries is %d", buffer_handle,
             _buffer_entries);
      return BM_INVALID_HANDLE;
   }

   if (!_buffer[buffer_handle - 1].attached) {
      cm_msg(MERROR, who, "invalid buffer handle %d: not attached", buffer_handle);
      return BM_INVALID_HANDLE;
   }

   (*pbuf) = &_buffer[buffer_handle - 1];

   return BM_SUCCESS;
}

/********************************************************************/
static void bm_lock_buffer(BUFFER *pbuf) {
   // NB: locking order: 1st buffer mutex, 2nd buffer semaphore. Unlock in reverse order.

   if (pbuf->buffer_mutex)
      ss_mutex_wait_for(pbuf->buffer_mutex, _bm_mutex_timeout);

   int status = ss_semaphore_wait_for(pbuf->semaphore, _bm_lock_timeout);

   if (status != SS_SUCCESS) {
      cm_msg(MERROR, "bm_lock_buffer", "Cannot lock buffer \"%s\", ss_semaphore_wait_for() status %d, aborting...",
             pbuf->buffer_header->name, status);
      fprintf(stderr,
              "bm_lock_buffer: Error: Cannot lock buffer \"%s\", ss_semaphore_wait_for() status %d, aborting...\n",
              pbuf->buffer_header->name, status);
      abort();
      /* DOES NOT RETURN */
   }

   // protect against double lock
   assert(!pbuf->locked);
   pbuf->locked = TRUE;

#if 0
   int x = MAX_CLIENTS - 1;
   if (pbuf->buffer_header->client[x].unused1 != 0) {
      printf("lllock [%s] unused1 %d pid %d\n", pbuf->buffer_header->name, pbuf->buffer_header->client[x].unused1, getpid());
   }
   //assert(pbuf->buffer_header->client[x].unused1 == 0);
   pbuf->buffer_header->client[x].unused1 = getpid();
#endif

   pbuf->count_lock++;
}

/********************************************************************/
static void bm_unlock_buffer(BUFFER *pbuf) {
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
   if (pbuf->buffer_mutex)
      ss_mutex_release(pbuf->buffer_mutex);
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

   if (buffer_handle > _buffer_entries || buffer_handle <= 0) {
      cm_msg(MERROR, "bm_init_buffer_counters", "invalid buffer handle %d", buffer_handle);
      return BM_INVALID_HANDLE;
   }

   if (!_buffer[buffer_handle - 1].attached) {
      cm_msg(MERROR, "bm_init_buffer_counters", "invalid buffer handle %d", buffer_handle);
      return BM_INVALID_HANDLE;
   }

   _buffer[buffer_handle - 1].buffer_header->num_in_events = 0;
   _buffer[buffer_handle - 1].buffer_header->num_out_events = 0;

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
      if (buffer_handle > _buffer_entries || buffer_handle <= 0) {
         cm_msg(MERROR, "bm_set_cache_size", "invalid buffer handle %d", buffer_handle);
         return BM_INVALID_HANDLE;
      }

      if (!_buffer[buffer_handle - 1].attached) {
         cm_msg(MERROR, "bm_set_cache_size", "invalid buffer handle %d", buffer_handle);
         return BM_INVALID_HANDLE;
      }

      if (read_size < 0 || read_size > 1E6) {
         cm_msg(MERROR, "bm_set_cache_size", "invalid read chache size %d", read_size);
         return BM_INVALID_PARAM;
      }

      if (write_size < 0 || write_size > 1E6) {
         cm_msg(MERROR, "bm_set_cache_size", "invalid write chache size %d", write_size);
         return BM_INVALID_PARAM;
      }

      /* manage read cache */
      BUFFER *pbuf = &_buffer[buffer_handle - 1];

      if (pbuf->read_cache_size > 0) {
         M_FREE(pbuf->read_cache);
         pbuf->read_cache = NULL;
      }

      if (read_size > 0) {
         pbuf->read_cache = (char *) M_MALLOC(read_size);
         if (pbuf->read_cache == NULL) {
            cm_msg(MERROR, "bm_set_cache_size", "not enough memory to allocate cache buffer, malloc(%d) failed",
                   read_size);
            return BM_NO_MEMORY;
         }
      }

      pbuf->read_cache_size = read_size;
      pbuf->read_cache_rp = pbuf->read_cache_wp = 0;

      if (pbuf->write_cache_mutex)
         ss_mutex_wait_for(pbuf->write_cache_mutex, _bm_mutex_timeout);

      // FIXME: should flush the write cache!
      if (pbuf->write_cache_size && pbuf->write_cache_wp > 0) {
         cm_msg(MERROR, "bm_set_cache_size", "buffer \"%s\" lost %d bytes from the write cache",
                pbuf->buffer_header->name, pbuf->write_cache_wp);
      }

      /* manage write cache */
      if (pbuf->write_cache_size > 0) {
         M_FREE(pbuf->write_cache);
         pbuf->write_cache = NULL;
      }

      if (write_size > 0) {
         pbuf->write_cache = (char *) M_MALLOC(write_size);
         if (pbuf->write_cache == NULL) {
            cm_msg(MERROR, "bm_set_cache_size", "not enough memory to allocate cache buffer, malloc(%d) failed",
                   write_size);
            return BM_NO_MEMORY;
         }
      }

      pbuf->write_cache_size = write_size;
      pbuf->write_cache_wp = 0;

      if (pbuf->write_cache_mutex)
         ss_mutex_release(pbuf->write_cache_mutex);
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
@param size size if the data part of the event in bytes
@param serial serial number
@return BM_SUCCESS
*/
INT bm_compose_event(EVENT_HEADER *event_header, short int event_id, short int trigger_mask, DWORD size, DWORD serial) {
   event_header->event_id = event_id;
   event_header->trigger_mask = trigger_mask;
   event_header->data_size = size;
   event_header->time_stamp = ss_time();
   event_header->serial_number = serial;

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
      BUFFER *pbuf;

      int status = bm_get_buffer("bm_add_event_request", buffer_handle, &pbuf);

      if (status != BM_SUCCESS)
         return status;

      /* avoid callback/non callback requests */
      if (func == NULL && pbuf->callback) {
         cm_msg(MERROR, "bm_add_event_request", "mixing callback/non callback requests not possible");
         return BM_INVALID_MIXING;
      }

      /* do not allow GET_RECENT with nonzero cache size */
      if (sampling_type == GET_RECENT && pbuf->read_cache_size > 0) {
         cm_msg(MERROR, "bm_add_event_request", "GET_RECENT request not possible if read cache is enabled");
         return BM_INVALID_PARAM;
      }

      /* lock buffer */
      bm_lock_buffer(pbuf);

      /* get a pointer to the proper client structure */
      BUFFER_HEADER *pheader = pbuf->buffer_header;
      BUFFER_CLIENT *pclient = bm_get_my_client(pbuf, pheader);

      /* look for a empty request entry */
      int i;
      for (i = 0; i < MAX_EVENT_REQUESTS; i++)
         if (!pclient->event_request[i].valid)
            break;

      if (i == MAX_EVENT_REQUESTS) {
         bm_unlock_buffer(pbuf);
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

      bm_unlock_buffer(pbuf);
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
      BUFFER *pbuf;

      int status = bm_get_buffer("bm_remove_event_request", buffer_handle, &pbuf);

      if (status != BM_SUCCESS)
         return status;

      INT i, deleted;

      /* lock buffer */
      bm_lock_buffer(pbuf);

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

      bm_unlock_buffer(pbuf);

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
         if ((pevent->event_id & 0xF000) == EVENTID_FRAG1 || (pevent->event_id & 0xF000) == EVENTID_FRAG)
            bm_defragment_event(buffer_handle, i, pevent, (void *) (pevent + 1), _request_list[i].dispatcher);
         else
            _request_list[i].dispatcher(buffer_handle, i, pevent, (void *) (pevent + 1));
      }
}

#ifdef LOCAL_ROUTINES

static void bm_incr_read_cache(BUFFER *pbuf, int total_size) {
   /* increment read cache read pointer */
   pbuf->read_cache_rp += total_size;

   if (pbuf->read_cache_rp == pbuf->read_cache_wp) {
      pbuf->read_cache_rp = 0;
      pbuf->read_cache_wp = 0;
   }
}

static BOOL bm_peek_read_cache(BUFFER *pbuf, EVENT_HEADER **ppevent, int *pevent_size, int *ptotal_size) {
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
         //printf("bm_peek_buffer: buffer [%s] client [%s], set read_wait!\n", pheader->name, pc->name);
         pc->read_wait = TRUE;
      }
      return BM_ASYNC_RETURN;
   }

   if (pc->read_wait) {
      //printf("bm_peek_buffer: buffer [%s] client [%s], clear read_wait!\n", pheader->name, pc->name);
      pc->read_wait = FALSE;
   }

   if ((pc->read_pointer < 0) || (pc->read_pointer >= pheader->size)) {
      cm_msg(MERROR, "bm_peek_buffer",
             "event buffer \"%s\" is corrupted: client \"%s\" read pointer %d is invalid. buffer read pointer %d, write pointer %d, size %d",
             pheader->name, pc->name, pc->read_pointer, pheader->read_pointer, pheader->write_pointer, pheader->size);
      return BM_CORRUPTED;
   }

   char *pdata = (char *) (pheader + 1);

   EVENT_HEADER *pevent = (EVENT_HEADER *) (pdata + pc->read_pointer);
   int event_size = pevent->data_size + sizeof(EVENT_HEADER);
   int total_size = ALIGN8(event_size);

   if ((total_size <= 0) || (total_size > pheader->size)) {
      cm_msg(MERROR, "bm_peek_buffer",
             "event buffer \"%s\" is corrupted: client \"%s\" read pointer %d points to invalid event: data_size %d, event_size %d, total_size %d. buffer size: %d, read_pointer: %d, write_pointer: %d",
             pheader->name, pc->name, pc->read_pointer, pevent->data_size, event_size, total_size, pheader->size,
             pheader->read_pointer, pheader->write_pointer);
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

static void bm_read_from_buffer_locked(BUFFER_HEADER *pheader, int rp, char *buf, int event_size) {
   char *pdata = (char *) (pheader + 1);

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

static int bm_wait_for_more_events_locked(BUFFER *pbuf, BUFFER_HEADER *pheader, BUFFER_CLIENT *pc, int async_flag,
                                          BOOL unlock_read_cache);

static int bm_fill_read_cache_locked(BUFFER *pbuf, BUFFER_HEADER *pheader, int async_flag) {
   BUFFER_CLIENT *pc = bm_get_my_client(pbuf, pheader);
   BOOL need_wakeup = FALSE;
   int count_events = 0;

   //printf("bm_fill_read_cache: [%s] async %d, size %d, rp %d, wp %d\n", pheader->name, async_flag, pbuf->read_cache_size, pbuf->read_cache_rp, pbuf->read_cache_wp);

   /* loop over all events in the buffer */

   while (1) {
      EVENT_HEADER *pevent;
      int event_size;
      int total_size;

      int status = bm_peek_buffer_locked(pbuf, pheader, pc, &pevent, &event_size, &total_size);
      if (status == BM_CORRUPTED) {
         return status;
      } else if (status != BM_SUCCESS) {
         /* event buffer is empty */
         if (async_flag == BM_NO_WAIT) {
            if (need_wakeup)
               bm_wakeup_producers_locked(pheader, pc);
            //printf("bm_fill_read_cache: [%s] async %d, size %d, rp %d, wp %d, events %d, buffer is empty\n", pheader->name, async_flag, pbuf->read_cache_size, pbuf->read_cache_rp, pbuf->read_cache_wp, count_events);
            if (pbuf->read_cache_rp == pbuf->read_cache_wp) {
               // read cache is empty
               return BM_ASYNC_RETURN;
            }
            return BM_SUCCESS;
         }
         int status = bm_wait_for_more_events_locked(pbuf, pheader, pc, async_flag, TRUE);
         if (status != BM_SUCCESS) {
            // we only come here with SS_ABORT & co
            //printf("bm_fill_read_cache: [%s] async %d, size %d, rp %d, wp %d, events %d, bm_wait_for_more_events() status %d\n", pheader->name, async_flag, pbuf->read_cache_size, pbuf->read_cache_rp, pbuf->read_cache_wp, count_events, status);
            return status;
         }
         // make sure we wait for new event only once
         async_flag = BM_NO_WAIT;
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

static int bm_wait_for_free_space_locked(int buffer_handle, BUFFER *pbuf, int async_flag, int requested_space) {
   int status;
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

   DWORD blocking_time = 0;
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

#if 0
         printf("bm_wait_for_free_space: buffer pointers: read: %d, write: %d, free space: %d, bufsize: %d, event size: %d\n", pheader->read_pointer, pheader->write_pointer, free, pheader->size, requested_space);
#endif

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
            if (!blocking_time) {
               blocking_time = ss_millitime();
            }
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

      /* return now in ASYNC mode */
      if (async_flag == BM_NO_WAIT)
         return BM_ASYNC_RETURN;

      ss_suspend_get_buffer_port(ss_gettid(), &pc->port);

      bm_unlock_buffer(pbuf);

      //printf("bm_wait_for_free_space: blocking client \"%s\"\n", blocking_client_name);

#ifdef DEBUG_MSG
      cm_msg(MDEBUG, "Send sleep: rp=%d, wp=%d, level=%1.1lf",
             pheader->read_pointer, pheader->write_pointer, 100 - 100.0 * size / pheader->size);
#endif

      ///* signal other clients wait mode */
      //int idx = bm_validate_client_index(pbuf, FALSE);
      //if (idx >= 0)
      //   pheader->client[idx].write_wait = requested_space;

      bm_cleanup("bm_wait_for_free_space", ss_millitime(), FALSE);

      status = ss_suspend(1000, MSG_BM);

      /* make sure we do sleep in this loop:
       * if we are the mserver receiving data on the event
       * socket and the data buffer is full, ss_suspend() will
       * never sleep: it will detect data on the event channel,
       * call rpc_server_receive() (recursively, we already *are* in
       * rpc_server_receive()) and return without sleeping. Result
       * is a busy loop waiting for free space in data buffer */
      if (status != SS_TIMEOUT)
         ss_sleep(10);

      bm_lock_buffer(pbuf);

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

      /* return if TCP connection broken */
      if (status == SS_ABORT) {
         // NB: buffer is locked!
         return SS_ABORT;
      }

#ifdef DEBUG_MSG
      cm_msg(MDEBUG, "Send woke up: rp=%d, wp=%d, level=%1.1lf",
             pheader->read_pointer, pheader->write_pointer, 100 - 100.0 * size / pheader->size);
#endif

   }
}

static int bm_wait_for_more_events_locked(BUFFER *pbuf, BUFFER_HEADER *pheader, BUFFER_CLIENT *pc, int async_flag, BOOL unlock_read_cache)
{
   if (pc->read_pointer != pheader->write_pointer) {
      // buffer has data
      return BM_SUCCESS;
   }

   if (async_flag == BM_NO_WAIT) {
      /* event buffer is empty and we are told to not wait */
      if (!pc->read_wait) {
         //printf("bm_wait_for_more_events: buffer [%s] client [%s] set read_wait in BM_NO_WAIT!\n", pheader->name, pc->name);
         pc->read_wait = TRUE;
      }
      return BM_ASYNC_RETURN;
   }

   while (pc->read_pointer == pheader->write_pointer) {
      /* wait until there is data in the buffer (write pointer moves) */

      if (!pc->read_wait) {
         //printf("bm_wait_for_more_events: buffer [%s] client [%s] set read_wait!\n", pheader->name, pc->name);
         pc->read_wait = TRUE;
      }

      ss_suspend_get_buffer_port(ss_gettid(), &pc->port);

      // NB: locking order is: 1st read cache lock, 2nd buffer lock, unlock in reverse order

      bm_unlock_buffer(pbuf);

      if (unlock_read_cache)
         if (pbuf->read_cache_mutex)
            ss_mutex_release(pbuf->read_cache_mutex);

      int status = ss_suspend(1000, MSG_BM);

      // NB: locking order is: 1st read cache lock, 2nd buffer lock, unlock in reverse order

      if (unlock_read_cache)
         if (pbuf->read_cache_mutex)
            ss_mutex_wait_for(pbuf->read_cache_mutex, _bm_mutex_timeout);

      bm_lock_buffer(pbuf);

      /* need to revalidate our BUFFER_CLIENT after releasing the buffer lock
       * because we may have been removed from the buffer by bm_cleanup() & co
       * due to a timeout or whatever. */
      pc = bm_get_my_client(pbuf, pheader);

      /* return if TCP connection broken */
      if (status == SS_ABORT)
         return SS_ABORT;
   }

   if (pc->read_wait) {
      //printf("bm_wait_for_more_events: buffer [%s] client [%s] clear read_wait!\n", pheader->name, pc->name);
      pc->read_wait = FALSE;
   }

   return BM_SUCCESS;
}

static void bm_write_to_buffer_locked(BUFFER_HEADER *pheader, const void *pevent, int event_size, int total_size) {
   char *pdata = (char *) (pheader + 1);

   //int old_write_pointer = pheader->write_pointer;

   /* new event fits into the remaining space? */
   if (pheader->write_pointer + total_size <= pheader->size) {
      memcpy(pdata + pheader->write_pointer, pevent, event_size);
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
      int size = pheader->size - pheader->write_pointer;

      memcpy(pdata + pheader->write_pointer, pevent, size);
      memcpy(pdata, ((const char *) pevent) + size, event_size - size);

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
@param async_flag Synchronous/asynchronous flag. If BM_WAIT, the function
blocks if the buffer has not enough free space to receive the event.
If BM_NO_WAIT, the function returns immediately with a
value of BM_ASYNC_RETURN without writing the event to the buffer
@return BM_SUCCESS, BM_INVALID_HANDLE, BM_INVALID_PARAM<br>
BM_ASYNC_RETURN Routine called with async_flag == BM_NO_WAIT and
buffer has not enough space to receive event<br>
BM_NO_MEMORY   Event is too large for network buffer or event buffer.
One has to increase the event buffer size "/Experiment/Buffer sizes/SYSTEM"
and/or /Experiment/MAX_EVENT_SIZE in ODB.
*/
INT bm_send_event(INT buffer_handle, const EVENT_HEADER *pevent, INT unused, INT async_flag) {
   const int event_size = sizeof(EVENT_HEADER) + pevent->data_size;

   //printf("bm_send_event: pevent %p, data_size %d, event_size %d, buf_size %d\n", pevent, pevent->data_size, event_size, xbuf_size);

   if (rpc_is_remote())
      return rpc_call(RPC_BM_SEND_EVENT, buffer_handle, pevent, event_size, async_flag);

#ifdef LOCAL_ROUTINES
   {
      int status;

      if (buffer_handle > _buffer_entries || buffer_handle <= 0) {
         cm_msg(MERROR, "bm_send_event", "invalid buffer handle %d", buffer_handle);
         return BM_INVALID_HANDLE;
      }

      BUFFER *pbuf = &_buffer[buffer_handle - 1];

      if (!pbuf->attached) {
         cm_msg(MERROR, "bm_send_event", "invalid buffer handle %d", buffer_handle);
         return BM_INVALID_HANDLE;
      }

      /* round up total_size to next DWORD boundary */
      int total_size = ALIGN8(event_size);

      /* NB: !!!the write cache is not thread-safe!!! */

      /* look if there is space in the cache */
      if (pbuf->write_cache_size) {
         if (pbuf->write_cache_mutex)
            ss_mutex_wait_for(pbuf->write_cache_mutex, _bm_mutex_timeout);

         if (pbuf->write_cache_size) {
            int status = BM_SUCCESS;

            /* if this event does not fit into the write cache, flush the write cache */
            if (pbuf->write_cache_wp + total_size > pbuf->write_cache_size) {
               if (pbuf->write_cache_mutex)
                  ss_mutex_release(pbuf->write_cache_mutex);
               status = bm_flush_cache(buffer_handle, async_flag);
               if (status != BM_SUCCESS) {
                  return status;
               }
               if (pbuf->write_cache_mutex)
                  ss_mutex_wait_for(pbuf->write_cache_mutex, _bm_mutex_timeout);
            }

            /* write this event into the write cache, if it fits */
            if (pbuf->write_cache_wp + total_size <= pbuf->write_cache_size) {
               //printf("bm_send_event: write %d/%d to cache size %d, wp %d\n", event_size, total_size, pbuf->write_cache_size, pbuf->write_cache_wp);

               memcpy(pbuf->write_cache + pbuf->write_cache_wp, pevent, event_size);

               pbuf->write_cache_wp += total_size;

               if (pbuf->write_cache_mutex)
                  ss_mutex_release(pbuf->write_cache_mutex);
               return BM_SUCCESS;
            }
         }

         /* event did not fit into the write cache, send it directly to shared memory */
         if (pbuf->write_cache_mutex)
            ss_mutex_release(pbuf->write_cache_mutex);
      }

      /* we come here only for events that are too big to fit into the cache */

      /* lock the buffer */
      bm_lock_buffer(pbuf);

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
      if (total_size >= pheader->size) {
         bm_unlock_buffer(pbuf);
         cm_msg(MERROR, "bm_send_event", "total event size (%d) larger than size (%d) of buffer \'%s\'", total_size,
                pheader->size, pheader->name);
         return BM_NO_MEMORY;
      }

      status = bm_wait_for_free_space_locked(buffer_handle, pbuf, async_flag, total_size);
      if (status != BM_SUCCESS) {
         bm_unlock_buffer(pbuf);
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

      bm_write_to_buffer_locked(pheader, pevent, event_size, total_size);

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

      /* unlock the buffer */
      bm_unlock_buffer(pbuf);
   }
#endif                          /* LOCAL_ROUTINES */

   return BM_SUCCESS;
}

/********************************************************************/
/**
Empty write cache.
This function should be used if events in the write cache
should be visible to the consumers immediately. It should be called at the
end of each run, otherwise events could be kept in the write buffer and will
flow to the data of the next run.
@param buffer_handle Buffer handle obtained via bm_open_buffer()
@param async_flag Synchronous/asynchronous flag.
If BM_WAIT, the function blocks if the buffer has not
enough free space to receive the full cache. If BM_NO_WAIT, the function returns
immediately with a value of BM_ASYNC_RETURN without writing the cache.
@return BM_SUCCESS, BM_INVALID_HANDLE<br>
BM_ASYNC_RETURN Routine called with async_flag == BM_NO_WAIT
and buffer has not enough space to receive cache<br>
BM_NO_MEMORY Event is too large for network buffer or event buffer.
One has to increase the event buffer size "/Experiment/Buffer sizes/SYSTEM"
and/or /Experiment/MAX_EVENT_SIZE in ODB.
*/
INT bm_flush_cache(INT buffer_handle, INT async_flag) {
   if (rpc_is_remote())
      return rpc_call(RPC_BM_FLUSH_CACHE, buffer_handle, async_flag);

#ifdef LOCAL_ROUTINES
   {
      INT status;

      if (buffer_handle > _buffer_entries || buffer_handle <= 0) {
         cm_msg(MERROR, "bm_flush_cache", "invalid buffer handle %d", buffer_handle);
         return BM_INVALID_HANDLE;
      }

      BUFFER *pbuf = &_buffer[buffer_handle - 1];

      if (!pbuf->attached) {
         cm_msg(MERROR, "bm_flush_cache", "invalid buffer handle %d", buffer_handle);
         return BM_INVALID_HANDLE;
      }

      if (pbuf->write_cache_size == 0)
         return BM_SUCCESS;

      /* check if anything needs to be flushed */
      if (pbuf->write_cache_wp == 0)
         return BM_SUCCESS;

      /* lock the buffer */
      bm_lock_buffer(pbuf);

      /* calculate some shorthands */
      BUFFER_HEADER *pheader = pbuf->buffer_header;

#if 0
      status = bm_validate_buffer_locked(pbuf);
      if (status != BM_SUCCESS) {
         printf("bm_flush_cache: corrupted 111!\n");
         abort();
      }
#endif

      status = bm_wait_for_free_space_locked(buffer_handle, pbuf, async_flag, pbuf->write_cache_wp);
      if (status != BM_SUCCESS) {
         bm_unlock_buffer(pbuf);
         return status;
      }

#if 0
      status = bm_validate_buffer_locked(pbuf);
      if (status != BM_SUCCESS) {
         printf("bm_flush_cache: corrupted 222!\n");
         abort();
      }
#endif

      if (pbuf->write_cache_mutex)
         ss_mutex_wait_for(pbuf->write_cache_mutex, _bm_mutex_timeout);

      if (pbuf->write_cache_wp == 0) {
         /* somebody emptied the cache while we were inside bm_wait_for_free_space */
         if (pbuf->write_cache_mutex)
            ss_mutex_release(pbuf->write_cache_mutex);
         return BM_SUCCESS;
      }

      /* we have space, so let's copy the event */
      int old_write_pointer = pheader->write_pointer;

      int request_id[MAX_CLIENTS];
      int i;
      for (i = 0; i < pheader->max_client_index; i++) {
         request_id[i] = -1;
      }

#if 0
      int first_wp = pheader->write_pointer;
      int first_rp = pheader->read_pointer;
#endif

      int rp = 0;
      while (rp < pbuf->write_cache_wp) {
         /* loop over all events in cache */

#if 0
         int old_wp = pheader->write_pointer;
#endif

         const EVENT_HEADER *pevent = (const EVENT_HEADER *) (pbuf->write_cache + rp);
         int event_size = (pevent->data_size + sizeof(EVENT_HEADER));
         int total_size = ALIGN8(event_size);

#if 0
         printf("bm_flush_cache: cache size %d, wp %d, rp %d, event data_size %d, event_size %d, total_size %d\n",
                pbuf->write_cache_size,
                pbuf->write_cache_wp,
                rp,
                pevent->data_size,
                event_size,
                total_size);
#endif

         assert(total_size >= (int) sizeof(EVENT_HEADER));
         assert(total_size <= pheader->size);

         bm_write_to_buffer_locked(pheader, pevent, event_size, total_size);

         pbuf->count_sent += 1;
         pbuf->bytes_sent += total_size;

#if 0
         status = bm_validate_buffer_locked(pbuf);
         if (status != BM_SUCCESS) {
            char* pdata = (char *) (pheader + 1);
            printf("bm_flush_cache: corrupted WWW! buffer: first wp %d, old wp %d, new wp %d, first rp %d, rp %d, cache size %d, wp %d, rp %d, event %d %d %d, ts 0x%08x, ds 0x%08x, at old_wp 0x%08x 0x%08x 0x%08x 0x%08x\n",
                   first_wp, old_wp, pheader->write_pointer,
                   first_rp, pheader->read_pointer,
                   pbuf->write_cache_size, pbuf->write_cache_wp, rp,
                   pevent->data_size,
                   event_size,
                   total_size,
                   pevent->time_stamp,
                   pevent->data_size,
                   ((uint32_t*)(pdata + old_wp))[0],
                   ((uint32_t*)(pdata + old_wp))[1],
                   ((uint32_t*)(pdata + old_wp))[2],
                   ((uint32_t*)(pdata + old_wp))[3]);
            abort();
         }
#endif

         /* see comment for the same code in bm_send_event().
          * We make sure the buffer is nevere 100% full */
         assert(pheader->write_pointer != pheader->read_pointer);

         /* check if anybody has a request for this event */
         for (i = 0; i < pheader->max_client_index; i++) {
            BUFFER_CLIENT *pc = pheader->client + i;
            int r = bm_find_first_request_locked(pc, pevent);
            if (r >= 0) {
               request_id[i] = r;
            }
         }

         /* this loop does not loop forever because rp
          * is monotonously incremented here. write_cache_wp does
          * not change */

         rp += total_size;

         assert(rp > 0);
         assert(rp <= pbuf->write_cache_size);
      }

      /* the write cache is now empty */
      pbuf->write_cache_wp = 0;

      if (pbuf->write_cache_mutex)
         ss_mutex_release(pbuf->write_cache_mutex);

      /* check which clients are waiting */
      for (i = 0; i < pheader->max_client_index; i++) {
         BUFFER_CLIENT *pc = pheader->client + i;
         bm_notify_reader_locked(pheader, pc, old_write_pointer, request_id[i]);
      }

#if 0
      status = bm_validate_buffer_locked(pbuf);
      if (status != BM_SUCCESS) {
         printf("bm_flush_cache: corrupted 333!\n");
         abort();
      }
#endif

      /* update statistics */
      pheader->num_in_events++;

      /* unlock the buffer */
      bm_unlock_buffer(pbuf);
   }
#endif                          /* LOCAL_ROUTINES */

   return BM_SUCCESS;
}

#ifdef LOCAL_ROUTINES

static INT bm_read_buffer(BUFFER *pbuf, INT buffer_handle, void **bufptr, void *buf, INT *buf_size, INT async_flag,
                          int convert_flags, BOOL dispatch) {
   INT status = BM_SUCCESS;

   int max_size = 0;
   if (buf_size) {
      max_size = *buf_size;
      *buf_size = 0;
   }

   BUFFER_HEADER *pheader = pbuf->buffer_header;

   //printf("bm_read_buffer: [%s] async %d, conv %d, ptr %p, buf %p, disp %d\n", pheader->name, async_flag, convert_flags, bufptr, buf, dispatch);

   BOOL locked = FALSE;

   // NB: locking order is: 1st read cache lock, 2nd buffer lock, unlock in reverse order

   /* look if there is anything in the cache */
   if (pbuf->read_cache_size > 0) {
      if (pbuf->read_cache_mutex)
         ss_mutex_wait_for(pbuf->read_cache_mutex, _bm_mutex_timeout);
      if (pbuf->read_cache_wp == 0) {
         bm_lock_buffer(pbuf);
         locked = TRUE;
         status = bm_fill_read_cache_locked(pbuf, pheader, async_flag);
         if (status != BM_SUCCESS) {
            bm_unlock_buffer(pbuf);
            if (pbuf->read_cache_mutex)
               ss_mutex_release(pbuf->read_cache_mutex);
            return status;
         }
      }
      EVENT_HEADER *pevent;
      int event_size;
      int total_size;
      if (bm_peek_read_cache(pbuf, &pevent, &event_size, &total_size)) {
         if (locked) {
            // do not need to keep the event buffer locked
            // when reading from the read cache
            bm_unlock_buffer(pbuf);
         }
         //printf("bm_read_buffer: [%s] async %d, conv %d, ptr %p, buf %p, disp %d, total_size %d, read from cache %d %d %d\n", pheader->name, async_flag, convert_flags, bufptr, buf, dispatch, total_size, pbuf->read_cache_size, pbuf->read_cache_rp, pbuf->read_cache_wp);
         status = BM_SUCCESS;
         if (buf) {
            if (event_size > max_size) {
               cm_msg(MERROR, "bm_read_buffer",
                      "buffer size %d is smaller than event size %d, event truncated. buffer \"%s\"", max_size,
                      event_size, pheader->name);
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
         }
         if (bufptr) {
            *bufptr = malloc(event_size);
            memcpy(*bufptr, pevent, event_size);
            status = BM_SUCCESS;
         }
         bm_incr_read_cache(pbuf, total_size);
         if (pbuf->read_cache_mutex)
            ss_mutex_release(pbuf->read_cache_mutex);
         if (dispatch) {
            // FIXME need to protect currently dispatched event against
            // another thread overwriting it by refilling the read cache
            bm_dispatch_event(buffer_handle, pevent);
            return BM_MORE_EVENTS;
         }
         return status;
      }
      if (pbuf->read_cache_mutex)
         ss_mutex_release(pbuf->read_cache_mutex);
   }

   /* we come here if the read cache is disabled */
   /* we come here if the next event is too big to fit into the read cache */

   if (!locked)
      bm_lock_buffer(pbuf);

   EVENT_HEADER *event_buffer = NULL;

   BUFFER_CLIENT *pc = bm_get_my_client(pbuf, pheader);

   while (1) {
      /* loop over events in the event buffer */

      status = bm_wait_for_more_events_locked(pbuf, pheader, pc, async_flag, FALSE);

      if (status != BM_SUCCESS) {
         bm_unlock_buffer(pbuf);
         return status;
      }

      /* check if event at current read pointer matches a request */

      EVENT_HEADER *pevent;
      int event_size;
      int total_size;

      status = bm_peek_buffer_locked(pbuf, pheader, pc, &pevent, &event_size, &total_size);
      if (status == BM_CORRUPTED) {
         bm_unlock_buffer(pbuf);
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
         }

         if (dispatch || bufptr) {
            assert(event_buffer == NULL); // make sure we only come here once
            event_buffer = (EVENT_HEADER *) malloc(event_size);

            bm_read_from_buffer_locked(pheader, pc->read_pointer, (char *) event_buffer, event_size);

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

   bm_unlock_buffer(pbuf);

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
@param async_flag Synchronous/asynchronous flag. If BM_WAIT, the function
blocks if no event is available. If BM_NO_WAIT, the function returns immediately
with a value of BM_ASYNC_RETURN without receiving any event.
@return BM_SUCCESS, BM_INVALID_HANDLE <br>
BM_TRUNCATED   The event is larger than the destination buffer and was
               therefore truncated <br>
BM_ASYNC_RETURN No event available
*/
INT bm_receive_event(INT buffer_handle, void *destination, INT *buf_size, INT async_flag) {
   //printf("bm_receive_event: handle %d, async %d\n", buffer_handle, async_flag);

   if (rpc_is_remote()) {
      int status, old_timeout = 0;

      if (!async_flag) {
         old_timeout = rpc_get_option(-1, RPC_OTIMEOUT);
         rpc_set_option(-1, RPC_OTIMEOUT, 0);
      }

      status = rpc_call(RPC_BM_RECEIVE_EVENT, buffer_handle, destination, buf_size, async_flag);

      if (!async_flag) {
         rpc_set_option(-1, RPC_OTIMEOUT, old_timeout);
      }

      //printf("bm_receive_event: handle %d, async %d, status %d, size %d, via RPC_BM_RECEIVE_EVENT\n", buffer_handle, async_flag, status, *buf_size);

      return status;
   }
#ifdef LOCAL_ROUTINES
   {
      INT convert_flags = 0;
      INT status = BM_SUCCESS;

      BUFFER *pbuf;

      status = bm_get_buffer("bm_receive_event", buffer_handle, &pbuf);

      if (status != BM_SUCCESS)
         return status;

      if (rpc_is_mserver())
         convert_flags = rpc_get_server_option(RPC_CONVERT_FLAGS);
      else
         convert_flags = 0;

      status = bm_read_buffer(pbuf, buffer_handle, NULL, destination, buf_size, async_flag, convert_flags, FALSE);
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
@param async_flag Synchronous/asynchronous flag. If BM_WAIT, the function
blocks if no event is available. If BM_NO_WAIT, the function returns immediately
with a value of BM_ASYNC_RETURN without receiving any event.
@return BM_SUCCESS, BM_INVALID_HANDLE <br>
BM_ASYNC_RETURN No event available
*/
INT bm_receive_event_alloc(INT buffer_handle, EVENT_HEADER **ppevent, INT async_flag) {
   if (rpc_is_remote()) {
      // nice try!
      abort();
#if 0
      int status, old_timeout = 0;

      if (!async_flag) {
         old_timeout = rpc_get_option(-1, RPC_OTIMEOUT);
         rpc_set_option(-1, RPC_OTIMEOUT, 0);
      }

      status = rpc_call(RPC_BM_RECEIVE_EVENT, buffer_handle, destination, buf_size, async_flag);

      if (!async_flag) {
         rpc_set_option(-1, RPC_OTIMEOUT, old_timeout);
      }
      return status;
#endif
   }
#ifdef LOCAL_ROUTINES
   {
      INT convert_flags = 0;
      INT status = BM_SUCCESS;

      BUFFER *pbuf;

      status = bm_get_buffer("bm_receive_event_alloc", buffer_handle, &pbuf);

      if (status != BM_SUCCESS)
         return status;

      if (rpc_is_mserver())
         convert_flags = rpc_get_server_option(RPC_CONVERT_FLAGS);
      else
         convert_flags = 0;

      return bm_read_buffer(pbuf, buffer_handle, (void **) ppevent, NULL, NULL, async_flag, convert_flags, FALSE);
   }
#else                           /* LOCAL_ROUTINES */

   return BM_SUCCESS;
#endif
}

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
      if (buffer_handle > _buffer_entries || buffer_handle <= 0) {
         cm_msg(MERROR, "bm_skip_event", "invalid buffer handle %d", buffer_handle);
         return BM_INVALID_HANDLE;
      }

      BUFFER *pbuf = &_buffer[buffer_handle - 1];
      BUFFER_HEADER *pheader = pbuf->buffer_header;

      if (!pbuf->attached) {
         cm_msg(MERROR, "bm_skip_event", "invalid buffer handle %d", buffer_handle);
         return BM_INVALID_HANDLE;
      }

      /* clear read cache */
      if (pbuf->read_cache_size > 0) {
         pbuf->read_cache_rp = 0;
         pbuf->read_cache_wp = 0;
      }

      bm_lock_buffer(pbuf);

      /* forward read pointer to global write pointer */
      BUFFER_CLIENT *pclient = bm_get_my_client(pbuf, pheader);
      pclient->read_pointer = pheader->write_pointer;

      bm_unlock_buffer(pbuf);
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

   return bm_read_buffer(pbuf, buffer_handle, NULL, NULL, NULL, BM_NO_WAIT, 0, TRUE);
}

/********************************************************************/
/**
Check a buffer if an event is available and call the dispatch function if found.
@param buffer_name       Name of buffer
@return BM_SUCCESS, BM_INVALID_HANDLE, BM_TRUNCATED, BM_ASYNC_RETURN, BM_CORRUPTED, RPC_NET_ERROR
*/
static INT bm_push_event(const char *buffer_name) {
   int i;
   for (i = 0; i < _buffer_entries; i++) {
      BUFFER *pbuf = _buffer + i;
      if (pbuf->attached) {
         if (strcmp(buffer_name, pbuf->buffer_header->name) == 0) {
            return bm_push_buffer(pbuf, i + 1);
         }
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
      INT idx, status = 0;
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

      /* go through all buffers */
      for (idx = 0; idx < _buffer_entries; idx++) {
         if (!_buffer[idx].attached)
            continue;

         int count_loops = 0;
         while (1) {
            if (idx < _buffer_entries
                && _buffer[idx].attached
                && _buffer[idx].buffer_header->name[0] != 0) {
               /* one bm_push_event could cause a run stop and a buffer close, which
                * would crash the next call to bm_push_event(). So check for valid
                * buffer on each call */

               status = bm_push_buffer(_buffer + idx, idx + 1);

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
   INT i;
   static DWORD last_time = 0;
   DWORD now = ss_millitime();

   //printf("bm_notify_client: buffer [%s], socket %d, time %d\n", buffer_name, client_socket, now - last_time);

   for (i = 0; i < _buffer_entries; i++)
      if (strcmp(buffer_name, _buffer[i].buffer_header->name) == 0)
         break;
   if (i == _buffer_entries)
      return BM_INVALID_HANDLE;

   /* don't send notification if client has no callback defined
      to receive events -> client calls bm_receive_event manually */
   if (!_buffer[i].callback)
      return DB_SUCCESS;

   int convert_flags = rpc_get_server_option(RPC_CONVERT_FLAGS);

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
   INT status, size;
   DWORD start_time;
   BOOL dispatched_something = FALSE;

   //printf("bm_poll_event!\n");

   start_time = ss_millitime();

   /* loop over all requests */
   int request_id;
   for (request_id = 0; request_id < _request_list_entries; request_id++) {
      /* continue if no dispatcher set (manual bm_receive_event) */
      if (_request_list[request_id].dispatcher == NULL)
         continue;

      do {
         if (_event_buffer_size == 0) {
            int size = _bm_max_event_size + sizeof(EVENT_HEADER);
            _event_buffer = (EVENT_HEADER *) M_MALLOC(size);
            if (!_event_buffer) {
               cm_msg(MERROR, "bm_poll_event", "not enough memory to allocate event buffer of size %d", size);
               return SS_ABORT;
            }
            _event_buffer_size = size;
            //printf("bm_poll: allocated event buffer size %d, max_event_size %d\n", size, _bm_max_event_size);
         }
         /* receive event */
         size = _event_buffer_size;
         status = bm_receive_event(_request_list[request_id].buffer_handle, _event_buffer, &size, BM_NO_WAIT);

         /* call user function if successful */
         if (status == BM_SUCCESS) {
            bm_dispatch_event(_request_list[request_id].buffer_handle, _event_buffer);
            dispatched_something = TRUE;
         }

         /* break if no more events */
         if (status == BM_ASYNC_RETURN)
            break;

         /* break if corrupted event buffer */
         if (status == BM_TRUNCATED) {
            cm_msg(MERROR, "bm_poll_event",
                   "received event was truncated, buffer size %d is too small, see messages and increase /Experiment/MAX_EVENT_SIZE in ODB",
                   _event_buffer_size);
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
      /* go through all buffers */
      int idx;
      for (idx = 0; idx < _buffer_entries; idx++) {
         if (!_buffer[idx].attached)
            continue;

         int status = bm_skip_event(idx+1);
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

   if ((pevent->event_id & 0xF000) == EVENTID_FRAG1) {
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

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/********************************************************************\
*                                                                    *
*                         RPC functions                              *
*                                                                    *
\********************************************************************/

/* globals */

static MUTEX_T *_client_connection_mutex = NULL;
static RPC_CLIENT_CONNECTION _client_connection[MAX_RPC_CONNECTION];
static RPC_SERVER_CONNECTION _server_connection; // connection to the mserver
static BOOL _rpc_is_remote = FALSE;

static RPC_SERVER_ACCEPTION _server_acception[MAX_RPC_CONNECTION];

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
   rpc_calc_convert_flags(rpc_get_option(0, RPC_OHW_TYPE), _server_connection.remote_hw_type, convert_flags);
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
   INT i, n, single_size;
   char *p;

   /* convert array */
   if (flags & (RPC_FIXARRAY | RPC_VARARRAY)) {
      single_size = tid_size[tid];
      /* don't convert TID_ARRAY & TID_STRUCT */
      if (single_size == 0)
         return;

      n = total_size / single_size;

      for (i = 0; i < n; i++) {
         p = (char *) data + (i * single_size);
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

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

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
   INT i, status, idx, size;
   struct sockaddr_in bind_addr;
   INT sock;
   INT remote_hw_type, hw_type;
   char version[32], v1[32];
   char local_host_name[HOST_NAME_LENGTH];
   struct hostent *phe;

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

   /* make this funciton multi-thread safe */
   if (!_client_connection_mutex) {
      ss_mutex_create(&_client_connection_mutex, FALSE);
   }

   ss_mutex_wait_for(_client_connection_mutex, 10000);

#if 0
   for (i = 0; i < MAX_RPC_CONNECTION; i++)
      if (_client_connection[i].send_sock != 0)
         printf("connection %d: client \"%s\" on host \"%s\" port %d, socket %d, connected %d\n", i, _client_connection[i].client_name, _client_connection[i].host_name, _client_connection[i].port, _client_connection[i].send_sock, _client_connection[i].connected);
#endif

   /* check if connection already exists */
   for (i = 0; i < MAX_RPC_CONNECTION; i++)
      if (_client_connection[i].send_sock != 0 &&
          strcmp(_client_connection[i].host_name.c_str(), host_name) == 0 && _client_connection[i].port == port) {
         status = ss_socket_wait(_client_connection[i].send_sock, 0);
         if (status == SS_TIMEOUT) { // socket should be empty
            *hConnection = i + 1;
            ss_mutex_release(_client_connection_mutex);
            return RPC_SUCCESS;
         }
         //cm_msg(MINFO, "rpc_client_connect", "Stale connection to \"%s\" on host %s is closed", _client_connection[i].client_name, _client_connection[i].host_name);
         closesocket(_client_connection[i].send_sock);
         _client_connection[i].send_sock = 0;
      }

   /* search for free entry */
   for (i = 0; i < MAX_RPC_CONNECTION; i++)
      if (_client_connection[i].send_sock == 0)
         break;

   /* open new network connection */
   if (i == MAX_RPC_CONNECTION) {
      cm_msg(MERROR, "rpc_client_connect", "maximum number of connections exceeded");
      ss_mutex_release(_client_connection_mutex);
      return RPC_NO_CONNECTION;
   }

   /* create a new socket for connecting to remote server */
   sock = socket(AF_INET, SOCK_STREAM, 0);
   if (sock == -1) {
      cm_msg(MERROR, "rpc_client_connect", "cannot create socket, socket() errno %d (%s)", errno, strerror(errno));
      ss_mutex_release(_client_connection_mutex);
      return RPC_NET_ERROR;
   }

   idx = i;
   _client_connection[idx].host_name = host_name;
   _client_connection[idx].client_name = client_name;
   _client_connection[idx].port = port;
   _client_connection[idx].exp_name = "";
   _client_connection[idx].rpc_timeout = DEFAULT_RPC_TIMEOUT;
   _client_connection[idx].rpc_timeout = DEFAULT_RPC_TIMEOUT;
   _client_connection[idx].send_sock = sock;
   _client_connection[idx].connected = 0;

   ss_mutex_release(_client_connection_mutex);

   /* connect to remote node */
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
   phe = gethostbyname(host_name);
   if (phe == NULL) {
      cm_msg(MERROR, "rpc_client_connect", "cannot lookup host name \'%s\'", host_name);
      _client_connection[idx].send_sock = 0;
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
      cm_msg(MERROR, "rpc_client_connect",
             "cannot connect to host \"%s\", port %d: connect() returned %d, errno %d (%s)", host_name, port, status,
             errno, strerror(errno));
      _client_connection[idx].send_sock = 0;
      return RPC_NET_ERROR;
   }

   _client_connection[idx].connected = 1;

   /* set TCP_NODELAY option for better performance */
   i = 1;
   setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *) &i, sizeof(i));

   /* send local computer info */
   std::string local_prog_name = rpc_get_name();
   ss_gethostname(local_host_name, sizeof(local_host_name));

   hw_type = rpc_get_option(0, RPC_OHW_TYPE);

   char str[128 + NAME_LENGTH + HOST_NAME_LENGTH];
   sprintf(str, "%d %s %s %s", hw_type, cm_get_version(), local_prog_name.c_str(), local_host_name);

   size = strlen(str) + 1;
   i = send(sock, str, size, 0);
   if (i < 0 || i != size) {
      cm_msg(MERROR, "rpc_client_connect", "cannot send %d bytes, send() returned %d, errno %d (%s)", size, i, errno,
             strerror(errno));
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

   /* receive remote computer info */
   i = recv_string(sock, str, sizeof(str), _rpc_connect_timeout);

   if (restore_watchdog_timeout) {
      cm_set_watchdog_params(watchdog_call, watchdog_timeout);
   }

   if (i <= 0) {
      cm_msg(MERROR, "rpc_client_connect", "timeout on receive remote computer info: %s", str);
      return RPC_NET_ERROR;
   }

   remote_hw_type = version[0] = 0;
   sscanf(str, "%d %s", &remote_hw_type, version);
   _client_connection[idx].remote_hw_type = remote_hw_type;

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
      cm_msg(MERROR, "rpc_client_connect", "remote MIDAS version \'%s\' differs from local version \'%s\'", version,
             cm_get_version());
   }

   *hConnection = idx + 1;

   return RPC_SUCCESS;
}

/********************************************************************/
void rpc_client_check()
/********************************************************************\

  Routine: rpc_client_check

  Purpose: Check all client connections if remote client closed link

\********************************************************************/
{
   INT i, status;

#if 0
   for (i = 0; i < MAX_RPC_CONNECTION; i++)
      if (_client_connection[i].send_sock != 0)
         printf("slot %d, checking client %s socket %d, connected %d\n", i, _client_connection[i].client_name, _client_connection[i].send_sock, _client_connection[i].connected);
#endif

   /* check for broken connections */
   for (i = 0; i < MAX_RPC_CONNECTION; i++)
      if (_client_connection[i].send_sock != 0 && _client_connection[i].connected) {
         int sock;
         fd_set readfds;
         struct timeval timeout;
         char buffer[64];
         int ok = 0;

         sock = _client_connection[i].send_sock;
         FD_ZERO(&readfds);
         FD_SET(sock, &readfds);

         timeout.tv_sec = 0;
         timeout.tv_usec = 0;

#ifdef OS_WINNT
         status = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);
#else
         do {
            status = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);
         } while (status == -1 && errno == EINTR); /* dont return if an alarm signal was cought */
#endif

         if (!FD_ISSET(sock, &readfds))
            continue;

         status = recv(sock, (char *) buffer, sizeof(buffer), MSG_PEEK);
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
                      "Connection to \"%s\" on host \"%s\" is broken, recv() errno %d (%s)",
                      _client_connection[i].client_name.c_str(),
                      _client_connection[i].host_name.c_str(),
                      errno, strerror(errno));
            }
         } else if (status == 0) {
            // connection closed by remote end without sending an EXIT message
            // this can happen if the remote end has crashed, so this message
            // is still necessary as a useful diagnostic for unexpected crashes
            // of midas programs. K.O.
            cm_msg(MINFO, "rpc_client_check",
                   "Connection to \"%s\" on host \"%s\" unexpectedly closed",
                   _client_connection[i].client_name.c_str(), _client_connection[i].host_name.c_str());
         } else {
            // read some data
            ok = 1;
            if (equal_ustring(buffer, "EXIT")) {
               /* normal exit */
               ok = 0;
            }
         }

         if (ok)
            continue;

         // connection lost, close the socket
         closesocket(sock);
         _client_connection[i].send_sock = 0;
      }
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
      cm_msg(MERROR, "rpc_server_connect", "cannot setsockopt(SOL_SOCKET, SO_SNDBUF), errno %d (%s)", errno,
             strerror(errno));

   /* send local computer info */
   std::string local_prog_name = rpc_get_name();
   hw_type = rpc_get_option(0, RPC_OHW_TYPE);
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
   INT i;

   if (hConn == -1) {
      /* close all open connections */
      for (i = MAX_RPC_CONNECTION - 1; i >= 0; i--)
         if (_client_connection[i].send_sock != 0)
            rpc_client_disconnect(i + 1, FALSE);

      /* close server connection from other clients */
      for (i = 0; i < MAX_RPC_CONNECTION; i++)
         if (_server_acception[i].recv_sock) {
            send(_server_acception[i].recv_sock, "EXIT", 5, 0);
            closesocket(_server_acception[i].recv_sock);
         }
   } else {
      /* notify server about exit */

      /* call exit and shutdown with RPC_NO_REPLY because client will exit immediately without possibility of replying */
      rpc_client_call(hConn, bShutdown ? (RPC_ID_SHUTDOWN | RPC_NO_REPLY) : (RPC_ID_EXIT | RPC_NO_REPLY));

      /* close socket */
      if (_client_connection[hConn - 1].send_sock)
         closesocket(_client_connection[hConn - 1].send_sock);

      _client_connection[hConn - 1].clear();
   }

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

static BOOL _mserver_mode = FALSE;

/********************************************************************/
INT rpc_set_mserver_mode(void)
/********************************************************************\

  Routine: rpc_set_mserver_mode

  Purpose: Set the RPC layer to mserver mode

  Function value:
    INT    RPC_SUCCESS

\********************************************************************/
{
   _mserver_mode = TRUE;
   return RPC_SUCCESS;
}

/********************************************************************/
INT rpc_is_mserver(void)
/********************************************************************\

  Routine: rpc_is_mserver

  Purpose: Return true if we are the mserver

  Input:
   none

  Output:
    none

  Function value:
    INT    RPC connection index

\********************************************************************/
{
   return _mserver_mode;
}

/********************************************************************/
INT rpc_get_option(HNDLE hConn, INT item)
/********************************************************************\

  Routine: rpc_get_option

  Purpose: Get actual RPC option

  Input:
    HNDLE hConn             RPC connection handle, -1 for server connection, -2 for rpc connect timeout

    INT   item              One of RPC_Oxxx

  Output:
    none

  Function value:
    INT                     Actual option

\********************************************************************/
{
   switch (item) {
      case RPC_OTIMEOUT:
         if (hConn == -1)
            return _server_connection.rpc_timeout;
         if (hConn == -2)
            return _rpc_connect_timeout;
         return _client_connection[hConn - 1].rpc_timeout;

      case RPC_OHW_TYPE: {
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

      default:
         cm_msg(MERROR, "rpc_get_option", "invalid argument");
         break;
   }

   return 0;
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
INT rpc_set_option(HNDLE hConn, INT item, INT value) {
   switch (item) {
      case RPC_OTIMEOUT:
         if (hConn == -1)
            _server_connection.rpc_timeout = value;
         else if (hConn == -2)
            _rpc_connect_timeout = value;
         else
            _client_connection[hConn - 1].rpc_timeout = value;
         break;

      case RPC_NODELAY:
         if (hConn == -1)
            setsockopt(_server_connection.send_sock, IPPROTO_TCP, TCP_NODELAY, (char *) &value, sizeof(value));
         else
            setsockopt(_client_connection[hConn - 1].send_sock, IPPROTO_TCP, TCP_NODELAY, (char *) &value,
                       sizeof(value));
         break;

      default:
         cm_msg(MERROR, "rpc_set_option", "invalid argument");
         break;
   }

   return 0;
}


/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/********************************************************************/
INT rpc_get_server_option(INT item)
/********************************************************************\

  Routine: rpc_get_server_option

  Purpose: Get actual RPC option for server connection

  Input:
    INT  item               One of RPC_Oxxx

  Output:
    none

  Function value:
    INT                     Actual option

\********************************************************************/
{
   INT i = 0;

   switch (item) {
      case RPC_CONVERT_FLAGS:
         return _server_acception[i].convert_flags;
      case RPC_ODB_HANDLE:
         return _server_acception[i].odb_handle;
      case RPC_CLIENT_HANDLE:
         return _server_acception[i].client_handle;
      case RPC_SEND_SOCK:
         return _server_acception[i].send_sock;
      case RPC_WATCHDOG_TIMEOUT:
         return _server_acception[i].watchdog_timeout;
   }

   return 0;
}


/********************************************************************/
INT rpc_set_server_option(INT item, INT value)
/********************************************************************\

  Routine: rpc_set_server_option

  Purpose: Set RPC option for server connection

  Input:
   INT  item               One of RPC_Oxxx
   INT  value              Value to set

  Output:
    none

  Function value:
    RPC_SUCCESS             Successful completion

\********************************************************************/
{
   INT i = 0;

   switch (item) {
      case RPC_CONVERT_FLAGS:
         _server_acception[i].convert_flags = value;
         break;
      case RPC_ODB_HANDLE:
         _server_acception[i].odb_handle = value;
         break;
      case RPC_CLIENT_HANDLE:
         _server_acception[i].client_handle = value;
         break;
      case RPC_WATCHDOG_TIMEOUT:
         _server_acception[i].watchdog_timeout = value;
         break;
   }

   return RPC_SUCCESS;
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
   va_list ap, aptmp;
   char arg[8], arg_tmp[8];
   INT i, status;
   INT param_size, arg_size, send_size;
   char *param_ptr;
   BOOL bpointer, bbig;
   DWORD rpc_status = 0;

   int idx = hConn - 1;

   if (_client_connection[idx].send_sock == 0) {
      cm_msg(MERROR, "rpc_client_call", "no rpc connection or invalid rpc connection handle %d", hConn);
      return RPC_NO_CONNECTION;
   }

   BOOL rpc_no_reply = routine_id & RPC_NO_REPLY;
   routine_id &= ~RPC_NO_REPLY;

   //if (rpc_no_reply)
   //   printf("rpc_client_call: routine_id %d, RPC_NO_REPLY\n", routine_id);

   int send_sock = _client_connection[idx].send_sock;
   int rpc_timeout = _client_connection[idx].rpc_timeout;

   // make local copy of the client name just in case _client_connection is erased by another thread

   const char *host_name = _client_connection[idx].host_name.c_str();
   const char *client_name = _client_connection[idx].client_name.c_str();

   /* find rpc_index */

   for (i = 0;; i++)
      if ((rpc_list[i].id == (int) routine_id) || (rpc_list[i].id == 0))
         break;

   int rpc_index = i;

   if (rpc_list[rpc_index].id == 0) {
      cm_msg(MERROR, "rpc_client_call", "call to \"%s\" on \"%s\" with invalid RPC ID %d", client_name, host_name,
             routine_id);
      return RPC_INVALID_ID;
   }

   const char *rpc_name = rpc_list[rpc_index].name;

   /* prepare output buffer */

   DWORD buf_size = sizeof(NET_COMMAND) + 1024;
   char *buf = (char *) malloc(buf_size);
   if (buf == NULL) {
      cm_msg(MERROR, "rpc_client_call",
             "call to \"%s\" on \"%s\" RPC \"%s\" cannot allocate %d bytes for transmit buffer", client_name, host_name,
             rpc_name, (int) buf_size);
      return RPC_NO_MEMORY;
   }

   NET_COMMAND *nc = (NET_COMMAND *) buf;
   nc->header.routine_id = routine_id;

   if (rpc_no_reply)
      nc->header.routine_id |= RPC_NO_REPLY;

   /* examine variable argument list and convert it to parameter array */
   va_start(ap, routine_id);

   /* find out if we are on a big endian system */
   bbig = ((rpc_get_option(0, RPC_OHW_TYPE) & DRI_BIG_ENDIAN) > 0);

   for (i = 0, param_ptr = nc->param; rpc_list[rpc_index].param[i].tid != 0; i++) {
      int tid = rpc_list[rpc_index].param[i].tid;
      int flags = rpc_list[rpc_index].param[i].flags;

      bpointer = (flags & RPC_POINTER) || (flags & RPC_OUT) ||
                 (flags & RPC_FIXARRAY) || (flags & RPC_VARARRAY) ||
                 tid == TID_STRING || tid == TID_ARRAY || tid == TID_STRUCT || tid == TID_LINK;

      int arg_type;

      if (bpointer)
         arg_type = TID_ARRAY;
      else
         arg_type = tid;

      /* floats are passed as doubles, at least under NT */
      if (tid == TID_FLOAT && !bpointer)
         arg_type = TID_DOUBLE;

      /* get pointer to argument */
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
         if (bpointer)
            arg_size = tid_size[tid];
         else
            arg_size = tid_size[arg_type];

         /* for strings, the argument size depends on the string length */
         if (tid == TID_STRING || tid == TID_LINK)
            arg_size = 1 + strlen((char *) *((char **) arg));

         /* for varibale length arrays, the size is given by
            the next parameter on the stack */
         if (flags & RPC_VARARRAY) {
            memcpy(&aptmp, &ap, sizeof(ap));
            rpc_va_arg(&aptmp, TID_ARRAY, arg_tmp);

            if (flags & RPC_OUT)
               arg_size = *((INT *) *((void **) arg_tmp));
            else
               arg_size = *((INT *) arg_tmp);

            *((INT *) param_ptr) = ALIGN8(arg_size);
            param_ptr += ALIGN8(sizeof(INT));
         }

         if (tid == TID_STRUCT || (flags & RPC_FIXARRAY))
            arg_size = rpc_list[rpc_index].param[i].n;

         /* always align parameter size */
         param_size = ALIGN8(arg_size);

         {
            size_t param_offset = (char *) param_ptr - (char *) nc;

            if (param_offset + param_size + 16 > buf_size) {
               size_t new_size = param_offset + param_size + 1024;
               buf = (char *) realloc(buf, new_size);
               if (buf == NULL) {
                  cm_msg(MERROR, "rpc_client_call",
                         "call to \"%s\" on \"%s\" RPC \"%s\" cannot resize the data buffer from %d bytes to %d bytes",
                         client_name, host_name, rpc_name, (int) buf_size, (int) new_size);
                  free(nc); // "nc" still points to the old value of "buf"
                  return RPC_NO_MEMORY;
               }
               buf_size = new_size;
               nc = (NET_COMMAND *) buf;
               param_ptr = buf + param_offset;
            }
         }

         if (bpointer)
            memcpy(param_ptr, (void *) *((void **) arg), arg_size);
         else {
            /* floats are passed as doubles on most systems */
            if (tid != TID_FLOAT)
               memcpy(param_ptr, arg, arg_size);
            else
               *((float *) param_ptr) = (float) *((double *) arg);
         }

         param_ptr += param_size;
      }
   }

   va_end(ap);

   nc->header.param_size = (POINTER_T) param_ptr - (POINTER_T) nc->param;

   send_size = nc->header.param_size + sizeof(NET_COMMAND_HEADER);

   /* in FAST TCP mode, only send call and return immediately */
   if (rpc_no_reply) {
      i = send_tcp(send_sock, (char *) nc, send_size, 0);

      if (i != send_size) {
         cm_msg(MERROR, "rpc_client_call", "call to \"%s\" on \"%s\" RPC \"%s\": send_tcp() failed", client_name,
                host_name, rpc_name);
         free(buf);
         return RPC_NET_ERROR;
      }

      free(buf);
      return RPC_SUCCESS;
   }

   /* in TCP mode, send and wait for reply on send socket */
   i = send_tcp(send_sock, (char *) nc, send_size, 0);
   if (i != send_size) {
      cm_msg(MERROR, "rpc_client_call", "call to \"%s\" on \"%s\" RPC \"%s\": send_tcp() failed", client_name,
             host_name, rpc_name);
      return RPC_NET_ERROR;
   }

   free(buf);
   buf = NULL;
   buf_size = 0;
   nc = NULL;

   bool restore_watchdog_timeout = false;
   BOOL watchdog_call;
   DWORD watchdog_timeout;
   cm_get_watchdog_params(&watchdog_call, &watchdog_timeout);

   //printf("watchdog timeout: %d, rpc_timeout: %d\n", watchdog_timeout, rpc_timeout);

   if (rpc_timeout >= (int) watchdog_timeout) {
      restore_watchdog_timeout = true;
      cm_set_watchdog_params(watchdog_call, rpc_timeout + 1000);
   }

   /* receive result on send socket */
   status = ss_recv_net_command(send_sock, &rpc_status, &buf_size, &buf, rpc_timeout);

   if (restore_watchdog_timeout) {
      cm_set_watchdog_params(watchdog_call, watchdog_timeout);
   }

   if (status == SS_TIMEOUT) {
      cm_msg(MERROR, "rpc_client_call", "call to \"%s\" on \"%s\" RPC \"%s\": timeout waiting for reply", client_name,
             host_name, rpc_name);
      if (buf)
         free(buf);
      return RPC_TIMEOUT;
   }

   if (status != SS_SUCCESS) {
      cm_msg(MERROR, "rpc_client_call", "call to \"%s\" on \"%s\" RPC \"%s\": error, ss_recv_net_command() status %d",
             client_name, host_name, rpc_name, status);
      if (buf)
         free(buf);
      return RPC_NET_ERROR;
   }

   /* extract result variables and place it to argument list */

   va_start(ap, routine_id);

   for (i = 0, param_ptr = buf; rpc_list[rpc_index].param[i].tid != 0; i++) {
      int tid = rpc_list[rpc_index].param[i].tid;
      int flags = rpc_list[rpc_index].param[i].flags;

      bpointer = (flags & RPC_POINTER) || (flags & RPC_OUT) ||
                 (flags & RPC_FIXARRAY) || (flags & RPC_VARARRAY) ||
                 tid == TID_STRING || tid == TID_ARRAY || tid == TID_STRUCT || tid == TID_LINK;

      int arg_type;

      if (bpointer)
         arg_type = TID_ARRAY;
      else
         arg_type = rpc_list[rpc_index].param[i].tid;

      if (tid == TID_FLOAT && !bpointer)
         arg_type = TID_DOUBLE;

      rpc_va_arg(&ap, arg_type, arg);

      if (rpc_list[rpc_index].param[i].flags & RPC_OUT) {

         if (param_ptr == NULL) {
            cm_msg(MERROR, "rpc_client_call",
                   "call to \"%s\" on \"%s\" RPC \"%s\": no data in RPC reply, needed to decode an RPC_OUT parameter. param_ptr is NULL",
                   client_name, host_name, rpc_name);
            rpc_status = RPC_NET_ERROR;
            break;
         }

         tid = rpc_list[rpc_index].param[i].tid;
         flags = rpc_list[rpc_index].param[i].flags;

         arg_size = tid_size[tid];

         if (tid == TID_STRING || tid == TID_LINK)
            arg_size = strlen((char *) (param_ptr)) + 1;

         if (flags & RPC_VARARRAY) {
            arg_size = *((INT *) param_ptr);
            param_ptr += ALIGN8(sizeof(INT));
         }

         if (tid == TID_STRUCT || (flags & RPC_FIXARRAY))
            arg_size = rpc_list[rpc_index].param[i].n;

         /* return parameters are always pointers */
         if (*((char **) arg))
            memcpy((void *) *((char **) arg), param_ptr, arg_size);

         /* parameter size is always aligned */
         param_size = ALIGN8(arg_size);

         param_ptr += param_size;
      }
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
   va_list ap, aptmp;
   char arg[8], arg_tmp[8];
   INT arg_type, rpc_timeout;
   INT i, idx, status;
   INT param_size, arg_size, send_size;
   INT tid, flags;
   char *param_ptr;
   BOOL bpointer, bbig;
   NET_COMMAND *nc;
   int send_sock;
   char *buf = NULL;
   DWORD buf_size = 0;
   DWORD rpc_status = 0;
   const char *rpc_name = NULL;

   BOOL rpc_no_reply = routine_id & RPC_NO_REPLY;
   routine_id &= ~RPC_NO_REPLY;

   //if (rpc_no_reply)
   //   printf("rpc_call: routine_id %d, RPC_NO_REPLY\n", routine_id);

   send_sock = _server_connection.send_sock;
   rpc_timeout = _server_connection.rpc_timeout;

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

   rpc_name = rpc_list[idx].name;

   /* prepare output buffer */

   buf_size = sizeof(NET_COMMAND) + 4 * 1024;
   buf = (char *) malloc(buf_size);
   if (buf == NULL) {
      ss_mutex_release(_mutex_rpc);
      cm_msg(MERROR, "rpc_call", "rpc \"%s\" cannot allocate %d bytes for transmit buffer", rpc_name, (int) buf_size);
      return RPC_NO_MEMORY;
   }

   nc = (NET_COMMAND *) buf;
   nc->header.routine_id = routine_id;

   if (rpc_no_reply)
      nc->header.routine_id |= RPC_NO_REPLY;

   /* examine variable argument list and convert it to parameter array */
   va_start(ap, routine_id);

   /* find out if we are on a big endian system */
   bbig = ((rpc_get_option(0, RPC_OHW_TYPE) & DRI_BIG_ENDIAN) > 0);

   for (i = 0, param_ptr = nc->param; rpc_list[idx].param[i].tid != 0; i++) {
      tid = rpc_list[idx].param[i].tid;
      flags = rpc_list[idx].param[i].flags;

      bpointer = (flags & RPC_POINTER) || (flags & RPC_OUT) ||
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
         if (bpointer)
            arg_size = tid_size[tid];
         else
            arg_size = tid_size[arg_type];

         /* for strings, the argument size depends on the string length */
         if (tid == TID_STRING || tid == TID_LINK)
            arg_size = 1 + strlen((char *) *((char **) arg));

         /* for varibale length arrays, the size is given by
            the next parameter on the stack */
         if (flags & RPC_VARARRAY) {
            memcpy(&aptmp, &ap, sizeof(ap));
            rpc_va_arg(&aptmp, TID_ARRAY, arg_tmp);

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
         param_size = ALIGN8(arg_size);

         {
            size_t param_offset = (char *) param_ptr - (char *) nc;

            if (param_offset + param_size + 16 > buf_size) {
               size_t new_size = param_offset + param_size + 1024;
               buf = (char *) realloc(buf, new_size);
               if (buf == NULL) {
                  ss_mutex_release(_mutex_rpc);
                  cm_msg(MERROR, "rpc_call", "rpc \"%s\" cannot resize the data buffer from %d bytes to %d bytes",
                         rpc_name, (int) buf_size, (int) new_size);
                  free(nc); // "nc" still points to the old value of "buf"
                  return RPC_NO_MEMORY;
               }
               buf_size = new_size;
               nc = (NET_COMMAND *) buf;
               param_ptr = buf + param_offset;
            }
         }

         if (bpointer)
            memcpy(param_ptr, (void *) *((void **) arg), arg_size);
         else {
            /* floats are passed as doubles on most systems */
            if (tid != TID_FLOAT)
               memcpy(param_ptr, arg, arg_size);
            else
               *((float *) param_ptr) = (float) *((double *) arg);
         }

         param_ptr += param_size;

      }
   }

   va_end(ap);

   nc->header.param_size = (POINTER_T) param_ptr - (POINTER_T) nc->param;

   send_size = nc->header.param_size + sizeof(NET_COMMAND_HEADER);

   /* do not wait for reply if requested RPC_NO_REPLY */
   if (rpc_no_reply) {
      i = send_tcp(send_sock, (char *) nc, send_size, 0);

      if (i != send_size) {
         ss_mutex_release(_mutex_rpc);
         cm_msg(MERROR, "rpc_call", "rpc \"%s\" error: send_tcp() failed", rpc_name);
         free(buf);
         return RPC_NET_ERROR;
      }

      ss_mutex_release(_mutex_rpc);
      free(buf);
      return RPC_SUCCESS;
   }

   /* in TCP mode, send and wait for reply on send socket */
   i = send_tcp(send_sock, (char *) nc, send_size, 0);
   if (i != send_size) {
      ss_mutex_release(_mutex_rpc);
      cm_msg(MERROR, "rpc_call", "rpc \"%s\" error: send_tcp() failed", rpc_name);
      free(buf);
      return RPC_NET_ERROR;
   }

   free(buf);
   buf = NULL;
   buf_size = 0;
   rpc_status = 0;
   nc = NULL;

   bool restore_watchdog_timeout = false;
   BOOL watchdog_call;
   DWORD watchdog_timeout;
   cm_get_watchdog_params(&watchdog_call, &watchdog_timeout);

   //printf("watchdog timeout: %d, rpc_timeout: %d\n", watchdog_timeout, rpc_timeout);

   if (!rpc_is_remote()) {
      // if RPC is remote, we are connected to an mserver,
      // the mserver takes care of watchdog timeouts.
      if (rpc_timeout >= (int) watchdog_timeout) {
         restore_watchdog_timeout = true;
         cm_set_watchdog_params(watchdog_call, rpc_timeout + 1000);
      }
   }

   status = ss_recv_net_command(send_sock, &rpc_status, &buf_size, &buf, rpc_timeout);

   if (restore_watchdog_timeout) {
      cm_set_watchdog_params(watchdog_call, watchdog_timeout);
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

   for (i = 0, param_ptr = buf; rpc_list[idx].param[i].tid != 0; i++) {
      tid = rpc_list[idx].param[i].tid;
      flags = rpc_list[idx].param[i].flags;

      bpointer = (flags & RPC_POINTER) || (flags & RPC_OUT) ||
                 (flags & RPC_FIXARRAY) || (flags & RPC_VARARRAY) ||
                 tid == TID_STRING || tid == TID_ARRAY || tid == TID_STRUCT || tid == TID_LINK;

      if (bpointer)
         arg_type = TID_ARRAY;
      else
         arg_type = rpc_list[idx].param[i].tid;

      if (tid == TID_FLOAT && !bpointer)
         arg_type = TID_DOUBLE;

      rpc_va_arg(&ap, arg_type, arg);

      if (rpc_list[idx].param[i].flags & RPC_OUT) {

         if (param_ptr == NULL) {
            cm_msg(MERROR, "rpc_call",
                   "routine \"%s\": no data in RPC reply, needed to decode an RPC_OUT parameter. param_ptr is NULL",
                   rpc_list[idx].name);
            rpc_status = RPC_NET_ERROR;
            break;
         }

         tid = rpc_list[idx].param[i].tid;
         arg_size = tid_size[tid];

         if (tid == TID_STRING || tid == TID_LINK)
            arg_size = strlen((char *) (param_ptr)) + 1;

         if (flags & RPC_VARARRAY) {
            arg_size = *((INT *) param_ptr);
            param_ptr += ALIGN8(sizeof(INT));
         }

         if (tid == TID_STRUCT || (flags & RPC_FIXARRAY))
            arg_size = rpc_list[idx].param[i].n;

         /* return parameters are always pointers */
         if (*((char **) arg))
            memcpy((void *) *((char **) arg), param_ptr, arg_size);

         /* parameter size is always aligned */
         param_size = ALIGN8(arg_size);

         param_ptr += param_size;
      }
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
INT rpc_send_event(INT buffer_handle, const EVENT_HEADER *event, INT buf_size, INT async_flag, INT mode) {
   INT i;
   NET_COMMAND *nc;
   unsigned long flag;
   BOOL would_block = 0;

   DWORD aligned_buf_size = ALIGN8(buf_size);

   int sock = -1;

   if (mode == 0)
      sock = _server_connection.send_sock;
   else
      sock = _server_connection.event_sock;

   _tcp_sock = sock; // remember socket for rpc_flush_event()

   if ((INT) aligned_buf_size != (INT) (ALIGN8(event->data_size + sizeof(EVENT_HEADER)))) {
      cm_msg(MERROR, "rpc_send_event", "event size mismatch");
      return BM_INVALID_PARAM;
   }

   if (!rpc_is_remote())
      return bm_send_event(buffer_handle, event, buf_size, async_flag);

   /* init network buffer */
   if (!_tcp_buffer)
      _tcp_buffer = (char *) M_MALLOC(NET_TCP_SIZE);
   if (!_tcp_buffer) {
      cm_msg(MERROR, "rpc_send_event", "not enough memory to allocate network buffer");
      return RPC_EXCEED_BUFFER;
   }

   /* check if not enough space in TCP buffer */
   if (aligned_buf_size + 4 * 8 + sizeof(NET_COMMAND_HEADER) >= (DWORD) (_opt_tcp_size - _tcp_wp)
       && _tcp_wp != _tcp_rp) {
      /* set socket to nonblocking IO */
      if (async_flag == BM_NO_WAIT) {
         flag = 1;
#ifdef OS_VXWORKS
         ioctlsocket(sock, FIONBIO, (int) &flag);
#else
         ioctlsocket(sock, FIONBIO, &flag);
#endif
      }

      i = send_tcp(sock, _tcp_buffer + _tcp_rp, _tcp_wp - _tcp_rp, 0);

      //printf("rpc_send_event: send %d\n", _tcp_wp-_tcp_rp);

      if (i < 0)
#ifdef OS_WINNT
         would_block = (WSAGetLastError() == WSAEWOULDBLOCK);
#else
         would_block = (errno == EWOULDBLOCK);
#endif

      /* set socket back to blocking IO */
      if (async_flag == BM_NO_WAIT) {
         flag = 0;
#ifdef OS_VXWORKS
         ioctlsocket(sock, FIONBIO, (int) &flag);
#else
         ioctlsocket(sock, FIONBIO, &flag);
#endif
      }

      /* increment read pointer */
      if (i > 0)
         _tcp_rp += i;

      /* check if whole buffer is sent */
      if (_tcp_rp == _tcp_wp)
         _tcp_rp = _tcp_wp = 0;

      if (i < 0 && !would_block) {
         cm_msg(MERROR, "rpc_send_event", "send_tcp() failed, return code = %d", i);
         return RPC_NET_ERROR;
      }

      /* return if buffer is not emptied */
      if (_tcp_wp > 0)
         return BM_ASYNC_RETURN;
   }

   if (mode == 0) {
      nc = (NET_COMMAND *) (_tcp_buffer + _tcp_wp);
      nc->header.routine_id = RPC_BM_SEND_EVENT | RPC_NO_REPLY;
      nc->header.param_size = 4 * 8 + aligned_buf_size;

      /* assemble parameters manually */
      *((INT *) (&nc->param[0])) = buffer_handle;
      *((INT *) (&nc->param[8])) = buf_size;

      /* send events larger than optimal buffer size directly */
      if (aligned_buf_size + 4 * 8 + sizeof(NET_COMMAND_HEADER) >= (DWORD) _opt_tcp_size) {
         /* send header */
         i = send_tcp(sock, _tcp_buffer + _tcp_wp, sizeof(NET_COMMAND_HEADER) + 16, 0);
         if (i <= 0) {
            cm_msg(MERROR, "rpc_send_event", "send_tcp() failed, return code = %d", i);
            return RPC_NET_ERROR;
         }

         /* send data */
         i = send_tcp(sock, (char *) event, aligned_buf_size, 0);
         if (i <= 0) {
            cm_msg(MERROR, "rpc_send_event", "send_tcp() failed, return code = %d", i);
            return RPC_NET_ERROR;
         }

         /* send last two parameters */
         *((INT *) (&nc->param[0])) = buf_size;
         *((INT *) (&nc->param[8])) = 0;
         i = send_tcp(sock, &nc->param[0], 16, 0);
         if (i <= 0) {
            cm_msg(MERROR, "rpc_send_event", "send_tcp() failed, return code = %d", i);
            return RPC_NET_ERROR;
         }
      } else {
         /* copy event */
         memcpy(&nc->param[16], event, buf_size);

         /* last two parameters (buf_size and async_flag */
         *((INT *) (&nc->param[16 + aligned_buf_size])) = buf_size;
         *((INT *) (&nc->param[24 + aligned_buf_size])) = 0;

         _tcp_wp += nc->header.param_size + sizeof(NET_COMMAND_HEADER);
      }

   } else {

      /* send events larger than optimal buffer size directly */
      if (aligned_buf_size + 4 * 8 + sizeof(INT) >= (DWORD) _opt_tcp_size) {
         /* send buffer */
         //printf("rpc_send_event: send %d (bh)\n", (int)sizeof(INT));
         i = send_tcp(sock, (char *) &buffer_handle, sizeof(INT), 0);
         if (i <= 0) {
            cm_msg(MERROR, "rpc_send_event", "send_tcp() failed, return code = %d", i);
            return RPC_NET_ERROR;
         }

         /* send data */
         //printf("rpc_send_event: send %d (aligned_buf_size)\n", aligned_buf_size);
         i = send_tcp(sock, (char *) event, aligned_buf_size, 0);
         if (i <= 0) {
            cm_msg(MERROR, "rpc_send_event", "send_tcp() failed, return code = %d", i);
            return RPC_NET_ERROR;
         }
      } else {
         /* copy event */
         *((INT *) (_tcp_buffer + _tcp_wp)) = buffer_handle;
         _tcp_wp += sizeof(INT);
         memcpy(_tcp_buffer + _tcp_wp, event, buf_size);

         _tcp_wp += aligned_buf_size;
      }
   }

   return RPC_SUCCESS;
}


/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/********************************************************************/
int rpc_get_send_sock()
/********************************************************************\

  Routine: rpc_get_send_sock

  Purpose: Return send socket to MIDAS server. Used by MFE.C for
           optimized event sending.

  Input:
    none

  Output:
    none

  Function value:
    int    socket

\********************************************************************/
{
   return _server_connection.send_sock;
}


/********************************************************************/
int rpc_get_event_sock()
/********************************************************************\

  Routine: rpc_get_event_sock

  Purpose: Return event send socket to MIDAS server. Used by MFE.C for
           optimized event sending.

  Input:
    none

  Output:
    none

  Function value:
    int    socket

\********************************************************************/
{
   return _server_connection.event_sock;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Send event residing in the TCP cache buffer filled by
           rpc_send_event. This routine should be called when a
           run is stopped.

@return RPC_SUCCESS, RPC_NET_ERROR
*/
INT rpc_flush_event() {
   if (!rpc_is_remote())
      return RPC_SUCCESS;

   /* return if rpc_send_event was not called */
   if (!_tcp_buffer || _tcp_wp == 0)
      return RPC_SUCCESS;

   /* empty TCP buffer */
   if (_tcp_wp > 0) {
      int to_send = _tcp_wp - _tcp_rp;
      //printf("rpc_flush_event: send %d\n", to_send);
      int i = send_tcp(_tcp_sock, _tcp_buffer + _tcp_rp, to_send, 0);

      if (i != to_send) {
         cm_msg(MERROR, "rpc_flush_event", "send_tcp(%d) returned %d, errno %d (%s)", to_send, i, errno,
                strerror(errno));
         return RPC_NET_ERROR;
      }
   }

   _tcp_rp = _tcp_wp = 0;

   return RPC_SUCCESS;
}

/********************************************************************/

typedef struct {
   int transition;
   int run_number;
   time_t trans_time;
   int sequence_number;
} TR_FIFO;

static TR_FIFO tr_fifo[10];
static int trf_wp, trf_rp;

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
   INT status, i;

   /* erase error string */
   *(CSTRING(2)) = 0;

   if (idx == RPC_RC_TRANSITION) {
      for (i = 0; i < MAX_TRANSITIONS; i++)
         if (_trans_table[i].transition == CINT(0) && _trans_table[i].sequence_number == CINT(4))
            break;

      /* call registerd function */
      if (i < MAX_TRANSITIONS) {
         if (_trans_table[i].func)
            /* execute callback if defined */
            status = _trans_table[i].func(CINT(1), CSTRING(2));
         else {
            /* store transition in FIFO */
            tr_fifo[trf_wp].transition = CINT(0);
            tr_fifo[trf_wp].run_number = CINT(1);
            tr_fifo[trf_wp].trans_time = time(NULL);
            tr_fifo[trf_wp].sequence_number = CINT(4);
            trf_wp = (trf_wp + 1) % 10;
            status = RPC_SUCCESS;
         }
      } else
         status = RPC_SUCCESS;

   } else {
      cm_msg(MERROR, "rpc_transition_dispatch", "received unrecognized command");
      status = RPC_INVALID_ID;
   }

   return status;
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

   if (trf_wp == trf_rp)
      return FALSE;

   if (transition)
      *transition = tr_fifo[trf_rp].transition;

   if (run_number)
      *run_number = tr_fifo[trf_rp].run_number;

   if (trans_time)
      *trans_time = (int) tr_fifo[trf_rp].trans_time;

   trf_rp = (trf_rp + 1) % 10;

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

   int sock = _server_acception[idx].recv_sock;

   if (!_server_acception[idx].net_buffer) {
      if (rpc_is_mserver())
         _server_acception[idx].net_buffer_size = NET_TCP_SIZE;
      else
         _server_acception[idx].net_buffer_size = NET_BUFFER_SIZE;

      _server_acception[idx].net_buffer = (char *) M_MALLOC(_server_acception[idx].net_buffer_size);
      _server_acception[idx].write_ptr = 0;
      _server_acception[idx].read_ptr = 0;
      _server_acception[idx].misalign = 0;
   }
   if (!_server_acception[idx].net_buffer) {
      cm_msg(MERROR, "recv_net_command", "Cannot allocate %d bytes for network buffer",
             _server_acception[idx].net_buffer_size);
      return -1;
   }

   int copied = 0;
   int param_size = -1;

   int write_ptr = _server_acception[idx].write_ptr;
   int read_ptr = _server_acception[idx].read_ptr;
   int misalign = _server_acception[idx].misalign;
   char *net_buffer = _server_acception[idx].net_buffer;

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

            if (_server_acception[idx].convert_flags)
               rpc_convert_single(&param_size, TID_UINT32, 0, _server_acception[idx].convert_flags);
         }

         //printf("recv_net_command: param_size %d, NET_COMMAND_HEADER %d, buffer_size %d\n", param_size, (int)sizeof(NET_COMMAND_HEADER), *pbufsize);

         /* check if parameters fit in buffer */
         if (*pbufsize < (param_size + (int) sizeof(NET_COMMAND_HEADER))) {
            int new_size = param_size + sizeof(NET_COMMAND_HEADER) + 1024;
            char *p = (char *) realloc(*pbuf, new_size);
            //printf("recv_net_command: reallocate buffer %d -> %d, %p\n", *pbufsize, new_size, p);
            if (p == NULL) {
               cm_msg(MERROR, "recv_net_command", "cannot reallocate buffer from %d bytes to %d bytes", *pbufsize,
                      new_size);
               _server_acception[idx].read_ptr = 0;
               _server_acception[idx].write_ptr = 0;
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
         write_ptr = recv(sock, net_buffer + misalign, _server_acception[idx].net_buffer_size - 8, 0);

         /* don't return if an alarm signal was cought */
      } while (write_ptr == -1 && errno == EINTR);
#else
      write_ptr = recv(sock, net_buffer + misalign, _server_acception[idx].net_buffer_size - 8, 0);
#endif

      /* abort if connection broken */
      if (write_ptr <= 0) {
         if (write_ptr == 0)
            cm_msg(MERROR, "recv_net_command", "rpc connection from \'%s\' on \'%s\' unexpectedly closed",
                   _server_acception[idx].prog_name.c_str(), _server_acception[idx].host_name.c_str());
         else
            cm_msg(MERROR, "recv_net_command", "recv() returned %d, errno: %d (%s)", write_ptr, errno,
                   strerror(errno));

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

   _server_acception[idx].write_ptr = write_ptr;
   _server_acception[idx].read_ptr = read_ptr;
   _server_acception[idx].misalign = misalign;

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
   INT idx;

   /* figure out to which connection socket belongs */
   for (idx = 0; idx < MAX_RPC_CONNECTION; idx++)
      if (_server_acception[idx].recv_sock == sock)
         break;

   return _server_acception[idx].write_ptr - _server_acception[idx].read_ptr;
}


/********************************************************************/
int recv_event_server_realloc(INT idx, char **pbuffer, int *pbuffer_size)
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
   RPC_SERVER_ACCEPTION *psa = &_server_acception[idx];
   psa->ev_write_ptr = 0;
   psa->ev_read_ptr = 0;

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

   /* check for sane event size */
   if (event_size <= 0 || total_size <= 0) {
      cm_msg(MERROR, "recv_event_server",
             "received event header with invalid data_size %d: event_size %d, total_size %d", pevent->data_size,
             event_size, total_size);
      return -1;
   }

   //printf("recv_event_server: idx %d, bh %d, event header: id %d, mask %d, serial %d, data_size %d, event_size %d, total_size %d\n", idx, *pbh, pevent->event_id, pevent->trigger_mask, pevent->serial_number, pevent->data_size, event_size, total_size);


   int bufsize = sizeof(INT) + event_size;

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

   int drd = recv_tcp2(sock, (*pbuffer) + rptr, to_read, 0);

   /* abort if connection broken */
   if (drd <= 0) {
      cm_msg(MERROR, "recv_event_server", "recv_tcp2(data) returned %d instead of %d", drd, to_read);
      return -1;
   }

   return bufsize;
}


/********************************************************************/
INT recv_event_check(int sock)
/********************************************************************\

  Routine: recv_event_check

  Purpose: Check if in TCP event receive buffer associated with sock
           is some data. Called by ss_suspend.

  Input:
    INT   sock               TCP receive socket

  Output:
    none

  Function value:
    INT   count              Number of bytes remaining in TCP buffer

\********************************************************************/
{
   INT idx;

   /* figure out to which connection socket belongs */
   for (idx = 0; idx < MAX_RPC_CONNECTION; idx++)
      if (_server_acception[idx].event_sock == sock)
         break;

   return _server_acception[idx].ev_write_ptr - _server_acception[idx].ev_read_ptr;
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
         param_size = ALIGN8(tid_size[tid]);

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
               rpc_convert_data(in_param_ptr, tid, flags, rpc_list[idx].param[i].n * tid_size[tid],
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
         param_size = ALIGN8(tid_size[tid]);

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
         param_size = ALIGN8(tid_size[tid]);

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
                                rpc_list[idx].param[i].n * tid_size[tid], convert_flags);
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
               for (idx = 0; idx < exptab.exptab.size(); idx++)
                  if (exptab.exptab[idx].name == callback.experiment) {
                     found = true;
                     break;
                  }
            }

            if (!found) {
               sprintf(str, "experiment \'%s\' not defined in exptab file \'%s\'\r", callback.experiment.c_str(), exptab.filename.c_str());
               cm_msg(MERROR, "rpc_server_accept", "%s", str);

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
   INT idx, i, status;
   //int version;
   unsigned int size;
   int sock;
   struct sockaddr_in acc_addr;
   INT client_hw_type = 0, hw_type;
   char str[100], client_program[NAME_LENGTH];
   char host_name[HOST_NAME_LENGTH];
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

   strcpy(host_name, "(unknown)");

   strcpy(client_program, "(unknown)");

   /* look for next free entry */
   for (idx = 0; idx < MAX_RPC_CONNECTION; idx++)
      if (_server_acception[idx].recv_sock == 0)
         break;
   if (idx == MAX_RPC_CONNECTION) {
      closesocket(sock);
      return RPC_NET_ERROR;
   }

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
      strlcpy(client_program, p, sizeof(client_program));
      p = strtok(NULL, " ");
   }
   if (p != NULL) {
      strlcpy(host_name, p, sizeof(host_name));
      p = strtok(NULL, " ");
   }

   //printf("rpc_client_accept: client_hw_type %d, version %d, client_name \'%s\', hostname \'%s\'\n", client_hw_type, version, client_program, host_name);

   /* save information in _server_acception structure */
   _server_acception[idx].recv_sock = sock;
   _server_acception[idx].send_sock = 0;
   _server_acception[idx].event_sock = 0;
   _server_acception[idx].remote_hw_type = client_hw_type;
   _server_acception[idx].host_name = host_name;
   _server_acception[idx].prog_name = client_program;
   _server_acception[idx].last_activity = ss_millitime();
   _server_acception[idx].watchdog_timeout = 0;
   _server_acception[idx].is_mserver = FALSE;

   /* send my own computer id */
   hw_type = rpc_get_option(0, RPC_OHW_TYPE);
   sprintf(str, "%d %s", hw_type, cm_get_version());
   status = send(sock, str, strlen(str) + 1, 0);
   if (status != (INT) strlen(str) + 1)
      return RPC_NET_ERROR;

   rpc_calc_convert_flags(hw_type, client_hw_type, &convert_flags);
   rpc_set_server_option(RPC_CONVERT_FLAGS, convert_flags);

   ss_suspend_set_server_acceptions_array(MAX_RPC_CONNECTION, _server_acception);

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
   INT idx, status;
   int recv_sock, send_sock, event_sock;
   struct sockaddr_in bind_addr;
   struct hostent *phe;
   char str[100], client_program[NAME_LENGTH];
   char host_name[HOST_NAME_LENGTH];
   INT client_hw_type, hw_type;
   INT convert_flags;
   char net_buffer[256];
   char *p;
   int flag;

   /* copy callback information */
   struct callback_addr callback(*pcallback);
   idx = callback.index;

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
      goto error;
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
      goto error;
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
      goto error;
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
      goto error;
   }
   //printf("rpc_server_callback: \'%s\'\n", net_buffer);

   /* get remote computer info */
   client_hw_type = strtoul(net_buffer, &p, 0);

   while (*p == ' ')
      p++;

   strlcpy(client_program, p, sizeof(client_program));

   //printf("hw type %d, name \'%s\'\n", client_hw_type, client_program);

   /* get the name of the remote host */
#ifdef OS_VXWORKS
   status = hostGetByAddr(bind_addr.sin_addr.s_addr, host_name);
   if (status != 0)
      strcpy(host_name, "unknown");
#else
   phe = gethostbyaddr((char *) &bind_addr.sin_addr, 4, PF_INET);
   if (phe == NULL)
      strcpy(host_name, "unknown");
   else
      strcpy(host_name, phe->h_name);
#endif

   /* save information in _server_acception structure */
   _server_acception[idx].recv_sock = recv_sock;
   _server_acception[idx].send_sock = send_sock;
   _server_acception[idx].event_sock = event_sock;
   _server_acception[idx].remote_hw_type = client_hw_type;
   _server_acception[idx].host_name = host_name;
   _server_acception[idx].prog_name = client_program;
   _server_acception[idx].last_activity = ss_millitime();
   _server_acception[idx].watchdog_timeout = 0;
   _server_acception[idx].is_mserver = TRUE;

   //printf("rpc_server_callback: _server_acception %p, idx %d\n", _server_acception, idx);

   /* send my own computer id */
   hw_type = rpc_get_option(0, RPC_OHW_TYPE);
   sprintf(str, "%d", hw_type);
   send(recv_sock, str, strlen(str) + 1, 0);

   rpc_calc_convert_flags(hw_type, client_hw_type, &convert_flags);
   rpc_set_server_option(RPC_CONVERT_FLAGS, convert_flags);

   ss_suspend_set_server_acceptions_array(MAX_RPC_CONNECTION, _server_acception);

   if (rpc_is_mserver()) {
      rpc_debug_printf("Connection to %s:%s established\n", _server_acception[idx].host_name.c_str(),
                       _server_acception[idx].prog_name.c_str());
   }

   return RPC_SUCCESS;

   error:

   closesocket(recv_sock);
   closesocket(send_sock);
   closesocket(event_sock);

   return RPC_NET_ERROR;
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
INT rpc_server_receive(INT idx, int sock, BOOL check)
/********************************************************************\

  Routine: rpc_server_receive

  Purpose: Receive rpc commands and execute them. Close the connection
           if client has broken TCP pipe.

  Input:
    INT    idx              Index to _server_acception structure in-
                            dicating which connection got data.
    int    sock             Socket which got data
    BOOL   check            If TRUE, only check if connection is
                            broken. This may be called via
                            bm_receive_event/ss_suspend(..,MSG_BM)

  Output:
    none

  Function value:
    RPC_SUCCESS             Successful completion
    RPC_EXCEED_BUFFER       Not enough memeory to allocate buffer
    SS_EXIT                 Server connection was closed
    SS_ABORT                Server connection was broken

\********************************************************************/
{
   INT status;

   /* only check if TCP connection is broken */
   if (check) {
      char test_buffer[256];
#ifdef OS_WINNT
      int n_received = recv(sock, test_buffer, sizeof(test_buffer), MSG_PEEK);
#else
      int n_received = recv(sock, test_buffer, sizeof(test_buffer), MSG_PEEK | MSG_DONTWAIT);

      /* check if we caught a signal */
      if ((n_received == -1) && (errno == EAGAIN))
         return SS_SUCCESS;
#endif

      if (n_received == -1)
         cm_msg(MERROR, "rpc_server_receive",
                "recv(%d,MSG_PEEK) returned %d, errno: %d (%s)", (int) sizeof(test_buffer),
                n_received, errno, strerror(errno));

      if (n_received <= 0)
         return SS_ABORT;

      return SS_SUCCESS;
   }

   /* receive command */
   if (sock == _server_acception[idx].recv_sock) {
      int remaining = 0;

      char *buf = NULL;
      int bufsize = 0;

      do {
         int n_received = recv_net_command_realloc(idx, &buf, &bufsize, &remaining);

         if (n_received <= 0) {
            status = SS_ABORT;
            cm_msg(MERROR, "rpc_server_receive", "recv_net_command() returned %d, abort", n_received);
            goto error;
         }

         status = rpc_execute(_server_acception[idx].recv_sock, buf, _server_acception[idx].convert_flags);

         if (status == SS_ABORT) {
            cm_msg(MERROR, "rpc_server_receive", "rpc_execute() returned %d, abort", status);
            goto error;
         }

         if (status == SS_EXIT || status == RPC_SHUTDOWN) {
            if (rpc_is_mserver())
               rpc_debug_printf("Connection to %s:%s closed\n", _server_acception[idx].host_name.c_str(),
                                _server_acception[idx].prog_name.c_str());
            goto exit;
         }

      } while (remaining);

      if (buf) {
         free(buf);
         buf = NULL;
         bufsize = 0;
      }
   } else {
      /* receive event */
      if (sock == _server_acception[idx].event_sock) {
         DWORD start_time = ss_millitime();

         char *buf = NULL;
         int bufsize = 0;

         do {
            int n_received = recv_event_server_realloc(idx, &buf, &bufsize);

            if (n_received < 0) {
               status = SS_ABORT;
               cm_msg(MERROR, "rpc_server_receive", "recv_event_server() returned %d, abort", n_received);
               goto error;
            }

            if (n_received == 0) {
               // no more data in the tcp socket
               break;
            }

            /* send event to buffer */
            INT *pbh = (INT *) buf;
            EVENT_HEADER *pevent = (EVENT_HEADER *) (pbh + 1);

            status = bm_send_event(*pbh, pevent, pevent->data_size + sizeof(EVENT_HEADER), BM_WAIT);
            if (status != BM_SUCCESS)
               cm_msg(MERROR, "rpc_server_receive", "bm_send_event() returned %d", status);

            /* repeat for maximum 0.5 sec */
         } while (ss_millitime() - start_time < 500);

         if (buf) {
            free(buf);
            buf = NULL;
            bufsize = 0;
         }
      }
   }

   return RPC_SUCCESS;

   error:

   {
      char str[80];
      strlcpy(str, _server_acception[idx].host_name.c_str(), sizeof(str));
      if (strchr(str, '.'))
         *strchr(str, '.') = 0;
      cm_msg(MTALK, "rpc_server_receive", "Program \'%s\' on host \'%s\' aborted",
             _server_acception[idx].prog_name.c_str(), str);
   }

   exit:

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

         if (_msg_mutex)
            ss_mutex_delete(_msg_mutex);
         _msg_mutex = 0;

         if (_msg_rb)
            rb_delete(_msg_rb);
         _msg_rb = 0;
      }
   }

   /* close server connection */
   if (_server_acception[idx].recv_sock)
      closesocket(_server_acception[idx].recv_sock);
   if (_server_acception[idx].send_sock)
      closesocket(_server_acception[idx].send_sock);
   if (_server_acception[idx].event_sock)
      closesocket(_server_acception[idx].event_sock);

   /* free TCP cache */
   M_FREE(_server_acception[idx].net_buffer);
   _server_acception[idx].net_buffer = NULL;

   /* mark this entry as invalid */
   _server_acception[idx].clear();

   /* signal caller a shutdonw */
   if (status == RPC_SHUTDOWN)
      return status;

   /* only the mserver should stop on server connection closure */
   if (!rpc_is_mserver()) {
      return SS_SUCCESS;
   }

   return status;
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
   INT i;
   struct linger ling;

   /* close all open connections */
   for (i = 0; i < MAX_RPC_CONNECTION; i++)
      if (_server_acception[i].recv_sock != 0) {
         /* lingering needed for PCTCP */
         ling.l_onoff = 1;
         ling.l_linger = 0;
         setsockopt(_server_acception[i].recv_sock, SOL_SOCKET, SO_LINGER, (char *) &ling, sizeof(ling));
         closesocket(_server_acception[i].recv_sock);

         if (_server_acception[i].send_sock) {
            setsockopt(_server_acception[i].send_sock, SOL_SOCKET, SO_LINGER, (char *) &ling, sizeof(ling));
            closesocket(_server_acception[i].send_sock);
         }

         if (_server_acception[i].event_sock) {
            setsockopt(_server_acception[i].event_sock, SOL_SOCKET, SO_LINGER, (char *) &ling, sizeof(ling));
            closesocket(_server_acception[i].event_sock);
         }

         _server_acception[i].recv_sock = 0;
         _server_acception[i].send_sock = 0;
         _server_acception[i].event_sock = 0;
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
   INT status, idx, i, convert_flags;
   NET_COMMAND nc;
   fd_set readfds;
   struct timeval timeout;

   for (idx = 0; idx < MAX_RPC_CONNECTION; idx++) {
      if (_server_acception[idx].recv_sock &&
          //_server_acception[idx].tid == ss_gettid() &&
          _server_acception[idx].watchdog_timeout &&
          (ss_millitime() - _server_acception[idx].last_activity >
           (DWORD) _server_acception[idx].watchdog_timeout)) {
/* printf("Send watchdog message to %s on %s\n",
                _server_acception[idx].prog_name,
                _server_acception[idx].host_name); */

         /* send a watchdog message */
         nc.header.routine_id = MSG_WATCHDOG;
         nc.header.param_size = 0;

         convert_flags = rpc_get_server_option(RPC_CONVERT_FLAGS);
         if (convert_flags) {
            rpc_convert_single(&nc.header.routine_id, TID_UINT32, RPC_OUTGOING, convert_flags);
            rpc_convert_single(&nc.header.param_size, TID_UINT32, RPC_OUTGOING, convert_flags);
         }

         /* send the header to the client */
         i = send_tcp(_server_acception[idx].send_sock, (char *) &nc, sizeof(NET_COMMAND_HEADER), 0);

         if (i < 0)
            goto exit;

         /* make some timeout checking */
         FD_ZERO(&readfds);
         FD_SET(_server_acception[idx].send_sock, &readfds);
         FD_SET(_server_acception[idx].recv_sock, &readfds);

         timeout.tv_sec = _server_acception[idx].watchdog_timeout / 1000;
         timeout.tv_usec = (_server_acception[idx].watchdog_timeout % 1000) * 1000;

         do {
            status = select(FD_SETSIZE, &readfds, NULL, NULL, &timeout);

            /* if an alarm signal was cought, restart select with reduced timeout */
            if (status == -1 && timeout.tv_sec >= WATCHDOG_INTERVAL / 1000)
               timeout.tv_sec -= WATCHDOG_INTERVAL / 1000;

         } while (status == -1);        /* dont return if an alarm signal was cought */

         if (!FD_ISSET(_server_acception[idx].send_sock, &readfds) &&
             !FD_ISSET(_server_acception[idx].recv_sock, &readfds))
            goto exit;

         /* receive result on send socket */
         if (FD_ISSET(_server_acception[idx].send_sock, &readfds)) {
            i = recv_tcp(_server_acception[idx].send_sock, (char *) &nc, sizeof(nc), 0);
            if (i <= 0)
               goto exit;
         }
      }
   }

   return RPC_SUCCESS;

   exit:

   cm_msg(MINFO, "rpc_check_channels", "client \"%s\" on host \"%s\" failed watchdog test after %d sec",
          _server_acception[idx].prog_name.c_str(),
          _server_acception[idx].host_name.c_str(),
          _server_acception[idx].watchdog_timeout / 1000);

   /* disconnect from experiment */
   if (rpc_is_mserver())
      cm_disconnect_experiment();

   /* close server connection */
   if (_server_acception[idx].recv_sock)
      closesocket(_server_acception[idx].recv_sock);
   if (_server_acception[idx].send_sock)
      closesocket(_server_acception[idx].send_sock);
   if (_server_acception[idx].event_sock)
      closesocket(_server_acception[idx].event_sock);

   /* free TCP cache */
   M_FREE(_server_acception[idx].net_buffer);
   _server_acception[idx].net_buffer = NULL;

   /* mark this entry as invalid */
   _server_acception[idx].clear();

   return RPC_NET_ERROR;
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
      BANK32A *pbk32a;

      pbk32a = (BANK32A *) ((char *) (((BANK_HEADER *) event) + 1) + ((BANK_HEADER *) event)->data_size);
      pbk32a->data_size = (DWORD) ((char *) pdata - (char *) (pbk32a + 1));
      if (pbk32a->type == TID_STRUCT && pbk32a->data_size == 0)
         printf("Warning: bank %c%c%c%c has zero size\n",
                pbk32a->name[0], pbk32a->name[1], pbk32a->name[2], pbk32a->name[3]);
      ((BANK_HEADER *) event)->data_size += sizeof(BANK32A) + ALIGN8(pbk32a->data_size);
      return pbk32a->data_size;
   } else if (bk_is32((BANK_HEADER *) event)) {
      BANK32 *pbk32;

      pbk32 = (BANK32 *) ((char *) (((BANK_HEADER *) event) + 1) + ((BANK_HEADER *) event)->data_size);
      pbk32->data_size = (DWORD) ((char *) pdata - (char *) (pbk32 + 1));
      if (pbk32->type == TID_STRUCT && pbk32->data_size == 0)
         printf("Warning: bank %c%c%c%c has zero size\n",
                pbk32->name[0], pbk32->name[1], pbk32->name[2], pbk32->name[3]);
      ((BANK_HEADER *) event)->data_size += sizeof(BANK32) + ALIGN8(pbk32->data_size);
      return pbk32->data_size;
   } else {
      BANK *pbk;

      pbk = (BANK *) ((char *) (((BANK_HEADER *) event) + 1) + ((BANK_HEADER *) event)->data_size);
      pbk->data_size = (WORD) ((char *) pdata - (char *) (pbk + 1));
      if (pbk->type == TID_STRUCT && pbk->data_size == 0)
         printf("Warning: bank %c%c%c%c has zero size\n", pbk->name[0], pbk->name[1], pbk->name[2],
                pbk->name[3]);
      ((BANK_HEADER *) event)->data_size += sizeof(BANK) + ALIGN8(pbk->data_size);
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

      rp = rb[h].rp;            // keep local copy, rb[h].rp might be changed by other thread

      /* check if enough size for wp >= rp without wrap-around */
      if (rb[h].wp >= rp
          && rb[h].wp + rb[h].max_event_size <= rb[h].buffer + rb[h].size - rb[h].max_event_size) {
         *p = rb[h].wp;
         return DB_SUCCESS;
      }

      /* check if enough size for wp >= rp with wrap-around */
      if (rb[h].wp >= rp && rb[h].wp + rb[h].max_event_size > rb[h].buffer + rb[h].size - rb[h].max_event_size &&
          rb[h].rp > rb[h].buffer) {    // next increment of wp wraps around, so need space at beginning
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

   if ((DWORD) size > rb[h].max_event_size)
      return DB_INVALID_PARAM;

   new_wp = rb[h].wp + size;

   /* wrap around wp if not enough space */
   if (new_wp > rb[h].buffer + rb[h].size - rb[h].max_event_size) {
      rb[h].ep = new_wp;
      new_wp = rb[h].buffer;
      assert(rb[h].rp != rb[h].buffer);
   }

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

   if (handle < 1 || handle > MAX_RING_BUFFER || rb[handle - 1].buffer == NULL)
      return DB_INVALID_HANDLE;

   h = handle - 1;

   if ((DWORD) size > rb[h].max_event_size)
      return DB_INVALID_PARAM;

   new_rp = rb[h].rp + size;

   /* wrap around if not enough space left */
   if (new_rp + rb[h].max_event_size > rb[h].buffer + rb[h].size)
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

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
