# v1.15.6 release checklist

## Release details

- Version: `1.15.6`
- Previous release: `v1.15.5`
- Target date: 2026-11-06
- Status: local checks passed; publishing starts when the release tag is pushed
- Release body: empty, as requested

## Before publishing

- [x] The CMake version and dated changelog section both say `1.15.6`.
- [x] The full desktop build passes locally on Windows with Qt 6.8.3.
- [x] The logic, recorder, lifecycle, and stream recovery suites pass locally.
- [ ] The GUI suites pass locally. All 27 GUI test cases pass in the normal and
  scaled runs, but the window layout check fails on this PC's display, as it
  did before these changes. The pull request build on the build server must
  pass it.
- [x] Generated stream files pass `tools/generate_stream_contracts.py --check`.
- [x] The release version passes `.github/scripts/verify-release-version.sh`.
- [x] `git diff --check` finds nothing.

## Publishing

- [ ] The pull request build passes on Linux x64, Windows x64, and Apple
  Silicon Mac before merging.
- [ ] The patch is merged into `main` through a pull request.
- [ ] Tag `v1.15.6` points at the released commit on `main`.
- [ ] The tagged build passes the logic tests, HoloLens tests, and desktop
  build, tests, and packaging on all three platforms.
- [ ] Five platform downloads and `SHA256SUMS.txt` are published.
- [x] The release workflow creates the release with an empty notes body.

## Not tested locally

- Linux and Mac builds, and all platform packaging, run on the build server.
- Real Vicon hardware, a HoloLens headset, and a physical recording session
  were not tested.
- The file-name fix only shows on a Windows PC whose code page is not UTF-8;
  the PC used for these checks has UTF-8 turned on.
