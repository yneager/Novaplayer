# LAMBDA Player

**Current version: v0.2.3**

LAMBDA Player is a Windows video player built with C++20, Qt 6 and libmpv, with optional real-time RIFE frame interpolation through mpv's VapourSynth filter.

## Version History

### v0.2.3 — Smoother scrolling

- Scrolling the Home page (and any other scrollable part of the Vui interface) is now smooth. The web UI was being painted on the CPU, which stalled frames while scrolling; LAMBDA Player now enables GPU rasterization for its interface.
- On the test PC, late frames while scrolling dropped from 35–43% to effectively none, and the worst frame dropped from about 480 ms to one screen refresh (16.7 ms).
- No other behaviour changed.

### v0.2.2 — Working video + polished window and UI

**Fixed**
- Opening a video works again. mpv events were lost at startup, so the player stayed on "LOADING" at 00:00 forever. The event handler is now connected before mpv is created.
- Video is visible under the Vui controls. libmpv now draws through its official OpenGL render API into a Qt widget (`vo=libmpv`) instead of a separate native window. The transparent web controls could not be shown over a native window.

**Window**
- Custom frameless window with themed minimize / maximize / close buttons in the Home and Player top bars.
- Windows 11 rounded corners and shadow; native drag, Aero Snap, double-click to maximize and resize from every edge.
- Mini player: a small always-on-top window sized to the video; Esc returns to the normal window.

**UI polish**
- The player fills the window at every size, with a rounded, anti-aliased video frame; layout adapts to short and narrow windows.
- Thin cyan themed scrollbars instead of the default Windows scrollbar.
- Smooth page fades between Home and Player; animated settings panel, toasts, auto-hiding fullscreen controls and hidden cursor.
- Subtitles move above the control bar while the controls are visible.
- Loading spinner, "Finished — press play to watch again" state, timeline hover time and buffered range, tooltips everywhere.

**Every button now does something real**
- Home: Search opens a video, the folder button plays a folder, the λ button shows About with shortcuts and licenses.
- Home: "Continue watching" shows your recent files with progress, and they resume where you stopped. "Resume/Play recent" continues playback.
- Home: nav links scroll to their sections and the rail arrows scroll their rail.
- Player: Next plays the next video in the same folder, and the mini-player button toggles the mini player.
- Errors such as unplayable files and interpolation problems appear as themed toasts instead of Windows message boxes.

### v0.2.1 — LAMBDA Player / Complete Vui CSS

This is the new active version line. The product name is now **LAMBDA Player** and both major application surfaces use the current Vui HTML/CSS as their visual source.

**Changed**
- Renamed the application from LAMBDA Player to **LAMBDA Player**. The Windows executable is `LambdaPlayer.exe` and CI artifacts use `LAMBDA-Player-Windows-x64`.
- Reset the active project version to **v0.2.1**. The abandoned/current-branch v0.3.x labels are no longer part of the maintained version history.
- Home continues to render the real Vui `index.html` + `home.css` locally through Qt WebEngine.
- Player now renders the real Vui `player.html` + `styles.css` as Chromium/WebEngine chrome instead of visually approximating that CSS with Qt widgets.
- A small local WebChannel bridge maps the Vui player controls to the existing C++/libmpv backend: Open, Home, play/pause, next, mute/volume, exact seek, speed, audio tracks, subtitles, external subtitles, RIFE interpolation and fullscreen.
- The Vui settings UI is rendered in CSS and populated from the real mpv track list and interpolation state.
- The WebEngine player reports its responsive player rectangle to Qt; the existing native libmpv `video_` HWND remains the video target underneath the CSS chrome.
- Google Fonts are not loaded from the network. Inter is bundled locally from the pinned upstream font release.

**Preserved**
- libmpv remains the playback/rendering backend.
- `video_` remains the native `wid` target; this change does not switch to a different media engine or mpv render API.
- `hwdec=auto-safe`, MKV seeking, `absolute+exact` seek semantics, audio/subtitle switching, external subtitles, chapters and keyboard shortcuts remain in place.
- RIFE remains the existing mpv VapourSynth filter path and still adds/removes only `@novarife`.
- `VSSCRIPT_PATH` is still configured before `mpv_create()`.

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

This is the original LAMBDA Player base-player milestone before RIFE was added.

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

> **Versioning rule:** v0.2.1 is the reset/current line for LAMBDA Player. Future versions are added above it; v0.2.0 and v0.1.0 remain as the retained earlier milestones. Detailed development history remains in [CHANGELOG.md](CHANGELOG.md).

## Automatic Windows build and releases

Open **Actions → Build Windows Portable → Run workflow** for a normal test build.

When it finishes, download the **LAMBDA-Player-Windows-x64** artifact, extract it and run `LambdaPlayer.exe`.

For a public GitHub Release, first update the version in `CMakeLists.txt`, commit it, then push a matching tag such as `v0.2.1`. The same workflow builds and smoke-tests the portable package, creates `LAMBDA-Player-Windows-x64-v0.2.1.zip`, and publishes it on the repository's **Releases** page automatically. The workflow rejects a tag whose version does not match `CMakeLists.txt`.

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

LAMBDA Player does not contain a custom RIFE implementation. It adds a labelled mpv filter (`@novarife`) that runs the bundled `rife/rife.vpy` script. Audio, subtitles, seeking, timing and final rendering remain handled by libmpv.

Requirements: a Vulkan-capable NVIDIA, AMD or Intel GPU with a current vendor driver and enough performance for the video's resolution. If interpolation cannot initialize, LAMBDA Player falls back to normal playback.

Not included yet: additional target frame rates, display-Hz matching, GPU/model selection, quality presets, realtime upscaling and settings persistence.

## Third-party Components

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the `licenses/` directory for the bundled components and their licenses. Third-party interpolation dependencies are pinned and SHA-256 verified by the GitHub Actions packaging workflow.

## Project Documentation

- [HANDOFF.md](HANDOFF.md) — current state, verification status and instructions for the next developer/AI agent.
- [CHANGELOG.md](CHANGELOG.md) — detailed chronological development history.
