# v1.15.3 release checklist

## Release details

- Version: `1.15.3`
- Previous release: `v1.15.2`
- Target date: 2026-10-17
- Status: local checks passed; publishing starts when the release tag is pushed
- Release body: empty, as requested
- The tagged build and publication will not be monitored for this release.

## Before publishing

- [x] The CMake version and dated changelog section both say `1.15.3`.
- [x] The recorder and GUI regression suites pass on Windows with Qt 6.8.3,
  including the scaled GUI run.
- [x] The full desktop build and all six test suites pass locally on Windows.
- [x] Generated stream files pass `tools/generate_stream_contracts.py --check`.
- [x] The release version passes `.github/scripts/verify-release-version.sh`.
- [x] `git diff --check` finds nothing.

## Publishing

- [ ] The patch is merged into `main` through a pull request.
- [ ] Tag `v1.15.3` points at the released commit on `main`.
- [ ] The tagged build passes the logic tests, HoloLens tests, and desktop
  build, tests, and packaging on Linux x64, Windows x64, and Apple Silicon Mac.
- [ ] Five platform downloads and `SHA256SUMS.txt` are published.
- [x] The release workflow creates the release with an empty notes body.

## Not tested locally

- Linux and Mac builds, and all platform packaging, run on the build server.
- Real Vicon hardware, a HoloLens headset, and a physical recording session
  were not tested.
