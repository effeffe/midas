// -*- mode: js; js-indent-level: 3; -*-
/********************************************************************\

 Name:         mhistory.js
 Created by:   Stefan Ritt

 Contents:     JavaScript history plotting routines

 Note: please load midas.js and mhttpd.js before mhistory.js

 \********************************************************************/

LN10 = 2.302585094;
LOG2 = 0.301029996;
LOG5 = 0.698970005;

function profile(flag) {
   if (flag === true || flag === undefined) {
      console.log("");
      profile.startTime = new Date().getTime();
      return;
   }

   let now = new Date().getTime();
   console.log("Profile: " + flag + ": " + (now-profile.startTime) + "ms");
   profile.startTime = new Date().getTime();
}

function mhistory_init() {
   // go through all data-name="mhistory" tags
   let mhist = Array.from(document.getElementsByTagName("div")).filter(d => {
      return d.className === "mjshistory";
   });

   let baseURL = window.location.href;
   if (baseURL.indexOf("?cmd") > 0)
      baseURL = baseURL.substr(0, baseURL.indexOf("?cmd"));
   baseURL += "?cmd=history";

   for (let i = 0; i < mhist.length; i++) {
      mhist[i].dataset.baseURL = baseURL;
      mhist[i].mhg = new MhistoryGraph(mhist[i]);
      mhist[i].mhg.initializePanel(i);
      mhist[i].mhg.resize();
      mhist[i].resize = function () {
         this.mhg.resize();
      };
   }
}

function mhistory_create(parentElement, baseURL, group, panel, tMin, tMax, index) {
   let d = document.createElement("div");
   parentElement.appendChild(d);
   d.dataset.baseURL = baseURL;
   d.dataset.group = group;
   d.dataset.panel = panel;
   d.mhg = new MhistoryGraph(d);
   if (!Number.isNaN(tMin) && !Number.isNaN(tMax)) {
      d.mhg.initTMin = tMin;
      d.mhg.initTMax = tMax;
   }
   d.mhg.initializePanel(index);
   return d;
}

function getUrlVars() {
   let vars = {};
   window.location.href.replace(/[?&]+([^=&]+)=([^&]*)/gi, function (m, key, value) {
      vars[key] = value;
   });
   return vars;
}

function MhistoryGraph(divElement) { // Constructor

   // create canvas inside the div
   this.parentDiv = divElement;
   this.baseURL = divElement.dataset.baseURL;
   this.group = divElement.dataset.group;
   this.panel = divElement.dataset.panel;
   this.canvas = document.createElement("canvas");
   this.canvas.style.border = "1ps solid black";
   divElement.appendChild(this.canvas);

   // colors
   this.color = {
      background: "#FFFFFF",
      axis: "#808080",
      grid: "#F0F0F0",
      label: "#404040",
      data: [
         "#00AAFF", "#FF9000", "#FF00A0", "#00C030",
         "#A0C0D0", "#D0A060", "#C04010", "#807060",
         "#F0C000", "#2090A0", "#D040D0", "#90B000",
         "#B0B040", "#B0B0FF", "#FFA0A0", "#A0FFA0"],
   };

   // scales
   this.tScale = 3600;
   this.yMin0 = undefined;
   this.yMax0 = undefined;
   this.tMax = Math.floor(new Date() / 1000);
   this.tMin = this.tMax - this.tScale;
   this.yMin = undefined;
   this.yMax = undefined;
   this.scroll = true;
   this.yZoom = false;
   this.showZoomButtons = true;
   this.tMinRequested = 0;
   this.tMinReceived = 0;

   // overwrite scale from URL if present
   let tMin = decodeURI(getUrlVars()["A"]);
   if (tMin !== "undefined") {
      this.initTMin = tMin;
      this.tMin = tMin;
   }
   let tMax = decodeURI(getUrlVars()["B"]);
   if (tMax !== "undefined") {
      this.initTMax = tMax;
      this.tMax = tMax;
   }

   // data arrays
   this.data = [];
   this.lastWritten = [];

   // graph arrays (in screen pixels)
   this.x = [];
   this.y = [];
   // t/v arrays corresponding to x/y
   this.t = [];
   this.v = [];

   // points array with min/max/avg
   this.p = [];

   // dragging
   this.drag = {
      active: false,
      lastT: 0,
      lastOffsetX: 0,
      lastDt: 0,
      lastVt: 0,
      lastMoveT : 0
   };

   // axis zoom
   this.zoom = {
      x: {active: false},
      y: {active: false}
   };

   // callbacks when certain actions are performed.
   // All callback functions should accept a single parameter, which is the 
   // MhistoryGraph object that triggered the callback.
   this.callbacks = {
      resetAxes: undefined,
      timeZoom: undefined,
      jumpToCurrent: undefined
   };

   // marker
   this.marker = {active: false};
   this.variablesWidth = 0;
   this.variablesHeight = 0;

   // labels
   this.showLabels = false;

   // solo
   this.solo = {active: false, index: undefined};

   // time when panel was drawn last
   this.lastDrawTime = 0;
   this.forceRedraw = false;

   // buttons
   this.button = [
      {
         src: "menu.svg",
         title: "Show / hide legend",
         click: function (t) {
            t.showLabels = !t.showLabels;
            t.redraw(true);
         }
      },
      {
         src: "maximize-2.svg",
         title: "Show only this plot",
         click: function (t) {
            window.location.href = t.baseURL + "&group=" + t.group + "&panel=" + t.panel;
         }
      },
      {
         src: "rotate-ccw.svg",
         title: "Reset histogram axes",
         click: function (t) {
            t.resetAxes();

            if (t.callbacks.resetAxes !== undefined) {
               t.callbacks.resetAxes(t);
            }
         }
      },
      {
         src: "play.svg",
         title: "Jump to current time",
         click: function (t) {
            t.scroll = true;
            t.scrollRedraw();

            if (t.callbacks.jumpToCurrent !== undefined) {
               t.callbacks.jumpToCurrent(t);
            }
         }
      },
      {
         src: "clock.svg",
         title: "Select timespan...",
         click: function (t) {
            if (t.intSelector.style.display === "none") {
               t.intSelector.style.display = "block";
               t.intSelector.style.left = ((t.canvas.getBoundingClientRect().x + window.pageXOffset +
                  t.x2) - t.intSelector.offsetWidth) + "px";
               t.intSelector.style.top = (t.canvas.getBoundingClientRect().y + window.pageYOffset +
                  this.y1 - 1) + "px";
            } else {
               t.intSelector.style.display = "none";
            }
         }
      },
      {
         src: "download.svg",
         title: "Download image/data...",
         click: function (t) {
            if (t.downloadSelector.style.display === "none") {
               t.downloadSelector.style.display = "block";
               t.downloadSelector.style.left = ((t.canvas.getBoundingClientRect().x + window.pageXOffset +
                  t.x2) - t.downloadSelector.offsetWidth) + "px";
               t.downloadSelector.style.top = (t.canvas.getBoundingClientRect().y + window.pageYOffset +
                  this.y1 - 1) + "px";
            } else {
               t.downloadSelector.style.display = "none";
            }
         }
      },
      {
         src: "settings.svg",
         title: "Configure this plot",
         click: function (t) {
            window.location.href = "?cmd=oldhistory&group=" + t.group + "&panel=" + t.panel
               + "&hcmd=Config" + "&redir=" + encodeURIComponent(window.location.href);
         }
      },
      {
         src: "help-circle.svg",
         title: "Show help",
         click: function () {
            dlgShow("dlgHelp", false);
         }
      }
   ];

   // load dialogs
   dlgLoad('dlgHistory.html');

   this.button.forEach(b => {
      b.img = new Image();
      b.img.src = "icons/" + b.src;
   });

   // marker
   this.marker = {active: false};

   // mouse event handlers
   divElement.addEventListener("mousedown", this.mouseEvent.bind(this), true);
   divElement.addEventListener("dblclick", this.mouseEvent.bind(this), true);
   divElement.addEventListener("mousemove", this.mouseEvent.bind(this), true);
   divElement.addEventListener("mouseup", this.mouseEvent.bind(this), true);
   divElement.addEventListener("wheel", this.mouseWheelEvent.bind(this), true);

   // Keyboard event handler (has to be on the window!)
   window.addEventListener("keydown", this.keyDown.bind(this));
}

function timeToSec(str) {
   let s = parseFloat(str);
   switch (str[str.length - 1]) {
      case 'm':
      case 'M':
         s *= 60;
         break;
      case 'h':
      case 'H':
         s *= 3600;
         break;
      case 'd':
      case 'D':
         s *= 3600 * 24;
         break;
   }

   return s;
}

function doQueryAB(t) {

   dlgHide('dlgQueryAB');

   let d1 = new Date(
      document.getElementById('y1').value,
      document.getElementById('m1').selectedIndex,
      document.getElementById('d1').selectedIndex + 1,
      document.getElementById('h1').selectedIndex);

   let d2 = new Date(
      document.getElementById('y2').value,
      document.getElementById('m2').selectedIndex,
      document.getElementById('d2').selectedIndex + 1,
      document.getElementById('h2').selectedIndex);

   if (d1 > d2)
      [d1, d2] = [d2, d1];

   t.tMin = d1.getTime() / 1000;
   t.tMax = d2.getTime() / 1000;
   t.scroll = false;
   t.loadOldData();
   t.redraw(true);

   if (t.callbacks.timeZoom !== undefined)
      t.callbacks.timeZoom(t);
}

MhistoryGraph.prototype.keyDown = function (e) {
   if (e.key === "u") {  // 'u' key
      this.scroll = true;
      this.scrollRedraw();
      e.preventDefault();
   }
   if (e.key === "r") {  // 'r' key
      this.resetAxes();
      e.preventDefault();
   }
   if (e.key === "Escape") {
      this.solo.active = false;
      this.redraw(true);
      e.preventDefault();
   }
   if (e.key === "y") {
      this.yZoom = false;
      this.findMinMax();
      this.redraw(true);
      e.preventDefault();
   }
};

MhistoryGraph.prototype.initializePanel = function (index) {

   // Retrieve group and panel
   this.group = this.parentDiv.dataset.group;
   this.panel = this.parentDiv.dataset.panel;

   if (this.group === undefined) {
      dlgMessage("Error", "Definition of \'dataset-group\' missing for history panel \'" + this.parentDiv.id + "\'. " +
         "Please use syntax:<br /><br /><b>&lt;div class=\"mjshistory\" " +
         "data-group=\"&lt;Group&gt;\" data-panel=\"&lt;Panel&gt;\"&gt;&lt;/div&gt;</b>", true);
      return;
   }
   if (this.panel === undefined) {
      dlgMessage("Error", "Definition of \'dataset-panel\' missing for history panel \'" + this.parentDiv.id + "\'. " +
         "Please use syntax:<br /><br /><b>&lt;div class=\"mjshistory\" " +
         "data-group=\"&lt;Group&gt;\" data-panel=\"&lt;Panel&gt;\"&gt;&lt;/div&gt;</b>", true);
      return;
   }

   if (this.group === "" || this.panel === "")
      return;

   this.plotIndex = index;
   this.marker = {active: false};
   this.drag = {active: false};
   this.data = undefined;
   this.x = [];
   this.y = [];
   this.events = [];
   this.tags = [];
   this.index = [];
   this.pendingUpdates = 0;

   // retrieve panel definition from ODB
   mjsonrpc_db_copy(["/History/Display/" + this.group + "/" + this.panel]).then(function (rpc) {
      if (rpc.result.status[0] !== 1) {
         dlgMessage("Error", "Panel \'" + this.group + "/" + this.panel + "\' not found in ODB", true)
      } else {
         this.odb = rpc.result.data[0];
         this.loadInitialData();
      }
   }.bind(this)).catch(function (error) {
      if (error.xhr !== undefined)
         mjsonrpc_error_alert(error);
      else
         throw(error);
   });
};

