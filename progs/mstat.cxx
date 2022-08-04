/********************************************************************\
  
  Name:         mstat.c
  Created by:   Pierre-Andre Amaudruz
  
  Contents:     Display/log some pertinent information of the ODB
  
  $Id$

\********************************************************************/

#undef NDEBUG // midas required assert() to be always enabled

#include <assert.h> // assert()
#include <fcntl.h>
#include "midas.h"
#include "msystem.h"
#include <sstream>

#define MAX_LINE        80
#define LINE_LENGTH     80

#if defined( OS_WINNT )
#define ESC_FLAG 0
#else
#define ESC_FLAG 1
#endif

INT rn;

/* Global variables 
 *  (required since no way to pass these inside callbacks) */
INT numprocs;
DWORD checktime, lastchecktime, delta_time;
INT nlocal;
HNDLE hKeynlocal, hKeychktime;
BOOL active_flag, esc_flag;
INT loop, cur_max_line;
std::string xststr[MAX_LINE];

/*------------------------------------------------------------------*/
INT open_log_midstat(INT file_mode, INT runn, char *svpath)
{
   char srun[32];
   INT fHandle;

   if (file_mode == 1) {        /* append run number */
      strcat(svpath, ".");
      sprintf(srun, "Run%4.4i", runn);
      strncat(svpath, srun, 256);
      printf("output with run file:%s-\n", svpath);
   }
   /* open device */
#ifdef OS_UNIX
   if ((fHandle = open(svpath, O_WRONLY | O_CREAT | O_TRUNC, 0644)) == -1)
#endif
#ifdef OS_OSF1
      if ((fHandle = open(svpath, O_WRONLY | O_CREAT | O_TRUNC, 0644)) == -1)
#endif
#ifdef OS_VXWORKS
         if ((fHandle = open(svpath, O_WRONLY | O_CREAT | O_TRUNC, 0644)) == -1)
#endif
#ifdef OS_WINNT
            if ((fHandle =
                 _open(svpath, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, S_IWRITE)) == -1)
#endif
            {
               printf("File %s cannot be created\n", svpath);
            }
   return (fHandle);
}

