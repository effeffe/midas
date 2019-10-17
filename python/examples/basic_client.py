"""
Example of a basic midas client that does not use the frontend framework.
It opens up a hotlink/callback that will be called when an ODB value
changes. 

In this case, the client exists quite quickly, but you could call
`client.communicate(10)` in a `while True` loop to run "forever" (the script
can still be safely killed by Ctrl-C or from the midas Programs webpage).

Expected output is:

```
In loop 0
In loop 1
New ODB content is {'an_int': 1, 'eg_float': 5.67}
In loop 2
In loop 3
In loop 4
```
"""

import midas.client

def my_odb_callback(client, path, odb_value):
    """
    Our example function that will be called when a value changes in the ODB.
    
    * client is the midas.client.MidasClient that registered the callback
    * path is the ODB path that was being watched
    * odb_value is the new value of path
    """
    print("New ODB content is %s" % odb_value)

if __name__ == "__main__":
    client = midas.client.MidasClient("pytest")
    
    # Update or create a directory in the ODB by passing a dict to `odb_set`
    client.odb_set("/pyexample", {"an_int": 1, "eg_float": 4.56})
    
    # Setup a callback function. Note the paramters of `my_odb_callback`
    # match what is specified in the `odb_watch` documentation.
    client.odb_watch("/pyexample", my_odb_callback)
    
    # For many clients, you would do "while True" here.
    for i in range(5):
        print("In loop %i" % i)
        if i == 1:
            # We set a new value in the ODB
            client.odb_set("/pyexample/eg_float", 5.67)
        
        # If the ODB value has changed, our callback function will
        # be called as part of `client.communicate()`. If the ODB
        # value hasn't changed, this just acts like a 10ms sleep.
        client.communicate(10)
        
    # We must stop watching the ODB before we can delete the entry we created.
    client.odb_stop_watching("/pyexample")
    
    # Now we can cleanup the directory we made.
    client.odb_delete("/pyexample")
    
    # Disconnect from midas before we exit.
    client.disconnect()