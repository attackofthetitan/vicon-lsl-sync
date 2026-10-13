# v1.15.2 release checklist

## Release details

- Version: `1.15.2`
- Previous release: `v1.15.1`
- Target date: 2026-10-13
- Status: local checks passed; awaiting the pull request and tagged builds
- Scope: LabRecorder startup and connection fixes, session cancellation, and
  preserving saved stream requirements

## Before publishing

- [x] The CMake version and dated changelog section both say `1.15.2`.
- [x] The recorder and GUI regression suites pass on Linux with Qt 6.4.2,
  including the scaled GUI run.
- [x] The full desktop build and all six test suites pass locally on Linux.
- [x] Generated stream files pass `tools/generate_stream_contracts.py --check`.
- [x] The release version passes `.github/scripts/verify-release-version.sh`.
- [x] `git diff --check` finds nothing.

## Publishing

- [ ] The pull request build passes and the patch is merged into `main`.
- [ ] Tag `v1.15.2` points at the released commit on `main`.
- [ ] The tagged build passes the logic tests, HoloLens tests, and desktop
  build, tests, and packaging on Linux x64, Windows x64, and Apple Silicon Mac.
- [ ] Five platform downloads and `SHA256SUMS.txt` are published and checked.
- [ ] The release notes contain the two brief changelog bullets.

## Not tested locally

- Windows and Mac builds and packaging run on the build server.
- Real Vicon hardware, a HoloLens headset, and a physical recording session
  were not tested.
