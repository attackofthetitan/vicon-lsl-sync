# v1.13.0 release checklist

## Release details

- Version: `1.13.0`
- Previous release: `v1.12.0`
- Target date: 2026-09-02
- Status: awaiting the hosted matrix on the release branch
- Scope: correctness fixes in the preview worker, desktop shutdown, recorder
  logging, and Vicon reconnect handling, plus hot-path and dead-code cleanup

## Pre-merge checks

- [x] The CMake version and dated changelog section both use `1.13.0`.
- [x] The changelog separates the behaviour fixes from the internal changes and
  states that stream names, channel layouts, metadata, and log text are
  unchanged.
- [x] The reconnect back-off has a regression test covering a server that
  accepts connections but never delivers a frame.
- [x] Marker and segment channel order is asserted against what reaches the
  outlet, now that the streams flatten samples themselves.
- [x] The dependency-light logic suite, the stream recovery suite, and the
  bridge lifecycle suite pass locally.
- [ ] The hosted Linux, Windows, macOS, and HoloLens matrix is green on the
  exact commit being released. This gates the release: the desktop sources
  could not be compiled locally because Qt is not installed on the development
  machine used for these changes.
- [ ] Generated C++ and C# stream files are current and `git diff --check` is
  clean.

## Merge

- [ ] The release branch is mergeable into `main` with no unresolved review
  thread.
- [ ] Required checks are green on the exact commit being merged.
- [ ] `main` contains the reviewed commit with no release-only change left on a
  side branch.

## Qualification and limitations

- [ ] Run the [hardware test guide](device-parity-runbook.md), or explicitly
  approve publication without the physical HoloLens 2, Vuforia, Vicon, and
  LabRecorder setup.
- [ ] Confirm the preview stream retry, staleness reporting, and desktop close
  deadline on a machine with the desktop application, since these fixes change
  timing behaviour that only the built desktop app exercises.
- [x] The macOS signing disposition is unchanged: CI applies ad-hoc signatures
  for package integrity, but no Developer ID certificate or notarization
  credential is configured.

## Publication

- [ ] Tag `v1.13.0` on the merged commit and let the tagged build publish the
  release assets.
- [ ] Confirm the published assets match the tagged commit.
