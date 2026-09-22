# v1.14.6 release checklist

## Release details

- Version: `1.14.6`
- Previous release: `v1.14.5`
- Target date: 2026-09-22
- Pull requests: none; releasing directly from `main`
- Status: prepared; publication pending tagged CI
- Scope: correct left/right-inverted HoloLens gaze in desktop stair calibration
  and target-aligned XDF playback

A patch release. Vuforia calibration now uses the same conversion from the
Unity-imported stair basis to the OBJ as manual registration, preserving
lateral movement, forward alignment, and height. Stream schemas, recorded
samples, manual registration's conversion, and fixed stair placement are
unchanged. This correction requires only a desktop update; the v1.14.5 HoloLens
update remains necessary for frozen-reference recording.

## Pre-publication checks

- [x] The CMake version and dated changelog section use `1.14.6`.
- [x] Desktop logic checks pass locally on the current main branch: 79 test cases.
- [x] The new left/right regression fails against the old Vuforia conversion.
- [x] Regression coverage includes gaze origins and directions, rotated
  HoloLens and stair poses, preserved handedness, and target-aligned XDF playback.
- [x] Generated stream contracts pass `tools/generate_stream_contracts.py --check`.
- [x] Release version passes `.github/scripts/verify-release-version.sh`.
- [x] `git diff --check` is clean.
- [x] Coordinate documentation and README explain the correction and replacing
  existing saved Vuforia solutions by recalibrating and saving again.

## Publication

- [ ] Release commit pushed to `main`; tag `v1.14.6` points to that commit.
- [ ] Tagged CI passes the logic matrix, HoloLens checks, and full desktop build,
  tests, and packaging on Linux x64, Windows x64, and macOS arm64.
- [ ] Five platform payloads and `SHA256SUMS.txt` are published and verified.
- [ ] Release notes explain how to replace saved mirrored calibrations.

## Device checks not run

- Physical Unity/OpenXR/Vuforia, HoloLens, and Vicon checks were not run locally.
- On the study machine, run **Calibrate from Stair Target**, look left and
  right while facing upstairs, and confirm the gaze follows the same side
  without changing forward or vertical alignment. Then **Save Session
  Calibration** to replace the old stored transform.
- HoloLens core, full desktop, and package checks run in tagged CI. This local
  environment does not have the .NET SDK or a configured desktop runtime build.
