# Hardware test guide

## Why this guide exists

The automated checks do not run Unity, Windows device APIs, or real Vicon hardware. They also do not run Extended Eye Tracking, OpenXR, Vuforia, or the ARM64 UWP liblsl build.

Use this guide to compare a changed build with a known-good build on real equipment. It proves that a code-only cleanup kept the same behavior. It does not approve a dependency update, stream-layout change, coordinate change, or new timing rule. Those changes need a separate plan and new expected results.

The expected behavior comes from:

- [Behavior that must stay the same](behavior-contract.md)
- [How services start, stop, and recover](runtime-state-machines.md)
- [How time and coordinates work](time-and-coordinate-semantics.md)

## When to use this guide

Complete the parts that apply when changing:

- `GazeDataProvider`, `GazePublisherWorker`, `GazeLSLOutlet`, `GazeTiming`, or `GazeCoordinateTransform`.
- `VuforiaModelTargetPoseOutlet`, `ModelTargetPoseEncoder`, target stream details, or stair alignment.
- A public or saved Unity component field.
- The ARM64 UWP liblsl build, managed LSL binding, Unity version, Mixed Reality OpenXR, Extended Eye Tracking SDK, or Vuforia version.
- HoloLens source IDs, stream names, value layout, expected rate, clock details, or coordinate names.
- Live preview stream discovery, clock correction, rate display, or alignment support.
- XDF clock correction or automatic recorded-data alignment.
- LabRecorder startup, stream selection, recording, or stream recovery.

For a desktop-only change, mark device-only parts as not needed and write down why.

## What you need

- A HoloLens 2 with working Extended Eye Tracking and permission available to the app.
- The real Unity project that uses these scripts. This repository does not contain a complete saved Unity scene.
- Microsoft Mixed Reality OpenXR 1.5.1 or later.
- The Microsoft Extended Eye Tracking SDK used by the app.
- A Vuforia Model Target for the physical stairs when checking alignment.
- One `GazeLSLConfig` asset used by both HoloLens outputs.
- A desktop computer on the same LSL-visible network.
- A Vicon DataStream server with tracked subjects or objects.
- The desktop app and LabRecorder built from the revision being checked.
- The physical stairs in the expected measured position.
- A place to save Unity logs, desktop logs, `.xdf` files, full stream details, and preview pictures or video.

## Record the setup

Create one record for each run. Fill every field.

| Field | Value |
| --- | --- |
| Repository revision | |
| Known-good revision | |
| Code cleanup being checked | |
| Date, time, and time zone | |
| Person running the check | |
| HoloLens model and OS build | |
| Unity editor and runtime version | |
| Scripting backend and API compatibility level | |
| Mixed Reality OpenXR version | |
| Extended Eye Tracking SDK version | |
| Vuforia version | |
| UWP liblsl revision | |
| Desktop liblsl revision | |
| LabRecorder revision and liblsl revision | |
| Vicon server, software, and SDK version | |
| `Stopwatch.Frequency` shown on the device | |
| Gaze stream name, type, and source ID | |
| Target stream name, type, and source ID | |
| Vicon marker and segment stream names | |
| Physical stair position and notes | |
| Recording filenames and log folders | |

Use the same equipment and software for the known-good and changed builds. If anything differs, list it. First rule out that setup difference before blaming the code change.

## Run automated checks first

From the repository root:

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

When the full desktop setup is available, initialize the Vicon SDK submodule and provide liblsl and Qt 6. Then build every target and run all registered CTest checks.

Record the result:

- [ ] Generated stream files are current.
- [ ] C++ checks that need no desktop dependencies pass.
- [ ] Device-independent C# checks pass.
- [ ] Full desktop and Qt checks pass when needed.
- [ ] The working tree has no unexpected source, third-party, or generated changes.

## Check the setup before using the device

1. Confirm the Unity scene has one active `GazeDataProvider` and one `GazeLSLOutlet` using the intended `GazeLSLConfig`.
2. For stair alignment, confirm one `VuforiaModelTargetPoseOutlet` uses the same config and the intended `ObserverBehaviour` or model target.
3. Confirm gaze and target components use the same Unity/XR world.
4. Confirm the user granted gaze permission.
5. Confirm the eye tracker offers exactly 90 Hz.
6. Confirm the desktop and HoloLens can discover each other's LSL streams through the current network and firewall.
7. Confirm the desktop app uses the intended Vicon server and stream names.
8. Confirm the selected recorder policy is usable: remote control is enabled for
   **Record every visible stream**, or the packaged command-line recorder is
   present for exact selection.
