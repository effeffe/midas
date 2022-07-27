/*

   Full ODB browser to be used by odb.html as the standard
   ODB browser or as a key picker with the function

      odb_picker('/', (flag,path) => {
         if (flag)
            console.log(path);
      });

   where the '/' is the starting path of the picker and
   "flag" is true if the OK button has been pressed and
   false if the Cancel button has been pressed. The
   "path" variable then contains the selected ODB key
   with its full path.

   Created by Stefan Ritt on July 21st, 2022

*/

let odb_css = `<style>
       #odb .colHeader {
           font-weight: bold;
       }
       #odb .mtable {
           border-spacing: 0;
       }
       #odb td, th {
           padding: 3px;
           user-select: none;
       }
       #odb img {
           cursor: pointer;
           border: 5px solid #E0E0E0;
           border-radius: 5px;
       }
       #odb img:hover {
           border: 5px solid #C8C8C8;
       }
   </style>`;

let odb_dialogs = `

<!-- Create key dialog -->
<div id="dlgCreate" class="dlgFrame">
   <div class="dlgTitlebar">Create ODB entry</div>
   <div class="dlgPanel">
      <table class="dialogTable" style="border-spacing:10px;">
         <tr>
            <td style="text-align: right">Directory:</td>
            <td style="text-align: left" id="odbCreateDir"></td>
         </tr>
         <tr>
            <td style="text-align: right">Type:</td>
            <td style="text-align: left">
               <select id="odbCreateType"
                       onchange="
                          document.getElementById('odbCreateStrLenTR').style.visibility =
                             (this.value==='12') ? 'visible' : 'hidden';
                       ">
                  <option selected value=7>Integer (32-bit)
                  <option value=9>Float (4 Bytes)
                  <option value=12>String
                  <option value=1>Byte
                  <option value=2>Signed Byte
                  <option value=3>Character (8-bit)
                  <option value=4>Word (16-bit)
                  <option value=5>Short Integer (16-bit)
                  <option value=6>Double Word (32-bit)
                  <option value=8>Boolean
                  <option value=10>Double Float (8 Bytes)
               </select>
            </td>
         </tr>
         <tr>
            <td style="text-align: right">Name:</td>
            <td style="text-align: left"><input type="text" size="20" maxlength="31"
                                                id="odbCreateName" onkeydown="return dlgCreateKeyDown(event, this)"></td>
         </tr>
         <tr>
            <td style="text-align: right">Array size:</td>
            <td style="text-align: left"><input type="text" size="6" value="1" id="odbCreateSize"></td>
         </tr>
         <tr id="odbCreateStrLenTR" style="visibility: hidden">
            <td style="text-align: right">String length:</td>
            <td style="text-align: left"><input type="text" size="6" value="32" id="odbCreateStrLen"></td>
         </tr>
      </table>
      <button class="dlgButtonDefault" onclick="if(do_new_key(this))dlgHide('dlgCreate');">Create</button>
      <button class="dlgButton" onclick="dlgHide('dlgCreate')">Cancel</button>
   </div>
</div>

<!-- Create link dialog -->
<div id="dlgCreateLink" class="dlgFrame">
   <div class="dlgTitlebar">Create ODB link</div>
   <div class="dlgPanel">
      <table class="dialogTable" style="border-spacing:10px;">
         <tr>
            <td style="text-align: right">Directory:</td>
            <td style="text-align: left" id="odbCreateLinkDir"></td>
         </tr>
         <tr>
            <td style="text-align: right">Link name:</td>
            <td style="text-align: left"><input type="text" size="20" maxlength="31"
                                                id="odbCreateLinkName" onkeydown="return dlgCreateLinkKeyDown(event, this)"></td>
         </tr>

         <tr>
            <td style="text-align: right">Link target:</td>
            <td style="text-align: left">
               <input type="text" size="50" maxlength="31"
                      id="odbCreateLinkTarget"
                      onkeydown="return dlgCreateLinkKeyDown(event, this)">
                  <button onclick="pickLinkTarget()">...</button>
            </td>
         </tr>
      </table>
      <button class="dlgButton" onclick="if(do_new_link(this))dlgHide('dlgCreateLink');">Create</button>
      <button class="dlgButton" onclick="dlgHide('dlgCreateLink')">Cancel</button>
   </div>
</div>

<!-- Select file dialog -->
<div id="dlgFileSelect" class="dlgFrame">
   <div class="dlgTitlebar">Select file</div>
   <div class="dlgPanel">
      </br>
      <div style="margin: auto;width:70%">
         <input type="file" id="fileSelector" accept=".json">
      </div>
      </br></br>
   <button class="dlgButton" onclick="loadFileFromSelector(this.parentNode.parentNode);dlgHide('dlgFileSelect');">Upload</button>
   <button class="dlgButton" onclick="dlgHide('dlgFileSelect')">Cancel</button>
   </div>
</div>
`;

function odb_browser(id, path, picker) {

   if (path === undefined) {
      // get path from URL
      let url = new URL(window.location.href);
      path = url.searchParams.get("odb_path");
      if (path === undefined || path === null || path === "")
         path = '/';
      if (path[0] !== '/')
         path = '/' + path;
   }

   // add special styles
   document.head.insertAdjacentHTML("beforeend", odb_css);

   // add special dialogs
   let d = document.createElement("div");
   d.innerHTML = odb_dialogs;
   document.body.appendChild(d);

   // install event handler for browser history popstate events
   window.addEventListener('popstate', (event) => {
      if (event.state)
         window.location.href = "?cmd=ODB&odb_path=" + event.state.path;
   });

   if (!picker) {
      // modify title
      if (document.title.indexOf("ODB") !== -1) {
         if (path === '/')
            document.title = "ODB";
         else
            document.title = "ODB " + path;
      }

      // push current path to history
      let url = window.location.href;
      if (url.search("&odb_path") !== -1)
         url = url.slice(0, url.search("&odb_path"));
      if (path !== '/')
         url += "&odb_path=" + path;
      window.history.pushState({'path': path}, '', url);
   }

   // crate table header
   d = document.getElementById(id);
   let table = document.createElement('TABLE');
   table.className = "mtable";
   let tb = document.createElement('TBODY');
   if (picker) {
      table.style.width = "100%";
      table.style.marginTop = "0";
      table.style.marginBottom = "0";
   }
   table.appendChild(tb);

   // Attach ODB to table body
   tb.odb = {
      picker: picker,
      id: id,
      updateTimer: 0,
      path: path,
      level: 0,
      dataIndex: 0,
      handleColumn : false,
      detailsColumn: false,
      dragSource: undefined,
      dragDestination: undefined,
      dragTargetRow: undefined,
      dragRowContent : undefined,
      key: []
   };

   let tr = document.createElement('TR');
   tb.appendChild(tr);
   let th = document.createElement('TH');
   tr.appendChild(th);
   th.className = "mtableheader";
   th.colSpan = "8";
   th.appendChild(document.createTextNode("Online Database Browser"));
   if (picker)
      tr.style.display = 'none';

   // Dynamic ODB path
   tr = document.createElement('TR');
   tb.appendChild(tr);
   let td = document.createElement('TD');
   td.innerHTML = "";
   td.colSpan = "8";
   tr.appendChild(td);

   tr.addEventListener('click', select_key); // used to deselect any kay

   // Toolbar
   tr = document.createElement('TR');
   tb.appendChild(tr);
   td = document.createElement('TD');
   td.colSpan = "8";
   tr.appendChild(td);
   td.innerHTML =
      '<img src="icons/file-plus.svg" title="Create key  CTRL+K" ' +
      'onclick="new_key(this)">&nbsp;&nbsp;' +
      '<img src="icons/folder-plus.svg" title="Create subdirectory" ' +
      'onclick="new_subdir(this)">&nbsp;&nbsp;' +
      '<img src="icons/link.svg" title="Create link" ' +
      'onclick="new_link(this)">&nbsp;&nbsp;' +
      '<img src="icons/edit-3.svg" title="Rename key" ' +
      'onclick="rename_key(this)">&nbsp;&nbsp;' +
      '<img src="icons/shuffle.svg" title="Reorder keys" ' +
      'onclick="toggle_handles(this)">&nbsp;&nbsp;' +
      '<img src="icons/copy.svg" title="Copy from ODB  Ctrl+C" ' +
      'onclick="odb_copy(this)">&nbsp;&nbsp;' +
      '<img src="icons/clipboard.svg" title="Paste to ODB  Ctrl+V" ' +
      'onclick="odb_paste(this)">&nbsp;&nbsp;' +
      '<img src="icons/download.svg" title="Download ODB" ' +
      'onclick="odb_download(this)">&nbsp;&nbsp;' +
      '<img src="icons/upload.svg" title="Upload ODB" ' +
      'onclick="odb_upload(this)">&nbsp;&nbsp;' +
      '<img src="icons/search.svg" title="Search keys" ' +
      'onclick="search_key(this)">&nbsp;&nbsp;' +
      '<img src="icons/trash-2.svg" title="Delete keys" ' +
      'onclick="odb_delete(this)">&nbsp;&nbsp;' +
      '<img src="icons/more-vertical.svg" title="More menu commands" ' +
      'onclick="more_menu(event)">&nbsp;&nbsp;' +
      '';

   tr.addEventListener('click', select_key); // used to deselect any kay
   if (picker)
      tr.style.display = 'none';

   // Column header
   tr = document.createElement('TR');
   tb.appendChild(tr);
   let a = [ "Handle", "Key", "Value", "Type", "#Val", "Size", "Written", "Mode" ];
   for (const t of a) {
      td = document.createElement('TD');
      td.className = 'colHeader';
      if (t === "Handle") {
         td.setAttribute('name', 'odbHandle');
         td.style.display = tb.odb.handleColumn ? 'table-cell' : 'none';
         td.style.width = "10px";
      } else if (t === "Value") {
         td.setAttribute('name', 'valueHeader');
         td.innerHTML = t;
      } else if (t !== "Key" && t !== "Value") {
         td.innerHTML = t;
         td.setAttribute('name', 'odbExt');
         td.style.display = tb.odb.detailColumn ? 'table-cell' : 'none';
      } else {
         td.innerHTML = t;
      }
      tr.appendChild(td);
   }

   if (!picker)
      tr.childNodes[2].innerHTML +=
         "<div title='Show key details' style='display:inline;float:right'>" +
         "<a id='expRight' href='#' onclick='expand(this);return false;'>&#x21E5;</a>"+
         "</div>";

   tr.childNodes[7].innerHTML +=
      "&nbsp;<div title='Hide key details' style='display:inline;float:right'>" +
      "<a id='expRight' href='#' onclick='expand(this);return false;'>&#x21E4;</a>"+
      "</div>";

   tr.addEventListener('click', select_key); // used to deselect any kay

   d.appendChild(table);

   // install shortcut key handler
   window.addEventListener('keydown', event => {
      global_keydown(event, tb);
   });

   // install global click handler to unselect submenus
   window.addEventListener('click', () => {
      close_submenus();
      unselect_all_keys(tb);
      unselect_all_array_elements(tb);
   });

   odb_update(tb);
}

