# HoloLens Gaze LSL

This folder contains the Unity scripts that send HoloLens 2 gaze to LSL. It also contains the build script for the ARM64 UWP version of `liblsl` used by the Unity app.

The `external/liblsl` folder is a Git submodule: a separate repository pinned to one revision. It points to [`attackofthetitan/liblsl-uwp-arm64`](https://github.com/attackofthetitan/liblsl-uwp-arm64). That repository is based on `sccn/liblsl` release `v1.16.2` and includes the changes needed for UWP on ARM64.

## Build liblsl for UWP ARM64

First, download the linked repository:

```powershell
git submodule update --init --recursive hololens-gaze-lsl/external/liblsl
```

Then run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\hololens-gaze-lsl\build-liblsl-uwp-arm64.ps1 -Config Release
```

The build creates:

```text
hololens-gaze-lsl/build/liblsl-uwp-arm64-install/bin/lsl.dll
hololens-gaze-lsl/build/liblsl-uwp-arm64-install/lib/lsl.lib
```

Both files come from `liblsl` release `v1.16.2`.

## Set up Unity

1. Add `GazeDataProvider` and `GazeLSLOutlet` to a scene object.
2. Create or choose a `GazeLSLConfig` asset.
3. Assign that asset to `GazeLSLOutlet`.
4. Add the Microsoft Extended Eye Tracking SDK.
5. Use Microsoft Mixed Reality OpenXR 1.5.1 or later.

The app asks the user for eye-gaze permission. It starts publishing only when the eye tracker supports exactly 90 Hz.

The tracking work is split between two threads:

- A 90 Hz worker reads gaze from the eye-tracking SDK.
- Unity's main thread converts each reading into the current Unity/OpenXR world at the time when the device captured it.

This keeps the gaze ray in the same stationary world as the optional Vuforia model-target stream.

## How timestamps are handled

Each eye-tracking reading includes `SystemRelativeTime`. On this device path, that value is a count from the Windows high-resolution timer, also called QPC. The app divides the count by `Stopwatch.Frequency` to convert it to seconds.

It does not use the fixed .NET `TimeSpan` rate. It also does not replace capture time with the time when Unity happened to read the sample.

The app drops a reading when its timestamp is:

- Missing or not positive.
- A duplicate of the last reading.
- Earlier than the last reading.
- More than 25 ms old.

The raw and converted queues may hold a normal small batch. If either queue spans more than 25 ms, the app drops the older queued readings and keeps only the newest sample. This creates a time gap instead of sending delayed gaze after the matching Vicon motion.

LSL and LabRecorder still handle clock differences between the HoloLens and the recording computer.

## Published stream

`GazeLSLOutlet` creates the LSL stream directly on the HoloLens. It does not send gaze through the desktop bridge.

If `liblsl.dll` cannot load, or if the LSL stream cannot start, Unity logs an error and gaze publishing stops. There is no fallback relay.

The stream always has 21 values. They describe combined, left-eye, and right-eye origins and directions, plus one valid flag for each ray. HoloLens 2 vergence is not included.

The stream layout stays fixed even when one eye is unavailable. In that case, the values for that eye are marked invalid.

For the full stream layout and timing rules, see [Behavior that must stay the same](../docs/behavior-contract.md) and [How time and coordinates work](../docs/time-and-coordinate-semantics.md).
