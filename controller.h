#include <time.h>

#define MEASUREMENT_FILE "measurement.txt"
#define PLOT_FILE "plots.dat"

typedef struct sample_t {
  float amps;
  time_t timestamp;
  int ordinal;
  struct sample_t *next;
} sample_t;

typedef struct {
  float average;
  float max;
  float min;
  int samples;
  __u_long duration;
} summary_t;