// build with :  gcc -o server_test server_test.c && ./server_test

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <errno.h>

#define NAME "/tmp/test_server.sock"
#define BUFFER_SIZE 1024

// to hold the messages we receive from the unix socket
char buf[BUFFER_SIZE];
// the socket connection
int sock;
// boolean enums (because they are pretty and nice to use)
typedef enum { false, true } bool;

/*
    Signal Handler for SIGINT, to close our
    connections and clean up before exiting
*/
void sigintHandler(int sig_num) {
    // close the socket connection
    close(sock);
    // delete the actual file used for the socket
    unlink(NAME);
    // print a nice friendly message
    printf("Goodbye\n");
    // exit cleanly
    exit(EXIT_SUCCESS);
}

void socketConnect() {
    struct sockaddr_un server;

    unlink(NAME);
    server.sun_family = AF_UNIX;
    strcpy(server.sun_path, NAME);

    // connect the socket
    sock = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock < 0) {
        perror("opening stream socket");
        exit(EXIT_FAILURE);
    }

    // bind the socket
    if (bind(sock, (struct sockaddr *) &server, sizeof(struct sockaddr_un))) {
        perror("binding stream socket");
        exit(EXIT_FAILURE);
    }

    if (listen(sock, 5) < 0)  {
        perror("listen");
        exit(EXIT_FAILURE);
    }

}


int main() {
    // the specific connection to the socket and the
    // number of bytes successfully read from it on each attempt
    int connection, bytes_read;

    // zero fill the buffer so we know when we reach the end of our data
    bzero(buf, BUFFER_SIZE);

    // Set the SIGINT (Ctrl-C) signal handler to sigintHandler
    signal(SIGINT, sigintHandler);

    socketConnect();

    // start accepting messages
    while(true) {

        // check for a connection on this socket
        connection = accept(sock, 0, 0);
        if (connection < 0) {
            usleep(100);
            continue;
        }

        // read from the socket and put the data into the buffer
        // -1 on error, 0 if no data, otherwise number of bytes received
        while(bytes_read = read(connection, buf, BUFFER_SIZE-1)) {

            // was data received from the socket
            if (bytes_read > 0) {
                printf("Received %s\n", buf);
            }
            // no data was available, and we received an unexpected response
            // (EAGAIN means no data was available at this time)
            else if (bytes_read == -1) {
                // if there was an error other than EAGAIN
                if (errno != EAGAIN) {
                    perror("socket read");
                    exit(EXIT_FAILURE);
                }
            }
            // bytes_read should be either -1 or greater than 0
            else {
                perror("bytes_read should be either -1 or greater than 0");
            }

            // zero fill the buffer again before we try and read into it
            bzero(buf, sizeof(buf));
        }

        close(connection);
    }

    return 0;

}