9. Select **Find LSL Streams** and confirm each required role shows the intended
   source ID, host, channel count, expected and measured rate, coordinate name,
   and sample age.
10. Confirm the study folder exists and **Recording Destination** shows the
    intended final `.xdf` path without an unconfirmed existing file.
11. Save the versioned session configuration or preset and a picture of the
    Unity Inspector wiring.

Do not test stair alignment when the gaze and target components use different Unity worlds.

## Test 1: permission and startup

### Steps

1. Start the Unity app from a full stop.
2. Watch permission handling and the device log.
3. Keep the app active until tracker discovery finishes.
4. Find the gaze stream from the desktop.
5. Stop the app fully and repeat once.

### Expected result

- Denied permission or missing device support logs an error and creates no partial gaze stream.
- A tracker without exact 90 Hz support logs an error and creates no gaze stream.
- A good tracker opens, selects 90 Hz, creates a spatial graph node, starts a new session number, and logs that it is ready.
- The LSL stream appears only after `TryGetEffectiveFrameRate` confirms the active 90 Hz session.
- Only one gaze stream uses the configured identity. There is no desktop relay copy.

### Save

- [ ] Unity log from launch through success or the expected failure.
- [ ] Stream list picture and full stream-details export.
- [ ] Second launch result that matches the first.
- [ ] Proof that no old stream remains after the app exits.

## Test 2: stream channels and details

Save the full LSL description for every available stream and compare it with
[Behavior that must stay the same](behavior-contract.md).

### Gaze

- [ ] Name, type, and source ID match the config.
- [ ] Format is `double64` and expected rate is `90`.
- [ ] There are exactly 21 values in generated-file order.
- [ ] Labels and units exactly match `stream-contracts/hololens-gaze.json`.
- [ ] `coordinate_frame` is `hololens_stationary_shared_with_gaze`.
- [ ] Capture time, device timer rate, clock, and queue-limit details match the guide.

### Target

- [ ] Name, type, and source ID match the config.
- [ ] Format is `double64`, expected rate is irregular or zero, and there are eight values.
- [ ] Labels and units match the target definition.
- [ ] Coordinate frame exactly matches gaze.
- [ ] Stream details say the time comes from the local clock when the transform is read.

### Vicon

- [ ] Marker and segment names and `MoCap` type match the config.
- [ ] Value order matches current Vicon discovery order.
- [ ] Units, expected rate or fallback, source IDs, and time details match the guide.
- [ ] Marker and segment source IDs keep the same computer-name ending after reconnect.

Keep the raw stream-description files, not only pictures.

Also save the desktop stream list. It must show the same name, type, source ID,
host, session ID, channels, expected rate, coordinate name, and channel-layout
result as the raw descriptions. Record any missing-details warning rather than
treating a fallback as complete.

## Test 3: steady gaze timing and rate

### Steps

1. Start gaze in a simple, low-load Unity scene.
2. Start the live preview and wait at least ten seconds.
3. Record at least 60 seconds with LabRecorder.
4. Move your gaze naturally so samples change.
5. Save the Unity log, preview status, and XDF file.

### Expected result

- Expected gaze rate is 90 Hz.
- After the two-second rate window fills, the preview shows a rate based on clock-corrected sample times.
- The dashboard separately shows expected and measured rates, sample age, live
  preview delay, skipped older input, and replaced display frames. Deliberately
  skipping old preview input must not be reported as source sample loss.
- Under the same setup, the changed build has the same normal-rate or low-rate result as the known-good build. The current warning starts below 80% of the expected rate, which is 72 Hz for a 90 Hz stream.
- Published gaze times are finite, positive, and always increase.
- The sent time follows the SDK reading time. It does not repeat a Unity render time.
- Small normal batches do not replay old gaze behind current Vicon movement.
- The XDF file contains the normal LSL clock-correction records.

### Calculate and save

- Sample count and recording length.
- Smallest, middle, 95th-percentile, and largest gap between sample times.
- Rate for the whole recording and for useful two-second windows.
- Count of duplicate or earlier times; expected count is zero.
- Count and length of gaps over 25 ms.
- A picture of persistent stream health after a normal update and after the
  source is deliberately allowed to become stale.
- Skipped older input, display replacements, and maximum displayed preview
  latency during the same interval.
- Device `Stopwatch.Frequency` and examples that divide raw reading counts by that value, when logging is available.

Do not invent a new allowed drop rate during code cleanup. Compare with the known-good build under the same conditions and report any clear difference.

## Test 4: overload and queue limits

Use a controlled load that you can repeat and undo. It should delay Unity's main-thread conversion work or create a queue without changing the code being checked. Write down the exact load.

