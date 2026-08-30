# Behavior that must stay the same

## How to use this guide

This guide lists behavior that a code-only cleanup must not change. It covers the command-line app, LSL streams, recording controls, preview files, saved settings, and build names.

If a proposed change affects an item in this guide, review it separately from code cleanup. State what users will see, how old data or settings will work, and how the new behavior will be checked.

For exact clock and coordinate rules, see [How time and coordinates work](time-and-coordinate-semantics.md).

Terms used below:

- **Metadata** means the labels and settings attached to an LSL stream.
- **Source ID** means the stable stream identity that helps LSL and LabRecorder recognize a stream after reconnect.
- **Finite** means a real number that is not `NaN` or positive or negative infinity.
- **Irregular rate** means the stream does not promise a fixed number of samples per second.
- **Normalized** means a direction or rotation has been scaled to its standard length. Some stream unit fields use the exact word `normalized`.

## Command-line app

`vicon-lsl-bridge` accepts these options:

| Option | Default | Rule |
| --- | --- | --- |
| `--server <ip:port>` | `localhost:801` | Sets the Vicon DataStream address. |
| `--marker-stream <name>` | `ViconMarkers` | Sets the marker stream name. |
| `--segment-stream <name>` | `ViconSegments` | Sets the segment stream name. |
| `--reconnect-interval <ms>` | `3000` | Accepts a whole number from 1 through `INT_MAX`. |
| `--help` | None | Prints help and exits with code 0. |

An unknown option, missing value, or invalid reconnect interval prints an error and the help text, then exits with code 1.

The old relay options `--no-hololens-gaze`, `--gaze-port`, and `--gaze-stream` remain invalid. The Unity app sends HoloLens gaze directly to LSL.

At startup, the app reports the chosen server, marker stream, and segment stream. It does not claim to relay gaze.

Keep all option names, defaults, accepted values, exit codes, and HoloLens ownership unchanged.

## Desktop bridge

### Connect and retry

- Connect in Vicon `ServerPush` mode.
- Enable marker and segment data.
- After a failed connection, wait for the chosen reconnect interval and try again until stopped.
- Split the wait into pieces no longer than 100 ms so Stop responds quickly.
- Read one Vicon frame before creating any LSL stream.
- If setup, the first frame, or layout discovery fails, disconnect and try again. Do not publish part of a layout.
- `stop()` only changes the run flag.
- Calling `run()` on a stopped `ViconLSLBridge` does not reset that flag. A stopped object is not reusable as a new session.

### Discover the layout

- Keep the subject, marker, and segment order returned by the Vicon SDK.
- If any count or name read fails, stop discovery and discard the partial result.
- An empty marker or segment layout is valid. No LSL stream is created for that empty group, and an empty send reports success.
- Check the layout after every 100 successfully handled frame loops.
- If either marker or segment layout changes, close and recreate both Vicon streams.
- If a repeating layout check fails, report the error but do not claim that the layout changed.

### Read and send frames

- Keep the Vicon frame number, subject or object name, operation name, SDK result, and readable error message until values reach the LSL stream boundary.
- Use the same timestamp for marker and segment samples from one frame.
- If either LSL send fails, end the current session. Close both streams, disconnect from Vicon, and reconnect.
- Keep source IDs stable when streams reconnect or are recreated.
- Keep the timestamp guard alive across reconnects so time cannot move backward.
- During session cleanup, clear the layout, frame counters, grouped errors, and last error status before reporting `Disconnected`.

### Report errors and status

- Group read errors by operation, subject, object, SDK result, and message.
- Log the first copy of an error and every 100th repeat by default.
- The summary shows the repeat count and the first formatted error.
- The first clean frame after a reported error clears the group and reports recovery.
- `BridgeStatus` includes the state, marker count, segment count, Vicon frame number, and a message.

## LSL streams

### Rules shared by Vicon streams

