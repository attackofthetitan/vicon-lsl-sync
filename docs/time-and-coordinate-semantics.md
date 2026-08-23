# Time and coordinate semantics

## Purpose

Synchronization and coordinate conversion are the highest-risk compatibility surfaces in this repository. This document records the current formulas, clock domains, fallback rules, matching policies, units, basis changes, and calibration assumptions.

Changing any formula or metadata value here is a functional migration, even when the code change appears to be a cleanup.

## Clock-domain glossary

| Term | Current meaning |
| --- | --- |
| LSL local clock | The steady clock returned by `lsl::local_clock()` or `LSL.LSL.local_clock()` on the producer host |
| Vicon receipt time | Desktop LSL local clock sampled immediately after a successful `GetFrame()` |
| Vicon pipeline latency | `GetLatencyTotal().Total`; an SDK processing estimate, not network delay and not a capture-accurate hardware timestamp |
| HoloLens system-relative time | The Extended Eye Tracking reading's `SystemRelativeTime.Ticks`, treated by this integration as the device QPC count |
| Runtime QPC frequency | `System.Diagnostics.Stopwatch.Frequency` on the HoloLens device |
| Corrected live inlet time | The timestamp returned by an inlet configured with `lsl::post_clocksync` |
| XDF stream time | Timestamp encoded in the source stream's clock domain before recorded offset correction |
| XDF recorder time | Stream timestamp plus the fitted recorded clock offset |
| Playback time | Corrected master-stream time made relative to its first sample |

## Vicon producer timestamps

### Candidate timestamp

After a successful Vicon `GetFrame()`:

1. Sample `receipt = lsl::local_clock()` immediately.
2. Read `GetLatencyTotal()`.
3. When the latency result is successful, finite, and nonnegative, compute:

   `candidate = receipt - latency_seconds`

4. If latency is invalid, negative, non-finite, or subtraction is non-finite, use:

   `candidate = receipt`

5. If receipt itself is non-finite, the candidate is `NaN` and the monotonic guard may only recover if its separately supplied fallback receipt is finite.

This value is an estimated acquisition time. It does not account for ServerPush transport or network delay and must not be described as capture accurate.

### Strict monotonicity guard

`enforceViconTimestamp` applies these rules:

1. Replace a non-finite candidate with the supplied immediate receipt timestamp.
2. Reject the frame if the selected timestamp is still non-finite.
3. If there is no previous timestamp, accept it.
4. If the selected timestamp is less than or equal to the previous timestamp:
   - Prefer a finite receipt timestamp that is strictly greater than the previous timestamp.
   - Otherwise use `nextafter(previous, +infinity)`.
5. Reject only if that repaired value is non-finite.
6. Store the accepted timestamp and report whether adjustment occurred.

The guard lives outside the reconnect loop. This is coupled to stable Vicon source IDs and LabRecorder stream recovery. Moving or resetting it per connection would allow recreated samples in the same logical recorded stream to move backwards.

### Marker and segment alignment

One accepted frame timestamp is passed to both marker and segment outlets. Object-level reads do not receive independent timestamps. Occluded/error values retain the frame timestamp and become invalid fixed-shape payloads.

### Metadata that describes this policy

Both Vicon streams currently advertise:

- `timestamp = estimated_acquisition_time`
- `clock_domain = lsl_local_clock`
- `timestamp_estimator = immediate_receipt_minus_valid_pipeline_latency`
- `timestamp_fallback = immediate_receipt_time`
- `latency_correction = GetLatencyTotal_pipeline_estimate`
- `timestamp_accuracy = acquisition_estimate_not_capture_accurate`
- `synchronization/timestamp_origin = local_receipt_minus_valid_vicon_pipeline_latency`

Code, documentation, and emitted XML must remain consistent.

## HoloLens gaze timestamps

### SDK timestamp interpretation

The integration intentionally treats `EyeGazeTrackerReading.SystemRelativeTime.Ticks` as a raw system-relative QPC count, despite the property being exposed through a `TimeSpan` type.

Production conversion is:

`lsl_timestamp_seconds = system_relative_ticks / Stopwatch.Frequency`

The reading's timestamp is not converted using `TimeSpan.TicksPerSecond`, and it is not replaced with Unity frame time, wall-clock time, or `LSL.local_clock()` at retrieval.

No second frequency-rescaling helper is used. The production path and its platform-neutral tests both consume the SDK reading ticks directly in the runtime `Stopwatch.Frequency` domain.

### Query and freshness policy

The publisher-side provider queries a reading at:

`TimeSpan.FromTicks(Stopwatch.GetTimestamp())`

The accepted reading timestamp must satisfy:

- `capture_ticks > 0`
- `query_ticks > 0`
- Age is no more than the 25 ms backlog budget, or
- If capture is slightly in the future, lead is no more than 1 ms.

