# How time and coordinates work

A mistake with time or coordinates can leave a stream looking fine while its
samples sit at the wrong moment or in the wrong place. This guide writes down the
formulas the code uses. Changing any of them changes behavior, so review that on
its own. The exact stream details each stream sends are in [Behavior that must
stay the same](behavior-contract.md).

## Words

| Word | Meaning |
| --- | --- |
| LSL clock | `lsl::local_clock()` or `LSL.LSL.local_clock()` on the sending computer or headset |
| Vicon receipt time | The desktop's LSL clock, read right after `GetFrame()` works |
| Vicon delay | `GetLatencyTotal().Total`, Vicon's guess at its processing time. It leaves out network delay |
| Headset ticks | `SystemRelativeTime.Ticks` on a gaze reading. Only used for ordering, because we don't know how fast it ticks |
| Gaze delivery delay | How long after capture the SDK hands over a reading |
| Corrected live time | A live sample's time after LSL corrects for the clock difference between computers |
| XDF stream time | The time the sender wrote, before the recorder's saved correction |
| Playback time | Corrected time of the main stream, starting at zero |

## Vicon timestamps

### The estimate

Right after `GetFrame()` works:

1. Read `receipt = lsl::local_clock()`.
2. Read `GetLatencyTotal()`. If it works and is finite and not negative:
   `candidate = receipt - latency_seconds`. Otherwise `candidate = receipt`.
3. If `receipt` isn't finite, the candidate is `NaN`, and the next step falls
   back to its own clock reading.

The SDK keeps up to 256 waiting frames instead of one, so the bridge can catch up
after a short delay without losing frames. Its latency doesn't count time spent
in that queue, so `ViconFrameClock` does this:

- The first frame is anchored to the candidate.
- Each later frame adds the frame-number difference divided by the camera rate.
- It follows the receipt estimate by at most 1,000 ppm. So draining a queue
  doesn't give old frames almost the same time.
- Missing frame numbers stay gaps. No samples are made up.
- A reconnect, a counter reset, or a rate change starts a new anchor.

It's still an estimate. It isn't an exact capture time.

### Time only goes forward

`enforceViconTimestamp`:

1. If the candidate isn't finite, use the receipt time. If that isn't finite
   either, drop the frame.
2. If there's no earlier time, accept it.
3. If it's the same as or earlier than the last time, use the receipt time if
   that's later, otherwise the smallest step after the last time,
   `nextafter(previous, +infinity)`.
4. Save the accepted time.

The record lives outside the reconnect loop and the source IDs don't change, so
LabRecorder can join a reopened stream onto its earlier copy without time going
backward.

Marker and segment samples from one frame share the same time. Single objects
never get their own times.

## HoloLens gaze timestamps

### Don't use the headset ticks as time

`SystemRelativeTime.Ticks` only goes up, but its speed is unknown, so it's never
turned into seconds. It's used for two things only: putting readings in order,
and passing to `SpatialGraphNode.TryLocate`, which takes the SDK's own ticks.
Every length of time on the gaze path (queue limits, rates, delays, ages) uses
the LSL clock instead. Don't divide ticks by `Stopwatch.Frequency` or
`TimeSpan.TicksPerSecond`.

We know this from the headset itself. Comparing the ticks with
`Stopwatch.GetTimestamp()`:

- A reading that its own time said was 0.022 s old came out as -7862.129 s, about
  2.2 hours in the future. A build that used this threw away every reading.
- In a later session, the same pair read 0.020 s and -231.332 s, and two seconds
  later 0.021 s and -233.588 s.
- In both sessions the gap grows by about 0.92 s for every second the headset is
  on. That's a different speed, not just a different start.

Nobody has measured the real tick speed. Don't write code that assumes one.

### The capture time

Each time the app asks for readings, it reads `DateTime.UtcNow` and
`LSL.local_clock()` once, together. Each reading then gets:

`lsl_timestamp = query_lsl_clock - (query_time - reading.Timestamp)`

