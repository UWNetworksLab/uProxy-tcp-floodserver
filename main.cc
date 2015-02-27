#include <getopt.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <fstream>
#include <string> 
#include <vector>

struct Stat {
  // Time the stat was taken.
  struct timespec now;
  // Tim the prior stat was take.
  struct timespec prior;
  // Total sent on this socket.
  uint64_t total_sent;
  // Total sent between 'prior' and 'now'
  uint64_t sent_last_interval;

  Stat() : total_sent(0), sent_last_interval(0) {}
  void snapshot(uint64_t sent) {
    clock_gettime(CLOCK_REALTIME, &now);
    total_sent = sent;
  }
  void diff(const Stat& other) {
    prior = other.now;
    sent_last_interval = total_sent - other.total_sent;
  }
  Stat& operator=(const Stat& other) {
    total_sent = other.total_sent;
    now = other.now;
    prior = other.prior;
    sent_last_interval = other.sent_last_interval;
  }
};

// Not yet, but soon, needed.
class ScopedLock {
public:
  ScopedLock(pthread_mutex_t *lock) : lock_(lock) {
    pthread_mutex_lock(lock_);
  }
  ~ScopedLock() {
    pthread_mutex_unlock(lock_);
  }
private:
  pthread_mutex_t *lock_;
};

// Once created, it's read-only.
#define MAX_BUF_SIZE (1073741824)
std::vector<uint8_t> out_buffer;

// Command options and configuration
std::ofstream output_file;
bool opt_has_output_file;
bool opt_print_status;
bool opt_stream_status;
double opt_update_interval_sec;

const double kOneBillion = 1000000000.0;
const int64_t kOneBillionInt = 1000000000LL;

double time_of(const struct timespec& t) {
  double d = t.tv_nsec;
  d = d / kOneBillion;
  d = d + t.tv_sec;
  return d;
}

double time_delta(const struct timespec& later,
                  const struct timespec& earlier) {
  int64_t nsec = later.tv_nsec - earlier.tv_nsec;
  int64_t sec = later.tv_sec - earlier.tv_sec;
  if (nsec < 0.0) {
    nsec += kOneBillionInt;
    sec -= 1;
  }
  double dv = (sec * kOneBillionInt) + nsec;
  return dv;
}

void output_stat(const Stat& stat);

void loop (int accepted_sock) {
  // write in out_buffer.size() blocks.
  Stat cur, prev;
  size_t len = out_buffer.size();
  uint64_t written = 0;
  struct timespec read_start, read_now;
  printf("Accept sock %d\n", accepted_sock);
  while (written >= 0) {
    output_stat(prev);
    clock_gettime(CLOCK_REALTIME, &read_start);
    double dt;
    // snapshot at 2Hz
    do {
      written += write(accepted_sock, &out_buffer[0], len);
      clock_gettime(CLOCK_REALTIME, &read_now);
      dt = time_delta(read_now, read_start);
    } while (dt < (kOneBillion * opt_update_interval_sec));
    cur.snapshot(written);
    cur.diff(prev);
    prev = cur;
  }
  close(accepted_sock);
}

std::string format_size(uint64_t amount) {
  double d = amount;
  // Yup, an snprintf returned as a string, because formatting in printf is
  // that much better.
  char retbuf[128];
  if (amount > 1000000000LL) {
    snprintf(retbuf, 128, "%4.2f Gb", d / 1000000000.0);
  } else if (amount > 1000000) {
    snprintf(retbuf, 128, "%4.2f Mb", d / 1000000.0);
  } else if (amount > 1000) {
    snprintf(retbuf, 128, "%4.2f Kb", d / 1000.0);
  } else {
    snprintf(retbuf, 128, "%lu byte", amount);
  }
  return std::string(retbuf);
}

void splat(char *dest, int count, char c) {
  while (count--) {
    *dest++ = c;
  }
}

void output_stat(const Stat& stat) {
  if (opt_has_output_file) {
    output_file << "{total: " << stat.total_sent
                << ", sent_last_interval: " << stat.sent_last_interval
                << ", now: " << time_of(stat.now)
                << ", prior: " << time_of(stat.prior)
                << "}\n";
  }
  if (opt_print_status) {
    double delta_t = time_of(stat.now) - time_of(stat.prior);
    double rate = stat.sent_last_interval / delta_t;
    char outbuf[81];
    int len = snprintf(outbuf, 80,
                       "%12.3f: %s/sec (dt = %6.2f)",
                       time_of(stat.now), format_size(rate).c_str(), delta_t);
    splat(&outbuf[len], 80-len, ' ');
    outbuf[79] = opt_stream_status ? '\n' : '\r';
    outbuf[80] = 0;
    fwrite(outbuf, 80, 1, stdout);
    fflush(stdout);
  }
}

