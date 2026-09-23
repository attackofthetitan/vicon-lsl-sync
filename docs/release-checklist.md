# v1.15.0 release checklist

## Release details

- Version: `1.15.0`
- Previous release: `v1.14.7`
- Target date: 2026-09-23
- Pull requests: none; releasing directly from `main`
- Status: prepared; publication pending tagged CI
- Scope: an easier macOS install, working bundled recorders on macOS, and a
  path check that tests the study folder

A minor release. The macOS disk image now holds one app with LabRecorder
inside it. The app ejects its disk image, offers to move itself to Applications,
and explains the macOS folder permissions it asks for. The bundled recorders
start on any Mac, and the path check tests the study folder instead of the
app's working folder.

## Pre-publication checks

- [x] The CMake version and dated changelog section use `1.15.0`.
- [x] The full desktop build and all six test programs pass locally on macOS
  arm64, including the GUI checks at normal and 1.5x scale and the new macOS
  installation checks.
- [x] A locally built package passes `.github/scripts/test-package-macos.sh`,
  with one local exception: Homebrew Qt gives a bundled `libbrotlicommon` an
  absolute install name, which the CI Qt build does not. The check also fails
  for a v1.14.7 package built on this machine.
- [x] The packaged LabRecorder loads Qt and `lsl` from its own bundle with the
  build folders hidden. The v1.14.7 package could not load `lsl` that way.
- [x] The app ejected a mounted disk image of its version, left other versions
  alone, retried an image in use, and reported the result.
- [x] The move copies the app with a valid signature and without the quarantine
  flag, and the reopen step waits for the old process to exit.
- [x] Generated stream contracts pass `tools/generate_stream_contracts.py --check`.
- [x] Release version passes `.github/scripts/verify-release-version.sh`.
- [x] `git diff --check` is clean.

## Publication

- [ ] Release commit pushed to `main` and its CI passes before tagging.
- [ ] Tag `v1.15.0` points to the release commit.
- [ ] Tagged CI passes the logic matrix, HoloLens checks, and full desktop build,
  tests, and packaging on Linux x64, Windows x64, and macOS arm64.
- [ ] Five platform payloads and `SHA256SUMS.txt` are published and verified.

## Checks not run

- The Move to Applications dialog, the Gatekeeper **Open Anyway** step, and the
  macOS folder and local network prompts need a Mac that has never run the app.
  Download the disk image, open the app from it, and record once.
- Physical Unity/OpenXR/Vuforia, HoloLens, and Vicon checks were not run.
  No device behavior changed.
- Windows and Linux builds, Windows packaging, and the HoloLens core tests run
  only in CI.
