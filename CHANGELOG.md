# Changelog

Notable user-facing, compatibility, build, and maintenance changes are recorded here.

## [Unreleased]

### Documentation

- Rewrote the project guides and README files in plain English. Commands, paths, settings, stream layouts, timing rules, and release promises did not change.

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

[Unreleased]: https://github.com/attackofthetitan/vicon-lsl-sync/compare/v1.10.5...HEAD
[1.10.5]: https://github.com/attackofthetitan/vicon-lsl-sync/compare/v1.10.4...v1.10.5
