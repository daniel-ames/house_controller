
typedef void (*handler_callback_t)(void *ctx);

typedef struct session {
  uint32_t id;
  uint32_t inactivity_timeout;
  uint32_t last_activity_time;
  bool abort_session;
  bool end_session;
  void *ctx;   // Context TBD
  handler_callback_t callback;
  struct session *next;
} session_t;


void* scheduler_thread(void *ptr);
uint32_t create_session(uint32_t inactivity_time, handler_callback_t);
void pet_the_dog(uint32_t session_id);

