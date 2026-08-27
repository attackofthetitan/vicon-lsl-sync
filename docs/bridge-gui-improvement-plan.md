# Bridge GUI Improvement Plan

Status: Implemented
Scope: `vicon-lsl-bridge/src/gui`, the preview code it uses, and its GUI-specific tests and packaging
Primary objective: make recording sessions safe, observable, responsive, and difficult to misconfigure without changing the established stream contracts

## Outcomes

The completed work should provide:

- An explicit, race-free state model for bridge, preview, recorder, and application shutdown.
- Bounded and visible shutdown behavior with no indefinite GUI-thread waits.
- Responsive CSV and XDF loading for long recordings with progress, cancellation, and bounded memory use.
- Unambiguous live and recorded stream selection based on stream identity rather than name alone.
- One shared configuration for bridge outputs, preview inputs, recording selection, and expected-stream checks.
- Validation of the exact recording path sent to LabRecorder, including overwrite and storage checks.
- Persistent recording, calibration, health, and error displays instead of one transient status string.
- A preview path with bounded frame delivery and a deliberate choice between a lightweight painter and a real 3D renderer.
- A guided session workflow covering preflight, recording, and post-recording verification.
- Dedicated automated coverage for GUI state transitions and failure paths.

## Constraints

- Preserve the stream names, schemas, source IDs, timestamp rules, coordinate conventions, and recovery behavior documented in `behavior-contract.md` unless a separate contract change is approved.
- Continue supporting an externally managed LabRecorder; never terminate a process the bridge did not launch.
- Keep recorder-only workflows possible. Strict preflight failures must have a deliberate, logged override rather than silently removing that workflow.
- Keep automatic calibration session-scoped by default until persisted calibration profiles include enough setup identity and quality metadata to be safe.
- Treat the built-in XDF reader as a preview and verification tool, not a replacement for scientific analysis libraries.
- Keep the new guided-session settings isolated from older application settings.

## Milestones

| Milestone | Priority | Result | Depends on |
| --- | --- | --- | --- |
| M1. Recorder and session state | Critical | Duplicate commands and pending-start shutdown races are removed | None |
| M2. Bounded lifecycle | Critical | Bridge, preview, recorder, and window shutdown cannot block indefinitely | M1 state vocabulary |
| M3. Configuration, stream identity, and path safety | High | Sources and destinations are explicit and validated | M1 |
| M4. Preview performance and rendering | High | Live and recorded previews remain responsive and bounded | M2 |
| M5. Operational dashboard and preflight | High | Users can see and verify session readiness and recording health | M1, M3, M4 metrics |
| M6. Playback, calibration, and post-run verification | Medium | Recorded-data review and alignment become durable workflows | M3, M4, M5 |
| M7. GUI verification and release gates | Continuous | State, failure, performance, and packaging regressions are detected | Begins in M1 |

## M1. Recorder and session state

### BG-001: Introduce explicit recorder operation states

Problem:

- `LabRecorderClient` accepts every command batch while earlier work is active.
- Recording state changes only after the entire command batch is acknowledged.
- While the state is `Unknown`, both Start and Stop may be available.
- Repeated clicks can queue duplicate Start or Stop operations.

Plan:

- Add an operation state such as `Idle`, `Refreshing`, `UpdatingFilename`, `Starting`, and `Stopping` alongside the acknowledged recording state.
- Expose the operation state and queue depth through signals or a read-only model.
- Reject, replace, or coalesce incompatible work instead of growing an unbounded command queue.
- Coalesce filename updates so only the newest unsent value remains.
- Reject a second Start while Start is active or queued.
- Make Stop supersede queued nonessential work and become the next safe command.
- Disable or relabel controls from the complete state model, not only connection and acknowledged recording state.
- Show operation progress, including which command in a batch is awaiting acknowledgement.

Acceptance criteria:

- Double-clicking Start sends exactly one start sequence.
- Double-clicking Stop sends exactly one stop command.
- Refresh and filename edits cannot insert unsafe work between the filename and Start commands.
- The GUI visibly distinguishes `Starting`, `Recording`, `Stopping`, `Stopped`, and `Unknown`.
- A timeout or disconnect clears or resolves queued work deterministically and leaves the UI in an actionable state.

