#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <poll.h>
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

#define SOCKET_FD  0
#define PIPE_FD    1

#define WAIT_INDEFINITELY  -1

#define umin(x,y) (((uint64_t)(x) < (uint64_t)(y)) ? (x) : (y))


int sockfd = -1;
int connfd = -1;
int shutdown_pipe[] = {-1, -1};
FILE *ostream = NULL;

extern pthread_cond_t sessions_cv;
extern pthread_t scheduler_pthread;
extern bool shutdown_flag;
extern pthread_mutex_t shutdown_flag_lock_m;



void dispatch(const char *msg, uint32_t length, char *peer_ip_address);
int innit_scheduler();


// Signal handler to close the port cleanly if we get killed
void handle_sig(int sig)
{
  (void)sig;
  if (shutdown_pipe[1] != -1) {
    uint8_t foxes = 0xff;
    write(shutdown_pipe[1], &foxes, 1);
  }
}

static bool no_big_deal(int err)
{
  return (err == EINTR ||
          err == EAGAIN ||
          err == EWOULDBLOCK ||
          err == ENETDOWN ||
          err == EPROTO ||
          err == ENOPROTOOPT ||
          err == EHOSTDOWN ||
          err == ENONET ||
          err == EHOSTUNREACH ||
          err == EOPNOTSUPP ||
          err == ENETUNREACH);
}
static bool maybe_a_problem(int err)
{
  return (err == EMFILE ||
          err == ENFILE ||
          err == ENOBUFS ||
          err == ENOMEM);
}


int main ()
{
  struct sockaddr_in serv_addr, cli_addr;
  socklen_t  clilen;
  struct timeval sock_timeout_val = {.tv_sec = 1, .tv_usec = 0};
  int bytes_read, msg_len = 0;
  int ret = 0;

  bool healthy_sample = false;

  char peer_ip_addr_str[IP_ADDRESS_SZ + 1];

  char rawbuf[MAX_BUFF_SZ];
  char msg[MAX_BUFF_SZ];
  char *end_of_msg;
  uint32_t space_left = 0,
           bytes_to_grab = 0;

  // close the port cleanly when I ctrl+C this sumbitch
  struct sigaction sa = {.sa_handler = handle_sig};
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  ostream = stdout;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
      out(ostream, "Failed to create socket: %s\n", strerror(errno));
      return 1;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(LISTEN_PORT);

  if(bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
      out(stderr, "Failed to bind socket: %s\n", strerror(errno));
      return 1;
  }

  if(listen(sockfd, 3) != 0) {
      out(stderr, "Failed to listen on socket: %s\n", strerror(errno));
      return 1;
  }

  if(pipe(shutdown_pipe) != 0) {
    out(stderr, "Failed to create shutdown pipe: %s\n", strerror(errno));
    return 1;
  }

  if( (ret = innit_scheduler()) != 0) {
      out(stderr, "Failed to kick off the scheduler. Something in innit_scheduler() failed. ret = %d\n", ret);
      return ret;
  }

  struct pollfd polls[] =
  {
    [SOCKET_FD] = {.fd = sockfd,           .events = POLLIN, .revents = 0},
    [PIPE_FD]   = {.fd = shutdown_pipe[0], .events = POLLIN, .revents = 0}
  };


  out(ostream, "listening on port %d\n", LISTEN_PORT);

  while(1) {

    // reset the return events of the FDs we're listing to
    polls[SOCKET_FD].revents = 0;
    polls[PIPE_FD].revents = 0;

    // Wait for the socket or the pipe to squawk
    ret = poll(polls, 2, WAIT_INDEFINITELY);
    if(ret < 0) {
      if (errno == EINTR) continue;
      out(stderr, "wth? Pole broke!: %s\n", strerror(errno));
      break;
    }

    if(polls[PIPE_FD].revents & POLLIN) {
      // Somebody wrote to the pipe. As of this writing, that means
      // I hit ctrl+c or otherwise sent a sigint or sigterm to the process.
      // Drain the pipe and bail.
      uint8_t buf[32];
      read(polls[PIPE_FD].fd, buf, sizeof(buf));
      break;
    }

    if(!(polls[SOCKET_FD].revents & POLLIN)) continue;

    clilen = sizeof(cli_addr);
    msg_len = 0;
    healthy_sample = false;
    memset(msg, 0, sizeof(msg));

    connfd = accept(sockfd, (struct sockaddr*)&cli_addr, &clilen);
    if (connfd < 0) {
      int errno_temp = errno;
      if(no_big_deal(errno_temp)) continue;
      if(maybe_a_problem(errno_temp)) {
        out(stderr, "Warning: accept() returned %d: %s\n", errno_temp, strerror(errno_temp));
        // TODO: backoff?
        continue;
      } else {
        out(stderr, "Something bad happened trying to accept() the socket. %d: %s\n", errno_temp, strerror(errno_temp));
        break;
      }
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
    connfd = -1;

    if(healthy_sample) dispatch(msg, msg_len, peer_ip_addr_str);
  }

  // Tell the scheduler to wrap things up and exit

  // Set the shutdown flag
  pthread_mutex_lock(&shutdown_flag_lock_m);
  shutdown_flag = true;
  pthread_mutex_unlock(&shutdown_flag_lock_m);

  // kick the scheduler
  pthread_cond_broadcast(&sessions_cv);

  // wait for it to die
  pthread_join(scheduler_pthread, NULL);

  close(sockfd);
  close(shutdown_pipe[1]);
  close(shutdown_pipe[0]);
}
