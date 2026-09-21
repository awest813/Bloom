# Building with DreamSDK on Windows

The installed SDK at `C:\DreamSDK` successfully builds Bloom with SH-4 GCC
15.1.0. Its KallistiOS revision is `804b3195ebd1a06a27cc2b3a5eacf7a2429040a3`
and kos-ports is `f4faacc42faaf552625777b7709e871a827e1055`.

Parallax, Tsunami, and their dependencies have been built and installed on
this host. For a fresh SDK, open DreamSDK Shell and run:

```sh
make -C "$KOS_PORTS/libtsunami" install \
  FETCH_CMD="curl --fail --location --remote-name" VALIDATE_DISTFILES=true
```

Run the port installation sequentially: the port setup/dependency targets
can race when the top-level command uses `-j`.

## Build

From the repository directory in DreamSDK Shell:

```sh
bash scripts/build-dreamsdk.sh
```

Or from PowerShell on this host:

```powershell
& 'C:\DreamSDK\usr\bin\bash.exe' --noprofile --norc `
  /f/GitHub/Bloom/scripts/build-dreamsdk.sh
```

The default output directory is `build/dreamsdk`. The first argument can
select a different build directory; remaining arguments go to CMake:

```powershell
& 'C:\DreamSDK\usr\bin\bash.exe' --noprofile --norc `
  /f/GitHub/Bloom/scripts/build-dreamsdk.sh build/dreamsdk-native `
  '-DWITH_GAME_PATH=/cd/ff6.chd' '-DWITH_PERF_LOG=ON'
