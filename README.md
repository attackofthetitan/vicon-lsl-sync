# Vicon LSL Bridge

Use this project to send Vicon motion data over [Lab Streaming Layer (LSL)](https://labstreaminglayer.org). LSL keeps data from different devices on a shared time base. [LabRecorder](https://github.com/labstreaminglayer/App-LabRecorder) can then save those streams in one `.xdf` file.

The project also includes Unity scripts that send HoloLens 2 eye-gaze data directly to LSL.

## Get started

1. Download the latest package from the [Releases page](https://github.com/attackofthetitan/vicon-lsl-sync/releases/latest).
2. Start your Vicon DataStream server.
3. Run `vicon-lsl-bridge-gui`.
4. Enter the Vicon server address and select **Start Streaming**.
5. Open the included LabRecorder app to record the streams.

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

- Start and stop the Vicon bridge.
- View live marker, segment, gaze, and stair-target data.
- Open merged CSV or XDF recordings for a visual check.
- Align HoloLens gaze with the Vicon world.
- Start, control, and stop LabRecorder.

The preview reads the same LSL streams that LabRecorder records. By default, it looks for `ViconMarkers`, `ViconSegments`, `HoloLensGaze`, and `HoloLensModelTargetPose`.

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

The result lasts only for the current preview session. Select **Use Manual Transform** to return to the saved translation and rotation controls.

Restarting the HoloLens app can create a new Unity world, so run the alignment again after a restart. If the physical stairs move, update the fixed Vicon stair pose before relying on the result.

The gaze publisher keeps the original device capture time. It drops duplicate, old, invalid, or out-of-order readings. If processing falls more than 25 ms behind, it drops the older queued readings and keeps the newest one. This leaves a visible time gap instead of replaying old gaze data. See [How time and coordinates work](docs/time-and-coordinate-semantics.md) for the exact rules.

## Record with LabRecorder

The desktop app talks to LabRecorder through its remote-control connection. The default address is `localhost:22345`.

1. Start the Vicon bridge.
2. In **Recording**, choose the study folder and filename pattern.
3. Fill in the participant, session, task, run, acquisition, and modality fields.
4. Start the included LabRecorder, or connect to one that already has remote control enabled.
5. Select **Refresh Streams** and check that the expected streams appear.
6. Select **Start Recording**.
7. Select **Stop Recording** when finished.

Before each start, the app asks LabRecorder to refresh its stream list and select every visible stream. This includes streams that started after LabRecorder opened.

Check these items before recording:

- The bridge is streaming.
- LabRecorder remote control is enabled and connected.
- `ViconMarkers` is visible when you expect marker data.
- `ViconSegments` is visible when you expect segment data.
- `HoloLensGaze` is visible when the Unity app is running.
- The filename preview points to the intended `.xdf` file.

An invalid folder, empty field, or unresolved filename token blocks recording until you fix it. A LabRecorder error does not stop the Vicon bridge.

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
- Qt 6 Core, Widgets, Network, and OpenGLWidgets if you want the desktop app.

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