MhistoryGraph.prototype.updateLastWritten = function () {
   //console.log("update last_written!!!\n");

   // load date of latest data points
   mjsonrpc_call("hs_get_last_written",
      {
         "time": this.tMin,
         "events": this.events,
         "tags": this.tags,
         "index": this.index
      }).then(function (rpc) {
      this.lastWritten = rpc.result.last_written;
      // protect against an infinite loop from draw() if rpc returns invalid times.
      // by definition, last_written returned by RPC is supposed to be less then tMin.
      for (let i = 0; i < this.lastWritten.length; i++) {
         let l = this.lastWritten[i];
         //console.log("updated last_written: event: " + this.events[i] + ", l: " + l + ", tmin: " + this.tMin + ", diff: " + (l - this.tMin));
         if (l > this.tMin) {
            this.lastWritten[i] = this.tMin;
         }
      }
      this.redraw(true);
   }.bind(this))
      .catch(function (error) {
         mjsonrpc_error_alert(error);
      });
}

MhistoryGraph.prototype.loadInitialData = function () {

   this.lastTimeStamp = Math.floor(Date.now() / 1000);

   if (this.initTMin !== undefined && this.initTMin !== "undefined") {
      this.tMin = this.initTMin;
      this.tMax = this.initTMax;
      this.tScale = this.tMax - this.tMin;
      this.scroll = false;
   } else {
      this.tScale = timeToSec(this.odb["Timescale"]);

      // overwrite via <div ... data-scale=<value> >
      if (this.parentDiv.dataset.scale !== undefined)
         this.tScale = timeToSec(this.parentDiv.dataset.scale);

      this.tMax = Math.floor(new Date() / 1000);
      this.tMin = this.tMax - this.tScale;
   }

   this.showLabels = this.odb["Show values"];
   this.showFill = this.odb["Show fill"];

   this.autoscaleMin = (this.odb["Minimum"] === this.odb["Maximum"] ||
      this.odb["Minimum"] === "-Infinity" || this.odb["Minimum"] === "Infinity");
   this.autoscaleMax = (this.odb["Minimum"] === this.odb["Maximum"] ||
      this.odb["Maximum"] === "-Infinity" || this.odb["Maximum"] === "Infinity");

   if (this.odb["Zero ylow"]) {
      this.autoscaleMin = false;
      this.odb["Minimum"] = 0;
   }

   this.logAxis = this.odb["Log axis"];

   // if only one variable present, convert it to array[0]
   if (!Array.isArray(this.odb.Variables))
      this.odb.Variables = new Array(this.odb.Variables);
   if (!Array.isArray(this.odb.Label))
      this.odb.Label = new Array(this.odb.Label);
   if (!Array.isArray(this.odb.Colour))
      this.odb.Colour = new Array(this.odb.Colour);

   this.odb["Variables"].forEach(v => {
      this.events.push(v.substr(0, v.indexOf(':')));
      let t = v.substr(v.indexOf(':') + 1);
      if (t.indexOf('[') !== -1) {
         this.tags.push(t.substr(0, t.indexOf('[')));
         this.index.push(parseInt(t.substr(t.indexOf('[') + 1)));
      } else {
         this.tags.push(t);
         this.index.push(0);
      }
   });

   if (this.odb["Show run markers"]) {
      this.events.push("Run transitions");
      this.events.push("Run transitions");

      this.tags.push("State");
      this.tags.push("Run number");
      this.index.push(0);
      this.index.push(0);
   }

   // interval selector
   this.intSelector = document.createElement("div");
   this.intSelector.id = "intSel";
   this.intSelector.style.display = "none";
   this.intSelector.style.position = "absolute";
   this.intSelector.className = "mtable";
   this.intSelector.style.borderRadius = "0";
   this.intSelector.style.border = "2px solid #808080";
   this.intSelector.style.margin = "0";
   this.intSelector.style.padding = "0";

   this.intSelector.style.left = "100px";
   this.intSelector.style.top = "100px";

   let table = document.createElement("table");
   let row = null;
   let cell;
   let link;
   let buttons = this.odb["Buttons"];
   if (buttons === undefined) {
      buttons = [];
      buttons.push("10m", "1h", "3h", "12h", "24h", "3d", "7d");
   }
   buttons.push("A&rarr;B");
   buttons.push("&lt;&lt;&lt;");
   buttons.push("&lt;&lt;");
   buttons.forEach(function (b, i) {
      if (i % 2 === 0)
         row = document.createElement("tr");

      cell = document.createElement("td");
      cell.style.padding = "0";

      link = document.createElement("a");
      link.href = "#";
      link.innerHTML = b;
      if (b === "A&rarr;B")
         link.title = "Display data between two dates";
      else if (b === "&lt;&lt;")
         link.title = "Go back in time to last available data";
      else if (b === "&lt;&lt;&lt;")
         link.title = "Go back in time to last available data for all variables on plot";
      else
         link.title = "Show last " + b;

      let mhg = this;
      link.onclick = function () {
         if (b === "A&rarr;B") {
            let currentYear = new Date().getFullYear();
            let dMin = new Date(this.tMin * 1000);
            let dMax = new Date(this.tMax * 1000);

            if (document.getElementById('y1').length === 0) {
               for (let i = currentYear; i > currentYear - 5; i--) {
                  let o = document.createElement('option');
                  o.value = i.toString();
                  o.appendChild(document.createTextNode(i.toString()));
                  document.getElementById('y1').appendChild(o);
                  o = document.createElement('option');
                  o.value = i.toString();
                  o.appendChild(document.createTextNode(i.toString()));
                  document.getElementById('y2').appendChild(o);
               }
            }

            document.getElementById('m1').selectedIndex = dMin.getMonth();
            document.getElementById('d1').selectedIndex = dMin.getDate() - 1;
            document.getElementById('h1').selectedIndex = dMin.getHours();
            document.getElementById('y1').selectedIndex = currentYear - dMin.getFullYear();

            document.getElementById('m2').selectedIndex = dMax.getMonth();
            document.getElementById('d2').selectedIndex = dMax.getDate() - 1;
            document.getElementById('h2').selectedIndex = dMax.getHours();
            document.getElementById('y2').selectedIndex = currentYear - dMax.getFullYear();

            document.getElementById('dlgQueryQuery').onclick = function () {
               doQueryAB(this);
            }.bind(this);

            dlgShow("dlgQueryAB");

         } else if (b === "&lt;&lt;") {

            mjsonrpc_call("hs_get_last_written",
               {
                  "time": this.tMin,
                  "events": this.events,
                  "tags": this.tags,
                  "index": this.index
               })
               .then(function (rpc) {

                  let last = rpc.result.last_written[0];
                  for (let i = 0; i < rpc.result.last_written.length; i++) {
                     if (this.events[i] === "Run transitions") {
                        continue;
                     }
                     let l = rpc.result.last_written[i];
                     last = Math.max(last, l);
                  }

                  if (last !== 0) { // no data, at all!
                     let scale = mhg.tMax - mhg.tMin;
                     mhg.tMax = last + scale / 2;
                     mhg.tMin = last - scale / 2;

                     mhg.scroll = false;
                     mhg.marker.active = false;
                     mhg.loadOldData();
                     mhg.redraw(true);

                     if (mhg.callbacks.timeZoom !== undefined)
                        mhg.callbacks.timeZoom(mhg);
                  }

               }.bind(this))
               .catch(function (error) {
                  mjsonrpc_error_alert(error);
               });

         } else if (b === "&lt;&lt;&lt;") {

            mjsonrpc_call("hs_get_last_written",
               {
                  "time": this.tMin,
                  "events": this.events,
                  "tags": this.tags,
                  "index": this.index
               })
               .then(function (rpc) {

                  let last = 0;
                  for (let i = 0; i < rpc.result.last_written.length; i++) {
                     let l = rpc.result.last_written[i];
                     if (this.events[i] === "Run transitions") {
                        continue;
                     }
                     if (last === 0) {
                        // no data for first variable
                        last = l;
                     } else if (l === 0) {
                        // no data for this variable
                     } else {
                        last = Math.min(last, l);
                     }
                  }
                  //console.log("last: " + last);

                  if (last !== 0) { // no data, at all!
                     let scale = mhg.tMax - mhg.tMin;
                     mhg.tMax = last + scale / 2;
                     mhg.tMin = last - scale / 2;

                     mhg.scroll = false;
                     mhg.marker.active = false;
                     mhg.loadOldData();
                     mhg.redraw(true);

                     if (mhg.callbacks.timeZoom !== undefined)
                        mhg.callbacks.timeZoom(mhg);
                  }

               }.bind(this))
               .catch(function (error) {
                  mjsonrpc_error_alert(error);
               });

         } else {

            mhg.tMax = new Date() / 1000;
            mhg.tMin = mhg.tMax - timeToSec(b);
            mhg.scroll = true;
            mhg.loadOldData();
            mhg.scrollRedraw();

            if (mhg.callbacks.timeZoom !== undefined)
               mhg.callbacks.timeZoom(mhg);
         }
         mhg.intSelector.style.display = "none";
         return false;
      }.bind(this);

      cell.appendChild(link);
      row.appendChild(cell);
      if (i % 2 === 1)
         table.appendChild(row);
   }, this);

   if (buttons.length % 2 === 1)
      table.appendChild(row);

   this.intSelector.appendChild(table);
   document.body.appendChild(this.intSelector);

   // download selector
   this.downloadSelector = document.createElement("div");
   this.downloadSelector.id = "downloadSel";
   this.downloadSelector.style.display = "none";
   this.downloadSelector.style.position = "absolute";
   this.downloadSelector.className = "mtable";
   this.downloadSelector.style.borderRadius = "0";
   this.downloadSelector.style.border = "2px solid #808080";
   this.downloadSelector.style.margin = "0";
   this.downloadSelector.style.padding = "0";

   this.downloadSelector.style.left = "100px";
   this.downloadSelector.style.top = "100px";

   table = document.createElement("table");
   let mhg = this;

   row = document.createElement("tr");
   cell = document.createElement("td");
   cell.style.padding = "0";
   link = document.createElement("a");
   link.href = "#";
   link.innerHTML = "CSV";
   link.title = "Download data in Comma Separated Value format";
   link.onclick = function () {
      mhg.downloadSelector.style.display = "none";
      mhg.download("CSV");
      return false;
   }.bind(this);
   cell.appendChild(link);
   row.appendChild(cell);
   table.appendChild(row);

   row = document.createElement("tr");
   cell = document.createElement("td");
   cell.style.padding = "0";
   link = document.createElement("a");
   link.href = "#";
   link.innerHTML = "PNG";
   link.title = "Download image in PNG format";
   link.onclick = function () {
      mhg.downloadSelector.style.display = "none";
      mhg.download("PNG");
      return false;
   }.bind(this);
   cell.appendChild(link);
   row.appendChild(cell);
   table.appendChild(row);

   this.downloadSelector.appendChild(table);
   document.body.appendChild(this.downloadSelector);

   // load initial data
   this.tMinRequested = this.tMin - this.tScale; // look one window ahead in past

   // limit one request to maximum one month
   if (this.lastTimeStamp - this.tMinRequested > 3600*24*30)
      this.tMinRequested = this.lastTimeStamp - 3600*24*30;

   this.pendingUpdates++;
   this.parentDiv.style.cursor = "progress";
   mjsonrpc_call("hs_read_arraybuffer",
      {
         "start_time": Math.floor(this.tMinRequested),
         "end_time": Math.floor(this.lastTimeStamp),
         "events": this.events,
         "tags": this.tags,
         "index": this.index
      }, "arraybuffer")
      .then(function (rpc) {

         this.receiveData(rpc);
         this.tMinReceived = this.tMinRequested;

         this.findMinMax();
         this.redraw(true);

         if (this.tMin - this.tScale < this.tMinRequested) {
            this.pendingUpdates--;
            this.loadOldData();
            return;
         }

         this.pendingUpdates--;
         if (this.pendingUpdates === 0)
            this.parentDiv.style.cursor = "default";

         if (this.updateTimer === undefined)
            this.updateTimer = window.setTimeout(this.update.bind(this), 1000);
         if (this.scrollTimer === undefined)
            this.scrollTimer = window.setTimeout(this.scrollRedraw.bind(this), 100);

      }.bind(this))
      .catch(function (error) {
         mjsonrpc_error_alert(error);
      });
};

