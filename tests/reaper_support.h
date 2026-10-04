/* Minimal allocator/state boundary for the production cleanup queue tests. */
#ifndef BLOOM_TEST_REAPER_SUPPORT_H
#define BLOOM_TEST_REAPER_SUPPORT_H
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "deps/lightrec/slist.h"
#include "deps/lightrec/reaper.h"

struct lightrec_state { int unused; };
enum { MEM_FOR_LIGHTREC };
static int fail_allocation;
static void *lightrec_malloc(struct lightrec_state *s, int kind, size_t size) {
    (void)s; (void)kind;
    return fail_allocation ? NULL : malloc(size);
}
static void lightrec_free(struct lightrec_state *s, int kind, size_t size, void *p) {
    (void)s; (void)kind; (void)size;
    free(p);
}
#define pr_err(...) ((void)0)
#endif