| Item | Current value |
| --- | --- |
| Type | `MoCap` |
| Value format | `double64` |
| Expected rate | The positive, finite Vicon frame rate; otherwise LSL irregular rate |
| Marker source ID | `vicon_markers_<hostname>` |
| Segment source ID | `vicon_segments_<hostname>` |
| Missing hostname | Use `default` |
| Timestamp | Estimated acquisition time in the local LSL clock |
| Layout change | Close the old stream and create a new one |
| Empty layout | Create no LSL stream and report success |

Both Vicon streams include these exact metadata values:

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

- The default name is `ViconMarkers`, and users may change it.
- Each `(subject, marker)` pair adds four values in Vicon discovery order.
- The labels are `<subject>:<marker>:X`, `<subject>:<marker>:Y`, `<subject>:<marker>:Z`, and `<subject>:<marker>:Valid`.
- X, Y, and Z use `mm`. `Valid` uses `bool`.
- A good marker sends XYZ and `1.0`.
- A hidden marker, SDK error, or disconnected marker sends `NaN, NaN, NaN, 0.0`.

### `ViconSegments`

- The default name is `ViconSegments`, and users may change it.
- Each `(subject, segment)` pair adds seven values in Vicon discovery order.
- The labels are `<subject>:<segment>:X`, `<subject>:<segment>:Y`, `<subject>:<segment>:Z`, `<subject>:<segment>:QX`, `<subject>:<segment>:QY`, `<subject>:<segment>:QZ`, and `<subject>:<segment>:QW`.
- X, Y, and Z use `mm`. The four-number rotation values use the exact unit name `quaternion`.
- A segment is good only when both its position and rotation reads are good.
- If either read fails or is hidden, send seven `NaN` values.
- The segment stream has no separate valid value.

### `HoloLensGaze`

- Default name: `HoloLensGaze`.
- Default type: `Gaze`.
- Default source ID: `hololens2_gaze`.
- Value format: `double64`.
- Expected rate: exactly 90 Hz.
- Value count: exactly 21.

`stream-contracts/hololens-gaze.json` defines the stream name defaults, labels, order, and units for both C++ and C#. Values appear in this order: combined origin, direction, and valid flag; left-eye origin, direction, and valid flag; right-eye origin, direction, and valid flag.

Origins use `meters`, directions use `normalized`, and valid values use `bool`.

The app creates the stream only after the tracker reports an active 90 Hz mode and supplies a spatial graph node. It sends the original positive, finite capture timestamp. It drops a sample with a bad timestamp instead of giving it a new time.

If the tracker does not support data for one eye, that eye stays in the 21-value layout and is marked invalid.

A lasting provider error stops the worker, restarts tracker discovery, and later recreates the stream. A fatal worker or stream error that is not a recoverable provider error disables publishing after it logs the error.

The gaze stream includes these values:

- `device = HoloLens2`
- `sdk = Microsoft.MixedReality.EyeTracking`
- `acquisition_mode = extended_eye_tracking_90hz`
- `timestamp = sdk_system_relative_time`
- `timestamp_units = seconds`
- `capture_clock_domain = windows_qpc_system_relative`
- `clock_domain = lsl_local_clock`
- `coordinate_frame = hololens_stationary_shared_with_gaze`
- `coordinate_units = meters`
- Backlog rule: `drop_when_capture_span_exceeds_25ms_retain_latest`

### `HoloLensModelTargetPose`

`stream-contracts/hololens-model-target.json` defines these defaults and the value layout:

- Name: `HoloLensModelTargetPose`.
- Type: `Calibration`.
- Source ID: `hololens2_stair_model_target`.
- Value format: `double64`.
- Rate: LSL irregular rate.
- Value count: eight.

The values are `PositionX`, `PositionY`, and `PositionZ` in `meters`; `RotationX`, `RotationY`, `RotationZ`, and `RotationW` in `normalized`; and `Tracked` in `bool`.

A tracked Unity pose is converted to the published right-handed coordinates:

- Position `(x, y, z)` becomes `(x, y, -z)`.
- Rotation `(x, y, z, w)` becomes `(-x, -y, z, w)`.

When the target is not tracked, send seven `NaN` values and `Tracked = 0.0`.

