# Behavior that must stay the same

## What this guide is for

This guide lists behavior that a code tidy-up must not change. It covers the command-line app, LSL streams, recording controls, preview files, saved settings, and build names.

If a change affects anything here, review it on its own. Say what users will notice, how old files and settings will still work, and how you will test it.

For exact clock and coordinate rules, see [How time and coordinates work](time-and-coordinate-semantics.md).

Words used below:

- **Stream details** are the labels and settings attached to an LSL stream (LSL calls this metadata).
- **Source ID** is a fixed name for a stream that lets LSL and LabRecorder recognise it again after it reconnects.
- **Finite** means a real number: not `NaN`, and not plus or minus infinity.
- **Irregular rate** means the stream does not promise a set number of samples per second.
- **Normalized** means a direction or rotation has been scaled to length 1. Some stream unit fields use the exact word `normalized`.

## Command-line app

`vicon-lsl-bridge` takes these options:

| Option | Default | Rule |
| --- | --- | --- |
| `--server <ip:port>` | `localhost:801` | The Vicon DataStream address. |
| `--marker-stream <name>` | `ViconMarkers` | The marker stream name. |
| `--segment-stream <name>` | `ViconSegments` | The segment stream name. |
| `--reconnect-interval <ms>` | `3000` | A whole number from 1 up to `INT_MAX`. |
| `--help` | None | Prints help and exits with code 0. |

An unknown option, a missing value, or a bad reconnect interval prints an error and the help text, then exits with code 1.

The old gaze relay options `--no-hololens-gaze`, `--gaze-port`, and `--gaze-stream` are still rejected. The Unity app sends HoloLens gaze straight to LSL.

On start, the app prints the server, marker stream, and segment stream it will use. It does not say it passes on gaze.

Keep every option name, default, allowed value, and exit code, and keep HoloLens
data out of this app.

## Desktop bridge

### Connect and retry

- Connect to Vicon in `ServerPush` mode.
- Turn on marker and segment data.
- If connecting fails, wait for the reconnect interval and try again until told to stop.
- Wait in steps of no more than 100 ms so Stop takes effect quickly.
- Read one Vicon frame before opening any LSL stream.
- If setup, the first frame, or reading the layout fails, disconnect and try again. Never publish half a layout.
- The first time reading the first frame fails, retry right away, because a
  server that is still starting up usually answers on the next try. If it keeps
  failing, wait the reconnect interval before each later retry, so a server that
  accepts connections but never sends a frame cannot cause a tight retry loop.
  Getting past the first frame resets the count.
- Treat the connection as lost when either our own record or the Vicon SDK says
  it is no longer connected.
- `stop()` only flips the run flag.
- Calling `run()` on a stopped `ViconLSLBridge` does not flip that flag back. A stopped bridge cannot be reused for a new session.

### Read the layout

- Keep subjects, markers, and segments in the order the Vicon SDK gives them.
- If any count or name read fails, stop and throw away the half-read layout.
- An empty marker or segment layout is fine. No LSL stream is opened for an empty group, and sending to it counts as success.
- Re-read the layout after every 100 frames handled.
- If the marker or segment layout changes, close and reopen both Vicon streams.
- If one of these repeat layout checks fails, report the error but do not say the layout changed.

### Read and send frames

- Keep the Vicon frame number, subject or object name, action name, SDK result, and readable error message all the way to the point where values are sent to LSL.
- Marker and segment samples from the same frame get the same timestamp.
- If sending to either LSL stream fails, end the session: close both streams, disconnect from Vicon, and reconnect.
- Keep source IDs the same when streams reconnect or are reopened.
- Keep the timestamp record across reconnects so time never goes backward.
- When cleaning up a session, clear the layout, frame counters, grouped errors, and last error before reporting `Disconnected`.

### Report errors and status

- Group read errors by action, subject, object, SDK result, and message.
- By default, log the first copy of an error and every 100th repeat.
- The summary shows how many times it repeated and the first error text.
- The first clean frame after an error clears the group and reports that things recovered.
- `BridgeStatus` holds the state, marker count, segment count, Vicon frame number, and a message.

## LSL streams

### Rules for both Vicon streams

