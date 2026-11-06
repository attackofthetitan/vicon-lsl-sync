# Reading the gaze delivery log

The gaze stream can be open before any gaze arrives. So the Unity log reports
how gaze is flowing separately from whether the stream is open.

## The four states

- `WaitingForProviderSample`: the worker is asking for gaze, but nothing has
  arrived yet.
- `RejectingInvalidTimestamp`: gaze is arriving, but the latest sample had a bad
  capture time and wasn't sent.
- `PublishingSamplesWithoutValidRays`: samples are going out, but the latest one
  had no usable ray.
- `PublishingValidGaze`: the latest sample had at least one usable ray.

An empty answer is counted but doesn't change the state. The worker asks more
often than the tracker makes readings, on purpose, so lots of empty answers are
normal.

A new state is logged right away. The one exception is "waiting" at startup,
which gets one second to clear first. Any state other than `PublishingValidGaze`
is logged again every five seconds with running totals.

## The counters

There are three places a reading can be lost, and they all look the same from
outside (`pushed 0 samples`):

1. getting it from the SDK,
2. moving it into the Unity world on Unity's main thread,
3. handing it to the publisher.

So when the state isn't `PublishingValidGaze`, a second line follows with
counts for each step:

- **Reading at the current time**: asks, empty answers, and readings that were
  too old. This is how a session starts, and how readings come in while catching
  up is paused.
- **Catching up** ("Drain" in the log): asks, readings, empty answers, failures
  inside the SDK, steps skipped as too soon, and whether catching up is paused
  and how often it has been.
- **Accepted**: readings accepted, readings refused for not being newer, and
  readings waiting to be converted.
- **Conversion**: Unity passes, readings converted, headset position lookups
  that failed, readings dropped from an old tracker session, and samples waiting
  to be sent.
- **Age of the last reading** on the SDK's clock. This tells a reading from a
  moment ago apart from one from minutes ago.
- **Delivery delay**: how long the tracker takes to hand over a new reading.
  Catching up waits this long plus one frame before asking. If it stays at zero,
  catching up will fail on every step.

Read them in order:

- Nothing accepted: the SDK isn't handing over readings.
- Accepted but nothing converted: Unity's main thread isn't converting them. If
  the pass count is zero, `Update` isn't running on the gaze reader at all.
- Converted but still waiting: the publisher isn't picking them up.

The counters reset with the queues whenever the tracker is found again.
