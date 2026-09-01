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

# Package vicon-lsl-bridge-gui as macOS application bundle
if [[ -d vicon-lsl-bridge/build/vicon-lsl-bridge-gui.app ]]; then
  cp -R vicon-lsl-bridge/build/vicon-lsl-bridge-gui.app package/
  if [[ -n "$macdeployqt" && -x "$macdeployqt" ]]; then
    "$macdeployqt" package/vicon-lsl-bridge-gui.app
  fi
  cat << 'EOF' > package/vicon-lsl-bridge-gui
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -d "$DIR/vicon-lsl-bridge-gui.app" ]]; then
  exec "$DIR/vicon-lsl-bridge-gui.app/Contents/MacOS/vicon-lsl-bridge-gui" "$@"
fi
EOF
  chmod +x package/vicon-lsl-bridge-gui
elif [[ -f vicon-lsl-bridge/build/vicon-lsl-bridge-gui ]]; then
  cp vicon-lsl-bridge/build/vicon-lsl-bridge-gui package/
fi

test -f package/vicon-lsl-bridge-gui

# Package LabRecorder as macOS application bundle
if [[ -d build-labrecorder/LabRecorder.app ]]; then
  cp -R build-labrecorder/LabRecorder.app package/
  if [[ -n "$macdeployqt" && -x "$macdeployqt" ]]; then
    "$macdeployqt" package/LabRecorder.app
  fi
  cat << 'EOF' > package/LabRecorder
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -d "$DIR/LabRecorder.app" ]]; then
  exec "$DIR/LabRecorder.app/Contents/MacOS/LabRecorder" "$@"
fi
EOF
  chmod +x package/LabRecorder
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

# Thin fat binaries to arm64 architecture
while IFS= read -r binary; do
  [[ "$(lipo -archs "$binary" 2>/dev/null)" == *" "* ]] || continue
  lipo -thin arm64 "$binary" -output "$binary.arm64"
  mv "$binary.arm64" "$binary"
done < <(find package -type f \( -perm -u+x -o -name '*.dylib' \))

# Set portable RPATHs on all standalone binaries
for bin in package/vicon-lsl-bridge package/LabRecorderCLI; do
  if [[ -f "$bin" ]]; then
    install_name_tool -add_rpath "@executable_path" "$bin" 2>/dev/null || true
    install_name_tool -add_rpath "@loader_path" "$bin" 2>/dev/null || true
    install_name_tool -add_rpath "@executable_path/Frameworks" "$bin" 2>/dev/null || true
    install_name_tool -add_rpath "@loader_path/Frameworks" "$bin" 2>/dev/null || true
  fi
done

# Codesign macOS application bundles
for app in package/vicon-lsl-bridge-gui.app package/LabRecorder.app; do
  if [[ -d "$app" ]]; then
    codesign --force --deep --sign - "$app"
  fi
done

# Codesign standalone Mach-O files and frameworks
if [[ -d package/Frameworks/lsl.framework ]]; then
  codesign --force --deep --sign - package/Frameworks/lsl.framework 2>/dev/null || true
fi
if [[ -d package/lsl.framework ]]; then
  codesign --force --deep --sign - package/lsl.framework 2>/dev/null || true
fi
find package -maxdepth 1 -name 'liblsl*.dylib' -exec codesign --force --sign - {} + 2>/dev/null || true
find package/Frameworks -maxdepth 1 -name 'liblsl*.dylib' -exec codesign --force --sign - {} + 2>/dev/null || true
codesign --force --sign - package/vicon-lsl-bridge
codesign --force --sign - package/LabRecorderCLI

# Create tar.gz archive and Apple Disk Image (.dmg)
tar -czf "${artifact_name}.tar.gz" -C package .
hdiutil create -volname "Vicon LSL Bridge" -srcfolder package -ov -format UDZO "${artifact_name}.dmg"

