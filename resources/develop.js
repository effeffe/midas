/********************************************************************\
 
 Name:         mhttpd.js
 Created by:   Stefan Ritt
 
 Contents:     JavaScript midas library for developing mhttpd custom 
               pages
 
\********************************************************************/

document.onmousemove = getMouseXY;

function getMouseXY(e)
{
   try {
      var x = e.pageX;
      var y = e.pageY;
      var p = 'abs: ' + x + '/' + y;
      i = document.getElementById('refimg');
      if (i == null)
         return false;
      document.body.style.cursor = 'crosshair';
      x -= i.offsetLeft;
      y -= i.offsetTop;
      while (i = i.offsetParent) {
         x -= i.offsetLeft;
         y -= i.offsetTop;
      }
      p += '   rel: ' + x + '/' + y;
      window.status = p;
      return true;
   }
   catch (e) {
      return false;
   }
}