function global_keydown(event, tb) {
   if (event.target.tagName === 'INPUT')
      return true;

   // Ctrl+C
   if ((event.ctrlKey || event.metaKey) && event.key === 'c') {
      odb_copy(tb);
      return false;
   }

   // Ctrl+V
   if ((event.ctrlKey || event.metaKey) && event.key === 'v') {
      odb_paste(tb);
      return false;
   }

   // Ctrl+K
   if ((event.ctrlKey || event.metaKey) && event.key === 'k') {
      event.preventDefault();
      new_key(tb);
      return false;
   }

   // Backspace
   if (event.key === 'Backspace') {
      odb_delete(tb);
      return false;
   }

   // Escape
   if (event.keyCode === 27)
      return close_submenus();

   return true;
}

function close_submenus() {
   let flag = false;
   // hide submenu if visible
   let m = document.getElementById('moreMenu');
   if (m !== null && m.style.display === 'block') {
      m.style.display = 'none';
      flag = true;
   }

   // hide context menu if visible
   m = document.getElementById('contextMenu');
   if (m !== null && m.style.display === 'block') {
      m.style.display = 'none';
      flag = true;
   }

   return flag;
}

function odb_picker(path, callback, param) {
   let d = document.createElement("div");
   d.className = "dlgFrame";
   d.style.zIndex = "31";
   d.style.width = "400px";
   d.callback = callback;
   d.callbackParam = param;
   d.shouldDestroy = true;

   d.innerHTML = "<div class=\"dlgTitlebar\" id=\"dlgMessageTitle\">" +
      "Please select ODB key</div>" +
      "<div class=\"dlgPanel\" style=\"padding: 2px;text-align: left;\">" +
      "<div id=\"dlgOdbPicker\"></div>" +
      "<div style='text-align: center;'>" +
      "<button class=\"dlgButton\" id=\"dlgMessageButton\" type=\"button\" " +
      " onClick=\"pickerButton(this, true)\">OK</button>" +
      "<button class=\"dlgButton\" id=\"dlgMessageButton\" type=\"button\" " +
      " onClick=\"pickerButton(this, false);\">Cancel</button>" +
      "</div>" +
      "</div>";

   document.body.appendChild(d);

   odb_browser('dlgOdbPicker', '/', true);

   dlgShow(d, true);
   d.style.top = "100px";

   return d;
}

function pickerButton(e, flag) {
   let d = e.parentElement.parentElement.parentElement;
   let path = d.childNodes[1].childNodes[0].childNodes[0].childNodes[0].odb.selectedKey;
   d.callback(flag, path);
   dlgMessageDestroy(e.parentElement);
}

function getOdbTb(e) {
   if (e === null)
      return;
   while (e.tagName !== 'TBODY') {
      e = e.parentNode;
      if (e === null)
         return;
   }
   return e;
}


function inline_edit_keydown(event, p) {
   let keyCode = ('which' in event) ? event.which : event.keyCode;

   if (keyCode === 27) {
      // cancel editing
      p.odbParam.inEdit = false;
      p.innerHTML = p.odbParam.oldhtml;
      return false;
   }

   if (keyCode === 13) {
      inline_edit_finish(p);
      return false;
   }

   return true;
}

function inline_edit_cancel(p) {
   // cancel editing
   if (p.odbParam.inEdit) {
      p.odbParam.inEdit = false;
      p.innerHTML = p.odbParam.oldhtml;
   }
}

function inline_edit_finish(p) {
   // finish editing
   let value = p.childNodes[0].value;
   p.odbParam.inEdit = false;
   p.innerHTML = p.odbParam.oldhtml;
   if (p.odbParam.callback)
      p.odbParam.callback(p, value, p.odbParam.param);
}

function inline_edit(event, p, str, callback, size, param) {
   if (p.odbParam !== undefined && p.odbParam.inEdit)
      return;

   if (event !== undefined)
      event.stopPropagation(); // don't propagate to rename_key()

   p.odbParam = {};
   p.odbParam.param = param;
   p.odbParam.callback = callback;
   p.odbParam.oldhtml = p.innerHTML;
   p.odbParam.inEdit = true;

   if (size === undefined)
      size = str.length;
   if (size === 0)
      size = 10;

   p.innerHTML = "<input type='text' size='" + size + "' value='" + str +
      "' onKeydown='return inline_edit_keydown(event, this.parentNode);'" +
      " onBlur='return inline_edit_cancel(this.parentNode);'>";

   // needed for Firefox
   setTimeout(function () {
      p.childNodes[0].focus();
      p.childNodes[0].select();
   }, 10);
}

function option_edit(event, e, flag) {
   event.stopPropagation();
   e.parentNode.inEdit = flag;
}

function odb_setoption(p) {
   let tr = p.parentNode;
   while (tr.tagName !== 'TR')
      tr = tr.parentNode;
   let tb = getOdbTb(tr);
   let path = tr.odbPath;
   let value = p.value;

   mjsonrpc_db_set_value(path, value).then(() => {
      tb.odb.skip_yellow = true;
      p.parentNode.inEdit = false;
      odb_update(tb);
   }).catch(error => mjsonrpc_error_alert(error));
}

function odb_setall(p, value) {
   // obtain selected array elements
   let tr = p.parentNode;
   while (tr.tagName !== 'TR')
      tr = tr.parentNode;
   let tb = getOdbTb(tr);

   let atr = getArrayTr(tr);

   let n = 0;
   for (const e of atr)
      if (e.odbSelected)
         n++;

   if (n === 0) {
      // set all values
      let path = p.odbParam.param + '[*]';
      mjsonrpc_db_set_value(path, value).then(() =>
         odb_update(tb)
      ).catch(error => mjsonrpc_error_alert(error));
   } else {
      let path = p.odbParam.param;
      path = path.substring(0, path.lastIndexOf('/'));
      let key = p.parentNode.parentNode.key;
      for (let i=0 ; i<atr.length ; i++)
         if (atr[i].odbSelected) {
            if (key.type === TID_STRING || key.type === TID_LINK)
               key.value[i] = value;
            else
               key.value[i] = parseFloat(value);
         }
      let req = {};
      req[key.name] = key.value;
      req[key.name+'/key'] = {};
      req[key.name+'/key'].type = key.type;
      req[key.name+'/key'].num_values = key.num_values;
      req[key.name+'/key'].item_size = key.item_size;
      mjsonrpc_db_paste([path], [req]).then(() =>
         odb_update(tb)
      ).catch(error => mjsonrpc_error_alert(error));
   }
}

function odb_setlink(p, value) {
   mjsonrpc_call("db_link", {"new_links":[p.odbParam.param],"target_paths":[value]}).then(rpc => {
   }).catch(error =>{ mjsonrpc_error_alert(error); });
}

function new_key(e) {
   let tb = getOdbTb(e);
   document.getElementById('odbCreateDir').innerHTML = tb.odb.path;
   dlgShow('dlgCreate', true, e);
   document.getElementById('odbCreateName').focus();
   document.getElementById('odbCreateName').select();
}

function do_new_key(e) {
   if (e.parentElement.parentElement.param)
      e = e.parentElement.parentElement.param;
   else
      e = e.parentElement.parentElement.parentElement.parentElement.parentElement.parentElement.param;

   let tb = getOdbTb(e);
   let path = document.getElementById('odbCreateDir').innerHTML;
   if (path === '/') path = "";
   let name = document.getElementById('odbCreateName').value;
   let type = parseInt(document.getElementById('odbCreateType').value);
   let size = parseInt(document.getElementById('odbCreateSize').value);
   let strlen = parseInt(document.getElementById('odbCreateStrLen').value);

   if (name.length < 1) {
      dlgAlert("No name specified");
      return false;
   }

   if (size < 1) {
      dlgAlert("Bad array length: " + size);
      return false;
   }

   if (strlen < 1) {
      dlgAlert("Bad string length " + strlen);
      return false;
   }

   let param = {};
   param.path = path + "/" + name;
   param.type = type;
   if (size > 1)
      param.array_length = size;
   if (strlen > 0)
      param.string_length = strlen;

   mjsonrpc_db_create([param]).then(rpc => {
      let status = rpc.result.status[0];
      if (status === 311) {
         dlgMessage("Error", "ODB entry \"" + name + "\" exists already");
      } else if (status !== 1) {
         dlgMessage("Error", "db_create_key() error " + status + ", see MIDAS messages");
      }
      let tb = getOdbTb(e);
      odb_update(tb);
   }).catch(error => mjsonrpc_error_alert(error));

   return true;
}

function new_link(e) {
   let tb = getOdbTb(e);
   document.getElementById('odbCreateLinkDir').innerHTML = tb.odb.path;
   dlgShow('dlgCreateLink', true, e);
   document.getElementById('odbCreateLinkName').focus();
   document.getElementById('odbCreateLinkName').select();
}

function pickLinkTarget() {
   odb_picker("/", setLinkTarget);
}

function setLinkTarget(flag, path) {
   if (flag)
      document.getElementById('odbCreateLinkTarget').value = path;
}

function do_new_link(e) {
   e = e.parentElement.parentElement.param;
   let tb = getOdbTb(e);

   let path = document.getElementById('odbCreateLinkDir').innerHTML;
   if (path === '/') path = "";
   let name = document.getElementById('odbCreateLinkName').value;
   let target = document.getElementById('odbCreateLinkTarget').value;

   if (name.length < 1) {
      dlgAlert("No name specified");
      return false;
   }

   if (target.length < 1) {
      dlgAlert("No link target specified");
      return false;
   }

   if (target[0] !== '/') {
      dlgAlert("Link target must be absolute (start with a \"/\")");
      return false;
   }

   path = path + "/" + name;

   mjsonrpc_call("db_link", {"new_links": [path], "target_paths":[target]}).then(rpc => {
      let status = rpc.result.status[0];
      if (status === 311) {
         dlgMessage("Error", "ODB entry \"" + name + "\" exists already");
      } else if (status !== 1) {
         dlgMessage("Error", "db_create_key() error " + status + ", see MIDAS messages");
      }

      odb_update(tb);
   }).catch(error => mjsonrpc_error_alert(error));

   return true;
}

