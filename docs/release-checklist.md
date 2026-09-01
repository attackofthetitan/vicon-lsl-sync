# v1.12.0 release checklist

## Release details

- Version: `1.12.0`
- Previous release: `v1.11.0`
- Target date: 2026-09-02
- Feature pull request: `#24`
- Status: ready to merge; hardware/signing disposition and publication checks
  remain pending
- Scope: native Apple Silicon build, test, packaging, and release support

## Pre-merge checks

- [x] The CMake version and dated changelog section both use `1.12.0`.
- [x] The changelog and README describe the new macOS assets and the current
  signing limitation.
- [x] Recorder discovery has tests for flat, nested, missing, and sibling macOS
  application-bundle layouts.
- [x] macOS package validation covers both the tarball and mounted disk image,
  both app bundles, configuration placement, executable permissions, ARM64-only
  Mach-O files, code signatures, portable dependency paths, and CLI startup.
- [x] The original pull-request head passed the complete Linux, Windows,
  macOS, and HoloLens hosted matrix.
- [x] The original macOS CI artifact was downloaded to an Apple Silicon Mac;
  its archive, disk image, signatures, ARM64 executables, dependency loading,
  and command-line startup passed locally.
- [x] The final pull-request commit passes every hosted job after the review
  changes.
- [x] The final hosted macOS artifact is downloaded and passes the package test
  on a separate Apple Silicon Mac.
- [x] Generated C++ and C# stream files are current and `git diff --check` is
  clean.

## Merge

- [x] PR `#24` is not a draft, is mergeable into `main`, and has no unresolved
  review thread.
- [x] Required checks are green on the exact commit being merged.
- [ ] `main` contains the reviewed commit with no release-only change left on a
  side branch.

## Qualification and limitations

- [ ] Run the [hardware test guide](device-parity-runbook.md), or explicitly
  approve publication without the physical HoloLens 2, Vuforia, Vicon, and
  LabRecorder setup.
- [x] Record the macOS signing disposition: CI applies ad-hoc signatures for
  package integrity, but no Developer ID certificate or notarization credential
  is configured.
- [ ] Decide whether the documented first-launch approval is acceptable for
  `v1.12.0`; otherwise configure Developer ID signing and notarization before
  tagging.

Automated and local package checks do not prove physical tracking-volume,
device, or recorder integration. They also do not make an ad-hoc signature pass
Gatekeeper as a notarized Developer ID application. If publication proceeds
with these limitations, state them in the release notes and use `v1.11.0` as
the rollback release.

## Publication

- [ ] An annotated `v1.12.0` tag points to the exact reviewed commit in `main`.
- [ ] The hosted release publishes the Windows ZIP, Windows portable GUI,
  Linux archive, macOS ARM64 disk image, macOS ARM64 archive, and
  `SHA256SUMS.txt`.
- [ ] Fresh downloads match `SHA256SUMS.txt`.
- [ ] The downloaded Windows command-line, desktop, and portable applications
  start successfully, and the portable payload matches the Windows ZIP.
- [ ] The downloaded Linux command-line application opens its help on the
  target Ubuntu version and loads the included `liblsl.so.2`.
- [ ] The downloaded macOS archive and disk image pass
  `.github/scripts/test-package-macos.sh` on Apple Silicon.
- [ ] The published notes match [CHANGELOG.md](../CHANGELOG.md), state the
  hardware and signing dispositions, and name `v1.11.0` as the rollback release.
