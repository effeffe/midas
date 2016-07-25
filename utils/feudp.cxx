//
// feudp.cxx
//
// Frontend for receiving and storing UDP packets as MIDAS data banks.
//

#include <stdio.h>
#include <netdb.h> // getnameinfo()
//#include <stdlib.h>
#include <string.h> // memcpy()
#include <errno.h> // errno
//#include <unistd.h>
//#include <time.h>

#include <string>
#include <vector>

#include "midas.h"

const char *frontend_name = "feudp";                     /* fe MIDAS client name */
const char *frontend_file_name = __FILE__;               /* The frontend file name */

extern "C" {
   BOOL frontend_call_loop = TRUE;       /* frontend_loop called periodically TRUE */
   int display_period = 0;               /* status page displayed with this freq[ms] */
   int max_event_size = 1*1024*1024;     /* max event size produced by this frontend */
   int max_event_size_frag = 5 * 1024 * 1024;     /* max for fragmented events */
   int event_buffer_size = 8*1024*1024;           /* buffer size to hold events */
}

extern "C" {
  int interrupt_configure(INT cmd, INT source, PTYPE adr);
  INT poll_event(INT source, INT count, BOOL test);
  int frontend_init();
  int frontend_exit();
  int begin_of_run(int run, char *err);
  int end_of_run(int run, char *err);
  int pause_run(int run, char *err);
  int resume_run(int run, char *err);
  int frontend_loop();
  int read_event(char *pevent, INT off);
}

#ifndef EQ_NAME
#define EQ_NAME "UDP"
#endif

#ifndef EQ_EVID
#define EQ_EVID 1
#endif

EQUIPMENT equipment[] = {
   { EQ_NAME,                         /* equipment name */
      {EQ_EVID, 0, "SYSTEM",          /* event ID, trigger mask, Evbuf */
       EQ_MULTITHREAD, 0, "MIDAS",    /* equipment type, EventSource, format */
       TRUE, RO_ALWAYS,               /* enabled?, WhenRead? */
       50, 0, 0, 0,                   /* poll[ms], Evt Lim, SubEvtLim, LogHist */
       "", "", "",}, read_event,      /* readout routine */
   },
   {""}
};
////////////////////////////////////////////////////////////////////////////

struct Source
{
  struct sockaddr addr;
  char bank_name[5];
  std::string host_name;
};

static std::vector<Source> gSrc;

static HNDLE hDB;
static HNDLE hKeySet; // equipment settings

static int gDataSocket;

int open_udp_socket(int server_port)
{
   int status;
   
   int fd = socket(AF_INET, SOCK_DGRAM, 0);
   
   if (fd < 0) {
      cm_msg(MERROR, "open_udp_socket", "socket(AF_INET,SOCK_DGRAM) returned %d, errno %d (%s)", fd, errno, strerror(errno));
      return -1;
   }

   int opt = 1;
   status = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

   if (status == -1) {
      cm_msg(MERROR, "open_udp_socket", "setsockopt(SOL_SOCKET,SO_REUSEADDR) returned %d, errno %d (%s)", status, errno, strerror(errno));
      return -1;
   }

   int bufsize = 8*1024*1024;

   status = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));

   if (status == -1) {
      cm_msg(MERROR, "open_udp_socket", "setsockopt(SOL_SOCKET,SO_RCVBUF) returned %d, errno %d (%s)", status, errno, strerror(errno));
      return -1;
   }

   struct sockaddr_in local_addr;
   memset(&local_addr, 0, sizeof(local_addr));

   local_addr.sin_family = AF_INET;
   local_addr.sin_port = htons(server_port);
   local_addr.sin_addr.s_addr = INADDR_ANY;

   status = bind(fd, (struct sockaddr *)&local_addr, sizeof(local_addr));

   if (status == -1) {
      cm_msg(MERROR, "open_udp_socket", "bind(port=%d) returned %d, errno %d (%s)", server_port, status, errno, strerror(errno));
      return -1;
   }

   return fd;
}

bool addr_match(const Source* s, void *addr, int addr_len)
{
  int v = memcmp(&s->addr, addr, addr_len);
#if 0
  for (int i=0; i<addr_len; i++)
    printf("%3d - 0x%02x 0x%02x\n", i, ((char*)&s->addr)[i], ((char*)addr)[i]);
  printf("match %d, hostname [%s] bank [%s], status %d\n", addr_len, s->host_name.c_str(), s->bank_name, v);
#endif
  return v==0;
}

int wait_udp(int socket, int msec)
{
   int status;
   fd_set fdset;
   struct timeval timeout;

   FD_ZERO(&fdset);
   FD_SET(socket, &fdset);

   timeout.tv_sec = msec/1000;
   timeout.tv_usec = (msec%1000)*1000;

   status = select(socket+1, &fdset, NULL, NULL, &timeout);

   if (status < 0) {
      cm_msg(MERROR, "wait_udp", "select() returned %d, errno %d (%s)", status, errno, strerror(errno));
      return -1;
   }

   if (status == 0) {
      return 0; // timeout
   }

   if (FD_ISSET(socket, &fdset)) {
      return 1; // have data
   }

   // timeout
   return 0;
}

