# Changelog

This file records notable completed work visible in the repository history.

## Version Map

- **v0.2.0** — RIFE interpolation milestone: initial RIFE integration, Windows activation fix, dynamic frame-rate labels and 60 fps mode.
- **v0.1.0** — base-player milestone: libmpv playback, controls, fullscreen overlay, audio/subtitle selection, external subtitles and MKV seek/subtitle fixes.

The README keeps the user-facing permanent version history. These chronological entries retain the more detailed implementation record.

## 2026-09-23 — Frame-rate labels and 60 fps mode

### Added
- **60 fps (RIFE)** mode. `rife.vpy` computes the RIFE factor as `60 / source fps`, for example 5/2 for 24 fps, 12/5 for 25 fps, 2/1 for 30 fps and 1001/400 for 23.976 fps. It does not use the plugin's `fps_num` option, because mpv's `video_in` clip has no frame rate. The source rate is mpv's `container_fps`, snapped to the nearest standard rate within 0.5%. Output timing still comes from mpv's per-frame durations. The mode is refused for sources at 59 fps or above and greyed out in the UI.

### Changed
- The selector no longer says "RIFE 2×". Entries are labelled with the current video's real frame rates, for example "Original (24 fps)", "48 fps (RIFE)" and "60 fps (RIFE)". Before a file is loaded they read "Original", "Double frame rate (RIFE)" and "60 fps (RIFE)".
- The mode is passed to `rife.vpy` through mpv's `user-data` sub-option as `<mode>|<runtime dir>`, where mode is `double` or `60`. Switching between RIFE modes removes and re-adds only `@novarife`.

### Verification
- The pinned `mpv.exe` and runtime were tested on an RX 9070 XT. `estimated-vf-fps` after enabling each mode:

  | Source | Mode | Output |
  |---|---|---|
  | 24 fps | 60 fps | 59.9995 |
  | 25 fps | 60 fps | 60.0000 |
  | 30 fps | 60 fps | 60.0006 |
  | 24 fps | double | 47.9996 |
  | 60 fps | 60 fps | refused with a clear message; playback continues |

- The C++ changes are build-verified by CI only.

## 2026-09-23 — RIFE 2× activation fix

### Fixed
- Selecting RIFE 2× failed with "mpv could not create the VapourSynth filter: error running command" (user report on the first RIFE build). Cause: on Windows mpv snapshots the process environment once, on its first `getenv()` call (`osdep/io.c`), so the `VSSCRIPT_PATH` NovaPlayer set when RIFE was selected was never seen and mpv could not find `VSScript.dll`. Reproduced with the pinned `libmpv-2.dll`.
- Fix: `VSSCRIPT_PATH` is now set before `mpv_create()`, and `vsscript.dll` is additionally preloaded by full path before the filter is added, so mpv's by-name fallback finds it. Both variants were verified against the pinned `libmpv-2.dll` (24 fps → 48 fps); the old order still reproduces the error.

## 2026-09-23 — RIFE 2× frame interpolation (first milestone)

### Added
- `Frame Interpolation: Off | RIFE 2×` selector in the control bar (default Off). RIFE 2× adds the labelled mpv video filter `@novarife:vapoursynth=file=<rife.vpy>:user-data=<runtime dir>`; Off removes only that filter and leaves the rest of the video-filter chain untouched.
- `src/interpolationcontroller.{h,cpp}`: minimal glue that locates the bundled runtime, checks the required files, sets `VSSCRIPT_PATH`/`PATH` for libmpv's VSScript lookup, adds/removes the filter and detects mpv's "Disabling filter novarife because it has failed" log message to fall back to Off with a user-visible error. No inference code lives in NovaPlayer.
- `resources/rife/rife.vpy`: VapourSynth script adapted from the MIT-licensed `Lafourkad/mpv-RIFE` structure. Uses `misc.SCDetect` for scene changes, converts YUV→RGBS with the matrix taken from mpv's frame properties, runs `rife.RIFE(..., model_path=<rife-v4.6_ensembleFalse>, factor_num=2, factor_den=1, sc=True)` and converts back to the source pixel format. All paths are relative to the runtime directory; no hardcoded machine paths.
- `.github/scripts/assemble-rife-runtime.ps1`: assembles the portable runtime from pinned, SHA-256-verified downloads: Python 3.12.10 embeddable, VapourSynth R80 (wheel extracted into `vapoursynth/Lib/site-packages`), VapourSynth-RIFE-ncnn-Vulkan `r9_mod_v33` (`librife_windows_x86-64.dll`), RIFE v4.6 model files from plugin commit `c3ec6aab`, and vs-miscfilters-obsolete R2 (`MiscFilters.dll`).
- `THIRD_PARTY_NOTICES.md` and `licenses/` with verbatim license texts for mpv, Python, VapourSynth, the RIFE plugin, rife-ncnn-vulkan, ncnn, the RIFE model, vs-miscfilters-obsolete and mpv-RIFE; both are copied into the portable package.

