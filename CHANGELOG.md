# Changelog

Notable user-facing, compatibility, build, and maintenance changes are recorded here.

## [Unreleased]

## [1.13.6] - 2026-09-06

### Fixed

- Stop Session no longer appears to hang for five seconds when the bridge is in
  the middle of a connection attempt. The Vicon SDK waits for its own connection
  timeout before it reports an unreachable server, and nothing can cancel that
  wait once it has started, so its five second default decided how long a stop
  request sat unanswered. The bridge now opens a plain connection to the
  endpoint first, with a much shorter budget, and asks the SDK to connect only
  when something answers there.
- Stop Session sends Stop to the selected-stream recorder, including during
  startup, and cancels recording starts that are still finding streams.
- Opening a recording directly, from recent files, or by dropping it into the
  preview waits for live preview to stop. Closing cancels a queued file open.
- The bridge status follows disconnects and reconnects instead of staying Running.
- Starting another recording waits until the previous file check finishes.
- HoloLens gaze no longer loses about one reading in six. Each acquisition asked
  the tracker for the reading at the current time, which returns exactly one, so
  a poll landing more than one tracker frame late lost every frame in between.
  Acquisition now walks forward from the last accepted capture time, taking up
  to 32 readings per step, so the captured rate no longer depends on how
  punctually the step runs.

### Changed

- Simplified session start and stop, removed redundant state and JSON wrappers,
  and kept preview button updates in one place.
- Reused stream schema constants and clarified playback names and status text.
- The gaze backlog budget is 500 ms rather than 25 ms. A full drained batch
  spans 355 ms at 90 Hz, so a tighter budget would discard exactly the readings
  draining recovers. Gaze publishing steps run at 1.25 times the nominal rate,
  because a step sends at most one sample and a queue built during a stall can
  only shrink if steps outpace the tracker.
- The gaze stream declares the frame rate the tracker reported for the mode it
  accepted, and the app measures the rate that actually arrives, warning while
  it stays below 80% of nominal and naming whether the tracker is publishing
  slowly or readings are being lost. Eye tracker calibration validity is logged.

### Compatibility

- No configuration, file format, or command line change. Roll back to `v1.13.5`.
- The gaze stream header changes value: `acquisition_mode` now carries the
  selected rate, `backlog_policy` reports the 500 ms budget, and a new
  `reading_retrieval` value records that readings are drained in sequence.
  Anything that matched the previous strings exactly needs updating.

## [1.13.5] - 2026-09-04

### Fixed

- The preview floor sits at the foot of the stairs. It was drawn at the padded
  bottom of the view box, a quarter of a metre below the model, so the ground
  appeared to float beneath the stairs it was meant to carry.

### Changed

- The preview floor covers the walking area rather than stopping at the stair
  footprint, reaching 4.6 m past the far end of the stairs and 0.6 m behind and
  to either side. The extent is taken from the recorded runs, whose valid
  markers reach 4.10 m past that end and stay inside the stair width. The floor
  is part of the view fit, so the whole walking range is visible at rest.
- The preview fits the scene as it is projected, instead of scaling the largest
  world span against the shorter side of the widget. A walkway several times
  longer than it is tall now fills a wide panel rather than a square of it.
- The drawing area keeps a little over half the preview panel once the controls
  no longer fit, rather than only the rows they leave over. The controls scroll
  sooner instead of the view being the part squeezed.

### Compatibility

- No configuration, file format, or command line change. Roll back to `v1.13.4`.


## [1.13.4] - 2026-09-03

### Fixed

- The live preview controls are no longer drawn on top of the drawing area. On a
  short window the preview claimed a fixed 320 pixels of height, more than the
  panel had left, and the controls beneath were painted over it. The drawing
  area now yields height instead, so the two never share pixels.
- The Session Status block lines up. "Elapsed:" sat in the column that holds
  values while its clock sat in the column that holds captions, and the preview
  update counter wrapped to three lines and dragged its row out of line with the
  rest. Every field is now a caption and a value in fixed columns, one line each.
- Status values shorten themselves rather than reshaping the window. A long
  recorder state or destination path used to either wrap and change the row
  height or refuse to let the window narrow; each now elides to one line and
  offers the full text as a tooltip.
- The preview status text and the delivery counter no longer overlap. They
  shared a row, so the counter wrapped to two lines and ran through the text
  beside it. They are separate lines now.
- The Events tab no longer scrolls sideways. Its four buttons sat in a row that
  could not wrap and needed 627 pixels in a 520 pixel tab.
- Text fields show the start of their value. Narrowing the window left each field
  scrolled to wherever it had been, so a study root read as "ders/9z/t6lypx..."
  and a stream name as "iconMarkers".
- The preview status and file state no longer blank themselves. The first resize
  overwrote both labels with the empty string they were tracking internally.
- A row of controls that wraps now reports the height it really needs. It claimed
  the height of a single row, so containers above it came up short by the rows
  that had wrapped, and a scroll bar appeared with space still to spare.

### Changed

- The playback transport, timeline, and loop control appear once a recording is
  loaded. Shown disabled at all times, they cost about ninety pixels and pushed
  the live controls into a scroll area on an ordinary window.
