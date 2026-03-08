#include <stdbool.h>
#include <pthread.h>

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
