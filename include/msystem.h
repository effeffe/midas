/********************************************************************\

  Name:         MSYSTEM.H
  Created by:   Stefan Ritt

  Contents:     Function declarations and constants for internal
                routines

  $Id$

\********************************************************************/

#ifndef _MSYSTEM_H
#define _MSYSTEM_H

/**dox***************************************************************/
/** @file msystem.h
The Midas System include file
*/

/** @defgroup msdefineh System Defines
 */
/** @defgroup msmacroh System Macros
 */
/** @defgroup mssectionh System Structure Declarations
 */

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

#include "midasinc.h"
#include <string> // std::string
#include <mutex>  // std::mutex

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/**dox***************************************************************/
/** @addtogroup msdefineh
 *  @{  */

/**
data representations
*/
#define DRI_16              (1<<0)  /**< - */
#define DRI_32              (1<<1)  /**< - */
#define DRI_64              (1<<2)  /**< - */
#define DRI_LITTLE_ENDIAN   (1<<3)  /**< - */
#define DRI_BIG_ENDIAN      (1<<4)  /**< - */
#define DRF_IEEE            (1<<5)  /**< - */
#define DRF_G_FLOAT         (1<<6)  /**< - */
//#define DR_ASCII            (1<<7)  /**< - */

/**dox***************************************************************/
/** @} *//* end of msdefineh */

/**dox***************************************************************/
/** @addtogroup msmacroh
 *  @{  */

/* Byte and Word swapping big endian <-> little endian */
/**
SWAP WORD macro */
#ifndef WORD_SWAP
#define WORD_SWAP(x) { BYTE _tmp;                               \
                       _tmp= *((BYTE *)(x));                    \
                       *((BYTE *)(x)) = *(((BYTE *)(x))+1);     \
                       *(((BYTE *)(x))+1) = _tmp; }
#endif

/**
SWAP DWORD macro */
#ifndef DWORD_SWAP
#define DWORD_SWAP(x) { BYTE _tmp;                              \
                       _tmp= *((BYTE *)(x));                    \
                       *((BYTE *)(x)) = *(((BYTE *)(x))+3);     \
                       *(((BYTE *)(x))+3) = _tmp;               \
                       _tmp= *(((BYTE *)(x))+1);                \
                       *(((BYTE *)(x))+1) = *(((BYTE *)(x))+2); \
                       *(((BYTE *)(x))+2) = _tmp; }
#endif

/**
SWAP QWORD macro */
#ifndef QWORD_SWAP
#define QWORD_SWAP(x) { BYTE _tmp;                              \
                       _tmp= *((BYTE *)(x));                    \
                       *((BYTE *)(x)) = *(((BYTE *)(x))+7);     \
                       *(((BYTE *)(x))+7) = _tmp;               \
                       _tmp= *(((BYTE *)(x))+1);                \
                       *(((BYTE *)(x))+1) = *(((BYTE *)(x))+6); \
                       *(((BYTE *)(x))+6) = _tmp;               \
                       _tmp= *(((BYTE *)(x))+2);                \
                       *(((BYTE *)(x))+2) = *(((BYTE *)(x))+5); \
                       *(((BYTE *)(x))+5) = _tmp;               \
                       _tmp= *(((BYTE *)(x))+3);                \
                       *(((BYTE *)(x))+3) = *(((BYTE *)(x))+4); \
                       *(((BYTE *)(x))+4) = _tmp; }
#endif

/**dox***************************************************************/
/** @} *//* end of msmacroh */

/**dox***************************************************************/
/** @addtogroup msdefineh
 *  @{  */

/**
Definition of implementation specific constants */
#define MESSAGE_BUFFER_SIZE    100000   /**< buffer used for messages */
#define MESSAGE_BUFFER_NAME    "SYSMSG" /**< buffer name for messages */
//#define MAX_RPC_CONNECTION     64       /**< server/client connections   */
#define MAX_STRING_LENGTH      256      /**< max string length for odb */
#define NET_BUFFER_SIZE        (8*1024*1024) /**< size of network receive buffers */

/*------------------------------------------------------------------*/
/* flag for conditional compilation of debug messages */
#undef  DEBUG_MSG

/* flag for local routines (not for pure network clients) */
#if !defined ( OS_MSDOS ) && !defined ( OS_VXWORKS )
#define LOCAL_ROUTINES
#endif