function new_subdir(e) {
   dlgQuery('Subdirectory name:', "", do_new_subdir, e);
}

function do_new_subdir(subdir, e) {
   if (subdir === false)
      return;

   if (subdir.length < 1) {
      dlgAlert("No name specified");
      return;
   }

   let tb = getOdbTb(e);
   let path = tb.odb.path;
   if (path === '/')
      path = "";
   let param = {};
   param.path = path + "/" + subdir;
   param.type = TID_KEY;

   mjsonrpc_db_create([param]).then(rpc => {
      let status = rpc.result.status[0];
      if (status === 311) {
         dlgMessage("Error", "ODB key \"" + subdir + "\" exists already");
      } else if (status !== 1) {
         dlgMessage("Error", "db_create_key() error " + status + ", see MIDAS messages");
      }
      odb_update(tb);
   }).catch(error => mjsonrpc_error_alert(error));
}

function more_menu(event) {

   event.stopPropagation(); // don't send click to select_key()

   let odb = getOdbTb(event.target).odb;

   let d = document.getElementById('moreMenu');
   if (d === null) {

      // create menu
      d = document.createElement("div");
      d.id = "moreMenu";
      d.style.display = "none";
      d.style.position = "absolute";
      d.className = "mtable";
      d.style.borderRadius = "0";
      d.style.border = "2px solid #808080";
      d.style.margin = "0";
      d.style.padding = "0";

      d.style.left = "100px";
      d.style.top = "100px";

      let table = document.createElement("table");

      // Show open records ----------

      let tr = document.createElement("tr");
      let td = document.createElement("td");
      td.style.padding = "0";

      let a = document.createElement("a");
      a.href = "" + odb.path;
      a.innerHTML = "Show open record";
      a.title = "Show ODB keys which are open by other programs";
      a.onclick = function () {
         d.style.display = 'none';
         // window.location.href = "?cmd=odb_sor&odb_path=" + odb.path;
         show_open_records(event.target);
         return false;
      }
      td.appendChild(a);
      tr.appendChild(td);
      table.appendChild(tr);

      // Show ODB clients  ----------

      tr = document.createElement("tr");
      td = document.createElement("td");
      td.style.padding = "0";

      a = document.createElement("a");
      a.href = "" + odb.path;
      a.innerHTML = "Show ODB clients";
      a.title = "Show clients currently attached to ODB";
      a.onclick = function () {
         d.style.display = 'none';
         window.location.href = "?cmd=odb_scl";
         return false;
      }
      td.appendChild(a);
      tr.appendChild(td);
      table.appendChild(tr);

      d.appendChild(table);

      document.body.appendChild(d);
   }

   let rect = event.target.getBoundingClientRect();

   d.style.display = 'block';
   d.style.left = (rect.left + window.scrollX) + 'px';
   d.style.top = (rect.bottom + 4 + window.scrollY) + 'px';
}

function change_color(e, color) {
   if (e.style !== undefined && (e.odb === undefined || !e.odb.inEdit))
      e.style.color = color;
   if (e.childNodes && (e.odb === undefined || !e.odb.inEdit))
      for (const c of e.childNodes)
         if (c.tagName !== 'SELECT')
            change_color(c, color);
}

function unselect_all_keys(tb) {
   for (let i=4 ; i<tb.childNodes.length ; i++) {
      let tr = tb.childNodes[i];

      // selected keys
      tr.odbSelected = false;
      tr.odbLastSelected = false;
      tr.style.backgroundColor = '';
      change_color(tr, '');
   }
}

function get_selected_keys(tb) {
   let paths = [];
   for (let i=4 ; i<tb.childNodes.length ; i++) {
      let tr = tb.childNodes[i];
      if (tr.odbSelected)
         paths.push({"path": tr.odbPath, "key": tr.key});
   }
   return paths;
}

function unselect_all_array_elements(tb) {
   for (let i=4 ; i<tb.childNodes.length ; i++) {
      let tr = tb.childNodes[i];

      // selected array elements
      if (tr.childNodes.length > 2) {
         tr.childNodes[2].style.backgroundColor = '';
         change_color(tr.childNodes[2], '');
      }
   }
}

function select_key(event) {
   event.preventDefault();
   event.stopPropagation();

   let tr = event.target;
   let tb = getOdbTb(tr);
   if (tb === undefined)
      return;

   let odb = tb.odb;

   // hide submenu if visible
   let m = document.getElementById('moreMenu');
   if (m !== null && m.style.display === 'block')
      m.style.display = 'none';

   // hide context menu if visible
   m = document.getElementById('contextMenu');
   if (m !== null && m.style.display === 'block')
      m.style.display = 'none';

   // un-select all array elements
   unselect_all_array_elements(tb);

   // don't select key when we are in edit mode
   while (tr.tagName !== 'TR') {
      tr = tr.parentNode;
      if (tr === null)
         return;
   }
   if (find_input_element(tr))
      return;

   if (odb.picker) {
      // unselect all keys
      for (let i = 4; i < tb.childNodes.length; i++)
         tb.childNodes[i].odbSelected = false;

      // don't select array values
      if (tr.childNodes[1].innerHTML !== "")
         tr.odbSelected = true;

      odb.selectedKey = tr.odbPath;
   } else {
      // check if click is on header row, if so remove selection further down
      let headerRow = false;
      for (let i = 0; i < 4; i++)
         if (tb.childNodes[i] === tr) {
            headerRow = true;
            break;
         }

      if (event.shiftKey && !headerRow) {
         // search last selected row
         let i1;
         for (i1 = 4; i1 < tb.childNodes.length; i1++)
            if (tb.childNodes[i1].odbLastSelected)
               break;
         if (i1 === tb.childNodes.length)
            i1 = 4; // non selected, so use first one

         let i2;
         for (i2 = 4; i2 < tb.childNodes.length; i2++)
            if (tb.childNodes[i2] === tr)
               break;

         if (i2 < tb.childNodes.length) {
            if (i1 > i2)
               [i1, i2] = [i2, i1];

            for (let i = i1; i <= i2; i++) {
               // don't select arrays
               if (tb.childNodes[i].childNodes[1].innerHTML !== "")
                  tb.childNodes[i].odbSelected = true;
            }
         }

      } else if ((event.metaKey || event.ctrlKey) && !headerRow) {

         // command key just toggles current selection
         tr.odbSelected = !tr.odbSelected;

         for (let i = 4; i < tb.childNodes.length; i++)
            tb.childNodes[i].odbLastSelected = false;
         if (tr.odbSelected)
            tr.odbLastSelected = true;

      } else {

         // no key pressed -> un-select all but current
         for (let i = 4; i < tb.childNodes.length; i++) {
            tb.childNodes[i].odbSelected = false;
            tb.childNodes[i].odbLastSelected = false;
         }

         // don't select header row and array values
         if (!headerRow && tr.childNodes[1].innerHTML !== "") {
            tr.odbSelected = true;
            tr.odbLastSelected = true;
         }
      }
   }

   // change color of all rows according to selection
   for (let i=4 ; i<tb.childNodes.length ; i++) {
      let tr = tb.childNodes[i];

      tr.style.backgroundColor = tr.odbSelected ? '#004CBD' : '';
      change_color(tr, tr.odbSelected ? '#FFFFFF' : '');
   }
}

function getArrayTr(tr) {
   // collect all rows belonging to "tr" in an array
   let tb = getOdbTb(tr);
   let atr = [];
   let i;
   for (i = 0; i < tb.childNodes.length; i++)
      if (tb.childNodes[i] === tr)
         break;
   while (tb.childNodes[i].odbPath.indexOf('[') !== -1)
      i--;
   i++; // first element
   do {
      atr.push(tb.childNodes[i++]);
   } while (i < tb.childNodes.length && tb.childNodes[i].odbPath.indexOf('[') !== undefined);
   return atr;
}

function select_array_element(event) {
   event.preventDefault();
   event.stopPropagation();

   let tr = event.target;
   let tb = getOdbTb(tr);
   if (tb === undefined)
      return;

   // don't select key when we are in edit mode
   while (tr.tagName !== 'TR') {
      tr = tr.parentNode;
      if (tr === null)
         return;
   }
   if (find_input_element(tr))
      return;

   let odb = tb.odb;

   // hide submenu if visible
   let m = document.getElementById('moreMenu');
   if (m !== null && m.style.display === 'block')
      m.style.display = 'none';

   // hide context menu if visible
   m = document.getElementById('contextMenu');
   if (m !== null && m.style.display === 'block')
      m.style.display = 'none';

   // remove selection from all non-array keys
   for (let i=0 ; i<tb.childNodes.length ; i++) {
      if (tb.childNodes[i].childNodes.length > 1 &&
         tb.childNodes[i].childNodes[1].innerHTML !== "" &&
         tb.childNodes[i].odbSelected) {
         tb.childNodes[i].odbSelected = false;
         tb.childNodes[i].style.backgroundColor = '';
         change_color(tb.childNodes[i], '');
      }
   }

   // create array of all array elements
   let atr = getArrayTr(tr);

   if (odb.picker) {
      // un-select all but current
      for (const tr of atr) {
         tr.odbSelected = false;
         tr.odbLastSelected = false;
      }
      tr.odbSelected = true;
      tr.odbLastSelected = true;
      odb.selectedKey = tr.odbPath;

   } else {

      if (event.shiftKey) {
         // search last selected row
         let i1;
         for (i1 = 0; i1 < atr.length; i1++)
            if (atr[i1].odbLastSelected)
               break;
         if (i1 === atr.length)
            i1 = 0; // non selected, so use first one

         let i2;
         for (i2 = 0; i2 < atr.length; i2++)
            if (atr[i2] === tr)
               break;

         if (i2 < atr.length) {
            if (i1 > i2)
               [i1, i2] = [i2, i1];

            for (let i = i1; i <= i2; i++)
               atr[i].odbSelected = true;
         }

      } else if (event.metaKey || event.ctrlKey) {

         // command key just toggles current selection
         tr.odbSelected = !tr.odbSelected;

         for (const tr of atr)
            tr.odbLastSelected = false;
         if (tr.odbSelected)
            tr.odbLastSelected = true;

      } else {

         // no key pressed -> un-select all but current
         for (const tr of atr) {
            tr.odbSelected = false;
            tr.odbLastSelected = false;
         }
         tr.odbSelected = true;
         tr.odbLastSelected = true;
      }
   }

   // change color of all rows according to selection
   for (const e of atr) {
      if (e.childNodes.length >= 3) {
         e.childNodes[2].style.backgroundColor = e.odbSelected ? '#004CBD' : '';
         change_color(e.childNodes[2], e.odbSelected ? '#FFFFFF' : '');
      }
   }
}

