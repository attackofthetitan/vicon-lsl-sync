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

# The rpaths a Mach-O file searches for its @rpath libraries.
rpaths() {
  otool -l "$1" | awk '/cmd LC_RPATH/ { found = 1; next }
    found && $1 == "path" { sub(/^ *path /, ""); sub(/ \(offset [0-9]+\)$/, ""); print; found = 0 }'
}

assert_arm64_signed_macho_payload() {
  local root="$1"
  local count=0
  local binary
  local archs
  local dependency
  local rpath

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

    while IFS= read -r rpath; do
      [[ "$rpath" != /* ]] || fail "Build-machine rpath '$rpath' in $binary"
    done < <(rpaths "$binary")
  done < <(find "$root" -type f -print0)

  (( count > 0 )) || fail "No Mach-O files found in $root"
}

# A program must find every library it links inside the package, the way dyld
# looks for it, or it does not start on a computer without the build folders.
assert_libraries_resolve() {
  local executable="$1"
  local directory
  local dependency
  local rpath
  local found
  local search=()
  directory="$(dirname "$executable")"

  while IFS= read -r rpath; do
    # A folder on the build machine is not in the package.
    [[ "$rpath" == "@"* ]] || continue
    rpath="${rpath/#@executable_path/$directory}"
    search+=("${rpath/#@loader_path/$directory}")
  done < <(rpaths "$executable")

  while read -r dependency _; do
    found=false
    case "$dependency" in
      /System/*|/usr/lib/*) found=true ;;
      @rpath/*)
        # The guard keeps an empty list usable under set -u in bash 3.2.
        for rpath in ${search[@]+"${search[@]}"}; do
          if [[ -e "$rpath/${dependency#@rpath/}" ]]; then
            found=true
            break
          fi
        done
        ;;
      @executable_path/*|@loader_path/*)
        [[ -e "$directory/${dependency#*/}" ]] && found=true
        ;;
    esac
    [[ "$found" == true ]] || fail "$executable cannot find '$dependency' inside the package"
  done < <(otool -L "$executable" | tail -n +2)
}

# macOS shows these reasons when it asks the user for network and folder access.
assert_access_reasons() {
  local plist="$1"
  local key
  for key in NSLocalNetworkUsageDescription NSDocumentsFolderUsageDescription \
             NSDesktopFolderUsageDescription NSDownloadsFolderUsageDescription \
             NSRemovableVolumesUsageDescription NSNetworkVolumesUsageDescription; do
    plutil -extract "$key" raw "$plist" >/dev/null || fail "$plist is missing $key"
  done
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
  test -f "$root/stair_model/stair_model1.obj"
  test -f "$bridge_app/Contents/Resources/stair_model/stair_model1.obj"
  assert_access_reasons "$bridge_app/Contents/Info.plist"
  assert_access_reasons "$recorder_app/Contents/Info.plist"

  [[ "$(plutil -extract CFBundleShortVersionString raw "$bridge_app/Contents/Info.plist")" == "$expected_version" ]]
  codesign --verify --deep --strict "$bridge_app"
  codesign --verify --deep --strict "$recorder_app"
  assert_arm64_signed_macho_payload "$root"
  assert_libraries_resolve "$root/vicon-lsl-bridge"
  assert_libraries_resolve "$root/LabRecorderCLI"
  assert_libraries_resolve "$bridge_app/Contents/MacOS/vicon-lsl-bridge-gui"
  assert_libraries_resolve "$recorder_app/Contents/MacOS/LabRecorder"

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

verify_disk_image() {
  local root="$1"
  local bridge_app="$root/vicon-lsl-bridge-gui.app"
  local recorder_app="$bridge_app/Contents/Helpers/LabRecorder.app"
  local tools="$root/Command Line Tools"

  # Drag-install layout: one app beside a link to /Applications. The recorders
  # travel inside it, so macOS approves them together with the app.
  test -d "$bridge_app"
  test ! -e "$root/LabRecorder.app"
  test -L "$root/Applications"
  [[ "$(readlink "$root/Applications")" == "/Applications" ]] \
    || fail "Applications link does not point at /Applications"
  # The app ejects the image by recognising this marker.
  [[ "$(cat "$root/.vicon-lsl-bridge-installer")" == "$expected_version" ]] \
    || fail "Installer marker does not name version $expected_version"

  test -x "$bridge_app/Contents/MacOS/vicon-lsl-bridge-gui"
  test -x "$recorder_app/Contents/MacOS/LabRecorder"
  test -x "$recorder_app/Contents/MacOS/LabRecorderCLI"
  test -f "$recorder_app/Contents/Resources/LabRecorder.cfg"
  test -d "$bridge_app/Contents/Frameworks/QtCore.framework"
  test -f "$bridge_app/Contents/Resources/stair_model/stair_model1.obj"
  [[ "$(plutil -extract CFBundleShortVersionString raw "$bridge_app/Contents/Info.plist")" == "$expected_version" ]]
  assert_access_reasons "$bridge_app/Contents/Info.plist"
  assert_access_reasons "$recorder_app/Contents/Info.plist"
  codesign --verify --deep --strict "$bridge_app"
  assert_arm64_signed_macho_payload "$bridge_app"
  assert_libraries_resolve "$bridge_app/Contents/MacOS/vicon-lsl-bridge-gui"
  assert_libraries_resolve "$recorder_app/Contents/MacOS/LabRecorder"
  assert_libraries_resolve "$recorder_app/Contents/MacOS/LabRecorderCLI"
  if ! ("$recorder_app/Contents/MacOS/LabRecorderCLI" -h 2>&1 || true) | grep -q 'Usage:'; then
    fail "The LabRecorderCLI inside the app did not start"
  fi

  # The command line payload stays on the image, out of the drag target's way.
  test -x "$tools/vicon-lsl-bridge"
  test -x "$tools/LabRecorderCLI"
  test -f "$tools/LabRecorder.cfg"
  if ! (cd "$tools" && ./vicon-lsl-bridge --help >/dev/null); then
    otool -L "$tools/vicon-lsl-bridge" >&2
    fail "vicon-lsl-bridge execution failed in $tools"
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
verify_disk_image "$mount_root"

echo "All macOS archive and disk-image verification checks passed successfully."
echo "The release package is ad-hoc signed; Developer ID signing and notarization are not configured."
