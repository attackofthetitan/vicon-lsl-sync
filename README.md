# Vicon LSL Bridge

Stream Vicon motion capture data into [Lab Streaming Layer (LSL)](https://labstreaminglayer.org) for time-synchronized recording alongside other data sources.

## Quick Start

1. Download the latest release for your platform from the [Releases](../../releases) page
2. Run **vicon-lsl-bridge-gui** — enter your Vicon server address and click **Start Streaming**
3. Open **LabRecorder** (also included in releases) to record all LSL streams into a single `.xdf` file

## Overview

The bridge connects to a Vicon DataStream server and creates marker/segment LSL outlets. HoloLens gaze is published by the Unity app as its own native LSL stream:

- **ViconMarkers** — 4 channels per marker (X, Y, Z in mm, Valid flag). Occluded markers are sent as NaN with Valid=0.
- **ViconSegments** — 7 channels per segment (X, Y, Z in mm, QX, QY, QZ, QW quaternion rotation).
- **HoloLensGaze** — native LSL stream from the HoloLens Unity app. The stream has 21 channels: combined, left-eye, and right-eye origin/direction plus valid flags.
- **HoloLensModelTargetPose** — optional native LSL stream from the Vuforia stair model target. It contains the target position, quaternion, and tracked flag in the same HoloLens world frame as gaze.

If the marker/segment layout changes mid-session (e.g., subjects added or removed), streams are automatically destroyed and recreated.

## Download

Pre-built binaries are available on the [Releases](../../releases) page for:

- Linux x64
- Windows x64

Each release also includes [LabRecorder](https://github.com/labstreaminglayer/App-LabRecorder) builds for convenient recording.
There is also a seperate GUI app for windows.

## GUI Application

The GUI app (`vicon-lsl-bridge-gui`) provides a simple interface to configure and start streaming without using the command line. Enter the Vicon server address, optionally change stream names, and click Start.

The GUI also includes an embedded native OpenGL preview. The preview subscribes to the same LSL streams that LabRecorder records (`ViconMarkers`, `ViconSegments`, and `HoloLensGaze` by default), combines them into one 3D scene, and applies saved per-stream transforms so Vicon, HoloLens gaze, and the stair model can share one coordinate frame.

The built-in XDF loader is intended for visual preview. It preserves absolute stream time and applies the recorded clock-offset history once; use the official [pyxdf](https://github.com/xdf-modules/pyxdf) or [xdf-Matlab](https://github.com/xdf-modules/xdf-Matlab) importer for scientific offline analysis.

### Automatic stair-target alignment

Attach `VuforiaModelTargetPoseOutlet` to the same Unity/XR scene as `GazeDataProvider` and assign the existing Vuforia stair `ModelTargetBehaviour` plus the `GazeLSLConfig` asset. The component publishes `HoloLensModelTargetPose` without modifying the raw gaze stream.

In the desktop preview, leave the default **Stair target** stream name or enter the configured name, start the preview, acquire the physical stair model target in Vuforia, then select **Calibrate from Stair Target**. The preview averages 20 tracked samples and applies the resulting HoloLens-to-Vicon rigid transform for the current preview session only; automatic alignment is not saved. **Use Manual Transform** returns to the persistent translation/Euler controls.

The Vicon-side stair pose is currently the best fixed estimate used by the preview. Repeat calibration after restarting the HoloLens world frame; if the physical stairs are relocated, update that fixed estimate until an editable stair-pose workflow is added.

The GUI can also prepare and control a LabRecorder session over LabRecorder's remote-control socket:

1. Start streaming from the bridge.
2. In **Recording**, set the study root, filename template, participant/session/task/run fields, and LabRecorder RCS host/port. The default RCS host is `localhost` and the default port is `22345`.
3. Launch LabRecorder from the GUI, or connect to an already-running LabRecorder with RCS enabled. LabRecorder must have RCS enabled and the GUI must be connected before recording controls become active.
4. Use **Refresh Streams**, **Start Recording**, and **Stop Recording** from the bridge GUI.

The filename preview shows the `.xdf` path that will be sent to LabRecorder after applying the template and operator fields. Validation is intended to catch empty or unsafe path components before recording starts, so operators can correct participant/session/task/run values instead of discovering the problem after a failed start.

By default, the GUI selects all visible LabRecorder streams immediately before starting a recording. This helps include newly discovered bridge streams without a separate manual selection step. Disable **Select all streams before start** when LabRecorder stream selection should be managed manually, for example during partial-stream tests or when another device should stay visible but unrecorded.

Before recording, confirm the operator preflight:

- Bridge streaming is running and LabRecorder RCS is enabled, connected, and listening on the configured host/port.
- **ViconMarkers** is visible in LabRecorder when marker data is expected.
- **ViconSegments** is visible in LabRecorder when segment data is expected.
- **HoloLensGaze** is visible when the HoloLens Unity app is publishing native LSL gaze.

The recording controls send LabRecorder commands and show command/connection errors without affecting bridge streaming.

## CLI Usage

A command-line version is also included for headless or scripted use:

```
vicon-lsl-bridge [options]

Options:
  --server <ip:port>          Vicon server address (default: localhost:801)
  --marker-stream <name>      LSL marker stream name (default: ViconMarkers)
  --segment-stream <name>     LSL segment stream name (default: ViconSegments)
  --reconnect-interval <ms>   Reconnection interval in ms (default: 3000)
  --help                      Show this help message
```

### Example

```bash
./vicon-lsl-bridge --server 192.168.1.100:801
```

## Recording

Use [LabRecorder](https://github.com/labstreaminglayer/App-LabRecorder) (included in releases) to record all LSL streams on the network into a single `.xdf` file. LabRecorder stores the producers' timestamps together with clock-offset chunks for offline synchronization; it does not apply the live preview's inlet post-processing. The bridge records Vicon streams; the HoloLens Unity app publishes gaze directly as native LSL.

## Building from source

### Requirements

- CMake 3.23+
- C++17 compiler
- Boost (thread, chrono, and header-only components)
- liblsl (fetched automatically via CMake)
- Vicon DataStream SDK (included as a submodule)
- Qt6 (Core, Widgets, Network, OpenGLWidgets) — optional, for the GUI app

### Linux

```bash
sudo apt-get install libboost-all-dev qt6-base-dev
cd vicon-lsl-bridge
cmake -B build
cmake --build build --config Release
```

### Windows

```bash
vcpkg install boost-thread:x64-windows-static-md boost-chrono:x64-windows-static-md boost-asio:x64-windows-static-md boost-filesystem:x64-windows-static-md boost-format:x64-windows-static-md boost-algorithm:x64-windows-static-md boost-date-time:x64-windows-static-md boost-math:x64-windows-static-md boost-range:x64-windows-static-md boost-lexical-cast:x64-windows-static-md

cd vicon-lsl-bridge
cmake -B build -A x64 "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_INSTALLATION_ROOT%/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --config Release
```

If Qt6 is installed, both `vicon-lsl-bridge` (CLI) and `vicon-lsl-bridge-gui` (GUI) will be built. Without Qt6, only the CLI is built.

## Testing

Run the dependency-light C++ suite without downloading Catch2:

```bash
cmake -S vicon-lsl-bridge -B build-logic \
  -DVICON_LSL_BRIDGE_BUILD_RUNTIME=OFF \
  -DVICON_LSL_BRIDGE_BUILD_GUI=OFF \
  -DVICON_LSL_BRIDGE_FETCH_CATCH2=OFF \
  -DBUILD_TESTING=ON
cmake --build build-logic --config Release --target vicon-lsl-bridge-logic-tests
ctest --test-dir build-logic --build-config Release --output-on-failure
```

For the complete bridge suite, configure with the Vicon SDK submodule, liblsl, and Qt6 available, build all targets, then run CTest from that build directory. Both Catch2 and the bundled dependency-light harness are exercised in CI.

The platform-neutral HoloLens publisher, tracker-lifetime, and pose-encoding checks run with:

```bash
dotnet run --project hololens-gaze-lsl/Tests/HoloLensCore.Tests.csproj --configuration Release
python tools/generate_stream_contracts.py --check
```

Device-specific WinRT, Unity, Vuforia, and spatial-frame behavior must also be validated in the real Unity project and on HoloLens hardware. The pinned LabRecorder submodule currently builds its XDF writer test executable without registering it with CTest; that upstream limitation is intentionally not patched in this repository.
