/********************************************************************\
 
 Name:         midas.js
 Created by:   Stefan Ritt
 Created by:   Konstantin Olchanski
 
 Contents:     JavaScript midas library for building custom pages
 
\********************************************************************/

// MIDAS type definitions from midas.h

var TID_BYTE  = 1;
var TID_SBYTE = 2;
var TID_CHAR  = 3;
var TID_WORD  = 4;
var TID_SHORT = 5;
var TID_DWORD = 6;
var TID_INT   = 7;
var TID_BOOL  = 8;
var TID_FLOAT = 9;
var TID_DOUBLE   = 10;
var TID_BITFIELD = 11;
var TID_STRING   = 12;
var TID_ARRAY    = 13;
var TID_STRUCT   = 14;
var TID_KEY      = 15;
var TID_LINK     = 16;

var AT_INTERNAL  = 1;
var AT_PROGRAM   = 2;
var AT_EVALUATED = 3;
var AT_PERIODIC  = 4;

var MT_ERROR =  (1<<0);
var MT_INFO  =  (1<<1);
var MT_DEBUG =  (1<<2);
var MT_USER  =  (1<<3);
var MT_LOG   =  (1<<4);
var MT_TALK  =  (1<<5);
var MT_CALL  =  (1<<6);

var STATE_STOPPED = 1;      /**< MIDAS run stopped                  */
var STATE_PAUSED  = 2;      /**< MIDAS run paused                   */
var STATE_RUNNING = 3;      /**< MIDAS run running                  */

var TR_START      =    1;   /**< Start transition  */
var TR_STOP       =    2;   /**< Stop transition  */
var TR_PAUSE      =    4;   /**< Pause transition */
var TR_RESUME     =    8;   /**< Resume transition  */
var TR_STARTABORT =   16;   /**< Start aborted transition  */
var TR_DEFERRED   = 4096;

/// \defgroup mjsonrpc_js JSON-RPC Javascript library (mjsonrpc_xxx)

var mjsonrpc_default_url_web = "";
var mjsonrpc_default_url_file = "https://localhost:8443/";

var mjsonrpc_url;

if (window.location.protocol == 'file:') {
   mjsonrpc_url = mjsonrpc_default_url_file;
} else {
   mjsonrpc_url = mjsonrpc_default_url_web;
}


function mjsonrpc_set_url(url)
{
   /// \ingroup mjsonrpc_js
   /// Change the URL of JSON-RPC server
   /// @param[in] url the new URL, i.e. "https://daqserver.example.com:8443" (string)
   /// @returns nothing
   mjsonrpc_url = url;
}

