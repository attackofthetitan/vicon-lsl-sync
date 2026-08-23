# Architecture and ownership boundaries

## Purpose

This document records the current component boundaries and dependency direction so that structural refactors can be reviewed independently from behavior changes. It describes the code at the current repository revision; it is not a proposal to change protocols, schemas, threading, or dependencies.

The governing compatibility rule is:

> Refactors may move implementation details, but they must preserve existing public source interfaces, executable behavior, stream contracts, settings, build targets, and vendor boundaries unless a separately reviewed migration explicitly changes one of them.

The detailed observable behavior is recorded in [behavior-contract.md](behavior-contract.md). Runtime transitions are recorded in [runtime-state-machines.md](runtime-state-machines.md), and clock/coordinate assumptions are recorded in [time-and-coordinate-semantics.md](time-and-coordinate-semantics.md).

## Repository ownership map

### First-party code

| Area | Owner and responsibility | May depend on |
| --- | --- | --- |
| `vicon-lsl-bridge/src` | Desktop bridge domain and runtime: configuration, command-line parsing, Vicon SDK adapter, frame mapping, stream schemas, LSL outlets, reconnect/layout orchestration | C++17; runtime files may use the Vicon SDK and liblsl |
| `vicon-lsl-bridge/src/preview` | Dependency-light preview domain: DTOs, parsing, math, calibration, playback timing, OBJ/CSV/XDF readers, rate tracking | C++17 standard library only |
| `vicon-lsl-bridge/src/gui` | Qt presentation and adapters: window/panel state, live LSL preview, LabRecorder remote control, process ownership, settings, rendering | Bridge runtime, preview domain, liblsl, Qt6 |
| `hololens-gaze-lsl/Assets/Scripts` | HoloLens/Unity producers: Extended Eye Tracking acquisition, world-space transformation, gaze publishing, Vuforia target-pose publishing | Unity, Microsoft Extended Eye Tracking, Mixed Reality OpenXR, Vuforia, managed liblsl binding |
| `stream-contracts` | Language-neutral stream schema inputs | JSON |
| `tools/generate_stream_contracts.py` | Generates the C++ and C# gaze and model-target stream contracts from their JSON inputs | Python standard library |
| `vicon-lsl-bridge/packaging/windows` | Windows deployment, license collection, and portable launcher/package implementation | PowerShell, Windows runtime tools |
| `.github/workflows/build-bridge.yml`, `.github/scripts` | Cross-platform build, validation, packaging, and release orchestration | Hosted build environments and pinned actions |

### Vendor boundaries

The following paths are Git submodules declared by `.gitmodules` and are not first-party refactor scope:

- `labrecorder` — upstream LabRecorder. The desktop GUI controls it through its remote-control TCP protocol and packaging includes its executables.
- `vicon-lsl-bridge/external/vicon-datastream-sdk` — Vicon DataStream SDK wrapper/source.
- `hololens-gaze-lsl/external/liblsl` — the UWP ARM64 liblsl fork used by the Unity application.

A behavior-preserving cleanup must not edit these submodules. Updating a submodule revision, carrying a downstream patch, changing the LabRecorder protocol, or reconciling liblsl revisions is a separate dependency migration with its own compatibility review.

The bridge can either use an installed liblsl package or fetch the pinned revision in `vicon-lsl-bridge/CMakeLists.txt`. Changing that selection policy or revision is also a dependency migration.

## Build-time component graph

The CMake targets form an intentional dependency gradient:

```text
vicon-lsl-bridge-logic
  command line + schemas + frame mapper + pure preview domain
                 |
                 v
vicon-lsl-bridge-runtime
  Vicon SDK adapter + LSL outlets + bridge orchestration
          |                         |
          v                         v
vicon-lsl-bridge             vicon-lsl-bridge-gui
  CLI executable               Qt/LSL desktop application
```

Important build contracts:

