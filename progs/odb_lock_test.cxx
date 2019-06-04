//
// odb_lock_test: test odb locking with multiple threads
//
// Author: Konstantin Olchanski, 2017-OCT-13
//

#include "midas.h"
#include "msystem.h"

void test1(HNDLE hDB)
{
   printf("test1: test recursive ODB lock\n");
   printf("lock1\n");
   db_lock_database(hDB);
   printf("lock2\n");
   db_lock_database(hDB);
   printf("lock3\n");
   db_lock_database(hDB);
   printf("sleep\n");
   sleep(5);
   printf("unlock3\n");
   db_unlock_database(hDB);
   printf("unlock2\n");
   db_unlock_database(hDB);
   printf("sleep\n");
   sleep(5);
   printf("unlock1\n");
   db_unlock_database(hDB);
   printf("done.\n");
}

HNDLE xhDB = -1;
BOOL xThread1done = false;

int thread1(void*)
{
   HNDLE hDB = xhDB;
   printf("t1: thread started\n");
   printf("t1: lock1\n");
   db_lock_database(hDB);
   printf("t1: lock1 done\n");
   printf("t1: sleep\n");
   sleep(5);
   printf("t1: unlock1\n");
   db_unlock_database(hDB);
   printf("t1: unlock1 done\n");
   printf("t1: thread done\n");
   xThread1done = TRUE;
   return 0;
}

void test2(HNDLE hDB)
{
   //int timeout = db_set_lock_timeout(hDB, 0);
   //db_set_lock_timeout(hDB, 10000);
   printf("test2: test multithread locking\n");
   printf("t0: lock1\n");
   db_lock_database(hDB);
   printf("t0: lock1 done\n");
   xhDB = hDB;
   ss_thread_create(thread1, NULL);
   printf("t0: sleep\n");
   sleep(5);
   printf("t0: unlock1\n");
   db_unlock_database(hDB);
   printf("t0: lock2\n");
   db_lock_database(hDB);
   printf("t0: lock2 done\n");
   printf("t0: unlock2\n");
   db_unlock_database(hDB);
   printf("t0: unlock2 done\n");
   printf("t0: waiting for thread1...\n");
   while (!xThread1done) {
      printf("t0: waiting\n");
      sleep(1);
   }
   printf("t0: done.\n");
   //db_set_lock_timeout(hDB, timeout);
}

int main(int argc, char *argv[])
{
   setbuf(stdout, NULL);
   setbuf(stderr, NULL);
   int status = 0;
   char host_name[256];
   char expt_name[256];
   host_name[0] = 0;
   expt_name[0] = 0;

   cm_get_environment(host_name, sizeof(host_name), expt_name, sizeof(expt_name));

   status = cm_connect_experiment1(host_name, expt_name, "odb_lock_test", 0, DEFAULT_ODB_SIZE, 0);
   assert(status == CM_SUCCESS);
   
   HNDLE hDB;
   status = cm_get_experiment_database(&hDB, NULL);
   assert(status == CM_SUCCESS);

   cm_set_watchdog_params(0, 0);

   test1(hDB);
   test2(hDB);

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