| Item | Value |
| --- | --- |
| Type | `MoCap` |
| Value format | `double64` |
| Expected rate | The Vicon frame rate if it is positive and finite; otherwise irregular |
| Marker source ID | `vicon_markers_<hostname>` |
| Segment source ID | `vicon_segments_<hostname>` |
| No computer name | Use `default` |
| Timestamp | Best guess of when Vicon captured the frame, on this computer's LSL clock |
| Layout change | Close the old stream and open a new one |
| Empty layout | Open no LSL stream and report success |

Both Vicon streams carry these exact details:

- `acquisition/device = Vicon`
- `acquisition/sdk = ViconDataStreamSDK`
- `acquisition/timestamp = estimated_acquisition_time`
- `acquisition/clock_domain = lsl_local_clock`
- `acquisition/timestamp_estimator = immediate_receipt_minus_valid_pipeline_latency`
- `acquisition/timestamp_fallback = immediate_receipt_time`
- `acquisition/latency_correction = GetLatencyTotal_pipeline_estimate`
- `acquisition/timestamp_accuracy = acquisition_estimate_not_capture_accurate`
- `synchronization/clock_domain = lsl_local_clock`
- `synchronization/timestamp_origin = local_receipt_minus_valid_vicon_pipeline_latency`
- `synchronization/offset_mean = 0`
- `synchronization/can_drop_samples = true`

### `ViconMarkers`

- The default name is `ViconMarkers`. Users can change it.
- Each `(subject, marker)` pair adds four values, in the order Vicon lists them.
- The labels are `<subject>:<marker>:X`, `<subject>:<marker>:Y`, `<subject>:<marker>:Z`, and `<subject>:<marker>:Valid`.
- X, Y, and Z are in `mm`. `Valid` is `bool`.
- A good marker sends its XYZ and `1.0`.
- A hidden marker, SDK error, or lost marker sends `NaN, NaN, NaN, 0.0`.

### `ViconSegments`

- The default name is `ViconSegments`. Users can change it.
- Each `(subject, segment)` pair adds seven values, in the order Vicon lists them.
- The labels are `<subject>:<segment>:X`, `<subject>:<segment>:Y`, `<subject>:<segment>:Z`, `<subject>:<segment>:QX`, `<subject>:<segment>:QY`, `<subject>:<segment>:QZ`, and `<subject>:<segment>:QW`.
- X, Y, and Z are in `mm`. The four rotation numbers use the exact unit name `quaternion`.
- A segment is good only when both its position and rotation read correctly.
- If either read fails or is hidden, send seven `NaN` values.
- There is no separate "valid" value for segments.

### `HoloLensGaze`

- Default name: `HoloLensGaze`.
- Default type: `Gaze`.
- Default source ID: `hololens2_gaze`.
- Value format: `double64`.
- Expected rate: exactly 90 Hz.
- Number of values: exactly 21.

`stream-contracts/hololens-gaze.json` sets the default names, labels, order, and units for both the C++ and C# code. The values come in this order: both eyes' origin, direction, and valid flag; then the left eye's; then the right eye's.

Origins are in `meters`, directions are `normalized`, and valid flags are `bool`.

The app opens the stream only after the eye tracker says it is running at 90 Hz and gives a position anchor (a spatial graph node). It sends the original capture time, which must be positive and finite. If a sample has a bad time, the app drops it instead of making up a new time.

The declared rate is the rate the tracker reported for the mode it accepted. It
is fixed in the stream header and says what the device was asked for, not what
it actually delivers: a tracker that slows itself down still reports the rate it
was set to. So the app also measures the real rate from the capture times it
accepts, and logs a warning while that stays below 80% of the declared rate. It
does not reopen the stream when this happens, because the declared rate cannot
change partway through and reopening would lose more data than the low rate
does.

If the tracker has no data for one eye, that eye still takes up its place in the 21 values and is marked invalid.

If the gaze reader keeps failing, the app stops the worker, looks for the tracker again, and later reopens the stream. Any other serious worker or stream error logs the error and turns publishing off.

The gaze stream carries these details:

