# Changelog

Notable user-facing, compatibility, build, and maintenance changes are recorded here.

## [Unreleased]

## [1.10.5] - 2026-08-23

### Changed

- Split the native mapper, bridge lifecycle, preview assembly, XDF loading, GUI composition, settings, and portable launcher implementations into focused modules while preserving their existing public interfaces and behavior.
- Replaced duplicated marker and segment outlet plumbing with a shared private numeric-outlet core.
- Made the HoloLens gaze and model-target stream identities and channel layouts generated from language-neutral manifests.
- Split oversized native and managed validation sources into focused suites without changing the covered contracts.
- Extracted CMake and release-workflow orchestration into reviewable modules and scripts while retaining existing targets, jobs, artifacts, and successful packaging behavior.

### Packaging

- Centralized Windows package path, staging, license, and output safety checks.
- Split portable payload verification, process launch, and safe-filesystem handling into dedicated native components.
- Retained payload digest rejection, verified extraction, certificate-overlay handling, and the existing Windows/Linux release inventory.

### Documentation

- Added architecture, observable-behavior, runtime-state, time/coordinate, device-parity, and release-readiness references.

### Compatibility

- No intentional runtime behavior, public source API, serialized Unity field, CLI, LSL stream schema, settings key, build target, or release artifact name change.

[Unreleased]: https://github.com/attackofthetitan/vicon-lsl-sync/compare/v1.10.5...HEAD
[1.10.5]: https://github.com/attackofthetitan/vicon-lsl-sync/compare/v1.10.4...v1.10.5