Both dates are in UTC, so a time zone or daylight saving change can't move it. A
reading that seems captured more than 50 ms after the ask means the headset
clock changed in between. Its time can't be worked out, so it's skipped and
counted, and the next ask waits a frame.

Don't replace this with Unity's frame time, or the LSL clock at send time.

### Getting every reading

Asking for "now" returns exactly one reading. If the ask is a little late, every
reading in between is lost (that lost about one in six). So each step works
forward from the last accepted capture time:

`TryGetReadingAfterSystemRelativeTime(TimeSpan.FromTicks(last_accepted_ticks))`

It keeps going until there's nothing newer, or it has 32 readings.

On this headset, the SDK can't say "nothing newer" cleanly. It throws a
`NullReferenceException` inside the SDK and leaves a broken object behind that
throws again later. One of those per step crashes the app within seconds. So the
app avoids asking for a reading that can't exist yet:

- It doesn't ask until one frame **plus the delivery delay** has passed since the
  last accepted capture. A reading arrives about 20 ms after capture, while a
  frame is about 11 ms, so the frame alone isn't enough. (Without the delay, one
  session had 2428 failures against 2184 readings.)
- It stops once it has the newest reading.

The delivery delay is the smallest age of any reading offered this tracker
session. It's zero before the first reading.

If the SDK still fails three times since catching up last resumed, catching up
pauses for ten seconds, and the app asks for the current reading instead, which
gets at most one per step. The pause is short on purpose, so a tracker that
starts late isn't stuck on the slow method all session. The count only resets
when a pause ends. (An older version only counted failures in a row, and leaked
thousands of objects in one session.) A clean `null` answer is never counted.

The first step of a session has no last capture time, so it uses
`TryGetReadingAtTimestamp(now)`. That's the only reading judged on age: its age,
`query_time - reading.Timestamp`, must be finite and within 50 ms either way.
Later, an old reading just means the step is catching up.

`GazeReadingGate` then only accepts a time later than the last one in the same
session. A refused reading also ends the catch-up. The gate resets with the
tracker session.

### Queue limits

The raw and converted queues each hold at most 360 items and 500 ms between the
oldest and newest capture time. A full catch-up batch of 32 readings is 355 ms at
90 Hz, so the time limit has to be bigger than that, or the queue would throw away
the readings the catch-up just got.

When adding an item:

1. While the queue has 360 items, drop the oldest.
2. If the new item makes the span over 500 ms, or impossible to work out, empty
   the queue. A span just below zero is fine, because the times come from two
   clocks that can differ slightly.
3. Add the item.

Before sending, if the converted queue is over its limit, keep only the newest.
This leaves a clear gap when the app falls behind. Never replay, fill in, or
re-time old samples.

### Sending schedule

Each step sends at most one sample, so steps run at 1.25 times the 90 Hz rate.
Without that margin, a queue that built up during a stall could never shrink.
The margin only changes how often the worker runs, not the declared rate.

- After a missed step, carry on from now. Don't run every missed step.
- Pass a finite, positive time to `push_sample` unchanged. Drop a sample with a
  bad time.
- Retry short reader errors. Restart the tracker after about one second of them.

This assumes the SDK's time and the headset's LSL clock stay in step between the
two reads. Run the hardware tests again after changing Windows, Unity, OpenXR, or
liblsl.

## Stair target timestamps

The target output reads `LSL.local_clock()` in `LateUpdate` just before it sends.
The SDK gives no capture time, so this is less exact than gaze.

While Vuforia is off on purpose, samples carry the last steady reference with
`Tracked = 2`. Their times say when it was sent, not when it was measured. This
lets a recording started after calibration still be lined up, as long as the
stairs and the Unity world don't change.

## Live preview time

Every live input calls `set_postprocessing(lsl::post_clocksync)`, so samples come
in already corrected to this computer's clock. Don't also apply the XDF rules.

