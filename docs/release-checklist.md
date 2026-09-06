# v1.14.0 release checklist

## Release details

- Version: `1.14.0`
- Previous release: `v1.13.7`
- Target date: 2026-09-07
- Pull requests: `#36` (preview gaze basis)
- Status: in progress
- Scope: preview gaze from a manually registered stair is drawn in the stair
  model's basis instead of the Vuforia model target's

A minor release. It changes what the preview draws for one class of publisher
without removing anything, so it carries a minor bump rather than a patch.

## Pre-merge checks

- [x] The CMake version and dated changelog section both use `1.14.0`.
- [x] The changelog states the cause, names the metadata value that selects each
  basis, and says recordings are unaffected.
- [x] `Vuforia.ModelTarget` and a missing `acquisition/sdk` both keep the
  transform they have always had, covered by a test that compares the defaulted
  overload against the explicit Vuforia basis.
- [x] The manual basis is covered by a test: a point and a direction given in
  registration-root coordinates arrive mirrored in X under the fixed
  `vicon_from_target`, and the input signs read `(-1, 1, -1)`.
- [x] The mirror is evidence-based, not tuned. The Unity mesh and the preview's
  `stair_model1.obj` agree on every extent and carry opposite X centres:
  `-1.6759` against `+1.6760`, with half-extents `1676.0000` and `1676.0001`.
- [x] Session and calibration profile files stay at version 1. A stored
  calibration persists its whole gaze transform, including `inputAxisSign`, so an
  existing profile replays exactly as it was solved.
- [x] The dependency-light logic suite passes locally: 740 assertions in 74 test
  cases.
- [x] The macOS GUI target builds against Qt6 with the panel change in it.
- [x] `git diff --check` is clean.
- [ ] Generated stream contracts are unchanged; `tools/generate_stream_contracts.py`
  needs Python 3.10 for `write_text(newline=...)` and was not run on this
  machine, which has 3.9.6. No stream contract was edited in this release.
- [x] The Vuforia path is confirmed against real recordings, not only by test.
  Three `sub-06` runs were replayed through the shipped transform and the gaze
  origin was compared with the participant's own pelvis markers, which Vicon
  measures independently. The origin sits 0.55 m to 0.59 m above the pelvis
  centroid and within 0.18 m to 0.20 m horizontally, across 71 to 148 matched
  samples per run, with the target calibration solving at 0.7 mm to 1.7 mm
  position error. That is where a standing adult's eyes belong.
- [x] The two bases differ by a mirror about the stair model's own lateral
  centre, which is why the wrong basis moves a gaze direction while leaving a
  walker near the centreline in place. A ray-hit test cannot separate them: the
  stair solid is symmetric across that centre, so both bases return identical
  hit counts and distances on the same recordings.
- [x] The hosted Linux, Windows, macOS, and HoloLens matrix is green on `#36`:
  builds on all three platforms, logic tests on all three, and
  `hololens-core-tests`.

## Merge

- [ ] Merged into `main` with a green matrix on the exact commit merged.
- [ ] `main` contains the reviewed commit with no release-only change left on a
  side branch.

## Qualification and limitations

- [ ] Known limitation: the manual basis is not confirmed against a recorded
  session. It rests on the mesh comparison above, on the device-side checks that
  showed the VIVE gaze stream to be physically correct, and on the symptom
  matching the difference between the bases exactly: a mirror about the stair's
  lateral centre moves gaze while leaving a walker on that centreline in place,
  which is what was reported. No VIVE recording with a solved calibration has
  been replayed through it. The archived HoloLens recordings cannot stand in,
  because the stair solid is symmetric across the mirror plane.
- [x] The macOS signing disposition is unchanged: CI applies ad-hoc signatures
  for package integrity, but no Developer ID certificate or notarization
  credential is configured.

## Known flake

`testConnectionTimeoutDoesNotShortenCommandTimeout` gives a localhost TCP
connect a 20 ms deadline, which a loaded Windows runner can miss. It is
unrelated to release content; re-run the Windows job if it trips again.

## Publication

- [ ] Tagged `v1.14.0` on the merge commit, with the tagged build green and the
  release assets published.
- [ ] Assets confirmed against `SHA256SUMS.txt`, with the bundle reporting
  version `1.14.0`.
