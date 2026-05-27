# Vicon LSL Bridge

Stream Vicon motion capture data into [Lab Streaming Layer (LSL)](https://labstreaminglayer.org) for time-synchronized recording alongside other data sources.

## Quick Start

1. Download the latest release for your platform from the [Releases](../../releases) page
2. Run **vicon-lsl-bridge-gui** — enter your Vicon server address and click **Start Streaming**
3. Open **LabRecorder** (also included in releases) to record all LSL streams into a single `.xdf` file

## Overview

The bridge connects to a Vicon DataStream server and creates two LSL outlets:

- **ViconMarkers** — 4 channels per marker (X, Y, Z in mm, Valid flag). Occluded markers are sent as NaN with Valid=0.
- **ViconSegments** — 7 channels per segment (X, Y, Z in mm, QX, QY, QZ, QW quaternion rotation).
- **HoloLensGaze** — optional embedded UDP receiver for the HoloLens gaze Unity app. The stream has 21 channels: combined, left-eye, and right-eye origin/direction plus valid flags.

If the marker/segment layout changes mid-session (e.g., subjects added or removed), streams are automatically destroyed and recreated.

## Download

Pre-built binaries are available on the [Releases](../../releases) page for:

- Linux x64
- Windows x64

Each release also includes [LabRecorder](https://github.com/labstreaminglayer/App-LabRecorder) builds for convenient recording.

## GUI Application

The GUI app (`vicon-lsl-bridge-gui`) provides a simple interface to configure and start streaming without using the command line. Enter the Vicon server address, optionally change stream names, and click Start.

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
- **HoloLensGaze** is visible only when the optional HoloLens gaze source or UDP relay is part of the session.

The recording controls send LabRecorder commands and show command/connection errors without affecting bridge streaming.

## CLI Usage

A command-line version is also included for headless or scripted use:

```
vicon-lsl-bridge [options]

Options:
  --server <ip:port>          Vicon server address (default: localhost:801)
  --marker-stream <name>      LSL marker stream name (default: ViconMarkers)
  --segment-stream <name>     LSL segment stream name (default: ViconSegments)
  --no-hololens-gaze          Disable embedded HoloLens gaze UDP-to-LSL receiver
  --gaze-port <port>          HoloLens gaze UDP port, 1-65535 (default: 16571)
  --gaze-stream <name>        HoloLens gaze LSL stream name (default: HoloLensGaze)
  --reconnect-interval <ms>   Reconnection interval in ms (default: 3000)
  --help                      Show this help message
```

### Example

```bash
./vicon-lsl-bridge --server 192.168.1.100:801
```

## Recording

Use [LabRecorder](https://github.com/labstreaminglayer/App-LabRecorder) (included in releases) to record all LSL streams on the network into a single `.xdf` file with synchronized timestamps.

The HoloLens Unity outlet uses native LSL when `liblsl.dll` loads successfully. If it cannot load LSL, or if `GazeLSLConfig.ForceUdpRelay` is enabled, it sends 21-channel `HLGAZE1` UDP packets to `vicon-lsl-bridge`; in that mode, run `vicon-lsl-bridge` and LabRecorder, and set `GazeLSLConfig.RelayHost`/`RelayPort` to the desktop running the bridge.

## Building from source

### Requirements

- CMake 3.23+
- C++17 compiler
- Boost (thread, chrono, and header-only components)
- liblsl (fetched automatically via CMake)
- Vicon DataStream SDK (included as a submodule)
- Qt6 (Core, Widgets) — optional, for the GUI app

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
