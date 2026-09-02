# How time and coordinates work

## Why this guide exists

Time and coordinate changes can make streams look valid while placing samples at the wrong moment or in the wrong place. This guide records the current formulas, units, backup rules, and alignment steps.

Changing a formula or metadata value here is a behavior change. Review it separately from code cleanup.

## Time terms

| Term | Meaning in this project |
| --- | --- |
| LSL local clock | The steady timer returned by `lsl::local_clock()` or `LSL.LSL.local_clock()` on the computer or device that sends the stream |
| Vicon receipt time | The desktop LSL clock read just after a successful `GetFrame()` |
| Vicon pipeline delay | `GetLatencyTotal().Total`; Vicon's estimate of processing delay, not network delay or an exact hardware capture time |
| HoloLens system-relative time | `SystemRelativeTime.Ticks` from the gaze reading, treated here as the device's high-resolution Windows timer count, called QPC |
| Device QPC rate | `System.Diagnostics.Stopwatch.Frequency` on the HoloLens |
| Corrected live time | A live input timestamp after LSL corrects the clock difference between computers |
| XDF stream time | The time written by the source before the recorder's saved clock correction |
| XDF recorder time | Stream time plus the fitted clock correction saved in XDF |
| Playback time | Corrected time from the chosen main stream, starting at zero |

## Vicon timestamps

### Choose a candidate time

After a successful Vicon `GetFrame()`:

1. Read `receipt = lsl::local_clock()` at once.
2. Read `GetLatencyTotal()`.
3. If the delay read succeeds and the value is finite and not negative, use:

   `candidate = receipt - latency_seconds`

4. If the delay is invalid, negative, not finite, or makes a non-finite result, use:

   `candidate = receipt`

5. If `receipt` is not finite, the candidate is `NaN`. The next step can recover only when its separate backup receipt is finite.

This is only an estimate of acquisition time. It does not include `ServerPush` transport or network delay. Do not describe it as an exact capture timestamp.

### Keep time moving forward

`enforceViconTimestamp` follows these rules:

1. If the candidate is not finite, replace it with the supplied receipt time.
2. If the result is still not finite, reject the frame.
3. If there is no earlier timestamp, accept it.
4. If the new value is equal to or earlier than the last value:
   - Use a finite receipt time when it is later than the last value.
   - Otherwise, use the next possible floating-point number after the last value with `nextafter(previous, +infinity)`.
5. Reject the frame only if the repaired value is not finite.
6. Save the accepted value and report whether it needed repair.

The timestamp state stays outside the reconnect loop. The Vicon source IDs also stay the same. Together, these rules stop time from moving backward when LabRecorder joins a recreated stream to its earlier copy.

### Keep markers and segments together

Marker and segment samples from one Vicon frame use the same accepted timestamp. Individual objects do not get separate times. A hidden or failed object keeps the frame time but uses an invalid fixed-size value.

### Vicon time metadata

Both Vicon streams use these exact values:

- `timestamp = estimated_acquisition_time`
- `clock_domain = lsl_local_clock`
- `timestamp_estimator = immediate_receipt_minus_valid_pipeline_latency`
- `timestamp_fallback = immediate_receipt_time`
- `latency_correction = GetLatencyTotal_pipeline_estimate`
- `timestamp_accuracy = acquisition_estimate_not_capture_accurate`
- `synchronization/timestamp_origin = local_receipt_minus_valid_vicon_pipeline_latency`

Keep the code, emitted LSL metadata, and documentation in agreement.

## HoloLens gaze timestamps

### Convert the device time

This project treats `EyeGazeTrackerReading.SystemRelativeTime.Ticks` as the raw count from the Windows high-resolution timer, QPC, even though the SDK exposes it through a `TimeSpan` value.

The conversion is:

`lsl_timestamp_seconds = system_relative_ticks / Stopwatch.Frequency`