MhistoryGraph.prototype.loadOldData = function () {

   let dt = Math.floor(this.tMax - this.tMin);

   if (this.tMin - dt / 2 < this.tMinRequested) {

      let oldTMinRequested = this.tMinRequested;
      this.tMinRequested = this.tMin - dt;

      // limit one request to maximum one month
      if (oldTMinRequested - this.tMinRequested > 3600*24*30)
         this.tMinRequested = oldTMinRequested - 3600*24*30;

      this.pendingUpdates++;
      this.parentDiv.style.cursor = "progress";
      mjsonrpc_call("hs_read_arraybuffer",
         {
            "start_time": Math.floor(this.tMinRequested),
            "end_time": Math.floor(oldTMinRequested),
            "events": this.events,
            "tags": this.tags,
            "index": this.index
         }, "arraybuffer")
         .then(function (rpc) {

            this.receiveData(rpc);

            if (this.tMin - dt / 2 < this.tMinRequested) {
               this.tMinReceived = this.tMinRequested;
               this.findMinMax();
               this.redraw(true);
               this.pendingUpdates--;
               this.loadOldData();
               return;
            }

            this.pendingUpdates--;
            if (this.pendingUpdates === 0)
               this.parentDiv.style.cursor = "default";

            this.tMinReceived = this.tMinRequested;
            this.redraw(true);

         }.bind(this))
         .catch(function (error) {
            mjsonrpc_error_alert(error);
         });
   }
};

MhistoryGraph.prototype.receiveData = function (rpc) {

   // decode binary array
   let array = new Float64Array(rpc);
   let nVars = array[1];
   let nData = array.slice(2 + nVars, 2 + 2 * nVars);
   let i = 2 + 2 * nVars;

   if (i >= array.length) {
      // RPC did not return any data

      if (this.data === undefined) {
         // must initialize the arrays otherwise nothing works.
         this.data = [];
         for (let index = 0; index < nVars; index++) {
            this.data.push({time: [], value: []});
         }
      }

      return false;
   }

   // push initial data
   if (this.data === undefined) {
      this.data = [];
      for (let index = 0; index < nVars; index++) {
         this.data.push({time: [], value: []});
      }
   }

   // append new values to end of arrays
   for (let index = 0; index < nVars; index++) {
      if (nData[index] === 0)
         continue;
      let t0 = array[i];

      if (this.data[index].time.length === 0) {
         // initial data

         let formula = this.odb["Formula"];

         let x = undefined;
         let v = undefined;
         if (formula !== undefined && formula[index] !== undefined && formula[index] !== "") {
            for (let j = 0; j < nData[index]; j++) {
               this.data[index].time.push(array[i++]);
               x = array[i++];
               v = eval(formula[index]);
               this.data[index].value.push(v);
            }
         } else {
            for (let j = 0; j < nData[index]; j++) {
               let t = array[i++];
               v = array[i++];
               this.data[index].time.push(t);
               this.data[index].value.push(v);
            }
         }
      } else if (t0 < this.data[index].time[0]) {
         // add data to the left
         //profile();

         let formula = this.odb["Formula"];

         // if (this.t1 === undefined) {
         //    this.t1 = [];
         //    this.v1 = [];
         // }
         // this.t1.length = 0;
         // this.v1.length = 0;
         let t1 = [];
         let v1 = [];

         let x = undefined;
         if (formula !== undefined && formula[index] !== undefined && formula[index] !== "") {
            for (let j = 0; j < nData[index]; j++) {
               let t = array[i++];
               x = array[i++];
               let v = eval(formula[index]);
               if (t < this.data[index].time[0]) {
                  // this.t1.push(t);
                  // this.v1.push(v);
                  t1.push(t);
                  v1.push(v);
               }
            }
         } else {
            for (let j = 0; j < nData[index]; j++) {
               let t = array[i++];
               let v = array[i++];
               if (t < this.data[index].time[0]) {
                  // this.t1.push(t);
                  // this.v1.push(v);
                  t1.push(t);
                  v1.push(v);
               }
            }
         }
         // this.data[index].time = this.t1.concat(this.data[index].time);
         // this.data[index].value = this.v1.concat(this.data[index].value);
         this.data[index].time = t1.concat(this.data[index].time);
         this.data[index].value = v1.concat(this.data[index].value);

         //profile("concat");
      } else {
         // add data to the right

         let formula = this.odb["Formula"];

         let x = undefined;
         if (formula !== undefined && formula[index] !== undefined && formula[index] !== "") {
            for (let j = 0; j < nData[index]; j++) {
               let t = array[i++];
               x = array[i++];
               let v = eval(formula[index]);

               // add data to the right
               if (t > this.data[index].time[this.data[index].time.length - 1]) {

                  this.data[index].time.push(t);
                  this.data[index].value.push(v);

                  this.lastTimeStamp = t;
               }
            }
         } else {
            for (let j = 0; j < nData[index]; j++) {
               let t = array[i++];
               let v = array[i++];

               // add data to the right
               if (t > this.data[index].time[this.data[index].time.length - 1]) {

                  this.data[index].time.push(t);
                  this.data[index].value.push(v);

                  this.lastTimeStamp = t;
               }
            }
         }
      }
   }

   this.findMinMax();
   return true;
};

MhistoryGraph.prototype.update = function () {

   // don't update window if content is hidden (other tab, minimized, etc.)
   if (document.hidden) {
      this.updateTimer = window.setTimeout(this.update.bind(this), 500);
      return;
   }

   let t = Math.floor(new Date() / 1000);

   mjsonrpc_call("hs_read_arraybuffer",
      {
         "start_time": Math.floor(this.lastTimeStamp),
         "end_time": Math.floor(t),
         "events": this.events,
         "tags": this.tags,
         "index": this.index
      }, "arraybuffer")
      .then(function (rpc) {

         if (this.receiveData(rpc)) {
            this.findMinMax();
            this.redraw(true);
         }

         this.updateTimer = window.setTimeout(this.update.bind(this), 1000);

      }.bind(this)).catch(function (error) {
      mjsonrpc_error_alert(error);
   });
};

MhistoryGraph.prototype.scrollRedraw = function () {
   if (this.scroll) {
      let dt = this.tMax - this.tMin;
      this.tMax = Math.floor(new Date() / 1000);
      this.tMin = this.tMax - dt;
      this.findMinMax();
      this.redraw(true);

      // calculate time for one pixel
      dt = (this.tMax - this.tMin) / (this.x2 - this.x1);
      dt = Math.min(Math.max(0.1, dt), 60);
      this.scrollTimer = window.setTimeout(this.scrollRedraw.bind(this), dt / 2 * 1000);
   } else {
      this.scrollTimer = window.setTimeout(this.scrollRedraw.bind(this), 1000);
   }
};

function binarySearch(array, target) {
   let startIndex = 0;
   let endIndex = array.length - 1;
   let middleIndex;
   while (startIndex <= endIndex) {
      middleIndex = Math.floor((startIndex + endIndex) / 2);
      if (target === array[middleIndex])
         return middleIndex;

      if (target > array[middleIndex])
         startIndex = middleIndex + 1;
      if (target < array[middleIndex])
         endIndex = middleIndex - 1;
   }

   return middleIndex;
}

