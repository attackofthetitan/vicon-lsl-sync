# Behavior that must stay the same

This is what users and other programs can see: command-line options, LSL streams,
recorder commands, file names, and saved settings. A tidy-up must not change any
of it. If your change does, review it on its own, say what users will notice, and
say how old files and settings keep working.

The order things happen in is in [How each part starts, stops, and
recovers](runtime-state-machines.md). Clock and coordinate math is in [How time
and coordinates work](time-and-coordinate-semantics.md).

A few words used below:

- **Source ID**: a fixed name for a stream, so LSL and LabRecorder know it again
  after it reconnects.
- **Stream details**: the labels and settings attached to an LSL stream (LSL
  calls this metadata).
- **Irregular rate**: the stream doesn't promise a set number of samples per
  second.

## Command-line app

| Option | Default | Rule |
| --- | --- | --- |
| `--server <ip:port>` | `localhost:801` | The Vicon address. Several addresses split by `;` all go to the Vicon SDK, and the app connects once any of them answers. |
| `--marker-stream <name>` | `ViconMarkers` | Marker stream name. |
| `--segment-stream <name>` | `ViconSegments` | Segment stream name. |
| `--reconnect-interval <ms>` | `3000` | A whole number from 1 up to `INT_MAX`. |
| `--help` | | Prints help and exits with code 0. |

An unknown option, a missing value, or a bad interval prints an error and the
help, then exits with code 1. The old gaze options (`--no-hololens-gaze`,
`--gaze-port`, `--gaze-stream`) are still refused, because the Unity app sends
gaze straight to LSL. On start, the app prints the server and both stream names.

## LSL streams

### Both Vicon streams

| Item | Value |
| --- | --- |
| Type | `MoCap` |
| Format | `double64` |
| Rate | The Vicon frame rate if positive and finite, otherwise irregular |
| Source IDs | `vicon_markers_<hostname>` and `vicon_segments_<hostname>`, with `default` if the computer name can't be read |
| Timestamp | Best guess of when Vicon captured the frame, on this computer's LSL clock |
| Empty layout | No stream is opened, and sending counts as success |

Both streams carry these details, exactly:

- `acquisition/device = Vicon`
- `acquisition/sdk = ViconDataStreamSDK`
- `acquisition/nominal_srate`: the rate with six decimals, like `119.880000`, or
  `0.000000` when irregular, whatever the computer's language settings
- `acquisition/timestamp = estimated_acquisition_time`
- `acquisition/clock_domain = lsl_local_clock`
- `acquisition/timestamp_estimator = frame_number_cadence_anchored_to_receipt_minus_pipeline_latency`
- `acquisition/client_buffer_frames = 256`
- `acquisition/clock_drift_slew_limit_ppm = 1000`
- `acquisition/timestamp_fallback = immediate_receipt_time`
- `acquisition/latency_correction = GetLatencyTotal_pipeline_estimate`
- `acquisition/timestamp_accuracy = acquisition_estimate_not_capture_accurate`
- `synchronization/clock_domain = lsl_local_clock`
- `synchronization/timestamp_origin = local_receipt_minus_valid_vicon_pipeline_latency`
- `synchronization/offset_mean = 0`
- `synchronization/can_drop_samples = true`

Source IDs stay the same when streams reconnect or reopen. The bridge reopens
both streams when the layout changes or the frame rate moves by more than 1%.

### `ViconMarkers`

- Four values per `(subject, marker)`, in Vicon's order: `<subject>:<marker>:X`,
  `Y`, `Z` in `mm`, and `<subject>:<marker>:Valid` as `bool`.
- A good marker sends its position and `1.0`.
- A hidden, failed, or lost marker sends `NaN, NaN, NaN, 0.0`.

### `ViconSegments`

- Seven values per `(subject, segment)`, in Vicon's order: `X`, `Y`, `Z` in `mm`,
  then `QX`, `QY`, `QZ`, `QW` with the unit `quaternion`.
- A segment is good only when both its position and rotation read correctly.
  Otherwise it sends seven `NaN` values. There's no separate valid flag.

### `HoloLensGaze`

Defaults come from `stream-contracts/hololens-gaze.json`, which sets the names,
labels, order, and units for both the C++ and C# code.

- Name `HoloLensGaze`, type `Gaze`, source ID `hololens2_gaze`.
- Format `double64`, rate exactly 90 Hz, exactly 21 values.
- Values: both eyes combined, then the left eye, then the right eye. Each has an
  origin in `meters`, a direction that's `normalized`, and a valid flag as
  `bool`. An eye with no data keeps its place and is marked invalid.

The stream only opens after the tracker runs at 90 Hz and gives a position
anchor (`SpatialGraphNode`). It sends the original capture time, which must be
positive and finite. A sample with a bad time is dropped, never given a new time.

