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

static uint samples = 0;
static bool session_active = false;
static uint32_t session_id = 0;

void compile_measurement_thread()
{
  // TODO: put all the collected data together and act on it (usually just email it)
}

static void sewage_pump_callback(void *ctx)
{
  session_active = false;

  // todo: create thread that runs compile_measurement_thread().
  // It will involve sending alerts so it could take a while.
}

void sewage_pump_handler(key_value_t *kvp)
{
  if(!session_active) {
    // This is a new session
    session_active = true;
    session_id = create_session(SP_INACTIVITY_TIMEOUT_MS, sewage_pump_callback, NULL);
  }

  // Tell the scheduler we're actively getting readings from the device
  pet_the_dog(session_id);

  // TODO: parse key value pairs and record them
#if 0
        samples++;

        // get time
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        time_str = asctime(timeinfo);
        // kill the trailing \n from the stupid date-time string
        index = 0;
        while(time_str[index] != '\n') index++;
        time_str[index] = 0;

        if (thread_working)
            // The child is still working. Just toss the sample.
            continue;

        s = malloc(sizeof(*s));
        memset(s, 0, sizeof(*s));
        if (!session) {
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
        p = strchr(msg, ':');
        p++;
        s->amps = strtof(p, NULL);
        s_prev = s;
        out(ostream, ".");
        fflush(stdout);
#endif

}