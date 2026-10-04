# Top Shop: initial 200 MHz Flycast test

Tested September 24, 2026 using the user's `Downloads/Top Shop.chd`.
The game booted through its title and mode menu, entered Story Mode, displayed
the cafe dialogue, and reached A1 Bank's first board turn and turn controls.
This establishes initial compatibility, not a complete playthrough.

The test uses the existing Release/Unai/HLE/AICA diagnostic executable from
`build/dreamsdk-hw-io`, with ten profile categories, 200 MHz Flycast, audio
enabled, and no frame skipping. For this unchanged executable, the packaged
Top Shop image occupies its configured `/cd/ff7.chd` path; the content is Top
Shop. Its original file was not modified. The separate run directory is
`build/validation/flycast-top-shop`; its manifest records source and build hashes.

| Observed phase | Capture sequences | Emulated seconds | Presented FPS |
| --- | --- | --- | --- |
| Story introduction / cafe dialogue | 94–102 | 25.11 | 11.27 |
| A1 Bank first-turn controls | 132–140 | 25.27 | 8.67 |

The board sample attributes 43.30% to drawing, 30.18% to execution excluding
I/O, 14.79% to sound, 3.30% to video, 3.01% to hardware-register handlers, and
5.43% to other work. Geometry and interpreter fallback are negligible.
The initial board scene was visually observed. Subsequent verification was
interrupted by user activity/occlusion, so this is an observational sample,
not a fully controlled replay. The introduction later advanced to the board.

The logger accepted complete intervals and the input harness acknowledged
Start, Story Mode confirmation, and dialogue confirmation. Capture summaries
are `board-summary-01.json` and `story-summary-01.json` beside `profile-01.csv`.
The profiler itself adds overhead, especially around frequent register calls.
No uninstrumented comparison, extended gameplay, audio-quality assessment, or
physical Dreamcast test has been completed. Neither sample establishes the
15–19 FPS target. Drawing is the leading board-scene optimization candidate.
