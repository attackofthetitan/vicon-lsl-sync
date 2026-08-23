#!/usr/bin/env bash
set -euo pipefail

if [[ ! "$GITHUB_REF_NAME" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Release tag must use the exact vN.N.N format: $GITHUB_REF_NAME" >&2
  exit 1
fi
tag_version="${GITHUB_REF_NAME#v}"
cmake_version="$(sed -nE 's/^[[:space:]]*project\(vicon-lsl-bridge[[:space:]]+VERSION[[:space:]]+([^[:space:])]+).*/\1/p' vicon-lsl-bridge/CMakeLists.txt | head -n1)"
if [[ -z "$cmake_version" ]]; then
  echo "Unable to read the vicon-lsl-bridge CMake project version" >&2
  exit 1
fi
if [[ "$tag_version" != "$cmake_version" ]]; then
  echo "Release tag $GITHUB_REF_NAME does not match CMake version $cmake_version" >&2
  exit 1
fi

escaped_version="${tag_version//./\\.}"
if ! grep -Eq "^## \\[$escaped_version\\] - [0-9]{4}-[0-9]{2}-[0-9]{2}$" CHANGELOG.md; then
  echo "CHANGELOG.md is missing a dated section for $tag_version" >&2
  exit 1
fi
