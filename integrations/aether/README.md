# Aether CMVS provider

This provider implements Aether's `engine_runtime_provider_v1_t` on macOS,
iOS, Android and Linux with the
shared Rust `cmvs-host` player. Aether owns the library, window and presentation;
CMVS produces CPU RGBA8 frames and owns native audio, script execution and saves.
The provider probes `data/pack/start.ps2` or `start.ps3` and `script.cpz` by magic,
then requires a supported startup fingerprint when opening the game.

The parent build adds this directory and links `aether_cmvs_runtime`, then calls
`aetherkiri::cmvs::RegisterRuntimeProvider()`. Rust stable and the target selected
by the CMake toolchain must be installed. The shared
[`platforms/host`](../../platforms/host/README.md) target selects the Rust triple,
NDK compiler wrappers and native audio link dependencies. `AETHERKIRI_CMVS_ABI_INCLUDE`
points to Aether's `abi/include`; `AETHERKIRI_CMVS_CARGO_TARGET_DIR` optionally
selects a reusable Cargo cache. The compositor is optimized with debug symbols
even when the parent application is Debug. Build one architecture at a time.

Before opening a game directory, set `default_font` to an available OpenType
font and provide a writable user directory. For Chinese patches the font must
contain Chinese glyphs. Gameplay slots (`saveNNN.dat`), system progress
(`system.dat`), settings (`cmvs.cfg`) and font preferences live in
`<game>/savedata/`. The previous `<writable>/cmvs/<game-hash>/` profile is
imported once without replacing existing local files or deleting old saves.
Read-only game directories retain that external per-game profile. Lock
contention, invalid paths and corrupt saves are reported instead of switching
to an empty profile. Game resource files remain read-only.
`cmvs_native_audio=0` selects the deterministic VM clock for automated replays;
native audio is enabled by default. Input coordinates are logical frame pixels.
Pause and pointer cancellation release held input. Unsupported CMVS options
report `NOT_SUPPORTED`; unrelated options broadcast by Aether are ignored.

The iOS parent must merge both `aether_cmvs_runtime` and `aether_cmvs_rust` into
its plugin archive and link the frameworks exported by this CMake target,
including AVFAudio. Both device and simulator Rust targets are selected from
the toolchain SDK, including Xcode's normalized absolute SDK paths.

Android supports arm64-v8a, armeabi-v7a, x86_64 and x86 with API 26 or later.
Before opening with native audio, the parent calls `InitializeAndroidContext`
with its Java VM and a JNI global Application reference retained for the process.
This is independent of Activity recreation and must happen before an asynchronous
game open. The provider links AAudio and uses nonblocking OS file locks for saves.
Linux supports aarch64 and x86_64 with glibc and ALSA development packages.
When combining multiple Rust static runtimes on Android/Linux, the parent must
use the same Rust toolchain and resolve duplicate std/alloc symbols at final link.

## Validation

The complete Aether macOS Debug application was built with its normal runtime
packages, including Internal. Amatsutsumi 1.03 with the PKP Chinese translation
was imported through the normal game library and started into translated title
graphics, intro subtitles and Chinese dialogue. Pointer and Enter input advanced
the opening story. The standalone provider test also replays this opening and
validates frame-buffer bounds, lifecycle and input through the public ABI.

The provider builds for arm64 iOS device and simulator. The UIKit player using
the same Rust host also builds on both targets and runs the translated opening
in the iOS 18.3 simulator. A complete Aether iOS application and physical iOS
device execution have not been validated in this change. See the Apple player
README for save/restart/load coverage and the complete-route coverage limit.

On Linux arm64, the provider builds and passes its ABI contract test, then runs
the translated Amatsutsumi opening for 2,770 frames into Chinese dialogue.
Android API 26 provider executables link for all four ABIs with NDK 28.2.
An API 33 arm64 emulator passes the provider contract, host error/input tests,
and six save-profile tests covering locking, atomic replacement, backups and
reopening. An Android application harness using Aether's context initialization
opens the game's startup resources twice with native audio and pause/resume.
This verifies the embedding and audio lifecycle; a complete Aether Android APK
and physical Android device gameplay have not been validated locally.

The game-local save regression runs both original Japanese and PKP-translated
Amatsutsumi resources through the shared player: save, close, reopen, load and
resave produce identical gameplay snapshot bytes. The provider replay also
checks that preferences land in `game/savedata/cmvs.cfg` instead of application
storage. Storage tests cover one-time migration, existing local saves, profile
locks, backups, read-only fallback and rejection of redirected save directories.

From the parent Aether checkout, a standalone provider contract test can be run
without distributing game assets:

```sh
cmake -S packages/cmvs-rs/integrations/aether -B out/cmvs-provider -G Ninja \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DAETHERKIRI_CMVS_ABI_INCLUDE="$PWD/abi/include"
cmake --build out/cmvs-provider --target cmvs-provider-test
ctest --test-dir out/cmvs-provider --output-on-failure
```

The executable optionally accepts `/path/to/game /path/to/font.otf /path/to/frame.ppm`
to replay the Amatsutsumi opening in an isolated temporary save directory.
The probe hard-links resource files into a temporary game directory; set
`TMPDIR` to a writable directory on the game's filesystem if necessary.
No game assets or generated snapshots belong in this repository.
