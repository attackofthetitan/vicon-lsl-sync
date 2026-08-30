# Vicon LSL Bridge

Use this project to send Vicon motion data over [Lab Streaming Layer (LSL)](https://labstreaminglayer.org). LSL keeps data from different devices on a shared time base. [LabRecorder](https://github.com/labstreaminglayer/App-LabRecorder) can then save those streams in one `.xdf` file.

The project also includes Unity scripts that send HoloLens 2 eye-gaze data directly to LSL.

## Get started

1. Download the latest package from the [Releases page](https://github.com/attackofthetitan/vicon-lsl-sync/releases/latest).
2. Start your Vicon DataStream server.
3. Run `vicon-lsl-bridge-gui`.
4. Choose the study folder and session values, then select **Start Session**.
5. Review the setup check. Fix required failures, or enter a reason and choose
   **Record Anyway** when the exception is deliberate.
6. Select **Stop Session** when the run is complete and review the XDF file check.

The default Vicon server address is `localhost:801`.

## Streams

| Stream | What it contains |
| --- | --- |
| `ViconMarkers` | Four values for each marker: X, Y, Z in millimetres, and a valid flag. A hidden or unreadable marker uses `NaN, NaN, NaN, 0`. |
| `ViconSegments` | Seven values for each segment: X, Y, Z in millimetres, followed by the four parts of its rotation. An unreadable segment uses seven `NaN` values. |
| `HoloLensGaze` | A 90 Hz stream sent by the Unity app. Its 21 values describe combined, left-eye, and right-eye rays in the Unity world, with a valid flag for each ray. |
| `HoloLensModelTargetPose` | An optional stream sent by the Unity app. Its eight values describe the Vuforia stair target position, rotation, and tracking state. |

You can change the two Vicon stream names. The HoloLens names come from the Unity configuration.

If Vicon subjects, markers, or segments change during a session, the bridge closes the old Vicon streams and creates new ones with the new layout.

## Use the desktop app

The desktop app lets you:

- Start and stop a complete session or control the bridge, preview, and recorder
  independently.
- Find LSL streams and choose each source by its source ID. You can deliberately
  follow a stream name when source IDs change between runs.
- Check the final XDF path and selected streams before Start.
- Keep bridge, recorder, preview, calibration, path, storage, stream health,
  errors, and file-check results visible.
- Open, seek, step, loop, and visually inspect merged CSV or XDF recordings.
- Align HoloLens gaze with the Vicon world and manage saved calibrations.
- Connect to an external recorder or start an included recorder without freezing
  the window.

By default, the marker and segment preview use the bridge output names, while
gaze and calibration use `HoloLensGaze` and
`HoloLensModelTargetPose`. The **Streams** tab shows name, type, source ID, host,
session, channels, expected and measured rates, coordinate name, sample age, and
warnings. When names are duplicated, the app does not silently choose one.

Session presets contain the recording setup. Import, Export, Reset, Save Preset,
and Load Preset do not include window size, splitter position, tab selection, or
recent paths. The saved file format starts at version 1; settings from older
application releases are not imported.

The built-in XDF reader is for visual checks. Use [pyxdf](https://github.com/xdf-modules/pyxdf) or [xdf-Matlab](https://github.com/xdf-modules/xdf-Matlab) for scientific analysis.

Old recordings marked `eye_tracker_space` contain gaze in the eye tracker's local space. The preview can show those rays, but it cannot align them from the stair target because the recording does not contain the changing tracker-to-world pose.

## Align gaze from the stair target

Automatic alignment needs gaze and target poses from the same Unity/OpenXR world.

In Unity:

1. Use Microsoft Mixed Reality OpenXR 1.5.1 or later.
2. Add `GazeDataProvider` and `GazeLSLOutlet` to the scene.
3. Add `VuforiaModelTargetPoseOutlet` to the same Unity/XR world.
4. Assign the stair `ModelTargetBehaviour` and the same `GazeLSLConfig` asset to the gaze and target components.

In the desktop preview:

1. Keep the default **Stair target** stream name, or enter the name from `GazeLSLConfig`.
2. Start the preview.
3. Point the HoloLens at the physical stair target until Vuforia tracks it.
4. Select **Calibrate from Stair Target**.
5. Hold the target still while the app collects 20 good samples.

The result lasts only for the current desktop session. Select **Use Manual Transform** to return to the saved translation and rotation controls.

An automatic solution stays session-only until you select **Save Session
Calibration**. A saved calibration records a setup name, stair model and
measured pose, coordinate names, transform, notes, creation time, and position
and angle error. Saved calibrations can be applied, copied, hidden, imported,
and exported. Confirm missing coordinate details deliberately; quality and
compatibility remain visible as stream status changes.

Restarting the HoloLens app can create a new Unity world, so run the alignment again after a restart. If the physical stairs move, update the fixed Vicon stair pose before relying on the result.

The Unity app keeps the original device capture time. It drops duplicate, old,
invalid, or out-of-order readings. If processing falls more than 25 ms behind,
it drops the older queued readings and keeps the newest one. This leaves a
visible time gap instead of replaying old gaze data. See [How time and
coordinates work](docs/time-and-coordinate-semantics.md) for the exact rules.

## Record with LabRecorder

The default recorder remote-control address is `localhost:22345`. Before
automatic launch, the desktop app checks that address so it does not start a
duplicate recorder. A recorder started from the desktop app is shown as
**Started here**; one that was already running is shown as **External** and is
never closed by the desktop app. **Disconnect / Detach** disconnects without
closing the recorder.

1. Start the Vicon bridge.
2. In **Recording**, choose the study folder and filename pattern.
3. Fill in the participant, session, task, run, acquisition, and modality fields.
4. In **Streams**, select **Find LSL Streams**, review each source and its status,
   and mark the streams that should be recorded or are required.
5. Choose how to record:

   - The default uses the included recorder and saves only the selected streams.
   - **Use external graphical recorder** uses its remote-control connection and
     deliberately records everything visible after an immediate refresh.

6. Select **Check Setup** and review every required, warning, and information
   item.
7. Select **Start Recording**, or use **Start Session** to run the guided bridge,
   preview, stream search, setup check, and recording steps.
8. Select **Stop Recording** or **Stop Session** when finished.

**Recording Destination** shows the final full path checked by the app, sent to
the recorder, and saved in the session details. The
app appends `.xdf` when needed and blocks paths that escape the study folder,
reserved Windows names, unwritable paths, existing files that were not approved,
and destinations outside the study root. **Find Next Run** finds an unused run
number. Low storage is a visible warning at the chosen threshold.

Check these items before recording:

- The bridge is streaming.
- The selected recorder is connected or available and is not busy with another
  command.
- Required streams are present, recent, have the expected channel layout, and
  use the expected coordinate names.
- `ViconMarkers`, `ViconSegments`, and `HoloLensGaze` show healthy measured
  rates when those roles are required.
- The filename preview points to the intended `.xdf` file.

An invalid folder, value, filename pattern, or selection explains the problem
and how to fix it. A recorder error does not stop the Vicon bridge. Repeated
Start or Stop requests are refused, and closing while Start is in progress
either cancels it before it is sent or sends one final Stop.

After a successful Stop, the app waits for the XDF file and checks the selected
streams, source IDs, channel layouts, time ranges, rates, gaps, and sample times
in the background. The run becomes **Checked**, **Checked with warnings**, or
**Needs attention**. This check never rewrites the recording; use **Open
Recording in Preview** for a visual review or export the session details.

## Review a recording

Open a merged CSV or XDF from **Preview**, drag a file onto the window, or choose
a recent file. Reading the file, finding stream details, correcting time, calibration,
and frame preparation run in the background with progress and cancellation. The
previous usable source remains visible unless the new file finishes successfully.

CSV and XDF share play/pause, a timeline, current time and frame position,
single-frame steps, start/end and selected-time jumps, speed, and an explicit
loop option. Long recordings use a configurable memory limit and draw fewer
frames when needed; reported file timing remains exact. XDF files with several
possible streams ask the user to choose the main time source and the streams to
use.
Compatible pieces from a source that restarted are joined in a predictable way.

## Use the command line

The package also includes a command-line app for scripts and computers without a desktop display:

```text
vicon-lsl-bridge [options]

Options:
  --server <ip:port>          Vicon server address (default: localhost:801)
  --marker-stream <name>      LSL marker stream name (default: ViconMarkers)
  --segment-stream <name>     LSL segment stream name (default: ViconSegments)
  --reconnect-interval <ms>   Reconnection interval in ms (default: 3000)
  --help                      Show this help message
```

Example:

```bash
./vicon-lsl-bridge --server 192.168.1.100:801
```

The HoloLens Unity app sends gaze directly to LSL. The desktop command does not relay gaze.

## Build from source

You need:

- CMake 3.23 or later.
- A C++17 compiler.
- Boost thread and chrono libraries, plus the Boost headers.
- The Vicon DataStream SDK linked repository (Git submodule).
- Qt 6 Core, Widgets, and Network if you want the desktop app.

CMake downloads liblsl when an installed copy is not available.

### Linux

```bash
sudo apt-get install libboost-all-dev qt6-base-dev
cd vicon-lsl-bridge
cmake -B build
cmake --build build --config Release
```

### Windows

```bat
vcpkg install boost-thread:x64-windows-static-md boost-chrono:x64-windows-static-md boost-asio:x64-windows-static-md boost-filesystem:x64-windows-static-md boost-format:x64-windows-static-md boost-algorithm:x64-windows-static-md boost-date-time:x64-windows-static-md boost-math:x64-windows-static-md boost-range:x64-windows-static-md boost-lexical-cast:x64-windows-static-md

cd vicon-lsl-bridge
cmake -B build -A x64 "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_INSTALLATION_ROOT%/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --config Release
```

With Qt 6, the build creates both `vicon-lsl-bridge` and `vicon-lsl-bridge-gui`. Without Qt 6, it creates only the command-line app.

## Run the checks

Run the C++ checks without the Vicon SDK, Qt, or a downloaded test library:

```bash
cmake -S vicon-lsl-bridge -B build-logic \
  -DVICON_LSL_BRIDGE_BUILD_RUNTIME=OFF \
  -DVICON_LSL_BRIDGE_BUILD_GUI=OFF \
  -DVICON_LSL_BRIDGE_FETCH_CATCH2=OFF \
  -DBUILD_TESTING=ON
cmake --build build-logic --config Release --target vicon-lsl-bridge-logic-tests
ctest --test-dir build-logic --build-config Release --output-on-failure
```

Run the HoloLens checks that do not need Unity or a device:

```bash
dotnet run --project hololens-gaze-lsl/Tests/HoloLensCore.Tests.csproj --configuration Release
python tools/generate_stream_contracts.py --check
```

For the full desktop checks, download the linked Vicon SDK repository and provide liblsl and Qt 6. Build every target, then run CTest from that build folder.

Unity, Windows device APIs, Vuforia, and physical Vicon behavior still need real hardware checks. Follow the [hardware test guide](docs/device-parity-runbook.md).

## More detail

- [How the code is organized](docs/architecture.md)
- [Behavior that must stay the same](docs/behavior-contract.md)
- [How services start, stop, and recover](docs/runtime-state-machines.md)
- [How time and coordinates work](docs/time-and-coordinate-semantics.md)
- [Hardware test guide](docs/device-parity-runbook.md)
- [Release checklist](docs/release-checklist.md)
- [Change history](CHANGELOG.md)

## Make a release

Release tags use the exact form `vN.N.N`. The tagged commit must already be in `main`. The tag version must match the CMake project version and a dated entry in [CHANGELOG.md](CHANGELOG.md).

The release workflow builds the Windows ZIP, Windows portable app, Linux archive, and `SHA256SUMS.txt`. Follow the [release checklist](docs/release-checklist.md) before and after publishing.
