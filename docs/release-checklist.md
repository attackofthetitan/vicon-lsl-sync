# v1.14.1 release checklist

## Release details

- Version: `1.14.1`
- Previous release: `v1.14.0`
- Target date: 2026-09-08
- Pull requests: `#37` (CI speedup and build tuning)
- Status: completed
- Scope: CI build workflow and build system performance optimizations (Ninja Multi-Config, ccache, /MP compilation, prebuilt Boost, Qt license caching, and deduplicated test compilation)

A patch release. It contains build system, CI pipeline, and packaging improvements without breaking any stream contracts or behavior.

## Pre-merge checks

- [x] The CMake version and dated changelog section both use `1.14.1`.
- [x] The dependency-light logic suite passes locally: 74 test cases.
- [x] Stream contracts verified unchanged with `tools/generate_stream_contracts.py --check`.
- [x] `git diff --check` is clean.
- [x] PR `#37` merged into `main` with green matrix.

## Publication

- [x] Tagged `v1.14.1` on the release commit, with the tagged build green and the
  release assets published.
- [x] Assets confirmed against `SHA256SUMS.txt`, with the bundle reporting
  version `1.14.1`.
