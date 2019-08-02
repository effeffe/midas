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
      mhist[i].resize = function () {
         this.mhg.resize();
      };
   }
}

function MhistoryGraph(divElement) { // Constructor
   // create canvas inside the div
   this.parentDiv = divElement;
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
         "#B0B040", "#B0B0FF", "#FFA0A0", "#A0FFA0",
         "#FF9000", "#00AAFF", "#FF00A0", "#00C030",
         "#D0A060", "#A0C0D0", "#C04010", "#807060",
         "#F0C000", "#2090A0", "#D040D0", "#90B000"],
      idata: [
         "#FFFF00", "#B0B0FF", "#FFA0A0", "#A0FFA0",
         "#FF9000", "#00AAFF", "#FF00A0", "#00C030",
         "#D0A060", "#A0C0D0", "#C04010", "#807060",
         "#F0C000", "#2090A0", "#D040D0", "#90B000"]
   };

   // scales
   this.tMax = new Date() / 1000;
   this.tMin = this.tMax - 3600;
   this.yMin = 0;
   this.yMax = 1;

   // graph arrays (in screen pixels)
   this.x = [];
   this.y = [];

   // mouse event handlers
   divElement.addEventListener("mousedown", this.mouseEvent.bind(this), true);
   divElement.addEventListener("mousemove", this.mouseEvent.bind(this), true);
   divElement.addEventListener("mouseup", this.mouseEvent.bind(this), true);

   divElement.addEventListener("wheel", this.mouseWheelEvent.bind(this), true);

   this.loadData();
}

MhistoryGraph.prototype.mouseEvent = function (e) {
   // fix buttons for IE
   if (!e.which && e.button) {
      if ((e.button & 1) > 0) e.which = 1;      // Left
      else if ((e.button & 4) > 0) e.which = 2; // Middle
      else if ((e.button & 2) > 0) e.which = 3; // Right
   }
};

