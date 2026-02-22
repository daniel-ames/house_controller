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


void dispatch(const char *msg)
{
  // incoming message.
  // Alls we know about this message at this point is that it's within size limitations (it's not a runaway message),
  // and that it's terminated with a newline. That's it.

}