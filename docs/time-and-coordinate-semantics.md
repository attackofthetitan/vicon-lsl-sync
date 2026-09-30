# How time and coordinates work

## What this guide is for

A mistake with time or coordinates can leave a stream looking fine while its samples sit at the wrong moment or in the wrong place. This guide writes down the formulas, units, fallbacks, and alignment steps the code uses today.

Changing a formula or stream detail here changes behavior. Review it separately from any code tidy-up.

## Time words

| Word | What it means here |
| --- | --- |
| LSL local clock | The steady clock from `lsl::local_clock()` or `LSL.LSL.local_clock()` on the computer or headset sending the stream |
| Vicon receipt time | The desktop's LSL clock, read just after `GetFrame()` succeeds |
| Vicon processing delay | `GetLatencyTotal().Total`: Vicon's guess at how long it took to process the frame. It does not include network delay and is not an exact capture time |
| HoloLens system-relative time | `SystemRelativeTime.Ticks` from a gaze reading. We only use it to put readings in order, because we do not know how fast it ticks on this device |
| Gaze delivery delay | How long after capturing a reading the eye-tracking SDK hands it over, measured as the smallest age of any reading the SDK has offered |
| Corrected live time | A live sample's time after LSL corrects for the clock difference between computers |
| XDF stream time | The time the sender wrote, before the recorder's saved clock correction |
| XDF recorder time | Stream time plus the clock correction saved in the XDF |
| Playback time | Corrected time of the main stream, starting at zero |

## Vicon timestamps

### Pick a time

After Vicon's `GetFrame()` succeeds:

1. Read `receipt = lsl::local_clock()` straight away.
2. Read `GetLatencyTotal()`.
3. If that works and the value is finite and not negative, use:

   `candidate = receipt - latency_seconds`

4. If the delay is missing, negative, not finite, or gives a result that is not finite, use:

   `candidate = receipt`

5. If `receipt` itself is not finite, the candidate is `NaN`. The next step then falls back to a clock reading of its own, which only helps if that one is finite.

This is only a best guess at when Vicon captured the frame. It leaves out the time the data spent on the network. Do not call it an exact capture time.

### Keep time moving forward

`enforceViconTimestamp` follows these rules:

1. If the candidate is not finite, use the receipt time instead.
2. If that is still not finite, drop the frame.
3. If there is no earlier time, accept it.
4. If the new time is the same as or earlier than the last one:
   - Use the receipt time if it is finite and later than the last one.
   - Otherwise, use the smallest possible step after the last one, `nextafter(previous, +infinity)`.
5. Drop the frame only if the fixed time is not finite.
6. Save the accepted time and report whether it had to be fixed.

The timestamp record lives outside the reconnect loop, and the Vicon source IDs stay the same. Together, these stop time from going backward when LabRecorder joins a reopened stream onto its earlier copy.

### Markers and segments share a time

Marker and segment samples from the same Vicon frame use the same accepted time. Single objects never get their own times. A hidden or failed object keeps the frame's time but sends a fixed-size "missing" value.

### Vicon time details

Both Vicon streams use these exact values:

- `timestamp = estimated_acquisition_time`
- `clock_domain = lsl_local_clock`
- `timestamp_estimator = immediate_receipt_minus_valid_pipeline_latency`
- `timestamp_fallback = immediate_receipt_time`
- `latency_correction = GetLatencyTotal_pipeline_estimate`
- `timestamp_accuracy = acquisition_estimate_not_capture_accurate`
- `synchronization/timestamp_origin = local_receipt_minus_valid_vicon_pipeline_latency`

Keep the code, the stream details it sends, and this guide in step.

## HoloLens gaze timestamps

### The headset's own time value

`EyeGazeTrackerReading.SystemRelativeTime.Ticks` is a count that only goes up.
We do not know how fast it ticks on this device, so it is never turned into
seconds. It has exactly two uses:

- putting readings in order, and marking the last reading taken;
- passing to `SpatialGraphNode.TryLocate`, which takes the SDK's own tick value
  as it is.

Every length of time on the gaze path (the queue's time limit, the measured
rate, the delivery delay, how old a reading is) is measured on the LSL clock
instead. Each reading already carries that time as `GazeSample.Timestamp`. Do
not divide those ticks by `Stopwatch.Frequency`, and do not use
`TimeSpan.TicksPerSecond`.

