# v1.12.1 release checklist

## Release details

- Version: `1.12.1`
- Previous release: `v1.12.0`
- Target date: 2026-09-02
- Feature pull request: `#25`
- Status: validated on the release branch; awaiting merge, hardware
  disposition, and publication
- Scope: correctness fixes in the preview worker, desktop shutdown, recorder
  logging, and Vicon reconnect handling, plus hot-path and dead-code cleanup

A patch release: every change is a fix or an internal cleanup, and nothing was
added to the command line, the streams, or the saved-session formats.

## Pre-merge checks

- [x] The CMake version and dated changelog section both use `1.12.1`.
- [x] The changelog separates the behaviour fixes from the internal changes and
  states that stream names, channel layouts, metadata, and log text are
  unchanged.
- [x] The behaviour contract records the first-frame retry rule and the
  connection-loss rule the fixes introduce.
- [x] The reconnect back-off has a regression test covering a server that
  accepts connections but never delivers a frame.
- [x] Marker and segment channel order is asserted against what reaches the
  outlet, now that the streams flatten samples themselves.
- [x] The dependency-light logic suite, the stream recovery suite, and the
  bridge lifecycle suite pass locally.
- [x] The hosted Linux, Windows, macOS, and HoloLens matrix is green on the
  exact commit to be released: run `33599838969` passed all eight jobs on
  `eb27378`, the head of `bridge-cleanup`. The tagged build must be green again
  on the merge commit that carries the tag.
- [x] Generated C++ and C# stream files are current: `generate_stream_contracts.py --check`
  reports no drift, and `git diff --check` is clean.

## Merge

- [ ] PR `#25` is mergeable into `main` with no unresolved review thread.
- [ ] Required checks are green on the exact commit being merged.
- [ ] `main` contains the reviewed commit with no release-only change left on a
  side branch.

## Qualification and limitations

- [ ] Run the [hardware test guide](device-parity-runbook.md), or explicitly
  approve publication without the physical HoloLens 2, Vuforia, Vicon, and
  LabRecorder setup.
- [ ] Confirm on a built desktop app that a preview stream published after the
  preview starts is now picked up, that a stream which stops delivering is
  reported as stale, and that closing does not wait indefinitely on a recorder
  that will not stop. These three fixes change timing behaviour that only the
  running desktop application exercises.
- [x] The macOS signing disposition is unchanged: CI applies ad-hoc signatures
  for package integrity, but no Developer ID certificate or notarization
  credential is configured.

## Publication

- [ ] Tag `v1.12.1` on the merged commit and let the tagged build publish the
  release assets.
- [ ] Confirm the published assets match the tagged commit.
