/********************************************************************\

 Name:         mihistory.js
 Created by:   Stefan Ritt

 Contents:     JavaScript image history routines

 Note: please load midas.js and mhttpd.js before mihistory.js

 \********************************************************************/

LN10 = 2.302585094;
LOG2 = 0.301029996;
LOG5 = 0.698970005;

function mihistory_init() {
   // go through all data-name="mhistory" tags
   let mhist = Array.from(document.getElementsByTagName("div")).filter(d => {
      return d.className === "mjsihistory";
   });

   let baseURL = window.location.href;
   if (baseURL.indexOf("?cmd") > 0)
      baseURL = baseURL.substr(0, baseURL.indexOf("?cmd"));
   baseURL += "?cmd=history";

   for (let i = 0; i < mhist.length; i++) {
      mhist[i].dataset.baseURL = baseURL;
      mhist[i].mhg = new MihistoryGraph(mhist[i]);
      mhist[i].mhg.initializeIPanel();
      mhist[i].mhg.resize();
      mhist[i].resize = function () {
         this.mhg.resize();
      };
   }
}

function mihistory_create(parentElement, baseURL, panel, tMin, tMax) {
   let d = document.createElement("div");
   parentElement.appendChild(d);
   d.dataset.baseURL = baseURL;
   d.dataset.panel = panel;
   d.mhg = new MihistoryGraph(d);
   if (!Number.isNaN(tMin) && !Number.isNaN(tMax)) {
      d.mhg.initTMin = tMin;
      d.mhg.initTMax = tMax;
   }
   d.mhg.initializeIPanel();
   return d;
}

function getUrlVars() {
   let vars = {};
   window.location.href.replace(/[?&]+([^=&]+)=([^&]*)/gi, function (m, key, value) {
      vars[key] = value;
   });
   return vars;
}

