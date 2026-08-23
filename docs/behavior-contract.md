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
- A new connection request stops both timers, clears queued work, and fails active work.
- It then closes the old socket, sets recording state to unknown, and starts the new connection timeout.
- End each command with a newline.
- Run commands one at a time in queued groups.
- Send the next command only after the current reply begins with `OK`.
- Ignore spaces, tabs, and line breaks before `OK`.
- Keep partial replies until enough text arrives.
- An unexpected reply, socket error, command timeout, or disconnect during a command fails active work and clears later work.
- It also sets recording state to unknown and closes the connection.
- Connection and command timeouts stay separate.

### Command order

- Refresh: `update`
- Change filename: `filename`
- Start without stream selection: `filename`, `start`
- Start from the desktop app: `update`, `select all`, `filename`, `start`
- Stop: `stop`

The `update` and `select all` steps must run before Start so LabRecorder includes streams that appeared after its last refresh.

### Filename fields

| Token | Value |
| --- | --- |
| `%p` | Participant |
| `%s` | Session |
| `%b` | Task or block |
| `%r`, `%n` | Run |
| `%a` | Acquisition |
| `%m` | Modality |

Replace `{` and `}` with `_`, replace line breaks with spaces, and remove spaces at the start and end. Leave empty fields out of the remote `filename` command.

Do not start recording unless:

- The study folder is set, exists, and is a folder.
- The filename pattern is not empty.
- Participant, session, task, acquisition, and modality are not empty.
- No known or unknown `%` token remains in the result.
- The final path preview is not empty.

The default pattern is:

`sub-%p/ses-%s/%m/sub-%p_ses-%s_task-%b_acq-%a_run-%r_%m.xdf`

### LabRecorder process ownership

- Use a valid user-selected program first. Otherwise, use `labrecorder/LabRecorder.exe` beside the desktop app when it exists.
- A LabRecorder started by the desktop app is owned by the desktop app.
- Retry the remote connection every 250 ms for up to 15 seconds, but only while it is neither connected nor already connecting.
- When the desktop app closes, ask only an owned LabRecorder to exit. If needed, force-close only that owned process.

## Desktop app

- Start takes the current server and stream names, saves them, disables those fields, starts one bridge worker, and enables Stop.
- Status shows bridge state, marker and segment counts, frame number, and a rate calculated by the GUI.
- A streaming status becomes stale after three seconds without an update. The displayed rate then becomes `0.0 Hz`, and readiness reports that status is stale.
- Normal streaming updates follow the bridge's 100-frame layout-check timing, not every frame.
- Closing asks the bridge to stop. If recording state is exactly `Recording`, closing also asks LabRecorder to stop.
- Closing waits up to four seconds for the bridge and up to 15 seconds for LabRecorder to confirm Stop. It then finishes closing and stops an owned LabRecorder process.
- Recording buttons follow `LabRecorderRuntimePolicy`. Bridge streaming appears in readiness text but is not a hard requirement inside `canStartRecording`.
- A valid filename change is sent to a connected, non-recording LabRecorder 300 ms after typing stops.

## Preview

### Live data

- By default, find `ViconMarkers`, `ViconSegments`, `HoloLensGaze`, and `HoloLensModelTargetPose` by exact name.
- Try again once per second when a stream is missing.
- Read full LSL metadata when possible. Use the fixed HoloLens labels if that metadata is incomplete.
- Ask LSL to correct clock differences for live data.
- Treat a stream as fresh for 500 ms after its newest sample.
- Read at most 16 samples from one stream in one pass and keep the newest.
- A new marker sample sets the frame time. Include fresh segment and gaze data only when each is within the time limit, which is 50 ms by default.
- Without a new marker, a new segment or gaze sample may make a frame. If both update, use the later time.
- `*_stream_present` means the LSL input is connected. It does not mean the stream is fresh or contains parsed values.
- Convert Vicon positions from millimetres to metres for display. Gaze is already in metres.
- Automatic stair alignment uses 20 stable target samples and lasts only for the current preview session. Manual controls stay saved.

### Merged CSV files

- Use the first row as labels.
- Prefer `relative_time` for frame time.
- Otherwise, subtract the first finite `lsl_time` from each finite `lsl_time`.
- Otherwise, use the row number starting at zero.
- Turn marker, segment, and gaze columns into the shared `PreviewFrame` form.

### XDF files

- Read numeric streams that the preview understands. Count and skip string streams.
- Ignore an incomplete final chunk only when its remaining length header or declared body is cut off. Report other malformed data as an error.
- A missing timestamp may be rebuilt only when an earlier timestamp and a positive expected rate are available.
- Fit and apply recorded clock offsets once.
- Repair corrected timestamps so they always increase.
- Choose the main stream in this order: markers, segments, another stream with `Vicon` in its name, gaze, then any numeric stream.
- Match other streams to the nearest corrected full timestamp within the chosen time limit.
- Show playback time from zero, based on the first corrected main-stream timestamp.
- Shared-world gaze may use automatic stair alignment. Old `eye_tracker_space` gaze may be shown but never aligned from the target.

## Saved settings

Settings use organization `ViconLSL` and application `ViconLSLBridge`.

Bridge and recording keys:

- `server`, `markerStream`, `segmentStream`
- `recordingRoot`, `recordingTemplate`
- `participant`, `session`, `task`, `run`, `acquisition`, `modality`
- `labRecorderExecutable`, `labRecorderHost`, `labRecorderPort`

Preview keys:

- `preview/markerStream`, `preview/segmentStream`, `preview/gazeStream`, `preview/calibrationStream`
- `preview/matchTolerance`, `preview/trailPoints`, `preview/playbackSpeed`
- `preview/gazeTx`, `preview/gazeTy`, `preview/gazeTz`
- `preview/gazeRx`, `preview/gazeRy`, `preview/gazeRz`
- `preview/stairModel`

Loading removes these old automatic-alignment keys:

- `preview/gazeUseQuaternion`
- `preview/gazeQTx`, `preview/gazeQTy`, `preview/gazeQTz`
- `preview/gazeQx`, `preview/gazeQy`, `preview/gazeQz`, `preview/gazeQw`

Changing a current key needs a plan that reads the old value and proves that saved user settings still work.

## Build and package names

Keep:

- CMake options that begin with `VICON_LSL_` or `VICON_LSL_BRIDGE_`.
- Targets `vicon-lsl-bridge-logic`, `vicon-lsl-bridge-runtime`, `vicon-lsl-bridge`, `vicon-lsl-bridge-gui`, and the Windows package targets.
- The C++ checks that run without the runtime, GUI, or a downloaded test library.
- Program names and the packaged `labrecorder`, `stair_model`, runtime, and license folders.
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
