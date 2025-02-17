
#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include "logger.h"

#define LOG_FILE  "/opt/house_controller/house_controller.log"

FILE *fp = NULL;


static void init_logger()
{
    int ret = 0;
    fp = fopen(LOG_FILE, "a");
    if (fp == -1)
        out(stderr, "No log file \"%s\", and I couldn't create one either! Error: %s\n", LOG_FILE, strerror(errno));
}


void logger_handle_sig()
{
    if (fp != NULL) fclose(fp);
}


void out(FILE *stream, char *str, ...)
{
    va_list args_for_stream;
    va_list args_for_log;
    va_list args_for_syslog;

    if (fp == NULL)
        init_logger();

    if (stream != NULL)
    {
        // Write to the console too. Really only useful for testing
        // because stdout/err/etc go to /dev/null when running normally.
        va_start(args_for_stream, str);
        vfprintf(stream, str, args_for_stream);
    }

    // Write to syslog
    va_start(args_for_syslog, str);
    vsyslog(LOG_USER | LOG_ERR, str, args_for_syslog);

    // Write to our own persistent log file
    if (fp != NULL && fp != -1)
    {
        va_start(args_for_log, str);
        vfprintf(fp, str, args_for_log);
        fflush(fp);
    }
}