function context_menu(event) {
   event.preventDefault();
   event.stopPropagation();

   let tr = event.target;
   while (tr.tagName !== 'TR')
      tr = tr.parentNode;
   let tb = getOdbTb(tr);
   let odb = tb.odb;
   let path = tr.odbPath;

   if (!tr.odbSelected)
      select_key(event);

   let d = document.getElementById('contextMenu');
   if (d === null) {
      // create menu on the first call

      d = document.createElement("div");
      d.id = "contextMenu";
      d.style.display = "none";
      d.style.position = "absolute";
      d.className = "mtable";
      d.style.borderRadius = "0";
      d.style.border = "2px solid #808080";
      d.style.margin = "0";
      d.style.padding = "0";

      d.style.left = "100px";
      d.style.top = "100px";

      let table = document.createElement("table");

      let menu = ["Copy key", "Copy plain text", "Delete key", "Rename key"];
      let trm;
      let tdm;
      let a;

      for (const m of menu) {
         trm = document.createElement("tr");
         tdm = document.createElement("td");
         tdm.style.padding = "0";
         a = document.createElement("a");
         a.href = "#";
         a.innerHTML = m;
         a.title = m;
         tdm.appendChild(a);
         trm.appendChild(tdm);
         table.appendChild(trm);
      }

      d.appendChild(table);
      document.body.appendChild(d);
   }

   // set event handler for copy menu
   d.childNodes[0].childNodes[0].childNodes[0].childNodes[0].onclick = function () {
      d.style.display = 'none';
      unselect_all_keys(tb);
      tr.odbSelected = true;
      odb_copy(tb);
      tr.odbSelected = false;
   }

   // set event handler for copy plain text menu
   d.childNodes[0].childNodes[1].childNodes[0].childNodes[0].onclick = function () {
      d.style.display = 'none';
      let path = tr.odbPath;
      mjsonrpc_db_copy([path]).then(rpc => {
         let text = odbASCII(rpc.result.data[0], path);
         if (navigator.clipboard && navigator.clipboard.writeText) {
            try {
               navigator.clipboard.writeText(text);
               dlgAlert("ODB directory \"" + path + "\" copied to clipboard as plain text");
            } catch(error) {
               dlgAlert(error);
            }
         }
      }).catch(error => mjsonrpc_error_alert(error));
   }

   // set event handler for delete key menu
   d.childNodes[0].childNodes[2].childNodes[0].childNodes[0].onclick = function () {
      d.style.display = 'none';
      odb_delete(tr);
   }

   // set event handler for rename menu
   d.childNodes[0].childNodes[3].childNodes[0].childNodes[0].onclick = function () {
      d.style.display = 'none';

      let key = tr.childNodes[1].innerHTML;
      if (key.indexOf('<a href=') !== -1) {
         for (const c of tr.childNodes[1].childNodes)
            if (c.tagName === 'A') {
               key = c.innerHTML;
               break;
            }
         if (key.indexOf('\u25BE') !== -1)
            key = key.substring(key.indexOf('\u25BE') + 2);
         if (key.indexOf('\u25B8') !== -1)
            key = key.substring(key.indexOf('\u25B8') + 2);
      }

      while (key.indexOf("&nbsp;") !== -1)
         key = key.substring(key.indexOf("&nbsp;")+6);

      tr.oldKey = key;

      inline_edit(event,
         tr.childNodes[1],
         key,
         do_rename_key,
         key.length,
         tr.odbPath);
   }

   let rect = event.target.getBoundingClientRect();

   d.style.display = 'block';
   d.style.left = (event.offsetX + rect.left + window.scrollX) + 'px';
   d.style.top = (event.offsetY+rect.top + window.scrollY) + 'px';

   return false;
}

function expand(e) {
   let tb = getOdbTb(e);
   let odb = tb.odb;

   let rarr = document.getElementById('expRight');
   let cells = document.getElementsByName('odbExt');
   odb.detailsColumn = !odb.detailsColumn;

   if (odb.detailsColumn) {
      for (const d of cells)
         d.style.display = 'table-cell';
      rarr.style.display = 'none';
   } else {
      for (const d of cells)
         d.style.display = 'none';
      rarr.style.display = 'inline';
   }
   odb.skip_yellow = true;
   odb_update(tb);
}

function clear_expanded(odb) {
   for (const key of odb.key) {
      if (key.type === TID_KEY)
         key.subdir_open = false;
      else
         key.expanded = false;
   }
}

function toggle_handles(e) {
   let tb = getOdbTb(e);
   let odb = tb.odb;

   // clear all expanded flags
   clear_expanded(odb);
   odb_print_all(tb);

   let n = document.getElementsByName('odbHandle');
   odb.handleColumn = !odb.handleColumn;

   if (odb.handleColumn) {
      for (const d of n) {
         let tr = d.parentNode;
         if (tr.firstChild.className !== 'colHeader') {
            tr.setAttribute('draggable', true);
            tr.addEventListener('dragstart', drag_start);
            tr.addEventListener('dragover', drag_move);
            tr.addEventListener('dragend', drag_end);
         }

         d.style.display = 'table-cell';
      }
   } else {
      for (const d of n) {
         let tr = d.parentNode;
         if (tr.firstChild.className !== 'colHeader') {
            tr.removeAttribute('draggable');
            tr.removeEventListener('dragstart', drag_start);
            tr.removeEventListener('dragover', drag_move);
            tr.removeEventListener('dragend', drag_end);
         }

         d.style.display = 'none';
      }
   }

   odb_update(tb);
}

function toggle_expanded(e) {
   let tr = e;
   while (tr.tagName !== 'TR')
      tr = tr.parentNode;

   if (tr.key.num_values >= 5000 && !tr.key.expanded) {
      dlgConfirm("ODB key \""+ tr.key.name + "\" has " + tr.key.num_values +
         " array elements. This can take very long. Do you want to continue?", do_toggle_expanded, tr);
   } else
      do_toggle_expanded(true, tr);
}

function do_toggle_expanded(flag, tr) {
   if (flag) {
      let tb = getOdbTb(tr);
      tr.key.expanded = !tr.key.expanded;
      unselect_all_keys(tb);
      unselect_all_array_elements(tb);
      odb_print_all(tb);
   }
}

function odb_delete(e) {

   let tb = getOdbTb(e);
   let currentPath = tb.odb.path;
   let paths = [];
   for (const tr of tb.childNodes) {
      if (tr.odbSelected)
         paths.push(tr.odbPath);
   }

   if (paths.length == 0) {
      // strip last directory from current path
      let newPath = currentPath;
      if (newPath.lastIndexOf('/') !== -1)
         newPath = newPath.substring(0, newPath.lastIndexOf('/'));
      if (newPath === "")
         newPath = '/';

      dlgConfirm('Are you sure to delete odb directory ' + currentPath + '?',
         do_odb_delete, {"e": e, "paths": [currentPath], "newPath": newPath} );
   } else if (paths.length == 1)
      dlgConfirm('Are you sure to delete odb key ' + paths[0] + '?',
         do_odb_delete, {"e": e, "paths": paths});
   else
      dlgConfirm('Are you sure to delete ' + paths.length + ' keys?',
         do_odb_delete, {"e": e, "paths": paths});
}

function do_odb_delete(flag, param) {
   if (flag) {
      mjsonrpc_db_delete(param.paths).then().catch(error => mjsonrpc_error_alert(error));
      if (param.newPath)
         subdir_goto(param.e, param.newPath);
      else {
         let tb = getOdbTb(param.e);
         odb_update(tb);
      }
   }
}

function rename_key_click(event) {
   let tr = event.target;
   while (tr.tagName !== 'TR')
      tr = tr.parentNode;
   if (!tr.odbSelected)
      return;
   if (tr.childNodes[1].odbParam && tr.childNodes[1].odbParam.inEdit)
      return;

   event.stopPropagation(); // don't select key again

   let key;
   if (tr.childNodes[1].childNodes[1].tagName === 'SPAN')
      key = tr.childNodes[1].childNodes[1].innerHTML;
   else
      key = tr.childNodes[1].innerHTML;
   while (key.indexOf("&nbsp;") !== -1)
      key = key.substring(key.indexOf("&nbsp;")+6);

   tr.oldKey = key;

   inline_edit(event,
      tr.childNodes[1].childNodes[1],
      key,
      do_rename_key,
      key.length,
      tr.odbPath);
}

function rename_key(element) {
   let tb = getOdbTb(element);
   let n = 0;
   let tr;
   for (const t of tb.childNodes) {
      if (t.odbSelected) {
         n++;
         tr = t;
      }
   }

   if (n === 0)
      dlgAlert("Please select key to be renamed");
   else if (n > 1)
      dlgAlert("Please select only single key to be renamed");
   else {
      tr.odbSelected = false;
      tr.style.backgroundColor = '';
      change_color(tr, '');

      let elem =  tr.childNodes[1].childNodes[2] ?
         tr.childNodes[1].childNodes[2] :
         tr.childNodes[1].childNodes[1];
      let key = elem.innerHTML;
      key = key.trim();

      tr.oldKey = key;

      inline_edit(undefined,
         elem,
         key,
         do_rename_key,
         key.length,
         tr.odbPath);
   }
}

function do_rename_key(p, str, path) {
   if (str === '') {
      dlgAlert("Empty name not allowed");
      return;
   }
   mjsonrpc_call("db_rename", { "paths": [path], "new_names": [str]})
      .then()
      .catch(error => mjsonrpc_error_alert(error));
   let old = p.parentNode.oldKey;

   p.innerHTML = p.innerHTML.replace(old, str);
}

function search_key() {
   dlgQuery("Enter key name (substring case insensitive):", "", do_search_key);
}

function do_search_key(str) {
   if (str !== false)
      window.location.href = "?cmd=Find&value=" + str;
}

