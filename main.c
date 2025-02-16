#include <stdio.h>
#include <stdlib.h>
//#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
//#include <regex.h>
#include <errno.h>

#include "logger.h"

#define LISTEN_PORT  27910
#define MAX_BUFF_SZ  256
#define IP_ADDRESS_SZ  15  // 111.222.333.444

int sockfd;
int connfd;
FILE *ostream = NULL;

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
    uint32_t  peer_addr = 0;
    int bytes_read, msg_len = 0;

    char ip_addr[IP_ADDRESS_SZ + 1];  // +1 for null
    char peer_ip_addr_str[IP_ADDRESS_SZ + 1];

    char buf[MAX_BUFF_SZ];

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
        peer_addr = (uint32_t)cli_addr.sin_addr.s_addr;
        memset(peer_ip_addr_str, 0, IP_ADDRESS_SZ + 1);
        snprintf(peer_ip_addr_str, IP_ADDRESS_SZ, "%d.%d.%d.%d", peer_addr & 0xff, (peer_addr >> 8) & 0xff, (peer_addr >> 16) & 0xff, (peer_addr >> 24) & 0xff);
        //out(ostream, "Peer connected: %s\n", peer_ip_addr_str);
        memset(buf, 0, MAX_BUFF_SZ);
        if ((bytes_read = read(connfd, buf, MAX_BUFF_SZ)) > 0)
        {
            msg_len = strnlen(buf, MAX_BUFF_SZ);
            out(ostream, "%s: %s\n", peer_ip_addr_str, buf);
        }

        close(connfd);
    }

}
