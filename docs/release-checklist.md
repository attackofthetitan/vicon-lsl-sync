# Release checklist

## Candidate

- Version: `1.10.5`
- Previous release: `v1.10.4`
- Release line: `release/v1.10.0`
- Intended scope: behavior-preserving structural refactor, packaging maintenance, validation expansion, and documentation

## Completed local gates

- [x] CMake project version and changelog version agree.
- [x] Generated C++ and C# stream contracts are current.
- [x] Dependency-light native suite passes with and without Catch2.
- [x] Full Windows Release build passes for the runtime, GUI, lifecycle, recovery, and LabRecorder targets.
- [x] Platform-neutral HoloLens timing, projection, encoding, publisher, and recovery suite passes with warnings treated as errors.
- [x] Windows packaging safety assertions pass under Windows PowerShell 5.1 and PowerShell 7.
- [x] A current portable launcher verifies normal extraction, rejects a modified payload, and handles a certificate overlay.
- [x] Workflow YAML, extracted PowerShell, and extracted Bash scripts parse successfully.
- [x] Documentation links, generated-file freshness, whitespace, and vendor-submodule cleanliness checks pass.

## Required before tagging

- [x] Review the complete refactor diff as four logical commits covering generated HoloLens contracts, native responsibilities, packaging safety, and release documentation.
- [ ] Run the hosted Windows and Linux matrix from the final commit.
- [x] Record the physical-device qualification decision for the applicable HoloLens/Vuforia checks in [device-parity-runbook.md](device-parity-runbook.md).
- [x] Confirm `1.10.5` remains the intended semantic version and that no additional user-facing change needs release notes.
- [ ] Merge the approved commit into `main` and confirm the candidate commit is an ancestor of `origin/main`.

## Physical-device qualification waiver

- Decision date: 2026-08-23
- Decision: Physical HoloLens 2, Vuforia, and Vicon/LabRecorder integration qualification is waived for `v1.10.5` because the required hardware path is unavailable for this release run.
- Evidence used instead: generated schema parity, managed timing/projection/encoding/recovery checks, native frame-mapping/lifecycle/recovery checks, recorder protocol checks, and Windows packaging/portable integrity checks.
- Scope boundary: the release introduces no intended SDK, dependency, public API, serialized field, stream schema, timestamp policy, coordinate convention, settings key, or artifact-name change.
- Residual risk: Unity/WinRT/Vuforia and physical Vicon behavior cannot be proven by automation. Roll back to `v1.10.4` if an integration regression appears and execute the full device runbook before a corrective release.

## Tag and publication

- [ ] Create annotated tag `v1.10.5` on the approved `main` commit and push the tag.
- [ ] Confirm the release workflow uploads the Windows ZIP, Windows portable GUI executable, Linux tarball, and `SHA256SUMS.txt`.
- [ ] Download each published asset and verify its checksum against `SHA256SUMS.txt`.
- [ ] Verify the Windows portable GUI and one extracted Windows package on a clean machine.
- [ ] Verify the Linux package starts with its bundled liblsl on a supported distribution.
- [ ] Confirm the published release notes match [CHANGELOG.md](../CHANGELOG.md).

Dependency upgrades, stream/timestamp/coordinate changes, serialized-field changes, or thread-ownership changes require a separate migration review and must not be folded into this patch release.
