# v1.14.7 release checklist

## Release details

- Version: `1.14.7`
- Previous release: `v1.14.6`
- Target date: 2026-09-23
- Pull requests: none; releasing directly from `main`
- Status: prepared; publication pending tagged CI
- Scope: simplify the desktop bridge code without changing its behavior

A patch release that simplifies the desktop bridge code. Behavior is unchanged:
streams, saved settings, calibrations, and recordings work as before.

## Pre-publication checks

- [x] The CMake version and dated changelog section use `1.14.7`.
- [x] Desktop logic checks pass locally on macOS arm64: 78 test cases with
  Catch2 and with the bundled test runner.
- [x] The full desktop build and all six test programs pass locally on macOS
  arm64, including the GUI checks at normal and 1.5x scale.
- [x] Preview, XDF, CSV, calibration, playback, timestamp, and diagnostic
  results match v1.14.6 in a side-by-side comparison, apart from one internal
  transform name that is never saved or shown. Command-line output and exit
  codes match.
- [x] The live preview worker gives the same frames and stream details as
  v1.14.6 when reading local LSL streams.
- [x] Generated stream contracts pass `tools/generate_stream_contracts.py --check`.
- [x] Release version passes `.github/scripts/verify-release-version.sh`.
- [x] `git diff --check` is clean.
- [x] Coordinate documentation no longer describes the removed gaze-transform
  helper or the publisher lookup.

## Publication

- [ ] Release commit pushed to `main` and its CI passes before tagging.
- [ ] Tag `v1.14.7` points to the release commit.
- [ ] Tagged CI passes the logic matrix, HoloLens checks, and full desktop build,
  tests, and packaging on Linux x64, Windows x64, and macOS arm64.
- [ ] Five platform payloads and `SHA256SUMS.txt` are published and verified.

## Device checks not run

- Physical Unity/OpenXR/Vuforia, HoloLens, and Vicon checks were not run
  locally. No device behavior changed; starting a session, recording, and
  playing back the file on the study machine is enough to confirm it.
- Windows and Linux builds, Windows packaging, and the HoloLens core tests run
  only in CI. This local environment does not have the .NET SDK.