Verification:

- Fake-server tests for rapid duplicate clicks, conflicting operations, split acknowledgements, timeouts, and reconnects.
- State-policy tests for every connection, acknowledged recording, and operation-state combination.

### BG-002: Make close aware of pending recorder work

Problem:

- Close currently sends Stop only when the acknowledged state is exactly `Recording`.
- Closing during a pending Start can finish without sending Stop; an owned process may then be terminated, while an external recorder may continue recording.

Plan:

- Track desired state separately from last acknowledged state.
- On close, prevent new non-shutdown work and inspect active and queued commands.
- If Start has not reached the server, cancel it.
- If Start may have reached the server, arrange for Stop to be the final recorder operation.
- Display the shutdown phase and remaining deadline.
- Record whether shutdown completed normally, timed out, or lost the recorder connection.

Acceptance criteria:

- Closing before Start is sent never begins a recording.
- Closing after Start may have been sent always attempts Stop when connected.
- External LabRecorder processes are never terminated.
- Owned LabRecorder processes are not terminated until Stop is acknowledged or the documented recorder deadline expires.

### BG-003: Make recorder process startup explicit and asynchronous

Plan:

- Try the configured remote-control endpoint before launching another process.
- Add an `Automatically launch LabRecorder` preference, with clear bundled/custom executable behavior.
- Convert `waitForStarted` and `waitForFinished` paths to asynchronous signal-driven transitions.
- Set the launched process working directory to the executable directory when appropriate.
- Drain or forward process output into the application event log with a bounded buffer.
- Distinguish `external`, `launching`, `owned running`, `owned exited`, and `launch failed` states.
- Provide an explicit Disconnect or Detach action without terminating an external process.

Acceptance criteria:

- Opening the application does not launch a duplicate recorder when the configured endpoint is already available.
- A slow or failed launch never freezes the window.
- Process output cannot grow an unbounded in-memory buffer.
- Ownership remains correct across launch failure, process exit, reconnect, and application close.

## M2. Bounded lifecycle

### BG-004: Replace ineffective close deadlines with cancellable shutdown

Problem:

- The window stops polling the bridge after four seconds, but destruction later performs an unbounded worker wait.
- Preview destruction also waits without a deadline.
- Blocking Vicon or LSL calls can therefore keep the process alive after the UI deadline has expired.

Plan:

- Define shared lifecycle states for bridge and preview workers: `Idle`, `Starting`, `Running`, `Stopping`, `Stopped`, and `Failed`.
- Audit every blocking SDK and LSL call for its maximum duration and cancellation behavior.
- Ensure stop requests can interrupt retry waits and prevent new blocking operations.
- Keep the window alive in a noninteractive `Closing` state until workers actually stop, or isolate non-cancellable work in a process that can be ended safely.
- Remove unbounded waits from GUI-thread destructors.
- Make the four-second bridge and fifteen-second recorder policies observable outcomes rather than hidden polling constants.

Acceptance criteria:

- Every shutdown path has a documented maximum UI-thread blocking interval.
- No GUI-thread destructor performs an unbounded wait.
- Close during Vicon connection, streaming, reconnect delay, preview resolution, calibration, file opening, recording Start, and recording Stop is covered.
- Repeated close requests do not create multiple shutdown sequences.

### BG-005: Add lifecycle diagnostics

Plan:

- Timestamp lifecycle transitions and stop requests.
- Report which component is delaying shutdown.
- Include final shutdown results in the diagnostic export.
- Preserve the current rule that only an owned LabRecorder process may be ended.

## M3. Configuration, stream identity, and path safety

### BG-006: Create one shared session configuration

Problem:

- Bridge marker and segment output names are separate from preview marker and segment input names.
- Custom bridge names can leave preview resolving unrelated defaults.

Plan:

