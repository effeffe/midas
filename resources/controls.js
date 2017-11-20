//
//  controls.js
//  Custom Controls
//
//  Created by Stefan Ritt on 5/8/15.
//

/*

 Usage
 =====

 Dialogs
 -------

 <button type="button" onClick="dlgShow('dlgXXX')">XXX</button>

 <div id="dlgXXX" class="dlgFrame">
   <div class="dlgTitlebar">Title</div>
   <div class="dlgPanel">
   <div>Dialog Contents</div>
     <button class="dlgButton" onClick="dlgHide('dlgDemo')">Close</button>
   </div>
 </div>


 Sliders
 -------

 <button name="ctrlHSlider" type="button" id="someId" class="ctrlHSlider" data-update="xxx()"></button>

 <button name="ctrlVSlider" type="button" id="otherId" class="ctrlVSlider" data-update="xxx()"></button>

 On each change of the slider, the function xxx(value, final) is called with
 value ranging from 0 to 1. Dragging the slider will cause many updates with
 final = false, and once the mouse button got released, the funciton is called
 once with final = true.

 To set the slider programmatically call

    document.getElementById('someId').set(0.5;

 Valid range is again 0 to 1.

 */

document.write("<style>" +
   ".dlgFrame {" +
   "   font-family: verdana,tahoma,sans-serif;" +
   "   border: 1px solid black;" +
   "   box-shadow: 4px 4px 10px 4px rgba(0,0,0,0.2);" +
   "   border-radius: 6px;" +
   "   position: absolute;" +
   "   top: 0;" +
   "   left: 0;" +
   "   z-index: 10;" +
   "   display: none; /* pre-hidden */" +
   "}" +
   ".dlgTitlebar {" +
   "   user-select: none;" +
   "   text-align: center;" +
   "   background-color: #C0C0C0;" +
   "   border-top-left-radius: 6px;" +
   "   border-top-right-radius: 6px;" +
   "   font-size: 12pt;" +
   "   padding: 3px;" +
   "}" +
   ".dlgTitlebar:hover {" +
   "   cursor: pointer;" +
   "}" +
   ".dlgButton {" +
   "   font-size: 1em;" +
   "   background-color: #D0D0D0;" +
   "   border: 1px solid #808080;" +
   "   border-radius: 6px;" +
   "   padding: 4px 10px;" +
   "   margin: 3px;" +
   "}" +
   ".dlgButton:hover {" +
   "   background-color: #F0F0F0;" +
   "}" +
   ".dlgPanel {" +
   "   background-color: #F0F0F0;" +
   "   text-align: center;" +
   "   padding: 10px;" +
   "   border-bottom-left-radius: 6px;" +
   "   border-bottom-right-radius: 6px;" +
   "}" +
   ".dlgBlackout {" +
   "   background: rgba(0,0,0,.5);" +
   "   position: fixed;" +
   "   top: 0;" +
   "   left: 0;" +
   "   bottom: 0;" +
   "   right: 0;" +
   "   z-index: 20;" +
   "}" +
   ".ctrlHSlider {" +
   "   width: 200px;" +
   "   height: 30px;" +
   "   border-radius: 5px;" +
   "   padding: 0;" +
   "}" +
   ".ctrlVSlider {" +
   "   width: 20px;" +
   "   height: 200px;" +
   "   border-radius: 5px;" +
   "   padding: 0;" +
   "}" +
"</style>");

(function (window) { // anonymous global function
   window.addEventListener("load", ctlInit, false);
})(window);

function ctlInit() {
   var CTL = new Controls();
   CTL.init();
}

function Controls() // constructor
{
}

Controls.prototype.init = function () // scan DOM
{
   // scan DOM for controls
   this.ctrlVSlider = document.getElementsByName("ctrlVSlider");
   this.ctrlHSlider = document.getElementsByName("ctrlHSlider");

   // ctrlVSlider
   for (var i = 0; i < this.ctrlVSlider.length; i++) {
      var cvs = document.createElement("canvas");
      var sl = this.ctrlVSlider[i];
      cvs.width = sl.clientWidth;
      cvs.height = sl.clientHeight;
      sl.appendChild(cvs);
      sl.canvas = cvs;

      sl.position = 0.5; // slider position 0...1
      sl.addEventListener("click", this.ctrlVSliderHandler.bind(this));
      sl.addEventListener("contextmenu", this.ctrlVSliderHandler.bind(this));
      sl.addEventListener("mousedown", this.ctrlVSliderHandler.bind(this));
      sl.addEventListener("mousemove", this.ctrlVSliderHandler.bind(this));
      sl.addEventListener("mouseup", this.ctrlVSliderHandler.bind(this));
      sl.addEventListener("mouseleave", this.ctrlVSliderHandler.bind(this));
      sl.addEventListener("touchmove", this.ctrlVSliderHandler.bind(this));
      sl.draw = this.ctrlVSliderDraw;
      sl.draw(sl);
   }

   // ctrlHSlider
   for (i = 0; i < this.ctrlHSlider.length; i++) {
      cvs = document.createElement("canvas");
      sl = this.ctrlHSlider[i];
      cvs.width = sl.clientWidth;
      cvs.height = sl.clientHeight;
      sl.appendChild(cvs);
      sl.canvas = cvs;

      sl.position = 0.5; // slider position 0...1
      sl.addEventListener("click", this.ctrlHSliderHandler.bind(this));
      sl.addEventListener("contextmenu", this.ctrlHSliderHandler.bind(this));
      sl.addEventListener("mousedown", this.ctrlHSliderHandler.bind(this));
      sl.addEventListener("mousemove", this.ctrlHSliderHandler.bind(this));
      sl.addEventListener("mouseup", this.ctrlHSliderHandler.bind(this));
      sl.addEventListener("mouseleave", this.ctrlHSliderHandler.bind(this));
      sl.addEventListener("touchmove", this.ctrlHSliderHandler.bind(this));
      sl.draw = this.ctrlHSliderDraw;
      sl.draw(sl);
      sl.set = this.ctrlHSliderSet;
   }

};

