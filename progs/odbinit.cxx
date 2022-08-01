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

static std::string to_string(int v)
{
   char buf[1024];
   sprintf(buf, "%d", v);
   return buf;
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

static std::vector<std::string> remove_dot_dot(const std::vector<std::string>& v)
{
   std::vector<std::string> out;
   for (unsigned i=0; i<v.size(); i++) {
      if (v[i] == "..") {
         if (out.size() > 0) {
            out.pop_back();
         }
      } else {
         out.push_back(v[i]);
      }
   }
   return out;
}

/*------------------------------------------------------------------*/

static void usage()
{
   printf("usage: odbinit [options...]\n");
   printf("options:\n");
   printf("  [-e Experiment] --- specify experiment name\n");
   printf("  [-s size] --- specify new size of ODB in bytes, default is %d (optional units: B, kB, MB, kiB, MiB)\n", DEFAULT_ODB_SIZE);
   printf("  [--env] --- create new env.sh and env.csh files in the current directory\n");
   printf("  [--exptab] --- create new exptab file in the current directory\n");
   printf("  [--cleanup] --- cleanup (preserve) old (existing) ODB files\n");
   printf("  [-n] --- dry run, report everything that will be done, but do not actually do anything\n");
   //printf("  [-g] --- debug\n");
   exit(1);
}

int DecodeSize(const char* s)
{
   // The disticntion between kB and kb is ignored... 
   // We assume the user means Bytes not bits

   int size=0;
   if (s) {
     //strtoul ignores any no numic characters (the UNITS)
     size = strtoul(s, NULL, 0);
   }

   const char units[] = {'k', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y'};
   int one_k=1000;
   //Search of 'i' in string (for kiB, MiB, GiB etc)
   for (size_t i=0; i<sizeof(s); i++)
   {
      if (s[i]=='i')
         one_k=1024;
   }
   //Search of major unit
   for (size_t i=0; i<sizeof(s); i++)
   {
      for (int j=0; j<8; j++)
      {
         if (s[i]==units[j])
         {
            //Only the first unit is used... kMB is meaningless
            while (j>-1)
            {
               size*=one_k;
               j--;
            }
            return size;
         }
      }
   }
   return size;
}

std::pair<double,std::string> HumanUnits(int odb_size)
{
   int unit_index=0;
   double odb_human_size=(double)odb_size;
   const char* units[] = {"B", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
   while (odb_human_size > 1000) {
     odb_human_size /= 1000;
     unit_index++;
   }
   return {odb_human_size,units[unit_index]};
}

/*------------------------------------------------------------------*/

int main(int argc, char *argv[])
{
   // set unbuffered output
   setbuf(stdout, NULL);
   setbuf(stderr, NULL);

   INT status;
   char host_name[HOST_NAME_LENGTH];
   char exp_name[NAME_LENGTH];

   int odb_size = 0; // DEFAULT_ODB_SIZE;

   /* get default from environment */
   status = cm_get_environment(host_name, sizeof(host_name), exp_name, sizeof(exp_name));

   printf("Checking environment... experiment name is \"%s\", remote hostname is \"%s\"\n", exp_name, host_name);

   if (strlen(host_name) > 0) {
      printf("Error: trying to use a remote connection to host \"%s\". odbinit must run locally. Sorry.\n", host_name);
      exit(1);
   }

   bool cleanup = false;
   bool dry_run = false;
   bool create_exptab = false;
   bool create_env = false;

   /* parse command line parameters */
   for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "-g") == 0) {
         //debug = TRUE;
      } else if (strcmp(argv[i], "-n") == 0) {
         dry_run = true;
      } else if (strcmp(argv[i], "--exptab") == 0) {
         create_exptab = true;
      } else if (strcmp(argv[i], "--env") == 0) {
         create_env = true;
      } else if (strcmp(argv[i], "--cleanup") == 0) {
         cleanup = true;
      } else if (strcmp(argv[i], "-e") == 0) {
         i++;
         strlcpy(exp_name, argv[i], sizeof(exp_name));
      } else if (strcmp(argv[i], "-s") == 0) {
         i++;
         odb_size = DecodeSize(argv[i]);
      } else {
         usage(); // DOES NOT RETURN
      }
   }

   printf("Checking command line... experiment \"%s\", cleanup %d, dry_run %d, create_exptab %d, create_env %d\n", exp_name, cleanup, dry_run, create_exptab, create_env);

#ifndef NO_LOCAL_ROUTINES

   if (create_exptab) {
      printf("Creating a new exptab file in the current directory...\n");

      FILE *fpr = fopen("exptab", "r");
      if (fpr) {
         fclose(fpr);
         printf("Error: exptab already exists in the current directory. Sorry...\n");
         exit(1);
      }

      if (strlen(exp_name) < 1) {
         strlcpy(exp_name, "Default", sizeof(exp_name));
      }
      if (strchr(exp_name, ' ')) {
         printf("Error: experiment name \"%s\" should not contain a space character. Sorry...\n", exp_name);
         exit(1);
      }
      const char* pwd = getenv("PWD");
      if (!pwd || strlen(pwd)<1) {
         printf("Error: env.variable PWD is not defined or is empty. Sorry...\n");
         exit(1);
      }
      if (strchr(pwd, ' ')) {
         printf("Error: env.variable PWD value \"%s\" should not contain a space character. Sorry...\n", pwd);
         exit(1);
      }
      const char* user = getenv("USER");
      if (!user || strlen(user)<1) {
         printf("Error: env.variable USER is not defined or is empty. Sorry...\n");
         exit(1);
      }
      if (strchr(user, ' ')) {
         printf("Error: env.variable USER value \"%s\" should not contain a space character. Sorry...\n", user);
         exit(1);
      }
      printf("Experiment name [%s], experiment directory [%s], username [%s]\n", exp_name, pwd, user);

      FILE* fp = fopen("exptab", "w");
      if (!fp) {
         printf("Error: Cannot write exptab file, fopen() errno %d (%s). Sorry...\n", errno, strerror(errno));
         exit(1);
      }

      fprintf(fp, "%s %s %s\n", exp_name, pwd, user);
      fclose(fp);

      printf("\n");
      printf("Please define env.variable MIDAS_EXPTAB=%s/%s\n", pwd, "exptab");
      printf("\n");
      printf("Done\n");
      exit(0);
   }

   if (create_env) {
      printf("Creating a new environment settings file in the current directory...\n");

      // check if files already exist

      FILE *fpr = fopen("env.sh", "r");
      if (fpr) {
         fclose(fpr);
         printf("Error: env.sh already exists in the current directory. Sorry...\n");
         exit(1);
      }

      fpr = fopen("env.csh", "r");
      if (fpr) {
         fclose(fpr);
         printf("Error: env.csh already exists in the current directory. Sorry...\n");
         exit(1);
      }

      // construct MIDAS_EXPTAB

      const char* pwd = getenv("PWD");
      if (!pwd || strlen(pwd)<1) {
         printf("Error: env.variable PWD is not defined or is empty. Sorry...\n");
         exit(1);
      }

      std::string midas_exptab;
      midas_exptab += pwd;
      midas_exptab += DIR_SEPARATOR;
      midas_exptab += "exptab";

      // try to extract midas location from argv[0]

      std::string argv0 = argv[0];

      if (argv0[0] != '/') {
         printf("\n");
         printf("Please run odbinit using the full path, i.e. run $HOME/packages/midas/linux/bin/odbinit\n");
         printf("\n");
         printf("Bye\n");
         exit(1);
      }

      // construct MIDASSYS

      std::string midassys;

      if (argv0[0] == '/') { // absolute path
         midassys += argv0;
      } else {
         midassys += pwd;
         midassys += "/";
         midassys += argv0;
      }

      std::vector<std::string> aa = split("/", midassys);
      if (aa.size() > 0) { // remove "odbinit"
         aa.pop_back();
      }
      if (aa.size() > 0) { // remove "bin"
         aa.pop_back();
      }
      if (aa.size() > 0) { // remove "darwin" or "linux"
         aa.pop_back();
      }

      midassys = join("/", remove_dot_dot(aa));

      // construct MIDAS path

      std::string path;

      {
         if (argv0[0] == '/') { // absolute path
            path += argv0;
         } else {
            path += pwd;
            path += "/";
            path += argv0;
         }

         std::vector<std::string> aa = split("/", path);
         if (aa.size() > 0) { // remove "odbinit"
            aa.pop_back();
         }

         path = join("/", remove_dot_dot(aa));
      }

      std::string hostname;

      {
         char s[256];
         gethostname(s, sizeof(s));
         hostname = s;
      }

      // start writing

      FILE *fpb = fopen("env.sh", "w");
      if (!fpb) {
         printf("Error: Cannot write env.sh file, fopen() errno %d (%s). Sorry...\n", errno, strerror(errno));
         exit(1);
      }

      FILE *fpc = fopen("env.csh", "w");
      if (!fpc) {
         printf("Error: Cannot write env.csh file, fopen() errno %d (%s). Sorry...\n", errno, strerror(errno));
         exit(1);
      }

      fprintf(fpb, "#!echo You must source\n");
      fprintf(fpc, "#!echo You must source\n");

      fprintf(fpb, "export MIDASSYS=\"%s\"\n", midassys.c_str());
      fprintf(fpc, "setenv MIDASSYS \"%s\"\n", midassys.c_str());

      fprintf(fpb, "export MIDAS_EXPTAB=\"%s\"\n", midas_exptab.c_str());
      fprintf(fpc, "setenv MIDAS_EXPTAB \"%s\"\n", midas_exptab.c_str());

      fprintf(fpb, "export PATH=$PATH:\"%s\"\n", path.c_str());
      fprintf(fpc, "setenv PATH $PATH\\:\"%s\"\n", path.c_str());

      fprintf(fpb, "# define mserver connection\n");
      fprintf(fpb, "case `hostname` in\n");
      fprintf(fpb, "%s) unset MIDAS_SERVER_HOST ;;\n", hostname.c_str());
      fprintf(fpb, "*) export MIDAS_SERVER_HOST=%s ;;\n", hostname.c_str());
      fprintf(fpb, "esac\n");

      fprintf(fpc, "# define mserver connection\n");
      fprintf(fpc, "switch (`hostname`)\n");
      fprintf(fpc, "case %s:\n", hostname.c_str());
      fprintf(fpc, "unsetenv MIDAS_SERVER_HOST\n");
      fprintf(fpc, "breaksw\n");
      fprintf(fpc, "default:\n");
      fprintf(fpc, "setenv MIDAS_SERVER_HOST %s\n", hostname.c_str());
      fprintf(fpc, "endsw\n");

      fprintf(fpb, "#end\n");
      fprintf(fpc, "#end\n");

      fclose(fpb);
      fclose(fpc);

      printf("\n");
      printf("Please source env.sh or env.csh\n");
      printf("\n");
      printf("Done\n");
      exit(0);
   }

   // check MIDASSYS

   printf("Checking MIDASSYS...");
   const char* midassys = getenv("MIDASSYS");

   if (!midassys) {
      printf("\n");
      printf("Error: Env.variable MIDASSYS is not defined.\n");
      printf("odbinit path [%s]\n", argv[0]);
      printf("\n");
      printf("Please run odbinit --env\n");
      printf("\n");
      printf("Bye.\n");
      exit(1);
   }

   printf("...%s\n", midassys);

   STRING_LIST exp_names;
   status = cm_list_experiments_local(&exp_names);

   if (status != CM_SUCCESS) {
      printf("Error: cm_list_experiments() status %d\n", status);
      printf("\n");
      printf("Cannot get the list of experiments, maybe the exptab file is missing.\n");
      printf("\n");
      printf("To create a new exptab file in the current directory, run odbinit --exptab -e new_experiment_name\n");
      printf("\n");
      printf("Bye...\n");
      exit(1);
   }

   char exptab_filename[MAX_STRING_LENGTH];

   status = cm_get_exptab_filename(exptab_filename, sizeof(exptab_filename));

   if (status != CM_SUCCESS) {
      printf("Error: cm_get_exptab_filename() status %d\n", status);
      printf("\n");
      printf("Cannot get the name of the exptab file. Sorry...\n");
      exit(1);
   }


   printf("Checking exptab... experiments defined in exptab file \"%s\":\n", exptab_filename);

   bool found_exp = false;
   for (unsigned i=0; i<exp_names.size(); i++) {
      printf("%d: \"%s\"", i, exp_names[i].c_str());
      if (exp_name[0] == 0)
         strlcpy(exp_name, exp_names[i].c_str(), sizeof (exp_name));
      if (equal_ustring(exp_names[i].c_str(), exp_name)) {
         printf(" <-- selected experiment");
         strlcpy(exp_name, exp_names[i].c_str(), sizeof (exp_name));
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
   ss_suspend_init_odb_port();

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
      printf("Preserving old ODB save file \"%s\" to \"%s\"\n", odb_path.c_str(), path1.c_str());
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

      std::pair<double,std::string> odb_size_human=HumanUnits(odb_size);

      printf("Requested ODB size is %d bytes (%.2f%s)\n", odb_size,odb_size_human.first,odb_size_human.second.c_str());

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
         {
            std::pair<double,std::string> default_odb_size_human=HumanUnits(DEFAULT_ODB_SIZE);
            fprintf(fp, "%d (%.2f%s)\n", DEFAULT_ODB_SIZE,default_odb_size_human.first,default_odb_size_human.second.c_str());
         }
         else
         {
            fprintf(fp, "%d (%.2f%s)\n", odb_size,odb_size_human.first,odb_size_human.second.c_str());
         }
         fclose(fp);

         fp = fopen(path1.c_str(), "r");
         if (!fp) {
            printf("Creation of ODB size file \"%s\" somehow failed.\n", path1.c_str());
            printf("Sorry.\n");
            exit(1);
         }
      }

      int file_odb_size = 0;
      std::pair<double,std::string> file_odb_size_human;
      {
         char buf[256];
         char *s = fgets(buf, sizeof(buf), fp);
         if (s) {
            file_odb_size = DecodeSize(s);
            file_odb_size_human=HumanUnits(file_odb_size);
         }
      }
      fclose(fp);

      printf("Saved ODB size from \"%s\" is %d bytes (%.2f%s)\n", path1.c_str(), file_odb_size,file_odb_size_human.first,file_odb_size_human.second.c_str());

      if (odb_size == 0)
         odb_size = file_odb_size;

      if (file_odb_size != odb_size) {
         printf("Requested ODB size %d is different from previous ODB size %d. You have 2 choices:\n", odb_size, file_odb_size);
         printf("1) to create ODB with old size, please try again without the \"-s\" switch.\n");
         printf("2) to create ODB with new size, please delete the file \"%s\" and try again.\n", path1.c_str());
         exit(1);
      }
   }
   std::pair<double,std::string> odb_size_human=HumanUnits(odb_size);
   printf("We will initialize ODB for experiment \"%s\" on host \"%s\" with size %d bytes (%.2f%s)\n", exp_name, host_name, odb_size, odb_size_human.first, odb_size_human.second.c_str());
   printf("\n");


   {
      HNDLE hDB;
      printf("Creating ODB...\n");
      status = db_open_database("ODB", odb_size, &hDB, "odbinit");
      printf("Creating ODB... db_open_database() status %d\n", status);
      if ((status != DB_SUCCESS) && (status != DB_CREATED)) {
         printf("Something went wrong... continuing...\n");
      }
      printf("Saving ODB...\n");
      status = db_close_database(hDB);
      printf("Saving ODB... db_close_database() status %d\n", status);
      if (status != DB_SUCCESS) {
         printf("Something went wrong... continuing...\n");
      }
   }

   printf("Connecting to experiment...\n");

   status = cm_connect_experiment1(host_name, exp_name, "ODBInit", NULL, 0, DEFAULT_WATCHDOG_TIMEOUT);

   if (status != CM_SUCCESS) {
      printf("Error: cm_connect_experiment() status %d\n", status);
      printf("Sorry...\n");
      exit(1);
   }

   printf("\n");
   printf("Connected to ODB for experiment \"%s\" on host \"%s\" with size %d bytes (%.2f%s)\n", exp_name, host_name, odb_size,odb_size_human.first,odb_size_human.second.c_str());

   cm_msg_flush_buffer();

   {
      HNDLE hDB;
      cm_get_experiment_database(&hDB, NULL);
      int size = NAME_LENGTH;
      char buf[NAME_LENGTH];
      status = db_get_value(hDB, 0, "/Experiment/Name", buf, &size, TID_STRING, FALSE);
      printf("Checking experiment name... status %d, found \"%s\"\n", status, buf);
      //status = db_save_json(hDB, 0, "odbinit.json");
      //printf("Saving odbinit.json... status %d\n", status);
   }

   printf("Disconnecting from experiment...\n");
   cm_disconnect_experiment();

   printf("\n");
   printf("Done\n");

#else
   printf("this version of odbinit is built with NO_LOCAL_ROUTINES and it will not work. odbinit only works locally!\n");
#endif // NO_LOCAL_ROUTINES

   return 0;
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
