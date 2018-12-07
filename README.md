# webserver
cd webserver
go build && ./webserver

# dmx server
cd service
gcc -Wall -pthread -o server server.c -lpigpiod_if2 -lrt
./server
