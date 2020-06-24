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
      mhist[i].mhg.initializeIPanel(i);
      mhist[i].mhg.resize();
      mhist[i].resize = function () {
         this.mhg.resize();
      };
   }
}

function mihistory_create(parentElement, baseURL, panel, index) {
   let d = document.createElement("div");
   parentElement.appendChild(d);
   d.dataset.baseURL = baseURL;
   d.dataset.panel = panel;
   d.mhg = new MihistoryGraph(d);
   d.mhg.initializeIPanel(index);
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
   this.imageElem.id = "hiImage_" + this.panel;

   this.buttonCanvas = document.createElement("canvas");
   this.buttonCanvas.width = 30;
   this.buttonCanvas.height = 30;
   this.buttonCanvas.style.position = "absolute";
   this.buttonCanvas.style.top = "0px";
   this.buttonCanvas.style.right = "0px";
   this.buttonCanvas.style.zIndex = "11";

   this.axisCanvas = document.createElement("canvas");

   this.fileLabel = document.createElement("div");
   this.fileLabel.id = "fileLabel_" + this.panel;
   this.fileLabel.style.backgroundColor = "white";
   this.fileLabel.style.opacity = "0.7";
   this.fileLabel.style.fontSize = "10px";
   this.fileLabel.style.padding = "2px";
   this.fileLabel.innerHTML = "";

   divElement.style.position = "relative";
   this.fileLabel.style.position = "absolute";
   this.fileLabel.style.top = "4px";
   this.fileLabel.style.right = "34px";
   this.fileLabel.style.zIndex = "10";

   divElement.appendChild(this.imageElem);
   divElement.appendChild(this.fileLabel);
   divElement.appendChild(this.buttonCanvas);
   divElement.appendChild(this.axisCanvas);

   // colors
   this.color = {
      background: "#FFFFFF",
      axis: "#808080",
      mark: "#0000A0",
      label: "#404040",
   };

   // scales
   this.tScale = 8*3600;
   this.tMax = Math.floor(new Date() / 1000);
   this.tMin = this.tMax - this.tScale;
   this.scroll = true;
   this.showZoomButtons = true;
   this.currentTime = 0;
   this.currentIndex = 0;
   this.playMode = 0;
   this.updatePaused = false;

   // overwrite scale from URL if present
   this.requestedTime = Math.floor(decodeURI(getUrlVars()["T"]));
   if (!Number.isNaN(this.requestedTime)) {
      this.tMax = this.requestedTime;
      this.tMin = this.requestedTime - this.tScale;
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
         src: "chevrons-left.svg",
         title: "Play backwards",
         click: function (t) {
            t.scroll = false;
            if (t.playMode > -1)
               t.playMode = -1;
            else
               t.playMode *= 2;
            t.loadOldData();
            t.play();
         }
      },
      {
         src: "chevron-left.svg",
         title: "Step one image back",
         click: function (t) {
            t.scroll = false;
            t.playMode = 0;
            if (t.currentIndex > 0)
               t.currentIndex--;
            t.loadOldData();
            t.play();
         }
      },
      {
         src: "pause.svg",
         title: "Stop playing",
         click: function (t) {
            t.scroll = false;
            t.playMode = 0;
            t.redraw();
         }
      },
      {
         src: "chevron-right.svg",
         title: "Step one image forward",
         click: function (t) {
            t.scroll = false;
            t.playMode = 0;
            if (t.currentIndex < t.imageArray.length-1)
               t.currentIndex++;
            t.loadOldData();
            t.play();
         }
      },
      {
         src: "chevrons-right.svg",
         title: "Play forward",
         click: function (t) {
            t.scroll = false;
            if (t.playMode < 1)
               t.playMode = 1;
            else
               t.playMode *= 2;
            t.loadOldData();
            t.play();
         }
      },
      {
         src: "zoom-in.svg",
         title: "Zoom in time axis",
         click: function (t) {
            t.tScale /= 2;
            t.tMin += t.tScale;
            t.drag.Vt = 0 // stop inertia
            t.redraw();
            if (t.callbacks.timeZoom !== undefined)
               t.callbacks.timeZoom(t);
         }
      },
      {
         src: "zoom-out.svg",
         title: "Zoom out time axis",
         click: function (t) {
            t.tMin -= t.tScale;
            t.tScale *= 2;
            t.drag.Vt = 0 // stop inertia
            t.redraw();
            t.loadOldData();

            if (t.callbacks.timeZoom !== undefined)
               t.callbacks.timeZoom(t);
         }
      },
      {
         src: "play.svg",
         title: "Jump to last image",
         click: function (t) {
            t.playMode = 0;
            t.scroll = true;
            t.drag.Vt = 0; // stop inertia

            t.currentIndex = t.imageArray.length-1;
            t.scrollRedraw();

            if (t.callbacks.jumpToCurrent !== undefined)
               t.callbacks.jumpToCurrent(t);
         }
      },
      {
         src: "clock.svg",
         title: "Select time...",
         click: function (t) {

            let currentYear = new Date().getFullYear();
            let dCur = new Date(t.currentTime * 1000);

            if (document.getElementById('y1').length === 0) {
               for (let i = currentYear; i > currentYear - 5; i--) {
                  let o = document.createElement('option');
                  o.value = i.toString();
                  o.appendChild(document.createTextNode(i.toString()));
                  document.getElementById('y1').appendChild(o);
               }
            }

            document.getElementById('m1').selectedIndex = dCur.getMonth();
            document.getElementById('d1').selectedIndex = dCur.getDate() - 1;
            document.getElementById('h1').selectedIndex = dCur.getHours();
            document.getElementById('y1').selectedIndex = currentYear - dCur.getFullYear

            document.getElementById('dlgQueryQuery').onclick = function () {
               doQueryT(t);
            }.bind(t);

            dlgShow("dlgQueryT");
         }
      },
      {
         src: "download.svg",
         title: "Download current image...",
         click: function (t) {
            t.download();
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

   // load button icons
   this.button.forEach(b => {
      b.img = new Image();
      b.img.src = "icons/" + b.src;
   });

   // mouse event handlers
   divElement.addEventListener("mousedown", this.mouseEvent.bind(this), true);
   divElement.addEventListener("dblclick", this.mouseEvent.bind(this), true);
   divElement.addEventListener("mousemove", this.mouseEvent.bind(this), true);
   divElement.addEventListener("touchstart", this.mouseEvent.bind(this), true);
   divElement.addEventListener("touchmove", this.mouseEvent.bind(this), true);
   divElement.addEventListener("touchend", this.mouseEvent.bind(this), true);
   divElement.addEventListener("touchcancel", this.mouseEvent.bind(this), true);
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

function doQueryT(t) {

   dlgHide('dlgQueryT');

   let d = new Date(
      document.getElementById('y1').value,
      document.getElementById('m1').selectedIndex,
      document.getElementById('d1').selectedIndex + 1,
      document.getElementById('h1').selectedIndex);

   t.scroll = false;
   t.playMode = 0;

   let tm = d.getTime() / 1000;
   t.tMax = tm;
   t.tMin = tm - t.tScale;

   if (tm < t.imageArray[0].time) {
      t.requestedTime = tm;
      t.loadOldData();
   } else {
      t.setCurrentIndex(tm);
   }

   if (t.callbacks.timeZoom !== undefined)
      t.callbacks.timeZoom(t);
}


MihistoryGraph.prototype.keyDown = function (e) {
   if (e.key === "u") {  // 'u' key
      this.scroll = true;
      this.scrollRedraw();
      e.preventDefault();
   }
};

MihistoryGraph.prototype.initializeIPanel = function (index) {

   // Retrieve panel
   this.panel = this.parentDiv.dataset.panel;

   if (this.panel === undefined) {
      dlgMessage("Error", "Definition of \'dataset-panel\' missing for image history panel \'" + this.parentDiv.id + "\'. " +
         "Please use syntax:<br /><br /><b>&lt;div class=\"mjsihistory\" " +
         "data-group=\"&lt;Group&gt;\" data-panel=\"&lt;Panel&gt;\"&gt;&lt;/div&gt;</b>", true);
      return;
   }

   if (this.panel === "")
      return;

   this.index = index;
   this.marker = {active: false};
   this.drag = {active: false};
   this.data = undefined;
   this.pendingUpdates = 0;

   // image arrays
   this.imageArray = [];

   // pause main refresh for a moment
   mhttpd_refresh_pause(true);
   this.updatePaused = true;

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

MihistoryGraph.prototype.setCurrentIndex = function(t) {
   let tmin = Math.abs(t - this.imageArray[0].time);
   let imin = 0;
   for (let i = 0; i < this.imageArray.length; i++) {
      if (Math.abs(t - this.imageArray[i].time) < tmin) {
         tmin = Math.abs(t - this.imageArray[i].time);
         imin = i;
      }
   }
   this.currentIndex = imin;
   this.redraw();
};

MihistoryGraph.prototype.loadInitialData = function () {

   // get time scale from ODB
   this.tScale = timeToSec(this.odb["Timescale"]);

   // overwrite via <div ... data-scale=<value> >
   if (this.parentDiv.dataset.scale !== undefined)
      this.tScale = timeToSec(this.parentDiv.dataset.scale);

   if (!Number.isNaN(this.requestedTime)) {
      this.tMax = this.requestedTime;
      this.tMin = this.requestedTime - this.tScale;
      this.scroll = false;
      this.tMinRequested = this.tMax;
   } else {
      this.tMax = Math.floor(new Date() / 1000);
      this.tMin = this.tMax - this.tScale;
      this.tMinRequested = this.tMax;
   }

   let table = document.createElement("table");
   let row = null;
   let cell;
   let link;

   // load latest image
   mjsonrpc_call("hs_image_retrieve",
      {
         "image": this.panel,
         "start_time": Math.floor(this.tMax),
         "end_time": Math.floor(this.tMax),
      })
      .then(function (rpc) {

         this.receiveData(rpc);
         this.redraw();

         this.updateTimer = window.setTimeout(this.update.bind(this), 1000);
         this.scrollTimer = window.setTimeout(this.scrollRedraw.bind(this), 1000);

      }.bind(this)).catch(function (error) {
      mjsonrpc_error_alert(error);
   });

};

MihistoryGraph.prototype.loadOldData = function () {

   if (this.tMin - this.tScale / 2 < this.tMinRequested) {

      let oldTMinRequested = this.tMinRequested;
      this.tMinRequested = this.tMin - this.tScale;

      this.pendingUpdates++;
      this.parentDiv.style.cursor = "progress";
      mjsonrpc_call("hs_image_retrieve",
         {
            "image": this.panel,
            "start_time": Math.floor(this.tMinRequested),
            "end_time": Math.floor(oldTMinRequested),
         })
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

MihistoryGraph.prototype.loadNextImage = function () {
   // look from current image backwards for first image not loaded
   let n = 0;
   let nParallel = 10; // number of parallel loads
   for (let i = this.currentIndex; i >= 0; i--) {
      if (this.imageArray[i].image.src === undefined || this.imageArray[i].image.src === "") {
         // load up to one window beyond current window
         if (this.imageArray[i].time > this.tMin - this.tScale) {
            this.imageArray[i].image.onload = function () {
               this.mhg.redraw();
            };
            this.imageArray[i].image.mhg = this;
            this.imageArray[i].image.src = this.panel + "/" + this.imageArray[i].image_name;
            n++;
            if (n === nParallel) {
               this.imageArray[i].image.onload = function () {
                  this.mhg.redraw();
                  this.mhg.loadNextImage();
               };
               return;
            }
         }
      }
   }

   // now check images AFTER currentIndex, like if we start with URL T=...
   for (let i = this.imageArray.length-1 ; i >= 0; i--)
      if (this.imageArray[i].image.src === undefined || this.imageArray[i].image.src === "") {
         this.imageArray[i].image.mhg = this;
         this.imageArray[i].image.src = this.panel + "/" + this.imageArray[i].image_name;
         n++;
         if (n === nParallel) {
            this.imageArray[i].image.onload = function () {
               this.mhg.loadNextImage();
            };
            return;
         }
      }

   // all done, so resume updates
   mhttpd_refresh_pause(false);
   this.updatePaused = false;
};

MihistoryGraph.prototype.receiveData = function (rpc) {

   if (rpc.result.data.time.length > 0) {
      let first = (this.imageArray.length === 0);
      let lastImageIndex;
      let newImage = [];
      let i1 = [];

      // append new values to end of array
      for (let i = 0; i < rpc.result.data.time.length; i++) {
         let img = {
            time: rpc.result.data.time[i],
            image_name: rpc.result.data.filename[i],
            image: new Image()
         }
         if (this.imageArray.length === 0 ||
            img.time > this.imageArray[this.imageArray.length - 1].time) {
            this.imageArray.push(img);
            newImage.push(img);
            lastImageIndex = this.imageArray.length - 1;
         } else if (img.time < this.imageArray[0].time) {
            i1.push(img);
         }
      }

      if (i1.length > 0) {
         // add new entries to the left side of the array
         this.imageArray = i1.concat(this.imageArray);
         this.currentIndex += i1.length;
      }

      if (this.scroll)
         this.currentIndex = this.imageArray.length - 1;

      if (!Number.isNaN(this.requestedTime)) {
         this.setCurrentIndex(this.requestedTime);
         this.requestedTime = NaN;
      }

      if (first) {
         // after loading of fist image, resize panel
         let img = this.imageArray[this.currentIndex];
         img.image.onload = function () {
            this.mhg.imageElem.src = this.src;
            this.mhg.imageElem.initialWidth = this.width;
            this.mhg.imageElem.initialHeight = this.height;
            this.mhg.resize();
            if (this.mhg.tMinRequested === 0)
               this.mhg.tMinRequested = this.mhg.tMax;
         };
         img.image.mhg = this;
         // trigger loading of image
         img.image.src = this.panel + "/" + img.image_name;
      } else {
         // load actual image
         this.loadNextImage();
      }
   }
};

MihistoryGraph.prototype.update = function () {

   // don't update if we are paused
   if (this.updatePaused) {
      this.updateTimer = window.setTimeout(this.update.bind(this), 1000);
      return;
   }

   // don't update window if content is hidden (other tab, minimized, etc.)
   if (document.hidden) {
      this.updateTimer = window.setTimeout(this.update.bind(this), 500);
      return;
   }

   let t = Math.floor(new Date() / 1000);
   let tStart = this.imageArray[this.imageArray.length-1].time+1;

   mjsonrpc_call("hs_image_retrieve",
      {
         "image": this.panel,
         "start_time": Math.floor(tStart),
         "end_time": Math.floor(t),
      })
      .then(function (rpc) {

         this.receiveData(rpc);
         this.redraw();

         this.updateTimer = window.setTimeout(this.update.bind(this), 1000);

      }.bind(this)).catch(function (error) {
      mjsonrpc_error_alert(error);
   });

};

MihistoryGraph.prototype.scrollRedraw = function () {
   if (this.scroll) {
      this.tMax = Math.floor(new Date() / 1000);
      this.tMin = this.tMax - this.tScale;
      this.redraw();
   }

   this.scrollTimer = window.setTimeout(this.scrollRedraw.bind(this), 1000);
};

MihistoryGraph.prototype.play = function () {
   window.clearTimeout(this.playTimer);

   if (this.playMode > 0) {
      this.currentIndex += this.playMode;
      if (this.currentIndex >= this.imageArray.length-1) {
         this.currentIndex = this.imageArray.length - 1;
         this.playMode = 0;
         return;
      }
   }

   if (this.playMode < 0) {
      this.currentIndex += this.playMode;
      if (this.currentIndex < 0) {
         this.currentIndex = 0;
         this.playMode = 0;
         return;
      }
   }

   // shift time axis according to current image
   this.tMax = this.imageArray[this.currentIndex].time;
   this.tMin = this.tMax - this.tScale;

   this.redraw();
   this.loadOldData();

   if (this.callbacks.timeZoom !== undefined)
      this.callbacks.timeZoom(this);

   if (this.playMode === 0)
      return;

   this.playTimer = window.setTimeout(this.play.bind(this), 100);
};

MihistoryGraph.prototype.mouseEvent = function (e) {

   // fix buttons for IE
   if (!e.which && e.button) {
      if ((e.button & 1) > 0) e.which = 1;      // Left
      else if ((e.button & 4) > 0) e.which = 2; // Middle
      else if ((e.button & 2) > 0) e.which = 3; // Right
   }

   // calculate mouse coordinates relative to history panel
   let rect = document.getElementById("histPanel").getBoundingClientRect();
   let mouseX, offsetY;

   if (e.type === "touchstart" || e.type === "touchmove" ||
       e.type === "touchend" || e.type === "touchcancel") {
      mouseX = Math.floor(e.changedTouches[e.changedTouches.length - 1].clientX - rect.left);
      offsetY = Math.floor(e.changedTouches[e.changedTouches.length - 1].clientY - rect.top);
   } else {
      mouseX = e.clientX - rect.left;
      offsetY = e.offsetY;
   }

   let cursor = this.pendingUpdates > 0 ? "progress" : "default";
   let title = "";

   if (e.type === "mousedown" || e.type === "touchstart") {

      this.loadOldData();

      // check for buttons
      if (e.target === this.buttonCanvas) {
         this.button.forEach(b => {
            if (offsetY > b.y1 && offsetY < b.y1 + b.width &&
               b.enabled) {
               cursor = "pointer";
               b.click(this);
            }
         });
      }

      // start dragging
      else {
         cursor = "ew-resize";

         this.drag.active = true;
         this.scroll = false;
         this.playMode = 0;
         this.drag.xStart = mouseX;
         this.drag.tStart = this.xToTime(mouseX);
         this.drag.tMinStart = this.tMin;
         this.drag.tMaxStart = this.tMax;
      }

   } else if (e.type === "mouseup" || e.type === "touchend" || e.type === "touchcancel") {

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

   } else if (e.type === "mousemove" || e.type === "touchmove") {

      // change cursor to arrow over image and axis
      cursor = "ew-resize";

      if (this.drag.active) {

         // execute dragging
         cursor = "ew-resize";
         let dt = (mouseX - this.drag.xStart) / (this.x2 - this.x1) * (this.tMax - this.tMin);
         this.tMin = this.drag.tMinStart - dt;
         this.tMax = this.drag.tMaxStart - dt;
         this.drag.lastDt = (mouseX - this.drag.lastOffsetX) / (this.x2 - this.x1) * (this.tMax - this.tMin);
         this.drag.lastT = new Date().getTime();
         this.drag.lastOffsetX = mouseX;

         // don't go into the future
         if (this.tMax > this.drag.lastT / 1000) {
            this.tMax = this.drag.lastT / 1000;
            this.tMin = this.tMax - (this.drag.tMaxStart - this.drag.tMinStart);
         }

         // seach image closest to current time
         if (this.imageArray.length > 0)
            this.setCurrentIndex(this.tMax);

         this.loadOldData();

         if (this.callbacks.timeZoom !== undefined)
            this.callbacks.timeZoom(this);
      }

      // change cursor to pointer over buttons
      if (e.target === this.buttonCanvas) {
         this.button.forEach(b => {
            if (e.offsetX > b.x1 && e.offsetY > b.y1 &&
               e.offsetX < b.x1 + b.width && e.offsetY < b.y1 + b.height) {
               cursor = "pointer";
               title = b.title;
            }
         });
      }

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

   // only use horizontal events
   if (e.deltaX === 0)
      return;

   e.preventDefault();

   this.scroll = false;
   this.playMode = 0;

   this.tMax += e.deltaX * 5;
   this.tMin = this.tMax - this.tScale;

   // don't go into the future
   let t = new Date().getTime();
   if (this.tMax > t / 1000) {
      this.tMax = t / 1000;
      this.tMin = this.tMax - this.tScale;
   }

   // seach image closest to current time
   if (this.imageArray.length > 0)
      this.setCurrentIndex(this.tMax);

   this.loadOldData();
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

      if (this.callbacks.timeZoom !== undefined)
         this.callbacks.timeZoom(this);

      if (this.drag.Vt !== 0)
         window.setTimeout(this.inertia.bind(this), 50);
   }
};

MihistoryGraph.prototype.setTimespan = function (tMin, tMax, scroll) {
   this.tMin = tMin;
   this.tMax = tMax;
   this.scroll = scroll;
   this.setCurrentIndex(tMax);
   this.loadOldData();
};

MihistoryGraph.prototype.resize = function () {
   this.width = this.parentDiv.clientWidth;
   this.height = this.parentDiv.clientHeight;

   this.axisCanvas.width = this.width;
   this.axisCanvas.height = 30;

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
         if (this.parentDiv.parentNode.tagName === "TD") {
            this.parentDiv.parentNode.style.height = this.parentDiv.style.height;
         }
      }
   }
   this.buttonCanvas.height = this.imageElem.height;

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
   if (this.currentTime > 0) {
      let url = window.location.href;
      if (url.search("&T=") !== -1)
         url = url.slice(0, url.search("&T="));
      url += "&T=" + Math.round(this.currentTime);

      if (url !== window.location.href)
         window.history.replaceState({}, "Image History", url);
   }
}

MihistoryGraph.prototype.draw = function () {

   // set image from this.currentIndex
   if (this.imageArray.length > 0) {
      if (this.imageElem.src !== this.imageArray[this.currentIndex].image.src)
         this.imageElem.src = this.imageArray[this.currentIndex].image.src;
      if (!this.imageArray[this.currentIndex].image.complete)
         this.fileLabel.innerHTML = "Loading " + this.imageArray[this.currentIndex].image_name;
      else
         this.fileLabel.innerHTML = this.imageArray[this.currentIndex].image_name;
      this.currentTime = this.imageArray[this.currentIndex].time;
   }

   // check for valid axis
   if (this.tMax === undefined || Number.isNaN(this.tMax))
      return;
   if (this.tMin === undefined || Number.isNaN(this.tMin))
      return;

   // don't go into the future
   let t = new Date().getTime();
   if (this.tMax > t / 1000) {
      this.tMax = t / 1000;
      this.tMin = this.tMax - this.tScale;
   }

   // draw time axis
   this.x1 = 0;
   this.y1 = 30;
   this.x2 = this.width-30;
   this.y2 = 0;

   let ctx = this.axisCanvas.getContext("2d");
   ctx.fillStyle = this.color.background;
   ctx.fillRect(0, 0, this.width, this.height);

   ctx.strokeStyle = this.color.axis;
   ctx.drawLine(this.x1, 8, this.x2, 8);
   ctx.strokeStyle = "#C00000";
   ctx.drawLine(this.x2, 0, this.x2, 30);

   ctx.strokeStyle = this.color.axis;
   this.drawTAxis(ctx, this.x1, 8, this.x2 - this.x1, this.width,
      4, 7, 10, 10, this.tMin, this.tMax);

   // marks on time axis
   for (let i=0 ; i<this.imageArray.length ; i++) {
      let x = this.timeToX(this.imageArray[i].time);
      if (this.imageArray[i].image.src != "" && this.imageArray[i].image.complete)
         ctx.strokeStyle = this.color.mark;
      else
         ctx.strokeStyle = this.color.axis;

      ctx.drawLine(x, 0, x, 8);
   }

   // draw buttons
   ctx = this.buttonCanvas.getContext("2d");
   if (this.button.length*28+2 < this.buttonCanvas.height)
      this.buttonCanvas.height = this.button.length*28+2;

   let y = 0;
   this.button.forEach(b => {
      b.x1 = 1;
      b.y1 = 1 + y * 28;
      b.width = 28;
      b.height = 28;
      b.enabled = true;

      if (b.src === "maximize-2.svg") {
         let s = window.location.href;
         if (s.indexOf("&T") > -1)
            s = s.substr(0, s.indexOf("&T"));
         if (s === encodeURI(this.baseURL + "&group=" + "Images" + "&panel=" + this.panel)) {
            b.enabled = false;
            return;
         }
      }

      if (b.src === "play.svg" && !this.scroll)
         ctx.fillStyle = "#FFC0C0";
      else if (b.src === "chevrons-right.svg" && this.playMode > 0)
         ctx.fillStyle = "#C0FFC0";
      else if (b.src === "chevrons-left.svg" && this.playMode < 0)
         ctx.fillStyle = "#C0FFC0";
      else
         ctx.fillStyle = "#F0F0F0";

      ctx.strokeStyle = "#808080";
      ctx.fillRect(b.x1, b.y1, b.width, b.height);
      ctx.strokeRect(b.x1, b.y1, b.width, b.height);
      ctx.drawImage(b.img, b.x1 + 2, b.y1 + 2);

      if (b.src === "chevrons-left.svg" && this.playMode < 0) {
         ctx.fillStyle = "#404040";
         ctx.font = "8px sans-serif";
         ctx.fillText("x" + (-this.playMode), b.x1+2, b.y1+b.height-2);
      }

      if (b.src === "chevrons-right.svg" && this.playMode > 0) {
         ctx.fillStyle = "#404040";
         ctx.font = "8px sans-serif";
         let s = "x" + this.playMode;
         ctx.fillText(s, b.x1+b.width-ctx.measureText(s).width-2, b.y1+b.height-2);
      }

      y++;
   });

   this.lastDrawTime = new Date().getTime();

   // update URL
   if (this.updateURLTimer !== undefined)
      window.clearTimeout(this.updateURLTimer);

   if (this.index === 0)
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

MihistoryGraph.prototype.download = function () {

   let filename = this.imageArray[this.currentIndex].image_name;

   // use trick from FileSaver.js
   let a = document.getElementById('downloadHook');
   if (a === null) {
      a = document.createElement("a");
      a.style.display = "none";
      a.id = "downloadHook";
      document.body.appendChild(a);
   }

   a.href = this.panel + "/" + filename;
   a.download = filename;
   a.click();
   dlgAlert("Image downloaded to '" + filename + "'");
};