### Changed
- `Build Windows Portable` now pins libmpv to shinchiro/mpv-winbuild-cmake release `20260923` (`mpv-dev-x86_64-20260923-git-6fd80b2003.7z`, SHA-256 verified) instead of downloading `latest`; this exact build was verified to contain mpv's `vapoursynth` filter and to load `VSScript.dll` at runtime.
- The workflow additionally copies the MSVC 2022 runtime DLLs app-locally (required by Qt, VapourSynth and the RIFE plugin) and runs the RIFE runtime assembly before uploading `NovaPlayer-Windows-x64`.
- mpv error-level log messages are now requested (`mpv_request_log_messages(mpv, "error")`) and forwarded to the interpolation controller.
- Project version bumped to 0.2.0; README rewritten for the current feature set.

### Verification
- User runtime-verified the MKV seeking / subtitle fixes from commit `3314ef13` (progress-bar seeking, embedded subtitles, external subtitles, multiple audio tracks, fullscreen overlay) before this milestone started.
- Local runtime verification of the interpolation stack was done with the exact pinned `mpv.exe` build (same code as `libmpv-2.dll`) plus the exact pinned runtime on Windows 11 / AMD Radeon RX 9070 XT: 24 fps → 47.99 fps and 30 fps → 60.00 fps (`estimated-vf-fps`), seek, pause/resume, filter remove/re-add, `hwdec=auto-safe` (d3d11va with mpv's automatic HW download), and the failure paths (missing VSScript, script error, missing model → playback continues, filter disabled). See `HANDOFF.md` for details and for what is not yet verified inside `NovaPlayer.exe` itself.

## 2026-09-23 — base player

### Added
- Created the initial C++20 / Qt 6 / libmpv Windows player application and CMake build configuration.
- Added local file opening, drag/drop loading, play/pause, seeking, time display, volume/mute, playback speed, fullscreen, hardware-decoding configuration, and keyboard shortcuts.
- Added the `Build Windows Portable` GitHub Actions workflow for Windows 2022 / MSVC 2022 x64.
- Added automatic libmpv development-package download and `.github/scripts/make-mpv-lib.ps1` to generate the MSVC import library from the libmpv DLL exports.
- Added audio-track and subtitle-track selectors populated from mpv's track list.
- Added external subtitle loading for common subtitle formats.

### Changed
- Reworked fullscreen controls so they overlay the video rather than reducing the video area's size.
- Added animated fullscreen control slide-in/slide-out, automatic hiding, and cursor hiding.

### Fixed
- Corrected MKV/progress-bar seeking by passing mpv seek flags as a single `absolute+exact` argument; added direct click-to-seek and made playback keyboard shortcuts work even when child controls have focus.
- Strengthened audio/subtitle switching by setting mpv `aid`/`sid` runtime properties directly, explicitly enabling subtitle visibility, enabling embedded fonts, and enabling Matroska subtitle preroll.
- Corrected Qt MOC/AUTOMOC configuration and source formatting so `MainWindow` meta-object code is generated and linked correctly on Windows.
- Added the explicit `QByteArray` include required by the track-property helper declarations.

### Verification
- Code at commit `3314ef130cc42cbc3f86fbee60ab6efc9a64f05b` completed `Build Windows Portable` GitHub Actions run #10 successfully, including build, packaging, and artifact upload. Runtime verification of the reported MKV seek/subtitle behavior remains pending.
- Earlier code at commit `027178e0f5b4c12100cc5c1e006c8ea700f7bf18` completed run #8 successfully.
