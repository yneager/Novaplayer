# AI Agent Handoff

## Last Updated
- Date: 2026-09-23
- Model/Agent: GPT-5.6 Sol
- Branch: `main`
- Latest commit: `027178e0f5b4c12100cc5c1e006c8ea700f7bf18` — `Include QByteArray for track property helpers` (latest code commit reviewed before this handoff documentation commit)

## Project Summary
NovaPlayer is a small Windows desktop video player implemented in C++20 with Qt 6 Widgets and libmpv. Qt owns the application window and controls, while libmpv is embedded into a native Qt video widget and provides media playback, codec support, hardware decoding, timing, audio, and subtitle handling.

The repository currently targets a portable Windows x64 build. GitHub Actions installs Qt, downloads a current Windows libmpv development package, generates an MSVC import library for libmpv, builds the Release executable, runs `windeployqt`, and uploads a `NovaPlayer-Windows-x64` artifact.

## Current State
The current `main` branch contains a functional single-window player implementation with:

- Local file opening through a file dialog.
- Drag-and-drop loading of local media.
- Optional media path loading from the first command-line argument.
- Play/pause.
- Absolute seeking through a slider and relative ±5 second keyboard seeking.
- Current-position and duration display.
- Volume and mute controls.
- Playback speed choices from 0.50x through 2.00x.
- Fullscreen mode.
- Fullscreen controls implemented as an overlay so showing the controls does not resize the video area.
- Animated fullscreen control slide-in/slide-out with automatic hiding and cursor hiding.
- Audio track selection populated from mpv's `track-list`.
- Subtitle track selection, including an Off option.
- Loading external subtitle files and selecting them through mpv.
- Hardware decoding requested through mpv with `hwdec=auto-safe`.
- A dark Qt Fusion-based UI.
- Windows portable packaging through GitHub Actions.

The current source code is newer than the README/CMake version label: `README.md` and `CMakeLists.txt` still identify the project as v0.1 even though audio/subtitle controls were added afterward.

## Recently Completed
Recent commits on `main`, newest first:

- `027178e0` — added the explicit `QByteArray` include required by the track-property helper declarations.
- `1b1fcc5a` — added audio track selection, subtitle selection, and external subtitle loading.
- `7b26d108` — changed fullscreen controls into an overlay with slide animation so they no longer resize the video.
- `a5ae10f3` — improved fullscreen control visibility and video sizing behavior.
- `a4c502e9` — fixed Qt AUTOMOC detection for `MainWindow`.
- `438de63d` / `ded3dcc4` — earlier Qt MOC/AUTOMOC build fixes.
- `380ab362` / `ac2c3f3f` — added the Windows portable GitHub Actions workflow and the libmpv MSVC import-library helper.
- Earlier commits added the Qt/libmpv player source, CMake configuration, and initial README.

The latest reviewed code commit, `027178e0`, completed GitHub Actions run #8 successfully using the `Build Windows Portable` workflow.

## Work In Progress
- The recent audio/subtitle UI is present in code and builds successfully, but the repository has no automated runtime tests for track switching or external subtitle loading.
- Project documentation/version metadata has not caught up with the latest implementation: the README title and CMake project version still say v0.1.
- No TODO/FIXME/XXX markers were found in the tracked source during this review.

RIFE frame interpolation and realtime upscaling are mentioned in `README.md` as future work, but there are currently no RIFE/upscaling source files or dependencies in the repository.

## Next Recommended Tasks
1. Manually verify the recent audio-track, subtitle-track, and external-subtitle features on Windows with representative multi-track media, then update `README.md` and the CMake project version to match the actual release state.
2. Add RIFE frame interpolation as an isolated, optional playback enhancement, preserving the existing libmpv playback path when interpolation is disabled.
3. Add realtime upscaling after the interpolation path is stable, as already identified in the README's future direction.
4. Add automated tests or a small testable abstraction for non-GUI logic as the codebase grows; currently the repository relies on successful compilation/packaging plus manual runtime testing.

## Known Issues / Bugs
No verified runtime bugs were identified during this repository review.

Verified project-state issues/gaps:

- `README.md` still says `NovaPlayer v0.1` and does not mention the newly implemented audio/subtitle controls.
- `CMakeLists.txt` still declares project version `0.1.0`.
- There is no automated test suite, lint job, or static-analysis job in the repository.
- The repository has no checked-in local-development dependency manager or vendored Qt/libmpv dependencies; local builds require those dependencies to be supplied externally.

## Tests / Verification
### Automated tests
No test files, test directories, CTest configuration, or other automated test framework were found in the tracked repository.

### Build verification
The latest reviewed code commit before this documentation change was:

- Commit: `027178e0f5b4c12100cc5c1e006c8ea700f7bf18`
- GitHub Actions workflow: `Build Windows Portable`
- Run: #8 / `35871621456`
- Result: **success**
- Verified stages: checkout, Qt installation, libmpv download, import-library generation, CMake configure, Release build, portable packaging, and artifact upload.

