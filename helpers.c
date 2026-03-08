#include "controller.h"


void free_kvp_list(key_value_t *kvp)
{
  for(key_value_t *k = kvp; k; ) {
    key_value_t *next = k->next;
    free(k);
    k = next;
  }
}
