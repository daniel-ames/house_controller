#include <stdbool.h>
#include <pthread.h>
#include <stdio.h>

#include "controller.h"


pthread_mutex_t panic_flag_lock_m = PTHREAD_MUTEX_INITIALIZER;
bool panic_flag;
void handle_sig();

void free_kvp_list(key_value_t *kvp)
{
  for(key_value_t *k = kvp; k; ) {
    key_value_t *next = k->next;
    free(k);
    k = next;
  }
}

void panic()
{
  // Set the panic flag
  pthread_mutex_lock(&panic_flag_lock_m);
  panic_flag = true;
  pthread_mutex_unlock(&panic_flag_lock_m);
  // hijack handle_sig to write to the shutdown pipe
  handle_sig(0);
}

bool there_is_a_panic()
{
  // Get the panic flag
  pthread_mutex_lock(&panic_flag_lock_m);
  bool panic_status = panic_flag;
  pthread_mutex_unlock(&panic_flag_lock_m);
  return panic_status;
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

  sprintf(out, "%02d:%02d:%02d %s", hour, time->tm_min, time->tm_sec, meridian);
}
