//
//  controls.js
//  Custom Controls
//
//  Created by Stefan Ritt on 5/8/15.
//

/*

 Usage
 =====

 Dialog boxes
 ------------

 Dialog boxes consists of normal HTML code defined on the current web page. By
 using the class "dlgFrame" they are automatically hidden until the
 dlgShow() function is called. The dialog has a title bar and can be moved
 around by dragging the title bar. The dlgHide() function should be called
 to close (hide) the dialog.
 
 Following button shows the dialog dlgXXX:
 
 <button type="button" onClick="dlgShow('dlgXXX')">XXX</button>

 Following HTML code defines the dialog dlgXXX:
 
 <div id="dlgXXX" class="dlgFrame">
    <div class="dlgTitlebar">Title</div>
    <div class="dlgPanel">
       <div>Dialog Contents</div>
       <button class="dlgButton" onClick="dlgHide('dlgXXX')">Close</button>
    </div>
 </div>


 Standard dialog boxes
 ---------------------

 dlgAlert(message)
    Replacement for alert() function showing a standard notification
 
 dlgConfirm(message, callback, param)
    Replacement of confirm() dialog. Shows a message dialog with a 'Cancel'
    and 'Ok' button. The callback is called with the first parameter either
    true (if Ok has been clicked) or false (if Cancel has been clicked) and
    the second parameter a copy of 'param' passed to dlgConfirm().

 dlgQuery(message, value, callback, param)
    Replacement of prompt() dialog. Shows a dialog box ith a 'Cancel', 'Ok'
    button and a field to enter a value. 'message' is shown before the
    input filed and can contain a string like 'Please enter value:'. If
    'cancel' is pressed, the 'callback' function is called with the first
    parameter equal 'false'. If 'Ok' is pressed, 'callback' is called with
    the first parameter being the value of the input field. 'param' is just
    passed to the callback function as an optional second parameter. So a
    typical callback function can look like

    function cb(value, param) {
       if (value !== false)
          alert('Value is '+value+', param is '+param);
    }

    where 'param' can also be ommitted.
    
 dlgMessage(title, message, modal, error, callback, param)
    Similar to dlgAlert, but with the option to set a custom title which
    gets a red background if error is true. After the 'Ok' button is pressed,
    the callback function is called with an optional parameter passed to
    dlgMessage. If modal equals true, the whole screen is greyed out and all
    mouse events are captured until the 'Ok' button is pressed.

 dlgWait(time, string)
    Shows a model dialog with a progress bar for 'time' seconds and 'string'
    in the first line. After 'time' seconds the dialog closes automatically.

 Sliders
 -------

 <button name="ctrlHSlider" type="button" class="ctrlHSlider" data-update="xxx()" id="yyy"></button>

 <button name="ctrlVSlider" type="button" class="ctrlVSlider" data-update="xxx()" id="yyy"></button>

 On each change of the slider, the function xxx(value, final) is called with
 value ranging from 0 to 1. Dragging the slider will cause many updates with
 final = false, and once the mouse button got released, the funciton is called
 once with final = true.
 
 To set the slider programmatically call
 
 document.getElementById('yyy').set(0.5);
 
 Valid range is again 0 to 1.


 Progress bars
 -------------

 <div name="ctrlProgress" style="width:xxxpx;color:yyy;" id="zzz"></div>

 document.getElementById('yyy').set(v); // v = 0...1



 Icon buttons
 ------------

 <button name="ctrlButton" data-draw="xxx" type="button" id="yyy" onClick="zzz()"></button>

 function xxx(cvs)
 {
   // example for an up-arrow with 36x36 pixels
   cvs.width = 36;
   cvs.height = 36;
   let ctx = cvs.getContext("2d");
   ctx.fillStyle = "#E0E0E0";
   ctx.fillRect(0, 0, 36, 36);
   ctx.beginPath();
   ctx.moveTo(18, 7);
   ctx.lineTo(31, 27);
   ctx.lineTo(5, 27);
   ctx.lineTo(18, 7);
   ctx.closePath();
   ctx.fillStyle = "#808080";
   ctx.fill();
 }

*/

