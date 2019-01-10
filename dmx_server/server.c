#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <errno.h>
#include <pigpio.h>

// 1 second = 1000 millisecond
// 1 millisecond = 1000 microsecond
// 1 microsecond = 1000 nano second

// 1 dmx512 bit (250kbps) = 4 microsecond
// 11 bits per channel
// 513 channels (channel 0 doesnt count, and is always a value of 0)
// approx 23 milliseconds per send
// 77 milliseconds between sends
// 10 sends per second (or 16 sends per second for faster bitwise?
// 1,000,000 in binary is 11110100001001000000

#define BUFFER_SIZE 1024
#define NAME "/tmp/dmx.sock"
// to hold the messages we receive from the unix socket
char buf[BUFFER_SIZE];
int sock, msgsock, rval;
struct sockaddr_un server;

// Physical pin 29, BCM pin 5, Wiring Pi pin 21
#define OUTPUT_PIN 5

// set to 0, 1 or 2
#define VERBOSE 0

// boolean enums (because they are pretty and nice to use)
typedef enum { false, true } bool;

int running = 1;

// for measuring how long execution takes
struct timeval tv;

#define DMX_CHANNELS 60
#define BREAK_BITS 40
#define MAB_BITS 5
#define DMX_BITS_COUNT BREAK_BITS + MAB_BITS + (DMX_CHANNELS * 11)

// TODO : looks like the first bit isnt being written, and the last bit in the array is LOW but should be HIGH

// used for more legible code below
#define CHANNEL_PART 1
#define VALUE_PART 2

bool dmx_bits[DMX_BITS_COUNT];

#define HIGH true
#define LOW false


// the socket connection
int sock;

FILE *f;

/*
    Signal Handler for SIGINT, to close our
    connections and clean up before exiting
*/
void sigintHandler(int sig_num) {
  running = 0;
  // close the socket connection
  close(sock);
  // delete the actual file used for the socket
  unlink(NAME);
  // print a nice friendly message
  fprintf(f, "Goodbye\n");
  fflush(f);
  // exit cleanly
  gpioTerminate();
  exit(sig_num);
}

void gpioSetup() {
  gpioInitialise();

  gpioSetMode(OUTPUT_PIN, PI_OUTPUT);
}

unsigned long dmx_bit_tick() {
  gettimeofday(&tv, NULL);
  return ((1000000 * tv.tv_sec) + tv.tv_usec);
}

void setDmxValue(unsigned short channel, unsigned short value) {
  if (channel > DMX_CHANNELS) {
    fprintf(f, "cant set channel %d as it is greater than %d max channels", channel, DMX_CHANNELS);
    fflush(f);
    exit(EXIT_FAILURE);
  }

  if (value > 255) {
    fprintf(f, "cant set channel %d to value %d as it is greater than 255", channel, value);
    fflush(f);
    exit(EXIT_FAILURE);
  }

  unsigned short start_bit = BREAK_BITS + MAB_BITS + (11 * channel);

  // the start bit is always 0
  dmx_bits[start_bit] = LOW;
  // the next 8 bits represent the value
  dmx_bits[start_bit + 1] = (value & 1) == 1 ? HIGH : LOW;
  dmx_bits[start_bit + 2] = (value & 2) == 2 ? HIGH : LOW;
  dmx_bits[start_bit + 3] = (value & 4) == 4 ? HIGH : LOW;
  dmx_bits[start_bit + 4] = (value & 8) == 8 ? HIGH : LOW;
  dmx_bits[start_bit + 5] = (value & 16) == 16 ? HIGH : LOW;
  dmx_bits[start_bit + 6] = (value & 32) == 32 ? HIGH : LOW;
  dmx_bits[start_bit + 7] = (value & 64) == 64 ? HIGH : LOW;
  dmx_bits[start_bit + 8] = (value & 128) == 128 ? HIGH : LOW;
  // the next 2 bits are stop bits
  dmx_bits[start_bit + 9] = HIGH;
  dmx_bits[start_bit + 10] = HIGH;
}