The declared rate is the rate the tracker was set to. A tracker that slows down
still reports that rate, so the app also measures the real rate and logs a
warning while it's below 80%. It doesn't reopen the stream, because the declared
rate can't change mid-stream and reopening would lose more data.

If the gaze reader keeps failing, the app stops the worker, finds the tracker
again, and reopens the stream. Any other serious error is logged and sending
stops.

Details, exactly:

- `acquisition/device = HoloLens2`
- `acquisition/sdk = Microsoft.MixedReality.EyeTracking`
- `acquisition/nominal_srate = 90`
- `acquisition/acquisition_mode = extended_eye_tracking_90hz` (the rate the
  tracker accepted)
- `acquisition/reading_retrieval = sequential_drain_after_last_capture`
- `acquisition/timestamp = eye_gaze_tracker_timestamp`
- `acquisition/timestamp_units = seconds`
- `acquisition/timestamp_conversion = lsl_query_time_minus_sdk_timestamp_age`
- `acquisition/capture_clock_domain = eye_gaze_tracker_datetime`
- `acquisition/clock_domain = lsl_local_clock`
- `acquisition/coordinate_frame = hololens_stationary_shared_with_gaze`
- `acquisition/coordinate_units = meters`
- `synchronization/clock_domain = lsl_local_clock`
- `synchronization/timestamp_origin = eye_gaze_tracker_reading_timestamp`
- `synchronization/timestamp_mapping = query_lsl_clock_minus_query_to_capture_age`
- `synchronization/can_drop_samples = true`
- `synchronization/backlog_policy = drop_when_capture_span_exceeds_500ms_retain_latest`

### `HoloLensModelTargetPose`

Defaults come from `stream-contracts/hololens-model-target.json`.

- Name `HoloLensModelTargetPose`, type `Calibration`, source ID
  `hololens2_stair_model_target`.
- Format `double64`, irregular rate, eight values: `PositionX`, `PositionY`,
  `PositionZ` in `meters`; `RotationX`, `RotationY`, `RotationZ`, `RotationW` as
  `normalized`; and `Tracked` as `state`.
- Sent every `LateUpdate`, timed with `LSL.local_clock()` read just before
  sending.

`Tracked` has three values:

- `1`: a live tracked position.
- `0`: no position. The other seven values are `NaN`. Normal tracking loss
  always sends this.
