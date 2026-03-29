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


#define SP_INACTIVITY_TIMEOUT_MS   ((uint32_t)2000)


typedef struct sample {
  float amps;
  time_t timestamp;
  uint64_t mono_time_ns;
  uint64_t wall_time_ns;
  uint32_t elapsed_ms;
  int ordinal;
  struct sample *next;
} sp_sample_t;

typedef struct {
  sp_sample_t *head_sample;
  sp_sample_t *tail_sample;
  uint64_t start_ns;
  uint64_t stop_ns;
  uint32_t number_of_samples;
  uint32_t session_id;
} sewage_pump_ctx_t;

static bool session_active = false;
static pthread_mutex_t sewage_pump_lock_m = PTHREAD_MUTEX_INITIALIZER;


static void set_session_active(bool active)
{
  pthread_mutex_lock(&sewage_pump_lock_m);
  session_active = active;
  pthread_mutex_unlock(&sewage_pump_lock_m);
}

static bool try_claim_new_session()
{
  pthread_mutex_lock(&sewage_pump_lock_m);
  bool currently_active = session_active;
  bool new_session_claimed = false;
  if(!currently_active) {
    session_active = true;
    new_session_claimed = true;
  }
  pthread_mutex_unlock(&sewage_pump_lock_m);
  return new_session_claimed;
}

static void destroy_context(sewage_pump_ctx_t *ctx)
{
  sp_sample_t *next, *s = ctx->head_sample;
  
  while(s) {
    next = s->next;
    free(s);
    s = next;
  }
  free(ctx);
}

static void compile_measurement(summary_t *summary, sewage_pump_ctx_t *ctx)
{
  sp_sample_t *s = ctx->head_sample;
  if(!s) {
    out(stderr, "Panic: ctx->head_sample is NULL\n");
    panic();
    return;
  }
  int count = 0;
  float min = 1000.0f, max = 0.0f, sum = 0.0f;
  char influxdb_line_protocol[1024];

  __u_long start_time = (__u_long)s->timestamp, end_time;


  while(s) {
    if(s->amps > max) max = s->amps;
    if(s->amps < min) min = s->amps;
    sum += s->amps;
    count++;
    end_time = (__u_long)s->timestamp;
    // fprintf(fp, "%d %.1f\n", count, s->amps);
    snprintf(influxdb_line_protocol, sizeof(influxdb_line_protocol), "pump_sample,device=sewage_pump-monitor-1,site=underhouse,subsystem=sewage_pump,pump_type=well amps=%.1f,elapsed_ms=%ui,ordinal=%ui %lu",
                                s->amps, s->elapsed_ms, s->ordinal, s->wall_time_ns);
    write_to_db(influxdb_line_protocol);

    // on to the next
    s = s->next;
  }

  summary->min = min;
  summary->max = max;
  summary->samples = count;
  summary->average = sum / (float)count;
  summary->duration = end_time - start_time;
}


static void* sewage_pump_callback(void *ptr)
{
  sewage_pump_ctx_t *ctx = (sewage_pump_ctx_t*)ptr;
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
  snprintf(influxdb_line_protocol, sizeof(influxdb_line_protocol), "pump_run,device=sewage_pump-monitor-1,site=underhouse,subsystem=sewage_pump,pump_type=ejector max_amps=%.1f,avg_amps=%.1f,duration_s=%ld,samples=%di",
                                summary.max, summary.average, summary.duration, summary.samples);
  write_to_db(influxdb_line_protocol);

  destroy_context(ctx);

  // This function must return a void* to match the signture for pthread_create().
  // Return null so gcc doesn't complain.
  return NULL;
}


void sewage_pump_handler(key_value_t *kvp)
{
  time_t rawtime;
  struct timespec ts;
  uint64_t mono_time_ns, wall_time_ns;
  sp_sample_t *s;
  static sewage_pump_ctx_t *ctx;
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
    out(stderr, "Panic: could not malloc sp_sample_t!\n");
    panic();
    return;
  }
  memset(s, 0, sizeof(*s));

  if(try_claim_new_session()) {
    // This is a new session
    ctx = malloc(sizeof(*ctx));
    if(!ctx) {
      out(stderr, "Panic: could not malloc sewage pump context!\n");
      free_kvp_list(kvp);
      free(s);
      set_session_active(false);
      panic();
      return;
    }
    ctx->number_of_samples = 0;
    ctx->head_sample = s;
    ctx->tail_sample = NULL;
    ctx->session_id = create_session(SP_INACTIVITY_TIMEOUT_MS, sewage_pump_callback, ctx);
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
