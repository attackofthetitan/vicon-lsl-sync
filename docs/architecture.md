# How the code is organized

## What this guide is for

This guide shows which part of the project does each job. Read it before you move or split code.

A tidy-up that only rearranges code must not change public names, how the app behaves, stream layouts, settings, build targets, or third-party code. If a change does touch any of those, review it on its own and plan how existing users and files will cope.

Related guides:

- [Behavior that must stay the same](behavior-contract.md)
- [How each part starts, stops, and recovers](runtime-state-machines.md)
- [How time and coordinates work](time-and-coordinate-semantics.md)

## Our code

| Folder | What it does | What it can use |
| --- | --- | --- |
| `vicon-lsl-bridge/src` | Reads settings and command-line options, talks to Vicon, turns each frame into samples, opens LSL streams, and reconnects after errors | C++17. Some files also use the Vicon SDK and liblsl |
| `vicon-lsl-bridge/src/preview` | Reads recordings and live samples, does the preview math and calibration, runs playback, and measures rates | The C++17 standard library only |
| `vicon-lsl-bridge/src/gui` | The Qt desktop app: live preview, LabRecorder control, saved settings, and drawing | The bridge and preview code, liblsl, and Qt 6 |
| `hololens-gaze-lsl/Assets/Scripts` | Reads HoloLens gaze, moves it into the Unity world, sends it to LSL, and sends the Vuforia stair target position | Unity, Extended Eye Tracking, Mixed Reality OpenXR, Vuforia, and the C# liblsl wrapper |
| `stream-contracts` | The HoloLens stream layouts, as JSON | JSON |
| `tools/generate_stream_contracts.py` | Writes matching C++ and C# stream definitions from those JSON files | The Python standard library |
| `vicon-lsl-bridge/packaging/windows` | Builds the Windows packages and collects the license files they need | PowerShell and Windows build tools |
| `.github/workflows/build-bridge.yml` and `.github/scripts` | Build, test, package, and publish the project | GitHub's build machines and pinned actions |

## Other people's code

These folders are Git submodules: copies of other projects, pinned to one version. We do not own their code.

- `labrecorder` is LabRecorder. The desktop app controls it over its remote-control connection and ships it in release packages.
- `vicon-lsl-bridge/external/vicon-datastream-sdk` is the Vicon DataStream SDK.
- `hololens-gaze-lsl/external/liblsl` is the version of liblsl built for HoloLens (ARM64 UWP) and used by Unity.

Do not edit these folders during a normal tidy-up. Treat a new submodule version, a local patch, a LabRecorder protocol change, or a new liblsl version as a separate dependency update.

The desktop bridge uses an installed liblsl if it finds one, and otherwise downloads the version named in `vicon-lsl-bridge/CMakeLists.txt`. Changing that rule is also a dependency update.

## Build layers

```text
vicon-lsl-bridge-logic
  command line, stream definitions, frame mapping, and preview code
                 |
                 v
vicon-lsl-bridge-runtime
  Vicon SDK access, LSL output, and reconnecting
          |                         |
          v                         v
vicon-lsl-bridge             vicon-lsl-bridge-gui
command-line program            Qt desktop program
```

Keep these rules:

- `vicon-lsl-bridge-logic` must build without the Vicon SDK, liblsl, Qt, or any download.
- `vicon-lsl-bridge-runtime` keeps all Vicon SDK and liblsl code out of the logic layer.
- The command-line app and desktop app stay separate programs.
- CMake options, target names, test names, and program file names stay the same.
- Windows packages keep the same launcher, stair model, LabRecorder folder, runtime libraries, and license file names.

## How Vicon data moves

```text
Vicon DataStream server
        |
        v
ViconClient ---- read results ----> ViconFrameMapper
        |                                  |
        | frame time                       | values, status, and errors
        v                                  v
ViconLSLBridge -----------------> MarkerStream / SegmentStream
                                             |
                                             v
                                         LSL streams
```

Each class has one main job:

- `ViconClient` turns Vicon SDK results into this project's own types. Nothing
  from the SDK leaks past this class, except as text in error messages.
- `ViconFrameMapper` keeps the order Vicon lists things in, decides which values are usable, fills in fixed-size "missing" values, collects errors, and makes sure timestamps only go forward.
- `MarkerStream` and `SegmentStream` keep their public interfaces. They share one private helper that sets up the LSL stream details, checks sample sizes, owns the stream, and handles send errors.
- `ViconLSLBridge::run()` connects, reads the first frame, opens the streams, and
  cleans up before trying again. `streamFrames()` sends frames and watches for
  layout changes. Deciding whether to retry stays in `run()`.
  `refreshStreams()` reads the layout once and uses it to open or replace
  streams. If a regular layout check fails, the current streams stay open. If
  the first check fails, the bridge waits and reconnects.
- The marker and segment streams build their values with plain loops, then hand
  them to the shared helper to check and send.

## Desktop app

