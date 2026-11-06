# Hardware test guide

The automated tests don't run Unity, Vicon, Extended Eye Tracking, OpenXR,
Vuforia, the HoloLens build of liblsl, or real screens. Use this guide to compare
a changed build with a known-good one on real equipment.

It shows a tidy-up didn't change anything. It doesn't sign off a dependency
update, stream change, coordinate change, or new timing rule. Those need their own
plan and new expected results.

## When to run it

Run the parts that apply when you change:

- The gaze scripts (`GazeDataProvider`, `GazePublisherWorker`, `GazeLSLOutlet`,
  `GazeTiming`, `GazeCoordinateTransform`) or the target scripts
  (`VuforiaModelTargetPoseOutlet`, `ModelTargetPoseEncoder`).
- A saved field on a Unity component.
- The HoloLens liblsl, the C# LSL wrapper, Unity, OpenXR, the eye tracking SDK, or
  Vuforia.
- Any HoloLens stream name, source ID, layout, rate, clock detail, or coordinate
  name.
- How the preview finds streams, corrects clocks, shows rates, or lines up gaze.
- How recording starts, picks streams, or recovers streams.

For a desktop-only change, skip the headset parts and write down why.

## What you need

- A HoloLens 2 with working Extended Eye Tracking and permission granted.
- The real Unity project. This repo doesn't have the full scene.
- Mixed Reality OpenXR 1.5.1 or later, the eye tracking SDK, and a Vuforia Model
  Target for the stairs.
- One `GazeLSLConfig` shared by both HoloLens outputs.
- A desktop on the same network, a Vicon server with tracked objects, and the
  desktop app and LabRecorder from the build you're testing.
- The real stairs in their measured place.
- Somewhere to keep Unity logs, desktop logs, XDF files, full stream
  descriptions, and screenshots.

## Write down the setup

For each run, record: the repo version and known-good version, the date and time
zone, who ran it, the HoloLens OS build, and the versions of Unity, OpenXR, the
eye tracking SDK, Vuforia, both liblsl builds, LabRecorder, and Vicon. Also note
the stream names and source IDs, the gaze delivery delay and counters from the
headset log, where the stairs are, and where the files are saved.

Use the same equipment for both builds. If anything differs, write it down and
rule it out before blaming the code.

## First, the automated tests

From the top of the repo:

```powershell
python tools/generate_stream_contracts.py --check

cmake -S vicon-lsl-bridge -B build-logic `
  -DVICON_LSL_BRIDGE_BUILD_RUNTIME=OFF `
  -DVICON_LSL_BRIDGE_BUILD_GUI=OFF `
  -DVICON_LSL_BRIDGE_FETCH_CATCH2=OFF `
  -DBUILD_TESTING=ON
cmake --build build-logic --config Release --target vicon-lsl-bridge-logic-tests
ctest --test-dir build-logic --build-config Release --output-on-failure

dotnet run --project hololens-gaze-lsl/Tests/HoloLensCore.Tests.csproj --configuration Release
```

With the full desktop setup (the Vicon SDK submodule, liblsl, and Qt 6), build
everything and run all the CTest tests too.

## Before using the headset

1. The Unity scene has one `GazeDataProvider` and one `GazeLSLOutlet` using the
   right config. For alignment, one `VuforiaModelTargetPoseOutlet` uses the same
   config and the right model target.
2. Gaze and target use the same Unity world. If not, don't test alignment.
3. Gaze permission is granted and the tracker offers exactly 90 Hz.
4. The desktop and HoloLens can see each other's streams through the network.
5. The desktop app has the right Vicon server and stream names, and the recorder
   mode works.
6. **Find LSL Streams** shows the right source ID, computer, channels, rates,
   coordinate name, and sample age for each role.
7. **Recording Destination** shows the right `.xdf` path.
8. Save the session setup and a screenshot of the Unity Inspector wiring.

## Test 1: permission and startup

Start the Unity app from fully closed, wait until it's done looking for the
tracker, find the gaze stream from the desktop, then close and do it again.

Expected:

- Denied permission or an unsupported headset logs an error, and no half-made
  stream appears.
- A tracker without exactly 90 Hz logs an error, and no stream appears.
- A good tracker picks 90 Hz, makes a position anchor, and logs that it's ready.
  The stream only appears after that.
