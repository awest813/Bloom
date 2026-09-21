// SPDX-License-Identifier: GPL-2.0-only
#include <kos/mutex.h>

/* Older libgcc gthread code calls this external symbol. Current KOS provides
 * only an inline mutex_lock(), so libgcc otherwise uses its weak startup stub
 * and later tries to unlock a mutex that was never acquired. */
int bloom_legacy_mutex_lock(mutex_t *mutex) __asm__("_mutex_lock");

int bloom_legacy_mutex_lock(mutex_t *mutex)
{
	return mutex_lock_timed(mutex, 0);
}