- Read at most 16 waiting samples per stream and keep the newest.
- A stream is fresh for 500 ms after its newest sample.

### Building a frame

The matching limit is 50 ms by default. Users can change it.

- A new marker sample sets the frame time. Segment and gaze data are added only if
  fresh and `abs(marker_time - other_time) <= limit`.
- With no new marker, a new fresh segment or gaze sample sets the time (the later
  one, if both are new). Any stream, markers included, is added only if fresh and
  within the limit of that time.

This shows the closest current sample. It never makes up samples in between.

### The live rate

Measured over a two-second window of corrected times:

- Ignore times that aren't finite, and exact repeats.
- If time goes backward, empty the window first.
- Keep the newest sample at or before the window start, so small wobbles don't
  shorten the window.
- No rate until at least two samples cover two seconds. Then
  `(sample_count - 1) / elapsed_seconds`.
- Gaze is "low" below 80% of its declared rate, so 72 Hz for 90 Hz, and only
  after a full window.
- Stop showing the rate once the stream is out of date.

## XDF preview time

### Sample times

- Times are 32-bit or 64-bit numbers.
- A missing time is `previous + 1 / nominal_srate`. Without an earlier time or a
  positive rate, that's an error. So is a time that isn't finite.

### Clock corrections

Each record holds a time on the sender's clock and how much to add to reach the
recorder's clock. Sort them by time (two with the same time is an error) and fit
a straight line through them, centred on their middle:

`offset(t) = offset_center + slope * (t - stream_center)`

`corrected_time = stream_time + offset(stream_time)`

With one record, or all at the same time, the slope is zero.

### Times that go backward

After correcting, keep a running shift. Add it to each time. If the result isn't
later than the last one, use `nextafter(previous, +infinity)` and add the size of
that fix to the shift, so later samples keep their spacing. Count the fixes and
show the count in the summary.

### Main stream and matching

The main stream is the first usable one of: `ViconMarkers`, `ViconSegments`, any
number stream with `Vicon` in its name, `HoloLensGaze`, then any number stream.

For each main-stream time, each other stream gives its closest sample (checking
the one at or after, and the one before), if it's within the limit. Matching uses
full corrected times. The frame is shown at `time - first_main_stream_time`, so
playback starts at zero.

XDF playback applies the saved corrections itself. It must not also use LSL's live
correction.

## CSV time

For each row: `relative_time` if it's finite, or else `lsl_time - first_lsl_time`,
or else the row number from zero. The CSV reader doesn't correct clocks or match
streams. It expects rows already merged. A row whose time goes backward stops the
load with an error naming the row, because playback can't run time backward.

## Coordinates

| Space | Units and directions |
| --- | --- |
| Vicon stream | Vicon's room, in millimetres, with segment rotation as the SDK sends it |
| Unity world | Unity's scene, left-handed |
| Eye-tracker ray | From Extended Eye Tracking, treated as right-handed |
| Published HoloLens world | The Unity world with Z flipped to be right-handed, in metres, named `hololens_stationary_shared_with_gaze` |
| Old tracker space | `eye_tracker_space`. It doesn't record where the headset was, so it can't be lined up with the stairs |
| Preview | One scene in metres |

Left- and right-handed say which way Z points compared with X and Y. Going between
them flips one axis.

### Vicon in the preview

The default scale is `0.001`, so millimetres become metres. Each position then
goes through, in order: flip each axis whose sign setting says so, scale, rotate
(by a quaternion if turned on, otherwise around X, then Y, then Z), and move.

Segment rotations are copied straight from the sample and aren't put through
these steps. The default setup only scales, so rotations show unchanged.

### Gaze into the world, on the headset

For each usable ray (finite origin, direction, and headset pose, and a non-zero
direction):

1. Flip Z on the origin and direction: `F(x, y, z) = (x, y, -z)`.
2. Rotate by `playspaceFromTracker` and add its position.
3. Apply the Unity world scale.
4. Rotate by `worldFromPlayspace` and add its position.
5. Make the direction length 1.
6. Flip Z again, to publish in the right-handed world shared with the target.
7. Make the direction length 1 again.

