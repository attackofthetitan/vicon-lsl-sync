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
file "$temp_dir/vicon-lsl-bridge-gui" | grep -E "Mach-O 64-bit (executable )?arm64"
file "$temp_dir/LabRecorderCLI" | grep -E "Mach-O 64-bit (executable )?arm64"

# 4. Verify liblsl shared library is packaged
find "$temp_dir" -maxdepth 1 -name 'liblsl*.dylib' -print -quit | grep -q .

echo "All macOS package verification checks passed successfully."
