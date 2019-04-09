package main

import (
  "os"
  "encoding/json"
  "fmt"
  "net/http"
  "net"
  "log"
  "bytes"
  "time"
)

type TemporaryError interface {
  Temporary() bool
}

var sock_err error;
var c net.Conn;

func maxClients(h http.Handler, n int) http.Handler {
  sema := make(chan struct{}, n)

  return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
    sema <- struct{}{}
    defer func() { <-sema }()

    h.ServeHTTP(w, r)
  })
}

// convert the JSON format into a string such as "2:25,3:100" (where this was for channel 2 with value 25 and channel 3 with value 100)
func createKeyValuePairs(m map[string]int) string {
    b := new(bytes.Buffer)
    var first = true;
    for key, value := range m {
      if first {
        first = false
        fmt.Fprintf(b, "%s:%d", key, value)
      } else {
        fmt.Fprintf(b, ",%s:%d", key, value)
      }
    }
    return b.String()
}

func getStatus(w http.ResponseWriter, r *http.Request) {
  time.Sleep(2*time.Second)
  w.Write([]byte("OK"))
}

func setChannelValues(w http.ResponseWriter, r *http.Request) {
  socket_file := ""
  if (len(os.Getenv("SNAP_DATA")) > 0) {
    socket_file = os.Getenv("SNAP_DATA") + "/dmx-server.sock"
  } else {
    socket_file = "/tmp/dmx-server.sock"
  }
  buf := new(bytes.Buffer)

  buf.ReadFrom(r.Body)
  newStr := buf.String()

  if r.Body == nil {
    http.Error(w, "Please send a request body", 400)
    return
  }

  m := map[string]int{}
  err := json.Unmarshal([]byte(newStr), &m)

  if err != nil {
    http.Error(w, err.Error(), 400)
    return
  }

  // simple command we're going to send to the dmx daemon
  msg := createKeyValuePairs(m)

  fmt.Println(msg)

  connectToSocket:
  c, sock_err = net.Dial("unix", socket_file)
  // if this is a temporary error, then retry
  if terr, ok := sock_err.(TemporaryError); ok && terr.Temporary() {
    time.Sleep(20*time.Millisecond)
    goto connectToSocket
  } else if (sock_err != nil) {
    log.Fatal("Dial error", sock_err)
  }

  defer c.Close()

  // send this command via the unix domain socket
  _, message_err := c.Write([]byte(msg))

  if message_err != nil {
    log.Fatal("Write error:", message_err)
  }

  // reply to the web request
  enableCors(&w)
  w.Write([]byte("OK (" + msg + ")"))
}

func main() {

  // open socket to dmx controller
  println("Connecting to DMX daemon")

  // start web server
  println("Starting Server")

  channelValues := http.HandlerFunc(setChannelValues)
  http.Handle("/", maxClients(channelValues, 1))

  statusHandler := http.HandlerFunc(getStatus)
  http.Handle("/status", maxClients(statusHandler, 5))

  println("Ready")

  if http_err := http.ListenAndServe(":8084", nil); http_err != nil {
    panic(http_err)
  }

}

func enableCors(w *http.ResponseWriter) {
  (*w).Header().Set("Access-Control-Allow-Origin", "*")
}

