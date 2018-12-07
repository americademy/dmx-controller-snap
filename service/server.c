// build with :  gcc -Wall -pthread -o server server.c -lpigpiod_if2 -lrt && ./server
// add disable_pvt=1 to /boot/config.txt

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <errno.h>
#include <pigpiod_if2.h>

#define NAME "/tmp/dmx.sock"
#define BUFFER_SIZE 1024

// set to 0, 1 or 2
#define VERBOSE 0
#define DEMO 0

// to hold the messages we receive from the unix socket
char buf[BUFFER_SIZE];
// the socket connection
int sock;
// boolean enums (because they are pretty and nice to use)
typedef enum { false, true } bool;

// PIN used for DMX data output
#define TX_PIN 5

#define DMX_CHANNELS 512

// Timings
#define BREAK_US 120
#define MAB_US 40
#define PREPACKET_IDLE_US 100
#define POSTPACKET_IDLE_US 100
#define SYMBOL_RATE 4 // 4us per bit
#define STOP_BITS 2

// used for more legible code below
#define CHANNEL_PART 1
#define VALUE_PART 2

// one value per channel
char dmx_values[DMX_CHANNELS];
// array of pulses large enough to hold the DMX512 packet in bit form
gpioPulse_t pulses[100];

// for tracking which pulse (as we build the wave of pulses)
int current_pulse = 0;
int total_pulse_duration = 0;

int running = 1;
int deamon_id;

int sock, msgsock, rval;
struct sockaddr_un server;
char buf[1024];

void cleanup() {
    running = 0;

    printf("cleaning up..\n");

    close(sock);
    close(msgsock);
    unlink(NAME);

    // pigpio_stop(deamon_id);
    printf("disconnected from pigpio..\n");

    printf("Goodbye\n");
    exit(EXIT_SUCCESS);
}

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

void gpioSetup() {
    int set_mode_result, write_result;
    int ch;

    // set all values to 0
    for (ch = 0; ch < DMX_CHANNELS; ch++) {
        dmx_values[ch] = 0;
    }

    // initialize
    deamon_id = pigpio_start("localhost", "8888");
    if (deamon_id < 0) {
        fprintf(stderr, "pigpio initialization failed (%i) have you ran sudo pigpiod\n", deamon_id);
        exit(EXIT_FAILURE);
    }

    // ensure the output pin is setup for output
    set_mode_result = set_mode(deamon_id, TX_PIN, PI_OUTPUT);
    if (set_mode_result < 0) {
        fprintf(stderr, "failed setting TX_PIN pin to output (error: %i)\n", deamon_id);
        exit(EXIT_FAILURE);
    }

    // put the pin in the correct default state
    write_result = gpio_write(deamon_id, TX_PIN, 1);
    if (write_result < 0) {
        fprintf(stderr, "failed writing 0 to TX_PIN pin (error: %i)\n", deamon_id);
        exit(EXIT_FAILURE);
    }

}

void output_low(int duration) {
    // create the desired pulse (note this is the inverse of the DMX standard because we are using a transistor to convert the logic level, this inverts the signal)
    pulses[current_pulse].gpioOn = 0;
    pulses[current_pulse].gpioOff = (1<<TX_PIN);
    pulses[current_pulse].usDelay = duration;
    // for tracking where we are in the pulse generation process
    current_pulse = current_pulse + 1;
    total_pulse_duration = total_pulse_duration + duration;
}

void output_high(int duration) {
    // create the desired pulse (note this is the inverse of the DMX standard because we are using a transistor to convert the logic level, this inverts the signal)
    pulses[current_pulse].gpioOn = (1<<TX_PIN);
    pulses[current_pulse].gpioOff = 0;
    pulses[current_pulse].usDelay = duration;
    // for tracking where we are in the pulse generation process
    current_pulse = current_pulse + 1;
    total_pulse_duration = total_pulse_duration + duration;
}

