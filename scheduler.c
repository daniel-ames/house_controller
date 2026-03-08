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

static uint32_t session_id_ctr = 1;

pthread_t scheduler_pthread;

bool shutdown_flag = false;
pthread_mutex_t scheduler_lock_m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t sessions_cv;

static session_t *sessions = NULL;

void* scheduler_thread(void *ptr);

// (pronounced innit SHED-djyooler, because I watch too much bri-ish tele, innit?)
int innit_scheduler()
{
  int ret = 0;
  pthread_condattr_t attr_cv;
  ret |= pthread_condattr_init(&attr_cv);
  ret |= pthread_condattr_setclock(&attr_cv, CLOCK_MONOTONIC);
  ret |= pthread_cond_init(&sessions_cv, &attr_cv);
  ret |= pthread_condattr_destroy(&attr_cv);

  // kickoff the SHEDuler
  pthread_attr_t attr_thread;
  ret |= pthread_attr_init(&attr_thread);
  ret |= pthread_create(&scheduler_pthread, &attr_thread, scheduler_thread, NULL);
  ret |= pthread_attr_destroy(&attr_thread);

  // Yes, I know (ret == !0) can mean 7 different things here. I'm fine with that.
  // I just want any errors to latch to keep the code cleaner. If there IS a latch,
  // THEN I'll come in here and start digging.
  return ret;
}


static bool i_dont_have_the_lock_and_shutdown_is_requested()
{
  // Hey future-daniel, DO NOT call this function from code that already has
  // the scheduler_lock_m lock! If you do, the following pthread_mutex_lock()
  // will deadlock, and you will be a sad panda.
  // I know what you're thinking: Yes, you CAN make it to where it will return
  // an error (EDEADLK) instead of just lock, but Sprocket got all preachy when 
  // I suggested that. Something about "using a smoke alarm as a kitchen timer"..
  // iono, but she was very persuasive.
  pthread_mutex_lock(&scheduler_lock_m);
  bool shutdown = shutdown_flag;
  pthread_mutex_unlock(&scheduler_lock_m);
  return shutdown;
}


uint32_t create_session(uint32_t inactivity_timeout, handler_callback_t callback, void* ctx)
{
  session_t *s, *new_session;
  struct timespec ts;

  pthread_mutex_lock(&scheduler_lock_m);
  if(shutdown_flag) {
    pthread_mutex_unlock(&scheduler_lock_m);
    return 0;
  }

  new_session = malloc(sizeof(session_t));
  if(!new_session) {
    out(stderr, "Panic: Could not malloc new_session!\n");
    pthread_mutex_unlock(&scheduler_lock_m);
    panic();
    return 0;
  }

  clock_gettime(CLOCK_MONOTONIC, &ts);

  new_session->id = session_id_ctr++;
  new_session->inactivity_timeout = inactivity_timeout;
  new_session->last_activity_time = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
  new_session->abort_session = false;
  new_session->end_session = false;
  new_session->ctx = ctx;
  new_session->callback = callback;
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
  pthread_cond_signal(&sessions_cv);
  pthread_mutex_unlock(&scheduler_lock_m);
  return new_session->id;
}

bool pet_the_dog(uint32_t session_id)
{
  struct timespec ts;
  bool dog_was_petted = false;

  pthread_mutex_lock(&scheduler_lock_m);
  for(session_t *s = sessions; s; s = s->next) {
    if(s->id == session_id) {
      clock_gettime(CLOCK_MONOTONIC, &ts);
      s->last_activity_time = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
      dog_was_petted = true;
      break;
    }
  }
  pthread_mutex_unlock(&scheduler_lock_m);
  return dog_was_petted;
}

void* scheduler_thread(void *ptr)
{
  (void)ptr;
  struct timespec ts;
  uint64_t current_time = 0, next_deadline = 0;;
  session_t **ptr_to_link, *s, *sessions_to_finalize = NULL;
  int ret = 0;
  bool there_is_a_deadline = false;

  // This is a busy loop that manages sessions
  while(1) {
    pthread_mutex_lock(&scheduler_lock_m);
    while(1) {
      next_deadline = 0;
      there_is_a_deadline = false;
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

        // When is the next expiration?
        if(s->inactivity_timeout) {
          // This one has a timeout. When is it?
          uint64_t this_deadline = s->last_activity_time + s->inactivity_timeout;
          if(!there_is_a_deadline || this_deadline < next_deadline) {
            there_is_a_deadline = true;
            next_deadline = this_deadline;
          }
        }

        ptr_to_link = &s->next;
      };

      // Go service the callbacks
      if (sessions_to_finalize) break;

      if(there_is_a_deadline) {
        // do a timed wait
        ts.tv_sec = next_deadline / 1000ull;
        ts.tv_nsec = (next_deadline % 1000ull) * 1000000ull;
        pthread_cond_timedwait(&sessions_cv, &scheduler_lock_m, &ts);
        continue;
      }

      if(shutdown_flag) break;

      // No deadlines or callbacks pending.
      // Just chill.
      pthread_cond_wait(&sessions_cv, &scheduler_lock_m);
    }
    pthread_mutex_unlock(&scheduler_lock_m);
    // Service the callbacks by spawning them in their own threads
    while(sessions_to_finalize) {
      s = sessions_to_finalize;
      sessions_to_finalize = sessions_to_finalize->next;
      if(!s->abort_session && s->callback) {
        pthread_attr_t attr;
        pthread_t thread;
        ret = pthread_attr_init(&attr);
        if (ret != 0) {
          // this is bad
          out(stderr, "Could not create attributes for callback. ret = %d\n", ret);
          panic();
          break;
        }
        ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (ret != 0) {
          // this is bad
          out(stderr, "Could not set detatched attribute for callback. ret = %d\n", ret);
          panic();
          break;
        }
        ret = pthread_create(&thread, &attr, s->callback, s->ctx);
        if (ret != 0) {
          // this is bad
          out(stderr, "Could not create thread for callback. ret = %d\n", ret);
          panic();
          break;
        }
        pthread_attr_destroy(&attr);
      }
      free(s);
    }
    if(i_dont_have_the_lock_and_shutdown_is_requested()) break;
  }
  return NULL;
}
