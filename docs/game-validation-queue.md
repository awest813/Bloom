# Ordered game validation and performance goals

User-directed order, September 24, 2026. Use stock 200 MHz Flycast with audio
enabled as the current hardware proxy. Physical Dreamcast results remain
unverified. Preserve per-game builds, hashes, captures, scene labels, and
input records; do not treat microbenchmark gains as game FPS.

1. **Several additional FFVII tests**, then move on without requiring the
   final FFVII performance goal first. Current candidate: `dreamsdk-hw-skip`
   (raw 16-bit sprite path plus constant-status polling shortcut).
   - Station dialogue performance sample with visual verification.
   - A second independent station window to assess variability.
   - Progression into the first battle, checking rendering and control response
     and measuring a labeled battle window where practical.
2. **Try Madden NFL 2000** from `Downloads/Madden NFL 2000.chd`.
   Verify boot, menus, and a playable match; measure an observed gameplay scene
   and record compatibility or audio issues. No user-specified FPS target yet.
3. **Final Fantasy Chronicles — Final Fantasy IV: 15–19 FPS**, then
   **Final Fantasy Chronicles — Chrono Trigger: 15–19 FPS**. Both matching
   CHDs are verified in Downloads. User inserted these after Madden and before
   Monster Rancher 2. Validate representative gameplay for each independently.
4. **Monster Rancher 2: 12–19 FPS** from `Downloads/Monster Rancher 2.chd`.
   Make this the performance priority after both Chronicles games. Validate actual
   gameplay rather than title-screen rates.
5. **Top Shop: 20 FPS and good audio** from `Downloads/Top Shop.chd`.
   Validate board gameplay and actual audio output. Audio subsystem timing or
   "audio enabled" alone does not establish good audio.
6. **Return to FFVII: 15–19 FPS** using the supplied Disc 1 CHD.

Current priority (October 1 user override): **Monster Rancher 2 — sustained
19 FPS with light frame skipping at 200 MHz**. Establish actual gameplay
performance, not title/movie rates or skipped/repeated frame counts. Start
with a no-frameskip baseline, then use a bounded light policy and verify its
actual skip ratio, responsiveness, graphics, and audio. The remaining games
above stay pending; the latest user instruction takes priority over their order.
Madden failed to reach gameplay with either HLE or bundled OpenBIOS; its
compatibility investigation is recorded in `madden-test.md`. FFVII's shoulder
issue also appears in the earlier renderer and remains unresolved.
The combined candidate reached the station. Two nonoverlapping windows from
the same boot measured 9.72 and 10.80 FPS (sequences 282–292 and 296–306).
These are not independent cold boots; sound time varied substantially.
The user reported missing parts at Cloud's shoulders. Candidate acceptance is
on hold while comparing against the earlier `hw-io` build, which predates both
the raw sprite specialization and constant GPU polling shortcut. That new
comparison run is `build/validation/flycast-ff7-render-check`.
The current candidate also reached the first battle: sequences 440–450 averaged
14.89 FPS over 30.29 seconds while waiting at Attack with guards acting.
Attack selection, target selection, and a hit dealing 59 damage were observed.
The attempted scripted pause hung Flycast before acknowledgement. That finished
test process was closed; overlay pause/resume commands have been removed.
Madden has launched in `flycast-madden-2000`.

## Resume checkpoint (September 26)

Both Madden HLE builds stall at a black screen; see [madden-test.md](madden-test.md).
The next OpenBIOS comparison build completed successfully in
`build/dreamsdk-madden-openbios` (snapshot address `0x8c19623c`). The user
resumed work on September 26. The OpenBIOS image is now packaged and running
in `build/validation/flycast-madden-openbios`; after initial startup frames it
stalled without drawing. Its test instance was closed. The FFIV HLE image and
fresh run folder `flycast-ff4-hw-skip` are ready. No game's performance goal
is newly achieved.

FFIV is now running in `flycast-ff4-hw-skip`, with input isolation and a new
`profile-01.csv`. MDEC/disc activity and presentations show its opening movie
is progressing. No controller commands have been sent. An unrelated Windows
Security prompt for Nazi Zombies: Portable obscures visual checks; the user
has been asked to handle it. Do not claim gameplay FPS from these movie samples.
The next image `bloom-chrono-trigger-hw-skip.cdi` is also prepared, not launched.

The earlier FFVII renderer also showed Cloud's shoulder/upper-back gaps at
the same Barret dialogue. This predates the latest two optimizations, but
the underlying visual issue remains unresolved. The comparison and Madden
test instances were closed for the pause. Existing captures remain intact.

The earlier FFVII-only goal is not achieved. This queue records the user's
new ordering and intermediate targets; it must guide subsequent continuations.
Do not skip ahead because a smaller test is easier or mark a game's target
achieved from an unrelated scene or another game's result.