function mjsonrpc_send_request(req)
{
   /// \ingroup mjsonrpc_js
   /// Send JSON-RPC request(s) via HTTP POST. RPC response and error handling is done using the Javascript Promise mechanism:
   ///
   /// \code
   /// var req = mjsonrpc_make_request(method, params, id);
   /// mjsonrpc_send_request(req).then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var id     = rpc.id;      // rpc response id (should be same as req.id)
   ///    var result = rpc.result;  // rpc response result
   ///    ...
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   ///
   /// @param[in] req request object or an array of request objects (object or array of objects)
   /// @returns new Promise

   return new Promise(function(resolve, reject) {
      var xhr = new XMLHttpRequest();
      //xhr.responseType = 'json'; // this does not work: behaviour is not defined if RPC returns unparsable JSON
      if (req.id == "arraybuffer") {
         xhr.responseType = 'arraybuffer';
      } else {
         xhr.responseType = 'text';
      }
      xhr.withCredentials = true;

      xhr.onreadystatechange = function()
      {
         //alert("XHR: ready state " + xhr.readyState + " status " + xhr.status);
         if (xhr.readyState == 4) {

            if (xhr.status != 200) {
               var error = new Object;
               error.request = req;
               error.xhr = xhr;
               reject(error);
               return;
            }

            var contentType = xhr.getResponseHeader("Content-Type");
            //console.log("XHR ContentType: " + contentType);
            //console.log("XHR response type: " + typeof(xhr.response));

            if (contentType == "application/octet-stream") {
               var response = xhr.response;
               //console.log("XHR response type: " + typeof(response));
               //console.log("XHR response: " + response);
               resolve(response);
               return;
            } else if (contentType == "application/json") {
               var rpc_response = null;

               try {
                  rpc_response = JSON.parse(xhr.response);
                  if (!rpc_response) {
                     throw "JSON parser returned null";
                  }
               } catch (exc) {
                  //alert("exception " + exc);
                  var error = new Object;
                  error.request = req;
                  error.xhr = xhr;
                  error.exception = exc;
                  reject(error);
                  return;
               }
               
               if (Array.isArray(rpc_response)) {
                  var batch = new Array;
                  for (var i=0; i<rpc_response.length; i++) {
                     var rpc = new Object;
                     rpc.request = req[i];
                     rpc.id = rpc_response[i].id;
                     if (rpc_response[i].hasOwnProperty("error")) {
                        rpc.error = rpc_response[i].error;
                     } else {
                        rpc.result = rpc_response[i].result;
                     }
                     batch.push(rpc);
                  }
                  resolve(batch);
                  return;
               }
               
               if (rpc_response.error) {
                  var error = new Object;
                  error.request = req;
                  error.xhr = xhr;
                  error.error = rpc_response.error;
                  reject(error);
                  return;
               }
               
               var rpc = new Object;
               rpc.request = req;
               rpc.id = rpc_response.id;
               rpc.result = rpc_response.result;
               resolve(rpc);
               return;
            } else {
               var error = new Object;
               error.request = req;
               error.xhr = xhr;
               error.error = "Unexpected Content-Type: " + contentType;
               reject(error);
               return;
            }
         }
      }

      xhr.open('POST', mjsonrpc_url + "?mjsonrpc");
      xhr.setRequestHeader('Content-Type', 'application/json');
      xhr.setRequestHeader('Accept', 'application/json');
      if (req == "send invalid json")
         xhr.send("invalid json");
      else
         xhr.send(JSON.stringify(req));
   });
}

function mjsonrpc_debug_alert(rpc) {
   /// \ingroup mjsonrpc_js
   /// Debug method to show RPC response
   /// @param[in] rpc object (object), see mjsonrpc_send_request()
   /// @returns nothing
   //console.log(rpc);
   if (Array.isArray(rpc)) {
      //console.log("here!");
      var a = "";
      rpc.forEach(function(r) {
         //console.log(r);
         if (r.error) {
            a += "method: \"" + r.request.method + "\", params: " + r.request.params + ", id: " + JSON.stringify(r.id) + ", error: " + JSON.stringify(r.error);
         } else {
            a += "method: \"" + r.request.method + "\", params: " + r.request.params + ", id: " + JSON.stringify(r.id) + ", response: " + JSON.stringify(r.result);
         }
         a += "\n";
      });
      alert("mjsonrpc_debug_alert: array:\n" + a);
   } else {
      alert("mjsonrpc_debug_alert: method: \"" + rpc.request.method + "\", params: " + rpc.request.params + ", id: " + JSON.stringify(rpc.id) + ", response: " + JSON.stringify(rpc.result));
   }
}

function mjsonrpc_decode_error(error) {
   /// \ingroup mjsonrpc_js
   /// Convert RPC error status to human-readable string
   /// @param[in] error rejected promise error object (object)
   /// @returns decoded error report (string)

   //console.log("mjsonrpc_decode_error: " + JSON.stringify(error));
   //console.log("mjsonrpc_decode_error xhr: " + JSON.stringify(error.xhr));
   //for (var x in error.xhr) {
   //   if (x == "responseText") continue;
   //   if (x == "responseXML") continue;
   //   console.log("xhr[" + x + "] = [" + error.xhr[x] + "]");
   //}

   function is_network_error(xhr) {
      return xhr.readyState==4 && xhr.status==0;
   }
   function is_http_error(xhr) {
      return xhr.readyState==4 && xhr.status!=200;
   }
   function print_xhr(xhr) {
      return "readyState: " + xhr.readyState + ", HTTP status: " + xhr.status + " (" + xhr.statusText + ")";
   }
   function print_request(request) {
      //console.log("print_request: " + request + ", JSON: " + JSON.stringify(request));
      if (Array.isArray(request)) {
         var s = "batch request: ";
         request.forEach(function(item, index) {
            s += print_request(item);
            s += " ";
         });
         return s;
      }
      return "method: \"" + request.method + "\", params: " + request.params + ", id: " + request.id;
   }

   if (error.xhr && is_network_error(error.xhr)) {
      return "network error: see javascript console, " + print_request(error.request);
   } else if (error.xhr && is_http_error(error.xhr)) {
      return "http error: " + print_xhr(error.xhr) + ", " + print_request(error.request);
   } else if (error.exception) {
      return "json parser exception: " + error.exception + ", " + print_request(error.request);
   } else if (error.error) {
      return "json-rpc error: " + JSON.stringify(error.error) + ", " + print_request(error.request);
   } else if (error.request && error.xhr) {
      return "unknown error, request: " + print_request(error.request) + ", xhr: " + print_xhr(error.xhr);
   } else {
      return "javascript exception: " + error;
   }
}

