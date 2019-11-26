#include "midas_c_compat.h"
#include "midas.h"
#include <string>
#include "string.h"
#include "stdlib.h"
#include "stdarg.h"

/*
We define a simple free function to ensure that python clients can
free any memory that was allocated by midas. We define it as part
of this library (rather than importing libc directly in python) to
ensure that the same version of libc is used for the alloc and free.
*/
void c_free(void* mem) {
   free(mem);
}

void c_free_list(void** mem_list, int arr_len) {
   for (int i = 0; i < arr_len; i++) {
      free(mem_list[i]);
   }

   free(mem_list);
}

/*
Copies the content for src to dest (at most dest_size bytes).
dest should already have been allocated to the correct size.
If the destination is not large enough to hold the entire src
string, we return DB_TRUNCATED; otherwise we return SUCCESS.

In general it's preferable to accept a char** from python rather than
a buffer of a fixed size. Although python must then remember to free
the memory we allocated.
 */
INT copy_string_to_c(std::string src, char* dest, DWORD dest_size) {
   strncpy(dest, src.c_str(), dest_size);

   if (src.size() > dest_size) {
      return DB_TRUNCATED;
   }

   return SUCCESS;
}

/*
Copies the content of vec into an array of type 'T' at dest. Will malloc the
memory needed, so you must later call c_free() on dest. Fills dest_len with
the size of the vector.
 */
template <class T> INT copy_vector_to_c(std::vector<T> vec, void** dest, int& dest_len) {
   dest_len = vec.size();
   *dest = malloc(sizeof(T) * dest_len);
   std::copy(vec.begin(), vec.end(), (T*)*dest);
   return SUCCESS;
}

/*
Copies the content of vec into an array of char* at dest. Will malloc the
memory needed for each string (and for the array itself), so you must later call
c_free_list() on dest. Fills dest_len with the size of the vector.
 */
INT copy_vector_string_to_c(std::vector<std::string> vec, char*** dest, int& dest_len) {
   dest_len = vec.size();
   *dest = (char**) malloc(sizeof(char*) * dest_len);

   for (int i = 0; i < dest_len; i++) {
      (*dest)[i] = strdup(vec[i].c_str());
   }

   return SUCCESS;
}

/*
Example of how one could wrap a midas function that returns/fills a std::string.
In this version we accept a buffer of a specified size from the user.

The python code would be:
```
buffer = ctypes.create_string_buffer(64)
lib.c_example_string_c_bufsize(buffer, 64)
py_str = buffer.value.decode("utf-8")
```
 */
INT c_example_string_c_bufsize(char* buffer, DWORD buffer_size) {
   std::string retval("My string that would come from a C++ function");
   return copy_string_to_c(retval, buffer, buffer_size);
}

/*
Example of how one could wrap a midas function that returns/fills a std::string.
In this version we allocate memory for the C char array. The caller must later
free this memory themselves.

The python code would be (note the final free!):
```
buffer = ctypes.c_char_p()
lib.c_example_string_c_alloc(ctypes.byref(buffer))
py_str = buffer.value.decode("utf-8")
lib.c_free(buffer)
```
 */
INT c_example_string_c_alloc(char** dest) {
   std::string retval("My string that would come from a C++ function");
   *dest = strdup(retval.c_str());
   return SUCCESS;
}

/*
Example of how one could wrap a midas function that returns/fills a std::vector.
In this version we allocate memory for the C array. The caller must later
free this memory themselves.

The python code would be (note the final free!):
```
import ctypes
import midas.client

client = midas.client.MidasClient("pytest")
lib = client.lib

arr = ctypes.c_void_p()
arr_len = ctypes.c_int()
lib.c_example_vector(ctypes.byref(arr), ctypes.byref(arr_len))
casted = ctypes.cast(arr, ctypes.POINTER(ctypes.c_float))
py_list = casted[:arr_len.value]
lib.c_free(arr)
```
 */
INT c_example_vector(void** dest, int& dest_len) {
   std::vector<float> retvec;
   for (int i = 0; i < 10; i++) {
      retvec.push_back(i/3.);
   }

   return copy_vector_to_c(retvec, dest, dest_len);
}

