# v1.10.5 release record

## Release details

- Version: `1.10.5`
- Previous release: `v1.10.4`
- Release branch: `release/v1.10.0`
- Main commit: `72a1d93b108459e728b59b8140d2943e82d8af70`
- Published: 2026-08-23
- [Release page](https://github.com/attackofthetitan/vicon-lsl-sync/releases/tag/v1.10.5)
- Scope: code organization, safer packaging, more checks, and new technical guides, with no intended behavior change

## Checks completed before merge

- [x] The CMake version and changelog version match.
- [x] Generated C++ and C# stream files are current.
- [x] The C++ suite that needs no desktop dependencies passes with and without Catch2.
- [x] The full Windows Release build passes for the bridge, desktop app, start/stop, recovery, and LabRecorder targets.
- [x] The device-independent HoloLens timing, coordinate, encoding, publishing, and recovery suite passes with warnings treated as errors.
- [x] Windows package path and file checks pass in Windows PowerShell 5.1 and PowerShell 7.
- [x] The current portable launcher extracts a good embedded ZIP, rejects a changed one, and handles extra certificate data added by Windows signing.
- [x] Workflow YAML, PowerShell scripts, and Bash scripts parse successfully.
- [x] Documentation links, generated files, whitespace, and third-party submodule revisions are clean.
- [x] The code cleanup was reviewed as separate commits for generated HoloLens streams, native code ownership, package safety, and release documentation.
- [x] The final hosted Windows and Linux build set passed.
- [x] The hardware decision below was recorded before the tag.
- [x] Version `1.10.5` remained the intended patch version.
- [x] The approved commit was merged into `main` before tagging.

## Hardware decision

The physical HoloLens 2, Vuforia, Vicon, and LabRecorder test was not run for `v1.10.5` because that hardware setup was not available on 2026-08-23.

The release used these checks instead:

- Generated C++ and C# stream layouts match.
- Device-independent timing, coordinate, encoding, publishing, and recovery checks pass.
- Native frame mapping, start/stop, and recovery checks pass.
- LabRecorder command and state checks pass.
- Windows package and portable-file checks pass.

This release does not intentionally change an SDK, dependency, public interface, saved Unity field, stream layout, timestamp rule, coordinate rule, setting name, or release filename.

Real Unity, Windows device API, Vuforia, and physical Vicon behavior cannot be proved without the equipment. If an integration problem appears, return to `v1.10.4` and complete the [hardware test guide](device-parity-runbook.md) before publishing a correction.

## Publication checks

- [x] Annotated tag `v1.10.5` points to the approved commit in `main`.
- [x] The release workflow published the Windows ZIP, Windows portable GUI, Linux archive, and `SHA256SUMS.txt`.
- [x] Fresh downloads of all four files match the checksums in `SHA256SUMS.txt`.
- [x] The downloaded Windows command-line app opens its help successfully.
- [x] The downloaded Windows desktop app and portable app complete their built-in test mode.
- [x] The portable app extracts to a new folder, and its 118 payload files match the Windows ZIP.
- [x] The downloaded Linux command-line app opens its help on Ubuntu 24.04 and loads the included `liblsl.so.2`.
- [x] The published release notes cover the same changes and hardware limit as [CHANGELOG.md](../CHANGELOG.md).

The hosted release run is saved on the [build page](https://github.com/attackofthetitan/vicon-lsl-sync/actions/runs/32633388765).

## Changes that need a separate move plan

Do not include any of these in normal patch-level code cleanup:

- Dependency or framework updates.
- Stream name, layout, time, or coordinate changes.
- Saved Unity field changes.
- Thread-ownership changes.
- Public command, source interface, setting, build target, or release filename changes.

For one of these changes, write down the old and new behavior. Explain how existing users or files move forward and which checks prove the move is safe.
