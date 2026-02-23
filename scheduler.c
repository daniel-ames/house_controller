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
static volatile bool list_is_locked = false;

static session_t *sessions = NULL;

static inline void get_list_lock()
{
  while(list_is_locked);
  list_is_locked = true;
}
static inline void release_list_lock()
{
  list_is_locked = false;
}


uint32_t create_session(uint32_t inactivity_timeout, void *callback)
{
  session_t *s;
  session_t *new_session = malloc(sizeof(session_t));

  get_list_lock();

  new_session->id = session_id_ctr++;
  new_session->inactivity_timeout = inactivity_timeout;
  new_session->abort_session = false;
  new_session->end_session = false;
  new_session->next = NULL;

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
  release_list_lock();
}

void pet_the_dog(uint32_t session_id)
{
  session_t *s = sessions;
  struct timespec ts;

  get_list_lock();
  do {
    if(s->id == session_id) {
      clock_gettime(CLOCK_MONOTONIC, &ts);
      s->last_activity_time = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
      break;
    }
  } while( (s = s->next) );
  release_list_lock();
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
  } while( (s = s->next) );

  return new_next;
}

void* scheduler_thread(void *ptr)
{
  session_t *s;
  struct timespec ts;
  uint64_t current_time = 0;

  // This is a busy loop that manages sessions
  while(1) {
    
    if(sessions) {
      get_list_lock();
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
          // Fire the callback
          (s->callback)(s);
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
      release_list_lock();
    }

    // give list modifiers a generous chance to make changes
    usleep(10);
  }
}