//
// mtcproxy - proxy TCP connections from one TCP port
// to a different TCP port possibly on a different host
//

package main

import (
    "io"
    "net"
    "fmt"
    "os"
)

var done map[net.Conn]bool

func copy(w, r *net.TCPConn) {
   _, e := io.Copy(w, r)
   if e != nil {
     fmt.Fprintf(os.Stderr, "io.Copy() error %v\n", e)
   } else {
     //fmt.Fprintf(os.Stderr, "io.Copy() done\n")
   }

   r.CloseRead()
   w.CloseWrite()
   //w.Close()

   //fmt.Fprintf(os.Stderr, "done %v %v\n", done[w], done[r])
   if (done[w]) {
     //fmt.Fprintf(os.Stderr, "close w = %v\n", w)
      w.Close()
      delete(done, w)
   } else {
      done[w] = true
   }

   if (done[r]) {
      //fmt.Fprintf(os.Stderr, "close r = %v\n", r)
      r.Close()
      delete(done, r)
   } else {
      done[r] = true
   }
}

func main() {
    done = make(map[net.Conn]bool)

    addr, e := net.ResolveTCPAddr("tcp", ":443")
    if e != nil {
       fmt.Fprintf(os.Stderr, "net.ResolveTCPAddr() error %v\n", e)
       return
    }

    raddr, e := net.ResolveTCPAddr("tcp", "localhost:8443")
    if e != nil {
       fmt.Fprintf(os.Stderr, "net.ResolveTCPAddr() error %v\n", e)
       return
    }

    s, e := net.ListenTCP("tcp", addr)
    if e != nil {
       fmt.Fprintf(os.Stderr, "net.ListenTCP() error %v\n", e)
       return
    }
    for {
        l, e := s.AcceptTCP()
        if e != nil {
    	   fmt.Fprintf(os.Stderr, "net.Accept() error %v\n", e)
       	   return
    	}
        r, e := net.DialTCP("tcp", nil, raddr)
        if e != nil {
    	   fmt.Fprintf(os.Stderr, "net.Dial() error %v\n", e)
	   continue
    	}
        go copy(l, r)
        go copy(r, l)
    }
}

// end