// default styles for dialog boxes
let controls_css = `<style>
   .dlgFrame {
      font-family: verdana,tahoma,sans-serif;
      border: 1px solid black;
      box-shadow: 6px 6px 10px 4px rgba(0,0,0,0.2);
      border-radius: 6px;
      position: absolute;
      top: 0;
      left: 0;
      z-index: 10;
      display: none; /* pre-hidden */
   }
   .dlgTitlebar {
      user-select: none;
      text-align: center;
      background-color: #C0C0C0;
      border-top-left-radius: 6px;
      border-top-right-radius: 6px;
      font-size: 10pt;
      padding: 2px;
      padding-left: 30px;
      padding-right: 30px;
   }
   .dlgTitlebar:hover {
      cursor: pointer;
   }
   .dlgButton {
      font-size: 10pt;
      background-color: #F8F8F8;
      border: 1px solid #808080;
      border-radius: 6px;
      padding: 2px 4px;
      margin: 3px;
   }
   .dlgButtonDefault {
      font-size: 10pt;
      background-color: #4187F7;
      color: #FFFFFF;
      border: 1px solid #808080;
      border-radius: 6px;
      padding: 2px 4px;
      margin: 3px;
   }
   .dlgButton:hover {
      background-color: #F0F0F0;
   }
   .dlgPanel {
      background-color: #F0F0F0;
      text-align: center;
      padding: 4px;
      border-bottom-left-radius: 6px;
      border-bottom-right-radius: 6px;
   }
   .dlgBlackout {
      background: rgba(0,0,0,.5);
      position: fixed;
      top: 0;
      left: 0;
      bottom: 0;
      right: 0;
      z-index: 20;
   }
   .ctrlHSlider {
      width: 200px;
      height: 30px;
      border-radius: 5px;
      padding: 0;
   }
   .ctrlVSlider {
      width: 20px;
      height: 200px;
      border-radius: 5px;
      padding: 0;
   }
   .ctrlProgress {
      border: 1px solid #A0A0A0;
      border-radius: 5px;
      width: 500px;
      height: 5px;
      background-color: #E0E0E0;
      margin-left: 6px;
      margin-top: 5px;
   }
   .ctrlProgressInd {
      border-radius: 5px;
      width: 0;
     height: 5px;
      background-color: #419bf9;
      margin: 0;
   }
   </style>`;

(function (window) { // anonymous global function
   window.addEventListener("load", ctlInit, false);
})(window);

function ctlInit() {
   let CTL = new Controls();
   CTL.init();
}

function Controls() // constructor
{
}

Controls.prototype.init = function () // scan DOM
{
   // add special style
   document.head.insertAdjacentHTML("beforeend", controls_css);
   
   // scan DOM for controls
   this.ctrlButton = document.getElementsByName("ctrlButton");
   this.ctrlVSlider = document.getElementsByName("ctrlVSlider");
   this.ctrlHSlider = document.getElementsByName("ctrlHSlider");
   this.ctrlProgress = document.getElementsByName("ctrlProgress");

   // ctrlButton
   for (let i = 0; i < this.ctrlButton.length; i++) {
      let cvs = document.createElement("canvas");
      this.ctrlButton[i].appendChild(cvs);

      if (this.ctrlButton[i].dataset.draw !== undefined)
         eval(this.ctrlButton[i].dataset.draw + "(cvs)");
   }

   // ctrlVSlider
   for (let i = 0; i < this.ctrlVSlider.length; i++) {
      let cvs = document.createElement("canvas");
      let sl = this.ctrlVSlider[i];
      cvs.width = sl.clientWidth;
      cvs.height = sl.clientHeight;
      sl.appendChild(cvs);
      sl.canvas = cvs;

      sl.position = 0.5; // slider position 0...1
      sl.addEventListener("click", this.ctrlVSliderHandler.bind(this));
      sl.addEventListener("contextmenu", this.ctrlVSliderHandler.bind(this));
      sl.addEventListener("mousemove", this.ctrlVSliderHandler.bind(this));
      sl.addEventListener("touchmove", this.ctrlVSliderHandler.bind(this));
      sl.draw = this.ctrlVSliderDraw;
      sl.draw(sl);
      sl.set = this.ctrlVSliderSet;
   }

   // ctrlHSlider
   for (let i = 0; i < this.ctrlHSlider.length; i++) {
      let cvs = document.createElement("canvas");
      let sl = this.ctrlHSlider[i];
      cvs.width = sl.clientWidth;
      cvs.height = sl.clientHeight;
      sl.appendChild(cvs);
      sl.canvas = cvs;

      sl.position = 0.5; // slider position 0...1
      sl.addEventListener("click", this.ctrlHSliderHandler.bind(this));
      sl.addEventListener("contextmenu", this.ctrlHSliderHandler.bind(this));
      sl.addEventListener("mousemove", this.ctrlHSliderHandler.bind(this));
      sl.addEventListener("touchmove", this.ctrlHSliderHandler.bind(this));
      sl.addEventListener("mouseup", this.ctrlHSliderHandler.bind(this));
      sl.draw = this.ctrlHSliderDraw;
      sl.draw(sl);
      sl.set = this.ctrlHSliderSet;
   }

   // ctrlProgress
   for (let i = 0; i < this.ctrlProgress.length; i++) {
      let p = this.ctrlProgress[i];
      p.className = "ctrlProgress";
      let ind = document.createElement("div");
      ind.className = "ctrlProgressInd";
      ind.style.height = p.style.height;
      ind.style.backgroundColor = p.style.color;
      p.appendChild(ind);
      p.set = this.ctrlProgressSet;
   }

};

