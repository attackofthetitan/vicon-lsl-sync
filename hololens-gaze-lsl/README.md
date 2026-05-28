# HoloLens Gaze LSL

This project contains the HoloLens gaze Unity scripts and the local `liblsl` UWP ARM64 build. The `external/liblsl` submodule points to [`attackofthetitan/liblsl-uwp-arm64`](https://github.com/attackofthetitan/liblsl-uwp-arm64), a public patched source repository based on `sccn/liblsl` `v1.16.2`.

## liblsl UWP ARM64

Initialize the submodule:

```powershell
git submodule update --init --recursive hololens-gaze-lsl/external/liblsl
```

Build:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\hololens-gaze-lsl\build-liblsl-uwp-arm64.ps1 -Config Release
```

Artifacts:

```text
build/liblsl-uwp-arm64-install/bin/lsl.dll
build/liblsl-uwp-arm64-install/lib/lsl.lib
```

The DLL is a UWP ARM64 build of `liblsl` `v1.16.2`.

## Unity Setup

Attach `GazeDataProvider` and `GazeLSLOutlet` to a scene object, then assign a `GazeLSLConfig` asset.

`GazeLSLOutlet` creates a native LSL outlet first. If `liblsl.dll` cannot be loaded, the outlet fails to initialize, or `ForceUdpRelay` is enabled, it falls back to sending `HLGAZE1` UDP packets to `vicon-lsl-bridge`.

The HoloLens gaze stream has 21 channels: combined, left-eye, and right-eye origin/direction plus valid flags. HoloLens 2 vergence data is not published.
