#!/usr/bin/env bash
set -euo pipefail

artifact_name="$1"

# Prints the folders a program searches when it looks for its libraries.
rpaths() {
  otool -l "$1" | awk '/cmd LC_RPATH/ { found = 1; next }
    found && $1 == "path" { sub(/^ *path /, ""); sub(/ \(offset [0-9]+\)$/, ""); print; found = 0 }'
}

add_rpath() {
  if ! rpaths "$1" | grep -xF -- "$2" >/dev/null; then
    install_name_tool -add_rpath "$2" "$1"
  fi
}

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

# The desktop app, with Qt copied inside it.
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

# Ship the stair model, which the preview needs to line up gaze.
test -f vicon-lsl-bridge/assets/stair_model/stair_model1.obj
mkdir -p package/stair_model package/vicon-lsl-bridge-gui.app/Contents/Resources/stair_model
cp -- vicon-lsl-bridge/assets/stair_model/stair_model1.obj \
      vicon-lsl-bridge/assets/stair_model/stair_model1.mtl package/stair_model/
cp -- vicon-lsl-bridge/assets/stair_model/stair_model1.obj \
      vicon-lsl-bridge/assets/stair_model/stair_model1.mtl \
      package/vicon-lsl-bridge-gui.app/Contents/Resources/stair_model/

# LabRecorder, with Qt copied inside it.
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

# Put LabRecorder.cfg where LabRecorder looks for it.
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

# Copy liblsl next to every program that uses it.
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

# Fail if no liblsl was found.
find package -maxdepth 1 -name 'liblsl*.dylib' -print -quit | grep -q .

# Strip the Intel code from programs built for both Intel and Apple Silicon.
while IFS= read -r binary; do
  [[ "$(lipo -archs "$binary" 2>/dev/null)" == *" "* ]] || continue
  lipo -thin arm64 "$binary" -output "$binary.arm64"
  mv "$binary.arm64" "$binary"
done < <(find package -type f \( -perm -u+x -o -name '*.dylib' \))

# Let the command-line programs find libraries next to them.
for bin in package/vicon-lsl-bridge package/LabRecorderCLI; do
  if [[ -f "$bin" ]]; then
    install_name_tool -add_rpath "@executable_path" "$bin" 2>/dev/null || true
    install_name_tool -add_rpath "@loader_path" "$bin" 2>/dev/null || true
    install_name_tool -add_rpath "@executable_path/Frameworks" "$bin" 2>/dev/null || true
    install_name_tool -add_rpath "@loader_path/Frameworks" "$bin" 2>/dev/null || true
  fi
done

# Remove library folders that only exist on the build machine, so every
# computer uses the bundled libraries that were tested.
while IFS= read -r binary; do
  file -b "$binary" | grep -q '^Mach-O' || continue
  while IFS= read -r rpath; do
    if [[ "$rpath" == /* ]]; then
      install_name_tool -delete_rpath "$rpath" "$binary"
    fi
  done < <(rpaths "$binary")
done < <(find package -type f)

# macdeployqt does not tell LabRecorder where its bundled lsl framework is,
# so without this it only starts on the computer that built it.
add_rpath package/LabRecorder.app/Contents/MacOS/LabRecorder "@executable_path/../Frameworks"

# Give LabRecorder the same reasons the bridge shows when macOS asks for
# network and folder access.
recorder_plist=package/LabRecorder.app/Contents/Info.plist
chmod u+w "$recorder_plist"  # The LabRecorder build writes it read-only.
plutil -replace NSLocalNetworkUsageDescription -string \
  "LabRecorder finds and records Lab Streaming Layer streams on your local network." \
  "$recorder_plist"
for key in NSDocumentsFolderUsageDescription NSDesktopFolderUsageDescription \
           NSDownloadsFolderUsageDescription NSRemovableVolumesUsageDescription \
           NSNetworkVolumesUsageDescription; do
  plutil -replace "$key" -string "LabRecorder saves recordings in the folder you choose." \
    "$recorder_plist"
done

# Sign everything with a local signature that needs no Apple developer ID.
for app in package/vicon-lsl-bridge-gui.app package/LabRecorder.app; do
  if [[ -d "$app" ]]; then
    codesign --force --deep --sign - "$app"
  fi
done

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

tar -czf "${artifact_name}.tar.gz" -C package .

# Put both recorders inside the one app on the disk image, so macOS approves
# them with the app and remembers its permissions once it is in /Applications.
dmg_root=dmg-root
rm -rf "$dmg_root"
mkdir -p "$dmg_root"
bridge_app="$dmg_root/vicon-lsl-bridge-gui.app"
helpers="$bridge_app/Contents/Helpers"
cp -R -- package/vicon-lsl-bridge-gui.app "$bridge_app"
mkdir -p "$helpers"
cp -R -- package/LabRecorder.app "$helpers/"
cp -- package/LabRecorderCLI "$helpers/LabRecorder.app/Contents/MacOS/"
add_rpath "$helpers/LabRecorder.app/Contents/MacOS/LabRecorderCLI" "@executable_path/../Frameworks"
codesign --force --deep --sign - "$bridge_app"
ln -s /Applications "$dmg_root/Applications"

# This file holds the version the image installs, so the app can tell the
# image is its own before it ejects it.
plutil -extract CFBundleShortVersionString raw package/vicon-lsl-bridge-gui.app/Contents/Info.plist \
  > "$dmg_root/.vicon-lsl-bridge-installer"

# Put everything except the apps in its own folder, leaving out the launcher
# scripts because they only work in the tar.gz layout.
mkdir -p "$dmg_root/Command Line Tools"
for entry in package/*; do
  name="$(basename "$entry")"
  case "$name" in
    vicon-lsl-bridge-gui.app|LabRecorder.app|vicon-lsl-bridge-gui|LabRecorder) continue ;;
  esac
  cp -R -- "$entry" "$dmg_root/Command Line Tools/"
done

test -x "$helpers/LabRecorder.app/Contents/MacOS/LabRecorder"
test -x "$helpers/LabRecorder.app/Contents/MacOS/LabRecorderCLI"
test -s "$dmg_root/.vicon-lsl-bridge-installer"
test -L "$dmg_root/Applications"
hdiutil create -volname "Vicon LSL Bridge" -srcfolder "$dmg_root" -ov -format UDZO -imagekey zlib-level=1 "${artifact_name}.dmg"
