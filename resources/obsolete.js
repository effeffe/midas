/********************************************************************\
 
 Name:         obsolete.js
 Created by:   Stefan Ritt
 
 Contents:     Obsolete functions from the JavaScript midas library used by mhttpd
 
\********************************************************************/

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

function getMouseXY(e)
{
   // to activate getMouseXY(), do this:
   // document.onmousemove = getMouseXY;

   try {
      var x = e.pageX;
      var y = e.pageY;
      var p = 'abs: ' + x + '/' + y;
      i = document.getElementById('refimg');
      if (i == null)
         return false;
      document.body.style.cursor = 'crosshair';
      x -= i.offsetLeft;
      y -= i.offsetTop;
      while (i = i.offsetParent) {
         x -= i.offsetLeft;
         y -= i.offsetTop;
      }
      p += '   rel: ' + x + '/' + y;
      window.status = p;
      return true;
   }
   catch (e) {
      return false;
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