#if defined(NO_LOCAL_ROUTINES)
#undef LOCAL_ROUTINES
#endif

/* YBOS support not in MSDOS */
#if !defined ( OS_MSDOS )
#define YBOS_SUPPORT
#endif

/** @} */

/*------------------------------------------------------------------*/

/* Mapping of function names for socket operations */

#ifdef OS_MSDOS

#define closesocket(s) ::close(s)
#define ioctlsocket(s,c,d) ::ioctl(s,c,d)
#define malloc(i) farmalloc(i)

#undef NET_TCP_SIZE
#define NET_TCP_SIZE 0x7FFF

#endif /* OS_MSDOS */

#ifdef OS_VMS

#define closesocket(s) ::close(s)
#define ioctlsocket(s,c,d)

#ifndef FD_SET
typedef struct {
   INT fds_bits;
} fd_set;

#define FD_SET(n, p)    ((p)->fds_bits |= (1 << (n)))
#define FD_CLR(n, p)    ((p)->fds_bits &= ~(1 << (n)))
#define FD_ISSET(n, p)  ((p)->fds_bits & (1 << (n)))
#define FD_ZERO(p)      ((p)->fds_bits = 0)
#endif /* FD_SET */

#endif /* OS_VMS */

/* Missing #defines in VMS */

#ifdef OS_VMS

#define P_WAIT   0
#define P_NOWAIT 1
#define P_DETACH 4

#endif

/* and for UNIX */

#ifdef OS_UNIX

#define closesocket(s) ::close(s)
#define ioctlsocket(s,c,d) ::ioctl(s,c,d)
#ifndef stricmp
#define stricmp(s1, s2) strcasecmp(s1, s2)
#endif

#define P_WAIT   0
#define P_NOWAIT 1
#define P_DETACH 4

#endif

/** @addtogroup msdefineh
 *  @{  */

#ifndef FD_SETSIZE
#define FD_SETSIZE 32
#endif

/** @} */

/* and VXWORKS */

#ifdef OS_VXWORKS

#define P_NOWAIT 1
#define closesocket(s) ::close(s)
#define ioctlsocket(s,c,d) ::ioctl(s,c,d)

#endif

/** @addtogroup msdefineh
 *  @{  */

/* missing O_BINARY for non-PC */
#ifndef O_BINARY
#define O_BINARY 0
#define O_TEXT   0
#endif

/** @} */

/**dox***************************************************************/
/** @addtogroup msmacroh
 *  @{  */

/* min/max/abs macros */
#ifndef MAX
#define MAX(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a,b)            (((a) < (b)) ? (a) : (b))
#endif

#ifndef ABS
#define ABS(a)              (((a) < 0)   ? -(a) : (a))
#endif

/* missing tell() for some operating systems */
#define TELL(fh) lseek(fh, 0, SEEK_CUR)

/* define file truncation */
#ifdef OS_WINNT
#define TRUNCATE(fh) chsize(fh, TELL(fh))
#else
#define TRUNCATE(fh) ftruncate(fh, TELL(fh))
#endif

/** @} */

/* missing isnan() & co under Windows */
#ifdef OS_WINNT
#include <float.h>
#define isnan(x) _isnan(x)
#define isinf(x) (!_finite(x))
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define ftruncate(x,y) _chsize(x,y)
#endif

/*------------------------------------------------------------------*/

/**dox***************************************************************/
/** @addtogroup mssectionh
 *  @{  */

/* Network structures */

typedef struct {
   DWORD routine_id;            /* routine ID like ID_BM_xxx    */
   DWORD param_size;            /* size in Bytes of parameter   */
} NET_COMMAND_HEADER;

typedef struct {
   NET_COMMAND_HEADER header;
   char param[32];              /* parameter array              */
} NET_COMMAND;

/** @} *//* end of mssectionh */

/** @addtogroup msdefineh
 *  @{  */

#define MSG_BM       1
#define MSG_ODB      2
#define MSG_CLIENT   3
#define MSG_SERVER   4
#define MSG_LISTEN   5
#define MSG_WATCHDOG 6

/** @} */

/**dox***************************************************************/
/** @addtogroup mssectionh
 *  @{  */

/* RPC structures */

struct callback_addr {
   std::string host_name;
   unsigned short host_port1;
   unsigned short host_port2;
   unsigned short host_port3;
   int debug;
   std::string experiment;
   std::string directory;
   std::string user;

