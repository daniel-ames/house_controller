#include <stdarg.h>

#define this  printf("%s(%d)\n", __FUNCTION__, __LINE__);

void out(FILE *stream, char *str, ...);