function mjsonrpc_error_alert(error, callback, param) {
   /// \ingroup mjsonrpc_js
   /// Handle all errors
   /// @param[in] error rejected promise error object (object)
   /// @returns nothing
   if (error.request) {
      var s = mjsonrpc_decode_error(error);
      dlgMessage("RPC Error", s, true, true, callback, param);
   } else {
      dlgMessage("Javascript exception or error", error, true, true, callback, param);
      throw(error);
   }
}

function mjsonrpc_make_request(method, params, id)
{
   /// \ingroup mjsonrpc_js
   /// Creates a new JSON-RPC request object
   /// @param[in] method name of the RPC method (string)
   /// @param[in] params parameters of the RPC method (object)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns the request object (object)

   if (id == null)
      id = Date.now();

   var req = new Object();
   req.jsonrpc = "2.0"; // version
   req.method = method;
   if (typeof params == 'string') {
      req.params = JSON.parse(params);
   } else {
      req.params = params;
   }
   if (!req.params)
      req.params = null; // make sure we have "params", even if set to null or undefined
   req.id = id;

   return req;
}

function mjsonrpc_call(method, params, id)
{
   /// \ingroup mjsonrpc_js
   /// Creates a JSON-RPC request and sends it to mhttpd via HTTP POST.
   /// RPC response and error handling is done using the Javascript Promise mechanism:
   ///
   /// \code
   /// mjsonrpc_call(method, params, id).then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var id     = rpc.id;      // rpc response id (should be same as req.id)
   ///    var result = rpc.result;  // rpc response result
   ///    ...
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   /// @param[in] method name of the RPC method (string)
   /// @param[in] params parameters of the RPC method (object)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns new Promise

   var req = mjsonrpc_make_request(method, params, id);
   return mjsonrpc_send_request(req);
}

function mjsonrpc_start_program(name, id) {
   /// \ingroup mjsonrpc_js
   /// Start a MIDAS program
   ///
   /// RPC method: "start_program"
   ///
   /// \code
   /// mjsonrpc_start_program("logger").then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var id     = rpc.id;      // rpc response id (should be same as req.id)
   ///    var result = rpc.result;  // rpc response result
   ///    var status = rpc.result.status; // return status of ss_system(), see MIDAS JSON-RPC docs
   ///    ...
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   /// @param[in] name Name of program to start, should be same as the ODB entry "/Programs/name" (string)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns new Promise

   var req = new Object();
   req.name = name;
   return mjsonrpc_call("start_program", req, id);
}

function mjsonrpc_stop_program(name, unique, id) {
   /// \ingroup mjsonrpc_js
   /// Stop a MIDAS program via cm_shutdown()
   ///
   /// RPC method: "cm_shutdown"
   ///
   /// \code
   /// mjsonrpc_stop_program("logger").then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var id     = rpc.id;      // rpc response id (should be same as req.id)
   ///    var result = rpc.result;  // rpc response result
   ///    var status = rpc.result.status; // return status of cm_shutdown(), see MIDAS JSON-RPC docs and cm_shutdown() docs
   ///    ...
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   /// @param[in] name Name of program to stop (string)
   /// @param[in] unique bUnique argument to cm_shutdown() (bool)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns new Promise

   var req = new Object();
   req.name = name;
   req.unique = unique;
   return mjsonrpc_call("cm_shutdown", req, id);
}

function mjsonrpc_cm_exist(name, unique, id) {
   /// \ingroup mjsonrpc_js
   /// Stop a MIDAS program via cm_exist()
   ///
   /// RPC method: "cm_exist"
   ///
   /// @param[in] name Name of program to stop (string)
   /// @param[in] unique bUnique argument to cm_shutdown() (bool)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   var req = new Object();
   req.name = name;
   req.unique = unique;
   return mjsonrpc_call("cm_exist", req, id);
}

