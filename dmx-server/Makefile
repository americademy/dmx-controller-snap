CC=gcc
server: server.c
	$(CC) -Wall -o server server.c -lpigpio -lrt -I/build/codeverse-dmx-controller/parts/pigpio/install/usr/local/include

install:
	install -d -m755  $(DESTDIR)/bin/
	install -m755 server $(DESTDIR)/bin/dmx-server
