# How services start, stop, and recover

## Why this guide exists

This guide lists the current startup, failure, retry, and shutdown order. A code-only cleanup may move this work into smaller files, but it must not change the order, wait times, owners, or reported states.

Related guides:

- [How the code is organized](architecture.md)
- [Behavior that must stay the same](behavior-contract.md)
- [How time and coordinates work](time-and-coordinate-semantics.md)

## Desktop bridge

### States visible to other code

`BridgeState` has four values:

- `Disconnected`
- `Connecting`
- `Streaming`
- `Stopped`

The bridge also performs internal steps for connection retry, first-frame reading, layout discovery, stream creation, layout replacement, and cleanup.

```mermaid
stateDiagram-v2
    [*] --> Connecting: run()
    Connecting --> Connecting: connection fails / wait and retry
    Connecting --> Stopped: Stop before a session starts
    Connecting --> InitialFrame: connection and setup work
    InitialFrame --> Connecting: first GetFrame fails / disconnect
    InitialFrame --> Initializing: first frame arrives
    Initializing --> Connecting: discovery or stream setup fails / disconnect and wait
    Initializing --> Streaming: streams are ready
    Streaming --> Streaming: frame read and both sends work
    Streaming --> Reinitializing: 100-frame check finds a new layout
    Reinitializing --> Streaming: both streams are recreated
    Reinitializing --> Disconnected: stream setup fails
    Streaming --> Disconnected: GetFrame or either send fails
    Streaming --> Disconnected: Stop
    Disconnected --> Connecting: clean up and wait while still running
    Disconnected --> Stopped: clean up after Stop
    Stopped --> [*]
```

### Connect

1. `run()` creates one `ViconTimestampState` before the reconnect loop.
2. `connectWithRetry()` reports `Connecting` before the first attempt.
3. After a failed attempt, report the wait time and sleep in pieces no longer than 100 ms.
4. A complete connection sets `ServerPush` mode and enables segment and marker data.
5. If any setup call fails, disconnect the SDK client and treat the connection as failed.
6. If Stop arrives during retry, leave the loop and report `Stopped`.

### Read the first frame and create streams

1. Read one Vicon frame before discovering names or creating streams.
2. If this first `GetFrame` fails, report `Connecting`, disconnect, and start the outer connection loop again. This path does not call the normal retry wait first.
3. On success, update `frame_count_`.
4. Stop discovery at the first failed count or name read. Return no layout and include the errors.
5. Clear errors from an earlier session only after discovery succeeds.
6. Use the Vicon frame rate as the LSL expected rate only when it is positive and finite. Otherwise, use irregular rate.
7. If the computer name cannot be read, use `default` in source IDs.
8. Create the marker stream before the segment stream. If either throws, close both and report failure.
9. An empty layout succeeds without creating an LSL stream.

### Send frames

For each pass through the streaming loop:

1. Read a frame. Leave the session if `GetFrame` fails.
2. Choose a finite timestamp that is later than the previous one. If no finite time can be made, skip that frame without sending it.
3. `buildViconFrame` reads every known marker and segment and returns fixed-size values plus any errors.
4. Send markers first and segments second with the same timestamp.
5. A hidden or failed item becomes an invalid fixed-size value. It does not stop the session.
6. If either LSL send fails, report that streams will be recreated and leave the session.
7. Group read errors after both send attempts.
8. After 100 handled loop passes, reset the layout counter, report status, and discover the layout again.

### Handle a layout check

- If discovery fails, report the error and keep the existing streams.
- If the layout is unchanged, continue streaming.
- If it changed, close both streams before creating replacements.
- If replacement fails, leave streaming and perform full cleanup.
- If replacement works, report that the streams were recreated.

### Clean up

Use this order:

1. Close the marker stream.
2. Close the segment stream.
3. Disconnect the Vicon client.
4. Reset frame count, layout counter, known layout, grouped errors, and the last error message.
5. Report `Disconnected`.
6. If still running, wait and reconnect.
7. Otherwise, report `Stopped` and return.

