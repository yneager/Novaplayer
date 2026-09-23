# NovaPlayer v0.1

Basic Windows video player built with C++20, Qt 6 and libmpv.

## Automatic Windows build

Open **Actions → Build Windows Portable → Run workflow**.

When it finishes, download the **NovaPlayer-Windows-x64** artifact, extract it and run `NovaPlayer.exe`.

No local Qt, Visual Studio, CMake or libmpv installation is required for this test route.

## Current features

- Open MP4/MKV and other formats supported by mpv
- Drag and drop
- Play/pause and seeking
- Volume/mute
- Playback speed
- Fullscreen
- Hardware decoding through mpv
- Keyboard: Space, Left/Right, M, F

RIFE frame interpolation and realtime upscaling come after the base player is verified.
