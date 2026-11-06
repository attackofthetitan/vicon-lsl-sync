# How the code is organized

Read this before you move or split code.

A tidy-up that only moves code around must not change public names, behavior,
stream layouts, settings, build targets, or third-party code. If your change
touches any of those, review it on its own and plan how existing users and files
will cope.

The other guides:

- [Behavior that must stay the same](behavior-contract.md): options, streams,
  recorder commands, file names, and settings.
- [How each part starts, stops, and recovers](runtime-state-machines.md): the
  order things happen in.
- [How time and coordinates work](time-and-coordinate-semantics.md): the math.
- [Hardware test guide](device-parity-runbook.md): checks on real equipment.

## Folders

| Folder | What it does | What it may use |
| --- | --- | --- |
| `vicon-lsl-bridge/src` | Reads options, talks to Vicon, turns frames into samples, opens LSL streams, and reconnects | C++17. Some files also use the Vicon SDK and liblsl |
| `vicon-lsl-bridge/src/preview` | Reads recordings and live samples, preview math, calibration, playback, rates | The C++17 standard library only |
| `vicon-lsl-bridge/src/gui` | The Qt desktop app | The bridge and preview code, liblsl, and Qt 6 |
| `hololens-gaze-lsl/Assets/Scripts` | Reads HoloLens gaze, moves it into the Unity world, sends it to LSL, and sends the Vuforia stair target | Unity, Extended Eye Tracking, Mixed Reality OpenXR, Vuforia, and C# liblsl |
| `stream-contracts` | The HoloLens stream layouts, as JSON | JSON |
| `tools/generate_stream_contracts.py` | Writes matching C++ and C# stream definitions from that JSON | The Python standard library |
| `vicon-lsl-bridge/packaging/windows` | Builds the Windows packages and collects license files | PowerShell and Windows build tools |
| `.github` | Builds, tests, packages, and publishes | GitHub's build machines, with actions pinned to exact versions |

Three folders are Git submodules, which means copies of other projects pinned
to one version. We don't own them, so don't edit them in a tidy-up:

- `labrecorder`: LabRecorder. The app controls it and ships it in releases.
- `vicon-lsl-bridge/external/vicon-datastream-sdk`: the Vicon DataStream SDK.
- `hololens-gaze-lsl/external/liblsl`: liblsl built for the HoloLens.

A new submodule version, a local patch, or a new liblsl version is a dependency
update and gets its own change. The desktop bridge uses an installed liblsl if
it finds one, and otherwise downloads the version named in `CMakeLists.txt`.

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

- The logic layer builds without the Vicon SDK, liblsl, Qt, or any download.
- All Vicon SDK and liblsl code stays in the runtime layer.
- The command-line and desktop apps stay separate programs.
- CMake options, target names, test names, and program names stay the same.

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

- `ViconClient` turns Vicon SDK results into our own types. Nothing from the SDK
  gets past it, except as text in error messages.
- `ViconServerAddress` splits the server setting into the hosts and ports the SDK
  will try, so the quick check before connecting probes the same ones.
- `ViconFrameMapper` keeps Vicon's order, decides which values are usable, fills
  in fixed-size "missing" values, collects errors, and keeps timestamps going up.
- `MarkerStream` and `SegmentStream` share one private helper that sets up the
  stream details, checks sample sizes, owns the stream, and handles send errors.
- `ViconLSLBridge::run()` connects, reads the first frame, opens the streams, and
  cleans up before trying again. It's the only place that decides to retry.

## Desktop app

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

- `SessionConfiguration` is the saved setup. Window size, layout, tabs, and
  recent files live in `SessionUiState` instead and aren't part of presets.
- `BridgeWindow` ties the controls to the session. It keeps the real state in
  variables, never by reading label text back. It creates its workers itself.
- `BridgeWorker` runs one `ViconLSLBridge` off the window thread.
- `PreviewStreamWorker` reads the live streams and keeps only the newest frame
  for a 30 or 60 Hz timer to draw.
- `StreamDiscoveryWorker` looks for streams right before recording.
- `LabRecorderClient` sends one group of remote commands at a time. It clears the
  group before saying it finished, so the next one can start right away.
- `RecorderProcessController` starts and stops only the recorders this app
  started.
