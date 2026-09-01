#!/usr/bin/env bash
set -euo pipefail

artifact_name="$1"
temp_dir="$(mktemp -d)"
trap 'rm -rf "$temp_dir"' EXIT

echo "Verifying macOS package artifacts for ${artifact_name}..."

# 1. Verify existence of primary release packages
test -f "${artifact_name}.tar.gz"
test -f "${artifact_name}.dmg"

# 2. Extract and inspect tar.gz
tar -xzf "${artifact_name}.tar.gz" -C "$temp_dir"

test -f "$temp_dir/vicon-lsl-bridge"
test -x "$temp_dir/vicon-lsl-bridge"
test -f "$temp_dir/vicon-lsl-bridge-gui"
test -x "$temp_dir/vicon-lsl-bridge-gui"
test -f "$temp_dir/LabRecorder"
test -x "$temp_dir/LabRecorder"
test -f "$temp_dir/LabRecorderCLI"
test -x "$temp_dir/LabRecorderCLI"

# 3. Check for ARM64 Mach-O architecture
file "$temp_dir/vicon-lsl-bridge" | grep -E "Mach-O 64-bit (executable )?arm64"
if [[ -d "$temp_dir/vicon-lsl-bridge-gui.app" ]]; then
  file "$temp_dir/vicon-lsl-bridge-gui.app/Contents/MacOS/vicon-lsl-bridge-gui" | grep -E "Mach-O 64-bit (executable )?arm64"
else
  file "$temp_dir/vicon-lsl-bridge-gui" | grep -E "Mach-O 64-bit (executable )?arm64"
fi
file "$temp_dir/LabRecorderCLI" | grep -E "Mach-O 64-bit (executable )?arm64"

# 4. Verify liblsl shared library is packaged
find "$temp_dir" -maxdepth 1 -name 'liblsl*.dylib' -print -quit | grep -q .

# 5. Verify config file placement
test -f "$temp_dir/LabRecorder.cfg"
if [[ -d "$temp_dir/LabRecorder.app" ]]; then
  test -f "$temp_dir/LabRecorder.app/Contents/Resources/LabRecorder.cfg"
  test ! -f "$temp_dir/LabRecorder.app/Contents/MacOS/LabRecorder.cfg"
fi

# 6. Verify code signature validity
echo "Checking code signatures..."
if [[ -d "$temp_dir/LabRecorder.app" ]]; then
  codesign --verify --deep "$temp_dir/LabRecorder.app"
fi
if [[ -d "$temp_dir/vicon-lsl-bridge-gui.app" ]]; then
  codesign --verify --deep "$temp_dir/vicon-lsl-bridge-gui.app"
fi
codesign --verify "$temp_dir/vicon-lsl-bridge"
codesign --verify "$temp_dir/LabRecorderCLI"

# 7. Test standalone binary execution
echo "Testing CLI binary execution..."
(cd "$temp_dir" && ./vicon-lsl-bridge --help >/dev/null)
(cd "$temp_dir" && ./LabRecorderCLI -h >/dev/null)

echo "All macOS package verification checks passed successfully."

