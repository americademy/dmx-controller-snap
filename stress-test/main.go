// concurrent.go
package main

import (
	"fmt"
	"net/http"
	"time"
	"io/ioutil"
	"strings"
)

func MakeRequest(ch chan<-string) {
	start := time.Now()
	reqBody := strings.NewReader("{\"3\":176,\"5\":214,\"7\":0,\"8\":255,\"9\":0,\"10\":28,\"11\":163,\"12\":33,\"13\":28,\"14\":163,\"15\":33}")
	req, _ := http.NewRequest("GET", "http://localhost:8084/", reqBody)

	client := &http.Client{}
	resp, _ := client.Do(req)

  secs := time.Since(start).Seconds()
  body, _ := ioutil.ReadAll(resp.Body)
  ch <- fmt.Sprintf("%.2f elapsed with response length: %d %s", secs, len(body), body)
}

func main() {
  start := time.Now()
  ch := make(chan string)

  for i := 1; i<=1000; i++ {
		go MakeRequest(ch)
  }

  for i := 1; i<=1000; i++ {
    fmt.Println(<-ch)
	}

  fmt.Printf("%.2fs elapsed\n", time.Since(start).Seconds())
}