function mjsonrpc_al_reset_alarm(alarms, id) {
   /// \ingroup mjsonrpc_js
   /// Reset alarms
   ///
   /// RPC method: "al_reset_alarm"
   ///
   /// \code
   /// mjsonrpc_al_reset_alarm(["alarm1", "alarm2"]).then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var id     = rpc.id;      // rpc response id (should be same as req.id)
   ///    var result = rpc.result;  // rpc response result
   ///    ... result.status[0]; // status of al_reset_alarm() for 1st alarm
   ///    ... result.status[1]; // status of al_reset_alarm() for 2nd alarm
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   /// @param[in] alarms Array of alarm names (array of strings)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns new Promise
   ///
   var req = new Object();
   req.alarms = alarms;
   return mjsonrpc_call("al_reset_alarm", req, id);
}

function mjsonrpc_al_trigger_alarm(name, message, xclass, condition, type, id) {
   /// \ingroup mjsonrpc_js
   /// Reset alarms
   ///
   /// RPC method: "al_reset_alarm"
   ///
   /// \code
   /// mjsonrpc_al_reset_alarm(["alarm1", "alarm2"]).then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var id     = rpc.id;      // rpc response id (should be same as req.id)
   ///    var result = rpc.result;  // rpc response result
   ///    ... result.status[0]; // status of al_reset_alarm() for 1st alarm
   ///    ... result.status[1]; // status of al_reset_alarm() for 2nd alarm
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   /// @param[in] alarms Array of alarm names (array of strings)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns new Promise
   ///
   var req = new Object();
   req.name = name;
   req.message = message;
   req.class = xclass;
   req.condition = condition;
   req.type = type;
   return mjsonrpc_call("al_trigger_alarm", req, id);
}

function mjsonrpc_db_copy(paths, id) {
   /// \ingroup mjsonrpc_js
   /// Get a copy of ODB. Symlinks are not resolved, ODB path names are not converted to lower-case.
   ///
   /// Instead of this function, please use db_get_values() as a simple way to get easy to use ODB values.
   ///
   /// RPC method: "db_copy"
   ///
   /// \code
   /// mjsonrpc_db_copy(["/runinfo", "/equipment/foo"]).then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var id     = rpc.id;      // rpc response id (should be same as req.id)
   ///    var result = rpc.result;  // rpc response result
   ///    ... result.status[0]; // status of db_get_value() for /runinfo
   ///    ... result.status[1]; // status of db_get_value() for /equipment
   ///    var runinfo = result.data[0]; // javascript object representing the ODB runinfo structure
   ///    var equipment = result.data[1]; // javascript object representing /equipment/foo
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   /// @param[in] paths Array of ODB paths (array of strings)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns new Promise
   ///
   var req = new Object();
   req.paths = paths;
   return mjsonrpc_call("db_copy", req, id);
}

function mjsonrpc_db_get_values(paths, id) {
   /// \ingroup mjsonrpc_js
   /// Get values of ODB variables
   ///
   /// RPC method: "db_get_values"
   ///
   /// \code
   /// mjsonrpc_db_get_values(["/runinfo", "/equipment"]).then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var id     = rpc.id;      // rpc response id (should be same as req.id)
   ///    var result = rpc.result;  // rpc response result
   ///    ... result.status[0]; // status of db_get_value() for /runinfo
   ///    ... result.status[1]; // status of db_get_value() for /equipment
   ///    ... result.last_written[0]; // "last written" timestamp for /runinfo
   ///    ... result.last_written[1]; // "last written" timestamp for /equipment
   ///    var runinfo = result.data[0]; // javascript object representing the ODB runinfo structure
   ///    ... runinfo["run number"];    // access the run number, note: all ODB names should be in lower-case.
   ///    ... runinfo["run number/last_written"]; // "last_written" timestamp for the run number
   ///    ... result.data[1].foo.variables.bar;   // access /equipment/foo/variables/bar
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   /// @param[in] paths Array of ODB paths (array of strings)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns new Promise
   ///
   var req = new Object();
   req.paths = paths;
   return mjsonrpc_call("db_get_values", req, id);
}

