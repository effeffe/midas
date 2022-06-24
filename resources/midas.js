/********************************************************************\
 
 Name:         midas.js
 Created by:   Stefan Ritt
 Created by:   Konstantin Olchanski
 
 Contents:     JavaScript midas library for building custom pages
 
\********************************************************************/

// MIDAS type definitions from midas.h

const tid_name = [
   "NULL",
   "UINT8",
   "INT8",
   "CHAR",
   "UINT16",
   "INT16",
   "UINT32",
   "INT32",
   "BOOL",
   "FLOAT",
   "DOUBLE",
   "BITFIELD",
   "STRING",
   "ARRAY",
   "STRUCT",
   "KEY",
   "LINK",
   "INT64",
   "UINT64"
];

const tid_size = [
   0,
   1,
   1,
   1,
   2,
   2,
   4,
   4,
   4,
   4,
   8,
   0,
   0,
   0,
   0,
   0,
   0,
   8,
   8
];

const TID_BYTE     =  1;
const TID_UINT8    =  1;
const TID_SBYTE    =  2;
const TID_INT8     =  2;
const TID_CHAR     =  3;
const TID_WORD     =  4;
const TID_UINT16   =  4;
const TID_SHORT    =  5;
const TID_INT16    =  5;
const TID_DWORD    =  6;
const TID_UINT32   =  6;
const TID_INT      =  7;
const TID_INT32    =  7;
const TID_BOOL     =  8;
const TID_FLOAT    =  9;
const TID_FLOAT32  =  9;
const TID_DOUBLE   = 10;
const TID_FLOAT64  = 10;
const TID_BITFIELD = 11;
const TID_STRING   = 12;
const TID_ARRAY    = 13;
const TID_STRUCT   = 14;
const TID_KEY      = 15;
const TID_LINK     = 16;
const TID_INT64    = 17;
const TID_UINT64   = 18;
const TID_QWORD    = 18;
const TID_LAST     = 19;

const AT_INTERNAL  = 1;
const AT_PROGRAM   = 2;
const AT_EVALUATED = 3;
const AT_PERIODIC  = 4;

const MT_ERROR =  (1<<0);
const MT_INFO  =  (1<<1);
const MT_DEBUG =  (1<<2);
const MT_USER  =  (1<<3);
const MT_LOG   =  (1<<4);
const MT_TALK  =  (1<<5);
const MT_CALL  =  (1<<6);

const STATE_STOPPED = 1;      /**< MIDAS run stopped                  */
const STATE_PAUSED  = 2;      /**< MIDAS run paused                   */
const STATE_RUNNING = 3;      /**< MIDAS run running                  */

const TR_START      =    1;   /**< Start transition  */
const TR_STOP       =    2;   /**< Stop transition  */
const TR_PAUSE      =    4;   /**< Pause transition */
const TR_RESUME     =    8;   /**< Resume transition  */
const TR_STARTABORT =   16;   /**< Start aborted transition  */
const TR_DEFERRED   = 4096;

const MODE_READ      = (1<<0);
const MODE_WRITE     = (1<<1);
const MODE_DELETE    = (1<<2);
const MODE_EXCLUSIVE = (1<<3);
const MODE_ALLOC     = (1<<6);
const MODE_WATCH     = (1<<7);

/// \defgroup mjsonrpc_js JSON-RPC Javascript library (mjsonrpc_xxx)

var mjsonrpc_default_url_web = "";
var mjsonrpc_default_url_file = "https://localhost:8443/";

var mjsonrpc_url;

