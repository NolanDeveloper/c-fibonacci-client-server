#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define PORT 20000

#define NUMBERS_TO_CHECK 100000
#define PORTION_SIZE 100

#define DEBUG_TAG  "Debug"
#define INFO_TAG   "Info"
#define ERROR_TAG  "Error"
#define OUTPUT_TAG "Output"

typedef char buffer_t[1024];

static int fd;
static int used;
static buffer_t buffer, message;

static void
print_timestamp(FILE * stream) {
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);
  fprintf(stream, "[%02d:%02d:%02d]", tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static void
vnote(const char * type, const char * fmt, va_list ap) {
  print_timestamp(stdout);
  printf(" (%s): ", type);
  vprintf(fmt, ap);
  putchar('\n');
}

static void
note(const char * type, const char * fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vnote(type, fmt, ap);
  va_end(ap);
}

static void
die(const char * fmt, ...) {
  va_list ap;
  note(ERROR_TAG, "System error: %s", strerror(errno));
  va_start(ap, fmt);
  vnote(ERROR_TAG, fmt, ap);
  va_end(ap);
  exit(EXIT_FAILURE);
}

int
main(int argc, char * argv[]) {
  struct sockaddr_in server_address;
  struct hostent * hosts;
  char * begin, * end_of_message;
  int error;
  int printed;
  int total;
  int bytes;
  int i, j;
  int is_true, is_false;
  if (argc != 2) die("usage: <program> server.com");
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (-1 == fd) die("Can't open socket");
  note(DEBUG_TAG, "Created socket");
  hosts = gethostbyname(argv[1]);
  if (!hosts) die("gethostbyname failed");
  note(DEBUG_TAG, "Resolved host");
  memset(&server_address, 0, sizeof(server_address));
  memcpy(&server_address.sin_addr, hosts->h_addr_list[0],
    sizeof(server_address.sin_addr));
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(PORT);
  if (-1 == connect(fd, (struct sockaddr *) &server_address,
      sizeof(server_address))) {
    die("Can't connect to server");
  }
  note(INFO_TAG, "Connected");
  i = 0;
  while (i < NUMBERS_TO_CHECK) {
    total = 0;
    for (j = 0; j < PORTION_SIZE; ++j) {
      printed = sprintf(message + total, "%d\r\n", i + j);
      total += printed;
    }
    note(DEBUG_TAG, "Sending message(%d bytes):\n%s", total, message);
    error = send(fd, message, total, 0);
    if (-1 == error) die("send failed");
    j = 0;
    while (j < PORTION_SIZE) {
      note(DEBUG_TAG, "Receiving new portion of data");
      bytes = recv(fd, buffer + used, sizeof(buffer) - used - 1, 0);
      if (-1 == bytes) die("recv failed");
      if (!bytes) {
        note(INFO_TAG, "Connection was closed by server");
        goto finish;
      }
      buffer[used + bytes] = '\0';
      begin = buffer + used;
      while (1) {
        end_of_message = strstr(begin, "\r\n");
        if (!end_of_message && begin == buffer) die("Message is too long");
        if (!end_of_message) break;
        *end_of_message = '\0';
        is_true = !strcmp(begin, "true");
        is_false = !strcmp(begin, "false");
        if (!is_true && !is_false) {
          die("Unknown response:\n%s", begin);
        } else {
          note(OUTPUT_TAG,
            is_true ? "%d is fibonacci number" : "%d is not fibonacci number",
            i + j++);
          begin = end_of_message + 2;
        }
      }
      used = buffer + used + bytes - begin;
      memmove(buffer, begin, used);
    }
    i += PORTION_SIZE;
  }
finish:
  close(fd);
  return 0;
}