/*
Example of how one could wrap a midas function that returns/fills a std::vector.
In this version we allocate memory for the C array. The caller must later
free this memory themselves.

The python code would be (note the final free!):
```
import ctypes
import midas.client
client = midas.client.MidasClient("pytest")
lib = client.lib

arr = ctypes.POINTER(ctypes.c_char_p)()
arr_len = ctypes.c_int()
lib.c_example_string_vector(ctypes.byref(arr), ctypes.byref(arr_len))
casted = ctypes.cast(arr, ctypes.POINTER(ctypes.c_char_p))
py_list = [casted[i].decode("utf-8") for i in range(arr_len.value)]
lib.c_free_list(arr, arr_len)
```
 */
INT c_example_string_vector(char*** dest, int& dest_len) {
   std::vector<std::string> retvec;
   retvec.push_back("Hello");
   retvec.push_back("world!");

   return copy_vector_string_to_c(retvec, dest, dest_len);
}

INT c_bm_flush_cache(INT buffer_handle, INT async_flag) {
   return bm_flush_cache(buffer_handle, async_flag);
}

INT c_bm_open_buffer(const char *buffer_name, INT buffer_size, INT * buffer_handle) {
   return bm_open_buffer(buffer_name, buffer_size, buffer_handle);
}

INT c_bm_receive_event(INT buffer_handle, void *destination, INT * buf_size, INT async_flag) {
   return bm_receive_event(buffer_handle, destination, buf_size, async_flag);
}

INT c_bm_remove_event_request(INT buffer_handle, INT request_id) {
   return bm_remove_event_request(buffer_handle, request_id);
}

INT c_bm_request_event(INT buffer_handle, short int event_id, short int trigger_mask, INT sampling_type, INT * request_id) {
   // Final argument is function pointer that python lib doesn't need.
   return bm_request_event(buffer_handle, event_id, trigger_mask, sampling_type, request_id, 0);
}

INT c_cm_check_deferred_transition(void) {
   return cm_check_deferred_transition();
}

INT c_cm_connect_experiment(const char *host_name, const char *exp_name, const char *client_name, void (*func) (char *)) {
   return cm_connect_experiment(host_name, exp_name, client_name, func);
}

INT c_cm_deregister_transition(INT transition) {
   return cm_deregister_transition(transition);
}

INT c_cm_disconnect_experiment() {
   return cm_disconnect_experiment();
}

INT c_cm_get_environment(char *host_name, int host_name_size, char *exp_name, int exp_name_size) {
   return cm_get_environment(host_name, host_name_size, exp_name, exp_name_size);
}

INT c_cm_get_experiment_database(HNDLE * hDB, HNDLE * hKeyClient) {
   return cm_get_experiment_database(hDB, hKeyClient);
}

const char* c_cm_get_revision(void) {
   // If this changes to returning a string, do:
   // return strdup(cm_get_revision().c_str());
   return cm_get_revision();
}

const char* c_cm_get_version(void) {
   // If this changes to returning a string, do:
   // return strdup(cm_get_version().c_str());
   return cm_get_version();
}

INT c_cm_msg(INT message_type, const char *filename, INT line, const char *routine, const char *format, ...) {
   va_list argptr;
   char message[1000];
   va_start(argptr, format);
   vsprintf(message, (char *) format, argptr);
   va_end(argptr);
   return cm_msg(message_type, filename, line, routine, message);
}

/*
Remember to call c_free_list on the dest afterwards. E.g.:
```
import ctypes
import midas.client
lib = midas.client.MidasClient("pytest").lib

arr = ctypes.POINTER(ctypes.c_char_p)()
arr_len = ctypes.c_int()
lib.c_cm_msg_facilities(ctypes.byref(arr), ctypes.byref(arr_len))
casted = ctypes.cast(arr, ctypes.POINTER(ctypes.c_char_p))
py_list = [casted[i].decode("utf-8") for i in range(arr_len.value)]
lib.c_free_list(arr, arr_len)
```
*/
INT c_cm_msg_facilities(char*** dest, int& dest_len) {
   std::vector<std::string> retvec;
   INT retcode = cm_msg_facilities(&retvec);
   if (retcode == SUCCESS) {
      return copy_vector_string_to_c(retvec, dest, dest_len);
   } else {
      return retcode;
   }
}

INT c_cm_register_deferred_transition(INT transition, BOOL(*func) (INT, BOOL)) {
   return cm_register_deferred_transition(transition, func);
}

INT c_cm_register_transition(INT transition, INT(*func) (INT, char *), int sequence_number) {
   return cm_register_transition(transition, func, sequence_number);
}

