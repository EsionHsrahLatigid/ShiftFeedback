# ShiftFeedback

ShiftFeedback is a YUP audio plugin instrument built from a deterministic C++20 DSP engine. It renders a MIDI-triggered, fixed-Hz frequency-shifted cross-feedback cloud with bounded self-oscillation safeguards.

## Targets

- Standalone app
- VST3 plugin
- AUv2 plugin
- Deterministic DSP regression test executable

Plugin identity:

- App ID: `audio.2bit.shiftfeedback`
- Plugin ID: `audio.2bit.ShiftFeedback`
- AU subtype: `SfBk`
- AU manufacturer: `2Bit`

## Build

The project is self-contained except for an adjacent YUP checkout at `../yup`.

```sh
cmake --preset engine-debug
cmake --build --preset engine-debug
ctest --preset engine-debug

cmake --preset plugin-release
cmake --build --preset plugin-release
ctest --preset plugin-release
```

The release preset builds the standalone, VST3, and AUv2 bundles.

## Continuous integration and releases

GitHub Actions tests and packages macOS 26 arm64 and Windows 2025 x64 builds. It uploads `ShiftFeedback-latest-macos-arm64.zip` and `ShiftFeedback-latest-windows-x64.zip`; a `v*` tag creates or updates one GitHub Release with both versioned ZIPs.