{
if (window.location.protocol == 'file:') {
   mjsonrpc_url = mjsonrpc_default_url_file;
} else {
   mjsonrpc_url = mjsonrpc_default_url_web;
}
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

   //console.log("mjsonrpc_send_request!");

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
               var xhr_response = xhr.response;

               // if xhr.responseType is "arraybuffer", we need to convert it back to text
               // before we can parse it as json. K.O.
               if (xhr.responseType == "arraybuffer") {
                  xhr_response = new TextDecoder("utf-8").decode(xhr.response);
               }

               try {
                  rpc_response = JSON.parse(xhr_response);
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

function mjsonrpc_db_save(path, filename, id) {
   /// \ingroup mjsonrpc_js
   /// Write part of ODB to file.
   ///
   /// RPC method: "db_save"
   ///
   /// \code
   /// mjsonrpc_db_save("/runinfo", "runinfo.odb"]).then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var id     = rpc.id;      // rpc response id (should be same as req.id)
   ///    var status = rpc.result.status;  // return status of MIDAS cm_msg1()
   ///    ...
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   /// @param[in] ODB paths (string)
   /// @param[in] filename (string)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns new Promise
   ///
   var req = new Object();
   req.path = path;
   req.filename = filename;
   return mjsonrpc_call("db_save", req, id);
}

function mjsonrpc_db_load(filename, id) {
   /// \ingroup mjsonrpc_js
   /// Read from ODB file.
   ///
   /// RPC method: "db_load"
   ///
   /// \code
   /// mjsonrpc_db_load("runinfo.odb"]).then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var id     = rpc.id;      // rpc response id (should be same as req.id)
   ///    var status = rpc.result.status;  // return status of MIDAS cm_msg1()
   ///    ...
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   /// @param[in] filename (string)
   /// @param[in] id optional request id (see JSON-RPC specs) (object)
   /// @returns new Promise
   ///
   var req = new Object();
   req.filename = filename;
   return mjsonrpc_call("db_load", req, id);
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

function mjsonrpc_get_timezone() {
   /// Get timezone offset of server in hours
   ///
   /// RPC method: "get_timezone"
   ///
   /// \code
   /// mjsonrpc_get_timezone().then(function(rpc) {
   ///    var req    = rpc.request; // reference to the rpc request
   ///    var tz     = rpc.result;  // server timezone
   ///    ...
   /// }).catch(function(error) {
   ///    mjsonrpc_error_alert(error);
   /// });
   /// \endcode
   /// @returns new Promise
   ///
   var req = new Object();
   return mjsonrpc_call("get_timezone", req, 0);
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

function bkToObj(array) {
   /// Midas banks-to-object: Parses a raw midas even received via the new bm_receive_event
   /// call and converts it into an JS object of with following elements and returns it:
   ///
   /// e.event_id       <-- elements of midas event header
   /// e.trigger_mask   <-/
   /// e.serial_number  <-/
   /// e.time_stamp     <-/
   /// e.data_size      <-/
   ///
   /// e.banks_16bit    <-- true if we have 16-bit banks
   /// e.banks_32bit    <-- true if we have 32-bit banks
   /// e.banks_32bita   <-- true if we have 64-bit aligned 32-bit banks


   /// e.bank[]         <-- array of banks
   ///   .name          <-- name of bank
   ///   .type          <-- bank type, one of TID_xxx
   ///   .size          <-- bank size in bytes
   ///   .hexdata[]     <-- array of raw (byte) data
   ///   .array[]       <-- array of converted data (UINT16, UINT32, FLOAT, DOUBLE)
   ///
   /// This function is used by the event_dump.html page and can be used in a custom
   /// page for single event displays.

   let e = {};

   let data8  = new Uint8Array(array);
   let data16 = new Uint16Array(array);
   let data32 = new Uint32Array(array);

   // decode event header
   e.event_id = data16[0];
   e.trigger_mask = data16[1];
   e.serial_number = data32[1];
   e.time_stamp = new Date(data32[2]*1000);
   e.data_size = data32[3];
   e.bank = [];
   let event_header_size = 16; // bytes

   // decode banks size and type
   e.bank_total_size = data32[4];
   e.bank_flags   = data32[5];
   e.banks_16bit  = (e.bank_flags === 0x01);
   e.banks_32bit  = (e.bank_flags === 0x11);
   e.banks_32bita = (e.bank_flags === 0x31);
   let bank_header_size = 8;

   let i8 = event_header_size + bank_header_size;

   //console.log("AAA bank type: " + e.banks_16bit + e.banks_32bit + e.banks_32bita);

   // iterate over banks
   while(i8 < e.data_size + event_header_size) {
      let b = {};
      b.name = String.fromCharCode(data8[i8], data8[i8 + 1], data8[i8 + 2], data8[i8 + 3]);
      if (e.banks_16bit) {
         b.type = data16[i8/2 + 2];
         b.size = data16[i8/2 + 3];
         i8 += 8;
      } else if (e.banks_32bit) {
         b.type = data32[i8/4 + 1];
         b.size = data32[i8/4 + 2];
         i8 += 12;
      } else if (e.banks_32bita) {
         b.type = data32[i8/4 + 1];
         b.size = data32[i8/4 + 2];
         i8 += 16;
      }
      if (b.type >= tid_name.length)
         b.type = 0;

      let size_align = 1;

      if (b.type === TID_INT8) {
      } else if (b.type === TID_UINT8) {
      } else if (b.type === TID_INT16) {
         size_align = 2;
      } else if (b.type === TID_UINT16) {
         size_align = 2;
      } else if (b.type === TID_INT32) {
         size_align = 4;
      } else if (b.type === TID_UINT32) {
         size_align = 4;
      } else if (b.type === TID_INT64) {
         size_align = 8;
      } else if (b.type === TID_UINT64) {
         size_align = 8;
      } else if (b.type === TID_FLOAT) {
         size_align = 4;
      } else if (b.type === TID_DOUBLE) {
         size_align = 8;
      } else {
      }

      let aligned_size = align8(b.size);

      if (b.size % size_align != 0) {
         //console.log("b.size: " + b.size + " misalign: " + b.size % size_align);
         b.size -= (b.size % size_align);
      }

      b.data = array.slice(i8, i8+b.size);
      i8 += aligned_size;

      //if (e.banks_32bit) {
      //   let pad = align8(b.size) - b.size;
      //   i8 += pad;
      //} else
      //   i8 = align8(i8);

      b.hexdata = new Uint8Array(b.data);

      if (b.type === TID_INT8)
         b.array = new Int8Array(b.data);
      else if (b.type === TID_UINT8)
         b.array = new Uint8Array(b.data);
      else if (b.type === TID_INT16)
         b.array = new Int16Array(b.data);
      else if (b.type === TID_UINT16)
         b.array = new Uint16Array(b.data);
      else if (b.type === TID_INT32)
         b.array = new Int32Array(b.data);
      else if (b.type === TID_UINT32)
         b.array = new Uint32Array(b.data);
      else if (b.type === TID_INT64)
         b.array = new Int32Array(b.data);
      else if (b.type === TID_UINT64)
         b.array = new Uint32Array(b.data);
      else if (b.type === TID_FLOAT)
         b.array = new Float32Array(b.data);
      else if (b.type === TID_DOUBLE)
         b.array = new Float64Array(b.data);
      else
         b.array = "Unknown data type " + b.type;

      e.bank.push(b);
   }

   return e;
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * js-indent-level: 3
 * indent-tabs-mode: nil
 * End:
 */