INT c_cm_set_transition_sequence(INT transition, INT sequence_number) {
   return cm_set_transition_sequence(transition, sequence_number);
}

INT c_cm_start_watchdog_thread(void) {
   return cm_start_watchdog_thread();
}

INT c_cm_stop_watchdog_thread(void) {
   return cm_stop_watchdog_thread();
}

INT c_cm_transition(INT transition, INT run_number, char *error, INT strsize, INT async_flag, INT debug_flag) {
   return cm_transition(transition, run_number, error, strsize, async_flag, debug_flag);
}

INT c_cm_yield(INT millisec) {
   return cm_yield(millisec);
}

INT c_db_close_record(HNDLE hdb, HNDLE hkey) {
   return db_close_record(hdb, hkey);
}

INT c_db_copy_json_ls(HNDLE hDB, HNDLE hKey, char **buffer, int* buffer_size, int* buffer_end) {
   return db_copy_json_ls(hDB, hKey, buffer, buffer_size, buffer_end);
}

INT c_db_copy_json_save(HNDLE hDB, HNDLE hKey, char **buffer, int* buffer_size, int* buffer_end) {
   return db_copy_json_save(hDB, hKey, buffer, buffer_size, buffer_end);
}

INT c_db_create_key(HNDLE hdb, HNDLE key_handle, const char *key_name, DWORD type) {
   return db_create_key(hdb, key_handle, key_name, type);
}

INT c_db_create_link(HNDLE hdb, HNDLE key_handle, const char *link_name, const char *destination) {
   return db_create_link(hdb, key_handle, link_name, destination);
}

INT c_db_delete_key(HNDLE database_handle, HNDLE key_handle, BOOL follow_links) {
   return db_delete_key(database_handle, key_handle, follow_links);
}

INT c_db_find_key(HNDLE hdb, HNDLE hkey, const char *name, HNDLE * hsubkey) {
   return db_find_key(hdb, hkey, name, hsubkey);
}

INT c_db_find_link(HNDLE hDB, HNDLE hKey, const char *key_name, HNDLE * subhKey) {
   return db_find_link(hDB, hKey, key_name, subhKey);
}

INT c_db_get_key(HNDLE hdb, HNDLE key_handle, KEY * key) {
   return db_get_key(hdb, key_handle, key);
}

INT c_db_get_link_data(HNDLE hdb, HNDLE key_handle, void *data, INT * buf_size, DWORD type) {
   return db_get_link_data(hdb, key_handle, data, buf_size, type);
}

INT c_db_get_value(HNDLE hdb, HNDLE hKeyRoot, const char *key_name, void *data, INT * size, DWORD type, BOOL create) {
   return db_get_value(hdb, hKeyRoot, key_name, data, size, type, create);
}

INT c_db_open_record(HNDLE hdb, HNDLE hkey, void *ptr, INT rec_size, WORD access, void (*dispatcher) (INT, INT, void *), void *info) {
   return db_open_record(hdb, hkey, ptr, rec_size, access, dispatcher, info);
}

INT c_db_reorder_key(HNDLE hDB, HNDLE hKey, INT index) {
   return db_reorder_key(hDB, hKey, index);
}

INT c_db_resize_string(HNDLE hDB, HNDLE hKeyRoot, const char *key_name, int num_values, int max_string_size) {
   return db_resize_string(hDB, hKeyRoot, key_name, num_values, max_string_size);
}

INT c_db_set_num_values(HNDLE hDB, HNDLE hKey, INT num_values) {
   return db_set_num_values(hDB, hKey, num_values);
}

INT c_db_set_value(HNDLE hdb, HNDLE hKeyRoot, const char *key_name, const void *data, INT size, INT num_values, DWORD type) {
   return db_set_value(hdb, hKeyRoot, key_name, data, size, num_values, type);
}

INT c_db_set_value_index(HNDLE hDB, HNDLE hKeyRoot, const char *key_name, const void *data, INT data_size, INT index, DWORD type, BOOL truncate) {
   return db_set_value_index(hDB, hKeyRoot, key_name, data, data_size, index, type, truncate);
}

INT c_rpc_flush_event(void) {
   return rpc_flush_event();
}

INT c_rpc_send_event(INT buffer_handle, const EVENT_HEADER *event, INT buf_size, INT async_flag, INT mode) {
   return rpc_send_event(buffer_handle, event, buf_size, async_flag, mode);
}
