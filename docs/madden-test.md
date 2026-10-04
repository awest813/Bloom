# Madden NFL 2000 boot investigation

Source: `C:/Users/allen/Downloads/Madden NFL 2000.chd`.
Tests use 200 MHz Flycast, Unai, AICA, and ten-part diagnostic profiling.
Physical Dreamcast behavior has not been tested.

The combined `hw-skip` candidate remains black after disc detection. Its
serial output repeatedly reports no drawing and zero presentations while the
guest CPU continues executing. The older `hw-io` executable reproduces this
failure, before the raw sprite specialization and constant GPU-poll shortcut.
Therefore those two changes are not necessary to reproduce the boot stall.

The earlier build's sequences 4–22 contain no presentations or draw calls.
Evidence: `build/validation/flycast-madden-baseline/boot-stall-summary-01.json`;
exact disc and executable hashes are in each run's manifest.

The first run's CSV contains only its header: diagnostics were sampled only
from an overlay callback, which does not run reliably without game frames.
The harness now samples from vblank as well; both Lua checks pass. No missing
CSV data from the first run has been fabricated or reconstructed.

Next comparison: the repository's bundled OpenBIOS instead of HLE, using a
separate build and capture. This is a hypothesis test, not a confirmed fix.
No menus, playable match, gameplay FPS, or audio quality have been validated.

Paused at the user's request after `build/dreamsdk-madden-openbios` completed
successfully. Its profiler symbol is `0x8c19623c`. Packaging and running this
comparison are the next steps; OpenBIOS has not yet been tested with Madden.

## September 26 resumed comparison

The OpenBIOS test ran at 200 MHz in `flycast-madden-openbios`. It produced
startup presentations, then stalled with zero drawing and presentations.
Sequences 12–30 are saved in `boot-stall-summary-01.json`. The startup
presentations do not establish that Madden reached a title screen; an unrelated
Windows permission dialog obscured visual inspection during this comparison.
OpenBIOS therefore did not resolve the loading failure. No match FPS exists.

ELF SHA-256: `29F12B170F1338A4325B3981FC2EFE54B9DDAFC286C2B763674466DE0A1F7970`.
CDI SHA-256: `5F1FD6F05FC2ABBB2A12287FE9CFA6D6A0DF7E0CF044090170676522B92510CE`.

Madden remains an unresolved compatibility issue. Proceed to the user's next
requested games, Final Fantasy IV and Chrono Trigger, without claiming Madden
playable or assigning a gameplay FPS to its boot loop.