MhistoryGraph.prototype.mouseEvent = function (e) {

   // fix buttons for IE
   if (!e.which && e.button) {
      if ((e.button & 1) > 0) e.which = 1;      // Left
      else if ((e.button & 4) > 0) e.which = 2; // Middle
      else if ((e.button & 2) > 0) e.which = 3; // Right
   }

   let cursor = this.pendingUpdates > 0 ? "progress" : "default";
   let title = "";
   let cancel = false;

   // cancel dragging in case we did not catch the mouseup event
   if (e.type === "mousemove" && e.buttons === 0 &&
      (this.drag.active || this.zoom.x.active || this.zoom.y.active))
      cancel = true;

   if (e.type === "mousedown") {

      this.intSelector.style.display = "none";
      this.downloadSelector.style.display = "none";

      // check for buttons
      this.button.forEach(b => {
         if (e.offsetX > b.x1 && e.offsetX < b.x1 + b.width &&
            e.offsetY > b.y1 && e.offsetY < b.y1 + b.width &&
            b.enabled) {
            b.click(this);
         }
      });

      // check for zoom buttons
      if (e.offsetX > this.width - 30 - 48 && e.offsetX < this.width - 30 - 24 &&
         e.offsetY > this.y1 - 24 && e.offsetY < this.y1) {
         // zoom in
         let delta = this.tMax - this.tMin;
         this.tMin += delta/4;
         this.tMax -= delta/4;
         this.findMinMax();
         this.redraw(true);
         e.preventDefault();
         return;
      }
      if (e.offsetX > this.width - 30 - 24 && e.offsetX < this.width - 30 &&
         e.offsetY > this.y1 - 24 && e.offsetY < this.y1) {
         // zoom out
         if (this.pendingUpdates === 0 || this.tMinReceived < this.tMin) {
            let delta = this.tMax - this.tMin;
            this.tMin -= delta / 2;
            this.tMax += delta / 2;
            // don't go into the future
            let now = Math.floor(new Date() / 1000);
            if (this.tMax > now) {
               this.tMax = now;
               this.tMin = now - 2*delta;
            }
            this.loadOldData();
            this.findMinMax();
            this.redraw(true);
         } else
            dlgMessage("Warning", "Don't press the '-' too fast!", true, false);
         e.preventDefault();
         return;
      }

      // check for dragging
      if (e.offsetX > this.x1 && e.offsetX < this.x2 &&
         e.offsetY > this.y2 && e.offsetY < this.y1) {
         this.drag.active = true;
         this.marker.active = false;
         this.scroll = false;
         this.drag.xStart = e.offsetX;
         this.drag.yStart = e.offsetY;
         this.drag.tStart = this.xToTime(e.offsetX);
         this.drag.tMinStart = this.tMin;
         this.drag.tMaxStart = this.tMax;
         this.drag.yMinStart = this.yMin;
         this.drag.yMaxStart = this.yMax;
         this.drag.vStart = this.yToValue(e.offsetY);
      }

      // check for axis dragging
      if (e.offsetX > this.x1 && e.offsetX < this.x2 && e.offsetY > this.y1) {
         this.zoom.x.active = true;
         this.scroll = false;
         this.zoom.x.x1 = e.offsetX;
         this.zoom.x.x2 = undefined;
         this.zoom.x.t1 = this.xToTime(e.offsetX);
      }
      if (e.offsetY < this.y1 && e.offsetY > this.y2 && e.offsetX < this.x1) {
         this.zoom.y.active = true;
         this.scroll = false;
         this.zoom.y.y1 = e.offsetY;
         this.zoom.y.y2 = undefined;
         this.zoom.y.v1 = this.yToValue(e.offsetY);
      }

   } else if (cancel || e.type === "mouseup") {

      if (this.drag.active) {
         this.drag.active = false;
      }

      if (this.zoom.x.active) {
         if (this.zoom.x.x2 !== undefined &&
            Math.abs(this.zoom.x.x1 - this.zoom.x.x2) > 5) {
            let t1 = this.zoom.x.t1;
            let t2 = this.xToTime(this.zoom.x.x2);
            if (t1 > t2)
               [t1, t2] = [t2, t1];
            if (t2 - t1 < 1)
               t1 -= 1;
            this.tMin = t1;
            this.tMax = t2;
         }
         this.zoom.x.active = false;
         this.findMinMax();
         this.redraw(true);

         if (this.callbacks.timeZoom !== undefined)
            this.callbacks.timeZoom(this);
      }

      if (this.zoom.y.active) {
         if (this.zoom.y.y2 !== undefined &&
            Math.abs(this.zoom.y.y1 - this.zoom.y.y2) > 5) {
            let v1 = this.zoom.y.v1;
            let v2 = this.yToValue(this.zoom.y.y2);
            if (v1 > v2)
               [v1, v2] = [v2, v1];
            this.yMin = v1;
            this.yMax = v2;
         }
         this.zoom.y.active = false;
         this.yZoom = true;
         this.findMinMax();
         this.redraw(true);
      }

   } else if (e.type === "mousemove") {

      if (this.drag.active) {

         // execute dragging
         cursor = "move";
         let dt = (e.offsetX - this.drag.xStart) / (this.x2 - this.x1) * (this.tMax - this.tMin);
         this.tMin = this.drag.tMinStart - dt;
         this.tMax = this.drag.tMaxStart - dt;
         this.drag.lastDt = (e.offsetX - this.drag.lastOffsetX) / (this.x2 - this.x1) * (this.tMax - this.tMin);
         this.drag.lastT = new Date().getTime();
         this.drag.lastOffsetX = e.offsetX;
         if (this.yZoom) {
            let dy = (this.drag.yStart - e.offsetY) / (this.y1 - this.y2) * (this.yMax - this.yMin);
            this.yMin = this.drag.yMinStart - dy;
            this.yMax = this.drag.yMaxStart - dy;
            if (this.logAxis && this.yMin <= 0)
               this.yMin = 1E-20;
            if (this.logAxis && this.yMax <= 0)
               this.yMax = 1E-18;
         }

         this.loadOldData();
         this.findMinMax();
         this.redraw();

         if (this.callbacks.timeZoom !== undefined)
            this.callbacks.timeZoom(this);

      } else {

         let redraw = false;

         // change cursor to pointer over buttons
         this.button.forEach(b => {
            if (e.offsetX > b.x1 && e.offsetY > b.y1 &&
               e.offsetX < b.x1 + b.width && e.offsetY < b.y1 + b.height) {
               cursor = "pointer";
               title = b.title;
            }
         });

         if (this.showZoomButtons) {
            // check for zoom buttons
            if (e.offsetX > this.width - 30 - 48 && e.offsetX < this.width - 30 - 24 &&
               e.offsetY > this.y1 - 24 && e.offsetY < this.y1) {
               cursor = "pointer";
               title = "Zoom in";
            }
            if (e.offsetX > this.width - 30 - 24 && e.offsetX < this.width - 30 &&
               e.offsetY > this.y1 - 24 && e.offsetY < this.y1) {
               cursor = "pointer";
               title = "Zoom out";
            }
         }

         // display zoom cursor
         if (e.offsetX > this.x1 && e.offsetX < this.x2 && e.offsetY > this.y1)
            cursor = "ew-resize";
         if (e.offsetY < this.y1 && e.offsetY > this.y2 && e.offsetX < this.x1)
            cursor = "ns-resize";

         // execute axis zoom
         if (this.zoom.x.active) {
            this.zoom.x.x2 = Math.max(this.x1, Math.min(this.x2, e.offsetX));
            this.zoom.x.t2 = this.xToTime(e.offsetX);
            redraw = true;
         }
         if (this.zoom.y.active) {
            this.zoom.y.y2 = Math.max(this.y2, Math.min(this.y1, e.offsetY));
            this.zoom.y.v2 = this.yToValue(e.offsetY);
            redraw = true;
         }

         // check if cursor close to graph point
         if (this.data !== undefined && this.x.length && this.y.length) {
            let minDist = 10000;
            for (let di = 0; di < this.data.length; di++) {

               let i1 = binarySearch(this.x[di], e.offsetX - 10);
               let i2 = binarySearch(this.x[di], e.offsetX + 10);

               for (let i = i1; i <= i2; i++) {
                  let d = (e.offsetX - this.x[di][i]) * (e.offsetX - this.x[di][i]) +
                     (e.offsetY - this.y[di][i]) * (e.offsetY - this.y[di][i]);
                  if (d < minDist) {
                     minDist = d;
                     this.marker.graphIndex = di;
                     this.marker.index = i;
                  }
               }
            }

            // exclude zoom buttons if visible
            if (this.showZoomButtons &&
               e.offsetX > this.width - 30 - 48 && this.offsetX < this.width - 30 &&
               e.offsetY > this.y1 - 24 && e.offsetY < this.y1) {
               this.marker.active = false;
            } else {
               this.marker.active = Math.sqrt(minDist) < 10 && e.offsetX > this.x1 && e.offsetX < this.x2;
               if (this.marker.active) {
                  this.marker.x = this.x[this.marker.graphIndex][this.marker.index];
                  this.marker.y = this.y[this.marker.graphIndex][this.marker.index];
                  this.marker.t = this.t[this.marker.graphIndex][this.marker.index];
                  this.marker.v = this.v[this.marker.graphIndex][this.marker.index];
                  this.marker.mx = e.offsetX;
                  this.marker.my = e.offsetY;
               }
            }
            if (this.marker.active)
               redraw = true;
            if (!this.marker.active && this.marker.activeOld)
               redraw = true;
            this.marker.activeOld = this.marker.active;

            if (redraw)
               this.redraw(true);
         }
      }
   } else if (e.type === "dblclick") {

      // check if inside zoom buttons
      if (e.offsetX > this.width - 30 - 48 && e.offsetX < this.width - 30 &&
         e.offsetY > this.y1 - 24 && e.offsetY < this.y1) {
         // just ignore it

      } else {

         // measure distance to graphs
         if (this.data !== undefined && this.x.length && this.y.length) {
            let minDist = 100;
            for (let di = 0; di < this.data.length; di++) {
               for (let i = 0; i < this.x[di].length; i++) {
                  if (this.x[di][i] > this.x1 && this.x[di][i] < this.x2) {
                     let d = Math.sqrt(Math.pow(e.offsetX - this.x[di][i], 2) +
                        Math.pow(e.offsetY - this.y[di][i], 2));
                     if (d < minDist) {
                        minDist = d;
                        this.solo.index = di;
                     }
                  }
               }
            }
            // check if close to graph point
            if (minDist < 10 && e.offsetX > this.x1 && e.offsetX < this.x2) {
               this.solo.active = !this.solo.active;
            } else {
               // check if inside label area
               if (this.showLabels) {
                  if (e.offsetX > this.x1 && e.offsetX < this.x1 + 25 + this.variablesWidth + 7) {
                     let i = Math.floor((e.offsetY - 30) / 17);
                     if (i < this.data.length) {
                        if (this.solo.active && this.solo.index === i) {
                           this.solo.active = false;
                        } else {
                           this.solo.active = true;
                           this.solo.index = i;
                        }
                     }
                  }
               }
            }
            this.redraw(true);
         }
      }
   }

   this.parentDiv.title = title;
   this.parentDiv.style.cursor = cursor;

   e.preventDefault();
};

MhistoryGraph.prototype.mouseWheelEvent = function (e) {

   if (e.offsetX > this.x1 && e.offsetX < this.x2 &&
      e.offsetY > this.y2 && e.offsetY < this.y1) {

      if (e.altKey || e.shiftKey) {

         // zoom Y axis
         this.yZoom = true;
         let f = (e.offsetY - this.y1) / (this.y2 - this.y1);

         let step = e.deltaY / 100;
         if (step > 0.5)
            step = 0.5;
         if (step < -0.5)
            step = -0.5;

         let dtMin = f * (this.yMax - this.yMin) * step;
         let dtMax = (1 - f) * (this.yMax - this.yMin) * step;

         if (((this.yMax + dtMax) - (this.yMin - dtMin)) / (this.yMax0 - this.yMin0) < 1000 &&
            (this.yMax0 - this.yMin0) / ((this.yMax + dtMax) - (this.yMin - dtMin)) < 1000) {
            this.yMin -= dtMin;
            this.yMax += dtMax;

            if (this.logAxis && this.yMin <= 0)
               this.yMin = 1E-20;
            if (this.logAxis && this.yMax <= 0)
               this.yMax = 1E-18;
         }

         this.redraw();

      } else if (e.ctrlKey || e.metaKey) {

         this.showZoomButtons = false;

         // zoom time axis
         let f = (e.offsetX - this.x1) / (this.x2 - this.x1);
         let m = 1 / 100;
         let dtMin = Math.abs(f * (this.tMax - this.tMin) * m * e.deltaY);
         let dtMax = Math.abs((1 - f) * (this.tMax - this.tMin) * m * e.deltaY);

         if ((this.tMax - dtMax) - (this.tMin + dtMin) > 10 && e.deltaY < 0) {
            // zoom in
            if (this.scroll) {
               this.tMin += dtMin;
            } else {
               this.tMin += dtMin;
               this.tMax -= dtMax;
            }

            this.redraw();
         }
         if ((this.tMax + dtMax) - (this.tMin - dtMin) < 3600 * 24 * 365 && e.deltaY > 0) {
            // zoom out
            if (this.scroll) {
               this.tMin -= dtMin;
            } else {
               this.tMin -= dtMin;
               this.tMax += dtMax;
            }

            this.loadOldData();
            this.redraw();
         }

         if (this.callbacks.timeZoom !== undefined)
            this.callbacks.timeZoom(this);
      } else  if (e.deltxX !== 0) {

         let dt = (this.tMax - this.tMin) / 1000 * e.deltaX;
         this.tMin += dt;
         this.tMax += dt;

         if (dt < 0)
            this.loadOldData();

         this.redraw();
      } else
         return;

      this.marker.active = false;

      e.preventDefault();
   }
};

MhistoryGraph.prototype.resetAxes = function () {
   this.tMax = Math.floor(new Date() / 1000);
   this.tMin = this.tMax - this.tScale;

   this.scroll = true;
   this.yZoom = false;
   this.showZoomButtons = true;
   this.findMinMax();
   this.redraw(true);
};

MhistoryGraph.prototype.setTimespan = function (tMin, tMax, scroll) {
   this.tMin = tMin;
   this.tMax = tMax;
   this.scroll = scroll;
   this.loadOldData();
   this.redraw();
};

MhistoryGraph.prototype.resize = function () {
   this.canvas.width = this.parentDiv.clientWidth;
   this.canvas.height = this.parentDiv.clientHeight;

   this.width = this.parentDiv.clientWidth;
   this.height = this.parentDiv.clientHeight;

   if (this.intSelector !== undefined)
      this.intSelector.style.display = "none";

   this.forceConvert = true;
   this.redraw(true);
};

MhistoryGraph.prototype.redraw = function (force) {
   this.forceRedraw = force;
   let f = this.draw.bind(this);
   window.requestAnimationFrame(f);
};

