package main

import (
  "net/http"
)

func set(w http.ResponseWriter, r *http.Request) {
  channel := r.URL.Query().Get("channel")
  value := r.URL.Query().Get("value")

  w.Write([]byte("channel:" + channel + " value:" + value))
}

func main() {
  http.HandleFunc("/set", set)

  if err := http.ListenAndServe(":8081", nil); err != nil {
    panic(err)
  }
}
