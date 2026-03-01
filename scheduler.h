
typedef void (*handler_callback_t)(void *ctx);

typedef struct session {
  uint32_t id;
  uint64_t inactivity_timeout;
  uint64_t last_activity_time;
  bool abort_session;
  bool end_session;
  void *ctx;   // Context TBD
  handler_callback_t callback;
  struct session *next;
} session_t;


void* scheduler_thread(void *ptr);
uint32_t create_session(uint32_t inactivity_time, handler_callback_t, void *ctx);
bool pet_the_dog(uint32_t session_id);

