/********************************************************************\

  Name:         mfed.c
  Created by:   Stefan Ritt

  Contents:     Dummy routines to simplify syntax for mfe based
                frontends.

                Users must #defien MFED before #include "mfe.h"
                and link against mfed.cxx

\********************************************************************/

#include <cstdio>
#include <cstring>
#include <history.h>
#include "midas.h"
#include "mfe.h"

/*-- Globals -------------------------------------------------------*/

/* frontend_loop is called periodically if this variable is TRUE    */
BOOL frontend_call_loop = TRUE;

/* a frontend status page is displayed with this frequency in ms    */
INT display_period = 0;

/* maximum event size produced by this frontend */
INT max_event_size = 1024 * 1024;

/* maximum event size for fragmented events (EQ_FRAGMENTED) */
INT max_event_size_frag = 5 * 1024 * 1024;

/* buffer size to hold events */
INT event_buffer_size = 2 * 1024 * 1024;

/*------------------------------------------------------------------*/

void set_max_event_size(int size)
{
   max_event_size = size;
}

void set_event_buffer_size(int size)
{
   event_buffer_size = size;
}

/*------------------------------------------------------------------*/

INT (*p_poll_event)(INT,INT,BOOL) = NULL;

void install_poll_event(INT (*f)(INT,INT,BOOL))
{
   p_poll_event = f;
}

INT poll_event(__attribute__((unused)) INT source, __attribute__((unused)) INT count, __attribute__((unused))BOOL test)
{
   if (p_poll_event)
      return p_poll_event(source, count, test);
   return 1;
}

INT interrupt_configure(__attribute__((unused)) INT cmd, __attribute__((unused)) INT source, __attribute__((unused)) PTYPE adr)
{
   return 1;
}

/*------------------------------------------------------------------*/

INT (*p_frontend_exit)() = NULL;

void install_frontend_exit(INT (*f)())
{
   p_frontend_exit = f;
}

INT frontend_exit()
{
   if (p_frontend_exit)
      return p_frontend_exit();
   return CM_SUCCESS;
}

/*------------------------------------------------------------------*/

INT (*p_begin_of_run)() = NULL;

void install_begin_of_run(INT (*f)())
{
   p_begin_of_run = f;
}

INT begin_of_run(__attribute__((unused)) INT rn, __attribute__((unused)) char *error)
{
   if (p_begin_of_run)
      return p_begin_of_run();
   return CM_SUCCESS;
}

/*------------------------------------------------------------------*/

INT (*p_end_of_run)() = NULL;

void install_end_of_run(INT (*f)())
{
   p_end_of_run = f;
}

INT end_of_run(__attribute__((unused)) INT rn, __attribute__((unused)) char *error)
{
   if (p_end_of_run)
      return p_end_of_run();
   return CM_SUCCESS;
}

/*------------------------------------------------------------------*/

INT (*p_pause_run)() = NULL;

void install_pause_run(INT (*f)())
{
   p_pause_run = f;
}

INT pause_run(__attribute__((unused)) INT rn, __attribute__((unused)) char *error)
{
   if (p_pause_run)
      return p_pause_run();
   return CM_SUCCESS;
}

/*------------------------------------------------------------------*/

INT (*p_resume_run)() = NULL;

void install_resume_run(INT (*f)())
{
   p_resume_run = f;
}

INT resume_run(__attribute__((unused)) INT rn, __attribute__((unused)) char *error)
{
   if (p_resume_run)
      return p_resume_run();
   return CM_SUCCESS;
}

/*------------------------------------------------------------------*/

INT (*p_frontend_loop)() = NULL;

void install_frontend_loop(INT (*f)())
{
   p_frontend_loop = f;
}

INT frontend_loop()
{
   if (p_frontend_loop)
      return p_frontend_loop();
   else
      ss_sleep(10); // don't eat all CPU
   return CM_SUCCESS;
}

