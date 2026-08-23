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

- [ ] Review the complete refactor diff as small logical commits or an equivalently reviewable pull request.
- [ ] Run the hosted Windows and Linux matrix from the final commit.
- [ ] Complete the applicable HoloLens/Vuforia checks in [device-parity-runbook.md](device-parity-runbook.md), or explicitly record why hardware qualification is not required for this patch.
- [ ] Confirm `1.10.5` remains the intended semantic version and that no additional user-facing change needs release notes.
- [ ] Merge the approved commit into `main` and confirm the candidate commit is an ancestor of `origin/main`.

## Tag and publication

- [ ] Create annotated tag `v1.10.5` on the approved `main` commit and push the tag.
- [ ] Confirm the release workflow uploads the Windows ZIP, Windows portable GUI executable, Linux tarball, and `SHA256SUMS.txt`.
- [ ] Download each published asset and verify its checksum against `SHA256SUMS.txt`.
- [ ] Verify the Windows portable GUI and one extracted Windows package on a clean machine.
- [ ] Verify the Linux package starts with its bundled liblsl on a supported distribution.
- [ ] Confirm the published release notes match [CHANGELOG.md](../CHANGELOG.md).

Dependency upgrades, stream/timestamp/coordinate changes, serialized-field changes, or thread-ownership changes require a separate migration review and must not be folded into this patch release.
