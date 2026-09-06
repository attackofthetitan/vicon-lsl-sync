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

- A worker drains gaze from the eye-tracking SDK. It steps at 1.25 times the
  90 Hz nominal rate, because a step sends at most one sample and a queue built
  during a stall can only shrink if steps outpace the tracker.
- Unity's main thread converts each reading into the current Unity/OpenXR world at the time when the device captured it.

This keeps the gaze ray in the same stationary world as the optional Vuforia model-target stream.

## How timestamps are handled

Each eye-tracking reading includes `SystemRelativeTime`. The app treats that value as an opaque monotonic count: it orders readings against each other and locates the device pose at the moment of capture, and it is never turned into a duration. The rate behind it is not the fixed .NET `TimeSpan` rate, and it is not `Stopwatch.Frequency` either -- on this device the same reading read 0.020 s old on the SDK's own clock and 231 s in the future against `Stopwatch`, with the gap widening as the session ran.

So every duration on the gaze path is measured on the LSL clock, which each reading already carries as its capture time. The app does not replace capture time with the time when Unity happened to read the sample.

Each step walks forward from the last accepted capture time, taking up to 32 readings, rather than asking for the reading at the current time. Asking for the reading at "now" returns one reading per call, so a poll that lands late loses every frame in between.

The app drops a reading when its timestamp is:

- Missing or not positive.
- A duplicate of the last reading.
- Earlier than the last reading.

Age is judged only on a reading fetched for the current time, which must be no more than 50 ms old. Once a cursor exists, an old reading means the step is catching up rather than that the tracker stalled.

The SDK on this device cannot report that it has no newer reading: its projection of that empty result throws inside the SDK instead of returning nothing, and leaves an object whose finalizer throws again, which crashes the app within seconds if it happens on every step. So the app asks for a newer reading only when one can exist, which is once a frame period has passed since the last capture *and* the tracker has had time to part with it, and stops draining as soon as the cursor reaches the newest published reading. That second term matters: readings arrive about 20 ms after capture against an 11 ms frame period, so without it every step made one further ask that could not be answered. The delay is measured as the freshest age any reading has been offered at, not assumed. If the SDK still fails that way three times since the drain last resumed, the app suspends draining for ten seconds and reads at the current time meanwhile, which takes at most one reading per step and may not keep up with the tracker. The suspension is temporary on purpose: empty results happen while the tracker has nothing newer to give, which is while it is not publishing, and a tracker that starts publishing later would otherwise spend the rest of the session on a fallback that cannot keep up. The first suspension is logged; the count is in the acquisition counters.

A read that fails inside the SDK never withholds gaze that is already converted and waiting. The queued sample is published, and the failure is reported once the queue is empty.

The raw and converted queues may hold a normal small batch. If either queue spans more than 500 ms, the app drops the older queued readings and keeps only the newest sample. This creates a time gap instead of sending delayed gaze after the matching Vicon motion. Both numbers are seconds on the LSL clock, so the budget stays above one full drained batch, which spans 355 ms at 90 Hz, and the queue does not discard the readings draining just recovered.

LSL and LabRecorder still handle clock differences between the HoloLens and the recording computer.

## Published stream

`GazeLSLOutlet` creates the LSL stream directly on the HoloLens. It does not send gaze through the desktop bridge.

If `liblsl.dll` cannot load, or if the LSL stream cannot start, Unity logs an error and gaze publishing stops. There is no fallback relay.

The stream always has 21 values. They describe combined, left-eye, and right-eye origins and directions, plus one valid flag for each ray. HoloLens 2 vergence is not included.

The stream layout stays fixed even when one eye is unavailable. In that case, the values for that eye are marked invalid.

For the full stream layout and timing rules, see [Behavior that must stay the same](../docs/behavior-contract.md) and [How time and coordinates work](../docs/time-and-coordinate-semantics.md).