The capture time we publish does not come from those ticks either. Each time the
app asks for readings, it reads `DateTime.UtcNow` and `LSL.local_clock()` once,
together. Every reading it gets then works out its own time from how old the SDK
says it is, with both date and time values in UTC so a time zone or daylight
saving change cannot move it:

`lsl_timestamp_seconds = query_lsl_clock - (query_time - reading.Timestamp)`

A reading that seems captured more than 50 ms after the ask means the headset's
clock was changed between the capture and the ask, so its time cannot be worked
out. It is skipped and counted in the gaze acquisition log, and the next ask
waits a frame.

Do not replace this with Unity's frame time, or with `LSL.local_clock()` read at
the moment the sample is sent.

### Getting every reading

The publisher does not simply ask for the reading at the current time. Asking
for "now" gives back exactly one reading, so if the ask comes more than one
tracker frame after the last one, every reading in between is lost. The
publisher runs at the same speed as the tracker, so small timing differences
were enough to lose about one reading in six.

Instead, each step works forward from the last capture time it accepted:

`TryGetReadingAfterSystemRelativeTime(TimeSpan.FromTicks(last_accepted_ticks))`

It keeps going until the SDK has nothing newer, or until it has taken 32
readings. That way, a step that runs a little late loses no reading. One that
falls more than 500 ms behind still loses the older readings, because the queue
keeps at most 500 ms of them (see below).

On this device, the SDK cannot say "nothing newer" cleanly. Its C# code does not
check for that empty result, so the normal end of a catch-up shows up as a
`NullReferenceException` thrown inside the SDK. It also leaves behind a broken
object that throws again later, when .NET cleans it up. One of those per step
crashes the app within seconds. So the app never asks for a reading that cannot
exist yet:

- It does not ask at all until one frame period **plus the delivery delay** has
  passed since the last accepted capture time.
- It stops as soon as a reading brings it up to the newest reading the tracker
  has made, because it has then caught up.

Both checks compare capture times in LSL seconds. The delivery delay matters
because a reading is not ready the instant it is captured. On this device it
arrives about 20 ms later, while one frame is about 11 ms. When the check used
the frame time alone, a reading the app had just taken was always already more
than one frame old, so every step made one extra ask that could never be
answered. Over one session, that was 2428 failures against 2184 readings, which
is about one leaked SDK object per step.

The delay is measured, not guessed. It is the smallest age of any reading the
SDK has offered in this tracker session. It uses the smallest value because a
reading picked up while catching up can be any age, which says nothing about how
fast the tracker hands over a new one. Before the first reading, the delay is
zero, so the app simply asks as often as it did before the delay was measured.

These checks prevent the bad ask in the normal case, but not while the tracker
is making readings more slowly than its set rate. So the app also catches and
counts the failure. After three failures since catching up last resumed, it
stops catching up for ten seconds. Meanwhile it asks for the reading at the
current time, which gets at most one reading per step.

The pause is short on purpose. The SDK only says "nothing newer" when the
tracker has nothing new to give, such as before it starts making readings. If
the pause lasted all session, a tracker that started a minute late would be
stuck on the slower method, which cannot keep up.

The count only starts over when a pause ends, not at the next good reading. A
catch-up that gets one reading and then fails never has two failures in a row.
An older version counted only failures in a row, so a catch-up that failed on
every step ran for a whole session, leaking thousands of objects with only
eleven pauses. A clean `null` answer is normal and never counted, so with an SDK
version that works properly, catching up never pauses.

The first step of a tracker session has no last capture time yet, so it gets one
from `TryGetReadingAtTimestamp(now)`. That reading is the only one judged on age.
Its age is `query_time - reading.Timestamp`. Both of those are ordinary date and
time values from the SDK, so no guess about the headset's own timer is needed.

- The age must be finite.
- It must be no more than 50 ms either way. It can go either way because the
  reading for "now" can be captured one frame before or after the ask.

Do not work out that age by comparing `SystemRelativeTime.Ticks` with
`Stopwatch.GetTimestamp()`. The two do not start from the same point, and the
headset has now shown that they do not tick at the same speed either:

- A reading whose own time said it was 0.022 s old measured -7862.129 s against
  the headset timer, about 2.2 hours in the future. A build that made that
  comparison threw away every reading the tracker offered, on a headset that was
  offering one on every ask.
- In a later session, the same pair read 0.020 s and -231.332 s, and two seconds
  later, 0.021 s and -233.588 s. If only the starting point were different, the
  gap would have stayed the same.
