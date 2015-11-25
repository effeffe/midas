/********************************************************************\
 
 Name:         mhttpd.js
 Created by:   Stefan Ritt
 
 Contents:     JavaScript midas library used by mhttpd
 
\********************************************************************/

// MIDAS type definitions
var TID_BYTE = 1;
var TID_SBYTE = 2;
var TID_CHAR = 3;
var TID_WORD = 4;
var TID_SHORT = 5;
var TID_DWORD = 6;
var TID_INT = 7;
var TID_BOOL = 8;
var TID_FLOAT = 9;
var TID_DOUBLE = 10;
var TID_BITFIELD = 11;
var TID_STRING = 12;
var TID_ARRAY = 13;
var TID_STRUCT = 14;
var TID_KEY = 15;
var TID_LINK = 16;

function XMLHttpRequestGeneric()
{
   var request;
   try {
      request = new XMLHttpRequest(); // Firefox, Opera 8.0+, Safari
   }
   catch (e) {
      try {
         request = new ActiveXObject('Msxml2.XMLHTTP'); // Internet Explorer
      }
      catch (e) {
         try {
            request = new ActiveXObject('Microsoft.XMLHTTP');
         }
         catch (e) {
           alert('Your browser does not support AJAX!');
           return undefined;
         }
      }
   }

   return request;
}

var ODBUrlBase = "";

function ODBSetURL(url)
{
    ODBUrlBase = url;
}

function ODBSet(path, value, pwdname)
{
   var value, request, url;

   if (pwdname != undefined)
      pwd = prompt('Please enter password', '');
   else
      pwd = '';

   var request = XMLHttpRequestGeneric();

   url = ODBUrlBase + '?cmd=jset&odb=' + path + '&value=' + encodeURIComponent(value);

   if (pwdname != undefined)
      url += '&pnam=' + pwdname;

   request.open('GET', url, false);

   if (pwdname != undefined)
      request.setRequestHeader('Cookie', 'cpwd='+pwd);

   request.send(null);

   if (request.status != 200 || request.responseText != 'OK') 
      alert('ODBSet error:\nPath: '+path+'\nHTTP Status: '+request.status+'\nMessage: '+request.responseText+'\n'+document.location) ;
}

function ODBGet(path, format, defval, len, type)
{
   var request = XMLHttpRequestGeneric();

   var url = ODBUrlBase + '?cmd=jget&odb=' + path;
   if (format != undefined && format != '')
      url += '&format=' + format;
   request.open('GET', url, false);
   request.send(null);

   if (path.match(/[*]/)) {
      if (request.responseText == null)
         return null;
      if (request.responseText == '<DB_NO_KEY>') {
         url = '?cmd=jset&odb=' + path + '&value=' + defval + '&len=' + len + '&type=' + type;

         request.open('GET', url, false);
         request.send(null);
         return defval;
      } else {
         var array = request.responseText.split('\n');
         return array;
      }
   } else {
      if ((request.responseText == '<DB_NO_KEY>' ||
           request.responseText == '<DB_OUT_OF_RANGE>') && defval != undefined) {
         url = '?cmd=jset&odb=' + path + '&value=' + defval + '&len=' + len + '&type=' + type;

         request.open('GET', url, false);
         request.send(null);
         return defval;
      }
      return request.responseText.split('\n')[0];
   }
}

function ODBMGet(paths, callback, formats)
{
   var request = XMLHttpRequestGeneric();

   var url = ODBUrlBase + '?cmd=jget';
   for (var i=0 ; i<paths.length ; i++) {
      url += '&odb'+i+'='+paths[i];
      if (formats != undefined && formats != '')
         url += '&format'+i+'=' + formats[i];
   }

   if (callback != undefined) {
      request.onreadystatechange = function() 
         {
         if (request.readyState == 4) {
            if (request.status == 200) {
               var array = request.responseText.split('$#----#$\n');
               for (var i=0 ; i<array.length ; i++)
                  if (paths[i].match(/[*]/)) {
                     array[i] = array[i].split('\n');
                     array[i].length--;
                  } else
                     array[i] = array[i].split('\n')[0];
               callback(array);
            }
         }
      }
      request.open('GET', url, true);
   } else
      request.open('GET', url, false);
   request.send(null);

   if (callback == undefined) {
      var array = request.responseText.split('$#----#$\n');
      for (var i=0 ; i<array.length ; i++) {
         if (paths[i].match(/[*]/)) {
            array[i] = array[i].split('\n');
            array[i].length--;
         } else
            array[i] = array[i].split('\n')[0];
      }
      return array;
   }
}

function ODBGetRecord(path)
{
   var request = XMLHttpRequestGeneric();

   var url = ODBUrlBase + '?cmd=jget&odb=' + path + '&name=1';
   request.open('GET', url, false);
   request.send(null);
   return request.responseText;
}

function ODBExtractRecord(record, key)
{
   var array = record.split('\n');
   for (var i=0 ; i<array.length ; i++) {
      var ind = array[i].indexOf(':');
      if (ind > 0) {
         var k = array[i].substr(0, ind);
         if (k == key)
            return array[i].substr(ind+1, array[i].length);
      }
      var ind = array[i].indexOf('[');
      if (ind > 0) {
         var k = array[i].substr(0, ind);
         if (k == key) {
            var a = new Array();
            for (var j=0 ; ; j++,i++) {
               if (array[i].substr(0, ind) != key)
                  break;
               var k = array[i].indexOf(':');
               a[j] = array[i].substr(k+1, array[i].length);
            }
            return a;
         }
      }
   }
   return null;
}

function ODBKey(path)
{
   var request = XMLHttpRequestGeneric();

   var url = ODBUrlBase + '?cmd=jkey&odb=' + path;
   request.open('GET', url, false);
   request.send(null);
   if (request.responseText == null)
      return null;
   var res = request.responseText.split('\n');
   this.name = res[0];
   this.type = res[1];
   this.num_values = res[2];
   this.item_size = res[3];
   this.last_written = res[4];
}

function ODBCopy(path, format)
{
   var request = XMLHttpRequestGeneric();

   var url = ODBUrlBase + '?cmd=jcopy&odb=' + path;
   if (format != undefined && format != '')
      url += '&format=' + format;
   request.open('GET', url, false);
   request.send(null);
   return request.responseText;
}

