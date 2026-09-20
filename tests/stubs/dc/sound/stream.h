/* Host declarations for the KOS calls used by the audio driver. */
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int snd_stream_hnd_t;
typedef void *(*snd_stream_callback_t)(snd_stream_hnd_t, int, int *);
#define SND_STREAM_INVALID (-1)
int snd_stream_init_ex(int channels, size_t buffer_size);
snd_stream_hnd_t snd_stream_alloc(snd_stream_callback_t callback, int buffer_size);
void snd_stream_start(snd_stream_hnd_t handle, uint32_t frequency, int stereo);
void snd_stream_volume(snd_stream_hnd_t handle, int volume);
int snd_stream_poll(snd_stream_hnd_t handle);
void snd_stream_destroy(snd_stream_hnd_t handle);
void snd_stream_shutdown(void);