   void clear() {
      host_name = "";
      host_port1 = 0;
      host_port2 = 0;
      host_port3 = 0;
      debug = 0;
      experiment = "";
      directory = "";
      user = "";
   }
};

typedef struct rpc_server_connection_struct {
   std::string host_name;       /*  server name        */
   INT port;                    /*  ip port                 */
   std::string exp_name;        /*  experiment to connect   */
   int send_sock;               /*  tcp send socket         */
   int recv_sock;               /*  tcp receive socket      */
   int event_sock;              /*  event socket            */
   std::mutex event_sock_mutex; /*  protect event socket against multithreaded access */
   INT remote_hw_type;          /*  remote hardware type    */
   INT rpc_timeout;             /*  in milliseconds         */

   void clear() {
      host_name = "";
      port = 0;
      exp_name = "";
      send_sock = 0;
      recv_sock = 0;
      event_sock = 0;
      remote_hw_type = 0;
      rpc_timeout = 0;
   }
} RPC_SERVER_CONNECTION;

typedef struct rpc_server_acception_struct {
   std::string prog_name;           /*  client program name     */
   std::string host_name;           /*  client name             */
   BOOL is_mserver = 0;             /*  this is an mserver server-side connection */
   int send_sock = 0;               /*  tcp send socket         */
   int recv_sock = 0;               /*  tcp receive socket      */
   int event_sock = 0;              /*  tcp event socket        */
   INT remote_hw_type = 0;          /*  hardware type           */
   INT watchdog_timeout = 0;        /*  in milliseconds         */
   DWORD last_activity = 0;         /*  time of last recv       */
   INT convert_flags = 0;           /*  convertion flags        */
   char *net_buffer = NULL;         /*  TCP cache buffer        */
   INT net_buffer_size = 0;         /*  size of TCP cache       */
   INT write_ptr=0, read_ptr=0, misalign=0;   /* pointers for cache */
   HNDLE odb_handle = 0;            /*  handle to online datab. */
   HNDLE client_handle = 0;         /*  client key handle .     */

   void clear() {
      prog_name = "";
      host_name = "";
      is_mserver = FALSE;
      send_sock = 0;
      recv_sock = 0;
      event_sock = 0;
      remote_hw_type = 0;
      watchdog_timeout = 0;
      last_activity = 0;
      convert_flags = 0;
      net_buffer = NULL;
      net_buffer_size = 0;
      write_ptr = 0;
      read_ptr = 0;
      misalign = 0;
      odb_handle = 0;
      client_handle = 0;
   }

   void close();
} RPC_SERVER_ACCEPTION;

typedef std::vector<RPC_SERVER_ACCEPTION*> RPC_SERVER_ACCEPTION_LIST;


typedef struct {
   INT size;                          /**< size in bytes              */
   INT next_free;                     /**< Address of next free block */
} FREE_DESCRIP;

typedef struct {
   INT handle;                        /**< Handle of record base key  */
   WORD access_mode;                  /**< R/W flags                  */
   WORD flags;                        /**< Data format, ...           */

} OPEN_RECORD;

typedef struct {
   char name[NAME_LENGTH];      /* name of client             */
   INT pid;                     /* process ID                 */
   INT unused0;                 /* was thread ID              */
   INT unused;                  /* was thread handle          */
   INT port;                    /* UDP port for wake up       */
   INT num_open_records;        /* number of open records     */
   DWORD last_activity;         /* time of last activity      */
   DWORD watchdog_timeout;      /* timeout in ms              */
   INT max_index;               /* index of last opren record */

   OPEN_RECORD open_record[MAX_OPEN_RECORDS];

} DATABASE_CLIENT;

typedef struct {
   char name[NAME_LENGTH];      /* name of database           */
   INT version;                 /* database version           */
   INT num_clients;             /* no of active clients       */
   INT max_client_index;        /* index of last client + 1   */
   INT key_size;                /* size of key area in bytes  */
   INT data_size;               /* size of data area in bytes */
   INT root_key;                /* root key offset            */
   INT first_free_key;          /* first free key memory      */
   INT first_free_data;         /* first free data memory     */

   DATABASE_CLIENT client[MAX_CLIENTS]; /* entries for clients        */

} DATABASE_HEADER;

/* Per-process buffer access structure (descriptor) */

