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

}