1. Record a stable period before the load.
2. Apply the load long enough to build more than 25 ms of captured data.
3. Remove the load and let the app recover.
4. Review timestamps and gaze/Vicon visual alignment.

Expected result:

- When either queue spans too much time, old entries are dropped and the newest remains.
- The recording has a clear time gap.
- After recovery, gaze returns near current motion. It does not send a fast burst of old data.
- Timestamps keep increasing.
- The stream still has 21 values.
- The preview may show a lower rate, but it must not show an old completed rate after the stream becomes stale.

Save:

- [ ] Exact load and time range.
- [ ] Before, during, and after timestamp plot or table.
- [ ] Duplicate and earlier-time count.
- [ ] Video or plot showing no delayed replay.
- [ ] Results from the same load on both builds.

## Test 5: tracker errors and restart

Use app focus, suspend, tracker-session tools, or safe repeatable fault controls that fit the real Unity project.

### Brief error

Create a projected Windows gaze-read error shorter than the lasting-error limit.

Expected: the warning count rises, publishing continues or has a short gap, and the tracker is not replaced at once.

### Lasting error

Keep the provider failing for about one expected second.

Expected: the worker reports provider failure, the output closes only after the worker exits, tracker discovery restarts, and a new session later publishes again.

### Removal and discovery

Use a supported tracker-removal event or a repeatable app start, stop, or suspend event.

Expected: old queues and the reading guard clear. A late result from the old tracker cannot become active. No old-session sample appears in the new session.

Pass when:

- [ ] A brief error does not cause repeated stream replacement.
- [ ] A lasting error creates one controlled recovery.
- [ ] Output resources stay open until the old worker can no longer send.
- [ ] The recreated stream keeps its name, type, and source ID.
- [ ] Recorded time never moves backward during recovery.
- [ ] LabRecorder handles stream recreation the same way as the known-good build.
- [ ] Unity logs show no unhandled error or overlapping restart loops.

## Test 6: valid and invalid rays and targets

### Gaze

Check combined gaze and, when available, separate left and right gaze in both good and bad tracking conditions.

- [ ] A valid origin and direction are finite, and direction length is close to one.
- [ ] A valid flag is `1.0` only when the converted ray can be used.
- [ ] An unsupported or invalid eye keeps its values in the fixed layout and marks them invalid.
- [ ] A failed spatial pose gives that capture invalid ray values. It does not give the ray a new time.

### Target

Acquire the stair target, then lose it.

- [ ] `TRACKED` and `EXTENDED_TRACKED` send a finite reflected pose and `Tracked = 1.0`.
- [ ] Other states send seven `NaN` values and `Tracked = 0.0`.
- [ ] Losing the target clears the live alignment sample set.
- [ ] Finding it again starts a new stable set.

Save example values for good and bad cases.

## Test 7: stair alignment and coordinate direction

### Prepare

1. Put the physical stair target in the same position and direction used by the known-good run.
2. Confirm the fixed settings still describe the expected Vicon target pose. The current ID is `stair-model-v1`; [How time and coordinates work](time-and-coordinate-semantics.md) lists its fixed position.
3. Restart the HoloLens app to create a fresh stationary Unity world.
4. Confirm that both gaze and target stream details use `hololens_stationary_shared_with_gaze`.

### Live steps

1. Start Vicon streaming and the desktop preview.
2. Find the Vuforia stair target and keep it still.
3. Enter a stable physical setup ID, measured stair pose, coordinate-frame names,
   and enough setup notes to reproduce the test, then select **Calibrate from
   Stair Target**.
4. Hold still until 20 samples pass.
5. Read the reported position and angle error values.
6. Look along known stair edges and compare the gaze ray with the physical and Vicon-aligned model.
7. Select **Save Session Calibration**, export the saved calibration, apply it,
   select **Clear Calibration**, then apply it again.
8. Select **Copy**, then **Hide** for the copy. Import the exported calibration
   into fresh settings and confirm its setup identity and quality.

### Expected result

- Target loss, more than 20 mm of movement, or more than 3 degrees of rotation restarts collection.
- A stable set of 20 samples within both position and angle error limits creates
  one session-only transform.
- Automatic values are not saved until **Save Session Calibration** is chosen.
- The saved calibration includes ID/version, physical setup, stair model identity
  and measured pose, gaze/target coordinate frames, transform, notes, creation
  time, sample count, position error, angle error, and any confirmed missing-details
  fallback.
- Applying the saved calibration is visible, reversible, and leaves its quality
  visible while stream-status events continue. Copy, Hide, export, and import
  preserve the scientific values.
