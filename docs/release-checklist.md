# v1.12.2 release checklist

## Release details

- Version: `1.12.2`
- Previous release: `v1.12.1`
- Target date: 2026-09-02
- Feature pull request: `#26`
- Status: validated on the release branch; awaiting merge and publication
- Scope: the session ordering, recording-readiness, and stream-reconciliation
  decisions move out of the desktop window into modules that can be tested on
  their own

A refactor-only patch release. No behaviour changes, so it carries a patch
number for the version bump alone.

## Pre-merge checks

- [x] The CMake version and dated changelog section both use `1.12.2`.
- [x] The changelog states plainly that nothing user-visible changed and names
  `v1.12.1` as the rollback release.
- [x] The extracted decisions are covered by tests: 75 assertions across the
  session sequencer, the setup-check policy, and the stream inventory, most of
  which had no coverage before.
- [x] The shutdown path still reads its state twice, because ending an owned
  recorder changes whether one is running. That ordering was implicit before
  and is now explicit and commented.
- [x] The dependency-light logic suite, the stream recovery suite, and the
  bridge lifecycle suite pass locally, along with the session sequencer
  decision tables built against a stubbed Qt.
- [ ] The hosted Linux, Windows, macOS, and HoloLens matrix is green on the
  exact commit being released.
- [x] Generated C++ and C# stream files are current and `git diff --check` is
  clean.

## Merge

- [ ] PR `#26` is mergeable into `main` with no unresolved review thread.
- [ ] Required checks are green on the exact commit being merged.
- [ ] `main` contains the reviewed commit with no release-only change left on a
  side branch.

## Qualification and limitations

- [ ] Run the [hardware test guide](device-parity-runbook.md), or explicitly
  approve publication without the physical HoloLens 2, Vuforia, Vicon, and
  LabRecorder setup.
- [ ] Start and stop a guided session in the built desktop application. The
  extracted decisions drive that sequence, and only the running application
  exercises them end to end.
- [x] The macOS signing disposition is unchanged: CI applies ad-hoc signatures
  for package integrity, but no Developer ID certificate or notarization
  credential is configured.

## Known flake

`testConnectionTimeoutDoesNotShortenCommandTimeout` gives a localhost TCP
connect a 20 ms deadline, which a loaded Windows runner can miss. It failed the
first `v1.12.1` tag build and passed on re-run. It is unrelated to release
content; re-run the Windows job if it trips again.

## Publication

- [ ] Tag `v1.12.2` on the merged commit and let the tagged build publish the
  release assets.
- [ ] Confirm the published assets match the tagged commit.
