#include <time.h>
#include <stdlib.h>
#include <stdint.h>

#define MEASUREMENT_FILE "measurement.txt"
#define PLOT_FILE "plots.dat"
#define MAX_BUFF_SZ  256

typedef enum {
  unknown_d,
  sewage_pump_d,
  well_house_d,
  driveway_d,
  generator_d
} device_id_e;

typedef enum {
  device_type,
  amps_type,
  volts_type,
  psi_type,
  on_off_type,
  epoch_type,
  us_type,
  ms_type,
  seconds_type,
  string_type
} key_type_e;

typedef struct key_value {
  key_type_e key;
  union {
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    double dbl;
    char *str;
  } value;
  struct key_value* next;
} key_value_t;

typedef struct {
  float average;
  float max;
  float min;
  int samples;
  __u_long duration;
} summary_t;


#include "helpers.h"

