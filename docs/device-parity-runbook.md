# HoloLens and integrated-system parity runbook

## Purpose

The repository's automated managed tests do not compile or execute Unity, WinRT, Microsoft Extended Eye Tracking, Mixed Reality OpenXR, Vuforia, UWP ARM64 liblsl, or physical Vicon hardware behavior. This runbook defines the evidence required when a refactor touches those boundaries.

It is a parity procedure, not a feature acceptance plan. The expected result is the behavior documented in:

- [behavior-contract.md](behavior-contract.md)
- [runtime-state-machines.md](runtime-state-machines.md)
- [time-and-coordinate-semantics.md](time-and-coordinate-semantics.md)

Do not use this runbook to approve a dependency upgrade, stream schema change, coordinate change, or revised timing policy. Those require a separate migration plan and new expected results.

## When this runbook is required

Complete all applicable sections when changing any of the following:

- `GazeDataProvider`, `GazePublisherWorker`, `GazeLSLOutlet`, `GazeTiming`, or `GazeCoordinateTransform`.
- `VuforiaModelTargetPoseOutlet`, `ModelTargetPoseEncoder`, target-pose metadata, or stair calibration.
- Public/serialized Unity component or configuration fields.
- UWP ARM64 liblsl build, managed LSL binding, Unity version, Mixed Reality OpenXR, Extended Eye Tracking SDK, or Vuforia version.
- HoloLens source IDs, stream names, channel schema, nominal rate, clock metadata, or coordinate-frame metadata.
- Desktop live preview resolution, clock synchronization, rate diagnostics, or calibration compatibility.
- XDF offset processing or automatic offline calibration.
- Integrated LabRecorder startup, selection, recording, or outlet recovery.

Pure desktop changes may mark device-only sections not applicable, but must state why.

## Required equipment and environment

- HoloLens 2 with working Extended Eye Tracking hardware and permission available to the application.
- The actual Unity application/project that integrates the scripts in this repository. The repository contains scripts and tests, not a complete serialized Unity scene.
- Microsoft Mixed Reality OpenXR 1.5.1 or later, matching the repository README requirement.
- Microsoft Extended Eye Tracking SDK integration used by the application.
- Vuforia Model Target setup for the physical stair target when testing calibration.
- A `GazeLSLConfig` asset assigned to both producers.
- Desktop machine on the same LSL-visible network.
- Vicon DataStream server and tracked subjects/objects for integrated tests.
- Desktop GUI and LabRecorder build produced from the revision under test.
- Physical stair model in the surveyed/expected location for calibration checks.
- A way to retain Unity device logs, desktop logs, `.xdf` recordings, stream metadata, and screenshots/video of the preview.

## Test record header

Create one record per run and fill every field:

| Field | Value |
| --- | --- |
| Repository revision | |
| Comparison/baseline revision | |
| Refactor pass under test | |
| Date/time/time zone | |
| Operator | |
| HoloLens model/OS build | |
| Unity editor/runtime version | |
| Scripting backend/API compatibility level | |
| Mixed Reality OpenXR version | |
| Extended Eye Tracking SDK version | |
| Vuforia version | |
| UWP liblsl revision | |
| Desktop liblsl revision | |
| LabRecorder revision/liblsl revision | |
| Vicon server/software/SDK version | |
| `Stopwatch.Frequency` observed on device | |
| Gaze stream name/type/source ID | |
| Target stream name/type/source ID | |
| Vicon marker/segment stream names | |
| Physical stair pose/reference notes | |
| Recording filenames and log locations | |

If the baseline and test runs do not use the same environment, list every difference. Do not attribute an observed difference to refactoring until environment drift has been ruled out.

## Pre-device automated baseline

Run from repository root before device deployment:

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

For the full desktop environment, also configure/build with the initialized Vicon SDK submodule, liblsl, and Qt6, then run all registered CTest tests from that build.

Record:

- [ ] Generated contracts are current.
- [ ] Dependency-light C++ suite passes.
- [ ] Platform-neutral managed suite passes.
- [ ] Full desktop runtime/Qt suite passes when applicable.
- [ ] Working tree contains no unexpected source, vendor, or generated changes.

## Deployment preflight

1. Confirm the Unity scene has one active `GazeDataProvider` and `GazeLSLOutlet` wired to the intended `GazeLSLConfig`.
2. For calibration, confirm one `VuforiaModelTargetPoseOutlet` uses the same configuration asset and the intended `ObserverBehaviour`/model target.
3. Confirm the gaze provider and target outlet share the same Unity/XR world.
4. Confirm gaze permission is granted on the device.
5. Confirm the eye tracker advertises an exact 90 Hz target frame rate.
6. Confirm the desktop and HoloLens can discover each other's LSL traffic through the current network/firewall configuration.
7. Confirm the desktop GUI points to the intended Vicon server and stream names.
8. Confirm LabRecorder RCS is enabled on the configured host/port.
9. Confirm the study root exists and filename fields render the expected `.xdf` path.
10. Capture the actual configuration values and Unity inspector wiring as evidence.

Do not continue to calibration if the HoloLens producers are in different scene frames or the target-pose component is attached to a different world origin.

## Scenario 1: permission, tracker selection, and startup

### Procedure

1. Start the Unity application from a clean launch.
2. Observe permission handling and device logs.
3. Keep the device and application active until tracker enumeration completes.
4. Discover the gaze stream on the desktop.
5. Repeat once after fully stopping and relaunching the application.

### Expected behavior

- Permission denial or unavailable device support logs an error and produces no partial gaze outlet.
- A tracker without exact 90 Hz support logs an error and does not start the outlet.
- A valid tracker opens, selects 90 Hz, creates a spatial graph node, increments session generation, and logs readiness.
- The outlet appears only after `TryGetEffectiveFrameRate` reports the active 90 Hz session.
- There is one gaze outlet with the configured identity, not a desktop-relayed duplicate.

### Evidence

- [ ] Unity log from launch through readiness or expected failure.
- [ ] Stream-discovery screenshot/metadata export.
- [ ] Repeat-launch result matches the first launch.
- [ ] No stale outlet remains after the application exits.

## Scenario 2: exact stream schemas and metadata

Inspect full LSL metadata for all available streams and compare with [behavior-contract.md](behavior-contract.md).

### Gaze acceptance

- [ ] Configured name, type, and source ID match.
- [ ] Channel format is `double64` and nominal rate is `90`.
- [ ] Exactly 21 channels appear in generated-contract order.
- [ ] Labels and units exactly match `stream-contracts/hololens-gaze.json`.
- [ ] `coordinate_frame` is `hololens_stationary_shared_with_gaze`.
- [ ] Timestamp, QPC-frequency, clock-domain, and backlog metadata match the documented values.

### Target acceptance

- [ ] Configured name, type, and source ID match.
- [ ] Channel format is `double64`, nominal rate is irregular/zero, and there are exactly eight channels.
- [ ] Labels and units match the target-pose contract.
- [ ] Coordinate frame matches gaze exactly.
- [ ] Timestamp metadata identifies local clock at transform read.

### Vicon acceptance

- [ ] Marker/segment names and `MoCap` type match configuration.
- [ ] Channel order follows current Vicon discovery order.
- [ ] Units, nominal frame rate/fallback, source IDs, and timestamp metadata match the contract.
- [ ] Marker and segment source IDs retain their hostname suffix across reconnect.

Retain the raw metadata export, not only screenshots.

## Scenario 3: steady gaze timing and rate

### Procedure

1. Start HoloLens gaze in a stable, low-load scene.
2. Start desktop live preview and wait at least ten seconds.
3. Record at least 60 seconds in LabRecorder.
4. Move gaze naturally so changing samples are observable.
5. Save Unity logs, preview status, and XDF.

### Expected behavior