- In both sessions, the gap grows by about 0.92 s of "future" for every second
  the headset has been on. That is what a speed difference looks like, not a
  different starting point.

The difference between two `SystemRelativeTime` values still puts readings in
the right order. It is not a length of time, because turning it into seconds
needs the tick speed. So the queue's time limit and the measured rate use LSL
capture times instead.

Once there is a last capture time, an old reading means the step is catching up,
not that the tracker has stalled, so age is not a reason to drop it.

`GazeReadingGate` then only accepts a time later than the last one it accepted in the same tracker session. It drops repeats and earlier times. The last time it accepted is also where catching up starts from, so a refused reading also ends the catch-up instead of being asked for again. The gate resets when the tracker session changes.

### Queue limits

The raw queue and the converted queue each hold at most 360 items, and at most 500 ms between the oldest and newest capture times.

One catch-up can return several readings at once, so the time limit has to be
bigger than a full batch. Otherwise the queue would throw away exactly the
readings the catch-up just recovered. At 90 Hz, a full batch of 32 readings
already covers 355 ms. The limit only exists to stop the data getting too old
after a real stall. Both numbers are seconds on the LSL clock, so they can be
compared directly. An older version worked out the limit from
`Stopwatch.Frequency` and compared it with SDK ticks, so its real length was
unknown and could end up shorter than one batch.

When adding an item:

1. While the queue already has 360 items, remove the oldest.
2. If the new item makes the time between oldest and newest more than 500 ms, or makes it impossible to work out, empty the queue. A span just below zero is fine, because capture times come from two clocks that can differ very slightly. A new tracker session, which could reset the clock completely, empties both queues itself.
3. Add the new item.

Before sending a converted sample, if the queue is over its limit, keep only the newest item.

This leaves a clear gap in time when the app falls behind. Do not replay old samples, fill in missing ones, or give old samples a new time.

### Sending schedule and errors

Each step sends at most one waiting sample, so steps run at 1.25 times the
declared rate. Without that margin, a queue that built up while Unity's main
thread was stuck could never shrink, because samples would arrive as fast as they
leave. The delay would never go away and would add up over each stall until the
queue's time limit threw it all away. A step that finds nothing sends nothing.
The margin only changes how often the worker runs, not the rate declared on the
stream.

- The worker runs one step every `1000 / nominal_rate` ms. The rate is exactly 90 Hz.
- After missing a step, move on to the next one from the current time. Do not try to catch up on every missed step.
- Pass a finite, positive `GazeSample.Timestamp` to `push_sample` unchanged.
- Drop a sample with a bad time, but keep to the schedule.
- An error from the gaze reader never creates a sample time.
- Retry short gaze reader errors. Restart the tracker after about one second of errors in a row.

### Gaze time details

The gaze stream uses:

- `timestamp = eye_gaze_tracker_timestamp`
- `timestamp_units = seconds`
- `timestamp_conversion = lsl_query_time_minus_sdk_timestamp_age`
- `capture_clock_domain = eye_gaze_tracker_datetime`
- `clock_domain = lsl_local_clock`
- `reading_retrieval = sequential_drain_after_last_capture`
- `synchronization/timestamp_origin = eye_gaze_tracker_reading_timestamp`
- `timestamp_mapping = query_lsl_clock_minus_query_to_capture_age`
- `backlog_policy = drop_when_capture_span_exceeds_500ms_retain_latest`

This assumes the SDK's reading time and `LSL.local_clock()` on the headset stay in step between the two reads. Run the hardware tests again after changing the Windows, Unity, OpenXR, or liblsl version.

## HoloLens stair target timestamps

The Vuforia target output reads `LSL.local_clock()` in `LateUpdate`, just before it reads, packs, and sends the current target position.

It uses:

- `timestamp = lsl_local_clock_at_pose_publication`
- `clock_domain = lsl_local_clock`
- `synchronization/timestamp_origin = local_clock_at_pose_publication`

The target has no capture time from the SDK and has an irregular rate. Gaze and target share one clock, but the target's time is less exact about when the position was really captured.

While Vuforia is turned off on purpose, target samples carry the last steady
stair reference with `Tracked = 2`. Their times say when the reference was sent,
not when a new position was measured. Live tracked samples use 1. Invalid
samples use 0 and seven NaNs. The reference is built from 20 steady positions
taken before the pause, and is cleared when Vuforia is turned back on or
`VuforiaModelTargetPoseOutlet` is turned off. This lets a recording started after calibration still be
lined up without keeping Vuforia running. The real stairs and the Unity world
must not change. Normal tracking loss never sends a frozen reference by itself.

