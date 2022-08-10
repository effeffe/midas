/********************************************************************\

  Name:         mserver.c
  Created by:   Stefan Ritt

  Contents:     Server program for midas RPC calls

  $Id$

\********************************************************************/

#undef NDEBUG // midas required assert() to be always enabled

#include "midas.h"
#include "msystem.h"

#ifndef HAVE_STRLCPY
#include "strlcpy.h"
#endif

#ifdef OS_UNIX
#include <sys/types.h>
#endif

struct callback_addr callback;

BOOL use_callback_addr = TRUE;

INT rpc_server_dispatch(INT index, void *prpc_param[]);

/*---- debug_print -------------------------------------------------*/

void debug_print(const char *msg)
{
   std::string file_name;
   static std::string client_name;
   static DWORD start_time = 0;

   if (!start_time)
      start_time = ss_millitime();

   /* print message to file */
#ifdef OS_LINUX
   file_name = "/tmp/mserver.log";
#else
   file_name = ss_getcwd();
   if (!ends_with_char(file_name, DIR_SEPARATOR)) {
      file_name += DIR_SEPARATOR_STRING;
   }
   file_name += "mserver.log";
#endif

   FILE *f = fopen(file_name.c_str(), "a");

   if (!f) {
      fprintf(stderr, "Cannot open \"%s\", errno %d (%s)\n", file_name.c_str(), errno, strerror(errno));
      return;
   }

   if (client_name.empty())
      client_name = cm_get_client_name();

   std::string str = msprintf("%10.3lf [%d,%s,%s] ", (ss_millitime() - start_time) / 1000.0,
           ss_getpid(), callback.host_name.c_str(), client_name.c_str());
   str += msg;
   str += "\n";

   fputs(str.c_str(), f);
   fclose(f);
}

/*---- main --------------------------------------------------------*/

