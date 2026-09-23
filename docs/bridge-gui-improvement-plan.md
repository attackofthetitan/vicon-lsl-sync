# Desktop app improvements

Status: done

This page is a short summary of the work on the desktop app. The other guides
have the exact behavior, hardware tests, and release steps.

## What changed

The desktop app now walks you through a recording session from setup to review.
The separate controls are still there for troubleshooting, but you no longer have
to jump between tabs and guess what comes next.

The work had five goals:

- Make starting and stopping safe, even while a command is still running.
- Show which streams and which file will be used before recording starts.
- Keep the window responsive while live data or a large recording loads.
- Save calibration details and show whether they fit the current setup.
- Check the recorded file after the recorder stops.

## Settings

One saved setup now covers the Vicon connection, stream names, preview streams,
recorder connection, recording choices, output folder, and saved calibration.
The marker and segment preview follow the bridge's stream names by default.
Advanced users can point the preview at other streams.

Presets save session choices. Window size, layout, the open tab, and recent files
are kept separately. Settings start at version 1, and the app refuses versions it
does not understand.

## Starting a session

**Start Session** does this in order:

1. Start the Vicon bridge, unless recorder-only mode is on.
2. Start the live preview when it is available.
3. Find the current streams.
4. Check the recorder, streams, file path, free space, stair model, and calibration.
5. Start recording only if every required check passes.

Warnings do not block recording. A failed required check can only be skipped if
you type a reason and choose **Record Anyway**. The reason is saved with the
session details.

The separate Start and Stop buttons are still there for troubleshooting.

## Recorder safety

The recorder handles one action at a time. Clicking Start or Stop again does not
pile up repeat commands. The app shows whether it is connecting, starting,
recording, stopping, or waiting for a reply.

The app first checks whether a recorder is already running. It only starts
another one if automatic start is on and the recorder it expects cannot be
reached. It will only ever close a recorder it started itself. Disconnecting from
a recorder someone else started never closes it.

When you close the window, new recorder work is refused. If Start may have
reached the recorder, the app asks it to stop before closing. The window stays
responsive and shows what is still stopping.

## Streams and the output file

The stream table shows each stream's name, source ID, computer, channel count,
rate, coordinate name, and how old its newest sample is. A saved setup can follow
one source ID, or follow a stream name if source IDs keep changing.

You can record every visible stream or pick an exact list. The app checks the
list again right before Start and saves the final list with the session details.

The path shown in the window is the path sent to the recorder. Before recording,
the app checks that the path:

- ends in `.xdf`;
- stays inside the chosen study folder, unless you allow otherwise;
- has no name or character that Windows does not allow;
- can be created and written to;
- does not overwrite an existing file, unless you allow that; and
- has enough free space for the warning level you set.

**Find Next Run** picks the next unused run number.

## Live preview and recordings

The live preview keeps only the newest frame waiting to be drawn. If drawing is
slower than the incoming data, old frames are replaced instead of piling up. Rate
measurements keep going on their own.

CSV and XDF files load in the background. The app shows progress, lets you
cancel, and limits how much memory it uses. If loading fails or is cancelled, the
previous recording stays open.

Playback supports jumping around, single-frame steps, time jumps, speed changes,
and a loop switch. Recent files and drag-and-drop use the same loading steps.

If an XDF file has more than one stream that could fill a role, the app asks you
to choose. If a stream restarted during recording, the matching pieces are joined
back together.

## Calibration

A saved calibration records the setup, stair model, coordinate names, the
alignment, notes, when it was made, and how accurate it was. Saved calibrations
can be picked, copied, imported, exported, or hidden.

A new calibration only lasts for the current session until you save it. If
coordinate names are missing or different, the app warns you and asks you to
confirm before using an older saved calibration.

## Stopping and checking the file

**Stop Session** goes in reverse:

1. Stop recording.
2. Wait for the file to finish writing, and check it.
3. Stop the preview.
4. Stop the Vicon bridge.
5. Close the bundled recorder, but only if this app started it.

The file check compares the recording with the stream list saved before Start. It
checks which streams are there, channel counts, time ranges, rates, gaps, and
fixed timestamps. The result is **Checked**, **Checked with warnings**, or
**Needs attention**. The check never changes or deletes the recording.

## Limits we kept on purpose

- Recorder-only sessions still work.
- A recorder someone else started stays under their control.
- The built-in XDF reader is for previews and basic file checks, not for
  analysis.
- Stream names, channel order, timestamps, and coordinate rules did not change.
- Automatic calibration is not saved unless you choose to save it.

## Tests before release

The automated tests cover repeated recorder commands, interrupted starts and
stops, recorder timeouts, which recorders the app may close, stream choice, path
errors, large and damaged files, preview limits, calibration results, and file
checks.

The window is also tested at small sizes and at more than one display scale. The
package test covers the bundled recorder, Qt files, preview model files, and
settings that stay inside the portable package.

See also:

- [How the code is organized](architecture.md)
- [Behavior that must stay the same](behavior-contract.md)
- [How each part starts, stops, and recovers](runtime-state-machines.md)
- [Hardware test guide](device-parity-runbook.md)
- [Release checklist](release-checklist.md)
