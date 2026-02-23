


typedef struct session {
  uint32_t id;
  uint32_t inactivity_timeout;
  uint32_t last_activity_time;
  bool abort_session;
  bool end_session;
  void (*callback)(struct session *s);
  struct session *next;
} session_t;


void* scheduler_thread(void *ptr);
uint32_t create_session(uint32_t inactivity_time, void *callback);
void pet_the_dog(uint32_t session_id);

