/********************************************************************\

 Name:         mhistory.js
 Created by:   Stefan Ritt

 Contents:     JavaScript history plotting routines

 Note: please load midas.js and mhttpd.js before mhistory.js

 \********************************************************************/

LN10 = 2.302585094;
LOG2 = 0.301029996;
LOG5 = 0.698970005;

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
      mhist[i].mhg.initializePanel();
      mhist[i].mhg.resize();
      mhist[i].resize = function () {
         this.mhg.resize();
      };
   }
}

function mhistory_create(parentElement, baseURL, group, panel, tMin, tMax) {
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
   d.mhg.initializePanel();
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

   // data arrays
   this.data = [];

   // graph arrays (in screen pixels)
   this.x = [];
   this.y = [];
   // t/v arrays corresponding to x/y
   this.t = [];
   this.v = [];

   // points array with min/max/avg
   this.p = [];

   // dragging
   this.drag = {active: false};

   // axis zoom
   this.zoom = {
      x: {active: false},
      y: {active: false}
   };

   // marker
   this.marker = {active: false};

   // labels
   this.showLabels = false;

   // solo
   this.solo = {active: false, index: undefined};

   // buttons
   this.button = [
      {
         src: "menu.svg",
         title: "Show / hide legend",
         click: function (t) {
            t.showLabels = !t.showLabels;
            t.redraw();
         }
      },
      {
         src: "maximize-2.svg",
         title: "Make plot bigger",
         click: function (t) {
            window.location.href = t.baseURL + "&group=" + t.group + "&panel=" + t.panel;
         }
      },
      {
         src: "rotate-ccw.svg",
         title: "Reset histogram axes",
         click: function (t) {

            t.tMax = Math.floor(new Date() / 1000);
            t.tMin = t.tMax - t.tScale;

            t.yMin0 = t.yMax0 = t.data[0].value[t.data[0].value.length - 1];
            for (let index = 0; index < t.data.length; index++)
               for (let j = 0; j < t.data[index].time.length; j++) {
                  if (t.data[index].time[j] > t.tMin) {
                     let v = t.data[index].value[j];
                     if (t.autoscaleMax)
                        if (v > t.yMax0)
                           t.yMax0 = v;
                     if (t.autoscaleMin)
                        if (v < t.yMin0)
                           t.yMin0 = v;
                  }
               }

            t.yMin = t.yMin0;
            t.yMax = t.yMax0;
            if (t.autoscaleMin)
               t.yMin -= (t.yMax0 - t.yMin0) / 10;
            if (t.autoscaleMax)
               t.yMax += (t.yMax0 - t.yMin0) / 10;

            t.scroll = true;
            t.yZoom = false;
            t.redraw();
         }
      },
      {
         src: "play.svg",
         title: "Jump to current time",
         click: function (t) {
            t.scroll = true;
            t.scrollRedraw();
         }
      },
      {
         src: "clock.svg",
         title: "Select timespan...",
         click: function (t) {
            if (t.intSelector.style.display === "none") {
               t.intSelector.style.display = "block";
               t.intSelector.style.left = ((t.canvas.getBoundingClientRect().x + t.x2) -
                  t.intSelector.offsetWidth) + "px";
               t.intSelector.style.top = (t.parentDiv.getBoundingClientRect().y + this.y1 - 1) + "px";
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
               t.downloadSelector.style.left = ((t.canvas.getBoundingClientRect().x + t.x2) -
                  t.downloadSelector.offsetWidth) + "px";
               t.downloadSelector.style.top = (t.parentDiv.getBoundingClientRect().y + this.y1 - 1) + "px";
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

function doQuery(t) {

   dlgHide('dlgQuery');

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
}


MhistoryGraph.prototype.keyDown = function (e) {
   if (e.key === "u") {  // 'u' key
      this.scroll = true;
      this.scrollRedraw();
      e.preventDefault();
   }
   if (e.key === "Escape") {
      this.solo.active = false;
      this.redraw();
      e.preventDefault();
   }
};

MhistoryGraph.prototype.initializePanel = function () {

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

MhistoryGraph.prototype.loadInitialData = function () {

   this.lastTimeStamp = Math.floor(Date.now() / 1000);

   if (this.initTMin !== undefined && this.initTMin !== "undefined") {
      this.tMin = this.initTMin;
      this.tMax = this.initTMax;
      this.tScale = this.tMax - this.tMin;
      this.scroll = false;
   } else {
      this.tScale = timeToSec(this.odb["Timescale"]);
      this.tMax = Math.floor(new Date() / 1000);
      this.tMin = this.tMax - this.tScale;
   }

   this.showLabels = this.odb["Show values"];

   this.autoscaleMin = (this.odb["Minimum"] === this.odb["Maximum"] ||
      this.odb["Minimum"] === "-Infinity" || this.odb["Minimum"] === "Infinity");
   this.autoscaleMax = (this.odb["Minimum"] === this.odb["Maximum"] ||
      this.odb["Maximum"] === "-Infinity" || this.odb["Maximum"] === "Infinity");

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
               doQuery(this);
            }.bind(this);

            dlgShow("dlgQuery");

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
                  rpc.result.last_written.forEach(l => {
                     last = Math.max(last, l);
                  });

                  let scale = mhg.tMax - mhg.tMin;
                  mhg.tMax = last + scale / 5;
                  mhg.tMin = mhg.tMax - scale;

                  mhg.scroll = false;
                  mhg.marker.active = false;
                  mhg.loadOldData();

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

                  let last = rpc.result.last_written[0];
                  rpc.result.last_written.forEach(l => {
                     last = Math.min(last, l);
                  });

                  let scale = mhg.tMax - mhg.tMin;
                  mhg.tMax = last + scale / 5;
                  mhg.tMin = mhg.tMax - scale;

                  mhg.scroll = false;
                  mhg.marker.active = false;
                  mhg.loadOldData();

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
   this.downloadSelector.id = "intSel";
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
   link = document.createElement("a");
   link.href = "#";
   link.innerHTML = "PNG";
   link.title = "Download image in PNG format";
   link.onclick = function () {
      mhg.downloadSelector.style.display = "none";
      this.download("PNG");
      return false;
   }.bind(this);
   cell.appendChild(link);
   row.appendChild(cell);
   table.appendChild(row);

   this.downloadSelector.appendChild(table);
   document.body.appendChild(this.downloadSelector);

   // load initial data
   this.tMinRequested = this.tMin - this.tScale; // look one window ahead in past
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

         this.pendingUpdates--;
         if (this.pendingUpdates === 0)
            this.parentDiv.style.cursor = "default";

         this.receiveData(rpc);
         this.redraw();

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

   if (this.tMin - dt/2 < this.tMinRequested) {

      let oldTMinRequestested = this.tMinRequested;
      this.tMinRequested = this.tMin - dt;

      this.pendingUpdates++;
      this.parentDiv.style.cursor = "progress";
      mjsonrpc_call("hs_read_arraybuffer",
         {
            "start_time": Math.floor(this.tMinRequested),
            "end_time": Math.floor(oldTMinRequestested),
            "events": this.events,
            "tags": this.tags,
            "index": this.index
         }, "arraybuffer")
         .then(function (rpc) {

            this.pendingUpdates--;
            if (this.pendingUpdates === 0)
               this.parentDiv.style.cursor = "default";

            this.receiveData(rpc);
            this.redraw();

         }.bind(this))
         .catch(function (error) {
            mjsonrpc_error_alert(error);
         });
   }
   this.redraw();
};


MhistoryGraph.prototype.receiveData = function (rpc) {

   // decode binary array
   let array = new Float64Array(rpc);
   let nVars = array[1];
   let i = 2 + nVars * 2;
   let t0 = array[i];

   // append newer values to end of arrays
   if (this.data === undefined) {

      this.data = [];

      // initial data
      for (let index = 0; index < nVars; index++) {

         let formula = this.odb["Formula"];
         this.data.push({time: [], value: []});

         let nData = array[2 + nVars + index];
         let x = undefined;
         let y = undefined;
         if (formula !== undefined && formula[index] !== undefined && formula[index] !== "") {
            for (let j = 0; j < nData; j++) {
               this.data[index].time.push(array[i++]);
               x = array[i++];
               y = eval(formula[index]);
               this.data[index].value.push(y);
            }
         } else {
            for (let j = 0; j < nData; j++) {
               let t = array[i++];
               y = array[i++];
               this.data[index].time.push(t);
               this.data[index].value.push(y);
            }
         }
      }

   } else if (this.data[0].time.length === 0 || t0 < this.data[0].time[0]) {

      // add data to the left
      for (let index = 0; index < nVars; index++) {

         let formula = this.odb["Formula"];

         let nData = array[2 + nVars + index];
         let i = 2 + nVars * 2 +  // offset first value
            index * nData * 2 +   // offset full channel
            nData * 2 - 1;        // offset end of channel

         let x = undefined;
         if (formula !== undefined && formula[index] !== undefined && formula[index] !== "") {
            for (let j = 0; j < nData; j++) {
               x = array[i--];
               let t = array[i--];
               let v = eval(formula[index]);
               if (t < this.data[index].time[0]) {
                  this.data[index].time.unshift(t);
                  this.data[index].value.unshift(v);
               }
            }
         } else {
            for (let j = 0; j < nData; j++) {
               let v = array[i--];
               let t = array[i--];
               if (this.data[index].time.length === 0) {
                  this.data[index].time.push(t);
                  this.data[index].value.push(v);
               }
               if (t < this.data[index].time[0]) {
                  this.data[index].time.unshift(t);
                  this.data[index].value.unshift(v);
               }
            }
         }
      }

   } else {

      // add data to the right
      for (let index = 0; index < nVars; index++) {

         let formula = this.odb["Formula"];

         let nData = array[2 + nVars + index];

         let x = undefined;
         if (formula !== undefined && formula[index] !== undefined && formula[index] !== "") {
            for (let j = 0; j < nData; j++) {
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
            for (let j = 0; j < nData; j++) {
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
};

MhistoryGraph.prototype.update = function () {

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

         this.receiveData(rpc);
         this.redraw();

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
      this.redraw();

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

   if (e.type === "mousedown") {

      // check for buttons
      this.button.forEach(b => {
         if (e.offsetX > b.x1 && e.offsetX < b.x1 + b.width &&
            e.offsetY > b.y1 && e.offsetY < b.y1 + b.width &&
            b.enabled) {
            b.click(this);
         }
      });

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
         this.zoom.x.t1 = this.xToTime(e.offsetX);
      }
      if (e.offsetY < this.y1 && e.offsetY > this.y2 && e.offsetX < this.x1) {
         this.zoom.y.active = true;
         this.scroll = false;
         this.zoom.y.y1 = e.offsetY;
         this.zoom.y.v1 = this.yToValue(e.offsetY);
      }

   } else if (e.type === "mouseup") {

      if (this.drag.active)
         this.drag.active = false;

      if (this.zoom.x.active) {
         let t1 = this.zoom.x.t1;
         let t2 = this.xToTime(this.zoom.x.x2);
         if (t1 > t2)
            [t1, t2] = [t2, t1];
         if (t2 - t1 < 1)
            t1 -= 1;
         this.tMin = t1;
         this.tMax = t2;
         this.zoom.x.active = false;
         this.redraw();
      }

      if (this.zoom.y.active) {
         let v1 = this.zoom.y.v1;
         let v2 = this.yToValue(this.zoom.y.y2);
         if (v1 > v2)
            [v1, v2] = [v2, v1];
         this.yMin = v1;
         this.yMax = v2;
         this.zoom.y.active = false;
         this.yZoom = true;
         this.redraw();
      }

   } else if (e.type === "mousemove") {

      if (this.drag.active) {

         // execute dragging
         cursor = "move";
         let dt = Math.floor((e.offsetX - this.drag.xStart) / (this.x2 - this.x1) * (this.tMax - this.tMin));
         this.tMin = this.drag.tMinStart - dt;
         this.tMax = this.drag.tMaxStart - dt;
         if (this.yZoom) {
            let dy = (this.drag.yStart - e.offsetY) / (this.y1 - this.y2) * (this.yMax - this.yMin);
            this.yMin = this.drag.yMinStart - dy;
            this.yMax = this.drag.yMaxStart - dy;
         }

         this.loadOldData();

      } else {

         // change curser to pointer over buttons
         this.button.forEach(b => {
            if (e.offsetX > b.x1 && e.offsetY > b.y1 &&
               e.offsetX < b.x1 + b.width && e.offsetY < b.y1 + b.height) {
               cursor = "pointer";
               title = b.title;
            }
         });

         // display zoom cursor
         if (e.offsetX > this.x1 && e.offsetX < this.x2 && e.offsetY > this.y1)
            cursor = "ew-resize";
         if (e.offsetY < this.y1 && e.offsetY > this.y2 && e.offsetX < this.x1)
            cursor = "ns-resize";

         // execute axis zoom
         if (this.zoom.x.active) {
            this.zoom.x.x2 = Math.max(this.x1, Math.min(this.x2, e.offsetX));
            this.zoom.x.t2 = this.xToTime(e.offsetX);
            this.redraw();
         }
         if (this.zoom.y.active) {
            this.zoom.y.y2 = Math.max(this.y2, Math.min(this.y1, e.offsetY));
            this.zoom.y.v2 = this.yToValue(e.offsetY);
            this.redraw();
         }

         // check if cursor close to graph point
         if (this.data !== undefined && this.x.length && this.y.length) {
            let minDist = 10000;
            for (let di = 0; di < this.data.length; di++) {

               let i1 = binarySearch(this.x[di], e.offsetX - 10);
               let i2 = binarySearch(this.x[di], e.offsetX + 10);

               for (let i = i1; i < i2; i++) {
                  let d = (e.offsetX - this.x[di][i]) * (e.offsetX - this.x[di][i]) +
                     (e.offsetY - this.y[di][i]) * (e.offsetY - this.y[di][i]);
                  if (d < minDist) {
                     minDist = d;
                     this.marker.graphIndex = di;
                     this.marker.index = i;
                  }
               }
            }
            this.marker.active = Math.sqrt(minDist) < 10 && e.offsetX > this.x1 && e.offsetX < this.x2;
            if (this.marker.active) {
               this.marker.x = this.x[this.marker.graphIndex][this.marker.index];
               this.marker.y = this.y[this.marker.graphIndex][this.marker.index];
               this.marker.t = this.t[this.marker.graphIndex][this.marker.index];
               this.marker.v = this.v[this.marker.graphIndex][this.marker.index];
               this.marker.mx = e.offsetX;
               this.marker.my = e.offsetY;
            }
            this.redraw();
         }
      }
   } else if (e.type === "dblclick") {

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
         this.redraw();
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
         }

      } else if (e.ctrlKey || e.metaKey) {

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
         }
      } else
         return;

      this.marker.active = false;

      e.preventDefault();
   }
};

MhistoryGraph.prototype.resize = function () {
   this.canvas.width = this.parentDiv.clientWidth;
   this.canvas.height = this.parentDiv.clientHeight;

   this.width = this.parentDiv.clientWidth;
   this.height = this.parentDiv.clientHeight;

   if (this.intSelector !== undefined)
      this.intSelector.style.display = "none";

   this.redraw();
};

MhistoryGraph.prototype.redraw = function () {
   let f = this.draw.bind(this);
   window.requestAnimationFrame(f);
};

MhistoryGraph.prototype.timeToX = function (t) {
   return (t - this.tMin) / (this.tMax - this.tMin) * (this.x2 - this.x1) + this.x1;
};

MhistoryGraph.prototype.valueToY = function (v) {
   if (this.logAxis)
      return this.y1 - (Math.log(v) - Math.log(this.yMin)) /
         (Math.log(this.yMax) - Math.log(this.yMin)) * (this.y1 - this.y2);
   else
      return this.y1 - (v - this.yMin) /
         (this.yMax - this.yMin) * (this.y1 - this.y2);
};

MhistoryGraph.prototype.xToTime = function (x) {
   return (x - this.x1) / (this.x2 - this.x1) * (this.tMax - this.tMin) + this.tMin;
};

MhistoryGraph.prototype.yToValue = function (y) {
   return (this.y1 - y) / (this.y1 - this.y2) * (this.yMax - this.yMin) + this.yMin;
};

MhistoryGraph.prototype.findMinMax = function () {

   let n = 0;

   if (!this.autoscaleMin)
      this.yMin0 = this.odb["Minimum"];

   if (!this.autoscaleMax)
      this.yMax0 = this.odb["Maximum"];

   if (!this.autoscaleMin && !this.autoscaleMax) {
      this.yMin = this.yMin0;
      this.yMax = this.yMax0;
      return;
   }

   if (this.autoscaleMin)
      this.yMin0 = undefined;
   if (this.autoscaleMax)
      this.yMax0 = undefined;
   for (let index = 0; index < this.data.length; index++) {
      if (this.events[index] === "Run transitions")
         continue;
      for (let i = 0; i < this.data[index].time.length; i++) {
         let t = this.data[index].time[i];
         let v = this.data[index].value[i];
         if (Number.isNaN(v))
            continue;
         if (t > this.tMin && t < this.tMax) {
            n++;

            if (this.yMin0 === undefined)
               this.yMin0 = v;
            if (this.yMax0 === undefined)
               this.yMax0 = v;
            if (this.autoscaleMin) {
               if (v < this.yMin0)
                  this.yMin0 = v;
            }
            if (this.autoscaleMax) {
               if (v > this.yMax0)
                  this.yMax0 = v;
            }
         }
      }
   }

   if (n === 0) {
      this.yMin0 = -0.5;
      this.yMax0 = 0.5;
   }

   if (this.yMin0 === this.yMax0) {
      this.yMin0 -= 0.5;
      this.yMax0 += 0.5;
   }

   if (!this.yZoom) {
      if (this.autoscaleMin)
      // leave 10% space above graph
         this.yMin = this.yMin0 - (this.yMax0 - this.yMin0) / 10;
      else
         this.yMin = this.yMin0;

      if (this.autoscaleMax)
      // leave 10% space above graph
         this.yMax = this.yMax0 + (this.yMax0 - this.yMin0) / 10;
      else
         this.yMax = this.yMax0;
   }
};

MhistoryGraph.prototype.draw = function () {
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

   this.findMinMax();

   ctx.lineWidth = 1;
   ctx.font = "14px sans-serif";

   if (this.height === undefined || this.width === undefined)
      return;
   if (this.yMin === undefined || Number.isNaN(this.yMin))
      return;
   if (this.yMax === undefined || Number.isNaN(this.yMax))
      return;

   let axisLabelWidth = this.drawVAxis(ctx, 50, this.height - 25, this.height - 35,
      -4, -7, -10, -12, 0, this.yMin, this.yMax, 0, false);

   this.x1 = axisLabelWidth + 15;
   this.y1 = this.height - 25;
   this.x2 = this.width - 30;
   this.y2 = 26;

   // title
   ctx.strokeStyle = this.color.axis;
   ctx.fillStyle = "#F0F0F0";
   ctx.strokeRect(this.x1, 6, this.x2 - this.x1, 20);
   ctx.fillRect(this.x1, 6, this.x2 - this.x1, 20);
   ctx.textAlign = "center";
   ctx.textBaseline = "middle";
   ctx.fillStyle = "#808080";
   ctx.fillText(this.group + " - " + this.panel, (this.x2 + this.x1) / 2, 16);

   ctx.strokeStyle = this.color.axis;
   ctx.drawLine(this.x1, this.y2, this.x2, this.y2);
   ctx.drawLine(this.x2, this.y2, this.x2, this.y1);

   if (this.logAxis && this.yMin < 1E-10)
      this.yMin = 1E-10;
   this.drawVAxis(ctx, this.x1, this.y1, this.y1 - this.y2,
      -4, -7, -10, -12, this.x2 - this.x1, this.yMin, this.yMax, this.logAxis, true);
   //this.drawHAxis(ctx, 50, this.y2-25, this.x2-70, 4, 7, 10, 12, 0, -10, 10, 0);
   this.drawTAxis(ctx, this.x1, this.y1, this.x2 - this.x1, this.width,
      4, 7, 10, 10, this.y2 - this.y1, this.tMin, this.tMax);

   // determine precision
   if (this.yMin === 0)
      this.yPrecision = Math.max(5, Math.ceil(Math.log(Math.abs(this.yMax)) / Math.log(10)) + 3);
   else if (this.yMax === 0)
      this.yPrecision = Math.max(5, Math.ceil(Math.log(Math.abs(this.yMin)) / Math.log(10)) + 3);
   else
      this.yPrecision = Math.max(5, Math.ceil(-Math.log(Math.abs(1 - this.yMax / this.yMin)) / Math.log(10)) + 3);

   this.variablesWidth = 0;
   this.odb["Variables"].forEach((v, i) => {
      if (this.odb.Label[i] !== "")
         this.variablesWidth = Math.max(this.variablesWidth, ctx.measureText(this.odb.Label[i]).width);
      else
         this.variablesWidth = Math.max(this.variablesWidth, ctx.measureText(v.substr(v.indexOf(':') + 1)).width);
   });
   this.variablesWidth += ctx.measureText("0").width * (this.yPrecision + 2);
   this.variablesHeight = this.odb["Variables"].length * 17 + 7;

   ctx.save();
   ctx.beginPath();
   ctx.rect(this.x1, this.y2, this.x2 - this.x1, this.y1 - this.y2);
   ctx.clip();

   // convert values to points
   for (let di = 0; di < this.data.length; di++) {
      this.x[di] = []; // x/y contain visible part of graph
      this.y[di] = [];
      this.t[di] = []; // t/v contain time/value pairs corresponding to x/y
      this.v[di] = [];

      let first = undefined;
      let last = undefined;
      let n = 0;
      for (let i = 0; i < this.data[di].time.length; i++) {
         let x = this.timeToX(this.data[di].time[i]);
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
   }

   // compress points to aggregate values
   let avgN = 0;
   let numberN = 0;
   for (let di = 0; di < this.data.length; di++) {
      if (this.events[di] === "Run transitions")
         continue;
      this.p[di] = [];
      let p = {};

      let xLast = undefined;
      for (let i = 0; i < this.x[di].length; i++) {
         let x = Math.floor(this.x[di][i]);
         let y = this.y[di][i];

         if (i === 0 || x > xLast) {

            if (p.x !== undefined) {
               // store point
               if (p.n > 0)
                  p.avg = p.avg / p.n;
               avgN += p.n;
               numberN++;
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
   }
   if (numberN > 0)
      avgN = avgN / numberN;

   // draw shaded areas
   for (let di = 0; di < this.data.length; di++) {
      if (this.solo.active && this.solo.index !== di)
         continue;
      if (this.events[di] === "Run transitions") {

         if (this.tags[di] === "State") {
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
         continue;
      }

      ctx.fillStyle = this.odb["Colour"][di];

      if (avgN > 2) {
         ctx.beginPath();
         let x0;
         let y0;
         let xLast;
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
         ctx.lineTo(xLast, this.y1);
         ctx.lineTo(x0, this.y1);
         ctx.lineTo(x0, y0);
         ctx.globalAlpha = 0.1;
         ctx.fill();
         ctx.globalAlpha = 1;
      } else {
         ctx.beginPath();
         let x0;
         let y0;
         let xLast;
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
         ctx.lineTo(xLast, this.y1);
         ctx.lineTo(x0, this.y1);
         ctx.lineTo(x0, y0);
         ctx.globalAlpha = 0.1;
         ctx.fill();
         ctx.globalAlpha = 1;
      }
   }

   // draw graphs
   for (let di = 0; di < this.data.length; di++) {
      if (this.solo.active && this.solo.index !== di)
         continue;
      if (this.events[di] === "Run transitions")
         continue;

      ctx.strokeStyle = this.odb["Colour"][di];

      if (avgN > 2) {
         let prevX = undefined;
         let prevY = undefined;
         for (let i = 0; i < this.p[di].length; i++) {
            let p = this.p[di][i];

            // draw line from end of previous cluster to beginning of current cluster
            if (prevX !== undefined)
               ctx.drawLine(prevX, prevY, p.x, p.first);

            // draw min-max line
            ctx.drawLine(p.x, p.min, p.x, p.max + 1);

            prevX = p.x;
            prevY = p.last;
         }
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

   ctx.restore(); // remove clipping

   // labels with variable names and values
   if (this.showLabels) {
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

            // convert value to string with 6 digits
            let value = this.v[i][index];
            let str = value.toPrecision(this.yPrecision);
            ctx.fillText(str, this.x1 + 25 + this.variablesWidth, 40 + i * 17);
         } else
            ctx.fillText('no data', this.x1 + 25 + this.variablesWidth, 40 + i * 17);

      });

      ctx.restore(); // remove clipping
   }

   // "updating" notice
   if (this.pendingUpdates > 0) {
      let str = "Updating data ...";
      ctx.strokeStyle = "#404040";
      ctx.fillStyle = "#F0F0F0";
      ctx.fillRect(this.x1 + 5, this.y1 - 22, 10 + ctx.measureText(str).width, 17);
      ctx.strokeRect(this.x1 + 5, this.y1 - 22, 10 + ctx.measureText(str).width, 17);
      ctx.fillStyle = "#404040";
      ctx.textAlign = "left";
      ctx.textBaseline = "middle";
      ctx.fillText(str, this.x1 + 10, this.y1 - 13);
   }

   // "empty window" notice
   if (this.data[0].time === undefined || this.data[0].time.length === 0) {
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

      if (b.src === "maximize-2.svg")
         if (window.location.href === encodeURI(this.baseURL + "&group=" + this.group + "&panel=" + this.panel)) {
            b.enabled = false;
            return;
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
         s = this.odb.Label[this.marker.graphIndex] + ": " + v.toPrecision(this.yPrecision);
      else
         s = this.odb["Variables"][this.marker.graphIndex] + ": " + v.toPrecision(this.yPrecision);

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
      w = ctx.measureText(s).width + 6;
      h = ctx.measureText("M").width * 1.2 + 6;
      x = this.marker.x - w / 2;
      y = this.y1 + 4;
      if (x <= this.x1)
         x = this.x1;
      if (x + w >= this.x2)
         x = this.x2 - w;

      ctx.strokeStyle = "#808080";
      ctx.fillStyle = "#F0F0F0";
      ctx.fillRect(x, y, w, h);
      ctx.strokeRect(x, y, w, h);
      ctx.fillStyle = "#404040";
      ctx.fillText(s, x + 3, y + h / 2);
   }
}
;

/*
MhistoryGraph.prototype.drawHAxis = function haxisDraw(ctx, x1, y1, width, minor, major,
                                                       text, label, grid, xmin, xmax, logaxis) {
   let dx, int_dx, frac_dx, x_act, label_dx, major_dx, x_screen, maxwidth;
   let tick_base, major_base, label_base, n_sig1, n_sig2, xs;
   let base = [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000];

   ctx.textAlign = "center";
   ctx.textBaseline = "top";

   if (xmax <= xmin || width <= 0)
      return;

   if (logaxis) {
      dx = Math.pow(10, Math.floor(Math.log(xmin) / Math.log(10)));
      if (dx === 0)
         dx = 1E-10;
      label_dx = dx;
      major_dx = dx * 10;
      n_sig1 = 4;
   } else {
      // use 6 as min tick distance
      dx = (xmax - xmin) / (width / 6);

      int_dx = Math.floor(Math.log(dx) / Math.log(10));
      frac_dx = Math.log(dx) / Math.log(10) - int_dx;

      if (frac_dx < 0) {
         frac_dx += 1;
         int_dx -= 1;
      }

      tick_base = frac_dx < (Math.log(2) / Math.log(10)) ? 1 : frac_dx < (Math.log(5) / Math.log(10)) ? 2 : 3;
      major_base = label_base = tick_base + 1;

      /!* rounding up of dx, label_dx *!/
      dx = Math.pow(10, int_dx) * base[tick_base];
      major_dx = Math.pow(10, int_dx) * base[major_base];
      label_dx = major_dx;

      do {
         // number of significant digits
         if (xmin === 0)
            n_sig1 = 0;
         else
            n_sig1 = Math.floor(Math.log(Math.abs(xmin)) /
               Math.log(10)) - Math.floor(Math.log(Math.abs(label_dx)) / Math.log(10)) + 1;

         if (xmax === 0)
            n_sig2 = 0;
         else
            n_sig2 = Math.floor(Math.log(Math.abs(xmax)) /
               Math.log(10)) - Math.floor(Math.log(Math.abs(label_dx)) / Math.log(10)) + 1;

         n_sig1 = Math.max(n_sig1, n_sig2);

         // toPrecision displays 1050 with 3 digits as 1.05e+3, so increase precision to number of digits
         if (Math.abs(xmin) < 100000)
            n_sig1 = Math.max(n_sig1, Math.floor(Math.log(Math.abs(xmin)) / Math.log(10) + 0.001) + 1);
         if (Math.abs(xmax) < 100000)
            n_sig1 = Math.max(n_sig1, Math.floor(Math.log(Math.abs(xmax)) / Math.log(10) + 0.001) + 1);

         // determination of maximal width of labels
         let str = (Math.floor(xmin / dx) * dx).toPrecision(n_sig1);
         let ext = ctx.measureText(str);
         maxwidth = ext.width;

         str = (Math.floor(xmax / dx) * dx).toPrecision(n_sig1).stripZeros();
         ext = ctx.measureText(str);
         maxwidth = Math.max(maxwidth, ext.width);
         str = (Math.floor(xmax / dx) * dx + label_dx).toPrecision(n_sig1).stripZeros();
         maxwidth = Math.max(maxwidth, ext.width);

         // increasing label_dx, if labels would overlap
         if (maxwidth > 0.5 * label_dx / (xmax - xmin) * width) {
            label_base++;
            label_dx = Math.pow(10, int_dx) * base[label_base];
            if (label_base % 3 === 2 && major_base % 3 === 1) {
               major_base++;
               major_dx = Math.pow(10, int_dx) * base[major_base];
            }
         } else
            break;

      } while (true);
   }

   x_act = Math.floor(xmin / dx) * dx;

   let last_label_x = x1;

   ctx.strokeStyle = this.color.axis;
   ctx.drawLine(x1, y1, x1 + width, y1);

   do {
      if (logaxis)
         x_screen = (Math.log(x_act) - Math.log(xmin)) /
            (Math.log(xmax) - Math.log(xmin)) * width + x1;
      else
         x_screen = (x_act - xmin) / (xmax - xmin) * width + x1;
      xs = Math.round(x_screen);

      if (x_screen > x1 + width + 0.001)
         break;

      if (x_screen >= x1) {
         if (Math.abs(Math.round(x_act / major_dx) - x_act / major_dx) <
            dx / major_dx / 10.0) {

            if (Math.abs(Math.round(x_act / label_dx) - x_act / label_dx) <
               dx / label_dx / 10.0) {
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
                  let str = x_act.toPrecision(n_sig1).stripZeros();
                  let ext = ctx.measureText(str);
                  if (xs - ext.width / 2 > x1 &&
                     xs + ext.width / 2 < x1 + width) {
                     ctx.strokeStyle = this.color.label;
                     ctx.fillStyle = this.color.label;
                     ctx.fillText(str, xs, y1 + label);
                  }
                  last_label_x = xs + ext.width / 2;
               }
            } else {
               // major tick mark
               ctx.strokeStyle = this.color.axis;
               ctx.drawLine(xs, y1, xs, y1 + major);

               // grid line
               if (grid !== 0 && xs > x1 && xs < x1 + width) {
                  ctx.strokeStyle = this.color.grid;
                  ctx.drawLine(xs, y1 - 1, xs, y1 + grid);
               }
            }

            if (logaxis) {
               dx *= 10;
               major_dx *= 10;
               label_dx *= 10;
            }
         } else {
            // minor tick mark
            ctx.strokeStyle = this.color.axis;
            ctx.drawLine(xs, y1, xs, y1 + minor);
         }

         // for logaxis, also put labels on minor tick marks
         if (logaxis) {
            if (label !== 0) {
               let str = x_act.toPrecision(n_sig1).stripZeros();
               let ext = ctx.measureText(str);
               if (xs - ext.width / 2 > x1 &&
                  xs + ext.width / 2 < x1 + width &&
                  xs - ext.width / 2 > last_label_x + 2) {
                  ctx.strokeStyle = this.color.label;
                  ctx.fillStyle = this.color.label;
                  ctx.fillText(str, xs, y1 + label);
               }

               last_label_x = xs + ext.width / 2;
            }
         }
      }

      x_act += dx;

      /!* suppress 1.23E-17 ... *!/
      if (Math.abs(x_act) < dx / 100)
         x_act = 0;

   } while (1);
};
*/

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
      if (dy === 0)
         dy = 1E-10;
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
                  let str = y_act.toPrecision(n_sig1).stripZeros();
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
               let str = y_act.toPrecision(n_sig1).stripZeros();
               if (ys - textHeight / 2 > y1 - height &&
                  ys + textHeight / 2 < y1 &&
                  ys + textHeight < last_label_y + 2)
                  if (draw) {
                     ctx.strokeStyle = this.color.label;
                     ctx.fillStyle = this.color.label;
                     ctx.fillText(str, x1 + label, ys);
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

function timeToLabel(sec, base, forceDate) {
   let d = new Date(sec * 1000);
   let options;

   if (forceDate) {
      if (base < 60) {
         options = {
            day: '2-digit', month: 'short', year: '2-digit',
            hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit'
         };
      } else if (base < 600)
         options = {
            day: '2-digit', month: 'short', year: '2-digit',
            hour12: false, hour: '2-digit', minute: '2-digit'
         };
      else if (base < 3600 * 24)
         options = {
            day: '2-digit', month: 'short', year: '2-digit',
            hour12: false, hour: '2-digit', minute: '2-digit'
         };
      else
         options = {day: '2-digit', month: 'short', year: '2-digit'};

      return d.toLocaleDateString('en-GB', options);
   }

   if (base < 60) {
      options = {hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit'};
      return d.toLocaleTimeString('en-GB', options);
   } else if (base < 600) {
      options = {hour12: false, hour: '2-digit', minute: '2-digit'};
      return d.toLocaleTimeString('en-GB', options);
   } else if (base < 3600 * 3) {
      options = {hour12: false, hour: '2-digit', minute: '2-digit'};
      return d.toLocaleTimeString('en-GB', options);
   } else if (base < 3600 * 24)
      options = {
         day: '2-digit', month: 'short', year: '2-digit',
         hour12: false, hour: '2-digit', minute: '2-digit'
      };
   else
      options = {day: '2-digit', month: 'short', year: '2-digit'};

   return d.toLocaleDateString('en-GB', options);
}


MhistoryGraph.prototype.drawTAxis = function (ctx, x1, y1, width, xr, minor, major,
                                              text, label, grid, xmin, xmax) {
   const base = [1, 5, 10, 60, 15 * 60, 30 * 60, 60 * 60, 3 * 60 * 60, 6 * 60 * 60,
      12 * 60 * 60, 24 * 60 * 60, 0];

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
            label_dx += 3600 * 24;

         if (label_base > major_base + 1) {
            if (base[major_base + 1])
               major_dx = base[++major_base];
            else
               major_dx += 3600 * 24;
         }

         if (major_base > tick_base + 1) {
            if (base[tick_base + 1])
               dx = base[++tick_base];
            else
               dx += 3600 * 24;
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
   }

};