void send_packets() {
    int ch;

    if (VERBOSE) {
        printf("Sending data\n");
        if (VERBOSE == 2) {
            for (ch = 0; ch < DMX_CHANNELS; ch++) {
                printf(" channel %i = %i\n", ch, dmx_values[ch]);
            }
        }
    }

    // clears all waveforms and any data added by calls to the wave_add_* functions
    wave_clear(deamon_id);
    // starts a new empty waveform
    wave_add_new(deamon_id);

    current_pulse = 0;
    total_pulse_duration = 0;

    // idle
    output_high(PREPACKET_IDLE_US);

    // BREAK
    output_low(BREAK_US);

    // MAB (Mark after break)
    output_high(MAB_US);

    int offset = PREPACKET_IDLE_US + BREAK_US + MAB_US;
    // "Start code" always 0 plus 2 stop bits
    char start_code[1];
    start_code[0] = 0;
    start_code[1] = 0;
    wave_add_serial(deamon_id, TX_PIN, 250000, 8, 4, offset, 2, start_code);
    // move the offset to account for the start code
    offset = offset + 44;
    wave_add_serial(deamon_id, TX_PIN, 250000, 8, 4, offset, DMX_CHANNELS, dmx_values);

    // move the offset to account for the start code
    offset = offset + (44 * DMX_CHANNELS);

    // idle
    output_high(POSTPACKET_IDLE_US);

    // add the pulses to the wave
    wave_add_generic(deamon_id, current_pulse, pulses);

    // creates a waveform from the data provided by the prior calls to the wave_add_* functions
    int wave_id = wave_create(deamon_id);

    // transmit the waveform with id wave_id (the waveform is sent once)
    wave_send_once(deamon_id, wave_id);

    // wait for the transmission to finish
    while (wave_tx_busy(deamon_id)) {
        // yield to kernel while DMA is in progress
        usleep(200);
    }

    // deletes the waveform with id wave_id
    wave_delete(deamon_id, wave_id);
}

int main() {
    // the specific connection to the socket and the
    // number of bytes successfully read from it on each attempt
    int connection, bytes_read;

    // for timing the sockets requests (to keep consistent time between dmx signals)
    struct timeval tv;
    unsigned long time_in_micros;

    // for automatically changing channels in demo mode
    int position = 0;
    int move = 1;

    // Set the SIGINT (Ctrl-C) signal handler to sigintHandler
    signal(SIGINT, sigintHandler);

    socketConnect();
    gpioSetup();

    while(running) {

        if (DEMO) {

            if (VERBOSE) {
                printf("position: %i\n", position);
            }

            if ((position + move) > 255) {
                move = - move;
            }
            if ((position + move) < 0) {
                move = - move;
            }

            position = position + move;

            // pan
            dmx_values[0] = position;
            // tilt
            dmx_values[1] = position;
            // dimmer
            dmx_values[5] = position;
            // blue
            dmx_values[8] = position;
            // strobe
            dmx_values[12] = position;
        }

        send_packets();

        gettimeofday(&tv, NULL);
        time_in_micros = 1000000 * tv.tv_sec + tv.tv_usec;

        connection = accept(sock, 0, 0);
        if (connection > 0) {
            // read from the socket and put the data into the buffer
            // -1 on error, 0 if no data, otherwise number of bytes received
            while(bytes_read = read(connection, buf, BUFFER_SIZE-1)) {

                // was data received from the socket
                if (bytes_read > 0) {
                    printf("Received %s\n", buf);

                    int channel = 0;
                    int value = 0;
                    int part = CHANNEL_PART;

                    // move through a string which looks like "1:12,2:45,4:99" and set
                    // the corresponding channel and value
                    for (int i = 0; i < strlen(buf); i++)
                    {
                        int num = 0;
                        switch(buf[i]) {
                            // when we reach the colon, we are preparing the value
                            case ':':
                                part = VALUE_PART;
                                break;

                            // when we reach a comma, we have finished this channel/value pair
                            case ',':
                              // set the last value (the socket API we expose is not 0 indexed, because neither is the DMX standard)
                              dmx_values[channel - 1] = value;

                              if (VERBOSE) {
                                  printf("Received channel %d value %d\n", channel, value);
                              }

                              // start again
                              part = CHANNEL_PART;
                              channel = 0;
                              value = 0;
                              break;

                            case '9':
                                num++;
                            case '8':
                                num++;
                            case '7':
                                num++;
                            case '6':
                                num++;
                            case '5':
                                num++;
                            case '4':
                                num++;
                            case '3':
                                num++;
                            case '2':
                                num++;
                            case '1':
                                num++;
                            case '0':
                                // update the channel or value
                                if (part == CHANNEL_PART) {
                                    channel = (channel * 10) + num;
                                } else if (part == VALUE_PART) {
                                    value = (value * 10) + num;
                                }
                                break;
                            default :
                                perror("unexpected character");
                        }
                    }
                    // set the last value (the socket API we expose is not 0 indexed, because neither is the DMX standard)
                    dmx_values[channel - 1] = value;

                    if (VERBOSE) {
                        printf("Received channel %d value %d\n", channel, value);
                    }
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

        // sleep for 1000 microseconds (subtract the time it took to read from the socket)
        gettimeofday(&tv, NULL);
        int duration = (1000000 * tv.tv_sec + tv.tv_usec) - time_in_micros;
        int sleep = 1000 - duration;
        // cant sleep for a negative number
        if (sleep < 0) {
            printf("Warning, took more than 1000 microseconds to read from socket (%d microseconds)\n", duration);
            sleep = 0;
        }
        usleep(sleep);

    }

    return 0;

}

