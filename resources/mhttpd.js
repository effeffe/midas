/********************************************************************\

 Name:         mhttpd.js
 Created by:   Stefan Ritt

 Contents:     JavaScript midas library used by mhttpd

 Note: please load midas.js before loading mhttpd.js

 \********************************************************************/

var run_state_names = {1: "Stopped", 2: "Paused", 3: "Running"};

var transition_names = {
   1: "Starting run...",
   2: "Stopping run...",
   4: "Pausing run...",
   8: "Resuming run...",
   16: "Start abort",
   4096: "Deferred"
};

var global_base_url = "";

//
// convert json dom values to text for display and editing
// this is similar to db_sprintf()
//

function mie_to_string(tid, jvalue, format) {
   if (tid == TID_BOOL) {
      if (jvalue)
         return "y";
      else
         return "n";
   }

   if (tid == TID_FLOAT || tid == TID_DOUBLE) {
      if (format && format.indexOf("p") != -1) {
         var p = parseInt(format.substr(format.indexOf("p")+1));
         return jvalue.toPrecision(p);
      }
      if (format && format.indexOf("f") != -1) {
         var p = parseInt(format.substr(format.indexOf("f")+1));
         return jvalue.toFixed(p);
      }
      return jvalue;
   }

   var t = typeof jvalue;

   if (t == 'number') {
      return "" + jvalue;
   }

   if (tid == TID_DWORD || tid == TID_INT || tid == TID_WORD || tid == TID_SHORT || tid == TID_BYTE) {
      if (format == undefined)
         format = "d";
      var str  = "";
      for (i = 0 ; i<format.length ; i++) {
         if (format[i] == "d") {
            if (str.length > 0)
               str += " / " + parseInt(jvalue);
            else
               str = parseInt(jvalue);
         }
         if (format[i] == "x") {
            if (str.length > 0)
               str += " / " + jvalue;
            else
               str = jvalue;
         }
         if (format[i] == "b") {
            var bin = parseInt(jvalue).toString(2);
            if (str.length > 0)
               str += " / " + bin + "b";
            else
               str = bin + "b";
         }
      }

      return str;
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

function mhttpd_escape(s) {
   var ss = s;

   if (typeof s !== 'string')
      return ss;

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

function mie_back_to_link(p, path, bracket) {
   var link = document.createElement('a');
   link.href = path + "?cmd=Set";
   link.innerHTML = "(loading...)";

   mjsonrpc_db_get_values([path]).then(function (rpc) {
      var value = rpc.result.data[0];
      var tid = rpc.result.tid[0];
      var mvalue = mie_to_string(tid, value);
      if (mvalue === "")
         mvalue = "(empty)";
      link.innerHTML = mhttpd_escape(mvalue);
      link.onclick = function () {
         ODBInlineEdit(p, path, bracket);
         return false;
      };
      link.onfocus = function () {
         ODBInlineEdit(p, path, bracket);
      };

      // what is this for?!?
      if (p.childNodes.length == 2)
         setTimeout(function () {
            p.appendChild(link);
            p.removeChild(p.childNodes[1])
         }, 10);
      else
         setTimeout(function () {
            p.appendChild(link);
            p.removeChild(p.childNodes[0])
         }, 10);
   }).catch(function (error) {
      mjsonrpc_error_alert(error);
   });
}

//
// odb inline edit - write new value to odb
//

function ODBFinishInlineEdit(p, path, bracket) {
   var value;

   if (p.ODBsent == true)
      return;

   if (p.childNodes.length == 2)
      value = p.childNodes[1].value;
   else
      value = p.childNodes[0].value;

   //console.log("mie_write odb [" + path + "] value [" + value + "]");

   mjsonrpc_db_set_value(path, value).then(function (rpc) {
      //mjsonrpc_debug_alert(rpc);
      p.ODBsent = true;
      mie_back_to_link(p, path, bracket);
   }).catch(function (error) {
      mjsonrpc_error_alert(error);
   });
}

//
// odb inline edit - key-press handler
//

function ODBInlineEditKeydown(event, p, path, bracket) {
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

function mie_link_to_edit(p, odb_path, bracket, cur_val) {
   var size = cur_val.length + 10;
   var index;

   p.ODBsent = false;

   var str = mhttpd_escape(cur_val);
   var width = p.offsetWidth - 10;

   if (odb_path.indexOf('[') > 0) {
      index = odb_path.substr(odb_path.indexOf('['));
      if (bracket == 0) {
         p.innerHTML = "<input type='text' size='" + size + "' value='" + str +
            "' onKeydown='return ODBInlineEditKeydown(event, this.parentNode,&quot;" +
            odb_path + "&quot;," + bracket + ");' onBlur='ODBFinishInlineEdit(this.parentNode,&quot;" +
            odb_path + "&quot;," + bracket + ");' >";
         setTimeout(function () {
            p.childNodes[0].focus();
            p.childNodes[0].select();
         }, 10); // needed for Firefox
      } else {
         p.innerHTML = index + "&nbsp;<input type='text' size='" + size + "' value='" + str +
            "' onKeydown='return ODBInlineEditKeydown(event, this.parentNode,&quot;" +
            odb_path + "&quot;," + bracket + ");' onBlur='ODBFinishInlineEdit(this.parentNode,&quot;" +
            odb_path + "&quot;," + bracket + ");' >";

         // what is this for?
         setTimeout(function () {
            p.childNodes[1].focus();
            p.childNodes[1].select();
         }, 10); // needed for Firefox
      }
   } else {

      p.innerHTML = "<input type='text' size='" + size + "' value='" + str +
         "' onKeydown='return ODBInlineEditKeydown(event, this.parentNode,&quot;" +
         odb_path + "&quot;," + bracket + ");' onBlur='ODBFinishInlineEdit(this.parentNode,&quot;" +
         odb_path + "&quot;," + bracket + ");' >";

      // what is this for?
      setTimeout(function () {
         p.childNodes[0].focus();
         p.childNodes[0].select();
      }, 10); // needed for Firefox
   }

   p.style.width = width + "px";
}

//
// odb inline edit - start editing
//

function ODBInlineEdit(p, odb_path, bracket) {
   mjsonrpc_db_get_values([odb_path]).then(function (rpc) {
      var value = rpc.result.data[0];
      var tid = rpc.result.tid[0];
      var mvalue = mie_to_string(tid, value);
      mie_link_to_edit(p, odb_path, bracket, mvalue);
   }).catch(function (error) {
      mjsonrpc_error_alert(error);
   });
}

/*---- mhttpd functions -------------------------------------*/

function mhttpd_disable_button(button) {
   button.disabled = true;
}

function mhttpd_enable_button(button) {
   button.disabled = false;
}

function mhttpd_hide_button(button) {
   button.style.visibility = "hidden";
   button.style.display = "none";
}

function mhttpd_unhide_button(button) {
   button.style.visibility = "visible";
   button.style.display = "";
}

function mhttpd_init_overlay(overlay) {
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

function mhttpd_hide_overlay(overlay) {
   overlay.style.visibility = "hidden";
   overlay.style.display = "none";
}

function mhttpd_unhide_overlay(overlay) {
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

function mhttpd_navigation_bar(current_page, path) {
   document.write("<div id='customHeader'>\n");
   document.write("</div>\n");

   document.write("<div class='mnav'>\n");
   document.write("  <table>\n");
   document.write("    <tr><td id='navigationTableButtons'></td></tr>\n");
   document.write("  </table>\n\n");
   document.write("</div>\n");

   if (!path)
      path = "";

   if (localStorage.mNavigationButtons != undefined) {
      document.getElementById("navigationTableButtons").innerHTML = localStorage.mNavigationButtons;
      var button = document.getElementById("navigationTableButtons").children;
      for (var i = 0; i < button.length; i++)
         if (button[i].value == current_page)
            button[i].className = "mnav mnavsel navButtonSel";
         else
            button[i].className = "mnav navButton";
      return;
   }

   mjsonrpc_db_get_values(["/Custom/Header", "/Experiment/Menu", "/Experiment/Menu Buttons"]).then(function (rpc) {
      var custom_header = rpc.result.data[0];

      if (custom_header && custom_header.length > 0)
         document.getElementById("customHeader").innerHTML = custom_header;

      var menu = rpc.result.data[1];
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
         b = ["Status", "ODB", "Messages", "Chat", "ELog", "Alarms", "Programs", "History", "MSCB", "Sequencer", "Config", "Help"];
      }

      var html = "";

      for (var i = 0; i < b.length; i++) {
         var bb = b[i].trim();
         var cc = "mnav navButton";
         if (bb == current_page) {
            cc = "mnav mnavsel navButtonSel";
         }
         html += "<input type=button name=cmd value='" + bb + "' class='" + cc + "' onclick='window.location.href=\'" + path + "?cmd=" + bb + "\';return false;'>\n";
      }
      document.getElementById("navigationTableButtons").innerHTML = html;

      // cache navigation buttons in browser local storage
      localStorage.setItem("mNavigationButtons", html);

   }).catch(function (error) {
      mjsonrpc_error_alert(error);
   });
}

function mhttpd_toggle_menu() {
   var m = document.getElementById("msidenav");

   if (m.initialWidth == undefined)
      m.initialWidth = m.clientWidth;

   if (m.style.width == "0px") {
      m.style.width = m.initialWidth + "px";
      document.getElementById("mmain").style.marginLeft = m.initialWidth + "px";
   } else {
      m.style.width = "0";
      document.getElementById("mmain").style.marginLeft = "0";
   }
}

var mhttpd_refresh_id;
var mhttpd_refresh_interval;
var mhttpd_spinning_wheel;

function mhttpd_init(current_page, interval, callback) {
   /*
    This funciton should be called from custom pages to initialize all ODB tags and refresh
    them periodically every "interval" in ms

    ODB Tags:

    <body class="mcss" onload="mhttpd_init('Test', 1000)">
    ...
    <div name="modbvalue" data-odb-path="/Runinfo/Run number" data-odb-editable="1"></div>
    ...

    If the attribute data-odb-editable is set to "1", the value can be changed in-line by clicking at it.

    ODB Buttons:
    <button name="modbbutton" class="modbbutton" data-odb-path="/Runinfo/Run number" data-odb-value="1"></button>

    Pressing this button sets a value in the ODB.
    */

   // initialize URL
   url = mhttpd_getParameterByName("URL");
   if (url)
      mjsonrpc_set_url(url);

   // create header
   var h = document.getElementById("mheader");
   if (h !== undefined) {
      h.style.display = "flex";
      h.innerHTML =
         "<div style='display:inline-block; flex:none;'>" +
         "<span class='mmenuitem' style='padding: 10px;margin-right: 20px;' onclick='mhttpd_toggle_menu()'>&#9776;</span>" +
         "<span id='mheader_expt_name'></span>" +
         "</div>" +

         "<div style='flex:auto;'>" +
         "  <div id='mheader_message'></div>" +
         "</div>" +

         "<div style='display: inline; flex:none;'>" +
         "  <div id='mheader_alarm'>&nbsp;</div>" +
         "  <div style='display: inline; font-size: 75%; margin-right: 10px' id='mheader_last_updated'></div>" +
         "</div>";
   }

   mhttpd_resize_sidenav();
   window.addEventListener('resize', mhttpd_resize_sidenav);

   // put error header in front of header
   var d = document.createElement('div');
   d.id = 'mheader_error';
   h.parentNode.insertBefore(d, h);

   // update header and menu
   if (document.getElementById("msidenav") !== undefined) {

      // get it from session storage cache if present
      if (sessionStorage.msidenav !== undefined && sessionStorage.mexpname !== undefined) {
         var menu = document.getElementById("msidenav");
         menu.innerHTML = sessionStorage.msidenav;
         var item = menu.children;
         for (var i = 0; i < item.length; i++) {
            if (item[i].className !== "mseparator") {
               if (item[i].innerHTML === current_page)
                  item[i].className = "mmenuitem mmenuitemsel";
               else
                  item[i].className = "mmenuitem";
            }
         }
         document.getElementById("mheader_expt_name").innerHTML = sessionStorage.mexpname;

         // now the side navigation has its full width, adjust the main body and make it visible
         var m = document.getElementById("mmain");
         if (m !== undefined) {
            m.style.marginLeft = document.getElementById("msidenav").clientWidth + "px";
            m.style.opacity = 1;
         }
      }

      // request it from server, since it might have changed
      mjsonrpc_db_get_values(["/Experiment/Base URL", "/Experiment/Name", "/Experiment/Menu", "/Experiment/Menu Buttons",
         "/Custom", "/Script", "/Alias"]).then(function (rpc) {

         var base_url = rpc.result.data[0];
         var expt_name = rpc.result.data[1];
         var menu = rpc.result.data[2];
         var buttons = rpc.result.data[3];
         var custom = rpc.result.data[4];
         var script = rpc.result.data[5];
         var alias = rpc.result.data[6];

         document.getElementById("mheader_expt_name").innerHTML = expt_name;
         sessionStorage.setItem("mexpname", expt_name);

         // check for base URL
         if (base_url === null) {
            base_url = "/";
         }
         if (base_url.slice(-1) !== "/")
            base_url += "/";

         global_base_url = base_url;

         // preload spinning wheel for later use
         if (mhttpd_spinning_wheel == undefined) {
            mhttpd_spinning_wheel = new Image();
            mhttpd_spinning_wheel.src = global_base_url + "spinning-wheel.gif";
         }

         // menu buttons
         var b = [];
         if (menu) {
            for (var k in menu) {
               if (k.indexOf('/') >= 0) // skip <key>/last_written and <key>/name
                  continue;
               if (menu[k]) // show button if not disabled
                  b.push(menu[k + "/name"]);
            }
         } else if (buttons && buttons.length > 0) {
            b = buttons.split(",");
         }

         if (!b || b.length < 1) {
            b = ["Status", "ODB", "Messages", "Chat", "ELog", "Alarms", "Programs", "History", "MSCB", "Sequencer", "Config", "Help"];
         }

         var html = "";

         for (var i = 0; i < b.length; i++) {
            var bb = b[i].trim();
            var cc = "mmenuitem";
            if (bb === current_page) {
               cc += " mmenuitemsel";
            }
            html += "<div class='" + cc + "'><a href='" + base_url + "?cmd=" + bb + "' class='mmenulink'>" + bb + "</a></div>\n";
         }

         // custom
         if (custom !== null && Object.keys(custom).length > 0) {
            // add separator
            html += "<div class='mseparator'></div>\n";

            for (var b in custom) {
               if (b.indexOf('/') >= 0) // skip <key>/last_written and <key>/name
                  continue;
               cc = "mmenuitem";
               if (custom[b + "/name"] === current_page)
                  cc += " mmenuitemsel";
               if (b === "path")
                  continue;
               var l = custom[b + "/name"];
               if (l.substr(-1) == '!')
                  continue;
               if (l.substr(-1) == '&')
                  l = l.slice(0, -1);
               html += "<div class='" + cc + "'><a href='" + base_url + "CS/" + custom[b + "/name"] + "' class='mmenulink'>" + l + "</a></div>\n";
            }
         }

         // script
         if (script !== null && Object.keys(script).length > 0) {
            // add separator
            html += "<div class='mseparator'></div>\n";

            for (var b in script) {
               if (b.indexOf('/') >= 0) // skip <key>/last_written and <key>/name
                  continue;
               var n = script[b + "/name"];
               html += "<div class='mmenuitem'><a href='?script=" + b + "' class='mmenulink'>" + n + "</a></div>\n";
            }

         }

         // alias
         if (alias !== null && Object.keys(alias).length > 0) {
            // add separator
            html += "<div class='mseparator'></div>\n";

            for (var b in alias) {
               if (b.indexOf('/') >= 0) // skip <key>/last_written and <key>/name
                  continue;
               var n = alias[b + "/name"];
               if (n.substr(n.length - 1) === "&") {
                  n = n.substr(0, n.length - 1);
                  html += "<div class='mmenuitem'><a href='" + alias[b] + "' class='mmenulink' target='_blank'>" + n + "&#8599;</a></div>\n";
               } else {
                  html += "<div class='mmenuitem'><a href='" + alias[b] + "' class='mmenulink'>" + n + "</a></div>\n";
               }
            }

         }

         // dummy spacer to fix scrolling to the bottom, must be at least header height
         html += "<div style='height: 64px;'></div>\n";

         document.getElementById("msidenav").innerHTML = html;

         // re-adjust size of mmain element if menu has changed
         var m = document.getElementById("mmain");
         if (m !== undefined) {
            m.style.marginLeft = document.getElementById("msidenav").clientWidth + "px";
            m.style.opacity = 1;
         }

         // cache navigation buttons in browser local storage
         sessionStorage.setItem("msidenav", html);


      }).then(function () {
         if (callback !== undefined)
            callback();
      }).catch(function (error) {
         mjsonrpc_error_alert(error);
      });
   }

   // go through all name="modb" tags
   var modb = document.getElementsByName("modb");
   for (var i = 0; i < modb.length; i++) {
      // nothing needs to be done here
   }

   // go through all name="modbvalue" tags
   var modbvalue = document.getElementsByName("modbvalue");
   for (var i = 0; i < modbvalue.length; i++) {
      var o = modbvalue[i];
      var loading = "(Loading " + modbvalue[i].dataset.odbPath + " ...)";
      if (o.dataset.odbEditable) {

         // add event handler if tag is editable
         var link = document.createElement('a');
         link.href = "#";
         link.innerHTML = loading;
         link.onclick = function () {
            ODBInlineEdit(this.parentElement, this.parentElement.dataset.odbPath);
         };
         link.onfocus = function () {
            ODBInlineEdit(this.parentElement, this.parentElement.dataset.odbPath);
         };

         o.appendChild(link);
      } else {
         // just display "loading" text, tag will be updated during mhttpd_refresh()
         o.innerHTML = loading;
      }
   }

   // go through all name="modbcheckbox" tags
   var modbcheckbox = document.getElementsByName("modbcheckbox");
   for (var i = 0; i < modbcheckbox.length; i++) {
      modbcheckbox[i].onclick = function () {
         mjsonrpc_db_set_value(this.dataset.odbPath, this.checked ? 1 : 0);
         mhttpd_refresh();
      };
   }

   // go through all name="modbbox" tags
   var modbbox = document.getElementsByName("modbbox");
   for (var i = 0; i < modbbox.length; i++) {
      modbbox[i].style.border = "1px solid #808080";
   }

   // attach "set" function to all ODB buttons
   var modbbutton = document.getElementsByName("modbbutton");
   for (var i = 0; i < modbbutton.length; i++)
      modbbutton[i].onclick = function () {
         mjsonrpc_db_set_value(this.dataset.odbPath, this.dataset.odbValue);
         mhttpd_refresh();
      };

   // replace all horizontal bars with proper <div>'s
   var mbar = document.getElementsByName("modbhbar");
   for (var i = 0; i < mbar.length; i++) {
      mbar[i].style.display = "inline-block";
      if (mbar[i].style.position === "")
         mbar[i].style.position = "relative";
      mbar[i].style.border = "1px solid #808080";
      var color = mbar[i].dataset.color;
      mbar[i].innerHTML = "<div style='background-color:" + color + ";" +
         "width:0;height:"+ mbar[i].clientHeight+"px;"+
         "position:relative; display:inline-block;border-right:1px solid #808080'>&nbsp;</div>";
   }

   // replace all vertical bars with proper <div>'s
   var mbar = document.getElementsByName("modbvbar");
   for (var i = 0; i < mbar.length; i++) {
      mbar[i].style.display = "inline-block";
      if (mbar[i].style.position === "")
         mbar[i].style.position = "relative";
      mbar[i].style.border = "1px solid #808080";
      var color = mbar[i].dataset.color;
      mbar[i].innerHTML = "<div style='background-color:" + color + "; height:0; width:100%; position:absolute; bottom:0; left:0; display:inline-block; border-top:1px solid #808080'>&nbsp;</div>";
   }

   // replace all thermometers with canvas
   var mth = document.getElementsByName("modbthermo");
   for (var i = 0; i < mth.length; i++) {
      mth[i].style.display = "inline-block";
      if (mth[i].style.position === "")
         mth[i].style.position = "relative";

      var cvs = document.createElement("canvas");
      var w = mth[i].clientWidth;
      var h = mth[i].clientHeight;
      w = Math.floor(w/4)*4; // 2 must be devidable by 4
      cvs.width = w + 1;
      cvs.height = h;
      mth[i].appendChild(cvs);
      mth[i].draw = mhttpd_thermo_draw;
      mth[i].draw();
   }

   // replace all gauges with canvas
   var mg = document.getElementsByName("modbgauge");
   for (var i = 0; i < mg.length; i++) {
      mg[i].style.display = "inline-block";
      if (mg[i].style.position === "")
         mg[i].style.position = "relative";

      var cvs = document.createElement("canvas");
      cvs.width = mg[i].clientWidth;
      cvs.height = mg[i].clientHeight;
      mg[i].appendChild(cvs);
      mg[i].draw = mhttpd_gauge_draw;
      mg[i].draw();
   }

   // store refresh interval and do initial refresh
   if (interval === undefined)
      interval = 1000;
   mhttpd_refresh_interval = interval;
   mhttpd_refresh();
}

function mhttpd_thermo_draw()
{
   var ctx = this.firstChild.getContext("2d");
   ctx.save();
   var w = this.firstChild.width;
   var h = this.firstChild.height;
   ctx.clearRect(0, 0, w, h);
   w = w-1; // space for full circles
   h = h-1;

   if (this.dataset.scale === "1") {
      w = w / 2;
      w = Math.floor(w/4)*4;
   }

   if (this.dataset.value === "1") {
      h = h - 14;
   }

   var x0 = Math.round(w/4*0);
   var x1 = Math.round(w/4*1);
   var x2 = Math.round(w/4*2);
   var x3 = Math.round(w/4*3);

   var v = this.value;
   if (v < this.dataset.minValue)
      v = this.dataset.minValue;
   if (v > this.dataset.maxValue)
      v = this.dataset.maxValue;
   var yt = (h-4*x1) - (h-5*x1)*(v-this.dataset.minValue)/(this.dataset.maxValue-this.dataset.minValue);

   ctx.translate(0.5, 0.5);
   ctx.strokeStyle = "#000000";
   ctx.fillStyle = "#FFFFFF";
   ctx.lineWidth = 1;

   // outer "glass"
   ctx.beginPath();
   ctx.arc(x2, x1, x1, Math.PI, 0);
   ctx.lineTo(x3, h-x1*4);
   ctx.lineTo(x3, h-x1*2*(1+Math.sin(60/360*2*Math.PI)));
   ctx.arc(x2, h-x2, x2, 300/360*2*Math.PI, 240/360*2*Math.PI);
   ctx.lineTo(x1, h-x1*2*(1+Math.sin(60/360*2*Math.PI)));
   ctx.lineTo(x1, x1);
   ctx.stroke();
   if (this.dataset.backgroundColor !== undefined) {
      ctx.fillStyle = this.dataset.backgroundColor;
      ctx.fill();
   }

   // inner "fluid"
   if (this.dataset.color === undefined) {
      ctx.strokeStyle = "#000000";
      ctx.fillStyle = "#000000";
   } else {
      ctx.strokeStyle = this.dataset.color;
      ctx.fillStyle = this.dataset.color;
   }

   ctx.beginPath();
   ctx.moveTo(x1+3, yt);
   ctx.lineTo(x3-3, yt);
   ctx.lineTo(x3-3, h-x2);
   ctx.lineTo(x1+3, h-x2);
   ctx.lineTo(x1+3, yt);
   ctx.stroke();
   ctx.fill();

   ctx.beginPath();
   ctx.arc(x2, h-x2, x2-4, 0, 2*Math.PI);
   ctx.stroke();
   ctx.fill();

   // re-draw outer "glass"
   ctx.strokeStyle = "#000000";
   ctx.fillStyle = "#FFFFFF";
   ctx.lineWidth = 1;
   ctx.beginPath();
   ctx.arc(x2, x1, x1, Math.PI, 0);
   ctx.lineTo(x3, h-x1*4);
   ctx.lineTo(x3, h-x1*2*(1+Math.sin(60/360*2*Math.PI)));
   ctx.arc(x2, h-x2, x2, 300/360*2*Math.PI, 240/360*2*Math.PI);
   ctx.lineTo(x1, h-x1*2*(1+Math.sin(60/360*2*Math.PI)));
   ctx.lineTo(x1, x1);
   ctx.stroke();

   // optional scale
   if (this.dataset.scale === "1") {
      ctx.beginPath();
      ctx.moveTo(x3+x1/2, x1);
      ctx.lineTo(x3+x1, x1);
      ctx.moveTo(x3+x1/2, h-4*x1);
      ctx.lineTo(x3+x1, h-4*x1);
      ctx.stroke();

      ctx.font = "12px sans-serif";
      ctx.fillStyle = "#000000";
      ctx.strokeStyle = "#000000";
      ctx.textBaseline = "middle";
      ctx.fillText(this.dataset.minValue, 4.5*x1, h-4*x1, 3.5*x1);
      ctx.fillText(this.dataset.maxValue, 4.5*x1, x1, 3.5*x1);
   }

   // optional value display
   if (this.dataset.value === "1") {
      ctx.font = "12px sans-serif";
      ctx.fillStyle = "#000000";
      ctx.strokeStyle = "#000000";
      ctx.textBaseline = "bottom";
      ctx.textAlign = "center";
      ctx.fillText(this.value, x2, this.firstChild.height, 4*x1);
   }

   ctx.restore();
}

function mhttpd_gauge_draw()
{
   var ctx = this.firstChild.getContext("2d");
   ctx.save();
   var w = this.firstChild.width;
   var h = this.firstChild.height;
   var y = h;
   if (this.dataset.scale === "1")
      y -= 15;
   else
      y -= 1;
   ctx.clearRect(0, 0, w, h);

   var v = this.value;
   if (v < this.dataset.minValue)
      v = this.dataset.minValue;
   if (v > this.dataset.maxValue)
      v = this.dataset.maxValue;
   v = (v - this.dataset.minValue) / (this.dataset.maxValue - this.dataset.minValue);

   ctx.translate(0.5, 0.5);
   ctx.strokeStyle = "#000000";
   ctx.fillStyle = "#FFFFFF";
   ctx.lineWidth = 1;

   ctx.beginPath();
   ctx.arc(w/2, y, w/2-1, Math.PI, 0);
   ctx.lineTo(w-w/5, y);
   ctx.arc(w/2, y, w/2-w/5, 0, Math.PI, true);
   ctx.lineTo(1, y);
   if (this.dataset.backgroundColor !== undefined) {
      ctx.fillStyle = this.dataset.backgroundColor;
      ctx.fill();
   }
   ctx.stroke();

   // inner bar
   ctx.beginPath();
   ctx.fillStyle = this.dataset.color;
   ctx.strokeStyle = this.dataset.color;
   ctx.arc(w/2, y, w/2-1, Math.PI, (1+v)*Math.PI);
   ctx.arc(w/2, y, w/2-w/5, (1+v)*Math.PI, Math.PI, true);
   ctx.lineTo(1, y);
   ctx.stroke();
   ctx.fill();

   // redraw outer frame
   ctx.strokeStyle = "#000000";
   ctx.fillStyle = "#FFFFFF";
   ctx.lineWidth = 1;
   ctx.beginPath();
   ctx.arc(w/2, y, w/2-1, Math.PI, 0);
   ctx.lineTo(w-w/5, y);
   ctx.arc(w/2, y, w/2-w/5, 0, Math.PI, true);
   ctx.lineTo(1, y);
   ctx.stroke();

   // optional value display
   if (this.dataset.value === "1") {
      ctx.font = "12px sans-serif";
      ctx.fillStyle = "#000000";
      ctx.strokeStyle = "#000000";
      ctx.textBaseline = "bottom";
      ctx.textAlign = "center";
      ctx.fillText(this.value, w/2, y, w);
   }

   // optional scale display
   if (this.dataset.scale === "1") {
      ctx.font = "12px sans-serif";
      ctx.fillStyle = "#000000";
      ctx.strokeStyle = "#000000";
      ctx.textBaseline = "bottom";
      ctx.textAlign = "center";
      ctx.fillText(this.dataset.minValue, 0.1*w, h, 0.2*w);
      ctx.fillText(this.dataset.maxValue, 0.9*w, h, 0.2*w);
   }

   ctx.restore();
}

function mhttpd_resize_sidenav() {
   var h = document.getElementById('mheader');
   var s = document.getElementById('msidenav');
   s.style.top = h.clientHeight + 1 + "px";
   var m = document.getElementById('mmain');
   m.style.paddingTop = h.clientHeight + 1 + "px";
}

function mhttpd_resize_header() {

}

var mhttpd_last_message = 1;

function mhttpd_refresh() {
   if (mhttpd_refresh_id !== undefined)
      window.clearTimeout(mhttpd_refresh_id);

   /* this fuction gets called by mhttpd_init to periodically refresh all ODB tags plus alarms and messages */

   var paths = [];

   // go through all "modb" tags
   var modb = document.getElementsByName("modb");
   for (var i = 0; i < modb.length; i++)
      paths.push(modb[i].dataset.odbPath);

   // go through all "modbvalue" tags
   var modbvalue = document.getElementsByName("modbvalue");
   for (i = 0; i < modbvalue.length; i++)
      paths.push(modbvalue[i].dataset.odbPath);

   var modbcheckbox = document.getElementsByName("modbcheckbox");
   for (i = 0; i < modbcheckbox.length; i++)
      paths.push(modbcheckbox[i].dataset.odbPath);

   var modbbox = document.getElementsByName("modbbox");
   for (i = 0; i < modbbox.length; i++)
      paths.push(modbbox[i].dataset.odbPath);

   var modbhbar = document.getElementsByName("modbhbar");
   for (i = 0; i < modbhbar.length; i++)
      paths.push(modbhbar[i].dataset.odbPath);

   var modbvbar = document.getElementsByName("modbvbar");
   for (i = 0; i < modbvbar.length; i++)
      paths.push(modbvbar[i].dataset.odbPath);

   var modbthermo = document.getElementsByName("modbthermo");
   for (i = 0; i < modbthermo.length; i++)
      paths.push(modbthermo[i].dataset.odbPath);

   var modbgauge = document.getElementsByName("modbgauge");
   for (i = 0; i < modbgauge.length; i++)
      paths.push(modbgauge[i].dataset.odbPath);

   // request ODB contents for all variables
   var req1 = mjsonrpc_make_request("db_get_values", {"paths": paths});

   // request current alarms
   var req2 = mjsonrpc_make_request("get_alarms");

   // request new messages
   var req3 = mjsonrpc_make_request("cm_msg_retrieve", {
      "facility": "midas",
      "time": mhttpd_last_message - 1,
      "min_messages": 100
   });

   // request new char messages
   var req4 = mjsonrpc_make_request("cm_msg_retrieve", {
      "facility": "chat",
      "time": mhttpd_last_message - 1,
      "min_messages": 100
   });

   mjsonrpc_send_request([req1, req2, req3, req4]).then(function (rpc) {

      // update time in header
      var d = new Date();
      var dstr = d.toLocaleString("en-gb", { hour12: false, day: 'numeric', month: 'short', year: 'numeric',
         hour: 'numeric', minute: 'numeric', second: 'numeric', timeZoneName: 'short' });

      if (document.getElementById("mheader_last_updated") !== undefined)
         document.getElementById("mheader_last_updated").innerHTML = dstr;

      var idata = 0;

      for (var i = 0; i < modb.length; i++,idata++) {
         value = rpc[0].result.data[idata];
         if (modb[i].value === undefined)
            modb[i].value = value;
         if (typeof value === 'object') { // subdircectory
            if (modb[i].onchange !== null) {
               modb[i].value = value;
               modb[i].onchange();
            }
         } else {                         // individual value
            if (modb[i].onchange !== null && value !== modb[i].value) {
               modb[i].value = value;
               modb[i].onchange();
            }
         }
      }

      for (i = 0; i < modbvalue.length; i++,idata++) {
         if (rpc[0].result.status[i] === 312) {
            modbvalue[i].innerHTML = "ODB key \""+modbvalue[i].dataset.odbPath+"\" not found";
         } else {
            var value = rpc[0].result.data[idata];
            var tid = rpc[0].result.tid[idata];
            var mvalue = mie_to_string(tid, value, modbvalue[i].dataset.format);
            if (mvalue === "")
               mvalue = "(empty)";
            var html = mhttpd_escape(mvalue);
            if (modbvalue[i].dataset.odbEditable) {
               modbvalue[i].childNodes[0].innerHTML = html;
            } else
               modbvalue[i].innerHTML = html;
         }
         if (modbvalue[i].onchange !== null)
            modbvalue[i].onchange();
      }

      for (i = 0; i < modbcheckbox.length; i++,idata++) {
         value = rpc[0].result.data[idata];
         modbcheckbox[i].checked = (value === 1 || value === true);
         if (modbcheckbox[i].onchange !== null)
            modbcheckbox[i].onchange();
      }

      for (i = 0; i < modbbox.length; i++,idata++) {
         value = rpc[0].result.data[idata];
         if (value === 1 || value === true) {
            modbbox[i].style.backgroundColor = modbbox[i].dataset.color;
         } else {
            if (modbbox[i].dataset.backgroundColor !== undefined)
               modbbox[i].style.backgroundColor = modbbox[i].dataset.backgroundColor;
            else
               modbbox[i].style.backgroundColor = "";
         }
         if (modbbox[i].onchange !== null)
            modbbox[i].onchange();
      }

      for (i = 0; i < modbhbar.length; i++,idata++) {
         value = rpc[0].result.data[idata];
         tid = rpc[0].result.tid[idata];
         mvalue = mie_to_string(tid, value);
         if (mvalue === "")
            mvalue = "(empty)";
         html = mhttpd_escape(""+mvalue);
         modbhbar.value = value;
         if (modbhbar[i].dataset.value === "1")
            modbhbar[i].children[0].innerHTML = html;
         if (modbhbar[i].dataset.minValue === undefined)
            modbhbar[i].dataset.minValue = 0
         if (modbhbar[i].dataset.maxValue === undefined)
            modbhbar[i].dataset.maxValue = 1;
         var percent = Math.round(100 * (value - modbhbar[i].dataset.minValue) /
            (modbhbar[i].dataset.maxValue - modbhbar[i].dataset.minValue));
         if (percent < 0)
            percent = 0;
         if (percent > 100)
            percent = 100;
         modbhbar[i].children[0].style.width = percent + "%";
         if (modbhbar[i].onchange !== null)
            modbhbar[i].onchange();
      }

      for (i = 0; i < modbvbar.length; i++,idata++) {
         value = rpc[0].result.data[idata];
         tid = rpc[0].result.tid[idata];
         mvalue = mie_to_string(tid, value);
         if (mvalue === "")
            mvalue = "(empty)";
         html = mhttpd_escape(""+mvalue);
         modbvbar[i].value = value;
         if (modbvbar[i].dataset.value === "1")
            modbvbar[i].children[0].innerHTML = html;
         if (modbvbar[i].dataset.minValue === undefined)
            modbvbar[i].dataset.minValue = 0;
         if (modbvbar[i].dataset.maxValue === undefined)
            modbvbar[i].dataset.maxValue = 1;
         percent = Math.round(100 * (value - modbvbar[i].dataset.minValue) /
            (modbvbar[i].dataset.maxValue - modbvbar[i].dataset.minValue));
         if (percent < 0)
            percent = 0;
         if (percent > 100)
            percent = 100;
         modbvbar[i].children[0].style.height = percent + "%";
         if (modbvbar[i].onchange !== null)
            modbvbar[i].onchange();
      }

      for (i = 0; i < modbthermo.length; i++,idata++) {
         value = rpc[0].result.data[idata];
         tid = rpc[0].result.tid[idata];
         mvalue = mie_to_string(tid, value);
         if (mvalue === "")
            mvalue = "(empty)";
         html = mhttpd_escape(""+mvalue);
         modbthermo[i].value = value;

         if (modbthermo[i].onchange !== null)
            modbthermo[i].onchange();

         modbthermo[i].draw();
      }

      for (i = 0; i < modbgauge.length; i++,idata++) {
         value = rpc[0].result.data[idata];
         tid = rpc[0].result.tid[idata];
         mvalue = mie_to_string(tid, value);
         if (mvalue === "")
            mvalue = "(empty)";
         html = mhttpd_escape(""+mvalue);
         modbgauge[i].value = value;

         if (modbgauge[i].onchange !== null)
            modbgauge[i].onchange();

         modbgauge[i].draw();
      }

      // update alarm display
      var e = document.getElementById('mheader_alarm');
      if (!rpc[1].result.alarm_system_active) {
         e.innerHTML = "<a href=\"?cmd=Alarms\">Alarms: Off</a>";
         e.className = "mgray mbox";
      } else {
         if (Object.keys(rpc[1].result.alarms) == 0) {
            e.innerHTML = "<a href=\"?cmd=Alarms\">Alarms: None</a>";
            e.className = "mgreen mbox";
         } else {
            var s = "";
            var n = 0;
            for (var a in rpc[1].result.alarms) {
               s += a + ", ";
               n++;
            }
            s = s.slice(0, -2);
            if (n > 1)
               e.innerHTML = "<a href=\"?cmd=Alarms\">Alarms: " + s + "</a>";
            else
               e.innerHTML = "<a href=\"?cmd=Alarms\">Alarm: " + s + "</a>";
            e.className = "mred mbox";

            mhttpd_alarm_play();
         }
      }

      // update messages
      if (rpc[2].result.messages !== undefined) {
         var msg = rpc[2].result.messages.split("\n");
         if (msg[msg.length - 1] === "")
            msg = msg.slice(0, -1);
      } else
         msg = undefined;

      // update chat messages
      if (rpc[3].result.messages !== undefined) {
         var chat = rpc[3].result.messages.split("\n");
         if (chat[chat.length - 1] === "")
            chat = chat.slice(0, -1);
      } else
         chat = undefined;

      mhttpd_message(msg, chat);
      mhttpd_resize_sidenav();

      if (mhttpd_refresh_interval != undefined && mhttpd_refresh_interval > 0)
         mhttpd_refresh_id = window.setTimeout(mhttpd_refresh, mhttpd_refresh_interval);

   }).catch(function (error) {

      if (error.xhr && error.xhr.readyState == 4 && error.xhr.status == 0) {
         mhttpd_error('Connection to server broken. Trying to reconnect&nbsp;&nbsp;');
         document.getElementById("mheader_error").appendChild(mhttpd_spinning_wheel);
         mhttpd_reconnect_id = window.setTimeout(mhttpd_reconnect, 1000);
      } else {
         mjsonrpc_error_alert(error);
      }
   });
}

function mhttpd_reconnect() {
   mjsonrpc_db_ls(["/"]).then(function (rpc) {
      // on successful connection remove error and schedule refresh
      if (document.getElementById("mheader_error")) {
         document.getElementById("mheader_error").innerHTML = "";
         document.getElementById("mheader_error").style.zIndex = 0; // below header
      }
      if (mhttpd_refresh_id != undefined)
         window.clearTimeout(mhttpd_refresh_id);
      mhttpd_refresh_id = window.setTimeout(mhttpd_refresh, mhttpd_refresh_interval);
   }).catch(function (error) {
      mhttpd_reconnect_id = window.setTimeout(mhttpd_reconnect, 1000);
   });
}

window.addEventListener('resize', mhttpd_resize_message);

function mhttpd_resize_message() {
   var d = document.getElementById("mheader_message");
   if (d.currentMessage !== undefined && d.style.display !== 'none')
      mhttpd_fit_message(d.currentMessage);
}

function mhttpd_close_message()
{
   var d = document.getElementById("mheader_message");

   // remember time of messages to suppress
   mhttpdConfigSet('suppressMessageBefore', d.currentMessageT);

   d.style.display = "none";
   mhttpd_resize_sidenav();
}

function mhttpd_fit_message(m)
{
   var d = document.getElementById("mheader_message");
   var cross = "&nbsp;&nbsp;&nbsp;<span style='cursor: pointer;' onclick='mhttpd_close_message();'>&#9587;</span>";
   var link1 = "<span style='cursor: pointer;' onclick='window.location.href=&quot;"+global_base_url+"?cmd=Messages&quot;'>";
   var link2 = "</span>";
   d.style.display = "inline-block";

   // limit message to fit parent element
   var parentWidth = d.parentNode.offsetWidth;
   var s = "";
   for (var i = 0; i < m.length+1; i++) {
      s = m.substr(0, i);
      if (i < m.length - 1)
         s += "...";
      d.innerHTML = link1 + s + link2 + cross;
      if (d.offsetWidth > parentWidth - 30)
         break;
   }

   if (s.substr(-3) === "...") {
      // if message got truncated, remove timestamp and type
      m = m.substr(m.indexOf(']')+1);

      for (var i = 0; i < m.length+1; i++) {
         s = m.substr(0, i);
         if (i < m.length - 1)
            s += "...";
         d.innerHTML = link1 + s + link2 + cross;
         if (d.offsetWidth > parentWidth - 30)
            break;
      }
   }
}

function mhttpd_message(msg, chat) {

   var mTalk = "";
   var mType = "";
   var chatName = "";
   var talkTime = 0;
   var lastMsg = "";
   var lastMsgT = 0;
   var lastChat = "";
   var lastChatT = 0;
   var lastT = 0;

   if (msg != undefined) {
      lastMsg = msg[0].substr(msg[0].indexOf(" ") + 1);
      lastMsgT = parseInt(msg[0]);
   }

   if (chat != undefined) {
      lastChat = chat[0].substr(chat[0].indexOf(" ") + 1);
      lastChatT = parseInt(chat[0]);
      if (chat[0].length > 0)
         mTalk = chat[0].substr(chat[0].indexOf("]") + 2);

      chatName = lastChat.substring(lastChat.indexOf("[") + 1, lastChat.indexOf(","));
      lastChat = lastChat.substr(0, lastChat.indexOf("[")) +
         "<b>" + chatName + ":</b>" +
         lastChat.substr(lastChat.indexOf("]") + 1);
   }

   if (lastChatT > lastMsgT) {
      var m = lastChat;
      var c = "var(--mblue)";
      mType = "USER";
      talkTime = lastChatT;
      lastT = lastChatT;
   } else {
      m = lastMsg;
      c = "var(--myellow)";
      mTalk = lastMsg.substr(lastMsg.indexOf("]") + 1);
      mType = m.substring(m.indexOf(",") + 1, m.indexOf("]"));
      talkTime = lastMsgT;
      lastT = lastMsgT;
   }

   if (m !== "") {
      var d = document.getElementById("mheader_message");
      if (d !== undefined && d.currentMessage !== m &&
         (mhttpdConfig().suppressMessageBefore == undefined || lastT > mhttpdConfig().suppressMessageBefore)) {

         if (mType === "USER" && mhttpdConfig().displayChat  ||
             mType === "TALK" && mhttpdConfig().displayTalk  ||
             mType === "ERROR" && mhttpdConfig().displayError ||
             mType === "INFO" && mhttpdConfig().displayInfo) {

            var first = (d.currentMessage === undefined);
            d.currentMessage = m; // store full message in user-defined attribute
            d.currentMessageT = lastMsgT; // store message time in user-defined attribute

            mhttpd_fit_message(m);
            d.age = new Date() / 1000;

            if (first) {
               if (m.search("ERROR]") > 0) {
                  d.style.backgroundColor = "var(--mred)";
                  d.style.color = "white";
               }
            } else {

               // manage backgroud color (red for errors, fading yellow for others)
               if (m.search("ERROR]") > 0) {
                  d.style.removeProperty("-webkit-transition");
                  d.style.removeProperty("transition");
                  d.style.backgroundColor = "var(--mred)";
               } else {
                  d.age = new Date() / 1000;
                  d.style.removeProperty("-webkit-transition");
                  d.style.removeProperty("transition");
                  d.style.backgroundColor = c;
                  setTimeout(function () {
                     d.style.setProperty("-webkit-transition", "background-color 3s", "");
                     d.style.setProperty("transition", "background-color 3s", "");
                  }, 10);
               }
            }
         }

         if (mTalk !== "") {
            if (mType === "USER" && mhttpdConfig().speakChat) {
               // do not speak own message
               if (document.getElementById("chatName") == undefined || document.getElementById("chatName").value != chatName)
                  mhttpd_speak(talkTime, mTalk);
            } else if (mType === "TALK" && mhttpdConfig().speakTalk) {
               mhttpd_speak(talkTime, mTalk);
            } else if (mType === "ERROR" && mhttpdConfig().speakError) {
               mhttpd_speak(talkTime, mTalk);
            } else if (mType === "INFO" && mhttpdConfig().speakInfo) {
               mhttpd_speak(talkTime, mTalk);
            }
         }
      }
      var t = new Date() / 1000;
      if (t > d.age + 5 && d.style.backgroundColor === "var(--myellow)")
         d.style.backgroundColor = "var(--mgray)";
   }
}

function mhttpd_error(error) {
   var d = document.getElementById("mheader_error");
   if (d !== undefined) {
      error += "<div style='display: inline; float: right; padding-right: 10px; cursor: pointer;' onclick='document.getElementById(&quot;mheader_error&quot;).style.zIndex = 0;'>&#9587;</div>";
      d.innerHTML = error;
      d.style.zIndex = 3; // above header
   }
}

function mhttpd_create_page_handle_create(mouseEvent) {
   var path = "";
   var type = "";
   var name = "";
   var arraylength = "";
   var stringlength = "";

   var form = document.getElementsByTagName('form')[0];

   if (form) {
      path = form.elements['odb'].value;
      type = form.elements['type'].value;
      name = form.elements['value'].value;
      arraylength = form.elements['index'].value;
      stringlength = form.elements['strlen'].value;
   } else {
      var e = document.getElementById("odbpath");
      path = JSON.parse(e.innerHTML);
      if (path == "/") path = "";

      type = document.getElementById("create_tid").value;
      name = document.getElementById("create_name").value;
      arraylength = document.getElementById("create_array_length").value;
      stringlength = document.getElementById("create_strlen").value;

      //alert("Path: " + path + " Name: " + name);
   }

   if (path == "/") path = "";

   if (name.length < 1) {
      alert("Name is too short");
      return false;
   }

   var int_array_length = parseInt(arraylength);

   //alert("int_array_length: " + int_array_length);

   if (!int_array_length || int_array_length < 1) {
      alert("Bad array length: " + arraylength);
      return false;
   }

   var int_string_length = parseInt(stringlength);

   if (!int_string_length || int_string_length < 1) {
      alert("Bad string length " + stringlength);
      return false;
   }

   var param = {};
   param.path = path + "/" + name;
   param.type = parseInt(type);
   if (int_array_length > 1)
      param.array_length = int_array_length;
   if (int_string_length > 0)
      param.string_length = int_string_length;

   mjsonrpc_db_create([param]).then(function (rpc) {
      var status = rpc.result.status[0];
      if (status == 311) {
         alert("ODB entry with this name already exists.");
      } else if (status != 1) {
         alert("db_create_key() error " + status + ", see MIDAS messages.");
      } else {
         location.search = ""; // reloads the document
      }
   }).catch(function (error) {
      mjsonrpc_error_alert(error);
      location.search = ""; // reloads the document
   });

   return false;
}

function mhttpd_create_page_handle_cancel(mouseEvent) {
   location.search = ""; // reloads the document
   return false;
}

function mhttpd_delete_page_handle_delete(mouseEvent, xpath) {
   var form = document.getElementsByTagName('form')[0];
   var path;
   var names = [];

   if (form) {
      path = form.elements['odb'].value;

      if (path == "/") path = "";

      for (var i = 0; ; i++) {
         var n = "name" + i;
         var v = form.elements[n];
         if (v == undefined) break;
         if (v == undefined) break;
         if (v.checked)
            names.push(path + "/" + v.value);
      }
   } else {
      var e = document.getElementById("odbpath");
      path = JSON.parse(e.innerHTML);
      if (path == "/") path = "";

      //alert("Path: " + path);

      for (var i = 0; ; i++) {
         var v = document.getElementById("delete" + i);
         if (v == undefined) break;
         if (v == undefined) break;
         if (v.checked) {
            var name = JSON.parse(v.value);
            if (name.length > 0) {
               names.push(path + "/" + name);
            }
         }
      }

      //alert("Names: " + names);
      //return false;
   }

   if (names.length < 1) {
      alert("Please select at least one ODB entry to delete.");
      return false;
   }

   //alert(names);

   var params = {};
   params.paths = names;
   mjsonrpc_call("db_delete", params).then(function (rpc) {
      var message = "";
      var status = rpc.result.status;
      //alert(JSON.stringify(status));
      for (var i = 0; i < status.length; i++) {
         if (status[i] != 1) {
            message += "Cannot delete \"" + rpc.request.params.paths[i] + "\", db_delete_key() status " + status[i] + "\n";
         }
      }
      if (message.length > 0)
         alert(message);
      location.search = ""; // reloads the document
   }).catch(function (error) {
      mjsonrpc_error_alert(error);
      location.search = ""; // reloads the document
   });

   //location.search = ""; // reloads the document
   return false;
}

function mhttpd_delete_page_handle_cancel(mouseEvent) {
   location.search = ""; // reloads the document
   return false;
}

function mhttpd_start_run() {
   mhttpd_goto_page("Start"); // DOES NOT RETURN
}

function mhttpd_stop_run() {
   dlgConfirm('Are you sure to stop the run?', function(flag) {
      if (flag == true) {
         mjsonrpc_call("cm_transition", {"transition": "TR_STOP"}).then(function (rpc) {
            //mjsonrpc_debug_alert(rpc);
            if (rpc.result.status != 1) {
               throw new Error("Cannot stop run, cm_transition() status " + rpc.result.status + ", see MIDAS messages");
            }
            mhttpd_goto_page("Transition"); // DOES NOT RETURN
         }).catch(function (error) {
            mjsonrpc_error_alert(error);
         });
      }

   });
}

function mhttpd_pause_run() {
   dlgConfirm('Are you sure to pause the run?', function(flag) {
      if (flag == true) {
         mjsonrpc_call("cm_transition", {"transition": "TR_PAUSE"}).then(function (rpc) {
            //mjsonrpc_debug_alert(rpc);
            if (rpc.result.status != 1) {
               throw new Error("Cannot pause run, cm_transition() status " + rpc.result.status + ", see MIDAS messages");
            }
            mhttpd_goto_page("Transition"); // DOES NOT RETURN
         }).catch(function (error) {
            mjsonrpc_error_alert(error);
         });
      }
   });
}


function mhttpd_resume_run() {
   dlgConfirm('Are you sure to resume the run?', function(flag) {
      if (flag == true) {
         mjsonrpc_call("cm_transition", {"transition": "TR_RESUME"}).then(function (rpc) {
            //mjsonrpc_debug_alert(rpc);
            if (rpc.result.status != 1) {
               throw new Error("Cannot resume run, cm_transition() status " + rpc.result.status + ", see MIDAS messages");
            }
            mhttpd_goto_page("Transition"); // DOES NOT RETURN
         }).catch(function (error) {
            mjsonrpc_error_alert(error);
         });
      }
   });
}

function mhttpd_cancel_transition() {
   dlgConfirm('Are you sure to cancel the currently active run transition?', function(flag) {
      if (flag == true) {
         mjsonrpc_call("db_paste", {"paths": ["/Runinfo/Transition in progress"], "values": [0]}).then(function (rpc) {
            //mjsonrpc_debug_alert(rpc);
            if (rpc.result.status != 1) {
               throw new Error("Cannot cancel transition, db_paste() status " + rpc.result.status + ", see MIDAS messages");
            }
            mhttpd_goto_page("Transition"); // DOES NOT RETURN
         }).catch(function (error) {
            mjsonrpc_error_alert(error);
         });
      }
   });
}

function mhttpd_reset_alarm(alarm_name) {
   mjsonrpc_call("al_reset_alarm", {"alarms": [alarm_name]}).then(function (rpc) {
      //mjsonrpc_debug_alert(rpc);
      if (rpc.result.status != 1 && rpc.result.status != 1004) {
         throw new Error("Cannot reset alarm, status " + rpc.result.status + ", see MIDAS messages");
      }
   }).catch(function (error) {
      mjsonrpc_error_alert(error);
   });
}

/*---- message functions -------------------------------------*/

var facility;
var first_tstamp = 0;
var last_tstamp = 0;
var end_of_messages = false;
var n_messages = 0;

function msg_load(f) {
   facility = f;
   var msg = ODBGetMsg(facility, 0, 100);
   msg_append(msg);
   if (isNaN(last_tstamp))
      end_of_messages = true;

   // set message window height to fit browser window
   mf = document.getElementById('messageFrame');
   mf.style.height = window.innerHeight - findPos(mf)[1] - 4;

   // check for new messages and end of scroll
   window.setTimeout(msg_extend, 1000);
}

function msg_prepend(msg) {
   var mf = document.getElementById('messageFrame');

   for (i = 0; i < msg.length; i++) {
      var line = msg[i];
      var t = parseInt(line);

      if (line.indexOf(" ") && (t > 0 || t == -1))
         line = line.substr(line.indexOf(" ") + 1);
      var e = document.createElement("p");
      e.className = "messageLine";
      e.appendChild(document.createTextNode(line));

      if (e.innerHTML == mf.childNodes[2 + i].innerHTML)
         break;
      mf.insertBefore(e, mf.childNodes[2 + i]);
      first_tstamp = t;
      n_messages++;

      if (line.search("ERROR]") > 0) {
         e.style.backgroundColor = "red";
         e.style.color = "white";
      } else {
         e.style.backgroundColor = "yellow";
         e.age = new Date() / 1000;
         e.style.setProperty("-webkit-transition", "background-color 3s");
         e.style.setProperty("transition", "background-color 3s");
      }

   }
}

function msg_append(msg) {
   var mf = document.getElementById('messageFrame');

   for (i = 0; i < msg.length; i++) {
      var line = msg[i];
      var t = parseInt(line);

      if (t != -1 && t > first_tstamp)
         first_tstamp = t;
      if (t != -1 && (last_tstamp == 0 || t < last_tstamp))
         last_tstamp = t;
      if (line.indexOf(" ") && (t > 0 || t == -1))
         line = line.substr(line.indexOf(" ") + 1);
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
      return [curleft, curtop];
   }
}

function msg_extend() {
   // set message window height to fit browser window
   mf = document.getElementById('messageFrame');
   mf.style.height = window.innerHeight - findPos(mf)[1] - 4;

   // if scroll bar is close to end, append messages
   if (mf.scrollHeight - mf.scrollTop - mf.clientHeight < 2000) {
      if (!end_of_messages) {

         if (last_tstamp > 0) {
            var msg = ODBGetMsg(facility, last_tstamp - 1, 100);
            if (msg[0] == "")
               end_of_messages = true;
            if (!end_of_messages) {
               msg_append(msg);
            }
         } else {
            // in non-timestamped mode, simple load full message list
            var msg = ODBGetMsg(facility, 0, n_messages + 100);
            n_messages = 0;

            var mf = document.getElementById('messageFrame');
            for (i = mf.childNodes.length - 1; i > 1; i--)
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
   for (i = 2; i < mf.childNodes.length; i++) {
      if (mf.childNodes[i].age != undefined) {
         t = new Date() / 1000;
         if (t > mf.childNodes[i].age + 5)
            mf.childNodes[i].style.backgroundColor = "";
      }
   }
   window.setTimeout(msg_extend, 1000);
}

/*---- site and session storage ----------------------------*/

/*
 Usage:

 flag = mhttpdConfig().speakChat;     // read

 mhttpdConfigSet('speakChat', false); // write individual config

 var c = mhttpdConfig();              // write whole config
 c.speakChat = false;
 c.... = ...;
 mhttpdConfigSetAll(c);


 Saves settings are kept in local storage, which gets
 cleared when the browser session ends. Then the default
 values are returned.
 */

var mhttpd_config_defaults = {
   'chatName': "",

   'displayChat': true,
   'displayTalk': true,
   'displayError': true,
   'displayInfo': false,

   'speakChat': true,
   'speakTalk': true,
   'speakError': false,
   'speakInfo': false,
   'speakVoice': 'Alex',
   'speakVolume': 1,

   'alarmSound': true,
   'alarmSoundFile': 'beep.mp3',
   'alarmRepeat': 60,
   'alarmVolume': 1,

   'var': {
      'lastSpeak': 0,
      'lastAlarm': 0
   },

   'suppressMessageBefore': 0
};

function mhttpdConfig() {
   var c = mhttpd_config_defaults;
   try {
      if (localStorage.mhttpd)
         c = JSON.parse(localStorage.mhttpd);

      // if element has been added to mhttpd_config_defaults, merge it
      if (Object.keys(c).length != Object.keys(mhttpd_config_defaults).length) {
         for (var o in mhttpd_config_defaults)
            if (!(o in c))
               c[o] = mhttpd_config_defaults[o];
      }
   } catch (e) {
   }

   return c;
}

function mhttpdConfigSet(item, value) {
   try {
      var c = mhttpdConfig();
      if (item.indexOf('.') > 0) {
         var c1 = item.substring(0, item.indexOf('.'));
         var c2 = item.substring(item.indexOf('.')+1);
         c[c1][c2] = value;
      } else
         c[item] = value;
      localStorage.setItem('mhttpd', JSON.stringify(c));
   } catch (e) {
   }
}

function mhttpdConfigSetAll(new_config) {
   try {
      localStorage.setItem('mhttpd', JSON.stringify(new_config));
   } catch (e) {
   }
}

/*---- sound and speak functions --------------------------*/

function mhttpd_alarm_play() {
   if (mhttpdConfig().alarmSound && mhttpdConfig().alarmSoundFile) {
      var now = new Date() / 1000;
      if (now > mhttpdConfig().var.lastAlarm + parseFloat(mhttpdConfig().alarmRepeat)) {
         mhttpdConfigSet("var.lastAlarm", now);
         var audio = new Audio(mhttpdConfig().alarmSoundFile);
         audio.volume = mhttpdConfig().alarmVolume;
         audio.play();
      }
   }
}

function mhttpd_speak(time, text) {

   if (!('speechSynthesis' in window))
      return;

   if (mhttpdConfig().speakChat) {
      if (time > mhttpdConfig().var.lastSpeak) {
         mhttpdConfigSet("var.lastSpeak", time);
         var u = new SpeechSynthesisUtterance(text);
         u.voice = speechSynthesis.getVoices().filter(function(voice) { return voice.name == mhttpdConfig().speakVoice; })[0];
         u.volume = mhttpdConfig().speakVolume;
            speechSynthesis.speak(u);
      }
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