Do not reset the timestamp state during reconnect. Stable source IDs and always-increasing timestamps let LabRecorder treat a recreated stream as the same recovered stream.

The run flag starts as true when the object is created. `run()` does not set it back to true. The desktop app creates a new bridge after each Stop.

### Bridge checks

- [ ] Stop before the first connection reports `Stopped` and returns quickly.
- [ ] Repeated connection failures wait for the chosen interval.
- [ ] A setup failure disconnects before retry.
- [ ] A first-frame failure reconnects without publishing part of a layout.
- [ ] The first consecutive first-frame failure reconnects without waiting, and
      every later consecutive one waits the chosen interval.
- [ ] A discovery failure discards partial names and waits before retry.
- [ ] Empty marker, segment, or both layouts reach `Streaming` without unwanted streams.
- [ ] Hidden or failed reads send invalid fixed-size values and keep streaming.
- [ ] A stream-creation exception closes any companion stream already created.
- [ ] A marker or segment send failure closes both streams and reconnects.
- [ ] Layout checks keep the 100-frame timing.
- [ ] A layout read error keeps current streams; a real change replaces both.
- [ ] Marker and segment samples from one frame have the same timestamp.
- [ ] Timestamps keep increasing after reconnect when source IDs stay the same.
- [ ] Stopping a live session reports `Disconnected` before `Stopped`.

## Desktop window and bridge worker

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Running: Start Streaming
    Running --> Stopping: Stop or close window
    Running --> Finished: bridge returns or throws
    Stopping --> Finished: bridge sees Stop
    Finished --> Idle: worker thread is cleaned up
```

- `BridgeWindow::onStart()` copies the server and two stream names into `Config`, saves them, disables the fields, creates one `BridgeWorker`, connects its signals, and starts it.
- The worker adds the bridge status callback inside `run()`.
- If the bridge throws, the worker sends `terminal(Failed, message)`.
- `onStop()` disables the Stop button and requests Stop. It does not destroy the worker itself.
- `onWorkerFinished()` restores the controls, clears rate and stale-state data, schedules thread deletion, clears the pointer, and lets a pending window close continue.

### Status age

- The GUI rate is the change in frame number divided by the time between GUI status messages.
- Normal status messages follow the 100-frame layout check, with extra messages for state changes and errors.
- If no streaming status arrives for more than 3000 ms, mark it stale once and show `0.0 Hz`.
- A new status clears the stale mark.

## LabRecorder remote connection

### Connection state

```mermaid
stateDiagram-v2
    [*] --> Disconnected
    Disconnected --> Connecting: connectToServer
    Error --> Connecting: connectToServer
    Connected --> Connecting: replace the connection
    Connecting --> Connected: socket connects
    Connecting --> Error: socket error or connection timeout
    Connected --> Error: protocol, write, or command timeout
    Connected --> Disconnected: remote closes while idle
    Error --> Error: close callback after a failure
```

`connectToServer()` replaces the old connection:

1. Stop both timers.
2. Fail active work as replaced.
3. Clear unsent command data and partial replies.
4. Close the old socket now.
5. Store separate connection and command timeouts.
6. Set recording state to `Unknown`.
7. Set connection state to `Connecting`, begin connecting, and start the connection timer if still needed.

After the socket connects, connection state is `Connected`. Recording state stays `Unknown` until this client receives a good reply to Start or Stop.

### Recording state

```mermaid
stateDiagram-v2
    [*] --> Unknown
    Unknown --> Recording: Start group succeeds
    Stopped --> Recording: Start group succeeds
    Unknown --> Stopped: Stop succeeds
    Recording --> Stopped: Stop succeeds
    Recording --> Unknown: connection or protocol fails
    Stopped --> Unknown: reconnect or failure