- Only one gaze stream uses the configured identity, and none comes through the
  desktop.

Save the Unity log, the stream list, a full stream description, and proof that
no old stream is left after closing.

## Test 2: stream details

Save the full LSL description of every stream and compare it with [Behavior that
must stay the same](behavior-contract.md):

- Gaze: name, type, and source ID from the config; `double64`; rate 90; exactly 21
  values with the labels and units from `stream-contracts/hololens-gaze.json`;
  coordinate name `hololens_stationary_shared_with_gaze`; and the time details.
- Target: name, type, and source ID from the config; `double64`; irregular rate;
  eight values; the same coordinate name as gaze.
- Vicon: names and `MoCap` type; values in Vicon's order; units, rate, source IDs,
  and time details. Source IDs keep the same computer name after reconnecting.

Keep the raw description files, not just screenshots. The desktop stream list
must agree with them. Write down any missing-details warning.

## Test 3: steady gaze timing

1. Start gaze in a simple scene.
2. Start the preview and wait at least ten seconds.
3. Record at least 60 seconds while looking around naturally.

Expected:

- The preview shows a rate once its two-second window fills, and the low-rate
  result matches the known-good build. The warning starts below 72 Hz.
- Gaze times are finite, positive, and always go up, and follow the SDK's reading
  time, not Unity's frame time.
- Normal small batches don't replay old gaze behind current Vicon movement.
- The XDF has the normal clock correction records.

Work out and save: sample count and length; the smallest, middle, 95th
percentile, and largest gap; the rate overall and in two-second windows; repeated
or earlier times (should be zero); gaps over 500 ms; and the share of expected
90 Hz readings captured, which should be close to 100%. Also save the delivery
delay, and the catch-up's SDK failure count next to its reading count. If those
two are close, the app is asking once per step for a reading that can't exist
yet.

Don't make up a new allowed drop rate. Compare with the known-good build.

## Test 4: overload

Apply a load you can repeat and undo, which slows Unity's main thread enough to
build up more than 500 ms of data. Record before, during, and after.

Expected: old items are dropped and the newest kept, leaving a clear gap; gaze
goes back to following current movement with no burst of old data; times keep
going up; there are always 21 values.

Save the exact load, a timestamp table or plot, and the same test on both builds.

## Test 5: tracker errors

- A short gaze read error: warnings go up, sending carries on or has a short gap,
  and the tracker isn't replaced.
- About one second of errors: the worker reports it, the stream only closes after
  the worker exits, the tracker is found again, and sending restarts once.
- Tracker removal, or an app suspend you can repeat: old queues are cleared, and
  nothing from the old session shows up in the new one.

In each case the reopened stream keeps its identity, time never goes backward,
LabRecorder handles it like the known-good build, and the logs show no unhandled
errors.

## Test 6: good and bad rays and targets

Gaze, with both eyes and each eye alone if possible:

- A usable ray has a finite origin and a direction of length about one.
- A valid flag is `1.0` only when the ray can be used.
- An unusable eye keeps its place and is marked invalid.
- A failed headset lookup gives invalid rays, never a new time.

Target, found and then lost:

- `TRACKED` and `EXTENDED_TRACKED` send a flipped position and `Tracked = 1`.
- Anything else sends seven `NaN` values and `Tracked = 0`.
- Losing the target clears the live alignment samples.

## Test 7: stair alignment

Put the target where it was in the known-good run, restart the HoloLens app for a
fresh world, and check both streams use `hololens_stationary_shared_with_gaze`.

1. Start Vicon and the preview. Find the target and keep it still.
2. Fill in the setup ID, stair position, coordinate names, and notes, then select
   **Calibrate from Stair Target** and hold still for 20 samples.
3. Read the error values. Compare the gaze ray with known stair edges.
4. **Save Session Calibration**, export it, apply it, clear it, and apply it
   again.
5. **Copy** it, **Hide** the copy, and import the export into fresh settings.

Expected:

- Losing the target, moving more than 20 mm, or turning more than 3 degrees starts
  collecting again.
- 20 steady samples give one alignment for this session only, not saved until you
  save it.
- Apply and clear work straight away and can be undone. Copy, hide, export, and
  import keep the measured values.