- `PreviewFileLoader` reads CSV and XDF files off the window thread.
  `RecordingVerifier` checks the finished XDF.
- On a Mac, `MacInstallation` runs before the window opens. It offers to move the
  app out of its disk image, and ejects the image once the app runs from
  somewhere else.

The preview only reads streams. It never changes their timestamps or layouts.

The preview is a quick visual check, so `PreviewWidget` is a plain `QWidget`
drawn with `QPainter`, with no OpenGL. It can draw without a screen for tests.
It doesn't hide objects behind other objects, light the model, or let you click
on objects.

CSV loading keeps one set of frames within the memory limit. XDF loading can
briefly hold a file index, the chosen streams, and the frames, each within the
limit, so it can peak at about three times the limit. Only the frames stay once
loading is done.

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

Gaze and the stair target use separate streams but the same coordinates. Gaze
never goes through the desktop app.

## Where work runs

| Who | Owns | Rule |
| --- | --- | --- |
| Command-line process | `ViconLSLBridge` and the stop request | `stop()` only flips a flag. The bridge closes things itself. |
| `BridgeWorker` | The running bridge | Talks to the window only through Qt signals. |
| Window thread | Widgets, session data, settings, timers, `LabRecorderClient`, `QProcess` | Never waits forever. Closing stays in Closing until required work is done. |
| `PreviewStreamWorker` | Live LSL inputs, rates, the newest frame | Searches and detail reads have time limits. Sample reads never wait. |
| `PreviewFileLoader` | Reading CSV and XDF and preparing frames | Checks for cancel between small batches. Returns only a finished result. |
| `RecordingVerifier` | The file check after Stop | Never changes a recording. Uses exact counts. |
| Unity main thread | Transforms, Vuforia, `SpatialGraphNode.TryLocate` | Moves gaze into the world. |
| `GazePublisherWorker` | Gaze timing and sending | Uses the capture time already in each sample. |
| HoloLens tracker lock | Tracker sessions and both queues | Old replies and samples never leak into a new tracker session. |

## Names that must not change

C++: keep the same header paths, names, parameters, and return types for
`Config`, command-line parsing, `ViconLSLBridge`, `BridgeState`, `BridgeStatus`,
`ViconClient` and its read types, `MarkerStream`, `SegmentStream`,
`StreamOutlet`, `StreamOutletFactory`, `StreamPushResult`, `StreamSchema`, frame
mapping and error types, the preview code under `src/preview`, and Qt classes,
signals, and slots under `src/gui`. Private code can move into smaller files.

Unity saves type and field names in scenes and assets. Keep these unless the
change also updates the scenes:

- `GazeDataProvider`, `GazeLSLOutlet`, `VuforiaModelTargetPoseOutlet`, and
  `GazeLSLConfig`, and the public fields on `GazeLSLConfig`.
- The saved `config`, `gazeProvider`, and `modelTarget` fields.
- `GazeSample` field names and their order.
- What the tests use: `IGazeSampleProvider`, `IGazeSampleOutlet`,
  `GazePublisherWorker`, `GazeCoordinateTransform`, and `ModelTargetPoseEncoder`.

These count as public too, even though they aren't code: command-line options and
exit codes, LSL stream names, layouts, details, and timing, LabRecorder command
order, saved setting names, how CSV and XDF files are read, CMake options and
targets, and release file names and contents.

## Tidying up safely

1. Write down what the code does now before changing it.
2. Only delete code once searches and tests show nothing uses it.
3. Keep the old public header while moving private code out of it.
4. Pull math and decisions out of threaded code before changing how threads work.
5. Keep dependency updates, stream changes, coordinate changes, and new features
   out of a tidy-up.
6. Keep live and recorded time correction separate. They follow different rules.
7. Don't touch submodules.

## What the tests don't cover

The automated tests cover most of the logic, the recorder protocol, the desktop
flows, packaging, and the HoloLens code without Unity. They don't cover:

- A saved example of the full LSL description of every stream.
- One example run through both the live and XDF preview.
- Unity, Vuforia, Vicon, the HoloLens, real screens, remote desktop, or virtual
  machines. Use the [hardware test guide](device-parity-runbook.md) for those.
