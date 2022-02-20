/********************************************************************\

  Name:         HISTORY.C
  Created by:   Stefan Ritt

  Contents:     MIDAS history functions

  $Id$

\********************************************************************/

#undef NDEBUG // midas required assert() to be always enabled

#include "midas.h"
#include "msystem.h"
#ifndef HAVE_STRLCPY
#include "strlcpy.h"
#endif
#include <assert.h>
#include <math.h> // sqrt()

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <map>

#include "midas.h"
#include "msystem.h"
#include "history.h"

#ifndef HAVE_STRLCPY
#include "strlcpy.h"
#endif

#define STRLCPY(dst, src) strlcpy(dst, src, sizeof(dst))

/** @defgroup hsfunctioncode Midas History Functions (hs_xxx)
 */

/**dox***************************************************************/
/** @addtogroup hsfunctioncode
 *
 *  @{  */

#if !defined(OS_VXWORKS)
/********************************************************************\
*                                                                    *
*                 History functions                                  *
*                                                                    *
\********************************************************************/

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

static std::vector<HISTORY*> _history;
static std::string _hs_path_name;

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

static bool xwrite(const std::string& fn, int fh, const void *buf, size_t count)
{
   ssize_t wr = write(fh, buf, count);

   if (wr < 0) {
      cm_msg(MERROR, "xwrite", "Error writing %d bytes to file \"%s\" errno %d (%s)", (int)count, fn.c_str(), errno, strerror(errno));
      return false;
   }

   if ((size_t)wr != count) {
      cm_msg(MERROR, "xwrite", "Error writing %d bytes to file \"%s\", short write %d bytes", (int)count, fn.c_str(), (int)wr);
      return false;
   }

   return true;
}

// xread() return value:
// 1 = ok
// 0 = expected EOF, if eof_ok == true
// -1 = error or short read or unexpected eof

static int xread(const std::string& fn, int fh, void *buf, size_t count, bool eof_ok = false)
{
   ssize_t rd = read(fh, buf, count);

   if (rd < 0) {
      cm_msg(MERROR, "xread", "Error reading from file \"%s\" errno %d (%s)", fn.c_str(), errno, strerror(errno));
      return -1;
   }

   if (rd == 0) {
      if (eof_ok)
         return 0;
      cm_msg(MERROR, "xread", "Error: Unexpected end-of-file when reading file \"%s\"", fn.c_str());
      return -1;
   }

   if ((size_t)rd != count) {
      cm_msg(MERROR, "xread", "Error: Truncated read from file \"%s\", requested %d bytes, read %d bytes", fn.c_str(), (int)count, (int)rd);
      return -1;
   }

   return 1;
}

static bool xseek(const std::string& fn, int fh, DWORD pos)
{
   off_t off = lseek(fh, pos, SEEK_SET);

   if (off < 0) {
      cm_msg(MERROR, "xseek", "Error in lseek(%llu, SEEK_SET) for file \"%s\", errno %d (%s)", (unsigned long long)pos, fn.c_str(), errno, strerror(errno));
      return false;
   }

   return true;
}

static bool xseek_end(const std::string& fn, int fh)
{
   off_t off = lseek(fh, 0, SEEK_END);

   if (off < 0) {
      cm_msg(MERROR, "xseek_end", "Error in lseek(SEEK_END) to end-of-file for file \"%s\", errno %d (%s)", fn.c_str(), errno, strerror(errno));
      return false;
   }

   return true;
}

static bool xseek_cur(const std::string& fn, int fh, int offset)
{
   off_t off = lseek(fh, offset, SEEK_CUR);

   if (off < 0) {
      cm_msg(MERROR, "xseek_cur", "Error in lseek(%d, SEEK_CUR) for file \"%s\", errno %d (%s)", offset, fn.c_str(), errno, strerror(errno));
      return false;
   }

   return true;
}

static DWORD xcurpos(const std::string& fn, int fh)
{
   off_t off = lseek(fh, 0, SEEK_CUR);

   if (off < 0) {
      cm_msg(MERROR, "xcurpos", "Error in lseek(0, SEEK_CUR) for file \"%s\", errno %d (%s)", fn.c_str(), errno, strerror(errno));
      return -1;
   }

   DWORD dw = off;

   if (dw != off) {
      cm_msg(MERROR, "xcurpos", "Error: lseek(0, SEEK_CUR) for file \"%s\" returned value %llu does not fir into a DWORD, maybe file is bigger than 2GiB or 4GiB", fn.c_str(), (unsigned long long)off);
      return -1;
   }

   return dw;
}

static bool xtruncate(const std::string& fn, int fh, DWORD pos)
{
   off_t off = lseek(fh, pos, SEEK_SET);

   if (off < 0) {
      cm_msg(MERROR, "xtruncate", "Error in lseek(%llu) for file \"%s\", errno %d (%s)", (unsigned long long)pos, fn.c_str(), errno, strerror(errno));
      return false;
   }

   int status = ftruncate(fh, pos);

   if (status != 0) {
      cm_msg(MERROR, "xtruncate", "Error setting file size of \"%s\" to %llu, errno %d (%s)", fn.c_str(), (unsigned long long)pos, errno, strerror(errno));
      return false;
   }

   return true;
}