/// \defgroup mjsonrpc_js JSON-RPC Javascript library (mjsonrpc_xxx)

var mjsonrpc_default_url = "";
var mjsonrpc_url = mjsonrpc_default_url;

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
      xhr.responseType = 'json';
      xhr.withCredentials = true;

      xhr.onreadystatechange = function()
      {
         //alert("XHR: ready state " + xhr.readyState + " status " + xhr.status);
         if (xhr.readyState == 4) {
            var exc = null;

            if (xhr.status == 200) {
               var have_response = false;
               try {
                  if (xhr.response) {
                     var response = xhr.response;
                     var error = response.error;
                     // alert("e " + JSON.stringify(xhr.response));
                     // (typeof xhr.response.error == 'undefined')
                     have_response = (error == null);
                  }
               } catch (xexc) {
                  // fall through
                  //alert("exception " + xexc);
                  exc = xexc;
               }

               if (have_response) {
                  var r = new Object;
                  r.request = req;
                  r.id = response.id;
                  r.result = response.result;
                  resolve(r);
                  return;
               }
            }

            var r = new Object;
            r.request = req;
            r.xhr = xhr;
            r.exception = exc;
            reject(r);
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
   alert("mjsonrpc_debug_alert: method: \"" + rpc.request.method + "\", params: " + rpc.request.params + ", id: " + JSON.stringify(rpc.id) + ", response: " + JSON.stringify(rpc.result));
}

function mjsonrpc_decode_error(request, xhr, exc) {
   /// \ingroup mjsonrpc_js
   /// Convert RPC error status to human-readable string
   /// @param[in] request request object (object)
   /// @param[in] xhr request XHR object (object)
   /// @param[in] exc request exception object (object)
   /// @returns decoded error report (string)

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
      return "method: \"" + request.method + "\", params: " + request.params + ", id: " + request.id;
   }

   if (is_network_error(xhr)) {
      return "network error: see javascript console, " + print_request(request);
   } else if (is_http_error(xhr)) {
      return "http error: " + print_xhr(xhr) + ", " + print_request(request);
   } else if (exc) {
      return "exception: " + exc + ", " + print_request(request);
   } else if (!xhr.response) {
      return "json-rpc error: null response, " + print_request(request);
   } else if (xhr.response.error) {
      return "json-rpc error: " + JSON.stringify(xhr.response.error) + ", " + print_request(request);
   } else {
      return "what happened?!?, " + print_request(request) + ", xhr: " + print_xhr(xhr) + ", exc: " + exc;
   }
}

