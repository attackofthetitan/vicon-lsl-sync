# Observable behavior contract

## Purpose and change policy

This document records behavior that a structural refactor must preserve. It covers executable interfaces, LSL streams, recording control, preview inputs, settings, and build interfaces. Timing and coordinate details are expanded in [time-and-coordinate-semantics.md](time-and-coordinate-semantics.md).

A proposed change to any item labeled **contract** below is not ordinary cleanup. It must be isolated as a migration or functional-change task with explicit compatibility and rollout validation.

## Command-line executable

The `vicon-lsl-bridge` executable accepts:

| Option | Default | Current behavior |
| --- | --- | --- |
| `--server <ip:port>` | `localhost:801` | Sets the Vicon DataStream endpoint |
| `--marker-stream <name>` | `ViconMarkers` | Sets the marker LSL stream name |
| `--segment-stream <name>` | `ViconSegments` | Sets the segment LSL stream name |
| `--reconnect-interval <ms>` | `3000` | Requires an integer from 1 through `INT_MAX` |
| `--help` | n/a | Prints usage and exits 0 |

Unknown options, missing values, and invalid reconnect intervals print an error plus usage and exit 1. The removed relay-style options `--no-hololens-gaze`, `--gaze-port`, and `--gaze-stream` remain errors; HoloLens gaze is produced natively by the Unity application.

The startup diagnostics identify the configured server, marker stream, and segment stream and do not advertise a desktop gaze relay.

**Contract:** option spellings, defaults, accepted value domain, action/exit behavior, and native-HoloLens ownership remain stable.

## Desktop bridge session

### Connection and retry

- The client connects in Vicon `ServerPush` mode and enables segment and marker data.
- A failed connect retries after the configured interval until stopped.
- Retry sleep is divided into at most 100 ms intervals so `stop()` is observed promptly.
- A successful connection must yield an initial frame before streams are initialized.
- Setup, initial-frame, or discovery failure disconnects and retries; a failed layout is not partially published.
- `stop()` changes the atomic run flag. A stopped `ViconLSLBridge` instance does not reset that flag in `run()` and is therefore not currently reusable as a fresh session.

### Discovery and layout

- Subject, marker, and segment order follows Vicon SDK enumeration order.
- Any failed subject count/name, marker count/name, or segment count/name aborts discovery and discards the partial layout.
- Empty marker and/or segment layouts are healthy. The corresponding stream object is configured but creates no LSL outlet, and empty pushes report success.
- Layout is rediscovered after each 100 successfully processed frame iterations.
- A changed marker or segment layout destroys and recreates both Vicon outlets.
- A layout-discovery error during a periodic check reports diagnostics but does not by itself claim a layout change.

### Frame and failure handling

- Marker and segment reads retain frame number, subject/object name, operation name, status, SDK result text, and diagnostic message until conversion at the outlet boundary.
- Both outlets receive the same selected frame timestamp.
- A failure to push either outlet ends the current streaming session. Both outlets are destroyed, the Vicon client is disconnected, and reconnect/recreation follows.
- Stream source IDs remain stable across reconnect/recreation, and the timestamp monotonicity state remains alive across those reconnects.
- Session cleanup clears layout, frame counters, diagnostic aggregation, and last diagnostic status before reporting disconnected.

### Diagnostics and status

- Read failures are grouped by operation, subject, object, SDK result, and message.
- A diagnostic is logged on its first occurrence and every 100th repeat by default.
- The reported summary states the diagnostic count and includes the first formatted diagnostic.
- The first clean frame after a reported diagnostic clears aggregation and reports recovery.
- `BridgeStatus` reports state, current marker/segment counts, the current Vicon frame number, and a message.

## LSL stream contracts

### Shared Vicon rules