int main(int argc, char **argv)
/********************************************************************\

  Routine: main (server.exe)

  Purpose: Main routine for MIDAS server process. If called one
           parameter, it listens for a connection under
           MIDAS_TCP_PORT. If a connection is requested, the action
           depends on the parameter:

           0: Single process server: This process executes RPC calls
              even if there are several connections.

           1: Multi thread server (only Windows NT): For each conn-
              ection, a seperate thread is started which servers this
              connection.

           2: Multi process server (Not MS-DOS):  For each conn-
              ection, a seperate subprocess is started which servers 
              this connection. The subprocess is again server.exe, now
              with four parameters: the IP address of the calling 
              host, the two port numbers used by the calling
              host and optionally the name of the experiment to connect
              to. With this information it calles back the client.

           This technique is necessary since the usual fork() doesn't
           work on some of the supported systems.

  Input:
    int  argc               Number of parameters in command line
    char **argv             Command line parameters:

                            ip-addr     callback address as longword
                            port-no     port number
                            program     program name as string
                            experiment  experiment name as string


  Output:
    none

  Function value:
    BM_SUCCESS              Successful completion

\********************************************************************/
{
   int i, flag;
   socklen_t size;
   //char name[256];
   char str[1000];
   BOOL inetd, daemon, debug;

#if defined(SIGPIPE) && defined(SIG_IGN)
   signal(SIGPIPE, SIG_IGN);
#endif

   setbuf(stdout, NULL);
   setbuf(stderr, NULL);

   ///* save executable file name */
   //if (argv[0] == NULL || argv[0][0] == 0)
   //  strlcpy(name, "mserver", sizeof(name));
   //else
   //  strlcpy(name, argv[0], sizeof(name));

#ifdef OS_UNIX
   ///* if no full path given, assume /usr/local/bin */
   //if (strchr(name, '/') == 0) {
   //   strlcpy(str, "/usr/local/bin/", sizeof(str));
   //   strlcat(str, name, sizeof(str));
   //   strlcpy(name, str, sizeof(name));
   //}
#endif

#if 0
   printf("mserver main, name [%s]\n", name);
   for (i=0; i<=argc; i++)
      printf("argv[%d] is [%s]\n", i, argv[i]);
   system("/bin/ls -la /proc/self/fd");
#endif

   if (getenv("MIDAS_MSERVER_DO_NOT_USE_CALLBACK_ADDR"))
      use_callback_addr = FALSE;

   //rpc_set_mserver_path(name);

   /* find out if we were started by inetd */
   size = sizeof(int);
   inetd = (getsockopt(0, SOL_SOCKET, SO_TYPE, (void *) &flag, &size) == 0);

   /* check for debug flag */
   debug = FALSE;
   for (i = 1; i < argc; i++)
      if (argv[i][0] == '-' && argv[i][1] == 'd')
         debug = TRUE;

   if (debug) {
      debug_print("mserver startup");
      for (i = 0; i < argc; i++)
         debug_print(argv[i]);
      rpc_set_debug(debug_print, 1);
   }

   if (argc < 7 && inetd) {
      /* accept connection from stdin */
      rpc_server_accept(0);
      return 0;
   }

   if (!inetd && argc < 7)
      printf("%s started interactively\n", argv[0]);

   debug = daemon = FALSE;

   if (argc < 7 || argv[1][0] == '-') {
      int status;
      char expt_name[NAME_LENGTH];
      int port = 0;

      /* Get if existing the pre-defined experiment */
      expt_name[0] = 0;
      cm_get_environment(NULL, 0, expt_name, sizeof(expt_name));

      /* parse command line parameters */
      for (i = 1; i < argc; i++) {
         if (strncmp(argv[i], "-e", 2) == 0) {
            strlcpy(expt_name, argv[++i], sizeof(expt_name));
         } else if (argv[i][0] == '-' && argv[i][1] == 'd')
            debug = TRUE;
         else if (argv[i][0] == '-' && argv[i][1] == 'D')
            daemon = TRUE;
         else if (argv[i][0] == '-' && argv[i][1] == 'p')
            port = strtoul(argv[++i], NULL, 0);
         else if (argv[i][0] == '-') {
            if (i + 1 >= argc || argv[i + 1][0] == '-')
               goto usage;
            else {
             usage:
               printf("usage: mserver [-e Experiment] [-s][-t][-m][-d][-p port]\n");
               printf("               -e    experiment to connect to\n");
               printf("               -m    Multi process server (default)\n");
               printf("               -p port Listen for connections on specifed tcp port. Default value is taken from ODB \"/Experiment/midas server port\"\n");
#ifdef OS_LINUX
               printf("               -D    Become a daemon\n");
               printf("               -d    Write debug info to stdout or to \"/tmp/mserver.log\"\n\n");
#else
               printf("               -d    Write debug info\"\n\n");
#endif
               return 0;
            }
         }
      }

      /* turn on debugging */
      if (debug) {
         if (daemon || inetd)
            rpc_set_debug(debug_print, 1);
         else
            rpc_set_debug((void (*)(const char *)) puts, 1);

         sprintf(str, "Arguments: ");
         for (i = 0; i < argc; i++)
            sprintf(str + strlen(str), " %s", argv[i]);
         rpc_debug_printf(str);

         rpc_debug_printf("Debugging mode is on");
      }

      /* become a daemon */
      if (daemon) {
         printf("Becoming a daemon...\n");
         ss_daemon_init(FALSE);
      }

      /* connect to experiment */
      status = cm_connect_experiment(NULL, expt_name, "mserver", 0);
      if (status != CM_SUCCESS) {
         printf("cannot connect to experiment \"%s\", status %d\n", expt_name, status);
         exit(1);
      }
      
      HNDLE hDB;
      HNDLE hClient;

      status = cm_get_experiment_database(&hDB, &hClient);
      assert(status == CM_SUCCESS);
      
      int odb_port = MIDAS_TCP_PORT;
      int size = sizeof(odb_port);

      status = db_get_value(hDB, 0, "/Experiment/Midas server port", &odb_port, &size, TID_DWORD, TRUE);
      assert(status == DB_SUCCESS);

      if (port == 0)
         port = odb_port;
      
      if (port == 0)
         port = MIDAS_TCP_PORT;

      printf("mserver will listen on TCP port %d\n", port);

      int lsock = 0; // mserver main listener socket
      int lport = 0; // mserver listener port number

      /* register server */
      status = rpc_register_listener(port, rpc_server_dispatch, &lsock, &lport);
      if (status != RPC_SUCCESS) {
         printf("Cannot start server, rpc_register_server() status %d\n", status);
         return 1;
      }

      /* register path of mserver executable */
      rpc_set_mserver_path(argv[0]);

      /* register MIDAS library functions */
      rpc_register_functions(rpc_get_internal_list(1), rpc_server_dispatch);

      ss_suspend_set_server_listener(lsock);

      /* run forever */
      while (1) {
         status = cm_yield(1000);
         //printf("status %d\n", status);
         if (status == RPC_SHUTDOWN)
            break;
      }

      closesocket(lsock);

      cm_disconnect_experiment();
   } else {

      /* here we come if this program is started as a subprocess */

      callback.clear();

      /* extract callback arguments and start receiver */
#ifdef OS_VMS
      strlcpy(callback.host_name, argv[2], sizeof(callback.host_name));
      callback.host_port1 = atoi(argv[3]);
      callback.host_port2 = atoi(argv[4]);
      callback.host_port3 = atoi(argv[5]);
      callback.debug = atoi(argv[6]);
      if (argc > 7)
         strlcpy(callback.experiment, argv[7], sizeof(callback.experiment));
      if (argc > 8)
         strlcpy(callback.directory, argv[8], sizeof(callback.directory));
      if (argc > 9)
         strlcpy(callback.user, argv[9], sizeof(callback.user));
#else
      callback.host_name = argv[1];
      callback.host_port1 = atoi(argv[2]);
      callback.host_port2 = atoi(argv[3]);
      callback.host_port3 = atoi(argv[4]);
      callback.debug = atoi(argv[5]);
      if (argc > 6)
         callback.experiment = argv[6];
      if (argc > 7)
         callback.directory = argv[7];
      if (argc > 8)
         callback.user = argv[8];
#endif
      //callback.index = 0;

      if (callback.debug) {
         rpc_set_debug(debug_print, 1);
         if (callback.directory[0]) {
            if (callback.user[0])
               rpc_debug_printf("Start subprocess in %s under user %s", callback.directory.c_str(), callback.user.c_str());
            else
               rpc_debug_printf("Start subprocess in %s", callback.directory.c_str());

         } else
            rpc_debug_printf("Start subprocess in current directory");
      }

      /* change the directory and uid */
      if (callback.directory.length() > 0)
         if (chdir(callback.directory.c_str()) != 0)
            rpc_debug_printf("Cannot change to directory \"%s\"", callback.directory.c_str());

      cm_msg_early_init();

      /* set the experiment name and expt path name */

      if (callback.directory.length() > 0)
         cm_set_path(callback.directory.c_str());
      
      if (callback.experiment.length() > 0)
         cm_set_experiment_name(callback.experiment.c_str());

      /* must be done after cm_set_path() */
      ss_suspend_init_odb_port();

      /* register system functions */
      rpc_register_functions(rpc_get_internal_list(0), rpc_server_dispatch);

      /* register MIDAS library functions */
      rpc_register_functions(rpc_get_internal_list(1), rpc_server_dispatch);

      int status = rpc_server_callback(&callback);
      
      if (status != RPC_SUCCESS) {
         cm_msg_flush_buffer();
         printf("Cannot start mserver, rpc_server_callback() status %d\n", status);
         return 1;
      }
      
      /* create alarm and elog semaphores */
      int semaphore_alarm, semaphore_elog, semaphore_history, semaphore_msg;
      ss_semaphore_create("ALARM", &semaphore_alarm);
      ss_semaphore_create("ELOG", &semaphore_elog);
      ss_semaphore_create("HISTORY", &semaphore_history);
      ss_semaphore_create("MSG", &semaphore_msg);
      cm_set_experiment_semaphore(semaphore_alarm, semaphore_elog, semaphore_history, semaphore_msg);

      rpc_server_loop();
      rpc_server_shutdown();

      ss_suspend_exit();
   }

   return 0;
}

