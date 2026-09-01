#!/usr/bin/env bash
set -euo pipefail

artifact_name="$1"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
expected_version="${2:-$(sed -nE 's/^[[:space:]]*project\(vicon-lsl-bridge[[:space:]]+VERSION[[:space:]]+([^[:space:])]+).*/\1/p' "$repo_root/vicon-lsl-bridge/CMakeLists.txt" | head -n1)}"
archive="${artifact_name}.tar.gz"
disk_image="${artifact_name}.dmg"
temp_dir="$(mktemp -d)"
archive_root="$temp_dir/archive"
mount_root="$temp_dir/dmg"
mounted=false

cleanup() {
  if [[ "$mounted" == true ]]; then
    hdiutil detach "$mount_root" >/dev/null || true
  fi
  rm -rf "$temp_dir"
}
trap cleanup EXIT

fail() {
  echo "$*" >&2
  exit 1
}

assert_arm64_signed_macho_payload() {
  local root="$1"
  local count=0
  local binary
  local archs
  local dependency

  while IFS= read -r -d '' binary; do
    if ! file -b "$binary" | grep -q '^Mach-O'; then
      continue
    fi
    count=$((count + 1))
    archs="$(lipo -archs "$binary")"
    [[ "$archs" == "arm64" ]] || fail "Expected arm64-only Mach-O file, found '$archs': $binary"
    codesign --verify --strict "$binary"

    while read -r dependency _; do
      case "$dependency" in
        @*|/System/*|/usr/lib/*) ;;
        *) fail "Non-portable dependency '$dependency' in $binary" ;;
      esac
    done < <(otool -L "$binary" | tail -n +2)
  done < <(find "$root" -type f -print0)

  (( count > 0 )) || fail "No Mach-O files found in $root"
}

verify_payload() {
  local root="$1"
  local bridge_app="$root/vicon-lsl-bridge-gui.app"
  local recorder_app="$root/LabRecorder.app"

  test -x "$root/vicon-lsl-bridge"
  test -x "$root/vicon-lsl-bridge-gui"
  test -x "$root/LabRecorder"
  test -x "$root/LabRecorderCLI"
  test -x "$bridge_app/Contents/MacOS/vicon-lsl-bridge-gui"
  test -x "$recorder_app/Contents/MacOS/LabRecorder"
  test -f "$root/LabRecorder.cfg"
  test -f "$recorder_app/Contents/Resources/LabRecorder.cfg"
  test ! -f "$recorder_app/Contents/MacOS/LabRecorder.cfg"
  test -d "$bridge_app/Contents/Frameworks/QtCore.framework"
  test -d "$recorder_app/Contents/Frameworks/QtCore.framework"
  test -e "$root/Frameworks/lsl.framework/lsl"
  find "$root" -maxdepth 1 -name 'liblsl*.dylib' -print -quit | grep -q .

  [[ "$(plutil -extract CFBundleShortVersionString raw "$bridge_app/Contents/Info.plist")" == "$expected_version" ]]
  codesign --verify --deep --strict "$bridge_app"
  codesign --verify --deep --strict "$recorder_app"
  assert_arm64_signed_macho_payload "$root"

  if ! (cd "$root" && ./vicon-lsl-bridge --help >/dev/null); then
    otool -L "$root/vicon-lsl-bridge" >&2
    otool -l "$root/vicon-lsl-bridge" | grep -A2 LC_RPATH >&2
    fail "vicon-lsl-bridge execution failed in $root"
  fi
  if ! (cd "$root" && (./LabRecorderCLI -h 2>&1 || true) | grep -q 'Usage:'); then
    (cd "$root" && ./LabRecorderCLI -h || true) >&2
    otool -L "$root/LabRecorderCLI" >&2
    otool -l "$root/LabRecorderCLI" | grep -A2 LC_RPATH >&2
    fail "LabRecorderCLI execution failed in $root"
  fi
}

echo "Verifying macOS package artifacts for ${artifact_name}..."
test -n "$expected_version"
test -f "$archive"
test -f "$disk_image"
hdiutil verify "$disk_image" >/dev/null

mkdir -p "$archive_root" "$mount_root"
tar -xzf "$archive" -C "$archive_root"
verify_payload "$archive_root"

hdiutil attach -readonly -nobrowse -mountpoint "$mount_root" "$disk_image" >/dev/null
mounted=true
verify_payload "$mount_root"

echo "All macOS archive and disk-image verification checks passed successfully."
echo "The release package is ad-hoc signed; Developer ID signing and notarization are not configured."
