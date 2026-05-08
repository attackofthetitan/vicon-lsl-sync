# liblsl UWP ARM64

`external/liblsl` is pinned to `v1.16.2` and patched for UWP ARM64.

Build from the repository root:

```powershell
git submodule update --init --recursive hololens-gaze-lsl/external/liblsl
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\hololens-gaze-lsl\build-liblsl-uwp-arm64.ps1 -Config Release
```

Artifacts:

```text
build/liblsl-uwp-arm64-install/bin/lsl.dll
build/liblsl-uwp-arm64-install/lib/lsl.lib
```