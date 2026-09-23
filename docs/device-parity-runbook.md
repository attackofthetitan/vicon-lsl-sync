# Hardware test guide

## What this guide is for

The automated tests do not run Unity, Windows device features, or a real Vicon system. They also do not run Extended Eye Tracking, OpenXR, Vuforia, or the HoloLens (ARM64 UWP) build of liblsl.

Use this guide to compare a changed build with a known-good build on real equipment. It shows that a code tidy-up did not change behavior. It does not sign off a dependency update, a stream layout change, a coordinate change, or a new timing rule. Those need their own plan and new expected results.

The expected behavior comes from:

- [Behavior that must stay the same](behavior-contract.md)
- [How each part starts, stops, and recovers](runtime-state-machines.md)
- [How time and coordinates work](time-and-coordinate-semantics.md)

## When to use this guide

Do the parts that apply when you change:

- `GazeDataProvider`, `GazePublisherWorker`, `GazeLSLOutlet`, `GazeTiming`, or `GazeCoordinateTransform`.
- `VuforiaModelTargetPoseOutlet`, `ModelTargetPoseEncoder`, target stream details, or stair alignment.
- A public or saved field on a Unity component.
- The HoloLens liblsl build, the C# LSL wrapper, the Unity version, Mixed Reality OpenXR, the Extended Eye Tracking SDK, or the Vuforia version.
- HoloLens source IDs, stream names, value layout, expected rate, clock details, or coordinate names.
- How the live preview finds streams, corrects clocks, shows rates, or supports alignment.
- XDF clock correction or automatic alignment of recordings.
- Starting LabRecorder, choosing streams, recording, or stream recovery.

For a desktop-only change, mark the headset-only parts as not needed and write down why.

## What you need

- A HoloLens 2 with working Extended Eye Tracking, where the app can get permission.
- The real Unity project that uses these scripts. This repository does not have a full saved Unity scene.
- Microsoft Mixed Reality OpenXR 1.5.1 or later.
- The Microsoft Extended Eye Tracking SDK the app uses.
- A Vuforia Model Target for the real stairs, when testing alignment.
- One `GazeLSLConfig` asset shared by both HoloLens outputs.
- A desktop computer on the same network, where it can see LSL streams.
- A Vicon DataStream server with tracked subjects or objects.
- The desktop app and LabRecorder built from the version being tested.
- The real stairs in their measured position.
- Somewhere to save Unity logs, desktop logs, `.xdf` files, full stream details, and screenshots or video.

## Record the setup

Make one record for each run. Fill in every field.

| Field | Value |
| --- | --- |
| Repository version | |
| Known-good version | |
| Code tidy-up being tested | |
| Date, time, and time zone | |
| Person running the test | |
| HoloLens model and OS build | |
| Unity editor and runtime version | |
| Scripting backend and API compatibility level | |
| Mixed Reality OpenXR version | |
| Extended Eye Tracking SDK version | |
| Vuforia version | |
| HoloLens liblsl version | |
| Desktop liblsl version | |
| LabRecorder version and its liblsl version | |
| Vicon server, software, and SDK version | |
| Gaze delivery delay shown on the headset | |
| Gaze delivery state, counters, and any catch-up fallback warning from the headset log | |
| Gaze stream name, type, and source ID | |
| Target stream name, type, and source ID | |
| Vicon marker and segment stream names | |
| Where the real stairs are, and any notes | |
| Recording file names and log folders | |

Use the same equipment and software for the known-good and changed builds. If anything is different, write it down, and rule it out before blaming the code change.

## Run the automated tests first

From the top of the repository:

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

If you have the full desktop setup, download the Vicon SDK submodule and install liblsl and Qt 6. Then build everything and run all the CTest tests.

Record the result:

- [ ] Generated stream files are up to date.
- [ ] C++ tests that need nothing extra pass.
- [ ] C# tests that need no headset pass.
- [ ] Full desktop and Qt tests pass, when needed.
- [ ] There are no unexpected changes to source, third-party, or generated files.

## Check the setup before using the headset