- **Clear Calibration** returns the preview to its uncalibrated HoloLens frame
  at once, and gaze visibly stops matching the Vicon-aligned stair model.
- Stair direction and gaze match the known-good build. There is no X/Z mirror, 180-degree reversal, or metre/millimetre mistake.
- Running alignment again after a HoloLens world restart restores the match.

### Recorded-data steps

1. Record gaze, target, marker, and segment streams while the target stays still.
2. Open the XDF file in the built-in preview.
3. Confirm that the stable-window calculation applies alignment and reports it in the summary.
4. Compare live and recorded geometry at matching corrected times.

### Save

- [ ] Picture or diagram of target placement.
- [ ] Preview picture or video before and after alignment.
- [ ] Sample count and both error values.
- [ ] Exported calibration JSON and a picture of its always-visible quality and
  stream-details compatibility indicators.
- [ ] Evidence that applying and clearing a saved calibration is reversible
  and that **Hide** removes the copy from normal selection without deleting the
  original.
- [ ] XDF file and its preview summary.
- [ ] Passing simple axis-direction checks.
- [ ] Overlay or side-by-side view of known-good and changed builds.

## Test 8: old or missing coordinate names

Use saved example files. Do not label a new file as old data.

For a file marked `eye_tracker_space`:

- [ ] Gaze appears for a data-quality check.
- [ ] Automatic stair alignment does not run.
- [ ] The summary says the gaze uses old tracker-local coordinates when a target is present.

For a file with an empty gaze or target frame name:

- [ ] The current old-data rule allows alignment when every other requirement passes.
- [ ] Live calibration or saving a calibration requires explicit confirmation
  when coordinate details are missing, and the session details record that
  confirmation.

For a file with two different nonempty frame names:

- [ ] Alignment is blocked, and live status shows the mismatch.

Changing the empty-name rule needs a separate compatibility plan and a review of real saved files.

## Test 9: Vicon reconnect and layout change

### Steps

1. Start Vicon marker and segment streams and begin LabRecorder capture.
2. Break and restore the Vicon connection.
3. In another run, add, remove, or reorder a subject or object so the discovered layout changes.
4. Keep recording through recovery.

### Expected result

- Connection loss closes both Vicon streams, disconnects, waits, and reconnects.
- Source IDs stay the same after recreation.
- Timestamps always increase within the recovered logical stream.
- The bridge finds layout changes on its 100-frame check.
- A change in either layout recreates both streams.
- The new value order follows the new Vicon discovery order.
- Empty layouts stay healthy and create no LSL stream.
- LabRecorder handles the recreated stream by source ID the same way as the known-good run.

### Save

- [ ] Bridge state and log order.
- [ ] Full stream details before and after.
- [ ] Source-ID comparison.
- [ ] Timestamp order analysis.
- [ ] XDF stream and layout review.

## Test 10: stream identity, recording controls, shutdown, and file checking

### Steps

1. Start the packaged desktop app. First leave an external graphical recorder
   running at the configured address and confirm automatic launch does not
   create a duplicate. Repeat with no recorder at that address and confirm the
   launched process is shown as **Started here**. The first recorder should be
   shown as **External**.
2. Start Vicon and HoloLens streams after the graphical recorder is already open.
   Select **Find LSL Streams** and bind required roles by source ID.
3. Start a second harmless publisher with a duplicate display name but different
   source ID. Confirm the role becomes visibly ambiguous until an identity is
   selected or **Follow by name** is deliberately enabled.
4. Recreate one publisher with the same source ID. Confirm the newest recovered
   instance is selected predictably and the recovery is reported.
5. Enter valid filename fields, test **Find Next Run**, and wait for the delayed
   filename update. Confirm the displayed final path is the exact remote
   command path.
6. In **Record every visible stream** mode, select **Check Setup** and Start. Confirm the
   immediate refresh includes streams that appeared after recorder startup.
7. Stop normally and wait for the recording file check.
8. Repeat in exact-selection mode, excluding the duplicate/unrelated publisher.
   Confirm the command-line recorder receives only the chosen identity queries.
9. Exercise a warning-only setup check, a blocked setup check, recorder-only mode,
   and one reasoned **Record Anyway** override.
10. Repeat close before Start is sent, during each Start command, while Recording,
    during Stop, and after recorder disconnect. Include bridge reconnect and a
    deliberately delayed preview search/details phase when the test harness is
    available.

### Expected result

- Checking the recorder address prevents a duplicate launch. The displayed
  start source remains correct through launch failure, exit, reconnect, detach,
  and close.
