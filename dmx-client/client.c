// use ./client 2 250

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <string.h>

#define NAME "/tmp/dmx.sock"

int main(int argc, char *argv[], char * envp[]) {

    int channel = strtol(argv[1], NULL, 10);
    int value = strtol(argv[2], NULL, 10);

    printf("Setting channel: %d to %d", channel, value);

    int sock;
    struct sockaddr_un server;
    sock = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);

    if (sock < 0) {
        perror("opening stream socket");
        exit(1);
    }

    server.sun_family = AF_UNIX;
    strcpy(server.sun_path, NAME);

    if (connect(sock, (struct sockaddr *) &server, sizeof(struct sockaddr_un)) < 0) {
        close(sock);
        perror("connecting stream socket");
        exit(1);

    } else {

        char data[80];
        sprintf(data, "%d:%d", channel, value);

        if (write(sock, data, sizeof(data)) < 0) {
            perror("writing on stream socket");
        }
        close(sock);
    }
}
