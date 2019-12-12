import midas.client

"""
A simple example program that connects to a midas experiment, 
reads an ODB value, then sets an ODB value.

Expected output is:

```
The experiment is currently stopped
The new value of /pyexample/eg_float is 5.670000
```
"""

if __name__ == "__main__":
    client = midas.client.MidasClient("pytest")
    
    # Read a value from the ODB. The return value is a normal python
    # type (an int in this case, but could also be a float, string, bool,
    # list or dict).
    state = client.odb_get("/Runinfo/State")
    
    if state == midas.STATE_RUNNING:
        print("The experiment is currently running")
    elif state == midas.STATE_PAUSED:
        print("The experiment is currently paused")
    elif state == midas.STATE_STOPPED:
        print("The experiment is currently stopped")
    else:
        print("The experiment is in an unexpected run state")
        
    
    # Update or create a directory in the ODB by passing a dict to `odb_set`
    client.odb_set("/pyexample", {"an_int": 1, "eg_float": 4.56})
    
    # Update a single value in the ODB
    client.odb_set("/pyexample/eg_float", 5.67)
    
    # Read the value back
    readback = client.odb_get("/pyexample/eg_float")
    
    print("The new value of /pyexample/eg_float is %f" % readback)
    
    # Delete the temporary directory we created
    client.odb_delete("/pyexample")
    
    # Disconnect from midas before we exit
    client.disconnect()