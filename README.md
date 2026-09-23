# NovaPlayer

**Current version: v0.2.0**

NovaPlayer is a Windows video player built with C++20, Qt 6 and libmpv, with optional real-time RIFE frame interpolation through mpv's VapourSynth filter.

## Version History

### v0.2.0 — RIFE Frame Interpolation

**Added**
- Real-time RIFE frame interpolation using the existing VapourSynth-RIFE-ncnn-Vulkan + ncnn/Vulkan stack.
- Portable bundled RIFE runtime; no separate Python, VapourSynth or RIFE installation is required.
- Frame Interpolation selector with:
  - **Original (source fps)** — normal libmpv playback.
  - **Double frame rate (RIFE)** — for example 24 → 48 fps and 30 → 60 fps.
  - **60 fps (RIFE)** — for supported sources below 60 fps.
- Dynamic interpolation labels showing the video's actual frame rates.
- Scene-change handling through the existing open-source RIFE/VapourSynth stack.
- Safe fallback to normal playback if the RIFE runtime/filter cannot start.
- Pinned and SHA-256-verified interpolation dependencies in the Windows build.
- Third-party notices and bundled license files.

**Fixed**
- Fixed RIFE activation on Windows by configuring `VSSCRIPT_PATH` before `mpv_create()` and preloading `vsscript.dll`.
- Prevented the interpolation filter from damaging unrelated mpv video filters by adding/removing only the labelled `@novarife` filter.
- 60 fps mode is disabled/refused for sources already at about 60 fps or higher instead of applying an invalid interpolation target.

**Verified**
- The RIFE feature and current interpolation modes have been runtime-tested by the user and reported working correctly.
- The portable Windows build is produced through GitHub Actions.

### v0.1.0 — Base Player

This is the original NovaPlayer base-player milestone before RIFE was added.

**Added**
- Local video playback through libmpv.
- MP4, MKV and other formats supported by mpv.
- Open-file dialog and drag-and-drop loading.
- Play/pause.
- Progress-bar seeking.
- Left/Right ±5 second keyboard seeking.
- Current time and duration display.
- Volume and mute.
- Playback speed control.
- Fullscreen mode.
- Dark Qt/Fusion interface.
- Hardware decoding through mpv with `hwdec=auto-safe`.
- Audio-track selection.
- Embedded subtitle-track selection.
- External subtitle loading.
- Windows portable GitHub Actions build.

**Fixed**
- Fixed Qt MOC/AUTOMOC generation and linking for `MainWindow`.
- Fixed fullscreen controls so they overlay the video instead of shrinking the video area.
- Added smooth fullscreen control slide-in/slide-out and auto-hide.
- Fixed MKV/progress-bar seeking by using mpv's correct combined `absolute+exact` seek flags.
- Added click-to-seek and made playback shortcuts work even when child controls have keyboard focus.
- Fixed audio/subtitle switching by setting mpv `aid`/`sid` directly.
- Explicitly enabled subtitle visibility, embedded fonts and Matroska subtitle preroll.

**Verified**
- The user runtime-tested the v0.1 playback baseline, including MKV seeking, embedded/external subtitles, multiple audio tracks and fullscreen controls.

> **Versioning rule:** when NovaPlayer moves to a new version, add the new version at the top of this section with its **Added**, **Changed** and/or **Fixed** items. Never remove older version entries. Detailed development history remains in [CHANGELOG.md](CHANGELOG.md).

## Automatic Windows build

Open **Actions → Build Windows Portable → Run workflow**.

When it finishes, download the **NovaPlayer-Windows-x64** artifact, extract it and run `NovaPlayer.exe`.

No local Qt, Visual Studio, CMake, libmpv, Python or VapourSynth installation is required for this test route. The portable package contains the frame-interpolation runtime.

## Current Features

- Open MP4/MKV and other formats supported by mpv
- Drag and drop
- Play/pause and seeking (click/drag the progress bar, Left/Right ±5 s)
- Volume/mute
- Playback speed
- Audio track and subtitle track selection
- External subtitle loading
- Fullscreen with overlay controls (slide-in/out, auto-hide)
- Hardware decoding through mpv (`hwdec=auto-safe`)
- Keyboard: Space, Left/Right, M, F, Esc
- Real-time RIFE interpolation: original frame rate, doubled frame rate, or 60 fps

## Frame Interpolation (RIFE)

The `Frame Interpolation` selector is labelled with the current video's real frame rates. For a 24 fps video, for example:

- **Original (24 fps)** — normal libmpv playback with no interpolation overhead.
- **48 fps (RIFE)** — doubles the source frame rate with the bundled RIFE v4.6 model.
- **60 fps (RIFE)** — interpolates supported sources below 60 fps to 60 fps.

Before a file is loaded the entries read **Original**, **Double frame rate (RIFE)** and **60 fps (RIFE)**.

Pipeline:

`media file → libmpv decode → mpv VapourSynth filter → VapourSynth → VapourSynth-RIFE-ncnn-Vulkan / ncnn / Vulkan → libmpv rendering`

NovaPlayer does not contain a custom RIFE implementation. It adds a labelled mpv filter (`@novarife`) that runs the bundled `rife/rife.vpy` script. Audio, subtitles, seeking, timing and final rendering remain handled by libmpv.

Requirements: a Vulkan-capable NVIDIA, AMD or Intel GPU with a current vendor driver and enough performance for the video's resolution. If interpolation cannot initialize, NovaPlayer falls back to normal playback.

Not included yet: additional target frame rates, display-Hz matching, GPU/model selection, quality presets, realtime upscaling and settings persistence.

## Third-party Components

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the `licenses/` directory for the bundled components and their licenses. Third-party interpolation dependencies are pinned and SHA-256 verified by the GitHub Actions packaging workflow.

## Project Documentation

- [HANDOFF.md](HANDOFF.md) — current state, verification status and instructions for the next developer/AI agent.
- [CHANGELOG.md](CHANGELOG.md) — detailed chronological development history.
