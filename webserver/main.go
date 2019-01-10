package main

import (
  "encoding/json"
  "fmt"
  "net/http"
  "net"
  "log"
  "bytes"
)

var sock_err error;
var c net.Conn;

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

func setChannelValues(w http.ResponseWriter, r *http.Request) {

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

  c, sock_err = net.Dial("unix", "/tmp/dmx.sock")

  if sock_err != nil {
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
  println("Preparing Server")
  http.HandleFunc("/", setChannelValues)

  println("Starting Server")
  if http_err := http.ListenAndServe(":8084", nil); http_err != nil {
    panic(http_err)
  }

}

func enableCors(w *http.ResponseWriter) {
  (*w).Header().Set("Access-Control-Allow-Origin", "*")
}