| Property | Marker and segment behavior |
| --- | --- |
| Stream type | `MoCap` |
| Channel format | `double64` |
| Nominal rate | Positive finite Vicon frame rate, otherwise LSL irregular rate |
| Source ID | `vicon_markers_<hostname>` or `vicon_segments_<hostname>`; hostname falls back to `default` |
| Timestamp | Estimated acquisition time in the local LSL clock domain |
| Layout change | Outlet is destroyed and recreated with the new schema |
| Empty layout | No outlet is created; the configured stream remains healthy |

The following metadata values are observable contracts for both streams:

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

#### `ViconMarkers`

- The stream name is configurable; the default is `ViconMarkers`.
- Each discovered `(subject, marker)` contributes four channels in discovery order:
  - `<subject>:<marker>:X`, `Y`, `Z`, units `mm`
  - `<subject>:<marker>:Valid`, unit `bool`
- A valid marker emits XYZ and numeric valid flag `1.0`.
- An occluded, SDK-error, or disconnected marker emits `NaN, NaN, NaN, 0.0`.

#### `ViconSegments`

- The stream name is configurable; the default is `ViconSegments`.
- Each discovered `(subject, segment)` contributes seven channels in discovery order:
  - `<subject>:<segment>:X`, `Y`, `Z`, units `mm`
  - `<subject>:<segment>:QX`, `QY`, `QZ`, `QW`, units `quaternion`
- A segment is valid only when both translation and quaternion reads are valid.
- If either read is invalid or occluded, all seven values are `NaN`.
- There is no segment valid-flag channel.

### `HoloLensGaze`

- The Unity configuration defaults are name `HoloLensGaze`, type `Gaze`, and source ID `hololens2_gaze`.
- The outlet is `double64`, nominally 90 Hz, and has exactly 21 channels.
- Stream identity plus channel order, labels, and units are defined by `stream-contracts/hololens-gaze.json` and generated into both languages. The order is combined origin/direction/valid, left-eye origin/direction/valid, then right-eye origin/direction/valid.
- Origins use `meters`, directions use `normalized`, and valid flags use `bool`.
- The outlet is created only when the tracker exposes the exact required 90 Hz mode and the spatial graph node is available.
- The original positive finite capture timestamp is passed to LSL. Samples with invalid timestamps are dropped rather than retimestamped.
- Individual-eye fields remain invalid when the tracker does not support them; the fixed 21-channel schema does not change.
- A persistent provider failure causes worker shutdown, tracker re-enumeration, and later outlet recreation. An outlet/worker failure that is not a recoverable provider failure disables publishing after logging the error.

Observable gaze acquisition metadata includes:

- `device = HoloLens2`
- `sdk = Microsoft.MixedReality.EyeTracking`
- `acquisition_mode = extended_eye_tracking_90hz`
- `timestamp = sdk_system_relative_time`
- `timestamp_units = seconds`
- `capture_clock_domain = windows_qpc_system_relative`
- `clock_domain = lsl_local_clock`
- `coordinate_frame = hololens_stationary_shared_with_gaze`
- `coordinate_units = meters`
- synchronization backlog policy `drop_when_capture_span_exceeds_25ms_retain_latest`

### `HoloLensModelTargetPose`

- The Unity configuration defaults are name `HoloLensModelTargetPose`, type `Calibration`, and source ID `hololens2_stair_model_target`; these values and the channel schema are generated from `stream-contracts/hololens-model-target.json`.
- The stream is `double64` with LSL irregular rate and eight channels:
  - `PositionX`, `PositionY`, `PositionZ` in `meters`
  - `RotationX`, `RotationY`, `RotationZ`, `RotationW` in `normalized`
  - `Tracked` in `bool`
- A tracked Unity pose is reflected into the published right-handed basis: position `(x, y, -z)` and quaternion `(-x, -y, z, w)`.
- An untracked pose emits seven `NaN` values and `Tracked = 0.0`.
- The sample timestamp is `LSL.local_clock()` read in `LateUpdate` immediately before encoding/pushing.
- The coordinate-frame metadata is `hololens_stationary_shared_with_gaze`.