1. Check the Unity scene has one active `GazeDataProvider` and one `GazeLSLOutlet` using the right `GazeLSLConfig`.
2. For stair alignment, check there is one `VuforiaModelTargetPoseOutlet` using the same config and the right `ObserverBehaviour` or model target.
3. Check the gaze and target components use the same Unity world.
4. Check the user allowed gaze permission.
5. Check the eye tracker offers exactly 90 Hz.
6. Check the desktop and HoloLens can see each other's LSL streams through the network and firewall.
7. Check the desktop app uses the right Vicon server and stream names.
8. Check the chosen recorder mode works: remote control is on for **Record every
   visible stream**, or the bundled command-line recorder is there for picking
   exact streams.
9. Select **Find LSL Streams** and check each required role shows the right
   source ID, computer, channel count, expected and measured rate, coordinate
   name, and sample age.
10. Check the study folder exists and **Recording Destination** shows the right
    final `.xdf` path, with no existing file you have not agreed to overwrite.
11. Save the session setup or preset, and a screenshot of how the Unity
    Inspector is wired up.

Do not test stair alignment if the gaze and target components use different Unity worlds.

## Test 1: permission and startup

### Steps

1. Start the Unity app from fully closed.
2. Watch what happens with permission, and the headset log.
3. Keep the app open until it has finished looking for the tracker.
4. Find the gaze stream from the desktop.
5. Close the app fully and do it once more.

### Expected result

- If permission is denied or the headset is not supported, an error is logged and no half-made gaze stream appears.
- A tracker that cannot do exactly 90 Hz logs an error and no gaze stream appears.
- A good tracker opens, picks 90 Hz, makes a position anchor, starts a new session number, and logs that it is ready.
- The LSL stream only appears after `TryGetEffectiveFrameRate` confirms the 90 Hz session.
- Only one gaze stream uses the set identity. There is no copy passed through the desktop.

### Save

- [ ] The Unity log from start until success or the expected failure.
- [ ] A screenshot of the stream list and a full stream details export.
- [ ] The second start's result, matching the first.
- [ ] Proof that no old stream is left after the app closes.

## Test 2: stream channels and details

Save the full LSL description of every stream and compare it with
[Behavior that must stay the same](behavior-contract.md).

### Gaze

- [ ] Name, type, and source ID match the config.
- [ ] Format is `double64` and expected rate is `90`.
- [ ] There are exactly 21 values, in the order of the generated file.
- [ ] Labels and units exactly match `stream-contracts/hololens-gaze.json`.
- [ ] `coordinate_frame` is `hololens_stationary_shared_with_gaze`.
- [ ] Capture time, clock, delivery delay, and queue limit details match the guide.

### Target

- [ ] Name, type, and source ID match the config.
- [ ] Format is `double64`, expected rate is irregular or zero, and there are eight values.
- [ ] Labels and units match the target definition.
- [ ] Coordinate name exactly matches gaze.
- [ ] The stream details say the time comes from the local clock when the position is read.

### Vicon

- [ ] Marker and segment names and the `MoCap` type match the config.
- [ ] Value order matches the order Vicon lists things in.
- [ ] Units, expected rate or fallback, source IDs, and time details match the guide.
- [ ] Marker and segment source IDs keep the same computer name at the end after reconnecting.

Keep the raw stream description files, not just screenshots.

Also save the desktop stream list. It must show the same name, type, source ID,
computer, session ID, channels, expected rate, coordinate name, and channel
layout result as the raw descriptions. Write down any missing-details warning
instead of treating a fallback as complete.

## Test 3: steady gaze timing and rate

### Steps

1. Start gaze in a simple Unity scene that does not work the headset hard.
2. Start the live preview and wait at least ten seconds.
3. Record at least 60 seconds with LabRecorder.
4. Look around naturally so the samples change.
5. Save the Unity log, the preview status, and the XDF file.

### Expected result

- Expected gaze rate is 90 Hz.
- Once the two-second rate window fills, the preview shows a rate based on clock-corrected sample times.
- The dashboard shows expected and measured rates, sample age, live preview
  delay, skipped older input, and replaced display frames separately. Skipping
  old preview input on purpose must not be reported as lost source samples.
- With the same setup, the changed build gives the same normal-rate or low-rate result as the known-good build. The warning starts below 80% of the expected rate, which is 72 Hz for a 90 Hz stream.
- Sent gaze times are finite, positive, and always go up.
- The sent time follows the SDK's reading time, not a Unity frame time.
- Small normal batches do not play old gaze behind current Vicon movement.
- The XDF file has the normal LSL clock correction records.

### Work out and save

