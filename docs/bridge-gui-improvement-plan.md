# Desktop app improvements

Status: complete

This page gives a short overview of the desktop app work. Use the other guides
for exact behavior, hardware checks, and release steps.

## What changed

The desktop app now guides a recording session from setup through review. It
keeps the direct controls for troubleshooting, but the usual workflow no longer
requires moving between unrelated tabs and guessing which step comes next.

The work focused on five goals:

- Make starting and stopping safe, even when a command is still in progress.
- Show which streams and output file will be used before recording begins.
- Keep the window responsive while live data or a large recording is loaded.
- Save calibration details and show whether they match the current setup.
- Check the recorded file after the recorder stops.

## Settings

One saved setup now covers the Vicon connection, stream names, preview inputs,
recorder connection, recording choices, output folder, and calibration profile.
Marker and segment preview inputs follow the bridge output names by default.
Advanced users can point the preview at other streams.

Saved presets contain session choices. Window size, splitter position, selected
tab, and recent files are stored separately. Settings start at version 1, and
the app rejects versions it does not understand.

## Starting a session

The normal Start Session action does this in order:

1. Start the Vicon bridge unless recorder-only mode is selected.
2. Start the live preview when it is available.
3. Find the current streams.
4. Check the recorder, streams, output path, storage, model, and calibration.
5. Start recording only when the required checks pass.

Warnings do not block recording. A failed required check can be bypassed only
when the user enters a reason and chooses Record Anyway. The reason is saved in
the session details.

Direct Start and Stop controls remain available for troubleshooting.

## Recorder safety

The recorder accepts one operation at a time. Repeated Start or Stop clicks do
not build a queue of duplicate commands. The app shows whether it is connecting,
starting, recording, stopping, or waiting for a reply.

The app first checks for an existing recorder. It starts another process only
when automatic launch is enabled and the configured recorder cannot be reached.
It may end only a recorder process that it started. Disconnecting from an
externally managed recorder never ends that process.

When the window closes, new recorder work is refused. If Start may have reached
the recorder, the app asks it to stop before closing. The window stays responsive
and shows which part is still stopping.

## Streams and output file

The stream table shows the stream name, source ID, host, channel count, rate,
coordinate name, and age of the newest sample. A saved setup can follow one
source ID or deliberately follow a stream name when source IDs are not stable.

Users can record every visible stream or select an exact list. The app checks
the list again immediately before Start and saves the final list with the
session details.

The path shown in the window is the path sent to the recorder. Before recording,
the app checks that the path:

- uses an `.xdf` file name;
- stays inside the chosen study folder unless the user allows otherwise;
- does not contain a Windows reserved name or unsupported character;
- can be created and written;
- does not overwrite an existing file unless overwriting is allowed; and
- has enough free storage for the configured warning limit.

Find Next Run chooses the next unused run number.

## Live preview and recorded files

The live preview keeps only the newest frame waiting for display. If drawing is
slower than the incoming data, old display frames are replaced instead of being
allowed to pile up. Stream-rate measurements continue independently.

CSV and XDF files load away from the window's main thread. The app reports
progress, allows cancellation, and limits how much data it keeps in memory. A
failed or canceled load leaves the previous recording available.

Playback supports seeking, frame steps, time jumps, speed changes, and an
explicit loop choice. Recent files and drag-and-drop opening use the same file
loading path.

When an XDF file contains more than one possible stream for a role, the app asks
the user to choose. It joins compatible pieces from the same source when a
stream was restarted during recording.

## Calibration

A calibration profile records the physical setup, stair model, coordinate names,
transform, notes, creation time, and measured quality. Profiles can be selected,
copied, imported, exported, or retired.

A newly measured calibration applies only to the current session until the user
saves it. Missing or different coordinate names require a warning and explicit
confirmation before an older profile is used.

## Stopping and checking the file

Stop Session works in the safe reverse order:

1. Stop the recording.
2. Wait for the output file to finish writing and check it.
3. Stop the preview.
4. Stop the Vicon bridge.
5. End an included recorder only if this app started it.

The file check compares the recording with the stream list saved before Start.
It checks stream identity, channel counts, time ranges, sample rates, gaps, and
repaired timestamps. The result is shown as Passed, Passed with warnings, or
Needs attention. The check never changes or deletes the recording.

## Limits kept on purpose

- Recorder-only sessions remain supported.
- An external recorder remains under the user's control.
- The built-in XDF reader is for preview and basic file checks, not scientific
  analysis.
- Stream names, channel order, timestamps, and coordinate rules stay unchanged.
- Automatic calibration is not saved unless the user chooses to save it.

## Checks before release

The automated checks cover repeated recorder commands, interrupted starts and
stops, recorder timeouts, process ownership, safe closing, stream selection,
path errors, large and damaged files, preview limits, calibration results, and
recording checks.

The interface is also checked at small window sizes and more than one display
scale. The package check covers the included recorder, Qt files, preview assets,
and settings that stay inside the portable package.

See also:

- [How the code is organized](architecture.md)
- [Behavior that must stay the same](behavior-contract.md)
- [How services start, stop, and recover](runtime-state-machines.md)
- [Hardware test guide](device-parity-runbook.md)
- [Release record](release-checklist.md)