`BridgeWindow` ties the on-screen controls to the session data and background
work. It keeps the real session state in variables instead of reading it back
from label text.

```text
QSettings ----> SessionConfiguration ----> bridge, preview, recorder, file name
                       |
                       v
BridgeWindow ----> SessionEventLog / SetupCheckResult
     |
     +----> dashboard, setup check, shutdown steps
     |
     +----> BridgeWorker / PreviewStreamWorker / file workers
```

The main desktop parts are:

- `SessionConfiguration` is the saved session setup. Unless you choose other
  preview streams, it points the marker and segment preview at the bridge's own
  streams. Window size, layout, tabs, and recent files live in `SessionUiState`
  and are not part of presets.
- `BridgeWindow` holds the setup check result, a limited event log, the last
  error, dashboard values, the shutdown steps, and the shared settings object,
  which it passes to `PreviewPanel`.
- The window and panel create their background workers themselves. There is no
  factory or extra controller layer.
- `BridgeWorker` runs one `ViconLSLBridge` off the window thread and reports its
  state.
- `PreviewStreamWorker` opens the chosen LSL streams and keeps only the newest
  frame waiting to be drawn. A 30 or 60 Hz timer draws it. Rate measurement and
  calibration keep going on their own.
- `StreamDiscoveryWorker` looks for streams right before recording.
  `RecorderProcessController` starts and stops only the recorders this app
  started. `LabRecorderClient` sends one group of remote commands at a time. The
  current group holds its own progress, so there is no separate "busy" flag. It
  clears the group before telling anyone it finished, so they can safely start
  the next one straight away.
- `PreviewFileLoader` reads CSV or XDF files, fixes up times, applies
  calibration, and prepares frames within a memory limit, all off the window
  thread. `RecordingVerifier` reads the finished XDF and reports on its samples
  and timing.
- On a Mac, `MacInstallation` runs before the window opens. If the app was opened
  from its disk image, it offers to move it to Applications. Once the app runs
  from somewhere else, it ejects the image. It recognises the image by a version
  file at its top level.

The preview only reads streams. It must never change their timestamps or
layouts. Each live input uses the time of its last sample to know whether data
is there. Connection errors and read errors reset samples the same way. Stream
search updates the stream list in one place and lets go of its lock before
telling the window. Playback may skip frames when drawing, but the file check
keeps exact counts, start and end times, gaps, clock fixes, and timestamp
repairs.

### Playback memory

CSV loading keeps one set of decoded frames within the memory limit. XDF loading
can briefly hold a file index, the chosen streams' data, and the decoded frames,
each within the limit. So XDF loading can use about three times the limit at its
peak, plus some extra for stream details and libraries. Once loading finishes,
only the decoded frames stay in memory.

The built-in XDF reader is for looking at data and basic file checks. It does not
replace proper XDF analysis tools.

### Drawing the preview

The preview is a quick visual check, so `PreviewWidget` is a plain `QWidget`
drawn with `QPainter`. It does not need OpenGL. It has Fit View, Reset Camera,
growing bounds, axis and unit labels, counts of usable values, trail clean-up,
readable colours, and can draw without a screen for tests. It does not hide
objects behind other objects, light the 3D model, or let you click on objects.

## How HoloLens data moves

```text
Eye tracker
    |
    v
GazeDataProvider reads a reading and its time on the publishing thread
    |
    v
raw queue
    |
    v
Unity main thread finds where the headset was and moves the ray into the world
    |
    v
converted queue
    |
    v
GazePublisherWorker -> GazeLSLOutlet -> LSL

Vuforia target in LateUpdate
    |
    v
VuforiaModelTargetPoseOutlet -> its own LSL stream
```

Gaze and the stair target use separate LSL streams but the same coordinates. Gaze never goes through the desktop bridge.

## Where work runs

| Who | What it owns | Rule to keep |
| --- | --- | --- |
| Command-line process | `ViconLSLBridge` and the stop request | `stop()` only flips the run flag. The bridge loop closes connections and streams itself. |
| `BridgeWorker` | The running bridge | It only updates the window through queued Qt signals. |
| Window thread | Widgets, session data, settings, timers, `LabRecorderClient`, and `QProcess` | It never waits forever. When you close the window, it stays in Closing until the work that must finish has finished. |
| `PreviewStreamWorker` | Four LSL inputs, rate measurement, and the newest waiting frame | Stream searches and detail reads have time limits, sample reads never wait, and alignment updates use a lock. |
| `PreviewFileLoader` | Reading CSV/XDF, choosing streams, calibration, and preparing frames | It checks for cancel between small batches of lines, chunks, and samples. It only returns a result once loading is complete. |
| `RecordingVerifier` | The XDF stream list and health report after Stop | It never changes or deletes a recording, and it uses exact counts even when the preview skips frames. |
| Unity main thread | Unity transforms, Vuforia objects, and `SpatialGraphNode.TryLocate` | It moves gaze into the world and reads scene objects. |
| `GazePublisherWorker` | Gaze timing, packing, and sending to LSL | It uses the capture time already stored in each sample. |
| HoloLens tracker lock | Tracker sessions, watcher versions, and both queues | Late replies and old samples can never leak into a new tracker session. |