Controls.prototype.ctrlVSliderDraw = function (b) {
   if (b === undefined)
      b = this;
   var w = b.canvas.width;
   var h = b.canvas.height;
   b.sliderOfs = 20;

   var ctx = b.canvas.getContext("2d");
   ctx.fillStyle = "#E0E0E0";
   ctx.fillRect(0, 0, b.canvas.width, b.canvas.height);

   var knob = b.sliderOfs + (1 - b.position) * (h - 2 * b.sliderOfs);

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

Controls.prototype.ctrlVSliderHandler = function (e) {
   e.preventDefault();
   var y = undefined;
   var b = e.currentTarget;

   if (b.canvas === undefined) // we can get events from parent node
      return;

   f = b.dataset.update;
   if (f.indexOf("("))
      f = f.substr(0, f.indexOf("("));

   if (e.type === "mousedown")
      b.dragging = true;

   if ((e.buttons === 1 && e.type === "mousemove") || e.type === "click")
      y = e.offsetY;
   if (e.type === "touchmove")
      y = e.changedTouches[e.changedTouches.length - 1].clientY - b.getBoundingClientRect().top;

   if (e.type === "contextmenu") {
      b.position = 0.5;
      this.ctrlVSliderDraw(b);
      var f = b.dataset.update;
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
         window[f](b.position, e.type === "click");
      } else if (e.type === "mouseleave" && b.dragging) {
         window[f](b.position, true);
      }
   }

   if (e.type === "mouseup" || e.type === "click" || e.type === "mouseleave")
      b.dragging = false;
};

Controls.prototype.ctrlHSliderDraw = function (b) {
   if (b === undefined)
      b = this;
   var w = b.canvas.width;
   var h = b.canvas.height;
   b.sliderOfs = 20;

   var ctx = b.canvas.getContext("2d");
   ctx.fillStyle = "#E0E0E0";
   ctx.fillRect(0, 0, b.canvas.width, b.canvas.height);

   var knob = b.sliderOfs + (b.position) * (w - 2 * b.sliderOfs);

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
   this.position = pos;
   this.draw();
};

Controls.prototype.ctrlHSliderHandler = function (e) {
   e.preventDefault();
   var x = undefined;
   var b = e.currentTarget;

   if (b.canvas === undefined) // we can get events from parent node
      return;

   f = b.dataset.update;
   if (f.indexOf("("))
      f = f.substr(0, f.indexOf("("));

   if (e.type === "mousedown")
      b.dragging = true;

   if ((e.buttons === 1 && e.type === "mousemove") || e.type === "click")
      x = e.offsetX;
   if (e.type === "touchmove")
      x = e.changedTouches[e.changedTouches.length - 1].clientX - b.getBoundingClientRect().left;

   if (e.type === "contextmenu") {
      b.position = 0.5;
      this.ctrlHSliderDraw(b);
      var f = b.dataset.update;
      if (f.indexOf("("))
         f = f.substr(0, f.indexOf("("));
      window[f](b.position);
   } else {
      if (x !== undefined) {
         b.position = (x - b.sliderOfs) / (b.clientWidth - 2 * b.sliderOfs);
         if (b.position < 0)
            b.position = 0;
         if (b.position > 1)
            b.position = 1;
         this.ctrlHSliderDraw(b);
         window[f](b.position, e.type === "click");
      } else if (e.type === "mouseleave" && b.dragging) {
         window[f](b.position, true);
      }
   }

   if (e.type === "mouseup" || e.type === "click" || e.type === "mouseleave")
      b.dragging = false;
};

//-------------------------------------------------------------------------------------------------