/*------------------------------------------------------------------*/
void compose_status(HNDLE hDB, HNDLE hKey)
{
   BOOL atleastone_active;
   INT i, size;
   char str[80], path[256];
   KEY key;
   HNDLE hSubkey;
   char strtmp[256];

   /* Clear string page */
   for (int jj = 0; jj < MAX_LINE; jj++) {
      xststr[jj].clear();
   }

   int j = 0;

/* --------------------- Run info -------------------------------- */
   {

      INT rs, rt;
      char cs[80], stt[80], spt[80], ex[80], rev[256], rev1[256];
      DWORD tb, tsb;

      size = sizeof(rs);
      db_get_value(hDB, 0, "/Runinfo/State", &rs, &size, TID_INT, TRUE);
      if (rs == STATE_RUNNING)
         strcpy(cs, "Running");
      if (rs == STATE_PAUSED)
         strcpy(cs, "Paused ");
      if (rs == STATE_STOPPED)
         strcpy(cs, "Stopped");
      size = sizeof(rn);
      db_get_value(hDB, 0, "/Runinfo/run number", &rn, &size, TID_INT, TRUE);
      size = sizeof(stt);
      db_get_value(hDB, 0, "/Runinfo/start time", stt, &size, TID_STRING, TRUE);
      size = sizeof(spt);
      db_get_value(hDB, 0, "/Runinfo/stop time", spt, &size, TID_STRING, TRUE);
      size = sizeof(tb);
      db_get_value(hDB, 0, "/runinfo/Start Time binary", &tb, &size, TID_DWORD, TRUE);
      size = sizeof(tsb);
      db_get_value(hDB, 0, "/runinfo/Stop Time binary", &tsb, &size, TID_DWORD, TRUE);
      size = sizeof(rt);
      db_get_value(hDB, 0, "/Runinfo/Requested transition", &rt, &size, TID_INT, TRUE);

      /* Experiment info */
      size = sizeof(ex);
      db_get_value(hDB, 0, "/experiment/name", ex, &size, TID_STRING, TRUE);

      {
         time_t full_time;
         time(&full_time);
         assert(sizeof(str) >= 32);
         ctime_r(&full_time, str);
         str[24] = 0;
      }
//      if (active_flag) {
//         sprintf(&(ststr[j++][0]), "*- MIDAS revision: %s - MIDAS status -- Alarm Checker active-------*", cm_get_revision());
//         j++; sprintf(&(ststr[j++][0]), "-%s-*", str);
//      } else 
      {
         sprintf(rev1, "%s", cm_get_revision());
         strcpy(rev, strstr(rev1, "midas"));
         xststr[j++] = msprintf("*- MIDAS Status Page  -%s ----------------------------*", str);
         xststr[j++] = msprintf("*- Revision  -%s---------*", rev);
      }  

      xststr[j] = msprintf("Experiment: %s, ", ex);
      xststr[j] += msprintf("Run: %d, ", rn);

      /* PAA revisit the /Runinfo for run time display */
      /* state */
      if (rs == STATE_RUNNING) {
         if (rt == TR_STOP)
            xststr[j] += msprintf(" Deferred_Stop");
         else {
            if (esc_flag)
               xststr[j] += msprintf("State: \033[1m%s\033[m", cs);
            else
               xststr[j] += msprintf("State: %s", cs);
         }
      } else {
         if (rt == TR_START)
            xststr[j] += msprintf("Deferred_Start");
         else
            xststr[j] += msprintf("State: %s", cs);
      }

      j++;

      /* time */
      if (rs != STATE_STOPPED) {
         DWORD full_time;
         cm_time(&full_time);
         DWORD difftime = (DWORD) full_time - tb;
         if (esc_flag)
            xststr[j++] = msprintf("Run time: %02d:%02d:%02d", difftime / 3600, difftime % 3600 / 60, difftime % 60);
         else
            xststr[j++] = msprintf("Run time: %02d:%02d:%02d", difftime / 3600, difftime % 3600 / 60, difftime % 60);

         xststr[j++] = msprintf("Start time: %s", stt);
      } else if (rs == STATE_STOPPED) {
         DWORD difftime;
         if (tsb < tb)
            difftime = 0;
         else
            difftime = tsb - tb;
         if (esc_flag)
            xststr[j++] = msprintf("Full Run time: %02d:%02d:%02d", difftime / 3600, difftime % 3600 / 60, difftime % 60);
         else
            xststr[j++] = msprintf("Full Run time: %02d:%02d:%02d", difftime / 3600, difftime % 3600 / 60, difftime % 60);

         xststr[j++] = msprintf("Start time: %s", stt);
         xststr[j++] += msprintf("Stop time:  %s", spt);
      }
      xststr[j++] = "";
   }  /* --- run info --- */

/* --------------------- Equipment tree -------------------------- */
   {
      double equevtsend;
      double equevtpsec;
      char equclient[256];
      char equnode[256];
      double equkbpsec;
      BOOL equenabled;

      size = sizeof(str);
      atleastone_active = FALSE;
      /* check if dir exists */
      if (db_find_key(hDB, 0, "/equipment", &hKey) == DB_SUCCESS) {
         xststr[j++] = "FE Equip.   Node              Evts Taken     Evt Rate[/s]   Data Rate[Kb/s]";
         for (i = 0;; i++) {
            db_enum_key(hDB, hKey, i, &hSubkey);
            if (!hSubkey)
               break;
            db_get_key(hDB, hSubkey, &key);
            if ((key.type == TID_KEY) &&
                ((strstr(key.name, "ODB")) == NULL) &&
                ((strstr(key.name, "BOR")) == NULL) &&
                ((strstr(key.name, "EOR")) == NULL)) {
               /* check if client running this equipment is present */
               /* extract client name from equipment */
               size = sizeof(equclient);
               sprintf(strtmp, "/equipment/%s/common/Frontend name", key.name);
               db_get_value(hDB, 0, strtmp, equclient, &size, TID_STRING, TRUE);
               /* search client name under /system/clients/xxx/name */
               if (cm_exist(equclient, TRUE) == CM_SUCCESS) {
                  atleastone_active = TRUE;
                  size = sizeof(equenabled);
                  sprintf(strtmp, "/equipment/%s/common/enabled", key.name);
                  db_get_value(hDB, 0, strtmp, &equenabled, &size, TID_BOOL, TRUE);

                  size = sizeof(equevtsend);
                  sprintf(strtmp, "/equipment/%s/statistics/events sent", key.name);
                  db_get_value(hDB, 0, strtmp, &equevtsend, &size, TID_DOUBLE, TRUE);

                  size = sizeof(equevtpsec);
                  sprintf(strtmp, "/equipment/%s/statistics/events per sec.", key.name);
                  db_get_value(hDB, 0, strtmp, &equevtpsec, &size, TID_DOUBLE, TRUE);

                  size = sizeof(equkbpsec);
                  sprintf(strtmp, "/equipment/%s/statistics/kBytes per sec.", key.name);
                  db_get_value(hDB, 0, strtmp, &equkbpsec, &size, TID_DOUBLE, TRUE);

                  size = sizeof(equnode);
                  sprintf(strtmp, "/equipment/%s/common/Frontend host", key.name);
                  db_get_value(hDB, 0, strtmp, equnode, &size, TID_STRING, TRUE);
                  {
                     char *pp, sdummy[257];
                     memset(sdummy, 0, 64);
                     xststr[j] = msprintf("%-11s ", key.name);
                     pp = strchr(equnode, '.');
                     if (pp != NULL)
                        xststr[j] += msprintf("%-18s", strncpy(sdummy, equnode, pp - equnode));
                     else
                        xststr[j] += msprintf("%-18s", strncpy(sdummy, equnode, sizeof(sdummy)-1));

                     if (equevtsend > 1E9)
                        xststr[j] += msprintf("%10.3lfG", equevtsend / 1E9);
                     else if (equevtsend > 1E6)
                        xststr[j] += msprintf("%10.3lfM", equevtsend / 1E6);
                     else
                        xststr[j] += msprintf("%10.0lf", equevtsend);

                     xststr[j] += "     ";
                     
                     if (equenabled) {
                        if (esc_flag) {
                           xststr[j] += msprintf("\033[7m%12.1lf\033[m", equevtpsec);
                           xststr[j] += "      ";
                           xststr[j] += msprintf("%12.1lf", equkbpsec);
                        } else {
                           xststr[j] += msprintf("%12.1lf", equevtpsec);
                           xststr[j] += "      ";
                           xststr[j] += msprintf("%12.1lf", equkbpsec);
                        }
                     } else {
                        xststr[j] += msprintf("%12.1lf", equevtpsec);
                        xststr[j] += "      ";
                        xststr[j] += msprintf("%12.1lf", equkbpsec);
                     }
                     j++;
                  }             /* get value */
               }                /* active */
            }                   /* eor==NULL */
         }                      /* for equipment */
      }
      /* Front-End message */
      if (!atleastone_active) {
         xststr[j++] = "... No Front-End currently running...";
      }
   }                            /* --- Equipment tree --- */

/* --------------------- Logger tree ----------------------------- */
/* search client name "Logger" under /system/clients/xxx/name */
   if ((cm_exist("logger", FALSE) == CM_SUCCESS)
       || (cm_exist("fal", FALSE) == CM_SUCCESS)) {
      char datadir[256];
      BOOL wd, lactive;
      char lpath[256];
      char ltype[64];
      char lstate[64];
      double levt;
      double lbyt;

      /* logger */
      xststr[j++] = "";
      size = sizeof(datadir);
      db_get_value(hDB, 0, "/logger/data dir", datadir, &size, TID_STRING, TRUE);
      std::string mesfile;
      cm_msg_get_logfile(NULL, 0, &mesfile, NULL, NULL);
      size = sizeof(wd);
      db_get_value(hDB, 0, "/logger/write data", &wd, &size, TID_BOOL, TRUE);
      xststr[j] = msprintf("Logger Data dir: %s", datadir);
      j++;
      xststr[j] = msprintf("Msg File: %s", mesfile.c_str());
      j++;

      /* check if dir exists */
      if (db_find_key(hDB, 0, "/logger/channels", &hKey) == DB_SUCCESS) {
         xststr[j++] = "Chan.   Active Type    Filename            Events Taken     KBytes Taken";
         for (i = 0;; i++) {
            db_enum_key(hDB, hKey, i, &hSubkey);
            if (!hSubkey)
               break;
            db_get_key(hDB, hSubkey, &key);
            if (key.type == TID_KEY) {
               size = sizeof(lactive);
               sprintf(strtmp, "/logger/channels/%s/settings/active", key.name);
               db_get_value(hDB, 0, strtmp, &lactive, &size, TID_BOOL, TRUE);
               sprintf(lstate, "No");
               if (lactive)
                  sprintf(lstate, "Yes");
               size = sizeof(lpath);
               sprintf(strtmp, "/logger/channels/%s/settings/Filename", key.name);
               db_get_value(hDB, 0, strtmp, lpath, &size, TID_STRING, TRUE);

               /* substitue "%d" by current run number */
               str[0] = 0;
               strcat(str, lpath);
               if (strchr(str, '%'))
                  sprintf(path, str, rn);
               else
                  strcpy(path, str);
               strcpy(lpath, path);

               size = sizeof(ltype);
               sprintf(strtmp, "/logger/channels/%s/settings/type", key.name);
               db_get_value(hDB, 0, strtmp, ltype, &size, TID_STRING, TRUE);

               size = sizeof(levt);
               sprintf(strtmp, "/logger/channels/%s/statistics/Events written", key.name);
               db_get_value(hDB, 0, strtmp, &levt, &size, TID_DOUBLE, TRUE);

               size = sizeof(lbyt);
               sprintf(strtmp, "/logger/channels/%s/statistics/Bytes written", key.name);
               db_get_value(hDB, 0, strtmp, &lbyt, &size, TID_DOUBLE, TRUE);
               lbyt /= 1024;
               if (lactive) {
                  if (esc_flag) {
                     xststr[j] = msprintf("  \033[7m%-3s\033[m", key.name);
                  } else {      /* no esc */
                     xststr[j] = msprintf("  %-3s", key.name);
                  }
                  xststr[j] += "   ";
                  if (wd == 1)
                     xststr[j] += msprintf("%-6s", lstate);
                  else
                     xststr[j] += msprintf("(%-4s)", lstate);
                  xststr[j] += " ";
                  xststr[j] += msprintf("%-7s", ltype);
                  xststr[j] += " ";
                  xststr[j] += msprintf("%-15s", lpath);
                  xststr[j] += "     ";
                  xststr[j] += msprintf("%12.0f", levt);
                  xststr[j] += "     ";
                  xststr[j] += msprintf("%12.2e", lbyt);
               } else {         /* not active */
                  xststr[j] = msprintf("  %-3s", key.name);
                  xststr[j] += "   ";
                  if (wd == 1)
                     xststr[j] += msprintf("%-6s", lstate);
                  else
                     xststr[j] += msprintf("(%-4s)", lstate);
                  xststr[j] += " ";
                  xststr[j] += msprintf("%-7s", ltype);
                  xststr[j] += " ";
                  xststr[j] += msprintf("%-15s", lpath);
                  xststr[j] += "     ";
                  xststr[j] += msprintf("%12.0f", levt);
                  xststr[j] += "     ";
                  xststr[j] += msprintf("%12.2e", lbyt);
               }
               j++;
            }                   /* key */
         }                      /* for */
      }                         /* exists */
   } else {
      xststr[j++] = msprintf("... Logger currently not running...");
   }

/* --------------------- Lazy logger tree ------------------------ */
/* include lazy if running */
/* search client name under /system/clients/xxx/name */
   {
      float cr, bs;
      INT status, nf, size, i, k;
      char bn[128], tl[128];
      HNDLE hlKey, hKey, hSubkey;
      char client_name[NAME_LENGTH];

      status = db_find_key(hDB, 0, "System/Clients", &hKey);
      if (status != DB_SUCCESS)
         return;

      k = 0;
      /* loop over all clients */
      for (i = 0;; i++) {
         status = db_enum_key(hDB, hKey, i, &hSubkey);
         if (status == DB_NO_MORE_SUBKEYS)
            break;

         if (status == DB_SUCCESS) {
            /* get client name */
            size = sizeof(client_name);
            db_get_value(hDB, hSubkey, "Name", client_name, &size, TID_STRING, TRUE);
            client_name[4] = 0; /* search only for the 4 first char */
            if (equal_ustring(client_name, "Lazy")) {
               sprintf(str, "/Lazy/%s", &client_name[5]);
               status = db_find_key(hDB, 0, str, &hlKey);
               if (status == DB_SUCCESS) {
                  size = sizeof(tl);
                  db_get_value(hDB, hlKey, "/Settings/List label", tl, &size, TID_STRING, TRUE);
                  if (*tl == '\0')
                     sprintf(tl, "<empty>");
                  size = sizeof(cr);
                  db_get_value(hDB, hlKey, "statistics/Copy progress (%)", &cr, &size, TID_DOUBLE, TRUE);
                  size = sizeof(nf);
                  db_get_value(hDB, hlKey, "statistics/Number of Files", &nf, &size, TID_INT, TRUE);
                  size = sizeof(bs);
                  db_get_value(hDB, hlKey, "statistics/Backup status (%)", &bs, &size, TID_DOUBLE, TRUE);
                  size = sizeof(bn);
                  db_get_value(hDB, hlKey, "statistics/Backup file", bn, &size, TID_STRING, TRUE);

                  if (k == 0) {
                     xststr[j++] = "";
                     //sprintf(ststr[j++],"");
                     xststr[j] = msprintf("%s %15s %25s %45s %60s", "Lazy Label", "Progress", "File name", "#files", "Total"); // FIXME
                  }
                  xststr[j] = msprintf("%15s %.0f[%%] %s %i %.1f[%%]", tl, cr, bn, nf, bs); // FIXME
                  k++;
               }
            }
         }
      }
   }

   xststr[j++] = "";

/* --------------------- System client list ---------------------- */
/* Get current Client listing */
   if (db_find_key(hDB, 0, "/system/clients", &hKey) == DB_SUCCESS) {
      char clientn[256], clienth[256];
      char *pp, sdummy[64];

      xststr[j] = "Clients:";
      for (int i = 0;; i++) {
         db_enum_key(hDB, hKey, i, &hSubkey);
         if (!hSubkey)
            break;
         db_get_key(hDB, hSubkey, &key);

         memset(strtmp, 0, sizeof(strtmp));
         size = sizeof(clientn);
         sprintf(strtmp, "name");
         db_get_value(hDB, hSubkey, strtmp, clientn, &size, TID_STRING, TRUE);
         memset(strtmp, 0, sizeof(strtmp));
         size = sizeof(clienth);
         sprintf(strtmp, "host");
         db_get_value(hDB, hSubkey, strtmp, clienth, &size, TID_STRING, TRUE);
         memset(sdummy, 0, 64);
         pp = strchr(clienth, '.');
         if (pp != NULL)
            xststr[j] += msprintf(" %s/%s", clientn, strncpy(sdummy, clienth, pp - clienth));
         else
            xststr[j] += msprintf(" %s/%s", clientn, clienth);
      }
      j++;
   }

   if (loop == 1) {
      xststr[j++] = msprintf("*- [!] to Exit ------- [R] to Refresh ---------------------- Delay:%2.i [sec]-*", delta_time / 1000);
   } else {
      xststr[j++] = "*---------------------------------------------------------------------------*";
   }

   cur_max_line = j;

   ///* remove '/0' */
   //for (int j = 0; j < MAX_LINE; j++)
   //   while (strlen(ststr[j]) < (LINE_LENGTH - 1))
   //      ststr[j][strlen(ststr[j])] = ' ';
   return;
}

