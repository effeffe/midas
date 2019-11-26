"""
This script can be used to check for memory leaks in the python midas library.
The simplest usage is to just edit the code in `hot_loop_func()` to run
whichever function is of interest. We will then execute that function thousands
of times, and see if any memory is leaked.
"""

import resource
import ctypes
import midas.client

def get_size_bytes():
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss

def summarize(sizes):
    print()
    first = None
    last = None
            
    for i, it_count in enumerate(sorted(sizes.keys())):
        print("Memory usage after %05d iterations: %10.6f MB" % (it_count, sizes[it_count]/1024/1024))
        
        if first is None and i > 1:
            # Don't use the early counts - wait for what should hopefully be a stead state
            first = sizes[it_count]
        
        last = sizes[it_count]
            
    print("")
    
    if last - first < 100:
        print("No obvious memory leak detected")
    else:
        print("*** POSSIBLE MEMORY LEAK DETECTED! (%s bytes) ***" % (last-first))
        
def hot_loop_func(client):
    arr = ctypes.POINTER(ctypes.c_char_p)()
    arr_len = ctypes.c_int()
    client.lib.c_example_string_vector(ctypes.byref(arr), ctypes.byref(arr_len))
    casted = ctypes.cast(arr, ctypes.POINTER(ctypes.c_char_p))
    py_list = [casted[i].decode("utf-8") for i in range(arr_len.value)]
    client.lib.c_free_list(arr, arr_len)
    del py_list
       
def main():
    client = midas.client.MidasClient("pytest")
    max_iterations = 20000
    sample_every = 2500
    sizes = {i: -1 for i in range(max_iterations + 1, sample_every)}
    
    for i in range(max_iterations + 1):
        hot_loop_func(client)
        client.communicate(1)
        
        if i % sample_every == 0:
            sizes[i] = get_size_bytes()
            print("At interation %s (%s bytes)" % (i, sizes[i]))
    
    summarize(sizes)
    
if __name__ == "__main__":
    main()
