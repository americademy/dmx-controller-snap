CC=gcc
server: server.c
	$(CC) -Wall -pthread -g -rdynamic -o server server.c -lrt

install:
	install -d -m755  $(DESTDIR)/bin/
	install -m755 server $(DESTDIR)/bin/dmx-server