int read_udp(int socket, char* buf, int bufsize, char* pbankname)
{
   if (wait_udp(socket, 100) < 1)
      return 0;

   struct sockaddr addr;
   socklen_t addr_len = sizeof(addr);
   int rd = recvfrom(socket, buf, bufsize, 0, &addr, &addr_len);
   
   if (rd < 0) {
      cm_msg(MERROR, "read_udp", "recvfrom() returned %d, errno %d (%s)", rd, errno, strerror(errno));
      return -1;
   }

   for (unsigned i=0; i<gSrc.size(); i++) {
      if (addr_match(&gSrc[i], &addr, addr_len)) {
         strlcpy(pbankname, gSrc[i].bank_name, 5);
         //printf("rd %d, bank [%s]\n", rd, pbankname);
         return rd;
      }
   }
   
   static int count_down = 10;

   if (count_down > 0) {
      count_down--;

      if (count_down == 0) {
         cm_msg(MERROR, "read_udp", "further messages are now suppressed...");
         return -1;
      }
   
      char host[NI_MAXHOST], service[NI_MAXSERV];
      
      int status = getnameinfo(&addr, addr_len, host, NI_MAXHOST, service, NI_MAXSERV, NI_NUMERICSERV);

      if (status != 0) {
         cm_msg(MERROR, "read_udp", "getnameinfo() returned %d (%s), errno %d (%s)", status, gai_strerror(status), errno, strerror(errno));
         return -1;
      }

      char bankname[NAME_LENGTH];
      int size = sizeof(bankname);
      
      status = db_get_value(hDB, hKeySet, host, bankname, &size, TID_STRING, FALSE);
      
      if (status == DB_NO_KEY) {
         cm_msg(MERROR, "read_udp", "UDP packet from unknown host \"%s\"", host);
         cm_msg(MINFO, "read_udp", "Register this host by running following commands:");
         cm_msg(MINFO, "read_udp", "odbedit -c \"create STRING /Equipment/%s/Settings/%s\"", EQ_NAME, host);
         cm_msg(MINFO, "read_udp", "odbedit -c \"set /Equipment/%s/Settings/%s AAAA\", where AAAA is the MIDAS bank name for this host", EQ_NAME, host);
         return -1;
      } else if (status == DB_SUCCESS) {
         if (strlen(bankname) != 4) {
            cm_msg(MERROR, "read_udp", "ODB \"/Equipment/%s/Settings/%s\" should be set to a 4 character MIDAS bank name", EQ_NAME, host);
            cm_msg(MINFO, "read_udp", "Use this command:");
            cm_msg(MINFO, "read_udp", "odbedit -c \"set /Equipment/%s/Settings/%s AAAA\", where AAAA is the MIDAS bank name for this host", EQ_NAME, host);
            return -1;
         }

         cm_msg(MINFO, "read_udp", "UDP packets from host \"%s\" will be stored in bank \"%s\"", host, bankname);
         
         Source s;
         s.host_name = host;
         strlcpy(s.bank_name, bankname, 5);
         memcpy(&s.addr, &addr, sizeof(s.addr));
         gSrc.push_back(s);
         
         strlcpy(pbankname, bankname, 5);
         
         return rd;
      } else {
         cm_msg(MERROR, "read_udp", "db_get_value(\"/Equipment/%s/Settings/%s\") status %d", EQ_NAME, host, status);
         return -1;
      }
   }
   
   return -1;
}

int interrupt_configure(INT cmd, INT source, PTYPE adr)
{
   return SUCCESS;
}

int frontend_init()
{
   int status;

   status = cm_get_experiment_database(&hDB, NULL);
   if (status != CM_SUCCESS) {
      cm_msg(MERROR, "frontend_init", "Cannot connect to ODB, cm_get_experiment_database() returned %d", status);
      return FE_ERR_ODB;
   }

   std::string path;
   path += "/Equipment";
   path += "/";
   path += EQ_NAME;
   path += "/Settings";

   std::string path1 = path + "/udp_port";

   int udp_port = 50005;
   int size = sizeof(udp_port);
   status = db_get_value(hDB, 0, path1.c_str(), &udp_port, &size, TID_INT, TRUE);
   
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "frontend_init", "Cannot find \"%s\", db_get_value() returned %d", path1.c_str(), status);
      return FE_ERR_ODB;
   }
   
   status = db_find_key(hDB, 0, path.c_str(), &hKeySet);
   
   if (status != DB_SUCCESS) {
      cm_msg(MERROR, "frontend_init", "Cannot find \"%s\", db_find_key() returned %d", path.c_str(), status);
      return FE_ERR_ODB;
   }
   
   gDataSocket = open_udp_socket(udp_port);
   
   if (gDataSocket < 0) {
      printf("frontend_init: cannot open udp socket\n");
      cm_msg(MERROR, "frontend_init", "Cannot open UDP socket for port %d", udp_port);
      return FE_ERR_HW;
   }

   cm_msg(MINFO, "frontend_init", "Frontend equipment \"%s\" is ready, listening on UDP port %d", EQ_NAME, udp_port);
   return SUCCESS;
}

int frontend_loop()
{
   ss_sleep(10);
   return SUCCESS;
}

int begin_of_run(int run_number, char *error)
{
   return SUCCESS;
}

int end_of_run(int run_number, char *error)
{
   return SUCCESS;
}

int pause_run(INT run_number, char *error)
{
   return SUCCESS;
}

int resume_run(INT run_number, char *error)
{
   return SUCCESS;
}

int frontend_exit()
{
   return SUCCESS;
}

INT poll_event(INT source, INT count, BOOL test)
{
   //printf("poll_event: source %d, count %d, test %d\n", source, count, test);

   if (test) {
      for (int i=0; i<count; i++)
         ss_sleep(10);
      return 1;
   }

   return 1;
}

#define MAX_UDP_SIZE (0x10000)

int read_event(char *pevent, int off)
{
   char buf[MAX_UDP_SIZE];
   char bankname[5];
   
   int length = read_udp(gDataSocket, buf, MAX_UDP_SIZE, bankname);
   if (length <= 0)
      return 0;
   
   bk_init32(pevent);
   char* pdata;
   bk_create(pevent, bankname, TID_BYTE, (void**)&pdata);
   memcpy(pdata, buf, length);
   bk_close(pevent, pdata + length);
   return bk_size(pevent); 
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
