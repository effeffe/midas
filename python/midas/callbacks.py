"""
This file contains tools to help set up python functions that will be called
when certain things happen (e.g. when a value changes in the ODB, or when a run
starts or stops.

They are used internally by functions in `midas.client.MidasClient` that
provide a more user-friendly interface (e.g. `odb_watch()`)
"""

import ctypes
import midas

# List of callback functions we've instantiated for watching ODB directories
hotlink_callbacks = []

# List of callback functions we've instantated for watching run transitions
transition_callbacks = []

# List of callback functions we've instantated for deferring a run transition
deferred_transition_callbacks = []

# List of callback functions we've instantated for RPC callbacks
rpc_callbacks = []

# Define the C style of the callback function we must pass to cm_open_record
# return void; args int (hDB) / int (hKey) / void* (info; unused).
HOTLINK_FUNC_TYPE = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_int, ctypes.c_void_p)

# Define the C style of the callback function we must pass to cm_register_transition
# return int (status); args int (run number) / char* (error message)
TRANSITION_FUNC_TYPE = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_char))

# Define the C style of the callback function we must pass to cm_register_deferred_transition
# return BOOL (start transition now?); args int (run number) / BOOL (first time being called?)
DEFERRED_TRANSITION_FUNC_TYPE = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32)

# Define the C style of the callback function we must pass to cm_register_function
# return int (status); args int (index) / void** (params)
RPC_CALLBACK_FUNC_TYPE = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_void_p))

def exception_message(e):
    """
    Get a printable version of an Exception.
    
    Args:
        e (Exception)
    Returns:
        str
    """
    if hasattr(e, 'message'):
        ret_str = e.message
    else:
        ret_str = str(e)
        
    if ret_str is None or len(ret_str) == 0:
        ret_str = type(e).__name__
        
    return ret_str


def make_hotlink_callback(path, callback, client):
    """
    Create a callback function that can be passed to db_open_record from the
    midas C library.
    
    Args:
        * path (str) - The ODB path that will be watched.
        * callback (function) - See below.
        * client (midas.client.MidasClient) - the client that's creating this.
        
    Returns:
        * `HOTLINK_FUNC_TYPE`
        
    Python function (`callback`) details:
        * Arguments it should accept:
            * client (midas.client.MidasClient)
            * path (str) - The ODB path being watched
            * odb_value (float/int/dict etc) - The new ODB value
        * Value it should return:
            * Anything or nothing, we don't do anything with it 
    """
    
    # We create a closure that tracks the path being watched and the
    # client that created us. This means the python callback users 
    # provide don't have to worry about hDB/hKey etc, and can just
    # be told the new value etc.
    def _wrapper(hDB, hKey, info):
        odb_value = client.odb_get(path, recurse_dir=True)
        try:
            callback(client, path, odb_value)
        except Exception as e:
            client.msg("Exception raised during callback on %s: %s" % (path, exception_message(e)), True)
        
    cb = HOTLINK_FUNC_TYPE(_wrapper)
    hotlink_callbacks.append(cb)
    return cb
    

def make_transition_callback(callback, client):
    """
    Create a callback function that can be passed to cm_register_transition 
    from the midas C library.
    
    Args:
        * callback (function) - See below.
        * client (midas.client.MidasClient) - the client that's creating this.
        
    Returns:
        * `TRANSITION_FUNC_TYPE`
        
    Python function (`callback`) details:
        * Arguments it should accept:
            * client (midas.client.MidasClient)
            * run_number (int)
        * Value it should return:
            * None, int or 2-tuple of (int, str) - None or 1 indicate
                the transition was successful. If the transition was
                unsuccessful, the string can be used to indicate why
                (max length 255). Exceptions are caught and result in
                a status code of 605, with the string taken from the
                exception message.
    """
    
    # We create a closure that tracks which python callback function
    # to call, and also means users don't have to worry about filling
    # error_msg themselves.
    def _wrapper(run_number, error_msg):
        ret_int = None
        ret_str = None
        
        try:
            retval = callback(client, run_number)
        except Exception as e:
            ret_str = exception_message(e)
            ret_int = midas.status_codes["FE_ERR_DRIVER"]
            retval = (ret_int, ret_str)
        
        if isinstance(retval, tuple) or isinstance(retval, list):
            ret_int = retval[0]
            if len(retval) > 1:
                ret_str = retval[1]
                
        if ret_int is None:
            ret_int = midas.status_codes["SUCCESS"]
            
        if not isinstance(ret_int, int):
            raise ValueError("Transition callback didn't return an allowed status (%s)" % ret_int)
            
        # Fill ret_str into error_msg (max 256 chars)
        if isinstance(ret_str, str) and len(ret_str) and error_msg:
            len_msg = min(len(ret_str), 255)
            for i in range(len_msg):
                error_msg[i] = ret_str[i].encode('ascii')
            error_msg[len_msg] = b'\x00'
            
        return ret_int 

    cb = TRANSITION_FUNC_TYPE(_wrapper)
    transition_callbacks.append(cb)
    return cb