Controls.prototype.ctrlVSliderDraw = function (b) {
   if (b === undefined)
      b = this;
   let w = b.canvas.width;
   let h = b.canvas.height;
   b.sliderOfs = 20;

   let ctx = b.canvas.getContext("2d");
   ctx.fillStyle = "#E0E0E0";
   ctx.fillRect(0, 0, b.canvas.width, b.canvas.height);

   let knob = b.sliderOfs + (1 - b.position) * (h - 2 * b.sliderOfs);

   ctx.strokeStyle = "#A0A0A0";
   ctx.lineWidth = 3;
   ctx.beginPath();
   ctx.moveTo(w / 2, b.sliderOfs);
   ctx.lineTo(w / 2, knob);
   ctx.stroke();

   ctx.strokeStyle = "#00A0FF";
   ctx.beginPath();
   ctx.moveTo(w / 2, knob);
   ctx.lineTo(w / 2, h - b.sliderOfs);
   ctx.stroke();

   ctx.fillStyle = "#E0E0E0";
   ctx.strokeStyle = "#C0C0C0";
   ctx.beginPath();
   ctx.arc(w / 2, knob, 10, 0, 2 * Math.PI);
   ctx.stroke();
   ctx.fill();
};

Controls.prototype.ctrlVSliderSet = function (pos) {
   if (pos < 0)
      pos = 0;
   if (pos > 1)
      pos = 1;
   this.position = pos;
   this.draw();
};

Controls.prototype.ctrlVSliderHandler = function (e) {
   e.preventDefault();
   let y = undefined;
   let b = e.currentTarget;

   if (b.canvas === undefined) // we can get events from parent node
      return;

   if ((e.buttons === 1 && e.type === "mousemove") || e.type === "click")
      y = e.offsetY;
   if (e.type === "touchmove")
      y = e.changedTouches[e.changedTouches.length - 1].clientY - b.getBoundingClientRect().top;

   if (e.type === "contextmenu") {
      b.position = 0.5;
      this.ctrlVSliderDraw(b);
      let f = b.dataset.update;
      if (f.indexOf("("))
         f = f.substr(0, f.indexOf("("));
      window[f](b.position);
   } else {
      if (y !== undefined) {
         b.position = 1 - (y - b.sliderOfs) / (b.clientHeight - 2 * b.sliderOfs);
         if (b.position < 0)
            b.position = 0;
         if (b.position > 1)
            b.position = 1;
         this.ctrlVSliderDraw(b);
         let f = b.dataset.update;
         if (f.indexOf("("))
            f = f.substr(0, f.indexOf("("));
         window[f](b.position);
      }
   }
};

