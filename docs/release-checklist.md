# v1.14.0 release checklist

## Release details

- Version: `1.14.0`
- Previous release: `v1.13.7`
- Target date: 2026-09-07
- Pull requests: `#TBD` (preview gaze basis)
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
- [ ] The hosted Linux, Windows, macOS, and HoloLens matrix is green on the pull
  request and on the merge commit.

## Merge

- [ ] Merged into `main` with a green matrix on the exact commit merged.
- [ ] `main` contains the reviewed commit with no release-only change left on a
  side branch.

## Qualification and limitations

- [ ] Known limitation: the corrected basis has not been confirmed against the
  physical Vicon, stair, and headset setup. It is derived from the mesh
  comparison above and from the device-side checks that showed the VIVE gaze
  stream to be physically correct, not from a recorded session drawn in the
  preview.
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