```

That FFVI configuration was used for verification. It expects `ff6.chd`
on the Dreamcast disc; it does not bundle a game. The alternate renderer also
builds with `-DGPU_PLUGIN=Unai -DWITH_PERF_LOG=OFF`. Quote CMake arguments in
PowerShell. Set `BLOOM_BUILD_JOBS` to change the default four build jobs.

Outputs are `bloom.elf`, raw `bloom.bin`, and scrambled `1ST_READ.BIN`.
Use the raw binary with mkdcdisc, or the scrambled file with BootDreams,
as described in the main README. The ELF contains the embedded OpenBIOS
by default; both verified builds have a 512 KiB `.bios` section.

## Windows-specific fixes

- `cmake/dreamsdk.toolchain.cmake` uses the SDK's executable compiler
  launchers. Native Windows CMake cannot run the extensionless KOS shell
  scripts directly during compiler identification.
- The build script excludes guest game, BIOS, savestate, and memory-card
  paths from MSYS argument conversion, including typed arguments such as
  `-DWITH_GAME_PATH:STRING=/cd/ff6.chd`. `/cd/ff6.chd` stays a Dreamcast path.
- Lightrec's `/rd/dummy.elf` name lives in a forced-include header, so
  compiler launchers cannot strip its quotes or rewrite it as a host path.
- Shell scripts use LF line endings through `.gitattributes`.
- The bundled GCC 15.1 runtime calls an external `mutex_lock`, whereas these
  KOS headers provide an inline implementation. Its weak fallback does not
  acquire the mutex, causing `mutex_unlock` to assert during C++ startup.
  The DreamSDK toolchain enables `WITH_LEGACY_KOS_MUTEX`, linking a small
  wrapper to `mutex_lock_timed(mutex, 0)`. Disable this option if using a KOS
  installation that already exports the compatibility symbol. The observed
  FFVII failure occurred before game loading, not in the game's renderer.

The FFVII retry passed that mutex assertion but stayed black with both OpenBIOS
and HLE. Inspection found `started` was enabled only after `OpenPlugins()`, so
the GPU open callback skipped presentation allocation. Game startup now enables
callbacks before opening plugins and clears the flag if opening fails. The
presentation lifecycle regression exercises this ordering in both renderer
modes. `build/validation/bloom-ff7-start.cdi` contains the fix (Unai + HLE);
it reaches the Sony Computer Entertainment America screen, but then stalls.
Gameplay has not yet been verified.

The September 21 FFVII checks passed all 22 host regressions. Temporary native
diagnostics showed `pvr_wait_ready()` timing out while `pvr_wait_render_done()`
succeeded: the opaque list remained untransferred (`0/1`), with no render in
progress or completed frame awaiting display. FPSCR was `0x00040000`, and the
store-queue mapping still pointed at TA input (`0x100001fc`). The same stall
occurred in Flycast's interpreter and dynamic recompiler, and with AICA playback
disabled. The test configuration was restored to dynamic recompilation and AICA.
The temporary SDK-private diagnostic includes were removed from the source.

`build/validation/bloom-ff7-dma.cdi` switches software presentation to a small
double-buffered opaque command list submitted through PVR DMA. It has passed the
former stall point and reached FFVII's introductory credits and New Game menu
with AICA enabled. A held Start input skips the credits; Up followed by Circle
selects and confirms New Game. The opening movie completed, Cloud and Barret
rendered at the station, and directional input moved Cloud along the platform
and triggered the first guard battle. Combat menus accepted Attack/target
confirmation. Both guards were defeated, Cloud reached level 7, and confirming
the reward screens returned to the station field. This verifies playable field
movement and a completed battle, not completion of the opening mission. Observed presentation
was roughly 8–11 FPS in the movie/field and 12–16 FPS in combat. A large black
upper area persisted in the movie, field, and battle; display placement still
needs investigation. These are Flycast observations, not hardware measurements.
Only the opaque list is enabled for software presentation, including the PVR
software diagnostic mode. Texture uploads still use store queues; hardware PVR
drawing retains its existing submission path. This is a validated startup
workaround with initial gameplay verified, not a diagnosis of the direct-transfer
failure on physical Dreamcast hardware or a full compatibility result.

The subsequent `build/validation/bloom-ff7-video.cdi` fixes the missing upper
rows by keeping one store-queue mapping across the whole display upload.
The pixel-conversion loop is isolated and unrolled for SH-4. Credits and the
opening movie render at full height. The final candidate reached the station:
Barret's upper-screen dialogue is visible, Circle advances it, and directional
input moves Cloud. Field presentation remained around 10 FPS, so this is not
a substantial field-gameplay speedup. A stable 640×480 startup sample improved
from about 11.1 to 15.2 presentation FPS, with upload time reduced from about
29.5 to 13.8 ms. See [performance.md](performance.md) for scope and conditions.
All 24 sanitizer regressions passed; hardware PVR with logging disabled and
software PVR with logging enabled also built successfully. The new candidate
uses the separate `flycast-video` instance; the original gameplay run remains
open in `flycast` with its earlier build.
The new test instance saved a station gameplay checkpoint at
`build/validation/flycast-video/data/bloom-ff7-video_9.state` (slot 9).
It belongs to this exact CDI/build; a Flycast state restores the entire
Dreamcast, including Bloom's code, so it cannot compare a newly rebuilt
binary against the old one. Loading that checkpoint has not yet been tested.

The September 21 performance follow-up added optional core profiling and
compared threaded versus synchronous Lightrec compilation in separate
`bloom-ff7-core.cdi` and `bloom-ff7-sync.cdi` images. Both reached Barret's first
station dialogue; the observed FPS ranges overlap, so threaded compilation
remains the default. See [performance.md](performance.md) for measurements and
limits. Each diagnostic instance saved its own slot-9 station checkpoint,
then was closed. The original `flycast` and corrected `flycast-video` gameplay
instances were preserved. Core profiling is disabled in the ordinary build
caches; the 25-check suite passed and normal Unai/PVR builds linked.

The corrected video image also completed the first two-guard battle, reached
level 7, and returned to the station field. The battlefield and reward menus
rendered at full height. Observed combat presentation varied roughly 13–18 FPS
and reward screens roughly 29–32 FPS; these are different workloads, not a
measured speedup against the old build. The earlier pre-battle checkpoint is
preserved as `bloom-ff7-video-before-first-battle.state` in the same `data`
directory. Slot 9 now holds the return-to-field checkpoint after that battle.
The opening mission is still unfinished, and checkpoint loading is untested.

After the DMA change, all 23 host regression checks passed, and the configured
`dreamsdk-native` hardware build, `dreamsdk-pvr-software` diagnostic build, and
`dreamsdk-ff7` Unai build linked successfully. The old `dreamsdk-pvr` directory
still uses a stale compiler configuration without the normal optimization and
register-allocation flags; it failed in Lightrec with an `R0_REGS` spill error.
Use the configured native directory or a fresh build directory instead.

For repeatable Flycast input testing, `scripts/flycast-test-controls.lua` uses
Flycast's public Lua input API and optional overlay. Copy it beside the test
instance's `emu.cfg` as `flycast.lua`, with no existing script overwritten.
Set `bloom_test_command_file` before the script to an absolute sidecar path, or
use `bloom-input.txt` in Flycast's working directory. A command such as
`1 16 60` holds Up for 60 Dreamcast vertical blanks. Each command needs a new
sequence number; masks 8, 16, 32, 64, 128, 2, and 4 represent Start, Up, Down,
Left, Right, Circle, and Cross. `2 0 0` releases the test input. The overlay
shows the held mask and remaining duration. This changes controller input only,
not guest memory or game progress. The optional save button uses test slot 9.
The sidecar command `3 save 9` also creates that test checkpoint; confirm slot 9
is unused before using it. Set `bloom_test_overlay = false` before the script
to keep the sidecar controls active without covering the game image.

## Performance checkpoint

Before the native SDK build, the GCC 17 Docker `perf3` image was observed
in Flycast's interpreter with presentation FPS mostly around 17–19 in the
startup samples (one sample was about 11.6), 100% reported SH-4 busy time,
and zero texture uploads or software primitives in those intervals.
Last-frame PVR time was around
3.2–3.9 ms; it is not an interval average and can remain unchanged when
no new rendering occurs.

Those observations suggest investigating CPU-side work next, but do not
separate PS1 execution, JIT compilation, audio mixing, or waiting. They
also do not measure native Dreamcast performance or establish gameplay
compatibility. A subsequent FFVII runtime attempt exposed the startup mutex
failure above; successful linking alone did not validate native startup.
