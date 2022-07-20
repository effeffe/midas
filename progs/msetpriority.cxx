// msetpriority.cxx - set real time priority for event builder, etc
//
// this program has to be setuid-root: su - root; chown root; chmod u+s
//
// g++ -o bin/msetpriority -O2 -g -Wall progs/msetpriority.cxx
//

#include <stdio.h>
#include <unistd.h> // getuid()
#include <sys/types.h> // getuid()
#include <stdlib.h> // exit()
#include <errno.h> // errno
#include <string.h> // strerror()
#include <sched.h> // sched_setscheduler()
#include <sys/resource.h> // setpriority()


int main(int argc, char **argv)
{
   int status;

   // check if we are root

   uid_t uid = getuid();
   uid_t euid = geteuid();

   //printf("uid %d, euid %d\n", uid, euid);

   if (euid != 0) {
      fprintf(stderr, "%s: must be setuid-root, please do: chown root, chmod u+s\n", argv[0]);
      exit(1);
   }

#if 1
   struct sched_param sparam; 
   sparam.sched_priority = 50; 
   //status = sched_setscheduler(0, SCHED_RR, &sparam); 
   status = sched_setscheduler(0, SCHED_FIFO, &sparam); 

   if (status < 0) {
      fprintf(stderr, "%s: sched_setscheduler() returned %d, errno %d (%s)\n", argv[0], status, errno, strerror(errno));
      exit(1);
   }
#endif

#if 0
   status = setpriority(PRIO_PROCESS, 0, -20); 

   if (status < 0) {
      fprintf(stderr, "%s: setpriority() returned %d, errno %d (%s)\n", argv[0], status, errno, strerror(errno));
      exit(1);
   }
#endif 

   // surrender root privileges
   status = setuid(uid);

   if (status < 0) {
      fprintf(stderr, "%s: cannot surrender root priveleges, setuid(%d) returned %d, errno %d (%s)\n", argv[0], uid, status, errno, strerror(errno));
      exit(1);
   }

   if (argc < 2) {
      fprintf(stderr, "Usage: %s program [arguments...]\n", argv[0]);
      exit(1);
   }

   status = execvp(argv[1], &argv[1]); // does not return unless error.

   fprintf(stderr, "%s: execvp(\"%s\") returned %d, errno %d (%s)\n", argv[0], argv[1], status, errno, strerror(errno));
   exit(1);
}

// end

