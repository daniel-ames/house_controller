#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
//#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
//#include <time.h>
//#include <regex.h>
#include <errno.h>
#include <pthread.h>

#include "controller.h"
#include "logger.h"

#define LISTEN_PORT  27910
#define MAX_BUFF_SZ  256
#define IP_ADDRESS_SZ  15  // 111.222.333.444

int send_email();

int sockfd;
int connfd;
FILE *ostream = NULL;
volatile int samples = 0;
volatile int session = 0;

sample_t *sample_head, *s_prev = NULL;

// Signal handler to close the port cleanly if we get killed
void handle_sig(int sig)
{
    close(connfd);
    close(sockfd);
    //logger_handle_sig();
    exit(0);
}

void clean_list()
{
    sample_t *next, *s = sample_head;
    
    do {
        next = s->next;
        free(s);
        s = next;
    } while(s != NULL);
}

void show_list()
{
    sample_t *next, *s = sample_head;
    struct tm * timeinfo;
    char *time_str;
    int index = 0;
    
    do {
        // parse the time
        timeinfo = localtime(&s->timestamp);
        time_str = asctime(timeinfo);
        // kill the trailing \n from the stupid date-time string
        index = 0;
        while(time_str[index] != '\n') index++;
        time_str[index] = 0;

        // Now show stuff
        printf("list item [%d]: %s\n", s->ordinal, time_str);
        
        // on to the next
        s = s->next;
    } while(s != NULL);
}

void* thread_func(void * ptr)
{
    int timeout = 10;
    int prev_counter = samples;
    printf("-------- start\n");
    fflush(stdout);
    while(timeout--)
    {
        sleep(1);
        if(samples > prev_counter)
          prev_counter = samples;
        else {
          session = 0;
          break;
        }
    }

    if (session) {
      // pump is still on after 10 seconds. something might be wrong.
      printf("\n-------- timeout\n");
      session = 0;
    }
    else {
      printf("\n-------- end\n");

    }

    show_list();

    fflush(stdout);
    clean_list();
    s_prev = NULL;
    samples = 0;
}

int main ()
{
    time_t rawtime;
    struct tm * timeinfo;

    int res;
    pthread_attr_t attr;
    pthread_t thread;

    struct sockaddr_in serv_addr, cli_addr;
    socklen_t  clilen;
    uint32_t  peer_addr = 0;
    int bytes_read, msg_len = 0;

    sample_t *s;

    char ip_addr[IP_ADDRESS_SZ + 1];  // +1 for null
    char peer_ip_addr_str[IP_ADDRESS_SZ + 1];

    char buf[MAX_BUFF_SZ];
    char *time_str;
    int index = 0;

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
        connfd = accept(sockfd, (struct sockaddr*)&cli_addr, &clilen);
        if (connfd < 0)
        {
            out(ostream, "bad response or something: %s\n", strerror(errno));
            break;
        }
        samples++;
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        time_str = asctime(timeinfo);
        // kill the trailing \n from the stupid date-time string
        index = 0;
        while(time_str[index] != '\n') index++;
        time_str[index] = 0;

        peer_addr = (uint32_t)cli_addr.sin_addr.s_addr;
        memset(peer_ip_addr_str, 0, IP_ADDRESS_SZ + 1);
        snprintf(peer_ip_addr_str, IP_ADDRESS_SZ, "%d.%d.%d.%d", peer_addr & 0xff, (peer_addr >> 8) & 0xff, (peer_addr >> 16) & 0xff, (peer_addr >> 24) & 0xff);
        memset(buf, 0, MAX_BUFF_SZ);
        if ((bytes_read = read(connfd, buf, MAX_BUFF_SZ)) > 0)
        {
            out(ostream, "%s, %s, %s\n", time_str, peer_ip_addr_str, buf);
            fflush(stdout);

            s = malloc(sizeof(*s));
            memset(s, 0, sizeof(*s));
            if (!session)
            {
                sample_head = s;
                session = 1;
                res = pthread_attr_init(&attr);
                if(res == -1) printf("%d\n", __LINE__);
                res = pthread_create(&thread, &attr, thread_func, NULL);
                if(res == -1) printf("%d\n", __LINE__);
                pthread_attr_destroy(&attr);
            }

            if (s_prev != NULL) {
              s_prev->next = s;
            }
            memcpy(&s->timestamp, &rawtime, sizeof(rawtime));
            s->ordinal = samples - 1;
            s->next = NULL;
            // TODO: set the amps
            s_prev = s;
        }

        close(connfd);
    }

}