MhistoryGraph.prototype.timeToXInit = function () {
   this.timeToXScale = 1 / (this.tMax - this.tMin) * (this.x2 - this.x1);
}

MhistoryGraph.prototype.timeToX = function (t) {
   return (t - this.tMin) * this.timeToXScale + this.x1;
};

MhistoryGraph.prototype.valueToYInit = function () {
   this.valueToYScale = 1 / (this.yMax - this.yMin) * (this.y1 - this.y2);
}

MhistoryGraph.prototype.valueToY = function (v) {
   if (this.logAxis) {
      if (v <= 0)
         return this.y1;
      return this.y1 - (Math.log(v) - Math.log(this.yMin)) /
         (Math.log(this.yMax) - Math.log(this.yMin)) * (this.y1 - this.y2);
   } else
      return this.y1 - (v - this.yMin) * this.valueToYScale;
};

MhistoryGraph.prototype.xToTime = function (x) {
   return (x - this.x1) / (this.x2 - this.x1) * (this.tMax - this.tMin) + this.tMin;
};

MhistoryGraph.prototype.yToValue = function (y) {
   return (this.y1 - y) / (this.y1 - this.y2) * (this.yMax - this.yMin) + this.yMin;
};

MhistoryGraph.prototype.findMinMax = function () {

   if (this.yZoom)
      return;

   if (!this.autoscaleMin)
      this.yMin0 = this.odb["Minimum"];

   if (!this.autoscaleMax)
      this.yMax0 = this.odb["Maximum"];

   if (!this.autoscaleMin && !this.autoscaleMax) {
      this.yMin = this.yMin0;
      this.yMax = this.yMax0;
      return;
   }

   let minValue = undefined;
   let maxValue = undefined;
   let n = 0;
   for (let index = 0; index < this.data.length; index++) {
      if (this.events[index] === "Run transitions")
         continue;
      let i1 = binarySearch(this.data[index].time, this.tMin);
      let i2 = binarySearch(this.data[index].time, this.tMax);
      if (minValue === undefined) {
         minValue = this.data[index].value[i1];
         maxValue = this.data[index].value[i1];
      }
      let v;
      for (let i = i1; i < i2; i++) {
         v = this.data[index].value[i];
         if (v < minValue)
            minValue = v;
         if (v > maxValue)
            maxValue = v;
      }
   }

   if (this.autoscaleMin)
      this.yMin0 = this.yMin = minValue;
   if (this.autoscaleMax)
      this.yMax0 = this.yMax = maxValue;

   if (minValue === undefined || maxValue === undefined) {
      this.yMin0 = -0.5;
      this.yMax0 = 0.5;
   }

   if (this.yMin0 === this.yMax0) {
      this.yMin0 -= 0.5;
      this.yMax0 += 0.5;
   }

   if (!this.yZoom) {
      if (this.autoscaleMin) {
         if (this.logAxis)
            this.yMin = 0.8 * this.yMin0;
         else
            // leave 10% space below graph
            this.yMin = this.yMin0 - (this.yMax0 - this.yMin0) / 10;
      } else
         this.yMin = this.yMin0;
      if (this.logAxis && this.yMin <= 0)
         this.yMin = 1E-20;

      if (this.autoscaleMax) {
         if (this.logAxis)
            this.yMax = 1.2 * this.yMax0;
         else
            // leave 10% space above graph
            this.yMax = this.yMax0 + (this.yMax0 - this.yMin0) / 10;
      } else
         this.yMax = this.yMax0;
      if (this.logAxis && this.yMax <= 0)
         this.yMax = 1E-18;
   }
};

function convertLastWritten(last) {
   if (last === 0)
      return "no data available";

   let d = new Date(last * 1000).toLocaleDateString(
      'en-GB', {
         day: '2-digit', month: 'short', year: '2-digit',
         hour12: false, hour: '2-digit', minute: '2-digit'
      }
   );

   return "last data: " + d;
}

MhistoryGraph.prototype.updateURL = function() {
   let url = window.location.href;
   if (url.search("&A=") !== -1)
      url = url.slice(0, url.search("&A="));
   url += "&A=" + Math.round(this.tMin) + "&B=" + Math.round(this.tMax);

   if (url !== window.location.href)
      window.history.replaceState({}, "Midas History", url);
}

function createPinstripeCanvas() {
   const patternCanvas = document.createElement("canvas");
   const pctx = patternCanvas.getContext('2d', { antialias: true });
   const colour = "#FFC0C0";

   const CANVAS_SIDE_LENGTH = 90;
   const WIDTH = CANVAS_SIDE_LENGTH;
   const HEIGHT = CANVAS_SIDE_LENGTH;
   const DIVISIONS = 4;

   patternCanvas.width = WIDTH;
   patternCanvas.height = HEIGHT;
   pctx.fillStyle = colour;

   // Top line
   pctx.beginPath();
   pctx.moveTo(0, HEIGHT * (1 / DIVISIONS));
   pctx.lineTo(WIDTH * (1 / DIVISIONS), 0);
   pctx.lineTo(0, 0);
   pctx.lineTo(0, HEIGHT * (1 / DIVISIONS));
   pctx.fill();

   // Middle line
   pctx.beginPath();
   pctx.moveTo(WIDTH, HEIGHT * (1 / DIVISIONS));
   pctx.lineTo(WIDTH * (1 / DIVISIONS), HEIGHT);
   pctx.lineTo(0, HEIGHT);
   pctx.lineTo(0, HEIGHT * ((DIVISIONS - 1) / DIVISIONS));
   pctx.lineTo(WIDTH * ((DIVISIONS - 1) / DIVISIONS), 0);
   pctx.lineTo(WIDTH, 0);
   pctx.lineTo(WIDTH, HEIGHT * (1 / DIVISIONS));
   pctx.fill();

   // Bottom line
   pctx.beginPath();
   pctx.moveTo(WIDTH, HEIGHT * ((DIVISIONS - 1) / DIVISIONS));
   pctx.lineTo(WIDTH * ((DIVISIONS - 1) / DIVISIONS), HEIGHT);
   pctx.lineTo(WIDTH, HEIGHT);
   pctx.lineTo(WIDTH, HEIGHT * ((DIVISIONS - 1) / DIVISIONS));
   pctx.fill();

   return patternCanvas;
}

