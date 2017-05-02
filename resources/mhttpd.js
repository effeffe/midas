/********************************************************************\
 
 Name:         mhttpd.js
 Created by:   Stefan Ritt
 
 Contents:     JavaScript midas library used by mhttpd

 Note: please load midas.js before loading mhttpd.js
 
\********************************************************************/

var run_state_names = { 1:"Stopped", 2:"Paused", 3:"Running" };

var transition_names = { 1:"Starting run...", 2:"Stopping run...", 4:"Pausing run...", 8:"Resuming run...", 16:"Start abort", 4096:"Deferred" };

//
// convert json dom values to text for display and editing
// this is similar to db_sprintf()
//

function mie_to_string(tid, jvalue)
{
   if (tid == TID_BOOL) {
      if (jvalue)
         return "y";
      else
         return "n";
   }
   
   var t = typeof jvalue;

   if (t == 'number') {
      return "" + jvalue;
   }

   if (tid == TID_DWORD) {
      return parseInt(jvalue) + " (" + jvalue + ")";
   }
   
   if (t == 'string') {
      return jvalue;
   }

   return jvalue + " (" + t + ")";
}

//
// stupid javascript does not have a function
// to escape javascript and html characters
// to make it safe to assign a json string
// to p.innerHTML. What gives? K.O.
//

function mhttpd_escape(s)
{
   var ss = s;

   while (ss.indexOf('"') >= 0)
      ss = ss.replace('"', '&quot;');
   
   while (ss.indexOf('>') >= 0)
      ss = ss.replace('>', '&gt;');
   
   while (ss.indexOf('<') >= 0)
      ss = ss.replace('<', '&lt;');
   
   //console.log("mhttpd_escape: [" + s + "] becomes [" + ss + "]");
   return ss;
}

//
// odb inline edit - make element a link to inline editor
//

function mie_back_to_link(p, path, bracket)
{
   var link = document.createElement('a');
   link.href = path+"?cmd=Set";
   link.innerHTML = "(loading...)";

   mjsonrpc_db_get_values([path]).then(function(rpc) {
      var value = rpc.result.data[0];
      var tid = rpc.result.tid[0];
      var mvalue = mie_to_string(tid, value);
      if (mvalue == "")
         mvalue = "(empty)";
      link.innerHTML = mhttpd_escape(mvalue);
      link.onclick = function(){ODBInlineEdit(p,path,bracket);return false;};
      link.onfocus = function(){ODBInlineEdit(p,path,bracket);};

      // what is this for?!?
      if (p.childNodes.length == 2)
         setTimeout(function(){p.appendChild(link);p.removeChild(p.childNodes[1])}, 10);
      else
         setTimeout(function(){p.appendChild(link);p.removeChild(p.childNodes[0])}, 10);
   }).catch(function(error) {
      mjsonrpc_error_alert(error);
   });
}

//
// odb inline edit - write new value to odb
//

function ODBFinishInlineEdit(p, path, bracket)
{
   var value;
 
   if (p.ODBsent == true)
      return;
   
   if (p.childNodes.length == 2)
      value = p.childNodes[1].value;
   else
      value = p.childNodes[0].value;

   //console.log("mie_write odb [" + path + "] value [" + value + "]");

   mjsonrpc_db_paste([path], [value]).then(function(rpc) {
      //mjsonrpc_debug_alert(rpc);
      p.ODBsent = true;
      mie_back_to_link(p, path, bracket);
   }).catch(function(error) {
      mjsonrpc_error_alert(error);
   });
}

//
// odb inline edit - key-press handler
//

function ODBInlineEditKeydown(event, p, path, bracket)
{
   var keyCode = ('which' in event) ? event.which : event.keyCode;
   
   if (keyCode == 27) {
      /* cancel editing */
      p.ODBsent = true;
      mie_back_to_link(p, path, bracket);
      return false;
   }

   if (keyCode == 13) {
      ODBFinishInlineEdit(p, path, bracket);
      return false;
   }

   return true;
}

