#!/usr/bin/env bash
set -euo pipefail

artifact_name="$1"

mkdir -p package
test -f vicon-lsl-bridge/build/vicon-lsl-bridge
test -f build-labrecorder/LabRecorderCLI

macdeployqt="${QT_ROOT_DIR:+$QT_ROOT_DIR/bin/macdeployqt}"
[[ -x "$macdeployqt" ]] || macdeployqt="$(command -v macdeployqt 2>/dev/null || true)"

cp vicon-lsl-bridge/build/vicon-lsl-bridge package/
cp build-labrecorder/LabRecorderCLI package/

if [[ -d vicon-lsl-bridge/build/vicon-lsl-bridge-gui.app ]]; then
  cp -R vicon-lsl-bridge/build/vicon-lsl-bridge-gui.app package/
  if [[ -n "$macdeployqt" && -x "$macdeployqt" ]]; then
    "$macdeployqt" package/vicon-lsl-bridge-gui.app
  fi
  if [[ -f vicon-lsl-bridge/build/vicon-lsl-bridge-gui.app/Contents/MacOS/vicon-lsl-bridge-gui ]]; then
    cp vicon-lsl-bridge/build/vicon-lsl-bridge-gui.app/Contents/MacOS/vicon-lsl-bridge-gui package/vicon-lsl-bridge-gui
  fi
elif [[ -f vicon-lsl-bridge/build/vicon-lsl-bridge-gui ]]; then
  cp vicon-lsl-bridge/build/vicon-lsl-bridge-gui package/
fi

test -f package/vicon-lsl-bridge-gui

if [[ -d build-labrecorder/LabRecorder.app ]]; then
  cp -R build-labrecorder/LabRecorder.app package/
  if [[ -n "$macdeployqt" && -x "$macdeployqt" ]]; then
    "$macdeployqt" package/LabRecorder.app
  fi
  if [[ -f build-labrecorder/LabRecorder.app/Contents/MacOS/LabRecorder ]]; then
    cp build-labrecorder/LabRecorder.app/Contents/MacOS/LabRecorder package/LabRecorder
  fi
elif [[ -f build-labrecorder/LabRecorder ]]; then
  cp build-labrecorder/LabRecorder package/
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

# Gather liblsl shared libraries and frameworks
mkdir -p package/Frameworks
find vicon-lsl-bridge/build -name 'liblsl*.dylib' -exec cp -P {} package/ \; || true
find vicon-lsl-bridge/build -name 'liblsl*.dylib' -exec cp -P {} package/Frameworks/ \; || true
find build-labrecorder -name 'liblsl*.dylib' -exec cp -P {} package/ \; || true
find build-labrecorder -name 'liblsl*.dylib' -exec cp -P {} package/Frameworks/ \; || true
find build-labrecorder -name 'lsl.framework' -exec cp -R {} package/ \; || true
find build-labrecorder -name 'lsl.framework' -exec cp -R {} package/Frameworks/ \; || true

if [[ -d package/vicon-lsl-bridge-gui.app ]]; then
  mkdir -p package/vicon-lsl-bridge-gui.app/Contents/Frameworks
  find vicon-lsl-bridge/build -name 'liblsl*.dylib' -exec cp -P {} package/vicon-lsl-bridge-gui.app/Contents/Frameworks/ \; || true
  find build-labrecorder -name 'lsl.framework' -exec cp -R {} package/vicon-lsl-bridge-gui.app/Contents/Frameworks/ \; || true
fi

if [[ -d package/LabRecorder.app ]]; then
  mkdir -p package/LabRecorder.app/Contents/Frameworks
  find build-labrecorder -name 'liblsl*.dylib' -exec cp -P {} package/LabRecorder.app/Contents/Frameworks/ \; || true
  find build-labrecorder -name 'lsl.framework' -exec cp -R {} package/LabRecorder.app/Contents/Frameworks/ \; || true
fi

# Verify shared library presence
find package -maxdepth 1 -name 'liblsl*.dylib' -print -quit | grep -q .

# Set portable RPATHs on all standalone binaries
for bin in package/vicon-lsl-bridge package/vicon-lsl-bridge-gui package/LabRecorder package/LabRecorderCLI; do
  if [[ -f "$bin" ]]; then
    install_name_tool -add_rpath "@executable_path" "$bin" 2>/dev/null || true
    install_name_tool -add_rpath "@loader_path" "$bin" 2>/dev/null || true
    install_name_tool -add_rpath "@executable_path/Frameworks" "$bin" 2>/dev/null || true
    install_name_tool -add_rpath "@loader_path/Frameworks" "$bin" 2>/dev/null || true
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$bin" 2>/dev/null || true
  fi
done

while IFS= read -r binary; do
  [[ "$(lipo -archs "$binary" 2>/dev/null)" == *" "* ]] || continue
  lipo -thin arm64 "$binary" -output "$binary.arm64"
  mv "$binary.arm64" "$binary"
done < <(find package -type f \( -perm -u+x -o -name '*.dylib' \))

# Codesign macOS application bundles inside-out
for app in package/vicon-lsl-bridge-gui.app package/LabRecorder.app; do
  if [[ -d "$app" ]]; then
    if [[ -d "$app/Contents/Frameworks" ]]; then
      find "$app/Contents/Frameworks" -maxdepth 1 -name '*.framework' -exec codesign --force --sign - {} + 2>/dev/null || true
      find "$app/Contents/Frameworks" -type f -name '*.dylib' -exec codesign --force --sign - {} + 2>/dev/null || true
    fi
    if [[ -d "$app/Contents/PlugIns" ]]; then
      find "$app/Contents/PlugIns" -type f -name '*.dylib' -exec codesign --force --sign - {} + 2>/dev/null || true
    fi
    if [[ -d "$app/Contents/MacOS" ]]; then
      find "$app/Contents/MacOS" -type f -perm -u+x -exec codesign --force --sign - {} + 2>/dev/null || true
    fi
    codesign --force --sign - "$app"
  fi
done

# Codesign root frameworks, dylibs, and binaries
find package/Frameworks -maxdepth 1 -name '*.framework' -exec codesign --force --sign - {} + 2>/dev/null || true
find package -maxdepth 1 -name '*.framework' -exec codesign --force --sign - {} + 2>/dev/null || true
find package -maxdepth 1 -type f \( -perm -u+x -o -name '*.dylib' \) -exec codesign --force --sign - {} +

# Create tar.gz archive and Apple Disk Image (.dmg)
tar -czf "${artifact_name}.tar.gz" -C package .
hdiutil create -volname "Vicon LSL Bridge" -srcfolder package -ov -format UDZO "${artifact_name}.dmg"