Controls.prototype.ctrlHSliderDraw = function (b) {
   if (b === undefined)
      b = this;
   let w = b.canvas.width;
   let h = b.canvas.height;
   b.sliderOfs = 20;

   let ctx = b.canvas.getContext("2d");
   ctx.fillStyle = "#E0E0E0";
   ctx.fillRect(0, 0, b.canvas.width, b.canvas.height);

   let knob = b.sliderOfs + (b.position) * (w - 2 * b.sliderOfs);

   ctx.strokeStyle = "#A0A0A0";
   ctx.lineWidth = 3;
   ctx.beginPath();
   ctx.moveTo(w - b.sliderOfs, h / 2);
   ctx.lineTo(knob, h / 2);
   ctx.stroke();

   ctx.strokeStyle = "#00A0FF";
   ctx.beginPath();
   ctx.moveTo(knob, h / 2);
   ctx.lineTo(b.sliderOfs, h / 2);
   ctx.stroke();

   ctx.fillStyle = "#E0E0E0";
   ctx.strokeStyle = "#C0C0C0";
   ctx.beginPath();
   ctx.arc(knob, h / 2, 10, 0, 2 * Math.PI);
   ctx.stroke();
   ctx.fill();
};

Controls.prototype.ctrlHSliderSet = function (pos) {
   if (pos < 0)
      pos = 0;
   if (pos > 1)
      pos = 1;
   this.position = pos;
   this.draw();
};

Controls.prototype.ctrlHSliderHandler = function (e) {
   e.preventDefault();
   let x = undefined;
   let b = e.currentTarget;

   if (b.canvas === undefined) // we can get events from parent node
      return;

   if ((e.buttons === 1 && e.type === "mousemove") || e.type === "click")
      x = e.offsetX;
   if (e.type === "touchmove")
      x = e.changedTouches[e.changedTouches.length - 1].clientX - b.getBoundingClientRect().left;

   if (e.type === "contextmenu") {
      b.position = 0.5;
      b.contextMenu = true;
      this.ctrlHSliderDraw(b);
      let f = b.dataset.update;
      if (f.indexOf("("))
         f = f.substr(0, f.indexOf("("));
      window[f](b.position, true);
   }

   if (x !== undefined) {
      b.contextMenu = false;
      b.position = (x - b.sliderOfs) / (b.clientWidth - 2 * b.sliderOfs);
      if (b.position < 0)
         b.position = 0;
      if (b.position > 1)
         b.position = 1;
      this.ctrlHSliderDraw(b);
      let f = b.dataset.update;
      if (f.indexOf("("))
         f = f.substr(0, f.indexOf("("));
      window[f](b.position, false);
   }

   if (e.type === "mouseup" && !b.contextMenu) {
      console.log(e.buttons);
      let f = b.dataset.update;
      if (f.indexOf("("))
         f = f.substr(0, f.indexOf("("));
      window[f](b.position, true);
   }
};

Controls.prototype.ctrlProgressSet = function (value) {
   if (value < 0)
      value = 0;
   if (value > 1)
      value = 1;
   this.firstChild.style.width = Math.round(parseInt(this.style.width) * value) + "px";
};

//-------------------------------------------------------------------------------------------------
var dlgLoadedDialogs = [];

function dlgLoad(url) {
   // check if dialog already laoded
   if (dlgLoadedDialogs.includes(url))
      return;

   dlgLoadedDialogs.push(url);

   // load dialog via AJAX
   return new Promise(function (resolve, reject) {
      let xhr = new XMLHttpRequest();
      xhr.onreadystatechange = function () {
         if (xhr.readyState === 4) {
            if (xhr.status === 200) {
               let d = document.createElement("div");
               d.innerHTML = xhr.responseText;
               document.body.appendChild(d);
               resolve(xhr.responseText);
            } else {
               dlgAlert("network error: see javascript console: dlgLoad() cannot load " + url + ", HTTP status: " + xhr.status);
               reject(xhr.responseURL);
            }
         }
      };

      xhr.open("GET", url, true);
      xhr.setRequestHeader('Content-type', 'text/html');
      xhr.send();
   });
}

