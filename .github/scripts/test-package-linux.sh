#!/usr/bin/env bash
# Checks the Linux archive by itself: its programs must start with only the
# package, not the build folders, the Qt install, or library paths set around it.
set -euo pipefail

artifact_name="$1"
archive="${artifact_name}.tar.gz"
temp_dir="$(mktemp -d)"
root="$temp_dir/package"
trap 'rm -rf "$temp_dir"' EXIT

fail() {
  echo "$*" >&2
  exit 1
}

clean_env=(env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH)
# The libraries the package carries; the rest come with the desktop.
bundled='^(liblsl|libQt6|libicu|libboost_)'

is_elf() {
  [[ "$(head -c 4 "$1")" == $'\x7fELF' ]]
}

[[ -f "$archive" ]] || fail "Missing $archive"
mkdir -p "$root"
root="$(cd "$root" && pwd -P)"
tar -xzf "$archive" -C "$root"

for program in vicon-lsl-bridge vicon-lsl-bridge-gui labrecorder/LabRecorder labrecorder/LabRecorderCLI; do
  [[ -x "$root/$program" ]] || fail "Missing program: $program"
done
for file in qt.conf labrecorder/qt.conf labrecorder/LabRecorder.cfg stair_model/stair_model1.obj \
            lib/liblsl.so.2 labrecorder/lib/liblsl.so.2 \
            plugins/platforms/libqxcb.so plugins/platforms/libqoffscreen.so; do
  [[ -f "$root/$file" ]] || fail "Missing file: $file"
done

# Every program, library and plugin must search only inside the package, and
# find Qt, ICU, Boost and liblsl there, LabRecorder using its own liblsl. A
# plugin may also need desktop libraries this machine lacks, such as X11 ones.
elf_count=0
while IFS= read -r -d '' file; do
  is_elf "$file" || continue
  elf_count=$((elf_count + 1))
  runpath="$(patchelf --print-rpath "$file")"
  IFS=: read -r -a entries <<< "$runpath"
  for entry in ${entries[@]+"${entries[@]}"}; do
    [[ "$entry" == '$ORIGIN'* ]] || fail "$file searches '$entry', outside the package"
  done
  listing="$("${clean_env[@]}" ldd "$file")"
  missing="$(awk '$2 == "=>" && $3 == "not" { print $1 }' <<< "$listing")"
  if [[ "$file" == "$root/plugins/"* ]]; then
    missing="$(grep -E "$bundled" <<< "$missing" || true)"
  fi
  [[ -z "$missing" ]] || fail "$file cannot find: $missing"
  while read -r name path; do
    case "$name" in
      liblsl.so*|libQt6*|libicu*|libboost_*)
        resolved="$(readlink -f "$path")"
        [[ "$resolved" == "$root"/* ]] || fail "$file loads $name from $path, outside the package"
        if [[ "$name" == liblsl.so* && "$file" == "$root/labrecorder/"* ]]; then
          [[ "$resolved" == "$root/labrecorder/lib/"* ]] || fail "$file loads the bridge's liblsl"
        fi
        ;;
    esac
  done < <(awk '$2 == "=>" { print $1, $3 }' <<< "$listing")
done < <(find "$root" -type f -print0)
(( elf_count > 0 )) || fail "No programs or libraries found in $archive"

"${clean_env[@]}" "$root/vicon-lsl-bridge" --help > /dev/null || fail "vicon-lsl-bridge --help failed"

output="$("${clean_env[@]}" QT_QPA_PLATFORM=offscreen QT_DEBUG_PLUGINS=1 \
  "$root/vicon-lsl-bridge-gui" --test 2>&1)" || fail "vicon-lsl-bridge-gui --test failed: $output"
grep -qF "$root/plugins/platforms/libqoffscreen.so" <<< "$output" || \
  fail "vicon-lsl-bridge-gui did not load Qt's platform plugin from the package"

# LabRecorderCLI prints its usage without arguments; only a loader error fails.
set +e
output="$("${clean_env[@]}" "$root/labrecorder/LabRecorderCLI" 2>&1)"
status=$?
set -e
if (( status == 127 )) || grep -q 'error while loading shared libraries' <<< "$output"; then
  fail "LabRecorderCLI does not start: $output"
fi

# LabRecorder keeps running until it is closed, so still running after a few
# seconds means it started.
set +e
output="$("${clean_env[@]}" QT_QPA_PLATFORM=offscreen timeout 5 "$root/labrecorder/LabRecorder" 2>&1)"
status=$?
set -e
(( status == 124 )) || fail "LabRecorder did not keep running (exit $status): $output"

echo "Linux package checks passed"
