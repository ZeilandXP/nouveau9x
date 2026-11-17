#define _POSIX_C_SOURCE 200809L
#include "host_os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void *host_alloc(size_t size, int zero, size_t align)
{
    void *p = NULL;
    if (!size) size = 1;
    if (align < sizeof(void *)) align = sizeof(void *);
    if (posix_memalign(&p, align, size)) return NULL;
    if (zero) memset(p, 0, size);
    return p;
}

void host_free(void *p) { free(p); }

uint64_t host_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void host_sleep_us(unsigned int usec)
{
    struct timespec ts = { usec / 1000000, (long)(usec % 1000000) * 1000 };
    nanosleep(&ts, NULL);
}

void host_write(const char *text) { fputs(text, stderr); }
void host_abort(void) { fflush(NULL); abort(); }
