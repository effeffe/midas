/********************************************************************\

  Name:         mfe_link_test_cxx.cxx
  Created by:   Konstantin Olchanski

  Contents:     Test mfe.h and mfe.c for correct linking with C++

\********************************************************************/

#include <stdio.h>
#include "midas.h"
#include "mfe.h"

/*------------------------------------------------------------------*/

const char* frontend_name = "frontend_name";
const char* frontend_file_name = "frontend_file_name";
BOOL  frontend_call_loop = 0;
int event_buffer_size = 1000;
int max_event_size = 1000;
int max_event_size_frag = 1000;
int display_period = 1000;
BOOL equipment_common_overwrite = FALSE;
EQUIPMENT equipment[1];
int frontend_init() { return 0; };
int frontend_exit() { return 0; };
int begin_of_run(int runno,char* errstr) { return 0; };
int end_of_run(int runno,char* errstr) { return 0; };
int pause_run(int runno,char* errstr) { return 0; };
int resume_run(int runno,char* errstr) { return 0; };
int interrupt_configure(INT cmd, INT source, POINTER_T adr) { return 0; };
int frontend_loop() { return 0; };
int poll_event(INT source, INT count, BOOL test) { return 0; };

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
