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
#include "db.h"


#define WH_INACTIVITY_TIMEOUT_MS   ((uint32_t)2000)

typedef struct sample {
  float amps_x;
  float amps_y;
  time_t timestamp;
  uint64_t mono_time_ns;
  uint64_t wall_time_ns;
  uint32_t elapsed_ms;
  int ordinal;
  struct sample *next;
} wh_sample_t;

typedef struct {
  wh_sample_t *head_sample;
  wh_sample_t *tail_sample;
  uint64_t start_ns;
  uint64_t stop_ns;
  uint32_t number_of_samples;
  uint32_t session_id;
} wellhouse_ctx_t;

static bool session_active = false;
static pthread_mutex_t wellhouse_lock_m = PTHREAD_MUTEX_INITIALIZER;

static void set_session_active(bool active)
{
  pthread_mutex_lock(&wellhouse_lock_m);
  session_active = active;
  pthread_mutex_unlock(&wellhouse_lock_m);
}

static bool try_claim_new_session()
{
  pthread_mutex_lock(&wellhouse_lock_m);
  bool currently_active = session_active;
  bool new_session_claimed = false;
  if(!currently_active) {
    session_active = true;
    new_session_claimed = true;
  }
  pthread_mutex_unlock(&wellhouse_lock_m);
  return new_session_claimed;
}

static void destroy_context(wellhouse_ctx_t *ctx)
{
  wh_sample_t *next, *s = ctx->head_sample;
  
  while(s) {
    next = s->next;
    free(s);
    s = next;
  }
  free(ctx);
}


static void compile_measurement(summary_t *summary, wellhouse_ctx_t *ctx)
{
  wh_sample_t *s = ctx->head_sample;
  if(!s) {
    out(stderr, "Panic: ctx->head_sample is NULL\n");
    panic();
    return;
  }
  int count = 0;
  float min_x = 1000.0f, max_x = 0.0f, sum_x = 0.0f;
  float min_y = 1000.0f, max_y = 0.0f, sum_y = 0.0f;
  char influxdb_line_protocol[1024];

  __u_long start_time = (__u_long)s->timestamp, end_time;

  while(s) {
    if(s->amps_x > max_x) max_x = s->amps_x;
    if(s->amps_x < min_x) min_x = s->amps_x;
    sum_x += s->amps_x;

    if(s->amps_y > max_y) max_y = s->amps_y;
    if(s->amps_y < min_y) min_y = s->amps_y;
    sum_y += s->amps_y;
    
    count++;
    end_time = (__u_long)s->timestamp;

    snprintf(influxdb_line_protocol, sizeof(influxdb_line_protocol), "pump_sample,device=wellhouse-monitor-1,site=wellhouse,subsystem=well_pump,pump_type=well amps_x=%.1f,amps_y=%.1f,elapsed_ms=%ui,ordinal=%ui %lu",
                                s->amps_x, s->amps_y, s->elapsed_ms, s->ordinal, s->wall_time_ns);
    write_to_db(influxdb_line_protocol);

    // on to the next
    s = s->next;
  }

  summary->min = min_x < min_y ? min_x : min_y;
  summary->max = max_x > max_y ? max_x : max_y;
  summary->samples = count;
  summary->average = ((sum_x / (float)count) + (sum_y / (float)count)) / 2.0f;
  summary->duration = end_time - start_time;
}