- Gaze nominal rate is 90 Hz.
- After the two-second measurement window fills, live preview reports an effective rate based on corrected timestamps.
- Under the approved baseline conditions, the refactored run has the same low-rate/no-low-rate classification as the baseline. The current UI threshold is less than 80% of nominal, or 72 Hz for a 90 Hz stream.
- Published gaze timestamps are finite, positive, and strictly increasing.
- The encoded timestamp follows the SDK reading's system-relative time and is not equal to a repeating Unity render timestamp.
- Normal batching does not cause old readings to replay behind current Vicon motion.
- XDF contains clock-offset chunks appropriate to normal LSL synchronization.

### Analysis record

For the gaze stream, calculate and retain:

- Sample count and duration.
- Minimum, median, 95th percentile, and maximum timestamp interval.
- Effective rate over the complete recording and representative two-second windows.
- Count of duplicate or regressing timestamps; expected zero.
- Count and duration of gaps greater than 25 ms.
- Recorded `Stopwatch.Frequency` and a sample of raw reading tick-to-seconds calculations when instrumentation is available.

Do not set a new allowable drop-rate threshold during a refactor. Compare with the approved baseline under the same scene and device conditions, and report any material difference.

## Scenario 4: overload and backlog policy

### Procedure

Use a controlled, reversible load that delays the Unity main-thread transformation consumer or otherwise creates a processing backlog without modifying the timestamp/queue implementation under test. Record the exact load method.

1. Record a stable pre-load interval.
2. Apply the load long enough to exceed a 25 ms capture-time span.
3. Remove the load and allow recovery.
4. Inspect published timestamps and gaze/Vicon visual alignment.

### Expected behavior

- When either queue exceeds the capture-span budget, older entries are discarded and the newest is retained.
- The recording contains an explicit timestamp gap.
- Recovery resumes near current motion; it does not rapidly replay a burst of old captures.
- Timestamps remain strictly increasing.
- Queue overload does not change the fixed 21-channel schema.
- Preview may report a reduced effective rate but must not display an old completed rate after the stream becomes stale.

### Evidence

- [ ] Exact load procedure and time interval.
- [ ] Before/during/after timestamp plot or interval table.
- [ ] No duplicate/regression count.
- [ ] Video or synchronized plot demonstrating no delayed replay.
- [ ] Baseline and refactored results use the same load.

## Scenario 5: transient and persistent tracker failure

Use available application focus/suspend, tracker-session, or controlled fault mechanisms appropriate to the actual Unity project. Do not physically or programmatically alter device state in a way that cannot be reproduced safely.

### Transient case

- Cause a brief projected WinRT read failure shorter than the persistent recovery threshold.
- Expected: warning count increases, publishing remains active or briefly gaps, and the tracker is not immediately replaced.

### Persistent case

- Cause continued provider failure for approximately one nominal second.
- Expected: worker exposes provider failure, outlet stops only after the worker exits, provider restarts watcher/tracker enumeration, and publishing later resumes with a new session generation.

### Removal/re-enumeration case

- Trigger a supported tracker removal/re-enumeration or application lifecycle equivalent.
- Expected: old queues and reading gate are cleared; late asynchronous completion from the old lifecycle cannot become active; no old-generation sample appears in the new session.

### Acceptance

- [ ] Transient failures do not cause rapid outlet churn.
- [ ] Persistent failure produces one controlled recovery sequence.
- [ ] Outlet resources are not disposed while the old worker can still push.
- [ ] Recreated outlet retains configured name/type/source ID.
- [ ] Recorded timestamps do not move backwards across recovery.
- [ ] LabRecorder behavior across outlet recreation matches the approved baseline.
- [ ] Unity logs show no unhandled exception or repeated overlapping restart.

## Scenario 6: ray validity and target tracking validity

### Gaze

Exercise combined gaze and, where supported, left/right gaze across valid and invalid tracking conditions.

- [ ] Valid ray origin/direction values are finite and direction magnitude is approximately one.
- [ ] Valid flags are numeric `1.0` only when the corresponding transformed ray is usable.
- [ ] Unsupported or invalid individual eyes retain their fixed channels with invalid flag, rather than changing schema.
- [ ] Failed spatial location produces invalid ray values for that capture rather than a retimestamped pose.