function drawCloseButton(c, mark) {
   if (!c.getContext)
      return;
   let ctx = c.getContext("2d");
   ctx.clearRect(0, 0, c.width, c.height);
   ctx.lineWidth = 0.5;
   ctx.beginPath();
   ctx.arc(c.width/2, c.height/2, c.width/2-1, 0, 2*Math.PI);
   ctx.fillStyle = '#FD5E59';
   ctx.fill();
   ctx.strokeStyle = '#DF2020';
   ctx.stroke();
   if (mark) {
      ctx.strokeStyle = '#000000';
      ctx.beginPath();
      ctx.lineWidth = 1;
      ctx.moveTo(c.width/2-3, c.height/2-3);
      ctx.lineTo(c.width/2+3, c.height/2+3);
      ctx.moveTo(c.width/2+3, c.height/2-3);
      ctx.lineTo(c.width/2-3, c.height/2+3);
      ctx.stroke();
   }
}

function dlgCenter(dlg) {
   let d;
   if (typeof dlg === "string")
      d = document.getElementById(dlg);
   else
      d = dlg;

   if (d === null) {
      dlgAlert("Dialog '" + dlg + "' does not exist");
      return;
   }

   d.style.left = Math.round(document.documentElement.clientWidth / 2 - d.offsetWidth / 2) + "px";
   if (document.documentElement.clientHeight / 2 - d.offsetHeight / 2 < 0)
      d.style.top = "0px";
   else
      d.style.top = Math.round(document.documentElement.clientHeight / 2 - d.offsetHeight / 2) + "px";
   if (d.offsetHeight > document.documentElement.clientHeight)
      d.style.position = "absolute";
}

