"""
Example of a midas client that registers an RPC handler.
This allows other midas clients (including the mhttpd webserver)
to call functions within this program.

This could also be done in a python frontend structure, but
here we just use a very basic client setup.


Example **javascript** code for calling the RPC function is below.
You should also include the midas-provided resources midas.js, mhttpd.js 
and controls.js.

```
function alert_rpc_error(status, reply) {
  // Give the user a nicer message if the error code is 103 (meaning that
  // the requested client wasn't found). dlgAlert comes from controls.js.
  if (status == 103) {
    dlgAlert("The pytest client must be running for this functionality to work."); 
  } else {
    dlgAlert("Failed to perform action!<div style='text-align:left'><br>Status code: " + status + "<br>Message: " + reply + "</div>"); 
  }  
}

function parse_rpc_response(rpc_result) {
  // Convert an RPC result into a status code and message string.
  // In the python code we specify a flag that means the main
  // rpc_result.status is always 1 if the python code ran, and the
  // "real" status code is embedded in the reply string. 
  //
  // See the notes in midas.client.MidasClient.register_jrpc_callback()
  // for the rationale.
  let status = rpc_result.status;
  let reply = "";
  
  if (status == 1) {
    // Only get a reply from mjsonrpc if status is 1
    let parsed = JSON.parse(rpc_result.reply);
    status = parsed["code"];
    reply = parsed["msg"];
  }
  
  return [status, reply];
}

function call_my_rpc() {
  // When you call call_my_rpc(), you should see a popup
  // saying "The sum is 10". 
  // This could be triggered by a button, a timer or anything
  // else.
  let params = Object()
  
  // The client we want to talk to. Note that it matches the name
  // in the python code when we call the MidasClient constructor.
  params.client_name = "pytest";
  
  // The command we want to run. The python code only has one
  // function that gets called, but the command is provided as an
  // argument to it (see rpc_handler() function).
  params.cmd = "compute_sum";
  
  // Arguments for what we want to sum. Must be a string.
  let jargs = {"int_list": [1,2,3,4]};
  params.args = JSON.stringify(jargs);
  
  mjsonrpc_call("jrpc", params).then(function(rpc) {
    let [status, reply] = parse_rpc_response(rpc.result);
    if (status == 1) {
      let jreply = JSON.parse(reply);
      alert("The sum is " + jreply["sum"]);
    } else {
      alert_rpc_error(status, reply);
    }
  }).catch(function(error) {
    mjsonrpc_error_alert(error);
  });
}
```
"""

import json
import midas
import midas.client

def rpc_handler(client, cmd, args, max_len):
    """
    This is the function that will be called when something/someone
    triggers the "JRPC" for this client (e.g. by using the javascript
    code above).
    
    Arguments:
        
    * client (midas.client.MidasClient)
    * cmd (str) - The command user wants to execute
    * args (str) - Other arguments the user supplied
    * max_len (int) - The maximum string length the user accepts in the return value

    Returns:
        
    2-tuple of (int, str) for status code, message.
    """
    ret_int = midas.status_codes["SUCCESS"]
    ret_str = ""
    
    if cmd == "compute_sum":
        jargs = json.loads(args)
        ints = jargs.get("int_list")
        sum_ints = sum(ints)
        
        ret_int = midas.status_codes["SUCCESS"]
        ret_str = json.dumps({"sum": sum_ints})
    else:
        ret_int = midas.status_codes["FE_ERR_DRIVER"]
        ret_str = "Unknown command '%s'" % cmd     
        
    return (ret_int, ret_str)

if __name__ == "__main__":
    client = midas.client.MidasClient("pytest")
    
    # Register our function.
    client.register_jrpc_callback(rpc_handler, True)
    
    # Spin forever. Program can be killed by Ctrl+C or
    # "Stop Program" through mhttpd.
    while True:
        client.communicate(10)
            
    # Disconnect from midas before we exit.
    client.disconnect()