Do not use `TimeSpan.TicksPerSecond`. Do not replace the capture time with Unity frame time, wall-clock time, or `LSL.local_clock()` at the time of retrieval.

The production code and device-independent checks both read the SDK count directly in the device's `Stopwatch.Frequency` time base. There is no second rate conversion.

### Ask for a current reading

The publishing side asks the SDK for a reading at:

`TimeSpan.FromTicks(Stopwatch.GetTimestamp())`

A reading may pass only when:

- `capture_ticks > 0`.
- `query_ticks > 0`.
- It is no more than 25 ms old.
- If its time is slightly ahead, it is no more than 1 ms ahead.

`GazeReadingGate` then accepts only a timestamp later than the last accepted timestamp in the same tracker session. It drops duplicates and earlier values. The gate resets when the tracker session changes.

### Limit queued data

The raw queue and the world-space queue each allow at most 360 items and at most 25 ms between oldest and newest capture times.

When adding an item:

1. Remove oldest items while the queue already has 360 items.
2. If the new item makes the time span negative or greater than 25 ms, clear the queue.
3. Add the new item.

Before sending a world-space sample, reduce an over-limit queue to its newest item.

This creates a clear timestamp gap during overload. Do not replay old samples, fill in missing samples, or give old samples a new current time.

### Publishing schedule and errors

- The worker schedules one step every `1000 / nominal_rate` ms. The current rate is exactly 90 Hz.
- After a missed schedule, move to the next interval from the current time. Do not run an unlimited catch-up loop.
- Send a finite, positive `GazeSample.Timestamp` to `push_sample` without changing it.
- Drop a sample with a bad time, but continue the schedule.
- A provider exception does not create a sample time.
- Retry brief provider errors. Restart the tracker after back-to-back errors last about one expected second.

### Gaze time metadata

The gaze stream uses:

- `timestamp = sdk_system_relative_time`
- `timestamp_units = seconds`
- `timestamp_conversion = system_relative_qpc_ticks_divided_by_runtime_qpc_frequency`
- `timestamp_tick_frequency_hz = Stopwatch.Frequency`, written without locale-specific formatting
- `capture_clock_domain = windows_qpc_system_relative`
- `clock_domain = lsl_local_clock`
- `synchronization/timestamp_origin = eye_gaze_tracker_system_relative_time`
- `timestamp_mapping = direct_qpc_ticks_to_lsl_local_clock_seconds`

This assumes that the Windows steady timer used by liblsl is backed by the same QPC time base. Repeat the hardware checks after changing Windows runtime, Unity, OpenXR, or liblsl versions.

## HoloLens target timestamps

The Vuforia target output reads `LSL.local_clock()` in `LateUpdate`, just before it reads, encodes, and sends the current target pose.

It uses:

- `timestamp = lsl_local_clock_at_transform_read`
- `clock_domain = lsl_local_clock`
- `synchronization/timestamp_origin = local_clock_at_transform_read`

The target has no SDK capture time and uses an irregular rate. Gaze and target share one clock, but the target time is less precise about the true capture moment.

## Live preview time

### Correct clocks once

Every live LSL input calls:

`set_postprocessing(lsl::post_clocksync)`

`pull_sample` therefore returns times corrected to the preview computer's local clock. Do not apply the XDF clock fitting rules to live data.

### Read and mark fresh data

- Read at most 16 ready samples from each stream in one pass and keep the newest.
- Keep a stream fresh for 500 ms after its latest read.
- Treat connection, sample presence, and freshness as separate states.

### Build one preview frame

The default matching limit is 50 ms, and users may change it.

When a marker sample arrives:

- Use its time for the frame.
- Always parse its marker data.
- Include segment and gaze data only when the stream is fresh and:

  `abs(marker_time - other_time) <= tolerance`

When no marker arrives but segment or gaze updates:

- If both update and are fresh, use the later time.
- Otherwise, use the time from the one fresh stream that updated.
- Include a stream only when its latest time is within the limit of that chosen time.

