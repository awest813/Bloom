#include "tests/reaper_support.h"
#include <sched.h>
#include <unistd.h>
static atomic_uint locks, completed;
static int counted_lock(pthread_mutex_t *m) {
    atomic_fetch_add(&locks, 1);
    return pthread_mutex_lock(m);
}
#define pthread_mutex_lock counted_lock

/* Production reaper is inserted here by test_regressions.py. */

#undef pthread_mutex_lock
static struct lightrec_state state;
static struct reaper *queue;
static unsigned seen[4000];
static atomic_bool callback_entered, callback_finished;
static void paused_callback(struct lightrec_state *s, void *data) {
    atomic_store(&callback_entered, true);
    /* Main requests a pause after seeing us enter. It must wait even though
     * removing the final queued item has already cleared the pending hint. */
    while (!atomic_load(&queue->sem)) sched_yield();
    atomic_store(&callback_finished, true);
}
static void *consume_one(void *arg) {
    lightrec_reaper_reap(queue);
    return NULL;
}
static void record(struct lightrec_state *s, void *data) {
    assert(s == &state);
    unsigned *slot = data;
    assert(*slot == 0);
    *slot = 1;
    atomic_fetch_add(&completed, 1);
}
static void add_during_callback(struct lightrec_state *s, void *data) {
    record(s, data);
    assert(lightrec_reaper_add(queue, record, &seen[1]) == 0);
}
static void *producer(void *arg) {
    uintptr_t start = (uintptr_t)arg;
    for (unsigned i = start; i < start + 2000; i++)
        assert(lightrec_reaper_add(queue, record, &seen[i]) == 0);
    return NULL;
}
int main(void) {
    alarm(20); /* Fail a lost-work/deadlock regression instead of hanging CI. */
    queue = lightrec_reaper_init(&state);
    assert(queue);
    unsigned before = atomic_load(&locks);
    for (int i = 0; i < 10000; i++) lightrec_reaper_reap(queue);
    assert(atomic_load(&locks) == before); /* Empty dispatch never locks. */

    fail_allocation = 1;
    assert(lightrec_reaper_add(queue, record, &seen[0]) == -ENOMEM);
    fail_allocation = 0;
    before = atomic_load(&locks);
    lightrec_reaper_reap(queue);
    assert(atomic_load(&locks) == before);

    lightrec_reaper_pause(queue);
    lightrec_reaper_pause(queue);
    assert(lightrec_reaper_add(queue, add_during_callback, &seen[0]) == 0);
    assert(lightrec_reaper_add(queue, add_during_callback, &seen[0]) == 0);
    lightrec_reaper_reap(queue);
    assert(!atomic_load(&completed));
    lightrec_reaper_continue(queue);
    lightrec_reaper_reap(queue);
    assert(!atomic_load(&completed));
    lightrec_reaper_continue(queue);
    lightrec_reaper_reap(queue);
    assert(atomic_load(&completed) == 2);
    before = atomic_load(&locks);
    lightrec_reaper_reap(queue);
    assert(atomic_load(&locks) == before);
    seen[0] = seen[1] = 0;
    atomic_store(&completed, 0);

    pthread_t consumer;
    assert(!lightrec_reaper_add(queue, paused_callback, &seen[0]));
    assert(!pthread_create(&consumer, NULL, consume_one, NULL));
    while (!atomic_load(&callback_entered)) sched_yield();
    assert(!atomic_load(&queue->pending));
    lightrec_reaper_pause(queue);
    assert(atomic_load(&callback_finished));
    assert(!pthread_join(consumer, NULL));
    lightrec_reaper_continue(queue);

    /* Producers can publish while the consumer observes an empty queue or
     * drops the mutex around a callback. Every item must be reaped once. */
    pthread_t a, b;
    assert(!pthread_create(&a, NULL, producer, (void *)(uintptr_t)0));
    assert(!pthread_create(&b, NULL, producer, (void *)(uintptr_t)2000));
    while (atomic_load(&completed) != 4000) {
        lightrec_reaper_reap(queue);
        sched_yield();
    }
    assert(!pthread_join(a, NULL) && !pthread_join(b, NULL));
    for (unsigned i = 0; i < 4000; i++) assert(seen[i] == 1);
    seen[0] = 0;
    assert(!lightrec_reaper_add(queue, record, &seen[0]));
    lightrec_reaper_destroy(queue);
    assert(seen[0] == 1 && atomic_load(&completed) == 4001);
    return 0;
}
