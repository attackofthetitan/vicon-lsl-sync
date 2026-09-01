#!/usr/bin/env bash
set -euo pipefail

version="${GITHUB_REF_NAME#v}"
if [[ -e release-assets ]]; then
  echo "Release asset staging path unexpectedly already exists" >&2
  exit 1
fi
mkdir -p release-assets
windows_dir="artifacts/vicon-lsl-bridge-windows-x64"
linux_dir="artifacts/vicon-lsl-bridge-linux-x64"
macos_dir="artifacts/vicon-lsl-bridge-macos-arm64"
portable_src="$windows_dir/vicon-lsl-bridge-windows-x64-gui-portable.exe"
windows_zip_src="$windows_dir/vicon-lsl-bridge-windows-x64.zip"
linux_tar_src="$linux_dir/vicon-lsl-bridge-linux-x64.tar.gz"
macos_dmg_src="$macos_dir/vicon-lsl-bridge-macos-arm64.dmg"
macos_tar_src="$macos_dir/vicon-lsl-bridge-macos-arm64.tar.gz"
for source in "$portable_src" "$windows_zip_src" "$linux_tar_src" "$macos_dmg_src" "$macos_tar_src"; do
  if [[ ! -f "$source" ]]; then
    echo "Release artifact is missing: $source" >&2
    exit 1
  fi
done
cp -- "$portable_src" "release-assets/vicon-lsl-bridge-v${version}-windows-x64-gui-portable.exe"
cp -- "$windows_zip_src" "release-assets/vicon-lsl-bridge-v${version}-windows-x64.zip"
cp -- "$linux_tar_src" "release-assets/vicon-lsl-bridge-v${version}-linux-x64.tar.gz"
cp -- "$macos_dmg_src" "release-assets/vicon-lsl-bridge-v${version}-macos-arm64.dmg"
cp -- "$macos_tar_src" "release-assets/vicon-lsl-bridge-v${version}-macos-arm64.tar.gz"
mapfile -t payloads < <(find release-assets -maxdepth 1 -type f -printf '%f\n' | sort)
if [[ "${#payloads[@]}" -ne 5 ]]; then
  echo "Expected exactly five payload files, found ${#payloads[@]}" >&2
  exit 1
fi
(
  cd release-assets
  sha256sum "${payloads[@]}" > SHA256SUMS.txt
)
