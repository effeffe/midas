/********************************************************************\

  Name:         mfe.h
  Created by:   Konstantin Olchanski

  Contents:     Externals for mfe.c that must be defined in the user frontend

\********************************************************************/

extern char *frontend_name;
extern char *frontend_file_name;
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

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
