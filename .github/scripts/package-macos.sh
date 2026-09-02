#!/usr/bin/env bash
set -euo pipefail

artifact_name="$1"

if [[ -e package ]]; then
  echo "Refusing to reuse an existing package directory" >&2
  exit 1
fi
mkdir -p package
test -f vicon-lsl-bridge/build/vicon-lsl-bridge
test -f build-labrecorder/LabRecorderCLI
test -d vicon-lsl-bridge/build/vicon-lsl-bridge-gui.app
test -d build-labrecorder/LabRecorder.app

macdeployqt="${QT_ROOT_DIR:+$QT_ROOT_DIR/bin/macdeployqt}"
[[ -x "$macdeployqt" ]] || macdeployqt="$(command -v macdeployqt 2>/dev/null || true)"
if [[ -z "$macdeployqt" || ! -x "$macdeployqt" ]]; then
  echo "macdeployqt is required to create a self-contained macOS package" >&2
  exit 1
fi

cp -- vicon-lsl-bridge/build/vicon-lsl-bridge package/
cp -- build-labrecorder/LabRecorderCLI package/

# Package vicon-lsl-bridge-gui as macOS application bundle
cp -R -- vicon-lsl-bridge/build/vicon-lsl-bridge-gui.app package/
"$macdeployqt" package/vicon-lsl-bridge-gui.app -libpath=vicon-lsl-bridge/build/_deps/liblsl-build 2>/dev/null || \
  "$macdeployqt" package/vicon-lsl-bridge-gui.app
cat << 'EOF' > package/vicon-lsl-bridge-gui
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -d "$DIR/vicon-lsl-bridge-gui.app" ]]; then
  exec "$DIR/vicon-lsl-bridge-gui.app/Contents/MacOS/vicon-lsl-bridge-gui" "$@"
fi
EOF
chmod +x package/vicon-lsl-bridge-gui

# The stair model the preview draws. Windows packaging already ships it; without
# it the preview has no stair to align gaze against.
test -f vicon-lsl-bridge/assets/stair_model/stair_model1.obj
mkdir -p package/stair_model package/vicon-lsl-bridge-gui.app/Contents/Resources/stair_model
cp -- vicon-lsl-bridge/assets/stair_model/stair_model1.obj \
      vicon-lsl-bridge/assets/stair_model/stair_model1.mtl package/stair_model/
cp -- vicon-lsl-bridge/assets/stair_model/stair_model1.obj \
      vicon-lsl-bridge/assets/stair_model/stair_model1.mtl \
      package/vicon-lsl-bridge-gui.app/Contents/Resources/stair_model/

# Package LabRecorder as macOS application bundle
cp -R -- build-labrecorder/LabRecorder.app package/
"$macdeployqt" package/LabRecorder.app -libpath=build-labrecorder/_deps/liblsl-build 2>/dev/null || \
  "$macdeployqt" package/LabRecorder.app
cat << 'EOF' > package/LabRecorder
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -d "$DIR/LabRecorder.app" ]]; then
  exec "$DIR/LabRecorder.app/Contents/MacOS/LabRecorder" "$@"
fi
EOF
chmod +x package/LabRecorder

# Ensure configuration files and resources are placed correctly
mkdir -p package/LabRecorder.app/Contents/Resources
if [[ -f package/LabRecorder.app/Contents/MacOS/LabRecorder.cfg ]]; then
  mv package/LabRecorder.app/Contents/MacOS/LabRecorder.cfg package/LabRecorder.app/Contents/Resources/
fi
if [[ -f labrecorder/LabRecorder.cfg ]]; then
  cp -- labrecorder/LabRecorder.cfg package/LabRecorder.cfg
  if [[ ! -f package/LabRecorder.app/Contents/Resources/LabRecorder.cfg ]]; then
    cp -- labrecorder/LabRecorder.cfg package/LabRecorder.app/Contents/Resources/
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
  codesign --force --deep --sign - package/Frameworks/lsl.framework
fi
if [[ -d package/lsl.framework ]]; then
  codesign --force --deep --sign - package/lsl.framework
fi
find package -maxdepth 1 -type f -name 'liblsl*.dylib' -exec codesign --force --sign - {} +
find package/Frameworks -maxdepth 1 -type f -name 'liblsl*.dylib' -exec codesign --force --sign - {} +
codesign --force --sign - package/vicon-lsl-bridge
codesign --force --sign - package/LabRecorderCLI

# Create tar.gz archive from the flat layout
tar -czf "${artifact_name}.tar.gz" -C package .

# The disk image is arranged for drag installation instead of being a copy of
# the flat layout. Running the app from the mounted image gives it a different
# path on every mount, so macOS cannot recognise it between launches and any
# permission the user grants is asked for again. Dragging both bundles to
# /Applications gives them one stable location, and keeps them siblings, which
# is how the bridge locates the recorder.
dmg_root=dmg-root
rm -rf "$dmg_root"
mkdir -p "$dmg_root"
cp -R -- package/vicon-lsl-bridge-gui.app "$dmg_root/"
cp -R -- package/LabRecorder.app "$dmg_root/"
ln -s /Applications "$dmg_root/Applications"

# Everything that is not an application bundle stays available, but out of the
# way of the drag target. The launcher wrappers are omitted: they expect the
# flat layout of the tar.gz and would not resolve here.
mkdir -p "$dmg_root/Command Line Tools"
for entry in package/*; do
  name="$(basename "$entry")"
  case "$name" in
    vicon-lsl-bridge-gui.app|LabRecorder.app|vicon-lsl-bridge-gui|LabRecorder) continue ;;
  esac
  cp -R -- "$entry" "$dmg_root/Command Line Tools/"
done

test -d "$dmg_root/vicon-lsl-bridge-gui.app"
test -L "$dmg_root/Applications"
hdiutil create -volname "Vicon LSL Bridge" -srcfolder "$dmg_root" -ov -format UDZO "${artifact_name}.dmg"