/*------------------------------------------------------------------*/

/* just a small test routine which doubles numbers */
INT rpc_test(BYTE b, WORD w, INT i, float f, double d, BYTE * b1, WORD * w1, INT * i1, float *f1, double *d1)
{
   printf("rpc_test: %d %d %d %1.1f %1.1lf\n", b, w, i, f, d);

   *b1 = b * 2;
   *w1 = w * 2;
   *i1 = i * 2;
   *f1 = f * 2;
   *d1 = d * 2;

   return 1;
}

/*----- rpc_server_dispatch ----------------------------------------*/

INT rpc_server_dispatch(INT index, void *prpc_param[])
/********************************************************************\

  Routine: rpc_server_dispatch

  Purpose: This routine gets registered as the callback function
           for all RPC calls via rpc_server_register. It acts as a
           stub to call the local midas subroutines. It uses the
           Cxxx(i) macros to cast parameters in prpc_param to
           their appropriate types.

  Input:
    int    index            RPC index defined in RPC.H
    void   *prpc_param      pointer to rpc parameter array

  Output:
    none

  Function value:
    status                  status returned from local midas function

\********************************************************************/
{
   INT status = 0;

   int convert_flags = rpc_get_convert_flags();

   switch (index) {
      /* common functions */

   case RPC_CM_SET_CLIENT_INFO:
      status = cm_set_client_info(CHNDLE(0), CPHNDLE(1), (use_callback_addr?callback.host_name.c_str():CSTRING(2)),
                                  CSTRING(3), CINT(4), CSTRING(5), CINT(6));
      break;

   case RPC_CM_CHECK_CLIENT:
      status = cm_check_client(CHNDLE(0), CHNDLE(1));
      break;

   case RPC_CM_SET_WATCHDOG_PARAMS:
      status = cm_set_watchdog_params(CBOOL(0), CINT(1));
      break;

   case RPC_CM_CLEANUP:
      status = cm_cleanup(CSTRING(0), CBOOL(1));
      break;

   case RPC_CM_GET_WATCHDOG_INFO:
      status = cm_get_watchdog_info(CHNDLE(0), CSTRING(1), CPDWORD(2), CPDWORD(3));
      break;

   case RPC_CM_MSG:
      status = cm_msg(CINT(0), CSTRING(1), CINT(2), CSTRING(3), "%s", CSTRING(4));
      break;

   case RPC_CM_MSG_LOG:
      status = cm_msg_log(CINT(0), CSTRING(1), CSTRING(2));
      break;

   case RPC_CM_EXECUTE:
      status = cm_execute(CSTRING(0), CSTRING(1), CINT(2));
      break;

   case RPC_CM_EXIST:
      status = cm_exist(CSTRING(0), CBOOL(1));
      break;

   case RPC_CM_SYNCHRONIZE:
      status = cm_synchronize(CPDWORD(0));
      break;

   case RPC_CM_ASCTIME:
      status = cm_asctime(CSTRING(0), CINT(1));
      break;

   case RPC_CM_TIME:
      status = cm_time(CPDWORD(0));
      break;

   case RPC_CM_MSG_RETRIEVE:
      status = cm_msg_retrieve(CINT(0), CSTRING(1), CINT(2));
      break;

      /* buffer manager functions */

   case RPC_BM_OPEN_BUFFER:
      status = bm_open_buffer(CSTRING(0), CINT(1), CPINT(2));
      break;

   case RPC_BM_CLOSE_BUFFER:
      //printf("RPC_BM_CLOSE_BUFFER(%d)!\n", CINT(0));
      status = bm_close_buffer(CINT(0));
      break;

   case RPC_BM_CLOSE_ALL_BUFFERS:
      //printf("RPC_BM_CLOSE_ALL_BUFFERS!\n");
      status = bm_close_all_buffers();
      break;

   case RPC_BM_GET_BUFFER_INFO:
      status = bm_get_buffer_info(CINT(0), (BUFFER_HEADER*)CARRAY(1));
      if (convert_flags) {
         BUFFER_HEADER *pb;

         /* convert event header */
         pb = (BUFFER_HEADER *) CARRAY(1);
         rpc_convert_single(&pb->num_clients, TID_INT, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pb->max_client_index, TID_INT, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pb->size, TID_INT, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pb->read_pointer, TID_INT, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pb->write_pointer, TID_INT, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pb->num_in_events, TID_INT, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pb->num_out_events, TID_INT, RPC_OUTGOING, convert_flags);
      }
      break;

   case RPC_BM_GET_BUFFER_LEVEL:
      status = bm_get_buffer_level(CINT(0), CPINT(1));
      break;

   case RPC_BM_INIT_BUFFER_COUNTERS:
      status = bm_init_buffer_counters(CINT(0));
      break;

   case RPC_BM_SET_CACHE_SIZE:
      status = bm_set_cache_size(CINT(0), CINT(1), CINT(2));
      break;

   case RPC_BM_ADD_EVENT_REQUEST:
      status = bm_add_event_request(CINT(0), CSHORT(1),
                                    CSHORT(2), CINT(3), (void (*)(HNDLE, HNDLE, EVENT_HEADER *, void *))
                                    (POINTER_T)
                                    CINT(4), CINT(5));
      break;

   case RPC_BM_REMOVE_EVENT_REQUEST:
      status = bm_remove_event_request(CINT(0), CINT(1));
      break;

   case RPC_BM_SEND_EVENT:
      if (convert_flags) {
         EVENT_HEADER *pevent;

         /* convert event header */
         pevent = (EVENT_HEADER *) CARRAY(1);
         rpc_convert_single(&pevent->event_id, TID_SHORT, 0, convert_flags);
         rpc_convert_single(&pevent->trigger_mask, TID_SHORT, 0, convert_flags);
         rpc_convert_single(&pevent->serial_number, TID_DWORD, 0, convert_flags);
         rpc_convert_single(&pevent->time_stamp, TID_DWORD, 0, convert_flags);
         rpc_convert_single(&pevent->data_size, TID_DWORD, 0, convert_flags);
      }

      status = bm_send_event(CINT(0), (const EVENT_HEADER*)(CARRAY(1)), CINT(2), CINT(3));
      break;

   case RPC_BM_RECEIVE_EVENT:
      status = bm_receive_event(CINT(0), CARRAY(1), CPINT(2), CINT(3));
      break;

   case RPC_BM_SKIP_EVENT:
      status = bm_skip_event(CINT(0));
      break;

   case RPC_BM_FLUSH_CACHE:
      //printf("RPC_BM_FLUSH_CACHE(%d,%d)!\n", CINT(0), CINT(1));
      if (CINT(0) == 0) {
         status = rpc_flush_event_socket(CINT(1));
      } else {
         status = bm_flush_cache(CINT(0), CINT(1));
      }
      break;

   case RPC_BM_MARK_READ_WAITING:
      status = BM_SUCCESS;
      break;

   case RPC_BM_EMPTY_BUFFERS:
      status = bm_empty_buffers();
      break;

      /* database functions */

   case RPC_DB_OPEN_DATABASE:
      status = db_open_database(CSTRING(0), CINT(1), CPHNDLE(2), CSTRING(3));
      break;

   case RPC_DB_CLOSE_DATABASE:
      status = db_close_database(CINT(0));
      break;

   case RPC_DB_FLUSH_DATABASE:
      status = db_flush_database(CINT(0));
      break;

   case RPC_DB_CLOSE_ALL_DATABASES:
      status = db_close_all_databases();
      break;

   case RPC_DB_CREATE_KEY:
      status = db_create_key(CHNDLE(0), CHNDLE(1), CSTRING(2), CDWORD(3));
      break;

   case RPC_DB_CREATE_LINK:
      status = db_create_link(CHNDLE(0), CHNDLE(1), CSTRING(2), CSTRING(3));
      break;

   case RPC_DB_SET_VALUE:
      rpc_convert_data(CARRAY(3), CDWORD(6), RPC_FIXARRAY, CINT(4), convert_flags);
      status = db_set_value(CHNDLE(0), CHNDLE(1), CSTRING(2), CARRAY(3), CINT(4), CINT(5), CDWORD(6));
      break;

   case RPC_DB_GET_VALUE:
      rpc_convert_data(CARRAY(3), CDWORD(5), RPC_FIXARRAY, CINT(4), convert_flags);
      status = db_get_value(CHNDLE(0), CHNDLE(1), CSTRING(2), CARRAY(3), CPINT(4), CDWORD(5), CBOOL(6));
      rpc_convert_data(CARRAY(3), CDWORD(5), RPC_FIXARRAY | RPC_OUTGOING, CINT(4), convert_flags);
      break;

   case RPC_DB_FIND_KEY:
      status = db_find_key(CHNDLE(0), CHNDLE(1), CSTRING(2), CPHNDLE(3));
      break;

   case RPC_DB_FIND_LINK:
      status = db_find_link(CHNDLE(0), CHNDLE(1), CSTRING(2), CPHNDLE(3));
      break;

   case RPC_DB_GET_PATH:
      status = db_get_path(CHNDLE(0), CHNDLE(1), CSTRING(2), CINT(3));
      break;

   case RPC_DB_GET_PARENT:
      status = db_get_parent(CHNDLE(0), CHNDLE(1), CPHNDLE(2));
      break;

   case RPC_DB_DELETE_KEY:
      status = db_delete_key(CHNDLE(0), CHNDLE(1), CBOOL(2));
      break;

   case RPC_DB_ENUM_KEY:
      status = db_enum_key(CHNDLE(0), CHNDLE(1), CINT(2), CPHNDLE(3));
      break;

   case RPC_DB_ENUM_LINK:
      status = db_enum_link(CHNDLE(0), CHNDLE(1), CINT(2), CPHNDLE(3));
      break;

   case RPC_DB_GET_NEXT_LINK:
      status = db_get_next_link(CHNDLE(0), CHNDLE(1), CPHNDLE(2));
      break;

   case RPC_DB_GET_KEY:
      status = db_get_key(CHNDLE(0), CHNDLE(1), (KEY*)CARRAY(2));
      if (convert_flags) {
         KEY *pkey;

         pkey = (KEY *) CARRAY(2);
         rpc_convert_single(&pkey->type, TID_DWORD, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pkey->num_values, TID_INT, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pkey->data, TID_INT, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pkey->total_size, TID_INT, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pkey->item_size, TID_INT, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pkey->access_mode, TID_WORD, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pkey->notify_count, TID_WORD, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pkey->next_key, TID_INT, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pkey->parent_keylist, TID_INT, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pkey->last_written, TID_INT, RPC_OUTGOING, convert_flags);
      }
      break;

   case RPC_DB_GET_LINK:
      status = db_get_link(CHNDLE(0), CHNDLE(1), (KEY*)CARRAY(2));
      if (convert_flags) {
         KEY *pkey;

         pkey = (KEY *) CARRAY(2);
         rpc_convert_single(&pkey->type, TID_DWORD, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pkey->num_values, TID_INT, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pkey->data, TID_INT, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pkey->total_size, TID_INT, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pkey->item_size, TID_INT, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pkey->access_mode, TID_WORD, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pkey->notify_count, TID_WORD, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pkey->next_key, TID_INT, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pkey->parent_keylist, TID_INT, RPC_OUTGOING, convert_flags);
         rpc_convert_single(&pkey->last_written, TID_INT, RPC_OUTGOING, convert_flags);
      }
      break;

   case RPC_DB_GET_KEY_INFO:
      status = db_get_key_info(CHNDLE(0), CHNDLE(1), CSTRING(2), CINT(3), CPINT(4), CPINT(5), CPINT(6));
      break;

   case RPC_DB_GET_KEY_TIME:
      status = db_get_key_time(CHNDLE(0), CHNDLE(1), CPDWORD(2));
      break;

   case RPC_DB_RENAME_KEY:
      status = db_rename_key(CHNDLE(0), CHNDLE(1), CSTRING(2));
      break;

   case RPC_DB_REORDER_KEY:
      status = db_reorder_key(CHNDLE(0), CHNDLE(1), CINT(2));
      break;

   case RPC_DB_GET_DATA:
      status = db_get_data(CHNDLE(0), CHNDLE(1), CARRAY(2), CPINT(3), CDWORD(4));
      rpc_convert_data(CARRAY(2), CDWORD(4), RPC_FIXARRAY | RPC_OUTGOING, CINT(3), convert_flags);
      break;

   case RPC_DB_GET_LINK_DATA:
      status = db_get_link_data(CHNDLE(0), CHNDLE(1), CARRAY(2), CPINT(3), CDWORD(4));
      rpc_convert_data(CARRAY(2), CDWORD(4), RPC_FIXARRAY | RPC_OUTGOING, CINT(3), convert_flags);
      break;

   case RPC_DB_GET_DATA1:
      status = db_get_data1(CHNDLE(0), CHNDLE(1), CARRAY(2), CPINT(3), CDWORD(4), CPINT(5));
      rpc_convert_data(CARRAY(2), CDWORD(4), RPC_FIXARRAY | RPC_OUTGOING, CINT(3), convert_flags);
      break;

   case RPC_DB_GET_DATA_INDEX:
      status = db_get_data_index(CHNDLE(0), CHNDLE(1), CARRAY(2), CPINT(3), CINT(4), CDWORD(5));
      rpc_convert_single(CARRAY(2), CDWORD(5), RPC_OUTGOING, convert_flags);
      break;

   case RPC_DB_SET_DATA:
      rpc_convert_data(CARRAY(2), CDWORD(5), RPC_FIXARRAY, CINT(3), convert_flags);
      status = db_set_data(CHNDLE(0), CHNDLE(1), CARRAY(2), CINT(3), CINT(4), CDWORD(5));
      break;

   case RPC_DB_SET_DATA1:
         rpc_convert_data(CARRAY(2), CDWORD(5), RPC_FIXARRAY, CINT(3), convert_flags);
         status = db_set_data1(CHNDLE(0), CHNDLE(1), CARRAY(2), CINT(3), CINT(4), CDWORD(5));
         break;

   case RPC_DB_NOTIFY_CLIENTS_ARRAY:
         rpc_convert_data(CARRAY(1), CINT(2), RPC_FIXARRAY, TID_DWORD, convert_flags);
         status = db_notify_clients_array(CHNDLE(0), (HNDLE*)CARRAY(1), CINT(2));
         break;

   case RPC_DB_SET_LINK_DATA:
      rpc_convert_data(CARRAY(2), CDWORD(5), RPC_FIXARRAY, CINT(3), convert_flags);
      status = db_set_link_data(CHNDLE(0), CHNDLE(1), CARRAY(2), CINT(3), CINT(4), CDWORD(5));
      break;

   case RPC_DB_SET_DATA_INDEX:
      rpc_convert_single(CARRAY(2), CDWORD(5), 0, convert_flags);
      status = db_set_data_index(CHNDLE(0), CHNDLE(1), CARRAY(2), CINT(3), CINT(4), CDWORD(5));
      break;

   case RPC_DB_SET_LINK_DATA_INDEX:
      rpc_convert_single(CARRAY(2), CDWORD(5), 0, convert_flags);
      status = db_set_link_data_index(CHNDLE(0), CHNDLE(1), CARRAY(2), CINT(3), CINT(4), CDWORD(5));
      break;

   case RPC_DB_SET_DATA_INDEX1:
      rpc_convert_single(CARRAY(2), CDWORD(5), 0, convert_flags);
      status = db_set_data_index1(CHNDLE(0), CHNDLE(1), CARRAY(2), CINT(3), CINT(4), CDWORD(5), CBOOL(6));
      break;

   case RPC_DB_SET_NUM_VALUES:
      status = db_set_num_values(CHNDLE(0), CHNDLE(1), CINT(2));
      break;

   case RPC_DB_SET_MODE:
      status = db_set_mode(CHNDLE(0), CHNDLE(1), CWORD(2), CBOOL(3));
      break;

   case RPC_DB_GET_RECORD:
      status = db_get_record(CHNDLE(0), CHNDLE(1), CARRAY(2), CPINT(3), CINT(4));
      break;

   case RPC_DB_SET_RECORD:
      status = db_set_record(CHNDLE(0), CHNDLE(1), CARRAY(2), CINT(3), CINT(4));
      break;

   case RPC_DB_GET_RECORD_SIZE:
      status = db_get_record_size(CHNDLE(0), CHNDLE(1), CINT(2), CPINT(3));
      break;

   case RPC_DB_CREATE_RECORD:
      status = db_create_record(CHNDLE(0), CHNDLE(1), CSTRING(2), CSTRING(3));
      break;

   case RPC_DB_CHECK_RECORD:
      status = db_check_record(CHNDLE(0), CHNDLE(1), CSTRING(2), CSTRING(3), CBOOL(4));
      break;

   case RPC_DB_ADD_OPEN_RECORD:
      status = db_add_open_record(CHNDLE(0), CHNDLE(1), CWORD(2));
      break;

   case RPC_DB_REMOVE_OPEN_RECORD:
      status = db_remove_open_record(CHNDLE(0), CHNDLE(1), CBOOL(2));
      break;

   case RPC_DB_LOAD:
      status = db_load(CHNDLE(0), CHNDLE(1), CSTRING(2), CBOOL(3));
      break;

   case RPC_DB_SAVE:
      status = db_save(CHNDLE(0), CHNDLE(1), CSTRING(2), CBOOL(3));
      break;

   case RPC_DB_SET_CLIENT_NAME:
      status = db_set_client_name(CHNDLE(0), CSTRING(1));
      break;

   case RPC_DB_GET_OPEN_RECORDS:
      status = db_get_open_records(CHNDLE(0), CHNDLE(1), CSTRING(2), CINT(3), CBOOL(4));
      break;

   case RPC_DB_COPY_XML:
      status = db_copy_xml(CHNDLE(0), CHNDLE(1), CSTRING(2), CPINT(3), CBOOL(4));
      break;

   case RPC_EL_SUBMIT:
      status = el_submit(CINT(0), CSTRING(1), CSTRING(2), CSTRING(3), CSTRING(4),
                         CSTRING(5), CSTRING(6), CSTRING(7),
                         CSTRING(8), (char*)CARRAY(9), CINT(10),
                         CSTRING(11), (char*)CARRAY(12), CINT(13),
                         CSTRING(14), (char*)CARRAY(15), CINT(16), CSTRING(17), CINT(18));
      break;

   case RPC_AL_CHECK:
      status = al_check();
      break;

   case RPC_AL_TRIGGER_ALARM:
      status = al_trigger_alarm(CSTRING(0), CSTRING(1), CSTRING(2), CSTRING(3), CINT(4));
      break;

      /* exit functions */
   case RPC_ID_EXIT:
      //printf("RPC_ID_EXIT!\n");
      status = RPC_SUCCESS;
      break;

   case RPC_ID_SHUTDOWN:
      status = RPC_SUCCESS;
      break;

      /* various functions */

   case RPC_TEST:
      status = rpc_test(CBYTE(0), CWORD(1), CINT(2), CFLOAT(3), CDOUBLE(4),
                        CPBYTE(5), CPWORD(6), CPINT(7), CPFLOAT(8), CPDOUBLE(9));
      break;

   default:
      cm_msg(MERROR, "rpc_server_dispatch", "received unrecognized command %d", index);
   }

   return status;
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