- Introduce a `SessionConfiguration` model containing Vicon endpoint, output stream names, expected HoloLens streams, preview matching settings, recorder endpoint, recording selection, path metadata, and calibration profile.
- Bind preview marker and segment names to bridge output names by default.
- Allow an advanced `Preview external streams` override without changing bridge outputs.
- Start the stored guided-session configuration at schema version 1.
- Add Reset, Save Preset, Load Preset, Import, and Export operations.
- Persist window geometry, splitter position, active tabs, and recent file locations separately from scientific/session settings.

Acceptance criteria:

- Changing a bridge output name updates the bound preview input immediately.
- Existing application settings remain untouched and the new guided-session
  schema starts from documented defaults.
- A preset fully describes the inputs needed to reproduce a session setup without including machine-specific transient state.

### BG-007: Add a live stream discovery and identity browser

Problem:

- Live preview resolves by name and uses the first result.
- Duplicate names can select a stale publisher or the wrong machine.

Plan:

- Discover candidate streams and display name, type, source ID, host, session ID, channel count, nominal rate, coordinate frame, and freshness.
- Bind a role to a selected identity rather than only a name.
- Warn when multiple candidates satisfy the same configured role.
- Define reconnection behavior when the same source ID returns and when only a differently identified stream appears.
- Allow an explicit `follow by name` mode for setups where source IDs are intentionally unstable.
- Surface missing or incomplete channel and coordinate metadata instead of silently hiding fallback behavior.

Acceptance criteria:

- Duplicate stream names never result in an unexplained arbitrary choice.
- The selected source identity and reconnection mode are visible.
- Metadata fallback produces a visible warning and diagnostic entry.

### BG-008: Add recorded-stream mapping

Problem:

- XDF assembly chooses the first recognized master, gaze, and target stream.
- Files with duplicate, recreated, or similarly named streams can be truncated, mixed, or mapped unexpectedly.

Plan:

- Inventory all XDF streams before frame assembly.
- Group streams by role, source ID, name, host, and time range.
- Automatically stitch compatible recovered instances when identity and schema permit it.
- Ask for a mapping when multiple incompatible candidates remain.
- Let users select the master timeline.
- Report unmatched sample percentages, time ranges, and applied clock corrections.
- Reject a recording with no supported preview stream instead of silently producing blank frames from an arbitrary numeric stream.

Acceptance criteria:

- Multiple same-role streams have deterministic, explained handling.
- Recovered compatible stream instances cover the complete recording timeline.
- The summary records the selected mapping and any excluded streams.

### BG-009: Replace unconditional recording selection with an allowlist

Plan:

- Display discovered LSL streams before recording.
- Support expected-stream presets and per-stream selection.
- Refresh immediately before Start, then reconcile the result with the selected policy.
- Warn when required streams are absent, stale, duplicated, or schema-incompatible.
- Keep an explicit `Record every visible stream` mode for existing workflows.
- Store the final selected-stream inventory with the session diagnostics.

Acceptance criteria:

- Users can see exactly which streams will be recorded before Start.
- Newly appearing required streams can be included without silently selecting unrelated streams.
- Start behavior remains deterministic if discovery changes during the command sequence.

### BG-010: Normalize and validate the exact recording destination

Problem:

- Root validation uses the raw input while preview and recorder commands use sanitized values.
- The destination is not checked for escape from the study root, extension, writeability, free space, reserved names, path length, or collision.

Plan:

- Build one normalized filename request and use it for validation, preview, command generation, and diagnostics.
- Prefer rejecting protocol-breaking characters with an inline explanation over silently changing the destination.
- Canonicalize the resolved path and require it to remain under the selected study root unless an advanced override is confirmed.
- Require or append the `.xdf` extension consistently.
- Validate parent-directory creation, write permissions, Windows reserved names, trailing spaces or periods, and practical path length.
- Check available storage and show a configurable warning threshold.
- Detect existing output files and require an explicit overwrite decision.
- Offer `Find next run` and optional automatic run increment after a successful recording.

Acceptance criteria:

- The path displayed to the user exactly matches the path sent to LabRecorder.
- Validation cannot succeed for one path while recording targets another.
- Accidental traversal outside the study root and accidental overwrite are blocked by default.
- Every filename failure identifies the field and corrective action.

## M4. Preview performance and rendering

