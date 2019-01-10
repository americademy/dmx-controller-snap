CC=gcc
server: server.c
	$(CC) -Wall -pthread -o server server.c -lrt -lpigpio

install:
	install -d -m755  $(DESTDIR)/bin/
	install -m755 server $(DESTDIR)/bin/dmx-server
