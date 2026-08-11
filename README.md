# ShiftFeedback

ShiftFeedback is a YUP audio plugin instrument built from a deterministic C++20 DSP engine. It renders a MIDI-triggered, fixed-Hz frequency-shifted cross-feedback cloud with bounded self-oscillation safeguards.

The standalone editor also includes a built-in `Trigger` pulse, a space-key gate when the editor has keyboard focus, and an output activity meter. External MIDI input is still supported and remains the primary plugin trigger path.

## Targets

- Standalone app
- VST3 plugin
- AUv2 plugin
- Deterministic DSP regression test executable
- Plugin bridge regression test executable for the standalone trigger path

Plugin identity:

- App ID: `jp.ehl.shiftfeedback`
- Plugin ID: `jp.ehl.shiftfeedback`
- AU subtype: `SfBk`
- AU manufacturer: `EHL1`

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

The release preset builds the standalone, VST3, and AUv2 bundles, then stages human-facing products under `artifacts/plugin-release/<platform-arch>/{standalone,vst3,au}`. macOS CI uses `macos-arm64`; Windows uses `windows-x64` without AU. `build/` remains CMake's internal workspace.

## Continuous integration and releases

GitHub Actions runs on pushes to `main`, pull requests, and manual dispatch. A classifier skips the macOS 26 arm64 and Windows 2025 x64 build jobs when a change is limited to project documentation or issue templates; the summary job still runs and verifies that the skip was intentional. Manual dispatch defaults to forcing the full build.

Full CI uploads `ShiftFeedback-latest-macos-arm64.zip` and `ShiftFeedback-latest-windows-x64.zip` plus `SHA256SUMS.txt` manifests with 14-day retention. Actions are pinned by commit SHA.

Release tags do not rebuild. A `vMAJOR.MINOR.PATCH` tag resolves to its target commit, checks that `CMakeLists.txt` declares the same `project(ShiftFeedback VERSION ...)`, finds exactly one successful `main` push CI run for that exact commit SHA, verifies that the two expected artifacts are unexpired, validates strict SHA-256 manifests, creates a draft release if needed, uploads exactly the two versioned ZIP assets, and publishes the draft.
