# Vicon LSL Bridge

Stream Vicon motion capture data into [Lab Streaming Layer (LSL)](https://labstreaminglayer.org) for time-synchronized recording alongside other data sources.

## Overview

The bridge connects to a Vicon DataStream server and creates two LSL outlets:

- **ViconMarkers** — 4 channels per marker (X, Y, Z in mm, Valid flag). Occluded markers are sent as NaN with Valid=0.
- **ViconSegments** — 7 channels per segment (X, Y, Z in mm, QX, QY, QZ, QW quaternion rotation).

If the marker/segment layout changes mid-session (e.g., subjects added or removed), streams are automatically destroyed and recreated.

## Download

Pre-built binaries are available on the [Releases](../../releases) page for:

- Linux x64
- Windows x64
- Windows x86

Each release also includes [LabRecorder](https://github.com/labstreaminglayer/App-LabRecorder) binaries for convenient recording.

## Usage

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

The bridge will connect to the Vicon server, enumerate all subjects/markers/segments, and begin streaming. Press Ctrl+C to stop.

## Recording

Use [LabRecorder](https://github.com/labstreaminglayer/App-LabRecorder) (included in releases) to record all LSL streams on the network into a single `.xdf` file with synchronized timestamps.

## Building from source

### Requirements

- CMake 3.23+
- C++17 compiler
- Boost (thread, chrono, and header-only components)
- liblsl (fetched automatically via CMake)
- Vicon DataStream SDK (included as a submodule)

### Linux

```bash
sudo apt-get install libboost-all-dev
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

The built executable will be at `build/Release/vicon-lsl-bridge.exe` (Windows) or `build/vicon-lsl-bridge` (Linux).
