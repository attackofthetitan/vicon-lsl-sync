# Changelog

Notable user-facing, compatibility, build, and maintenance changes are recorded here.

## [Unreleased]

## [1.12.0] - 2026-09-02

### Added

- Added native Apple Silicon builds for the bridge command-line and desktop
  applications, LabRecorder, and LabRecorderCLI.
- Added versioned macOS ARM64 disk-image and tarball release assets with
  bundled Qt and LSL runtime dependencies.

### Changed

- The desktop app now discovers bundled recorder executables in Windows,
  Unix, and macOS application-bundle layouts.
- The hosted build now compiles, tests, packages, signs, and validates macOS
  ARM64 alongside the existing Windows and Linux targets.

### Compatibility

- Existing Windows and Linux release filenames, command-line options, LSL
  stream layouts, timestamps, coordinates, and saved-session formats are
  unchanged.
- macOS packages are ad-hoc signed for integrity but are not Developer ID
  signed or notarized. A first launch may require explicit approval in macOS.
- Physical Vicon, HoloLens, Vuforia, and LabRecorder integration remains a
  manual qualification step. Use `v1.11.0` as the rollback release.

## [1.11.0] - 2026-09-01

### Added

- Added one guided desktop-session path that starts the bridge and preview,
  discovers streams, checks the recording setup, starts recording, and stops
  each owned component in order.
- Added versioned session presets, managed calibration profiles, explicit stream
  identities, recording-destination checks, and post-recording XDF verification.
- Added responsive CSV and XDF loading with cancellation, bounded memory use,
  stream mapping, seeking, frame stepping, playback speed, and optional looping.

### Changed

- Recorder commands now run one operation at a time, wait for explicit replies,
  and distinguish a recorder started by the desktop app from an external one.
- The desktop app now keeps session state in one place, exposes setup and file
  check results, and keeps background preview and shutdown work off the window
  thread.
- Rewrote the project guides and README files in plain English. Commands, paths,
  stream layouts, timing rules, and release filenames did not change.

### Fixed

- Fixed selected-stream query errors being cleared by later terms, incorrect
  path-field diagnostics, canceled file checks being reported as failures, stale
  stair-model readiness, and incomplete preview timelines surviving rejection.
- Fixed stream-health warnings obscuring measured freshness and made duplicate
  stream names require an explicit identity choice.

### Compatibility

- Existing command-line options, LSL stream names and layouts, timestamp and
  coordinate rules, build targets, and release filenames remain unchanged.
- Session presets and saved calibration files use their first versioned format;
  desktop settings from releases before `v1.11.0` are not imported.
- Physical HoloLens 2, Vuforia, Vicon, and LabRecorder qualification remains a
  publication gate when that equipment is available. Use `v1.10.5` as the
  rollback release if an integration problem appears.

## [1.10.5] - 2026-08-23

### Changed

- Split large bridge, preview, XDF, desktop, settings, and portable-launcher files into smaller files. Public names and behavior stay the same.
- Moved duplicate marker and segment LSL output work into one private helper. Stream names and layouts stay the same.
- Made the HoloLens gaze and model-target layouts come from shared JSON files. Generated C++ and C# files now use the same source.
- Split large C++ and C# check files into smaller groups without removing any covered behavior.
- Moved CMake and release work into smaller modules and scripts. Build targets, hosted jobs, release files, and package results stay the same.

### Packaging

- Put Windows path, temporary-folder, license, and output checks in one place.
- Split portable-file checking, program startup, and safe folder handling into smaller C++ files.
- Kept checksum rejection, checked extraction, support for extra Windows certificate data, and the same Windows and Linux release files.

### Documentation

- Added guides for code ownership, fixed behavior, startup and recovery, time and coordinates, hardware checks, and release work.

### Compatibility

- There is no intended change to program behavior, public C++ or C# names, saved Unity fields, command-line options, or LSL stream layouts.
- Setting names, build targets, and release filenames also stay the same.
- The HoloLens 2, Vuforia, Vicon, and LabRecorder hardware setup was not available for this release. Automated stream, timing, start/stop, recovery, recording, and package checks passed. Use `v1.10.4` as the rollback version if a hardware problem appears.

[Unreleased]: https://github.com/attackofthetitan/vicon-lsl-sync/compare/v1.12.0...HEAD
[1.12.0]: https://github.com/attackofthetitan/vicon-lsl-sync/compare/v1.11.0...v1.12.0
[1.11.0]: https://github.com/attackofthetitan/vicon-lsl-sync/compare/v1.10.5...v1.11.0
[1.10.5]: https://github.com/attackofthetitan/vicon-lsl-sync/compare/v1.10.4...v1.10.5