**Contract:** Any change to a stream name default, type, source ID policy, channel order/count/units, metadata, rate, invalid encoding, timestamp, coordinate frame, or recreation behavior is a stream migration.

## LabRecorder remote-control behavior

### Connection and command protocol

- The default RCS host is `localhost`; the default port is `22345`.
- A connection attempt resets queued/active work, aborts the previous socket, sets recording state to unknown, and starts the connection timeout.
- Commands are newline terminated and processed one at a time in queued batches.
- The next command is sent only after the current response begins with `OK`. Leading whitespace and line breaks are ignored, and fragmented replies are accumulated.
- An unexpected reply, socket error, command timeout, or mid-command disconnect fails active work, clears queued work, sets recording state unknown, and closes the connection.
- Connection and command timeouts are independent.

### Command ordering

- Refresh: `update`
- Filename update: one `filename` command
- Start without selection: `filename`, `start`
- GUI-controlled start with selection: `update`, `select all`, `filename`, `start`
- Stop: `stop`

The pre-start `update` followed by `select all` is required so streams that appeared after LabRecorder's previous discovery are included.

### Filename fields

Supported template tokens are:

| Token | Field |
| --- | --- |
| `%p` | participant |
| `%s` | session |
| `%b` | task/block |
| `%r`, `%n` | run |
| `%a` | acquisition |
| `%m` | modality |

Sanitization replaces `{` and `}` with `_`, replaces CR/LF with spaces, and trims surrounding whitespace. Empty fields are omitted from the RCS `filename` command.

The GUI refuses recording start unless:

- The study root is nonempty, exists, and is a directory.
- The template is nonempty.
- Participant, session, task, acquisition, and modality are nonempty.
- No known or unknown `%` placeholder remains unresolved.
- The rendered path preview is nonempty.

The default template is:

`sub-%p/ses-%s/%m/sub-%p_ses-%s_task-%b_acq-%a_run-%r_%m.xdf`

### Process ownership

- A valid configured executable is preferred; otherwise the bundled `labrecorder/LabRecorder.exe` beside the GUI is used when present.
- An automatically or manually launched process is marked owned by the GUI.
- RCS connection is retried every 250 ms for up to 15 seconds while it is neither connected nor already connecting.
- Closing the GUI terminates, then if needed kills, only an owned LabRecorder process.

## Desktop GUI behavior

- Starting creates one `BridgeWorker` from the current server/marker/segment fields, persists settings, disables those fields, and enables Stop.
- Status displays bridge state, counts, frame number, and a GUI-derived frame rate.
- Streaming status is considered stale after three seconds without a status update; the displayed rate becomes `0.0 Hz` and readiness reports staleness.
- Status updates normally arrive with the bridge's 100-frame layout-check cadence, not on every frame.
- Closing requests bridge stop. If recording state is `Recording`, it also queues Stop.
- Close readiness waits up to four seconds for the bridge and up to fifteen seconds for an acknowledged recording stop before finalizing and stopping an owned recorder process.
- Recording buttons follow `LabRecorderRuntimePolicy`; bridge streaming is reported in readiness but is not itself a prerequisite in `canStartRecording`.
- Valid filename edits are synchronized to a connected, non-recording LabRecorder after a 300 ms debounce.

## Preview behavior

### Live preview

- Defaults resolve `ViconMarkers`, `ViconSegments`, `HoloLensGaze`, and `HoloLensModelTargetPose` by exact stream name.
- Resolver results are retried once per second. Full inlet metadata is requested when possible; fixed HoloLens channel labels provide a fallback for incomplete metadata.
- Inlets use live LSL clock synchronization.
- A stream is fresh for 500 ms after its most recent sample.
- At most 16 samples per inlet are pulled in one polling pass; the latest is retained.
- A marker update anchors a frame. Fresh segment and gaze samples are included only when within the configured timestamp tolerance, default 50 ms.
- If no marker updated, a segment or gaze update can emit a fallback frame. When both updated, the later timestamp anchors the fallback.
- `*_stream_present` indicates inlet connection, not freshness or presence of parsed values.
- Vicon positions are scaled from millimetres to metres for display. Gaze input remains metre based.
- Automatic calibration collects 20 stable tracked target samples using the fixed stair profile and applies the result only for the current preview session. Manual controls remain persistent.