def make_deferred_transition_callback(callback, client):
    """
    Create a callback function that can be passed to cm_register_deferred_transition 
    from the midas C library.
    
    Args:
        * callback (function) - See below.
        * client (midas.client.MidasClient) - the client that's creating this.
        
    Returns:
        * `DEFERRED_TRANSITION_FUNC_TYPE`
        
    Python function (`callback`) details:
        * Arguments it should accept:
            * client (midas.client.MidasClient)
            * run_number (int)
        * Value it should return:
            * True if the transition can proceed, False if the transition
              should wait.
    """
    
    # We create a closure that tracks which python callback function
    # to call, and converts the python True/False into the BOOL that
    # midas expects. The dummy param is for midas' "first" parameter, which
    # doesn't seem to be very well documented.
    def _wrapper(run_number, dummy):
        try:
            retval = callback(client, run_number)
        except Exception as e:
            client.msg("Exception raised during deferred transition callback: %s" % exception_message(e), True)
            retval = True

        return retval

    cb = DEFERRED_TRANSITION_FUNC_TYPE(_wrapper)
    deferred_transition_callbacks.append(cb)
    return cb


def make_rpc_callback(callback, client):
    """
    Create a callback function that can be passed to cm_register_function from the midas
    C library.

    Args:
        * callback (function) - See below.
        * client (midas.client.MidasClient) - the client that's creating this.
        
    Returns:
        * `RPC_FUNC_TYPE`
        
    Python function (`callback`) details:
        * Arguments it should accept:
            * client (midas.client.MidasClient)
            * cmd (str) - The command user wants to execute
            * args (str) - Other arguments the user supplied
            * max_len (int) - The maximum string length the user accepts in the return value
        * Value it should return:
            * A tuple of (int, str) or just an int. The integer should be a status code
                from midas.status_codes. The string, if present, should be any text that
                should be returned to the caller. The maximum string length that will be
                returned to the user is given by the `max_len` parameter.
    """
    
    # As with the other make_xxx_callback functions, we create a closure.
    def _wrapper(index, params):
        cmd = ctypes.cast(params[0], ctypes.c_char_p).value.decode("utf-8")
        args = ctypes.cast(params[1], ctypes.c_char_p).value.decode("utf-8")
        buf_p = ctypes.cast(params[2], ctypes.POINTER(ctypes.c_char))
        max_len = ctypes.cast(params[3], ctypes.POINTER(ctypes.c_int)).contents.value
        
        if max_len <= 0:
            retval = (midas.status_codes["FE_ERR_DRIVER"], "max_len must be > 0")
        else:
            try:
                retval = callback(client, cmd, args, max_len)
            except Exception as e:
                retval = (midas.status_codes["FE_ERR_DRIVER"], exception_message(e))

        if isinstance(retval, int):
            ret_str = ""
        elif retval is None or len(retval) != 2:
            ret_int = midas.status_codes["FE_ERR_DRIVER"]
            ret_str = "Invalid return value from callback functions"
        else:
            (ret_int, ret_str) = retval
            
        # Write return value to buffer midas created for us.
        addr = ctypes.addressof(buf_p.contents)
        dest_chars = (ctypes.c_char * max_len).from_address(addr)
        write_size = min(len(ret_str), max_len - 1)
        dest_chars[:write_size] = bytes(ret_str[:write_size], 'utf-8')
        dest_chars[write_size] = b'\0'
        
        return ret_int

    cb = RPC_CALLBACK_FUNC_TYPE(_wrapper)
    rpc_callbacks.append(cb)
    return cb