Read `LSL.local_clock()` in `LateUpdate` just before encoding and sending the sample. Use `hololens_stationary_shared_with_gaze` as the coordinate-frame metadata.

Treat any of these as a stream change that needs its own move plan:

- A new stream name default, type, or source ID.
- A new value count, order, label, unit, or invalid value.
- A new metadata value, rate, timestamp, coordinate frame, or recreation rule.

## LabRecorder remote control

### Connection and replies

- Default host: `localhost`.
- Default port: `22345`.
- A new connection request is rejected while Start, Stop, acknowledged recording,
  or uncertain sent-Start work is active on a connected endpoint.
- Otherwise it stops both timers, fails active work, closes
  the old socket, sets recording state to unknown, and starts the new connection
  timeout. Reconnecting after connection loss preserves uncertain sent-Start
  evidence until Stop is acknowledged.
- End each command with a newline.
- Run one command group at a time.
- Send the next command only after the current reply begins with `OK`.
- Ignore spaces, tabs, and line breaks before `OK`.
- Keep partial replies until enough text arrives.
- An unexpected reply, socket error, command timeout, or disconnect fails the
  current command group.
- It also sets recording state to unknown and closes the connection.
- Connection and command timeouts stay separate.

The recorder keeps connection state, the last confirmed recording state, and the
state it is trying to reach. Its current operation is `Idle`, `Refreshing`,
`UpdatingFilename`, `Starting`, `Stopping`, or `ShuttingDown`. The dashboard
shows which command in the current group is waiting for a reply. While one group
is active, the recorder refuses other work. This prevents repeated Start or Stop
requests and keeps unrelated commands out of the Start sequence.

### Command order

- Refresh: `update`
- Change filename: `filename`
- Start without stream selection: `filename`, `start`
- Start in **Record every visible stream** mode: `update`, `select all`,
  `filename`, `start`
- Stop: `stop`

The `update` and `select all` steps must run before Start so LabRecorder includes streams that appeared after its last refresh.

The exact-selection mode does not pretend that the graphical remote-control
protocol has an allowlist command. It launches the packaged `LabRecorderCLI`
with the validated absolute output path and one query for each selected stream.
The query uses source ID when available and otherwise constrains name by host.
The selected discovery snapshot is fixed before launch. Pressing Stop writes the
CLI terminator, and this CLI process is owned by the desktop app.

### Filename fields

| Token | Value |
| --- | --- |
| `%p` | Participant |
| `%s` | Session |
| `%b` | Task or block |
| `%r`, `%n` | Run |
| `%a` | Acquisition |
| `%m` | Modality |

One cleaned filename request is used for checking, display, session details,
directory creation, and the recorder command. Braces and line breaks would break
the recorder command, so the app rejects them and explains which field to fix.
It does not silently change an accepted path. Leading and trailing spaces are
removed. Empty optional fields are left out of the remote `filename` command.

Do not start recording unless the study root is an existing full directory path;
the template is a relative path; participant, session, task, acquisition,
modality, and a positive run are present; no `%` placeholder is unresolved; and
the final destination remains under the study root unless the advanced
outside-root override is explicitly enabled. Append `.xdf` when the template
does not already end in that extension. Reject parent traversal, symlink escape,
absolute templates, Windows-reserved names or characters, trailing spaces or
periods, impractical path length, an unwritable destination, and a collision
unless overwrite is explicitly enabled. Missing parent directories are created
only for an accepted Start request. Low or unavailable storage is a visible
warning at the configured threshold rather than a silent condition.

The path shown in **Exact Recording Destination** must equal the path passed to
the recorder. **Find Next Run** searches at most 1,000 positive run values for a
nonexistent destination. Optional automatic increment runs only after the file
exists and the configured file-check completion rule is satisfied.

The default pattern is:

`sub-%p/ses-%s/%m/sub-%p_ses-%s_task-%b_acq-%a_run-%r_%m.xdf`

### LabRecorder process ownership

- Probe the configured endpoint before automatic launch. Use a valid
  user-selected program first; otherwise use `labrecorder/LabRecorder.exe`
  beside the desktop app when it exists.
