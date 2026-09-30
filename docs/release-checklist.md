# v1.15.1 release checklist

## Release details

- Version: `1.15.1`
- Previous release: `v1.15.0`
- Target date: 2026-10-01
- Pull requests: #40
- Status: ready; waiting for the pull request build, then the tagged build
- Scope: fixes from a code review, including a crash when stopping a session
  and a Linux download that did not run on its own

A patch release. Stopping a session no longer crashes the app, and a lost
recorder connection can be reconnected so the recording can be stopped. The
Linux download carries everything it needs. The release also fixes number
reading on computers that use a decimal comma, recorders stopping after the app
closes, short Vicon pauses restarting the streams, and several smaller problems
with calibrations, the setup check, the file check, and playback.

## Before publishing

- [x] The CMake version and the dated changelog section both say `1.15.1`.
- [x] The full desktop build and all six test programs pass on Linux, also with
  a German language setting. This used the system's Qt 6.4, not the Qt 6.8.3
  the release build uses.
- [x] The logic tests pass with memory and undefined-behavior checks, and with
  Catch2.
- [x] The HoloLens tests pass.
- [x] A Linux package built here passes `.github/scripts/test-package-linux.sh`.
  With the build folders and the system's Qt hidden, all four programs start;
  the v1.15.0 package could not find liblsl that way.
- [x] Each new test fails without its fix.
- [x] Generated stream files pass `tools/generate_stream_contracts.py --check`.
- [x] The release version passes `.github/scripts/verify-release-version.sh`.
- [x] `git diff --check` finds nothing.

## Publishing

- [ ] The pull request build passes and #40 is merged into `main`.
- [ ] Tag `v1.15.1` points at the merge commit.
- [ ] The tagged build passes the logic tests, HoloLens tests, and the full
  desktop build, tests, and packaging on Linux x64, Windows x64, and Apple
  Silicon Mac.
- [ ] Five platform downloads and `SHA256SUMS.txt` are published and checked.

## Not tested

- Windows and Mac builds, and the recorder change for Qt 6.6 and newer, only run
  on the build server.
- The HoloLens scripts need Unity and a headset: check that the stair target
  stream closes when it stops, and that gaze times stay right after a time zone
  change.
- Real Vicon and LabRecorder tests were not run. Check a short recording, a
  Vicon pause, and Stop Session after unplugging the recorder's network.