MhistoryGraph.prototype.draw = function () {
   //profile(true);

   // draw maximal 30 times per second
   if (!this.forceRedraw) {
      if (new Date().getTime() < this.lastDrawTime + 30)
         return;
      this.lastDrawTime = new Date().getTime();
   }
   this.forceRedraw = false;

   let update_last_written = false;

   let ctx = this.canvas.getContext("2d");

   ctx.fillStyle = this.color.background;
   ctx.fillRect(0, 0, this.width, this.height);

   if (this.data === undefined) {
      ctx.lineWidth = 1;
      ctx.font = "14px sans-serif";
      ctx.strokeStyle = "#808080";
      ctx.fillStyle = "#808080";
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText("Data being loaded ...", this.width / 2, this.height / 2);
      return;
   }

   ctx.lineWidth = 1;
   ctx.font = "14px sans-serif";

   if (this.height === undefined || this.width === undefined)
      return;
   if (this.yMin === undefined || Number.isNaN(this.yMin))
      return;
   if (this.yMax === undefined || Number.isNaN(this.yMax))
      return;

   let axisLabelWidth = this.drawVAxis(ctx, 50, this.height - 25, this.height - 35,
      -4, -7, -10, -12, 0, this.yMin, this.yMax, this.logAxis, false);

   this.x1 = axisLabelWidth + 15;
   this.y1 = this.height - 25;
   this.x2 = this.width - 30;
   this.y2 = 26;

   this.timeToXInit();  // initialize scale factor t -> x
   this.valueToYInit(); // initialize scale factor v -> y

   // title
   ctx.strokeStyle = this.color.axis;
   ctx.fillStyle = "#F0F0F0";
   ctx.strokeRect(this.x1, 6, this.x2 - this.x1, 20);
   ctx.fillRect(this.x1, 6, this.x2 - this.x1, 20);
   ctx.textAlign = "center";
   ctx.textBaseline = "middle";
   ctx.fillStyle = "#808080";
   ctx.fillText(this.group + " - " + this.panel, (this.x2 + this.x1) / 2, 16);

   // draw axis
   ctx.strokeStyle = this.color.axis;
   ctx.drawLine(this.x1, this.y2, this.x2, this.y2);
   ctx.drawLine(this.x2, this.y2, this.x2, this.y1);

   if (this.logAxis && this.yMin < 1E-20)
      this.yMin = 1E-20;
   if (this.logAxis && this.yMax < 1E-18)
      this.yMax = 1E-18;
   this.drawVAxis(ctx, this.x1, this.y1, this.y1 - this.y2,
      -4, -7, -10, -12, this.x2 - this.x1, this.yMin, this.yMax, this.logAxis, true);
   this.drawTAxis(ctx, this.x1, this.y1, this.x2 - this.x1, this.width,
      4, 7, 10, 10, this.y2 - this.y1, this.tMin, this.tMax);

   // draw hatched area for "future"
   let t = Math.floor(new Date() / 1000);
   if (this.tMax > t) {
      let x = this.timeToX(t);
      ctx.fillStyle = ctx.createPattern(createPinstripeCanvas(), 'repeat');
      ctx.fillRect(x, 26, this.x2 - x, this.y1 - this.y2);

      ctx.strokeStyle = this.color.axis;
      ctx.strokeRect(x, 26, this.x2 - x, this.y1 - this.y2);
   }

   // determine precision
   let n_sig1, n_sig2;
   if (this.yMin === 0)
      n_sig1 = 1;
   else
      n_sig1 = Math.floor(Math.log(Math.abs(this.yMin)) / Math.log(10)) -
         Math.floor(Math.log(Math.abs((this.yMax - this.yMin) / 50)) / Math.log(10)) + 1;

   if (this.yMax === 0)
      n_sig2 = 1;
   else
      n_sig2 = Math.floor(Math.log(Math.abs(this.yMax)) / Math.log(10)) -
         Math.floor(Math.log(Math.abs((this.yMax - this.yMin) / 50)) / Math.log(10)) + 1;

   n_sig1 = Math.max(n_sig1, n_sig2);
   n_sig1 = Math.max(1, n_sig1);

   // toPrecision displays 1050 with 3 digits as 1.05e+3, so increase precision to number of digits
   if (Math.abs(this.yMin) < 100000)
      n_sig1 = Math.max(n_sig1, Math.floor(Math.log(Math.abs(this.yMin)) /
         Math.log(10) + 0.001) + 1);
   if (Math.abs(this.yMax) < 100000)
      n_sig1 = Math.max(n_sig1, Math.floor(Math.log(Math.abs(this.yMax)) /
         Math.log(10) + 0.001) + 1);

   this.yPrecision = Math.max(6, n_sig1); // use at least 5 digits

   ctx.save();
   ctx.beginPath();
   ctx.rect(this.x1, this.y2, this.x2 - this.x1, this.y1 - this.y2);
   ctx.clip();

   // profile("drawinit");

   let nPoints = 0;
   for (let di = 0; di < this.data.length; di++)
      nPoints += this.data[di].time.length;

   // convert values to points if window has changed or number of points have changed
   if (this.tMin !== this.tMinOld || this.tMax !== this.tMaxOld ||
      this.yMin !== this.yMinOld || this.yMax !== this.yMaxOld ||
      nPoints !== this.nPointsOld || this.forceConvert) {

      this.tMinOld = this.tMin;
      this.tMaxOld = this.tMax;
      this.yMinOld = this.yMin;
      this.yMaxOld = this.yMax;
      this.nPointsOld = nPoints;
      this.forceConvert = false;

      //profile();
      for (let di = 0; di < this.data.length; di++) {
         if (this.x[di] === undefined) {
            this.x[di] = []; // x/y contain visible part of graph
            this.y[di] = [];
            this.t[di] = []; // t/v contain time/value pairs corresponding to x/y
            this.v[di] = [];
         }

         let first = undefined;
         let last = undefined;
         let n = 0;

         let i1 = binarySearch(this.data[di].time, this.tMin);
         let i2 = this.data[di].time.length;
         for (let i = i1; i < i2 ; i++) {
            let t = this.data[di].time[i];
            if (t >= this.tMin && t <= this.tMax) {
               let x = this.timeToX(t);
               let v = this.valueToY(this.data[di].value[i]);
               if (!Number.isNaN(v) && x >= this.x1 && x <= this.x2) {
                  this.x[di][n] = x;
                  this.y[di][n] = v;
                  this.t[di][n] = this.data[di].time[i];
                  this.v[di][n] = this.data[di].value[i];
                  if (first === undefined)
                     first = i;
                  last = i;
                  n++;
               }
            }
            if (t > this.tMax)
               break;
         }
         // add one point beyond right limit
         if (last + 1 < this.data[di].time.length) {
            this.x[di][n] = this.timeToX(this.data[di].time[last + 1]);
            this.y[di][n] = this.valueToY(this.data[di].value[last + 1]);
            this.t[di][n] = this.data[di].time[last + 1];
            this.v[di][n] = this.data[di].value[last + 1];
            n++;
         }
         // add one point beyond left limit
         if (first > 0) {
            this.x[di].unshift(this.timeToX(this.data[di].time[first - 1]));
            this.y[di].unshift(this.valueToY(this.data[di].value[first - 1]));
            this.t[di].unshift(this.data[di].value[first - 1]);
            this.v[di].unshift(this.data[di].time[first - 1]);
         }
         this.x[di].length = n;
         this.y[di].length = n;
         this.t[di].length = n;
         this.v[di].length = n;
      }

      //profile("Value to points");

      this.avgN = [];

      // compress points to aggregate values
      for (let di = 0; di < this.data.length; di++) {
         if (this.events[di] === "Run transitions")
            continue;
         this.p[di] = [];
         let p = {};

         let sum0 = 0;
         let sum1 = 0;

         let xLast = undefined;
         let l = this.x[di].length;
         for (let i = 0; i < l; i++) {
            let x = Math.floor(this.x[di][i]);
            let y = this.y[di][i];

            if (i === 0 || x > xLast) {

               if (p.x !== undefined) {
                  // store point
                  if (p.n > 0)
                     p.avg = p.avg / p.n;
                  sum0 += 1;
                  sum1 += p.n;
                  this.p[di].push(p);
                  p = {};
               }
               p.n = 1;
               p.x = x;
               xLast = x;
               p.min = y;
               p.max = y;
               p.avg = y;
               p.first = y;
               p.last = y;
            } else {
               p.n++;
               if (y < p.min)
                  p.min = y;
               if (y > p.max)
                  p.max = y;
               p.avg += y;
               p.last = y;
            }
         }

         if (sum0 > 0)
            this.avgN.push(sum1 / sum0);
         else
            this.avgN.push(0);
      }

      // profile("Compress points");
   }

   // draw shaded areas
   if (this.showFill) {
      for (let di = 0; di < this.data.length; di++) {
         if (this.solo.active && this.solo.index !== di)
            continue;

         if (this.events[di] === "Run transitions")
            continue;

         ctx.fillStyle = this.odb["Colour"][di];

         if (typeof this.avgN !== 'undefined' && this.avgN[di] > 2) {
            ctx.beginPath();
            let x0 = undefined;
            let y0 = undefined;
            let xLast = 0;
            for (let i = 0; i < this.p[di].length; i++) {
               let p = this.p[di][i];
               if (x0 === undefined) {
                  x0 = p.x;
                  y0 = p.first;
                  ctx.moveTo(p.x, p.first);
               } else {
                  ctx.lineTo(p.x, p.first);
               }
               xLast = p.x;
               ctx.lineTo(p.x, p.last);
            }
            ctx.lineTo(xLast, this.valueToY(0));
            ctx.lineTo(x0, this.valueToY(0));
            ctx.lineTo(x0, y0);
            ctx.globalAlpha = 0.1;
            ctx.fill();
            ctx.globalAlpha = 1;
         } else {
            ctx.beginPath();
            let x0 = undefined;
            let y0 = undefined;
            let xLast = 0;
            let i;
            for (i = 0; i < this.x[di].length; i++) {
               let x = this.x[di][i];
               let y = this.y[di][i];
               if (x0 === undefined) {
                  x0 = x;
                  y0 = y;
                  ctx.moveTo(x, y);
               } else {
                  ctx.lineTo(x, y);
               }
               xLast = x;
            }
            ctx.lineTo(xLast, this.valueToY(0));
            ctx.lineTo(x0, this.valueToY(0));
            ctx.lineTo(x0, y0);
            ctx.globalAlpha = 0.1;
            ctx.fill();
            ctx.globalAlpha = 1;
         }
      }
   }

   // profile("Draw shaded areas");

   // draw graphs
   for (let di = 0; di < this.data.length; di++) {
      if (this.solo.active && this.solo.index !== di)
         continue;

      if (this.events[di] === "Run transitions") {

         if (this.tags[di] === "State") {
            if (this.x[di].length < 200) {
               for (let i = 0; i < this.x[di].length; i++) {
                  if (this.v[di][i] === 1) {
                     ctx.strokeStyle = "#FF0000";
                     ctx.fillStyle = "#808080";
                     ctx.textAlign = "right";
                     ctx.textBaseline = "top";
                     ctx.fillText(this.v[di + 1][i], this.x[di][i] - 5, this.y2 + 3);
                  } else if (this.v[di][i] === 3) {
                     ctx.strokeStyle = "#00A000";
                     ctx.fillStyle = "#808080";
                     ctx.textAlign = "left";
                     ctx.textBaseline = "top";
                     ctx.fillText(this.v[di + 1][i], this.x[di][i] + 3, this.y2 + 3);
                  } else {
                     ctx.strokeStyle = "#F9A600";
                  }

                  ctx.setLineDash([8, 2]);
                  ctx.drawLine(Math.floor(this.x[di][i]), this.y1, Math.floor(this.x[di][i]), this.y2);
                  ctx.setLineDash([]);
               }
            }
         }

      } else {

         ctx.strokeStyle = this.odb["Colour"][di];

         if (typeof this.avgN !== 'undefined' && this.avgN[di] > 2) {
            ctx.beginPath();
            for (let i = 0; i < this.p[di].length; i++) {
               let p = this.p[di][i];

               // draw lines first - max - min - last
               if (i === 0)
                  ctx.moveTo(p.x, p.first);
               else
                  ctx.lineTo(p.x, p.first);
               ctx.lineTo(p.x, p.max + 1); // in case min==max
               ctx.lineTo(p.x, p.min);
               ctx.lineTo(p.x, p.last);
            }
            ctx.stroke();
         } else {
            if (this.x[di].length === 1) {
               let x = this.x[di][0];
               let y = this.y[di][0];
               ctx.fillStyle = this.odb["Colour"][di];
               ctx.fillRect(x - 1, y - 1, 3, 3);
            } else {
               ctx.beginPath();
               let first = true;
               for (let i = 0; i < this.x[di].length; i++) {
                  let x = this.x[di][i];
                  let y = this.y[di][i];
                  if (first) {
                     first = false;
                     ctx.moveTo(x, y);
                  } else {
                     ctx.lineTo(x, y);
                  }
               }
               ctx.stroke();
            }
         }
      }
   }

   ctx.restore(); // remove clipping

   // profile("Draw graphs");

   // labels with variable names and values
   if (this.showLabels) {
      this.variablesHeight = this.odb["Variables"].length * 17 + 7;
      this.variablesWidth = 0;

      this.odb["Variables"].forEach((v, i) => {
         let width;
         if (this.odb.Label[i] !== "")
            width = ctx.measureText(this.odb.Label[i]).width;
         else
            width = ctx.measureText(v.substr(v.indexOf(':') + 1)).width;

         width += 20; // space between name and value

         if (this.v[i].length > 0) {
            // use last point in array
            let index = this.v[i].length - 1;

            // use point at current marker
            if (this.marker.active)
               index = this.marker.index;

            if (index < this.v[i].length) {
               // convert value to string with 6 digits
               let value = this.v[i][index];
               let str = "  " + value.toPrecision(this.yPrecision).stripZeros();
               width += ctx.measureText(str).width;
            }
         } else {
            width += ctx.measureText(convertLastWritten(this.lastWritten[i])).width;
         }

         this.variablesWidth = Math.max(this.variablesWidth, width);
      });

      ctx.save();
      ctx.beginPath();
      ctx.rect(this.x1, this.y2, 25 + this.variablesWidth + 7, this.variablesHeight + 2);
      ctx.clip();

      ctx.strokeStyle = this.color.axis;
      ctx.fillStyle = "#F0F0F0";
      ctx.globalAlpha = 0.5;
      ctx.strokeRect(this.x1, this.y2, 25 + this.variablesWidth + 5, this.variablesHeight);
      ctx.fillRect(this.x1, this.y2, 25 + this.variablesWidth + 5, this.variablesHeight);
      ctx.globalAlpha = 1;

      this.odb["Variables"].forEach((v, i) => {
         ctx.lineWidth = 4;
         ctx.strokeStyle = this.odb["Colour"][i];
         ctx.drawLine(this.x1 + 5, 40 + i * 17, this.x1 + 20, 40 + i * 17);
         ctx.lineWidth = 1;

         ctx.textAlign = "left";
         ctx.textBaseline = "middle";
         ctx.fillStyle = "#404040";
         if (this.odb.Label[i] !== "")
            ctx.fillText(this.odb.Label[i], this.x1 + 25, 40 + i * 17);
         else
            ctx.fillText(v.substr(v.indexOf(':') + 1), this.x1 + 25, 40 + i * 17);

         ctx.textAlign = "right";
         if (this.v[i].length > 0) {
            // use last point in array
            let index = this.v[i].length - 1;

            // use point at current marker
            if (this.marker.active)
               index = this.marker.index;

            if (index < this.v[i].length) {
               // convert value to string with 6 digits
               let value = this.v[i][index];
               let str = value.toPrecision(this.yPrecision).stripZeros();
               ctx.fillText(str, this.x1 + 25 + this.variablesWidth, 40 + i * 17);
            }
         } else {
            if (this.lastWritten.length > 0) {
               if (this.lastWritten[i] > this.tMax) {
                  //console.log("last written is in the future: " + this.events[i] + ", lw: " + this.lastWritten[i], ", this.tMax: " + this.tMax, ", diff: " + (this.lastWritten[i] - this.tMax));
                  update_last_written = true;
               }
               ctx.fillText(convertLastWritten(this.lastWritten[i]),
                  this.x1 + 25 + this.variablesWidth, 40 + i * 17);
            } else {
               //console.log("last_written was not loaded yet");
               update_last_written = true;
            }
         }

      });

      ctx.restore(); // remove clipping
   }

   // "updating" notice
   if (this.pendingUpdates > 0 && this.tMinReceived > this.tMin) {
      let str = "Updating data ...";
      ctx.strokeStyle = "#404040";
      ctx.fillStyle = "#FFC0C0";
      ctx.fillRect(this.x1 + 5, this.y1 - 22, 10 + ctx.measureText(str).width, 17);
      ctx.strokeRect(this.x1 + 5, this.y1 - 22, 10 + ctx.measureText(str).width, 17);
      ctx.fillStyle = "#404040";
      ctx.textAlign = "left";
      ctx.textBaseline = "middle";
      ctx.fillText(str, this.x1 + 10, this.y1 - 13);
   }

   let no_data = true;

   for (let i = 0; i < this.data.length; i++) {
      if (this.data[i].time === undefined || this.data[i].time.length === 0) {
      } else {
         no_data = false;
      }
   }

   // "empty window" notice
   if (no_data) {
      ctx.font = "16px sans-serif";
      let str = "No data available";
      ctx.strokeStyle = "#404040";
      ctx.fillStyle = "#F0F0F0";
      let w = ctx.measureText(str).width + 10;
      let h = 16 + 10;
      ctx.fillRect((this.x1 + this.x2) / 2 - w / 2, (this.y1 + this.y2) / 2 - h / 2, w, h);
      ctx.strokeRect((this.x1 + this.x2) / 2 - w / 2, (this.y1 + this.y2) / 2 - h / 2, w, h);
      ctx.fillStyle = "#404040";
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText(str, (this.x1 + this.x2) / 2, (this.y1 + this.y2) / 2);
      ctx.font = "14px sans-serif";
   }

   // buttons
   let y = 0;
   this.button.forEach(b => {
      b.x1 = this.width - 30;
      b.y1 = 6 + y * 28;
      b.width = 28;
      b.height = 28;
      b.enabled = true;

      if (b.src === "maximize-2.svg") {
         let s = window.location.href;
         if (s.indexOf("&A") > -1)
            s = s.substr(0, s.indexOf("&A"));
         if (s === encodeURI(this.baseURL + "&group=" + this.group + "&panel=" + this.panel)) {
            b.enabled = false;
            return;
         }
      }

      if (b.src === "play.svg" && !this.scroll)
         ctx.fillStyle = "#FFC0C0";
      else
         ctx.fillStyle = "#F0F0F0";
      ctx.strokeStyle = "#808080";
      ctx.fillRect(b.x1, b.y1, b.width, b.height);
      ctx.strokeRect(b.x1, b.y1, b.width, b.height);
      ctx.drawImage(b.img, b.x1 + 2, b.y1 + 2);

      y++;
   });

   // zoom buttons
   if (this.showZoomButtons) {
      let xb = this.width - 30 - 48;
      let yb = this.y1 - 24;
      ctx.fillStyle = "#F0F0F0";
      ctx.globalAlpha = 0.5;
      ctx.fillRect(xb, yb, 24, 24);
      ctx.globalAlpha = 1;
      ctx.strokeStyle = "#808080";
      ctx.strokeRect(xb, yb, 24, 24);
      ctx.strokeStyle = "#202020";
      ctx.drawLine(xb + 5, yb + 12, xb + 19, yb + 12);
      ctx.drawLine(xb + 12, yb + 5, xb + 12, yb + 19);

      xb += 24;
      ctx.globalAlpha = 0.5;
      ctx.fillRect(xb, yb, 24, 24);
      ctx.globalAlpha = 1;
      ctx.strokeStyle = "#808080";
      ctx.strokeRect(xb, yb, 24, 24);
      ctx.strokeStyle = "#202020";
      ctx.drawLine(xb + 5, yb + 12, xb + 19, yb + 12);
   }

   // axis zoom
   if (this.zoom.x.active) {
      ctx.fillStyle = "#808080";
      ctx.globalAlpha = 0.2;
      ctx.fillRect(this.zoom.x.x1, this.y2, this.zoom.x.x2 - this.zoom.x.x1, this.y1 - this.y2);
      ctx.globalAlpha = 1;
      ctx.strokeStyle = "#808080";
      ctx.drawLine(this.zoom.x.x1, this.y1, this.zoom.x.x1, this.y2);
      ctx.drawLine(this.zoom.x.x2, this.y1, this.zoom.x.x2, this.y2);
   }
   if (this.zoom.y.active) {
      ctx.fillStyle = "#808080";
      ctx.globalAlpha = 0.2;
      ctx.fillRect(this.x1, this.zoom.y.y1, this.x2 - this.x1, this.zoom.y.y2 - this.zoom.y.y1);
      ctx.globalAlpha = 1;
      ctx.strokeStyle = "#808080";
      ctx.drawLine(this.x1, this.zoom.y.y1, this.x2, this.zoom.y.y1);
      ctx.drawLine(this.x1, this.zoom.y.y2, this.x2, this.zoom.y.y2);
   }

   // marker
   if (this.marker.active) {

      // round marker
      ctx.beginPath();
      ctx.globalAlpha = 0.1;
      ctx.arc(this.marker.x, this.marker.y, 10, 0, 2 * Math.PI);
      ctx.fillStyle = "#000000";
      ctx.fill();
      ctx.globalAlpha = 1;

      ctx.beginPath();
      ctx.arc(this.marker.x, this.marker.y, 4, 0, 2 * Math.PI);
      ctx.fillStyle = "#000000";
      ctx.fill();

      ctx.strokeStyle = "#A0A0A0";
      ctx.drawLine(this.marker.x, this.y1, this.marker.x, this.y2);

      // text label
      let v = this.marker.v;

      let s;
      if (this.odb.Label[this.marker.graphIndex] !== "")
         s = this.odb.Label[this.marker.graphIndex] + ": " + v.toPrecision(this.yPrecision).stripZeros();
      else
         s = this.odb["Variables"][this.marker.graphIndex] + ": " + v.toPrecision(this.yPrecision).stripZeros();

      let w = ctx.measureText(s).width + 6;
      let h = ctx.measureText("M").width * 1.2 + 6;
      let x = this.marker.mx + 20;
      let y = this.marker.my + h / 3 * 2;
      let xl = x;
      let yl = y;

      if (x + w >= this.x2) {
         x = this.marker.x - 20 - w;
         xl = x + w;
      }

      if (y > (this.y1 - this.y2) / 2) {
         y = this.marker.y - h / 3 * 5;
         yl = y + h;
      }

      ctx.strokeStyle = "#808080";
      ctx.fillStyle = "#F0F0F0";
      ctx.textBaseline = "middle";
      ctx.fillRect(x, y, w, h);
      ctx.strokeRect(x, y, w, h);
      ctx.fillStyle = "#404040";
      ctx.fillText(s, x + 3, y + h / 2);

      // vertical line
      ctx.strokeStyle = "#808080";
      ctx.drawLine(this.marker.x, this.marker.y, xl, yl);

      // time label
      s = timeToLabel(this.marker.t, 1, true);
      w = ctx.measureText(s).width + 10;
      h = ctx.measureText("M").width * 1.2 + 11;
      x = this.marker.x - w / 2;
      y = this.y1;
      if (x <= this.x1)
         x = this.x1;
      if (x + w >= this.x2)
         x = this.x2 - w;

      ctx.strokeStyle = "#808080";
      ctx.fillStyle = "#F0F0F0";
      ctx.fillRect(x, y, w, h);
      ctx.strokeRect(x, y, w, h);
      ctx.fillStyle = "#404040";
      ctx.fillText(s, x + 5, y + h / 2);
   }

   this.lastDrawTime = new Date().getTime();

   // profile("Finished draw");

   if (update_last_written) {
      this.updateLastWritten();
   }

   // update URL
   if (this.updateURLTimer !== undefined)
      window.clearTimeout(this.updateURLTimer);

   if (this.plotIndex === 0)
      this.updateURLTimer = window.setTimeout(this.updateURL.bind(this), 500);
};