## Live preview time

### Correct clocks once

Every live LSL input calls:

`set_postprocessing(lsl::post_clocksync)`

So `pull_sample` returns times already corrected to the preview computer's clock. Do not also apply the XDF clock correction rules to live data.

### Read data and mark it fresh

- Read at most 16 waiting samples from each stream at a time and keep the newest.
- A stream stays fresh for 500 ms after its latest sample.
- Being connected, having a sample, and being fresh are three separate things.

### Build one preview frame

The default matching limit is 50 ms. Users can change it.

When a marker sample arrives:

- Use its time for the frame.
- Always read its marker data.
- Add segment and gaze data only when that stream is fresh and:

  `abs(marker_time - other_time) <= tolerance`

When there is no new marker, but segment or gaze data is new:

- If both are new and fresh, use the later time.
- Otherwise, use the time of the one fresh stream that is new.
- Only add a stream if its latest time is within the limit of the chosen time.

This is a "closest current sample" match for display. It never makes up samples between real ones.

### Show the live rate

The preview works out the rate from corrected sample times over a two-second window:

- Ignore times that are not finite, and exact repeats.
- If time goes backward, empty the window before adding the new time.
- Keep the newest sample at or before the start of the window, so small timing wobbles do not shorten the window.
- Do not show a rate until at least two samples cover two seconds.
- Work out `(sample_count - 1) / elapsed_seconds`.
- Only mark gaze as low after a full window, and only when it is below 80% of a positive, finite expected rate.
- Stop showing the last rate once the stream is out of date.

For a 90 Hz gaze stream, "low" means below 72 Hz.

## XDF preview time

### Read sample times

- A time can be stored as a 32-bit or 64-bit decimal number.
- A missing time becomes `previous + 1 / nominal_srate`.
- A missing time is an error if there is no earlier time or the expected rate is not positive and finite.
- A sample time that is not finite is an error.

### Work out the saved clock corrections

Each XDF clock correction record holds:

- When it was measured, on the sending stream's clock.
- How much to add to get to the recorder's clock.

Sort these by stream time. Two with the same time are an error.

Fit a straight line through them, centred on their middle:

`offset(t) = offset_center + slope * (t - stream_center)`

Then apply:

`corrected_time = stream_time + offset(stream_time)`

With only one record, or when all records have the same time, the slope is zero and the correction is the same everywhere.

### Fix times that go backward

After correcting, keep samples in order and make times always go up:

1. Keep a running shift.
2. Add the shift to the next corrected time.
3. If the result is the same as or earlier than the last one, use `nextafter(previous, +infinity)` instead.
4. Add the size of that fix to the running shift, so later samples keep their spacing from the fixed one.

Count the fixed values and show the count in the recording summary.

### Choose the main stream and playback time

Pick the first usable stream in this order:

1. The `ViconMarkers` role.
2. The `ViconSegments` role.
3. Any number stream with `Vicon` in its name, in any letter case.
4. The `HoloLensGaze` role.
5. Any number stream.

For each main stream time, find the closest sample in each other stream. Look at both the first sample at or after that time and the one just before it, and only accept the closer one if it is within the chosen time limit.

Keep the full corrected times in `XdfStreamData.timestamps` for matching. Show each frame at:

`master_absolute_time - first_master_absolute_time`

So playback starts at zero, while matching still uses the full corrected times.

XDF playback applies the saved clock correction itself. It must not also use LSL's live clock correction.

## Merged CSV time

For each row:

1. Use `relative_time` if it is there and finite.
2. Otherwise, use `lsl_time - first_finite_lsl_time` if `lsl_time` is finite.
3. Otherwise, use the row number, starting at zero.

The CSV reader does not correct clocks or match separate streams. It expects the rows to be merged already.

## Coordinate words

| Space | Units and directions |
| --- | --- |
| Vicon stream | Vicon's room positions in millimetres; segment rotation as the SDK sends it |
| Unity world | Unity's scene coordinates, which are left-handed |
| Eye-tracker ray | A ray from Extended Eye Tracking, which the code treats as right-handed |
| Published HoloLens shared world | The Unity world flipped to be right-handed, in metres, named `hololens_stationary_shared_with_gaze` |
| Old tracker space | `eye_tracker_space`; it does not record where the headset was, so it cannot be lined up with the stairs |
| Preview display | One metre-based scene, after the fixed Vicon scale and, if there is one, the HoloLens alignment |