- `2`: Vuforia was turned off on purpose, so this is the last steady reference
  instead of a new measurement. The reference is the average of 20 positions
  that all stayed within 2 cm and 3 degrees of the first one, already in the
  published world (it isn't flipped twice). Turning Vuforia back on, or turning
  `VuforiaModelTargetPoseOutlet` off, clears it. A sudden jump while tracking
  throws the old reference away.

Readers must accept `2` as a valid fixed reference but keep it apart from live
tracking. Old readers that test `Tracked > 0.5` still work. The current preview
accepts only 0, 1, or 2. Never treat a frozen reference as a new measurement
just because its timestamp is new.

Details, exactly:

- `acquisition/device = HoloLens2`
- `acquisition/sdk = Vuforia.ModelTarget`
- `acquisition/reference_target = stair_model_target`
- `acquisition/coordinate_frame = hololens_stationary_shared_with_gaze`
- `acquisition/coordinate_units = meters`
- `acquisition/nominal_srate = 0`
- `acquisition/acquisition_mode = unity_rendered_pose`
- `acquisition/timestamp = lsl_local_clock_at_pose_publication`
- `acquisition/clock_domain = lsl_local_clock`
- `acquisition/pose_state_version = 2`
- `acquisition/tracking_states = 0=invalid,1=live_tracked,2=frozen_reference`
- `acquisition/pose_retention = stable_reference_while_vuforia_disabled`
- `acquisition/reference_window_samples = 20`
- `synchronization/clock_domain = lsl_local_clock`
- `synchronization/timestamp_origin = local_clock_at_pose_publication`
- `synchronization/offset_mean = 0`
- `synchronization/can_drop_samples = true`

### What counts as a stream change

A new default name, type, or source ID; a new number of values, order, label,
unit, or missing value; or a new detail, rate, timestamp rule, coordinate name,
or reopen rule. Each one needs its own plan.

## LabRecorder

### Remote control

- Default address `localhost:22345`.
- Each command ends with a newline. Only one group of commands runs at a time,
  and the next command goes only after the reply starts with `OK`.
- Command groups:
  - Refresh: `update`
  - Change file name: `filename`
  - Start: `filename`, `start`
  - Start in **Record every visible stream** mode: `update`, `select all`,
    `filename`, `start`, so streams that appeared since LabRecorder last looked
    are included
  - Stop: `stop`

### Recording only some streams

The LabRecorder window can't be told to record only some streams. So when you
pick streams, the app starts the bundled `LabRecorderCLI` with the full output
path and one search per stream. A search uses the source ID if there is one, and
otherwise the name plus the computer. The list is fixed at launch.

A picked stream counts as visible if the stream search finds it, or the preview
got a sample from it in the last second. A picked stream that isn't visible is
left out, with a warning in the event log. Stop sends Enter to the recorder.

### File names

| Token | Value |
| --- | --- |
| `%p` | Participant |
| `%s` | Session |
| `%b` | Task or block |
| `%r`, `%n` | Run |
| `%a` | Acquisition |
| `%m` | Modality |

The default pattern is
`sub-%p/ses-%s/%m/sub-%p_ses-%s_task-%b_acq-%a_run-%r_%m.xdf`.

Recording only starts when:

- The study folder exists.
- The template is a relative path.
- Participant, session, task, acquisition, modality, and a positive run are
  filled in, and no `%` token is left over.
- The final file is inside the study folder, unless "allow outside the study
  folder" is on.

`.xdf` is added if it's missing. The app refuses `..` steps and links that leave
the folder, full paths as templates, names or characters Windows doesn't allow,
names ending in a space or dot, paths that are too long, places it can't write,
and existing files unless overwriting is on. Braces and line breaks would break
the recorder command, and a `%` in a value would be read as a placeholder, so
they're refused with the field to fix. Spaces at the
ends are trimmed. Missing folders are only made once Start is accepted.

The path shown in **Recording Destination** is exactly the path the recorder
gets. LabRecorder's window would lowercase a template, switch to `%n`, and pad
the run to three digits, so the app fills in the tokens itself: it sends
`template` as `%b` and puts the finished path in `task`. The other fields are
still sent so LabRecorder shows them, but they don't change the path.

**Find Next Run** tries up to 1,000 run numbers for one whose file doesn't exist.
The automatic run increase only happens after the file exists and passes its
check.

### Starting and closing LabRecorder

- Check the recorder address first, and never start a second copy if something
  answers there.
- Use the program the user picked, or else the one in the `labrecorder` folder
  next to the app.
- Starting it never freezes the window. Keep at most 64 KiB of its output.
- Only close a recorder this app started, and only after Stop is done or 15
  seconds pass. Give it one more second, then force it. A recorder someone else
  started is never closed.
- On Linux and macOS, a recorder started here ignores `SIGPIPE`, so it keeps
  recording after the app quits.

## Desktop app

- Start takes the server and stream names, saves them, locks those fields, and
  starts one bridge worker.
- The status shows the bridge state, marker and segment counts, the frame number,
  and a rate the app works out itself. Updates come every 100 frames. With no
  update for three seconds while streaming, the status is marked out of date and
  the rate shows `0.0 Hz`.
- Indicators always show the session, bridge, recorder, preview, calibration,
  file, path, and file check. A normal update never wipes the last error.
- The event log keeps 1,000 entries. It can be copied or exported with the setup,
  stream lists, rates, setup and file checks, but never recording samples.
- The path is checked, and a valid file name sent to a connected, idle
  LabRecorder, 300 ms after you stop typing, never on every key, because the
  check writes a test file in the study folder. During Start, the new path goes
  with the start command instead.

### Setup check

Items are required, warning, or information. They cover how recent the bridge
status is, whether the recorder is ready, the path, the picked streams, sample
age, channel layout, coordinate names, rates, the stair model, and calibration.

- A required failure blocks Start, unless **Record Anyway** gets a reason. The
  reason only covers that one Start, and is saved with the session details.
- A declared rate more than 1% off the saved one, or a stream arriving below 80%
  of its declared rate, is a warning.
- Recorder-only mode turns the bridge check into information.
- A recorder window whose address isn't this computer is a warning, because the
  file check and run numbers only work if the study folder is the same shared
  path on both computers.

## Preview

### Live data

- By default, the marker and segment preview follow the bridge's stream names.
  **Preview external streams** lets you set them yourself.
- Each role follows one source ID. **Follow by name** is an opt-in for source IDs
  that change between runs. A missing source ID never quietly turns into a match
  by name. Two streams with the same name and no source ID are reported as
  unclear. If one source comes back several times, the newest copy is picked and
  the app says so.
- If the stream details are incomplete, the fixed HoloLens labels are used, with
  a warning.
- Vicon positions are shown in metres.
- Only one frame waits to be drawn. Skipped input and replaced frames are counted
  separately and aren't lost data.
- Stair alignment only starts when asked, uses 20 steady target samples, and lasts
  until the app closes unless saved. You can't type in an alignment. Without one,
  gaze is drawn as the HoloLens sent it.

### CSV files

- Loads in the background and keeps showing what was there before. A cancelled or
  failed load never leaves half a recording.
- The first row holds the column names.
- Frame time is `relative_time`, or else `lsl_time` minus the first `lsl_time`,
  or else the row number from zero.
- A row whose time is earlier than the one before it stops the load with an
  error naming the row. Equal times are fine.
- Numbers use a decimal point, whatever the language settings.
- A column group with `QX`, `QY`, `QZ`, and `QW` is a segment, never a marker.

### XDF files

- List every stream first. Number streams are read, text streams are counted and
  skipped, and a file with nothing usable is refused.
- Only a final chunk cut off mid-way is skipped. Any other broken data is an
  error.
- Numbers in stream headers, like `nominal_srate`, use a decimal point whatever
  the language settings.
- Clock corrections are applied once, and times are fixed so they always go up.
  The repair count covers every sample, not just the ones kept for drawing.
- Pieces of a restarted stream are joined. The same source ID on different
  computers is kept apart. If roles clash, the user picks the main stream and
  which groups to include.
- The summary lists the main stream, the chosen and left-out groups, joined
  pieces, unmatched percentages, time ranges, and clock corrections.
- Old `eye_tracker_space` gaze can be shown but is never lined up with the stairs.

CSV and XDF share the same playback: a timeline, play and pause, jumps that keep
the speed, single steps, jumps to the start and end, a loop switch, recent files,
drag and drop, and **Export Image**, which never changes the data.

### Limits

- Memory limit: 16 to 2,048 MiB, set by the user.
- At most 200,000 decoded frames, and 2,000,000 values per XDF stream.
- Safety limits: 64 GiB per XDF file, 100,000,000 declared samples per stream,
  65,536 channels, 4,096 streams, and a 4 MiB stream header. Going over one is an
  error.
- Loading checks for cancel at least every 1,024 steps and aims to stop within
  250 ms. The live preview aims for under 100 ms of delay.

### Saved calibrations

A saved calibration holds an ID and name, setup name, stair model path and
identity, the measured stair position, the gaze alignment, coordinate names,
notes, creation time, sample count, position and angle error, whether missing
details were confirmed, and a hidden flag. Some stored names are old on purpose
(`translationRmsM`, `rotationRmsDegrees`, `retired`) so older files still load.

- You can pick, apply, copy, hide, import, and export them. Picking only shows
  one. The quality shown is always for the calibration in use.
- **Clear Calibration** goes back to the HoloLens's own coordinates.
- One with no measured gaze alignment, like the built-in stair setup, can't be
  applied. Run **Calibrate from Stair Target** first.
- **Save Session Calibration** updates the calibration the current one came
  from. With a different one picked, it saves a new entry instead.
- If coordinate details are missing, the user must confirm before it's complete.

### File check after recording

After Stop, the app waits for the exact file and checks it in the background. It
compares the recorded streams with the list from before Start: name, source ID,
computer, channels, time range, sample count, rate, gaps, clock corrections, and
repaired timestamps. A stream with a set rate that stops more than two seconds
(or ten samples, if longer) before the end gets a warning. The result is
**Checked**, **Checked with warnings**, or **Needs attention**. A Stop reply alone
never counts as proof the data was saved. The check never changes the file, and
closing the app skips it.

## Saved settings

- Settings use organization `ViconLSL` and application `ViconLSLBridge`.
- The setup is version-1 JSON at `session/configuration`, with the version in
  `session/configurationVersion`. Presets and import and export use the same
  format.
- Settings from older app versions aren't brought over, on purpose. An unknown
  version is refused, never guessed at.
- Window size, layout, tabs, the last ten recordings, and last-used folders are
  kept out of presets.
- Calibrations are stored at `session/calibrationProfiles`. Saving keeps any
  entry this version can't read, unchanged, so a newer version's calibrations
  survive a run of an older one.

## Build and package names

Keep:

- CMake options starting with `VICON_LSL_` or `VICON_LSL_BRIDGE_`.
- The targets `vicon-lsl-bridge-logic`, `vicon-lsl-bridge-runtime`,
  `vicon-lsl-bridge`, `vicon-lsl-bridge-gui`, and the Windows package targets.
- Program names, and the packaged `labrecorder` folder (with `LabRecorder` and
  `LabRecorderCLI`), `stair_model`, runtime, and license folders.
- The Linux archive: both programs at the top with `qt.conf`, their libraries in
  `lib`, Qt plugins in `plugins`, and LabRecorder in `labrecorder` with its own
  `labrecorder/lib`. Everything is found through `$ORIGIN`, never a build folder.
- The C++ tests that need no runtime or download, the generated stream check, and
  the C# tests that need no headset.
