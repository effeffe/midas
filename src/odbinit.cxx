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

/*------------------------------------------------------------------*/

int main(int argc, char *argv[])
{
   INT status, i, size;
   char host_name[HOST_NAME_LENGTH];
   char exp_name[NAME_LENGTH];
   char cmd[2000], dir[256], str[2000];
   BOOL debug;
   BOOL corrupted;
   BOOL reload_from_file = FALSE;
   HNDLE hDB;

   int odb_size = DEFAULT_ODB_SIZE;

   cmd[0] = dir[0] = 0;
   debug = corrupted = FALSE;

   char exptab_filename[MAX_STRING_LENGTH];
   char exp_names[MAX_EXPERIMENT][NAME_LENGTH];

   /* get default from environment */
   status = cm_get_environment(host_name, sizeof(host_name), exp_name, sizeof(exp_name));

   printf("Checking environment... experiment name is \"%s\", remote hostname is \"%s\"\n", exp_name, host_name);

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
         printf(" *** selected experiment ***");
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
   
   printf("Checking exptab... selected experiment \"%s\", experiment directory \"%s\"\n", exp_name, exp_dir);
   
   /* parse command line parameters */
   for (i = 1; i < argc; i++) {
      if (argv[i][0] == '-' && argv[i][1] == 'g')
         debug = TRUE;
      else if (argv[i][0] == '-' && argv[i][1] == 'R')
         reload_from_file = TRUE;
      else if (argv[i][0] == '-' && argv[i][1] == 'C')
         corrupted = TRUE;
      else if (argv[i][0] == '-') {
         if (i + 1 >= argc || argv[i + 1][0] == '-')
            goto usage;
         if (argv[i][1] == 'e')
            strcpy(exp_name, argv[++i]);
         else if (argv[i][1] == 'h')
            strcpy(host_name, argv[++i]);
         else if (argv[i][1] == 's')
            odb_size = atoi(argv[++i]);
         else {
          usage:
            printf("usage: odbedit [-h Hostname] [-e Experiment]\n");
            printf("               [-s size]\n");
            printf("               [-g (debug)] [-C (connect to corrupted ODB)]\n");
            printf("               [-R (reload ODB from .ODB.SHM)]\n\n");
            return 0;
         }
      } else
         strlcpy(host_name, argv[i], sizeof(host_name));
   }

   printf("We will initialize ODB for experiment \"%s\" on host \"%s\" with size %d bytes\n", exp_name, host_name, odb_size);

   status = cm_connect_experiment1(host_name, exp_name, "ODBEdit", NULL, odb_size, DEFAULT_WATCHDOG_TIMEOUT);

   if (status == CM_WRONG_PASSWORD)
      return 1;

   printf("Connected to ODB for experiment \"%s\" on host \"%s\" with size %d bytes\n", exp_name, host_name, odb_size);

   cm_msg_flush_buffer();

   if (reload_from_file) {
      status = ss_shm_delete("ODB");
      printf("ss_shm_delete(ODB) status %d\n", status);
      printf("Please run odbedit again without \'-R\' and ODB will be reloaded from .ODB.SHM\n");
      return 1;
   }

   if ((status == DB_INVALID_HANDLE) && corrupted) {
      cm_get_error(status, str);
      puts(str);
      printf("ODB is corrupted, connecting anyway...\n");
   } else if (status != CM_SUCCESS) {
      cm_get_error(status, str);
      puts(str);
      return 1;
   }

   /* get experiment name */
   if (!exp_name[0]) {
      cm_get_experiment_database(&hDB, NULL);
      size = NAME_LENGTH;
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
