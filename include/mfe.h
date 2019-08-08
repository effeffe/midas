/********************************************************************\

  Name:         mfe.h
  Created by:   Konstantin Olchanski

  Contents:     Externals for mfe.c that must be defined in the user frontend

\********************************************************************/

#ifndef _MFE_H_
#define _MFE_H_

#include "midas.h"

// following items must be provided to mfe.c by the user frontend

extern const char *frontend_name;
extern const char *frontend_file_name;
extern BOOL frontend_call_loop;
extern INT max_event_size;
extern INT max_event_size_frag;
extern INT event_buffer_size;
extern INT display_period;
extern EQUIPMENT equipment[];
extern INT frontend_init(void);
extern INT frontend_exit(void);
extern INT frontend_loop(void);
extern INT begin_of_run(INT run_number, char *error);
extern INT end_of_run(INT run_number, char *error);
extern INT pause_run(INT run_number, char *error);
extern INT resume_run(INT run_number, char *error);
extern INT poll_event(INT source, INT count, BOOL test);
extern INT interrupt_configure(INT cmd, INT source, POINTER_T adr);

// following items are available inside mfe.c to be used by the user frontend
// look inside mfe.c to find out what they do.

extern INT rpc_mode;
extern INT run_state;                  /* STATE_RUNNING, STATE_STOPPED, STATE_PAUSED */
extern INT run_number;
extern DWORD actual_time;              /* current time in seconds since 1970 */
extern DWORD actual_millitime;         /* current time in milliseconds */
extern DWORD rate_period;              /* period in ms for rate calculations */

extern int gWriteCacheSize;

extern char host_name[HOST_NAME_LENGTH];
extern char exp_name[NAME_LENGTH];
extern char full_frontend_name[256];

extern INT max_bytes_per_sec;
extern INT optimize;                 /* set this to one to opimize TCP buffer size */
extern INT fe_stop;                  /* stop switch for VxWorks */
extern BOOL debug;                   /* disable watchdog messages from server */
extern DWORD auto_restart;           /* restart run after event limit reached stop */
extern INT manual_trigger_event_id;  /* set from the manual_trigger callback */
extern INT verbosity_level;          /* can be used by user code for debugging output */
extern BOOL lockout_readout_thread;  /* manual triggers, periodic events and 1Hz flush cache lockout the readout thread */

extern HNDLE hDB;
extern HNDLE hClient;

extern volatile int stop_all_threads;


extern EQUIPMENT *interrupt_eq;
extern EQUIPMENT *multithread_eq;
extern BOOL slowcont_eq;
extern void *event_buffer;
extern void *frag_buffer;
extern int *n_events;
extern void mfe_error_check(void);
extern void readout_enable(BOOL flag);
extern int readout_enabled(void);
extern void display(BOOL bInit);
extern void rotate_wheel(void);
extern BOOL logger_root(void);
extern void set_rate_period(int ms);
extern int get_rate_period(void);

extern INT manual_trigger(INT idx, void *prpc_param[]);

/*---- frontend functions from midas.h ----*/
INT get_frontend_index(void);
void mfe_get_args(int *argc, char ***argv);
void register_cnaf_callback(int debug);
void mfe_error(const char *error);
void mfe_set_error(void (*dispatcher) (const char *));
int set_equipment_status(const char *name, const char *eq_status, const char *status_class);
INT create_event_rb(int i);
INT get_event_rbh(int i);
void stop_readout_threads(void);
int is_readout_thread_enabled(void);
int is_readout_thread_active(void);
void signal_readout_thread_active(int index, int flag);
INT set_odb_equipment_common(EQUIPMENT eq[], const char *name);

#endif

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
