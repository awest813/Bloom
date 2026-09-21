// SPDX-License-Identifier: GPL-2.0-only
#ifndef BLOOM_CORE_PROFILE_H
#define BLOOM_CORE_PROFILE_H

/* Call only around guest execution, outside any profiled entry point. */
void bloom_profile_reset(void);
void bloom_profile_stop(void);

#endif
