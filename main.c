#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "controller.h"
#include "logger.h"

#define LISTEN_PORT  27910
#define IP_ADDRESS_SZ  15  // 111.222.333.444

#define umin(x,y) (((uint64_t)(x) < (uint64_t)(y)) ? (x) : (y))


int sockfd;
int connfd;
FILE *ostream = NULL;
volatile int samples = 0;
volatile int session = 0;
volatile int thread_working = 0;

sample_t *sample_head, *s_prev = NULL;

void dispatch(const char *msg, uint length, char *peer_ip_address);

// Signal handler to close the port cleanly if we get killed
void handle_sig(int sig)
{
    close(connfd);
    close(sockfd);
    //logger_handle_sig();
    exit(0);
}



int main ()
{
  struct sockaddr_in serv_addr, cli_addr;
  socklen_t  clilen;
  struct timeval sock_timeout_val = {.tv_sec = 1, .tv_usec = 0};
  int bytes_read, msg_len = 0;

  bool healthy_sample = false;

  char peer_ip_addr_str[IP_ADDRESS_SZ + 1];

  char rawbuf[MAX_BUFF_SZ];
  char msg[MAX_BUFF_SZ];
  char *p,
       *end_of_msg;
  uint space_left = 0,
       bytes_to_grab = 0;

  // close the port cleanly when I ctrl+C this sumbitch
  signal(SIGINT, handle_sig);
  signal(SIGTERM, handle_sig);

  ostream = stdout;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
  {
      out(ostream, "Failed to create socket: %s\n", strerror(errno));
      return 1;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(LISTEN_PORT);

  if(bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
  {
      out(stderr, "Failed to bind socket: %s\n", strerror(errno));
      return 1;
  }

  listen(sockfd, 3);

  out(ostream, "listening on port %d\n", LISTEN_PORT);

  while(1)
  {
    clilen = sizeof(cli_addr);
    msg_len = 0;
    healthy_sample = false;
    memset(msg, 0, sizeof(msg));

    connfd = accept(sockfd, (struct sockaddr*)&cli_addr, &clilen);
    if (connfd < 0) {
      out(stderr, "bad response or something: %s\n", strerror(errno));
      // TODO: don't kill the daemon just because of one bad connection
      break;
    }
    setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, &sock_timeout_val, sizeof(sock_timeout_val));

    // get the remote peer (useful for debug)
    //peer_addr = (uint32_t)cli_addr.sin_addr.s_addr;
    memset(peer_ip_addr_str, 0, IP_ADDRESS_SZ + 1);
    inet_ntop(AF_INET, &cli_addr.sin_addr, peer_ip_addr_str, sizeof(peer_ip_addr_str));
    //snprintf(peer_ip_addr_str, IP_ADDRESS_SZ, "%d.%d.%d.%d", peer_addr & 0xff, (peer_addr >> 8) & 0xff, (peer_addr >> 16) & 0xff, (peer_addr >> 24) & 0xff);

    do {
      bytes_read = read(connfd, rawbuf, MAX_BUFF_SZ);
      // parse
      if(bytes_read > 0) {
        // how much sapce is left in the buffer?
        space_left = sizeof(msg) - msg_len - 1;

        // grab the whole buffer if we have room, else, just grab however much we have room for
        bytes_to_grab = (uint) umin(space_left, bytes_read);

        memcpy(&msg[msg_len], rawbuf, bytes_to_grab);
        msg_len += bytes_to_grab;
        end_of_msg = strchr(msg, '\n');
        if (end_of_msg) {
          // Message is healthy
          healthy_sample = true;
          break;
        }
        if (space_left == 0) {
          msg[sizeof(msg) - 1] = 0;
          out(stderr, "message too long from peer: %s\n", peer_ip_addr_str);
          out(stderr, "message: \"%s\"\n", msg);
          break;
        }
      } else if (bytes_read == 0) {
        // connection closed by remote peer. I think.
        break;
      } else {
        // socket error
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          out(stderr, "timed-out waiting for newline from peer: %s\n", peer_ip_addr_str);
          out(stderr, "  incomplete message: \"%s\"\n", msg);
        } else {
          out(stderr, "read error from peer: %s\n", peer_ip_addr_str);
          perror("  errno:");
        }
        break;
      }
    } while(1);
    close(connfd);

    if(healthy_sample) dispatch(msg, msg_len, peer_ip_addr_str);
  }
}