function dlgShow(dlg, modal, param) {
   let d;
   if (typeof dlg === "string")
      d = document.getElementById(dlg);
   else
      d = dlg;

   if (d === null) {
      dlgAlert("Dialog '" + dlg + "' does not exist");
      return;
   }

   // add optional parameter to dialog
   d.param = param;

   // put "close" icon into title bar
   let t;
   if (d.childNodes[0].className === "dlgTitlebar")
      t = d.childNodes[0];
   if (d.childNodes[1] && d.childNodes[1].className === "dlgTitlebar")
      t = d.childNodes[1];
   if (t !== undefined) {
      let ttext = t.innerHTML;
      if (ttext.search('dlgHide') === -1) {
         t.innerHTML = "<div style=\"position: absolute;left: 6px;top: 3px;\" " +
            "onclick=\"dlgClose(this);\">" +
            "<canvas id=\"cvsClose\" width=\"14px\" height=\"14px\"></canvas>" +
            "</div>" + ttext;
      }
      d.canvas = t.childNodes[0].childNodes[0];
      drawCloseButton(d.canvas, false);
   }

   d.dlgAx = 0;
   d.dlgAy = 0;
   d.dlgDx = 0;
   d.dlgDy = 0;
   d.modal = (modal === true);

   d.style.display = "block";
   dlgCenter(d);

   // put dialog on top of all other dialogs
   let dlgs = document.getElementsByClassName("dlgFrame");
   for (let i = 0; i < dlgs.length; i++)
      dlgs[i].style.zIndex = "30"; // on top of blackout (20)
   d.style.zIndex = "31";

   // move dialog right-down if on top of previous one
   if (dlgs.length > 1) {
      d.style.left = (parseInt(dlgs[dlgs.length - 2].style.left) + 30).toString() + "px";
      d.style.top = (parseInt(dlgs[dlgs.length - 2].style.top) + 30).toString() + "px";
   }

   // enable scrolling if dialog box goes beyond screen
   d.oldScroll = window.getComputedStyle(document.body).overflow;
   document.body.style.overflow = "scroll";

   if (d.modal) {
      let b = document.getElementById("dlgBlackout");
      if (b === undefined || b === null) {
         b = document.createElement("div");
         b.id = "dlgBlackout";
         b.className = "dlgBlackout";
         document.body.appendChild(b);
      }

      b.style.display = "block";
      d.dlgBlackout = b;
   }

   d.dlgMouseDown = function (e) {
      if (d.style.display === "none")
         return;

      // ignore right mouse clicks
      if (e.button !== 0)
         return;

      if ((e.target === this || e.target.parentNode === this) &&
         e.target.className === "dlgTitlebar") {
         e.preventDefault();
         this.Ax = e.clientX;
         this.Ay = e.clientY;
         this.Dx = parseInt(this.style.left);
         this.Dy = parseInt(this.style.top);
      }

      if (d.modal && e.target !== d && !d.contains(e.target)) {
         // catch all mouse events outside the dialog
         e.preventDefault();
      } else {
         if (e.target === this || d.contains(e.target)) {
            let dlgs = document.getElementsByClassName("dlgFrame");
            for (let i = 0; i < dlgs.length; i++)
               dlgs[i].style.zIndex = "30";
            d.style.zIndex = "31";
         }
      }
   };

   d.dlgMouseMove = function (e) {
      if (d.style.display === "none")
         return;

      // draw close button with "x" if mouse cursor is inside
      drawCloseButton(d.canvas, e.target === d.canvas);

      if (this.Ax > 0 && this.Ay > 0) {
         e.preventDefault();
         let x = e.clientX;
         let y = e.clientY;
         // stop dragging if leaving window
         if (x < 0 || y < 0 ||
            x > document.documentElement.clientWidth ||
            y > document.documentElement.clientHeight ||
            (this.Dy + (y - this.Ay)) < 0) {
            this.Ax = 0;
            this.Ay = 0;
         } else {
            this.style.left = (this.Dx + (x - this.Ax)) + "px";
            this.style.top = (this.Dy + (y - this.Ay)) + "px";
         }
      }
   };

   d.dlgMouseUp = function () {
      this.Ax = 0;
      this.Ay = 0;
   };

   d.dlgTouchStart = function (e) {
      if (d.style.display === "none")
         return;

      if ((e.target === this || e.target.parentNode === this) &&
         e.target.className === "dlgTitlebar") {
         e.preventDefault();
         this.Ax = e.targetTouches[0].clientX;
         this.Ay = e.targetTouches[0].clientY;
         this.Dx = parseInt(this.style.left);
         this.Dy = parseInt(this.style.top);
      }

      if (d.modal && e.target !== d && !d.contains(e.target)) {
         // catch all mouse events
         e.preventDefault();
      } else {
         if (e.target === this || d.contains(e.target)) {
            let dlgs = document.getElementsByClassName("dlgFrame");
            for (let i = 0; i < dlgs.length; i++)
               dlgs[i].style.zIndex = "30";
            d.style.zIndex = "31";
         }
      }
   };

   d.dlgTouchMove = function (e) {
      if (d.style.display === "none")
         return;

      if (this.Ax > 0 && this.Ay > 0) {
         e.preventDefault();
         let x = e.changedTouches[e.changedTouches.length - 1].clientX;
         let y = e.changedTouches[e.changedTouches.length - 1].clientY;
         this.style.left = (this.Dx + (x - this.Ax)) + "px";
         this.style.top = (this.Dy + (y - this.Ay)) + "px";
      }
   };

   d.dlgTouchEnd = function (e) {
      if (d.style.display === "none")
         return;

      if (this.Ax > 0 && this.Ay > 0) {
         e.preventDefault();
         this.Ax = 0;
         this.Ay = 0;
      }
   };

   d.dlgTouchCancel = function (e) {
      if (d.style.display === "none")
         return;

      if (this.Ax > 0 && this.Ay > 0) {
         e.preventDefault();
         this.Ax = 0;
         this.Ay = 0;
      }
   };

   window.addEventListener("mousedown", d.dlgMouseDown.bind(d), true);
   window.addEventListener("mousemove", d.dlgMouseMove.bind(d), true);
   window.addEventListener("mouseup", d.dlgMouseUp.bind(d), true);
   window.addEventListener("touchstart", d.dlgTouchStart.bind(d), true);
   window.addEventListener("touchmove", d.dlgTouchMove.bind(d), true);
   window.addEventListener("touchend", d.dlgTouchEnd.bind(d), true);
   window.addEventListener("touchcancel", d.dlgTouchCancel.bind(d), true);
}

function dlgMove(d, x, y) {
   d.style.left = x + "px";
   d.style.top = y + "px";
}

function dlgClose(div) {
   let dlg = div.parentNode.parentNode;
   if (dlg.shouldDestroy)
      dlgMessageDestroy(div);
   else if (dlg.id !== undefined)
      dlgHide(dlg.id);
}