- `device = HoloLens2`
- `sdk = Microsoft.MixedReality.EyeTracking`
- `acquisition_mode = extended_eye_tracking_<selected>hz`, using the rate the tracker reported for the mode it accepted
- `reading_retrieval = sequential_drain_after_last_capture`
- `timestamp = eye_gaze_tracker_timestamp`
- `timestamp_units = seconds`
- `timestamp_conversion = lsl_query_time_minus_sdk_timestamp_age`
- `capture_clock_domain = eye_gaze_tracker_datetime`
- `clock_domain = lsl_local_clock`
- `coordinate_frame = hololens_stationary_shared_with_gaze`
- `coordinate_units = meters`
- Backlog rule: `drop_when_capture_span_exceeds_500ms_retain_latest`

### `HoloLensModelTargetPose`

`stream-contracts/hololens-model-target.json` sets these defaults and the value layout:

- Name: `HoloLensModelTargetPose`.
- Type: `Calibration`.
- Source ID: `hololens2_stair_model_target`.
- Value format: `double64`.
- Rate: irregular.
- Number of values: eight.

The values are `PositionX`, `PositionY`, and `PositionZ` in `meters`; `RotationX`, `RotationY`, `RotationZ`, and `RotationW` in `normalized`; and `Tracked` in `state`.

Unity uses left-handed coordinates, so a tracked Unity position and rotation are flipped into the right-handed coordinates we publish:

- Position `(x, y, z)` becomes `(x, y, -z)`.
- Rotation `(x, y, z, w)` becomes `(-x, -y, z, w)`.

`Tracked = 1.0` means a live, tracked position. Normal tracking loss sends seven
`NaN` values and `Tracked = 0.0`. Turning Vuforia off on purpose keeps the last
steady reference, made from 20 positions, and sends it with `Tracked = 2.0`.
That reference averages the positions and rotations (with rotation signs lined
up first) in the already-flipped shared world. It is not flipped a second time.
All 20 samples must stay within 2 cm and 3 degrees of the first one. Without a
steady reference, paused samples stay invalid. Turning Vuforia back on or
turning the outlet off clears the reference. If a tracked position suddenly
jumps, the old reference is thrown away before a new one is collected.

Compatibility: the stream keeps its eight channels, labels, order, source ID,
and coordinate name. Its header adds `pose_state_version = 2`, the three
`tracking_states`, and `pose_retention = stable_reference_while_vuforia_disabled`.
Anything that used to read `Tracked` as yes/no must now accept 2 as a valid
fixed reference while still telling it apart from live tracking. Older built-in
readers that accept `Tracked > 0.5` can use it. The current reader accepts only
0, 1, or 2 and rejects anything else. Never treat a frozen reference as a new
measurement just because its timestamp is new or its values do not vary.

Read `LSL.local_clock()` in `LateUpdate` just before packing and sending the sample. Use `hololens_stationary_shared_with_gaze` as the coordinate name.

Each of these counts as a stream change that needs its own plan:

- A new default name, type, or source ID.
- A new number of values, order, label, unit, or invalid value.
- A new detail value, rate, timestamp rule, coordinate name, or reopen rule.

## LabRecorder remote control

### Connection and replies

- Default host: `localhost`.
- Default port: `22345`.
- A request to connect again is refused while Start, Stop, confirmed recording,
  or a Start that may have been sent is still active on the current connection.
- Otherwise, the app stops both timers, fails any work in progress, closes the
  old connection, sets the recording state to unknown, and starts the new
  connection timer. Reconnecting after the connection was lost still remembers
  that a Start may have been sent, until Stop is confirmed.
- End each command with a newline.
- Send one group of commands at a time.
- Send the next command only after the current reply starts with `OK`.
- Skip spaces, tabs, and line breaks before `OK`.
- Keep partial replies until enough text arrives.
- An unexpected reply, connection error, command timeout, or disconnect fails
  the current group.
- It also sets the recording state to unknown and closes the connection.
- The connection timeout and command timeout are separate.

The app tracks whether it is connected, the last recording state LabRecorder
confirmed, and the state it is trying to reach. What it is doing right now is one
of `Idle`, `Refreshing`, `UpdatingFilename`, `Starting`, `Stopping`, or
`ShuttingDown`. The dashboard shows which command in the current group is
waiting for a reply. While a group is running, other work is refused. This stops
repeated Start or Stop clicks and keeps other commands out of the Start steps.