typedef struct {
   char name[NAME_LENGTH];      /* Name of database             */
   BOOL attached;               /* TRUE if database is attached */
   INT client_index;            /* index to CLIENT str. in buf. */
   DATABASE_HEADER *database_header;    /* pointer to database header   */
   void *database_data;         /* pointer to database data     */
   HNDLE semaphore;             /* semaphore handle             */
   INT lock_cnt;                /* flag to avoid multiple locks */
   void* shm_adr;               /* address of shared memory     */
   HNDLE shm_size;              /* size of shared memory        */
   HNDLE shm_handle;            /* handle (id) to shared memory */
   BOOL protect;                /* read/write protection        */
   BOOL protect_read;           /* read is permitted            */
   BOOL protect_write;          /* write is permitted           */
   MUTEX_T *mutex;              /* mutex for multi-thread access */
   INT timeout;                 /* timeout for mutex and semaphore */
   BOOL inside_lock_unlock;     /* protection against recursive call to db_lock/unlock */

} DATABASE;

/* Open record descriptor */

typedef struct {
   HNDLE handle;                /* Handle of record base key  */
   HNDLE hDB;                   /* Handle of record's database */
   WORD access_mode;            /* R/W flags                  */
   void *data;                  /* Pointer to local data      */
   void *copy;                  /* Pointer of copy to data    */
   INT buf_size;                /* Record size in bytes       */
   void (*dispatcher) (INT, INT, void *);       /* Pointer to dispatcher func. */
   void *info;                  /* addtl. info for dispatcher */

} RECORD_LIST;

/* Watch record descriptor */

typedef struct {
   HNDLE handle;                /* Handle of watched base key  */
   HNDLE hDB;                   /* Handle of watched database */
   void (*dispatcher) (INT, INT, INT, void* info); /* Pointer to dispatcher func. */
   void* info;                  /* addtl. info for dispatcher */
} WATCH_LIST;

/* Event request descriptor */

typedef struct {
   INT buffer_handle;           /* Buffer handle */
   short int event_id;          /* same as in EVENT_HEADER */
   short int trigger_mask;
   EVENT_HANDLER* dispatcher;   /* Dispatcher func. */

} REQUEST_LIST;

/**dox***************************************************************/
/** @} *//* end of mssectionh */

/** @addtogroup msdefineh
 *  @{  */

/*---- Logging channel information ---------------------------------*/

#define LOG_TYPE_DISK 1
#define LOG_TYPE_TAPE 2
#define LOG_TYPE_FTP  3
#define LOG_TYPE_SFTP 4

/** @} */

#if defined(OS_VXWORKS)

/*---- VxWorks specific taskSpawn arguments ----------------------*/

typedef struct {
   char name[32];
   int priority;
   int options;
   int stackSize;
   int arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10;
} VX_TASK_SPAWN;

#endif

