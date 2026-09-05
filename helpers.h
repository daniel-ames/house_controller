
void free_kvp_list(key_value_t *kvp);
void panic();
bool there_is_a_panic();
void time_my_way(struct tm * time, char * out);
uint64_t get_mono_time_ns();
uint64_t get_wall_time_ns();