- Launch does not block the window, uses the program's directory as its working
  directory, and distinguishes external, launching, owned-running,
  owned-exited, launch-failed, and detached states.
- Retain at most 64 KiB of recorder output and send limited lines to the
  event log. A slow or failed launch never waits on the GUI thread.
- Retry the remote connection every 250 ms for up to 15 seconds, but only while
  it is neither connected nor already connecting.
- Disconnecting from or closing the app around an external process never ends
  that process. Detach deliberately relinquishes ownership.
- End an owned process during shutdown only after Stop settles or the 15-second
  recorder deadline expires. Give termination one further second before the
  owned process is force-ended.

## Desktop app

- Start takes the current server and stream names, saves them, disables those fields, starts one bridge worker, and enables Stop.
- Status shows bridge state, marker and segment counts, frame number, and a rate calculated by the GUI.
- A streaming status becomes stale after three seconds without an update. The displayed rate then becomes `0.0 Hz`, and readiness reports that status is stale.
- Normal streaming updates follow the bridge's 100-frame layout-check timing, not every frame.
- Separate indicators always show the session, bridge, recorder,
  preview, calibration, file, path, and file-check state. A normal status
  update never clears the persistent last error. The timestamped event log is
  limited to 1,000 entries and can be copied or exported with configuration,
  stream lists, state changes, rates, setup checks, shutdown, and file-check data,
  but not recording samples.
- Recording controls use the connection, confirmed state, requested state,
  current operation, process, path, and setup-check result. The dashboard
  shows `STARTING`, `RECORDING`, or `STOPPING`, elapsed time, exact destination,
  run, endpoint, ownership, stream health, storage, skipped older input, and
  replaced display-frame counts.
- A valid filename change is sent to a connected, non-recording LabRecorder 300 ms after typing stops.
- **Start Session** guides bridge start, preview start, stream search, the setup
  check, and recording while leaving each independent control available.
  **Stop Session** reverses the safe order: recording, preview, bridge, then an
  owned recorder when requested. Partial completion remains visible and
  stoppable.
- The setup check classifies bridge recency, recorder readiness, exact path,
  selected streams, sample age, channel layout, coordinate details, expected
  rate, stair model, and calibration as required, warning, or information.
  Recorder-only mode
  deliberately removes the bridge requirement. A required failure blocks Start
  unless **Record Anyway** receives a nonempty reason; the result and reason are
  retained in the session details.

Closing follows one fixed process without blocking the window. It refuses new
work, cancels stream search and file checks, requests preview and bridge Stop,
and asks the recorder to shut down exactly once. If Start is already running,
that command group finishes and the recorder then receives exactly one Stop.
Repeated close requests do not start another sequence. The window stays
responsive and visible until every required part actually stops. Four-second
bridge, two-second preview/file, and 15-second recorder deadlines are visible
status results, not permission to destroy running work. Only a recorder started
by this app may be ended. An external recorder remains untouched even after
connection loss, which is recorded as `Recorder connection lost`.
Ordinary window operations, including Stop requests, have a 50 ms target and no
window cleanup waits forever.

## Preview

### Live data

- The default marker and segment bindings follow the bridge output names.
  **Preview external streams** makes those bindings independently configurable.
- Discover name, type, source ID, host, session ID, publisher UID and creation
  time, channel count, expected/measured rate, coordinate name, sample age, and
  channel-layout health. A role normally uses one source ID. **Follow by name**
  is an explicit choice for source IDs that change between runs.
- A missing selected source ID does not silently fall back by name. Duplicate
  names without a selected identity are reported as ambiguous. Multiple visible
  instances of the same recovered source ID select the newest publisher and say
  so; choosing by name is also visible.
- Try again once per second when a stream is missing. LSL stream searches are
  limited to 50 ms, stream-detail reads to 250 ms, and sample reads do not wait.
- Read the full LSL stream details when possible. Use the fixed HoloLens labels
  if those details are incomplete, but show a warning in the session log.
