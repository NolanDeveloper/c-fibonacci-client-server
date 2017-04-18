#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define PORT 20000

#define PORTION_SIZE 1000
#define PORTIONS 1000

#define DEBUG_TAG  "Debug"
#define INFO_TAG   "Info"
#define ERROR_TAG  "Error"
#define OUTPUT_TAG "Output"

typedef char buffer_t[22];

static int fd;
static int used;
static buffer_t buffer, message;
static char * server_url;
static int verbose;

static void
print_timestamp(FILE * stream) {
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);
  fprintf(stream, "[%02d:%02d:%02d]", tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static void
vnote(const char * type, const char * fmt, va_list ap) {
  if (!verbose && strcmp(ERROR_TAG, type)) return;
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

static void
send_portion(int i) {
  int j;
  int printed;
  for (j = 0; j < PORTION_SIZE; ++j) {
    printed = sprintf(message, "%d\r\n", i * PORTION_SIZE + j);
    if (-1 == send(fd, message, printed, 0)) die("send failed");
  }
}

static int
receive_portion(int i) {
  int received;
  int is_true, is_false;
  char * begin, * end_of_message;
  int j;
  for (j = 0; j < PORTION_SIZE;) {
    begin = buffer;
    received = recv(fd, buffer + used, sizeof(buffer) - used - 1, 0);
    if (-1 == received) die("recv failed");
    if (!received) {
      note(INFO_TAG, "Connection was closed by server");
      return 1;
    }
    buffer[used + received] = '\0';
    note(INFO_TAG, "Got bytes:\n%s", buffer + used);
    while (j < PORTION_SIZE) {
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
          i * PORTION_SIZE + j++);
        begin = end_of_message + 2;
      }
    }
    used = buffer + used + received - begin;
    memmove(buffer, begin, used);
  }
  return 0;
}

static void
show_usage(char * program) {
  die("usage: %s server.com", program);
}

static void
process_arguments(int argc, char * argv[]) {
  char * argument;
  int i;
  for (i = 1; i < argc; ++i) {
    argument = argv[i];
    if (!strcmp("-h", argument)) show_usage(argv[0]);
    else if (!strcmp("-v", argument)) verbose = 1;
    else if (!server_url) {
      server_url = argument;
    } else show_usage(argv[0]);
  }
}

int
main(int argc, char * argv[]) {
  struct sockaddr_in server_address;
  struct hostent * hosts;
  int i;
  process_arguments(argc, argv);
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
  send_portion(0);
  send_portion(1);
  for (i = 0; i < PORTIONS - 2; ++i) {
    if (1 == receive_portion(i)) goto finish;
    send_portion(i + 2);
  }
  receive_portion(PORTIONS - 2);
  receive_portion(PORTIONS - 1);
finish:
  close(fd);
  return 0;
}
