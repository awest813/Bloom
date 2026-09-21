# Audio audit — 2026-09-20

Reviewed the production AICA output driver, dfsound output selection and
SPU open/close/feed integration, host tests, standalone stereo test, and the
installed KallistiOS stream implementation.

## Fixes and checks

- Playback polling errors were ignored. The driver now reports the error once,
  destroys the stream, clears queued PCM, and continues emulation silently.
  It does not restart an invalid handle from the overflow path. The next SPU
  output open, or emulator restart, can initialize playback again.
- The standalone stereo test was missing `bloom_want_silent_audio()`, which
  prevented linking it without the application's settings code. It now supplies
  an explicit audible-test setting and builds with the documented command.
- Added checks for errors through busy polling, empty feeds, and overflow
  polling; repeated cleanup; reopening after failure; and batches larger than
  the ring retaining the complete newest stereo frames.

All 17 host regression checks passed with address/undefined-behavior sanitizers.
The native DreamSDK AICA/PVR build and standalone audio test linked successfully.
The test ELF is locally available at `build/validation/bloom-audio-smoke.elf`.
No new listening test was performed during this audit. The earlier two-pass
Flycast tone test remains historical evidence, not validation of these changes.

## Verified driver behavior

dfsound supplies interleaved signed 16-bit stereo at 44,100 Hz. Each AICA
channel has an 8 KiB buffer, about 93 ms of playback. Startup waits for enough
interleaved PCM to fill both channels; the aligned bounce buffer serves one
half-buffer request. KOS invokes the callback synchronously from start/poll
and passes bytes, despite the callback header's sample-oriented naming.

Ring operations preserve left/right pairs. Overflow discards the oldest queued
PCM, and callbacks fill missing samples with zero instead of replaying stale
bounce-buffer contents. Initialization/allocation failures select dfsound's
silent output. A later polling failure leaves the selected AICA driver inert
until reopen. The driver owns the sound subsystem in the current application;
adding another sound user will require shared lifecycle management.

Polling and feeding run on the emulation thread. Mixing threads and tempo
adjustment are disabled by the AICA configuration. The driver does not introduce
a concurrent callback, so ring locking is unnecessary under this contract.

## Remaining validation and quality work

The AICA configuration retains its existing 75% mixer gain and disables reverb,
interpolation, and XA pitch adjustment. These are performance tradeoffs; this
audit does not claim accurate reverb, resampling, XA/CDDA playback, or SPU IRQ
behavior in games.

The next runtime check is the rebuilt stereo test: verify both frequencies,
clean starts/stops, and both passes after reopening. Then compare a repeatable
game scene with voices, XA/CDDA, and silent output, recording clipping, underruns,
and frame time. Repeat on physical Dreamcast hardware.

Long emulation stalls can still prevent timely polling and let the AICA buffer
loop before software regains control. Silence padding cannot repair sustained
underproduction. Measure polling gaps, dropped frames, and underruns before
changing buffer sizes or adding threaded servicing. Evaluate interpolation and
reverb separately against that measured CPU budget.
