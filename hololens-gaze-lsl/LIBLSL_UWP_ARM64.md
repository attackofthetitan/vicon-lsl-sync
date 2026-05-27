# liblsl UWP ARM64

`external/liblsl` points to the public patched source repository
[`attackofthetitan/liblsl-uwp-arm64`](https://github.com/attackofthetitan/liblsl-uwp-arm64),
based on `sccn/liblsl` `v1.16.2`.

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
