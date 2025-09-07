#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "controller.h"
#include "logger.h"

#define LISTEN_PORT  27910
#define MAX_BUFF_SZ  256
#define IP_ADDRESS_SZ  15  // 111.222.333.444


int sockfd;
int connfd;
FILE *ostream = NULL;
volatile int samples = 0;
volatile int session = 0;
volatile int thread_working = 0;

sample_t *sample_head, *s_prev = NULL;

// Signal handler to close the port cleanly if we get killed
void handle_sig(int sig)
{
    close(connfd);
    close(sockfd);
    //logger_handle_sig();
    exit(0);
}

// make the time look like: 3:45:24 PM
void time_my_way(struct tm * time, char * out)
{
    int hour = 0;
    char meridian[3] = {0};
    if (time->tm_hour > 12) {
        meridian[0] = 'P';
        hour = time->tm_hour - 12;
    }
    else {
        meridian[0] = 'A';
        hour = time->tm_hour == 0 ? 12 : time->tm_hour;
    }

    meridian[1] = 'M';

    sprintf(out, "%d:%d:%d %s", hour, time->tm_min, time->tm_sec, meridian);
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

void compile_measurement(summary_t *summary)
{
    sample_t *next, *s = sample_head;
    struct tm * timeinfo;
    char time_str[16] = {0};  //12:44:55 AM\0\0\0\0
    int count = 0;
    float min = 1000.0f, max = 0.0f, sum = 0.0f;

    __u_long start_time = (__u_long)s->timestamp, end_time;

    FILE *fp = fopen(PLOT_FILE, "w");

    do {
        // // parse the time
        // timeinfo = localtime(&s->timestamp);
        // time_my_way(timeinfo, time_str);

        // // show it (optional)
        // printf("list item [%d], time: %s, amps: %f\n", s->ordinal, time_str, s->amps);

        if(s->amps > max) max = s->amps;
        if(s->amps < min) min = s->amps;
        sum += s->amps;
        count++;
        end_time = (__u_long)s->timestamp;
        fprintf(fp, "%d %.1f\n", count, s->amps);

        // on to the next
        s = s->next;
    } while(s != NULL);

    fflush(fp);
    fclose(fp);

    summary->min = min;
    summary->max = max;
    summary->samples = count;
    summary->average = sum / (float)count;
    summary->duration = end_time - start_time;
}


void* thread_func(void * ptr)
{
    time_t rawtime;
    struct tm * timeinfo;
    int timeout = 10;
    int prev_counter = samples;
    summary_t summary;
    char subject[256] = {0};
    char *time_str;
    int index = 0;
    
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    time_str = asctime(timeinfo);
    // kill the trailing \n from the stupid date-time string
    while(time_str[index] != '\n') index++;
    time_str[index] = 0;

    out(ostream, "[%s] Flush started", time_str);
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

    // fix: power blips cause the esp32 to send like one packet, then time-out, then send another
    // while this thread is still buttoning things up. The parent thread then starts trying to use
    // the linked-list, often while this child is trying to deallocate it. Crash!
    // Use thread_working to tell the parent to hold it's dadgum horses.
    thread_working = 1;

    if (session) {
      // pump is still on after 10 seconds. something might be wrong.
      printf("\n-------- timeout\n");
      session = 0;
    }
    else {
    //   printf("\n-------- end\n");
    }

    compile_measurement(&summary);

    out(ostream, "\nSummary:\n");
    out(ostream, "  min     : %f\n", summary.min);
    out(ostream, "  max     : %f\n", summary.max);
    out(ostream, "  average : %f\n", summary.average);
    out(ostream, "  samples : %d\n", summary.samples);
    out(ostream, "  duration: %lu\n\n", summary.duration);

    // Put the highlights in the subject line
    sprintf(subject, "Flush - M:%.1f, A:%.1f, D:%ld", summary.max, summary.average, summary.duration);

    // write the results out to a file
    FILE *fp = fopen(MEASUREMENT_FILE, "w");
    fprintf(fp, "To: danieladamames@gmail.com\r\n");
    fprintf(fp, "From: ameshousecontroller@gmail.com\r\n");
    fprintf(fp, "Subject: %s\r\n", subject);
    fprintf(fp, "MIME-Version: 1.0\r\n");
    fprintf(fp, "Content-Type: multipart/related; boundary=\"xxxx38th parallel\"\r\n");
    fprintf(fp, "\r\n");
    fprintf(fp, "This is a multipart message in MIME format.\r\n");
    fprintf(fp, "\r\n");
    fprintf(fp, "--xxxx38th parallel\r\n");
    fprintf(fp, "Content-Type: text/html; charset=\"UTF-8\"\r\n");
    fprintf(fp, "\r\n");
    fprintf(fp, "<p style=\"white-space: pre;\">\r\n");
    fprintf(fp, "max/average/samples/duration: %.2f/%.2f/%d/%lu\r\n", summary.max, summary.average, summary.samples, summary.duration);
    fprintf(fp, "</p>\r\n");
    fprintf(fp, "<img src=\"cid:foo_bar\" alt=\"graph\">\r\n");
    fprintf(fp, "\r\n");
    fprintf(fp, "--xxxx38th parallel\r\n");
    fprintf(fp, "Content-Type: image/png; name=\"pic.png\"\r\n");
    fprintf(fp, "Content-Disposition: attachment; filename=\"pic.png\"\r\n");
    fprintf(fp, "Content-Transfer-Encoding: base64\r\n");
    fprintf(fp, "X-Attachment-Id: foo_bar\r\n");
    fprintf(fp, "Content-ID: <foo_bar>\r\n");
    fprintf(fp, "\r\n");
    fflush(fp);
    fclose(fp);

    system("./sendit.sh");

    clean_list();
    s_prev = NULL;
    samples = 0;

    thread_working = 0;

    // This function must return a void* to match the signture for pthread_create().
    // Return null so gcc doesn't complain.
    return (void*)0;
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
    char *p;
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
            if (thread_working)
                // The child is still working. Just toss the sample.
                continue;

            s = malloc(sizeof(*s));
            memset(s, 0, sizeof(*s));
            if (!session)
            {
                out(ostream, "From %s\n", peer_ip_addr_str);
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
            p = strchr(buf, ':');
            p++;
            s->amps = strtof(p, NULL);
            s_prev = s;
            out(ostream, ".");
            fflush(stdout);
        }

        close(connfd);
    }

}