```

The last confirmed state is only part of the decision. `LabRecorderClient` also
tracks the state it wants and one current operation:

- `Idle`
- `Refreshing`
- `UpdatingFilename`
- `Starting`
- `Stopping`
- `ShuttingDown`

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Refreshing: Refresh
    Idle --> UpdatingFilename: delayed valid filename edit
    Idle --> Starting: Start accepted
    Refreshing --> Idle: acknowledged
    UpdatingFilename --> Idle: newest filename acknowledged
    Starting --> Idle: complete or failed
    Starting --> Stopping: close waits for Start, then sends Stop
    Idle --> Stopping: Stop accepted
    Stopping --> Idle: acknowledged
    Idle --> ShuttingDown: close with no remote Stop required
    Stopping --> ShuttingDown: close waits for existing Stop
```

Controls use the connection, confirmed state, requested state, current operation,
output-path check, and closing state together. Only one command group can run.
A second Start or Stop, a refresh, or a filename change is refused while that
group is active.

While the confirmed state is `Unknown`, recovery Start or Stop is possible only
when the connection is active and no other operation is running. The window
always shows `Starting`, `Recording`, `Stopping`, `Stopped`, or `Unknown` and the
command number that is waiting for a reply.

### Command groups

```mermaid
stateDiagram-v2
    [*] --> Writing: group accepted
    Writing --> AwaitingReply: full command and newline accepted
    AwaitingReply --> Writing: reply starts with OK and commands remain
    AwaitingReply --> Complete: reply starts with OK and group is done
    Writing --> Failed: write error
    AwaitingReply --> Failed: timeout, disconnect, or bad reply
    Complete --> [*]
    Failed --> [*]
```

Keep these rules:

- Only one group is active.
- Only one command in that group waits for a reply.
- Keep any part of a command that the socket has not accepted yet.
- Keep reply pieces until `OK` can be checked.
- Ignore leading carriage returns, line feeds, spaces, and tabs.
- The two bytes `OK` are enough. Do not wait for the rest of the line.
- A failure ends the group and closes the connection.
- A good group changes recording state only when the group declares a state other than `Unknown`.
- Start is one indivisible `update`, `select all`, `filename`, `start` group for record-every-visible mode.
- Closing while Start is active lets that one group finish, then sends exactly one
  Stop. All new work is refused after closing begins.
- A timeout, malformed reply, disconnect, or permitted replacement connection
  fails active work and returns confirmed and requested state to `Unknown`.
  Replacement is refused while connected recording work is active; reconnect
  after loss remembers that Start may have reached the recorder until Stop is
  confirmed.

### Start the included LabRecorder

1. Check the configured remote-control address first. Never start a duplicate process when that address is already reachable.
2. Launch only when automatic launch is enabled and nothing answers at that
   address.
3. Use a valid user-selected program first. Otherwise, look for `labrecorder/LabRecorder.exe` beside the desktop app.
4. Start it without blocking the window and use the program's directory as its
   working directory. State is `External`, `Launching`, `OwnedRunning`,
   `OwnedExited`, `LaunchFailed`, or `Detached`.
5. Drain standard output and error into the event log while retaining at most 64 KiB of process output and 4 KiB per emitted line.
6. Retry remote control every 250 ms only while neither connected nor connecting, for at most 15 seconds.
7. Disconnecting from an external process never ends it. Detach leaves a
   recorder started here running and prevents the app from closing it later.

These internal state names appear in the interface as plain descriptions such
as **External**, **Starting here**, and **Started here**.

Exact-selection mode starts the included `LabRecorderCLI` without blocking the
window. It passes one full output path and one search for each selected stream.
The search uses source ID when available and otherwise limits the name by host.
Pressing Stop sends Enter to the program. The app owns this process and does not
confuse it with an external graphical recorder.

### LabRecorder checks

- [ ] Replacing a connection fails active work.
- [ ] The connection timeout does not change the command timeout.
- [ ] Partial writes and split `OK` replies move forward by exactly one command.
- [ ] A bad reply closes the connection and reports no more than its first 80 bytes.
- [ ] Start sends `update`, `select all`, `filename`, and `start` in that order.
- [ ] A timeout or disconnect stops all later commands in the group.
- [ ] Recording state changes only after a confirmed Start or Stop.
- [ ] Double Start and Stop produce exactly one remote operation.
- [ ] Close during every Start command lets the group finish and then sends one
  final `stop`.
