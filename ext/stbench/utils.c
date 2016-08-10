#include "./utils.h"

#include <execinfo.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/resource.h>

void
report_rss(const char header[]) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    elog("%s Max RSS = %ld\n", header, ru.ru_maxrss);
}


void
dump_backtrace(void) {
    void *callstack[128];
    int frames = backtrace(callstack, 128);
    char **strs = backtrace_symbols(callstack, frames);
    for (int i = 0; i < frames; ++i) {
	elog("%s\n", strs[i]);
    }
    free(strs);
}