- Nothing is mirrored, turned round, or off by a metre-millimetre mix-up, compared
  with the known-good build.
- Aligning again after restarting the HoloLens world brings the match back.

### With Vuforia paused

1. With the stairs and world unchanged, calibrate, then press **M** to pause
   Vuforia, leaving the target output on.
2. Start a new recording. Target samples hold a fixed position with `Tracked = 2`.
3. Restart only the desktop app and open that XDF without a saved calibration.
   Gaze lines up with Vicon, and the summary says it used a frozen reference.
4. Do several separate recordings while paused. Each opens on its own, and gaze
   timing doesn't change.
5. Turn Vuforia back on, find the target, and pause again: the new reference is
   used. Restarting the HoloLens app or turning off the target output throws the
   old one away. Pausing with no reference gives `Tracked = 0` and a playback
   warning.
6. Look away with Vuforia on: the output goes invalid, not frozen or live.

### From a recording

Record gaze, target, markers, and segments with the target still. Open the XDF in
the preview. It finds a steady stretch, lines up gaze, and says so. Live and
recorded shapes match at matching times.

Save a photo of where the target is, before and after screenshots, the error
values, the exported calibration, the XDF and its summary, and a side-by-side of
both builds.

## Test 8: old or missing coordinate names

Use saved example files. Don't relabel a new file as old.

- `eye_tracker_space` gaze is shown, but never lined up, and the summary says so.
- An empty coordinate name allows alignment if everything else passes, but live
  calibration and saving ask you to confirm.
- Two different names block alignment, and the live status shows it.

## Test 9: Vicon reconnect and layout change

Record while you break and restore the Vicon connection. In another run, add,
remove, or reorder a subject.

Expected: both streams close and reopen with the same source IDs; times keep going
up; a layout change is caught within 100 frames and reopens both streams in
Vicon's new order; empty layouts open no stream; and LabRecorder handles it like
the known-good build.

Save the bridge log, stream descriptions before and after, and the XDF layouts.

## Test 10: recording controls and closing

1. With a LabRecorder window already running, start the packaged app. It
   doesn't start a second one and shows **External**. Then try with nothing
   running: the one it starts shows **Started here**.
2. Start the Vicon and HoloLens streams. **Find LSL Streams** and link each role
   by source ID.
3. Start a harmless second stream with the same name but another source ID. The
   role shows as unclear until you pick one or turn on **Follow by name**.
4. Restart a stream with the same source ID. The newest copy is picked, and the
   app says so.
5. Fill in the file name fields, try **Find Next Run**, and check the path shown
   is exactly what the recorder gets.
6. In **Record every visible stream** mode, check and start. Streams that appeared
   after LabRecorder started are included. Stop and wait for the file check.
7. Repeat in exact-stream mode. The recorder only gets the streams you picked.
8. Try a setup check with only warnings, a blocked one, recorder-only mode, and
   **Record Anyway** with a reason.
9. Close the app before Start, during each Start command, while recording, during
   Stop, and after the recorder disconnects.

Expected:

- Start sends `update`, `select all`, `filename`, `start` in that order.
- Double clicks send one command. Closing cancels a Start that wasn't sent, or
  sends one final Stop after one that might have been.
- Closing stays responsive and shows what it's waiting for. Only a recorder this
  app started is ever closed.
- The file check reports **Checked**, **Checked with warnings**, or **Needs
  attention**, and never changes the file.

Save the remote command log, stream lists from both modes, the final path and file
check, setup check results, and dashboard screenshots while starting, recording,
stopping, and checking.

## Stop and ask when

Don't settle these by quietly changing code or expected results:

- The real Unity scene and who owns its assets. This repo doesn't have them.
- The stair position, if anyone disputes it. It's in [How time and coordinates
  work](time-and-coordinate-semantics.md#the-stairs).
- An allowed drop rate for gaze. The repo only warns below 80% of the rate.
- How fast the headset ticks. The code no longer depends on it, but nobody has
  measured it. See [the evidence](time-and-coordinate-semantics.md#dont-use-the-headset-ticks-as-time).
- What the bundled LabRecorder should do when a stream comes back with the same
  source ID.
- A saved `eye_tracker_space` file for old-data tests.