- Ask LSL to correct clock differences for live data.
- Treat a stream as fresh for 500 ms after its newest sample.
- Read at most 16 samples from one stream in one pass and keep the newest.
- A new marker sample sets the frame time. Include fresh segment and gaze data only when each is within the time limit, which is 50 ms by default.
- Without a new marker, a new segment or gaze sample may make a frame. If both update, use the later time.
- `*_stream_present` means the LSL input is connected. It does not mean the stream is fresh or contains parsed values.
- Convert Vicon positions from millimetres to metres for display. Gaze is already in metres.
- Keep only one live frame waiting for display. The window draws at 30 or 60 Hz,
  while stream-rate and calibration measurements continue separately. Show
  preview delay, skipped older input, and replaced display frames separately;
  these deliberate skips are not source data loss.
- Automatic stair alignment starts only when requested, uses 20 stable target
  samples, and lasts only for the current desktop session until explicitly saved
  as a managed profile. Manual controls stay saved.

### Merged CSV files

- Load on a worker while retaining the prior usable live or recorded source.
  Report progress and honor cancellation between limited line/sample groups. A
  cancellation or failure never installs partial playback state.
- Use the first row as labels.
- Prefer `relative_time` for frame time.
- Otherwise, subtract the first finite `lsl_time` from each finite `lsl_time`.
- Otherwise, use the row number starting at zero.
- Turn marker, segment, and gaze columns into the shared `PreviewFrame` form.
- Keep a memory-limited set of frames and draw fewer frames when needed. Keep
  exact source timing separately.

### XDF files

- Inventory every stream before assembly. Read numeric streams that the preview
  understands, count and skip string streams, and reject a file with no supported
  preview role.
- Ignore an incomplete final chunk only when its remaining length header or declared body is cut off. Report other malformed data as an error.
- A missing timestamp may be rebuilt only when an earlier timestamp and a positive expected rate are available.
- Fit and apply recorded clock offsets once.
- Repair corrected timestamps so they always increase.
- Group possible streams by role, source ID, name, host, and channel layout.
  Automatically join compatible pieces across their full time ranges. A source
  ID reused on different hosts is not assumed to be the same publisher.
- Choose the suggested master in this order: markers, segments, another supported
  Vicon stream, then gaze. Require an explicit mapping when incompatible
  candidates remain, and let the user choose the master and included groups.
- Match other streams to the nearest corrected full timestamp within the chosen time limit.
- Show playback time from zero, based on the first corrected main-stream timestamp.
- Shared-world gaze may use automatic stair alignment. Old `eye_tracker_space` gaze may be shown but never aligned from the target.
- The summary names the master stream ID, selected groups and stream IDs,
  excluded groups, stitched instances, unmatched percentages, time ranges, and
  applied clock corrections.

CSV and XDF playback share a timeline, current/duration and frame position,
play/pause, speed-preserving seek, one-frame steps, start/end jumps, configurable
time jumps, and an explicit loop toggle. Recent files and drag-and-drop open are
supported. **Export Image** writes only the current preview image and never
changes the source data. The preview drawing code supplies Fit View, Reset Camera,
expanding bounds, axes/units, a legend, valid/total counts, layout-change trail
cleanup, palette-aware drawing, and a headless rendering path without an OpenGL
runtime dependency.

### Playback limits and responsiveness

- CSV reading keeps one memory-limited set of frames. XDF loading can temporarily
  keep a file index, the selected stream data, and decoded frames. Each uses the
  configured 16-2,048 MiB limit. Only decoded frames remain after loading.
- The decoded preview has a 200,000-frame ceiling and an XDF stream retains at
  most 2,000,000 stored values. Safety limits are 64 GiB per XDF, 100,000,000
  declared samples per stream, 65,536 channels, 4,096 streams, and a 4 MiB
  header. Exceeding a limit is an error, not an allocation attempt.
- File cancellation is checked at least every 1,024 work units and has a
  250 ms target. Progress covers reading, indexing, stream details, timestamps,
  calibration, and frame preparation. Live preview latency has a 100 ms target.

