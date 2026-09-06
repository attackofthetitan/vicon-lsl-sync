# Gaze delivery observability

The HoloLens gaze LSL outlet can exist before the first gaze sample is available. The Unity log therefore reports the delivery state separately from outlet creation.

The publisher exposes four states:

- `WaitingForProviderSample`: the worker is polling, but the provider has not returned a gaze sample yet.
- `RejectingInvalidTimestamp`: provider samples are arriving, but the latest sample had an invalid capture timestamp and was not pushed to LSL.
- `PublishingSamplesWithoutValidRays`: LSL samples are being pushed, but the latest published sample had no valid combined, left-eye, or right-eye ray.
- `PublishingValidGaze`: the latest published sample contained at least one valid gaze ray.

Empty provider polls are counted but do not overwrite the last real sample classification, because the publisher intentionally polls faster than the nominal tracker rate to drain backlog.

Unity reports state transitions immediately, except that the normal initial waiting state gets a one-second grace period. Non-healthy states repeat every five seconds with cumulative counters. A valid-gaze transition is logged once.

## Acquisition counters

Delivery state names the stage the outlet can see. It cannot name which stage
inside the provider lost the reading, and there are three, each able to discard
every reading while leaving the same `pushed 0 samples` line behind it: taking a
reading from the SDK, converting it to world space on the Unity main thread, and
handing it to the publisher.

So every report of a state other than `PublishingValidGaze` is followed by a
second line counting all three:

- Reading at the current time: asked, empty, too old. This is how a session starts
  and how it acquires once the drain has been abandoned.
- Drain: asked, read, empty, failed inside the SDK, skipped as too soon, whether
  it is currently suspended, and how many times it has been.
- Accepted, not newer than the last accepted reading, and waiting to convert.
- Conversion: passes, converted, locate failures, dropped as a stale tracker
  session, and waiting to publish.
- The age of the last reading the SDK offered, measured twice: on the SDK's own
  wall clock, and on the device timer against the reading's tick count, with that
  timer's frequency. Only the first decides anything. The second is there because
  a reading captured moments ago and one captured minutes ago are the same
  rejection from outside, and the two ages disagreeing says the device timer is
  not the clock behind `SystemRelativeTime`.

Read them as a chain. Nothing accepted means the SDK is not handing over readings.
Readings accepted with nothing converted means the Unity main thread is not
converting them, and a conversion pass count of zero means `Update` is not running
on the provider at all. Samples converted and waiting to publish means the
publisher is not taking them.

The counters follow the tracker session: re-enumerating the tracker resets them
along with the queues and the reading gate.