### Target

Acquire and then lose Vuforia target tracking.

- [ ] `TRACKED` and `EXTENDED_TRACKED` produce finite reflected position/quaternion and `Tracked = 1.0`.
- [ ] Other statuses produce seven `NaN` values and `Tracked = 0.0`.
- [ ] Losing target clears live calibration collection.
- [ ] Regaining target starts a fresh stable collection.

Retain representative raw samples for both valid and invalid cases.

## Scenario 7: coordinate-frame and stair calibration parity

### Preparation

1. Place the physical stair target at the baseline location/orientation.
2. Confirm the fixed profile still represents the expected Vicon target pose. The current code uses profile `stair-model-v1` and the fixed translation documented in [time-and-coordinate-semantics.md](time-and-coordinate-semantics.md).
3. Restart the HoloLens application so the stationary world is newly established.
4. Ensure gaze and target metadata both name `hololens_stationary_shared_with_gaze`.

### Live procedure

1. Start Vicon streaming and desktop preview.
2. Acquire the Vuforia stair target and keep it stationary.
3. Start or allow automatic calibration.
4. Hold stable until 20 accepted poses complete.
5. Inspect the reported translation and rotation RMS.
6. Look along known stair edges/steps and compare the combined gaze ray with the physical/Vicon-aligned model.
7. Switch to Manual Transform and then recalibrate.

### Expected behavior

- Target loss or motion beyond 20 mm / 3 degrees restarts collection.
- A stable 20-sample set within both RMS limits creates one automatic session transform.
- Automatic transform is not written to persistent settings.
- `Use Manual Transform` immediately restores persistent manual controls.
- The rendered stair ascent direction and calibrated gaze agree with the approved baseline; there is no X/Z mirror, 180-degree reversal, or metre/millimetre error.
- Recalibration after HoloLens world restart restores alignment.

### Offline procedure

1. Record gaze, target, marker, and segment streams during a stable target window.
2. Load the XDF in the built-in preview.
3. Confirm the stable-window solver applies calibration and reports it in the summary.
4. Compare representative live and offline geometry at corresponding corrected times.

### Evidence

- [ ] Photo/diagram of physical target placement.
- [ ] Live preview screenshot/video before and after calibration.
- [ ] Reported sample count and RMS values.
- [ ] XDF and offline summary.
- [ ] Synthetic basis-vector tests remain passing.
- [ ] Baseline/refactored overlay or side-by-side comparison.

## Scenario 8: legacy and missing coordinate metadata

Use retained fixtures; do not relabel a new recording as legacy.

- Load a recording labeled `eye_tracker_space`.
  - [ ] Gaze is available for acquisition inspection.
  - [ ] Stair-target automatic calibration is not applied.
  - [ ] Summary identifies legacy tracker-local gaze when a target exists.
- Load a fixture with empty gaze or target frame metadata.
  - [ ] Current backward-compatible rule allows calibration when the other requirements pass.
- Load a fixture with two different nonempty frame strings.
  - [ ] Calibration is rejected and live status identifies the mismatch.

Changing the empty-metadata compatibility rule requires a migration and a corpus-based impact analysis.

## Scenario 9: Vicon reconnect and layout recreation

### Procedure

1. Start Vicon marker/segment streaming and LabRecorder capture.
2. Interrupt and restore the Vicon connection.
3. During a separate run, add/remove/reorder a subject or object so discovery changes.
4. Continue recording through recovery.

### Expected behavior

- Connection loss destroys both Vicon outlets, disconnects, waits, and reconnects.
- Source IDs remain stable across recreation.
- Timestamps remain strictly increasing within a recovered logical stream.
- Layout is detected on the current 100-frame check cadence.
- Both streams are recreated when either layout changes.
- New channel order follows new SDK discovery order.
- Empty layouts remain healthy and create no outlet.
- LabRecorder includes recreated streams according to its source-ID recovery behavior and the approved baseline.