//
// odb inline edit - convert link to edit field
//

function mie_link_to_edit(p, odb_path, bracket, cur_val)
{
   var size = cur_val.length+10;
   var index;
   
   p.ODBsent = false;

   var str = mhttpd_escape(cur_val);
   var width = p.offsetWidth - 10;

   if (odb_path.indexOf('[') > 0) {
      index = odb_path.substr(odb_path.indexOf('['));
      if (bracket == 0) {
         p.innerHTML = "<input type=\"text\" size=\""+size+"\" value=\""+str+"\" onKeydown=\"return ODBInlineEditKeydown(event, this.parentNode,\'"+odb_path+"\',"+bracket+");\" onBlur=\"ODBFinishInlineEdit(this.parentNode,\'"+odb_path+"\',"+bracket+");\" >";
         setTimeout(function(){p.childNodes[0].focus();p.childNodes[0].select();}, 10); // needed for Firefox
      } else {
         p.innerHTML = index+"&nbsp;<input type=\"text\" size=\""+size+"\" value=\""+str+"\" onKeydown=\"return ODBInlineEditKeydown(event, this.parentNode,\'"+odb_path+"\',"+bracket+");\" onBlur=\"ODBFinishInlineEdit(this.parentNode,\'"+odb_path+"\',"+bracket+");\" >";

         // what is this for?
         setTimeout(function(){p.childNodes[1].focus();p.childNodes[1].select();}, 10); // needed for Firefox
      }
   } else {
      
      p.innerHTML = "<input type=\"text\" size=\""+size+"\" value=\""+str+"\" onKeydown=\"return ODBInlineEditKeydown(event, this.parentNode,\'"+odb_path+"\',"+bracket+");\" onBlur=\"ODBFinishInlineEdit(this.parentNode,\'"+odb_path+"\',"+bracket+");\" >";

      // what is this for?
      setTimeout(function(){p.childNodes[0].focus();p.childNodes[0].select();}, 10); // needed for Firefox
   }
   
   p.style.width = width+"px";
}

//
// odb inline edit - start editing
//

