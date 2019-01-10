CC=gcc
server: server.c
	$(CC) -Wall -pthread -I/build/codeverse-dmx-controller/parts/pigpio/install/usr/local/include -I/build/codeverse-dmx-controller/parts/pigpio/install/usr/local/lib -o server server.c -lrt -lpigpio

install:
	install -d -m755  $(DESTDIR)/bin/
	install -m755 server $(DESTDIR)/bin/dmx-server