"Left-handed" and "right-handed" describe which way the Z axis points compared with X and Y. Moving between them means flipping one axis.

## Show Vicon data in the preview

The default Vicon preview scale is `0.001`. A position `(x, y, z)` in millimetres becomes `(0.001x, 0.001y, 0.001z)` in metres.

Marker and segment positions then go through these steps in order:

1. Flip each axis if its sign setting says so.
2. Scale.
3. Rotate: use the four-number rotation (a quaternion) if it is turned on, or otherwise rotate around X, then Y, then Z.
4. Move by the translation.

The preview copies segment rotation values straight from the LSL sample. It does not combine them with the position steps above. Because the default setup only scales positions, Vicon rotations are shown unchanged. Changing this would change coordinate behavior.

## Move HoloLens gaze into the world on the headset

For each usable eye ray:

1. Check that the origin, direction, headset positions, and rotations are finite and usable, and that the direction is not zero.
2. Flip the origin and direction's Z to go from tracker coordinates to Unity's tracker coordinates:

   `F(x, y, z) = (x, y, -z)`

3. Rotate by the `playspaceFromTracker` rotation and add its position to the origin.
4. Apply the Unity world scale to each part.
5. Rotate by `worldFromPlayspace` and add its position to the origin.
6. Rescale the direction to length 1 after scaling and rotating.
7. Flip Z again on the origin and direction, to publish in the right-handed world shared with the target.
8. Rescale the final direction to length 1.

Look up where the headset was at the original gaze capture time. Using Unity's current frame time would change behavior.

## Move the Vuforia target into the published world

For a tracked Unity position:

- Position `(x, y, z)` becomes `(x, y, -z)`.
- Rotation `(x, y, z, w)` becomes `(-x, -y, z, w)`.

In maths terms this is `F R(q) F` with `F = diag(1, 1, -1)`: the same Z flip applied to the rotation. So gaze and target publish in the same right-handed world.

When the target is not tracked, send `NaN` for all seven position and rotation values and zero for the tracked value.

## Preview alignment and the stairs

### Gaze without a calibration

You cannot type in a gaze alignment. Where the HoloLens world sits in Vicon's
room cannot be known until it is measured, so a session with no worked-out or
applied calibration leaves gaze as it is:

- Scale is `1.0`, because gaze is already in metres.
- Axis signs are `(1, 1, 1)`.
- No rotation and no translation.

So gaze is drawn in the published `hololens_stationary_shared_with_gaze`
coordinates. It is shown, but not lined up with Vicon, and the calibration state
says **Not calibrated**.

The preview uses the same gaze alignment whatever coordinate name the gaze stream reports. Changing that needs coordinate tests.

### Can gaze and target be lined up?

Compare coordinate names, ignoring letter case:

- `eye_tracker_space` gaze can never be lined up.
- If either name is empty, allow it, for older recordings.
- Otherwise, both names must match.

Changing the empty-name rule could stop older recordings from lining up. Review it on its own and test with real saved files.

### Fixed stair settings

The stair settings are:

- ID: `stair-model-v1`.
- Samples needed: `20`.
- Allowed position movement: `0.02 m`.
- Allowed rotation movement: `3 degrees`.
- Fixed `vicon_from_target` position: `(-2.882676086, 0.310499985, 0.0)` metres.
- Fixed target rotation: none.

The real stairs never move. The measurement confirmed on 2026-09-17 puts the
bottom front-left corner 120.5 cm straight ahead of and 21.3 cm to the left of
the Vicon origin, facing up the stairs. Forward is `-X`, left is `-Y`, and the
floor is `Z=0`, so this corner is at `(-1.205, -0.213, 0.0)` metres. The stairs
go up along `-X`.

The stair OBJ file is in millimetres. Its matching corner is at
`(1677.676086, -523.499985, 0.0)`, not at the model's origin. With no rotation,
take that corner, scale it by `0.001`, and subtract it from the measured Vicon
corner to get the fixed model position above. Scale the model by `0.001`, then
apply the fixed rotation and position to place it in the preview's metre space.

The built-in **Default stair setup**, before any alignment has been worked out,
updates to this measurement when settings load. Saved calibrations keep their
original position and alignment. To replace one based on the old estimate,
select **Default stair setup**, run **Calibrate from Stair Target**, and save the
new session calibration. The stairs stay put, but a new HoloLens world still
needs a new gaze-to-Vicon calibration.