### Managed calibration profiles

A version-1 managed profile contains an ID and display name, physical setup ID,
stair model path and identity, measured fixed Vicon stair pose, gaze transform,
gaze and target coordinate frames, setup notes, creation time, sample count,
translation and rotation RMS, confirmation for missing stream details, and retirement
state. Profiles can be selected, applied, duplicated, retired, imported, and
exported. Applying a profile is visible and reversible by choosing the manual
transform. A new automatic solve remains session-only until **Save Session
Calibration** is chosen. Collection progress, quality, rejection, and coordinate
compatibility remain visible; missing coordinate details require an explicit
fallback confirmation before a profile is complete.

### Check the file after recording

After a confirmed Stop, wait for the exact destination to exist and inspect it
away from the window thread. Compare the stream list saved before Start with the
recorded name, source ID, host, channel layout, time range, sample count,
measured rate, gaps, clock corrections, and repaired timestamps. The result is
`Verified`, `Verified with warnings`, or `Needs attention`; a Stop
reply by itself is not presented as proof of saved data. The file check never
edits or deletes the XDF. Findings are part of the session details and
the file can be opened directly in playback.

## Saved settings

Settings use organization `ViconLSL` and application `ViconLSLBridge`.

The saved session setup is version-1 JSON at `session/configuration`;
`session/configurationVersion` records the file-format version. It contains the
bridge address and outputs, selected stream IDs and matching choice, preview
limits, recorder address and selection, output path rules, session options, and
selected calibration profile. Named presets and JSON Import/Export use the same
format.

The guided-session settings start at format version 1 and are stored as one JSON
value. Settings from older application releases are intentionally not imported.
Unsupported format versions are rejected instead of being guessed or rewritten.

Machine/UI state is deliberately outside presets: `ui/windowGeometry`,
`ui/mainSplitter`, active control and preview tabs, up to ten recent recordings,
and recent preset/diagnostic directories. Managed calibration profiles are
stored separately at `session/calibrationProfiles`.

Changing the version-1 format requires an explicit format change and tests for
both rejection and the new round trip.

## Build and package names

Keep:

- CMake options that begin with `VICON_LSL_` or `VICON_LSL_BRIDGE_`.
- Targets `vicon-lsl-bridge-logic`, `vicon-lsl-bridge-runtime`, `vicon-lsl-bridge`, `vicon-lsl-bridge-gui`, and the Windows package targets.
- The C++ checks that run without the runtime, GUI, or a downloaded test library.
- Program names and the packaged `labrecorder` (including `LabRecorder.exe` and
  `LabRecorderCLI.exe`), `stair_model`, runtime, and license folders.
- The generated-stream check and the device-independent C# check project.

Handle dependency updates and package-layout changes separately from code cleanup.

## Before merging a code cleanup

Check every line that the change may affect:

- [ ] Existing public headers still compile from the same paths with the same names and signatures.
- [ ] Command-line defaults, help, errors, output types, and exit codes match the earlier version.
- [ ] Marker and segment order, units, invalid values, rates, source IDs, timestamps, and LSL metadata match saved expected results.
- [ ] Empty layouts, send failures, stream recreation, and timestamp forwarding pass.
- [ ] Vicon discovery, timing, invalid reads, and grouped error reporting pass.
- [ ] Preview parsing, math, alignment, rate, playback, CSV, and XDF results match.
- [ ] LabRecorder command order, partial replies, timeouts, disconnects, and filename checks pass.
- [ ] GUI settings, state changes, readiness, source changes, and closing behavior match.
- [ ] Generated C++ and C# streams are current and have the same value order.
- [ ] Device-independent HoloLens timing, conversion, queue, publishing, cancellation, and recovery checks pass.
- [ ] A device-related change completes the [hardware test guide](device-parity-runbook.md).
- [ ] CMake combinations, target names, and package contents stay the same.
- [ ] No third-party submodule content or revision changed.

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
- Checks under `vicon-lsl-bridge/tests` and `hololens-gaze-lsl/Tests`