MhistoryGraph.prototype.mouseWheelEvent = function (e) {

   let f = (e.offsetX - this.x1) / (this.x2 - this.x1);
   let dtMin = f * (this.tMax - this.tMin) / 100 * e.deltaY;
   let dtMax = (1 - f) * (this.tMax - this.tMin) / 100 * e.deltaY;
   this.tMin -= dtMin;
   this.tMax += dtMax;

   let now = new Date() / 1000;
   if (this.tMax > now)
      this.tMax = now;

   this.redraw();
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

MhistoryGraph.prototype.loadData = function () {
   let t = Date.now() / 1000;
   mjsonrpc_call("hs_read",
      {
         "start_time": t - 24 * 3600,
         "end_time": t,
         "events": ["System", "System"],
         "tags": ["Trigger per sec.", "Trigger kB per sec."],
         "index": [0, 0]
      })
      .then(function (rpc) {
         if (this.data === undefined) {
            this.yMin = 0;
            this.yMax = 0;

            rpc.result.data.forEach(d => {
               this.yMin = Math.min(this.yMin, ...d.value);
               this.yMax = Math.max(this.yMax, ...d.value);
            });
            if (this.yMin === this.yMax) {
               this.yMin -= 0.5;
               this.yMax += 0.5;
            } else {
               this.yMax += (this.yMax - this.yMin) * 0.05;
            }
         }
         this.data = rpc.result.data;
         this.redraw();
      }.bind(this))
      .catch(function (error) {
         mjsonrpc_error_alert(error);
      })
};

MhistoryGraph.prototype.draw = function () {
   let ctx = this.canvas.getContext("2d");

   if (this.data === undefined) {
      ctx.translate(0, 0);
      ctx.lineWidth = 1;
      ctx.font = "14px sans-serif";
      ctx.strokeStyle = "#808080";
      ctx.fillStyle = "#808080";
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText("Data being loaded ...", this.width / 2, this.height / 2);
      return;
   }

   //ctx.translate(0.5, 0.5);
   ctx.lineWidth = 1;
   ctx.font = "14px sans-serif";

   let maxwidth = this.drawVAxis(ctx, 50, this.height - 25, this.height - 35,
      -4, -7, -10, -12, 0, this.yMin, this.yMax, 0, false);

   this.x1 = maxwidth + 15;
   this.y1 = this.height - 25;
   this.x2 = this.width - 10;
   this.y2 = 10;

   ctx.fillStyle = this.color.background;
   ctx.fillRect(0, 0, this.width, this.height);

   ctx.strokeStyle = this.color.axis;
   ctx.drawLine(this.x1, this.y2, this.x2, this.y2);
   ctx.drawLine(this.x2, this.y2, this.x2, this.y1);

   this.drawVAxis(ctx, this.x1, this.y1, this.y1 - this.y2,
      -4, -7, -10, -12, this.x2 - this.x1, this.yMin, this.yMax, 0, true);
   //this.drawHAxis(ctx, 50, this.y2-25, this.x2-70, 4, 7, 10, 12, 0, -10, 10, 0);
   this.drawTAxis(ctx, this.x1, this.y1, this.x2 - this.x1, this.width,
      4, 7, 10, 10, this.y2 - this.y1, this.tMin, this.tMax);


   ctx.save();
   ctx.beginPath();
   ctx.rect(this.x1, this.y2, this.x2 - this.x1, this.y1 - this.y2);
   ctx.clip();

   for (let di = 0; di < this.data.length; di++) {
      if (di >= this.color.data.length) {
         ctx.strokeStyle = "#000000";
         ctx.fillStyle = "#F0F0F0";
      } else {
         ctx.strokeStyle = this.color.data[di];
         ctx.fillStyle = this.color.data[di];
      }

      this.x[di] = [];
      this.y[di] = [];

      for (let i = 0; i < this.data[di].count; i++) {
         this.x[di][i] = (this.data[di].time[i] - this.tMin) /
            (this.tMax - this.tMin) * (this.x2 - this.x1) + this.x1;
         this.y[di][i] = this.y1 - (this.data[di].value[i] - this.yMin) /
            (this.yMax - this.yMin) * (this.y1 - this.y2);
      }

      ctx.beginPath();
      let x0;
      let y0;
      let xLast;
      for (let i = 0; i < this.data[di].count; i++) {
         if (i === this.data[di].count - 1 || this.x[di][i + 1] >= this.x1) {
            if (x0 === undefined) {
               x0 = this.x[di][i];
               y0 = this.y[di][i];
               ctx.moveTo(this.x[di][i], this.y[di][i]);
            } else
               ctx.lineTo(this.x[di][i], this.y[di][i]);
            xLast = this.x[di][i];
            if (this.x[di][i] > this.x2)
               break;
         }
      }
      ctx.lineTo(xLast, this.y1);
      ctx.lineTo(x0, this.y1);
      ctx.lineTo(x0, y0);
      ctx.save();
      ctx.globalAlpha = 0.3;
      ctx.fill();
      ctx.restore();

      ctx.beginPath();
      x0 = undefined;
      for (let i = 0; i < this.data[di].count; i++) {
         if (i === this.data[di].count - 1 || this.x[di][i + 1] >= this.x1) {
            if (x0 === undefined) {
               x0 = this.x[di][i];
               y0 = this.y[di][i];
               ctx.moveTo(this.x[di][i], this.y[di][i]);
            } else
               ctx.lineTo(this.x[di][i], this.y[di][i]);
            if (this.x[di][i] > this.x2)
               break;
         }
      }
      ctx.stroke();
   }

   ctx.restore(); // remove clipping
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
         n_sig1 = 0;
      else
         n_sig1 = Math.floor(Math.log(Math.abs(ymin)) / Math.log(10)) -
            Math.floor(Math.log(Math.abs(label_dy)) / Math.log(10)) + 1;

      if (ymax === 0)
         n_sig2 = 0;
      else
         n_sig2 = Math.floor(Math.log(Math.abs(ymax)) / Math.log(10)) -
            Math.floor(Math.log(Math.abs(label_dy)) / Math.log(10)) + 1;

      n_sig1 = Math.max(n_sig1, n_sig2);

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
      if (base < 600)
         options = {
            day: '2-digit', month: 'short', year: '2-digit',
            hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit'
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

   if (base < 600) {
      options = {hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit'};
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

