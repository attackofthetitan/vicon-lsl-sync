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
  macdeployqt="${QT_ROOT_DIR:+$QT_ROOT_DIR/bin/macdeployqt}"
  [[ -x "$macdeployqt" ]] || macdeployqt="$(command -v macdeployqt)"
  "$macdeployqt" package/LabRecorder.app
  if [[ -f build-labrecorder/LabRecorder.app/Contents/MacOS/LabRecorder ]]; then
    cp build-labrecorder/LabRecorder.app/Contents/MacOS/LabRecorder package/LabRecorder
  fi
fi

test -f package/LabRecorder

# Ensure configuration files and resources are placed correctly
mkdir -p package/LabRecorder.app/Contents/Resources
if [[ -f package/LabRecorder.app/Contents/MacOS/LabRecorder.cfg ]]; then
  mv package/LabRecorder.app/Contents/MacOS/LabRecorder.cfg package/LabRecorder.app/Contents/Resources/
fi
if [[ -f labrecorder/LabRecorder.cfg ]]; then
  cp labrecorder/LabRecorder.cfg package/LabRecorder.cfg
  if [[ ! -f package/LabRecorder.app/Contents/Resources/LabRecorder.cfg ]]; then
    cp labrecorder/LabRecorder.cfg package/LabRecorder.app/Contents/Resources/
  fi
fi

# Gather liblsl shared libraries
find vicon-lsl-bridge/build/_deps/liblsl-build -name 'liblsl*.dylib' -exec cp -P {} package/ \; || true
find build-labrecorder -name 'liblsl*.dylib' -exec cp -P {} package/ \; || true
if [[ -d package/LabRecorder.app ]]; then
  mkdir -p package/LabRecorder.app/Contents/Frameworks
  find build-labrecorder -name 'liblsl*.dylib' -exec cp -P {} package/LabRecorder.app/Contents/Frameworks/ \; || true
  find build-labrecorder -name 'lsl.framework' -exec cp -R {} package/LabRecorder.app/Contents/Frameworks/ \; || true
fi

# Verify shared library presence
find package -maxdepth 1 -name 'liblsl*.dylib' -print -quit | grep -q .

while IFS= read -r binary; do
  [[ "$(lipo -archs "$binary" 2>/dev/null)" == *" "* ]] || continue
  lipo -thin arm64 "$binary" -output "$binary.arm64"
  mv "$binary.arm64" "$binary"
done < <(find package -type f \( -perm -u+x -o -name '*.dylib' \))

# Codesign macOS application bundle inside-out
if [[ -d package/LabRecorder.app ]]; then
  if [[ -d package/LabRecorder.app/Contents/Frameworks ]]; then
    find package/LabRecorder.app/Contents/Frameworks -maxdepth 1 -name '*.framework' -exec codesign --force --sign - {} +
    find package/LabRecorder.app/Contents/Frameworks -type f -name '*.dylib' -exec codesign --force --sign - {} +
  fi
  if [[ -d package/LabRecorder.app/Contents/PlugIns ]]; then
    find package/LabRecorder.app/Contents/PlugIns -type f -name '*.dylib' -exec codesign --force --sign - {} +
  fi
  if [[ -d package/LabRecorder.app/Contents/MacOS ]]; then
    find package/LabRecorder.app/Contents/MacOS -type f -perm -u+x -exec codesign --force --sign - {} +
  fi
  codesign --force --sign - package/LabRecorder.app
fi

# Codesign root binaries and dylibs
find package -maxdepth 1 -type f \( -perm -u+x -o -name '*.dylib' \) -exec codesign --force --sign - {} +

# Create tar.gz archive and Apple Disk Image (.dmg)
tar -czf "${artifact_name}.tar.gz" -C package .
hdiutil create -volname "Vicon LSL Bridge" -srcfolder package -ov -format UDZO "${artifact_name}.dmg"

