#!/usr/bin/env bash
set -euo pipefail

tag_commit="$(git rev-parse "${GITHUB_REF_NAME}^{commit}")"
if [[ "$tag_commit" != "$GITHUB_SHA" ]]; then
  echo "Release tag $GITHUB_REF_NAME resolves to $tag_commit, not $GITHUB_SHA" >&2
  exit 1
fi
if ! git merge-base --is-ancestor "$GITHUB_SHA" origin/main; then
  echo "Release commit $GITHUB_SHA is not an ancestor of origin/main" >&2
  exit 1
fi
