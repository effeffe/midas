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
   // go through all name="mhistory" tags
   let mhist = document.getElementsByName("mjshistory");
   for (let i = 0; i < mhist.length; i++) {
      mhist[i].mhg = new MhistoryGraph(mhist[i]);
      mhist[i].resize = function() { this.mhg.resize(); };
   }
}

function MhistoryGraph(divElement) { // Constructor
   // create canvas inside the div
   this.parentDiv = divElement;
   this.canvas = document.createElement("canvas");
   this.canvas.style.border = "1ps solid black";
   divElement.appendChild(this.canvas);

   // mouse event handlers
   window.addEventListener("mousedown", this.mouseEvent.bind(this), true);
   window.addEventListener("mousemove", this.mouseEvent.bind(this), true);
   window.addEventListener("mouseup", this.mouseEvent.bind(this), true);
}

MhistoryGraph.prototype.mouseEvent = function(e) {
   // fix buttons for IE
   if (!e.which && e.button) {
      if ((e.button & 1) > 0) e.which = 1;      // Left
      else if ((e.button & 4) > 0) e.which = 2; // Middle
      else if ((e.button & 2) > 0) e.which = 3; // Right
   }
};

MhistoryGraph.prototype.resize = function() {
   this.canvas.width = this.parentDiv.clientWidth;
   this.canvas.height = this.parentDiv.clientHeight;

   this.x1 = 0;
   this.y1 = 0;
   this.x2 = this.parentDiv.clientWidth;
   this.y2 = this.parentDiv.clientHeight;

   this.redraw();
};

MhistoryGraph.prototype.redraw = function() {
   let f = this.draw.bind(this);
   window.requestAnimationFrame(f);
};

MhistoryGraph.prototype.draw = function() {
   let ctx = this.canvas.getContext("2d");

   ctx.translate(0.5, 0.5);
   ctx.strokeStyle = "#00000";
   ctx.fillStyle = "#000000";
   ctx.lineWidth = 1;
   ctx.font = "12px sans-serif";

   ctx.drawLine(50, 10, this.x2-20, 10);
   ctx.drawLine(this.x2-20, 10, this.x2-20, this.y2-25);

   this.drawVAxis(ctx, 50, this.y2-25, this.y2-35, -4, -8, -10, -12, 0, 0, 1000, 0);
   this.drawHAxis(ctx, 50, this.y2-25, this.x2-70, 4, 8, 10, 12, 0, -10, 10, 0);
};

