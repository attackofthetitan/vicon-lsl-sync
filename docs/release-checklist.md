# v1.13.0 release checklist

## Release details

- Version: `1.13.0`
- Previous release: `v1.12.2`
- Target date: 2026-09-02
- Feature pull request: pending
- Status: prepared on the release branch; awaiting hosted checks, merge, and
  publication
- Scope: the manual HoloLens transform is removed from the desktop preview, and
  the desktop controls that only work in some states are limited to those states

A minor release. It removes a user-facing feature and changes what the preview
draws when no calibration exists, so it carries a minor bump rather than a patch.

## Pre-merge checks

- [x] The CMake version and dated changelog section both use `1.13.0`.
- [x] The changelog separates the removal, the behaviour changes, the fixes, and
  the compatibility statement, and names `v1.12.2` as the rollback release.
- [x] Session configuration files stay at version 1. `manualGazeTranslation` and
  `manualGazeRotationDegrees` are ignored when read and dropped when written, so
  an existing settings file still loads.
- [x] The uncalibrated gaze transform is covered by a test: a default profile
  moves neither a gaze origin nor a gaze direction, which is what the preview now
  uses until a calibration is solved or applied.
- [x] The dependency-light logic suite passes locally: 69 of 69.
- [ ] The hosted Linux, Windows, macOS, and HoloLens matrix is green on the exact
  commit being released. **This is the first compile of the changed Qt code.**
  The desktop window, preview panel, and session model were changed on a machine
  with no CMake and no Qt, so `vicon-lsl-labrecorder-tests` and
  `vicon-lsl-bridge-gui-tests` have never been built locally. Do not merge on a
  red or skipped matrix.
- [x] Generated C++ and C# stream files are current and `git diff --check` is
  clean.

## Merge

- [ ] The pull request is mergeable into `main` with no unresolved review thread.
- [ ] Required checks are green on the exact commit being merged.
- [ ] `main` contains the reviewed commit with no release-only change left on a
  side branch.

## Qualification and limitations

- [ ] Run the [hardware test guide](device-parity-runbook.md), or explicitly
  approve publication without the physical HoloLens 2, Vuforia, Vicon, and
  LabRecorder setup. Test 7 changed: **Use Manual Transform** is now **Clear
  Calibration**, and the step that returned to the manual controls now returns to
  the uncalibrated HoloLens frame.
- [ ] Confirm in the built desktop application that gaze is drawn unaligned, and
  that the calibration state reads **Not calibrated**, before any calibration is
  solved or applied. Only the running application shows what the removal looks
  like to an operator.
- [ ] Start and stop a guided session in the built desktop application, and
  confirm that the newly state-gated controls enable and disable as that sequence
  runs. The gating is driven from the preview panel and the window refresh, and
  only the running application exercises every transition.
- [x] The macOS signing disposition is unchanged: CI applies ad-hoc signatures
  for package integrity, but no Developer ID certificate or notarization
  credential is configured.

## Known flake

`testConnectionTimeoutDoesNotShortenCommandTimeout` gives a localhost TCP
connect a 20 ms deadline, which a loaded Windows runner can miss. It failed the
first `v1.12.1` tag build and passed on re-run. It is unrelated to release
content; re-run the Windows job if it trips again.

## Publication

- [ ] Tag `v1.13.0` on the merged commit and let the tagged build publish the
  release assets.
- [ ] Confirm the published assets match the tagged commit.