### Merged CSV playback

- The first row supplies labels.
- `relative_time` is preferred as frame time.
- Otherwise `lsl_time` is made relative to the first finite LSL time.
- Otherwise the zero-based row index is used.
- Marker, segment, and gaze columns are parsed into the shared `PreviewFrame` representation.

### XDF playback

- The custom reader loads previewable numeric streams; string streams are counted/skipped.
- Incomplete final chunks are ignored only when the remaining variable-length header or declared chunk body is truncated. Other malformed input raises an error.
- Implicit timestamps require a preceding timestamp and a positive nominal rate.
- Recorded clock offsets are fitted and applied once; corrected regressions are repaired to strict monotonicity.
- The master-stream preference is markers, segments, another stream whose name contains `Vicon`, gaze, then any numeric stream.
- Other streams are nearest-matched by corrected absolute timestamp within tolerance.
- Display time starts at zero relative to the corrected first master timestamp.
- Shared-world gaze may be automatically calibrated from a stable target window. Legacy `eye_tracker_space` gaze is displayed without target calibration.

## Persisted settings contract

Organization is `ViconLSL`; application is `ViconLSLBridge`.

Bridge/recording keys:

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

Loading intentionally removes obsolete automatic-calibration keys `preview/gazeUseQuaternion`, `preview/gazeQTx`, `preview/gazeQTy`, `preview/gazeQTz`, `preview/gazeQx`, `preview/gazeQy`, `preview/gazeQz`, and `preview/gazeQw`.

Changing or removing a current key requires a settings migration that reads the old key and proves retained user configuration.

## Build and packaging contract

Preserve:

- CMake cache options beginning `VICON_LSL_` and `VICON_LSL_BRIDGE_`.
- Targets `vicon-lsl-bridge-logic`, `vicon-lsl-bridge-runtime`, `vicon-lsl-bridge`, `vicon-lsl-bridge-gui`, and Windows portable/package targets.
- The ability to run dependency-light tests with runtime, GUI, and test-framework fetching disabled.
- CLI and GUI executable names and the packaged `labrecorder`, `stair_model`, runtime, and license directory layout.
- The generated-contract check and platform-neutral managed test project.

Dependency upgrades and packaging-layout changes must be separate from module cleanup.

## Validation checklist for behavior-preserving passes

Before merge, select all rows affected by the pass:

- [ ] Public headers compile through their existing paths and signatures.
- [ ] CLI defaults, help/error cases, output categories, and exit codes match the baseline.
- [ ] Marker and segment schemas, order, units, invalid encoding, nominal rate, source IDs, timestamps, and normalized XML match golden expectations.
- [ ] Empty layout, outlet failure, recreation, and timestamp propagation tests pass.
- [ ] Vicon discovery, timestamp, invalid-read, and diagnostic aggregation tests pass.
- [ ] Preview parsing, math, calibration, rate, playback, CSV, and XDF golden tests pass.
- [ ] LabRecorder command order, fragmented acknowledgement, timeout, disconnect, and filename tests pass.
- [ ] GUI settings keys, state transitions, readiness, source switching, and close behavior match the baseline.
- [ ] Generated stream outputs are current and channel/sample parity holds across C++ and C#.
- [ ] Platform-neutral managed timing, transform, backlog, publisher, cancellation, and recovery tests pass.
- [ ] Device-dependent changes complete the checklist in [device-parity-runbook.md](device-parity-runbook.md).
- [ ] CMake configuration combinations, target names, and package inventory remain unchanged.
- [ ] No vendor submodule revision or content changed.

## Evidence sources

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
- Current tests under `vicon-lsl-bridge/tests` and `hololens-gaze-lsl/Tests`
