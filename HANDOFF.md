# AI Agent Handoff

## Last Updated
- Date: 2026-09-23
- Model/Agent: Claude (claude-fable-5-1 / claude-opus-5-5)
- Branch: `main`
- Previous code commit: `3314ef130cc42cbc3f86fbee60ab6efc9a64f05b` — `Fix MKV seeking and subtitle selection`
- This commit: "Add optional RIFE 2× frame interpolation via mpv VapourSynth filter" (see `git log`; CI run number is reported in the session summary and should be recorded by the next agent after checking Actions)

## Project Summary
NovaPlayer is a small Windows desktop video player implemented in C++20 with Qt 6 Widgets and libmpv. Qt owns the window and controls; libmpv is embedded into a native Qt widget (`wid`) and does decoding, rendering, timing, audio and subtitles.

The repository targets a portable Windows x64 build produced by the `Build Windows Portable` GitHub Actions workflow (artifact `NovaPlayer-Windows-x64`).

## Latest Fix: RIFE Activation (after user report)
The first RIFE build (PR #1) was built by CI. When the user selected RIFE 2× it failed with "mpv could not create the VapourSynth filter: error running command".

Root cause: mpv on Windows caches the environment on its first `getenv()` call (`osdep/io.c`, `init_getenv`, run once). NovaPlayer set `VSSCRIPT_PATH` only when RIFE was selected, long after `mpv_create()`, so mpv never saw it and `dlopen("VSScript.dll")` failed. The earlier local tests used `mpv.exe` with the variable set before launch, which is why they did not catch it.

Fix and verification: `InterpolationController::configureProcessEnvironment()` is now called in `MainWindow::initMpv()` before `mpv_create()`. `setMode()` additionally preloads `vsscript.dll` by full path with `LoadLibraryExW`. A ctypes harness driving the pinned `libmpv-2.dll` in-process confirmed three results:
- the old order reproduces the user's error;
- setting the variable early gives 47.9996 fps;
- preloading only gives 47.9996 fps.

This must still be confirmed in `NovaPlayer.exe` by the user.

## Runtime Verification Of The Base Player (user-confirmed)
The user manually tested the build of commit `3314ef13` (Actions run #10) and confirmed these work correctly:

- video playback
- MKV seeking (progress-bar click, progress-bar drag, Left/Right ±5 s)
- embedded subtitles (selection and visible rendering)
- external subtitles
- multiple audio tracks
- fullscreen overlay controls (slide-up animation, auto-hide)

These are the "do not break" baseline for all further work.

## Current State
Everything from the base player above, plus:

- **Frame Interpolation: Off | RIFE 2×** combo box in the track row of the control bar. Default Off.
- RIFE 2× = mpv's built-in `vapoursynth` video filter running `rife/rife.vpy`, which calls the existing VapourSynth-RIFE-ncnn-Vulkan plugin (RIFE v4.6, ncnn/Vulkan). NovaPlayer contains no decoding, rendering or inference code.
- Portable runtime (embedded Python + VapourSynth + plugin + model) is assembled by CI from pinned, SHA-256-verified downloads.

### Pipeline
`media file → libmpv decode → mpv vapoursynth filter (@novarife) → VapourSynth R80 → misc.SCDetect → YUV→RGBS → rife.RIFE(factor 2/1, sc=True, v4.6) → RGBS→source YUV format → libmpv rendering (subtitles/OSD drawn afterwards by mpv) → NovaPlayer video widget`

### How the filter is added/removed
`InterpolationController` (`src/interpolationcontroller.{h,cpp}`):
1. Checks that `rife/rife.vpy`, `rife/librife_windows_x86-64.dll`, `rife/MiscFilters.dll`, `rife/models/rife-v4.6_ensembleFalse/flownet.{bin,param}`, `vapoursynth/python.exe`, `vapoursynth/python3.dll` and `vapoursynth/Lib/site-packages/vapoursynth/vsscript.dll` exist next to `NovaPlayer.exe`.
2. `VSSCRIPT_PATH` is set to the bundled `vsscript.dll` at startup, **before `mpv_create()`**, because mpv caches the environment. When RIFE is selected, `vsscript.dll` is also preloaded by full path.
3. Runs `vf add @novarife:vapoursynth=file=%N%<rife.vpy>:user-data=%N%<rife dir>` (mpv `%len%` quoting because Windows paths contain `:`).
4. Off runs `vf remove @novarife` — only that label; unrelated filters are never cleared.

Failure handling:
- Missing files → error dialog, combo reverts to Off, no filter added.
- `vf add` fails synchronously (e.g. VSScript cannot load) → mpv does not add the filter; error dialog, Off.
- Script/plugin fails when video reaches it (missing model, no Vulkan GPU, Python exception) → mpv logs `Disabling filter novarife because it has failed.` and passes video through unfiltered. NovaPlayer requests error-level mpv log messages, detects that line, removes `@novarife`, reverts the combo to Off and shows the Python exception text.

## Verification Status

### Verified: libmpv VapourSynth support (dependency audit)
- The workflow previously downloaded `shinchiro/mpv-winbuild-cmake` **latest**. It is now pinned to release `20260923`, asset `mpv-dev-x86_64-20260923-git-6fd80b2003.7z`, SHA-256 `372F29C292D0C8B4CE916225739E5872E35B8E11F3F4590C285BAED8BA551100`.
- `libmpv-2.dll` from that exact archive was inspected: it contains the build configuration string `-Dvapoursynth=enabled`, the feature list includes `vapoursynth`, and it contains the filter strings `VapourSynth bridge`, `VSScript.dll`, `getVSScriptAPI`, `buffered-frames`, `video_in`, `container_fps`. No dependency change was needed.
- The same release's `mpv.exe` (`mpv-x86_64-20260923-git-6fd80b2003.7z`, SHA-256 `A2FC7178EE5D49869B7719E907ED402D6191E27167D4631FF9AA2592910632AA`, mpv v0.41.0-1055-g6fd80b200) is built from the same source and lists `vapoursynth  VapourSynth bridge` in `--vf=help`. It was used as the runtime test vehicle below.
- mpv source (`video/filter/vf_vapoursynth.c`): on Windows it `dlopen`s `$VSSCRIPT_PATH`, else `VSScript.dll`; the script is reloaded on every seek; output timing comes from `_DurationNum/_DurationDen`, which the RIFE plugin halves for factor 2.

### Runtime-verified locally (mpv.exe from the pinned build + the exact pinned runtime, Windows 11, AMD Radeon RX 9070 XT, driver 32.0.31041.1004)
Driven over mpv's JSON IPC with the same `vf add/remove @novarife` commands NovaPlayer issues, and the runtime laid out exactly as the CI script assembles it:

| Check | Result |
|---|---|
| 24 fps H.264 720p, RIFE 2× | `estimated-vf-fps` 24.02 → **47.9996** |
| 30 fps H.264 720p, RIFE 2× | 30.03 → **60.0006** |
| Generated frames | screenshot shows the on-frame counter blended between two source frames (true synthesized frame, not duplication / display interpolation) |
| `hwdec=auto-safe` (d3d11va) with RIFE | works: mpv's autoconvert inserts `HW-downloading from d3d11` + `nv12 -> yuv420p` automatically; `hwdec-current` stays `d3d11va` → **NovaPlayer keeps `auto-safe`, no hwdec change needed** |
| `hwdec=no` with RIFE | works |
| Seek (absolute+exact) while active | works; script reloads on seek as documented, stays at 48 fps |
| Pause / resume while active | works |
| `vf remove @novarife` | back to 24 fps and `d3d11` direct HW output (normal path restored) |
| Remove then re-add | works |
| External SRT subtitle while active | rendered crisply on top of the interpolated video (subtitles are not passed through RIFE) |
| VSScript not loadable | `vf add` returns error, filter not added, playback continues |
| Script error (plugin path) / missing model | mpv logs Python exception, disables `novarife`, playback continues unfiltered |
| `resources/rife/rife.vpy` | compiles with the bundled Python 3.12.10 |
| `.github/scripts/assemble-rife-runtime.ps1` | ran locally: all 6 downloads SHA-256 verified, layout correct, runtime ≈ 51 MB |

### Build verification
- Pushed with this commit; the `Build Windows Portable` run for it must be checked. NovaPlayer's C++ changes were **not** compiled locally (no Qt/MSVC on the agent machine). If CI fails, fix it before anything else.

### NOT yet verified (needs the user with the CI artifact)
- `NovaPlayer.exe` itself with RIFE 2× selected (UI path, env var hand-off to libmpv, error dialogs).
- Progress-bar click/drag and Left/Right seeking in NovaPlayer with RIFE on.
- Embedded MKV subtitles, audio-track switching, mute/volume and A/V sync over a long playback with RIFE on.
- Fullscreen overlay slide/auto-hide with RIFE on.
- NVIDIA and Intel GPUs; low-end GPUs / 1080p–4K real-time performance (only 720p on an RX 9070 XT was tested).
- Running on a clean PC without the VC++ redistributable (the workflow now bundles the MSVC runtime DLLs app-locally for this).
- HDR / 10-bit content: the script converts using the source matrix and returns the source pixel format, but no transfer-function handling is done in the RGB step; HDR (PQ/HLG) through RIFE is **unverified and not claimed as supported**. Selecting Off gives normal HDR playback.

## Pinned Third-Party Versions
| Component | Pin | SHA-256 |
|---|---|---|
| libmpv (shinchiro) | `20260923` / `mpv-dev-x86_64-20260923-git-6fd80b2003.7z` | `372F29C292D0C8B4CE916225739E5872E35B8E11F3F4590C285BAED8BA551100` |
| Python embeddable | 3.12.10 | `4ACBED6DD1C744B0376E3B1CF57CE906F9DC9E95E68824584C8099A63025A3C3` |
| VapourSynth | R80 `VapourSynth64-Portable-R80.zip` | `5D927152D9DB29D104C8960BF44D0DF7777835F7741D310184BFAE6FDA2F4F22` |
| VapourSynth-RIFE-ncnn-Vulkan | `r9_mod_v33` (commit `c3ec6aabc07c8fa37a4f58d7fed9e2ad1fc1b13f`) `librife_windows_x86-64.dll` | `36A25B471BE88E6F915320C818022DC8657DD9BEAC22A8C3158BD7F4260CC410` |
| RIFE v4.6 model `rife-v4.6_ensembleFalse/flownet.bin` | plugin commit above | `03393CF14FE6D0AF015D24D93BAE1E5925F2828CB3B6555300A132643C617AA5` |
| `…/flownet.param` | plugin commit above | `AE9B08AF43FBA97E27AA2C40C04C35B1A75F637B2D2B01A941FA94361AB25CBD` |
| vs-miscfilters-obsolete | R2 `miscfilters-r2.7z` | `54CF54C4D66151C01C1C663ED0D47CA99C7F1B0A94927BD99B35362C02172BD2` |

Model identifier: the plugin's model index 23 = `rife-v4.6 (ensemble=False)` maps to folder `models/rife-v4.6_ensembleFalse` in this plugin version (verified in the pinned README, the repository tree and the strings in the pinned DLL). The script passes `model_path` explicitly, so no index mapping is relied on.

GPU selection: not set; the plugin uses `ncnn::get_default_gpu_index()`.

## Important Files
- `src/interpolationcontroller.{h,cpp}` — RIFE glue (paths, file checks, `VSSCRIPT_PATH`, `vf add/remove @novarife`, failure detection).
- `src/mainwindow.{h,cpp}` — UI; interpolation combo, mpv log-message forwarding, error dialog.
- `resources/rife/rife.vpy` — VapourSynth script (adapted from MIT `Lafourkad/mpv-RIFE`).
- `.github/scripts/assemble-rife-runtime.ps1` — pinned, hash-verified runtime assembly.
- `.github/workflows/windows-portable.yml` — build + package (pinned libmpv, MSVC runtime, RIFE runtime).
- `.github/scripts/make-mpv-lib.ps1` — MSVC import library for libmpv.
- `THIRD_PARTY_NOTICES.md`, `licenses/` — license documentation (copied into the package).

Portable package layout additions:
```
NovaPlayer.exe, libmpv-2.dll, Qt DLLs, msvcp140*.dll/vcruntime140*.dll
THIRD_PARTY_NOTICES.md, licenses\
rife\rife.vpy, rife\librife_windows_x86-64.dll, rife\MiscFilters.dll, rife\models\rife-v4.6_ensembleFalse\
vapoursynth\python.exe, python3.dll, python312.dll, python312.zip, python312._pth (+ "Lib\site-packages")
vapoursynth\Lib\site-packages\vapoursynth\ (vsscript.dll, libvapoursynth.dll, vapoursynth.pyd, ...)
```

## Architecture / Important Decisions
- Qt + libmpv split and native `wid` embedding are unchanged.
- Interpolation is only an mpv video filter; no custom decoder/renderer/inference. The filter is labelled so it can be removed without touching other filters.
- `hwdec=auto-safe` is kept: mpv downloads HW frames automatically when the VapourSynth filter is active, and returns to direct HW output when it is removed (verified). `mpv-RIFE` uses `--hwdec=no`, which was tested and is not needed.
- mpv's `vapoursynth` filter only accepts planar YUV; the script refuses other input and mpv falls back to unfiltered playback.
- License choice: only MIT/BSD/LGPL code/binaries are used; `president-not-sure/mpv-interpolation` (GPLv2) was read for ideas only, nothing copied. The bundled libmpv is a GPL build (as before this milestone) — see `THIRD_PARTY_NOTICES.md`.
- mpv event handling threading model is unchanged (wakeup → queued `processMpvEvents()`); log messages arrive through the same event loop.

## Do Not Break
- Keep `Q_OBJECT` + `CMAKE_AUTOMOC` (both `MainWindow` and `InterpolationController` use `Q_OBJECT`).
- Keep seek flags as one argument (`absolute+exact`).
- Keep the fullscreen overlay behaviour and the native-widget setup of `video_`/`controls_`.
- Never clear the whole mpv `vf` chain; only remove `@novarife`.
- Do not change dependency pins without updating hashes, `THIRD_PARTY_NOTICES.md` and this file.
- RIFE must stay optional: any failure must leave normal playback working and the combo on Off.
- Any environment variable meant for mpv or its filters must be set **before `mpv_create()`**; mpv on Windows never re-reads the environment.
- Test mpv integration changes in-process against `libmpv-2.dll` (for example with a ctypes harness), not only with `mpv.exe` launched with a prepared environment.

## Next Recommended Tasks
1. Confirm the CI run for this commit passed; download the artifact and runtime-test in `NovaPlayer.exe`: RIFE 2× on a 24 fps and a 30 fps file (use mpv stats / visual smoothness), seek (click, drag, arrows), pause, embedded + external subtitles, audio-track switch, mute/volume, fullscreen overlay, Off → normal playback.
2. Test the failure path by renaming `rife\models` in the extracted artifact → selecting RIFE 2× should show an error and stay on Off.
3. Test on NVIDIA and Intel GPUs and on 1080p/4K content; record performance.
4. Later milestones (not started): target-FPS modes, display-Hz matching, GPU selector, model selector, presets, upscaling, settings persistence.
