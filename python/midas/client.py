"""
This module contains user-friendly pythonic wrappers around the midas C 
library.
"""

import midas
import midas.structs
import midas.callbacks
import midas.event
import ctypes
import os
import os.path
import sys
import datetime
import inspect
import collections
import logging

ver = sys.version_info
if ver <= (3, 0):
    raise EnvironmentError("Sorry, we only support python 3, but you're running version %d.%d" % (ver[0], ver[1]))

_midas_connected = False # Whether we've connected to a midas experiment
_midas_lib_loaded = None # The `midas.MidasLib` we have loaded

logger = logging.getLogger("midas")

class MidasClient:
    """
    This is the main class that contains pythonic wrappers around the midas
    C library. Most users will interact with this class, the definitions in
    the main midas.__init__ file, and optionally the Frontend class defined
    in midas.frontend.
    
    # Normal access
    
    Most common functions have wrappers defined in this class. For example,
    the `odb_set` function allows you to pass a normal python values as
    arguments:
    
    ```
    import midas.client
    
    client = midas.client.MidasClient("NormalExample")
    client.odb_set("/my/odb_path", 4.56)
    ```
    
    # Low-level access
    
    If you want to use a function that doesn't have a python wrapper, you will
    need to:
    
    * Define an appropriate C-compatible function in include/midas_c_compat.h
        and src/midas_c_compat.cxx
    * Recompile using cmake (cd build && make && make install)
    * Call the function from python like client.lib.c_my_func(). You will need 
        to convert any arguments to ctypes values (and if the C function 
        doesn't return an integer, then there is even more work to do - see the
        MidasLib class in the main `midas` module).
    
    # Members
    
    * name (str) - The name of this client.
    * lib (`midas.MidasLib`) - The ctypes wrapper of the midas library.
    * hDB (`ctypes.c_int`) - HNDLE to the experiment's ODB
    * hClient (`ctypes.c_int`) - HNDLE to this client's entry in the ODB
    * event_buffers (dict of {int: `ctypes.c_char_p`}) - Character buffers that
        have been opened, keyed by buffer handle.
    """
    def __init__(self, client_name, host_name=None, expt_name=None):
        """
        Load the midas library, and connect to the chosen experiment.
        
        Args:
            * client_name (str) - The name this program will be referred to
                within the midas system (e.g. on the "Programs" webpage).
            * host_name (str) - Which server to connect to. If None, it will
                be determined from the MIDAS_SERVER_HOST environment variable
                (and will default to this machine if MIDAS_SERVER_HOST is not
                set).
            *  expt_name (str) - Which experiment to connect to on the server.
                If None, it will be determined automatically from the 
                MIDAS_EXPT_NAME environment variable.
        """
        global _midas_connected, _midas_lib_loaded
        
        if _midas_connected:
            raise RuntimeError("You can only have one client connected to midas per process! Disconnect any previous clients you created before creating a new one.")
        
        if not _midas_lib_loaded:
            midas_sys = os.getenv("MIDASSYS", None)
            
            if midas_sys is None:
                raise EnvironmentError("Set environment variable $MIDASSYS to path of midas")
            
            lib_files = ["libmidas-c-compat.so", "libmidas-c-compat.dylib"]
            lib_dir = os.path.join(midas_sys, "lib")
            lib_path = None
            
            for lib_file in lib_files:
                test = os.path.join(lib_dir, lib_file)
                
                if os.path.exists(test):
                    lib_path = test
                    break
                
            if lib_path is None:
                raise EnvironmentError("Couldn't find libraries %s in %s - make sure midas was compiled using cmake" % (lib_files, lib_path))
            
            # Automatically checks return codes and raises exceptions.
            _midas_lib_loaded = midas.MidasLib(lib_path)
            
        self.name = client_name    
        self.lib = _midas_lib_loaded 
        
        c_host_name = ctypes.create_string_buffer(32)
        c_expt_name = ctypes.create_string_buffer(32)
        c_client_name = ctypes.create_string_buffer(client_name.encode('ascii'), 32)
        
        if host_name is None or expt_name is None:
            self.lib.c_cm_get_environment(c_host_name, ctypes.sizeof(c_host_name), c_expt_name, ctypes.sizeof(c_expt_name))
            
        if host_name is not None:
            c_host_name.value = host_name.encode('ascii')
            
        if expt_name is not None:
            c_expt_name.value = expt_name.encode('ascii')
        
        # We automatically connect to this experiment's ODB
        self.lib.c_cm_connect_experiment(c_host_name, c_expt_name, c_client_name, None)
        _midas_connected = True
                
        self.hDB = ctypes.c_int()
        self.hClient = ctypes.c_int()

        self.lib.c_cm_get_experiment_database(ctypes.byref(self.hDB), ctypes.byref(self.hClient))
        self.lib.c_cm_start_watchdog_thread()
        
        self.event_buffers = {}
        
    def disconnect(self):
        """
        Nicely disconnect from midas. If this function isn't called at the end
        of a program, midas will complain in the message log and it might take
        a while for our disappearance to be noted.
        
        This function will be called automatically from the destructor of this
        class, HOWEVER some implementations of python do not guarantee that
        __del__ will be called at system exit.
        
        To be sure of a trouble-free disconnection from midas, you should call
        this function at the end of your program. 
        """
        global _midas_connected
        
        if _midas_connected and hasattr(self, 'lib') and self.lib:
            self.lib.c_cm_disconnect_experiment()
            _midas_connected = False
            
    def __del__(self):
        """
        Python doesn't guarantee that __del__ will be called at the end of a
        program, but if we do get called (or the user explicitly deletes us)
        we should disconnect from midas.
        """
        self.disconnect()
    
    def msg(self, message, is_error=False):
        """
        Send a message into the midas message log.
        
        These messages are stored in a text file, and are visible on the the
        "messages" webpage.
        
        Args:
            * message (str) - The actual message.
            * is_error (bool) - Whether this message is informational or an 
                error message. Error messages are highlighted in red on the
                message page.
        """
        
        # Find out where this function was called from. We go up
        # 1 frame in the stack to get to the lowest-level user
        # function that called us.
        # 0. fn_A()
        # 1. fn_B() # <--- Get this function
        # 2. midas.client.msg()
        caller = inspect.getframeinfo(inspect.stack()[1][0])
        filename = ctypes.create_string_buffer(bytes(caller.filename, "utf-8"))
        line = ctypes.c_int(caller.lineno)
        routine = ctypes.create_string_buffer(bytes(caller.function, "utf-8"))
        c_msg = ctypes.create_string_buffer(bytes(message, "utf-8"))
        msg_type = ctypes.c_int(midas.MT_ERROR if is_error else midas.MT_INFO)
    
        self.lib.c_cm_msg(msg_type, filename, line, routine, c_msg)

    def communicate(self, time_ms):
        """
        If you have a long-running script, this function should be called
        repeatedly in a loop. It serves several purposes:
        * letting midas know the client is still working
        * allowing users to stop your program from the "Programs" webpage
        * telling your client about any ODB changes that it opened a hotlink for
        * letting your client know about run transitions
        * and more...
        
        Args:
            * time_ms (int) - How many millisecs to wait before returning
                from this function.
        """
        c_time_ms = ctypes.c_int()
        start = datetime.datetime.now()
        
        # cm_yield may return early if some events were available, so we run
        # in a loop until the timeout the user specified has expired.
        while time_ms > 0:
            c_time_ms.value = time_ms
            
            try:
                self.lib.c_cm_yield(c_time_ms)
            except midas.MidasError as e:
                if e.code == midas.status_codes["RPC_SHUTDOWN"]:
                    print("Midas shutdown")
                    self.disconnect()
                    exit(0)
                else:
                    raise
                
            now = datetime.datetime.now()
            elapsed_ms = int((now - start).total_seconds() * 1000) + 1 # Avoid slight rounding errors
            time_ms -= elapsed_ms
    
    def odb_exists(self, path):
        """
        Whether the given path exists in the ODB.
        
        Args:
            * path (str) - e.g. "/Runinfo/State"
            
        Returns:
            bool
        """
        c_path = ctypes.create_string_buffer(bytes(path, "utf-8"))
        hKey = ctypes.c_int()
        
        try:
            self.lib.c_db_find_key(self.hDB, 0, c_path, ctypes.byref(hKey))
        except KeyError:
            return False
        
        return True
    
    def odb_delete(self, path, follow_links=False):
        """
        Delete the given location in the ODB (and all of its children if the
        specified path points to a directory).
        
        Args:
            * path (str) - e.g. "/Runinfo/State"
            * follow_links (bool) - Whether to follow symlinks in the ODB or 
                not. If False, we'll delete the link itself, rather than the 
                target of the link.
        """
        c_path = ctypes.create_string_buffer(bytes(path, "utf-8"))
        c_follow = ctypes.c_uint32(follow_links)
        hKey = ctypes.c_int()
        
        if follow_links:
            self.lib.c_db_find_key(self.hDB, 0, c_path, ctypes.byref(hKey))
        else:
            self.lib.c_db_find_link(self.hDB, 0, c_path, ctypes.byref(hKey))
            
        self.lib.c_db_delete_key(self.hDB, hKey, c_follow)
        
    def odb_get(self, path, recurse_dir=True, include_key_metadata=False):
        """
        Get the content of an ODB entry, with automatic conversion to appropriate
        python types. You can request either a single ODB key or an entire
        directory (in which case a dict will be returned).
        
        A note on dicts - midas directories have an associated order to them
        (you can rearrange the order of entries in a midas directory by calling
        `db_reorder_key` in the C library). Until python3.7, basic python dicts
        did not have a well-defined order. Therefore we return a 
        `collections.OrderedDict` rather than a basic dict, so the order in 
        which you iterate over the keys will match the order in which you 
        see them on the midas status page, for example.
        
        Args:
            * path (str) - e.g. "/Runinfo". You may specify a single array index
                if desired (e.g. "/Path/To/My/Array[1]").
            * recurse_dir (bool) - If `path` points to a directory, whether to
                recruse down and get all entries beneath that directory, or just
                the entries immediately below the specified path. `recurse_dir`
                being False acts more like an `ls` of the directory, while it
                being True acts more like a full dump of the content.
            * include_key_metadata (bool) - Whether to include extra information
                in the return value containing metadata about each ODB key. 
                Examples metadata include when the ODB key was last written, the
                data type (e.g. TID_INT) etc. The metadata is stored in extra
                dict entries named as "<name>/key" (e.g. if the actual ODB 
                content is in "State", the metadata is in "State/key").
                
        Returns:
            * If `path` points to a directoy, a dict. Dicts are keyed by the
                ODB key name. If `recurse_dir` is False and one of the entries
                in `path` is also a directory, it's value will be an empty dict.
            * If `path` points to a single ODB key, an int/float/bool etc (or
                a list of them). If `include_key_metadata` is True the return 
                value will be a dict, with two entries (one for the value and
                one for the metadata).
                
        Raises:
            * KeyError if `path` doesn't exist in the ODB
            * midas.MidasError for other midas-related issues
        """
        (array_single, dummy, dummy2) = self._odb_path_is_for_single_array_element(path)
        c_path = ctypes.create_string_buffer(bytes(path, "utf-8"))
        
        hKey = ctypes.c_int()
        self.lib.c_db_find_key(self.hDB, 0, c_path, ctypes.byref(hKey))
        
        key_metadata = self._odb_get_key_from_hkey(hKey)
        
        if key_metadata.type == midas.TID_KEY:
            # If we are getting the content of a directory, we can use the simple
            # db_copy_json_xxx functions.
            buf = ctypes.c_char_p()
            bufsize = ctypes.c_int()
            bufend = ctypes.c_int()

            if recurse_dir:
                self.lib.c_db_copy_json_save(self.hDB, hKey, ctypes.byref(buf), ctypes.byref(bufsize), ctypes.byref(bufend))
            else:
                self.lib.c_db_copy_json_ls(self.hDB, hKey, ctypes.byref(buf), ctypes.byref(bufsize), ctypes.byref(bufend))
        
            if bufsize.value > 0:
                retval = midas.safe_to_json(buf.value, use_ordered_dict=True)
            else:
                retval = {}
        
            retval = self._convert_dwords_from_odb(retval)
        
            if not include_key_metadata:
                retval = self._prune_metadata_from_odb(retval)
                
            # Free the memory allocated by midas
            self.lib.c_free(buf)
        else:
            # The db_copy_json_xxx functions only support getting the content of a
            # directory, not a single value. We therefore have to use db_get_value
            # when getting single ODB entries.
            c_path = ctypes.create_string_buffer(bytes(path, "utf-8"))
            array_len = key_metadata.num_values
            
            if array_single:
                array_len = 1
            
            if key_metadata.type == midas.TID_STRING:
                str_len = key_metadata.item_size
            else:
                str_len = None
            
            # Get a ctypes object for midas to put the data into.
            c_rdb = self._midas_type_to_ctype(key_metadata.type, array_len, str_len=str_len)
            c_size = ctypes.c_int(ctypes.sizeof(c_rdb))
            
            self.lib.c_db_get_value(self.hDB, 0, c_path, ctypes.byref(c_rdb), ctypes.byref(c_size), key_metadata.type, 0)
            
            if key_metadata.type == midas.TID_STRING:
                if array_len > 1:
                    # Arrays of string are the nastiest bit, as we only given one
                    # "mega-string" containing all the entries which we now need to
                    # split up.
                    # This line splits the string into sections of correct length; 
                    # then for each line it removes everything after the first null 
                    # byte, then decode into a python string. We need to do it this 
                    # way (rather than simply splitting c_rdb.value) as otherwise 
                    # we'd just get null for everything after the end of the first 
                    # entry in the array.
                    retval = [c_rdb[i * str_len:(i+1) * str_len].split(b'\0',1)[0].decode("utf-8") for i in range(array_len)]
                else:
                    retval = c_rdb.value.decode("utf-8")
            else:
                if array_len > 1:
                    # Convert to a regular list.
                    retval = [x for x in c_rdb]
                else:
                    retval = c_rdb.value
                    
            if include_key_metadata:
                # Normally we just return a single value, but if the user
                # wants the metadata as well, we need to mockup a dict
                # containing the relevant information.
                name_data = "%s" % key_metadata.name.decode("utf-8")
                name_meta = "%s/key" % name_data
                retval = {name_data: retval}
                retval[name_meta] = {"type": key_metadata.type,
                                     "access_mode": key_metadata.access_mode,
                                     "last_written": key_metadata.last_written}
                
                if key_metadata.type == midas.TID_STRING:
                    retval[name_meta]["item_size"] = key_metadata.item_size
                if key_metadata.num_values > 1:
                    retval[name_meta]["num_values"] = key_metadata.num_values
            
        return retval
    
    def odb_set(self, path, contents, create_if_needed=True, 
                    remove_unspecified_keys=True, resize_arrays=True, 
                    lengthen_strings=True, explicit_new_midas_type=None, 
                    update_structure_only=False):
        """
        Set the value of an ODB key, or an entire directory. You may pass in
        normal python values, lists and dicts and they will be converted to
        appropriate midas ODB key types (e.g. int becomes midas.TID_INT, bool
        becomes midas.TID_BOOL).
        
        Sensible defaults have been chosen for converting python types to the C 
        types used internally in the midas ODB. However if you want more control 
        over the ODB type, you may use the types defined in the ctypes library.
        For example, regular python integers become a midas.TID_INT, but you can
        use a `ctypes.c_uint32` to get a midas.TID_DWORD.
        
        If you are setting the content of a directory and care about the order
        in which the entries appear in that directory, `contents` should be a
        `collections.OrderedDict` rather than a basic python dict. See the note
        in `odb_get` for more about dictionary ordering.
        
        Args:
            * path (str) - The ODB entry to set. You may specify a single array
                index if desired (e.g. "/Path/To/My/Array[1]").
            * contents (int/float/string/tuple/list/dict etc) - The new value to set
            * create_if_needed (bool) - Automatically create the ODB entry
                (and parent directories) if needed.
            * remove_unspecified_keys (bool) - If `path` points to a directory
                and `contents` is a dict, remove any ODB keys within `path` that
                aren't present in `contents`. This means that the ODB will exactly
                match the dict you passed in. You may want to set this to False
                if you want to only update a few entries within a directory, and
                want to do so with only a single call to `odb_set()`.
            * resize_arrays (bool) - Automatically resize any ODB arrays to match 
                the length of lists present in `contents`. Arrays will be both
                lengthened and shortened as needed.
            * lengthen_strings (bool) - Automatically increase the storage size
                of a TID_STRING entry in the ODB if it is not long enough to 
                store the value specified. We will include enough space for a
                final null byte.
            *  explicit_new_midas_type (one of midas.TID_xxx) - If you're 
                setting the value of a single ODB entry, you can explicitly
                specify the type to use when creating the ODB entry (if needed).
            * update_structure_only (bool) - If you want to add/remove entries
                in an ODB directory, but not change the value of any entries
                that already exist. Only makes sense if contents is a dict / 
                `collections.OrderedDict`. Think of it like db_check_record from
                the C library.
                
        Raises:
            * KeyError if `create_if_needed` is False and the ODB entry does not
                already exist.
            * TypeError if there is a problem converting `contents` to the C 
                type we must pass to the midas library (e.g. the ODB entry is
                a TID_STRING but you passed in a float).
            * ValueError if there is a non-type-related problem with `contents`
                (e.g. if `resize_arrays` is False and you provided a list that
                doesn't match the size of the existing ODB array).
            * midas.MidasError if there is some other midas issue.
        """
        
        c_path = ctypes.create_string_buffer(bytes(path, "utf-8"))
        did_create = False
        
        (array_single, array_index, c_path_no_idx) = self._odb_path_is_for_single_array_element(path)
        
        if not self.odb_exists(path):
            if create_if_needed:
                if explicit_new_midas_type:
                    # User told use what ODB type to use.
                    new_midas_type = explicit_new_midas_type
                else:
                    # Choose ODB type based on input (e.g. a dict means
                    # we'll create a TID_KEY, an in means TID_INT etc)
                    new_midas_type = self._ctype_to_midas_type(contents)
                    
                self.lib.c_db_create_key(self.hDB, 0, c_path_no_idx, new_midas_type)
                did_create = True
            else:
                raise KeyError("ODB path doesn't exist")

        hKey = ctypes.c_int()
        self.lib.c_db_find_key(self.hDB, 0, c_path_no_idx, ctypes.byref(hKey))
        key_metadata = self._odb_get_key_from_hkey(hKey)
        
        if key_metadata.type == midas.TID_STRING and (lengthen_strings or did_create):
            self._odb_lengthen_string_to_fit_content(key_metadata, c_path_no_idx, contents)
            key_metadata = self._odb_get_key_from_hkey(hKey)
            
        if key_metadata.type != midas.TID_KEY and (resize_arrays or did_create):
            self._odb_resize_array_to_match_content(key_metadata, hKey, contents, array_index)
            key_metadata = self._odb_get_key_from_hkey(hKey)
        
        if key_metadata.type == midas.TID_KEY:
            # This is currently inefficient and can result in many rather slow
            # calls to ctypes. It would be nicer to use db_paste_json, but that
            # doesn't allow us to have the remove_unspecified_keys=False mode.
            if not isinstance(contents, dict):
                raise TypeError("Pass a dict when setting the content of a midas directory")
            
            if not path.endswith("/"):
                path += "/"

            if remove_unspecified_keys:
                current_content = self.odb_get(path, recurse_dir=False)
                
                for k in current_content.keys():
                    if k not in contents:
                        self.odb_delete(path + k)
                    
            for k,v in contents.items():
                update = did_create or isinstance(v, dict) or not update_structure_only
                
                if update or not self.odb_exists(path + k):
                    self.odb_set(path + k, v, create_if_needed, remove_unspecified_keys, resize_arrays, lengthen_strings, update_structure_only=update_structure_only)

            if isinstance(contents, collections.OrderedDict):
                self._odb_fix_directory_order(path, hKey, contents)
        else:
            # Single value
            if key_metadata.type == midas.TID_STRING:
                str_len = key_metadata.item_size
            else:
                str_len = None

            c_value = self._midas_type_to_ctype(key_metadata.type, array_len=None, str_len=str_len, initial_value=contents)
            our_num_values = self._odb_get_content_array_len(key_metadata, contents)
            
            if array_single:
                # Single value in an existing array
                if key_metadata.type == midas.TID_STRING:
                    using_str_len = ctypes.sizeof(c_value)
                    odb_str_len = key_metadata.item_size
                    
                    if using_str_len != odb_str_len:
                        raise ValueError("ODB string length is %s, but we want to set strings of length %s" % (odb_str_len, using_str_len))

                self.lib.c_db_set_value_index(self.hDB, 0, c_path_no_idx, ctypes.byref(c_value), ctypes.sizeof(c_value), array_index, key_metadata.type, 0)
            else:
                # Single value for a single value
                if key_metadata.num_values != our_num_values:
                    raise ValueError("ODB array length is %s, but you provided %s values" % (key_metadata.num_values, our_num_values))
                
                if key_metadata.type == midas.TID_STRING:
                    using_str_len = ctypes.sizeof(c_value) / key_metadata.num_values
                    odb_str_len = key_metadata.item_size
                    
                    if using_str_len != odb_str_len:
                        raise ValueError("ODB string length is %s, but we want to set strings of length %s" % (odb_str_len, using_str_len))

                self.lib.c_db_set_value(self.hDB, 0, c_path, ctypes.byref(c_value), ctypes.sizeof(c_value), our_num_values, key_metadata.type)
    
    def odb_link(self, link_path, destination_path):
        """
        Create or edit a link within the ODB.
        
        To remove the link call `odb_delete` with the `follow_links` flag set 
        to False.
        
        Args:
            * link_path (str) - The ODB path for the link name
            * destination_path (str) - Where in the ODB the link points to
        """
        c_link_path = ctypes.create_string_buffer(bytes(link_path, "utf-8"))
        c_dest_path = ctypes.create_string_buffer(bytes(destination_path, "utf-8"))
        self.lib.c_db_create_link(self.hDB, 0, c_link_path, c_dest_path)
        
    def odb_get_link_destination(self, link_path):
        """
        Get where in the ODB a link points to.
        
        Args:
            * link_path (str) - The ODB path for the link name        
        
        Returns:
            str, the ODB path the link points to
        """
        c_dest_path = ctypes.create_string_buffer(256)
        c_size = ctypes.c_int(ctypes.sizeof(c_dest_path))
        hKey = self._odb_get_hkey(link_path, follow_link=False)
        self.lib.c_db_get_link_data(self.hDB, hKey, c_dest_path, ctypes.byref(c_size), midas.TID_LINK)
        return c_dest_path.value.decode("utf-8")
    
    def odb_last_update_time(self, path):
        """
        Get when an ODB key was last written to.
        
        Args:
            * path (str) - The ODB path
            
        Returns:
            datetime.datetime
        """
        ts = self._odb_get_key(path).last_written
        return datetime.datetime.fromtimestamp(ts)
    
    def odb_watch(self, path, callback):
        """
        Register a function that will be called when a value in the ODB changes.
        You must later call `communicate()` so that midas can inform your
        client about any ODB changes.
        
        Args:
            * path (str) - The ODB path to watch (including all its children).
            * callback (function) - See below. You can register the same 
                callback function to watch multiple ODB entries if you wish.
            
        Python function (`callback`) details:
            * Arguments it should accept:
                * client (midas.client.MidasClient)
                * path (str) - The ODB path being watched
                * odb_value (float/int/dict etc) - The new ODB value
            * Value it should return:
                * Anything or nothing, we don't do anything with it 
                
        Example (see `examples/basic_client.py` for more:
        
        ```
        import midas.client
        
        def my_odb_callback(client, path, odb_value):
            print("New ODB content at %s is %s" % (path, odb_value))

        my_client = midas.client.MidasClient("pytest")
        
        # Note we pass in a reference to the function - we don't call it!
        # (i.e. we pass `my_odb_callback` not `my_odb_callback()`).
        my_client.odb_watch("/Runinfo", my_odb_callback)
        
        while True:
            my_client.communicate(100)
        ```
        """
        logger.debug("Registering callback function for watching %s" % path)
        hKey = self._odb_get_hkey(path)
        cb = midas.callbacks.make_hotlink_callback(path, callback, self)
        self.lib.c_db_open_record(self.hDB, hKey, None, 0, 1, cb, None)
        
    def odb_stop_watching(self, path):
        """
        Stop watching any ODB entries that you previously registered through
        `odb_watch()`.
        
        Args:
            * path (str) - The ODB path you no longer want to watch.
        """
        logger.debug("De-registering callback function that was watching %s" % path)
        hKey = self._odb_get_hkey(path)
        self.lib.c_db_close_record(self.hDB, hKey)
        
    def open_event_buffer(self, buffer_name, buf_size=None, max_event_size=None):
        """
        Open a buffer that can be used to send or receive midas events.
        
        Args:
            * buffer_name (str) - The name of the buffer to open. "SYSTEM" is
                the main midas buffer, but other clients can read/write to/from
                their own buffers if desired.
            * buf_size (int) - The size of the buffer to open, in bytes. If not
                specified, defaults to 10 * max_event_size.
            * max_event_size (int) - The size of the largest event we expect
                to receive/send. Defaults to the value of the ODB setting
                "/Experiment/MAX_EVENT_SIZE".
                
        Returns:
            int, the "buffer handle" that must be passed to other buffer/event-
                related functions.
        """
        if max_event_size is None:
            max_event_size = self.odb_get("/Experiment/MAX_EVENT_SIZE")
            
        if buf_size is None:
            buf_size = max_event_size * 10
        
        c_name = ctypes.create_string_buffer(bytes(buffer_name, "utf-8"))
        buffer_size = ctypes.c_int(buf_size)
        buffer_handle = ctypes.c_int()
        
        # Midas allocates the main buffer for us...
        self.lib.c_bm_open_buffer(c_name, buffer_size, ctypes.byref(buffer_handle))
        
        # But we need to allocate space we'll extract each event into.
        # In future we might want to allow using a ring buffer as well as
        # this single-event buffer.
        self.event_buffers[buffer_handle.value] = ctypes.create_string_buffer(max_event_size + 100)
        return buffer_handle.value

    def send_event(self, buffer_handle, event):
        """
        Send an event into a midas buffer.
        
        Args:
            * buffer_handle (int) - The return value from a previous call to
                `open_event_buffer()`.
            * event (`midas.event.Event`) - The event to write.
            
        Example:
        
        ```
        import midas.client
        import midas.event
        
        client = midas.client.MidasClient("MyClientName")
        buffer_handle = client.open_event_buffer("SYSTEM")
        
        event = midas.event.Event()
        event.header.event_id = 123
        event.header.serial_number = 1
        event.body.create_bank("MYBK", midas.TID_WORD, [1,2,3,4])
        
        client.send_event(buffer_handle, event)
        ```
        """
        buf = event.pack()
        rpc_mode = 1
        
        self.lib.c_rpc_send_event(buffer_handle, buf, len(buf), midas.BM_WAIT, rpc_mode)
        self.lib.c_rpc_flush_event()
        self.lib.c_bm_flush_cache(buffer_handle, midas.BM_WAIT)
        
    def register_event_request(self, buffer_handle, event_id=-1, trigger_mask=-1, sampling_type=midas.GET_ALL):
        """
        Register to be told about events matching certain criteria in a given
        buffer.
        
        To actually get the events, you must later call `receive_event()`.
        
        See `examples/event_receiver.py` for a full example.
        
        Args:
            * buffer_handle (int) - The return value from a previous call to
                `open_event_buffer()`.
            * event_id (int) - Limit to only events where the event ID matches
                this. -1 means no filtering.
            * trigger_mask (int) - Limit to only events where the trigger mask matches
                this. -1 means no filtering.
            * sampling_type (int) - One of :
                `midas.GET_ALL` (get all events)
                `midas.GET_NONBLOCKING` (get as many as possible without blocking producer)
                `midas.GET_RECENT` (like non-blocking, but only get events < 1s old)
                
        Returns:
            int, the "request ID", which can later be used to cancel this
                event request.
        """
        request_id = ctypes.c_int()
        c_event_id = ctypes.c_short(event_id)
        c_trigger_mask = ctypes.c_short(trigger_mask)
        c_sampling_type = ctypes.c_short(sampling_type)
        
        self.lib.c_bm_request_event(buffer_handle, c_event_id, c_trigger_mask, c_sampling_type, ctypes.byref(request_id))
        
        return request_id.value
    
    def receive_event(self, buffer_handle, async_flag=True):
        """
        Receive an event from a buffer. You must previously have called
        `open_event_buffer()` to open the buffer and `register_event_request()`
        to define which events you want to be told about.
        
        See `examples/event_receiver.py` for a full example.
        
        Args:
            * buffer_handle (int) - The return value from a previous call to
                `open_event_buffer()`.
            * async_flag (bool) - If True, we'll return None if there is not
                currently an event in the buffer. If False, we'll wait until
                an event is ready and then return.
            
        Returns:
            `midas.event.Event`
        
        """
        # Buffer for unpacking event
        buf = self.event_buffers[buffer_handle]
        buf_size = ctypes.c_int(ctypes.sizeof(buf))
        c_async_flag = ctypes.c_int(async_flag)
        
        try:
            self.lib.c_bm_receive_event(buffer_handle, ctypes.byref(buf), ctypes.byref(buf_size), c_async_flag)
        except midas.MidasError as e:
            if e.code == midas.status_codes["BM_ASYNC_RETURN"]:
                return None
            else:
                raise
            
        # buf now contains the event contents
        event = midas.event.Event()
        event.unpack(buf)
        
        return event
    
    def deregister_event_request(self, buffer_handle, request_id):
        """
        Cancel an event request that had previously been opened with 
        `register_event_request()`.
        
        Args:
            * buffer_handle (int) - The return value from a previous call to
                `open_event_buffer()`.
            * request_id (int) - The return value from a previous call to
                `register_event_request()`.
        """
        self.lib.c_bm_remove_event_request(buffer_handle, request_id)        

    def start_run(self, run_num=0, async_flag=False):
        """
        Start a new run.
        
        Args:
            * run_num (int) - The new run number. If 0, the run number will
                be automatically incremented.
            * async_flag (bool) - If True, this function will return as soon
                as the transition has started; if False, it will wait until
                the transition has completed (or failed). 
                
        Raises:
            `midas.TransitionFailedError` if async_flag is False and the 
                transition fails.
        """
        self._run_transition(midas.TR_START, run_num, async_flag)
        
    def stop_run(self, async_flag=False):
        """
        Stop a run.
        
        Args:
            * async_flag (bool) - If True, this function will return as soon
                as the transition has started; if False, it will wait until
                the transition has completed (or failed). 
                
        Raises:
            `midas.TransitionFailedError` if async_flag is False and the 
                transition fails.
        """        
        self._run_transition(midas.TR_STOP, 0, async_flag)
    
    def pause_run(self, async_flag=False):
        """
        Pause a run.
        
        Args:
            * async_flag (bool) - If True, this function will return as soon
                as the transition has started; if False, it will wait until
                the transition has completed (or failed). 
                
        Raises:
            `midas.TransitionFailedError` if async_flag is False and the 
                transition fails.
        """        
        self._run_transition(midas.TR_PAUSE, 0, async_flag)
        
    def resume_run(self, async_flag=False):
        """
        Resume a paused run.
        
        Args:
            * async_flag (bool) - If True, this function will return as soon
                as the transition has started; if False, it will wait until
                the transition has completed (or failed). 
                
        Raises:
            `midas.TransitionFailedError` if async_flag is False and the 
                transition fails.
        """        
        self._run_transition(midas.TR_RESUME, 0, async_flag)
        
    def register_transition_callback(self, transition, sequence, callback):
        """
        Register a function to be called when a transition happens.
        
        Args:
            * transition (int) - One of `midas.TR_START`, `midas.TR_STOP`,
                `midas.TR_PAUSE`, `midas.TR_RESUME`, `midas.TR_STARTABORT`.
            * sequence (int) - The order in which this function will be 
                called.
            * callback (function) - See below.
            
        Python function (`callback`) details:
            * Arguments it should accept:
                * client (midas.client.MidasClient)
                * run_number (int) - The current/new run number.
            * Value it should return - choose from:
                * None or 1 - Transition was successful
                * int that isn't 1 - Transition failed
                * 2-tuple of (int, str) - Transition failed and the reason why.
        """
        cb = midas.callbacks.make_transition_callback(callback, self)
        self.lib.c_cm_register_transition(transition, cb, sequence)

    def set_transition_sequence(self, transition, sequence):
        """
        Update the order in which our transition callback functions are called.
        
        Args:
            * transition (int) - One of `midas.TR_START`, `midas.TR_STOP`,
                `midas.TR_PAUSE`, `midas.TR_RESUME`, `midas.TR_STARTABORT`.
            * sequence (int) - The order in which this function will be 
                called.
        """
        self.lib.c_cm_set_transition_sequence(transition, sequence)
    
    def deregister_transition_callback(self, transition):
        """
        Deregister functions previously registered with 
        `register_transition_callback()`, so they are no longer called.
        
        Args:
            * transition (int) - One of `midas.TR_START`, `midas.TR_STOP`,
                `midas.TR_PAUSE`, `midas.TR_RESUME`, `midas.TR_STARTABORT`.
        """
        self.lib.c_cm_deregister_transition(transition)
    
    def register_deferred_transition_callback(self, transition, callback):
        """
        Register a function that can defer a run transition. No client will 
        receive the actual transition until your callback function says that 
        it is okay.
        
        Args:
            * transition (int) - One of `midas.TR_START`, `midas.TR_STOP`,
                `midas.TR_PAUSE`, `midas.TR_RESUME`, `midas.TR_STARTABORT`.
            * callback (function) - See below. This will be called frequently
                by midas during the deferral period, and tells midas whether
                the transition can proceed or not yet.
            
        Python function (`callback`) details:
            * Arguments it should accept:
                * client (midas.client.MidasClient)
                * run_number (int) - The current/new run number
            * Value it should return:
                bool - True if the transition can proceed; False if the 
                    transition should wait.
        
        """
        cb = midas.callbacks.make_deferred_transition_callback(callback, self)
        self.lib.c_cm_register_deferred_transition(transition, cb);

    def get_midas_version(self):
        """
        Get the version of midas.
        
        Returns:
            2-tuple of (str, str) for (midas version, git revision)
        """
        ver = self.lib.c_cm_get_version()
        rev = self.lib.c_cm_get_revision()
        
        return (ver.decode("utf-8"), rev.decode("utf-8"))
    
    #
    # Most users should not need to use the functions below here, as they are
    # quite low-level and helper functions for the more user-friendly interface
    # above.
    #
    
    
    def _convert_dwords_from_odb(self, curr_place):
        """
        Unsigned ints are returned as JSON strings (like "0x0000").
        Change them to actual numbers.
        
        Args:
            * curr_place (dict or collections.OrderedDict) - ODB JSON dump
            
        Returns
            dict, with all WORD/DWORDs as actual numbers.
        """
        if not isinstance(curr_place, dict):
            return curr_place
    
        # Initialize as an ordered dict, dict or whatever
        retval = type(curr_place)()
    
        for k,v in curr_place.items():
            if k.endswith("/key"):
                retval[k] = v
                continue
            
            if isinstance(v, dict):
                retval[k] = self._prune_metadata_from_odb(v)
            else:
                meta = curr_place[k+"/key"]
                
                if meta["type"] == midas.TID_WORD or meta["type"] == midas.TID_DWORD:
                    if isinstance(v, list):
                        retval[k] = [int(x, 16) for x in v]
                    else:
                        retval[k] = int(v, 16)
                else:
                    retval[k] = v
    
        return retval
    
    def _prune_metadata_from_odb(self, curr_place):
        """
        If an ODB query has included "/key" entries, you can strip that
        metadata out with this function. We recurse down the whole ODB
        structure in necessary.
    
        Args:
    
        * curr_place (dict or collections.OrderedDict) - ODB structure
    
        Returns:
            dict, with all "XXX/key" entries removed.
        """
        if not isinstance(curr_place, dict):
            return curr_place
    
        # Initialize as an ordered dict, dict or whatever
        retval = type(curr_place)()
    
        for k,v in curr_place.items():
            if k.endswith("/key"):
                continue
            if isinstance(v, dict):
                retval[k] = self._prune_metadata_from_odb(v)
            else:
                retval[k] = v
    
        return retval
    
    def _odb_path_is_for_single_array_element(self, path):
        """
        Whether a user has specified an ODB path that references a single
        element in an array.
        
        Args:
            * path (str)
            
        Returns:
            3-tuple of (bool, int/None, ctypes string buffer) for
                Whether it's for a single element
                The array index
                The path without the array index
        """
        path_bits = path.split("/")
        array_spec = path_bits[-1].find("[")
        
        if array_spec > -1 and path_bits[-1].find("]") == len(path_bits[-1]) - 1:
            array_str = path_bits[-1][array_spec+1:-1]
            
            if array_str.find(":") > -1:
                raise KeyError("Array slices are not supported")
            
            path_bits[-1] = path_bits[-1][:array_spec]
            path_without = ctypes.create_string_buffer(bytes("/".join(path_bits), "utf-8"))
            return (True, int(array_str), path_without)
        
        path_without = ctypes.create_string_buffer(bytes(path, "utf-8"))
        
        return (False, None, path_without)
    
    def _is_list_like(self, value):
        """
        Whether a variable should be treated as a list, in terms of converting
        it to an ODB array.
        
        Args:
            * value (anything)
            
        Returns:
            bool
        """
        return isinstance(value, list) or isinstance(value, tuple) or isinstance(value, ctypes.Array)
    
    def _odb_get_content_array_len(self, key_metadata, contents):
        """
        Get the length of ODB array needed to fit the given content. This isn't
        as trivial as just calling len() as we need to consider strings as well.
        
        Args:
            * key_metadata (`midas.structs.Key`)
            * contents (anything)
            
        Returns:
            int
        """
        our_num_values = 1
        
        if self._is_list_like(contents):
            if key_metadata.type == midas.TID_STRING and isinstance(contents, ctypes.Array) and contents._type_ == ctypes.c_char:
                our_num_values = 1
            else:
                our_num_values = len(contents)
        
        return our_num_values
        
    def _odb_resize_array_to_match_content(self, key_metadata, hKey, contents, array_idx=None):
        """
        Resize an array in the ODB to be the correct length for the content 
        we're about to add to it.
        
        Args:
            * key_metadata (`midas.structs.Key`)
            * hKey (int) - ODB key handle
            * contents (anything)
            * array_idx (int) - If setting a single array element, the index
                we're going to set.
        """
        if array_idx is not None:
            # Don't shrink arrays if setting a single value
            our_num_values = max(key_metadata.num_values, array_idx)
        else:
            our_num_values = self._odb_get_content_array_len(key_metadata, contents)
        
        if our_num_values != key_metadata.num_values:
            self.lib.c_db_set_num_values(self.hDB, hKey, our_num_values)
    
    def _get_max_str_len(self, contents):
        """
        Get the size of ODB string needed to fit the given content.
        
        Args:
            * contents (string, list of string, etc)
            
        Returns:
            int
        """
        if contents is None:
            our_max_string_size = 32
        elif self._is_list_like(contents):
            if isinstance(contents, ctypes.Array) and contents._type_ == ctypes.c_char:
                # Single c string provided. Add a null byte if needed.
                our_max_string_size = len(contents)
                
                if contents[-1] != b"\x00":
                    our_max_string_size += 1
            else:
                # List of strings provided. Add a null byte.
                our_max_string_size = max(len(x) for x in contents) + 1
        else:
            # Single string provided. Add a null byte.
            our_max_string_size = len(contents) + 1
        
        if our_max_string_size > midas.MAX_STRING_LENGTH:
            our_max_string_size = midas.MAX_STRING_LENGTH
    
        return our_max_string_size
    
    def _odb_lengthen_string_to_fit_content(self, key_metadata, c_path, contents):
        """
        ODB entries for strings have a set capacity. Increase the capacity to 
        fit the content we're about to add to it.
        
        Args:
            * key_metadata (`midas.structs.Key`)
            * c_path (ctypes string buffer) - Path to our ODB entry
            * contents (str, list of str, ctypes string buffer etc)
        """
        if key_metadata.type != midas.TID_STRING:
            raise TypeError("Dont call _odb_lengthen_string_to_fit_content() on a non-string ODB key")
        
        our_max_string_size = self._get_max_str_len(contents)
        
        if our_max_string_size > key_metadata.item_size:
            self.lib.c_db_resize_string(self.hDB, 0, c_path, key_metadata.num_values, our_max_string_size)
    
    def _odb_get_type(self, path):
        """
        Get the midas type of the ODB entry at the specified path.
        
        Args:
            * path (str) - The ODB path
            
        Returns:
            int, one of midas.TID_xxx
        """
        return self._odb_get_key(path).type
 
    def _odb_get_hkey(self, path, follow_link=True):
        """
        Get a key handle for the ODB entry at the specified path.
        
        Args:
            * path (str) - The ODB path
            * follow_link (bool)
            
        Returns:
            int
        """
        c_path = ctypes.create_string_buffer(bytes(path, "utf-8"))
        hKey = ctypes.c_int()
        
        if follow_link:
            self.lib.c_db_find_key(self.hDB, 0, c_path, ctypes.byref(hKey))
        else:
            self.lib.c_db_find_link(self.hDB, 0, c_path, ctypes.byref(hKey))
        
        return hKey
        
    def _odb_get_key(self, path):
        """
        Get metadata about the ODB entry at the specified path.
        
        Args:
            * path (str) - The ODB path
            
        Returns:
            `midas.structs.Key`
        """
        hKey = self._odb_get_hkey(path)
        return self._odb_get_key_from_hkey(hKey)
    
    def _odb_get_key_from_hkey(self, hKey):
        """
        Convert a key handle to the actual key metadata.
        
        Args:
            * hKey (int) - from `_odb_get_hkey()`
            
        Returns:
            `midas.structs.Key`            
        """
        key = midas.structs.Key()
        self.lib.c_db_get_key(self.hDB, hKey, ctypes.byref(key))
        return key        
                    
    def _odb_fix_directory_order(self, path, hKey, contents):
        """
        ODB directories have a well-defined order. If the user gave an
        ordered dictionary, this function ensures that the ODB order matches
        the order the user specified.
        
        Args:
            * path (str) - The ODB path
            * hKey (int) - from `_odb_get_hkey()`
            * contents (`collections.OrderedDict`)
        """
        buf = ctypes.c_char_p()
        bufsize = ctypes.c_int()
        bufend = ctypes.c_int()

        self.lib.c_db_copy_json_ls(self.hDB, hKey, ctypes.byref(buf), ctypes.byref(bufsize), ctypes.byref(bufend))
    
        if bufsize.value <= 0:
            return
        
        odbjson = midas.safe_to_json(buf.value, True)
        current_order = [k for k in self._prune_metadata_from_odb(odbjson).keys()]
        target_order = [k for k in contents.keys()]
        
        if len(current_order) != len(target_order):
            # User only updated some of the values, and didn't specify others.
            # By definition we don't have enough information to determine the
            # correct order.
            return
        
        if not path.endswith("/"):
            path += "/"
        
        # We do this rather simplistically and just find the first 
        # ODB entry that's in the wrong place. Then set the index
        # for all the entries after that one.
        force_fix = False
        
        for i in range(len(target_order)):
            if force_fix or current_order[i] != target_order[i]:
                force_fix = True
                subkey = self._odb_get_hkey(path + target_order[i])
                self.lib.c_db_reorder_key(self.hDB, subkey, i)
        
    def _midas_type_to_ctype(self, midas_type, array_len=None, str_len=None, initial_value=None):
        """
        Get the appropriate ctype object for the given midas type, optionally
        setting the value based on a given python value.
        
        Args:
            * midas_type (int) - One of midas.TID_xxx
            * array_len (int) - Explicit array length. If None, but initial_value
                is not None, it's taken from the size needed for initial_value.
                If array_len and initial_value are None, it's 1.
            * str_len (int) - Explicit string length if creating a ctypes string 
                buffer for a midas.TID_STRING. If None, but initial_value
                is not None, it's taken from the size needed for initial_value.
                If str_len and initial_value are None, it's 32.
            * initial_value (anything)
            
        Returns:
            ctypes.c_int8, ctypes.c_uint32 etc....
        """
        if array_len is None:
            if self._is_list_like(initial_value): 
                array_len = len(initial_value)
            else:
                array_len = 1

        if midas_type == midas.TID_STRING:
            if str_len is None:
                str_len = self._get_max_str_len(initial_value)
                
            total_str_len = str_len * array_len
            total_initial_value = None

            if initial_value is None: 
                total_initial_value = b""
            else:
                if isinstance(initial_value, ctypes.Array):
                    total_initial_value = initial_value.value
                    # We previously assumed this was a list, but it's really
                    # just one string that was passed in as an array fo chars.
                    # Update the total string length to reflect that.
                    total_str_len = str_len
                elif self._is_list_like(initial_value):
                    total_initial_value = b""
                    
                    for this_val in initial_value:
                        add_val = this_val[:str_len]
                        if isinstance(add_val, bytes):
                            total_initial_value += add_val
                        else:
                            total_initial_value += bytes(add_val, "utf-8")
                        total_initial_value += bytes(str_len - len(add_val)) # Pad with null bytes
                elif isinstance(initial_value, bytes):
                    total_initial_value = initial_value[:str_len]
                else:
                    total_initial_value = bytes(initial_value[:str_len], "utf-8")
            
            if total_initial_value is not None:
                retval = ctypes.create_string_buffer(total_initial_value, total_str_len)
                
            return retval
        
        
        retval = None
        rettype = None
        casttype = None

        if midas_type == midas.TID_BYTE or midas_type == midas.TID_CHAR:
            rettype = ctypes.c_ubyte
            casttype = int
        elif midas_type == midas.TID_SBYTE:
            rettype = ctypes.c_byte
            casttype = int
        elif midas_type == midas.TID_WORD:
            rettype = ctypes.c_ushort
            casttype = int
        elif midas_type == midas.TID_SHORT:
            rettype = ctypes.c_short
            casttype = int
        elif midas_type == midas.TID_DWORD or midas_type == midas.TID_BITFIELD:
            rettype = ctypes.c_uint
            casttype = int
        elif midas_type == midas.TID_INT:
            rettype = ctypes.c_int
            casttype = int
        elif midas_type == midas.TID_BOOL:
            rettype = ctypes.c_uint
            casttype = int
        elif midas_type == midas.TID_FLOAT:
            rettype = ctypes.c_float
            casttype = float
        elif midas_type == midas.TID_DOUBLE:
            rettype = ctypes.c_double
            casttype = float
        else:
            raise NotImplementedError("Requested midas type (%s) not handled yet" % midas_type)
                
        if array_len is None or array_len == 1:
            if isinstance(initial_value, ctypes._SimpleCData):
                if isinstance(initial_value, rettype):
                    retval = initial_value
                else:
                    if midas_type == midas.TID_BOOL and isinstance(initial_value, ctypes.c_bool):
                        # Bools are special - users can provide a c_bool but
                        # we actually need to give a c_uint32 to midas...
                        retval = rettype(int(initial_value.value))
                    else:
                        raise TypeError("Expected a %s but you provided a %s" % (rettype, type(initial_value)))
            else:
                retval = rettype()
                
                if initial_value is not None:
                    retval.value = casttype(initial_value)
        else:
            rettype_arr = rettype * array_len
            
            if initial_value is None:
                retval = rettype_arr()
            elif isinstance(initial_value, ctypes.Array):
                if isinstance(initial_value, rettype_arr):
                    # Already provided a c_int * 4, for example.
                    retval = initial_value
                else:
                    if midas_type == midas.TID_BOOL and isinstance(initial_value[0], ctypes.c_bool) or initial_value[0] is True or initial_value[0] is False:
                        pyint = [int(x) for x in initial_value]
                        retval = rettype_arr(*pyint)
                    else:
                        raise TypeError("Expected a %s but you provided a %s" % (rettype_arr, type(initial_value)))
            elif isinstance(initial_value[0], ctypes._SimpleCData):
                for i, v in enumerate(initial_value):
                    if not isinstance(v, rettype):
                        if midas_type == midas.TID_BOOL and isinstance(v, ctypes.c_bool):
                            initial_value[i] = True if v else False
                        else:
                            raise TypeError("Expected a %s but you provided a %s" % (rettype, type(v)))
                retval = rettype_arr(*initial_value)
            else:
                castlist = [casttype(x) for x in initial_value]
                retval = rettype_arr(*castlist)
            
        return retval
    
    def _ctype_to_midas_type(self, value):
        """
        Convert from a ctypes object to the appropriate midas.TID_xxx type.
        
        Args:
            * value (anything)
            
        Returns:
            int, one of midas.TID_xxx
        """
        if isinstance(value, ctypes.Array):
            value = value._type_()
            
            if isinstance(value, ctypes.c_char):
                return midas.TID_STRING
            
        if isinstance(value, list) or isinstance(value, tuple):
            if len(value) == 0:
                raise ValueError("Can't determine type of 0-length array")
            value = value[0]
            
        if value is True or value is False or isinstance(value, ctypes.c_bool):
            # Note the order here is important isinstance(True, int) also returns True!
            return midas.TID_BOOL
        if isinstance(value, int) or isinstance(value, ctypes.c_int):
            return midas.TID_INT
        if isinstance(value, float) or isinstance(value, ctypes.c_double):
            return midas.TID_DOUBLE
        if isinstance(value, ctypes.c_float):
            return midas.TID_FLOAT
        if isinstance(value, str):
            return midas.TID_STRING
        if isinstance(value, dict):
            return midas.TID_KEY
        if isinstance(value, ctypes.c_ubyte):
            return midas.TID_BYTE
        if isinstance(value, ctypes.c_byte):
            return midas.TID_SBYTE
        if isinstance(value, ctypes.c_ushort):
            return midas.TID_WORD
        if isinstance(value, ctypes.c_short):
            return midas.TID_SHORT
        if isinstance(value, ctypes.c_uint):
            return midas.TID_DWORD
        if isinstance(value, dict):
            return midas.TID_KEY
    
        raise TypeError("Couldn't find an appropriate midas type for value of type %s" % type(value))
    
    def _run_transition(self, trans, run_num, async_flag):
        c_trans = ctypes.c_int(trans)
        c_run_num = ctypes.c_int(run_num)
        c_str = ctypes.create_string_buffer(256)
        c_str_len = ctypes.c_int(ctypes.sizeof(c_str))
        c_async = ctypes.c_int(midas.TR_ASYNC if async_flag else midas.TR_SYNC)
        c_debug = ctypes.c_int(0)
        retval = self.lib.c_cm_transition(c_trans, c_run_num, c_str, c_str_len, c_async, c_debug)
        
        if retval != midas.status_codes["SUCCESS"]:
            py_str = c_str.value.decode("utf-8")
            
            if retval == 110:
                raise midas.TransitionDeferredError(retval, py_str)
            else:
                raise midas.TransitionFailedError(retval, py_str)
        
        return retval