/********************************************************************/
/**
Sets the path for future history file accesses. Should
be called before any other history function is called.
@param path             Directory where history files reside
@return HS_SUCCESS
*/
static INT hs_set_path(const char *path)
{
   assert(path);
   assert(path[0] != 0);
   
   _hs_path_name = path;

   /* check for trailing directory seperator */
   if (_hs_path_name.back() != DIR_SEPARATOR)
      _hs_path_name += DIR_SEPARATOR_STR;

   return HS_SUCCESS;
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

/********************************************************************/

/**
Open history file belonging to certain date. Internal use
           only.
@param ltime          Date for which a history file should be opened.
@param suffix         File name suffix like "hst", "idx", "idf"
@param mode           R/W access mode
@param fh             File handle
@return HS_SUCCESS
*/
static INT hs_open_file(time_t ltime, const char *suffix, INT mode, std::string *pfile_name, int *fh)
{
   struct tm *tms;
   time_t ttime;

   /* generate new file name YYMMDD.xxx */
#if !defined(OS_VXWORKS)
#if !defined(OS_VMS)
   tzset();
#endif
#endif
   ttime = (time_t) ltime;
   tms = localtime(&ttime);

   //sprintf(file_name, "%s%02d%02d%02d.%s", _hs_path_name, tms->tm_year % 100, tms->tm_mon + 1, tms->tm_mday, suffix);
   std::string file_name;
   file_name += _hs_path_name;
   char tmp[100];
   sprintf(tmp, "%02d%02d%02d", tms->tm_year % 100, tms->tm_mon + 1, tms->tm_mday);
   file_name += tmp;
   file_name += ".";
   file_name += suffix;

   if (pfile_name)
      *pfile_name = file_name;

   /* open file, add O_BINARY flag for Windows NT */
   *fh = open(file_name.c_str(), mode | O_BINARY, 0644);

   //printf("hs_open_file: time %d, file \'%s\', fh %d\n", (int)ltime, file_name.c_str(), *fh);

   return HS_SUCCESS;
}

/********************************************************************/
static INT hs_gen_index(DWORD ltime)
/********************************************************************\

  Routine: hs_gen_index

  Purpose: Regenerate index files ("idx" and "idf" files) for a given
           history file ("hst"). Interal use only.

  Input:
    time_t ltime            Date for which a history file should
                            be analyzed.

  Output:
    none

  Function value:
    HS_SUCCESS              Successful completion
    HS_FILE_ERROR           Index files cannot be created

\********************************************************************/
{
   char event_name[NAME_LENGTH];
   int fh, fhd, fhi;
   HIST_RECORD rec;
   INDEX_RECORD irec;
   DEF_RECORD def_rec;
   int recovering = 0;
   //time_t now = time(NULL);

   cm_msg(MINFO, "hs_gen_index", "generating index files for time %d", (int) ltime);
   printf("Recovering index files...\n");

   if (ltime == 0)
      ltime = (DWORD) time(NULL);

   std::string fni;
   std::string fnd;
   std::string fn;

   /* open new index file */
   hs_open_file(ltime, "idx", O_RDWR | O_CREAT | O_TRUNC, &fni, &fhi);
   hs_open_file(ltime, "idf", O_RDWR | O_CREAT | O_TRUNC, &fnd, &fhd);

   if (fhd < 0 || fhi < 0) {
      cm_msg(MERROR, "hs_gen_index", "cannot create index file");
      return HS_FILE_ERROR;
   }

   /* open history file */
   hs_open_file(ltime, "hst", O_RDONLY, &fn, &fh);
   if (fh < 0)
      return HS_FILE_ERROR;
   xseek(fn, fh, 0);

   /* loop over file records in .hst file */
   do {
      if (xread(fn, fh, (char *) &rec, sizeof(rec), true) <= 0) {
         break;
      }

      /* check if record type is definition */
      if (rec.record_type == RT_DEF) {
         /* read name */
         if (xread(fn, fh, event_name, sizeof(event_name)) < 0)
            return HS_FILE_ERROR;

         printf("Event definition %s, ID %d\n", event_name, rec.event_id);

         /* write definition index record */
         def_rec.event_id = rec.event_id;
         memcpy(def_rec.event_name, event_name, sizeof(event_name));
         DWORD pos = xcurpos(fn, fh);
         if (pos == (DWORD)-1) return HS_FILE_ERROR;
         def_rec.def_offset = pos - sizeof(event_name) - sizeof(rec);
         xwrite(fnd, fhd, (char *) &def_rec, sizeof(def_rec));

         //printf("data def at %d (age %d)\n", rec.time, now-rec.time);

         /* skip tags */
         xseek_cur(fn, fh, rec.data_size);
      } else if (rec.record_type == RT_DATA && rec.data_size > 1 && rec.data_size < 1 * 1024 * 1024) {
         /* write index record */
         irec.event_id = rec.event_id;
         irec.time = rec.time;
         DWORD pos = xcurpos(fn, fh);
         if (pos == (DWORD)-1) return HS_FILE_ERROR;
         irec.offset = pos - sizeof(rec);
         xwrite(fni, fhi, (char *) &irec, sizeof(irec));

         //printf("data rec at %d (age %d)\n", rec.time, now-rec.time);

         /* skip data */
         xseek_cur(fn, fh, rec.data_size);
      } else {
         if (!recovering)
            cm_msg(MERROR, "hs_gen_index", "broken history file for time %d, trying to recover", (int) ltime);

         recovering = 1;
         xseek_cur(fn, fh, 1 - (int)sizeof(rec));

         continue;
      }

   } while (TRUE);

   close(fh);
   close(fhi);
   close(fhd);

   printf("...done.\n");

   return HS_SUCCESS;
}


/********************************************************************/
static INT hs_search_file(DWORD * ltime, INT direction)
/********************************************************************\

  Routine: hs_search_file

  Purpose: Search an history file for a given date. If not found,
           look for files after date (direction==1) or before date
           (direction==-1) up to one year.

  Input:
    DWORD  *ltime           Date of history file
    INT    direction        Search direction

  Output:
    DWORD  *ltime           Date of history file found

  Function value:
    HS_SUCCESS              Successful completion
    HS_FILE_ERROR           No file found

\********************************************************************/
{
   time_t lt;
   int fh, fhd, fhi;
   std::string fn;
   struct tm *tms;

   if (*ltime == 0)
      *ltime = ss_time();

   lt = (time_t) * ltime;
   do {
      /* try to open history file for date "lt" */
      hs_open_file(lt, "hst", O_RDONLY, &fn, &fh);

      /* if not found, look for next day */
      if (fh < 0)
         lt += direction * 3600 * 24;

      /* stop if more than a year before starting point or in the future */
   } while (fh < 0 && (INT) * ltime - (INT) lt < 3600 * 24 * 365 && lt <= (time_t) ss_time());

   if (fh < 0)
      return HS_FILE_ERROR;

   if (lt != (time_t) *ltime) {
      /* if switched to new day, set start_time to 0:00 */
      tms = localtime(&lt);
      tms->tm_hour = tms->tm_min = tms->tm_sec = 0;
      *ltime = (DWORD) mktime(tms);
   }

   /* check if index files are there */
   hs_open_file(*ltime, "idf", O_RDONLY, NULL, &fhd);
   hs_open_file(*ltime, "idx", O_RDONLY, NULL, &fhi);

   if (fh > 0)
      close(fh);
   if (fhd > 0)
      close(fhd);
   if (fhi > 0)
      close(fhi);

   /* generate them if not */
   if (fhd < 0 || fhi < 0)
      hs_gen_index(*ltime);

   return HS_SUCCESS;
}


/********************************************************************/
static INT hs_define_event(DWORD event_id, const char *name, const TAG * tag, DWORD size)
/********************************************************************\

  Routine: hs_define_event

  Purpose: Define a new event for which a history should be recorded.
           This routine must be called before any call to
           hs_write_event. It also should be called if the definition
           of the event has changed.

           The event definition is written directly to the history
           file. If the definition is identical to a previous
           definition, it is not written to the file.


  Input:
    DWORD  event_id         ID for this event. Must be unique.
    char   name             Name of this event
    TAG    tag              Tag list containing names and types of
                            variables in this event.
    DWORD  size             Size of tag array

  Output:
    <none>

  Function value:
    HS_SUCCESS              Successful completion
    HS_NO_MEMEORY           Out of memory
    HS_FILE_ERROR           Cannot open history file

\********************************************************************/
{
   {
      HIST_RECORD rec, prev_rec;
      DEF_RECORD def_rec;
      time_t ltime;
      char str[256], event_name[NAME_LENGTH], *buffer;
      int fh, fhi, fhd;
      std::string fn, fni, fnd;
      INT n, status, semaphore;
      struct tm *tmb;

      //printf("hs_define_event: event_id %d, name [%s]\n", event_id, name);

      /* request semaphore */
      cm_get_experiment_semaphore(NULL, NULL, &semaphore, NULL);
      status = ss_semaphore_wait_for(semaphore, 5 * 1000);
      if (status != SS_SUCCESS)
         return SUCCESS;        /* someone else blocked the history system */

      /* allocate new space for the new history descriptor */
      /* check if history already open */
      int index = -1;
      for (unsigned i = 0; i < _history.size(); i++)
         if (_history[i]->event_id == event_id) {
            index = i;
            break;
         }

      /* if not found, create new one */
      if (index < 0) {
         index = _history.size();
         _history.push_back(new HISTORY);
      }

      /* assemble definition record header */
      rec.record_type = RT_DEF;
      rec.event_id = event_id;
      rec.time = (DWORD) time(NULL);
      rec.data_size = size;
      strlcpy(event_name, name, NAME_LENGTH);

      /* if history structure not set up, do so now */
      if (!_history[index]->hist_fh) {
         /* open history file */
         hs_open_file(rec.time, "hst", O_CREAT | O_RDWR, &fn, &fh);
         if (fh < 0) {
            ss_semaphore_release(semaphore);
            return HS_FILE_ERROR;
         }

         /* open index files */
         hs_open_file(rec.time, "idf", O_CREAT | O_RDWR, &fnd, &fhd);
         hs_open_file(rec.time, "idx", O_CREAT | O_RDWR, &fni, &fhi);
         xseek_end(fn, fh);
         xseek_end(fni, fhi);
         xseek_end(fnd, fhd);

         DWORD fh_pos = xcurpos(fn, fh);
         DWORD fhd_pos = xcurpos(fnd, fhd);

         if (fh_pos == (DWORD)-1) return HS_FILE_ERROR;
         if (fhd_pos == (DWORD)-1) return HS_FILE_ERROR;

         /* regenerate index if missing */
         if (fh_pos > 0 && fhd_pos == 0) {
            close(fh);
            close(fhi);
            close(fhd);
            hs_gen_index(rec.time);
            hs_open_file(rec.time, "hst", O_RDWR, &fn, &fh);
            hs_open_file(rec.time, "idx", O_RDWR, &fni, &fhi);
            hs_open_file(rec.time, "idf", O_RDWR, &fnd, &fhd);
            xseek_end(fn, fh);
            xseek_end(fni, fhi);
            xseek_end(fnd, fhd);
         }

         ltime = (time_t) rec.time;
         tmb = localtime(&ltime);
         tmb->tm_hour = tmb->tm_min = tmb->tm_sec = 0;

         DWORD pos = xcurpos(fn, fh);
         if (pos == (DWORD)-1) return HS_FILE_ERROR;

         /* setup history structure */
         _history[index]->hist_fn = fn;
         _history[index]->index_fn = fni;
         _history[index]->def_fn = fnd;
         _history[index]->hist_fh = fh;
         _history[index]->index_fh = fhi;
         _history[index]->def_fh = fhd;
         _history[index]->def_offset = pos;
         _history[index]->event_id = event_id;
         _history[index]->event_name = event_name;
         _history[index]->base_time = (DWORD) mktime(tmb);
         _history[index]->n_tag = size / sizeof(TAG);
         _history[index]->tag = (TAG *) M_MALLOC(size);
         memcpy(_history[index]->tag, tag, size);

         /* search previous definition */
         fhd_pos = xcurpos(fnd, fhd);
         if (fhd_pos == (DWORD)-1) return HS_FILE_ERROR;
         n = fhd_pos / sizeof(def_rec);
         def_rec.event_id = 0;
         for (int i = n - 1; i >= 0; i--) {
            if (!xseek(fnd, fhd, i * sizeof(def_rec))) return HS_FILE_ERROR;
            if (xread(fnd, fhd, (char *) &def_rec, sizeof(def_rec)) < 0) return HS_FILE_ERROR;
            if (def_rec.event_id == event_id)
               break;
         }
         xseek_end(fnd, fhd);

         /* if definition found, compare it with new one */
         if (def_rec.event_id == event_id) {
            buffer = (char *) M_MALLOC(size);
            memset(buffer, 0, size);

            xseek(fn, fh, def_rec.def_offset);
            xread(fn, fh, (char *) &prev_rec, sizeof(prev_rec));
            xread(fn, fh, str, NAME_LENGTH);
            xread(fn, fh, buffer, size);
            xseek_end(fn, fh);

            if (prev_rec.data_size != size || strcmp(str, event_name) != 0 || memcmp(buffer, tag, size) != 0) {
               /* write definition to history file */
               xwrite(fn, fh, (char *) &rec, sizeof(rec));
               xwrite(fn, fh, event_name, NAME_LENGTH);
               xwrite(fn, fh, (char *) tag, size);

               /* write index record */
               def_rec.event_id = event_id;
               memcpy(def_rec.event_name, event_name, sizeof(event_name));
               def_rec.def_offset = _history[index]->def_offset;
               xwrite(fnd, fhd, (char *) &def_rec, sizeof(def_rec));
            } else
               /* definition identical, just remember old offset */
               _history[index]->def_offset = def_rec.def_offset;

            M_FREE(buffer);
         } else {
            /* write definition to history file */
            xwrite(fn, fh, (char *) &rec, sizeof(rec));
            xwrite(fn, fh, event_name, NAME_LENGTH);
            xwrite(fn, fh, (char *) tag, size);

            /* write definition index record */
            def_rec.event_id = event_id;
            memcpy(def_rec.event_name, event_name, sizeof(event_name));
            def_rec.def_offset = _history[index]->def_offset;
            xwrite(fn, fhd, (char *) &def_rec, sizeof(def_rec));
         }
      } else {
         fn = _history[index]->hist_fn;
         fnd = _history[index]->def_fn;
         fh = _history[index]->hist_fh;
         fhd = _history[index]->def_fh;

         /* compare definition with previous definition */
         buffer = (char *) M_MALLOC(size);
         memset(buffer, 0, size);

         xseek(fn, fh, _history[index]->def_offset);
         xread(fn, fh, (char *) &prev_rec, sizeof(prev_rec));
         xread(fn, fh, str, NAME_LENGTH);
         xread(fn, fh, buffer, size);

         xseek_end(fn, fh);
         xseek_end(fnd, fhd);

         if (prev_rec.data_size != size || strcmp(str, event_name) != 0 || memcmp(buffer, tag, size) != 0) {
            /* save new definition offset */
            DWORD pos = xcurpos(fn, fh);
            if (pos == (DWORD)-1) return HS_FILE_ERROR;
            _history[index]->def_offset = pos;

            /* write definition to history file */
            xwrite(fn, fh, (char *) &rec, sizeof(rec));
            xwrite(fn, fh, event_name, NAME_LENGTH);
            xwrite(fn, fh, (char *) tag, size);

            /* write index record */
            def_rec.event_id = event_id;
            memcpy(def_rec.event_name, event_name, sizeof(event_name));
            def_rec.def_offset = _history[index]->def_offset;
            xwrite(fnd, fhd, (char *) &def_rec, sizeof(def_rec));
         }

         M_FREE(buffer);
      }

      ss_semaphore_release(semaphore);
   }

   return HS_SUCCESS;
}


/********************************************************************/
static INT hs_write_event(DWORD event_id, const void *data, DWORD size)
/********************************************************************\

  Routine: hs_write_event

  Purpose: Write an event to a history file.

  Input:
    DWORD  event_id         Event ID
    void   *data            Data buffer containing event
    DWORD  size             Data buffer size in bytes

  Output:
    none
                            future hs_write_event

  Function value:
    HS_SUCCESS              Successful completion
    HS_NO_MEMEORY           Out of memory
    HS_FILE_ERROR           Cannot write to history file
    HS_UNDEFINED_EVENT      Event was not defined via hs_define_event

\********************************************************************/
{
   HIST_RECORD rec, drec;
   DEF_RECORD def_rec;
   INDEX_RECORD irec;
   int fh, fhi, fhd;
   DWORD last_pos_data, last_pos_index;
   std::string fn, fni, fnd;
   INT semaphore;
   int status;
   struct tm tmb, tmr;
   time_t ltime;

   /* request semaphore */
   cm_get_experiment_semaphore(NULL, NULL, &semaphore, NULL);
   status = ss_semaphore_wait_for(semaphore, 5 * 1000);
   if (status != SS_SUCCESS) {
      cm_msg(MERROR, "hs_write_event", "semaphore timeout");
      return SUCCESS;           /* someone else blocked the history system */
   }

   /* find index to history structure */
   int index = -1;
   for (unsigned i = 0; i < _history.size(); i++)
      if (_history[i]->event_id == event_id) {
         index = i;
         break;
      }
   if (index < 0) {
      ss_semaphore_release(semaphore);
      return HS_UNDEFINED_EVENT;
   }

   /* assemble record header */
   rec.record_type = RT_DATA;
   rec.event_id = _history[index]->event_id;
   rec.time = (DWORD) time(NULL);
   rec.def_offset = _history[index]->def_offset;
   rec.data_size = size;

   irec.event_id = _history[index]->event_id;
   irec.time = rec.time;

   /* check if new day */
   ltime = (time_t) rec.time;
   memcpy(&tmr, localtime(&ltime), sizeof(tmr));
   ltime = (time_t) _history[index]->base_time;
   memcpy(&tmb, localtime(&ltime), sizeof(tmb));

   if (tmr.tm_yday != tmb.tm_yday) {
      /* close current history file */
      close(_history[index]->hist_fh);
      close(_history[index]->def_fh);
      close(_history[index]->index_fh);

      /* open new history file */
      hs_open_file(rec.time, "hst", O_CREAT | O_RDWR, &fn, &fh);
      if (fh < 0) {
         ss_semaphore_release(semaphore);
         return HS_FILE_ERROR;
      }

      /* open new index file */
      hs_open_file(rec.time, "idx", O_CREAT | O_RDWR, &fni, &fhi);
      if (fhi < 0) {
         ss_semaphore_release(semaphore);
         return HS_FILE_ERROR;
      }

      /* open new definition index file */
      hs_open_file(rec.time, "idf", O_CREAT | O_RDWR, &fnd, &fhd);
      if (fhd < 0) {
         ss_semaphore_release(semaphore);
         return HS_FILE_ERROR;
      }

      xseek_end(fn, fh);
      xseek_end(fni, fhi);
      xseek_end(fnd, fhd);

      /* remember new file handles */
      _history[index]->hist_fn = fn;
      _history[index]->index_fn = fni;
      _history[index]->def_fn = fnd;

      _history[index]->hist_fh = fh;
      _history[index]->index_fh = fhi;
      _history[index]->def_fh = fhd;

      _history[index]->def_offset = xcurpos(fn, fh);
      rec.def_offset = _history[index]->def_offset;

      tmr.tm_hour = tmr.tm_min = tmr.tm_sec = 0;
      _history[index]->base_time = (DWORD) mktime(&tmr);

      /* write definition from _history structure */
      drec.record_type = RT_DEF;
      drec.event_id = _history[index]->event_id;
      drec.time = rec.time;
      drec.data_size = _history[index]->n_tag * sizeof(TAG);

      xwrite(fn, fh, (char *) &drec, sizeof(drec));
      xwrite(fn, fh, _history[index]->event_name.c_str(), NAME_LENGTH);
      xwrite(fn, fh, (char *) _history[index]->tag, drec.data_size);

      /* write definition index record */
      def_rec.event_id = _history[index]->event_id;
      memcpy(def_rec.event_name, _history[index]->event_name.c_str(), sizeof(def_rec.event_name));
      def_rec.def_offset = _history[index]->def_offset;
      xwrite(fnd, fhd, (char *) &def_rec, sizeof(def_rec));
   }

   /* go to end of file */
   xseek_end(_history[index]->hist_fn, _history[index]->hist_fh);
   last_pos_data = irec.offset = xcurpos(_history[index]->hist_fn, _history[index]->hist_fh);

   /* write record header */
   xwrite(_history[index]->hist_fn, _history[index]->hist_fh, (char *) &rec, sizeof(rec));

   /* write data */
   if (!xwrite(_history[index]->hist_fn, _history[index]->hist_fh, (char *) data, size)) {
      /* disk maybe full? Do a roll-back! */
      xtruncate(_history[index]->hist_fn, _history[index]->hist_fh, last_pos_data);
      ss_semaphore_release(semaphore);
      return HS_FILE_ERROR;
   }

   /* write index record */
   xseek_end(_history[index]->index_fn, _history[index]->index_fh);
   last_pos_index = xcurpos(_history[index]->index_fn, _history[index]->index_fh);
   int size_of_irec = sizeof(irec);
   if (!xwrite(_history[index]->index_fn, _history[index]->index_fh, (char *) &irec, size_of_irec)) {
      /* disk maybe full? Do a roll-back! */
      xtruncate(_history[index]->hist_fn, _history[index]->hist_fh, last_pos_data);
      xtruncate(_history[index]->index_fn, _history[index]->index_fh, last_pos_index);
      ss_semaphore_release(semaphore);
      return HS_FILE_ERROR;
   }

   ss_semaphore_release(semaphore);
   return HS_SUCCESS;
}


/********************************************************************/
static INT hs_enum_events(DWORD ltime, char *event_name, DWORD * name_size, INT event_id[], DWORD * id_size)
/********************************************************************\

  Routine: hs_enum_events

  Purpose: Enumerate events for a given date

  Input:
    DWORD  ltime            Date at which events should be enumerated

  Output:
    char   *event_name      Array containing event names
    DWORD  *name_size       Size of name array
    char   *event_id        Array containing event IDs
    DWORD  *id_size         Size of ID array

  Function value:
    HS_SUCCESS              Successful completion
    HS_NO_MEMEORY           Out of memory
    HS_FILE_ERROR           Cannot open history file

\********************************************************************/
{
   int fh, fhd;
   std::string fn, fnd;
   INT status, i, n;
   DEF_RECORD def_rec;

   /* search latest history file */
   status = hs_search_file(&ltime, -1);
   if (status != HS_SUCCESS) {
      cm_msg(MERROR, "hs_enum_events", "cannot find recent history file");
      return HS_FILE_ERROR;
   }

   /* open history and definition files */
   hs_open_file(ltime, "hst", O_RDONLY, &fn, &fh);
   hs_open_file(ltime, "idf", O_RDONLY, &fnd, &fhd);
   if (fh < 0 || fhd < 0) {
      cm_msg(MERROR, "hs_enum_events", "cannot open index files");
      return HS_FILE_ERROR;
   }
   xseek(fnd, fhd, 0);

   /* loop over definition index file */
   n = 0;
   do {
      /* read event definition */
      if (xread(fnd, fhd, (char *) &def_rec, sizeof(def_rec), true) <= 0)
         break;

      /* look for existing entry for this event id */
      for (i = 0; i < n; i++)
         if (event_id[i] == (INT) def_rec.event_id) {
            strcpy(event_name + i * NAME_LENGTH, def_rec.event_name);
            break;
         }

      /* new entry found */
      if (i == n) {
         if (((i * NAME_LENGTH) > ((INT) * name_size)) || ((i * sizeof(INT)) > (*id_size))) {
            cm_msg(MERROR, "hs_enum_events", "index buffer too small");
            close(fh);
            close(fhd);
            return HS_NO_MEMORY;
         }

         /* copy definition record */
         strcpy(event_name + i * NAME_LENGTH, def_rec.event_name);
         event_id[i] = def_rec.event_id;
         n++;
      }
   } while (TRUE);

   close(fh);
   close(fhd);
   *name_size = n * NAME_LENGTH;
   *id_size = n * sizeof(INT);

   return HS_SUCCESS;
}


/********************************************************************/
static INT hs_count_events(DWORD ltime, DWORD * count)
/********************************************************************\

  Routine: hs_count_events

  Purpose: Count number of different events for a given date

  Input:
    DWORD  ltime            Date at which events should be counted

  Output:
    DWORD  *count           Number of different events found

  Function value:
    HS_SUCCESS              Successful completion
    HS_FILE_ERROR           Cannot open history file

\********************************************************************/
{
   int fh, fhd;
   std::string fn, fnd;
   INT status, i, n;
   DWORD *id;
   DEF_RECORD def_rec;

   /* search latest history file */
   status = hs_search_file(&ltime, -1);
   if (status != HS_SUCCESS) {
      cm_msg(MERROR, "hs_count_events", "cannot find recent history file");
      return HS_FILE_ERROR;
   }

   /* open history and definition files */
   hs_open_file(ltime, "hst", O_RDONLY, &fn, &fh);
   hs_open_file(ltime, "idf", O_RDONLY, &fnd, &fhd);
   if (fh < 0 || fhd < 0) {
      cm_msg(MERROR, "hs_count_events", "cannot open index files");
      return HS_FILE_ERROR;
   }

   /* allocate event id array */
   xseek_end(fnd, fhd);
   DWORD pos_fhd = xcurpos(fnd, fhd);
   if (pos_fhd == (DWORD)-1) return HS_FILE_ERROR;
   id = (DWORD *) M_MALLOC(pos_fhd/sizeof(def_rec) * sizeof(DWORD));
   xseek(fnd, fhd, 0);

   /* loop over index file */
   n = 0;
   do {
      /* read definition index record */
      if (xread(fnd, fhd, (char *) &def_rec, sizeof(def_rec), true) <= 0)
         break;

      /* look for existing entries */
      for (i = 0; i < n; i++)
         if (id[i] == def_rec.event_id)
            break;

      /* new entry found */
      if (i == n) {
         id[i] = def_rec.event_id;
         n++;
      }
   } while (TRUE);


   M_FREE(id);
   close(fh);
   close(fhd);
   *count = n;

   return HS_SUCCESS;
}


/********************************************************************/
static INT hs_get_event_id(DWORD ltime, const char *name, DWORD * id)
/********************************************************************\

  Routine: hs_get_event_id

  Purpose: Return event ID for a given name. If event cannot be found
           in current definition file, go back in time until found

  Input:
    DWORD  ltime            Date at which event ID should be looked for

  Output:
    DWORD  *id              Event ID

  Function value:
    HS_SUCCESS              Successful completion
    HS_FILE_ERROR           Cannot open history file
    HS_UNDEFINED_EVENT      Event "name" not found

\********************************************************************/
{
   int fh, fhd;
   std::string fn, fnd;
   INT status;
   DWORD lt;
   DEF_RECORD def_rec;

   /* search latest history file */
   if (ltime == 0)
      ltime = (DWORD) time(NULL);

   lt = ltime;

   do {
      status = hs_search_file(&lt, -1);
      if (status != HS_SUCCESS) {
         cm_msg(MERROR, "hs_count_events", "cannot find recent history file");
         return HS_FILE_ERROR;
      }

      /* open history and definition files */
      hs_open_file(lt, "hst", O_RDONLY, &fn, &fh);
      hs_open_file(lt, "idf", O_RDONLY, &fnd, &fhd);
      if (fh < 0 || fhd < 0) {
         cm_msg(MERROR, "hs_count_events", "cannot open index files");
         return HS_FILE_ERROR;
      }

      /* loop over index file */
      *id = 0;
      do {
         /* read definition index record */
         if (xread(fnd, fhd, (char *) &def_rec, sizeof(def_rec), true) <= 0)
            break;

         if (strcmp(name, def_rec.event_name) == 0) {
            *id = def_rec.event_id;
            close(fh);
            close(fhd);
            return HS_SUCCESS;
         }
      } while (TRUE);

      close(fh);
      close(fhd);

      /* not found -> go back one day */
      lt -= 3600 * 24;

   } while (lt > ltime - 3600 * 24 * 365 * 10); /* maximum 10 years */

   return HS_UNDEFINED_EVENT;
}


/********************************************************************/
static INT hs_count_vars(DWORD ltime, DWORD event_id, DWORD * count)
/********************************************************************\

  Routine: hs_count_vars

  Purpose: Count number of variables for a given date and event id

  Input:
    DWORD  ltime            Date at which tags should be counted

  Output:
    DWORD  *count           Number of tags

  Function value:
    HS_SUCCESS              Successful completion
    HS_FILE_ERROR           Cannot open history file

\********************************************************************/
{
   int fh, fhd;
   std::string fn, fnd;
   INT i, n, status;
   DEF_RECORD def_rec;
   HIST_RECORD rec;

   /* search latest history file */
   status = hs_search_file(&ltime, -1);
   if (status != HS_SUCCESS) {
      cm_msg(MERROR, "hs_count_tags", "cannot find recent history file");
      return HS_FILE_ERROR;
   }

   /* open history and definition files */
   hs_open_file(ltime, "hst", O_RDONLY, &fn, &fh);
   hs_open_file(ltime, "idf", O_RDONLY, &fnd, &fhd);
   if (fh < 0 || fhd < 0) {
      cm_msg(MERROR, "hs_count_tags", "cannot open index files");
      return HS_FILE_ERROR;
   }

   /* search last definition */
   xseek_end(fnd, fhd);
   n = xcurpos(fnd, fhd) / sizeof(def_rec);
   def_rec.event_id = 0;
   for (i = n - 1; i >= 0; i--) {
      if (!xseek(fnd, fhd, i * sizeof(def_rec))) return HS_FILE_ERROR;
      if (xread(fnd, fhd, (char *) &def_rec, sizeof(def_rec)) < 0) return HS_FILE_ERROR;
      if (def_rec.event_id == event_id)
         break;
   }
   if (def_rec.event_id != event_id) {
      cm_msg(MERROR, "hs_count_tags", "event %d not found in index file", event_id);
      return HS_FILE_ERROR;
   }

   /* read definition */
   xseek(fn, fh, def_rec.def_offset);
   xread(fn, fh, (char *) &rec, sizeof(rec));
   *count = rec.data_size / sizeof(TAG);

   close(fh);
   close(fhd);

   return HS_SUCCESS;
}


/********************************************************************/
static INT hs_enum_vars(DWORD ltime, DWORD event_id, char *var_name, DWORD * size, DWORD * var_n, DWORD * n_size)
/********************************************************************\

  Routine: hs_enum_vars

  Purpose: Enumerate variable tags for a given date and event id

  Input:
    DWORD  ltime            Date at which tags should be enumerated
    DWORD  event_id         Event ID

  Output:
    char   *var_name        Array containing variable names
    DWORD  *size            Size of name array
    DWORD  *var_n           Array size of variable
    DWORD  *n_size          Size of n array

  Function value:
    HS_SUCCESS              Successful completion
    HS_NO_MEMEORY           Out of memory
    HS_FILE_ERROR           Cannot open history file

\********************************************************************/
{
   char str[256];
   int fh, fhd;
   std::string fn, fnd;
   INT i, n, status;
   DEF_RECORD def_rec;
   HIST_RECORD rec;
   TAG *tag;

   /* search latest history file */
   status = hs_search_file(&ltime, -1);
   if (status != HS_SUCCESS) {
      cm_msg(MERROR, "hs_enum_vars", "cannot find recent history file");
      return HS_FILE_ERROR;
   }

   /* open history and definition files */
   hs_open_file(ltime, "hst", O_RDONLY, &fn, &fh);
   hs_open_file(ltime, "idf", O_RDONLY, &fnd, &fhd);
   if (fh < 0 || fhd < 0) {
      cm_msg(MERROR, "hs_enum_vars", "cannot open index files");
      return HS_FILE_ERROR;
   }

   /* search last definition */
   xseek_end(fnd, fhd);
   n = xcurpos(fnd, fhd) / sizeof(def_rec);
   def_rec.event_id = 0;
   for (i = n - 1; i >= 0; i--) {
      if (!xseek(fnd, fhd, i * sizeof(def_rec))) return HS_FILE_ERROR;
      if (xread(fnd, fhd, (char *) &def_rec, sizeof(def_rec)) < 0) return HS_FILE_ERROR;
      if (def_rec.event_id == event_id)
         break;
   }
   if (def_rec.event_id != event_id) {
      cm_msg(MERROR, "hs_enum_vars", "event %d not found in index file", event_id);
      return HS_FILE_ERROR;
   }

   /* read definition header */
   xseek(fn, fh, def_rec.def_offset);
   xread(fn, fh, (char *) &rec, sizeof(rec));
   xread(fn, fh, str, NAME_LENGTH);

   /* read event definition */
   n = rec.data_size / sizeof(TAG);
   tag = (TAG *) M_MALLOC(rec.data_size);
   xread(fn, fh, (char *) tag, rec.data_size);

   if (n * NAME_LENGTH > (INT) * size || n * sizeof(DWORD) > *n_size) {

      /* store partial definition */
      for (i = 0; i < (INT) * size / NAME_LENGTH; i++) {
         strcpy(var_name + i * NAME_LENGTH, tag[i].name);
         var_n[i] = tag[i].n_data;
      }

      cm_msg(MERROR, "hs_enum_vars", "tag buffer too small");
      M_FREE(tag);
      close(fh);
      close(fhd);
      return HS_NO_MEMORY;
   }

   /* store full definition */
   for (i = 0; i < n; i++) {
      strcpy(var_name + i * NAME_LENGTH, tag[i].name);
      var_n[i] = tag[i].n_data;
   }
   *size = n * NAME_LENGTH;
   *n_size = n * sizeof(DWORD);

   M_FREE(tag);
   close(fh);
   close(fhd);

   return HS_SUCCESS;
}


/********************************************************************/
static INT hs_get_var(DWORD ltime, DWORD event_id, const char *var_name, DWORD * type, INT * n_data)
/********************************************************************\

  Routine: hs_get_var

  Purpose: Get definition for certain variable

  Input:
    DWORD  ltime            Date at which variable definition should
                            be returned
    DWORD  event_id         Event ID
    char   *var_name        Name of variable

  Output:
    INT    *type            Type of variable
    INT    *n_data          Number of items in variable

  Function value:
    HS_SUCCESS              Successful completion
    HS_NO_MEMEORY           Out of memory
    HS_FILE_ERROR           Cannot open history file

\********************************************************************/
{
   char str[256];
   int fh, fhd;
   std::string fn, fnd;
   INT i, n, status;
   DEF_RECORD def_rec;
   HIST_RECORD rec;
   TAG *tag;

   /* search latest history file */
   status = hs_search_file(&ltime, -1);
   if (status != HS_SUCCESS) {
      cm_msg(MERROR, "hs_get_var", "cannot find recent history file");
      return HS_FILE_ERROR;
   }

   /* open history and definition files */
   hs_open_file(ltime, "hst", O_RDONLY, &fn, &fh);
   hs_open_file(ltime, "idf", O_RDONLY, &fnd, &fhd);
   if (fh < 0 || fhd < 0) {
      cm_msg(MERROR, "hs_get_var", "cannot open index files");
      return HS_FILE_ERROR;
   }

   /* search last definition */
   xseek_end(fnd, fhd);
   n = xcurpos(fnd, fhd) / sizeof(def_rec);
   def_rec.event_id = 0;
   for (i = n - 1; i >= 0; i--) {
      if (!xseek(fnd, fhd, i * sizeof(def_rec))) return HS_FILE_ERROR;
      if (xread(fnd, fhd, (char *) &def_rec, sizeof(def_rec)) < 0) return HS_FILE_ERROR;
      if (def_rec.event_id == event_id)
         break;
   }
   if (def_rec.event_id != event_id) {
      cm_msg(MERROR, "hs_get_var", "event %d not found in index file", event_id);
      return HS_FILE_ERROR;
   }

   /* read definition header */
   xseek(fn, fh, def_rec.def_offset);
   xread(fn, fh, (char *) &rec, sizeof(rec));
   xread(fn, fh, str, NAME_LENGTH);

   /* read event definition */
   n = rec.data_size / sizeof(TAG);
   tag = (TAG *) M_MALLOC(rec.data_size);
   xread(fn, fh, (char *) tag, rec.data_size);

   /* search variable */
   for (i = 0; i < n; i++)
      if (strcmp(tag[i].name, var_name) == 0)
         break;

   close(fh);
   close(fhd);

   if (i < n) {
      *type = tag[i].type;
      *n_data = tag[i].n_data;
   } else {
      *type = *n_data = 0;
      cm_msg(MERROR, "hs_get_var", "variable %s not found", var_name);
      M_FREE(tag);
      return HS_UNDEFINED_VAR;
   }

   M_FREE(tag);
   return HS_SUCCESS;
}


/********************************************************************/
static INT hs_get_tags(DWORD ltime, DWORD event_id, char event_name[NAME_LENGTH], int* n_tags, TAG** tags)
/********************************************************************\

  Routine: hs_get_tags

  Purpose: Get tags for event id

  Input:
    DWORD  ltime            Date at which variable definition should
                            be returned
    DWORD  event_id         Event ID

  Output:
    char    event_name[NAME_LENGTH] Event name from history file
    INT    *n_tags          Number of tags
    TAG   **tags            Pointer to array of tags (should be free()ed by the caller)

  Function value:
    HS_SUCCESS              Successful completion
    HS_NO_MEMEORY           Out of memory
    HS_FILE_ERROR           Cannot open history file

\********************************************************************/
{
   int fh, fhd;
   std::string fn, fnd;
   INT i, n, status;
   DEF_RECORD def_rec;
   HIST_RECORD rec;

   *n_tags = 0;
   *tags = NULL;

   if (rpc_is_remote())
      assert(!"RPC not implemented");

   /* search latest history file */
   status = hs_search_file(&ltime, -1);
   if (status != HS_SUCCESS) {
      cm_msg(MERROR, "hs_get_tags", "cannot find recent history file, hs_search_file() status %d", status);
      return HS_FILE_ERROR;
   }

   /* open history and definition files */
   hs_open_file(ltime, "hst", O_RDONLY, &fn, &fh);
   hs_open_file(ltime, "idf", O_RDONLY, &fnd, &fhd);
   if (fh < 0 || fhd < 0) {
      cm_msg(MERROR, "hs_get_tags", "cannot open index files for time %d", ltime);
      if (fh>0)
         close(fh);
      if (fhd>0)
         close(fhd);
      return HS_FILE_ERROR;
   }

   /* search last definition */
   xseek_end(fnd, fhd);
   n = xcurpos(fnd, fhd) / sizeof(def_rec);
   def_rec.event_id = 0;
   for (i = n - 1; i >= 0; i--) {
      if (!xseek(fnd, fhd, i * sizeof(def_rec))) return HS_FILE_ERROR;
      if (xread(fnd, fhd, (char *) &def_rec, sizeof(def_rec)) < 0) return HS_FILE_ERROR;
      //printf("reading index file found event_id %d, looking for %d\n", def_rec.event_id, event_id);
      if (def_rec.event_id == event_id)
         break;
   }

   if (def_rec.event_id != event_id) {
      //cm_msg(MERROR, "hs_get_tags", "event %d not found in index file", event_id);
      close(fh);
      close(fhd);
      return HS_UNDEFINED_EVENT;
   }

   /* read definition header */
   if (!xseek(fn, fh, def_rec.def_offset)) return HS_FILE_ERROR;
   if (xread(fn, fh, (char *) &rec, sizeof(rec)) < 0) return HS_FILE_ERROR;
   if (xread(fn, fh, event_name, NAME_LENGTH) < 0)    return HS_FILE_ERROR;

   /* read event definition */
   *n_tags = rec.data_size / sizeof(TAG);

   *tags = (TAG*) malloc(rec.data_size);

   if (xread(fn, fh, (char *) (*tags), rec.data_size) < 0) return HS_FILE_ERROR;

   close(fh);
   close(fhd);

   return HS_SUCCESS;
}

double hs_to_double(int tid, const void* ybuffer)
{
   int j = 0;
   /* convert data to float */
   switch (tid) {
   default:
      return 0;
   case TID_BYTE:
      return *(((BYTE *) ybuffer) + j);
   case TID_SBYTE:
      return *(((char *) ybuffer) + j);
   case TID_CHAR:
      return *(((char *) ybuffer) + j);
   case TID_WORD:
      return *(((WORD *) ybuffer) + j);
   case TID_SHORT:
      return *(((short *) ybuffer) + j);
   case TID_DWORD:
      return *(((DWORD *) ybuffer) + j);
   case TID_INT:
      return *(((INT *) ybuffer) + j);
   case TID_BOOL:
      return *(((BOOL *) ybuffer) + j);
   case TID_FLOAT:
      return *(((float *) ybuffer) + j);
   case TID_DOUBLE:
      return *(((double *) ybuffer) + j);
   }
   /* NOT REACHED */
}

static INT hs_read(DWORD event_id, DWORD start_time, DWORD end_time, DWORD interval, const char *tag_name, DWORD var_index, DWORD * time_buffer, DWORD * tbsize, void *data_buffer, DWORD * dbsize, DWORD * data_type, DWORD * data_n, MidasHistoryBufferInterface* buffer)
/********************************************************************\

  Routine: hs_read

  Purpose: Read history for a variable at a certain time interval

  Input:
    DWORD  event_id         Event ID
    DWORD  start_time       Starting Date/Time
    DWORD  end_time         End Date/Time
    DWORD  interval         Minimum time in seconds between reported
                            events. Can be used to skip events
    char   *tag_name        Variable name inside event
    DWORD  var_index        Index if variable is array

  Output:
    DWORD  *time_buffer     Buffer containing times for each value
    DWORD  *tbsize          Size of time buffer
    void   *data_buffer     Buffer containing variable values
    DWORD  *dbsize          Data buffer size
    DWORD  *type            Type of variable (one of TID_xxx)
    DWORD  *n               Number of time/value pairs found
                            in specified interval and placed into
                            time_buffer and data_buffer


  Function value:
    HS_SUCCESS              Successful completion
    HS_NO_MEMEORY           Out of memory
    HS_FILE_ERROR           Cannot open history file
    HS_WRONG_INDEX          var_index exceeds array size of variable
    HS_UNDEFINED_VAR        Variable "tag_name" not found in event
    HS_TRUNCATED            Buffer too small, data has been truncated

\********************************************************************/
{
   DWORD prev_time, last_irec_time;
   int fh, fhd, fhi, cp = 0;
   std::string fn, fnd, fni;
   int delta;
   int status;
   int cache_size;
   INDEX_RECORD irec, *pirec;
   HIST_RECORD rec, drec;
   INT old_def_offset;
   TAG *tag;
   char str[NAME_LENGTH];
   struct tm *tms;
   char *cache = NULL;
   time_t ltime;

   int tag_index = -1;
   int var_type = -1;
   unsigned var_size = 0;
   unsigned var_offset = 0;
         
   int ieof = 0;

   //printf("hs_read event %d, time %d:%d, tagname: \'%s\', varindex: %d\n", event_id, start_time, end_time, tag_name, var_index);

   /* if not time given, use present to one hour in past */
   if (start_time == 0)
      start_time = (DWORD) time(NULL) - 3600;
   if (end_time == 0)
      end_time = (DWORD) time(NULL);

   if (data_n)
      *data_n = 0;
   prev_time = 0;
   last_irec_time = start_time;

   /* search history file for start_time */
   status = hs_search_file(&start_time, 1);
   if (status != HS_SUCCESS) {
      cm_msg(MERROR, "hs_read", "cannot find recent history file");
      if (data_n)
         *data_n = 0;
      if (tbsize)
         *tbsize = 0;
      if (dbsize)
         *dbsize = 0;
      return HS_FILE_ERROR;
   }

   /* open history and definition files */
   hs_open_file(start_time, "hst", O_RDONLY, &fn, &fh);
   hs_open_file(start_time, "idf", O_RDONLY, &fnd, &fhd);
   hs_open_file(start_time, "idx", O_RDONLY, &fni, &fhi);
   if (fh < 0 || fhd < 0 || fhi < 0) {
      cm_msg(MERROR, "hs_read", "cannot open index files");
      if (tbsize)
         *tbsize = 0;
      if (dbsize)
         *dbsize = 0;
      if (data_n)
         *data_n = 0;
      if (fh > 0)
         close(fh);
      if (fhd > 0)
         close(fhd);
      if (fhi > 0)
         close(fhi);
      return HS_FILE_ERROR;
   }

   /* try to read index file into cache */
   xseek_end(fni, fhi);
   cache_size = xcurpos(fni, fhi);

   if (cache_size == 0) {
      goto nextday;
   }

   if (cache_size > 0) {
      cache = (char *) M_MALLOC(cache_size);
      if (cache) {
         xseek(fni, fhi, 0);
         if (xread(fni, fhi, cache, cache_size) < 0) {
            M_FREE(cache);
            if (fh > 0)
               close(fh);
            if (fhd > 0)
               close(fhd);
            if (fhi > 0)
               close(fhi);
            return HS_FILE_ERROR;
         }
      }

      /* search record closest to start time */
      if (cache == NULL) {
         xseek_end(fni, fhi);
         delta = (xcurpos(fni, fhi) / sizeof(irec)) / 2;
         xseek(fni, fhi, delta * sizeof(irec));
         do {
            delta = (int) (abs(delta) / 2.0 + 0.5);
            if (xread(fni, fhi, (char *) &irec, sizeof(irec)) < 0)
               return HS_FILE_ERROR;
            if (irec.time > start_time)
               delta = -delta;

            xseek_cur(fni, fhi, (delta - 1) * sizeof(irec));
         } while (abs(delta) > 1 && irec.time != start_time);
         if (xread(fni, fhi, (char *) &irec, sizeof(irec)) < 0)
            return HS_FILE_ERROR;
         if (irec.time > start_time)
            delta = -abs(delta);

         int i = xcurpos(fni, fhi) + (delta - 1) * sizeof(irec);
         if (i <= 0)
            xseek(fni, fhi, 0);
         else
            xseek_cur(fni, fhi, (delta - 1) * sizeof(irec));
         if (xread(fni, fhi, (char *) &irec, sizeof(irec)) < 0)
            return HS_FILE_ERROR;
      } else {
         delta = (cache_size / sizeof(irec)) / 2;
         cp = delta * sizeof(irec);
         do {
            delta = (int) (abs(delta) / 2.0 + 0.5);
            pirec = (INDEX_RECORD *) (cache + cp);

            //printf("pirec %p, cache %p, cp %d\n", pirec, cache, cp);

            if (pirec->time > start_time)
               delta = -delta;

            cp = cp + delta * sizeof(irec);

            if (cp < 0)
               cp = 0;
         } while (abs(delta) > 1 && pirec->time != start_time);
         pirec = (INDEX_RECORD *) (cache + cp);
         if (pirec->time > start_time)
            delta = -abs(delta);

         if (cp <= delta * (int) sizeof(irec))
            cp = 0;
         else
            cp = cp + delta * sizeof(irec);

         if (cp >= cache_size)
            cp = cache_size - sizeof(irec);
         if (cp < 0)
            cp = 0;

         memcpy(&irec, (INDEX_RECORD *) (cache + cp), sizeof(irec));
         cp += sizeof(irec);
      }
   } else {                     /* file size > 0 */

      cache = NULL;
      irec.time = start_time;
   }

   /* read records, skip wrong IDs */
   old_def_offset = -1;
   last_irec_time = start_time - 24 * 60 * 60;
   do {
      //printf("time %d -> %d\n", last_irec_time, irec.time);

      if (irec.time < last_irec_time) {
         cm_msg(MERROR, "hs_read", "corrupted history data: time does not increase: %d -> %d", last_irec_time, irec.time);
         //*tbsize = *dbsize = *n = 0;
         if (fh > 0)
            close(fh);
         if (fhd > 0)
            close(fhd);
         if (fhi > 0)
            close(fhi);
         hs_gen_index(last_irec_time);
         return HS_SUCCESS;
      }
      last_irec_time = irec.time;
      if (irec.event_id == event_id && irec.time <= end_time && irec.time >= start_time) {
         /* check if record time more than "interval" seconds after previous time */
         if (irec.time >= prev_time + interval) {
            prev_time = irec.time;
            xseek(fn, fh, irec.offset);
            if (xread(fn, fh, (char *) &rec, sizeof(rec)) < 0) {
               cm_msg(MERROR, "hs_read", "corrupted history data at time %d", (int) irec.time);
               //*tbsize = *dbsize = *n = 0;
               if (fh > 0)
                  close(fh);
               if (fhd > 0)
                  close(fhd);
               if (fhi > 0)
                  close(fhi);
               hs_gen_index(last_irec_time);
               return HS_SUCCESS;
            }

            /* if definition changed, read new definition */
            if ((INT) rec.def_offset != old_def_offset) {
               xseek(fn, fh, rec.def_offset);
               xread(fn, fh, (char *) &drec, sizeof(drec));
               xread(fn, fh, str, NAME_LENGTH);

               tag = (TAG *) M_MALLOC(drec.data_size);
               if (tag == NULL) {
                  if (data_n)
                     *data_n = 0;
                  if (tbsize)
                     *tbsize = 0;
                  if (dbsize)
                     *dbsize = 0;
                  if (cache)
                     M_FREE(cache);
                  if (fh > 0)
                     close(fh);
                  if (fhd > 0)
                     close(fhd);
                  if (fhi > 0)
                     close(fhi);
                  return HS_NO_MEMORY;
               }
               xread(fn, fh, (char *) tag, drec.data_size);

               /* find index of tag_name in new definition */
               for (DWORD i = 0; i < drec.data_size / sizeof(TAG); i++)
                  if (equal_ustring(tag[i].name, tag_name)) {
                     tag_index = i;
                     break;
                  }

               /*
                  if ((DWORD) i == drec.data_size/sizeof(TAG))
                  {
                  *n = *tbsize = *dbsize = 0;
                  if (cache)
                  M_FREE(cache);

                  return HS_UNDEFINED_VAR;
                  }
                */

               if (tag_index >= 0 && var_index >= tag[tag_index].n_data) {
                  if (data_n)
                     *data_n = 0;
                  if (tbsize)
                     *tbsize = 0;
                  if (dbsize)
                     *dbsize = 0;
                  if (cache)
                     M_FREE(cache);
                  M_FREE(tag);
                  if (fh > 0)
                     close(fh);
                  if (fhd > 0)
                     close(fhd);
                  if (fhi > 0)
                     close(fhi);
                  return HS_WRONG_INDEX;
               }

               /* calculate offset for variable */
               if (tag_index >= 0) {
                  var_type = tag[tag_index].type;

                  if (data_type)
                     *data_type = var_type;

                  /* loop over all previous variables */
                  var_offset = 0;
                  for (int i=0; i<tag_index; i++)
                     var_offset += rpc_tid_size(tag[i].type) * tag[i].n_data;

                  /* strings have size n_data */
                  if (tag[tag_index].type == TID_STRING)
                     var_size = tag[tag_index].n_data;
                  else
                     var_size = rpc_tid_size(tag[tag_index].type);

                  var_offset += var_size * var_index;
               }

               M_FREE(tag);
               old_def_offset = rec.def_offset;
               xseek(fn, fh, irec.offset + sizeof(rec));
            }

            if (buffer) {
               /* copy time from header */
               DWORD t = irec.time;
               char buf[16]; // biggest data is 8-byte "double"
               assert(var_size <= sizeof(buf));
               xseek_cur(fn, fh, var_offset);
               xread(fn, fh, buf, var_size);
               buffer->Add(t, hs_to_double(var_type, buf));
            } else if (tag_index >= 0 && data_n) {
               /* check if data fits in buffers */
               if ((*data_n) * sizeof(DWORD) >= *tbsize || (*data_n) * var_size >= *dbsize) {
                  *dbsize = (*data_n) * var_size;
                  *tbsize = (*data_n) * sizeof(DWORD);
                  if (cache)
                     M_FREE(cache);
                  if (fh > 0)
                     close(fh);
                  if (fhd > 0)
                     close(fhd);
                  if (fhi > 0)
                     close(fhi);
                  return HS_TRUNCATED;
               }

               /* copy time from header */
               time_buffer[*data_n] = irec.time;

               /* copy data from record */
               xseek_cur(fn, fh, var_offset);
               xread(fn, fh, (char *) data_buffer + (*data_n) * var_size, var_size);

               /* increment counter */
               (*data_n)++;
            }
         }
      }
         
      /* read next index record */
      if (cache) {
         if (cp >= cache_size) {
            ieof = -1;
            M_FREE(cache);
            cache = NULL;
         } else {

          try_again:

            ieof = sizeof(irec);

            memcpy(&irec, cache + cp, sizeof(irec));
            cp += sizeof(irec);

            /* if history file is broken ... */
            if (irec.time < last_irec_time || irec.time > last_irec_time + 24 * 60 * 60) {
               //if (irec.time < last_irec_time) {
               //printf("time %d -> %d, cache_size %d, cp %d\n", last_irec_time, irec.time, cache_size, cp);

               //printf("Seeking next record...\n");

               while (cp < cache_size) {
                  DWORD *evidp = (DWORD *) (cache + cp);
                  if (*evidp == event_id) {
                     //printf("Found at cp %d\n", cp);
                     goto try_again;
                  }

                  cp++;
               }

               ieof = -1;
            }
         }
      } else {
         ieof = xread(fni, fhi, (char *) &irec, sizeof(irec), true);
      }

      /* end of file: search next history file */
      if (ieof <= 0) {
         nextday:

         if (fh > 0)
            close(fh);
         if (fhd > 0)
            close(fhd);
         if (fhi > 0)
            close(fhi);
         fh = fhd = fhi = 0;

         /* advance one day */
         ltime = (time_t) last_irec_time;
         tms = localtime(&ltime);
         tms->tm_hour = tms->tm_min = tms->tm_sec = 0;
         last_irec_time = (DWORD) mktime(tms);

         last_irec_time += 3600 * 24;

         if (last_irec_time > end_time)
            break;

         /* search next file */
         status = hs_search_file(&last_irec_time, 1);
         if (status != HS_SUCCESS)
            break;

         /* open history and definition files */
         hs_open_file(last_irec_time, "hst", O_RDONLY, &fn, &fh);
         hs_open_file(last_irec_time, "idf", O_RDONLY, &fnd, &fhd);
         hs_open_file(last_irec_time, "idx", O_RDONLY, &fni, &fhi);
         if (fh < 0 || fhd < 0 || fhi < 0) {
            cm_msg(MERROR, "hs_read", "cannot open index files");
            break;
         }

         /* try to read index file into cache */
         xseek_end(fni, fhi);
         cache_size = xcurpos(fni, fhi);

         if (cache_size == 0) {
            goto nextday;
         }

         xseek(fni, fhi, 0);
         cache = (char *) M_MALLOC(cache_size); // FIXME: is this a memory leak?
         if (cache) {
            if (xread(fni, fhi, cache, cache_size) < 0) {
               break;
            }
            /* read first record */
            cp = 0;
            memcpy(&irec, cache, sizeof(irec));
         } else {
            /* read first record */
            if (xread(fni, fhi, (char *) &irec, sizeof(irec)) < 0) {
               break;
            }
         }

         /* old definition becomes invalid */
         old_def_offset = -1;
      }
      //if (event_id==4 && irec.event_id == event_id)
      //  printf("time %d end %d\n", irec.time, end_time);
   } while (irec.time < end_time);

   if (cache)
      M_FREE(cache);
   if (fh)
      close(fh);
   if (fhd)
      close(fhd);
   if (fhi)
      close(fhi);

   if (dbsize && data_n)
      *dbsize = *data_n * var_size;
   if (tbsize && data_n)
      *tbsize = *data_n * sizeof(DWORD);

   return HS_SUCCESS;
}

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

/********************************************************************/
/**
Display history for a given event at stdout. The output
can be redirected to be read by Excel for example. 
@param event_id         Event ID
@param start_time       Starting Date/Time
@param end_time         End Date/Time
@param interval         Minimum time in seconds between reported                                                                                
                            events. Can be used to skip events
@param binary_time      Display DWORD time stamp
@return HS_SUCCESS, HS_FILE_ERROR
*/
/********************************************************************/
static INT hs_dump(DWORD event_id, DWORD start_time, DWORD end_time, DWORD interval, BOOL binary_time)
{
   DWORD prev_time, last_irec_time;
   time_t ltime;
   int fh, fhd, fhi;
   std::string fn, fnd, fni;
   INT i, j, delta, status, n_tag = 0, old_n_tag = 0;
   INDEX_RECORD irec;
   HIST_RECORD rec, drec;
   INT old_def_offset, offset;
   TAG *tag = NULL, *old_tag = NULL;
   char str[NAME_LENGTH], data_buffer[10000];
   struct tm *tms;

   /* if not time given, use present to one hour in past */
   if (start_time == 0)
      start_time = (DWORD) time(NULL) - 3600;
   if (end_time == 0)
      end_time = (DWORD) time(NULL);

   /* search history file for start_time */
   status = hs_search_file(&start_time, 1);
   if (status != HS_SUCCESS) {
      cm_msg(MERROR, "hs_dump", "cannot find recent history file");
      return HS_FILE_ERROR;
   }

   /* open history and definition files */
   hs_open_file(start_time, "hst", O_RDONLY, &fn, &fh);
   hs_open_file(start_time, "idf", O_RDONLY, &fnd, &fhd);
   hs_open_file(start_time, "idx", O_RDONLY, &fni, &fhi);
   if (fh < 0 || fhd < 0 || fhi < 0) {
      cm_msg(MERROR, "hs_dump", "cannot open index files");
      return HS_FILE_ERROR;
   }

   /* search record closest to start time */
   xseek_end(fni, fhi);
   delta = (xcurpos(fni, fhi) / sizeof(irec)) / 2;
   xseek(fni, fhi, delta * sizeof(irec));
   do {
      delta = (int) (abs(delta) / 2.0 + 0.5);
      xread(fni, fhi, (char *) &irec, sizeof(irec));
      if (irec.time > start_time)
         delta = -delta;

      xseek_cur(fni, fhi, (delta - 1) * sizeof(irec));
   } while (abs(delta) > 1 && irec.time != start_time);
   xread(fni, fhi, (char *) &irec, sizeof(irec));
   if (irec.time > start_time)
      delta = -abs(delta);

   i = xcurpos(fni, fhi) + (delta - 1) * sizeof(irec);
   if (i <= 0)
      xseek(fni, fhi, 0);
   else
      xseek_cur(fni, fhi, (delta - 1) * sizeof(irec));
   xread(fni, fhi, (char *) &irec, sizeof(irec));

   /* read records, skip wrong IDs */
   old_def_offset = -1;
   prev_time = 0;
   last_irec_time = 0;
   do {
      if (irec.time < last_irec_time) {
         cm_msg(MERROR, "hs_dump", "corrupted history data: time does not increase: %d -> %d", last_irec_time, irec.time);
         hs_gen_index(last_irec_time);
         return HS_FILE_ERROR;
      }
      last_irec_time = irec.time;
      if (irec.event_id == event_id && irec.time <= end_time && irec.time >= start_time) {
         if (irec.time >= prev_time + interval) {
            prev_time = irec.time;
            xseek(fn, fh, irec.offset);
            xread(fn, fh, (char *) &rec, sizeof(rec));

            /* if definition changed, read new definition */
            if ((INT) rec.def_offset != old_def_offset) {
               xseek(fn, fh, rec.def_offset);
               xread(fn, fh, (char *) &drec, sizeof(drec));
               xread(fn, fh, str, NAME_LENGTH);

               if (tag == NULL)
                  tag = (TAG *) M_MALLOC(drec.data_size);
               else
                  tag = (TAG *) realloc(tag, drec.data_size);
               if (tag == NULL)
                  return HS_NO_MEMORY;
               xread(fn, fh, (char *) tag, drec.data_size);
               n_tag = drec.data_size / sizeof(TAG);

               /* print tag names if definition has changed */
               if (old_tag == NULL || old_n_tag != n_tag || memcmp(old_tag, tag, drec.data_size) != 0) {
                  printf("Date\t");
                  for (i = 0; i < n_tag; i++) {
                     if (tag[i].n_data == 1 || tag[i].type == TID_STRING)
                        printf("%s\t", tag[i].name);
                     else
                        for (j = 0; j < (INT) tag[i].n_data; j++)
                           printf("%s%d\t", tag[i].name, j);
                  }
                  printf("\n");

                  if (old_tag == NULL)
                     old_tag = (TAG *) M_MALLOC(drec.data_size);
                  else
                     old_tag = (TAG *) realloc(old_tag, drec.data_size);
                  memcpy(old_tag, tag, drec.data_size);
                  old_n_tag = n_tag;
               }

               old_def_offset = rec.def_offset;
               xseek(fn, fh, irec.offset + sizeof(rec));
            }

            /* print time from header */
            if (binary_time)
               printf("%d ", irec.time);
            else {
               ltime = (time_t) irec.time;
               sprintf(str, "%s", ctime(&ltime) + 4);
               str[20] = '\t';
               printf("%s", str);
            }

            /* read data */
            xread(fn, fh, data_buffer, rec.data_size);

            /* interprete data from tag definition */
            offset = 0;
            for (i = 0; i < n_tag; i++) {
               /* strings have a length of n_data */
               if (tag[i].type == TID_STRING) {
                  printf("%s\t", data_buffer + offset);
                  offset += tag[i].n_data;
               } else if (tag[i].n_data == 1) {
                  /* non-array data */
                  db_sprintf(str, data_buffer + offset, rpc_tid_size(tag[i].type), 0, tag[i].type);
                  printf("%s\t", str);
                  offset += rpc_tid_size(tag[i].type);
               } else
                  /* loop over array data */
                  for (j = 0; j < (INT) tag[i].n_data; j++) {
                     db_sprintf(str, data_buffer + offset, rpc_tid_size(tag[i].type), 0, tag[i].type);
                     printf("%s\t", str);
                     offset += rpc_tid_size(tag[i].type);
                  }
            }
            printf("\n");
         }
      }

      /* read next index record */
      i = xread(fni, fhi, (char *) &irec, sizeof(irec), true);

      /* end of file: search next history file */
      if (i <= 0) {
         close(fh);
         close(fhd);
         close(fhi);

         /* advance one day */
         ltime = (time_t) last_irec_time;
         tms = localtime(&ltime);
         tms->tm_hour = tms->tm_min = tms->tm_sec = 0;
         last_irec_time = (DWORD) mktime(tms);

         last_irec_time += 3600 * 24;
         if (last_irec_time > end_time)
            break;

         /* search next file */
         status = hs_search_file((DWORD *) & last_irec_time, 1);
         if (status != HS_SUCCESS)
            break;

         /* open history and definition files */
         hs_open_file(last_irec_time, "hst", O_RDONLY, &fn, &fh);
         hs_open_file(last_irec_time, "idf", O_RDONLY, &fnd, &fhd);
         hs_open_file(last_irec_time, "idx", O_RDONLY, &fni, &fhi);
         if (fh < 0 || fhd < 0 || fhi < 0) {
            cm_msg(MERROR, "hs_dump", "cannot open index files");
            break;
         }

         /* read first record */
         i = xread(fni, fhi, (char *) &irec, sizeof(irec), true);
         if (i <= 0)
            break;

         /* old definition becomes invalid */
         old_def_offset = -1;
      }
   } while (irec.time < end_time);

   M_FREE(tag);
   M_FREE(old_tag);
   close(fh);
   close(fhd);
   close(fhi);

   return HS_SUCCESS;
}

/**dox***************************************************************/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

#endif                          /* OS_VXWORKS hs section */

/**dox***************************************************************/
#endif                          /* DOXYGEN_SHOULD_SKIP_THIS */

#if 0
char* sort_names(char* names)
{
   int i, p;
   int len = 0;
   int num = 0;
   char* arr_names;
   struct poor_mans_list sorted;

   for (i=0, p=0; names[p]!=0; i++) {
      const char*pp = names+p;
      int pplen = strlen(pp);
      //printf("%d [%s] %d\n", i, pp, pplen);
      if (pplen > len)
         len = pplen;
      p += strlen(names+p) + 1;
   }

   num = i;

   len+=1; // space for string terminator '\0'

   arr_names = (char*)malloc(len*num);

   for (i=0, p=0; names[p]!=0; i++) {
      const char*pp = names+p;
      strlcpy(arr_names+i*len, pp, len);
      p += strlen(names+p) + 1;
   }

   free(names);

   qsort(arr_names, num, len, sort_tags);

   list_init(&sorted);

   for (i=0; i<num; i++)
      list_add(&sorted, arr_names+i*len);

   return sorted.names;
}
#endif

/*------------------------------------------------------------------*/

#define AMALLOC(num_bins) (double*)malloc(sizeof(double)*(num_bins))
#define AFREE(var) if (var) free(var); (var) = NULL;

class BinnedBuffer: public MidasHistoryBufferInterface
{
public:
   double start_time = 0;
   double end_time = 0;
   int num_bins = 0;

public:
   double bin_size = 0;
   int* count = NULL;
   double* sum0 = NULL;
   double* sum1 = NULL;
   double* sum2 = NULL;
   double* vmax = NULL;
   double* vmin = NULL;

   double* mean = NULL;
   double* rms  = NULL;

   time_t last_time = 0;
   double last_value = 0;

public:
   void Init()
   {
      bin_size = (end_time - start_time)/num_bins;

      count = (int*)malloc(sizeof(int)*num_bins);
      sum0 = AMALLOC(num_bins);
      sum1 = AMALLOC(num_bins);
      sum2 = AMALLOC(num_bins);
      vmax = AMALLOC(num_bins);
      vmin = AMALLOC(num_bins);
      mean = AMALLOC(num_bins);
      rms = AMALLOC(num_bins);
   }

   void Finish()
   {
      for (int i=0; i<num_bins; i++) {
         if (count[i] > 0) {
            mean[i] = sum1[i]/sum0[i];
            double var = sum2[i]/sum0[i] - mean[i]*mean[i];
            if (var > 0)
               rms[i] = sqrt(var);
            else
               rms[i] = 0;
         } else {
            mean[i] = 0;
            rms[i] = 0;
         }
      }
   }

   ~BinnedBuffer() // dtor
   {
      AFREE(count);
      AFREE(sum0);
      AFREE(sum1);
      AFREE(sum2);
      AFREE(vmax);
      AFREE(vmin);
      
      AFREE(mean);
      AFREE(rms);
   }

public:
   void Add(time_t t, double v)
   {
      int i = (t-start_time)/bin_size;
      assert(i >= 0);
      assert(i < num_bins);
      if (count[i] == 0) {
         vmin[i] = v;
         vmax[i] = v;
      }
      count[i]++;
      sum0[i] += 1;
      sum1[i] += v;
      sum2[i] += v*v;
      if (v > vmax[i]) vmax[i] = v;
      if (v < vmin[i]) vmin[i] = v;

      last_time = t;
      last_value = v;
   }
};

/*------------------------------------------------------------------*/

class MidasHistory: public MidasHistoryInterface
{
public:
   HNDLE fDB;
   int fDebug;

   std::vector<std::string> fEventsCache;
   std::map<std::string, std::vector<TAG> > fTagsCache;
   std::map<std::string, int > fEvidCache;

public:
   MidasHistory() // ctor
   {
      fDebug = 0;
   }

   ~MidasHistory() // dtor
   {
      // empty
   }

   /*------------------------------------------------------------------*/

   int hs_connect(const char* path)
   {
      cm_get_experiment_database(&fDB, NULL);

      /* delete obsolete odb entries */

      if (1) {
         HNDLE hKey;
         int status = db_find_key(fDB, 0, "/History/ListSource", &hKey);
         if (status == DB_SUCCESS)
            db_delete_key(fDB, hKey, FALSE);
      }

      ::hs_set_path(path);

      if (fDebug)
         printf("hs_connect: path [%s]\n", path);

      return HS_SUCCESS;
   }

   /*------------------------------------------------------------------*/

   int hs_disconnect()
   {
      hs_clear_cache();
      return HS_SUCCESS;
   }

   /*------------------------------------------------------------------*/

   int hs_set_debug(int debug)
   {
      return debug;
   }

   /*------------------------------------------------------------------*/

   int hs_clear_cache()
   {
      if (fDebug)
         printf("hs_clear_cache!\n");

      fEventsCache.clear();
      fTagsCache.clear();
      fEvidCache.clear();
      return HS_SUCCESS;
   }

   /*------------------------------------------------------------------*/

   int FindEventId(const char* event_name)
   {
      HNDLE hKeyRoot;
      int status;
      char name[256];
      STRLCPY(name, event_name);
      char *s = strchr(name, '/');
      if (s)
         *s = ':';

      //printf("Looking for event id for \'%s\'\n", name);
   
      status = db_find_key(fDB, 0, "/History/Events", &hKeyRoot);
      if (status == DB_SUCCESS) {
         for (int i = 0;; i++) {
            HNDLE hKey;
            KEY key;
         
            status = db_enum_key(fDB, hKeyRoot, i, &hKey);
            if (status != DB_SUCCESS)
               break;
         
            status = db_get_key(fDB, hKey, &key);
            assert(status == DB_SUCCESS);
         
            //printf("key \'%s\'\n", key.name);

            int evid = (WORD) strtol(key.name, NULL, 0);
            if (evid == 0)
               continue;

            char tmp[NAME_LENGTH+NAME_LENGTH+2];
            int size = sizeof(tmp);
            status = db_get_data(fDB, hKey, tmp, &size, TID_STRING);
            assert(status == DB_SUCCESS);

            //printf("got %d \'%s\' looking for \'%s\'\n", evid, tmp, name);

            if (equal_ustring(name, tmp))
               return evid;
         }
      }

      return -1;
   }

   /*------------------------------------------------------------------*/

   int AllocateEventId(const char* event_name)
   {
      int status;
      char name[256];
      STRLCPY(name, event_name);
      char *s = strchr(name, '/');
      if (s)
         *s = ':';
      
      // special event id for run transitions
      if (strcmp(name, "Run transitions")==0) {
         status = db_set_value(fDB, 0, "/History/Events/0", name, strlen(name)+1, 1, TID_STRING);
         assert(status == DB_SUCCESS);
         return 0;
      }

      if (1) {
         std::string tmp = msprintf("/Equipment/%s/Common/Event ID", name);

         WORD evid = 0;
         int size = sizeof(evid);
         status = db_get_value(fDB, 0, tmp.c_str(), &evid, &size, TID_WORD, FALSE);
         if (status == DB_SUCCESS) {

            std::string he = msprintf("/History/Events/%d", evid);

            std::string xname;
            status = db_get_value_string(fDB, 0, he.c_str(), 0, &xname);
            if (status == DB_SUCCESS && xname != event_name) {
               cm_msg(MERROR, "add_event", "History events \"%s\" and \"%s\" use the same history event id %d. If both equipments write to the history, their data will be mixed up. To fix this, enable per-variable history, turn off the \"MIDAS\" history (use \"FILE\" history) or change event IDs or set \"common/log history\" to zero", event_name, xname.c_str(), evid);
            }

            status = db_set_value(fDB, 0, he.c_str(), name, strlen(name)+1, 1, TID_STRING);
            assert(status == DB_SUCCESS);

            //printf("AllocateEventId: event [%s] allocated common/event id %d\n", event_name, evid);

            return evid;
         }
      }

      for (int evid = 101; evid < 65000; evid++) {
         char tmp[256];
         HNDLE hKey;

         sprintf(tmp,"/History/Events/%d", evid);

         status = db_find_key(fDB, 0, tmp, &hKey);
         if (status != DB_SUCCESS) {

            status = db_set_value(fDB, 0, tmp, name, strlen(name)+1, 1, TID_STRING);
            assert(status == DB_SUCCESS);

            //printf("AllocateEventId: event [%s] allocated next sequential id %d\n", event_name, evid);

            return evid;
         }
      }

      cm_msg(MERROR, "AllocateEventId", "Cannot allocate history event id - all in use - please examine /History/Events");
      return -1;
   }

   /*------------------------------------------------------------------*/

   int CreateOdbTags(int event_id, const char* event_name, int ntags, const TAG tags[])
   {
      int disableTags;
      int oldTags;
      int size, status;

      /* create history tags for mhttpd */

      disableTags = 0;
      size = sizeof(disableTags);
      status = db_get_value(fDB, 0, "/History/DisableTags", &disableTags, &size, TID_BOOL, TRUE);

      oldTags = 0;
      size = sizeof(oldTags);
      status = db_get_value(fDB, 0, "/History/CreateOldTags", &oldTags, &size, TID_BOOL, FALSE);

      if (disableTags) {
         HNDLE hKey;

         status = db_find_key(fDB, 0, "/History/Tags", &hKey);
         if (status == DB_SUCCESS) {
            status = db_delete_key(fDB, hKey, FALSE);
            if (status != DB_SUCCESS)
               cm_msg(MERROR, "add_event", "Cannot delete /History/Tags, db_delete_key() status %d", status);
         }

      } else if (oldTags) {

         char buf[256];

         sprintf(buf, "/History/Tags/%d", event_id);

         //printf("Set tag \'%s\' = \'%s\'\n", buf, event_name);

         status = db_set_value(fDB, 0, buf, (void*)event_name, strlen(event_name)+1, 1, TID_STRING);
         assert(status == DB_SUCCESS);

         for (int i=0; i<ntags; i++) {
            WORD v = (WORD) tags[i].n_data;
            sprintf(buf, "/History/Tags/Tags %d/%s", event_id, tags[i].name);

            //printf("Set tag \'%s\' = %d\n", buf, v);

            status = db_set_value(fDB, 0, buf, &v, sizeof(v), 1, TID_WORD);
            assert(status == DB_SUCCESS);

            if (strlen(tags[i].name) == NAME_LENGTH-1)
               cm_msg(MERROR, "add_event",
                      "Tag name \'%s\' in event %d (%s) may have been truncated to %d characters",
                      tags[i].name, event_id, event_name, NAME_LENGTH-1);
         }

      } else {

         const int kLength = 32 + NAME_LENGTH + NAME_LENGTH;
         char buf[kLength];
         HNDLE hKey;

         sprintf(buf, "/History/Tags/%d", event_id);
         status = db_find_key(fDB, 0, buf, &hKey);

         if (status == DB_SUCCESS) {
            // add new tags
            KEY key;

            status = db_get_key(fDB, hKey, &key);
            assert(status == DB_SUCCESS);

            assert(key.type == TID_STRING);

            if (key.item_size < kLength && key.num_values == 1) {
               // old style tags are present. Convert them to new style!

               HNDLE hTags;

               cm_msg(MINFO, "add_event", "Converting old event %d (%s) tags to new style", event_id, event_name);

               strlcpy(buf, event_name, kLength);

               status = db_set_data(fDB, hKey, buf, kLength, 1, TID_STRING);
               assert(status == DB_SUCCESS);

               sprintf(buf, "/History/Tags/Tags %d", event_id);

               status = db_find_key(fDB, 0, buf, &hTags);

               if (status == DB_SUCCESS) {
                  for (int i=0; ; i++) {
                     HNDLE h;
                     int size;
                     KEY key;
                     WORD w;

                     status = db_enum_key(fDB, hTags, i, &h);
                     if (status == DB_NO_MORE_SUBKEYS)
                        break;
                     assert(status == DB_SUCCESS);

                     status = db_get_key(fDB, h, &key);

                     size = sizeof(w);
                     status = db_get_data(fDB, h, &w, &size, TID_WORD);
                     assert(status == DB_SUCCESS);

                     sprintf(buf, "%d[%d] %s", 0, w, key.name);
                  
                     status = db_set_data_index(fDB, hKey, buf, kLength, 1+i, TID_STRING);
                     assert(status == DB_SUCCESS);
                  }

                  status = db_delete_key(fDB, hTags, TRUE);
                  assert(status == DB_SUCCESS);
               }

               // format conversion has changed the key, get it again
               status = db_get_key(fDB, hKey, &key);
               assert(status == DB_SUCCESS);
            }

            if (1) {
               // add new tags
         
               int size = key.item_size * key.num_values;
               int num = key.num_values;

               char* s = (char*)malloc(size);
               assert(s != NULL);

               TAG* t = (TAG*)malloc(sizeof(TAG)*(key.num_values + ntags));
               assert(t != NULL);

               status = db_get_data(fDB, hKey, s, &size, TID_STRING);
               assert(status == DB_SUCCESS);

               for (int i=1; i<key.num_values; i++) {
                  char* ss = s + i*key.item_size;

                  t[i].type = 0;
                  t[i].n_data = 0;
                  t[i].name[0] = 0;

                  if (isdigit(ss[0])) {
                     //sscanf(ss, "%d[%d] %s", &t[i].type, &t[i].n_data, t[i].name);

                     t[i].type = strtoul(ss, &ss, 0);
                     assert(*ss == '[');
                     ss++;
                     t[i].n_data = strtoul(ss, &ss, 0);
                     assert(*ss == ']');
                     ss++;
                     assert(*ss == ' ');
                     ss++;
                     strlcpy(t[i].name, ss, sizeof(t[i].name));

                     //printf("type %d, n_data %d, name [%s]\n", t[i].type, t[i].n_data, t[i].name);
                  }
               }

               for (int i=0; i<ntags; i++) {
                  int k = 0;

                  for (int j=1; j<key.num_values; j++) {
                     if (equal_ustring((char*)tags[i].name, (char*)t[j].name)) {
                        if ((tags[i].type!=t[j].type) || (tags[i].n_data!=t[j].n_data)) {
                           cm_msg(MINFO, "add_event", "Event %d (%s) tag \"%s\" type and size changed from %d[%d] to %d[%d]",
                                  event_id, event_name,
                                  tags[i].name,
                                  t[j].type, t[j].n_data,
                                  tags[i].type, tags[i].n_data);
                           k = j;
                           break;
                        }

                        k = -1;
                        break;
                     }
                  }

                  // if tag not present, k==0, so append it to the array

                  if (k==0)
                     k = num;

                  if (k > 0) {
                     sprintf(buf, "%d[%d] %s", tags[i].type, tags[i].n_data, tags[i].name);

                     status = db_set_data_index(fDB, hKey, buf, kLength, k, TID_STRING);
                     assert(status == DB_SUCCESS);

                     if (k >= num)
                        num = k+1;
                  }
               }

               free(s);
               free(t);
            }

         } else if (status == DB_NO_KEY) {
            // create new array of tags
            status = db_create_key(fDB, 0, buf, TID_STRING);
            assert(status == DB_SUCCESS);

            status = db_find_key(fDB, 0, buf, &hKey);
            assert(status == DB_SUCCESS);

            strlcpy(buf, event_name, kLength);

            status = db_set_data(fDB, hKey, buf, kLength, 1, TID_STRING);
            assert(status == DB_SUCCESS);

            for (int i=0; i<ntags; i++) {
               sprintf(buf, "%d[%d] %s", tags[i].type, tags[i].n_data, tags[i].name);

               status = db_set_data_index(fDB, hKey, buf, kLength, 1+i, TID_STRING);
               assert(status == DB_SUCCESS);
            }
         } else {
            cm_msg(MERROR, "add_event", "Error: db_find_key(%s) status %d", buf, status);
            return HS_FILE_ERROR;
         }
      }

      return HS_SUCCESS;
   }

   /*------------------------------------------------------------------*/

   int hs_define_event(const char* event_name, time_t timestamp, int ntags, const TAG tags[])
   {
      int event_id = FindEventId(event_name);
      if (event_id < 0)
         event_id = AllocateEventId(event_name);
      if (event_id < 0)
         return HS_FILE_ERROR;
      fEvidCache[event_name] = event_id;
      CreateOdbTags(event_id, event_name, ntags, tags);
      return ::hs_define_event(event_id, (char*)event_name, (TAG*)tags, ntags*sizeof(TAG));
   }

   /*------------------------------------------------------------------*/

   int hs_write_event(const char*  event_name, time_t timestamp, int data_size, const char* data)
   {
      int event_id = fEvidCache[event_name];
      //printf("write event [%s] evid %d\n", event_name, event_id);
      return ::hs_write_event(event_id, (void*)data, data_size);
   }

   /*------------------------------------------------------------------*/

   int hs_flush_buffers()
   {
      //printf("hs_flush_buffers!\n");
      return HS_SUCCESS;
   }

   /*------------------------------------------------------------------*/

   int GetEventsFromOdbEvents(std::vector<std::string> *events)
   {
      HNDLE hKeyRoot;
      int status;

      status = db_find_key(fDB, 0, "/History/Events", &hKeyRoot);
      if (status != DB_SUCCESS) {
         return HS_FILE_ERROR;
      }

      /* loop over tags to display event names */
      for (int i = 0;; i++) {
         HNDLE hKeyEq;
         char *s;
         char evname[1024+NAME_LENGTH];
         int size;
      
         status = db_enum_key(fDB, hKeyRoot, i, &hKeyEq);
         if (status != DB_SUCCESS)
            break;
    
         size = sizeof(evname);
         status = db_get_data(fDB, hKeyEq, evname, &size, TID_STRING);
         assert(status == DB_SUCCESS);

         s = strchr(evname,':');
         if (s)
            *s = '/';

         /* skip duplicated event names */

         int found = 0;
         for (unsigned i=0; i<events->size(); i++) {
            if (equal_ustring(evname, (*events)[i].c_str())) {
               found = 1;
               break;
            }
         }
    
         if (found)
            continue;

         events->push_back(evname);

         //printf("event \'%s\'\n", evname);
      }

      return HS_SUCCESS;
   }

   int GetEventsFromOdbTags(std::vector<std::string> *events)
   {
      HNDLE hKeyRoot;
      int status;

      status = db_find_key(fDB, 0, "/History/Tags", &hKeyRoot);
      if (status != DB_SUCCESS) {
         return HS_FILE_ERROR;
      }
   
      /* loop over tags to display event names */
      for (int i = 0;; i++) {
         HNDLE hKeyEq;
         KEY key;
         char *s;
         WORD event_id;
         char evname[1024+NAME_LENGTH];
         int size;
      
         status = db_enum_key(fDB, hKeyRoot, i, &hKeyEq);
         if (status != DB_SUCCESS)
            break;
    
         /* get event name */
         db_get_key(fDB, hKeyEq, &key);
      
         //printf("key \'%s\'\n", key.name);
      
         if (key.type != TID_STRING)
            continue;

         /* parse event name in format: "event_id" or "event_id:var_name" */
         s = key.name;
      
         event_id = (WORD)strtoul(s,&s,0);
         if (event_id == 0)
            continue;
         if (s[0] != 0)
            continue;

         size = sizeof(evname);
         status = db_get_data_index(fDB, hKeyEq, evname, &size, 0, TID_STRING);
         assert(status == DB_SUCCESS);

         /* skip duplicated event names */

         int found = 0;
         for (unsigned i=0; i<events->size(); i++) {
            if (equal_ustring(evname, (*events)[i].c_str())) {
               found = 1;
               break;
            }
         }
    
         if (found)
            continue;

         events->push_back(evname);

         //printf("event %d \'%s\'\n", event_id, evname);
      }

      return HS_SUCCESS;
   }

   int hs_get_events(time_t t, std::vector<std::string> *pevents)
   {
      assert(pevents);
      pevents->clear();

      if (fEventsCache.size() == 0) {
         int status;

         if (fDebug)
            printf("hs_get_events: reading events list!\n");
         
         status = GetEventsFromOdbTags(&fEventsCache);

         if (status != HS_SUCCESS)
            status = GetEventsFromOdbEvents(&fEventsCache);

         if (status != HS_SUCCESS)
            return status;
      }

      for (unsigned i=0; i<fEventsCache.size(); i++)
         pevents->push_back(fEventsCache[i]);
         
      return HS_SUCCESS;
   }

   int GetEventIdFromHS(time_t ltime, const char* evname, const char* tagname)
   {
      HNDLE hKeyRoot;
      int status;

      status = db_find_key(fDB, 0, "/History/Events", &hKeyRoot);
      if (status != DB_SUCCESS) {
         return -1;
      }

      for (int i = 0;; i++) {
         HNDLE hKey;
         KEY key;
         int  evid;
         char buf[256];
         int size;
         char *s;
         int ntags = 0;
         TAG* tags = NULL;
         char event_name[NAME_LENGTH];

         status = db_enum_key(fDB, hKeyRoot, i, &hKey);
         if (status != DB_SUCCESS)
            break;

         status = db_get_key(fDB, hKey, &key);
         assert(status == DB_SUCCESS);

         if (!isdigit(key.name[0]))
            continue;

         evid = atoi(key.name);

         assert(key.item_size < (int)sizeof(buf));

         size = sizeof(buf);
         status = db_get_data(fDB, hKey, buf, &size, TID_STRING);
         assert(status == DB_SUCCESS);

         strlcpy(event_name, buf, sizeof(event_name));

         s = strchr(buf,':');
         if (s)
            *s = 0;

         //printf("Found event %d, event [%s] name [%s], looking for [%s][%s]\n", evid, event_name, buf, evname, tagname);

         if (!equal_ustring((char *)evname, buf))
            continue;

         status = ::hs_get_tags((DWORD)ltime, evid, event_name, &ntags, &tags);

         for (int j=0; j<ntags; j++) {
            //printf("at %d [%s] looking for [%s]\n", j, tags[j].name, tagname);

            if (equal_ustring((char *)tagname, tags[j].name)) {
               if (tags)
                  free(tags);
               return evid;
            }
         }

         if (tags)
            free(tags);
         tags = NULL;
      }

      return -1;
   }

   int GetEventIdFromOdbTags(const char* evname, const char* tagname)
   {
      HNDLE hKeyRoot;
      int status;
      
      status = db_find_key(fDB, 0, "/History/Tags", &hKeyRoot);
      if (status != DB_SUCCESS) {
         return -1;
      }

      for (int i = 0;; i++) {
         HNDLE hKey;
         KEY key;
         int evid;
         char buf[256];
         int size;
         char *s;

         status = db_enum_key(fDB, hKeyRoot, i, &hKey);
         if (status != DB_SUCCESS)
            break;

         status = db_get_key(fDB, hKey, &key);
         assert(status == DB_SUCCESS);

         if (key.type != TID_STRING)
            continue;

         if (!isdigit(key.name[0]))
            continue;

         evid = atoi(key.name);

         assert(key.item_size < (int)sizeof(buf));

         size = sizeof(buf);
         status = db_get_data_index(fDB, hKey, buf, &size, 0, TID_STRING);
         assert(status == DB_SUCCESS);

         s = strchr(buf,'/');
         if (s)
            *s = 0;

         //printf("Found event %d, name [%s], looking for [%s][%s]\n", evid, buf, evname, tagname);

         if (!equal_ustring((char *)evname, buf))
            continue;

         for (int j=1; j<key.num_values; j++) {
            size = sizeof(buf);
            status = db_get_data_index(fDB, hKey, buf, &size, j, TID_STRING);
            assert(status == DB_SUCCESS);

            if (!isdigit(buf[0]))
               continue;

            s = strchr(buf,' ');
            if (!s)
               continue;

            s++;
 
            //printf("at %d [%s] [%s] compare to [%s]\n", j, buf, s, tagname);

            if (equal_ustring((char *)tagname, s)) {
               //printf("Found evid %d\n", evid);
               return evid;
            }
         }
      }

      return -1;
   }

   int GetEventId(time_t t, const char* event_name, const char* tag_name, int *pevid)
   {
      int event_id = -1;

      if (fDebug && event_name != NULL && tag_name != NULL)
         printf("xhs_event_id for event [%s], tag [%s]\n", event_name, tag_name);

      *pevid = 0;

      /* use "/History/Tags" if available */
      event_id = GetEventIdFromOdbTags(event_name, tag_name);
      
      /* if no Tags, use "/History/Events" and hs_get_tags() to read definition from history files */
      if (event_id < 0)
         event_id = GetEventIdFromHS(t, event_name, tag_name);

      /* if nothing works, use hs_get_event_id() */
      if (event_id <= 0) {
         DWORD evid = 0;
         int status = ::hs_get_event_id((DWORD)t, (char*)event_name, &evid);
         if (status != HS_SUCCESS)
            return status;
         event_id = evid;
      }
      
      if (event_id < 0)
         return HS_UNDEFINED_VAR;
      
      *pevid = event_id;
      
      return HS_SUCCESS;
   }
   
   int GetTagsFromHS(const char* event_name, std::vector<TAG> *ptags)
   {
      time_t now = time(NULL);
      int evid;
      int status = GetEventId(now, event_name, NULL, &evid);
      if (status != HS_SUCCESS)
         return status;
      
      if (fDebug)
         printf("hs_get_tags: get tags for event [%s] %d\n", event_name, evid);
      
      int ntags;
      TAG* tags;
      status =  ::hs_get_tags((DWORD)now, evid, (char*)event_name, &ntags, &tags);
      
      if (status != HS_SUCCESS)
         return status;
      
      for (int i=0; i<ntags; i++)
         ptags->push_back(tags[i]);
      
      if (tags)
         free(tags);
      
      if (fDebug)
         printf("hs_get_tags: get tags for event [%s] %d, found %d tags\n", event_name, evid, ntags);

      return HS_SUCCESS;
   }

   int GetTagsFromOdb(const char* event_name, std::vector<TAG> *ptags)
   {
      HNDLE hKeyRoot;
      int status;

      status = db_find_key(fDB, 0, "/History/Tags", &hKeyRoot);
      if (status != DB_SUCCESS) {
         return HS_FILE_ERROR;
      }
   
      /* loop over equipment to display event name */
      for (int i = 0;; i++) {
         HNDLE hKey;
         KEY key;
         WORD event_id;
         char buf[256];
         int size;
         char* s;
      
         status = db_enum_key(fDB, hKeyRoot, i, &hKey);
         if (status != DB_SUCCESS)
            break;
    
         /* get event name */
         status = db_get_key(fDB, hKey, &key);
         assert(status == DB_SUCCESS);
      
         /* parse event id */
         if (!isdigit(key.name[0]))
            continue;

         event_id = atoi(key.name);
         if (event_id == 0)
            continue;

         if (key.item_size >= (int)sizeof(buf))
            continue;

         if (key.num_values == 1) { // old format of "/History/Tags"

            HNDLE hKeyDir;
            sprintf(buf, "Tags %d", event_id);
            status = db_find_key(fDB, hKeyRoot, buf, &hKeyDir);
            if (status != DB_SUCCESS)
               continue;

            /* loop over tags */
            for (int j=0; ; j++) {
               HNDLE hKey;
               WORD array;
               int size;
               char var_name[NAME_LENGTH];
            
               status = db_enum_key(fDB, hKeyDir, j, &hKey);
               if (status != DB_SUCCESS)
                  break;
            
               /* get event name */
               status = db_get_key(fDB, hKey, &key);
               assert(status == DB_SUCCESS);
            
               array = 1;
               size  = sizeof(array);
               status = db_get_data(fDB, hKey, &array, &size, TID_WORD);
               assert(status == DB_SUCCESS);
            
               strlcpy(var_name, key.name, sizeof(var_name));
            
               //printf("Found %s, event %d (%s), tag (%s) array %d\n", key.name, event_id, event_name, var_name, array);
            
               TAG t;
               STRLCPY(t.name, var_name);
               t.n_data = array;
               t.type = 0;
               
               ptags->push_back(t);
            }

            continue;
         }

         if (key.type != TID_STRING)
            continue;

         size = sizeof(buf);
         status = db_get_data_index(fDB, hKey, buf, &size, 0, TID_STRING);
         assert(status == DB_SUCCESS);

         if (strchr(event_name, '/')==NULL) {
            char* s = strchr(buf, '/');
            if (s)
               *s = 0;
         }

         //printf("evid %d, name [%s]\n", event_id, buf);

         if (!equal_ustring(buf, event_name))
            continue;

         /* loop over tags */
         for (int j=1; j<key.num_values; j++) {
            int array;
            int size;
            char var_name[NAME_LENGTH];
            int ev_type;
         
            size = sizeof(buf);
            status = db_get_data_index(fDB, hKey, buf, &size, j, TID_STRING);
            assert(status == DB_SUCCESS);

            //printf("index %d [%s]\n", j, buf);

            if (!isdigit(buf[0]))
               continue;

            sscanf(buf, "%d[%d]", &ev_type, &array);

            s = strchr(buf, ' ');
            if (!s)
               continue;
            s++;

            STRLCPY(var_name, s);

            TAG t;
            STRLCPY(t.name, var_name);
            t.n_data = array;
            t.type = ev_type;

            //printf("Found %s, event %d, tag (%s) array %d, type %d\n", buf, event_id, var_name, array, ev_type);

            ptags->push_back(t);
         }
      }

      return HS_SUCCESS;
   }

   /*------------------------------------------------------------------*/

   int hs_get_tags(const char* event_name, time_t t, std::vector<TAG> *ptags)
   {
      std::vector<TAG>& ttt = fTagsCache[event_name];

      if (ttt.size() == 0) {
         int status = HS_FILE_ERROR;

         if (fDebug)
            printf("hs_get_tags: reading tags for event [%s]\n", event_name);

         status = GetTagsFromOdb(event_name, &ttt);

         if (status != HS_SUCCESS)
            status = GetTagsFromHS(event_name, &ttt);

         if (status != HS_SUCCESS)
            return status;
      }

      for (unsigned i=0; i<ttt.size(); i++)
         ptags->push_back(ttt[i]);

      return HS_SUCCESS;
   }

   /*------------------------------------------------------------------*/

   int hs_get_last_written(time_t start_time, int num_var, const char* const event_name[], const char* const tag_name[], const int var_index[], time_t last_written[])
   {
      for (int i=0; i<num_var; i++)
         last_written[i] = 0;
      return HS_FILE_ERROR;
   }

   /*------------------------------------------------------------------*/


   int hs_read(time_t start_time, time_t end_time, time_t interval,
               int num_var,
               const char* const event_name[], const char* const tag_name[], const int var_index[],
               int num_entries[],
               time_t* time_buffer[], double* data_buffer[],
               int read_status[])
   {
      DWORD* tbuffer = NULL;
      char* ybuffer = NULL;
      DWORD bsize, tsize;
      int hbuffer_size = 0;
      
      if (hbuffer_size == 0) {
         hbuffer_size = 1000 * sizeof(DWORD);
         tbuffer = (DWORD*)malloc(hbuffer_size);
         ybuffer = (char*)malloc(hbuffer_size);
      }

      for (int i=0; i<num_var; i++) {
         DWORD tid = 0;
         int event_id = 0;

         if (event_name[i]==NULL) {
            read_status[i] = HS_UNDEFINED_EVENT;
            num_entries[i] = 0;
            continue;
         }
         
         int status = GetEventId(end_time, event_name[i], tag_name[i], &event_id);
         
         if (status != HS_SUCCESS) {
            read_status[i] = status;
            continue;
         }
         
         DWORD n_point = 0;
         
         do {
            bsize = tsize = hbuffer_size;
            memset(ybuffer, 0, bsize);
            status = ::hs_read(event_id, (DWORD)start_time, (DWORD)end_time, (DWORD)interval,
                               tag_name[i], var_index[i],
                               tbuffer, &tsize,
                               ybuffer, &bsize,
                               &tid, &n_point,
                               NULL);
         
            if (fDebug)
               printf("hs_read %d \'%s\' [%d] returned %d, %d entries\n", event_id, tag_name[i], var_index[i], status, n_point);
         
            if (status == HS_TRUNCATED) {
               hbuffer_size *= 2;
               tbuffer = (DWORD*)realloc(tbuffer, hbuffer_size);
               assert(tbuffer);
               ybuffer = (char*)realloc(ybuffer, hbuffer_size);
               assert(ybuffer);
            }

         } while (status == HS_TRUNCATED);
        
         read_status[i] = status;

         time_t* x = (time_t*)malloc(n_point*sizeof(time_t));
         assert(x);
         double* y = (double*)malloc(n_point*sizeof(double));
         assert(y);

         time_buffer[i] = x;
         data_buffer[i] = y;

         int n_vp = 0;

         for (unsigned j = 0; j < n_point; j++) {
            x[n_vp] = tbuffer[j];
          
            /* convert data to float */
            switch (tid) {
            default:
               y[n_vp] =  0;
               break;
            case TID_BYTE:
               y[n_vp] =  *(((BYTE *) ybuffer) + j);
               break;
            case TID_SBYTE:
               y[n_vp] =  *(((char *) ybuffer) + j);
               break;
            case TID_CHAR:
               y[n_vp] =  *(((char *) ybuffer) + j);
               break;
            case TID_WORD:
               y[n_vp] =  *(((WORD *) ybuffer) + j);
               break;
            case TID_SHORT:
               y[n_vp] =  *(((short *) ybuffer) + j);
               break;
            case TID_DWORD:
               y[n_vp] =  *(((DWORD *) ybuffer) + j);
               break;
            case TID_INT:
               y[n_vp] =  *(((INT *) ybuffer) + j);
               break;
            case TID_BOOL:
               y[n_vp] =  *(((BOOL *) ybuffer) + j);
               break;
            case TID_FLOAT:
               y[n_vp] =  *(((float *) ybuffer) + j);
               break;
            case TID_DOUBLE:
               y[n_vp] =  *(((double *) ybuffer) + j);
               break;
            }
          
            n_vp++;
         }

         num_entries[i] = n_vp;
      }

      if (ybuffer)
         free(ybuffer);
      if (tbuffer)
         free(tbuffer);

      return HS_SUCCESS;
   }

   /*------------------------------------------------------------------*/
#if 0
   int hs_read2(time_t start_time, time_t end_time, time_t interval,
                int num_var,
                const char* const event_name[], const char* const tag_name[], const int var_index[],
                int num_entries[],
                time_t* time_buffer[],
                double* mean_buffer[],
                double* rms_buffer[],
                double* min_buffer[],
                double* max_buffer[],
                int read_status[])
   {
      int status = hs_read(start_time, end_time, interval, num_var, event_name, tag_name, var_index, num_entries, time_buffer, mean_buffer, read_status);

      for (int i=0; i<num_var; i++) {
         int num = num_entries[i];
         rms_buffer[i] = (double*)malloc(sizeof(double)*num);
         min_buffer[i] = (double*)malloc(sizeof(double)*num);
         max_buffer[i] = (double*)malloc(sizeof(double)*num);

         for (int j=0; j<num; j++) {
            rms_buffer[i][j] = 0;
            min_buffer[i][j] = mean_buffer[i][j];
            max_buffer[i][j] = mean_buffer[i][j];
         }
      }
      
      return status;
   }
#endif

   int hs_read_buffer(time_t start_time, time_t end_time,
                      int num_var, const char* const event_name[], const char* const tag_name[], const int var_index[],
                      MidasHistoryBufferInterface* buffer[],
                      int read_status[])
   {
      for (int i=0; i<num_var; i++) {
         int event_id = 0;

         if (event_name[i]==NULL) {
            read_status[i] = HS_UNDEFINED_EVENT;
            continue;
         }
         
         int status = GetEventId(end_time, event_name[i], tag_name[i], &event_id);
         
         if (status != HS_SUCCESS) {
            read_status[i] = status;
            continue;
         }
         
         status = ::hs_read(event_id, (DWORD)start_time, (DWORD)end_time, 0,
                            tag_name[i], var_index[i],
                            NULL, NULL,
                            NULL, NULL,
                            NULL, NULL,
                            buffer[i]);
         
         if (fDebug) {
            printf("hs_read %d \'%s\' [%d] returned %d\n", event_id, tag_name[i], var_index[i], status);
         }
        
         read_status[i] = status;
      }

      return HS_SUCCESS;
   }
   
   int hs_read_binned(time_t start_time, time_t end_time, int num_bins,
                      int num_var, const char* const event_name[], const char* const tag_name[], const int var_index[],
                      int num_entries[],
                      int* count_bins[], double* mean_bins[], double* rms_bins[], double* min_bins[], double* max_bins[],
                      time_t last_time[], double last_value[],
                      int read_status[])
   {
      int status;

      MidasHistoryBufferInterface** buffer = new MidasHistoryBufferInterface*[num_var];
      BinnedBuffer** binnedbuffer = new BinnedBuffer*[num_var];

      for (int i=0; i<num_var; i++) {
         BinnedBuffer* b = new BinnedBuffer();
         b->start_time = start_time;
         b->end_time = end_time;
         b->num_bins = num_bins;
         b->Init();
         buffer[i] = b;
         binnedbuffer[i] = b;
      }

      status = hs_read_buffer(start_time, end_time,
                              num_var, event_name, tag_name, var_index,
                              buffer,
                              read_status);

      for (int i=0; i<num_var; i++) {
         BinnedBuffer* b = binnedbuffer[i];
         binnedbuffer[i] = NULL;
         buffer[i] = NULL;
         b->Finish();
         if (count_bins) {
            *count_bins = b->count;
            b->count = NULL;
         }
         if (mean_bins) {
            *mean_bins = b->mean;
            b->mean = NULL;
         }
         if (rms_bins) {
            *rms_bins = b->rms;
            b->rms = NULL;
         }
         if (min_bins) {
            *min_bins = b->vmin;
            b->vmin = NULL;
         }
         if (max_bins) {
            *max_bins = b->vmax;
            b->vmax = NULL;
         }
         if (last_time)
            last_time[i] = b->last_time;
         if (last_value)
            last_value[i] = b->last_value;
         delete b;
      }

      delete[] buffer;
      delete[] binnedbuffer;

      return status;
   }

}; // end class


/********************************************************************/
/**
Define history panel in ODB with certain variables and default
values for everything else
@param group            History group name
@param panel            Historyh panel name
@param var              Vector of variables
@return HS_SUCCESS
*/
/********************************************************************/

#define HISTORY_PANEL(_name) const char *_name[] = {\
"[.]",\
"Variables = STRING : [64] :",\
"Timescale = STRING : [32] 10m",\
"Zero ylow = BOOL : n",\
"Show run markers = BOOL : y",\
"Buttons = STRING[7] :",\
"[32] 10m",\
"[32] 1h",\
"[32] 3h",\
"[32] 12h",\
"[32] 24h",\
"[32] 3d",\
"[32] 7d",\
"Log axis = BOOL : n",\
"Show values = BOOL : y",\
"Sort vars = BOOL : n",\
"Show old vars = BOOL : n",\
"Minimum = FLOAT : -inf",\
"Maximum = FLOAT : inf",\
"Label = STRING : [32] ",\
"Colour = STRING : [32] ",\
"Formula = STRING : [64] ",\
"Show fill = BOOL : y",\
"",\
NULL }


