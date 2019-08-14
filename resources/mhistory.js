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
      return d.dataset.name === "mjshistory";
   });

   for (let i = 0; i < mhist.length; i++) {
      mhist[i].mhg = new MhistoryGraph(mhist[i]);
      mhist[i].mhg.initializePanel();
      mhist[i].resize = function () {
         this.mhg.resize();
      };
   }
}

function mhistory_create(parentElement, baseURL, group, panel) {
   let d = document.createElement("div");
   parentElement.appendChild(d);
   d.dataset.baseURL = baseURL;
   d.dataset.group = group;
   d.dataset.panel = panel;
   d.mhg = new MhistoryGraph(d);
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
   this.yMin0 = 0;
   this.yMax0 = 1;
   this.tMax = Math.floor(new Date() / 1000);
   this.tMin = this.tMax - this.tScale;
   this.yMin = this.yMin0;
   this.yMax = this.yMax0;
   this.scroll = true;

   // data aggays
   this.data = [];

   // graph arrays (in screen pixels)
   this.x = [];
   this.y = [];

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

   // buttons
   this.button = [
      {
         src: "menu.svg",
         click: function (t) {
            t.showLabels = !t.showLabels;
            t.redraw();
         }
      },
      {
         src: "maximize-2.svg",
         click: function (t) {
            window.location.href = t.baseURL + "&group=" + t.group + "&panel=" + t.panel;
         }
      },
      {
         src: "rotate-ccw.svg",
         click: function (t) {
            t.yMin = t.yMin0;
            t.yMax = t.yMax0;
            if (t.autoscale)
               t.yMax += (t.yMax0 - t.yMin0) / 10;
            t.tMax = Math.floor(new Date() / 1000);
            t.tMin = t.tMax - t.tScale;
            t.scroll = true;
            t.redraw();
         }
      },
      {
         src: "play.svg",
         click: function (t) {
            t.yMin = t.yMin0;
            t.yMax = t.yMax0;
            if (t.autoscale)
               t.yMax += (t.yMax0 - t.yMin0) / 10;
            t.tMax = Math.floor(new Date() / 1000);
            t.tMin = t.tMax - (t.tMax - t.tMin);
            t.scroll = true;
            t.scrollRedraw();
         }
      },
      {
         src: "clock.svg",
         click: function () {
            dlgMessage("Notification", "Not yet implemented");
         }
      },
      {
         src: "settings.svg",
         click: function (t) {
            window.location.href = "?cmd=oldhistory&group=" + t.group + "&panel=" + t.panel
               + "&hcmd=Config" + "&redir=" + encodeURIComponent(window.location.href);
         }
      },
      {
         src: "help-circle.svg",
         click: function (t) {
            dlgShow(t.helpDialog, false);
         }
      }
   ];

   // help dialog
   this.helpDialog = document.createElement("div");
   this.helpDialog.id = "dlgHelp";
   this.helpDialog.className = "dlgFrame";
   this.helpDialog.style.zIndex = "20";

   this.helpDialog.innerHTML = "<div class=\"dlgTitlebar\" id=\"dlgMessageTitle\">Help</div>" +
      "<div class=\"dlgPanel\" style=\"padding: 5px;\">" +
      "<div id=\"dlgMessageString\">" +

      "<table class='mtable'>" +
      "<tr>" +
      "<td>Action</td>" +
      "<td>Howto</td>" +
      "</tr>" +
      "<tr>" +
      "<td>Horizontal Zoom</td>" +
      "<td>Scroll mouse wheel or drag along X-Axis</td>" +
      "</tr>" +
      "<tr>" +
      "<td>Vertical Zoom</td>" +
      "<td>&nbsp;Press ALT or Shift key and scroll mouse wheel or drag along Y-Axis&nbsp;</td>" +
      "</tr>" +
      "<tr>" +
      "<td>Pan</td>" +
      "<td>Drag inside graph</td>" +
      "</tr>" +
      "<tr>" +
      "<td>Display values</td>" +
      "<td>Hover over graph</td>" +
      "</tr>" +
      "<tr>" +
      "<td>&nbsp;Back to live scolling after pan/zoom&nbsp;</td>" +
      "<td>Click on <img src='icons/play.svg' style='vertical-align:middle' alt='Live scrolling'> or press spacebar </td>" +
      "</tr>" +
      "<tr>" +
      "<td>&nbsp;Reset axis&nbsp;</td>" +
      "<td>Click on <img src='icons/rotate-ccw.svg' style='vertical-align:middle' alt='Reset'></td>" +
      "</tr>" +
      "</table>" +

      "</div>" +
      "<button class=\"dlgButton\" id=\"dlgMessageButton\" style=\"background-color:#F8F8F8\" type=\"button\" " +
      " onClick=\"dlgHide('dlgHelp')\">Close</button>" +
      "</div>";

   document.body.appendChild(this.helpDialog);

   this.button.forEach(b => {
      b.img = new Image();
      b.img.src = "icons/" + b.src;
   });

   // marker
   this.marker = {active: false};

   // mouse event handlers
   divElement.addEventListener("mousedown", this.mouseEvent.bind(this), true);
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

MhistoryGraph.prototype.keyDown = function (e) {
   if (e.key === " ") {  // space key
      let dt = this.tMax - this.tMin;
      this.tMax = Math.floor(new Date() / 1000);
      this.tMin = this.tMax - dt;
      this.scroll = true;
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
         "Please use syntax:<br /><br /><b>&lt;div name=\"mjshistory\" " +
         "data-group=\"&lt;Group&gt;\" data-panel=\"&lt;Panel&gt;\"&gt;&lt;/div&gt;</b>", true);
      return;
   }
   if (this.panel === undefined) {
      dlgMessage("Error", "Definition of \'dataset-panel\' missing for history panel \'" + this.parentDiv.id + "\'. " +
         "Please use syntax:<br /><br /><b>&lt;div name=\"mjshistory\" " +
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
   this.yMin0 = 0;
   this.yMax0 = 0;

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

   this.tScale = timeToSec(this.odb["Timescale"]);
   this.tMax = Math.floor(new Date() / 1000);
   this.tMin = this.tMax - this.tScale;

   this.showLabels = this.odb["Show values"];

   this.autoscale = (this.odb["Minimum"] === this.odb["Maximum"] ||
      this.odb["Minimum"] === "-Infinity" ||
      this.odb["Maximum"] === "Infinity");
   if (!this.autoscale) {
      this.yMin0 = this.odb["Minimum"];
      this.yMax0 = this.odb["Maximum"];
   }

   this.logAxis = this.odb["Log axis"];
   if (this.logAxis)
      this.yMin0 = this.yMin = 1;

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

   this.tMinRequested = this.lastTimeStamp - this.tScale * 2;

   mjsonrpc_call("hs_read_arraybuffer",
      {
         "start_time": this.tMinRequested,
         "end_time": this.lastTimeStamp,
         "events": this.events,
         "tags": this.tags,
         "index": this.index
      }, "arraybuffer")
      .then(function (rpc) {

         this.receiveData(rpc);

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

   if (this.tMin - dt < this.tMinRequested) {

      let oldTMinRequestested = this.tMinRequested;
      this.tMinRequested = this.tMin - dt;

      // let t = Math.floor(new Date() / 1000);
      // console.log((this.tMinRequested - t) + " - " + (oldTMinRequestested - t));

      mjsonrpc_call("hs_read_arraybuffer",
         {
            "start_time": this.tMinRequested,
            "end_time": oldTMinRequestested,
            "events": this.events,
            "tags": this.tags,
            "index": this.index
         }, "arraybuffer")
         .then(function (rpc) {

            this.receiveData(rpc);

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
   let i = 2 + nVars * 2;
   let t0 = array[i];

   // append newer values to end of arrays
   if (this.data === undefined) {

      this.data = [];

      // initial data
      for (let index = 0; index < nVars; index++) {

         this.data.push({time: [], value: []});

         let nData = array[2 + nVars + index];
         for (let j = 0; j < nData; j++) {
            this.data[index].time.push(array[i++]);
            this.data[index].value.push(array[i++]);
         }
      }

      if (this.autoscale) {
         this.data.forEach(d => {
            d.value.forEach(v => {
               if (v > this.yMax0)
                  this.yMax0 = v;
               if (v < this.yMin0)
                  this.yMin0 = v;

            });
         });
      }

   } else if (t0 < this.data[0].time[0]) {

      // add data to the left
      for (let index = 0; index < nVars; index++) {
         let nData = array[2 + nVars + index];
         let i = 2 + nVars * 2 +  // offset first value
            index * nData * 2 +   // offset full channel
            nData * 2 - 1;        // offset end of channel

         for (let j = 0; j < nData; j++) {
            let v = array[i--];
            let t = array[i--];

            if (this.autoscale) {
               if (v > this.yMax0)
                  this.yMax0 = v;
               if (v < this.yMin0)
                  this.yMin0 = v;
            }

            if (t < this.data[index].time[0]) {
               this.data[index].time.unshift(t);
               this.data[index].value.unshift(v);
            }
         }
      }

   } else {

      // add data to the right
      for (let index = 0; index < nVars; index++) {
         let nData = array[2 + nVars + index];
         for (let j = 0; j < nData; j++) {
            let t = array[i++];
            let v = array[i++];

            // add data to the right
            if (t > this.data[index].time[this.data[index].time.length - 1]) {

               if (this.autoscale) {
                  if (v > this.yMax0)
                     this.yMax0 = v;
                  if (v < this.yMin0)
                     this.yMin0 = v;
               }

               this.data[index].time.push(t);
               this.data[index].value.push(v);

               this.lastTimeStamp = t;
            }
         }
      }
   }

   if (this.yMin0 === this.yMax0) {
      this.yMin0 -= 0.5;
      this.yMax0 += 0.5;
   }

   if (this.scroll) {
      if (this.autoscale)
      // leave 10% space above graph
         this.yMax = this.yMax0 + (this.yMax0 - this.yMin0) / 10;
      else
         this.yMax = this.yMax0;
      this.yMin = this.yMin0;
   }
};

MhistoryGraph.prototype.update = function () {

   let t = Math.floor(new Date() / 1000);

   mjsonrpc_call("hs_read_arraybuffer",
      {
         "start_time": this.lastTimeStamp,
         "end_time": t,
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

MhistoryGraph.prototype.mouseEvent = function (e) {

   // fix buttons for IE
   if (!e.which && e.button) {
      if ((e.button & 1) > 0) e.which = 1;      // Left
      else if ((e.button & 4) > 0) e.which = 2; // Middle
      else if ((e.button & 2) > 0) e.which = 3; // Right
   }

   let cursor = "default";

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
         this.redraw();
      }

   } else if (e.type === "mousemove") {

      if (this.drag.active) {

         // execute dragging
         cursor = "move";
         let dt = Math.floor((e.offsetX - this.drag.xStart) / (this.x2 - this.x1) * (this.tMax - this.tMin));
         this.tMin = this.drag.tMinStart - dt;
         this.tMax = this.drag.tMaxStart - dt;
         let dy = (this.drag.yStart - e.offsetY) / (this.y1 - this.y2) * (this.yMax - this.yMin);
         this.yMin = this.drag.yMinStart - dy;
         this.yMax = this.drag.yMaxStart - dy;
         this.redraw();

         this.loadOldData();

      } else {

         // change curser to pointer over buttons
         this.button.forEach(b => {
            if (e.offsetX > b.x1 && e.offsetY > b.y1 &&
               e.offsetX < b.x1 + b.width && e.offsetY < b.y1 + b.height) {
               cursor = "pointer";
            }
         });

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
            let minDist = 100;
            for (let di = 0; di < this.data.length; di++) {
               for (let i = 0; i < this.x[di].length; i++) {
                  if (this.x[di][i] > this.x1 && this.x[di][i] < this.x2) {
                     let d = Math.sqrt(Math.pow(e.offsetX - this.x[di][i], 2) +
                        Math.pow(e.offsetY - this.y[di][i], 2));
                     if (d < minDist) {
                        minDist = d;
                        this.marker.x = this.x[di][i];
                        this.marker.y = this.y[di][i];
                        this.marker.mx = e.offsetX;
                        this.marker.my = e.offsetY;
                        this.marker.graphIndex = di;
                        this.marker.index = i;
                     }
                  }
               }
            }
            this.marker.active = minDist < 10 && e.offsetX > this.x1 && e.offsetX < this.x2;
            this.redraw();
         }
      }
   }

   this.parentDiv.style.cursor = cursor;

   e.preventDefault();
};

MhistoryGraph.prototype.mouseWheelEvent = function (e) {

   if (e.offsetX > this.x1 && e.offsetX < this.x2 &&
      e.offsetY > this.y2 && e.offsetY < this.y1) {
      if (e.altKey || e.shiftKey) {
         let f = (e.offsetY - this.y1) / (this.y2 - this.y1);
         let dtMin = f * (this.yMax - this.yMin) / 100 * e.deltaY;
         let dtMax = (1 - f) * (this.yMax - this.yMin) / 100 * e.deltaY;
         if (((this.yMax + dtMax) - (this.yMin - dtMin)) / (this.yMax0 - this.yMin0) < 1000 &&
            (this.yMax0 - this.yMin0) / ((this.yMax + dtMax) - (this.yMin - dtMin)) < 1000) {
            this.yMin -= dtMin;
            this.yMax += dtMax;
         }
      } else {
         let f = (e.offsetX - this.x1) / (this.x2 - this.x1);
         let dtMin = Math.floor(f * (this.tMax - this.tMin) / 100 * e.deltaY);
         let dtMax = Math.floor((1 - f) * (this.tMax - this.tMin) / 100 * e.deltaY);
         if ((this.tMax + dtMax) - (this.tMin - dtMin) > 10 &&
            (this.tMax + dtMax) - (this.tMin - dtMin) < 3600 * 24 * 365) {
            this.tMin -= dtMin;
            this.tMax += dtMax;
         }
      }

      this.loadOldData();

      this.marker.active = false;
      this.scroll = false;
      this.redraw();

      e.preventDefault();
   }
};

MhistoryGraph.prototype.resize = function () {
   this.canvas.width = this.parentDiv.clientWidth;
   this.canvas.height = this.parentDiv.clientHeight;

   this.width = this.parentDiv.clientWidth;
   this.height = this.parentDiv.clientHeight;

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

MhistoryGraph.prototype.draw = function () {
   let ctx = this.canvas.getContext("2d");

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

   let axisLabelWidth = this.drawVAxis(ctx, 50, this.height - 25, this.height - 35,
      -4, -7, -10, -12, 0, this.yMin, this.yMax, 0, false);

   let variablesWidth = 0;
   this.odb["Variables"].forEach(v => {
      variablesWidth = Math.max(variablesWidth, ctx.measureText(v.substr(v.indexOf(':') + 1)).width);
   });
   variablesWidth += ctx.measureText("00.000000").width;
   let variablesHeight = this.odb["Variables"].length * 17 + 7;

   this.x1 = axisLabelWidth + 15;
   this.y1 = this.height - 25;
   this.x2 = this.width - 30;
   this.y2 = 26;

   ctx.fillStyle = this.color.background;
   ctx.fillRect(0, 0, this.width, this.height);

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

   ctx.save();
   ctx.beginPath();
   ctx.rect(this.x1, this.y2, this.x2 - this.x1, this.y1 - this.y2);
   ctx.clip();

   for (let di = 0; di < this.data.length; di++) {
      ctx.fillStyle = this.odb["Colour"][di];

      this.x[di] = [];
      this.y[di] = [];

      for (let i = 0; i < this.data[di].time.length; i++) {
         this.x[di][i] = this.timeToX(this.data[di].time[i]);
         this.y[di][i] = this.valueToY(this.data[di].value[i]);
      }

      ctx.beginPath();
      let x0;
      let y0;
      let xPrev;
      let xLast;
      for (let i = 0; i < this.x[di].length; i++) {
         if (i === this.x[di].length - 1 || this.x[di][i + 1] >= this.x1) {
            let x = this.x[di][i];
            let y = this.y[di][i];
            if (x0 === undefined) {
               x0 = x;
               y0 = y;
               xPrev = x0;
               ctx.moveTo(x, y);
            } else {
               //if (this.x[di][i] > xPrev + 1) {
               ctx.lineTo(x, y);
               //   xPrev = this.x[di][i];
               //}
            }
            xLast = x;
            if (x > this.x2)
               break;
         }
      }
      ctx.lineTo(xLast, this.y[di][i]);
      ctx.lineTo(xLast, this.y1);
      ctx.lineTo(x0, this.y1);
      ctx.lineTo(x0, y0);
      ctx.globalAlpha = 0.05;
      ctx.fill();
      ctx.globalAlpha = 1;
   }

   for (let di = 0; di < this.data.length; di++) {
      ctx.strokeStyle = this.odb["Colour"][di];

      ctx.beginPath();
      let first = true;
      for (let i = 0; i < this.data[di].time.length; i++) {
         if (i === this.data[di].time.length - 1 || this.x[di][i + 1] >= this.x1) {
            let x = this.x[di][i];
            let y = this.y[di][i];
            if (first) {
               first = false;
               ctx.moveTo(x, y);
            } else {
               ctx.lineTo(x, y);
            }
            if (x > this.x2)
               break;
         }
      }
      ctx.stroke();
   }

   ctx.restore(); // remove clipping

   // labels with variable names and values
   if (this.showLabels) {
      ctx.save();
      ctx.beginPath();
      ctx.rect(this.x1, this.y2, 25 + variablesWidth + 7, variablesHeight + 2);
      ctx.clip();

      ctx.strokeStyle = this.color.axis;
      ctx.fillStyle = "#F0F0F0";
      ctx.globalAlpha = 0.5;
      ctx.strokeRect(this.x1, this.y2, 25 + variablesWidth + 5, variablesHeight);
      ctx.fillRect(this.x1, this.y2, 25 + variablesWidth + 5, variablesHeight);
      ctx.globalAlpha = 1;

      this.odb["Variables"].forEach((v, i) => {
         ctx.lineWidth = 4;
         ctx.strokeStyle = this.color.data[i];
         ctx.drawLine(this.x1 + 5, 40 + i * 17, this.x1 + 20, 40 + i * 17);
         ctx.lineWidth = 1;

         ctx.textAlign = "left";
         ctx.textBaseline = "middle";
         ctx.fillStyle = "#404040";
         ctx.fillText(v.substr(v.indexOf(':') + 1), this.x1 + 25, 40 + i * 17);

         ctx.textAlign = "right";
         if (this.data[i].value.length > 0) {
            // use last point in array
            let index = this.data[i].value.length - 1;

            // use point at current marker
            if (this.marker.active)
               index = this.marker.index;

            // convert value to string with 6 digits
            let value = this.data[i].value[index];
            let str;
            if (value < 1)
               str = value.toFixed(5);
            else
               str = value.toPrecision(6);
            ctx.fillText(str, this.x1 + 25 + variablesWidth, 40 + i * 17);
         } else
            ctx.fillText('no data', this.x1 + 25 + variablesWidth, 40 + i * 17);

      });

      ctx.restore(); // remove clipping
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
      let v = this.data[this.marker.graphIndex].value[this.marker.index];
      let s = this.odb["Variables"][this.marker.graphIndex] + ": " + v.toPrecision(6);

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
      s = timeToLabel(this.data[this.marker.graphIndex].time[this.marker.index], 1, true);
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
};

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
      return;

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

