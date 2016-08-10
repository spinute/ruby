#pragma once

#include <stdarg.h>
#define elog(...) fprintf(stderr, __VA_ARGS__)

long get_rss(void);
void dump_backtrace(void);