INT hs_define_panel(const char *group, const char *panel, const std::vector<std::string> var)
{
   HNDLE hDB, hKey, hKeyVar;
   HISTORY_PANEL(history_panel_str);
   char str[256];

   const char *color[] = {
           "#00AAFF", "#FF9000", "#FF00A0", "#00C030",
           "#A0C0D0", "#D0A060", "#C04010", "#807060",
           "#F0C000", "#2090A0", "#D040D0", "#90B000",
           "#B0B040", "#B0B0FF", "#FFA0A0", "#A0FFA0",
           "#808080"};

   cm_get_experiment_database(&hDB, nullptr);

   snprintf(str, sizeof(str), "/History/Display/%s/%s", group, panel);

   db_create_record(hDB, 0, str, strcomb1(history_panel_str).c_str());
   db_find_key(hDB, 0, str, &hKey);
   if (!hKey)
      return DB_NO_MEMORY;

   int i=0;
   for(auto const& v: var) {
      db_find_key(hDB, hKey, "Variables", &hKeyVar);
      db_set_data_index(hDB, hKeyVar, v.c_str(), 64, i, TID_STRING);

      str[0] = 0;
      db_set_value_index(hDB, hKey, "Formula", str, 64, i, TID_STRING, false);
      db_set_value_index(hDB, hKey, "Label", str, 32, i, TID_STRING, false);
      db_set_value_index(hDB, hKey, "Colour", color[i < 16 ? i : 16], 32, i, TID_STRING, false);

      i++;
   }

   return HS_SUCCESS;
}


MidasHistoryInterface* MakeMidasHistory()
{
#if 0
   // midas history is a singleton class
   static MidasHistory* gh = NULL;
   if (!gh)
      gh = new MidasHistory;
   return gh;
#endif
   return new MidasHistory();
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