function ODBInlineEdit(p, odb_path, bracket)
{
   mjsonrpc_db_get_values([odb_path]).then(function(rpc) {
      var value = rpc.result.data[0];
      var tid = rpc.result.tid[0];
      var mvalue = mie_to_string(tid, value);
      mie_link_to_edit(p, odb_path, bracket, mvalue);
   }).catch(function(error) {
      mjsonrpc_error_alert(error);
   });
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

function mhttpd_getParameterByName(name) {
    var match = RegExp('[?&]' + name + '=([^&]*)').exec(window.location.search);
    return match && decodeURIComponent(match[1].replace(/\+/g, ' '));
}

function mhttpd_goto_page(page) {
   window.location.href = '?cmd=' + page; // reloads the page from new URL
   // DOES NOT RETURN
}

function mhttpd_navigation_bar(current_page, path)
{
   document.write("<div id=\"customHeader\">\n");
   document.write("</div>\n");

   document.write("<div class=\"mnavcss\">\n");
   document.write("<table class=\"navigationTable\">\n");
   document.write("<tr><td id=\"navigationTableButtons\">(navigation buttons will be loaded here)\n</td></tr>\n");
   document.write("</table>\n\n");
   document.write("</div>\n");

   //console.log("current_page: " + current_page + ", path: " + path);

   if (!path) {
      path = "";
   }

   mjsonrpc_db_get_values(["/Custom/Header", "/Experiment/Menu", "/Experiment/Menu Buttons"]).then(function(rpc) {
      var custom_header = rpc.result.data[0];
      //alert(custom_header);

      if (custom_header && custom_header.length > 0)
         document.getElementById("customHeader").innerHTML = custom_header;

      var menu    = rpc.result.data[1];
      var buttons = rpc.result.data[2];
      var b = [];

      if (menu) {
         for (var k in menu) {
            var kk = k + "/name";
            if (kk in menu) {
               if (menu[k]) {
                  b.push(menu[kk]);
               }
            }
         }
      } else if (buttons && buttons.length > 0) {
         b = buttons.split(",");
      }

      if (!b || b.length < 1) {
         b = [ "Status", "ODB", "Messages", "Chat", "ELog", "Alarms", "Programs", "History", "MSCB", "Sequencer", "Config", "Example", "Help" ];
      }

      var html = "";
      
      for (var i=0; i<b.length; i++) {
         var bb = b[i].trim();
         var cc = "mnavcss navButton";
         if (bb == current_page) {
            cc = "mnavcss mnavselcss navButtonSel";
         }
         html += "<input type=button name=cmd value=\""+bb+"\" class=\""+cc+"\" onclick=\"window.location.href=\'"+path+"?cmd="+bb+"\';return false;\">\n";
      }
      document.getElementById("navigationTableButtons").innerHTML = html;
   }).catch(function(error) {
      mjsonrpc_error_alert(error);
   });
}

function mhttpd_page_footer()
{
   /*---- spacer for footer ----*/
   //document.write("<div class=\"push\"></div>\n");

   /*---- footer div ----*/
   document.write("<div id=\"footerDiv\" class=\"mfootercss footerDiv\">\n");
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
}

function mhttpd_delete_page_handle_cancel(mouseEvent)
{
   location.search = ""; // reloads the document
   return false;
}

function mhttpd_start_run()
{
   mhttpd_goto_page("Start"); // DOES NOT RETURN
}

function mhttpd_stop_run()
{
   var flag = confirm('Are you sure to stop the run?');
   if (flag == true) {
      mjsonrpc_call("cm_transition", {"transition":"TR_STOP"}).then(function(rpc) {
         //mjsonrpc_debug_alert(rpc);
         if (rpc.result.status != 1) {
            throw new Error("Cannot stop run, cm_transition() status " + rpc.result.status + ", see MIDAS messages");
         }
         mhttpd_goto_page("Transition"); // DOES NOT RETURN
      }).catch(function(error) {
         mjsonrpc_error_alert(error);
      });
   }
}

function mhttpd_pause_run()
{
   var flag = confirm('Are you sure to pause the run?');
   if (flag == true) {
      mjsonrpc_call("cm_transition", {"transition":"TR_PAUSE"}).then(function(rpc) {
         //mjsonrpc_debug_alert(rpc);
         if (rpc.result.status != 1) {
            throw new Error("Cannot pause run, cm_transition() status " + rpc.result.status + ", see MIDAS messages");
         }
         mhttpd_goto_page("Transition"); // DOES NOT RETURN
      }).catch(function(error) {
         mjsonrpc_error_alert(error);
      });
   }
}


function mhttpd_resume_run()
{
   var flag = confirm('Are you sure to resume the run?');
   if (flag == true) {
      mjsonrpc_call("cm_transition", {"transition":"TR_RESUME"}).then(function(rpc) {
         //mjsonrpc_debug_alert(rpc);
         if (rpc.result.status != 1) {
            throw new Error("Cannot resume run, cm_transition() status " + rpc.result.status + ", see MIDAS messages");
         }
         mhttpd_goto_page("Transition"); // DOES NOT RETURN
      }).catch(function(error) {
         mjsonrpc_error_alert(error);
      });
   }
}

function mhttpd_cancel_transition()
{
   var flag = confirm('Are you sure to cancel the currently active run transition?');
   if (flag == true) {
      mjsonrpc_call("db_paste", {"paths":["/Runinfo/Transition in progress"], "values":[0]}).then(function(rpc) {
         //mjsonrpc_debug_alert(rpc);
         if (rpc.result.status != 1) {
            throw new Error("Cannot cancel transition, db_paste() status " + rpc.result.status + ", see MIDAS messages");
         }
         mhttpd_goto_page("Transition"); // DOES NOT RETURN
      }).catch(function(error) {
         mjsonrpc_error_alert(error);
      });
   }
}

function mhttpd_reset_alarm(alarm_name)
{
   mjsonrpc_call("al_reset_alarm", { "alarms" : [ alarm_name ] }).then(function(rpc) {
      //mjsonrpc_debug_alert(rpc);
      if (rpc.result.status != 1 && rpc.result.status != 1004) {
         throw new Error("Cannot reset alarm, status " + rpc.result.status + ", see MIDAS messages");
      }
      location.search = ""; // reloads the document
   }).catch(function(error) {
      mjsonrpc_error_alert(error);
   });
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

/*---- site and session storage ----------------------------*/

function storage_get(name, default_value)
{
   //console.log("storage_get: name [" + name + "], default value [" + default_value + "]");
   try {
      var x = name in localStorage;
      var v = localStorage[name];
      //console.log("storage_get: in: " + x + ", value [" + v + "]");
      if (!x) {
         //console.log("storage_get: name [" + name + "], undefined, default value [" + default_value + "]");
         return default_value;
      } else {
         return localStorage.getItem(name);
      }
   } catch (err) {
      return default_value;
   }
}

function storage_set(name, value)
{
   //console.log("storage_set: name [" + name + "], value [" + value + "]");
   try {
      localStorage.setItem(name, value);
   } catch (err) {
   }
}

function storage_chatSpeak(v)
{
   if (v == true) {
      storage_set("chatSpeak", "1");
   } else if (v == false) {
      storage_set("chatSpeak", "0");
   } else {
      return storage_get("chatSpeak", "0") == "1";
   }
}

function storage_alarmSound(v)
{
   if (v == true) {
      storage_set("alarmSound", "1");
   } else if (v == false) {
      storage_set("alarmSound", "0");
   } else {
      return storage_get("alarmSound", "1") == "1";
   }
}

function storage_alarmSpeak(v)
{
   if (v == true) {
      storage_set("alarmSpeak", "1");
   } else if (v == false) {
      storage_set("alarmSpeak", "0");
   } else {
      return storage_get("alarmSpeak", "1") == "1";
   }
}

/*---- alarm functions -------------------------------------*/

function mhttpd_alarm_play(url)
{
   //console.log("maybePlay: [" + url + "]");
   if (storage_alarmSound()) {
      var audio = new Audio(url);
      audio.play();
   }
}

function mhttpd_alarm_speak(t)
{
   if (storage_alarmSpeak()) {
      var u = new SpeechSynthesisUtterance(t);
      window.speechSynthesis.speak(u);
   }
}

/*---- MTALK messages -------------------------------------*/

function talk_maybeSpeak(tim, msg)
{
   try {
      if (storage_alarmSpeak() && storage_get("lastTalkSpeak", "") != tim) {
         var u = new SpeechSynthesisUtterance(msg);
         window.speechSynthesis.speak(u);
         storage_set("lastTalkSpeak", tim);
      }
   } catch (err) {}
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
   storage_chatSpeak(t.checked);
}

function chat_maybeSpeak(tim, msg)
{
   try {
      if (storage_chatSpeak() && storage_get("lastChatSpeak", "") != tim) {
         var u = new SpeechSynthesisUtterance(msg);
         window.speechSynthesis.speak(u);
         storage_set("lastChatSpeak", tim);
      }
   } catch (err) {}
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
      // store name to local storage
      storage_set("chatName", n.value);

      m = document.getElementById('text');
      
      ODBGenerateMsg(MT_USER, "chat", n.value, m.value);
      
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

   var stored_name = storage_get("chatName", false);
   if (stored_name) {
      document.getElementById('name').value = stored_name;
   }

   document.getElementById('speak').checked = storage_get("chatSpeak", false);

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
   if (name == document.getElementById('name').value)
      d1.className = "chatNameMine";
   else
      d1.className = "chatNameTheirs";
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
      
      if (n == document.getElementById('name').value) {
         b.className = "chatBubbleMine";
         b.childNodes[0].className = "chatNameMine";
      } else {
         b.className = "chatBubbleTheirs";
         b.childNodes[0].className = "chatNameTheirs";
      }
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
