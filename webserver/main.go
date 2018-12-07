package main

import (
  "net/http"
  "net"
  "log"
)

var sock_err error;
var c net.Conn;

func set(w http.ResponseWriter, r *http.Request) {
  channel := r.URL.Query().Get("channel")
  value := r.URL.Query().Get("value")

  // simple command we're going to send to the dmx daemon
  msg := channel + ":" + value

  // send this command via the unix domain socket
  _, message_err := c.Write([]byte(msg))

  if message_err != nil {
    log.Fatal("Write error:", message_err)
  }

  // reply to the web request
  w.Write([]byte("OK (" + channel + ":" + value + ")"))
}

func main() {

  // open socket to dmx controller
  println("Connecting to DMX daemon")
  c, sock_err = net.Dial("unix", "/tmp/test_server.sock")

  if sock_err != nil {
    log.Fatal("Dial error", sock_err)
  }

  defer c.Close()


  // start web server
  println("Preparing Server")
  http.HandleFunc("/set", set)

  println("Starting Server")
  if http_err := http.ListenAndServe(":8081", nil); http_err != nil {
    panic(http_err)
  }

}
