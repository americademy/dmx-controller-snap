CC=gcc
server: server.c
	$(CC) -Wall -o server server.c -lpigpio -lrt

install:
	install -d -m755  $(DESTDIR)/bin/
	install -m755 server $(DESTDIR)/bin/dmx-server