This is a nearest-current visual match. It does not create new samples between real samples.

### Show the live rate

The preview measures rate from corrected sample times over a two-second window:

- Ignore non-finite and exact duplicate times.
- If time moves backward, clear the window before adding the new time.
- Keep the newest sample at or before the window start so small timing changes do not shorten the full window.
- Do not show a rate until at least two samples cover two seconds.
- Calculate `(sample_count - 1) / elapsed_seconds`.
- Mark gaze as low only after a full window and only below 80% of a positive, finite expected rate.
- Do not keep showing an old rate after the stream becomes stale.

For a 90 Hz gaze stream, the current low-rate line is 72 Hz.

## XDF preview time

### Read sample times

- An explicit time may use a 32-bit or 64-bit floating-point value.
- A missing time becomes `previous + 1 / nominal_srate`.
- A missing time is an error when there is no previous time or the expected rate is not positive and finite.
- A non-finite sample time is an error.

### Fit saved clock corrections

Each XDF clock-offset record contains:

- The measurement time in the source stream's clock.
- The amount to add to reach recorder time.

Sort these measurements by stream time. Reject two measurements with the same time.

Fit a straight line around the center of the values:

`offset(t) = offset_center + slope * (t - stream_center)`

Then apply:

`corrected_time = stream_time + offset(stream_time)`

With one measurement, or when all measurement times are the same, use slope zero and a constant correction.

### Repair time that moves backward

After correction, keep sample order and force times to increase:

1. Keep a running shift.
2. Add that shift to the next corrected time.
3. If the result is equal to or earlier than the last output, replace it with `nextafter(previous, +infinity)`.
4. Add the repair amount to the running shift so later samples keep their spacing from the repaired line.

Count repaired values and include the count in the recording summary.

### Choose the main stream and playback time

Choose the first usable stream in this order:

1. `ViconMarkers` role.
2. `ViconSegments` role.
3. Any numeric stream with `Vicon` in its name, ignoring letter case.
4. `HoloLensGaze` role.
5. Any numeric stream.

For every main-stream time, use binary search to find the closest sample from another stream. Check both the first sample at or after that time and the sample just before it. Accept the closest only when it is within the chosen time limit.

Keep corrected full times in `XdfStreamData.timestamps` for matching. Show each frame at:

`master_absolute_time - first_master_absolute_time`

Playback therefore starts at zero while matching still uses the full corrected times.

The XDF path applies saved clock correction itself. It must not also use live LSL clock correction.

## Merged CSV time

For each row:

1. Use finite `relative_time` when present.
2. Otherwise, use `lsl_time - first_finite_lsl_time` when `lsl_time` is finite.
3. Otherwise, use the output row number starting at zero.

The CSV reader does not fit clock differences or match separate streams. It assumes the rows were already merged.

## Coordinate terms

| Space | Units and direction rules |
| --- | --- |
| Vicon stream | Global Vicon positions in millimetres; segment rotation in the order sent by the SDK |
| Unity world | Unity scene coordinates, which use a left-handed system |
| Eye-tracker ray | A ray from Extended Eye Tracking, described in code as right-handed |
| Published HoloLens shared world | The Unity world reflected into a right-handed system, in metres, named `hololens_stationary_shared_with_gaze` |
| Old tracker space | `eye_tracker_space`; it lacks the changing pose needed for stair alignment |
| Preview display | One metre-scale scene after the fixed Vicon scale and, when one exists, the solved HoloLens transform |

## Show Vicon data in the preview

The default Vicon preview scale is `0.001`. A position `(x, y, z)` in millimetres becomes `(0.001x, 0.001y, 0.001z)` in metres.

Marker and segment positions then use this order:

1. Apply the sign chosen for each input axis.
2. Apply one scale.
3. Apply the enabled four-number rotation, called a quaternion. Otherwise, apply Euler X, then Y, then Z rotation.
4. Add translation.