- The preview source fields reflow from two per row to one as the panel narrows,
  rather than holding a fixed grid and scrolling sideways.
- The window minimum is 800x560, replacing 680x540. The old floor could be
  reached but never laid out; the content actually demanded 800x601.
- Storage and preview update counts read as plain values under their captions,
  instead of repeating the caption inside the value.

### Compatibility

- No configuration, file format, or command line change. Roll back to `v1.13.3`.


## [1.13.3] - 2026-09-03

### Changed

- The macOS disk image is now arranged for drag installation. Both applications
  sit beside a link to the Applications folder, with the command line tools in a
  folder of their own. Run from the mounted image, the app had a different
  location on every mount, so macOS asked for permissions again each launch.
- The macOS application declares why it needs local network access, so the
  request explains that it is for finding Lab Streaming Layer streams.

### Known limitation

- The build is still ad-hoc signed. First launch still needs approval under
  Privacy and Security, and permissions are asked for again after an update.
  Both need Developer ID signing and notarization, which are not configured.

### Compatibility

- The `.tar.gz` layout is unchanged. Roll back to `v1.13.2`.


## [1.13.2] - 2026-09-02

### Fixed

- The stair model is packaged for macOS and Linux again. Only the Windows package
  shipped it, so on the other platforms the preview had no stair to draw and gaze
  had nothing to be aligned against.
- Opening a recording no longer stretches the preview controls. The load summary
  runs to several hundred characters and was wrapped over several lines, pushing
  the transport controls and timeline out of view; it is now shown on one line
  with the full text in its tooltip.

### Compatibility

- No format change. Roll back to `v1.13.1`.


## [1.13.1] - 2026-09-02

### Fixed

- The measured stair pose is no longer rounded to millimetres. The alignment read
  it back from a display that held three decimals, losing 0.6 mm of a fixed,
  known value. The saved calibration is now used directly, and the pose is shown
  read-only at full precision so a stray edit cannot move it.

### Compatibility

- No format change. Saved calibrations, stream layouts, timestamps and
  coordinates are unchanged. Roll back to `v1.13.0`.


## [1.13.0] - 2026-09-02

### Removed

- The manual HoloLens transform in the desktop preview. The pose of the HoloLens
  world in Vicon coordinates cannot be known before it is measured, so a
  hand-entered translation and rotation could only produce a wrong alignment.

### Changed

- Gaze alignment now comes only from a solved or applied stair-target
  calibration. Without one the preview draws gaze in its published HoloLens
  frame and reports **Not calibrated**.
- **Clear Calibration** replaces **Use Manual Transform**.
- Controls that only work in some states are enabled only in those states.

### Fixed

- A rejected calibration solve no longer leaves the previous alignment drawing.
- Selecting a saved calibration no longer overwrites the quality reading for the
  calibration actually in use.
- The window no longer needs sideways scrolling. Rows of controls wrap, and the
  preview's buttons, transport and timeline stay visible instead of being pushed
  out of view by the settings tabs.

### Compatibility

- Session settings stay at version 1 and still load; the two saved manual gaze
  values are ignored and dropped. Saved calibrations, stream layouts, timestamps
  and coordinates are unchanged. Roll back to `v1.12.2`.

## [1.12.2] - 2026-09-02

### Changed

- The order a guided session starts and stops in, the checks that decide whether
  a recording may begin, and the rules for which streams survive a rediscovery
  now live beside the desktop window rather than inside it, so each can be
  exercised on its own.

### Compatibility

- No user-visible change. The desktop application behaves as it did in
  `v1.12.1`; the decisions it makes were moved, not altered. Command-line
  options, LSL stream layouts, timestamps, coordinates, and saved-session
  formats are unchanged. Use `v1.12.1` as the rollback release.

## [1.12.1] - 2026-09-02

### Fixed

- The live preview now retries streams that were not yet published when the
  preview started. A frozen clock reading meant the one-second resolve retry
  could never come due, so a stream that was absent at startup stayed absent.
- The preview stream status line now keeps updating, and a stream that stops
  delivering is reported as stale. Both were driven by the same frozen clock
  reading and previously updated only once.
- Closing the desktop app no longer waits forever on a recorder that does not
  settle. The fifteen-second stop deadline compared two readings of a clock
  that never advanced, so it could not expire.
- Recorder log lines are no longer spliced together across channels. Standard
  output and standard error shared one partial-line buffer, so an unterminated
  output line was completed by the next error text and reported at the wrong
  severity.
- A Vicon server that accepts connections but never delivers a frame no longer
  causes an unthrottled connect, read, and disconnect loop. The first failure
  still retries immediately; later consecutive failures wait the configured
  reconnect interval.
- The bridge now treats a dropped Vicon connection as disconnected. The
  connection check reported only what the client had last requested and never
  consulted the SDK.

### Changed

- Marker and segment frames no longer copy the subject, object, and operation
  names for every item on every frame. That context is carried by the
  diagnostic raised for a failed read, which is where it was already read from.
- Marker and segment samples are flattened once into the outlet's channel
  vector instead of being built as an intermediate array of per-item samples.
