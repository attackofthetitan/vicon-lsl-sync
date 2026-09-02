#!/usr/bin/env bash
set -euo pipefail

artifact_name="$1"

mkdir -p package
test -f vicon-lsl-bridge/build/vicon-lsl-bridge
test -f vicon-lsl-bridge/build/vicon-lsl-bridge-gui
test -f build-labrecorder/LabRecorder
test -f build-labrecorder/LabRecorderCLI
find vicon-lsl-bridge/build/_deps/liblsl-build -name 'liblsl.so*' -print -quit | grep -q .
find build-labrecorder -name 'liblsl.so*' -print -quit | grep -q .
cp vicon-lsl-bridge/build/vicon-lsl-bridge package/
cp vicon-lsl-bridge/build/vicon-lsl-bridge-gui package/
find vicon-lsl-bridge/build/_deps/liblsl-build -name 'liblsl.so*' -exec cp -P {} package/ \;
# The stair model the preview draws, beside the binary as on Windows.
test -f vicon-lsl-bridge/assets/stair_model/stair_model1.obj
mkdir -p package/stair_model
cp vicon-lsl-bridge/assets/stair_model/stair_model1.obj \
   vicon-lsl-bridge/assets/stair_model/stair_model1.mtl package/stair_model/
cp build-labrecorder/LabRecorder package/
cp build-labrecorder/LabRecorderCLI package/
find build-labrecorder -name 'liblsl.so*' -exec cp -P {} package/ \;
find package -maxdepth 1 -name 'liblsl.so*' -print -quit | grep -q .
tar -czf "${artifact_name}.tar.gz" -C package .