### BG-011: Load CSV and XDF asynchronously

Plan:

- Move parsing, timestamp correction, calibration solving, and frame assembly off the GUI thread.
- Add progress stages: reading, indexing, metadata, timestamps, calibration, and frame preparation.
- Support cancellation between bounded chunks.
- Reject or safely handle files whose declared sizes or counts exceed configured limits.
- Keep the previous usable source visible until a new file has loaded successfully.
- Preserve detailed errors without leaving partially loaded playback state.

Acceptance criteria:

- The window remains interactive while loading a large recording.
- Cancel returns to the previous stable source without leaking a worker or retaining partial data.
- A failed load does not clear a previously loaded recording unless the user requests it.

### BG-012: Introduce indexed and bounded playback storage

Plan:

- Avoid retaining the file bytes, all raw per-stream samples, assembled frames, and duplicate timestamps simultaneously.
- Build a stream index and decode only the active playback window where practical.
- Add bounded frame caching and visual decimation for high-rate or long recordings.
- Expose estimated and current memory use.
- Keep exact timestamps and stream metadata available for verification even when drawing is decimated.

Acceptance criteria:

- Memory use has a documented bound or predictable relationship to the configured cache, not recording duration alone.
- A multi-hour recording can be opened and scrubbed on the supported workstation profile.
- Decimation changes only display density, never reported timing or validation results.

### BG-013: Bound live-frame delivery

Problem:

- The worker emits vector-heavy frames for every assembled update, with no backpressure or queue bound.
- A slow renderer can accumulate old frames and show increasing latency.

Plan:

- Replace queued frame-per-update delivery with a single latest-frame mailbox or bounded ring.
- Render at a configurable 30 or 60 Hz while continuing to measure source rates independently.
- Count frames replaced before display and show preview latency.
- Prefer chunk pulls and retaining the newest appropriate sample when an inlet backlog develops.
- Keep calibration samples and health metrics independent from display-frame dropping.

Acceptance criteria:

- Preview latency remains bounded when rendering is intentionally slowed.
- Event-queue memory does not grow with sustained input.
- Displayed-drop counters distinguish source loss from intentional preview coalescing.

### BG-014: Decide and implement the rendering direction

Decision gate:

- If the preview remains a lightweight visual check, replace `QOpenGLWidget` with a regular QWidget painter and remove the OpenGL runtime dependency.
- If depth, occlusion, perspective, mesh lighting, and object picking are product requirements, implement an actual 3D renderer and benchmark it.

Common work:

- Add Fit View and Reset Camera controls.
- Prevent moving data from being permanently clipped by bounds fixed from the first frame.
- Add axis labels, units, a color legend, and valid/total counts.
- Remove trails for objects that disappear after a layout change.
- Support a headless rendering path for automated visual checks where feasible.

Acceptance criteria:

- Rendering technology matches the actual feature set and deployment requirements.
- The preview has deterministic camera reset and fit behavior.
- Invalid or occluded objects are distinguishable from valid visible objects.

## M5. Operational dashboard and preflight

### BG-015: Split persistent state from the event log

Plan:

- Replace free-form status strings as the source of truth with typed component states.
- Show separate bridge, recorder, preview, calibration, file, and path indicators.
- Keep calibration quality visible after periodic stream-health updates.
- Update `Last error` only for real errors and preserve it until acknowledged or superseded by another error.
- Add a bounded, timestamped event log with severity and component filters.
- Add Copy and Export Diagnostic Bundle actions, including configuration, stream inventory, state transitions, rate summaries, and recent errors without recording data by default.

Acceptance criteria:

- A normal status message never replaces the last real error.
- Recording and calibration state remain visible while transient events arrive.
- The diagnostic log remains bounded during long sessions.

### BG-016: Add a guided session preflight

Plan:

- Check Vicon connection and recent bridge status.
- Check expected LSL streams, identities, freshness, rates, channel schemas, and coordinate frames.
- Check LabRecorder connection and absence of conflicting in-flight work.
- Check normalized output path, writeability, collision, and available storage.
- Check stair model and calibration status when the selected workflow requires them.
- Classify results as required failure, warning, or informational.
- Provide a deliberate `Record anyway` override with reason capture for policies that permit it.