- Sample count and recording length.
- Smallest, middle, 95th-percentile, and largest gap between sample times.
- Rate over the whole recording and over useful two-second windows.
- Number of repeated or earlier times. It should be zero.
- Number and length of gaps over 500 ms.
- How much of the 90 Hz grid was captured: divide the sample count by the recording length in 11.111 ms steps. The app catches up on every reading the tracker makes, so this should be close to 100%. A steady shortfall means readings are being lost before the queues, not dropped by them.
- A screenshot of stream health after a normal update, and after the stream is
  left to go out of date on purpose.
- Skipped older input, replaced display frames, and the largest preview delay
  shown over the same time.
- The gaze delivery delay shown in the reading counters, and the catch-up's
  failed-inside-the-SDK count next to its reading count. If the failure count is
  close to the reading count, the app is asking once per step for a reading that
  cannot exist yet.

Do not make up a new allowed drop rate during a tidy-up. Compare with the known-good build under the same conditions and report any clear difference.

## Test 4: overload and queue limits

Use a load that you can repeat and undo. It should slow down Unity's main-thread conversion or build up a queue, without changing the code being tested. Write down exactly what load you used.

1. Record a steady stretch before the load.
2. Apply the load long enough to build up more than 500 ms of captured data.
3. Remove the load and let the app recover.
4. Look at the timestamps, and at how gaze and Vicon line up on screen.

Expected result:

- When either queue covers too much time, old items are dropped and the newest is kept.
- The recording has a clear gap in time.
- After recovering, gaze goes back to following current movement. It does not send a quick burst of old data.
- Timestamps keep going up.
- The stream still has 21 values.
- The preview may show a lower rate, but must not keep showing an old rate after the stream goes out of date.

Save:

- [ ] The exact load and when it was applied.
- [ ] A timestamp plot or table from before, during, and after.
- [ ] The number of repeated and earlier times.
- [ ] Video or a plot showing no late replay.
- [ ] Results from the same load on both builds.

## Test 5: tracker errors and restart

Use app focus, suspend, tracker session tools, or safe fault switches that you can repeat and that suit the real Unity project.

### Short error

Cause a Windows gaze read error that is shorter than the "keeps failing" limit.

Expected: the warning count goes up, sending keeps going or has a short gap, and the tracker is not replaced straight away.

### Error that keeps going

Keep the gaze reader failing for about one second.

Expected: the worker reports the failure, the output only closes after the worker has exited, the app looks for the tracker again, and a new session later starts sending again.

### Removal and finding it again

Use a supported tracker removal, or an app start, stop, or suspend you can repeat.

Expected: old queues and the reading-time check are cleared. A late reply from the old tracker cannot take over. No sample from the old session shows up in the new one.

Pass when:

- [ ] A short error does not cause the stream to be replaced again and again.
- [ ] An error that keeps going causes one controlled recovery.
- [ ] The output stays open until the old worker can no longer send.
- [ ] The reopened stream keeps its name, type, and source ID.
- [ ] Recorded time never goes backward during recovery.
- [ ] LabRecorder handles the reopened stream the same way as with the known-good build.
- [ ] Unity logs show no unhandled errors or overlapping restart loops.

## Test 6: usable and unusable rays and targets

### Gaze

Test gaze from both eyes and, when available, each eye on its own, with both good and bad tracking.

- [ ] A usable origin and direction are finite, and the direction has a length close to one.
- [ ] A valid flag is `1.0` only when the converted ray can be used.
- [ ] An unsupported or unusable eye keeps its place in the fixed layout and is marked invalid.
- [ ] If the headset position lookup fails, that capture gets invalid ray values. It does not get a new time.

### Target

Find the stair target, then lose it.

- [ ] `TRACKED` and `EXTENDED_TRACKED` send a finite, flipped position and `Tracked = 1.0`.
- [ ] Other states send seven `NaN` values and `Tracked = 0.0`.
- [ ] Losing the target clears the live alignment samples.
- [ ] Finding it again starts a new steady set.

Save example values for good and bad cases.

## Test 7: stair alignment and coordinate direction

### Prepare

1. Put the real stair target in the same place and facing the same way as in the known-good run.
2. Check the fixed settings still describe where the target is in Vicon. The current ID is `stair-model-v1`; [How time and coordinates work](time-and-coordinate-semantics.md) lists its fixed position.
3. Restart the HoloLens app to get a fresh Unity world.
4. Check that both the gaze and target stream details use `hololens_stationary_shared_with_gaze`.