- [ ] Connection replacement, malformed reply, disconnect, and process exit
  leave a clear state and recovery action.
- [ ] The app checks the address before launch; custom and included program
  lookup, working directory, limited output, detach, and the 15-second deadline
  remain correct.

## Closing the desktop window

```mermaid
stateDiagram-v2
    [*] --> Open
    Open --> Closing: first closeEvent
    Closing --> Closing: repeated close / report current deadlines
    Closing --> Closing: a component reports progress
    Closing --> Finalizing: all required components stop
    Finalizing --> Closed: final close
```

On the first close request:

- Enter `Closing` once and ignore repeated close requests.
- Refuse new Start, stream search, filename, and guided work.
- Record which bridge, preview, file, stream-search, file-check, and recorder work
  must stop, along with request times and deadlines.
- Call `LabRecorderClient::beginShutdown()`. It cancels a Start that has not reached `start`; otherwise it arranges or waits for one final Stop. An already active Stop is never duplicated.
- Ask the preview and bridge to stop without blocking, cancel file and stream
  work, and keep the window responsive.
- Poll state every 50 ms only to update the visible component and remaining-time display. No poll declares a still-running worker destroyed.

The visible deadlines are four seconds for the bridge, two seconds for preview
and file work, and 15 seconds for the recorder. These times report a delay; they
do not make the window wait. A Vicon call that cannot be canceled may continue
after four seconds. The window stays responsive and shows it until the call
returns. LSL stream searches are limited to 50 ms, stream-detail reads to 250 ms,
and sample reads do not wait.

A recorder started by the app may be ended only after remote Stop is settled or
the 15-second recorder deadline is exceeded. It receives one further second to
close before the app forces it to end. An external process is never ended. If
the remote connection is already lost, an external recorder settles locally
with a recorded `RecorderConnectionLost` result; a recorder started here waits
for the deadline.

Normal cleanup does not wait for active work. A still-running worker cleans itself
up when it finishes. Backup cleanup waits at most two seconds, followed by one
final 100 ms attempt, but the normal close path does not destroy active workers.

Normal work on the window thread should finish within 50 ms. A Stop request only
sets a cancel flag and returns. SDK, LSL, file, process, and file-check cleanup
runs away from the window thread.

Check normal replies, every Start command, active Stop, disconnect, rules for
recorders started here or elsewhere, bridge connection and retry delays,
preview search and calibration, file opening, canceled file checks, and repeated
close requests.

## Preview

### Change between live and recorded data

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Starting: Start Preview
    Starting --> Running: worker starts
    Starting --> Stopping: Stop or close
    Running --> Stopping: Stop or close
    Running --> Stopping: Open CSV or XDF and remember request
    Stopping --> Stopped: worker actually finishes
    Stopped --> Starting: Start Preview
    Idle --> OfflineLoaded: file load succeeds
    Stopped --> OfflineLoaded: pending file load succeeds
    OfflineLoaded --> Playing: Play Recording
    Playing --> OfflineLoaded: Pause Recording
    OfflineLoaded --> Starting: Start Preview
    Starting --> Failed: worker fails
    Failed --> Starting: retry
```

- Starting live preview stops playback, clears old trails, copies the selected
  streams and controls into `PreviewWorkerConfig`, keeps one newest frame for
  display, and starts a worker. Calibration collection begins only when the user
  selects **Calibrate from Stair Target**.
- Stop only sets a cancel flag and returns within the normal 50 ms window target.
  The panel stays in `Stopping`, with restart and file-open controls
  disabled, until the worker truly ends. The two-second preview deadline is a
  visible delay report, not a forced stop.
- Opening a CSV or XDF while live stores one pending file, asks the worker to stop, and loads the file only after `finished` arrives.
- CSV and XDF use the same playback storage and clock after loading.
- Live drawing runs at 30 or 60 Hz and uses only the newest waiting frame. Stream
  rate, skipped older input, calibration samples, replaced display frames, and
  display delay remain separate counts.

### One live stream

```mermaid
stateDiagram-v2
    [*] --> Resolving
    Resolving --> ConnectedNoSample: stream found and input opens
    Resolving --> Resolving: missing or open error / retry after 1 s
    ConnectedNoSample --> Fresh: sample arrives
    Fresh --> Fresh: newer sample arrives
    Fresh --> Stale: no sample for more than 500 ms
    Stale --> Fresh: sample arrives
    ConnectedNoSample --> Resolving: input error
    Fresh --> Resolving: input error
    Stale --> Resolving: input error
