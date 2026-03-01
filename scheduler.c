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

typedef struct callback_list {
  handler_callback_t callback;
  void *ctx;
  struct callback_list *next;
} callback_list_t;

static uint32_t session_id_ctr = 0;
static pthread_mutex_t list_lock_m = PTHREAD_MUTEX_INITIALIZER;
static session_t *sessions = NULL;
static callback_list_t *callbacks = NULL;


uint32_t create_session(uint32_t inactivity_timeout, handler_callback_t callback)
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

void pet_the_dog(uint32_t session_id)
{
  session_t *s = sessions;
  struct timespec ts;

  pthread_mutex_lock(&list_lock_m);
  do {
    if(s->id == session_id) {
      clock_gettime(CLOCK_MONOTONIC, &ts);
      s->last_activity_time = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
      break;
    }
  } while( (s = s->next) );
  pthread_mutex_unlock(&list_lock_m);
}

static session_t* remove_this_session_and_get_the_next_one(uint32_t session_id)
{
  session_t *prev = NULL, *s = sessions, *new_next = NULL;

  do {
    if(s->id == session_id) {
      // remove the requested entry
      if(!prev) {
        // this is the first entry.
        if (!s->next) {
          // this is the ONLY entry
          sessions = NULL;
        } else {
          sessions = s->next;
          new_next = s->next;
        }
      } else {
        // This is not the first entry
        if(!s->next) {
          // This is the LAST entry
          prev->next = NULL;
        } else {
          // This is neither the first nor the last entry.
          // Connect the previous to the one after this
          prev->next = s->next;
          new_next = s->next;
        }
      }
      free(s);
      break;
    }
    prev = s;
  } while( (s = s->next) );

  return new_next;
}

static void push_cb(callback_list_t cb_in)
{
  callback_list_t *cbl = malloc(sizeof(*callbacks));
  static callback_list_t *prev = NULL;

  memcpy(cbl, &cb_in, sizeof(cb_in));
  cbl->next = NULL;
  if (!callbacks)
    // this is the first one
    callbacks = cbl;
  else
    // this is NOT the first one, so point the previous to this one
    prev->next = cbl;

  prev = cbl;
}

static callback_list_t pop_cb()
{
  callback_list_t cbl = {NULL, NULL, NULL};
  if (!callbacks) return cbl;
  cbl = *callbacks;
  callback_list_t *cbl_next = callbacks->next;
  free(callbacks);
  callbacks = cbl_next;
  return cbl;
}

void* scheduler_thread(void *ptr)
{
  session_t *s;
  struct timespec ts;
  uint64_t current_time = 0;
  callback_list_t cbl;

  // This is a busy loop that manages sessions
  while(1) {
    
    if(sessions) {
      pthread_mutex_lock(&list_lock_m);
      clock_gettime(CLOCK_MONOTONIC, &ts);
      current_time = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
      s = sessions;
      do {
        if(s->abort_session) {
          // Just remove the list entry.
          s = remove_this_session_and_get_the_next_one(s->id);
          
          // If that was the last entry, then don't attempt to continue walking the list
          if(!s) break;
          continue;
        }
        if(s->end_session) {
          // Queue the callback
          cbl.callback = s->callback;
          cbl.ctx = s->ctx;
          push_cb(cbl);
          // This session is done. Remove it.
          s = remove_this_session_and_get_the_next_one(s->id);
          if(!s) break;
          continue;
        }
        // No one is telling us to abort or end the session.
        // So we go by timeouts.
        if ( s->inactivity_timeout && (current_time - s->last_activity_time) > s->inactivity_timeout) s->end_session = true;

        s = s->next;
      } while(s);
      pthread_mutex_unlock(&list_lock_m);

      // Dequeue the callbacks. These callbacks must be ridiculously fast.
      // If they need to do more than a few instructions (and they always will),
      // then a callback should spawn a new thread to do the work, and return fast.
      // The scheduler is for scheduling, not sending email alerts.
      // TODO: instead of calling the callback, should we just spawn a thread for it here?
      cbl = pop_cb();
      while(cbl.callback) {
        cbl.callback(cbl.ctx);
        cbl = pop_cb();
      }
    }

    // give list modifiers a generous chance to make changes
    usleep(10);
  }
}