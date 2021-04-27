#ifndef _MIDAS_C_COMPAT_H_
#define _MIDAS_C_COMPAT_H_
#include "midas.h"
#include <string>
#include "string.h"

/*
This file defines C-compatible functions that can be called from other
languages (currently python). The implementation of most of these functions
are simple wrappers to functions from the main library, although some do
conversions between C char* and C++ strings.

The definitions of these functions should NEVER use std::string etc - they must
only use regular C types (or structures containing regular C types).

If the return value of a function defined here is not an INT, the type of the
return value must be specified in the constructor of the MidasLib class in
python/midas/__init__.py.

 */

extern "C" {
   void c_free(void* mem);
   void c_free_list(void** mem_list, int arr_len);
   INT c_example_string_c_bufsize(char* buffer, DWORD buffer_size);
   INT c_example_string_c_alloc(char** dest);
   INT c_example_vector(void** dest, int& arr_len);
   INT c_example_string_vector(char*** dest, int& arr_len);

   INT c_bm_flush_cache(INT buffer_handle, INT async_flag);
   INT c_bm_open_buffer(const char *buffer_name, INT buffer_size, INT * buffer_handle);
   INT c_bm_receive_event(INT buffer_handle, void *destination, INT * buf_size, INT async_flag);
   INT c_bm_remove_event_request(INT buffer_handle, INT request_id);
   INT c_bm_request_event(INT buffer_handle, short int event_id, short int trigger_mask, INT sampling_type, INT * request_id);
   INT c_cm_check_deferred_transition();
   INT c_cm_connect_client(const char *client_name, HNDLE * hConn);
   INT c_cm_connect_experiment(const char *host_name, const char *exp_name, const char *client_name, void (*func) (char *));
   INT c_cm_disconnect_client(HNDLE hConn, BOOL bShutdown);
   INT c_cm_deregister_transition(INT transition);
   INT c_cm_disconnect_experiment();
   INT c_cm_get_environment(char *host_name, int host_name_size, char *exp_name, int exp_name_size);
   INT c_cm_get_experiment_database(HNDLE * hDB, HNDLE * hKeyClient);
   const char* c_cm_get_revision(void);
   const char* c_cm_get_version(void);
   INT c_cm_msg(INT message_type, const char *filename, INT line, const char *routine, const char *format, ...) MATTRPRINTF(5,6);
   INT c_cm_msg_facilities(char*** dest, int& dest_len);
   INT c_cm_register_deferred_transition(INT transition, BOOL(*func) (INT, BOOL));
   INT c_cm_register_function(INT id, INT(*func) (INT, void **));
   INT c_cm_register_transition(INT transition, INT(*func) (INT, char *), int sequence_number);
   INT c_cm_set_transition_sequence(INT transition, INT sequence_number);
   INT c_cm_start_watchdog_thread(void);
   INT c_cm_stop_watchdog_thread(void);
   INT c_cm_transition(INT transition, INT run_number, char *error, INT strsize, INT async_flag, INT debug_flag);
   INT c_cm_yield(INT millisec);
   INT c_db_close_record(HNDLE hdb, HNDLE hkey);
   INT c_db_copy_json_ls(HNDLE hDB, HNDLE hKey, char **buffer, int* buffer_size, int* buffer_end);
   INT c_db_copy_json_save(HNDLE hDB, HNDLE hKey, char **buffer, int* buffer_size, int* buffer_end);
   INT c_db_create_key(HNDLE hdb, HNDLE key_handle, const char *key_name, DWORD type);
   INT c_db_create_link(HNDLE hdb, HNDLE key_handle, const char *link_name, const char *destination);
   INT c_db_delete_key(HNDLE database_handle, HNDLE key_handle, BOOL follow_links);
   INT c_db_enum_key(HNDLE hDB, HNDLE hKey, INT idx, HNDLE * subkey_handle);
   INT c_db_find_key(HNDLE hdb, HNDLE hkey, const char *name, HNDLE * hsubkey);
   INT c_db_find_link(HNDLE hDB, HNDLE hKey, const char *key_name, HNDLE * subhKey);
   INT c_db_get_key(HNDLE hdb, HNDLE key_handle, KEY * key);
   INT c_db_get_link_data(HNDLE hdb, HNDLE key_handle, void *data, INT * buf_size, DWORD type);
   INT c_db_get_parent(HNDLE hDB, HNDLE hKey, HNDLE * parenthKey);
   INT c_db_get_value(HNDLE hdb, HNDLE hKeyRoot, const char *key_name, void *data, INT * size, DWORD type, BOOL create);
   INT c_db_open_record(HNDLE hdb, HNDLE hkey, void *ptr, INT rec_size, WORD access, void (*dispatcher) (INT, INT, void *), void *info);
   INT c_db_rename_key(HNDLE hDB, HNDLE hKey, const char *name);
   INT c_db_reorder_key(HNDLE hDB, HNDLE hKey, INT index);
   INT c_db_resize_string(HNDLE hDB, HNDLE hKeyRoot, const char *key_name, int num_values, int max_string_size);
   INT c_db_set_link_data(HNDLE hdb, HNDLE key_handle, void *data, INT buf_size, int num_values, DWORD type);
   INT c_db_set_num_values(HNDLE hDB, HNDLE hKey, INT num_values);
   INT c_db_set_value(HNDLE hdb, HNDLE hKeyRoot, const char *key_name, const void *data, INT size, INT num_values, DWORD type);
   INT c_db_set_value_index(HNDLE hDB, HNDLE hKeyRoot, const char *key_name, const void *data, INT data_size, INT index, DWORD type, BOOL truncate);
   INT c_db_unwatch(HNDLE hDB, HNDLE hKey);
   INT c_db_watch(HNDLE hDB, HNDLE hKey, void (*dispatcher) (INT, INT, INT, void*), void* info);
   INT c_jrpc_client_call(HNDLE hconn, char* cmd, char* args, char* buf, int buf_length);
   INT c_rpc_flush_event(void);
   INT c_rpc_is_remote(void);
   INT c_rpc_send_event(INT buffer_handle, const EVENT_HEADER *event, INT buf_size, INT async_flag, INT mode);
   INT c_ss_daemon_init(BOOL keep_stdout);
}

#endif