The `GazeReadingGate` then accepts only a timestamp strictly greater than the last timestamp in the current tracker session. Duplicate or regressing readings are discarded. The gate resets when the active tracker lifecycle changes.

### Queue and overload policy

Both raw and transformed queues have a maximum count of 360 and a maximum capture-time span of 25 ms.

On enqueue:

1. Drop oldest entries while the queue is at maximum count.
2. If the new item makes the capture-time span negative or greater than 25 ms, clear the queue.
3. Enqueue the new item.

Before a transformed sample is dequeued, an over-span queue is collapsed to its newest item.

This policy intentionally creates timestamp gaps under overload. It must not be changed into replaying a stale backlog, interpolating missed samples, or retimestamping old samples at current time.

### Publishing cadence and failures

- The publishing worker schedules ticks at `1000 / nominal_rate` milliseconds, with nominal rate currently exactly 90 Hz.
- A missed schedule advances to the next current-time interval rather than executing an unbounded catch-up loop.
- A finite positive `GazeSample.Timestamp` is passed verbatim to `push_sample`.
- A sample with invalid timestamp is dropped while cadence advances.
- Provider exceptions do not create a sample timestamp. Transient exceptions are retried; persistent consecutive exceptions trigger tracker recovery.

### Gaze timestamp metadata

The stream declares:

- `timestamp = sdk_system_relative_time`
- `timestamp_units = seconds`
- `timestamp_conversion = system_relative_qpc_ticks_divided_by_runtime_qpc_frequency`
- `timestamp_tick_frequency_hz = Stopwatch.Frequency` rendered invariantly
- `capture_clock_domain = windows_qpc_system_relative`
- `clock_domain = lsl_local_clock`
- `synchronization/timestamp_origin = eye_gaze_tracker_system_relative_time`
- `timestamp_mapping = direct_qpc_ticks_to_lsl_local_clock_seconds`

This contract assumes the Windows steady clock used by liblsl is the same QPC-backed domain. That assumption requires device parity validation after runtime, Unity, OpenXR, or liblsl changes.

## HoloLens model-target timestamps

The target outlet samples `LSL.local_clock()` during `LateUpdate`, immediately before encoding and pushing the current Vuforia transform.

It declares:

- `timestamp = lsl_local_clock_at_transform_read`
- `clock_domain = lsl_local_clock`
- `synchronization/timestamp_origin = local_clock_at_transform_read`

Unlike gaze, target pose has no SDK capture timestamp and uses irregular rate. The two HoloLens streams share a clock domain but have different acquisition timing accuracy.

## Live preview time handling

### Clock synchronization

Each resolved live inlet calls:

`set_postprocessing(lsl::post_clocksync)`

The timestamps returned by `pull_sample` are therefore treated as corrected into the preview host's local clock. No additional XDF-style offset fit is applied in the live path.

### Pulling and freshness

- Each poll drains at most 16 currently available samples per stream and retains the latest sample/time.
- A stream remains fresh for 500 ms of worker elapsed time after its latest pull.
- Connection, sample presence, and freshness are separate concepts.

### Frame anchoring and matching

Default tolerance is 50 ms and is user configurable.

When a marker sample updates:

- Marker time is the frame timestamp.
- Current marker payload is always parsed.
- Segment and gaze payloads are included only if each stream is fresh and:

  `abs(marker_time - other_time) <= tolerance`

When no marker updates but segment or gaze updates:

- If both are fresh and updated, the later timestamp is the frame timestamp.
- Otherwise the updated fresh stream's timestamp is used.
- Each stream is included only when its latest time lies within tolerance of that fallback anchor.

This is nearest-latest live visualization, not resampling or interpolation.

### Live rate measurement

The preview rate tracker uses corrected sample timestamps and a default rolling window of two seconds:

- Non-finite and exact duplicate timestamps are ignored.
- A regression clears the window before accepting the new timestamp.
- It retains the newest sample at or before the cutoff so jitter does not prevent a full-window measurement.
- A rate is not available until at least two timestamps span two seconds.
- Effective rate is `(sample_count - 1) / elapsed_seconds`.
- Gaze is labeled low rate only after a full window and when effective rate is below 80% of a positive finite nominal rate.
- A stale stream does not display its old measured rate as current.

## XDF preview time handling

### Encoded timestamps

- Explicit timestamps may be 32-bit or 64-bit floating point.
- An omitted timestamp is reconstructed as `previous + 1 / nominal_srate`.
- Reconstruction without a previous timestamp or positive finite nominal rate is an error.
- Non-finite sample timestamps are errors.

### Clock-offset measurements

Each clock-offset chunk contains:

- Collection time in the source stream's clock domain.
- Offset mapping that stream clock into recorder time.

Measurements are sorted by stream time, and duplicate measurement times are rejected.