MhistoryGraph.prototype.drawHAxis = function haxisDraw(ctx, x1, y1, width, minor, major, text, label, grid, xmin, xmax, logaxis) {
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

      /* rounding up of dx, label_dx */
      dx = Math.pow(10, int_dx) * base[tick_base];
      major_dx = Math.pow(10, int_dx) * base[major_base];
      label_dx = major_dx;

      do {
         // number of significant digits
         if (xmin === 0)
            n_sig1 = 0;
         else
            n_sig1 = Math.floor(Math.log(Math.abs(xmin)) / Math.log(10)) - Math.floor(Math.log(Math.abs(label_dx)) / Math.log(10)) + 1;

         if (xmax === 0)
            n_sig2 = 0;
         else
            n_sig2 = Math.floor(Math.log(Math.abs(xmax)) / Math.log(10)) - Math.floor(Math.log(Math.abs(label_dx)) / Math.log(10)) + 1;

         n_sig1 = Math.max(n_sig1, n_sig2);

         // toPrecision displays 1050 with 3 digits as 1.05e+3, so increase presicion to number of digits
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

   ctx.drawLine(x1, y1, x1 + width, y1);

   do {
      if (logaxis)
         x_screen = (Math.log(x_act) - Math.log(xmin)) / (Math.log(xmax) - Math.log(xmin)) * width + x1;
      else
         x_screen = (x_act - xmin) / (xmax - xmin) * width + x1;
      xs = Math.floor(x_screen + 0.5);

      if (x_screen > x1 + width + 0.001)
         break;

      if (x_screen >= x1) {
         if (Math.abs(Math.floor(x_act / major_dx + 0.5) - x_act / major_dx) <
            dx / major_dx / 10.0) {

            if (Math.abs(Math.floor(x_act / label_dx + 0.5) - x_act / label_dx) <
               dx / label_dx / 10.0) {
               // label tick mark
               ctx.drawLine(xs, y1, xs, y1 + text);

               // grid line
               if (grid !== 0 && xs > x1 && xs < x1 + width)
                  ctx.drawLine(xs, y1, xs, y1 + grid);

               // label
               if (label !== 0) {
                  let str = x_act.toPrecision(n_sig1).stripZeros();
                  let ext = ctx.measureText(str);
                  if (xs - ext.width / 2 > x1 &&
                     xs + ext.width / 2 < x1 + width)
                     ctx.fillText(str, xs, y1 + label);
                  last_label_x = xs + ext.width/2;
               }
            } else {
               // major tick mark
               ctx.drawLine(xs, y1, xs, y1 + major);

               // grid line
               if (grid !== 0 && xs > x1 && xs < x1 + width)
                  ctx.drawLine(xs, y1 - 1, xs, y1 + grid);
            }

            if (logaxis) {
               dx *= 10;
               major_dx *= 10;
               label_dx *= 10;
            }
         } else
            // minor tick mark
            ctx.drawLine(xs, y1, xs, y1 + minor);

         // for logaxis, also put labes on minor tick marks
         if (logaxis) {
            if (label !== 0) {
               let str = x_act.toPrecision(n_sig1).stripZeros();
               let ext = ctx.measureText(str);
               ctx.save();
               ctx.fillStyle = "black";
               if (xs - ext.width / 2 > x1 &&
                  xs + ext.width / 2 < x1 + width &&
                  xs - ext.width / 2 > last_label_x + 2)
                  ctx.fillText(str, xs, y1 + label);
               ctx.restore();

               last_label_x = xs + ext.width/2;
            }
         }
      }

      x_act += dx;

      /* supress 1.23E-17 ... */
      if (Math.abs(x_act) < dx / 100)
         x_act = 0;

   } while (1);
};

MhistoryGraph.prototype.drawVAxis = function(ctx, x1, y1, height, minor, major, text, label, grid, ymin, ymax, logaxis) {
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
         n_sig1 = 0;
      else
         n_sig1 = Math.floor(Math.log(Math.abs(ymin)) / Math.log(10)) - Math.floor(Math.log(Math.abs(label_dy)) / Math.log(10)) + 1;

      if (ymax === 0)
         n_sig2 = 0;
      else
         n_sig2 = Math.floor(Math.log(Math.abs(ymax)) / Math.log(10)) - Math.floor(Math.log(Math.abs(label_dy)) / Math.log(10)) + 1;

      n_sig1 = Math.max(n_sig1, n_sig2);

      // toPrecision displays 1050 with 3 digits as 1.05e+3, so increase presicion to number of digits
      if (Math.abs(ymin) < 100000)
         n_sig1 = Math.max(n_sig1, Math.floor(Math.log(Math.abs(ymin)) / Math.log(10) + 0.001) + 1);
      if (Math.abs(ymax) < 100000)
         n_sig1 = Math.max(n_sig1, Math.floor(Math.log(Math.abs(ymax)) / Math.log(10) + 0.001) + 1);

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

   ctx.drawLine(x1, y1, x1, y1 - height);

   do {
      if (logaxis)
         y_screen = y1 - (Math.log(y_act) - Math.log(ymin)) / (Math.log(ymax) - Math.log(ymin)) * height;
      else
         y_screen = y1 - (y_act - ymin) / (ymax - ymin) * height;
      ys = Math.floor(y_screen + 0.5);

      if (y_screen < y1 - height - 0.001)
         break;

      if (y_screen <= y1 + 0.001) {
         if (Math.abs(Math.floor(y_act / major_dy + 0.5) - y_act / major_dy) <
            dy / major_dy / 10.0) {

            if (Math.abs(Math.floor(y_act / label_dy + 0.5) - y_act / label_dy) <
               dy / label_dy / 10.0) {
               // label tick mark
               ctx.drawLine(x1, ys, x1 + text, ys);

               // grid line
               if (grid !== 0 && ys < y1 && ys > y1 - height)
                  ctx.drawLine(x1, ys, x1 + grid, ys);

               // label
               if (label !== 0) {
                  let str = y_act.toPrecision(n_sig1).stripZeros();
                  ctx.fillText(str, x1 + label, ys);
                  last_label_y = ys - textHeight / 2;
               }
            } else {
               // major tick mark
               ctx.drawLine(x1, ys, x1 + major, ys);

               // grid line
               if (grid !== 0 && ys < y1 && ys > y1 - height)
                  ctx.drawLine(x1, ys, x1 + grid, ys);
            }

            if (logaxis) {
               dy *= 10;
               major_dy *= 10;
               label_dy *= 10;
            }

         } else
         // minor tick mark
            ctx.drawLine(x1, ys, x1 + minor, ys);

         // for logaxis, also put labes on minor tick marks
         if (logaxis) {
            if (label !== 0) {
               let str = y_act.toPrecision(n_sig1).stripZeros();
               if (ys - textHeight / 2 > y1 - height &&
                  ys + textHeight / 2 < y1 &&
                  ys + textHeight < last_label_y + 2)
                  ctx.fillText(str, x1 + label, ys);

               last_label_y = ys;
            }
         }
      }

      y_act += dy;

      // supress 1.23E-17 ...
      if (Math.abs(y_act) < dy / 100)
         y_act = 0;

   } while (1);
};
