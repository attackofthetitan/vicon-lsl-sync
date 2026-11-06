# HoloLens Gaze LSL

The Unity scripts that send HoloLens 2 gaze to LSL, and the script that builds the
HoloLens version of `liblsl` (ARM64 UWP) that the Unity app needs.

`external/liblsl` is a Git submodule. It points to
[`attackofthetitan/liblsl-uwp-arm64`](https://github.com/attackofthetitan/liblsl-uwp-arm64),
which is `sccn/liblsl` `v1.16.2` with the changes it needs to run on HoloLens.

## Build liblsl for HoloLens

```powershell
git submodule update --init --recursive hololens-gaze-lsl/external/liblsl
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\hololens-gaze-lsl\build-liblsl-uwp-arm64.ps1 -Config Release
```

This makes:

```text
hololens-gaze-lsl/build/liblsl-uwp-arm64-install/bin/lsl.dll
hololens-gaze-lsl/build/liblsl-uwp-arm64-install/lib/lsl.lib
```

## Set up Unity

1. Add `GazeDataProvider` and `GazeLSLOutlet` to an object in the scene.
2. Make or pick a `GazeLSLConfig` asset and give it to `GazeLSLOutlet`.
3. Add the Microsoft Extended Eye Tracking SDK.
4. Use Microsoft Mixed Reality OpenXR 1.5.1 or later.

The app asks for permission to track the user's eyes, and only starts sending
once the tracker runs at exactly 90 Hz.

The work runs on two threads:

- A background worker gets readings from the eye tracking SDK. It runs 1.25 times
  as often as the tracker, because each step sends at most one sample, and a
  backlog can only clear if steps run faster than readings arrive.
- Unity's main thread moves each reading into the Unity world, using where the
  headset was when the reading was taken.

That keeps gaze in the same world as the optional Vuforia stair target stream.

## The stream

`GazeLSLOutlet` opens the stream on the HoloLens itself. Gaze doesn't go through
the desktop app. If `liblsl.dll` can't load or the stream can't start, Unity logs
an error and gaze stops. There's no backup route.

The stream always has 21 values: the start point and direction for both eyes
combined, the left eye, and the right eye, each with a valid flag. It doesn't
include where the two lines of sight meet. If one eye isn't available, its values
are just marked invalid.

## Timestamps

Each sample carries the time the reading was really taken, on the LSL clock, never
the time Unity happened to read it.

- Each step picks up every reading since the last one, up to 32, so a late step
  loses nothing.
- Readings with a missing, repeated, or earlier time are dropped.
- If either queue holds more than 500 ms, the older readings are dropped and the
  newest kept. You get a gap instead of gaze arriving late.
- The headset's own `SystemRelativeTime` is only used to put readings in order,
  because its tick speed is unknown.
- This headset's SDK throws an error when there's no newer reading, so the app
  only asks once a newer one can exist.

LSL and LabRecorder handle the clock difference between the HoloLens and the
recording computer. The full rules, and the headset evidence behind them, are in
[How time and coordinates work](../docs/time-and-coordinate-semantics.md). The
stream layout is in [Behavior that must stay the same](../docs/behavior-contract.md).

## Record with Vuforia paused

Keep `VuforiaModelTargetPoseOutlet` on and record `HoloLensModelTargetPose` along
with gaze. While it can see the stairs, it builds a steady reference from 20
positions that stay within 2 cm and 3 degrees. Once the stairs are found, press
**M** to pause Vuforia. It keeps sending that reference with `Tracked = 2`, even
for recordings started after the pause, so playback can still line up gaze.

`Tracked = 1` means live, and `0` means invalid (seven `NaN`). Normal tracking
loss always sends invalid. Pausing before there's a steady reference leaves the
samples invalid and logs a warning. Turning Vuforia back on, or turning the
component off, clears the reference.

Don't move the stairs or reset the Unity world while using a frozen reference. If
you do, find the stairs again. Frozen samples aren't new measurements, and the
fact that they never change doesn't mean the calibration is perfect.