The reader performs a centered least-squares fit:

`offset(t) = offset_center + slope * (t - stream_center)`

Then:

`corrected_time = stream_time + offset(stream_time)`

With one measurement or zero time variance, slope is zero and the correction is constant.

### Strict timestamp repair

After correction, timestamps are made strictly increasing in original sample order:

1. Maintain a cumulative shift.
2. Add that shift to the next corrected timestamp.
3. If the result is less than or equal to the previous output, replace it with `nextafter(previous, +infinity)`.
4. Add the repair delta to the cumulative shift so later samples retain spacing relative to the repaired timeline.

The number of repaired timestamps is recorded and included in the recording summary.

### Master selection and playback time

The first usable stream in this priority order becomes master:

1. Role `ViconMarkers`
2. Role `ViconSegments`
3. Any numeric stream whose name contains `Vicon` case-insensitively
4. Role `HoloLensGaze`
5. Any numeric stream

Other streams are nearest-matched to each corrected absolute master time by binary search, considering the lower-bound sample and its predecessor. Matches require the configured tolerance.

`XdfStreamData.timestamps` retain corrected absolute time for matching. Each emitted `PreviewFrame.timestamp` is:

`master_absolute_time - first_master_absolute_time`

Thus display playback starts at zero without discarding the absolute-time relationship used for cross-stream matching.

The XDF path applies recorded clock offsets itself and must not also apply live inlet post-processing.

## Merged CSV playback time

For each data row:

1. Use finite `relative_time` when present.
2. Else use finite `lsl_time - first_finite_lsl_time`.
3. Else use the zero-based output frame index.

The CSV reader does not fit clock offsets or nearest-match independent streams; it assumes the input rows are already merged.

## Coordinate-frame glossary

| Frame | Units and basis |
| --- | --- |
| Vicon stream | Global Vicon positions in millimetres; segment quaternion in SDK-published component order |
| Unity world | Unity scene frame, left-handed convention |
| Tracker ray | Extended Eye Tracking tracker-space ray, documented in code as right-handed |
| Published HoloLens shared frame | Right-handed reflection of Unity world, metres, metadata `hololens_stationary_shared_with_gaze` |
| Legacy tracker frame | `eye_tracker_space`; lacks the time-varying pose needed for stair calibration |
| Preview display | Metre-scale combined scene after configured/manual/automatic transforms |

## Vicon preview conversion

The default Vicon preview profile has scale `0.001`, so position `(x, y, z)` millimetres becomes `(0.001x, 0.001y, 0.001z)` metres.

Marker and segment position parsing then applies the generic transform order:

1. Component-wise input-axis sign.
2. Uniform scale.
3. Quaternion rotation when enabled, otherwise Euler X then Y then Z.
4. Translation.

Segment quaternion components are currently copied from the LSL sample and are not composed with the position transform profile. The default Vicon profile only scales position, so this leaves the SDK orientation unchanged. Changing this is a functional coordinate change, not a refactor.

## HoloLens gaze conversion on device

For each usable tracker ray:

1. Validate finite origin/direction, finite tracker/world transforms, nonzero direction, and usable quaternions.
2. Reflect tracker origin and direction across Z to enter Unity's tracker basis:

   `F(x, y, z) = (x, y, -z)`

3. Rotate by the located `playspaceFromTracker` quaternion and translate the origin by its position.
4. Apply the Unity world/playspace component-wise scale.
5. Rotate by `worldFromPlayspace` and translate the origin.
6. Normalize the direction after scale and world rotation.
7. Reflect origin and direction across Z again to publish the right-handed shared world used by the target encoder.
8. Normalize the final direction.

The pose is located at the gaze reading's original system-relative timestamp. Locating at the current Unity frame time would be a behavior change.

## Model-target basis conversion

For a tracked Unity world pose:

- Position `(x, y, z)` becomes `(x, y, -z)`.
- Quaternion `(x, y, z, w)` becomes `(-x, -y, z, w)`.

This is the quaternion basis change `F R(q) F` for `F = diag(1, 1, -1)`. Gaze and target metadata therefore name the same published shared frame.

For an untracked target, pose components are `NaN` and only the tracked flag is zero.

## Preview transforms and calibration

### Manual gaze transform

The persistent manual profile uses:

- Scale `1.0` because gaze is already in metres.
- Default input-axis sign `(1, 1, 1)`.
- User Euler rotation applied X, then Y, then Z.
- User translation applied last.
- Direction receives axis sign and rotation, but no scale/translation, and is normalized.

`gazeTransformForCoordinateFrame` currently returns the supplied transform unchanged. Coordinate-frame gating for calibration is handled separately. A refactor may internalize this no-op, but changing its output requires coordinate parity validation and the public wrapper must remain if source compatibility is required.

