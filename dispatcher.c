#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "controller.h"
#include "logger.h"

#define DEVICE_TOKEN_LEN  4   // "dev="
#define AMPS_TOKEN_LEN    5   // "amps="
#define AMPSn_TOKEN_LEN    6   // "ampsn="

void sewage_pump_handler(key_value_t *kvp);
void wellhouse_handler(key_value_t *kvp);

static key_value_t *kvp, *kvp_head, *kvp_prev;

static void make_kvp(key_type_e type, char *str)
{
  kvp = malloc(sizeof(*kvp));
  if(!kvp) {
    out(stderr, "Panic: could not malloc key_value_t!\n");
    panic();
    return;
  }
  
  kvp->next = NULL;
  kvp->key = type;
  switch(type) {
    case device_type:
      kvp->value.u32 = strtoul(str, NULL, 10);
      break;
    case amps_type:
    case ampsx_type:
    case ampsy_type:
      kvp->value.dbl = strtod(str, NULL);
      break;
    case volts_type:
    case psi_type:
    case on_off_type:
    case epoch_type:
    case us_type:
    case ms_type:
    case seconds_type:
    case string_type:
      break;
  }

  if(kvp_prev) kvp_prev->next = kvp;
  else kvp_head = kvp;
  kvp_prev = kvp;
}

static void msg_is_malformed(const char *msg, char *peer_ip_address)
{
  out(stderr, "Discarding malformed message from peer: %s\n", peer_ip_address);
  out(stderr, "  \"%s\"\n", msg);
}

void dispatch(const char *msg, uint32_t length, char *peer_ip_address)
{
  // incoming message.
  // Alls we know about this message at this point is that it's within size limitations (it's not a runaway message),
  // and that it's terminated with a newline. That's it.
  // All we care about right now is the device type, but go ahead and do the text parsing here, once.
  char *saveptr;
  device_id_e dev_id = unknown_d;
  kvp = kvp_head = kvp_prev = NULL;

  // Work with a copy of msg so I can use strtok()
  char *cursor, *p = malloc(length + 1);
  p[length] = 0;
  memcpy(p, msg, length);

  cursor = strtok_r(p, " ", &saveptr);
  do {
    if(!strncmp(cursor, "dev=", DEVICE_TOKEN_LEN)) {
      // device id
      if(!cursor[DEVICE_TOKEN_LEN]) {
        // value is blank. this should never happen
        msg_is_malformed(msg, peer_ip_address);
        dev_id = unknown_d;
        break;
      }
      make_kvp(device_type, &cursor[DEVICE_TOKEN_LEN]);
      // Intercept this one. We need to know what device this is so we know
      // what handler to call.
      dev_id = kvp_prev->value.u32;
      continue;
    }
    if(!strncmp(cursor, "amps=", AMPS_TOKEN_LEN)) {
      // amps
      if(!cursor[AMPS_TOKEN_LEN]) {
        // value is blank. this should never happen
        msg_is_malformed(msg, peer_ip_address);
        dev_id = unknown_d;
        break;
      }
      make_kvp(amps_type, &cursor[AMPS_TOKEN_LEN]);
    }
    if(!strncmp(cursor, "ampsx=", AMPSn_TOKEN_LEN)) {
      // amps_x
      if(!cursor[AMPSn_TOKEN_LEN]) {
        // value is blank. this should never happen
        msg_is_malformed(msg, peer_ip_address);
        dev_id = unknown_d;
        break;
      }
      make_kvp(ampsx_type, &cursor[AMPSn_TOKEN_LEN]);
    }
    if(!strncmp(cursor, "ampsy=", AMPSn_TOKEN_LEN)) {
      // amps_y
      if(!cursor[AMPSn_TOKEN_LEN]) {
        // value is blank. this should never happen
        msg_is_malformed(msg, peer_ip_address);
        dev_id = unknown_d;
        break;
      }
      make_kvp(ampsy_type, &cursor[AMPSn_TOKEN_LEN]);
    }

  } while( (cursor = strtok_r(NULL, " ", &saveptr)) );

  free(p);

  // We now have a linked list of key-value pairs.
  // Pass it to the appropriate handler.
  // It is the handler's responsibility to free the linked list.
  switch(dev_id) {
    case sewage_pump_d:
      sewage_pump_handler(kvp_head);
      break;
    case well_house_d:
      wellhouse_handler(kvp_head);
      break;
    case driveway_d:
    case generator_d:
    case unknown_d:
      // error path...
      free_kvp_list(kvp_head);
      break;
  }

}