function mjsonrpc_db_get_value(paths, id) {
   /// \ingroup mjsonrpc_js
   /// Get values of ODB variables
   ///
   /// RPC method: "db_get_value"
   ///
   /// \code
   /// mjsonrpc_db_get_value("/runinfo").then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var id     = rpc.id;      // rpc response id (should be same as req.id)
   ///    var result = rpc.result;  // rpc response result
   ///    ... result.status[0]; // status of db_get_value() for /runinfo
   ///    ... result.status[1]; // status of db_get_value() for /equipment
   ///    ... result.last_written[0]; // "last written" timestamp for /runinfo
   ///    ... result.last_written[1]; // "last written" timestamp for /equipment
   ///    var runinfo = result.data[0]; // javascript object representing the ODB runinfo structure
   ///    ... runinfo["run number"];    // access the run number, note: all ODB names should be in lower-case.
   ///    ... runinfo["run number/last_written"]; // "last_written" timestamp for the run number
   ///    ... result.data[1].foo.variables.bar;   // access /equipment/foo/variables/bar
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   /// @param[in] paths Array of ODB paths (array of strings)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns new Promise
   ///
   var req = new Object();
   req.paths = [paths];
   return mjsonrpc_call("db_get_values", req, id);
}

function mjsonrpc_db_ls(paths, id) {
   /// \ingroup mjsonrpc_js
   /// Get list of contents of an ODB subdirectory, similar to odbedit command "ls -l". To get values of ODB variables, use db_get_values().
   ///
   /// RPC method: "db_ls"
   ///
   /// \code
   /// mjsonrpc_db_ls(["/alarms/alarms", "/equipment"]).then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var id     = rpc.id;      // rpc response id (should be same as req.id)
   ///    var result = rpc.result;  // rpc response result
   ///    ... result.status[0]; // status of db_copy_json_ls() for /alarms/alarms
   ///    ... result.status[1]; // status of db_copy_json_ls() for /equipment
   ///    var alarms = result.data[0]; // javascript object representing the contents of ODB /alarms/alarms
   ///    var equipment = result.data[1]; // javascript object representing the contents of ODB /equipment
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   /// @param[in] paths Array of ODB paths (array of strings)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns new Promise
   ///
   var req = new Object();
   req.paths = paths;
   return mjsonrpc_call("db_ls", req, id);
}

function mjsonrpc_db_resize(paths, new_lengths, id) {
   /// \ingroup mjsonrpc_js
   /// Change size of ODB arrays
   ///
   /// RPC method: "db_resize"
   ///
   /// \code
   /// mjsonrpc_db_resize(["/test/intarray1", "/test/dblarray2"], [10, 20]).then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var id     = rpc.id;      // rpc response id (should be same as req.id)
   ///    var result = rpc.result;  // rpc response result
   ///    ... result.status[0]; // status of db_set_num_values() for 1st path
   ///    ... result.status[1]; // status of db_set_num_values() for 2nd path
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   /// @param[in] paths Array of ODB paths (array of strings)
   /// @param[in] new_sizes Array of new sizes for each path (array of ints)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns new Promise
   ///
   var req = new Object();
   req.paths = paths;
   req.new_lengths = new_lengths;
   return mjsonrpc_call("db_resize", req, id);
}

function mjsonrpc_db_key(paths, id) {
   /// \ingroup mjsonrpc_js
   /// Get ODB keys
   ///
   /// RPC method: "db_key"
   ///
   /// \code
   /// mjsonrpc_db_key(["/test/intarray1", "/test/dblarray2"]).then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var id     = rpc.id;      // rpc response id (should be same as req.id)
   ///    var result = rpc.result;  // rpc response result
   ///    ... result.status[0]; // status of db_get_key() for 1st path
   ///    ... result.status[1]; // status of db_get_key() for 2nd path
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   /// @param[in] paths Array of ODB paths (array of strings)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns new Promise
   ///
   var req = new Object();
   req.paths = paths;
   return mjsonrpc_call("db_key", req, id);
}

