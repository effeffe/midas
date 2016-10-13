/********************************************************************\

  Name:         odbinit.cxx
  Created by:   Konstantin Olchanski & Stefan Ritt

  Contents:     Initialize the MIDAS online data base.

  $Id$

\********************************************************************/

#include <stdio.h>
#include <string>

#include "midas.h"
#include "msystem.h"

#ifndef HAVE_STRLCPY
#include "strlcpy.h"
#endif

std::string to_string(int v)
{
   char buf[1024];
   sprintf(buf, "%d", v);
   return buf;
}

/*------------------------------------------------------------------*/

int main(int argc, char *argv[])
{
   INT status;
   char host_name[HOST_NAME_LENGTH];
   char exp_name[NAME_LENGTH];
   BOOL corrupted;
   HNDLE hDB;

   int odb_size = 0; // DEFAULT_ODB_SIZE;

   corrupted = FALSE;

   char exptab_filename[MAX_STRING_LENGTH];
   char exp_names[MAX_EXPERIMENT][NAME_LENGTH];

   /* get default from environment */
   status = cm_get_environment(host_name, sizeof(host_name), exp_name, sizeof(exp_name));

   printf("Checking environment... experiment name is \"%s\", remote hostname is \"%s\"\n", exp_name, host_name);

   bool cleanup = false;
   bool dry_run = false;

   /* parse command line parameters */
   for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "-g") == 0) {
         //debug = TRUE;
      } else if (strcmp(argv[i], "-n") == 0) {
         dry_run = true;
      } else if (strcmp(argv[i], "--cleanup") == 0) {
         cleanup = true;
      } else if (argv[i][0] == '-' && argv[i][1] == 'C')
         corrupted = TRUE;
      else if (argv[i][0] == '-') {
         if (i + 1 >= argc || argv[i + 1][0] == '-')
            goto usage;
         if (argv[i][1] == 'e')
            strcpy(exp_name, argv[++i]);
         else if (argv[i][1] == 's')
            odb_size = atoi(argv[++i]);
         else {
          usage:
            printf("usage: odbinit [options...]\n");
            printf("options:\n");
            printf("               [-e Experiment] --- specify experiment name\n");
            printf("               [-s size] --- specify new size of ODB in bytes, default is %d\n", DEFAULT_ODB_SIZE);
            printf("               [--cleanup] --- cleanup (preserve) old (existing) ODB files\n");
            printf("               [-n] --- dry run, report everything that will be done, but do not actually do anything\n");
            printf("               [-g] --- debug\n");
            printf("               [-C (connect to corrupted ODB)]\n");
            return 0;
         }
      } else
         strlcpy(host_name, argv[i], sizeof(host_name));
   }

   printf("Checking command line... experiment \"%s\", cleanup %d, dry_run %d\n", exp_name, cleanup, dry_run);

   status = cm_list_experiments(host_name, exp_names);

   status = cm_get_exptab_filename(exptab_filename, sizeof(exptab_filename));

   printf("Checking exptab... experiments defined in exptab file \"%s\":\n", exptab_filename);

   bool found_exp = false;
   for (int i=0; i<MAX_EXPERIMENT; i++) {
      if (exp_names[i][0] == 0)
         break;
      printf("%d: \"%s\"", i, exp_names[i]);
      if (exp_name[0] == 0)
         strlcpy(exp_name, exp_names[i], sizeof (exp_name));
      if (equal_ustring(exp_names[i], exp_name)) {
         printf(" <-- selected experiment");
         strlcpy(exp_name, exp_names[i], sizeof (exp_name));
         found_exp = true;
      }
      printf("\n");
   }

   if (!found_exp) {
      printf("Specified experiment \"%s\" not found in exptab. Sorry...\n", exp_name);
      exit(1);
   }

   char exp_dir[MAX_STRING_LENGTH];
   char exp_user[MAX_STRING_LENGTH];

   status = cm_get_exptab(exp_name, exp_dir, sizeof(exp_dir), exp_user, sizeof(exp_user));

   if (status != CM_SUCCESS) {
      printf("Specified experiment \"%s\" not found in exptab, cm_get_exptab() returned %d. Sorry...\n", exp_name, status);
      exit(1);
   }

   printf("\n");
   printf("Checking exptab... selected experiment \"%s\", experiment directory \"%s\"\n", exp_name, exp_dir);

   cm_set_experiment_name(exp_name);
   cm_set_path(exp_dir);


   printf("\n");

   {
      printf("Checking experiment directory \"%s\"\n", exp_dir);

#ifdef S_ISDIR
      struct stat stat_buf;
      int v = stat(exp_dir, &stat_buf);

      if (v != 0) {
         printf("Invalid experiment directory \"%s\" does not seem to exist, stat() returned %d, errno %d (%s)\n", exp_dir, v, errno, strerror(errno));
         printf("Sorry.\n");
         exit(1);
      }

      if (!S_ISDIR(stat_buf.st_mode)) {
         printf("Invalid experiment directory \"%s\" is not a directory\n", exp_dir);
         printf("Sorry.\n");
         exit(1);
      }
#else
#error No support for stat() and S_ISDIR on this system!
#endif
   }
   
   std::string odb_path;

   {
      std::string path;
      path += exp_dir;
      path += ".ODB.SHM";

      FILE *fp = fopen(path.c_str(), "r");
      if (fp) {
         fclose(fp);
         printf("Found existing ODB save file: \"%s\"\n", path.c_str());
         if (cleanup) {
            // cannot get rid of ODB save file yet - it is used as SysV semaphore key
            // so delete semaphore first, delete .ODB.SHM next.
            // hope deleting the semaphore and crashing all MIDAS programs does not corrupt the ODB save file.
            odb_path = path;
         } else {
            printf("Looks like this experiment ODB is already initialized.\n");
            printf("To create new empty ODB, please rerun odbinit with the \"--cleanup\" option.\n");
            exit(1);
         }
      } else {
         printf("Good: no ODB save file\n");
      }
   }

   printf("\n");
   printf("Checking shared memory...\n");

   {
      printf("Deleting old ODB shared memory...\n");
      if (!dry_run) {
         status = ss_shm_delete("ODB");
         if (status == SS_NO_MEMORY) {
            printf("Good: no ODB shared memory\n");
         } else if (status == SS_SUCCESS) {
            printf("Deleted existing ODB shared memory, please check that all MIDAS programs are stopped and try again.\n");
            exit(1);
         } else {
            printf("ss_shm_delete(ODB) status %d\n", status);
            printf("Please check that all MIDAS programs are stopped and try again.\n");
            exit(1);
         }
      }
   }
   
   {
      printf("Deleting old ODB semaphore...\n");
      if (!dry_run) {
         HNDLE sem;
         int cstatus = ss_semaphore_create("ODB", &sem);
         int dstatus = ss_semaphore_delete(sem, TRUE);
         printf("Deleting old ODB semaphore... create status %d, delete status %d\n", cstatus, dstatus);
      }
   }
   
   if (odb_path.length() > 0 && cleanup) {
      time_t now = time(NULL);
      std::string path1;
      path1 += odb_path;
      path1 += ".";
      path1 += to_string(now);
      printf("Preserving old ODB save file \%s\" to \"%s\"\n", odb_path.c_str(), path1.c_str());
      if (!dry_run) {
         status = rename(odb_path.c_str(), path1.c_str());
         if (status != 0) {
            printf("rename(%s, %s) returned %d, errno %d (%s)\n", odb_path.c_str(), path1.c_str(), status, errno, strerror(errno));
            printf("Sorry.\n");
            exit(1);
         }
      }
   }

   printf("\n");

   {
      printf("Checking ODB size...\n");
      printf("Requested ODB size is %d bytes\n", odb_size);

      std::string path1;
      path1 += exp_dir;
      path1 += "/";
      path1 += ".ODB_SIZE.TXT";

      printf("ODB size file is \"%s\"\n", path1.c_str());

      FILE *fp = fopen(path1.c_str(), "r");
      if (!fp) {
         printf("ODB size file \"%s\" does not exist, creating it...\n", path1.c_str());
         fp = fopen(path1.c_str(), "w");
         if (!fp) {
            printf("Cannot create ODB size file \"%s\", fopen() errno %d (%s)\n", path1.c_str(), errno, strerror(errno));
            printf("Sorry.\n");
            exit(1);
         }
         if (odb_size == 0)
            fprintf(fp, "%d\n", DEFAULT_ODB_SIZE);
         else
            fprintf(fp, "%d\n", odb_size);
         fclose(fp);

         fp = fopen(path1.c_str(), "r");
         if (!fp) {
            printf("Creation of ODB size file \"%s\" somehow failed.\n", path1.c_str());
            printf("Sorry.\n");
            exit(1);
         }
      }

      int file_odb_size = 0;
      fscanf(fp, "%d", &file_odb_size);
      fclose(fp);

      printf("Saved ODB size from \"%s\" is %d bytes\n", path1.c_str(), file_odb_size);

      if (odb_size == 0)
         odb_size = file_odb_size;

      if (file_odb_size != odb_size) {
         printf("Requested ODB size %d is different from previous ODB size %d. You have 2 choices:\n", odb_size, file_odb_size);
         printf("1) to create ODB with old size, please try again without the \"-s\" switch.\n");
         printf("2) to create ODB with new size, please delete the file \"%s\" and try again.\n", path1.c_str());
         exit(1);
      }
   }
   
   printf("We will initialize ODB for experiment \"%s\" on host \"%s\" with size %d bytes\n", exp_name, host_name, odb_size);
   printf("\n");

   status = cm_connect_experiment1(host_name, exp_name, "ODBInit", NULL, odb_size, DEFAULT_WATCHDOG_TIMEOUT);

   if (status == CM_WRONG_PASSWORD)
      return 1;

   printf("Connected to ODB for experiment \"%s\" on host \"%s\" with size %d bytes\n", exp_name, host_name, odb_size);

   cm_msg_flush_buffer();

   if ((status == DB_INVALID_HANDLE) && corrupted) {
      char str[2000];
      cm_get_error(status, str);
      puts(str);
      printf("ODB is corrupted, connecting anyway...\n");
   } else if (status != CM_SUCCESS) {
      char str[2000];
      cm_get_error(status, str);
      puts(str);
      return 1;
   }

   /* get experiment name */
   if (!exp_name[0]) {
      cm_get_experiment_database(&hDB, NULL);
      int size = NAME_LENGTH;
      db_get_value(hDB, 0, "/Experiment/Name", exp_name, &size, TID_STRING, TRUE);
   }

   cm_disconnect_experiment();

   if (status != 1)
      return EXIT_FAILURE;

   return EXIT_SUCCESS;
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
