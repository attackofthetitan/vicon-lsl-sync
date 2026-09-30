#!/usr/bin/env bash
# Packs the Linux build so it runs without the build folders or a Qt install:
# every program finds its libraries inside the package through $ORIGIN paths.
set -euo pipefail

artifact_name="$1"
bridge_build="${BRIDGE_BUILD_DIR:-vicon-lsl-bridge/build}"
recorder_build="${RECORDER_BUILD_DIR:-build-labrecorder}"

fail() {
  echo "$*" >&2
  exit 1
}

[[ ! -e package ]] || fail "Refusing to reuse an existing package directory"
command -v patchelf >/dev/null || fail "patchelf is required to package for Linux"
for file in "$bridge_build/vicon-lsl-bridge" "$bridge_build/vicon-lsl-bridge-gui" \
            "$recorder_build/LabRecorder" "$recorder_build/LabRecorderCLI" \
            labrecorder/LabRecorder.cfg vicon-lsl-bridge/assets/stair_model/stair_model1.obj; do
  [[ -f "$file" ]] || fail "Missing file to package: $file"
done

# The libraries the package carries; the rest come with the desktop.
bundled='^(liblsl|libQt6|libicu|libboost_)'

# Prints "name path" for each library a program loads, as the build machine
# finds it. A plugin may also need desktop libraries this machine lacks.
libraries() {
  local listing
  local missing
  listing="$(ldd "$1")"
  missing="$(awk '$2 == "=>" && $3 == "not" { print $1 }' <<< "$listing")"
  if [[ "$1" == */plugins/* ]]; then
    missing="$(grep -E "$bundled" <<< "$missing" || true)"
  fi
  [[ -z "$missing" ]] || fail "$1 needs libraries the build machine cannot find: $missing"
  awk '$2 == "=>" && $3 ~ /^\// { print $1, $3 }' <<< "$listing"
}

# Copies what a desktop computer may not have: Qt, the ICU it uses, Boost, and
# liblsl, which goes beside its program because the bridge and LabRecorder each
# build their own. The release uses Qt's own build, which carries its other
# third-party code inside its libraries.
bundle_libraries() {
  local program="$1"
  local lsl_dir="$2"
  local listing
  local name
  local path
  listing="$(libraries "$program")"
  while read -r name path; do
    case "$name" in
      liblsl.so*) cp -L -- "$path" "$lsl_dir/$name" ;;
      libQt6*|libicu*|libboost_*) [[ -e "package/lib/$name" ]] || cp -L -- "$path" "package/lib/$name" ;;
    esac
  done <<< "$listing"
}

# Qt's plugins must come from the Qt the programs link, so look next to it first.
qt_core="$(libraries "$bridge_build/vicon-lsl-bridge-gui" | awk '$1 == "libQt6Core.so.6" { print $2 }')"
[[ -n "$qt_core" ]] || fail "vicon-lsl-bridge-gui does not link libQt6Core.so.6"
qt_lib_dir="$(dirname "$(readlink -f "$qt_core")")"
qt_plugins=""
for candidate in "$qt_lib_dir/../plugins" "$qt_lib_dir/qt6/plugins" "${QT_ROOT_DIR:+$QT_ROOT_DIR/plugins}"; do
  if [[ -n "$candidate" && -f "$candidate/platforms/libqxcb.so" ]]; then
    qt_plugins="$(cd "$candidate" && pwd)"
    break
  fi
done
[[ -n "$qt_plugins" ]] || fail "Qt plugins were not found beside $qt_lib_dir; set QT_ROOT_DIR"

mkdir -p package/lib package/plugins/platforms package/labrecorder/lib package/stair_model

cp -- "$bridge_build/vicon-lsl-bridge" "$bridge_build/vicon-lsl-bridge-gui" package/
bundle_libraries "$bridge_build/vicon-lsl-bridge" package/lib
bundle_libraries "$bridge_build/vicon-lsl-bridge-gui" package/lib

cp -- "$recorder_build/LabRecorder" "$recorder_build/LabRecorderCLI" labrecorder/LabRecorder.cfg \
      package/labrecorder/
bundle_libraries "$recorder_build/LabRecorder" package/labrecorder/lib
bundle_libraries "$recorder_build/LabRecorderCLI" package/labrecorder/lib

[[ -f package/lib/liblsl.so.2 && -f package/labrecorder/lib/liblsl.so.2 ]] || \
  fail "liblsl was not found for both the bridge and LabRecorder"

# The window needs the X11 plugin and its OpenGL helpers, the checks and
# computers without a screen need the offscreen one, and typing uses the input
# method plugins.
shopt -s nullglob
plugins=("$qt_plugins/platforms/libqxcb.so" "$qt_plugins/platforms/libqoffscreen.so"
         "$qt_plugins/xcbglintegrations"/*.so "$qt_plugins/platforminputcontexts"/*.so)
shopt -u nullglob
for plugin in "${plugins[@]}"; do
  group="$(basename "$(dirname "$plugin")")"
  mkdir -p "package/plugins/$group"
  cp -- "$plugin" "package/plugins/$group/"
  bundle_libraries "$plugin" package/lib
done

# The preview draws this stair model, found next to the program as on Windows.
cp -- vicon-lsl-bridge/assets/stair_model/stair_model1.obj \
      vicon-lsl-bridge/assets/stair_model/stair_model1.mtl package/stair_model/

# Point every program and library at the package instead of the build folders.
patchelf --set-rpath '$ORIGIN/lib' package/vicon-lsl-bridge package/vicon-lsl-bridge-gui
patchelf --set-rpath '$ORIGIN/lib:$ORIGIN/../lib' package/labrecorder/LabRecorder
patchelf --set-rpath '$ORIGIN/lib' package/labrecorder/LabRecorderCLI
find package/lib package/labrecorder/lib -type f -name '*.so*' -exec patchelf --set-rpath '$ORIGIN' {} +
find package/plugins -type f -name '*.so' -exec patchelf --set-rpath '$ORIGIN/../../lib' {} +

# Qt otherwise looks for plugins where it was installed on the build machine.
printf '[Paths]\nPrefix = .\nPlugins = plugins\n' > package/qt.conf
printf '[Paths]\nPrefix = ..\nPlugins = plugins\n' > package/labrecorder/qt.conf

tar -czf "${artifact_name}.tar.gz" -C package .
