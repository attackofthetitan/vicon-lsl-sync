# How the code is organized

## Why this guide exists

This guide shows which part of the project owns each job. Use it when moving or splitting code.

A code-only cleanup may move code, but it must not change public names, program behavior, stream layouts, settings, build targets, or third-party code. Review any such behavior change separately and plan how existing users and files move forward.

Related guides:

- [Behavior that must stay the same](behavior-contract.md)
- [How services start, stop, and recover](runtime-state-machines.md)
- [How time and coordinates work](time-and-coordinate-semantics.md)

## Code owned by this project

| Path | What it does | What it may use |
| --- | --- | --- |
| `vicon-lsl-bridge/src` | Reads settings and command-line options, talks to Vicon, turns frames into samples, creates LSL streams, and reconnects after errors | C++17; runtime files may use the Vicon SDK and liblsl |
| `vicon-lsl-bridge/src/preview` | Reads preview files and samples, performs preview math and calibration, controls playback, and tracks rates | C++17 standard library only |
| `vicon-lsl-bridge/src/gui` | Shows the Qt desktop app, runs the live preview, controls LabRecorder, saves settings, and draws data | Bridge and preview code, liblsl, and Qt 6 |
| `hololens-gaze-lsl/Assets/Scripts` | Reads HoloLens gaze, converts it to the Unity world, sends it to LSL, and sends the Vuforia target pose | Unity, Extended Eye Tracking, Mixed Reality OpenXR, Vuforia, and the managed liblsl binding |
| `stream-contracts` | Stores the HoloLens stream layouts in JSON | JSON |
| `tools/generate_stream_contracts.py` | Creates matching C++ and C# stream definitions from the JSON files | Python standard library |
| `vicon-lsl-bridge/packaging/windows` | Builds safe Windows packages and gathers required license files | PowerShell and Windows build tools |
| `.github/workflows/build-bridge.yml` and `.github/scripts` | Build, check, package, and publish the project | Hosted build systems and pinned actions |

## Third-party code

These folders are Git submodules. This project pins them but does not own their source:

- `labrecorder` contains LabRecorder. The desktop app controls it through its remote-control TCP connection and includes its programs in release packages.
- `vicon-lsl-bridge/external/vicon-datastream-sdk` contains the Vicon DataStream SDK wrapper and source.
- `hololens-gaze-lsl/external/liblsl` contains the ARM64 UWP liblsl fork used by Unity.

Do not edit these folders during a normal code cleanup. Treat any new submodule revision, local third-party patch, LabRecorder protocol change, or liblsl version change as a separate dependency update.

The desktop bridge can use an installed liblsl package or download the revision named in `vicon-lsl-bridge/CMakeLists.txt`. Review a change to that rule as a separate dependency update.

## Build layers

```text
vicon-lsl-bridge-logic
  command line, stream definitions, frame mapping, and preview code
                 |
                 v
vicon-lsl-bridge-runtime
  Vicon SDK access, LSL output, and reconnect control
          |                         |
          v                         v
vicon-lsl-bridge             vicon-lsl-bridge-gui
command-line program            Qt desktop program
```

Keep these rules:

- `vicon-lsl-bridge-logic` must build without the Vicon SDK, liblsl, Qt, or a downloaded test library.
- `vicon-lsl-bridge-runtime` keeps Vicon SDK and liblsl code out of the logic layer.
- The command-line and desktop apps remain separate programs.
- Existing CMake options, target names, test names, and program filenames stay the same.
- Windows packages keep the current launcher, stair model, LabRecorder layout, runtime libraries, and license filenames.

## How Vicon data moves

```text
Vicon DataStream server
        |
        v
ViconClient ---- read results ----> ViconFrameMapper
        |                                  |
        | frame time                       | fixed-size samples and errors
        v                                  v
ViconLSLBridge -----------------> MarkerStream / SegmentStream
                                             |
                                             v
                                         LSL streams
```

Each class has one main job:

- `ViconClient` turns Vicon SDK results into types owned by this project. SDK result values do not pass beyond this class, except as text in error messages.
- `ViconFrameMapper` keeps Vicon discovery order, decides whether values are valid, creates fixed-size invalid values, gathers errors, and keeps timestamps increasing.
- `MarkerStream` and `SegmentStream` keep their existing public interfaces. A shared private helper creates the common LSL information, checks sample size, owns the output stream, and handles send errors.
- `ViconLSLBridge` owns the connection loop, retries, layout checks, stream replacement, source IDs, status messages, and cleanup order.

## Desktop app work

The desktop app runs three separate jobs:

1. `BridgeWorker` runs one `ViconLSLBridge` on a background thread and sends status back to `BridgeWindow`.
2. `PreviewStreamWorker` reads LSL streams on another background thread. It sends complete `PreviewFrame` values to the drawing code on the main GUI thread.
3. `LabRecorderClient` and the optional LabRecorder process stay on the main GUI thread. They control recording; they do not write XDF files themselves.

The preview only reads streams. It must not change the timestamps or layouts sent by producers.