### Command order

- Refresh: `update`
- Change file name: `filename`
- Start without choosing streams: `filename`, `start`
- Start in **Record every visible stream** mode: `update`, `select all`,
  `filename`, `start`
- Stop: `stop`

`update` and `select all` must run before Start so LabRecorder includes streams that appeared since it last looked.

The LabRecorder window cannot be told over remote control to record only
certain streams. So when you pick exact streams, the app starts the bundled
`LabRecorderCLI` with the checked full output path and one search per chosen
stream. Each search uses the source ID when there is one, and otherwise the name
plus the computer it comes from. The list of streams is fixed before launch.
Pressing Stop sends Enter to the command-line recorder, which tells it to finish. Because the app
started this recorder, it may close it on shutdown once recording has stopped.

### File name fields

| Token | Value |
| --- | --- |
| `%p` | Participant |
| `%s` | Session |
| `%b` | Task or block |
| `%r`, `%n` | Run |
| `%a` | Acquisition |
| `%m` | Modality |

The same cleaned-up file name is used for checking, display, the session
details, making folders, and the recorder command. Braces and line breaks would
break the recorder command, so the app refuses them and says which field to fix.
It never quietly changes a path it accepted. Spaces at the start and end are
removed. Empty optional fields are left out of the remote `filename` command.

Do not start recording unless all of these hold: the study folder is a full path
that exists; the template is a relative path; participant, session, task,
acquisition, modality, and a positive run are filled in; no `%` token is left
over; and the final file stays inside the study folder, unless the advanced
"allow outside the study folder" option is on. Add `.xdf` if the template does
not already end with it. Refuse `..` steps that climb out of a folder, links
that point outside it, full paths as templates, names or characters Windows does
not allow, names ending in a space or a dot, paths that are too long, places that
cannot be written to, and files that already exist unless overwriting is turned
on. Missing folders are only created once Start is accepted. Low or unknown free
space shows a warning at the level you set; it is never silent.

The path in **Recording Destination** must be exactly the path given to the
recorder. So the app fills in the tokens itself instead of letting the
LabRecorder window do it. That window would lowercase a template, switch it to
the old run counter `%n`, and pad the run to three digits, and so would write to
a different path than the one checked here. Instead, the remote command sets
`template` to `%b` and puts the finished relative path in `task`, which is the
one field LabRecorder fills in first and copies without changing case. `root`,
`participant`, `session`, `run`, `acquisition`, and `modality` are still sent so
the recorder window shows the same details, but they no longer change the path.
Recording details are edited in this app.

**Find Next Run** tries up to 1,000 positive run numbers to find one whose file
does not exist yet. The optional automatic run increase only happens after the
file exists and the chosen file check rule has passed.

The default pattern is:

`sub-%p/ses-%s/%m/sub-%p_ses-%s_task-%b_acq-%a_run-%r_%m.xdf`

### Starting and closing LabRecorder

- Check the recorder address before starting LabRecorder. Use the program the
  user picked if it is valid. Otherwise use `labrecorder/LabRecorder.exe` next to
  the desktop app, if it is there.
- Starting it never freezes the window. It runs from the program's own folder.
  The app records whether the recorder was already running, is starting here,
  was started here, has stopped, failed to start, or was detached.
- Keep at most 64 KiB of recorder output, and send a limited number of lines to
  the event log. A slow or failed start never makes the window wait.
- Try the remote connection every 250 ms for up to 15 seconds, but only while it
  is not connected and not already connecting.
- Disconnecting from, or closing the app around, a recorder someone else started
  never closes it. Detach leaves a recorder started here running and stops the
  app from closing it later.
- On shutdown, close a recorder started here only after Stop is done or the
  15-second recorder limit runs out. Give it one more second to close, then force
  it to close.

## Desktop app

- Start takes the current server and stream names, saves them, locks those fields, starts one bridge worker, and enables Stop.
- The status shows the bridge state, marker and segment counts, frame number, and a rate the app works out itself.
- If no update arrives for three seconds while streaming, the status counts as out of date. The rate then shows `0.0 Hz`, and the readiness check says the status is out of date.
- Normal streaming updates arrive every 100 frames, in step with the layout check, not every frame.
- Separate indicators always show the session, bridge, recorder, preview,
  calibration, file, path, and file check state. A normal status update never
  wipes the last error. The event log keeps up to 1,000 timed entries. It can be
  copied or exported together with the setup, stream lists, state changes,
  rates, setup checks, shutdown, and file check data, but not recording samples.
