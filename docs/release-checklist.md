# v1.14.5 release checklist

## Release details

- Version: `1.14.5`
- Previous release: `v1.14.4`
- Target date: 2026-09-18
- Pull requests: none; releasing directly from `main`
- Status: prepared; publication pending tagged CI
- Scope: preserve a stable stair reference in recordings started after Vuforia
  is paused, and identify missing or frozen calibration during desktop playback

The target stream keeps eight channels. Its final `Tracked` channel now has unit
`state`: 0 invalid, 1 live tracked, and 2 frozen reference. The updated HoloLens
publisher must be deployed; desktop binaries alone cannot supply missing target
poses. The XDF contains the frozen target pose, not the final desktop transform.

## Pre-publication checks

- [x] The CMake version and dated changelog section use `1.14.5`.
- [x] Platform-neutral HoloLens checks pass locally: 34 test cases, including
  stable acquisition, intentional pause, tracking loss, resume, reset, invalid
  poses, position jumps, and quaternion averaging.
- [x] Desktop logic checks pass locally: 78 test cases, including an XDF recorded
  entirely with frozen target poses and the original all-invalid pattern.
- [x] Desktop GUI checks pass locally at normal and 1.5x scale: five cases each.
- [x] All six desktop CTest suites pass, including recorder, lifecycle, and
  stream recovery; the v1.14.5 desktop executable builds successfully.
- [x] Generated stream contracts pass `tools/generate_stream_contracts.py --check`.
- [x] `git diff --check` is clean.
- [x] Behavior, timing, recording instructions, and the hardware runbook describe
  frozen-reference semantics and the required HoloLens deployment.

## Publication

- [ ] Release commit pushed to `main`; tag `v1.14.5` points to that commit.
- [ ] Tagged CI passes the logic matrix, HoloLens checks, and full desktop build,
  tests, and packaging on Linux x64, Windows x64, and macOS arm64.
- [ ] Five platform payloads and `SHA256SUMS.txt` are published and verified.
- [ ] Release notes explain the HoloLens upgrade and reference-pose semantics.

## Device checks not run

- Physical Unity/OpenXR/Vuforia, HoloLens, and Vicon checks were not run locally.
- Follow **Recording after deliberately pausing Vuforia** in
  `docs/device-parity-runbook.md`: record after pausing, reopen in a fresh desktop
  session, then check resume, reacquisition, and ordinary tracking loss.
