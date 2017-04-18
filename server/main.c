#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <errno.h>

#define MAX(a, b) ((a) < (b) ? (b) : (a))

#define PORT 20000
#define MAX_CONNECTIONS 1024

#define STATS_TAG "Stats"
#define DEBUG_TAG "Debug"
#define INFO_TAG  "Info"
#define ERROR_TAG "Error"

typedef char buffer_t[1024];

struct ConnectionData {
  buffer_t buffer;
  int used;
  int closed;
};

static struct pollfd sockets[MAX_CONNECTIONS];
static struct ConnectionData data[MAX_CONNECTIONS - 1];
static int number_of_sockets;
static int messages_sent;

static void
print_timestamp(FILE * stream) {
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);
  fprintf(stream, "[%02d:%02d:%02d]",
    tm.tm_hour, tm.tm_min, tm.tm_sec);
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

static long
is_square(long n) {
  long square_root = floor(sqrt(n));
  return square_root * square_root == n;
}

static long
is_fibonacci(long n) {
  long t = 5 * n * n;
  return is_square(t + 4) || is_square(t - 4);
}

static void
prepare_server() {
  int error, t;
  static struct sockaddr_in server;
  int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (-1 == server_fd) die("Could not create socket");
  note(DEBUG_TAG, "Server socket was created");
  server.sin_family = AF_INET;
  server.sin_port = htons(PORT);
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  t = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t));
  error = bind(server_fd, (struct sockaddr *) &server, sizeof(server));
  if (-1 == error) die("Could not bind socket");
  note(DEBUG_TAG, "Server socket was binded");
  error = listen(server_fd, 128);
  if (-1 == error) die("Could not listen on socket");
  note(DEBUG_TAG, "Server socket's in listen mode");
  sockets[number_of_sockets].fd = server_fd;
  sockets[number_of_sockets].events = POLLIN;
  ++number_of_sockets;
}

static void
accept_new_client(int server_fd) {
  struct sockaddr_in address;
  socklen_t address_length = sizeof(address);
  int client_fd = accept(server_fd,
    (struct sockaddr *)&address, &address_length);
  if (client_fd < 0) {
    note(ERROR_TAG, "Could not establish new connection");
    note(ERROR_TAG, "accept failed: %s", strerror(errno));
    return;
  }
  note(DEBUG_TAG, "Client accepted '%s'", inet_ntoa(address.sin_addr));
  sockets[number_of_sockets].fd = client_fd;
  sockets[number_of_sockets].events = POLLIN;
  data[number_of_sockets].used = 0;
  data[number_of_sockets].closed = 0;
  ++number_of_sockets;
}

static int
send_message(int client_fd, int is_fibonacci) {
  int result;
  if (is_fibonacci) {
    result = send(client_fd, "true\r\n", 6, 0);
  } else {
    result = send(client_fd, "false\r\n", 7, 0);
  }
  if (-1 == result) {
    note(ERROR_TAG, "send failed: %s", strerror(errno));
    return -1;
  }
  if (is_fibonacci) {
    note(INFO_TAG, "'true' was sent");
  } else {
    note(INFO_TAG, "'false' was sent");
  }
  ++messages_sent;
  return 0;
}

static void
process_new_data(int client_fd, struct ConnectionData * data) {
  int error;
  char * begin;
  int received;
  begin = data->buffer;
  received = recv(client_fd, data->buffer + data->used,
    sizeof(data->buffer) - data->used - 1, 0);
  if (!received) {
    note(DEBUG_TAG, "Connection was closed by client");
    goto close_connection;
  }
  if (-1 == received) {
    note(ERROR_TAG, "recv failed: %s", strerror(errno));
    goto close_connection;
  }
  data->buffer[data->used + received] = '\0';
  note(DEBUG_TAG, "%d bytes were received:\n%s", received,
    data->buffer + data->used);
  while (1) {
    char * end_of_message;
    long n;
    end_of_message = strstr(begin, "\r\n");
    if (!end_of_message && begin == data->buffer) {
      note(ERROR_TAG, "Message is too long:\n%s", begin);
      goto close_connection;
    }
    if (!end_of_message) break;
    *end_of_message = '\0';
    note(INFO_TAG, "New message was received: '%s'", begin);
    if (1 != sscanf(begin, "%ld", &n)) {
      note(ERROR_TAG, "Received message is unknown");
      goto close_connection;
    }
    begin = end_of_message + 2;
    if (-1 == send_message(client_fd, is_fibonacci(n))) goto close_connection;
  }
  data->used = received - (begin - (data->buffer + data->used));
  data->closed = 0;
  memmove(data->buffer, begin, data->used);
  data->buffer[data->used] = '\0';
  note(DEBUG_TAG, "Buffered data(%d bytes):\n%s", data->used,
    data->buffer);
  return;
close_connection:
  data->closed = 1;
}

static void
clean_closed_connections() {
  int s, d;
  for (s = d = 0; s != number_of_sockets - 1; ++s) {
    if (data[s].closed) continue;
    sockets[d + 1] = sockets[s + 1];
    data[d] = data[s];
    ++d;
  }
  number_of_sockets = d + 1;
}

static void
show_stats_if_ready() {
  static struct timeval prev = { 0, 0 };
  static double max_messages_per_second = 0;
  struct timeval now;
  double seconds;
  double messages_per_second;
  if (-1 == gettimeofday(&now, NULL)) {
      note(ERROR_TAG, "gettimeofday failed: %s", strerror(errno));
      return;
  }
  seconds = (double) (now.tv_sec - prev.tv_sec) +
    (now.tv_usec - prev.tv_usec) / 1000000.;
  if (seconds < 1.) return;
  messages_per_second = messages_sent / seconds;
  max_messages_per_second = MAX(messages_per_second, max_messages_per_second);
  note(STATS_TAG, "messages sent: %d", messages_sent);
  note(STATS_TAG, "messages per second: %lf", messages_per_second);
  note(STATS_TAG, "max messages per second: %lf", max_messages_per_second);
  messages_sent = 0;
  prev = now;
}

int
main() {
  int i, result;
  prepare_server();
  while (1) {
    note(INFO_TAG, "Polling... Number of connections: %d",
      number_of_sockets - 1);
    result = poll(sockets, number_of_sockets, ONE_SECOND);
    if (-1 == result) die("poll failed");
    show_stats_if_ready();
    if (result) {
        for (i = 0; i < number_of_sockets; ++i) {
          struct pollfd socket = sockets[i];
          if (!(socket.revents & POLLIN)) continue;
          if (!i) {
            accept_new_client(socket.fd);
          } else {
            process_new_data(socket.fd, &data[i - 1]);
          }
        }
        clean_closed_connections();
    }
  }
  note(INFO_TAG, "Server was stoped");
  return 0;
}