- Recording buttons depend on the connection, confirmed state, wanted state,
  current action, recorder process, path, and setup check. The dashboard shows
  `STARTING`, `RECORDING`, or `STOPPING`, time elapsed, final file path, run,
  recorder address, who started the recorder, stream health, free space, older
  input that was skipped, and how many display frames were replaced.
- A valid file name change is sent to a connected, idle LabRecorder 300 ms after you stop typing.
- **Start Session** walks through starting the bridge, starting the preview,
  finding streams, the setup check, and recording, while every separate control
  stays available. **Stop Session** does it in reverse: recording, preview,
  bridge, then a recorder the app started, if asked. If only some steps
  finished, you can see which ones and stop them.
- The setup check sorts these into required, warning, or information: how recent
  the bridge status is, whether the recorder is ready, the exact path, chosen
  streams, sample age, channel layout, coordinate details, expected rate, stair
  model, and calibration. Recorder-only mode drops the bridge requirement on
  purpose. A required failure blocks Start unless **Record Anyway** is given a
  reason. The result and reason are saved with the session details. **Record
  Anyway** is only offered while the latest check has a required failure that
  has not been accepted.

Closing always follows the same steps without freezing the window. It refuses
new work, cancels stream searches and file checks, asks the preview and bridge
to stop, and asks the recorder to shut down exactly once. If Start is already
running, that group finishes first and then the recorder gets exactly one Stop.
Closing again does not start the steps over. The window stays open and responsive
until every part that must stop has stopped. The four-second bridge, two-second
preview and file, and 15-second recorder limits are shown as status. They are
not permission to kill work that is still running. Only a recorder this app
started may be closed. A recorder someone else started is left alone, even after
the connection is lost, which is logged as `Recorder connection lost`. Normal
window actions, including Stop, should take no more than 50 ms, and no window
clean-up waits forever.

## Preview

### Live data

- By default, the marker and segment preview follow the bridge's stream names.
  **Preview external streams** lets you set them separately.
- For each stream, find its name, type, source ID, computer, session ID,
  publisher ID and start time, channel count, expected and measured rate,
  coordinate name, sample age, and whether its channels look right. Each role
  normally follows one source ID. **Follow by name** is an opt-in for source IDs
  that change between runs.
- If the chosen source ID is missing, the app does not quietly fall back to the
  name. Two streams with the same name and no chosen source ID are reported as
  unclear. If several copies of the same source ID are visible after it came
  back, the newest one is picked and the app says so. Picking by name is shown
  too.
- If a stream is missing, try again once a second. Stream searches wait at most
  50 ms, stream detail reads at most 250 ms, and sample reads never wait.
- Read the full LSL stream details when possible. If they are incomplete, use
  the fixed HoloLens labels, but log a warning.
- Ask LSL to correct the clock difference between computers for live data.
- A stream counts as fresh for 500 ms after its newest sample.
- Read at most 16 samples from a stream at a time and keep the newest.
- A new marker sample sets the frame time. Add segment and gaze data only if each is fresh and within the time limit, which is 50 ms by default.
- If there is no new marker, a new segment or gaze sample can make a frame on its own. If both are new, use the later time.
- `*_stream_present` means the LSL input is connected. It does not mean the stream is fresh or has usable values.
- Show Vicon positions in metres instead of millimetres. Gaze is already in metres.
- Keep only one live frame waiting to be drawn. The window draws at 30 or 60 Hz,
  while rate and calibration measurements keep going on their own. Show preview
  delay, skipped older input, and replaced display frames separately. These
  skips are on purpose and are not lost source data.
- Automatic stair alignment only starts when asked. It uses 20 steady target
  samples and only lasts until the desktop app closes, unless you save it as a
  calibration. You cannot type in an alignment. Without a solved or applied
  calibration, gaze is drawn as the HoloLens sent it.

### Merged CSV files

