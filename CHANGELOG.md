# Changelog

This file records notable completed work visible in the repository history.

## 2026-09-23

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
