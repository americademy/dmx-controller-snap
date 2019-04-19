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
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <execinfo.h>

#define GPSET0 7
#define GPCLR0 10

unsigned piModel;
unsigned piRev;

static volatile uint32_t  *gpioReg = MAP_FAILED;

#define PI_BANK (gpio>>5)
#define PI_BIT  (1<<(gpio&0x1F))

// set to 0 or 1, when 1 (truthy) it will not actually write to the gpiomem and
// can be tested on non raspberry pi devices, such as an ubuntu virtual machine
// on my mac
#define SIMULATE 0

// if SOCK_NONBLOCK is not defined, then define it as the same value of O_NONBLOCK
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK O_NONBLOCK
#endif

/* gpio modes. */

#define PI_OUTPUT 1

void gpioSetMode(unsigned gpio, unsigned mode)
{
   int reg, shift;

   reg   =  gpio/10;
   shift = (gpio%10) * 3;

   gpioReg[reg] = (gpioReg[reg] & ~(7<<shift)) | (mode<<shift);
}

void gpioWrite(unsigned gpio, unsigned level)
{
  if (SIMULATE) {
    return;
  } else if (level == 0) {
    *(gpioReg + GPCLR0 + PI_BANK) = PI_BIT;
  } else {
    *(gpioReg + GPSET0 + PI_BANK) = PI_BIT;
  }
}

unsigned gpioHardwareRevision(void)
{
   static unsigned rev = 0;

   FILE * filp;
   char buf[512];
   char term;
   int chars=4; /* number of chars in revision string */

   if (rev) return rev;

   piModel = 0;

   filp = fopen ("/proc/cpuinfo", "r");

   if (filp != NULL)
   {
      while (fgets(buf, sizeof(buf), filp) != NULL)
      {
         if (piModel == 0)
         {
            if (!strncasecmp("model name", buf, 10))
            {
               if (strstr (buf, "ARMv6") != NULL)
               {
                  piModel = 1;
                  chars = 4;
               }
               else if (strstr (buf, "ARMv7") != NULL)
               {
                  piModel = 2;
                  chars = 6;
               }
               else if (strstr (buf, "ARMv8") != NULL)
               {
                  piModel = 2;
                  chars = 6;
               }
            }
         }

         if (!strncasecmp("revision", buf, 8))
         {
            if (sscanf(buf+strlen(buf)-(chars+1),
               "%x%c", &rev, &term) == 2)
            {
               if (term != '\n') rev = 0;
            }
         }
      }

      fclose(filp);
   }
   return rev;
}

int gpioInitialise(void)
{
   int fd;

   piRev = gpioHardwareRevision(); /* sets piModel and piRev */

   fd = open("/dev/gpiomem", O_RDWR | O_SYNC) ;

   if (fd < 0)
   {
      fprintf(stderr, "failed to open /dev/gpiomem\n");
      return -1;
   }

   gpioReg = (uint32_t *)mmap(NULL, 0xB4, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

   close(fd);

   if (gpioReg == MAP_FAILED)
   {
      fprintf(stderr, "Bad, mmap failed\n");
      return -1;
   }
   return 0;
}




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

char LOG_FILE[256];
char SOCKET_FILE[256];

#define BUFFER_SIZE 10240000
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

#define DMX_CHANNELS 78
#define BREAK_BITS 40
#define MAB_BITS 5
#define DMX_BITS_COUNT BREAK_BITS + MAB_BITS + (DMX_CHANNELS * 11)

// TODO : looks like the first bit isn't being written, and the last bit in the array is LOW but should be HIGH

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
  unlink(SOCKET_FILE);
  // print a nice friendly message
  fprintf(f, "Goodbye\n");
  fflush(f);
  // exit cleanly
  exit(sig_num);
}

