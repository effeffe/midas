//
// get_record_test: test db_create_record(), db_get_record(), db_set_record(), etc
//
// Author: Konstantin Olchanski, 2017-OCT-11
//

#include "midas.h"
#include "msystem.h"
#include "history.h"
#ifndef HAVE_STRLCPY
#include "strlcpy.h"
#endif

typedef struct {
   INT       ivalue;
   INT       iarray[2];
   char      svalue[20];
   char      sarray[2][32];
} test1_struct;

void print_test1(const test1_struct* s)
{
   printf("test1_struct: ivalue %d, iarray %d %d, svalue [%s], sarray [%s] [%s]\n", s->ivalue, s->iarray[0], s->iarray[1], s->svalue, s->sarray[0], s->sarray[1]);
}

#define test1_STR "\
ivalue = INT : 1\n\
iarray = INT[2] : \n\
[0] 1\n\
[1] 2\n\
svalue = STRING : [20] /Runinfo/Run number\n\
sarray = STRING[2] : \n\
[32] str1\n\
[32] str2\n\
"

void test1(HNDLE hDB, HNDLE hKey)
{
   int status;
   printf("test1!\n");

   HNDLE hh;
   db_find_key(hDB, hKey, "test1", &hh);
   if (hh) {
      printf("already exists, skipping!\n");
      return;
   }
   printf("create test1\n");
   status = db_create_record(hDB, hKey, "test1", test1_STR);
   printf("db_create_record status %d\n", status);
}

void test1a(HNDLE hDB, HNDLE hKey)
{
   int status;
   printf("test1a!\n");

   printf("check test1\n");
   status = db_check_record(hDB, hKey, "test1", test1_STR, TRUE);
   printf("db_check_record status %d\n", status);
}

void test1b(HNDLE hDB, HNDLE hKey)
{
   int status;
   printf("test1b!\n");

   test1_struct s;

   HNDLE hh;
   db_find_key(hDB, hKey, "test1", &hh);

   printf("get test1\n");
   int size = sizeof(s);
   status = db_get_record(hDB, hh, &s, &size, 0);
   printf("db_get_record status %d, size %d/%d\n", status, (int)sizeof(s), size);
   print_test1(&s);
}

void test1c(HNDLE hDB, HNDLE hKey)
{
   int status;
   printf("test1c - db_get_record1!\n");

   test1_struct s;

   HNDLE hh;
   db_find_key(hDB, hKey, "test1", &hh);

   printf("get test1\n");
   int size = sizeof(s);
   status = db_get_record1(hDB, hh, &s, &size, 0, test1_STR);
   printf("db_get_record1 status %d, size %d/%d\n", status, (int)sizeof(s), size);
   print_test1(&s);
}

void test1d(HNDLE hDB, HNDLE hKey)
{
   int status;
   printf("test1d - db_get_record2!\n");

   test1_struct s;

   HNDLE hh;
   db_find_key(hDB, hKey, "test1", &hh);

   printf("get test1\n");
   int size = sizeof(s);
   status = db_get_record2(hDB, hh, &s, &size, 0, test1_STR, 0);
   printf("db_get_record2 status %d, size %d/%d\n", status, (int)sizeof(s), size);
   print_test1(&s);
}

typedef struct {
   WORD      wvalue;
   DWORD     dwvalue;
   double    dvalue;
   char      cvalue;
   DWORD     dwvalue2;
   float     fvalue;
   double    dvalue2;
   char      cvalue2;
   char      svalue[10];
   double    dvalue3;
} test2_struct;

void print_test2(const test2_struct* s)
{
   printf("test2_struct: wvalue %d, dwvalue %d, dvalue %f, cvalue %d, dwvalue2 %d, fvalue %f, dvalue2 %f, cvalue2 %d, svalue [%s], dvalue3 %f\n", s->wvalue, s->dwvalue, s->dvalue, s->cvalue, s->dwvalue2, s->fvalue, s->dvalue2, s->cvalue2, s->svalue, s->dvalue3);
}

#define test2_STR "\
wvalue = WORD : 1\n\
dwvalue = DWORD : 2\n\
dvalue = DOUBLE : 3.3\n\
cvalue = CHAR : 4\n\
dwvalue2 = DWORD : 5\n\
fvalue = FLOAT : 6.6\n\
dvalue2 = DOUBLE : 7.7\n\
cvalue2 = CHAR : 8\n\
svalue = STRING : [10] 99999\n\
dvalue3 = DOUBLE : 10.01\n\
"

void test2(HNDLE hDB, HNDLE hKey)
{
   int status;
   printf("test2!\n");

   HNDLE hh;
   db_find_key(hDB, hKey, "test2", &hh);
   if (hh) {
      printf("already exists, skipping!\n");
      return;
   }
   printf("create test2\n");
   status = db_create_record(hDB, hKey, "test2", test2_STR);
   printf("db_create_record status %d\n", status);
}

void test2b(HNDLE hDB, HNDLE hKey)
{
   int status;
   printf("test2b!\n");

   test2_struct s;

   HNDLE hh;
   db_find_key(hDB, hKey, "test2", &hh);

   printf("get test2\n");
   int size = sizeof(s);
   status = db_get_record(hDB, hh, &s, &size, 0);
   printf("db_get_record status %d, size %d/%d\n", status, (int)sizeof(s), size);
   print_test2(&s);
}

void test2d(HNDLE hDB, HNDLE hKey)
{
   int status;
   printf("test2d - db_get_record2!\n");

   test2_struct s;

   HNDLE hh;
   db_find_key(hDB, hKey, "test2", &hh);

   printf("get test2\n");
   int size = sizeof(s);
   status = db_get_record2(hDB, hh, &s, &size, 0, test2_STR, 0);
   printf("db_get_record2 status %d, size %d/%d\n", status, (int)sizeof(s), size);
   print_test2(&s);
}

int main(int argc, char *argv[])
{
   int status = 0;
   HNDLE hDB;
   char host_name[256];
   char expt_name[256];
   host_name[0] = 0;
   expt_name[0] = 0;

   cm_get_environment(host_name, sizeof(host_name), expt_name, sizeof(expt_name));

   status = cm_connect_experiment1(host_name, expt_name, "get_record_test", 0, DEFAULT_ODB_SIZE, 0);
   assert(status == CM_SUCCESS);
   
   status = cm_get_experiment_database(&hDB, NULL);
   assert(status == CM_SUCCESS);

   HNDLE hKey = 0;

   test1(hDB, hKey);
   //test1a(hDB, hKey);
   //test1b(hDB, hKey);
   test1b(hDB, hKey);
   test1d(hDB, hKey);

   test2(hDB, hKey);
   test2b(hDB, hKey);
   test2d(hDB, hKey);

   status = cm_disconnect_experiment();
   assert(status == CM_SUCCESS);

   return 0;
}   

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