### Evidence

- [ ] Bridge status/log transition sequence.
- [ ] Pre/post full stream metadata.
- [ ] Source-ID comparison.
- [ ] Timestamp monotonicity analysis.
- [ ] XDF stream/layout inspection.

## Scenario 10: integrated recording control and shutdown

### Procedure

1. Launch the desktop GUI with the packaged LabRecorder payload when applicable.
2. Confirm automatic process ownership and RCS connection.
3. Start Vicon and HoloLens producers after LabRecorder is already open.
4. Enter valid filename fields and wait for the debounced filename update.
5. Start recording from the GUI.
6. Verify newly visible streams were refreshed and selected.
7. Stop normally; repeat once by closing the GUI while recording.

### Expected behavior

- Start batch order is `update`, `select all`, `filename`, `start`.
- Visible Vicon, gaze, and target streams are selected.
- Filename matches the preview and sanitized fields.
- Normal Stop is acknowledged before recording state becomes `Stopped`.
- Close requests bridge stop and, only when state is `Recording`, recording Stop.
- Close waits according to the documented 4-second bridge and 15-second recording limits.
- Only a GUI-owned LabRecorder process is terminated on final close.

### Evidence

- [ ] RCS transcript or test server log.
- [ ] Final XDF path and stream inventory.
- [ ] GUI readiness/status screenshots.
- [ ] Normal-stop and close-while-recording results.
- [ ] External-recorder case proves the GUI does not terminate an unowned process.

## Final parity checklist

- [ ] Test record header is complete.
- [ ] Baseline and refactored runs use comparable environments.
- [ ] Automated pre-device baseline passes.
- [ ] Gaze and target schemas/metadata are exact.
- [ ] Vicon schemas/source IDs/timestamps remain compatible.
- [ ] Gaze startup requires exact 90 Hz and correct permission/node state.
- [ ] Steady timestamp/rate evidence is retained.
- [ ] Overload produces gaps, not stale replay.
- [ ] Tracker failure/restart preserves ownership and session isolation.
- [ ] Invalid gaze/target states preserve fixed schemas and invalid encodings.
- [ ] Live and offline stair calibration match the baseline orientation and scale.
- [ ] Legacy/missing/mismatched coordinate-frame cases follow current rules.
- [ ] Vicon reconnect/layout recreation remains recordable.
- [ ] LabRecorder command order, selection, filename, and shutdown remain compatible.
- [ ] Unity, desktop, and recorder logs contain no new unhandled errors.
- [ ] XDF, metadata exports, logs, calculations, and visual evidence are archived with the change.
- [ ] No vendor submodule content or revision changed as part of the refactor.

## Stop conditions and unresolved baseline inputs

Stop the review and request a separate decision if any of these is missing or disputed:

- The actual Unity scene/prefab wiring and serialized-asset ownership. It is not stored completely in this repository.
- An approved physical/surveyed Vicon pose for the stair target. The README describes the current value as the best fixed estimate, not a universally surveyed truth.
- An approved device/drop-rate tolerance beyond the existing 80%-of-nominal preview warning. This repository does not define a release-grade maximum drop percentage.
- Evidence that the device SDK's `SystemRelativeTime.Ticks` remains a raw QPC count for the exact runtime/SDK version under test.
- Expected LabRecorder behavior when a HoloLens or Vicon outlet is recreated with the same source ID in the bundled recorder revision.
- A baseline `eye_tracker_space` fixture for legacy compatibility.

Do not resolve these ambiguities by silently changing code or expected results inside a structural pass.

## Evidence sources

- `README.md`
- `hololens-gaze-lsl/README.md`
- Current HoloLens scripts under `hololens-gaze-lsl/Assets/Scripts`
- Platform-neutral managed tests under `hololens-gaze-lsl/Tests`
- Desktop producer, preview, GUI, and tests under `vicon-lsl-bridge`
- Current stream contract under `stream-contracts`
- Current build and package validation in `.github/workflows/build-bridge.yml`