### Coordinate-frame compatibility

Calibration compatibility is case-insensitive:

- Gaze frame `eye_tracker_space` is always incompatible.
- If either gaze or target frame metadata is empty, compatibility currently returns true for backward compatibility.
- Otherwise the normalized frame strings must be equal.

Changing the empty-metadata rule could disable calibration for older streams and is a migration.

### Fixed stair profile

The current profile is:

- ID `stair-model-v1`
- Required samples `20`
- Translation tolerance `0.02 m`
- Rotation tolerance `3 degrees`
- Fixed `vicon_from_target` translation `(-2.853343307500, 0.292672723112, 0.006432986454)`
- Fixed identity target rotation

The stair OBJ vertices are interpreted as millimetres (`scale = 0.001`), then the fixed profile rotation/translation places the mesh in preview metre space.

### Stable-pose solution

- Live collection compares each tracked pose with the first pose in the current collection.
- Losing tracking clears the collection.
- Moving outside tolerance clears and restarts the collection.
- The solution averages finite tracked translations and hemisphere-aligned normalized quaternions.
- At least 20 usable poses are required.
- Translation and rotation RMS must remain within the profile tolerances.
- Offline XDF calibration searches for a stable window with the same policy.

### Gaze-to-Vicon calibration transform

The current algorithm:

1. Inverts the averaged `holo_from_target` pose.
2. Composes the fixed `vicon_from_target` pose with a 180-degree target-basis rotation around Z represented by quaternion `(0, 0, 1, 0)`.
3. Reflects the inverse target-to-HoloLens translation/rotation basis as implemented in `gazeTransformFromTargetCalibration`.
4. Produces a quaternion-based HoloLens preview transform.
5. Sets gaze input Z sign to `-1` to preserve the stair model's existing target-local basis convention.

The fixed pose, extra target-basis rotation, reflection, and input Z sign form one compatibility unit. Simplifying one element in isolation can mirror or reverse gaze relative to the stairs.

Automatic calibration is session-only. It is not written to QSettings. Manual translation/Euler controls remain the persistent fallback.

## Cross-path parity requirements

The same producer payload should parse to the same geometry through live and XDF paths after accounting for their different time origins:

- Marker names, validity, and metre positions match.
- Segment names, validity, metre positions, and raw quaternions match.
- Gaze ray names, validity, origins, and normalized directions match.
- Shared-frame gaze and target are eligible for the same stair calibration.
- `eye_tracker_space` gaze is never automatically calibrated.

Live and XDF timestamp values are not expected to be numerically identical in the emitted `PreviewFrame`: live frames retain corrected local-clock time, while XDF playback frames are relative to the master start. Matching decisions should nevertheless agree for equivalent corrected absolute samples and tolerance.

## Validation evidence and checklist

### Existing evidence

- Vicon timestamp estimation/fallback/regression tests in `ViconFrameMapperTests.cpp`.
- Preview tolerance, transforms, calibration, legacy-frame, XDF offset/repair, and playback tests in the focused `Preview*Tests.cpp` files under `vicon-lsl-bridge/tests`.
- Preview rate-window tests in `PreviewRateTests.cpp`.
- HoloLens system-relative timing, freshness, gate, backlog, and publisher timestamp tests in the focused `Program.*Tests.cs` files under `hololens-gaze-lsl/Tests`.
- README acquisition/timestamp/legacy-frame descriptions.

### Required for a time or coordinate refactor

- [ ] Golden Vicon receipt/latency cases include valid, negative, non-finite, overflow, equal, and regressing candidates.
- [ ] Reconnect test proves Vicon timestamps remain strictly increasing across outlet recreation with stable source IDs.
- [ ] Marker and segment timestamps are bit-equal for a frame.
- [ ] Device evidence records `Stopwatch.Frequency`, raw reading ticks, converted LSL times, and local LSL times.
- [ ] Device capture shows no duplicate/regressing gaze timestamps and explicit gaps rather than delayed replay during overload.
- [ ] Live inlet and XDF fixture each apply clock correction exactly once.
- [ ] Constant and drifting XDF offsets match the centered-fit golden values.
- [ ] Timestamp repair count and repaired values match golden output.
- [ ] Boundary matching includes exact tolerance, just inside, and just outside cases.
- [ ] Synthetic basis vectors and quaternions verify both HoloLens reflections.
- [ ] Manual transform order is verified with noncommuting rotations and translation.
- [ ] Fixed stair calibration aligns a known synthetic target/gaze fixture and the physical model.
- [ ] Legacy `eye_tracker_space`, empty metadata, matching metadata, and mismatched metadata follow current compatibility rules.
- [ ] Live and XDF preview geometry parity holds for the same canonical payload.

Use [device-parity-runbook.md](device-parity-runbook.md) for hardware evidence.

## Evidence sources

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