MhistoryGraph.prototype.drawVAxis = function (ctx, x1, y1, height, minor, major,
                                              text, label, grid, ymin, ymax, logaxis, draw) {
   let dy, int_dy, frac_dy, y_act, label_dy, major_dy, y_screen;
   let tick_base, major_base, label_base, n_sig1, n_sig2, ys;
   let base = [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000];

   if (x1 > 0)
      ctx.textAlign = "right";
   else
      ctx.textAlign = "left";
   ctx.textBaseline = "middle";
   let textHeight = parseInt(ctx.font.match(/\d+/)[0]);

   if (ymax <= ymin || height <= 0)
      return undefined;

   if (logaxis) {
      dy = Math.pow(10, Math.floor(Math.log(ymin) / Math.log(10)));
      if (dy === 0) {
         ymin = 1E-20;
         dy = 1E-20;
      }
      label_dy = dy;
      major_dy = dy * 10;
      n_sig1 = 4;
   } else {
      // use 6 as min tick distance
      dy = (ymax - ymin) / (height / 6);

      int_dy = Math.floor(Math.log(dy) / Math.log(10));
      frac_dy = Math.log(dy) / Math.log(10) - int_dy;

      if (frac_dy < 0) {
         frac_dy += 1;
         int_dy -= 1;
      }

      tick_base = frac_dy < (Math.log(2) / Math.log(10)) ? 1 : frac_dy < (Math.log(5) / Math.log(10)) ? 2 : 3;
      major_base = label_base = tick_base + 1;

      // rounding up of dy, label_dy
      dy = Math.pow(10, int_dy) * base[tick_base];
      major_dy = Math.pow(10, int_dy) * base[major_base];
      label_dy = major_dy;

      // number of significant digits
      if (ymin === 0)
         n_sig1 = 1;
      else
         n_sig1 = Math.floor(Math.log(Math.abs(ymin)) / Math.log(10)) -
            Math.floor(Math.log(Math.abs(label_dy)) / Math.log(10)) + 1;

      if (ymax === 0)
         n_sig2 = 1;
      else
         n_sig2 = Math.floor(Math.log(Math.abs(ymax)) / Math.log(10)) -
            Math.floor(Math.log(Math.abs(label_dy)) / Math.log(10)) + 1;

      n_sig1 = Math.max(n_sig1, n_sig2);
      n_sig1 = Math.max(1, n_sig1);

      // toPrecision displays 1050 with 3 digits as 1.05e+3, so increase precision to number of digits
      if (Math.abs(ymin) < 100000)
         n_sig1 = Math.max(n_sig1, Math.floor(Math.log(Math.abs(ymin)) /
            Math.log(10) + 0.001) + 1);
      if (Math.abs(ymax) < 100000)
         n_sig1 = Math.max(n_sig1, Math.floor(Math.log(Math.abs(ymax)) /
            Math.log(10) + 0.001) + 1);

      // increase label_dy if labels would overlap
      while (label_dy / (ymax - ymin) * height < 1.5 * textHeight) {
         label_base++;
         label_dy = Math.pow(10, int_dy) * base[label_base];
         if (label_base % 3 === 2 && major_base % 3 === 1) {
            major_base++;
            major_dy = Math.pow(10, int_dy) * base[major_base];
         }
      }
   }

   y_act = Math.floor(ymin / dy) * dy;

   let last_label_y = y1;
   let maxwidth = 0;

   if (draw) {
      ctx.strokeStyle = this.color.axis;
      ctx.drawLine(x1, y1, x1, y1 - height);
   }

   do {
      if (logaxis)
         y_screen = y1 - (Math.log(y_act) - Math.log(ymin)) /
            (Math.log(ymax) - Math.log(ymin)) * height;
      else
         y_screen = y1 - (y_act - ymin) / (ymax - ymin) * height;
      ys = Math.round(y_screen);

      if (y_screen < y1 - height - 0.001)
         break;

      if (y_screen <= y1 + 0.001) {
         if (Math.abs(Math.round(y_act / major_dy) - y_act / major_dy) <
            dy / major_dy / 10.0) {

            if (Math.abs(Math.round(y_act / label_dy) - y_act / label_dy) <
               dy / label_dy / 10.0) {
               // label tick mark
               if (draw) {
                  ctx.strokeStyle = this.color.axis;
                  ctx.drawLine(x1, ys, x1 + text, ys);
               }

               // grid line
               if (grid !== 0 && ys < y1 && ys > y1 - height)
                  if (draw) {
                     ctx.strokeStyle = this.color.grid;
                     ctx.drawLine(x1, ys, x1 + grid, ys);
                  }

               // label
               if (label !== 0) {
                  let str;
                  if (Math.abs(y_act) < 0.001 && Math.abs(y_act) > 1E-20)
                     str = y_act.toExponential(n_sig1).stripZeros();
                  else
                     str = y_act.toPrecision(n_sig1).stripZeros();
                  maxwidth = Math.max(maxwidth, ctx.measureText(str).width);
                  if (draw) {
                     ctx.strokeStyle = this.color.label;
                     ctx.fillStyle = this.color.label;
                     ctx.fillText(str, x1 + label, ys);
                  }
                  last_label_y = ys - textHeight / 2;
               }
            } else {
               // major tick mark
               if (draw) {
                  ctx.strokeStyle = this.color.axis;
                  ctx.drawLine(x1, ys, x1 + major, ys);
               }

               // grid line
               if (grid !== 0 && ys < y1 && ys > y1 - height)
                  if (draw) {
                     ctx.strokeStyle = this.color.grid;
                     ctx.drawLine(x1, ys, x1 + grid, ys);
                  }
            }

            if (logaxis) {
               dy *= 10;
               major_dy *= 10;
               label_dy *= 10;
            }

         } else
            // minor tick mark
         if (draw) {
            ctx.strokeStyle = this.color.axis;
            ctx.drawLine(x1, ys, x1 + minor, ys);
         }

         // for logaxis, also put labels on minor tick marks
         if (logaxis) {
            if (label !== 0) {
               let str;
               if (Math.abs(y_act) < 0.001 && Math.abs(y_act) > 1E-20)
                  str = y_act.toExponential(n_sig1).stripZeros();
               else
                  str = y_act.toPrecision(n_sig1).stripZeros();
               if (ys - textHeight / 2 > y1 - height &&
                  ys + textHeight / 2 < y1 &&
                  ys + textHeight < last_label_y + 2) {
                  maxwidth = Math.max(maxwidth, ctx.measureText(str).width);
                  if (draw) {
                     ctx.strokeStyle = this.color.label;
                     ctx.fillStyle = this.color.label;
                     ctx.fillText(str, x1 + label, ys);
                  }
               }

               last_label_y = ys;
            }
         }
      }

      y_act += dy;

      // suppress 1.23E-17 ...
      if (Math.abs(y_act) < dy / 100)
         y_act = 0;

   } while (1);

   return maxwidth;
};

