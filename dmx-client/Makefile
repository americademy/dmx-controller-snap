CC=gcc
client: client.c
	$(CC) -Wall -o client client.c

install:
	install -d -m755  $(DESTDIR)/bin/
	install -m755 client $(DESTDIR)/bin/dmx-client