- `vicon-lsl-bridge-logic` must continue to configure and build without the Vicon SDK, liblsl, Qt, or a downloaded test framework.
- `vicon-lsl-bridge-runtime` keeps the SDK/liblsl dependencies out of the logic target.
- `vicon-lsl-bridge` and `vicon-lsl-bridge-gui` are distinct executable entry points.
- Existing CMake target names, cache option names, test names, and packaged executable names are public build interfaces.
- Windows packaging depends on the current portable launcher, stair-model asset paths, LabRecorder directory layout, runtime DLL inventory, and license manifest names.

## Runtime component graph

### Desktop producer path

```text
Vicon DataStream server
        |
        v
ViconClient -- status-bearing reads --> ViconFrameMapper
        |                                  |
        | frame time                       | fixed-shape samples + diagnostics
        v                                  v
ViconLSLBridge -----------------> MarkerStream / SegmentStream
                                             |
                                             v
                                      LSL network outlets
```

Ownership is intentionally narrow:

- `ViconClient` translates SDK results into repository-owned read types. SDK-specific result values do not propagate past this adapter except as diagnostic text.
- `ViconFrameMapper` owns discovery order, validity rules, invalid-value encoding, diagnostics, and timestamp monotonicity helpers.
- `MarkerStream` and `SegmentStream` retain the public facades and domain-specific sample conversion. Their shared private numeric-outlet core owns `lsl::stream_info`, outlet lifetime, common stream metadata, shape validation, and push-failure handling.
- `ViconLSLBridge` owns the blocking connection/session loop, layout checks, stream recreation, retry timing, source-ID construction, status publication, and cleanup ordering.

### Desktop GUI and recording path

The Qt GUI owns three independent activities:

1. `BridgeWorker` runs one `ViconLSLBridge` on a worker thread and relays status to `BridgeWindow`.
2. `PreviewStreamWorker` consumes LSL streams on another worker thread and emits `PreviewFrame` values to `PreviewWidget` on the GUI thread.
3. `LabRecorderClient` and the optional owned `QProcess` stay on the GUI thread. They control, but do not implement, recording.

The preview is a consumer. It must not change producer timestamps or schemas. The built-in XDF reader is a visualization path, not the authoritative scientific import path; the README directs scientific analysis to the official XDF importers.

### HoloLens producer path

```text
EyeGazeTracker watcher/session
        |
        v
GazeDataProvider raw reading (publisher thread)
        |
        v
timestamped raw queue
        |
        v
SpatialGraphNode locate + world transform (Unity main thread)
        |
        v
transformed sample queue
        |
        v
GazePublisherWorker -> GazeLSLOutlet -> LSL network

Vuforia target transform (Unity LateUpdate)
        |
        v
VuforiaModelTargetPoseOutlet -> independent LSL stream
```

The gaze and target producers are independent outlets but share a coordinate-frame contract. HoloLens gaze is not relayed through the desktop bridge.

## Thread and object ownership

| Owner | Thread-sensitive resources | Invariant |
| --- | --- | --- |
| CLI process | `ViconLSLBridge`, signal stop request | `stop()` only changes the atomic run flag; the bridge loop owns connection/outlet cleanup |
| `BridgeWorker` | `ViconLSLBridge` run loop | GUI state is updated only through queued Qt signals |
| GUI thread | Widgets, `LabRecorderClient`, timers, optional LabRecorder `QProcess` | Socket, process, settings, and widgets remain on the GUI thread |
| `PreviewStreamWorker` | Four LSL inlets and latest-sample state | Inlet resolution/pulling stays on the worker; gaze-transform updates are mutex protected |
| Unity main thread | `SpatialGraphNode.TryLocate`, Unity transforms, Vuforia transform access | World-space conversion and scene objects are not moved to the publishing thread |
| `GazePublisherWorker` thread | Provider polling, cadence, sample encoding, outlet pushes | Uses the capture timestamp already attached to `GazeSample`; it does not replace it with retrieval time |
| HoloLens tracker gate | Tracker, node, watcher generations, both queues | Session-generation checks prevent samples or asynchronous callbacks from an old tracker lifecycle from entering a new one |

## Public compatibility surface

### C++ source API

`vicon-lsl-bridge-logic` and `vicon-lsl-bridge-runtime` publish `src` as an include directory. There is no installed development package, but the headers are still a source-level compatibility surface for repository consumers and tests.

