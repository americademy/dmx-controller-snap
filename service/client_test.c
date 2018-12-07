// build with :  gcc -o client_test client_test.c && ./client_test

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>

#define NAME "/tmp/test_server.sock"

int main() {

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

        char data[80] = "0:1,1:1,2:1,3:1";

        if (write(sock, data, sizeof(data)) < 0) {
            perror("writing on stream socket");
        }

        printf("Sending data\n");
        close(sock);
    }
}