- The Vicon client implements the bridge's client interface directly, replacing
  a forwarding shim that restated all sixteen methods.
- The bridge's public header no longer includes the Vicon SDK header, which it
  did not need and which every desktop translation unit was parsing.
- The event log appends new entries instead of re-rendering every retained
  entry for each one.

### Compatibility

- Command-line options, LSL stream names, channel layouts, channel metadata,
  timestamps, coordinates, saved-session formats, and log text are unchanged.
- Physical Vicon, HoloLens, Vuforia, and LabRecorder integration remains a
  manual qualification step. Use `v1.12.0` as the rollback release.

## [1.12.0] - 2026-09-02

### Added

- Added native Apple Silicon builds for the bridge command-line and desktop
  applications, LabRecorder, and LabRecorderCLI.
- Added versioned macOS ARM64 disk-image and tarball release assets with
  bundled Qt and LSL runtime dependencies.

### Changed

- The desktop app now discovers bundled recorder executables in Windows,
  Unix, and macOS application-bundle layouts.
- The hosted build now compiles, tests, packages, signs, and validates macOS
  ARM64 alongside the existing Windows and Linux targets.

### Compatibility

- Existing Windows and Linux release filenames, command-line options, LSL
  stream layouts, timestamps, coordinates, and saved-session formats are
  unchanged.
- macOS packages are ad-hoc signed for integrity but are not Developer ID
  signed or notarized. A first launch may require explicit approval in macOS.
- Physical Vicon, HoloLens, Vuforia, and LabRecorder integration remains a
  manual qualification step. Use `v1.11.0` as the rollback release.

## [1.11.0] - 2026-09-01

### Added

- Added one guided desktop-session path that starts the bridge and preview,
  discovers streams, checks the recording setup, starts recording, and stops
  each owned component in order.
- Added versioned session presets, managed calibration profiles, explicit stream
  identities, recording-destination checks, and post-recording XDF verification.
- Added responsive CSV and XDF loading with cancellation, bounded memory use,
  stream mapping, seeking, frame stepping, playback speed, and optional looping.

### Changed

- Recorder commands now run one operation at a time, wait for explicit replies,
  and distinguish a recorder started by the desktop app from an external one.
- The desktop app now keeps session state in one place, exposes setup and file
  check results, and keeps background preview and shutdown work off the window
  thread.
- Rewrote the project guides and README files in plain English. Commands, paths,
  stream layouts, timing rules, and release filenames did not change.

### Fixed

- Fixed selected-stream query errors being cleared by later terms, incorrect
  path-field diagnostics, canceled file checks being reported as failures, stale
  stair-model readiness, and incomplete preview timelines surviving rejection.
- Fixed stream-health warnings obscuring measured freshness and made duplicate
  stream names require an explicit identity choice.

### Compatibility

- Existing command-line options, LSL stream names and layouts, timestamp and
  coordinate rules, build targets, and release filenames remain unchanged.
- Session presets and saved calibration files use their first versioned format;
  desktop settings from releases before `v1.11.0` are not imported.
- Physical HoloLens 2, Vuforia, Vicon, and LabRecorder qualification remains a
  publication gate when that equipment is available. Use `v1.10.5` as the
  rollback release if an integration problem appears.

## [1.10.5] - 2026-08-23

### Changed

- Split large bridge, preview, XDF, desktop, settings, and portable-launcher files into smaller files. Public names and behavior stay the same.
- Moved duplicate marker and segment LSL output work into one private helper. Stream names and layouts stay the same.
- Made the HoloLens gaze and model-target layouts come from shared JSON files. Generated C++ and C# files now use the same source.
- Split large C++ and C# check files into smaller groups without removing any covered behavior.
- Moved CMake and release work into smaller modules and scripts. Build targets, hosted jobs, release files, and package results stay the same.

### Packaging

- Put Windows path, temporary-folder, license, and output checks in one place.
- Split portable-file checking, program startup, and safe folder handling into smaller C++ files.
- Kept checksum rejection, checked extraction, support for extra Windows certificate data, and the same Windows and Linux release files.

### Documentation

- Added guides for code ownership, fixed behavior, startup and recovery, time and coordinates, hardware checks, and release work.

### Compatibility

- There is no intended change to program behavior, public C++ or C# names, saved Unity fields, command-line options, or LSL stream layouts.
- Setting names, build targets, and release filenames also stay the same.
- The HoloLens 2, Vuforia, Vicon, and LabRecorder hardware setup was not available for this release. Automated stream, timing, start/stop, recovery, recording, and package checks passed. Use `v1.10.4` as the rollback version if a hardware problem appears.

[Unreleased]: https://github.com/attackofthetitan/vicon-lsl-sync/compare/v1.12.0...HEAD
[1.12.0]: https://github.com/attackofthetitan/vicon-lsl-sync/compare/v1.11.0...v1.12.0
[1.11.0]: https://github.com/attackofthetitan/vicon-lsl-sync/compare/v1.10.5...v1.11.0
[1.10.5]: https://github.com/attackofthetitan/vicon-lsl-sync/compare/v1.10.4...v1.10.5