- Load in the background and keep showing the last usable live or recorded data.
  Report progress and check for cancel between small batches of lines or
  samples. A cancelled or failed load never leaves half a recording behind.
- The first row holds the column names.
- Use `relative_time` for frame time when it is there.
- Otherwise, subtract the first finite `lsl_time` from each finite `lsl_time`.
- Otherwise, use the row number, starting at zero.
- Turn marker, segment, and gaze columns into the shared `PreviewFrame` form.
- Keep a set of frames within the memory limit and skip frames when drawing if
  needed. Keep the exact source timing separately.

### XDF files

- List every stream before building frames. Read the number streams the preview
  understands, count and skip text streams, and refuse a file that has nothing
  the preview can use.
- Ignore a cut-off final chunk only when its length or declared body is cut off. Report any other broken data as an error.
- A missing timestamp can only be filled in when there is an earlier timestamp and a positive expected rate.
- Work out and apply the recorded clock corrections exactly once.
- Fix corrected timestamps so they always go up.
- Group candidate streams by role, source ID, name, computer, and channel layout.
  Join matching pieces across their whole time range. The same source ID on
  different computers is not assumed to be the same stream.
- Suggest a main stream in this order: markers, segments, any other Vicon stream
  the preview understands, then gaze. If clashing candidates are left over, ask
  the user to choose the main stream and which groups to include.
- Match every other stream to the nearest corrected full timestamp within the chosen time limit.
- Show playback time from zero, starting at the first corrected main-stream timestamp.
- Gaze in the shared world can use automatic stair alignment. Old `eye_tracker_space` gaze can be shown but is never lined up from the target.
- The summary lists the main stream ID, chosen groups and stream IDs, left-out
  groups, joined pieces, unmatched percentages, time ranges, and the clock
  corrections applied.

CSV and XDF playback share a timeline, current time and length, frame position,
play and pause, jumping that keeps the current speed, single-frame steps, jumps
to the start and end, time jumps you can set, and a loop switch. The playback
buttons, timeline, and **Open Recent** are only enabled when there is something
for them to act on. Recent files and drag-and-drop are supported. **Export
Image** saves only the current picture and never changes the data. The drawing
code has Fit View, Reset Camera, growing bounds, axes and units, a legend,
usable/total counts, trail clean-up when the layout changes, colours that follow
the system theme, and can draw without a screen and without OpenGL.

### Playback limits and speed

- CSV reading keeps one set of frames within the memory limit. XDF loading can
  briefly hold a file index, the chosen streams' data, and decoded frames, each
  within the 16–2,048 MiB limit you set. Only decoded frames stay after loading.
- The decoded preview holds at most 200,000 frames, and one XDF stream keeps at
  most 2,000,000 values. Safety limits are 64 GiB per XDF file, 100,000,000
  declared samples per stream, 65,536 channels, 4,096 streams, and a 4 MiB
  header. Going over a limit is an error; the app does not try to allocate it.
- File loading checks for cancel at least every 1,024 steps and aims to stop
  within 250 ms. Progress covers reading, indexing, stream details, timestamps,
  calibration, and frame preparation. The live preview aims for less than 100 ms
  of delay.

### Saved calibrations

A version-1 saved calibration holds an ID and display name, setup name, stair
model path and identity, the measured fixed Vicon stair position, the gaze
alignment, gaze and target coordinate names, setup notes, when it was made,
sample count, position and angle error, whether missing stream details were
confirmed, and a hidden flag. The stored error fields are still named
`translationRmsM` and `rotationRmsDegrees`, and the hidden flag is still named
`retired`, so older files keep working. Saved calibrations can be picked,
applied, copied, hidden, imported, and exported. Picking one only shows it. The
quality shown still describes the calibration actually in use, and one that is
only picked is marked as not applied. Buttons that need a picked calibration, a
running preview, or a calibration in use are only enabled then. Applying one is
visible and can be undone with **Clear Calibration**, which takes the preview
back to the HoloLens's own coordinates. A new automatic result only lasts for
this session until **Save Session Calibration** is chosen. Collection progress,
quality, rejection reasons, and whether coordinates match stay visible. If
coordinate details are missing, the user must confirm before a saved
calibration is complete.

### Check the file after recording

