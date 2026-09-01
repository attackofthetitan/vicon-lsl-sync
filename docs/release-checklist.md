# v1.11.0 release checklist

## Release details

- Version: `1.11.0`
- Previous release: `v1.10.5`
- Target date: 2026-09-01
- Feature pull requests, in order: `#21`, `#22`, `#23`, `#19`
- Documentation pull request: `#20`, after `#19`
- Status: ready for ordered merge; hosted packaging, hardware disposition, and
  publication checks remain pending
- Scope: guided desktop sessions, explicit recorder and stream control,
  responsive recording preview, saved session and calibration data, and
  post-recording file checks

## Pre-merge checks

- [x] The CMake version and dated changelog section both use `1.11.0`.
- [x] Generated C++ and C# stream files are current.
- [x] The dependency-light C++ build passes without Catch2.
- [x] The full Windows Release build and all 74 registered checks pass.
- [x] The device-independent HoloLens timing, coordinate, encoding, publishing,
  cancellation, and recovery suite passes all 15 checks.
- [x] Windows package path and file safety checks pass all 26 assertions.
- [x] The scaled desktop layout that failed on `#20` passes with the reviewed
  top-branch fixes.
- [x] The three third-party submodule revisions match `main`.
- [x] Whitespace and generated-file checks are clean.
- [x] The documentation branch contains the feature branch and compares as
  documentation-only after its base is changed to `release/bridge-gui`.

## Ordered merge

- [ ] Merge `#21` into `main` after its hosted checks remain green.
- [ ] Change `#22` to target `main`, confirm the diff and hosted checks, then
  merge it.
- [ ] Change `#23` to target `main`, confirm the diff and hosted checks, then
  merge it.
- [ ] Change `#19` to target `main`, confirm the complete Windows, Linux, and
  HoloLens matrix, then merge it.
- [ ] Change `#20` to target `main`, confirm that it contains only documentation,
  then merge it.
- [ ] Confirm `main` contains every reviewed commit and has no uncommitted
  release-only change.

Do not merge the stack out of order. Each later branch assumes the earlier
branch is already present.

## Desktop-session qualification

### Configuration and evidence

- [ ] Reset, Save, Load, Import, and Export preserve every session field in the
  version-1 JSON format and reject unsupported versions without rewriting them.
- [ ] Saved calibrations round-trip their ID, version, setup, stair identity and
  pose, coordinate names, transform, notes, creation time, quality, fallback
  confirmation, and hidden state.
- [ ] The displayed path exactly matches the recorder destination after
  extension, traversal, reserved-name, trailing-character, length, writeability,
  storage, collision, overwrite, and Find Next Run checks.
- [ ] Exported session details preserve setup-check results, any Record Anyway
  reason, selected streams, recorder ownership, file-check findings, and run
  increment outcome.

### State and shutdown

- [ ] Repeated or conflicting recorder commands, split or malformed replies,
  timeouts, reconnects, replacement connections, process exit, and detach have
  predictable visible results.
- [ ] Closing before or during Start, while recording, during Stop, after a
  disconnect, or during background preview and file work remains responsive and
  sends no duplicate final recorder command.
- [ ] Only a recorder started by this application is closed by it; an external
  recorder is detached and left running.

### Selection, preview, and accessibility

- [ ] Source-ID selection, explicit follow-by-name, duplicate names, restarted
  sources, and same source IDs on different hosts remain visible and predictable.
- [ ] CSV and XDF loading, cancellation, stream choice, long-file memory limits,
  seeking beyond two hours, playback controls, and exact file-check numbers pass.
- [ ] Small, default, and scaled layouts keep controls reachable; keyboard focus,
  shortcuts, labels, light and dark themes, high contrast, and screen-reader names
  are usable.

## Hardware disposition

- [ ] Run the [hardware test guide](device-parity-runbook.md), or record an
  explicit decision to publish without the physical HoloLens 2, Vuforia, Vicon,
  and LabRecorder setup.
- [ ] Record the tested hardware and software versions, results, exceptions, and
  approver before creating the tag.

This release does not intentionally change an SDK revision, dependency revision,
LSL stream layout, timestamp rule, coordinate rule, command-line option, build
target, or release filename. It does change desktop recording orchestration, so
device-independent checks cannot prove physical integration behavior. If the
hardware setup is unavailable, document that limitation in the release notes and
use `v1.10.5` as the rollback release.

## Publication

- [ ] The release commit is merged into `main` before tagging.
- [ ] An annotated `v1.11.0` tag points to that exact commit in `main`.
- [ ] The hosted release run publishes the Windows ZIP, Windows portable GUI,
  Linux archive, and `SHA256SUMS.txt`.
- [ ] Fresh downloads match `SHA256SUMS.txt`.
- [ ] The downloaded Windows command-line, desktop, and portable applications
  start successfully, and the portable payload matches the Windows ZIP.
- [ ] The downloaded Linux command-line application opens its help on the target
  Ubuntu version and loads the included `liblsl.so.2`.
- [ ] The published notes match [CHANGELOG.md](../CHANGELOG.md) and state the
  hardware disposition and rollback version.
