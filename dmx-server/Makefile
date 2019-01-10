CC=gcc
server: server.c
	$(CC) -Wall -I/build/codeverse-dmx-controller/parts/pigpio/install/usr/local/include -o server server.c -lrt

install:
	install -d -m755  $(DESTDIR)/bin/
	install -m755 server $(DESTDIR)/bin/dmx-server