### Live steps

1. Start Vicon streaming and the desktop preview.
2. Find the Vuforia stair target and keep it still.
3. Enter a fixed setup ID, the measured stair position, the coordinate names, and
   enough setup notes to repeat the test. Then select **Calibrate from Stair
   Target**.
4. Hold still until 20 samples pass.
5. Read the position and angle error values shown.
6. Look along known stair edges and compare the gaze ray with the real stairs and the Vicon-aligned model.
7. Select **Save Session Calibration**, export the saved calibration, apply it,
   select **Clear Calibration**, then apply it again.
8. Select **Copy**, then **Hide** on the copy. Import the exported calibration
   into fresh settings and check its setup identity and quality.

### Expected result

- Losing the target, moving more than 20 mm, or turning more than 3 degrees starts collecting again.
- A steady set of 20 samples within both the position and angle limits makes
  one alignment that lasts for this session only.
- Automatic values are not saved until **Save Session Calibration** is chosen.
- The saved calibration includes ID and version, setup, stair model identity and
  measured position, gaze and target coordinate names, the alignment, notes,
  creation time, sample count, position error, angle error, and whether missing
  details were confirmed.
- Applying the saved calibration is visible, can be undone, and keeps its
  quality on screen while stream status keeps updating. Copy, Hide, export, and
  import keep the measured values.
- **Clear Calibration** takes the preview back to the HoloLens's own coordinates
  straight away, and gaze clearly stops matching the Vicon-aligned stair model.
- Stair direction and gaze match the known-good build. Nothing is mirrored in X or Z, turned round 180 degrees, or off by a metre/millimetre mix-up.
- Running alignment again after restarting the HoloLens world brings the match back.

### Recording after pausing Vuforia on purpose

This test covers the new frozen-reference behavior. It is not a check that
nothing changed from the old output, where every paused sample was invalid. The
automated tests cover keeping the reference and rebuilding alignment from an
XDF, but this also needs Unity, OpenXR, and Vuforia on the headset.

1. With the stairs and Unity world unchanged, find a steady target and finish the
   desktop calibration. Pause Vuforia with **M**, leaving the target outlet on.
2. Start a new recording after the pause, with gaze, target, and Vicon. Check the
   target samples hold a fixed, finite position and `Tracked = 2`.
3. Close and restart only the desktop app, then open that XDF without applying
   a saved calibration. Check gaze position and direction against the real
   Vicon path. The summary must say it found a frozen stair reference.
4. Repeat for several separate runs while paused. Check every XDF can be opened
   on its own. Gaze times and live sample rates must not change.
5. Turn Vuforia back on and find the target again. Pause again and check the new
   reference is used. Restarting the HoloLens app or turning off the target
   outlet must throw away the old reference. Pausing before a new one is found
   gives `Tracked = 0` and seven NaNs, with a playback calibration warning.
6. Look away from the target with Vuforia still on. Check the output becomes
   invalid instead of wrongly claiming a frozen or live position.

### Recording steps

1. Record gaze, target, marker, and segment streams while the target stays still.
2. Open the XDF file in the built-in preview.
3. Check that it finds a steady stretch, applies the alignment, and says so in the summary.
4. Compare live and recorded shapes at matching corrected times.

### Save

- [ ] A photo or diagram of where the target is.
- [ ] Preview screenshots or video before and after alignment.
- [ ] Sample count and both error values.
- [ ] The exported calibration JSON, and a screenshot of its quality and
  coordinate-match indicators.
- [ ] Proof that applying and clearing a saved calibration can be undone, and
  that **Hide** removes the copy from the normal list without deleting the
  original.
- [ ] The XDF file and its preview summary.
- [ ] Simple axis direction checks that pass.
- [ ] An overlay or side-by-side view of the known-good and changed builds.

## Test 8: old or missing coordinate names

Use saved example files. Do not label a new file as old data.

For a file marked `eye_tracker_space`:

- [ ] Gaze is shown for a data quality check.
- [ ] Automatic stair alignment does not run.
- [ ] If a target is present, the summary says the gaze uses old tracker-relative coordinates.

For a file with an empty gaze or target coordinate name:

- [ ] The old-data rule allows alignment when everything else passes.
- [ ] Live calibration, or saving a calibration, asks you to confirm when
  coordinate details are missing, and the session details record that you
  confirmed.

