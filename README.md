# NovaPlayer

**Current version: v0.3.1**

NovaPlayer is a Windows video player built with C++20, Qt 6 and libmpv, with optional real-time RIFE frame interpolation through mpv's VapourSynth filter.

## Version History

### v0.3.1 — Exact Vui Homepage / Smooth WebEngine Rendering

This corrective milestone replaces the hand-recreated native Qt homepage with the **actual current Vui HTML/CSS** rendered locally through Qt WebEngine.

**Changed**
- The homepage now renders the real `yneager/Vui/index.html` + `home.css` instead of an approximation made from custom Qt painting.
- Chromium/WebEngine handles Vui's CSS grid/flex layout, blur/backdrop-filter, masks, gradients, transforms, hover transitions and keyframe animations directly.
- Enabled Qt WebEngine's scroll animator so wheel/anchor scrolling is animated instead of jumping between Qt scroll positions.
- The Vui page remains offline: Google Fonts network references were removed and Inter is bundled locally from a pinned upstream commit.
- Player/card links are intercepted inside the local page and routed to NovaPlayer's existing local-file/player navigation; no remote website is loaded.
- The first Continue Watching card can become the current-session Resume entry without changing Vui's visual structure.
- The WebEngine page is frozen while the native player is visible so decorative homepage animation does not waste CPU/GPU during playback.

**Preserved**
- The native libmpv player, `video_` HWND embedding, RIFE/VapourSynth path, seek behavior, fullscreen controls, subtitles/audio tracks and existing playback code remain unchanged.

### v0.3.0 — Vui Application Shell

This milestone turns NovaPlayer into a cohesive Vui-styled desktop application while preserving the existing libmpv and RIFE playback engine.

**Added / Changed**
- NovaPlayer now launches into a native Vui streaming homepage instead of an empty video surface.
- Added a floating glass navigation bar, animated cinematic hero, Continue Watching rail, Trending poster rail, editorial Collections and Fresh Transmissions sections based on the current `yneager/Vui` design.
- Added abstract locally rendered Vui artwork, planet/orb motion, grid/light movement, hover lift/glow treatments and responsive horizontal rails. The interface has no network dependency and does not download Google Fonts or remote artwork.
- Added an explicit local **Open Video** action, hero Play action and drag/drop flow that all reuse NovaPlayer's existing `openPath()` path.
- Restored and refined the native Vui player chrome from the earlier UI work while retaining the existing native libmpv `wid` video surface.
- Added a page architecture around one persistent libmpv instance: Vui Home → Player → Vui Home.
- Player Home pauses the current item without destroying mpv; the homepage Continue Watching card becomes a session Resume action for the loaded local file.
- Added a native transition curtain between Home and Player so the transition remains compatible with the Windows native video child window.
- The player retains real actions for play/pause, playlist-next, volume/mute, accurate seeking, chapters, speed, audio/subtitle selection, external subtitles, fullscreen and RIFE. PiP remains disabled because it is not implemented.
- Branding remains NovaPlayer, presented as **NOVA / VUI** where appropriate.

**Preserved**
- libmpv playback, `hwdec=auto-safe`, MKV seek/subtitle fixes, fullscreen overlay auto-hide, audio tracks, external subtitles and all existing RIFE modes/failure fallback.
- `VSSCRIPT_PATH` is still configured before `mpv_create()`; seeking still uses the combined `absolute+exact` flag; interpolation still removes only `@novarife`.

**Packaging**
- The Vui shell is implemented with Qt 6 Widgets/custom painting and is compiled into `NovaPlayer.exe`. No Qt WebEngine or extra runtime UI asset directory is required.
- Windows portable packaging and the pinned libmpv/RIFE runtime remain unchanged.

**Verification status**
- Source integration preserves the existing playback paths by construction; the Windows portable GitHub Actions build is checked after the commit is pushed.
- Runtime playback and RIFE behavior still require user/local testing of the produced Windows artifact; CI success alone is not treated as runtime verification.

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

## Automatic Windows build and releases

Open **Actions → Build Windows Portable → Run workflow** for a normal test build.

When it finishes, download the **NovaPlayer-Windows-x64** artifact, extract it and run `NovaPlayer.exe`.

For a public GitHub Release, first update the version in `CMakeLists.txt`, commit it, then push a matching tag such as `v0.3.2`. The same workflow builds and smoke-tests the portable package, creates `NovaPlayer-Windows-x64-v0.3.2.zip`, and publishes it on the repository's **Releases** page automatically. The workflow rejects a tag whose version does not match `CMakeLists.txt`.

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
