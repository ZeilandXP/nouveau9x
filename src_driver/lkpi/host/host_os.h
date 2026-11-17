/* The handful of C-library services the host backend needs. Kept behind
 * this header because lkpi.h redefines too many libc names to share a
 * translation unit with <stdio.h> and friends. */
#ifndef LKPI_HOST_OS_H
#define LKPI_HOST_OS_H
#include <stddef.h>
#include <stdint.h>
void *host_alloc(size_t size, int zero, size_t align);
void host_free(void *p);
uint64_t host_time_ns(void);
void host_sleep_us(unsigned int usec);
void host_write(const char *text);
void host_abort(void) __attribute__((noreturn));
#endif