The workflow's effective build commands are:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DMPV_INCLUDE_DIR="<libmpv include root>" -DMPV_LIBRARY="<path to mpv.lib>"
cmake --build build --config Release --parallel
```

Packaging then copies `NovaPlayer.exe` and `libmpv-2.dll` and runs Qt's `windeployqt`.

There is no lint or typecheck command configured.

### Repository-status audit note
The remote branch head and complete tracked tree were verified through the GitHub API. A local `git clone`/ `git status` attempt from the agent execution environment could not reach `github.com` because that shell environment had no DNS/network access. GitHub reports the current/default branch as `main`; the repository does not expose a server-side uncommitted working-tree state.

## Important Files
- `README.md` — short project overview, Windows artifact instructions, current basic feature list, and the stated future direction toward RIFE/upscaling.
- `CMakeLists.txt` — C++20/Qt 6/libmpv build definition; enables `CMAKE_AUTOMOC`, locates libmpv, and defines the `NovaPlayer` executable.
- `src/main.cpp` — QApplication setup, Fusion style/dark stylesheet, initial window sizing, and optional command-line media loading.
- `src/mainwindow.h` — `MainWindow` interface, Qt slots/signals, mpv handle, playback controls, track selectors, and fullscreen overlay state.
- `src/mainwindow.cpp` — main application behavior: UI construction, libmpv initialization/event handling, media commands, track enumeration/selection, external subtitles, keyboard input, drag/drop, and fullscreen overlay animation.
- `.github/workflows/windows-portable.yml` — Windows 2022 CI build and portable packaging workflow; runs on pushes to `main` and manual workflow dispatch.
- `.github/scripts/make-mpv-lib.ps1` — extracts exports from the downloaded libmpv DLL with `dumpbin`, creates a DEF file, and generates `mpv.lib` with MSVC `lib.exe`.

No tests, migrations, database schema, authentication configuration, or additional documentation files were present before this handoff was added.

## Architecture / Important Decisions
- **Qt + libmpv split:** Qt implements the desktop UI; libmpv handles playback. The code does not implement its own demuxer/decoder.
- **Native embedding:** `video_` is forced to a native widget and its `winId()` is passed to mpv through the `wid` option before `mpv_initialize()`.
- **Hardware decode:** mpv is configured with `hwdec=auto-safe`.
- **mpv UI/input ownership:** mpv's OSC and default input bindings are disabled; NovaPlayer supplies its own Qt controls and keyboard handling.
- **Qt-thread event handling:** the mpv wakeup callback emits `mpvWakeup()`; that signal is connected to `processMpvEvents()` with `Qt::QueuedConnection`, keeping event processing on the Qt thread.
- **Observed mpv properties:** `time-pos`, `duration`, `pause`, and `mute` are observed and reflected in the UI.
- **Track handling:** after `MPV_EVENT_FILE_LOADED`, the UI queries `track-list/count` and `track-list/<index>/...` properties. Audio changes set `aid`; subtitle changes set `sid`; external subtitles use `sub-add <path> select`.
- **Fullscreen overlay:** outside fullscreen, controls live in the main vertical layout. Entering fullscreen removes the controls widget from that layout and uses it as a native child overlay with animated geometry. This is specifically intended to prevent the controls from changing the video widget's fullscreen geometry.
- **Portable CI packaging:** the workflow downloads a shinchiro/mpv-winbuild-cmake development archive, locates the headers/DLL, generates an MSVC import library, builds with Visual Studio 2022 x64, and uses `windeployqt`.
- **Qt MOC requirement:** `MainWindow` uses `Q_OBJECT`; `CMAKE_AUTOMOC ON` is required. Commit history shows previous linker failures when MOC generation/detection was not correct.

## Environment / Setup Notes
### Local build requirements
- Windows x64.
- Visual Studio 2022 C++ toolchain.
- CMake 3.21 or newer.
- Qt 6 Widgets development installation.
- libmpv development headers and a linkable MSVC import library.

CMake accepts:

- `MPV_ROOT`
- `MPV_INCLUDE_DIR`
- `MPV_LIBRARY`

A typical local configure/build is:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DMPV_INCLUDE_DIR="<path>" -DMPV_LIBRARY="<path>"
cmake --build build --config Release --parallel
```

### CI-generated environment variable names
The Windows workflow uses these generated variables:

- `MPV_INCLUDE_ROOT`
- `MPV_DLL`
- `MPV_LIB`
- `QT_ROOT_DIR`

No repository secrets, API keys, databases, external service credentials, or authentication environment variables were identified.

## Do Not Break
- Keep `Q_OBJECT` in `MainWindow` and keep CMake AUTOMOC enabled. The history contains repeated Qt meta-object linker failures before this was corrected.
- Do not move mpv event processing directly onto mpv's callback thread. Preserve the wakeup-callback → Qt signal → queued `processMpvEvents()` pattern unless the threading model is deliberately redesigned and tested.
- Preserve the fullscreen overlay behavior: controls should overlay the video in fullscreen rather than consume layout space and resize the video.
- Be careful when changing native-widget attributes or parentage for `video_` and `controls_`; libmpv is attached to the native `video_` window handle, and fullscreen controls are intentionally raised as a separate native child.
- Preserve the Windows packaging path unless replaced deliberately: the workflow currently depends on the generated `mpv.lib`, copies the libmpv DLL as `libmpv-2.dll`, and lets `windeployqt` collect Qt runtime dependencies.
- Do not claim RIFE interpolation or realtime upscaling is implemented until corresponding code/dependencies and verification actually exist; the current repository only mentions them as future work.

## Notes for the Next AI Agent
Start by reading this file, then run `git log --oneline -15` and inspect the current `main` diff/tree before changing code.

The latest development area is audio/subtitle track handling. Confirm those controls work at runtime on Windows before treating that feature as fully release-verified. Bring README/CMake versioning in sync with the code.

After the base player and track controls are verified, the repository's documented next major direction is RIFE frame interpolation, followed by realtime upscaling. Introduce those features incrementally and keep the existing no-enhancement libmpv path working as a fallback.

When making changes on `main`, use the existing GitHub Actions `Build Windows Portable` workflow as the minimum build/package gate and do not report success unless the workflow actually passes.