Acceptance criteria:

- Start Recording explains every blocking condition inline.
- Recorder-only sessions remain possible through an explicit mode or override.
- The preflight result and any override are included in session diagnostics.

### BG-017: Add a persistent recording dashboard

Plan:

- Show a prominent recording indicator and `Starting` or `Stopping` intermediate states.
- Show elapsed duration, destination path, run identifier, recorder ownership, and endpoint.
- Show selected streams with freshness, effective rate, nominal rate, channel count, and warnings.
- Show available storage and preview/source drop indicators.
- Provide one clear emergency Stop action while preventing duplicate commands.

Acceptance criteria:

- Recording state is understandable without reading a transient message.
- The user can confirm destination and stream selection throughout the run.
- Critical health changes remain visible until acknowledged.

### BG-018: Add one-click session orchestration

Plan:

- Provide a guided sequence for Connect Vicon, Start Bridge, Start Preview, Run Preflight, and Start Recording.
- Preserve independent controls for troubleshooting and advanced workflows.
- Stop in the reverse safe order: recording, preview, bridge, then owned recorder when requested.
- Show partial completion and recovery actions when a step fails.

Acceptance criteria:

- A normal session can be started and stopped without switching among unrelated controls.
- Failure at any step leaves already-started components visible and safely stoppable.

## M6. Playback, calibration, and post-run verification

### BG-019: Build complete playback controls

Plan:

- Add a timeline scrubber with current time, duration, and frame/sample position.
- Add step forward, step backward, jump to start/end, and configurable time jumps.
- Add an explicit loop toggle instead of always wrapping silently.
- Preserve playback position while changing speed.
- Add recent-file history and drag-and-drop opening.
- Optionally export a preview image or short visual segment without changing source data.

Acceptance criteria:

- Users can seek without playing the entire recording.
- End-of-recording behavior is explicit and testable.
- CSV and XDF use the same playback behavior and controls.

### BG-020: Add managed calibration profiles

Plan:

- Represent calibration profiles with an ID, version, stair-model identity, fixed Vicon stair pose, transform, coordinate frames, setup notes, creation time, and quality metrics.
- Allow profile import, export, selection, duplication, and retirement.
- Provide UI for updating the measured stair pose without recompiling.
- Keep newly solved automatic calibration session-only until the user explicitly saves it.
- Display collection progress, translation and rotation RMS, rejection reason, and metadata compatibility.
- Warn or require confirmation when coordinate-frame metadata is missing and fallback compatibility is used.
- Throttle calibration progress updates so target-pose rate cannot dominate GUI work.

Acceptance criteria:

- A saved profile contains enough information to identify the physical and coordinate setup it belongs to.
- Applying a profile is visible, reversible, and recorded in diagnostics.
- Calibration quality remains visible after completion.

### BG-021: Verify recordings automatically after Stop

Plan:

- After a successful Stop, locate and open the resulting XDF in a background verifier.
- Confirm expected streams, source identities, schemas, time ranges, duration, effective rates, gaps, clock corrections, and repaired timestamps.
- Compare the recorded selection with the preflight inventory.
- Mark the run `Verified`, `Verified with warnings`, or `Needs attention`.
- Link directly to detailed findings and offer to open the recording in playback.
- Increment the run number only after the output file exists and the configured completion policy is satisfied.

Acceptance criteria:

- A successful recorder acknowledgement alone is not presented as proof that expected data were saved.
- Verification failures never delete or rewrite the recording.
- Results can be exported with the session diagnostics.

## M7. GUI verification and release gates

### BG-022: Extract testable controllers and service interfaces

Plan:

- Separate state transition logic from widget construction.
- Inject interfaces for bridge worker creation, recorder client, process launcher, stream resolver, file loader, clock, settings, and file dialogs.
- Keep Qt widgets as views over explicit models or controllers.
- Move integration-only checks out of the production entry point where practical.

Acceptance criteria:

- Recorder and shutdown state transitions can be tested without a real Vicon server, LSL network, recorder process, or visible window.
- Core GUI behavior does not depend on matching status-label text.