function odb_copy(e) {
   let tb = getOdbTb(e);
   let selKeys = get_selected_keys(tb);
   let paths = [];
   let dirFlag = false;
   if (selKeys.length === 0) {
      paths.push(tb.odb.path);
      dirFlag = true;
   } else {
      for (const k of selKeys)
         paths.push(k.path);
   }
   mjsonrpc_db_copy(paths).then(rpc => {
      let text = '';

      // generate text from objects, add top directoy which is missing from db_copy
      for (const [i,d] of rpc.result.data.entries()) {
         if (i === 0) {
            if (dirFlag) {
               let dir = tb.odb.path.substring(tb.odb.path.lastIndexOf('/')+1);
               if (dir === '')
                  dir = 'root';
               text += '{ \"'+ dir +
                  '\": '+JSON.stringify(d, null, '   ')+'}';
            } else if (selKeys[i].key.type === TID_KEY) {
               text += '{ \"'+ selKeys[i].key.name +'\": '+JSON.stringify(d, null, '   ')+'}';
            } else
               text += JSON.stringify(d, null, '   ');
         } else
            text += JSON.stringify(d, null, '   ').substring(1); // strip first '{'
         if (i < rpc.result.data.length - 1)
            text = text.substring(0, text.lastIndexOf('}')) + ','; // replace last '}' by ','
      }

      // put text to clipboard if possible
      if (navigator.clipboard && navigator.clipboard.writeText) {
         try {
            navigator.clipboard.writeText(text);
            if (dirFlag)
               dlgAlert("ODB directory \"" + paths + "\" copied to clipboard");
            else {
               if (paths.length === 1)
                  dlgAlert("ODB key \"" + paths + "\" copied to clipboard");
               else
                  dlgAlert("ODB keys \"" + paths + "\" copied to clipboard");
            }
         } catch(error) {
            dlgAlert(error);
         }
      }

      // store text locally as a backup
      try {
         sessionStorage.setItem('odbclip', text);
      } catch (error) {
         dlgAlert(error);
      }

   }).catch(error => mjsonrpc_error_alert(error));
}

function odb_paste(e) {
   let tb = getOdbTb(e);
   let odb = tb.odb;
   try {
      let text = sessionStorage.getItem('odbclip');
      if (text) {
         let keys = JSON.parse(text);
         odb_paste_keys(tb, keys);
         return;
      }
   } catch (error) {
      dlgAlert("Invalid JSON format in clipboard:<br /><br />" + error);
      return;
   }

   if (navigator.clipboard && navigator.clipboard.readText) {
      navigator.clipboard.readText().then(text => {
         try {
            if (text) {
               let keys = JSON.parse(text);
               odb_paste_keys(tb, keys);
               return;
            } else {
               dlgAlert("Nothing stored in clipboard");
            }
         } catch (error) {
            dlgAlert("Invalid JSON format in clipboard:<br /><br />" + error);
         }
      });
   } else
      dlgAlert("Paste not possible in this browser");
}

function odb_paste_keys(tb, newKeys) {
   let odb = tb.odb;

   for (const key in newKeys) {
      if (key.indexOf('/') !== -1)
         continue;

      // check for existing key in current keys
      let cKeyMaxIndex = 0;
      let cKeyIndex = 0;
      let cKeyBare = "";
      let ckb = "";
      let found = false;
      for (const cKey of odb.key) {
         if (cKey.name.indexOf(' copy') === -1)
            ckb = cKey.name;
         else
            ckb = cKey.name.substring(0, cKey.name.indexOf(' copy'));

         if (ckb === key) {
            found = true;
            cKeyBare = ckb;
            if (cKey.name.indexOf(' copy') !== -1) {
               cKeyIndex = parseInt(cKey.name.substring(cKey.name.indexOf(' copy')+5));
               if (isNaN(cKeyIndex))
                  cKeyIndex = 1;
               if (cKeyIndex > cKeyMaxIndex)
                  cKeyMaxIndex = cKeyIndex;
            }
         }
      }

      // rename new key if name exists
      if (found) {
         let newName = cKeyBare + ' copy';
         if (cKeyIndex > 0)
            newName += ' ' + (cKeyIndex+1).toString();

         newKeys[newName] = newKeys[key];
         delete newKeys[key];

         newKeys[newName + '/key'] = newKeys[key + '/key'];
         delete newKeys[key + '/key'];
      }
   }

   mjsonrpc_db_paste([odb.path], [newKeys]).then(rpc =>
      odb_update(tb)).catch(error =>
      dlgAlert(error));

}

function odbASCII(o, path) {
   let t = "";
   let need_path = true;
   for (const key in o) {
      if (key.indexOf('/') !== -1)
         continue;

      if (o[key+'/key'] === undefined) {
         t += odbASCII(o[key], path + '/' + key);
         need_path = true;
         continue;
      }

      if (need_path) {
         need_path = false;
         t += '\n[' + path + ']\n';
      }

      let tid = o[key+'/key'].type;
      let num_values = o[key+'/key'].num_values;
      let item_size = o[key+'/key'].item_size;
      t += key + " = ";
      t += tid_name[tid];

      if (num_values > 1) {
         t += '[' + num_values + ']';
         t += " :\n";
         for (let i=0 ; i<num_values ; i++) {
            t += '[' + i + '] ';
            if (tid === TID_STRING || tid === TID_LINK)
               t += '[' + item_size + '] ';
            t += mie_to_string(tid, o[key][i]);
            t += '\n';
         }
      } else {
         t += " : ";
         if (tid === TID_STRING || tid === TID_LINK)
            t += '[' + item_size + '] ';
         t += mie_to_string(tid, o[key]);
         t += '\n';
      }
   }
   return t;
}

function odb_download(e) {
   let tb = getOdbTb(e);
   mjsonrpc_db_copy([tb.odb.path]).then(rpc => {

      let dirs = tb.odb.path.split('/');
      let filename = dirs[dirs.length - 1];
      if (filename === '')
         filename = 'root';
      filename += ".json";

      let header = {
         "/MIDAS version": "2.1",
         "/filename": filename,
         "/ODB path": tb.odb.path
      }
      header = JSON.stringify(header, null, '   ');
      header = header.substring(0, header.length-2) + ','; // strip trailing '}'

      let odbJson = JSON.stringify(rpc.result.data[0], null, '   ');
      if (odbJson.indexOf('{') === 0)
         odbJson = odbJson.substring(1); // strip leading '{'

      odbJson = header + odbJson;

      // use trick from FileSaver.js
      let a = document.getElementById('downloadHook');
      if (a === null) {
         a = document.createElement("a");
         a.style.display = "none";
         a.id = "downloadHook";
         document.body.appendChild(a);
      }

      let blob = new Blob([odbJson], {type: "text/json"});
      let url = window.URL.createObjectURL(blob);

      a.href = url;
      a.download = filename;
      a.click();
      window.URL.revokeObjectURL(url);
      dlgAlert("ODB subtree \"" + tb.odb.path +
         "\" downloaded to file \"" + filename + "\"");

   }).catch(error => mjsonrpc_error_alert(error));
}

async function odb_upload(e) {
   // Chrome has file picker, others might have not
   try {
      let fileHandle;
      [fileHandle] = await window.showOpenFilePicker();
      const file = await fileHandle.getFile();
      const text = await file.text();
      pasteBuffer(e, text, file);
   } catch (error) {
      if (error.name !== 'AbortError') {
         // fall-back to old method
         dlgShow('fileSelector', true, e);
      }
   }
}

function loadFileFromSelector(e) {

   let input = document.getElementById('fileSelector');
   let file = input.files[0];
   if (file !== undefined) {
      let reader = new FileReader();
      reader.readAsText(file);

      reader.onerror = function () {
         dlgAlert('File read error: ' + reader.error);
      };

      reader.onload = function () {
         pasteBuffer(e.param, reader.result, file.name);
      };
   }
}

function pasteBuffer(e, text, filename) {
   let tb = getOdbTb(e);
   let path = tb.odb.path;
   let odbJson;
   try {
      odbJson = JSON.parse(text);
   } catch (error) {
      dlgAlert(error);
      return;
   }
   // delete /MIDAS version, /filename etc.
   for (let [name, value] of Object.entries(odbJson)) {
      if (name[0] === '/')
         delete odbJson[name];
   }
   mjsonrpc_db_paste([path], [odbJson]).then(rpc =>
      dlgAlert("File \"" + filename.name + "\" successfully loaded to ODB under " + path)).catch(error =>
      mjsonrpc_error_alert(error));
}

function subdir_open_click(event, e) {
   event.stopPropagation();
   let tr = e.parentNode;
   while (tr.tagName !== 'TR')
      tr = tr.parentNode;
   let path = tr.odbPath;

   subdir_open(event.target, path);
}

// user clicks on subdirectory, so open it
function subdir_open(tr, path) {
   while (tr.tagName !== 'TR')
      tr = tr.parentNode;
   let tb = getOdbTb(tr);
   let odb = tb.odb;

   // don't open subdirs if in reorder mode
   if (odb.handleColumn)
      return;

   unselect_all_keys(tb);
   unselect_all_array_elements(tb);

   // find key belonging to 'path'
   tr.key.subdir_open = !tr.key.subdir_open;
   tb.odb.skip_yellow = true;
   odb_update(tb);
}

function subdir_goto_click(event, e) {
   event.stopPropagation();
   let tr = e.parentNode;
   while (tr.tagName !== 'TR')
      tr = tr.parentNode;
   let path = tr.odbPath;

   subdir_goto(event.target, path);
}

function subdir_goto(e, path) {
   let tb = getOdbTb(e);
   let odb = tb.odb;

   unselect_all_keys(tb);
   unselect_all_array_elements(tb);

   // kill old timer
   if (odb.updateTimer !== undefined)
      window.clearTimeout(odb.updateTimer);

   // update URL
   if (!odb.picker) {
      let url = window.location.href;
      if (url.search("&odb_path") !== -1)
         url = url.slice(0, url.search("&odb_path"));
      url += "&odb_path=" + path;
      if (url !== window.location.href)
         window.history.pushState({'path': path}, '', url);
   }

   // modify title
   if (document.title.indexOf("ODB") !== -1) {
      if (path === '/')
         document.title = "ODB";
      else
         document.title = "ODB " + path;
   }

   // update ODB object
   odb.path = path;
   odb.level = 0;
   odb.dataIndex = 0;
   odb.key = [];
   odb.skip_yellow = true;

   odb_update(tb);
}

// push all paths of open subdirectories recursively for a following db_ls
function push_paths(paths, odb, path) {
   odb.dataIndex = paths.length;
   paths.push(path);
   for (let i=0 ; i< odb.key.length ; i++) {
      if (odb.key[i].type === TID_KEY && odb.key[i].subdir_open) {
         if (odb.key[i].value.id === undefined) {
            odb.key[i].value = {
               id: odb.id,
               path: odb.path === '/' ? '/' + odb.key[i].name : odb.path + '/' + odb.key[i].name,
               level: odb.level + 1,
               dataIndex: paths.length,
               key: []
            };
         }
         push_paths(paths, odb.key[i].value, odb.key[i].value.path);
      }
   }
}