function dlgShow(dlg, modal) {
   var d;
   if (typeof dlg === "string")
      d = document.getElementById(dlg);
   else
      d = dlg;

   d.dlgAx = 0;
   d.dlgAy = 0;
   d.dlgDx = 0;
   d.dlgDy = 0;
   d.modal = (modal !== undefined);

   d.style.display = "block";
   d.style.left = Math.round(document.documentElement.clientWidth / 2 - d.offsetWidth / 2) + "px";
   if (document.documentElement.clientHeight / 2 - d.offsetHeight / 2 < 0)
      d.style.top = "0px";
   else
      d.style.top = Math.round(document.documentElement.clientHeight / 2 - d.offsetHeight / 2) + "px";

   if (d.modal) {
      var b = document.getElementById("dlgBlackout");
      if (b === null) {
         b =  document.createElement("div");
         b.id = "dlgBlackout";
         b.className = "dlgBlackout";
         document.body.appendChild(b);
      }
      
      b.style.display = "block";
      d.style.zIndex = 21; // on top of dlgBlackout (20)
   }
   
   d.dlgMouseDown = function (e) {
      if ((e.target === this || e.target.parentNode === this) &&
         e.target.className === "dlgTitlebar") {
         e.preventDefault();
         this.Ax = e.clientX;
         this.Ay = e.clientY;
         this.Dx = parseInt(this.style.left);
         this.Dy = parseInt(this.style.top);
      }

      if (d.modal) {
         // catch all mouse events
         e.preventDefault();
      } else {
         var p = e.target;
         while (p !== undefined && p !== this && p !== document.body)
            p = p.parentElement;
         
         if (p === this) {
            var dlgs = document.getElementsByClassName("dlgFrame");
            for (var i=0 ; i<dlgs.length ; i++)
               dlgs[i].style.zIndex = 10;
            d.style.zIndex = 11;
         }
      }
   };

   d.dlgMouseMove = function (e) {
      if (this.Ax > 0 && this.Ay > 0) {
         e.preventDefault();
         var x = e.clientX;
         var y = e.clientY;
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
      if (e.target === this || e.target.parentNode === this) {
         e.preventDefault();
         this.Ax = e.targetTouches[0].clientX;
         this.Ay = e.targetTouches[0].clientY;
         this.Dx = parseInt(this.style.left);
         this.Dy = parseInt(this.style.top);
      }
   };

   d.dlgTouchMove = function (e) {
      var x = e.changedTouches[e.changedTouches.length - 1].clientX;
      var y = e.changedTouches[e.changedTouches.length - 1].clientY;
      this.style.left = (this.Dx + (x - this.Ax)) + "px";
      this.style.top = (this.Dy + (y - this.Ay)) + "px";
   };

   window.addEventListener("mousedown", d.dlgMouseDown.bind(d), true);
   window.addEventListener("mousemove", d.dlgMouseMove.bind(d), true);
   window.addEventListener("mouseup", d.dlgMouseUp.bind(d), true);
   window.addEventListener("touchstart", d.dlgTouchStart.bind(d), true);
   window.addEventListener("touchmove", d.dlgTouchMove.bind(d), true);
}

function dlgHide(dlg) {
   var d = document.getElementById("dlgBlackout");
   if (d !== null)
      d.style.display = "none";
   d = document.getElementById(dlg);
   d.style.display = "none";
}

function dlgMessageDestroy(b)
{
   var dlg = b.parentElement.parentElement;
   if (dlg.modal) {
      var d = document.getElementById("dlgBlackout");
      if (d !== null)
         d.style.display = "none";
   }
   document.body.removeChild(dlg);
}

function dlgMessage(title, string, modal, error)
{
   d =  document.createElement("div");
   d.className = "dlgFrame";
   d.style.zIndex = modal? 21 : 20;

   d.innerHTML = "<div class=\"dlgTitlebar\" id=\"dlgMessageTitle\">"+title+"</div>"+
   "<div class=\"dlgPanel\" style=\"padding: 30px;\">"+
   "<div id=\"dlgMessageString\">"+string+"</div>"+
   "<br /><br /><button class=\"dlgButton\" id=\"dlgMessageButton\" type=\"button\" "+
   " onClick=\"dlgMessageDestroy(this)\">Close</button>"+
   "</div>";

   document.body.appendChild(d);

   if (error === true) {
      var t = document.getElementById("dlgMessageTitle");
      t.style.backgroundColor = "#9E2A2A";
      t.style.color = "white";
   }
         
   dlgShow(d, modal);
   return d;
}

// replacement for alert() function
function dlgAlert(s)
{
   dlgMessage('MIDAS message', s, true, false);
}

// replacement for confirm() function
function dlgConfirm(string, confirmCallback)
{
   d = document.createElement("div");
   d.className = "dlgFrame";
   d.style.zIndex = 21;
   d.callback = confirmCallback;

   d.innerHTML = "<div class=\"dlgTitlebar\" id=\"dlgMessageTitle\">Please confirm</div>"+
      "<div class=\"dlgPanel\" style=\"padding: 30px;\">"+
      "<div id=\"dlgMessageString\">"+string+"</div>"+
      "<br /><br />" +
      "<button class=\"dlgButton\" id=\"dlgMessageButton\" type=\"button\" "+
      " onClick=\"this.parentElement.parentElement.callback(false);dlgMessageDestroy(this);\">Cancel</button>"+
      "<button class=\"dlgButton\" id=\"dlgMessageButton\" type=\"button\" "+
      " onClick=\"this.parentElement.parentElement.callback(true);dlgMessageDestroy(this);\">OK</button>"+
      "</div>";

   document.body.appendChild(d);

   dlgShow(d, true);
   return d;
}