For a file with two different, non-empty coordinate names:

- [ ] Alignment is blocked, and the live status shows the mismatch.

Changing the empty-name rule needs its own compatibility plan and a look at real saved files.

## Test 9: Vicon reconnect and layout change

### Steps

1. Start the Vicon marker and segment streams and start recording in LabRecorder.
2. Break the Vicon connection, then restore it.
3. In another run, add, remove, or reorder a subject or object so the layout changes.
4. Keep recording through the recovery.

### Expected result

- Losing the connection closes both Vicon streams, disconnects, waits, and reconnects.
- Source IDs stay the same after reopening.
- Timestamps always go up within the recovered stream.
- The bridge spots layout changes on its 100-frame check.
- A change in either layout reopens both streams.
- The new value order follows Vicon's new order.
- Empty layouts stay healthy and open no LSL stream.
- LabRecorder handles the reopened stream by source ID the same way as in the known-good run.

### Save

- [ ] Bridge states and log order.
- [ ] Full stream details before and after.
- [ ] A source ID comparison.
- [ ] A check that timestamps stay in order.
- [ ] A look at the XDF streams and layouts.

## Test 10: stream identity, recording controls, shutdown, and file check

### Steps

1. Start the packaged desktop app. First leave a LabRecorder window running at
   the set address and check the app does not start a second one. Then do it
   again with nothing at that address, and check the recorder it starts shows as
   **Started here**. The first recorder should show as **External**.
2. Start the Vicon and HoloLens streams after the LabRecorder window is already
   open. Select **Find LSL Streams** and link each required role by source ID.
3. Start a second, harmless stream with the same display name but a different
   source ID. Check the role clearly shows as unclear until you pick one or turn
   on **Follow by name**.
4. Restart one stream with the same source ID. Check the newest copy is picked in
   a predictable way and the recovery is reported.
5. Enter valid file name fields, try **Find Next Run**, and wait for the delayed
   file name update. Check the final path shown is exactly the path sent to the
   recorder.
6. In **Record every visible stream** mode, select **Check Setup** and Start.
   Check the refresh just before Start picks up streams that appeared after the
   recorder started.
7. Stop normally and wait for the file check.
8. Do it again in exact-stream mode, leaving out the duplicate or unrelated
   stream. Check the command-line recorder only gets searches for the streams you
   picked.
9. Try a setup check with only warnings, a blocked setup check, recorder-only
   mode, and one **Record Anyway** with a reason.
10. Close the app at each of these points: before Start is sent, during each
    Start command, while recording, during Stop, and after the recorder
    disconnects. Include a bridge reconnect, and a slow preview search or
    details step if your test setup can do it.

### Expected result

- Checking the recorder address stops a second copy from starting. Who started
  the recorder stays correct through failed starts, exits, reconnects, detach,
  and close.
- Record-every-stream Start sends `update`, `select all`, `filename`, and `start`
  in that order. Exact-stream mode starts the bundled command-line recorder with
  only the streams picked beforehand.
- Duplicate names never lead to an unexplained choice. A stream that comes back
  with the same source uses the newest copy. The same source ID on different
  computers is kept apart.
- The saved file name matches the final path shown and the session details
  exactly. Paths that climb out of the folder, reserved names, places that cannot
  be written to, and existing files you have not agreed to overwrite are still
  blocked.
- A normal Stop reply arrives before the state becomes `Stopped`.
- Pressing Start or Stop twice sends one command. Closing cancels a Start that
  was not sent, or sends one final Stop after any Start that may have reached
  the recorder.
- Closing stays responsive and shows which part is holding things up. The
  four-second bridge, two-second preview and file, and 15-second recorder limits
  are shown as results. The window never deletes a worker that is still running.
- The final close may only close a recorder the desktop app started, and only
  after Stop finishes or its time limit runs out. A recorder someone else started
  is never closed, even after the connection is lost.
- The file check reports the expected source and channel layout, time range,
  sample count, length, measured rate, gaps, clock corrections, and fixed
  timestamps as **Checked**, **Checked with warnings**, or **Needs attention**.
  It never changes the XDF. The automatic run number increase only happens under
  the chosen rule, after the file exists.

### Save