// update overall ODB display
function odb_update(tb) {
   let odb = tb.odb;

   if (odb.updateTimer !== undefined)
      window.clearTimeout(odb.updateTimer);
   let row = { i:4, nvk:0 };

   // show clickable ODB path in top row
   let dirs;
   if (odb.path === '/')
      dirs = ['/'];
   else
      dirs = odb.path.split('/');
   let path = '';
   let s = "<a href=\"#\" onclick=\"subdir_goto(this, '/');return false;\" title=\"Root directory\">" +
      "<img src=\"icons/home.svg\" style=\"border:none; height: 16px; vertical-align: middle;margin-bottom: 2px;\">" +
      "</a>&nbsp;/&nbsp;";
   for (let i=1 ; i<dirs.length ; i++) {
      path += "/" + dirs[i];
      s += "<a href=\"#\" onclick=\"subdir_goto(this, '" + path + "');return false;\">"+dirs[i]+"</a>";
      if (i < dirs.length - 1)
         s += "&nbsp;/&nbsp;";
   }
   if (s !== tb.childNodes[1].firstChild.innerHTML)
      tb.childNodes[1].firstChild.innerHTML = s;

   // request ODB data
   let paths = [];
   push_paths(paths, odb, odb.path);

   mjsonrpc_db_ls(paths).then(rpc => {
      odb_extract(odb, rpc.result.data);
      odb_print(tb, row, tb.odb);
      odb.skip_yellow = false;

      // call timer in one second to update again
      odb.updateTimer = window.setTimeout(odb_update, 1000, tb);
   }).catch();
}

// extract keys and put them into odb tree from db_ls result in dataArray
function odb_extract(odb, dataArray) {
   let data = dataArray[odb.dataIndex];
   let n = 0;
   for (const item in data) {
      if (item.indexOf('/') !== -1)
         continue;

      let key = {};
      key.name = item;
      key.value = data[item];
      let linkFlag = data[item + '/key'] !== undefined && data[item + '/key'].link !== undefined;
      if (!linkFlag && typeof key.value === 'object' && Object.keys(key.value).length === 0) {
         key.type = TID_KEY;
         key.subdir_open = false;
         key.num_values = 1;
         key.item_size = 0;
         key.last_written = 0;
         key.access_mode = 0;
      } else {
         key.type = data[item + '/key'].type;
         key.link = data[item + '/key'].link;
         key.num_values = data[item + '/key'].num_values;
         if (key.num_values === undefined)
            key.num_values = 1;
         if (data[item + '/key'].item_size !== undefined)
            key.item_size = data[item + '/key'].item_size;
         else
            key.item_size = tid_size[key.type];
         key.last_written = data[item + '/key'].last_written;
         key.access_mode = data[item + '/key'].access_mode;
      }
      key.options = (key.name.substring(0, 7) === "Options");

      if (odb.key.length <= n) {
         key.expanded = (key.num_values > 1 && key.num_values <= 10);
         if (odb.picker)
            key.expanded = false;
         odb.key.push(key);
      } else {
         if (odb.key[n].subdir_open) {
            key.subdir_open = true;
            key.value = odb.key[n].value;
         }
         if (odb.key[n].expanded !== undefined) {
            key.expanded = odb.key[n].expanded;
         }
         odb.key[n] = key;
      }
      n++;

      // if current key is a subdirectory, call us recursively
      if (key.type === TID_KEY && key.subdir_open) {
         odb.key[n-1].value.picker = odb.picker;
         odb_extract(odb.key[n-1].value, dataArray);
      }
   }

   // if local data remaining, ODB key must have been deleted, so delete also local data
   if (odb.key.length > n) {
      odb.key = odb.key.slice(0, n);
      odb.skip_yellow = true;
   }
}

function odb_print_all(tb) {
   let odb = tb.odb;
   let row = { i:4, nvk:0 };
   let nValueKeys = 0;
   odb.skip_yellow = true;
   odb_print(tb, row, tb.odb);
}

function odb_print(tb, row, odb) {
   for (const key of odb.key) {

      // search for "Options <key>"
      let options = odb.key.find(o => o.name === 'Options ' + key.name);

      // Print current key
      odb_print_key(tb, row, odb.path, key, odb.level, options ? options.value : undefined);
      row.i++;
      if (key.type !== TID_KEY)
         row.nvk++;

      // Print whole subdirectory if open
      if (key.type === TID_KEY && key.subdir_open) {

         // Propagate skip_yellow flag (dirctory just opened) to all subkeys
         key.value.skip_yellow = odb.skip_yellow;

         // Print whole subdirectory
         odb_print(tb, row, key.value);
      }
   }

   // Hide 'value' if only directories are listed
   let ds = document.getElementsByName('valueHeader');
   for (d of ds)
      if (tb.contains(d))
         d.style.display = row.nvk > 0 ? 'table-cell' : 'none';

   // At the end, remove old rows if subdirectory has been closed
   if (odb.level === 0)
      while (tb.childNodes.length > row.i)
         tb.removeChild(tb.childNodes[tb.childNodes.length-1]);
}

function find_input_element(e) {
   if (e === undefined)
      return false;
   for (const c of e.childNodes) {
      if (c.tagName === 'INPUT')
         return true;
      if (c.tagName === 'SELECT' && e.inEdit)
         return true;
      if (c.childNodes.length > 0)
         if (find_input_element(c))
            return true;
   }
   return false;
}

