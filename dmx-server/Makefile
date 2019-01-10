CC=gcc
server: server.c
	$(CC) -Wall -o server server.c -I/build/codeverse-dmx-controller/parts/pigpio/install/usr/local -I/build/codeverse-dmx-controller/parts/pigpio/install/usr/local/include -lpigpio -lrt

install:
	install -d -m755  $(DESTDIR)/bin/
	install -m755 server $(DESTDIR)/bin/dmx-server