```

Connection and sample age are different. `PreviewFrame::*_stream_present` means
an input is connected even when it has no sample or its last sample is old.

Resolution normally requires the saved source ID. If the same source returns in
several publisher instances, choose the newest creation time and report the
recovery. A missing identity never silently degrades to a name-only match.
**Follow by name** permits a predictable name match and reports duplicates or a
fallback. Stream searches use a 50 ms timeout, stream-detail reads use 250 ms,
sample reads do not wait, and a missing stream retries after one second.

### Load a recorded file and choose streams

```mermaid
stateDiagram-v2
    [*] --> StableSource
    StableSource --> Loading: Open CSV/XDF or drop file
    Loading --> Reading
    Reading --> Indexing
    Indexing --> StreamDetails
    StreamDetails --> Mapping: several possible XDF streams
    Mapping --> Timestamps: user supplies master and groups
    StreamDetails --> Timestamps: one clear choice
    Timestamps --> Calibration
    Calibration --> FramePreparation
    FramePreparation --> Loaded: publish complete result
    Reading --> StableSource: cancel or error
    Indexing --> StableSource: cancel or error
    Mapping --> StableSource: cancel
    Timestamps --> StableSource: cancel or error
    Calibration --> StableSource: cancel or error
    FramePreparation --> StableSource: cancel or error
    Loaded --> StableSource: current source replaced
```

The worker handles reading, indexing, stream details, time correction, stream
choice, calibration, and memory-limited frame preparation. It checks
cancellation between at most 1,024 lines, chunks, samples, or sample groups, and
reports progress for every stage. The 250 ms cancellation target includes time
waiting for a stream choice, which cancellation ends immediately. Failure or cancellation retains the
previous usable source and never publishes a partial recording.

Before preparing XDF frames, group possible streams by role, source ID, name,
host, and channel layout. Compatible pieces are joined. The same source ID on
different hosts, or several incompatible streams for one role, requires the user
to choose. The choice sets the main timeline and included streams. The summary
records the main ID, selected and excluded IDs, joined pieces, time ranges,
unmatched samples, and clock corrections. If no supported stream exists, loading
fails instead of choosing an unrelated numeric stream.

Playback is `Loaded`, `Playing`, or `Paused`. A seek updates the shared CSV/XDF
clock without changing speed. Start/end, one-frame and configurable-time steps,
loop-off end behavior, loop-on wrapping, recent-file opening, drag-and-drop, and
current-image export are explicit choices. The configured memory limit caps the
decoded result, and drawing fewer frames does not change file-check numbers.

### Stair alignment

```mermaid
stateDiagram-v2
    [*] --> Manual
    Manual --> Collecting: user selects Calibrate
    Collecting --> Collecting: add a stable tracked pose
    Collecting --> Collecting: target is lost or moves / restart collection
    Collecting --> Manual: math or quality check fails
    Collecting --> AutomaticSession: 20 good samples solve alignment
    AutomaticSession --> SavedProfile: Save Session Calibration
    AutomaticSession --> Manual: Use Manual Transform
    SavedProfile --> Manual: Use Manual Transform
    Manual --> SavedProfile: Apply saved calibration