## Names that must not change

### C++

Keep the same header paths, names, and function signatures for:

- `Config`, command-line results, and command-line parsing and printing.
- `ViconLSLBridge`, `BridgeState`, `BridgeStatus`, and the status callback.
- `ViconClient` and its read-result types.
- `MarkerStream`, `SegmentStream`, `StreamOutlet`, `StreamOutletFactory`, and `StreamPushResult`.
- `StreamSchema`, sample types, frame mapping, discovery, and error-reporting types and functions.
- Preview types and functions under `src/preview`.
- Qt classes, signals, and slots under `src/gui`.

Private code can move into smaller files as long as the existing headers still work.

### Unity scripts

Unity scenes and assets save type and field names. Keep these names unless the change also updates existing scenes and assets:

- `GazeDataProvider`, `GazeLSLOutlet`, `VuforiaModelTargetPoseOutlet`, and `GazeLSLConfig`.
- The saved `config`, `gazeProvider`, and `modelTarget` fields.
- Public fields on `GazeLSLConfig`.
- `GazeSample` field names and their order.
- The interfaces and method signatures the tests use: `IGazeSampleProvider`, `IGazeSampleOutlet`, `GazePublisherWorker`, `GazeCoordinateTransform`, and `ModelTargetPoseEncoder`.

### Commands, streams, files, and settings

These count as public behavior even though they are not code:

- Command-line options, defaults, messages, and exit codes.
- LSL stream names, layouts, details, timing, coordinates, and how streams get replaced.
- The order of LabRecorder commands and how replies are handled.
- Saved setting names and values.
- How CSV and XDF files are read for the preview.
- CMake options and targets.
- Release file names and what is inside each package.

## How to tidy up code safely

1. Write down what the code does now and what it should output.
2. Only delete code once searches and tests show nothing uses it.
3. Keep the old public header while moving private code into smaller files.
4. Keep stream names and channel layouts in one place before removing duplicate stream code.
5. Pull math and decisions out of threaded code before changing how threads are controlled.
6. Keep dependency updates, framework changes, stream changes, coordinate changes, and new features out of a tidy-up.
7. Keep live and recorded time correction separate. They follow different rules.
8. Do not touch submodules.

## What the tests cover today

The tests cover:

- Command-line behavior, stream layouts, Vicon mapping and timing, preview parsing and math, calibration, CSV/XDF loading, playback, and rate display.
- Opening streams and send failures, empty layouts, expected rates, and passing timestamps through.
- Recorder commands and states, button rules, repeated clicks, replies that
  arrive in pieces or are broken, timeouts, replaced connections, closing during
  Start, which recorders the app may close, file path rules, setup checks, file
  checks, the settings format, and saved calibrations.
- Memory limits for short, one-hour, and multi-hour recordings; cancelling CSV
  and XDF loads; file size limits; joining restarted streams; keeping only the
  newest live frame; jumping around in playback; drawing at small and scaled
  sizes; readable colours; keyboard and screen-reader labels; and stopping the
  preview.
- The packaged app's layout, finding LSL streams on the same computer, finding the bundled or custom recorder, portable paths, optional recorder start, and the stair model files.
- On a Mac, that packaged programs find every library inside the package, and
  which disk images the app offers to move from or ejects.
- HoloLens channel and position packing, coordinate changes, timing, queue rules, sending, cancelling, and recovery, all without Unity or a headset.
- Generated files being up to date, builds on every platform, and the contents of
  the Windows package.

Still missing:

- A saved example of the full LSL stream description for every stream.
- A single example that sends the same fake samples through both the live and XDF preview while keeping their different clock rules.
- Unity, Windows device features, Vuforia, and real hardware still need the [hardware test guide](device-parity-runbook.md).
- Real screens, remote desktop, virtual machines, Vicon, HoloLens, and Vuforia
  still need the hardware guide. Because the preview is a plain `QWidget`, the
  automated tests can draw it without a graphics card.

## Main source files

- `README.md`
- `.gitmodules`
- `vicon-lsl-bridge/CMakeLists.txt`
- `vicon-lsl-bridge/src/ViconLSLBridge.*`
- `vicon-lsl-bridge/src/ViconClient.*`
- `vicon-lsl-bridge/src/ViconFrameMapper.*`
- `vicon-lsl-bridge/src/MarkerStream.*`
- `vicon-lsl-bridge/src/SegmentStream.*`
- `vicon-lsl-bridge/src/gui/*`
- `vicon-lsl-bridge/src/preview/*`
- `hololens-gaze-lsl/README.md`
- `hololens-gaze-lsl/Assets/Scripts/*`
- `stream-contracts/hololens-gaze.json`
- `stream-contracts/hololens-model-target.json`
- `.github/workflows/build-bridge.yml`