static void* wellhouse_callback(void *ptr)
{
  wellhouse_ctx_t *ctx = (wellhouse_ctx_t*)ptr;
  set_session_active(false);

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ctx->stop_ns = ts.tv_sec * 1000000000 + ts.tv_nsec;

  summary_t summary;
  char influxdb_line_protocol[1024];

  compile_measurement(&summary, ctx);

  out(stdout, "\nSummary:\n");
  out(stdout, "  min     : %f\n", summary.min);
  out(stdout, "  max     : %f\n", summary.max);
  out(stdout, "  average : %f\n", summary.average);
  out(stdout, "  samples : %d\n", summary.samples);
  out(stdout, "  duration: %lu\n\n", summary.duration);

  // The first part of the line protocol is tags. The second part (after the space) is fields.
  snprintf(influxdb_line_protocol, sizeof(influxdb_line_protocol), "pump_run,device=wellhouse-monitor-1,site=wellhouse,subsystem=well_pump,pump_type=well max_amps=%.1f,avg_amps=%.1f,duration_s=%ld,samples=%di,start_ns=%lui,stop_ns=%lui",
                                summary.max, summary.average, summary.duration, summary.samples, ctx->start_ns, ctx->stop_ns);
  write_to_db(influxdb_line_protocol);

  destroy_context(ctx);

  // This function must return a void* to match the signture for pthread_create().
  // Return null so gcc doesn't complain.
  return NULL;
}


void wellhouse_handler(key_value_t *kvp)
{
  time_t rawtime;
  struct timespec ts;
  uint64_t mono_time_ns, wall_time_ns;
  wh_sample_t *s;
  static wellhouse_ctx_t *ctx;
  struct tm * timeinfo;
  char *time_str;
  int index = 0;

  // get time
  time(&rawtime);
  clock_gettime(CLOCK_MONOTONIC, &ts);
  mono_time_ns = ts.tv_sec * 1000000000 + ts.tv_nsec;
  clock_gettime(CLOCK_REALTIME, &ts);
  wall_time_ns = ts.tv_sec * 1000000000 + ts.tv_nsec;

  s = malloc(sizeof(*s));
  if(!s) {
    out(stderr, "Panic: could not malloc wh_sample_t!\n");
    panic();
    return;
  }
  memset(s, 0, sizeof(*s));

  if(try_claim_new_session()) {
    // This is a new session
    ctx = malloc(sizeof(*ctx));
    if(!ctx) {
      out(stderr, "Panic: could not malloc wellhouse context!\n");
      free_kvp_list(kvp);
      free(s);
      set_session_active(false);
      panic();
      return;
    }
    ctx->number_of_samples = 0;
    ctx->head_sample = s;
    ctx->tail_sample = NULL;
    ctx->session_id = create_session(WH_INACTIVITY_TIMEOUT_MS, wellhouse_callback, ctx);
    ctx->start_ns = wall_time_ns;

    if(!ctx->session_id) {
      // No session id was issued.
      // We are probably in the middle of a shutdown.
      // Throw this session away.
      free_kvp_list(kvp);
      free(s);
      free(ctx);
      set_session_active(false);
      return;
    }
    
    timeinfo = localtime(&rawtime);
    time_str = asctime(timeinfo);
    // kill the trailing \n from the stupid date-time string
    while(time_str[index] != '\n') index++;
    time_str[index] = 0;

    out(stdout, "[%s] Well pump started", time_str);
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

  s->mono_time_ns = mono_time_ns;
  s->wall_time_ns = wall_time_ns;
  s->elapsed_ms = (uint32_t)((wall_time_ns - ctx->start_ns) / 1000000ull);
  memcpy(&s->timestamp, &rawtime, sizeof(rawtime));

  if(ctx->tail_sample)
    ctx->tail_sample->next = s;

  s->ordinal = ++ctx->number_of_samples;
  s->next = NULL;

  // Now parse
  for(key_value_t *k = kvp; k; k = k->next) {
    if (k->key == ampsx_type) {
      // This is what we came for
      s->amps_x = k->value.dbl;
      continue;
    }
    if (k->key == ampsy_type) {
      // This is what we came for
      s->amps_y = k->value.dbl;
      continue;
    }
  }

  // It is the handler's responsibility to free kvp items
  free_kvp_list(kvp);

  ctx->tail_sample = s;
  out(stdout, "+");
  fflush(stdout);
}