Refactors must retain facade headers and existing names/signatures for:

- `Config`, command-line result/action types, and parsing/formatting functions.
- `ViconLSLBridge`, `BridgeState`, `BridgeStatus`, and its status callback.
- `ViconClient` and repository-owned read DTOs.
- `MarkerStream`, `SegmentStream`, `StreamOutlet`, `StreamOutletFactory`, and `StreamPushResult`.
- `StreamSchema`, sample aliases, mapper/discovery/diagnostic types and functions.
- Preview DTOs and functions declared under `src/preview`.
- Qt classes and signals/slots declared under `src/gui`.

Moving definitions to focused private headers is allowed if the current umbrella headers continue to compile unchanged for consumers.

### Unity source and serialized API

The following are compatibility-sensitive because Unity scenes, prefabs, or configuration assets can refer to them by type or serialized field name:

- `GazeDataProvider`, `GazeLSLOutlet`, `VuforiaModelTargetPoseOutlet`, and `GazeLSLConfig`.
- Serialized `config`, `gazeProvider`, and `modelTarget` fields.
- Public fields on `GazeLSLConfig`.
- `GazeSample` field names and channel mapping.
- `IGazeSampleProvider`, `IGazeSampleOutlet`, `GazePublisherWorker`, `GazeCoordinateTransform`, and `ModelTargetPoseEncoder` signatures used by platform-neutral tests or integrations.

Renaming or changing these requires a Unity migration plan and serialized-asset verification.

### Protocol and persistence API

The following are public even though they are not language-level APIs:

- CLI flags, defaults, messages used operationally, and exit codes.
- LSL stream identity, schemas, metadata, timestamp/coordinate semantics, and outlet recreation behavior.
- LabRecorder RCS command order and acknowledgement handling.
- QSettings organization/application names and keys.
- Merged CSV and XDF preview interpretation.
- CMake options/targets and release artifact inventory.

## Refactor sequencing rules

1. Capture contracts and golden outputs before moving implementation.
2. Delete only code proven unreferenced in first-party production and test paths. A test seam is not dead code merely because production does not call it directly.
3. Keep compatibility facades while splitting oversized modules.
4. Centralize schema/metadata definitions in a separate pass before deduplicating outlet implementations.
5. Extract pure state and transformation logic before changing threaded orchestration.
6. Do not combine dependency revisions, framework migrations, schema changes, coordinate changes, or new features with structural cleanup.
7. Do not collapse live and offline synchronization into one policy; their clock-correction paths are intentionally different.
8. Keep submodules untouched during first-party refactors.

## Current validation evidence

The repository currently provides:

- Cross-platform dependency-light C++ tests for command-line behavior, stream schemas, Vicon mapping/timestamps/diagnostics, preview parsing/math/calibration, CSV/XDF loading, playback, and rate tracking.
- Runtime tests with injected stream outlets for outlet failure/recreation, empty layouts, nominal rates, and timestamp propagation.
- Qt tests for LabRecorder command sequencing, fragmented replies, timeouts, disconnects, filename policy, runtime policy, and exact window settings defaults/keys.
- A packaged GUI integration entry point covering layout/tooltips, local LSL resolution, optional bundled LabRecorder startup, and stair assets.
- Platform-neutral managed tests for channel/pose encoding, coordinate transformation, system-relative timing, backlog policy, publisher cadence, cancellation, and recovery.
- Generated-contract freshness checking.
- Cross-platform build and Windows package inventory checks.

Known gaps that must be filled before deep orchestration refactors:

- No exact normalized `stream_info` XML snapshot covers every producer.
- No fixture proves live-preview and XDF-preview parity from the same synthetic source samples while preserving their different clock policies.
- No automated Unity/WinRT/Vuforia device validation exists; use [device-parity-runbook.md](device-parity-runbook.md).
- The bridge lifecycle fixture covers initial-frame, outlet-failure, and stop paths; periodic layout-change and discovery-error sequences remain covered at the mapper/outlet layers rather than by one end-to-end fake-client scenario.

## Evidence sources

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
