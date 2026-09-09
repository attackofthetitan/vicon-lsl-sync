# v1.14.2 release checklist

## Release details

- Version: `1.14.2`
- Previous release: `v1.14.1`
- Target date: 2026-09-09
- Pull requests: none; released directly from `main`
- Status: completed
- Scope: the graphical LabRecorder writes the destination the app checked and displayed, instead of re-expanding the filename template under its own legacy rules

A patch release. It changes the encoding of the remote `filename` command sent
to the graphical recorder. Stream schemas, XDF contents, saved session profiles,
filename templates, and CLI options are unchanged.

## Pre-merge checks

- [x] The CMake version and dated changelog section both use `1.14.2`.
- [x] The dependency-light logic suite passes locally: 79 test cases.
- [x] Stream contracts verified unchanged with `tools/generate_stream_contracts.py --check`.
- [x] `git diff --check` is clean.
- [x] The recorder's substitution rules were re-read in `labrecorder/src/mainwindow.cpp`
  (`rcsUpdateFilename`, `replaceFilename`, `counterPlaceholder`) and match what
  `LabRecorderFilenamePolicy::filenameCommand` now relies on: `template` and
  `modality` are lowercased, `%b` is substituted first and keeps its case, and
  the legacy counter is `%n` padded to three digits.
- [x] Removing the fix fails the filename, protocol, and destination assertions,
  confirming the regression coverage has teeth.
- [x] `docs/behavior-contract.md` and `README.md` describe the wire encoding.

## Publication

- [ ] Tag `v1.14.2` on the release commit, with the tagged build green and the
  release assets published.
- [ ] Confirm assets against `SHA256SUMS.txt`, with the bundle reporting
  version `1.14.2`.

## Device checks not run

- Physical Vicon, HoloLens, and Vuforia checks were not run for this release.
- A recording against the graphical LabRecorder should be spot-checked on the
  study machine: confirm the written `.xdf` path matches **Recording
  Destination** exactly, including capitalisation, subdirectories, and the run
  number as entered.