```

`SavedProfile` is the internal state name; the interface shows **Saved calibration**.

- Losing the target clears the collected poses.
- A pose outside the allowed movement from the first pose restarts collection from that new pose.
- Missing or incompatible coordinate details pause collection for an explicit
  fallback confirmation. Compatibility and the rejection reason remain visible.
- Automatic alignment stays in memory for the desktop session and is not saved
  unless the user explicitly creates a complete saved calibration.
- Transform changes reach the worker through a lock. The drawing area then asks to fit the view again.
- Saved-calibration import/export, **Copy**, **Hide**, stair-pose editing, and
  **Apply** preserve the record ID, version, physical setup, stair identity,
  coordinate names, notes, creation time, sample count, and position/angle error.
- Calibration progress is emitted no faster than every 100 ms so the target
  stream cannot dominate GUI work.

## Check the setup and record a session

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Preparing: Start Session
    Preparing --> Preparing: start bridge and preview / discover streams
    Preparing --> SetupBlocked: required check fails
    SetupBlocked --> Ready: correct checks
    SetupBlocked --> Ready: Record Anyway with reason
    Preparing --> Ready: all required checks pass
    Ready --> Starting: Start recorder
    Starting --> Recording: Start acknowledged or exact-selection recorder starts
    Starting --> Failed: recorder operation fails
    Recording --> Stopping: Stop Session or Stop Recording
    Stopping --> Verifying: Stop acknowledged and file finalizes
    Verifying --> Complete: file check finishes
    Verifying --> Failed: file missing or needs attention
    Failed --> Idle: recover or begin another run
    Complete --> Idle: begin another run
```

Setup items are `Required`, `Warning`, or `Information`. Required bridge,
recorder, path, selected stream, sample age, and channel-layout failures block
Start. Recorder-only mode turns the bridge requirement into information.
Warnings such as low storage, missing stream details, duplicate choices, or a
low expected rate stay visible but do not block. A blocked result can be bypassed
only with a reason; both the result and reason enter the session log and export.

The guided action starts bridge, preview, stream search, setup check, and recorder
in order while preserving independent controls. Its reverse action stops the
recorder, waits for the file check, then stops preview and bridge. Any partially
completed state remains visible and independently stoppable.

## Check the file after recording

```mermaid
stateDiagram-v2
    [*] --> NotRun
    NotRun --> WaitingForFile: successful Stop
    WaitingForFile --> Running: exact XDF appears
    WaitingForFile --> NeedsAttention: finalization deadline expires
    Running --> Verified: all required data passes
    Running --> VerifiedWithWarnings: data present with warnings
    Running --> NeedsAttention: missing/incompatible data or read failure
    Running --> NeedsAttention: canceled by close
```

The last diagram uses internal state names. The interface shows `Verified` as
**Checked** and `VerifiedWithWarnings` as **Checked with warnings**.

The file check runs away from the window thread and never changes the recording.
It compares the recorded streams with the list saved before Start, then reports
source ID, channel layout, sample count, range, duration, measured rate, gaps,
clock corrections, repaired timestamps, and recovery from a cut-off file ending.
The report can be exported and links to playback. Automatic run increment occurs
only after the output exists and the selected completion rule passes.

## HoloLens tracker and provider

### Tracker life

```mermaid
stateDiagram-v2
    [*] --> PermissionRequest
    PermissionRequest --> Watching: access allowed and watcher starts
    PermissionRequest --> Unavailable: denied or startup fails
    Watching --> OpeningTracker: tracker appears
    OpeningTracker --> Active90Hz: open, exact 90 Hz, and spatial node work
    OpeningTracker --> Watching: wrong rate, no node, or open failure
    Active90Hz --> Watching: active tracker is removed
    Active90Hz --> Restarting: repeated read or pose failure
    Restarting --> PermissionRequest: old tracker and watcher stop
    Active90Hz --> Destroyed: Unity component is destroyed
    Watching --> Destroyed: Unity component is destroyed
```

Four counters and guards keep old work out of a new session:

- `watcherGeneration` rejects a late result from an old watcher start.
- `trackerLifecycleGeneration` rejects a late `OpenAsync` result after removal or restart.
- `sessionGeneration` marks raw readings and tells the LSL output when the tracker session changes.
- Starting, removing, or restarting a tracker resets the reading-time guard, clears both queues, and clears pose-failure counts.

