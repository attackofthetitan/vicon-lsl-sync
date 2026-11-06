# Vicon LSL Bridge

This project sends Vicon motion data over [Lab Streaming Layer
(LSL)](https://labstreaminglayer.org). LSL puts data from different devices on
the same clock, and [LabRecorder](https://github.com/labstreaminglayer/App-LabRecorder)
saves it all into one `.xdf` file.

It also has Unity scripts that send HoloLens 2 eye gaze straight to LSL.

## Get started

1. Download the latest package from the [Releases
   page](https://github.com/attackofthetitan/vicon-lsl-sync/releases/latest).
2. Start your Vicon DataStream server.
3. Run `vicon-lsl-bridge-gui`. Allow the network and folder access it asks for
   when it starts.
4. Pick the study folder, fill in the session details, and select **Start
   Session**.
5. Look over the setup check and fix anything marked required. To record anyway,
   type a reason and select **Record Anyway**.
6. Select **Stop Session** when you're done, then look over the file check.

The app looks for Vicon at `localhost:801` unless you change it.

### On a Mac

The Mac download needs Apple Silicon. Open the disk image and drag the app into
**Applications**. LabRecorder is already inside the app. If you want the
command-line tools, copy them from their own folder on the disk image. Then open
the app from **Applications**, and it ejects the disk image for you. If you open
it from the disk image instead, it offers to move itself first.

Apple hasn't checked this app, so the first time macOS says it can't verify it.
Go to **System Settings > Privacy & Security** and choose **Open Anyway**. macOS
then asks once before the app uses your local network, which LSL needs, and
before it saves into Documents, Desktop, Downloads, or an external or network
drive. If you said no, the path check tells you, and **Open Privacy Settings**
takes you to the right setting.

If an older version put a separate LabRecorder app in **Applications**, move it
to the Trash, and clear **Recorder program** in the app if it still points there.

### On Linux

Unpack the archive and run `./vicon-lsl-bridge-gui` from its folder. It brings
its own Qt, liblsl, and LabRecorder. If Qt says it couldn't load the `xcb`
plugin, install the library it names, usually with
`sudo apt install libxcb-cursor0`.

## Streams

| Stream | What it holds |
| --- | --- |
| `ViconMarkers` | Four values per marker: X, Y, Z in millimetres, and a flag for whether it was seen. A hidden marker sends `NaN, NaN, NaN, 0`. |
| `ViconSegments` | Seven values per segment: X, Y, Z in millimetres, then four numbers for its rotation. An unreadable segment sends seven `NaN`. |
| `HoloLensGaze` | From the Unity app, 90 times a second. 21 values for where both eyes, the left eye, and the right eye are looking, each with a usable flag. |
| `HoloLensModelTargetPose` | Optional, from the Unity app. Eight values for the position and rotation of the Vuforia stair target, and whether it's tracked. |

You can rename the two Vicon streams. The HoloLens names come from the Unity
settings.

If Vicon subjects, markers, or segments change, or the Vicon frame rate changes,
the bridge replaces its streams. LabRecorder can't follow new subjects or markers,
so if that happens while recording, the app shows an error. Stop and start a new
recording.

## The desktop app

With the desktop app you can:

- Start and stop a whole session in one step, or run the bridge, preview, and
  recorder one at a time.
- Find LSL streams and pick each one by its source ID, a fixed name the stream
  keeps when it restarts. If source IDs change between runs, follow a stream by
  name instead.
- See the final file path and picked streams before you start, and keep an eye on
  everything while you record.
- Open a merged CSV or XDF recording and play it back, jump around, or loop it.
- Line up HoloLens gaze with Vicon and save that alignment.

The **Streams** tab shows each stream's name, type, source ID, computer, channels,
expected and measured rate, coordinate name, how old its last sample is, and any
warnings. When two streams share a name, the app doesn't quietly pick one.

Presets save your recording setup, not the window layout or recent files.
Settings from older versions aren't brought over.

The preview combines updates to keep up with the screen. Its **preview updates
replaced** and **combined updates** counters can go up during a healthy
recording. They count display updates, not lost samples. The bridge itself sends
every Vicon frame it gets.

The built-in XDF reader is for a quick look. Use
[pyxdf](https://github.com/xdf-modules/pyxdf) or
[xdf-Matlab](https://github.com/xdf-modules/xdf-Matlab) for real analysis.

Old recordings marked `eye_tracker_space` store gaze relative to the eye tracker.
The preview can show it, but can't line it up with the stairs, because the file
doesn't say where the headset was.

## Line up gaze with the stairs

Gaze and the stair target need to come from the same Unity world.

In Unity:

1. Use Mixed Reality OpenXR 1.5.1 or later.
2. Add `GazeDataProvider`, `GazeLSLOutlet`, and `VuforiaModelTargetPoseOutlet` to
   the scene.
3. Give the target component the stair `ModelTargetBehaviour`.
4. Give the gaze and target components the same `GazeLSLConfig` asset.

In the desktop preview:

1. Keep the default **Stair target** stream name, or type the one from
   `GazeLSLConfig`.
2. Start the preview and look at the real stairs until Vuforia finds them.
3. Select **Calibrate from Stair Target** and keep still while it collects 20
   good samples.

After that you can pause Vuforia with **M**. Keep `VuforiaModelTargetPoseOutlet`
on and record `HoloLensModelTargetPose` every time. While Vuforia is paused, the
HoloLens keeps sending the last steady stair position, marked as frozen, so a
recording started later can still be lined up when you open it. When you turn
Vuforia back on, it has to find the stairs again. Playback warns you if a file has
nothing to line up gaze with.

An alignment only lasts until you close the app. You can't type one in, because
the HoloLens position is unknown until measured. Until you align or apply a saved
one, gaze is drawn as the HoloLens sent it. **Clear Calibration** takes you back
there.

To keep it, select **Save Session Calibration**. You can apply, copy, hide,
import, and export saved calibrations. If coordinate details are missing, you
confirm before using one.

If an old saved calibration swaps left and right, run **Calibrate from Stair
Target** and **Save Session Calibration** again. New ones use the fixed math, and
so does XDF playback.

The stairs don't move. Their bottom front-left corner is 120.5 cm ahead of and
21.3 cm left of the Vicon origin, on the floor: `(-1.205, -0.213, 0)` metres,
facing up the stairs. The built-in **Default stair setup** already uses this. If
a saved calibration used the older estimate, pick **Default stair setup**,
calibrate again, and save. Restarting the HoloLens app can reset its world, so
align again after a restart.

The Unity app keeps the time each gaze reading was really taken, skips repeated or
broken readings, and picks up every reading since the last one. If it falls more
than 500 ms behind, it drops the older waiting readings and keeps the newest, so
you get a gap instead of late gaze. See [How time and coordinates
work](docs/time-and-coordinate-semantics.md) for details.

## Record with LabRecorder

By default, **Start Recording** runs LabRecorderCLI with the streams you picked.
No recorder window opens, and you don't need **Connect**.

If you'd rather see a recorder window, or record on another computer, turn on
**Record every visible stream in a separate recorder window**. The app then talks
to the LabRecorder window at `localhost:22345`, unless you change it. Turn on
remote control in LabRecorder with the same port. That window can't be told to
record only some streams, so it records every stream it can see. Before starting
its own LabRecorder, the app checks that address so it doesn't start a second
one. It shows **Started here** for one it started and **External** for one that
was already running, and it never closes an external one. **Disconnect / Detach**
lets go of a recorder without closing it.

To record:

1. Start the Vicon bridge.
2. In **Recording**, pick the study folder and file name pattern, and fill in the
   participant, session, task, run, acquisition, and modality.
3. In **Streams**, select **Find LSL Streams**. Mark which streams to record and
   which ones must be there.
4. Select **Check Setup** and read every item.
5. Select **Start Recording**. Or use **Start Session**, which starts the bridge
   and preview, finds streams, checks the setup, and starts recording for you.
6. Select **Stop Recording** or **Stop Session** when you're done.

The app fills in the file name pattern itself, so the path in **Recording
Destination** is exactly where the file goes. Because of how LabRecorder's remote
control works, its template box shows `%b` and its task box holds the finished
file name. Change recording details in this app, not in LabRecorder.

The app adds `.xdf` if needed. It blocks paths that leave the study folder, use
names Windows doesn't allow, can't be written to, or point at a file that already
exists unless you allow it. **Find Next Run** finds an unused run number. You get
a warning when free space is low.

Before you record, check that the bridge is streaming, the recorder is ready,
every required stream is there with recent data and the right channels, and the
file path is right.

If something is wrong, the app says what and how to fix it. A recorder error
doesn't stop the Vicon bridge. One Stop click is queued if the recorder is busy.
If the recorder connection was lost, select **Connect** and the Stop goes out
when it's back. Pressing Start or Stop twice does nothing extra. If you close the
app during Start, it either cancels Start or sends one last Stop.

After Stop, the app waits for the file and checks it in the background: the
streams, source IDs, channels, time ranges, rates, gaps, and sample times. The run
is marked **Checked**, **Checked with warnings**, or **Needs attention**. The
check never changes the file. **Open Recording in Preview** opens it straight
away.

## Look at a recording

Open a merged CSV or XDF file from **Preview**, drag it onto the window, or pick a
recent file. It loads in the background with a progress bar and a cancel button,
and what you had open stays on screen until it's done.

Both file types play the same way: play and pause, a timeline, single steps,
jumps, speed, and loop. Long recordings stay within a memory limit you set, so the
preview may skip some frames. The file check still uses exact numbers. If an XDF
has more than one stream that could be used, the app asks which to use as the main
timeline. If a stream restarted during a recording, its pieces are joined.

## Command line

The package also has a command-line app for scripts and computers without a
screen:

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

It doesn't pass on HoloLens gaze. The Unity app sends that straight to LSL.

## Build from source

You need:

- CMake 3.23 or later and a C++17 compiler.
- Boost thread, chrono, and headers.
- The Vicon DataStream SDK, which is a Git submodule here.
- Qt 6 Core, Widgets, and Network, for the desktop app.

If liblsl isn't installed, CMake downloads it.

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

With Qt 6 you get both `vicon-lsl-bridge` and `vicon-lsl-bridge-gui`. Without it,
you only get the command-line app.
