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

Attach `GazeDataProvider` and `GazeLSLOutlet` to a scene object, then assign a `GazeLSLConfig` asset to the outlet.

The Unity project must include the Microsoft Extended Eye Tracking SDK and Mixed Reality OpenXR 1.5.1 or later. `GazeDataProvider` requests eye-gaze access, opens `EyeGazeTracker`, and starts only when the tracker exposes an exact 90 Hz mode. The 90 Hz LSL worker acquires raw SDK readings, while Unity's main thread locates the corresponding dynamic `SpatialGraphNode` at each reading's `SystemRelativeTime` and transforms the combined, left, and right rays into the stationary Unity/OpenXR scene frame.

Each distinct SDK reading is timestamped with `LSL.local_clock()` as soon as it is accepted. No QPC conversion or custom clock mapping is used. Normal LSL inlet clock correction and XDF clock-offset chunks therefore synchronize the stream with LSL streams from other hosts.

`GazeLSLOutlet` creates a native LSL outlet. If `liblsl.dll` cannot be loaded or the outlet fails to initialize, gaze publishing fails loudly in Unity logs instead of falling back to a bridge relay.

The HoloLens gaze stream has 21 channels: combined, left-eye, and right-eye origin/direction plus valid flags. HoloLens 2 vergence data is not published.
