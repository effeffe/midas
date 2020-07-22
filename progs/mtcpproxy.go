//
// mtcproxy - proxy TCP connections from one TCP port
// to a different TCP port possibly on a different host
//

package main

import (
	"flag"
	"fmt"
	"io"
	"net"
	"os"
	"sync"
)

var conf_verbose = flag.Bool("v", false, "report activity")
var conf_from = flag.String("from", ":443", "listen on this address")
var conf_to = flag.String("to", "localhost:8443", "forward to connections to this address")

var g_done map[net.Conn]bool
var g_done_mutex sync.Mutex

func copy(c, w, r *net.TCPConn) {
	written, e := io.Copy(w, r)
	if e != nil {
		fmt.Fprintf(os.Stderr, "Connection from \"%v\": io.Copy() error %v\n", c.RemoteAddr(), e)
	} else {
		if *conf_verbose {
			fmt.Fprintf(os.Stderr, "Connection from \"%v\": io.Copy() copied %v bytes\n", c.RemoteAddr(), written)
		}
	}

	r.CloseRead()
	w.CloseWrite()
	//w.Close()

	g_done_mutex.Lock()
	defer g_done_mutex.Unlock()

	if *conf_verbose {
		fmt.Fprintf(os.Stderr, "Connection from \"%v\": writer done: %v, reader done: %v\n", c.RemoteAddr(), g_done[w], g_done[r])
	}

	if g_done[w] {
		if *conf_verbose {
			fmt.Fprintf(os.Stderr, "Connection from \"%v\": writer close\n", c.RemoteAddr())
		}
		//fmt.Fprintf(os.Stderr, "close w = %v\n", w)
		w.Close()
		delete(g_done, w)
	} else {
		g_done[w] = true
	}

	if g_done[r] {
		if *conf_verbose {
			fmt.Fprintf(os.Stderr, "Connection from \"%v\": reader close\n", c.RemoteAddr())
		}
		//fmt.Fprintf(os.Stderr, "close r = %v\n", r)
		r.Close()
		delete(g_done, r)
	} else {
		g_done[r] = true
	}
}

func main() {
	flag.Parse()

	g_done = make(map[net.Conn]bool)

	if *conf_verbose {
		fmt.Fprintf(os.Stderr, "Listening on \"%v\", forwarding connections to \"%v\"\n", *conf_from, *conf_to)
	}

	addr, e := net.ResolveTCPAddr("tcp", *conf_from)
	if e != nil {
		fmt.Fprintf(os.Stderr, "net.ResolveTCPAddr() error %v\n", e)
		return
	}

	raddr, e := net.ResolveTCPAddr("tcp", *conf_to)
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

		if *conf_verbose {
			fmt.Fprintf(os.Stderr, "Connection from \"%v\"\n", l.RemoteAddr())
		}

		r, e := net.DialTCP("tcp", nil, raddr)
		if e != nil {
			fmt.Fprintf(os.Stderr, "Connection from \"%v\": net.Dial() error %v\n", l.RemoteAddr(), e)
			l.Close()
			continue
		}
		go copy(l, l, r)
		go copy(l, r, l)
	}
}

// end