function dlgHide(dlg) {
   if (typeof dlg === "string")
      dlg = document.getElementById(dlg);
   else if (dlg.type === "button") {
      do {
         dlg = dlg.parentElement;
      } while (dlg.className !== 'dlgFrame');
   }

   if (dlg.modal) {
      // only remove blackout if we are the only modal dialog left
      let dlgs = document.getElementsByClassName("dlgFrame");
      let n=0;
      for (let i = 0; i < dlgs.length; i++)
         if (dlgs[i].style.display === "block" && dlgs[i].modal)
            n++;
      if (n === 1) {
         let d = document.getElementById("dlgBlackout");
         if (d !== undefined && d !== null)
            d.style.display = "none";
      }
   }
   dlg.style.display = "none";
   if (dlg.oldScroll !== "")
      document.body.style.overflow = dlg.oldScroll;
}

function dlgMessageDestroy(b) {
   let dlg = b.parentElement.parentElement;
   if (dlg.modal) {
      // only remove blackout if we are the only modal dialog left
      let dlgs = document.getElementsByClassName("dlgFrame");
      let n=0;
      for (let i = 0; i < dlgs.length; i++)
         if (dlgs[i].style.display === "block" && dlgs[i].modal)
            n++;
      if (n === 1) {
         let d = document.getElementById("dlgBlackout");
         if (d !== undefined && d !== null)
            d.style.display = "none";
      }
   }
   // dialog is not really removed from memory, event listerner is still active and
   // grabs mousdown events, so mark its display "none" to prevent eating mouse events
   // above in dlgMouseDown routine
   dlg.style.display = "none";
   document.body.removeChild(dlg);
}

function dlgMessage(title, string, modal, error, callback, param) {
   let d = document.createElement("div");
   d.className = "dlgFrame";
   d.style.zIndex = modal ? "31" : "30";
   d.callback = callback;
   d.callbackParam = param;
   d.shouldDestroy = true;

   d.innerHTML = "<div class=\"dlgTitlebar\" id=\"dlgMessageTitle\">" + title + "</div>" +
      "<div class=\"dlgPanel\" style=\"padding: 30px;\">" +
      "<div id=\"dlgMessageString\">" + string + "</div>" +
      "<br /><br /><button class=\"dlgButton\" id=\"dlgMessageButton\" style=\"background-color:#F8F8F8\" type=\"button\" " +
      " onClick=\"let d=this.parentElement.parentElement;if(d.callback!==undefined)d.callback(d.callbackParam);dlgMessageDestroy(this)\">Close</button>" +
      "</div>";

   document.body.appendChild(d);

   if (error === true) {
      let t = document.getElementById("dlgMessageTitle");
      t.style.backgroundColor = "#9E2A2A";
      t.style.color = "white";
   }

   dlgShow(d, modal);
   return d;
}

function dlgAlert(s, callback) {
   dlgMessage('Message', s, true, false, callback);
}

function dlgConfirm(string, confirmCallback, param) {
   let d = document.createElement("div");
   d.className = "dlgFrame";
   d.style.zIndex = "31";
   d.callback = confirmCallback;
   d.callbackParam = param;
   d.shouldDestroy = true;

   d.innerHTML = "<div class=\"dlgTitlebar\" id=\"dlgMessageTitle\">Please confirm</div>" +
      "<div class=\"dlgPanel\" style=\"padding: 30px;\">" +
      "<div id=\"dlgMessageString\">" + string + "</div>" +
      "<br /><br />" +
      "<button class=\"dlgButtonDefault\" id=\"dlgMessageButtonOk\" type=\"button\" " +
      " onClick=\"let d=this.parentElement.parentElement;d.callback(true,d.callbackParam);dlgMessageDestroy(this);\">OK</button>" +
      "<button class=\"dlgButton\" id=\"dlgMessageButtonCancel\" type=\"button\" " +
      " onClick=\"let d=this.parentElement.parentElement;d.callback(false,d.callbackParam);dlgMessageDestroy(this);\">Cancel</button>" +
      "</div>";

   document.body.appendChild(d);

   dlgShow(d, true);
   document.getElementById('dlgMessageButtonOk').focus();
   return d;
}

