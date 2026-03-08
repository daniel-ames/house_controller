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
  sample_t *head_sample;
  sample_t *tail_sample;
  uint32_t number_of_samples;
  uint32_t session_id;
} sewage_pump_ctx_t;

static volatile bool session_active = false;


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

static void destroy_context(sewage_pump_ctx_t *ctx)
{
  sample_t *next, *s = ctx->head_sample;
  
  do {
    next = s->next;
    free(s);
    s = next;
  } while(s != NULL);
  free(ctx);
}

static void cleanup_working_dir(char *temp_dir)
{
  char file_path[256] = {0};
  snprintf(file_path, sizeof(file_path), "%s/%s", temp_dir, MEASUREMENT_FILE);
  unlink(file_path);
  snprintf(file_path, sizeof(file_path), "%s/%s", temp_dir, PLOT_FILE);
  unlink(file_path);
  snprintf(file_path, sizeof(file_path), "%s/%s", temp_dir, "email");
  unlink(file_path);
  snprintf(file_path, sizeof(file_path), "%s/%s", temp_dir, "out.png");
  unlink(file_path);
  snprintf(file_path, sizeof(file_path), "%s/%s", temp_dir, "out.b64");
  unlink(file_path);
  snprintf(file_path, sizeof(file_path), "%s", temp_dir);
  rmdir(file_path);
}

static void compile_measurement(summary_t *summary, sewage_pump_ctx_t *ctx, char *temp_dir)
{
  sample_t *s = ctx->head_sample;
  // struct tm * timeinfo;
  // char time_str[16] = {0};  //12:44:55 AM\0\0\0\0
  int count = 0;
  float min = 1000.0f, max = 0.0f, sum = 0.0f;
  char plot_file_path[256] = {0};

  __u_long start_time = (__u_long)s->timestamp, end_time;

  snprintf(plot_file_path, sizeof(plot_file_path), "%s/%s", temp_dir, PLOT_FILE);

  FILE *fp = fopen(plot_file_path, "w");

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


static void* sewage_pump_callback(void *ptr)
{
  sewage_pump_ctx_t *ctx = (sewage_pump_ctx_t*)ptr;
  session_active = false;

  summary_t summary;
  char subject[256] = {0};
  char temp_dir[] = "_sp_XXXXXX";
  char measurement_file_path[256] = {0};
  char command[256] = {0};

  // create a unique temp working directory
  if(!mkdtemp(temp_dir)) {
    out(stderr, "Panic: Couldn't create temp dir \"%s\". %d: %s\n", temp_dir, errno, strerror(errno));
    panic();
    return NULL;
  }

  compile_measurement(&summary, ctx, temp_dir);

  out(stdout, "\nSummary:\n");
  out(stdout, "  min     : %f\n", summary.min);
  out(stdout, "  max     : %f\n", summary.max);
  out(stdout, "  average : %f\n", summary.average);
  out(stdout, "  samples : %d\n", summary.samples);
  out(stdout, "  duration: %lu\n\n", summary.duration);

  // Put the highlights in the subject line
  snprintf(subject, 256, "Flush - M:%.1f, A:%.1f, D:%ld", summary.max, summary.average, summary.duration);

  // write the results out to a file
  snprintf(measurement_file_path, sizeof(measurement_file_path), "%s/%s", temp_dir, MEASUREMENT_FILE);
  FILE *fp = fopen(measurement_file_path, "w");
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

  snprintf(command, sizeof(command), "./sendit.sh %s", temp_dir);
  system(command);

  destroy_context(ctx);

  cleanup_working_dir(temp_dir);

  // This function must return a void* to match the signture for pthread_create().
  // Return null so gcc doesn't complain.
  return NULL;
}


void sewage_pump_handler(key_value_t *kvp)
{
  time_t rawtime;
  sample_t *s;
  static sewage_pump_ctx_t *ctx;
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
    ctx = malloc(sizeof(*ctx));
    ctx->number_of_samples = 0;
    ctx->head_sample = s;
    ctx->tail_sample = NULL;
    ctx->session_id = create_session(SP_INACTIVITY_TIMEOUT_MS, sewage_pump_callback, ctx);

    if(!ctx->session_id) {
      // No session id was issued.
      // We are probably in the middle of a shutdown.
      // Throw this session away.
      free_kvp_list(kvp);
      free(s);
      free(ctx);
      session_active = false;
      return;
    }
    
    timeinfo = localtime(&rawtime);
    time_str = asctime(timeinfo);
    // kill the trailing \n from the stupid date-time string
    while(time_str[index] != '\n') index++;
    time_str[index] = 0;

    out(stdout, "[%s] Flush started", time_str);
  }

  // Tell the scheduler we're actively getting readings from the device
  if(!pet_the_dog(ctx->session_id)) {
    // This is highly improbable, but not impossible.
    // This could be a blip. If it is, I don't mind just tossing it.
    // If it really is the start of a new flush faster than a new session could be created for it, then:
    //   1. Something is wrong with the pump. IMPORTANT TODO: write an alert path for this.
    //   2. More samples will follow this one, and by then, this improbable gap
    //      between the last session ending and a new session beginning, will have
    //      passed, and we won't land in here for those next samples.
    // That said, whether it's a blip or a new session, we can reasonably dontcare this sample.
    free(s);
    free_kvp_list(kvp);
    return;
  }

  if (ctx->tail_sample)
    ctx->tail_sample->next = s;

  memcpy(&s->timestamp, &rawtime, sizeof(rawtime));
  s->ordinal = ++ctx->number_of_samples;
  s->next = NULL;

  // Now parse
  for(key_value_t *k = kvp; k; k = k->next) {
    if (k->key == amps_type) {
      // This is what we came for
      s->amps = k->value.dbl;
      break;
    }
  }

  // It is the handler's responsibility to free kvp items
  free_kvp_list(kvp);

  ctx->tail_sample = s;
  out(stdout, ".");
  fflush(stdout);
}