void prepareDmxValues() {
  unsigned short bit = 0;
  unsigned short channel = 0;

  // the "break" before the DMX signal
  while(bit < BREAK_BITS){
    dmx_bits[bit++] = LOW;
  }

  // the "mark after break" for the DMX signal
  while(bit < BREAK_BITS + MAB_BITS){
    dmx_bits[bit++] = HIGH;
  }

  // set all the other channels to a default value of 0
  for (channel = 0; channel < DMX_CHANNELS; channel++) {
    setDmxValue(channel, 0);
  }

}

void waitForInterrupt() {
  unsigned long previous_tick = dmx_bit_tick();
  unsigned long tick = previous_tick;
  while ((tick - previous_tick) <= 1) {
    previous_tick = tick;
    tick = dmx_bit_tick();
  }
}

int transmit_payload() {
  register int current_position = 0;
  gpioWrite(OUTPUT_PIN, dmx_bits[current_position]);

  register unsigned long first_tick = dmx_bit_tick();

  while(current_position < DMX_BITS_COUNT) {
    register int ticks = dmx_bit_tick() - first_tick - (current_position * 4);

    // every 4 microseconds (because DMX is 250kbps)
    // we allow 5 too, because it would be fast enough to switch
    if (ticks == 4) {
      current_position++;
      // if there is a change to the pin
      if (dmx_bits[current_position] != dmx_bits[current_position-1]) {
        // write the change
        gpioWrite(OUTPUT_PIN, dmx_bits[current_position]);
      }
    }
    // todo : consider ticks = 8, 12, 16 and doing all the bits before increasing the value of current_position (for performance reasons)
    else if (ticks > 4) {
      // missed a tick, stop here and return the number of successfully transmitted bits
      return DMX_BITS_COUNT - current_position;
    }

  }
  // all bits transmitted successfully, return 0
  return 0;
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
  int skipped_bits = 0;
  int transmits = 0;
  int fails = 0;

  f = fopen("/var/log/dmx_server", "a+"); // a+ (create + append) option will allow appending which is useful in a log file
  if (f == NULL) {
    perror("opening log file");
  }

  // the specific connection to the socket and the
  // number of bytes successfully read from it on each attempt
  int connection, bytes_read;

  socketConnect();
  gpioSetup();

  // Set the SIGINT (Ctrl-C) signal handler to sigintHandler
  gpioSetSignalFunc(SIGINT, sigintHandler);

  prepareDmxValues();

  while(running) {

    gpioWrite(OUTPUT_PIN, true);
    usleep(1000);
    waitForInterrupt();
    skipped_bits = transmit_payload();
    transmits++;

    if (skipped_bits) {
      fails++;
      fprintf(f, "skipped %d bits for transmission %d\n", skipped_bits, transmits);
      fflush(f);
      usleep(1000);
    } else {
      fails = 0;
    }

    gpioWrite(OUTPUT_PIN, true);

    if (fails > 10000) {
      exit(EXIT_FAILURE);
    }

    connection = accept(sock, 0, 0);
    if (connection > 0) {
      // read from the socket and put the data into the buffer
      // -1 on error, 0 if no data, otherwise number of bytes received
      while((bytes_read = read(connection, buf, BUFFER_SIZE-1)) != 0) {

        // was data received from the socket
        if (bytes_read > 0) {
          fprintf(f, "Received %s\n", buf);
          fflush(f);

          int channel = 0;
          int value = 0;
          int part = CHANNEL_PART;

          // move through a string which looks like "1:12,2:45,4:99" and set
          // the corresponding channel and value
          for (int i = 0; i < strlen(buf); i++) {
            int num = 0;
            switch(buf[i]) {
              // when we reach the colon, we are preparing the value
              case ':':
                part = VALUE_PART;
                break;

              // when we reach a comma, we have finished this channel/value pair
              case ',':
                // set the last value (the socket API we expose is not 0 indexed, because neither is the DMX standard)
                setDmxValue(channel, value);

                if (VERBOSE) {
                  fprintf(f, "Received channel %d value %d\n", channel, value);
                  fflush(f);
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
          setDmxValue(channel, value);

          if (VERBOSE) {
            fprintf(f, "Received channel %d value %d\n", channel, value);
            fflush(f);
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

  }

  return 0;

}