The built-in XDF reader is for visual checks. The official XDF tools remain the right choice for scientific analysis.

## How HoloLens data moves

```text
Eye tracker
    |
    v
GazeDataProvider reads a timestamped value on the publishing thread
    |
    v
raw queue
    |
    v
Unity main thread finds the device pose and converts the ray to world space
    |
    v
converted queue
    |
    v
GazePublisherWorker -> GazeLSLOutlet -> LSL

Vuforia target in LateUpdate
    |
    v
VuforiaModelTargetPoseOutlet -> separate LSL stream
```

Gaze and target pose use separate LSL streams, but they share one coordinate system. Gaze never passes through the desktop bridge.

## Which thread owns what

| Owner | What it owns | Rule to keep |
| --- | --- | --- |
| Command-line process | `ViconLSLBridge` and the stop request | `stop()` only changes the run flag. The bridge loop cleans up connections and streams. |
| `BridgeWorker` | The running bridge | It updates the GUI only through queued Qt signals. |
| Main GUI thread | Widgets, settings, timers, `LabRecorderClient`, and an optional LabRecorder process | These objects stay on the main GUI thread. |
| `PreviewStreamWorker` | Four LSL inputs and the latest samples | It resolves and reads streams. Transform updates use a lock. |
| Unity main thread | Unity transforms, Vuforia objects, and `SpatialGraphNode.TryLocate` | It performs world conversion and reads scene objects. |
| `GazePublisherWorker` | Gaze timing, encoding, and LSL sends | It uses the capture time already stored in each sample. |
| HoloLens tracker guard | Tracker sessions, watcher versions, and both queues | Old callbacks and old samples cannot enter a new tracker session. |

## Names and interfaces that must stay stable

### C++

Keep the existing header paths, names, and function signatures for:

- `Config`, command-line results, and command-line parsing and formatting.
- `ViconLSLBridge`, `BridgeState`, `BridgeStatus`, and the status callback.
- `ViconClient` and its project-owned read results.
- `MarkerStream`, `SegmentStream`, `StreamOutlet`, `StreamOutletFactory`, and `StreamPushResult`.
- `StreamSchema`, sample types, frame mapping, discovery, and error-reporting types and functions.
- Preview types and functions under `src/preview`.
- Qt classes, signals, and slots under `src/gui`.

Private code may move to smaller files as long as existing headers still work.

### Unity and C#

Unity scenes and assets store type and field names. Keep these names unless the change includes a plan to update existing Unity scenes and assets:

- `GazeDataProvider`, `GazeLSLOutlet`, `VuforiaModelTargetPoseOutlet`, and `GazeLSLConfig`.
- The saved `config`, `gazeProvider`, and `modelTarget` fields.
- Public fields on `GazeLSLConfig`.
- `GazeSample` field names and value order.
- Interfaces and method signatures used by the device-independent checks: `IGazeSampleProvider`, `IGazeSampleOutlet`, `GazePublisherWorker`, `GazeCoordinateTransform`, and `ModelTargetPoseEncoder`.

### Commands, streams, files, and settings

The following are public behavior even though they are not source-code interfaces:

- Command-line options, defaults, useful messages, and exit codes.
- LSL stream names, layouts, metadata, timing, coordinates, and replacement behavior.
- LabRecorder command order and reply handling.
- Saved settings names and values.
- CSV and XDF preview rules.
- CMake options and targets.
- Release filenames and package contents.

## How to clean up code safely

1. Record the current behavior and expected outputs.
2. Delete code only after searches and checks prove nothing uses it.
3. Keep the old public header while moving private work into smaller files.
4. Put stream names and channel layouts in one source before removing duplicate stream code.
5. Move math and state decisions out of threaded code before changing thread control.
6. Keep dependency updates, framework changes, stream changes, coordinate changes, and new features out of a code-only cleanup.
7. Keep live and recorded time correction separate; they follow different rules.
8. Do not change submodules during a first-party code cleanup.

## Checks available now

The repository checks:

- Command-line behavior, stream layouts, Vicon mapping and time handling, preview parsing and math, calibration, CSV/XDF loading, playback, and rate display.
- Stream creation and send failure, empty layouts, expected rates, and timestamp forwarding.
- LabRecorder command order, broken-up replies, timeouts, disconnects, filename rules, window state, and saved setting names.
- Packaged GUI layout, local LSL discovery, optional LabRecorder startup, and stair assets.
- HoloLens channel and pose encoding, coordinate conversion, time handling, queue rules, publishing, cancellation, and recovery without Unity or hardware.
- Generated-file freshness, cross-platform builds, and Windows package contents.

Important gaps remain:

- There is no saved, normalized XML example for every complete LSL stream description.
- There is no single saved example that sends the same fake samples through both live and XDF preview paths while keeping their different clock rules.
- Unity, Windows device APIs, Vuforia, and physical hardware still need the [hardware test guide](device-parity-runbook.md).
- The bridge checks cover first-frame, send-failure, and stop paths. A single fake-client scenario does not yet cover every repeating layout check and discovery error from start to finish.

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
