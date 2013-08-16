/********************************************************************\

  Name:         history.h
  Created by:   Konstantin Olchanski / TRIUMF

  Contents:     Interface for the MIDAS history system

  $Id$

\********************************************************************/

#ifndef HISTORY_H
#define HISTORY_H

#include <string>
#include <vector>

#include "midas.h"

/// \file history.h
///

class MidasHistoryInterface;

// "factory" pattern

MidasHistoryInterface* MakeMidasHistory();
MidasHistoryInterface* MakeMidasHistoryODBC();
MidasHistoryInterface* MakeMidasHistorySqlite();
MidasHistoryInterface* MakeMidasHistorySqlDebug();

#define HS_GET_READER   1
#define HS_GET_WRITER   2
#define HS_GET_INACTIVE 4
#define HS_GET_DEFAULT  8

// construct history interface class from logger history channel definition in /Logger/History/0/...
int hs_get_history(HNDLE hDB, HNDLE hKey, int flags, int debug_flag, MidasHistoryInterface **mh);

// MIDAS history data buffer interface class

class MidasHistoryBufferInterface
{
 public:
   MidasHistoryBufferInterface() { }; // ctor
   virtual ~MidasHistoryBufferInterface() { }; // dtor
 public:
   virtual void Add(time_t time, double value) = 0;
};

// MIDAS history interface class

class MidasHistoryInterface
{
 public:
   char name[NAME_LENGTH]; /// history channel name
   char type[NAME_LENGTH]; /// history type: MIDAS, ODBC, SQLITE, etc

 public:
   MidasHistoryInterface() // ctor
      {
         name[0] = 0;
         type[0] = 0;
      }

   virtual ~MidasHistoryInterface() { }; // dtor

 public:
  virtual int hs_connect(const char* connect_string) = 0; ///< returns HS_SUCCESS
  virtual int hs_disconnect() = 0;                  ///< disconnect from history, returns HS_SUCCESS

  virtual int hs_set_debug(int debug) = 0;          ///< set debug level, returns previous debug level

  virtual int hs_clear_cache() = 0; ///< clear internal cache, returns HS_SUCCESS

  // functions for writing into the history, used by mlogger

  virtual int hs_define_event(const char* event_name, int ntags, const TAG tags[]) = 0; ///< see hs_define_event(), returns HS_SUCCESS or HS_FILE_ERROR

  virtual int hs_write_event(const char*  event_name, time_t timestamp, int data_size, const char* data) = 0; ///< see hs_write_event(), returns HS_SUCCESS or HS_FILE_ERROR

  virtual int hs_flush_buffers() = 0; ///< flush buffered data to storage where it is visible to mhttpd

  // functions for reading from the history, used by mhttpd, mhist

  virtual int hs_get_events(std::vector<std::string> *pevents) = 0; ///< get list of all events, returns HS_SUCCESS

  virtual int hs_get_tags(const char* event_name, std::vector<TAG> *ptags) = 0; ///< use event names returned by hs_get_events_odbc(), see hs_get_tags(), returns HS_SUCCESS

  virtual int hs_get_last_written(int num_var, const char* const event_name[], const char* const tag_name[], const int var_index[], time_t last_written[]) = 0;

  virtual int hs_read_buffer(time_t start_time, time_t end_time,
                             int num_var, const char* const event_name[], const char* const tag_name[], const int var_index[],
                             MidasHistoryBufferInterface* buffer[],
                             int status[]) = 0; ///< returns HS_SUCCESS

  virtual int hs_read(time_t start_time, time_t end_time, time_t interval,
                      int num_var, const char* const event_name[], const char* const tag_name[], const int var_index[],
                      int num_entries[], time_t* time_buffer[], double* data_buffer[],
                      int status[]) = 0; ///< see hs_read(), returns HS_SUCCESS

  virtual int hs_read_binned(time_t start_time, time_t end_time, int num_bins,
                             int num_var, const char* const event_name[], const char* const tag_name[], const int var_index[],
                             int num_entries[],
                             int* count_bins[], double* mean_bins[], double* rms_bins[], double* min_bins[], double* max_bins[],
                             time_t last_time[], double last_value[],
                             int status[]) = 0; ///< returns HS_SUCCESS
};

#endif
// end
