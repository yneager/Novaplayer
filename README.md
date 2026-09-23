# NovaPlayer v0.2

Windows video player built with C++20, Qt 6 and libmpv, with optional real-time
RIFE frame interpolation (2×) through mpv's VapourSynth filter.

## Automatic Windows build

Open **Actions → Build Windows Portable → Run workflow**.

When it finishes, download the **NovaPlayer-Windows-x64** artifact, extract it and run `NovaPlayer.exe`.

No local Qt, Visual Studio, CMake, libmpv, Python or VapourSynth installation is required.
The portable package contains everything, including the frame-interpolation runtime.

## Current features

- Open MP4/MKV and other formats supported by mpv
- Drag and drop
- Play/pause and seeking (click/drag the progress bar, Left/Right ±5 s)
- Volume/mute
- Playback speed
- Audio track and subtitle track selection, external subtitle loading
- Fullscreen with overlay controls (slide-in/out, auto-hide)
- Hardware decoding through mpv (`hwdec=auto-safe`)
- Keyboard: Space, Left/Right, M, F, Esc
- **Frame interpolation with RIFE**: original frame rate, double frame rate, or 60 fps (see below)

## Frame interpolation (RIFE)

The `Frame Interpolation` selector in the control bar is labelled with the
current video's real frame rates, for example for a 24 fps video:

- **Original (24 fps)** (default) – the normal libmpv playback path, no interpolation overhead.
- **48 fps (RIFE)** – doubles the frame rate with the RIFE v4.6 neural network
  (23.976 → 47.952, 24 → 48, 25 → 50, 30 → 60, 60 → 120).
- **60 fps (RIFE)** – interpolates to 60 fps (24 → 60, 25 → 60, 30 → 60).
  Only available for videos below 60 fps.

Before a file is loaded the entries read "Original", "Double frame rate (RIFE)"
and "60 fps (RIFE)".

How it works: `media file → libmpv decode → mpv vapoursynth video filter →
VapourSynth → VapourSynth-RIFE-ncnn-Vulkan (ncnn/Vulkan) → libmpv rendering`.
NovaPlayer only adds a labelled mpv filter (`@novarife`) that runs the bundled
script `rife/rife.vpy`; audio, subtitles, seeking, timing and rendering stay in
libmpv. Selecting Off removes that single filter again.

Requirements: a Vulkan-capable GPU (NVIDIA, AMD or Intel) with a current vendor
driver, and enough GPU performance for the video's resolution. If the runtime,
GPU or driver is unavailable, NovaPlayer shows a message, switches back to Off
and playback continues normally.

Not included yet (planned as later milestones): other target frame rates, display-Hz
matching, GPU/model selection, quality presets, upscaling, settings persistence.

## Third-party components

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the `licenses/`
directory for the bundled components (Qt, libmpv, Python, VapourSynth,
VapourSynth-RIFE-ncnn-Vulkan, rife-ncnn-vulkan, ncnn, RIFE model,
vs-miscfilters-obsolete) and their licenses. All third-party versions are
pinned and SHA-256 verified in the GitHub Actions workflow.

## Project documentation

- [HANDOFF.md](HANDOFF.md) – current state, verification status and notes for the next developer/agent.
- [CHANGELOG.md](CHANGELOG.md) – notable completed work.