int usage(char *prog) {
  printf("%s: start a TCP server (random data) and write as fast as possible\n"
         "  Options:\n"
         "  -f, --file FILENAME: output filename for output rate logs.\n"
         "  -b, --bufsize SIZE: size of buffer to write(2) to socket in loop.\n"
         "  -p, --port PORT: port number to serve on.\n"
         "  -i, --interval SECONDS: interval (floating-point) between status updates.\n"
         "  --quiet: do not print rate status messages.\n"
         "  --statstream: do not overwrite own status message on console\n",
         prog);
}

int main(int argc, char **argv) {
  int c;
  char *filename = NULL;
  int bufsize = 4096;
  // Status modes:
  // 0: print nothing.
  // 1: print a self updating status message to stdout.  This is a message
  //    suffixed with \r to overwrite prior versions of itself.
  // 2: print a stream of messages to stdout.  This message has a newline at
  //    the end instead.
  int status_mode = 1;
  int port = 1224;
  opt_update_interval_sec = 1.0;

  do {
    static struct option long_options[] = {
      {"file",       required_argument, 0,            'f' },
      {"bufsize",    required_argument, 0,            'b' },
      {"statstream", no_argument,       &status_mode, 2 },
      {"quiet",      no_argument,       &status_mode, 0 },
      {"interval",   required_argument, 0,            'i' },
      {"port",       required_argument, 0,            'p' },
      {0,            0,                 0,            0 }
    };
    int option_index = 0;

    c = getopt_long(argc, argv, "f:b:i:sp:qh", long_options,
                    &option_index);
    switch (c) {
    case -1:  // No more options, or getopt_long already did the work.
    case 0:
      break;
    case 'f':
      filename = optarg;
      break;
    case 'p': {
      port = atoi(optarg);
      if (port < 1024 || port > 65535) {
        printf("Invalid port number: %s\n", optarg);
        exit(1);
      }
    } break;
    case 'b': {
      bufsize = atoi(optarg);
      if (bufsize < 0 || bufsize > MAX_BUF_SIZE) {
        printf("Invalid buffer size argument: %s\n", optarg);
        exit(1);
      }
    } break;
    case 'q': {
      status_mode = 0;
    } break;
    case 'i': {
      opt_update_interval_sec = atof(optarg);
      if (opt_update_interval_sec < 0.001 || opt_update_interval_sec > 600) {
        printf("Invalid interval argument: %s\n", optarg);
        exit(1);
      }
    } break;
    case 'h':
      usage(argv[0]);
      exit(1);
    }
  } while (c >= 0);

  // Buffer setup.
  out_buffer.resize(bufsize);
  FILE * urandom = fopen("/dev/urandom", "r");
  if (!urandom) {
    perror("open /dev/urandom for buffer fill");
    exit(1);
  }
  fread(&out_buffer[0], 1, bufsize, urandom);
  fclose(urandom);

  // Status setup.
  if (filename != NULL) {
    output_file.open(filename);
    if (!output_file.is_open()) {
      perror(filename);
      exit(1);
    } else {
      printf("Logging status messages to %s\n", filename);
      opt_has_output_file = true;
    }
  }
  switch (status_mode) {
  case 0:
    opt_print_status = false;
    opt_stream_status = false;
    break;
  case 1:
    opt_print_status = true;
    opt_stream_status = false;
    break;
  case 2:
    opt_print_status = true;
    opt_stream_status = true;
    break;
  default:
    puts("invalid status_mode. I blame getopt_long!!");
    exit(1);
  }

  // Open up, listen, and run an accept loop.
  printf("Listening to port %d\n", port);
  printf(" Print status: %s, Stream status: %s\n",
         opt_print_status ? "yes":"no",
         opt_stream_status ? "yes":"no");
  int sock;
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    exit(1);
  }

  struct sockaddr_in addr = {0};
  socklen_t addr_len = sizeof(addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = INADDR_ANY;
  if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    perror("bind");
    exit(1);
  }

  if (listen(sock, 2) < 0) {
    perror("listen");
    exit(1);
  }

  int client_sock;
  while ((client_sock = accept(sock, reinterpret_cast<struct sockaddr*>(&addr),
                               &addr_len)) > 0) {
    loop(client_sock);
  }
  perror("socket");
  exit(0);
}

