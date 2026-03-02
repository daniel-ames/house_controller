#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "controller.h"
#include "scheduler.h"
#include "logger.h"


#define SP_INACTIVITY_TIMEOUT_MS   ((uint32_t)2000)


typedef struct {
  sample_t *samples;
} sewage_pump_ctx_t;


static uint32_t samples = 0;
static bool session_active = false;
static uint32_t session_id = 0;
static sewage_pump_ctx_t ctx;


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
    sample_t *next, *s = ctx.samples;
    
    do {
        next = s->next;
        free(s);
        s = next;
    } while(s != NULL);
}

static void compile_measurement(summary_t *summary)
{
    sample_t *s = ctx.samples;
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
    } while(s);

    fflush(fp);
    fclose(fp);

    summary->min = min;
    summary->max = max;
    summary->samples = count;
    summary->average = sum / (float)count;
    summary->duration = end_time - start_time;
}


void* sewage_pump_callback(void *ptr)
{
    summary_t summary;
    char subject[256] = {0};

    compile_measurement(&summary);

    out(stdout, "\nSummary:\n");
    out(stdout, "  min     : %f\n", summary.min);
    out(stdout, "  max     : %f\n", summary.max);
    out(stdout, "  average : %f\n", summary.average);
    out(stdout, "  samples : %d\n", summary.samples);
    out(stdout, "  duration: %lu\n\n", summary.duration);

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
    session_active = false;

    // This function must return a void* to match the signture for pthread_create().
    // Return null so gcc doesn't complain.
    return (void*)0;
}


void sewage_pump_handler(key_value_t *kvp)
{
  time_t rawtime;
  sample_t *s;
  static sample_t *s_prev;
  struct tm * timeinfo;
  char *time_str;
  int index = 0;

  // get time
  time(&rawtime);

  s = malloc(sizeof(*s));
  memset(s, 0, sizeof(*s));

  if(!session_active) {
    // This is a new session
    session_active = true;
    samples = 1;
    ctx.samples = s;
    s_prev = NULL;
    session_id = create_session(SP_INACTIVITY_TIMEOUT_MS, sewage_pump_callback, NULL);
    
    timeinfo = localtime(&rawtime);
    time_str = asctime(timeinfo);
    // kill the trailing \n from the stupid date-time string
    while(time_str[index] != '\n') index++;
    time_str[index] = 0;

    out(stdout, "[%s] Flush started", time_str);
  }

  // Tell the scheduler we're actively getting readings from the device
  pet_the_dog(session_id);

  if (s_prev)
    s_prev->next = s;

  memcpy(&s->timestamp, &rawtime, sizeof(rawtime));
  s->ordinal = samples++;
  s->next = NULL;

  // Now parse
  for(key_value_t *k = kvp; k; ) {
    key_value_t *next = k->next;
    if (k->key == amps_type) {
      // This is what we came for
      s->amps = k->value.dbl;
    }
    // It is the handler's responsibility to free kvp items
    free(k);
    k = next;
  }

  s_prev = s;
  out(stdout, ".");
  fflush(stdout);
}