/*------------------------------------------------------------------*/
int main(int argc, char **argv)
{
   INT status, last_time = 0, file_mode;
   HNDLE hDB, hKey;
   char host_name[HOST_NAME_LENGTH], expt_name[NAME_LENGTH], str[32];
   char svpath[256], strdis[80];
   signed char ch;
   INT fHandle, i, j = 0, last_max_line = 0;
   INT msg;
   BOOL debug;

   esc_flag = ESC_FLAG;

   /* set default */
   cm_get_environment(host_name, sizeof(host_name), expt_name, sizeof(expt_name));
   svpath[0] = 0;
   file_mode = 1;
   loop = 0;
   delta_time = 5000;

   /* get parameters */
   /* parse command line parameters */
   for (i = 1; i < argc; i++) {
      if (argv[i][0] == '-' && argv[i][1] == 'd')
         debug = TRUE;
      else if (strncmp(argv[i], "-l", 2) == 0)
         loop = 1;
      else if (argv[i][0] == '-') {
         if (i + 1 >= argc || argv[i + 1][0] == '-')
            goto usage;
         if (strncmp(argv[i], "-w", 2) == 0)
            delta_time = 1000 * (atoi(argv[++i]));
         else if (strncmp(argv[i], "-f", 2) == 0)
            strcpy(svpath, argv[++i]);
         else if (strncmp(argv[i], "-e", 2) == 0)
            strcpy(expt_name, argv[++i]);
         else if (strncmp(argv[i], "-h", 2) == 0)
            strcpy(host_name, argv[++i]);
         else if (strncmp(argv[i], "-c", 2) == 0) {
            strcpy(str, argv[++i]);
            if (strncmp(str, "n", 1) == 0 || strncmp(str, "N", 1) == 0)
               file_mode = 0;
         } else {
          usage:
            printf("usage: mstat  -l (loop) -w delay (5sec) -f filename (null)\n");
            printf("              -c compose (Addrun#/norun#)\n");
            printf("             [-h Hostname] [-e Experiment]\n\n");
            return 0;
         }
      }
   }

   if (debug) // avoid complaint about unused "debug"
      status = SUCCESS;

   /* connect to experiment */
   status = cm_connect_experiment(host_name, expt_name, "MStatus", 0);
   if (status != CM_SUCCESS)
      return 1;

#ifdef _DEBUG
   cm_set_watchdog_params(TRUE, 0);
#endif

   /* turn off message display, turn on message logging */
   cm_set_msg_print(MT_ALL, 0, NULL);

   /* connect to the database */
   cm_get_experiment_database(&hDB, &hKey);

   /* generate status page */
   if (loop == 0) {
      j = 0;
      if (svpath[0] != 0) {
         compose_status(hDB, hKey);
         fHandle = open_log_midstat(file_mode, rn, svpath);
         esc_flag = 0;
         compose_status(hDB, hKey);
         while ((j < cur_max_line) && (xststr[j][0] != '\0')) {
            int wr;
            strncpy(svpath, xststr[j].c_str(), 80);
            svpath[80] = '\0';
            printf("%s\n", svpath);
            wr = write(fHandle, "\n", 1);
            assert(wr == 1);
            wr = write(fHandle, xststr[j].c_str(), xststr[j].length());
            assert(wr == (int)xststr[j].length());
            j++;
         }
         close(fHandle);
      } else {
         esc_flag = 0;
         compose_status(hDB, hKey);
         for (int k=0; k<cur_max_line; k++) {
            printf("%s\n", xststr[k].c_str());
         }
      }
   } else {

      /* initialize ss_getchar() */
      ss_getchar(0);

      ss_clear_screen();

      do {
         if ((ss_millitime() - last_time) > delta_time) {
            last_time = ss_millitime();
            compose_status(hDB, hKey);
            if (cur_max_line < last_max_line)
               ss_clear_screen();
            last_max_line = cur_max_line;

            for (int j=0; j<cur_max_line; j++) {
               strlcpy(strdis, xststr[j].c_str(), sizeof(strdis));
               ss_printf(0, j, "%s", xststr[j].c_str());
            }
         }
         ch = 0;
         while (ss_kbhit()) {
            ch = ss_getchar(0);
            if (ch == -1)
               ch = getchar();
            if (ch == 'R')
               ss_clear_screen();
            if ((char) ch == '!')
               break;
         }
         msg = cm_yield(200);
      } while (msg != RPC_SHUTDOWN && msg != SS_ABORT && ch != '!');
   }
   printf("\n");

   /* reset terminal */
   ss_getchar(TRUE);

   cm_disconnect_experiment();
   return 1;
}
