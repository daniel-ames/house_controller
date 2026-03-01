#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
// #include <sys/queue.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "controller.h"
#include "scheduler.h"
#include "logger.h"

static uint32_t session_id_ctr = 0;
static pthread_mutex_t list_lock_m = PTHREAD_MUTEX_INITIALIZER;
static session_t *sessions = NULL;


uint32_t create_session(uint32_t inactivity_timeout, handler_callback_t callback, void* ctx)
{
  session_t *s;
  session_t *new_session = malloc(sizeof(session_t));
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);

  new_session->id = session_id_ctr++;
  new_session->inactivity_timeout = inactivity_timeout;
  new_session->last_activity_time = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
  new_session->abort_session = false;
  new_session->end_session = false;
  new_session->ctx = ctx;
  new_session->callback = callback;
  new_session->next = NULL;

  pthread_mutex_lock(&list_lock_m);
  // scheduler is paused
  if(!sessions) {
    sessions = new_session;
  } else {
    s = sessions;
    while(s->next) s = s->next;
    // This is the last entry. Tack onto it
    s->next = new_session;
  }
  // release the scheduler
  pthread_mutex_unlock(&list_lock_m);
  return new_session->id;
}

bool pet_the_dog(uint32_t session_id)
{
  struct timespec ts;
  bool dog_was_petted = false;

  pthread_mutex_lock(&list_lock_m);
  for(session_t *s = sessions; s; s = s->next) {
    if(s->id == session_id) {
      clock_gettime(CLOCK_MONOTONIC, &ts);
      s->last_activity_time = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
      dog_was_petted = true;
      break;
    }
  }
  pthread_mutex_unlock(&list_lock_m);
  return dog_was_petted;
}


void* scheduler_thread(void *ptr)
{
  struct timespec ts;
  uint64_t current_time = 0;
  // session_t session;
  session_t **ptr_to_link, *s, *sessions_to_finalize = NULL;

  // This is a busy loop that manages sessions
  while(1) {
    pthread_mutex_lock(&list_lock_m);
    if(sessions) {
      clock_gettime(CLOCK_MONOTONIC, &ts);
      current_time = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
      ptr_to_link = &sessions;

      while(*ptr_to_link) {
        s = *ptr_to_link;
        if(s->abort_session || s->end_session ||
           ( s->inactivity_timeout && (current_time - s->last_activity_time) > s->inactivity_timeout)) {
          
          // detatch this session from the active list by pointing this items 'next'
          // to the next item's 'next'. i think...
          *ptr_to_link = s->next;
          // stitch this session to the finalize list
          s->next = sessions_to_finalize;
          sessions_to_finalize = s;
          continue;
        }

        ptr_to_link = &s->next;
      };
      pthread_mutex_unlock(&list_lock_m);

      // Service the callbacks. These callbacks must be ridiculously fast.
      // If they need to do more than a few instructions (and they always will),
      // then a callback should spawn a new thread to do the work, and return fast.
      // The scheduler is for scheduling, not sending email alerts.
      // TODO: instead of calling the callback, should we just spawn a thread for it here?
      while(sessions_to_finalize) {
        s = sessions_to_finalize;
        sessions_to_finalize = sessions_to_finalize->next;
        if(!s->abort_session && s->callback)
          s->callback(s->ctx);
        free(s);
      }
    } else {
      pthread_mutex_unlock(&list_lock_m);
      // give list modifiers a generous chance to make changes
      usleep(10);
    }
  }
}