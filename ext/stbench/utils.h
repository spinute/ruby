#pragma once

#include <stdarg.h>
#define elog(...) fprintf(stderr, __VA_ARGS__)

void report_rss(const char header[]);
void dump_backtrace(void);