The preview currently copies segment quaternion values directly from the LSL sample. It does not combine them with the position transform. Because the default profile only scales positions, the Vicon orientation stays unchanged. Changing this is a coordinate behavior change.

## Convert HoloLens gaze on the device

For each usable eye ray:

1. Check that origin, direction, poses, and rotations are finite and usable. Check that direction is not zero.
2. Reflect origin and direction across Z to move from tracker coordinates into Unity tracker coordinates:

   `F(x, y, z) = (x, y, -z)`

3. Rotate by the `playspaceFromTracker` rotation and add its position to the origin.
4. Apply the Unity world/playspace scale to each component.
5. Rotate by `worldFromPlayspace` and add its position to the origin.
6. Normalize the direction after scale and world rotation.
7. Reflect origin and direction across Z again to publish the right-handed world shared with the target.
8. Normalize the final direction.

Find the device pose at the original gaze capture time. Using the current Unity frame time would change behavior.

## Convert the Vuforia target

For a tracked Unity pose:

- Position `(x, y, z)` becomes `(x, y, -z)`.
- Quaternion `(x, y, z, w)` becomes `(-x, -y, z, w)`.

This is the rotation-basis change `F R(q) F` where `F = diag(1, 1, -1)`. Gaze and target therefore publish the same right-handed world name.

When the target is not tracked, send `NaN` for all seven pose values and zero for the tracked value.

## Preview transforms and stair alignment

### Gaze transform without a calibration

There is no hand-entered gaze transform. The pose of the HoloLens world in Vicon
coordinates cannot be known before it is measured, so a session with no solved or
applied calibration uses an identity gaze transform:

- Scale is `1.0`, because gaze is already in metres.
- Axis signs are `(1, 1, 1)`.
- There is no rotation and no translation.

Gaze is therefore drawn in the published `hololens_stationary_shared_with_gaze`
frame. It is displayed, but it is not aligned to Vicon, and the calibration state
reads **Not calibrated**.

`gazeTransformForCoordinateFrame` currently returns the supplied transform without changing it. Code may simplify this private work, but the public wrapper must stay when source compatibility needs it. Changing the result needs coordinate checks.

### Decide whether gaze and target can align

Compare coordinate-frame names without letter-case differences:

- `eye_tracker_space` gaze is never compatible.
- If either frame name is empty, allow alignment for older data.
- Otherwise, both names must match.

Changing the empty-name rule may stop older streams from aligning. Review it separately and check real saved files.

### Fixed stair settings

The current stair settings are:

- ID: `stair-model-v1`.
- Required samples: `20`.
- Allowed position movement: `0.02 m`.
- Allowed rotation movement: `3 degrees`.
- Fixed `vicon_from_target` position: `(-2.853343307500, 0.292672723112, 0.006432986454)`.
- Fixed target rotation: identity.

The stair OBJ file uses millimetres. Scale it by `0.001`, then apply the fixed rotation and position to place it in the preview's metre space.

### Find a stable target pose

- Compare each tracked pose with the first pose in the current set.
- Clear the set when tracking is lost.
- If movement is outside either limit, clear the set and start again with the new pose.
- Average finite positions and normalized rotations. Flip equivalent quaternion signs into the same half before averaging.
- Require at least 20 usable poses.
- Require both position and rotation RMS values to stay within their limits.
- Use the same rules when searching an XDF recording for a stable window.

### Build the gaze-to-Vicon transform

The current calculation:

1. Inverts the averaged `holo_from_target` pose.
2. Combines the fixed `vicon_from_target` pose with a 180-degree rotation around target Z, stored as quaternion `(0, 0, 1, 0)`.
3. Reflects the target-to-HoloLens position and rotation as implemented in `gazeTransformFromTargetCalibration`.
4. Creates a quaternion-based HoloLens preview transform.
5. Sets the gaze input Z sign to `-1` to keep the stair model's current target-local direction rule.

The fixed pose, extra Z rotation, reflection, and input Z sign work as one set. Removing only one can mirror or reverse gaze relative to the stairs.

