# v1.13.0 release checklist

## Release details

- Version: `1.13.0`
- Previous release: `v1.12.2`
- Target date: 2026-09-02
- Pull requests: `#27` (removal), `#28` (layout)
- Status: published
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
- [x] The hosted Linux, Windows, macOS, and HoloLens matrix was green on `#27`
  and on the merge commit `d23ead3`. `#28` must be green before it is merged.
- [x] The full suite also passes locally on macOS: 6 of 6, including both GUI
  scale variants.
- [x] Generated C++ and C# stream files are current and `git diff --check` is
  clean.

## Merge

- [x] `#27` merged into `main` as `d23ead3`.
- [x] `#28` merged into `main` as `f83d5c8` with a green matrix on the exact
  commit merged.
- [x] `main` contains both reviewed commits with no release-only change left on a
  side branch.

## Qualification and limitations

- [x] Publication approved without the physical HoloLens 2, Vuforia, Vicon, and
  LabRecorder setup. The hardware guide was not run; test 7 changed, since **Use
  Manual Transform** is now **Clear Calibration**.
- [x] The built application was run on macOS. The calibration state reads **Not
  calibrated** and the state-gated controls are disabled when they cannot act.
  That run found the control rows scrolling sideways, fixed in `#28`.
- [ ] Known limitation: at 680x540, the minimum the window declares, about 49 px
  of sideways scrolling remains. Accepted for this release; the checked size is
  800x600.
- [x] The macOS signing disposition is unchanged: CI applies ad-hoc signatures
  for package integrity, but no Developer ID certificate or notarization
  credential is configured.

## Known flake

`testConnectionTimeoutDoesNotShortenCommandTimeout` gives a localhost TCP
connect a 20 ms deadline, which a loaded Windows runner can miss. It failed the
first `v1.12.1` tag build and passed on re-run. It is unrelated to release
content; re-run the Windows job if it trips again.

## Publication

- [x] Tagged `v1.13.0` on `f83d5c8`. The tagged build was green and published the
  release assets.
- [x] Assets confirmed: the macOS archive matches its `SHA256SUMS.txt` entry and
  its bundle reports version `1.13.0`.
