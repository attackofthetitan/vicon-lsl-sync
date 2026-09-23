# HoloLens Gaze LSL

This folder has the Unity scripts that send HoloLens 2 gaze to LSL. It also has the script that builds the HoloLens version of `liblsl` (ARM64 UWP) that the Unity app needs.

The `external/liblsl` folder is a Git submodule: a copy of another repository, pinned to one version. It points to [`attackofthetitan/liblsl-uwp-arm64`](https://github.com/attackofthetitan/liblsl-uwp-arm64), which is `sccn/liblsl` release `v1.16.2` with the changes needed to run on HoloLens.

## Build liblsl for HoloLens

First, download the submodule:

```powershell
git submodule update --init --recursive hololens-gaze-lsl/external/liblsl
```

Then run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\hololens-gaze-lsl\build-liblsl-uwp-arm64.ps1 -Config Release
```

This makes:

```text
hololens-gaze-lsl/build/liblsl-uwp-arm64-install/bin/lsl.dll
hololens-gaze-lsl/build/liblsl-uwp-arm64-install/lib/lsl.lib
```

Both come from `liblsl` release `v1.16.2`.

## Set up Unity

1. Add `GazeDataProvider` and `GazeLSLOutlet` to an object in the scene.
2. Make or pick a `GazeLSLConfig` asset.
3. Give that asset to `GazeLSLOutlet`.
4. Add the Microsoft Extended Eye Tracking SDK.
5. Use Microsoft Mixed Reality OpenXR 1.5.1 or later.

The app asks the user for permission to track their eyes. It only starts sending once the eye tracker says it can run at exactly 90 Hz.

The work is split across two threads:

- A background worker collects gaze readings from the eye-tracking SDK. It runs
  1.25 times as often as the tracker's 90 Hz, because each step sends at most one
  sample, and a backlog built up during a pause can only shrink if steps run
  faster than the tracker.
- Unity's main thread moves each reading into the Unity world, using where the headset was at the moment the reading was taken.

This keeps gaze in the same fixed world as the optional Vuforia stair target stream.

## Record with Vuforia paused

Keep `VuforiaModelTargetPoseOutlet` turned on and record `HoloLensModelTargetPose`
along with gaze. While it can see the stairs, the outlet builds a steady
reference from 20 positions that stay within 2 cm and 3 degrees of each other.
Once the stairs have been found, press **M** to pause Vuforia. The outlet keeps
sending that reference with `Tracked = 2`, even for recordings started after the
pause. Playback can then line up gaze using the reference saved in the XDF.

`Tracked = 1` means a live position, and `0` means invalid (seven NaNs). Normal
tracking loss still sends invalid positions; only turning Vuforia off on purpose
sends the frozen reference. If there is no steady reference yet, pausing leaves
the samples invalid and logs a warning. Turn tracking back on to get one. Turning
Vuforia back on, or turning the outlet off, clears the old reference.

Do not move the stairs or reset the Unity world while using a frozen reference.
If you do, find the stairs again. Frozen samples carry the current time but are
not new measurements, and the fact that they never change does not mean the
calibration is perfect.

## How timestamps work

Each eye-tracking reading has a `SystemRelativeTime`. The app only uses it to put readings in order and to look up where the headset was when the reading was taken. It never turns it into seconds, because we do not know how fast it ticks. It is not the standard .NET `TimeSpan` speed, and it is not `Stopwatch.Frequency` either: on this headset, the same reading was 0.020 s old by the SDK's own clock and 231 s in the future by `Stopwatch`, and the gap grew as the session went on.

So every length of time on the gaze path is measured on the LSL clock, which each reading already carries as its capture time. The app never replaces the capture time with the time Unity happened to read the sample.

Instead of asking for the reading at the current time, each step works forward from the last capture time it accepted, taking up to 32 readings. Asking for "now" only gives one reading, so if the ask comes late, every reading in between is lost.

The app drops a reading when its time is:

- Missing or not positive.
- The same as the last reading.
- Earlier than the last reading.

Only a reading fetched for the current time is judged on age, and it must be no more than 50 ms old. Once there is a last capture time, an old reading just means the step is catching up, not that the tracker has stalled.

On this headset, the SDK cannot say "there is nothing newer" cleanly. It throws an error inside the SDK instead of returning nothing, and leaves behind an object that throws again later. If that happens on every step, the app crashes within seconds. So the app only asks for a newer reading when one can exist: once one frame has passed since the last capture, *and* the tracker has had time to hand it over. It stops as soon as it has the newest reading.

The second part matters. Readings arrive about 20 ms after they are taken, while one frame is about 11 ms, so without it every step would make one more ask that could not be answered. The delay is measured, not guessed: it is the youngest age any reading has been offered at.

If the SDK still fails this way three times since catching up last resumed, the app stops catching up for ten seconds and asks for the reading at the current time instead. That takes at most one reading per step and may fall behind the tracker. The pause is short on purpose. The SDK only says "nothing newer" while the tracker has nothing new to give, which is when it is not making readings. A tracker that starts later would otherwise be stuck on the slower method for the rest of the session. The first pause is logged, and the total is in the reading counters.

A read that fails inside the SDK never holds back gaze that is already converted and waiting. The waiting sample is sent, and the failure is only reported once the queue is empty.

The raw and converted queues can hold a normal small batch. If either queue covers more than 500 ms, the app drops the older readings and keeps only the newest. This leaves a gap in time instead of sending gaze late, after the matching Vicon movement. Both numbers are seconds on the LSL clock, so the limit stays above one full batch (355 ms at 90 Hz) and the queue does not throw away readings the app just caught up on.

LSL and LabRecorder still handle the clock difference between the HoloLens and the recording computer.

## The stream

`GazeLSLOutlet` opens the LSL stream on the HoloLens itself. Gaze does not go through the desktop bridge.

If `liblsl.dll` cannot load, or the LSL stream cannot start, Unity logs an error and gaze stops being sent. There is no backup route.

The stream always has 21 values: the start point and direction for both eyes combined, the left eye, and the right eye, plus one valid flag for each. HoloLens 2 vergence (where the two eyes meet) is not included.

The layout stays the same even when one eye is not available. That eye's values are just marked invalid.

For the full stream layout and timing rules, see [Behavior that must stay the same](../docs/behavior-contract.md) and [How time and coordinates work](../docs/time-and-coordinate-semantics.md).