function dlgQueryKeyDown(event, inp) {
   let keyCode = ('which' in event) ? event.which : event.keyCode;

   if (keyCode === 27) {
      // cancel editing
      let d = inp.parentElement.parentElement.parentElement;
      d.callback(false, d.callbackParam);
      dlgMessageDestroy(inp.parentElement);
      return false;
   }

   if (keyCode === 13) {
      // finish editing
      let d = inp.parentElement.parentElement.parentElement;
      d.callback(inp.value, d.callbackParam);
      dlgMessageDestroy(inp.parentElement);
      return false;
   }

   return true;
}

function dlgQuery(string, value, queryCallback, param) {
   let d = document.createElement("div");
   d.className = "dlgFrame";
   d.style.zIndex = "31";
   d.callback = queryCallback;
   d.callbackParam = param;
   d.shouldDestroy = true;

   d.innerHTML = "<div class=\"dlgTitlebar\" id=\"dlgMessageTitle\">&nbsp;</div>" +
       "<div class=\"dlgPanel\" style=\"padding: 20px;\">" +
       "<div id=\"dlgMessageString\">" + string + "&nbsp;&nbsp;<input type='text' size='30' id='dlgQueryInput' onkeydown='return dlgQueryKeyDown(event, this);' value='" + value + "'></input></div>" +
       "<br /><br />" +
       "<button class=\"dlgButtonDefault\" id=\"dlgMessageButton\" type=\"button\" " +
       " onClick=\"let d=this.parentElement.parentElement;d.callback(document.getElementById('dlgQueryInput').value,d.callbackParam);dlgMessageDestroy(this);\">OK</button>" +
       "<button class=\"dlgButton\" id=\"dlgMessageButton\" type=\"button\" " +
       " onClick=\"let d=this.parentElement.parentElement;d.callback(false,d.callbackParam);dlgMessageDestroy(this);\">Cancel</button>" +
       "</div>";

   document.body.appendChild(d);

   dlgShow(d, true);
   document.getElementById('dlgQueryInput').focus();
   document.getElementById('dlgQueryInput').select();

   return d;
}

let dlgWaitDialog;
let dlgWaitProgress;
let dlgWaitTime;
let dlgWaitFunc;
let dlgWaitFuncParam;

function dlgWait(time, string, func, param) {

   let d = document.createElement("div");
   d.className = "dlgFrame";
   d.style.zIndex = "31";
   d.shouldDestroy = true;

   <!-- wait dialog -->
   d.innerHTML = "<div class=\"dlgTitlebar\" id=\"dlgMessageTitle\">Please wait...</div>" +
      "<div class=\"dlgPanel\" style=\"padding: 20px;\">" +
      "<div id=\"dlgMessageString\">" + string + "</div>" +
      "<br />" +
      "<div name=\"ctrlProgress\" style=\"width:250px;\" id=\"dlgWaitProgress\"></div>" +
      "</div>";

   document.body.appendChild(d);

   // init progress bar
   let p = document.getElementById('dlgWaitProgress');
   p.className = "ctrlProgress";
   let ind = document.createElement("div");
   ind.className = "ctrlProgressInd";
   ind.style.height = p.style.height;
   ind.style.backgroundColor = p.style.color;
   p.appendChild(ind);
   p.set = function (value) {
      this.firstChild.style.width = Math.round(parseInt(this.style.width) * value) + "px";
   };

   dlgShow(d, true);
   dlgWaitDialog = d;

   dlgWaitProgress = 0;
   dlgWaitTime = time;
   dlgWaitFunc = func;
   dlgWaitFuncParam = param;
   window.setTimeout(updateDlgWaitProgress, 100);
}

function updateDlgWaitProgress() {
   dlgWaitProgress += 0.1;
   let d = document.getElementById("dlgWaitProgress");
   d.set(dlgWaitProgress / dlgWaitTime);
   if (dlgWaitProgress >= dlgWaitTime) {
      dlgWaitDialog.style.display = "none";
      document.body.removeChild(dlgWaitDialog);
      let d = document.getElementById("dlgBlackout");
      if (d !== undefined && d !== null)
         d.style.display = "none";
      if (dlgWaitFunc !== undefined)
         dlgWaitFunc(dlgWaitFuncParam);
   } else
      window.setTimeout(updateDlgWaitProgress, 100);
}