Use where the headset was at the gaze capture time, never Unity's current frame
time.

### The Vuforia target into the published world

- Position `(x, y, z)` becomes `(x, y, -z)`.
- Rotation `(x, y, z, w)` becomes `(-x, -y, z, w)`.

That's `F R(q) F` with `F = diag(1, 1, -1)`, the same Z flip as gaze. When the
target isn't tracked, send seven `NaN` values and `Tracked = 0`.

## Lining up gaze with the stairs

### No calibration

You can't type in an alignment, because where the HoloLens world sits in Vicon's
room is unknown until measured. Without one, gaze is drawn as sent: scale 1, signs
`(1, 1, 1)`, no rotation, no move. It's shown but not lined up, and the state says
**Not calibrated**.

### Can gaze and target be lined up?

Compare coordinate names, ignoring case:

- `eye_tracker_space` gaze never can.
- If either name is empty, allow it, for older recordings.
- Otherwise the names must match.

Changing the empty-name rule could break older recordings. Test it with real
files.

### The stairs

- ID `stair-model-v1`, 20 samples, 0.02 m and 3 degree limits.
- Fixed `vicon_from_target` position `(-2.882676086, 0.310499985, 0.0)` m, with
  no rotation.

The stairs don't move. As measured on 2026-09-17, the bottom front-left corner is
120.5 cm straight ahead of and 21.3 cm left of the Vicon origin. Forward is `-X`,
left is `-Y`, and the floor is `Z = 0`, so the corner is at `(-1.205, -0.213, 0)`
m. The stairs go up along `-X`.

The stair OBJ file is in millimetres, and its matching corner is at
`(1677.676086, -523.499985, 0)`, not at the model's origin. Scale that by 0.001
and subtract it from the measured corner to get the fixed position above.

The built-in **Default stair setup** moves to this measurement when settings load,
until an alignment has been worked out with it. Saved calibrations keep what they
stored. To replace one made with the old estimate, pick **Default stair setup**,
run **Calibrate from Stair Target**, and save. A new HoloLens world always needs a
new calibration, even though the stairs haven't moved.

### A steady target

- Compare each tracked position with the first in the set.
- Losing tracking clears the set. Moving past either limit starts it again from
  the new position.
- Average the positions and rotations, lining up rotation signs first, since `q`
  and `-q` are the same rotation.
- Need at least 20, with the spread inside both limits.
- XDF playback uses the same rules to find a steady stretch.

### Working out the alignment

The target stream's `acquisition/sdk` may be the Vuforia model target or a manual
stair placement in Unity. Both use the same Unity stair model, so the preview
treats them the same.

The preview draws the OBJ in its own coordinates, but Unity's import flips X. So:

1. Invert the averaged `holo_from_target`.
2. Flip it in Z, back to the target's Unity coordinates.
3. Flip it in X, to the drawn model's coordinates.
4. Apply the fixed `vicon_from_target`.
5. Set the gaze input signs to `(-1, 1, -1)`, the X and Z flips together.

Two flips keep the handedness. Seen from the target, the change is
`(x, y, z) -> (-x, y, -z)`. An older version turned 180 degrees around Z instead
of flipping X, which swapped left and right in gaze. The inverted position and
the signs must change together, or it breaks as soon as the target turns.

Only the preview does this. Recordings keep the data as it was sent. Older saved
Vuforia calibrations keep what they stored, so run **Calibrate from Stair Target**
and **Save Session Calibration** again to replace a mirrored one.

## Live and recorded should match

Given the same values, the live and XDF preview should show the same markers,
segments (with raw rotations), and gaze rays, and use the same stair alignment.
`eye_tracker_space` gaze never uses automatic alignment. The frame times won't
match, since live uses the corrected clock and XDF starts at zero, but the matching
decisions should agree.