function odb_print_key(tb, row, path, key, level, options) {
   let odb = tb.odb;

   // ful path to key
   let keyPath = (path === '/' ? '/' + key.name : path + '/' + key.name);

   // create empty row
   let tr = document.createElement('TR');

   // Handle column
   let td = document.createElement('TD');
   td.className = 'odbKey';
   td.setAttribute('name', 'odbHandle');
   td.style.display = odb.handleColumn ? 'table-cell' : 'none';
   td.style.width = "10px";
   tr.appendChild(td);
   td.innerHTML = '<img style="cursor: all-scroll; height: 15px; padding: 0; border: 0" src="icons/menu.svg">';

   td.childNodes[0].setAttribute('draggable', false);

   // Key name
   td = document.createElement('TD');
   td.className = 'odbKey';
   tr.appendChild(td);

   // Add three spaces of indent for each level
   let indent = "<span>";
   for (let i = 0; i < level; i++)
      indent += "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;";
   indent += "</span>";

   // Remember indent level for key rename
   tr.level = odb.level;

   // Remember key (need to toggle expanded flag)
   tr.key = key;

   // Remember key path
   tr.odbPath = keyPath;

   // Set special class for "Options ..." keys
   if (key.options && !odb.detailsColumn)
      tr.style.display = 'none';

   // Print subdir with open subdir handler
   if (key.type === TID_KEY) {
      if (odb.handleColumn)
         // do not show any links in re-order mode
         td.innerHTML = indent + key.name;
      else {
         let handler = "onclick=\"subdir_open_click(event, this);return false;\" ";

         if (key.subdir_open)
            td.innerHTML = indent + "<a href='#' " +
               handler +
               ">&nbsp;\u25BE&nbsp;</a>";
         else
            td.innerHTML = indent + "<a href='#' " +
               handler +
               ">&nbsp;\u25B8&nbsp;</a>";

         handler = "onclick=\"subdir_goto_click(event, this);return false;\" ";
         td.innerHTML += "<a href='#' " + handler + "> " + key.name + " </a>";
      }

      if (key.link) {
         if (odb.picker)
            td.innerHTML +=  ' &rarr; <span>' + key.link + '</span>';
         else
            td.innerHTML += ' &rarr; <span>'+ '<a href="#" ' +
               'onclick="inline_edit(event, this.parentNode, \'' +
               key.link + '\', odb_setlink, undefined, \'' + keyPath +
               '\'); return false;" ' +
               ' title="Change value">' + key.link +
               '</a></span>';
      }

   } else if (key.link === undefined) {
      td.innerHTML = indent + '<span>' + key.name + '</span>';
      if (!odb.picker) {
         td.childNodes[1].addEventListener('click', rename_key_click);
         td.childNodes[1].style.cursor = "pointer";
      }
   } else {
      td.innerHTML = indent + '<span>' + key.name + '</span>';
      if (!odb.picker) {
         td.childNodes[1].addEventListener('click', rename_key_click);
         td.childNodes[1].style.cursor = "pointer";
      }
      if (odb.picker)
         td.innerHTML += ' &rarr; <span>' + key.link + '</span>';
      else
         td.innerHTML += ' &rarr; <span>'+ '<a href="#" ' +
            'onclick="inline_edit(event, this.parentNode, \'' +
            key.link + '\', odb_setlink, undefined, \'' + keyPath +
            '\'); return false;" ' +
            ' title="Change value">' + key.link +
            '</a></span>';
   }

   // Subdirs occupy all 8 columns
   if (key.type === TID_KEY) {
      td.colSpan = "8";

      if (tb.childNodes.length > row.i) {
         let selected = tb.childNodes[row.i].odbSelected;
         if (selected) {
            tr.style.backgroundColor = '#004CBD';
            change_color(tr, '#FFFFFF');
         }

         if (!tb.childNodes[row.i].isEqualNode(tr)) {
            // check for edit mode
            let inlineEdit = find_input_element(tb.childNodes[row.i]);
            if (!inlineEdit) {
               if (tb.childNodes[row.i].innerHTML != tr.innerHTML)
                  tb.childNodes[row.i].innerHTML = tr.innerHTML;
            }
         }
      } else
         tb.appendChild(tr);

      // Remove and install mouse click handlers
      if (tb.childNodes[row.i].childNodes[2] !== undefined)
         tb.childNodes[row.i].childNodes[2].removeEventListener('click', select_array_element);
      tb.childNodes[row.i].addEventListener('click', select_key);
      if (!odb.picker)
         tb.childNodes[row.i].addEventListener('contextmenu', context_menu);

      // Remember ODB path in row
      tb.childNodes[row.i].odbPath = keyPath;
      tb.childNodes[row.i].key = key;

      // Remove and odbOptions class or invisibility
      tb.childNodes[row.i].className = "";
      tb.childNodes[row.i].style.display = 'table-row';

      return;
   }

   // Value
   td = document.createElement('TD');
   td.className = 'odbKey';
   tr.appendChild(td);

   if (Array.isArray(key.value)) {
      if (odb.picker)
         td.innerHTML =
            '<div style="display: inline;"></div>';
      else
         td.innerHTML =
            '<div style="display: inline;"><a href="#" ' +
            'onclick="inline_edit(event, this.parentNode, \'' +
            key.value[0] + '\', odb_setall, 10, \'' + keyPath +
            '\');return false;" ' +
            'title="Set array elements to same value">*</a></div>';

      if (key.expanded === false)
         td.innerHTML +=
            '<div style="display: inline;float: right">' +
            '<a href="#"' +
            'onclick="toggle_expanded(this.parentNode);return false;" ' +
            'title="Show array elements">\u25B8</a>' +
            '</div>';
      else
         td.innerHTML +=
            '<div style="display: inline;float: right">' +
            '<a href="#"' +
            'onclick="toggle_expanded(this.parentNode);return false;" ' +
            'title="Hide array elements">\u25BE</a>' +
            '</div>';
   } else {

      if (options) {
         let h = "<select onchange='odb_setoption(this)' " +
            " onclick='option_edit(event, this, true)'" +
            " onblur='option_edit(event, this, false)'" +
            "'>\n";
         for (const o of options) {
            if (key.value === o)
               h += "<option selected value='" + o + "'>" + o + "</option>";
            else
               h += "<option value='" + o + "'>" + o + "</option>";
         }
         h += "</select>\n";
         td.innerHTML = h;
      } else {
         let edit = '<a href="#" onclick="ODBInlineEdit(this.parentNode, \'' + keyPath + '\');return false;" ' +
            'onfocus="ODBInlineEdit(this.parentNode, \'' + keyPath + '\')" title="Change value">';

         let v = key.value.toString();
         if (key.type === TID_STRING && v === "")
            v = "(empty)";
         else if (key.type === TID_STRING && v.trim() === "")
            v = "(spaces)";
         else if (key.type === TID_LINK)
            v = '<span style="color:red;background-color:yellow">(cannot resolve link)</span>';
         else if (key.type === TID_BOOL)
            v = (key.value ? 'y' : 'n') + ' (' + (key.value ? '1' : '0') + ')';
         else if (v.substring(0, 2) === "0x")
            v += " (" + parseInt(key.value) + ')';
         else if (key.type !== TID_STRING && key.type !== TID_LINK && key.type !== TID_FLOAT && key.type !== TID_DOUBLE)
            v += " (0x" + key.value.toString(16).toUpperCase() + ')';
         if (odb.picker || odb.handleColumn)
            td.innerHTML = v;
         else
            td.innerHTML = edit + v + '</a>';
      }
   }

   // Type
   td = document.createElement('TD');
   td.className = 'odbKey';
   td.setAttribute('name', 'odbExt');
   td.style.display = odb.detailsColumn ? 'table-cell' : 'none';
   tr.appendChild(td);
   td.appendChild(document.createTextNode(tid_name[key.type]));

   // #Val
   td = document.createElement('TD');
   td.className = 'odbKey';
   td.setAttribute('name', 'odbExt');
   td.style.display = odb.detailsColumn ? 'table-cell' : 'none';
   tr.appendChild(td);
   td.innerHTML = '<a href="#" onclick="resize_array(event, \''+keyPath+'\','+key.num_values+');return false;">'
      + key.num_values + '</a>';

   // Size
   td = document.createElement('TD');
   td.className = 'odbKey';
   td.setAttribute('name', 'odbExt');
   td.style.display = odb.detailsColumn ? 'table-cell' : 'none';
   tr.appendChild(td);
   if (key.type === TID_STRING)
      td.innerHTML = '<a href="#" onclick="resize_string(event, \''+keyPath+'\','+key.item_size+','+key.num_values+');return false;">'
         + key.item_size + '</a>';
   else
      td.innerHTML = key.item_size;

   // Written
   td = document.createElement('TD');
   td.className = 'odbKey';
   td.setAttribute('name', 'odbExt');
   td.style.display = odb.detailsColumn ? 'table-cell' : 'none';
   tr.appendChild(td);
   let s = Math.floor(0.5 + new Date().getTime() / 1000 - key.last_written);
   let t;
   if (s < 60)
      t = s + 's';
   else if (s < 60 * 60)
      t = Math.floor(0.5 + s / 60) + 'm';
   else if (s < 60 * 60 * 24)
      t = Math.floor(0.5 + s / 60 / 60) + 'h';
   else if (s < 60 * 60 * 24 * 99)
      t = Math.floor(0.5 + s / 60 / 60 / 24) + 'd';
   else
      t = ">99d";
   td.appendChild(document.createTextNode(t));

   // Mode
   td = document.createElement('TD');
   td.className = 'odbKey';
   td.setAttribute('name', 'odbExt');
   td.style.display = odb.detailsColumn ? 'table-cell' : 'none';
   tr.appendChild(td);
   let mode = key.access_mode;
   let m = "";
   if (mode & MODE_READ)
      m += "R";
   if (mode & MODE_WRITE)
      m += "W";
   if (mode & MODE_DELETE)
      m += "D";
   if (mode & MODE_EXCLUSIVE)
      m += "X";
   if (mode & MODE_WATCH)
      m += "W";
   td.appendChild(document.createTextNode(m));

   // invert color if selected
   if (tb.childNodes.length > row.i) {
      let selected = tb.childNodes[row.i].odbSelected;
      if (selected) {
         tr.style.backgroundColor = '#004CBD';
         change_color(tr, '#FFFFFF');
      }
   }

   if (row.i >= tb.childNodes.length) {
      // append new row if nothing exists
      tb.appendChild(tr);
   } else {
      // replace current row if it differs
      if (!tb.childNodes[row.i].isEqualNode(tr)) {
         if (tb.childNodes[row.i].childNodes[1].colSpan !== 1)
            tb.childNodes[row.i].childNodes[1].colSpan = "1";
         for (let i = 0; i < 8; i++) {
            let yellowBg = false;

            // check for data change in value column
            if (i === 2) {
               let oldElem = tb.childNodes[row.i].childNodes[i];
               if (oldElem === undefined)
                  oldElem = "";
               else {
                  while (oldElem.childNodes[0] !== undefined &&
                  (oldElem.tagName === 'TD' || oldElem.tagName === 'A' || oldElem.tagName === 'DIV'))
                     oldElem = oldElem.childNodes[0]; // get into <td> <a> <div> elements
                  oldElem = oldElem.parentNode.innerHTML;
               }
               let newElem = tr.childNodes[i];
               if (newElem === undefined)
                  newElem = "";
               else {
                  while (newElem.childNodes[0] !== undefined &&
                  (newElem.tagName === 'TD' || newElem.tagName === 'A' || newElem.tagName === 'DIV'))
                     newElem = newElem.childNodes[0]; // get into <td> <a> <div> elements
                  newElem = newElem.parentNode.innerHTML;
               }

               if (oldElem !== newElem &&  // value changed
                  !odb.skip_yellow &&       // skip if globally disabled
                  // skip if edit just finished
                  !(oldElem.indexOf('(') == -1 && newElem.indexOf('(') !== -1) &&
                  // skip '*' of arrays
                  newElem.indexOf('>*</a>') === -1)
                  yellowBg = true;
               else
                  yellowBg = false;
            }

            // check for edit mode
            let inlineEdit = find_input_element(tb.childNodes[row.i].childNodes[i]);

            if (tb.childNodes[row.i].childNodes[i] === undefined)
               tb.childNodes[row.i].appendChild(tr.childNodes[i].cloneNode(true));
            else if (!inlineEdit) {
               let e = tb.childNodes[row.i].childNodes[i];

               if (tb.childNodes[row.i].childNodes[i].innerHTML !== tr.childNodes[i].innerHTML)
                  tb.childNodes[row.i].childNodes[i].innerHTML = tr.childNodes[i].innerHTML;

               if (yellowBg) {
                  e.style.backgroundColor = 'var(--myellow)';
                  e.style.setProperty("-webkit-transition", "", "");
                  e.style.setProperty("transition", "", "");
                  e.age = new Date() / 1000;
               }

               if (e.age !== undefined && new Date() / 1000 > e.age + 1) {
                  e.style.setProperty("-webkit-transition", "background-color 1s", "");
                  e.style.setProperty("transition", "background-color 1s", "");
                  e.style.backgroundColor = "";
               }
            }
         }
      }
   }

   // Install mouse click handler
   if (tb.childNodes[row.i].childNodes[2] !== undefined)
      tb.childNodes[row.i].childNodes[2].removeEventListener('click', select_array_element);
   tb.childNodes[row.i].addEventListener('click', select_key);

   if (!odb.picker)
      tb.childNodes[row.i].addEventListener('contextmenu', context_menu);

   if (!odb.picer && key.type !== TID_KEY)
      tb.childNodes[row.i].childNodes[1].childNodes[1].addEventListener('click', rename_key_click);

   // Remember ODB path and key in row
   tb.childNodes[row.i].odbPath = keyPath;
   tb.childNodes[row.i].key = key;

   // Copy visibility for odbOptions
   tb.childNodes[row.i].style.display = tr.style.display;

   // Print array values
   if (Array.isArray(key.value)) {
      if (key.expanded === false) {
         // do nothing
      } else for (let i=0 ; i<key.value.length ; i++) {
         row.i++;

         // return if in edit mode
         if (tb.childNodes[row.i] !== undefined &&
            tb.childNodes[row.i].childNodes[2] !== undefined &&
            tb.childNodes[row.i].childNodes[2].inEdit)
            continue;

         // create empty row
         let tr = document.createElement('TR');

         // store key path
         tr.odbPath = keyPath + '[' + i + ']';

         // hide option keys if not in detailed column mode
         if (key.options && !odb.detailsColumn)
            tr.style.display = 'none';

         // Handle column (empty for array values)
         let td = document.createElement('TD');
         td.className = 'odbKey';
         td.setAttribute('name', 'odbHandle');
         td.style.display = odb.handleColumn ? 'table-cell' : 'none';
         td.style.width = "10px";
         tr.appendChild(td);

         // Key name
         td = document.createElement('TD');
         td.className = 'odbKey';
         tr.appendChild(td);

         // Key value
         td = document.createElement('TD');
         td.className = 'odbKey';
         tr.appendChild(td);
         let p = (path === '/' ? '/' + key.name : path + '/' + key.name);
         p += '['+i+']';
         let edit = '<a href="#" onclick="ODBInlineEdit(this.parentNode, \''+p+'\');return false;"  '+
            'onfocus="ODBInlineEdit(this.parentNode, \''+p+'\')" title="Change array element">';

         let v = key.value[i].toString();
         if (key.type === TID_STRING && v === "")
            v = "(empty)";
         else if (key.type === TID_BOOL)
            v = (key.value[i] ? 'y' : 'n') + ' (' + (key.value[i] ? '1' : '0') +')';
         else if (v.substring(0, 2) === "0x")
            v += " (" + parseInt(key.value[i]) + ')';
         else if (key.type !== TID_STRING && key.type !== TID_LINK && key.type !== TID_FLOAT && key.type !== TID_DOUBLE)
            v += " (0x" + key.value[i].toString(16).toUpperCase() + ')';
         td.innerHTML = '['+i+'] '+ edit + v + '</a>';

         // Empty fill cells
         for (let i=0 ; i<5 ; i++) {
            td = document.createElement('TD');
            td.className = 'odbKey';
            td.setAttribute('name', 'odbExt');
            td.style.display = odb.detailsColumn ? 'table-cell' : 'none';
            tr.appendChild(td);
         }

         if (row.i >= tb.childNodes.length) {
            tb.appendChild(tr);
         } else {
            if (!tb.childNodes[row.i].isEqualNode(tr)) {
               if (tr.childNodes.length === 1) { // Subdir
                  tb.childNodes[row.i].replaceWith(tr);
               } else if (tr.childNodes.length === 8) { // Key
                  let odbSelected = tb.childNodes[row.i].odbSelected;
                  let odbLastSelected = tb.childNodes[row.i].odbLastSelected;
                  if (tb.childNodes[row.i].childNodes[1].colSpan !== 1)
                     tb.childNodes[row.i].childNodes[1].colSpan = "1";
                  for (let i = 0; i < 8; i++) {
                     let changed = false;
                     let oldValue;
                     let newValue;

                     if (i === 2) {
                        if (tb.childNodes[row.i].childNodes[2] !== undefined) {
                           if (tb.childNodes[row.i].childNodes[2].childNodes[1] !== undefined)
                              oldValue = tb.childNodes[row.i].childNodes[2].childNodes[1].innerHTML;
                           else if (tb.childNodes[row.i].childNodes[2].childNodes[0] !== undefined)
                              oldValue = tb.childNodes[row.i].childNodes[2].childNodes[0].innerHTML
                           else
                              oldValue = tb.childNodes[row.i].childNodes[2].innerHTML;
                        }
                        newValue = tr.childNodes[2].childNodes[1].innerHTML;
                     }

                     if (oldValue !== undefined &&
                        oldValue !== newValue &&  // value changed
                        i === 2 &&                // we are in value column
                        !odb.skip_yellow &&       // skip if globally disabled
                        // skip if edit just finished
                        !(oldValue.indexOf('(') == -1 && newValue.indexOf('(') !== -1) &&
                        // skip '*' of arrays
                        newValue.indexOf('>*</a>') === -1 &&
                        tb.childNodes[row.i].odbSelected !== true)
                        changed = true;

                     if (tb.childNodes[row.i].childNodes[i] === undefined)
                        tb.childNodes[row.i].appendChild(tr.childNodes[i].cloneNode(true));
                     else {
                        // preserve color if key is selected
                        let c = tb.childNodes[row.i].childNodes[i].style.color;
                        tb.childNodes[row.i].childNodes[i].innerHTML = tr.childNodes[i].innerHTML;
                        change_color(tb.childNodes[row.i].childNodes[i], c);
                     }

                     let e = tb.childNodes[row.i].childNodes[i];
                     if (changed) {
                        e.style.backgroundColor = 'var(--myellow)';
                        e.style.setProperty("-webkit-transition", "", "");
                        e.style.setProperty("transition", "", "");
                        e.age = new Date() / 1000;
                     }

                     if (e.age !== undefined && new Date() / 1000 > e.age + 1) {
                        e.style.setProperty("-webkit-transition", "background-color 1s", "");
                        e.style.setProperty("transition", "background-color 1s", "");
                        e.style.backgroundColor = "";
                     }
                  }

                  tb.childNodes[row.i].odbSelected = odbSelected;
                  tb.childNodes[row.i].odbLastSelected = odbLastSelected;
               }
            }
         }

         // remove/install mouse click handlers
         tb.childNodes[row.i].removeEventListener('click', select_key);
         if (!odb.picker)
            tb.childNodes[row.i].removeEventListener('contextmenu', context_menu);
         tb.childNodes[row.i].childNodes[2].addEventListener('click', select_array_element);
         tb.childNodes[row.i].odbPath = tr.odbPath;
         tb.childNodes[row.i].key = undefined;

         tb.childNodes[row.i].style.display = tr.style.display;

      }
   }
}