let options1 = {
   day: '2-digit', month: 'short', year: '2-digit',
   hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit'
};

let options2 = {
   day: '2-digit', month: 'short', year: '2-digit',
   hour12: false, hour: '2-digit', minute: '2-digit'
};

let options3 = {
   day: '2-digit', month: 'short', year: '2-digit',
   hour12: false, hour: '2-digit', minute: '2-digit'
};

let options4 = {day: '2-digit', month: 'short', year: '2-digit'};

let options5 = {hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit'};

let options6 = {hour12: false, hour: '2-digit', minute: '2-digit'};

let options7 = {hour12: false, hour: '2-digit', minute: '2-digit'};

let options8 = {
   day: '2-digit', month: 'short', year: '2-digit',
   hour12: false, hour: '2-digit', minute: '2-digit'
};

let options9 = {day: '2-digit', month: 'short', year: '2-digit'};

function timeToLabel(sec, base, forceDate) {
   let d = new Date(sec * 1000);
   let options;

   if (forceDate) {
      if (base < 60) {
         options = options1;
      } else if (base < 600) {
         options = options2;
      } else if (base < 3600 * 24) {
         options = options3;
      } else {
         options = options4;
      }

      return d.toLocaleDateString('en-GB', options);
   }

   if (base < 60) {
      return d.toLocaleTimeString('en-GB', options5);
   } else if (base < 600) {
      return d.toLocaleTimeString('en-GB', options6);
   } else if (base < 3600 * 3) {
      return d.toLocaleTimeString('en-GB', options7);
   } else if (base < 3600 * 24) {
      options = options8;
   } else {
      options = options9;
   }

   return d.toLocaleDateString('en-GB', options);
}


MhistoryGraph.prototype.drawTAxis = function (ctx, x1, y1, width, xr, minor, major,
                                              text, label, grid, xmin, xmax) {
   const base = [1, 5, 10, 60, 2 * 60, 5 * 60, 10 * 60, 15 * 60, 30 * 60, 3600,
      3 * 3600, 6 * 3600, 12 * 3600, 24 * 3600, 2 * 24 * 3600, 10 * 24 * 3600,
      30 * 24 * 3600, 0];

   ctx.textAlign = "left";
   ctx.textBaseline = "top";

   if (xmax <= xmin || width <= 0)
      return;

   /* force date display if xmax not today */
   let d1 = new Date(xmax * 1000);
   let d2 = new Date();
   let forceDate = d1.getDate() !== d2.getDate() || (d2 - d1 > 1000 * 3600 * 24);

   /* use 5 pixel as min tick distance */
   let dx = Math.round((xmax - xmin) / (width / 5));

   let tick_base;
   for (tick_base = 0; base[tick_base]; tick_base++) {
      if (base[tick_base] > dx)
         break;
   }
   if (!base[tick_base])
      tick_base--;
   dx = base[tick_base];

   let major_base = tick_base;
   let major_dx = dx;

   let label_base = major_base;
   let label_dx = dx;

   do {
      let str = timeToLabel(xmin, label_dx, forceDate);
      let maxwidth = ctx.measureText(str).width;

      /* increasing label_dx, if labels would overlap */
      if (maxwidth > 0.8 * label_dx / (xmax - xmin) * width) {
         if (base[label_base + 1])
            label_dx = base[++label_base];
         else
            label_dx += 3600 * 24 * 30;

         if (label_base > major_base + 1) {
            if (base[major_base + 1])
               major_dx = base[++major_base];
            else
               major_dx += 3600 * 24 * 30;
         }

         if (major_base > tick_base + 1) {
            if (base[tick_base + 1])
               dx = base[++tick_base];
            else
               dx += 3600 * 24 * 30;
         }

      } else
         break;
   } while (1);

   let d = new Date(xmin * 1000);
   let tz = d.getTimezoneOffset() * 60;

   let x_act = Math.floor((xmin - tz) / dx) * dx + tz;

   ctx.strokeStyle = this.color.axis;
   ctx.drawLine(x1, y1, x1 + width, y1);

   do {
      let x_screen = Math.round((x_act - xmin) / (xmax - xmin) * width + x1);
      let xs = Math.round(x_screen);

      if (x_screen > x1 + width + 0.001)
         break;

      if (x_screen >= x1) {
         if ((x_act - tz) % major_dx === 0) {
            if ((x_act - tz) % label_dx === 0) {
               // label tick mark
               ctx.strokeStyle = this.color.axis;
               ctx.drawLine(xs, y1, xs, y1 + text);

               // grid line
               if (grid !== 0 && xs > x1 && xs < x1 + width) {
                  ctx.strokeStyle = this.color.grid;
                  ctx.drawLine(xs, y1, xs, y1 + grid);
               }

               // label
               if (label !== 0) {
                  let str = timeToLabel(x_act, label_dx, forceDate);

                  // if labels at edge, shift them in
                  let xl = xs - ctx.measureText(str).width / 2;
                  if (xl < 0)
                     xl = 0;
                  if (xl + ctx.measureText(str).width >= xr)
                     xl = xr - ctx.measureText(str).width - 1;
                  ctx.strokeStyle = this.color.label;
                  ctx.fillStyle = this.color.label;
                  ctx.fillText(str, xl, y1 + label);
               }
            } else {
               // major tick mark
               ctx.strokeStyle = this.color.axis;
               ctx.drawLine(xs, y1, xs, y1 + major);
            }

            // grid line
            if (grid !== 0 && xs > x1 && xs < x1 + width) {
               ctx.strokeStyle = this.color.grid;
               ctx.drawLine(xs, y1 - 1, xs, y1 + grid);
            }
         } else {
            // minor tick mark
            ctx.strokeStyle = this.color.axis;
            ctx.drawLine(xs, y1, xs, y1 + minor);
         }
      }

      x_act += dx;

   } while (1);
};

MhistoryGraph.prototype.download = function (mode) {

   let leftDate = new Date(this.tMin * 1000);
   let rightDate = new Date(this.tMax * 1000);
   let filename = this.group + "-" + this.panel + "-" +
      leftDate.getFullYear() +
      ("0" + leftDate.getMonth() + 1).slice(-2) +
      ("0" + leftDate.getDate()).slice(-2) + "-" +
      ("0" + leftDate.getHours()).slice(-2) +
      ("0" + leftDate.getMinutes()).slice(-2) +
      ("0" + leftDate.getSeconds()).slice(-2) + "-" +
      rightDate.getFullYear() +
      ("0" + rightDate.getMonth() + 1).slice(-2) +
      ("0" + rightDate.getDate()).slice(-2) + "-" +
      ("0" + rightDate.getHours()).slice(-2) +
      ("0" + rightDate.getMinutes()).slice(-2) +
      ("0" + rightDate.getSeconds()).slice(-2);

   // use trick from FileSaver.js
   let a = document.getElementById('downloadHook');
   if (a === null) {
      a = document.createElement("a");
      a.style.display = "none";
      a.id = "downloadHook";
      document.body.appendChild(a);
   }

   if (mode === "CSV") {
      filename += ".csv";

      let data = "Time,";
      this.odb["Variables"].forEach(v => {
         data += v + ",";
      });
      data = data.slice(0, -1);
      data += '\n';

      for (let i = 0; i < this.data[0].time.length; i++) {

         let l = "";
         if (this.data[0].time[i] > this.tMin && this.data[0].time[i] < this.tMax) {
            l += this.data[0].time[i] + ",";
            for (let di = 0; di < this.odb["Variables"].length; di++)
               l += this.data[di].value[i] + ",";
            l = l.slice(0, -1);
            l += '\n';
            data += l;
         }

      }

      let blob = new Blob([data], {type: "text/csv"});
      let url = window.URL.createObjectURL(blob);

      a.href = url;
      a.download = filename;
      a.click();
      window.URL.revokeObjectURL(url);
      dlgAlert("Data downloaded to '" + filename + "'");

   } else if (mode === "PNG") {
      filename += ".png";

      this.canvas.toBlob(function (blob) {
         let url = window.URL.createObjectURL(blob);

         a.href = url;
         a.download = filename;
         a.click();
         window.URL.revokeObjectURL(url);
         dlgAlert("Image downloaded to '" + filename + "'");

      }, 'image/png');
   } else if (mode === "URL") {
      // Create new element
      let el = document.createElement('textarea');

      // Set value (string to be copied)
      let url = this.baseURL + "&group=" + this.group + "&panel=" + this.panel +
         "&A=" + this.tMin + "&B=" + this.tMax;
      url = encodeURI(url);
      el.value = url;

      // Set non-editable to avoid focus and move outside of view
      el.setAttribute('readonly', '');
      el.style = {position: 'absolute', left: '-9999px'};
      document.body.appendChild(el);
      // Select text inside element
      el.select();
      // Copy text to clipboard
      document.execCommand('copy');
      // Remove temporary element
      document.body.removeChild(el);

      dlgMessage("Info", "URL<br/><br/>" + url + "<br/><br/>copied to clipboard", true, false);
   }

};
