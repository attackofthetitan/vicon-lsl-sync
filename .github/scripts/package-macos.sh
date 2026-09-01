#!/usr/bin/env bash
set -euo pipefail

artifact_name="$1"

mkdir -p package
test -f vicon-lsl-bridge/build/vicon-lsl-bridge
test -f vicon-lsl-bridge/build/vicon-lsl-bridge-gui
test -f build-labrecorder/LabRecorderCLI

cp vicon-lsl-bridge/build/vicon-lsl-bridge package/
cp vicon-lsl-bridge/build/vicon-lsl-bridge-gui package/
cp build-labrecorder/LabRecorderCLI package/

if [[ -f build-labrecorder/LabRecorder ]]; then
  cp build-labrecorder/LabRecorder package/
elif [[ -d build-labrecorder/LabRecorder.app ]]; then
  cp -R build-labrecorder/LabRecorder.app package/
  if [[ -f build-labrecorder/LabRecorder.app/Contents/MacOS/LabRecorder ]]; then
    cp build-labrecorder/LabRecorder.app/Contents/MacOS/LabRecorder package/LabRecorder
  fi
fi

test -f package/LabRecorder

# Gather liblsl shared libraries
find vicon-lsl-bridge/build/_deps/liblsl-build -name 'liblsl*.dylib' -exec cp -P {} package/ \; || true
find build-labrecorder -name 'liblsl*.dylib' -exec cp -P {} package/ \; || true

# Verify shared library presence
find package -maxdepth 1 -name 'liblsl*.dylib' -print -quit | grep -q .

# Create tar.gz archive and Apple Disk Image (.dmg)
tar -czf "${artifact_name}.tar.gz" -C package .
hdiutil create -volname "Vicon LSL Bridge" -srcfolder package -ov -format UDZO "${artifact_name}.dmg"