void sigsegvHandler(int sig) {
  void *array[10];
  size_t size;

  // get void*'s for all entries on the stack
  size = backtrace(array, 10);

  // print out all the frames to stderr
  fprintf(stderr, "Error: signal %d:\n", sig);
  backtrace_symbols_fd(array, size, STDERR_FILENO);
  exit(EXIT_FAILURE);
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
    fprintf(stderr, "cant set channel %d as it is greater than %d max channels\n", channel, DMX_CHANNELS);
    exit(EXIT_FAILURE);
  }

  if (value > 255) {
    fprintf(stderr, "cant set channel %d to value %d as it is greater than 255\n", channel, value);
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

    fprintf(f, "Starting socket server on %s\n", SOCKET_FILE);

    if (access(SOCKET_FILE, F_OK) != -1) {
      fprintf(f, "Deleting existing socket file\n");
      unlink(SOCKET_FILE);
    }

    server.sun_family = AF_UNIX;
    strcpy(server.sun_path, SOCKET_FILE);

    // connect the socket
    sock = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock < 0) {
        fprintf(stderr, "ERROR: opening stream socket\n");
        exit(EXIT_FAILURE);
    }

    // bind the socket
    if (bind(sock, (struct sockaddr *) &server, sizeof(struct sockaddr_un))) {
        fprintf(stderr, "ERROR: binding stream socket\n");
        exit(EXIT_FAILURE);
    }

    if (listen(sock, 5) < 0)  {
        fprintf(stderr, "ERROR: listen\n");
        exit(EXIT_FAILURE);
    }
}

int main() {
  int skipped_bits = 0;
  int transmits = 0;
  int fails = 0;

  const char *snap_data_path;
  if (getenv("SNAP_DATA")) {
    snap_data_path = getenv("SNAP_DATA");
  } else {
    snap_data_path = "/tmp";
  }
  snprintf(LOG_FILE, sizeof LOG_FILE, "%s/log", snap_data_path);
  snprintf(SOCKET_FILE, sizeof SOCKET_FILE, "%s/dmx-server.sock", snap_data_path);

  printf("Starting server\nlogging output to: %s\n", LOG_FILE);

  f = fopen(LOG_FILE, "a+"); // a+ (create + append) option will allow appending which is useful in a log file
  if (f == NULL) {
    fprintf(stderr, "Could not open log file for writing\n");
    exit(EXIT_FAILURE);
  }

  fprintf(f, "Starting server\n");

  // the specific connection to the socket and the
  // number of bytes successfully read from it on each attempt
  int connection, bytes_read;

  socketConnect();
  if (SIMULATE == 0) {
    gpioSetup();
  }

  // Set the SIGINT (Ctrl-C) signal handler to sigintHandler
  signal(SIGINT, sigintHandler);
  signal(SIGSEGV, sigsegvHandler);

  prepareDmxValues();

  while(running) {

    gpioWrite(OUTPUT_PIN, true);
    usleep(1000);
    waitForInterrupt();
    skipped_bits = transmit_payload();
    transmits++;

    if (skipped_bits) {
      // on failure, push the logic level down in an attempt to skip the last byte (as the stop bits wont be in the correct place)
      gpioWrite(OUTPUT_PIN, false);
      fails++;
      fprintf(f, "skipped %d bits for transmission %d\n", skipped_bits, transmits);
      fflush(f);
      usleep(1000);
    } else {
      fails = 0;
    }

    gpioWrite(OUTPUT_PIN, true);

    // if we're getting a lot of interrupts in a row, then sleep for .1 second every 10th time
    if (fails >= 10 && fails % 10 == 0) {
      usleep(100000);
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
                fprintf(stderr, "ERROR: unexpected character\n");
                fprintf(stderr, "ERROR: unexpected character in buffer %s\n", buf);
                exit(EXIT_FAILURE);
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
            fprintf(stderr, "ERROR: socket read\n");
            exit(EXIT_FAILURE);
          }
        }
        // bytes_read should be either -1 or greater than 0
        else {
          fprintf(stderr, "ERROR: bytes_read should be either -1 or greater than 0\n");
          exit(EXIT_FAILURE);
        }

        // zero fill the buffer again before we try and read into it
        bzero(buf, sizeof(buf));
      }

      close(connection);
    }

  }

  return 0;

}