After Stop is confirmed, wait for the exact file to appear and check it in the
background. Compare the list of streams saved before Start with the recorded
name, source ID, computer, channel layout, time range, sample count, measured
rate, gaps, clock corrections, and repaired timestamps. The result is `Checked`,
`Checked with warnings`, or `Needs attention`. A Stop reply alone is never shown
as proof that data was saved. The file check never changes or deletes the XDF.
The findings go into the session details, and the file can be opened straight
into playback.

## Saved settings

Settings use organization `ViconLSL` and application `ViconLSLBridge`.

The session setup is saved as version-1 JSON at `session/configuration`.
`session/configurationVersion` holds the format version. The setup holds the
bridge address and stream names, chosen stream IDs and how they are matched,
preview limits, recorder address and stream choice, file path rules, session
options, and the chosen saved calibration. Named presets and JSON import and
export use the same format.

The settings format starts at version 1 and is stored as one JSON value.
Settings from older versions of the app are not brought over on purpose. Unknown
format versions are refused instead of guessed at or rewritten.

Computer and window state is kept out of presets on purpose: `ui/windowGeometry`,
`ui/mainSplitter`, the open control and preview tabs, up to ten recent
recordings, and the last folders used for presets and session details. Saved
calibrations are stored on their own at `session/calibrationProfiles`.

Changing the version-1 format needs a deliberate format change, plus tests that
the old version is refused and the new one saves and loads correctly.

## Build and package names

Keep:

- CMake options starting with `VICON_LSL_` or `VICON_LSL_BRIDGE_`.
- The targets `vicon-lsl-bridge-logic`, `vicon-lsl-bridge-runtime`, `vicon-lsl-bridge`, `vicon-lsl-bridge-gui`, and the Windows package targets.
- The C++ tests that run without the runtime, the desktop app, or any download.
- Program names, and the packaged `labrecorder` (with `LabRecorder.exe` and
  `LabRecorderCLI.exe`), `stair_model`, runtime, and license folders.
- The generated stream check and the C# test project that runs without a headset.

Do dependency updates and package layout changes separately from a code tidy-up.

## Before merging a code tidy-up

Tick every line the change could affect:

- [ ] Public headers still compile from the same paths with the same names and signatures.
- [ ] Command-line defaults, help, errors, output, and exit codes match the last version.
- [ ] Marker and segment order, units, invalid values, rates, source IDs, timestamps, and LSL details match the saved expected results.
- [ ] Empty layouts, send failures, reopening streams, and timestamp pass-through tests pass.
- [ ] Vicon layout reading, timing, bad reads, and grouped error tests pass.
- [ ] Preview parsing, math, alignment, rate, playback, CSV, and XDF results match.
- [ ] LabRecorder command order, partial replies, timeouts, disconnects, and file name tests pass.
- [ ] Desktop settings, state changes, readiness, source changes, and closing behavior match.
- [ ] Generated C++ and C# stream files are up to date and list values in the same order.
- [ ] HoloLens timing, coordinate, queue, sending, cancel, and recovery tests pass.
- [ ] Any change that touches a device goes through the [hardware test guide](device-parity-runbook.md).
- [ ] CMake option combinations, target names, and package contents stay the same.
- [ ] No third-party submodule file or version changed.

## Main source files

- `README.md`
- `vicon-lsl-bridge/src/CommandLine.*`
- `vicon-lsl-bridge/src/Config.h`
- `vicon-lsl-bridge/src/ViconLSLBridge.*`
- `vicon-lsl-bridge/src/ViconClient.*`
- `vicon-lsl-bridge/src/ViconFrameMapper.*`
- `vicon-lsl-bridge/src/MarkerStream.*`
- `vicon-lsl-bridge/src/SegmentStream.*`
- `vicon-lsl-bridge/src/StreamSchema.*`
- `vicon-lsl-bridge/src/gui/*`
- `vicon-lsl-bridge/src/preview/*`
- `hololens-gaze-lsl/README.md`
- `hololens-gaze-lsl/Assets/Scripts/*`
- `stream-contracts/hololens-gaze.json`
- `stream-contracts/hololens-model-target.json`
- Tests under `vicon-lsl-bridge/tests` and `hololens-gaze-lsl/Tests`