### Find a steady target position

- Compare each tracked position with the first one in the current set.
- Clear the set when tracking is lost.
- If the target moved more than either limit, clear the set and start again from the new position.
- Average the finite positions and rotations. Rotations can be written two ways with opposite signs, so line up their signs before averaging.
- Need at least 20 usable positions.
- The spread of both position and rotation must stay within their limits.
- Use the same rules when searching an XDF recording for a steady stretch.

### Work out the gaze-to-Vicon alignment

The target stream's `acquisition/sdk` value says who sent it:
`Unity.XR.manual_stair_registration` means the stairs were placed by hand from
three points on the Unity model, and any other value, or none, means the
Vuforia model target. Both find the same Unity-imported stair model, so the
preview handles both the same way and does not read this value.

The preview draws the stair OBJ in the file's own coordinates, but Unity's model
import flips X. So after undoing the published world's Z flip, the preview also
undoes that X flip to reach the drawn model. The steps are:

1. Invert the averaged `holo_from_target` position and rotation.
2. Flip the target-to-HoloLens position and rotation in Z, to get back to the target's Unity coordinates.
3. Flip that position and rotation in X, to reach the drawn stair model's coordinates.
4. Apply the fixed `vicon_from_target` position and rotation.
5. Set the gaze input signs to `(-1, 1, -1)`, which is the X flip combined with the Z flip from step 2.

Two flips together keep the handedness the same, which is needed when mapping
the right-handed gaze coordinates onto Vicon. Seen from the target, the change is
`(x, y, z) -> (-x, y, -z)`: forward and height line up with the stairs, and the
sideways Y axis is kept. The older Vuforia code used a 180-degree turn around Z
instead of the X flip. That also flipped the sideways Y axis, which swapped left
and right in gaze.

The inverted position and the input signs must change together. Flipping just
one input sign breaks as soon as the target is rotated. Only the preview does
this conversion; recordings keep the data as it was sent. XDF playback that
works out alignment from target positions uses the fixed conversion
automatically. Older saved Vuforia calibrations keep what they stored: run
**Calibrate from Stair Target**, then **Save Session Calibration**, to replace a
mirrored one.

Automatic alignment only lasts for the current preview session. It is not saved.
There is no fallback: clearing a calibration, or an alignment that fails its
quality limits, takes the preview back to showing gaze as it was sent (see
above).

## Live and recorded should look the same

Given the same values, the live and XDF preview should show the same shapes, once you allow for their different starting times:

- Marker names, usable flags, and positions in metres match.
- Segment names, usable flags, positions in metres, and raw rotations match.
- Gaze ray names, usable flags, origins, and directions match.
- Gaze and target with the shared-world name can use the same stair alignment.
- `eye_tracker_space` gaze never uses automatic target alignment.

Do not expect the live and XDF `PreviewFrame.timestamp` numbers to match. Live time stays on the corrected local clock, while XDF playback starts from zero. Their matching decisions should still agree for the same corrected times and the same limit.

## Tests for a time or coordinate change

Existing tests cover Vicon time, preview matching, alignment steps, stair alignment, and old coordinate names. They also cover XDF clock fixing, playback, live rate, HoloLens time conversion, reading age, queue limits, and published times.

Before merging a time or coordinate change:

- [ ] Saved Vicon cases cover good, negative, not-finite, overflowing, equal, and earlier times.
- [ ] Reconnecting shows Vicon time keeps going up across reopened streams with the same source IDs.
- [ ] Marker and segment timestamps from one frame are exactly equal.
- [ ] Headset results record `Stopwatch.Frequency`, raw reading counts, converted LSL times, and local LSL times.
- [ ] A headset recording has no repeated or earlier gaze times, and shows gaps instead of late replays when overloaded.
- [ ] Live and XDF each correct clocks exactly once.
- [ ] Constant and changing XDF corrections match the saved expected values.
- [ ] The fix count and fixed times match the saved results.
- [ ] Matching tests cover exactly at, just inside, and just outside the time limit.
- [ ] Simple axis and rotation examples prove both HoloLens flips.
- [ ] Without a calibration, gaze stays as it was sent instead of using a guessed alignment.
- [ ] Fixed stair alignment works for known fake data and the real model.
- [ ] Old, empty, matching, and different coordinate names follow the rules.
- [ ] Live and XDF preview shapes match for the same values.

Use the [hardware test guide](device-parity-runbook.md) for headset results.

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