function mjsonrpc_db_delete(paths, id) {
   /// \ingroup mjsonrpc_js
   /// Delete ODB entries
   ///
   /// RPC method: "db_delete"
   ///
   /// \code
   /// mjsonrpc_db_delete(["/test/test1", "/test/test2"]).then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var id     = rpc.id;      // rpc response id (should be same as req.id)
   ///    var result = rpc.result;  // rpc response result
   ///    ... result.status[0]; // status of db_delete() for 1st path
   ///    ... result.status[1]; // status of db_delete() for 2nd path
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   /// @param[in] paths Array of ODB paths (array of strings)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns new Promise
   ///
   var req = new Object();
   req.paths = paths;
   return mjsonrpc_call("db_delete", req, id);
}

function mjsonrpc_db_paste(paths, values, id) {
   /// \ingroup mjsonrpc_js
   /// Write values info ODB.
   ///
   /// RPC method: "db_paste"
   ///
   /// \code
   /// mjsonrpc_db_paste(["/runinfo/run number", "/equipment/foo/settings/bar"], [123,456]).then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var id     = rpc.id;      // rpc response id (should be same as req.id)
   ///    var result = rpc.result;  // rpc response result
   ///    ... result.status[0]; // status of db_set_value() for /runinfo
   ///    ... result.status[1]; // status of db_set_value() for /equipment
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   /// @param[in] paths Array of ODB paths (array of strings)
   /// @param[in] values Array of ODB values (array of anything)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns new Promise
   ///
   var req = new Object();
   req.paths = paths;
   req.values = values;
   return mjsonrpc_call("db_paste", req, id);
}

function mjsonrpc_db_set_value(path, value, id) {
   /// \ingroup mjsonrpc_js
   /// Write value info ODB.
   ///
   /// RPC method: "db_set_value"
   ///
   /// \code
   /// mjsonrpc_set_value("runinfo/run number", 123).then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var id     = rpc.id;      // rpc response id (should be same as req.id)
   ///    var result = rpc.result;  // rpc response result
   ///    ... result.status[0]; // status of db_set_value() for /runinfo
   ///    ... result.status[1]; // status of db_set_value() for /equipment
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   /// @param[in] paths Array of ODB paths (array of strings)
   /// @param[in] values Array of ODB values (array of anything)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns new Promise
   ///
   var req = new Object();
   req.paths = [path];
   req.values = [value];
   return mjsonrpc_call("db_paste", req, id);
}

function mjsonrpc_db_create(paths, id) {
   /// \ingroup mjsonrpc_js
   /// Create ODB entries
   ///
   /// RPC method: "db_create"
   ///
   /// @param[in] paths Array of ODB entries to create (array of objects)
   /// @param[in] paths[i].path ODB path name to create (string)
   /// @param[in] paths[i].type TID_xxx data type (integer)
   /// @param[in] paths[i].array_length Optional array length (default is 1) (integer)
   /// @param[in] paths[i].string_length Optional string length (default is NAME_LENGTH) (integer)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns new Promise

   return mjsonrpc_call("db_create", paths, id);
}

function mjsonrpc_cm_msg(message, type, id) {
   /// \ingroup mjsonrpc_js
   /// Get values of ODB variables
   ///
   /// RPC method: "cm_msg1"
   ///
   /// \code
   /// mjsonrpc_cm_msg("this is a new message").then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var id     = rpc.id;      // rpc response id (should be same as req.id)
   ///    var status = rpc.result.status;  // return status of MIDAS cm_msg1()
   ///    ...
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   /// @param[in] message Text of midas message (string)
   /// @param[in] type optional message type, one of MT_xxx. Default is MT_INFO (integer)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns new Promise
   ///
   var req = new Object();
   req.message = message;
   if (type)
      req.type = type;
   return mjsonrpc_call("cm_msg1", req, id);
}

function modbset(path, value)
   /// \ingroup mjsonrpc_js
   /// Simplified function to initiate an ODB write RPC request. This function returns immediately, without waiting for the request to complete. There is no way to check that the write request completed without error. Multiple calls to modbset() may result in out-of-order writes to ODB (later requests may be processed before earlier requests).
   ///
{
   if (Array.isArray(path)) {
      mjsonrpc_db_paste(path,value).then(function(rpc) {}).catch(function(error) {
         mjsonrpc_error_alert(error); });
   } else {
      mjsonrpc_db_paste([path],[value]).then(function(rpc) {}).catch(function(error) {
         mjsonrpc_error_alert(error); });
   }
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * js-indent-level: 3
 * indent-tabs-mode: nil
 * End:
 */