- [ ] A log of the remote commands, or the test server's log.
- [ ] Stream lists for record-every-stream and exact-stream modes, which source
  each role was linked to, the duplicate warning, and the recovered-copy result.
- [ ] The final XDF path, the list of recorded streams, and the file check report.
- [ ] The setup check's required, warning, and information results, and the
  recorder-only or Record Anyway reason saved in the session details.
- [ ] Dashboard screenshots during Starting, Recording, Stopping, and the file
  check, including the path, who started the recorder, rates, free space, and
  drop counts.
- [ ] A normal Stop, and the result of every close during Start, disconnect, and
  so on, with the times each part changed state and whether it hit its time
  limit.
- [ ] Proof that the desktop app does not close a LabRecorder it did not start.

## Final checklist

- [ ] The setup record is complete.
- [ ] The known-good and changed runs used comparable setups.
- [ ] The automated tests passed first.
- [ ] Gaze and target channel layouts and stream details match exactly.
- [ ] Vicon layouts, source IDs, and timestamps still work the same way.
- [ ] Gaze only starts with permission, a position anchor, and exactly 90 Hz.
- [ ] Steady timing and rate numbers are saved.
- [ ] Stream identity, duplicate names, a stream coming back, and duplicate
  source IDs are all handled visibly and predictably.
- [ ] Overload leaves gaps instead of late replays.
- [ ] A tracker restart keeps sessions and resources apart.
- [ ] Unusable gaze and target states keep fixed layouts and invalid values.
- [ ] Live and recorded stair alignment match the known-good direction and scale.
- [ ] A complete saved calibration and its quality are saved.
- [ ] Old, missing, and different coordinate names follow the rules.
- [ ] Vicon reconnects and layout changes can still be recorded.
- [ ] Recorder command order, both stream-choice modes, the exact path, closing
  during Start, who started the recorder, and time-limited shutdown all match the
  documented behavior.
- [ ] Setup check and file check results are in the session details.
- [ ] Unity, desktop, and recorder logs have no new unhandled errors.
- [ ] XDF files, stream details, logs, numbers, and screenshots are saved with the change.
- [ ] No third-party submodule file or version changed.

## Stop and ask for a decision when

Stop the review if any of these is missing or in dispute:

- The real Unity scene, how its prefabs are wired, or who owns the saved assets. This repository does not store the whole scene.
- Whether everyone agrees on the measured stair position. The bottom front-left
  corner is at `(-1.205, -0.213, 0)` metres in Vicon coordinates, confirmed on
  2026-09-17. [The coordinate guide](time-and-coordinate-semantics.md#fixed-stair-settings)
  works out the model's position from it. Any other setup needs its own
  measurement.
- An agreed limit for how many samples the headset may drop, beyond the preview's warning below 80% of the expected rate. This repository does not set a maximum drop rate for releases.
- What `SystemRelativeTime.Ticks` can be compared with, and how fast it ticks.
  Headset results now show it does not tick at `Stopwatch.Frequency`, which an
  earlier version of this item assumed:
  - A reading whose own time said it was 0.022 s old measured -7862.129 s against
    `Stopwatch.GetTimestamp()`, about 2.2 hours in the future.
  - In a later session the pair read 0.020 s and -231.332 s, and two seconds
    later, 0.021 s and -233.588 s. If only the starting point were different, the
    gap would have stayed the same.
  - In both sessions the gap grows by about 0.92 s of "future" for every second
    the headset has been on. That is a speed difference, not a different starting
    point.

  The code no longer depends on the answer. Those ticks are now only used to put
  readings in order and to look up the headset position, and every length of time
  on the gaze path is measured on the LSL clock. So this item no longer blocks
  anything, but it is still open: nobody has measured the real tick speed, and no
  code should start assuming one. Write it down; do not quietly change the rule.
- What the bundled LabRecorder version should do when a HoloLens or Vicon stream comes back with the same source ID.
- A saved `eye_tracker_space` file for old-data tests.

Do not settle these questions by quietly changing code or expected results during a tidy-up.

## Main source files

- `README.md`
- `hololens-gaze-lsl/README.md`
- HoloLens scripts under `hololens-gaze-lsl/Assets/Scripts`
- C# tests that need no headset, under `hololens-gaze-lsl/Tests`
- Desktop bridge, preview, app, and tests under `vicon-lsl-bridge`
- Stream definitions under `stream-contracts`
- `.github/workflows/build-bridge.yml`
