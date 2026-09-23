# Vicon LSL Bridge

This project sends Vicon motion data over [Lab Streaming Layer (LSL)](https://labstreaminglayer.org). LSL puts data from different devices on the same clock. [LabRecorder](https://github.com/labstreaminglayer/App-LabRecorder) can then save all of it in one `.xdf` file.

The project also has Unity scripts that send HoloLens 2 eye-gaze data straight to LSL.

## Get started

1. Download the latest package from the [Releases page](https://github.com/attackofthetitan/vicon-lsl-sync/releases/latest).
2. Start your Vicon DataStream server.
3. Run `vicon-lsl-bridge-gui`.
4. Choose the study folder and fill in the session details, then select **Start Session**.
5. Look over the setup check. Fix anything marked as required. If you want to
   record anyway, type a reason and choose **Record Anyway**.
6. Select **Stop Session** when you are done, then look over the file check.

The app looks for the Vicon server at `localhost:801` unless you change it.

### On a Mac

The Mac download needs Apple Silicon. Open the disk image and drag the app into
the **Applications** folder shown next to it. LabRecorder is already inside the
app. Open the app from **Applications** and it ejects the disk image for you. If
you open it from the disk image instead, it offers to move itself to
**Applications** first. The command-line tools are in a separate folder on the
disk image, so copy them before opening the app if you want them.

Apple has not checked (notarized) this app. The first time you open it, macOS
says it could not verify the app. Open **System Settings > Privacy & Security**
and choose **Open Anyway**. macOS then asks once before the app uses your local
network, which LSL needs. It also asks before the app saves recordings in
Documents, Desktop, or Downloads, or on an external or network drive. If you
said no, the path check tells you, and **Open Privacy Settings** takes you to
the setting that fixes it.

If an older version put a separate LabRecorder app in **Applications**, move it
to the Trash. If **Recorder program** in the app still shows that app's path,
clear the field.

## Streams

| Stream | What it holds |
| --- | --- |
| `ViconMarkers` | Four values per marker: X, Y, Z in millimetres, and a flag that says whether the marker was seen. A hidden or unreadable marker sends `NaN, NaN, NaN, 0`. |
| `ViconSegments` | Seven values per segment: X, Y, Z in millimetres, then four numbers for its rotation. An unreadable segment sends seven `NaN` values. |
| `HoloLensGaze` | Sent by the Unity app 90 times a second. Its 21 values describe where both eyes, the left eye, and the right eye are looking, each with a flag that says whether it is usable. |
| `HoloLensModelTargetPose` | Optional, sent by the Unity app. Its eight values give the position and rotation of the Vuforia stair target, and whether it is being tracked. |

You can rename the two Vicon streams. The HoloLens stream names come from the Unity settings.

If Vicon subjects, markers, or segments change during a session, the bridge closes the old Vicon streams and opens new ones that match.

## Use the desktop app

With the desktop app you can:

- Start and stop a whole session in one step, or control the bridge, preview,
  and recorder one at a time.
- Find LSL streams and pick each one by its source ID. If source IDs change
  between runs, you can choose to follow a stream by name instead.
- See the final file path and chosen streams before you start.
- Keep an eye on the bridge, recorder, preview, calibration, file path, free
  space, stream health, errors, and file check results.
- Open a merged CSV or XDF recording and play it back, jump around, step
  through it, or loop it.
- Line up HoloLens gaze with the Vicon space and keep saved calibrations.
- Connect to a recorder you started yourself, or let the app start its own copy,
  without the window freezing.

By default, the marker and segment preview use the bridge's stream names, and
gaze and calibration use `HoloLensGaze` and `HoloLensModelTargetPose`. The
**Streams** tab shows each stream's name, type, source ID, computer, session,
channel count, expected and measured rate, coordinate name, how old the last
sample is, and any warnings. When two streams share a name, the app does not
quietly pick one.

Presets save your recording setup. They do not save the window size, layout, the
open tab, or recent files. Settings from older versions of the app are not
brought over.

The built-in XDF reader is for a quick visual check. Use [pyxdf](https://github.com/xdf-modules/pyxdf) or [xdf-Matlab](https://github.com/xdf-modules/xdf-Matlab) for real analysis.

Old recordings marked `eye_tracker_space` store gaze relative to the eye tracker itself. The preview can show that gaze, but it cannot line it up with the stairs, because the file does not say where the headset was at each moment.

## Line up gaze using the stair target

Automatic alignment needs gaze and stair target positions from the same Unity world.

In Unity:

1. Use Microsoft Mixed Reality OpenXR 1.5.1 or later.
2. Add `GazeDataProvider` and `GazeLSLOutlet` to the scene.
3. Add `VuforiaModelTargetPoseOutlet` to the same scene.
4. Give the target component the stair `ModelTargetBehaviour`. Give the gaze and target components the same `GazeLSLConfig` asset.

In the desktop preview:

1. Keep the default **Stair target** stream name, or type the name set in `GazeLSLConfig`.
2. Start the preview.
3. Look at the real stairs with the HoloLens until Vuforia finds them.
4. Select **Calibrate from Stair Target**.
5. Keep the target still while the app collects 20 good samples.

You can then pause Vuforia with **M**. Keep the target outlet running and
include `HoloLensModelTargetPose` in every recording. While Vuforia is paused,
the HoloLens keeps sending the last steady stair position and marks it as frozen
instead of live. That way a recording started after calibration can still be
lined up when you open it later. After you turn Vuforia back on, it has to find
the stairs again. Playback warns you if a file has nothing it can use to line up
gaze.

A new alignment only lasts until you close the desktop app. You cannot type in
the HoloLens position and rotation by hand, because they are unknown until
measured. Until you run an alignment or apply a saved one, the preview draws gaze
as the HoloLens sent it. **Clear Calibration** takes you back to that.

To keep an alignment, select **Save Session Calibration**. A saved calibration
stores a setup name, the stair model and where it was measured, coordinate
names, the alignment itself, notes, when it was made, and how accurate it was.
You can apply, copy, hide, import, and export saved calibrations. If coordinate
details are missing, you must confirm before using one. Its accuracy and whether
it still fits the current streams stay on screen.

If a saved Vuforia calibration swaps left and right, run **Calibrate from Stair
Target** and **Save Session Calibration** again with this version. Saved
calibrations keep what they stored, but a new one uses the fixed math. XDF
playback that lines up gaze from recorded target positions also uses the fix.

The stairs never move. Their bottom front-left corner is 120.5 cm straight ahead
of and 21.3 cm to the left of the Vicon origin, on the floor. In metres, facing
up the stairs, that is `(-1.205, -0.213, 0)`. The built-in **Default stair
setup** already allows for the gap between that corner and the model's own
origin. If a saved calibration was made with the older, estimated position,
select **Default stair setup**, calibrate again, and save. Restarting the
HoloLens app can reset its world, so run the gaze alignment again after a
restart even though the stairs have not moved.

The Unity app keeps the time each gaze reading was actually taken. It skips
repeated, broken, or out-of-order readings, and picks up every reading the eye
tracker made since the last one it kept. If it falls more than 500 ms behind, it
throws away the older waiting readings and keeps the newest. That leaves a gap in
time instead of playing back old gaze late. See [How time and coordinates
work](docs/time-and-coordinate-semantics.md) for the details.

## Record with LabRecorder

The app talks to LabRecorder at `localhost:22345` unless you change it. Before
starting its own LabRecorder, the app checks that address so it does not start
a second one. A recorder the app started shows as **Started here**. One that was
already running shows as **External**, and the app never closes it.
**Disconnect / Detach** lets go of the recorder without closing it.

1. Start the Vicon bridge.
2. In **Recording**, choose the study folder and file name pattern.
3. Fill in the participant, session, task, run, acquisition, and modality.
4. In **Streams**, select **Find LSL Streams**. Check each stream and mark
   which ones to record and which ones must be there.
5. Choose how to record:

   - By default, the app uses its own copy of LabRecorder and saves only the
     streams you picked.
   - **Use external graphical recorder** uses the LabRecorder window you already
     have open. It refreshes the stream list right before starting and records
     every stream it can see.

6. Select **Check Setup** and read every required item, warning, and note.
7. Select **Start Recording**. Or use **Start Session** to have the app start the
   bridge and preview, find streams, check the setup, and start recording for
   you.
8. Select **Stop Recording** or **Stop Session** when you are done.

The app fills in the file name pattern itself before handing it to LabRecorder.
This keeps the exact letters, folders, and run number shown in the destination
preview. Because of how LabRecorder's remote control works, its template box
shows `%b` and its task box holds the finished file name. Change recording
details in this app, not in LabRecorder.

**Recording Destination** shows the full path the app checked, sent to the
recorder, and saved with the session details. The app adds `.xdf` if needed. It
blocks paths that leave the study folder, use names Windows does not allow,
cannot be written to, or point at a file that already exists unless you allowed
that. **Find Next Run** finds a run number that is not used yet. The app warns
you when free space drops below the level you set.

Before you record, check that:

- The bridge is streaming.
- The recorder is connected or ready and is not busy with another command.
- Every required stream is there, has recent data, has the right number of
  channels, and uses the expected coordinate names.
- `ViconMarkers`, `ViconSegments`, and `HoloLensGaze` show healthy rates when
  they are required.
- The file name preview points to the `.xdf` file you want.

If a folder, value, file name pattern, or choice is wrong, the app says what is
wrong and how to fix it. A recorder error does not stop the Vicon bridge.
Pressing Start or Stop again while one is already running does nothing. If you
close the app while Start is in progress, it either cancels Start before it is
sent or sends one last Stop.

After Stop, the app waits for the XDF file and checks it in the background. It
looks at the chosen streams, source IDs, channel counts, time ranges, rates,
gaps, and sample times. The run is marked **Checked**, **Checked with
warnings**, or **Needs attention**. This check never changes the file. Use
**Open Recording in Preview** to look at it, or export the session details.

## Look at a recording

Open a merged CSV or XDF file from **Preview**, drag it onto the window, or pick
a recent file. The file loads in the background with a progress bar and a
cancel button. What you had open stays on screen until the new file has fully
loaded.

CSV and XDF files have the same playback controls: play and pause, a timeline,
the current time and frame, single-frame steps, jumps to the start, end, or a
chosen time, speed, and a loop switch. Long recordings stay within a memory limit
you can set, so the preview may skip some frames. The file check still uses exact
numbers. If an XDF file has more than one stream that could be used, the app asks
which one to use as the main timeline and which streams to include. If a stream
restarted during a recording, the matching pieces are joined back together.

## Use the command line

The package also has a command-line app for scripts and for computers without a screen:

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

The HoloLens Unity app sends gaze straight to LSL. The command-line app does not pass gaze along.

## Build from source

You need:

- CMake 3.23 or later.
- A C++17 compiler.
- The Boost thread and chrono libraries, and the Boost headers.
- The Vicon DataStream SDK, which is linked into this repository as a Git submodule.
- Qt 6 Core, Widgets, and Network, if you want the desktop app.

If liblsl is not installed, CMake downloads it.

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

With Qt 6, you get both `vicon-lsl-bridge` and `vicon-lsl-bridge-gui`. Without Qt 6, you only get the command-line app.

## Run the tests

These C++ tests do not need the Vicon SDK, Qt, or any download:

```bash
cmake -S vicon-lsl-bridge -B build-logic \
  -DVICON_LSL_BRIDGE_BUILD_RUNTIME=OFF \
  -DVICON_LSL_BRIDGE_BUILD_GUI=OFF \
  -DVICON_LSL_BRIDGE_FETCH_CATCH2=OFF \
  -DBUILD_TESTING=ON
cmake --build build-logic --config Release --target vicon-lsl-bridge-logic-tests
ctest --test-dir build-logic --build-config Release --output-on-failure
```

These HoloLens tests do not need Unity or a headset:

```bash
dotnet run --project hololens-gaze-lsl/Tests/HoloLensCore.Tests.csproj --configuration Release
python tools/generate_stream_contracts.py --check
```

For the full desktop tests, download the Vicon SDK submodule and install liblsl and Qt 6. Build everything, then run CTest from that build folder.

Unity, Windows device features, Vuforia, and the real Vicon system still have to be tested by hand. Follow the [hardware test guide](docs/device-parity-runbook.md).

## More detail

- [How the code is organized](docs/architecture.md)
- [Behavior that must stay the same](docs/behavior-contract.md)
- [How each part starts, stops, and recovers](docs/runtime-state-machines.md)
- [How time and coordinates work](docs/time-and-coordinate-semantics.md)
- [Hardware test guide](docs/device-parity-runbook.md)
- [Release checklist](docs/release-checklist.md)
- [Change history](CHANGELOG.md)

## Make a release

Release tags look exactly like `vN.N.N`. The tagged commit must already be on `main`. The tag version must match the version in CMake and a dated entry in [CHANGELOG.md](CHANGELOG.md).

The release build makes the Windows ZIP, the Windows portable app, the Linux
archive, the Mac (Apple Silicon) disk image and archive, and `SHA256SUMS.txt`.
Follow the [release checklist](docs/release-checklist.md) before and after
publishing.