/*---- Function declarations ---------------------------------------*/

   /*---- common function ----*/
   INT EXPRT cm_set_path(const char *path);
   INT EXPRT cm_get_path(char *path, int path_size);
   std::string EXPRT cm_get_path();
   INT EXPRT cm_get_path_string(std::string* path);
   INT EXPRT cm_set_experiment_name(const char *name);
   INT cm_dispatch_ipc(const char *message, int message_size, int client_socket);
   INT EXPRT cm_msg_log(INT message_type, const char *facility, const char *message);
   void EXPRT name2c(char *str);
   INT cm_delete_client_info(HNDLE hDB, INT pid);
   std::string EXPRT cm_get_history_path(const char* history_channel);

   /*---- online database ----*/
   INT EXPRT db_lock_database(HNDLE database_handle);
   INT EXPRT db_unlock_database(HNDLE database_handle);
   //INT EXPRT db_get_lock_cnt(HNDLE database_handle);
   INT EXPRT db_set_lock_timeout(HNDLE database_handle, int timeout_millisec);
   INT db_update_record_local(INT hDB, INT hKeyRoot, INT hKey, int index);
   INT db_update_record_mserver(INT hDB, INT hKeyRoot, INT hKey, int index, int client_socket);
   INT db_close_all_records(void);
   INT EXPRT db_flush_database(HNDLE hDB);
   INT EXPRT db_notify_clients(HNDLE hDB, HNDLE hKey, int index, BOOL bWalk);
   INT EXPRT db_set_client_name(HNDLE hDB, const char *client_name);
   INT db_delete_key1(HNDLE hDB, HNDLE hKey, INT level, BOOL follow_links);
   INT EXPRT db_show_mem(HNDLE hDB, char **result, BOOL verbose);
   INT EXPRT db_get_free_mem(HNDLE hDB, INT *key_size, INT *data_size);
   INT db_allow_write_locked(DATABASE* p, const char* caller_name);
   //void db_update_last_activity(DWORD actual_time);
   void db_cleanup(const char *who, DWORD actual_time, BOOL wrong_interval);
   void db_cleanup2(const char* client_name, int ignore_timeout, DWORD actual_time,  const char *who);
   void db_set_watchdog_params(DWORD timeout);
   INT db_check_client(HNDLE hDB, HNDLE hKeyClient);

   /*---- rpc functions -----*/
   INT rpc_register_listener(int port, RPC_HANDLER func, int *plsock, int *pport);
   RPC_LIST EXPRT *rpc_get_internal_list(INT flag);
   INT rpc_server_receive_rpc(int idx, RPC_SERVER_ACCEPTION* sa);
   INT rpc_server_receive_event(int idx, RPC_SERVER_ACCEPTION* sa, int timeout_msec);
   INT rpc_server_callback(struct callback_addr *callback);
   INT EXPRT rpc_server_accept(int sock);
   INT rpc_client_accept(int sock);
   INT rpc_client_dispatch(int sock);
   INT EXPRT rpc_get_convert_flags(void);
   INT recv_tcp_check(int sock);
   INT rpc_deregister_functions(void);
   INT rpc_check_channels(void);
   void EXPRT rpc_client_check(void);
   INT rpc_server_disconnect(void);
   INT EXPRT rpc_set_opt_tcp_size(INT tcp_size);
   INT EXPRT rpc_get_opt_tcp_size(void);
   INT EXPRT rpc_set_mserver_path(const char *mserver_path);
   const char* EXPRT rpc_get_mserver_path(void);
   RPC_SERVER_ACCEPTION* rpc_get_mserver_acception(void);

   /** @addtogroup msfunctionc */
   /** @{ */

   /*---- system services ----*/
   INT ss_shm_open(const char *name, INT size, void **shm_adr, size_t *shm_size, HNDLE *handle, BOOL get_size);
   INT ss_shm_close(const char *name, void *shm_adr, size_t shm_size, HNDLE handle, INT destroy_flag);
   INT ss_shm_flush(const char *name, const void *shm_adr, size_t shm_size, HNDLE handle);
   INT EXPRT ss_shm_delete(const char *name);
   INT ss_shm_protect(HNDLE handle, void *shm_adr, size_t shm_size);
   INT ss_shm_unprotect(HNDLE handle, void **shm_adr, size_t shm_size, BOOL read, BOOL write, const char* caller_name);
   INT ss_spawnv(INT mode, const char *cmdname, const char* const argv[]);
   INT ss_shell(int sock);
   INT EXPRT ss_daemon_init(BOOL keep_stdout);
   INT EXPRT ss_system(const char *command);
   INT EXPRT ss_exec(const char *cmd, INT * child_pid);
   BOOL EXPRT ss_existpid(INT pid);
   INT EXPRT ss_getpid(void);
   midas_thread_t EXPRT ss_gettid(void);
   std::string ss_tid_to_string(midas_thread_t thread_id);
   INT EXPRT ss_semaphore_create(const char *semaphore_name, HNDLE * semaphore_handle);
   INT EXPRT ss_semaphore_wait_for(HNDLE semaphore_handle, INT timeout);
   INT EXPRT ss_semaphore_release(HNDLE semaphore_handle);
   INT EXPRT ss_semaphore_delete(HNDLE semaphore_handle, INT destroy_flag);
   INT EXPRT ss_mutex_create(MUTEX_T **mutex, BOOL recursive);
   INT EXPRT ss_mutex_wait_for(MUTEX_T *mutex, INT timeout);
   INT EXPRT ss_mutex_release(MUTEX_T *mutex);
   INT EXPRT ss_mutex_delete(MUTEX_T *mutex);
   INT ss_alarm(INT millitime, void (*func) (int));
   INT ss_suspend_init_odb_port();
   INT ss_suspend_get_odb_port(INT * port);
   INT ss_suspend_get_buffer_port(midas_thread_t thread_id, INT * port);
   INT ss_suspend_set_rpc_thread(midas_thread_t thread_id);
   INT ss_suspend_set_server_listener(int listen_socket);
   INT ss_suspend_set_client_listener(int listen_socket);
   INT ss_suspend_set_client_connection(RPC_SERVER_CONNECTION* connection);
   INT ss_suspend_set_server_acceptions(RPC_SERVER_ACCEPTION_LIST* acceptions);
   INT ss_resume(INT port, const char *message);
   INT ss_suspend_exit(void);
   INT ss_exception_handler(void (*func) (void));
   INT EXPRT ss_suspend(INT millisec, INT msg);
   midas_thread_t EXPRT ss_thread_create(INT(*func) (void *), void *param);
   INT EXPRT ss_thread_kill(midas_thread_t thread_id);
   INT EXPRT ss_thread_set_name(std::string name);
   std::string ss_thread_get_name();
   INT EXPRT ss_get_struct_align(void);
   INT EXPRT ss_get_struct_padding(void);
   INT EXPRT ss_timezone(void);
   INT EXPRT ss_stack_get(char ***string);
   void EXPRT ss_stack_print(void);
   void EXPRT ss_stack_history_entry(char *tag);
   void EXPRT ss_stack_history_dump(char *filename);
   INT ss_gethostname(char* buffer, int buffer_size);
   std::string ss_gethostname();
   BOOL ss_pid_exists(int pid);
   void ss_kill(int pid);

   /*---- socket routines ----*/
   INT EXPRT send_tcp(int sock, char *buffer, DWORD buffer_size, INT flags);
   INT EXPRT recv_tcp(int sock, char *buffer, DWORD buffer_size, INT flags);
   INT EXPRT recv_tcp2(int sock, char *buffer, int buffer_size, int timeout_ms);
   INT EXPRT recv_string(int sock, char *buffer, DWORD buffer_size, INT flags);
   INT EXPRT ss_socket_wait(int sock, int millisec);
   INT EXPRT ss_recv_net_command(int sock, DWORD* routine_id, DWORD* param_size, char **param_ptr, int timeout_ms);

   /*---- mserver event socket ----*/
   bool ss_event_socket_has_data();
   int  rpc_flush_event_socket(int timeout_msec);

   /** @} */

   /*---- event buffer routines ----*/
   INT EXPRT eb_create_buffer(INT size);
   INT EXPRT eb_free_buffer(void);
   BOOL EXPRT eb_buffer_full(void);
   BOOL EXPRT eb_buffer_empty(void);
   EVENT_HEADER EXPRT *eb_get_pointer(void);
   INT EXPRT eb_increment_pointer(INT buffer_handle, INT event_size);
   INT EXPRT eb_send_events(BOOL send_all);

   /*---- dual memory event buffer routines ----*/
   INT EXPRT dm_buffer_create(INT size, INT usize);
   INT EXPRT dm_buffer_release(void);
   BOOL EXPRT dm_area_full(void);
   EVENT_HEADER EXPRT *dm_pointer_get(void);
   INT EXPRT dm_pointer_increment(INT buffer_handle, INT event_size);
   INT EXPRT dm_area_send(void);
   INT EXPRT dm_area_flush(void);
   INT EXPRT dm_task(void *pointer);
   DWORD EXPRT dm_buffer_time_get(void);
   INT EXPRT dm_async_area_send(void *pointer);

   /*---- ring buffer routines ----*/
   int EXPRT rb_set_nonblocking(void);
   int EXPRT rb_create(int size, int max_event_size, int *ring_buffer_handle);
   int EXPRT rb_delete(int ring_buffer_handle);
   int EXPRT rb_get_wp(int handle, void **p, int millisec);
   int EXPRT rb_increment_wp(int handle, int size);
   int EXPRT rb_get_rp(int handle, void **p, int millisec);
   int EXPRT rb_increment_rp(int handle, int size);
   int EXPRT rb_get_buffer_level(int handle, int * n_bytes);

   /*---- stop watch ----*/
   std::chrono::time_point<std::chrono::high_resolution_clock> ss_us_start();
   unsigned int ss_us_since(std::chrono::time_point<std::chrono::high_resolution_clock> start);

/*---- Include RPC identifiers -------------------------------------*/

#include "mrpc.h"

#endif /* _MSYSTEM_H */

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