function mjsonrpc_error_alert(error) {
   /// \ingroup mjsonrpc_js
   /// Handle all errors
   /// @param[in] error rejected promise error object (object)
   /// @returns nothing

   if (error.request) {
      var s = mjsonrpc_decode_error(error.request, error.xhr, error.exception);
      alert("mjsonrpc_error_alert: " + s);
   } else {
      alert("mjsonroc_error_alert: " + error);
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
   ///    ... result.last_written[0]; // "last written" timestamp for /runinfo
   ///    ... result.last_written[1]; // "last written" timestamp for /equipment
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

function ODBCall(url, callback)
{
   var request = XMLHttpRequestGeneric();
      
   if (callback != undefined) {
      request.onreadystatechange = function() 
         {
            if (request.readyState == 4) {
               if (request.status == 200) {
                  callback(request.responseText);
               }
            }
         }
      request.open('GET', url, true);
      request.send(null);
      return;
   }
   
   request.open('GET', url, false);
   request.send(null);
   return request.responseText;
}

function ODBMCopy(paths, callback, encoding)
{
   var url = ODBUrlBase + '?cmd=jcopy';
   for (var i=0 ; i<paths.length ; i++) {
      url += '&odb'+i+'='+encodeURIComponent(paths[i]);
   }

   if (encoding != undefined && encoding != '')
      url += '&encoding=' + encodeURIComponent(encoding);

   return ODBCall(url, callback);
}

function ODBMLs(paths, callback)
{
   var url = ODBUrlBase + '?cmd=jcopy&encoding=json-norecurse';
   for (var i=0 ; i<paths.length ; i++) {
      url += '&odb'+i+'='+encodeURIComponent(paths[i]);
   }

   return ODBCall(url, callback);
}

function ODBMCreate(paths, types, arraylengths, stringlengths, callback)
{
   var url = ODBUrlBase + '?cmd=jcreate';
   for (var i=0 ; i<paths.length ; i++) {
      url += '&odb'+i+'='+encodeURIComponent(paths[i]);
      url += '&type'+i+'='+encodeURIComponent(types[i]);
      if (arraylengths != undefined) {
         url += '&arraylen'+i+'='+encodeURIComponent(arraylengths[i]);
      }
      if (stringlengths != undefined) {
         url += '&strlen'+i+'='+encodeURIComponent(stringlengths[i]);
      }
   }
   return ODBCall(url, callback);
}

function ODBMResize(paths, arraylengths, stringlengths, callback)
{
   var url = ODBUrlBase + '?cmd=jresize';
   for (var i=0 ; i<paths.length ; i++) {
      url += '&odb'+i+'='+encodeURIComponent(paths[i]);
      url += '&arraylen'+i+'='+encodeURIComponent(arraylengths[i]);
      url += '&strlen'+i+'='+encodeURIComponent(stringlengths[i]);
   }
   return ODBCall(url, callback);
}

function ODBMRename(paths, names, callback)
{
   var url = ODBUrlBase + '?cmd=jrename';
   for (var i=0 ; i<paths.length ; i++) {
      url += '&odb'+i+'='+encodeURIComponent(paths[i]);
      url += '&name'+i+'='+encodeURIComponent(names[i]);
   }
   return ODBCall(url, callback);
}

function ODBMLink(paths, links, callback)
{
   var url = ODBUrlBase + '?cmd=jlink';
   for (var i=0 ; i<paths.length ; i++) {
      url += '&dest'+i+'='+encodeURIComponent(paths[i]);
      url += '&odb'+i+'='+encodeURIComponent(links[i]);
   }
   return ODBCall(url, callback);
}

function ODBMReorder(paths, indices, callback)
{
   var url = ODBUrlBase + '?cmd=jreorder';
   for (var i=0 ; i<paths.length ; i++) {
      url += '&odb'+i+'='+encodeURIComponent(paths[i]);
      url += '&index'+i+'='+encodeURIComponent(indices[i]);
   }
   return ODBCall(url, callback);
}

function ODBMKey(paths, callback)
{
   var url = ODBUrlBase + '?cmd=jkey&encoding=json';
   for (var i=0 ; i<paths.length ; i++) {
      url += '&odb'+i+'='+encodeURIComponent(paths[i]);
   }
   return ODBCall(url, callback);
}

function ODBMDelete(paths, callback)
{
   var url = ODBUrlBase + '?cmd=jdelete';
   for (var i=0 ; i<paths.length ; i++) {
      url += '&odb'+i+'='+encodeURIComponent(paths[i]);
   }
   return ODBCall(url, callback);
}

function ODBRpc_rev0(name, rpc, args)
{
   var request = XMLHttpRequestGeneric();

   var url = ODBUrlBase +  '?cmd=jrpc_rev0&name=' + name + '&rpc=' + rpc;
   for (var i = 2; i < arguments.length; i++) {
     url += '&arg'+(i-2)+'='+arguments[i];
   };
   request.open('GET', url, false);
   request.send(null);
   if (request.responseText == null)
      return null;
   this.reply = request.responseText.split('\n');
}

function ODBRpc_rev1(name, rpc, max_reply_length, args)
{
   var request = XMLHttpRequestGeneric();

   var url = ODBUrlBase + '?cmd=jrpc_rev1&name=' + name + '&rpc=' + rpc + '&max_reply_length=' + max_reply_length;
   for (var i = 3; i < arguments.length; i++) {
     url += '&arg'+(i-3)+'='+arguments[i];
   };
   request.open('GET', url, false);
   request.send(null);
   if (request.responseText == null)
      return null;
   return request.responseText;
}

function ODBRpc(program_name, command_name, arguments_string, callback, max_reply_length)
{
   var url = ODBUrlBase + '?cmd=jrpc';
   url += '&name=' + encodeURIComponent(program_name);
   url += '&rcmd=' + encodeURIComponent(command_name);
   url += '&rarg=' + encodeURIComponent(arguments_string);
   if (max_reply_length) {
      url += '&max_reply_length=' + encodeURIComponent(max_reply_length);
   }
   return ODBCall(url, callback);
}

function ODBGetMsg(facility, start, n)
{
   var request = XMLHttpRequestGeneric();

   var url = ODBUrlBase + '?cmd=jmsg&f='+facility+'&t=' + start+'&n=' + n;
   request.open('GET', url, false);
   request.send(null);

   if (n > 1 || n == 0) {
      var array = request.responseText.split('\n');
      while (array.length > 1 && array[array.length-1] == "")
         array = array.slice(0, array.length-1);
      return array;
   } else
      return request.responseText;
}

const MT_ERROR =  (1<<0);
const MT_INFO  =  (1<<1);
const MT_DEBUG =  (1<<2);
const MT_USER  =  (1<<3);
const MT_LOG   =  (1<<4);
const MT_TALK  =  (1<<5);
const MT_CALL  =  (1<<6);

function ODBGenerateMsg(type,facility,user,msg)
{
   var request = XMLHttpRequestGeneric();

   var url = ODBUrlBase + '?cmd=jgenmsg';
   url += '&type='+type;
   url += '&facility='+facility;
   url += '&user='+user;
   url += '&msg=' + encodeURIComponent(msg);
   request.open('GET', url, false);
   request.send(null);
   return request.responseText;
}

function ODBGetAlarms()
{
   var request = XMLHttpRequestGeneric();
   request.open('GET', ODBUrlBase + '?cmd=jalm', false);
   request.send(null);
   var a = request.responseText.split('\n');
   a.length = a.length-1;
   return a;
}

function ODBEdit(path)
{
   var value = ODBGet(encodeURIComponent(path));
   var new_value = prompt('Please enter new value', value);
   if (new_value != undefined) {
      ODBSet(encodeURIComponent(path), new_value);
      window.location.reload();
   }
}

function ODBFinishInlineEdit(p, path, bracket)
{
   var value;
 
   if (p.ODBsent == true)
      return;
   
   if (p.childNodes.length == 2)
      value = p.childNodes[1].value;
   else
      value = p.childNodes[0].value;

   ODBSet(encodeURIComponent(path), value);
   p.ODBsent = true;
   
   var link = document.createElement('a');
   if (value == "")
      value = "(empty)";
   link.innerHTML = value;
   link.href = path+"?cmd=Set";
   link.onclick = function(){ODBInlineEdit(p,path,bracket);return false;};
   link.onfocus = function(){ODBInlineEdit(p,path,bracket);};
   
   if (p.childNodes.length == 2)
      setTimeout(function(){p.appendChild(link);p.removeChild(p.childNodes[1])}, 10);
   else
      setTimeout(function(){p.appendChild(link);p.removeChild(p.childNodes[0])}, 10);
}

function ODBInlineEditKeydown(event, p, path, bracket)
{
   var keyCode = ('which' in event) ? event.which : event.keyCode;
   
   if (keyCode == 27) {
      /* cancel editing */
      p.ODBsent = true;

      var value = ODBGet(encodeURIComponent(path));
      var link = document.createElement('a');
      if (value == "")
         value = "(empty)";
      link.innerHTML = value;
      link.href = path+"?cmd=Set";
      link.onclick = function(){ODBInlineEdit(p,path,bracket);return false;};
      link.onfocus = function(){ODBInlineEdit(p,path,bracket);};
      
      if (p.childNodes.length == 2)
         setTimeout(function(){p.appendChild(link);p.removeChild(p.childNodes[1])}, 10);
      else
         setTimeout(function(){p.appendChild(link);p.removeChild(p.childNodes[0])}, 10);
   
      return false;
   }

   if (keyCode == 13) {
      ODBFinishInlineEdit(p, path, bracket);
      return false;
   }

   return true;
}

function ODBInlineEdit(p, odb_path, bracket)
{
   var cur_val = ODBGet(encodeURIComponent(odb_path));
   var size = cur_val.length+10;
   var index;
   
   p.ODBsent = false;
   var str = cur_val;
   var width = p.offsetWidth - 10;
   while (str.indexOf('"') >= 0)
      str = str.replace('"', '&quot;');

   if (odb_path.indexOf('[') > 0) {
      index = odb_path.substr(odb_path.indexOf('['));
      if (bracket == 0) {
         p.innerHTML = "<input type=\"text\" size=\""+size+"\" value=\""+str+"\" onKeydown=\"return ODBInlineEditKeydown(event, this.parentNode,\'"+odb_path+"\',"+bracket+");\" onBlur=\"ODBFinishInlineEdit(this.parentNode,\'"+odb_path+"\',"+bracket+");\" >";
         setTimeout(function(){p.childNodes[0].focus();p.childNodes[0].select();}, 10); // needed for Firefox
      } else {
         p.innerHTML = index+"&nbsp;<input type=\"text\" size=\""+size+"\" value=\""+str+"\" onKeydown=\"return ODBInlineEditKeydown(event, this.parentNode,\'"+odb_path+"\',"+bracket+");\" onBlur=\"ODBFinishInlineEdit(this.parentNode,\'"+odb_path+"\',"+bracket+");\" >";
         setTimeout(function(){p.childNodes[1].focus();p.childNodes[1].select();}, 10); // needed for Firefox
      }
   } else {
      
      p.innerHTML = "<input type=\"text\" size=\""+size+"\" value=\""+str+"\" onKeydown=\"return ODBInlineEditKeydown(event, this.parentNode,\'"+odb_path+"\',"+bracket+");\" onBlur=\"ODBFinishInlineEdit(this.parentNode,\'"+odb_path+"\',"+bracket+");\" >";

      setTimeout(function(){p.childNodes[0].focus();p.childNodes[0].select();}, 10); // needed for Firefox
   }
   
   p.style.width = width+"px";
}

/*---- mhttpd functions -------------------------------------*/

function mhttpd_disable_button(button)
{
   button.disabled = true;
}

function mhttpd_enable_button(button)
{
   button.disabled = false;
}

function mhttpd_hide_button(button)
{
   button.style.visibility = "hidden";
   button.style.display = "none";
}

function mhttpd_unhide_button(button)
{
   button.style.visibility = "visible";
   button.style.display = "";
}

function mhttpd_init_overlay(overlay)
{
   mhttpd_hide_overlay(overlay);

   // this element will hide the underlaying web page
   
   overlay.style.zIndex = 10;
   //overlay.style.backgroundColor = "rgba(0,0,0,0.5)"; /*dim the background*/
   overlay.style.backgroundColor = "white";
   overlay.style.position = "fixed";
   overlay.style.top = "0%";
   overlay.style.left = "0%";
   overlay.style.width = "100%";
   overlay.style.height = "100%";

   return overlay.children[0];
}

function mhttpd_hide_overlay(overlay)
{
   overlay.style.visibility = "hidden";
   overlay.style.display = "none";
}

function mhttpd_unhide_overlay(overlay)
{
   overlay.style.visibility = "visible";
   overlay.style.display = "";
}

function mhttpd_page_footer()
{
   /*---- spacer for footer ----*/
   //document.write("<div class=\"push\"></div>\n");

   /*---- footer div ----*/
   document.write("<div id=\"footerDiv\" class=\"footerDiv\">\n");
   mjsonrpc_db_get_values(["/Experiment/Name"]).then(function(rpc) {
      document.getElementById("mhttpd_expt_name").innerHTML = "Experiment " + rpc.result.data[0];
   }).catch(function(error) {
      mjsonrpc_error_alert(error);
   });
   document.write("<div style=\"display:inline; float:left;\" id=\"mhttpd_expt_name\">Experiment %s</div>");
   document.write("<div style=\"display:inline;\"><a href=\"?cmd=Help\">Help</a></div>");
   document.write("<div style=\"display:inline; float:right;\" id=\"mhttpd_last_updated\">" + new Date + "</div>");
   document.write("</div>\n");
}

function mhttpd_create_page_handle_create(mouseEvent)
{
   var form = document.getElementsByTagName('form')[0];
   var path = form.elements['odb'].value;
   var type = form.elements['type'].value;
   var name = form.elements['value'].value;
   var arraylength = form.elements['index'].value;
   var stringlength = form.elements['strlen'].value;

   if (path == "/") path = "";

   if (name.length < 1) {
      alert("Name is too short");
      return false;
   }

   if (parseInt(arraylength) < 1) {
      alert("Bad array length: " + arraylength);
      return false;
   }

   if (parseInt(stringlength) < 1) {
      alert("Bad string length " + stringlength);
      return false;
   }

   var param = {};
   param.path = path + "/" + name;
   param.type = parseInt(type);
   if (arraylength>1)
      param.array_length = parseInt(arraylength);
   if (stringlength>0)
      param.string_length = parseInt(stringlength);

   mjsonrpc_db_create([param]).then(function(rpc) {
      var status = rpc.result.status[0];
      if (status == 311) {
         alert("ODB entry with this name already exists.");
      } else if (status != 1) {
         alert("db_create_key() error " + status + ", see MIDAS messages.");
      } else {
         location.search = ""; // reloads the document
      }
   }).catch(function(error) {
      mjsonrpc_error_alert(error);
      location.search = ""; // reloads the document
   });

   return false;
   
   var result = JSON.parse(ODBMCreate([ path + "/" + name ], [ type ], [ arraylength ], [ stringlength ]));

   if (result[0] == 311) {
      alert("ODB entry with this name already exists.");
   } else if (result[0] != 1) {
      alert("ODBMCreate() error " + result + ", and that's all we know.");
   } else {
      location.search = ""; // reloads the document
   }
   //window.reload();
   return false;
}

function mhttpd_create_page_handle_cancel(mouseEvent)
{
   location.search = ""; // reloads the document
   return false;
}

function mhttpd_delete_page_handle_delete(mouseEvent)
{
   var form = document.getElementsByTagName('form')[0];
   var path = form.elements['odb'].value;

   if (path == "/") path = "";

   var names = [];
   for (var i=0; ; i++) {
      var n = "name" + i;
      var v = form.elements[n];
      if (v == undefined) break;
      if (v == undefined) break;
      if (v.checked)
         names.push(path + "/" + v.value);
   }

   if (names.length < 1) {
      alert("Please select at least one ODB entry to delete.");
      return false;
   }

   //alert(names);

   var params = {};
   params.paths = names;
   mjsonrpc_call("db_delete", params).then(function(rpc) {
      var message = "";
      var status = rpc.result.status;
      //alert(JSON.stringify(status));
      for (var i=0; i<status.length; i++) {
         if (status[i] != 1) {
            message += "Cannot delete \"" + rpc.request.params.paths[i] + "\", db_delete_key() status " + status[i] + "\n";
         }
      }
      if (message.length > 0)
         alert(message);
      location.search = ""; // reloads the document
   }).catch(function(error) {
      mjsonrpc_error_alert(error);
      location.search = ""; // reloads the document
   });

   //location.search = ""; // reloads the document
   return false;

   var result = JSON.parse(ODBMDelete(names));

   location.search = ""; // reloads the document

   //window.reload();
   return false;
}

function mhttpd_delete_page_handle_cancel(mouseEvent)
{
   location.search = ""; // reloads the document
   return false;
}

var mhttpd_programs_page_update_timer_id;
var mhttpd_programs_start_stop_overlay;
var mhttpd_programs_start_stop_modal;
var mhttpd_programs_start_stop_timer_id;

function mhttpd_programs_start_stop_poll(msg, name, start_command, count)
{
   //msg.style.textAlign = "left";
   if (start_command) {
      msg.children[0].innerHTML = "Waiting for program \"" + name + "\" to start.";
      msg.children[1].innerHTML = "Start command: " + start_command;
   } else {
      msg.children[0].innerHTML = "Waiting for program \"" + name + "\" to stop.";
      msg.children[1].innerHTML = "";
   }
   mjsonrpc_cm_exist(name, false).then(function(rpc) {
      var done = false;
      if (start_command)
         done = (rpc.result.status == 1);
      else
         done = (rpc.result.status != 1);
      msg.children[2].innerHTML = "cm_exist() status: " + rpc.result.status + " done: " + done;
      msg.children[3].innerHTML = "Will try again: " + count;
      if (done) {
         //mhttpd_hide_overlay(mhttpd_programs_start_stop_overlay);
         //mhttpd_programs_page_update();
         if (start_command)
            msg.children[3].innerHTML = "Program started";
         else
            msg.children[3].innerHTML = "Program stopped";
      } else {
         if (count > 0) {
            mhttpd_programs_start_stop_timer_id = setTimeout(function() { mhttpd_programs_start_stop_poll(msg, name, start_command, count-1); }, 1000);
         } else {
            if (start_command)
               msg.children[3].innerHTML = "Timeout. Program cannot be started by MIDAS. Try to start it in a terminal, run: " + start_command;
            else
               msg.children[3].innerHTML = "Timeout. Program cannot be stopped by MIDAS. Try to kill it with \"kill -KILL\"";
         }
      }
   }).catch(function(error) {
      mjsonrpc_error_alert(error);
      mhttpd_hide_overlay(mhttpd_programs_start_stop_overlay);
   });
}

function mhttpd_programs_page_add_table_entry(table, name, start_command, colspan)
{
   function new_attribute(name, value)
   {
      var att = document.createAttribute(name);
      if (value)
         att.value = value;
      return att;
   }

   var tr = document.createElement("tr");
   tr.setAttributeNode(new_attribute("id", "program " + name));
   var td;
   var input;

   td = document.createElement("td");
   td.setAttributeNode(new_attribute("id", "program name " + name));
   td.setAttributeNode(new_attribute("align", "center"));
   td.setAttributeNode(new_attribute("colspan", colspan));
   td.innerHTML = "<a href='Programs/" + name + "'>"+name+"</a>";
   tr.appendChild(td);

   td = document.createElement("td");
   td.setAttributeNode(new_attribute("id", "program host " + name));
   td.setAttributeNode(new_attribute("align", "center"));
   tr.appendChild(td);

   td = document.createElement("td");
   td.setAttributeNode(new_attribute("id", "program alarm " + name));
   td.setAttributeNode(new_attribute("align", "center"));
   tr.appendChild(td);

   td = document.createElement("td");
   td.setAttributeNode(new_attribute("id", "program autorestart " + name));
   td.setAttributeNode(new_attribute("align", "center"));
   tr.appendChild(td);

   td = document.createElement("td");
   td.setAttributeNode(new_attribute("id", "program control " + name));

   var start_button = document.createElement("button");
   input = start_button;
   input.id = "start " + name;
   input.type = "button";
   input.innerHTML = "Start " + name;
   mhttpd_disable_button(input);
   input.onclick = function() {
      mhttpd_disable_button(start_button);
      mhttpd_unhide_overlay(mhttpd_programs_start_stop_overlay);
      //alert('Start!');
      mjsonrpc_start_program(name).then(function(rpc) {
         mhttpd_programs_start_stop_poll(mhttpd_programs_start_stop_modal, name, start_command, 10);
      }).catch(function(error) {
         mjsonrpc_error_alert(error);
         mhttpd_hide_overlay(mhttpd_programs_start_stop_overlay);
      });
   };

   td.appendChild(input);

   // need spacer beteen the buttons
   //td.appendChild("x&nbspx");

   var stop_button = document.createElement("button");
   input = stop_button;
   input.id = "stop " + name;
   input.type = "button";
   input.innerHTML = "Stop " + name;
   mhttpd_disable_button(input);
   input.onclick = function() {
      mhttpd_disable_button(stop_button);
      mhttpd_unhide_overlay(mhttpd_programs_start_stop_overlay);
      //alert('Stop!');
      mjsonrpc_stop_program(name, false).then(function(rpc) {
         mhttpd_programs_start_stop_poll(mhttpd_programs_start_stop_modal, name, null, 10);
      }).catch(function(error) {
         mjsonrpc_error_alert(error);
         mhttpd_hide_overlay(mhttpd_programs_start_stop_overlay);
      });
   };

   td.appendChild(input);

   tr.appendChild(td);

   table.appendChild(tr);

   return tr;
}

function mhttpd_programs_page_callback(rpc)
{
   document.getElementById('updateStatus').innerHTML = "Processing new data...";

   function set_text(dom_prefix, item, value)
   {
      var e = document.getElementById(dom_prefix + item);
      if (e) e.innerHTML = value;
   }

   function set_color(dom_prefix, item, value)
   {
      var e = document.getElementById(dom_prefix + item);
      if (e) e.style.backgroundColor = value;
   }

   function set_class(dom_prefix, item, value)
   {
      var e = document.getElementById(dom_prefix + item);
      if (e) e.className = value;
   }

   function find_client(clients, name)
   {
      var found;
      var name_lc = name.toLowerCase(name);
      for (var key in clients) {
         var cname = clients[key].name;
         var cname_lc =  cname.toLowerCase();
         var same_name = (cname_lc == name_lc);
         var matching_name = false;

         if (!same_name) {
            var n = cname_lc.search(name_lc);
            if (n==0) {
               matching_name = true;
               // FIXME: check that remaining text is all numbers: for "odbedit", match "odbedit111", but not "odbeditxxx"
            }
         }

         if (same_name || matching_name) {
            if (!found)
               found = new Array();
            found.push(key);
         }
      }
      return found;
   }

   //alert("Hello: " + JSON.stringify(r));

   var programs = rpc.result.data[0];
   var clients  = rpc.result.data[1];
   var alarms   = rpc.result.data[2];

   for (var name in programs) {
      var e = document.getElementById("program " + name);
      var required = programs[name].required;
      var xclients = find_client(clients, name);
      //alert("name " + name + " clients: " + JSON.stringify(xclients));
      if (required || xclients) {
         var start_command = programs[name]["start command"];

         if (!e) {
            e = mhttpd_programs_page_add_table_entry(document.getElementById("xstripeList"), name, start_command, 1);
         }

         e.style.display = '';

         if (xclients) {
            var s = "";
            for (var i=0; i<xclients.length; i++) {
               var key = xclients[i];
               var host = clients[key].host;
               if (host) {
                  if (s.length > 0)
                     s += "<br>";
                  s += "<a href='/System/Clients/"+key+"'>"+host+"</a>";
               }
            }
            set_text("program host ", name, s);
            set_class("program host ", name, "greenLight");
            // enable start/stop buttons
            var e = document.getElementById("start " + name);
            if (e) {
               mhttpd_disable_button(e);
               mhttpd_hide_button(e);
            }
            var e = document.getElementById("stop " + name);
            if (e) {
               mhttpd_enable_button(e);
               mhttpd_unhide_button(e);
            }
         } else {
            set_text("program host ", name, "Not running");
            set_class("program host ", name, "redLight");
            // enable start/stop buttons
            var e = document.getElementById("start " + name);
            if (e && start_command) {
               mhttpd_enable_button(e);
               mhttpd_unhide_button(e);
            }
            var e = document.getElementById("stop " + name);
            if (e) {
               mhttpd_disable_button(e);
               mhttpd_hide_button(e);
            }
         }

         var alarm_class = programs[name]["alarm class"];
         if (alarm_class.length > 0) {
            set_text("program alarm ", name, "<a href='Alarms/Classes/"+ alarm_class + "'>" + alarm_class + "</a>");
            set_class("program alarm ", name, "yellowLight");
         } else {
            set_text("program alarm ", name, "-");
         }

         if (programs[name]["auto restart"])
            set_text("program autorestart ", name, "Yes");
         else
            set_text("program autorestart ", name, "No");
      } else {
         if (e) {
            e.style.display = 'none';
         }
      }
   }

   document.getElementById('mhttpd_last_updated').innerHTML = new Date;
   document.getElementById('updateStatus').innerHTML = "";
}

function mhttpd_programs_page_update()
{
   var paths = [ "/Programs", "/System/Clients", "/Alarms" ];
   document.getElementById('updateStatus').innerHTML = "Requesting new data...";
   mjsonrpc_db_get_values(paths).then(function(rpc) {
      mhttpd_programs_page_callback(rpc);
   }).catch(function(error) {
      document.getElementById('updateStatus').innerHTML = "RPC error...";
   });
   document.getElementById('updateStatus').innerHTML = "Waiting for new data...";
}

function mhttpd_programs_page_update_periodic()
{
   clearTimeout(mhttpd_programs_page_update_timer_id);
   var update_period = 1000;
   mhttpd_programs_page_update();
   mhttpd_programs_page_update_timer_id = setTimeout('mhttpd_programs_page_update_periodic()', update_period);
}

function mhttpd_programs_page()
{
   // this is called when the "programs" page is loaded

   document.write("<td><input type=button value=\'Refresh now\' onClick=\'mhttpd_programs_page_update();\'></input> <tt id='lastUpdated'>lastUpdated</tt> <tt id='updateStatus'>updateStatus</tt></td>\n");

   document.write("<div id=\"mhttpd_programs_start_stop_overlay\">\n");
   document.write("<div>\n");
   document.write("<p>text1</p>\n");
   document.write("<p>text2</p>\n");
   document.write("<p>text3</p>\n");
   document.write("<p>text4</p>\n");
   document.write("<input type=button value=\'Close overlay\' onClick=\'clearTimeout(mhttpd_programs_start_stop_timer_id); mhttpd_hide_overlay(mhttpd_programs_start_stop_overlay); mhttpd_programs_page_update();\'></input>\n");
   document.write("</div>\n");
   document.write("</div>\n");
   
   document.write("<table class=\"subStatusTable\" id=\"xstripeList\">\n");
   document.write("<tr><td colspan=5 class=\"subStatusTitle\">Programs</td></tr>");
   document.write("<tr class=\"titleRow\"><th>Program<th>Running on host<th>Alarm class<th>Autorestart<th>Commands</tr>\n");
   document.write("</table>\n");

   mhttpd_programs_start_stop_overlay = document.getElementById("mhttpd_programs_start_stop_overlay");
   mhttpd_programs_start_stop_modal = mhttpd_init_overlay(mhttpd_programs_start_stop_overlay);
   //mhttpd_unhide_overlay(mhttpd_programs_start_stop_overlay);

   document.write("<script>mhttpd_programs_page_update_periodic();</script>\n");
   mhttpd_page_footer();
}

/*---- message functions -------------------------------------*/

var facility;
var first_tstamp = 0;
var last_tstamp = 0;
var end_of_messages = false;
var n_messages = 0;

function msg_load(f)
{
   facility = f;
   var msg = ODBGetMsg(facility, 0, 100);
   msg_append(msg);
   if (isNaN(last_tstamp))
      end_of_messages = true;
   
   // set message window height to fit browser window
   mf = document.getElementById('messageFrame');
   mf.style.height = window.innerHeight-findPos(mf)[1]-4;
   
   // check for new messages and end of scroll
   window.setTimeout(msg_extend, 1000);
}

function msg_prepend(msg)
{
   var mf = document.getElementById('messageFrame');
   
   for(i=0 ; i<msg.length ; i++) {
      var line = msg[i];
      var t = parseInt(line);
      
      if (line.indexOf(" ") && (t>0 || t==-1))
         line = line.substr(line.indexOf(" ")+1);
      var e = document.createElement("p");
      e.className = "messageLine";
      e.appendChild(document.createTextNode(line));
      
      if (e.innerHTML == mf.childNodes[2+i].innerHTML)
         break;
      mf.insertBefore(e, mf.childNodes[2+i]);
      first_tstamp = t;
      n_messages++;
      
      if (line.search("ERROR]") > 0) {
         e.style.backgroundColor = "red";
         e.style.color = "white";
      } else {
         e.style.backgroundColor = "yellow";
         e.age = new Date()/1000;
         e.style.setProperty("-webkit-transition", "background-color 3s");
         e.style.setProperty("transition", "background-color 3s");
      }
      
   }
}

function msg_append(msg)
{
   var mf = document.getElementById('messageFrame');
   
   for(i=0 ; i<msg.length ; i++) {
      var line = msg[i];
      var t = parseInt(line);
      
      if (t != -1 && t > first_tstamp)
         first_tstamp = t;
      if (t != -1 && (last_tstamp == 0 || t < last_tstamp))
         last_tstamp = t;
      if (line.indexOf(" ") && (t>0 || t==-1))
         line = line.substr(line.indexOf(" ")+1);
      var e = document.createElement("p");
      e.className = "messageLine";
      e.appendChild(document.createTextNode(line));
      if (line.search("ERROR]") > 0) {
         e.style.backgroundColor = "red";
         e.style.color = "white";
      }

      mf.appendChild(e);
      n_messages++;
   }
}

function findPos(obj) {
   var curleft = curtop = 0;
   if (obj.offsetParent) {
      do {
         curleft += obj.offsetLeft;
         curtop += obj.offsetTop;
         } while (obj = obj.offsetParent);
      return [curleft,curtop];
   }
}

function msg_extend()
{
   // set message window height to fit browser window
   mf = document.getElementById('messageFrame');
   mf.style.height = window.innerHeight-findPos(mf)[1]-4;

   // if scroll bar is close to end, append messages
   if (mf.scrollHeight-mf.scrollTop-mf.clientHeight < 2000) {
      if (!end_of_messages) {
         
         if (last_tstamp > 0) {
            var msg = ODBGetMsg(facility, last_tstamp-1, 100);
            if (msg[0] == "")
               end_of_messages = true;
            if (!end_of_messages) {
               msg_append(msg);
            }
         } else {
            // in non-timestamped mode, simple load full message list
            var msg = ODBGetMsg(facility, 0, n_messages+100);
            n_messages = 0;

            var mf = document.getElementById('messageFrame');
            for (i=mf.childNodes.length-1 ; i>1 ; i--)
               mf.removeChild(mf.childNodes[i]);
            msg_append(msg);
         }
      }
   }
   
   // check for new message if time stamping is on
   if (first_tstamp) {
      var msg = ODBGetMsg(facility, first_tstamp, 0);
      msg_prepend(msg);
   }
   
   // remove color of elements
   for (i=2 ; i<mf.childNodes.length ; i++) {
      if (mf.childNodes[i].age != undefined) {
         t = new Date()/1000;
         if (t > mf.childNodes[i].age + 5)
            mf.childNodes[i].style.backgroundColor = "";
      }
   }
   window.setTimeout(msg_extend, 1000);
}

/*---- alarm functions -------------------------------------*/

function alarm_load()
{
    // hide speak button if browser does not support
    try {
	u = new SpeechSynthesisUtterance("Hello");
    } catch (err) {
	document.getElementById('aspeak').style.display = 'none';
	document.getElementById('aspeakLabel').style.display = 'none';
    }
    
    // get options from local storage
    if (typeof(Storage) !== "undefined") {
	if (sessionStorage.alarmSpeak === undefined) 
	    sessionStorage.alarmSpeak = "1";
        document.getElementById("aspeak").checked = (sessionStorage.alarmSpeak == "1");
    }
}

function aspeak_click(t)
{
   if (typeof(Storage) !== "undefined") {
      if (t.checked)
         sessionStorage.alarmSpeak = "1";
      else
         sessionStorage.alarmSpeak = "0";
   }
      
}

function alarm_speak(t)
{
    if (typeof(Storage) !== "undefined") {
	if (sessionStorage.alarmSpeak == "1")  {
	    u = new SpeechSynthesisUtterance(t);
	    window.speechSynthesis.speak(u);
	    }
    } 
}

/*---- chat functions -------------------------------------*/

function chat_kp(e)
{
   key = (e.which) ? e.which : event.keyCode;
   if (key == '13') {
      chat_send();
      return false;
   }
   return true;
}

function rb()
{
   n = document.getElementById('name');
   n.style.backgroundColor = "";
}

function speak_click(t)
{
   if (typeof(Storage) !== "undefined") {
      if (t.checked)
         sessionStorage.chatSpeak = "1";
      else
         sessionStorage.chatSpeak = "0";
   }
      
}

function chat_send()
{
   // check for name
   n = document.getElementById('name');
   if (n.value == "") {
      n.style.backgroundColor = "#FF8080";
      n.style.setProperty("-webkit-transition", "background-color 400ms");
      n.style.setProperty("transition", "background-color 400ms");
      window.setTimeout(rb, 200);
      n.focus();

   } else {
      m = document.getElementById('text');
      
      ODBGenerateMsg(MT_USER, "chat", n.value, m.value);
      
      // store name to local storage
      if (typeof(Storage) !== "undefined")
         sessionStorage.chatName = n.value;

      m.value = "";
      m.focus();
   }
}

function chat_load()
{
   var msg = ODBGetMsg("chat", 0, 100);
   chat_append(msg);
   if (isNaN(last_tstamp))
      end_of_messages = true;
   
   // hide speak button if browser does not support
   try {
      u = new SpeechSynthesisUtterance("Hello");
   } catch (err) {
      document.getElementById('speak').style.display = 'none';
      document.getElementById('speakLabel').style.display = 'none';
   }
   
   // get options from local storage
   if (typeof(Storage) !== "undefined") {
      if (sessionStorage.chatName != undefined)
         document.getElementById('name').value = sessionStorage.chatName;
      if (sessionStorage.chatSpeak != undefined)
         document.getElementById('speak').checked = (sessionStorage.chatSpeak == "1");

   }
   
   chat_reformat();
   
   // check for new messages and end of scroll
   window.setTimeout(chat_extend, 1000);
}

function chat_format(line)
{
   var t = parseInt(line);
   
   if (line.indexOf(" ") && (t>0 || t==-1))
      line = line.substr(line.indexOf(" ")+1);
   
   var name = line.substr(line.indexOf("[")+1, line.indexOf(",")-line.indexOf("[")-1);
   var text = line.substr(line.indexOf("]")+2);
   var time = line.substr(0, 8);
   var date = line.substr(13, 10);
   var e = document.createElement("div");
   
   if (name == document.getElementById('name').value)
      e.className = "chatBubbleMine";
   else
      e.className = "chatBubbleTheirs";
   
   var d1 = document.createElement("div");
   var d2 = document.createElement("div");
   d1.className = "chatName";
   d2.className = "chatMsg";
   d1.appendChild(document.createTextNode(""));
   
   now = new Date();
   if (now.getDate() == parseInt(date.substr(8, 2)))
      d1.innerHTML = name + '&nbsp;(' + time + ')';
   else
      d1.innerHTML = name + '&nbsp;(' + time + '&nbsp;' + date + ')';
   d2.appendChild(document.createTextNode(text));
   e.appendChild(d1);
   e.appendChild(d2);
   
   return e;
}

function chat_prepend(msg)
{
   var mf = document.getElementById('messageFrame');
   
   for(i=0 ; i<msg.length ; i++) {
      var line = msg[i];
      var t = parseInt(line);
      
      var e = chat_format(line);
      
      // stop if this message is already in the list
      if (e.innerHTML == mf.childNodes[2+i*2].innerHTML)
         break;
      
      // insert message
      mf.insertBefore(e, mf.childNodes[2+i*2]);
      
      // insert div element to clear floating
      var d = document.createElement("div");
      d.style.clear = "both";
      mf.insertBefore(d, mf.childNodes[3+i*2]);

      // speak message if checkbox on
      if (document.getElementById('speak').checked && e.className != "chatBubbleMine") {
         u=new SpeechSynthesisUtterance(line.substr(line.indexOf("]")+2));
         window.speechSynthesis.speak(u);
      }
      
      first_tstamp = t;
      n_messages++;
      
      // fading background
      if (e.className == "chatBubbleTheirs") {
         e.style.backgroundColor = "yellow";
         e.age = new Date()/1000;
         e.style.setProperty("-webkit-transition", "background-color 3s");
         e.style.setProperty("transition", "background-color 3s");
      } else {
         e.style.backgroundColor = "#80FF80";
         e.age = new Date()/1000 - 4;
         e.style.setProperty("-webkit-transition", "background-color 3s");
         e.style.setProperty("transition", "background-color 3s");
      }
   }
}

function chat_append(msg)
{
   var mf = document.getElementById('messageFrame');
   
   for(i=0 ; i<msg.length ; i++) {
      
      if (msg[i] == "")
         continue;
      var t = parseInt(msg[i]);
      if (t != -1 && t > first_tstamp)
         first_tstamp = t;
      if (t != -1 && (last_tstamp == 0 || t < last_tstamp))
         last_tstamp = t;

      mf.appendChild(chat_format(msg[i]));
      
      var d = document.createElement("div");
      d.style.clear = "both";
      mf.appendChild(d);
      
      n_messages++;
   }
}

function chat_reformat()
{
   // set message window height to fit browser window
   mf = document.getElementById('messageFrame');
   mf.style.height = window.innerHeight-findPos(mf)[1]-4;

   // check for reformat of messages if name is given
   for (i=2 ; i<mf.childNodes.length ; i+=2) {
      var b = mf.childNodes[i];
      
      var n = b.childNodes[0].innerHTML;
      if (n.indexOf('&'))
         n = n.substr(0, n.indexOf('&'));
      
      if (n == document.getElementById('name').value)
         b.className = "chatBubbleMine";
      else
         b.className = "chatBubbleTheirs";
   }
}

function chat_extend()
{
   // if scroll bar is close to end, append messages
   mf = document.getElementById('messageFrame');
   if (mf.scrollHeight-mf.scrollTop-mf.clientHeight < 2000) {
      if (!end_of_messages) {
         
         if (last_tstamp > 0) {
            var msg = ODBGetMsg("chat", last_tstamp-1, 100);
            if (msg[0] == "")
               end_of_messages = true;
            if (!end_of_messages) {
               chat_append(msg);
            }
         } else {
            // in non-timestamped mode, simple load full message list
            var msg = ODBGetMsg("chat", 0, n_messages+100);
            n_messages = 0;
            
            var mf = document.getElementById('messageFrame');
            for (i=mf.childNodes.length-1 ; i>1 ; i--)
               mf.removeChild(mf.childNodes[i]);
            chat_append(msg);
         }
      }
   }

   chat_reformat();
   
   // check for new message if time stamping is on
   if (first_tstamp) {
      var msg = ODBGetMsg("chat", first_tstamp, 0);
      chat_prepend(msg);
   }
   
   // remove color of elements
   for (i=2 ; i<mf.childNodes.length ; i++) {
      if (mf.childNodes[i].age != undefined) {
         t = new Date()/1000;
         if (mf.childNodes[i].age != undefined) {
            if (t > mf.childNodes[i].age + 5)
               mf.childNodes[i].style.backgroundColor = "";
         }
      }
   }
   window.setTimeout(chat_extend, 1000);
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * js-indent-level: 3
 * indent-tabs-mode: nil
 * End:
 */