function resize_array(event, path, n) {
   event.stopPropagation(); // do not select row
   dlgQuery("Enter size of array:", n, do_resize_array, path);
}

function do_resize_array(n, path) {
   mjsonrpc_db_resize([path],[parseInt(n)]).then(rpc => {
   }).catch(error => mjsonrpc_error_alert(error));
}

function resize_string(event, path, size, num_values) {
   event.stopPropagation(); // do not select row
   dlgQuery("Enter new string size:", size, do_resize_string, { "path": path, "num_values": num_values });
}

function do_resize_string(size, p) {
   mjsonrpc_call("db_resize_string",
      { "paths": [p.path],
         "new_lengths": [parseInt(p.num_values)],
         "new_string_lengths": [parseInt(size)]}).then(rpc => {
   }).catch(error => mjsonrpc_error_alert(error));
}

function dlgCreateKeyDown(event, inp) {
   let keyCode = ('which' in event) ? event.which : event.keyCode;

   if (keyCode === 27) {
      // cancel editing
      dlgHide('dlgCreate');
      return false;
   }

   if (keyCode === 13) {
      // finish editing
      if (do_new_key(event.target))
         dlgHide('dlgCreate');
      return false;
   }

   return true;
}

function dlgCreateLinkKeyDown(event, inp) {
   let keyCode = ('which' in event) ? event.which : event.keyCode;

   if (keyCode === 27) {
      // cancel editing
      dlgHide('dlgCreateLink');
      return false;
   }

   if (keyCode === 13) {
      // finish editing
      if (do_new_link(this))
         dlgHide('dlgCreateLink');
      return false;
   }

   return true;
}

function drag_start(event) {
   let tr = event.target;
   while (tr.tagName !== 'TR')
      tr = tr.parentNode;
   let tb = getOdbTb(tr);

   tb.dragTargetRow = Array.from(tb.children).indexOf(tr);
   tb.dragSourceRow = tb.dragTargetRow;
   tb.dragSource = tr.odbPath;
   tb.dragRowContent = tr.cloneNode(true);

   window.setTimeout(() => {
      window.clearTimeout(tb.odb.updateTimer);
      let w = window.getComputedStyle(tr).width;
      let h = window.getComputedStyle(tr).height;
      tr.innerHTML = '<td colspan="8" style="background-color:white; border: 2px dashed #6bb28c"></td>';
      tr.style.height = parseInt(h) + "px";
      tr.style.width = parseInt(w) + "px";
   }, 10);

}

function drag_move(event, td) {
   event.preventDefault();

   let tr = event.target;
   while (tr.tagName !== 'TR')
      tr = tr.parentNode;
   let tb = getOdbTb(tr);
   let children = Array.from(tb.children);

   if (children.indexOf(tr) > tb.dragTargetRow)
      tr.after(tb.childNodes[tb.dragTargetRow]);
   else if (children.indexOf(tr) < tb.dragTargetRow)
      tr.before(tb.childNodes[tb.dragTargetRow]);

   tb.dragTargetRow = children.indexOf(tr);
}

function drag_end(event, td) {
   let tr = event.target;
   while (tr.tagName !== 'TR')
      tr = tr.parentNode;
   let tb = getOdbTb(tr);

   let ttr = tb.childNodes[tb.dragTargetRow];
   ttr.innerHTML = tb.dragRowContent.innerHTML;
   ttr.style.height = "";
   ttr.style.width = "";

   if (tb.dragSourceRow !== tb.dragTargetRow) {
      mjsonrpc_call("db_reorder", {"paths": [tb.dragSource], "indices": [tb.dragTargetRow - 4]}).then(rpc => {
         tb.odb.updateTimer = window.setTimeout(odb_update, 10, tb);
      }).catch(error => mjsonrpc_error_alert(error));
   } else
      tb.odb.updateTimer = window.setTimeout(odb_update, 10, tb);
}

function show_open_records(e) {
   let odb = getOdbTb(e).odb;
   let title = "ODB open records under \"" + odb.path + "\"";

   let d = document.createElement("div");
   d.className = "dlgFrame";
   d.style.zIndex = "30";
   d.style.minWidth = "400px";
   d.shouldDestroy = true;

   d.innerHTML = "<div class=\"dlgTitlebar\" id=\"dlgMessageTitle\">" + title + "</div>" +
      "<div class=\"dlgPanel\" style=\"padding: 2px;\">" +
      "<div id=\"dlgSOR\"></div>" +
      "<button class=\"dlgButton\" id=\"dlgMessageButton\" +" +
      "type=\"button\" " +
      " onClick=\"dlgMessageDestroy(this)\">Close</button>" +
      "</div>";

   document.body.appendChild(d);

   update_open_records(odb);
   dlgShow(d, false);
}

function update_open_records(odb) {
   let path = odb.path;
   mjsonrpc_call("db_sor", {"path": path}).then (rpc => {
      let sor = rpc.result.sor;
      let paths = {};
      for (const s of sor) {
         if (paths[s.path])
            paths[s.path] += ', ' + s.name;
         else
            paths[s.path] = s.name;
      }
      let sorted_paths = Object.keys(paths).sort();
      let html = '<table class="mtable" style="width: 100%;margin-top: 0;margin-bottom:0"><tbody>';
      html += '<tr><th>ODB Path</th><th>Open by</th></tr>';
      for (const p of sorted_paths)
         html += '<tr><td style="padding: 8px">' + p + '</td>' +
            '<td style="padding: 8px">' + paths[p] + '</td></tr>';
      html += '</tbody></table>';

      let d = document.getElementById('dlgSOR');
      if (d === null)
         return; // dialog has been closed
      if (d.innerHTML !== html) {
         d.innerHTML = html;
         dlgCenter(d.parentElement.parentElement);
      }
      window.setTimeout(update_open_records, 1000, odb);

   }).catch( error => mjsonrpc_error_alert(error));
}