- All-visible Start sends `update`, `select all`, `filename`, and `start` in that
  order. Exact selection launches the packaged command-line recorder with only
  the frozen selected identities.
- Duplicate names never produce an unexplained choice. Same-source recovery uses
  the newest instance; a source-ID collision across hosts remains distinct.
- The saved filename exactly matches the final destination display and
  session details. Traversal, reserved names, unwritable paths, and unconfirmed
  collisions remain blocked.
- A normal Stop reply arrives before state becomes `Stopped`.
- Double Start and Stop produce one operation. Close cancels an unsent Start or
  makes one final Stop follow any Start that may have reached the server.
- Closing remains responsive and shows the component delaying shutdown. The
  four-second bridge, two-second preview/file, and 15-second recorder limits are
  visible results; the window does not destroy a still-running worker.
- Final close may end only a recorder process started by the desktop app and only
  after Stop settles or its deadline expires. An external process is never ended,
  including after connection loss.
- The file check reports expected source and channel layout, time range, sample
  count, duration, measured rate, gaps, clock corrections, and repaired
  timestamps as **Checked**, **Checked with warnings**, or **Needs attention**.
  It never edits the XDF. Automatic run increment occurs only under the selected
  completion policy after the file exists.

### Save

- [ ] Remote-command transcript or test-server log.
- [ ] All-visible and exact-selection inventories, identity bindings, duplicate
  warning, and recovered-instance result.
- [ ] Final XDF path, recorded stream list, and file-check report.
- [ ] Setup-check required/warning/information results and the recorder-only or
  override reason stored in the session details.
- [ ] Persistent dashboard pictures during Starting, Recording, Stopping, and
  file check, including destination, who started the recorder, rates, storage,
  and drop counters.
- [ ] Normal Stop and every pending-Start/close/disconnect result with component
  transition times and deadline outcomes.
- [ ] Outside-process result proving the desktop app does not close a LabRecorder it did not start.

## Final checklist

- [ ] Setup record is complete.
- [ ] Known-good and changed runs use comparable setups.
- [ ] Automated checks pass first.
- [ ] Gaze and target channel layouts and stream details match exactly.
- [ ] Vicon layouts, source IDs, and timestamps remain compatible.
- [ ] Gaze starts only with permission, a spatial node, and exact 90 Hz.
- [ ] Steady timing and rate calculations are saved.
- [ ] Stream identity, duplicate-name, same-source recovery, and source-ID
  handling of duplicate IDs is visible and predictable.
- [ ] Overload creates gaps instead of delayed replay.
- [ ] Tracker restart keeps resources and sessions separate.
- [ ] Invalid gaze and target states keep fixed layouts and invalid values.
- [ ] Live and recorded stair alignment match the known-good direction and scale.
- [ ] A complete saved calibration and persistent quality evidence are saved.
- [ ] Old, missing, and different coordinate names follow current rules.
- [ ] Vicon reconnect and layout changes remain recordable.
- [ ] Recorder command order, both selection policies, exact path, pending-Start
  close, who started the recorder, and time-limited shutdown behavior match the
  documented behavior.
- [ ] Setup-check and post-recording file-check evidence is included in the
  session details.
- [ ] Unity, desktop, and recorder logs contain no new unhandled errors.
- [ ] XDF files, stream details, logs, calculations, and visual records are saved with the change.
- [ ] No third-party submodule file or revision changed.

## Stop and ask for a decision when

Stop the review when one of these items is missing or disputed:

- The real Unity scene, prefab wiring, or saved-asset owner. This repository does not store the whole scene.
- An approved measured Vicon pose for the stair target. The current value is the best fixed estimate, not a universal measurement.
- An approved device drop-rate limit beyond the current preview warning below 80% of the expected rate. This repository does not define a release-grade maximum drop rate.
- Proof that `SystemRelativeTime.Ticks` is still a raw QPC count for the exact device runtime and SDK version.
- Expected LabRecorder behavior when a HoloLens or Vicon stream returns with the same source ID in the included LabRecorder revision.
- A saved `eye_tracker_space` file for old-data checks.

Do not settle these questions by quietly changing code or expected results during cleanup.

## Main source files

- `README.md`
- `hololens-gaze-lsl/README.md`
- HoloLens scripts under `hololens-gaze-lsl/Assets/Scripts`
- Device-independent C# checks under `hololens-gaze-lsl/Tests`
- Desktop bridge, preview, GUI, and checks under `vicon-lsl-bridge`
- Stream definitions under `stream-contracts`
- `.github/workflows/build-bridge.yml`
