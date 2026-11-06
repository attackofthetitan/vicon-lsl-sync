# Release checklist

Last release: `v1.15.6`, 2026-11-06. Release notes are left empty.

Before you tag a release, check that:

- [ ] The version in `vicon-lsl-bridge/CMakeLists.txt` and the dated heading in
  `CHANGELOG.md` both say the new version.
- [ ] The desktop app builds and all six test suites pass on Windows with Qt
  6.8.3, including the scaled GUI run.
- [ ] `python tools/generate_stream_contracts.py --check` passes.
- [ ] `.github/scripts/verify-release-version.sh` passes for the new tag.
- [ ] `git diff --check` finds nothing.
- [ ] The pull request build passes on Linux, Windows, and Mac.

Then merge the pull request and push the tag (`vN.N.N`) on the merge commit. The
tag build makes the release by itself, with five downloads and
`SHA256SUMS.txt`.

## Known gaps

- Linux and Mac builds and all packaging only run on the build server.
- Nobody tests on real Vicon hardware, a HoloLens, or a full recording session
  before a release.
- The GUI layout check can fail on a local PC with a different screen. The
  build server's Windows run is the one that counts.
- The file name fix in 1.15.6 only shows on a Windows PC that doesn't use UTF-8
  as its code page.