function MihistoryGraph(divElement) { // Constructor

   // create canvas inside the div
   this.parentDiv = divElement;
   this.baseURL = divElement.dataset.baseURL;
   this.panel = divElement.dataset.panel;
   this.imageElem = document.createElement("img");
   this.imageElem.style.border = "2px solid #808080";
   this.imageElem.style.margin = "auto";
   this.imageElem.style.display = "block";
   this.imageElem.id = "hiImage";
   this.canvas = document.createElement("canvas");
   this.canvas.style.border = "1ps solid black";
   divElement.appendChild(this.imageElem);
   divElement.appendChild(this.canvas);

   // colors
   this.color = {
      background: "#FFFFFF",
      axis: "#808080",
      mark: "#8CBDFF",
      label: "#404040",
   };

   // scales
   this.tScale = 24*3600;
   this.tMax = Math.floor(new Date() / 1000);
   this.tMin = this.tMax - this.tScale;
   this.tCur = 0;
   this.scroll = true;
   this.showZoomButtons = true;

   // overwrite scale from URL if present
   let tCur = decodeURI(getUrlVars()["A"]);
   if (tCur !== "undefined") {
      this.tCur = tCur;
      this.tMax = tCur;
      this.tmin = tCur = this.tScale;
   }

   // callbacks when certain actions are performed.
   // All callback functions should accept a single parameter, which is the 
   // MhistoryGraph object that triggered the callback.
   this.callbacks = {
      resetAxes: undefined,
      timeZoom: undefined,
      jumpToCurrent: undefined
   };

   // buttons
   this.button = [
      {
         src: "maximize-2.svg",
         title: "Show only this plot",
         click: function (t) {
            window.location.href = t.baseURL + "&group=" + "Images" + "&panel=" + t.panel;
         }
      },
      {
         src: "play.svg",
         title: "Jump to last image",
         click: function (t) {
            t.scroll = true;
            t.scrollRedraw();
            t.drag.Vt = 0; // stop inertia

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
         title: "Download image...",
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
         src: "help-circle.svg",
         title: "Show help",
         click: function () {
            dlgShow("dlgIHelp", false);
         }
      }
   ];

   // load dialogs
   dlgLoad('dlgIHistory.html');

   this.button.forEach(b => {
      b.img = new Image();
      b.img.src = "icons/" + b.src;
   });

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

   let d = new Date(
      document.getElementById('y1').value,
      document.getElementById('m1').selectedIndex,
      document.getElementById('d1').selectedIndex + 1,
      document.getElementById('h1').selectedIndex);

   t.tMax = d.getTime() / 1000;
   t.tMin = t.tMax - 24*3600;
   t.scroll = false;
}


MihistoryGraph.prototype.keyDown = function (e) {
   if (e.key === "u") {  // 'u' key
      this.scroll = true;
      this.scrollRedraw();
      e.preventDefault();
   }
};

MihistoryGraph.prototype.initializeIPanel = function () {

   // Retrieve panel
   this.panel = this.parentDiv.dataset.panel;

   if (this.panel === undefined) {
      dlgMessage("Error", "Definition of \'dataset-panel\' missing for history panel \'" + this.parentDiv.id + "\'. " +
         "Please use syntax:<br /><br /><b>&lt;div class=\"mjshistory\" " +
         "data-group=\"&lt;Group&gt;\" data-panel=\"&lt;Panel&gt;\"&gt;&lt;/div&gt;</b>", true);
      return;
   }

   if (this.panel === "")
      return;

   this.marker = {active: false};
   this.drag = {active: false};
   this.data = undefined;
   this.pendingUpdates = 0;

   // image arrays
   this.imageArray = [];

   // retrieve panel definition from ODB
   mjsonrpc_db_copy(["/History/Images/" + this.panel]).then(function (rpc) {
      if (rpc.result.status[0] !== 1) {
         dlgMessage("Error", "Image \'" + this.panel + "\' not found in ODB", true)
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

MihistoryGraph.prototype.loadInitialData = function () {

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
                  for (let i = 0; i < rpc.result.last_written.length; i++) {
                     if (this.events[i] === "Run transitions") {
                        continue;
                     }
                     let l = rpc.result.last_written[i];
                     last = Math.max(last, l);
                  }

                  let scale = mhg.tMax - mhg.tMin;
                  mhg.tMax = last + scale / 2;
                  mhg.tMin = last - scale / 2;

                  mhg.scroll = false;
                  mhg.marker.active = false;
                  mhg.loadOldData();

                  if (mhg.callbacks.timeZoom !== undefined) {
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

            if (mhg.callbacks.timeZoom !== undefined) {
               mhg.callbacks.timeZoom(mhg);
            }
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

   // load latest images
   mjsonrpc_call("hs_image_retrieve",
      {
         "image": this.panel,
         "start_time": Math.floor(this.tMin),
         "end_time": Math.floor(this.tMax),
      })
      .then(function (rpc) {

         this.receiveData(rpc);
         this.redraw();

         this.updateTimer = window.setTimeout(this.update.bind(this), 1000);

      }.bind(this)).catch(function (error) {
      mjsonrpc_error_alert(error);
   });

};

MihistoryGraph.prototype.loadOldData = function () {

   let dt = Math.floor(this.tMax - this.tMin);

   this.redraw();
};


MihistoryGraph.prototype.receiveData = function (rpc) {
   for (let i=0 ; i<rpc.result.data.time.length ; i++) {
      let img = {
         time:       rpc.result.data.time[i],
         image_name: rpc.result.data.filename[i],
         image :     new Image()
      }
      this.imageArray.push(img);
   }

   this.tCur = this.imageArray[this.imageArray.length - 1].time;
   this.loadImage(this.imageArray[this.imageArray.length - 1]);

   for (let i=0 ; i<rpc.result.data.time.length-2 ; i++) {
      this.imageArray[i].image.src = this.panel + "/" + this.imageArray[i].image_name;
   }
};

MihistoryGraph.prototype.loadImage = function (image) {
   image.image.onload = function() {
      document.getElementById("hiImage").src = this.src;
      this.mhg.imageElem.initialWidth = this.width;
      this.mhg.imageElem.initialHeight = this.height;
      this.mhg.resize();
   }
   image.image.mhg = this;
   image.image.src = this.panel + "/" + image.image_name;
};

MihistoryGraph.prototype.update = function () {

   // don't update window if content is hidden (other tab, minimized, etc.)
   if (document.hidden) {
      this.updateTimer = window.setTimeout(this.update.bind(this), 500);
      return;
   }

   let t = Math.floor(new Date() / 1000);

   /*
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
   */
};

MihistoryGraph.prototype.mouseEvent = function (e) {

   // fix buttons for IE
   if (!e.which && e.button) {
      if ((e.button & 1) > 0) e.which = 1;      // Left
      else if ((e.button & 4) > 0) e.which = 2; // Middle
      else if ((e.button & 2) > 0) e.which = 3; // Right
   }

   let cursor = this.pendingUpdates > 0 ? "progress" : "default";
   let title = "";

   if (e.type === "mousedown") {

      this.intSelector.style.display = "none";

      // check for zoom buttons
      if (e.offsetX > this.width - 30 - 48 && e.offsetX < this.width - 30 - 24 &&
         e.offsetY > this.y1 - 24 && e.offsetY < this.y1) {
         // zoom in
         let delta = this.tMax - this.tMin;
         this.tMin += delta/4;
         this.tMax -= delta/4;
         this.drag.Vt = 0; // stop inertia
         this.redraw();
      }
      if (e.offsetX > this.width - 30 - 24 && e.offsetX < this.width - 30 &&
         e.offsetY > this.y1 - 24 && e.offsetY < this.y1) {
         // zoom out
         let delta = this.tMax - this.tMin;
         this.tMin -= delta/2;
         this.tMax += delta/2;
         this.drag.Vt = 0; // stop inertia
         this.loadOldData();
      }

      // check for dragging
      if (e.offsetX > this.x1 && e.offsetX < this.x2 &&
         e.offsetY > this.y2 && e.offsetY < this.y1) {

         cursor = "ew-resize";

         this.drag.active = true;
         this.scroll = false;
         this.drag.xStart = e.offsetX;
         this.drag.tStart = this.xToTime(e.offsetX);
         this.drag.tMinStart = this.tMin;
         this.drag.tMaxStart = this.tMax;
      }

   } else if (e.type === "mouseup") {

      if (this.drag.active) {
         this.drag.active = false;
         let now = new Date().getTime();
         if (this.drag.lastDt !== undefined && now - this.drag.lastT !== 0)
            this.drag.Vt = this.drag.lastDt / (now - this.drag.lastT);
         else
            this.drag.Vt = 0;
         this.drag.lastMoveT = now;
         window.setTimeout(this.inertia.bind(this), 50);
      }

   } else if (e.type === "mousemove") {

      // stop dragging if outside of axis
      if (e.offsetX < this.x1 || e.offsetX > this.x2 ||
         e.offsetY < this.y2 || e.offsetY > this.y1) {
         this.drag.active = false;
      }

      if (this.drag.active) {

         // execute dragging
         cursor = "ew-resize";
         let dt = (e.offsetX - this.drag.xStart) / (this.x2 - this.x1) * (this.tMax - this.tMin);
         this.tMin = this.drag.tMinStart - dt;
         this.tMax = this.drag.tMaxStart - dt;
         this.drag.lastDt = (e.offsetX - this.drag.lastOffsetX) / (this.x2 - this.x1) * (this.tMax - this.tMin);
         this.drag.lastT = new Date().getTime();
         this.drag.lastOffsetX = e.offsetX;

         // don't go into the future
         if (this.tMax > this.drag.lastT / 1000) {
            this.tMax = this.drag.lastT / 1000;
            this.tMin = this.tMax - (this.drag.tMaxStart - this.drag.tMinStart);
         }

         this.redraw();

      } else {

         // change cursor to arrow over time axis
         if (e.offsetX > this.x1 && e.offsetX < this.x2 &&
            e.offsetY > this.y2 && e.offsetY < this.y1) {
            cursor = "ew-resize";
         }

         // change cursor to pointer over buttons
         this.button.forEach(b => {
            if (e.offsetX > b.x1 && e.offsetY > b.y1 &&
               e.offsetX < b.x1 + b.width && e.offsetY < b.y1 + b.height) {
               cursor = "pointer";
               title = b.title;
            }
         });

      }

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

   } else if (e.type === "dblclick") {

   }

   this.parentDiv.title = title;
   this.parentDiv.style.cursor = cursor;

   e.preventDefault();
};

MihistoryGraph.prototype.mouseWheelEvent = function (e) {

   if (e.offsetX > this.x1 && e.offsetX < this.x2 &&
      e.offsetY > this.y2 && e.offsetY < this.y1) {

      let dt = this.tMax - this.tMin;
      this.tMax -= e.deltaY * 10;
      this.tMin = this.tMax - dt;

      // don't go into the future
      let t = new Date().getTime();
      if (this.tMax > t / 1000) {
         this.tMax = t / 1000;
         this.tMin = this.tMax - dt;
      }

      this.redraw();

      e.preventDefault();
   }
};

MihistoryGraph.prototype.inertia = function () {
   if (this.drag.Vt !== 0) {
      let now = new Date().getTime();
      let dt = now - this.drag.lastMoveT;
      this.drag.lastMoveT = now;

      this.tMin -= this.drag.Vt * dt;
      this.tMax -= this.drag.Vt * dt;

      this.drag.Vt = this.drag.Vt * 0.85;
      if (Math.abs(this.drag.Vt) < 0.005) {
         this.drag.Vt = 0;
      }

      this.redraw();

      if (this.callbacks.timeZoom !== undefined) {
         this.callbacks.timeZoom(this);
      }

      if (this.drag.Vt !== 0)
         window.setTimeout(this.inertia.bind(this), 50);
   }
};

MihistoryGraph.prototype.setTimespan = function (tMin, tMax, scroll) {
   this.tMin = tMin;
   this.tMax = tMax;
   this.scroll = scroll;
   this.loadOldData();
};

MihistoryGraph.prototype.resize = function () {
   this.width = this.parentDiv.clientWidth;
   this.height = this.parentDiv.clientHeight;

   this.canvas.width = this.width;
   this.canvas.height = 30;

   let iAR = 0;
   let vAR = 0;
   if (this.imageElem.initialWidth > 0) {
      iAR = this.imageElem.initialWidth / this.imageElem.initialHeight;
      vAR = this.width / (this.height - 30);
   }

   if (iAR === 0) {
      this.imageElem.width = this.width;
      this.imageElem.height = this.height - 30;
   } else {
      if (iAR < vAR) {
         this.imageElem.height = this.height - 30 - 4;
         this.imageElem.width = (this.height - 30) * iAR;
      } else {
         this.imageElem.width = this.width-4;
         this.imageElem.height = this.width / iAR;
      }

      if (this.imageElem.height + 30 < this.parentDiv.clientHeight) {
         let diff = this.parentDiv.clientHeight - (this.imageElem.height + 30);
         this.parentDiv.style.height = (parseInt(this.parentDiv.style.height) - diff) + "px";
      }
   }

   if (this.intSelector !== undefined)
      this.intSelector.style.display = "none";

   this.redraw();
};

MihistoryGraph.prototype.redraw = function () {
   let f = this.draw.bind(this);
   window.requestAnimationFrame(f);
};

MihistoryGraph.prototype.timeToX = function (t) {
   return (t - this.tMin) / (this.tMax - this.tMin) * (this.x2 - this.x1) + this.x1;
};

MihistoryGraph.prototype.xToTime = function (x) {
   return (x - this.x1) / (this.x2 - this.x1) * (this.tMax - this.tMin) + this.tMin;
};

MihistoryGraph.prototype.yToValue = function (y) {
   return (this.y1 - y) / (this.y1 - this.y2) * (this.yMax - this.yMin) + this.yMin;
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

MihistoryGraph.prototype.updateURL = function() {
   let url = window.location.href;
   if (url.search("&A=") !== -1)
      url = url.slice(0, url.search("&A="));
   url += "&A=" + Math.round(this.tCur);

   window.history.replaceState(null, "Image History", url);
}

MihistoryGraph.prototype.draw = function () {

   // select image closest to selected time
   if (this.imageArray.length > 0) {
      let tmin = Math.abs(this.tMax - this.imageArray[0].time);
      let imin = 0;
      for (let i = 0; i < this.imageArray.length; i++) {
         if (Math.abs(this.tMax - this.imageArray[i].time) < tmin) {
            tmin = Math.abs(this.tMax - this.imageArray[i].time);
            imin = i;
         }
      }
      document.getElementById("hiImage").src = this.imageArray[imin].image.src;
   }

   // draw time axis
   this.x1 = 0;
   this.y1 = 30;
   this.x2 = this.width-20;
   this.y2 = 0;

   let ctx = this.canvas.getContext("2d");
   ctx.fillStyle = this.color.background;
   ctx.fillRect(0, 0, this.width, this.height);

   ctx.strokeStyle = this.color.axis;
   ctx.drawLine(this.x1, 5, this.x2, 5);
   ctx.strokeStyle = "#C00000";
   ctx.drawLine(this.x2, 0, this.x2, 30);

   ctx.strokeStyle = this.color.axis;
   this.drawTAxis(ctx, this.x1, 5, this.x2 - this.x1, this.width,
      4, 7, 10, 10, this.tMin, this.tMax);

   // marks on time axis
   ctx.strokeStyle = this.color.mark;
   for (let i=0 ; i<this.imageArray.length ; i++) {
      let x = this.timeToX(this.imageArray[i].time);
         ctx.drawLine(x, 0, x, 5);
   }

   // buttons
   /*
   let y = 0;
   this.button.forEach(b => {
      b.x1 = this.width - 30;
      b.y1 = 6 + y * 28;
      b.width = 28;
      b.height = 28;
      b.enabled = true;

      if (b.src === "maximize-2.svg")
         if (window.location.href === encodeURI(this.baseURL + "&group=" + "Images" + "&panel=" + this.panel)) {
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
   */

   // zoom buttons
   /*
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
   */

   this.lastDrawTime = new Date().getTime();

   // update URL
   if (this.updateURLTimer !== undefined)
      window.clearTimeout(this.updateURLTimer);
   this.updateURLTimer = window.setTimeout(this.updateURL.bind(this), 500);
};

let ioptions1 = {
   day: '2-digit', month: 'short', year: '2-digit',
   hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit'
};

let ioptions2 = {
   day: '2-digit', month: 'short', year: '2-digit',
   hour12: false, hour: '2-digit', minute: '2-digit'
};

let ioptions3 = {
   day: '2-digit', month: 'short', year: '2-digit',
   hour12: false, hour: '2-digit', minute: '2-digit'
};

let ioptions4 = {day: '2-digit', month: 'short', year: '2-digit'};

let ioptions5 = {hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit'};

let ioptions6 = {hour12: false, hour: '2-digit', minute: '2-digit'};

let ioptions7 = {hour12: false, hour: '2-digit', minute: '2-digit'};

let ioptions8 = {
   day: '2-digit', month: 'short', year: '2-digit',
   hour12: false, hour: '2-digit', minute: '2-digit'
};

let ioptions9 = {day: '2-digit', month: 'short', year: '2-digit'};

function timeToLabel(sec, base, forceDate) {
   let d = new Date(sec * 1000);
   let options;

   if (forceDate) {
      if (base < 60) {
         options = ioptions1;
      } else if (base < 600) {
         options = ioptions2;
      } else if (base < 3600 * 24) {
         options = ioptions3;
      } else {
         options = ioptions4;
      }

      return d.toLocaleDateString('en-GB', options);
   }

   if (base < 60) {
      return d.toLocaleTimeString('en-GB', ioptions5);
   } else if (base < 600) {
      return d.toLocaleTimeString('en-GB', ioptions6);
   } else if (base < 3600 * 3) {
      return d.toLocaleTimeString('en-GB', ioptions7);
   } else if (base < 3600 * 24) {
      options = ioptions8;
   } else {
      options = ioptions9;
   }

   return d.toLocaleDateString('en-GB', options);
}


MihistoryGraph.prototype.drawTAxis = function (ctx, x1, y1, width, xr, minor, major,
                                              text, label, xmin, xmax) {
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

               // label
               if (label !== 0) {
                  let str = timeToLabel(x_act, label_dx, forceDate);

                  // if labels at edge, don't show them
                  let xl = xs - ctx.measureText(str).width / 2;
                  if (xl > 0 && xl + ctx.measureText(str).width < xr) {
                     ctx.strokeStyle = this.color.label;
                     ctx.fillStyle = this.color.label;
                     ctx.fillText(str, xl, y1 + label);
                  }
               }
            } else {
               // major tick mark
               ctx.strokeStyle = this.color.axis;
               ctx.drawLine(xs, y1, xs, y1 + major);
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

MihistoryGraph.prototype.download = function (mode) {

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

   if (mode === "PNG") {
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