### One gaze sample

At each publishing step:

1. `TryGetNextSample` reads at most one raw value while holding the tracker guard.
2. Reject a missing, old, duplicate, out-of-order, or invalid capture time.
3. Copy the combined ray and any available left and right rays in tracker space.
4. Add the raw reading under the 25 ms time-span and 360-item limits.
5. Unity `Update` handles at most 32 raw readings. For each one, find the device pose at the original time, convert the rays, and add a `GazeSample` to the next queue.
6. Before publishing, reduce an over-limit converted queue to its newest value and then take the oldest retained value.

If finding the device pose simply fails, still make a sample at the capture time with invalid rays. If it throws repeatedly, restart the tracker. Never invent a new capture time.

## HoloLens LSL output

```mermaid
stateDiagram-v2
    [*] --> WaitingForTracker: Start and references are valid
    WaitingForTracker --> Publishing: active session reports 90 Hz
    Publishing --> Stopping: rate or session disappears or changes
    Publishing --> RecoveringProvider: provider keeps failing
    Publishing --> Disabled: output or worker fails permanently
    RecoveringProvider --> WaitingForTracker: worker stops and tracker restart begins
    Stopping --> WaitingForTracker: worker stops and resources close
    Stopping --> Stopping: 500 ms limit / keep resources
    WaitingForTracker --> Destroyed: OnDestroy
    Publishing --> Destroyed: OnDestroy after a successful stop
```

Keep these rules:

- Do not replace or close the stream while the old worker may still send to it.
- If the worker does not stop within 500 ms, keep the worker and stream and try again during a later update.
- Brief provider errors do not replace the tracker. About one second of back-to-back provider errors starts recovery.
- Drop bad capture timestamps, but continue the publishing schedule.
- Treat an LSL output exception as a permanent worker failure.
- When recreating the stream, use the saved name, type, source ID, and current expected rate.

The model-target output has fewer states. It checks its references, creates one stream, and sends in every `LateUpdate`. A creation or send exception disables it. Destruction releases its references.

### Device checks

- [ ] Denied permission leaves no partial stream.
- [ ] A tracker without exact 90 Hz never starts publishing.
- [ ] A late `OpenAsync` result from an old session cannot replace the current tracker.
- [ ] Removal clears both queues and blocks old-session samples.
- [ ] Brief provider errors do not restart the tracker at once.
- [ ] Lasting provider errors stop the worker before tracker restart.
- [ ] A worker stop timeout keeps output resources until the worker ends.
- [ ] A recreated stream keeps its identity and never sends an earlier timestamp.
- [ ] Destroying the component stops the watcher, tracker, and worker before releasing resources.

Use the [hardware test guide](device-parity-runbook.md) to collect device evidence.

## Main source files

- `vicon-lsl-bridge/src/ViconLSLBridge.cpp`
- `vicon-lsl-bridge/src/ViconClient.cpp`
- `vicon-lsl-bridge/src/ViconFrameMapper.*`
- `vicon-lsl-bridge/src/gui/BridgeWindow.*`
- `vicon-lsl-bridge/src/gui/LabRecorderClient.*`
- `vicon-lsl-bridge/src/gui/LabRecorderRuntimePolicy.*`
- `vicon-lsl-bridge/src/gui/PreviewPanel.*`
- `vicon-lsl-bridge/src/gui/PreviewStreamWorker.*`
- `hololens-gaze-lsl/Assets/Scripts/GazeDataProvider.cs`
- `hololens-gaze-lsl/Assets/Scripts/GazePublisherWorker.cs`
- `hololens-gaze-lsl/Assets/Scripts/GazeLSLOutlet.cs`
- `hololens-gaze-lsl/Assets/Scripts/VuforiaModelTargetPoseOutlet.cs`
- State and recovery checks under `vicon-lsl-bridge/tests` and `hololens-gaze-lsl/Tests`
