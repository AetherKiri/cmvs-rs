# Apple players

The macOS player uses the existing native window and audio backend. The iOS
player uses UIKit for folder selection, touch input and RGBA presentation, with
the shared `cmvs-host` player for the VM, software compositor, OpenType text,
native audio and saves. Xcode, Python 3 and Rust stable are required.

```sh
python3 platforms/apple/build.py macos
target/release/cmvs-desktop /path/to/game --encoding shift-jis \
  --user-data-dir /path/to/writable-user --audio native --font /path/to/font.otf

rustup target add aarch64-apple-ios aarch64-apple-ios-sim
python3 platforms/apple/build.py ios --font /path/to/font.otf
python3 platforms/apple/build.py ios-simulator --font /path/to/font.otf
```

iOS output is `target/apple/<target>/Cmvs.app`. The device app is unsigned:
sign it with your own provisioning profile and identity before installing it.
The simulator app has an ad-hoc signature. The build includes the explicitly
provided font; verify its redistribution terms before sharing the bundle.
Game files are never bundled.

Open a game folder through the iOS document picker. The security-scoped folder
remains accessible until closing the game. Input is mapped through the
letterboxed game rectangle; a short tap survives until the next VM frame.
Saves and settings go to a stable per-folder namespace under the app's
Documents/Saves directory. Selecting another folder saves preferences and
releases the previous player. Backgrounding pauses execution and mutes audio.

For simulator diagnostics, `SIMCTL_CHILD_CMVS_GAME_DIR` can point to an existing
read-only game directory when running `xcrun simctl launch`. Optional
`SIMCTL_CHILD_CMVS_SMOKE_FRAMES` and `SIMCTL_CHILD_CMVS_SMOKE_OUTPUT` capture a
completed frame and close the player after the requested number of VM frames.
These use the same player and presentation path as the document picker.
`SIMCTL_CHILD_CMVS_SMOKE_ACTIONS` accepts a JSON array of input events, each with
`frame`, optional logical `x`/`y`, and optional CMVS `code`/`pressed` fields.
For example, `[{"frame":600,"code":22,"pressed":true},{"frame":601,"code":22,"pressed":false}]`
presses and releases Enter after 600 completed frames.

## PKP translation overlays

The GBK PKP patch family used by Amatsutsumi 1.03 is supported directly:
`Pk/Scripts.pk` overlays scripts, `Pk/Graphic.pk` and `Pk/Mosic.pk` overlay
images, and loose `Pk/*.cmv` files replace movies. Script entries contain
unencrypted LZSS PS2A payloads inside raw-DEFLATE PKP entries. Image replacements
may be PNG or PB3. Movie frame and audio offsets are resolved from the same
replacement container. Indexes, paths and decompressed allocations are bounded.

`Pk/Scripts.pk` enables per-player GBK parsing and glyph decoding, including
the patch's musical-note glyph. Script offsets and save bytes are preserved.
Installations without that overlay retain Shift-JIS behavior. Supply a font
with Chinese glyphs for the translated version. No patch DLL is executed,
and no game, translation, or embedded DLL font is distributed by this project.
Other DLL hooks and loose script/image override formats are not implemented.

The host C API is in `crates/cmvs-host/cmvs_host.h`. Calls on a handle are
serialized by its owner. The host must respect cooperative budget slices:
only completed VM frames advance the serial and expose new RGBA pixels. Native
audio owns its device callback. The host does not implement desktop window
management, a font picker or a save-comment editor. Font selection is cancelled;
unsupported platform requests report errors. CMV decoding is shared with core;
the optional desktop MGV/Theora factory is not installed by this host.

## Validation, 2026-11-06

Apple Silicon macOS release compilation and iOS arm64 device/simulator linking
were tested. Both the original Japanese title and the PKP Chinese translation
ran with native audio. The iOS 18.3 simulator advanced the translated version
for 2,850 completed frames, through the replacement intro movie into Chinese
dialogue, and exported a 1280x720 frame. Device execution has not been tested.
Core/CLI/font/host tests pass, including PKP bounds and decompression, PNG,
GBK punctuation and multibyte parsing, glyph decoding, and quick input pulses.
The desktop, font, host and probe crates pass Clippy with `--no-deps -D warnings`. Whole-dependency strict
Clippy encounters the pre-existing `manual_range_contains` warning in
`cmvs-core/src/camera/view.rs` with Rust 1.96.

Both original and translated real-installation probes pass R1, I4 and V1-V4: fresh startup, backlog
round-trip, save/restart/load continuation through 20 message waits, snapshot
round-trip and backup-slot behavior. These are headless input replays, not
physical iOS touch verification. The original-resource V5, which plays a complete route before
checking system-save persistence, exhausted both a 10-million-step budget and
the default 50-million-step budget; complete-route coverage is not claimed.

The exact 1.03 startup hash is registered as `amatsutsumi-103`; its script
archive and original x86/x64 executables match the existing Amatsutsumi record.
Sharing an archive is allowed only when profile, game ID, executable evidence
and resume policy agree; unknown startup hashes still fail identification.
The Aether provider and its integration validation are described in
[`integrations/aether/README.md`](../../integrations/aether/README.md).