Automatic alignment lasts only for the current preview session. It is not saved.
There is no fallback transform: clearing a calibration, or a solve that fails its
quality limits, returns the preview to the identity gaze transform above.

## Match live and recorded geometry

Given the same source values, live and XDF preview paths should show the same geometry after allowing for their different time origins:

- Marker names, valid states, and metre positions match.
- Segment names, valid states, metre positions, and raw rotations match.
- Gaze ray names, valid states, origins, and normalized directions match.
- Gaze and target with the shared-world name can use the same stair alignment.
- `eye_tracker_space` gaze never uses automatic target alignment.

Do not expect live and XDF `PreviewFrame.timestamp` numbers to match. Live time stays in the corrected local clock. XDF playback starts from zero. Their matching decisions should still agree for equivalent corrected source times and the same limit.

## Checks for a time or coordinate change

Existing checks cover Vicon time, preview matching, transforms, stair alignment, and old frame names. They also cover XDF clock repair, playback, live rate, HoloLens time conversion, reading age, queue limits, and published times.

Before merging a time or coordinate change:

- [ ] Saved Vicon cases cover good, negative, non-finite, overflow, equal, and earlier candidate times.
- [ ] Reconnect proves Vicon time keeps increasing across stream recreation with stable source IDs.
- [ ] Marker and segment timestamps from one frame are bit-for-bit equal.
- [ ] Device evidence records `Stopwatch.Frequency`, raw reading counts, converted LSL times, and local LSL times.
- [ ] Device recording has no duplicate or earlier gaze times and shows gaps instead of delayed replay during overload.
- [ ] Live and XDF paths each correct clocks exactly once.
- [ ] Constant and changing XDF offsets match saved fitted values.
- [ ] Repair count and repaired times match saved results.
- [ ] Matching checks cover exactly at, just inside, and just outside the time limit.
- [ ] Simple axis vectors and rotations prove both HoloLens reflections.
- [ ] An uncalibrated session leaves gaze in its published frame instead of applying a guessed transform.
- [ ] Fixed stair alignment works for known fake data and the physical model.
- [ ] Old, empty, matching, and different frame names follow current rules.
- [ ] Live and XDF preview geometry matches for the same values.

Use the [hardware test guide](device-parity-runbook.md) for device evidence.

## Main source files

- `README.md`
- `hololens-gaze-lsl/README.md`
- `vicon-lsl-bridge/src/ViconClient.cpp`
- `vicon-lsl-bridge/src/ViconFrameMapper.*`
- `vicon-lsl-bridge/src/MarkerStream.cpp`
- `vicon-lsl-bridge/src/SegmentStream.cpp`
- `vicon-lsl-bridge/src/gui/PreviewStreamWorker.cpp`
- `vicon-lsl-bridge/src/preview/PreviewRate.*`
- `vicon-lsl-bridge/src/preview/PreviewMath.*`
- `vicon-lsl-bridge/src/preview/PreviewParsing.*`
- `vicon-lsl-bridge/src/preview/PreviewCalibration.*`
- `vicon-lsl-bridge/src/preview/PreviewXdf*`
- `vicon-lsl-bridge/src/preview/PreviewCsv.cpp`
- `hololens-gaze-lsl/Assets/Scripts/GazeTiming.cs`
- `hololens-gaze-lsl/Assets/Scripts/GazeDataProvider.cs`
- `hololens-gaze-lsl/Assets/Scripts/GazePublisherWorker.cs`
- `hololens-gaze-lsl/Assets/Scripts/GazeLSLOutlet.cs`
- `hololens-gaze-lsl/Assets/Scripts/GazeCoordinateTransform.cs`
- `hololens-gaze-lsl/Assets/Scripts/ModelTargetPoseEncoder.cs`
- `hololens-gaze-lsl/Assets/Scripts/VuforiaModelTargetPoseOutlet.cs`