### BG-023: Add state and interaction coverage

Required scenarios:

- Rapid duplicate Start and Stop clicks.
- Close before Start is sent, during each Start command, while Recording, during Stop, and after disconnect.
- Recorder connection replacement, timeout, malformed reply, process exit, and external-process ownership.
- Bridge stop during connection, streaming, reconnect, and SDK delay.
- Preview stop during resolution, metadata fetch, polling, and calibration.
- Bound and unbound preview stream-name behavior after bridge configuration changes.
- Duplicate live stream names and source-ID recovery.
- Multiple same-role XDF streams and recovered-instance stitching.
- Filename normalization, traversal, invalid Windows names, collision, write failure, and low storage.
- Large-file progress, cancellation, malformed counts, truncated files, and memory bounds.
- Live-frame backpressure and latency under a deliberately slow renderer.
- Calibration success, instability, missing metadata, profile persistence, and persistent quality display.
- Preflight blocking, warning, override, and recorder-only modes.
- Post-recording verification success, warning, and failure.

### BG-024: Add UI, accessibility, and packaging checks

Plan:

- Verify layout at common small displays and scaling factors, not only 1920x1080.
- Add scrollable controls where minimum-size pressure would otherwise push content off-screen.
- Add keyboard navigation, shortcuts, label buddies, accessible names, and focus indicators.
- Verify high-contrast and light/dark palette behavior.
- Verify the chosen preview renderer under local desktop, remote desktop, virtual machine, and headless test environments as supported.
- Verify bundled and custom LabRecorder startup, existing-recorder connection, portable extraction paths, and isolated settings.

### BG-025: Add performance budgets

Define and enforce budgets for:

- Maximum GUI-thread stall during live operation.
- Maximum frame-display latency and event-queue growth.
- Memory use for representative short, one-hour, and multi-hour recordings.
- File-open progress responsiveness and cancellation latency.
- Shutdown responsiveness for every worker state.
- Diagnostic log and process-output buffer size.

## Documentation updates

Each milestone must update the relevant repository documentation:

- `behavior-contract.md`: externally visible state, selection, path, and shutdown rules.
- `runtime-state-machines.md`: recorder operations, close behavior, file loading, preflight, and verification states.
- `architecture.md`: shared configuration, controllers, services, loader/indexing, and renderer choice.
- `device-parity-runbook.md`: stream identity, rate health, calibration profiles, and post-recording verification evidence.
- `release-checklist.md`: configuration, packaging, accessibility, performance budgets, and supported shutdown cases.
- `README.md`: normal session workflow, recorder ownership, stream selection, playback, and calibration profiles.

## Recommended delivery sequence

1. Implement BG-001 and BG-002 together, including fake-recorder tests.
2. Implement BG-003 through BG-005 so lifecycle and ownership are reliable before adding orchestration.
3. Introduce the shared configuration in BG-006, then stream identity and recorded mapping in BG-007 and BG-008.
4. Complete selection and destination safety in BG-009 and BG-010.
5. Make loading and live delivery bounded through BG-011 to BG-013, then complete the renderer decision in BG-014.
6. Build the durable state display and preflight in BG-015 and BG-016 before the dashboard and one-click workflow.
7. Add playback, managed calibration, and automatic verification in BG-019 through BG-021.
8. Expand interaction, accessibility, packaging, and performance gates continuously rather than postponing them to the end.

## Definition of done

The plan is complete when:

- No recorder command race or pending-start close path can produce an unexplained final state.
- No GUI-thread path waits indefinitely for a worker or child process.
- Large recording load, playback, and cancellation stay within documented responsiveness and memory budgets.
- Live and recorded stream choices are deterministic and visible.
- The displayed destination is the exact validated destination used for recording.
- Recording, calibration, stream health, and errors have persistent, semantically distinct displays.
- Preflight and post-recording verification provide an auditable session result.
- The new configuration persists safely and established stream and timing contracts remain intact.
- Automated coverage exercises the required state, failure, performance, and packaging scenarios.
- User documentation and runtime state diagrams match the implemented behavior.
