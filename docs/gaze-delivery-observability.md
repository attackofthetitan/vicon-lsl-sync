# Reading the gaze delivery log

The HoloLens gaze stream can be open before any gaze has arrived. So the Unity log reports how gaze is flowing separately from whether the stream was opened.

The publisher is always in one of four states:

- `WaitingForProviderSample`: the worker is asking for gaze, but none has arrived yet.
- `RejectingInvalidTimestamp`: gaze is arriving, but the latest sample had a bad capture time and was not sent.
- `PublishingSamplesWithoutValidRays`: samples are being sent, but the latest one had no usable ray for either eye or both eyes combined.
- `PublishingValidGaze`: the latest sample sent had at least one usable ray.

When the worker asks and gets nothing, that is counted, but it does not change the state. The worker asks more often than the tracker makes readings on purpose, so it can catch up on readings that are waiting, and many of those asks will come back empty.

Unity logs a change of state straight away. The one exception is the normal "waiting" state at startup, which gets one second to clear first. Any state other than `PublishingValidGaze` is logged again every five seconds, with running totals. Reaching `PublishingValidGaze` is logged once.

## Reading counters

The state only tells you what the stream can see. It cannot tell you where
inside the gaze reader a reading was lost. There are three places where every
reading could be thrown away, and each would leave the same `pushed 0 samples`
line in the log:

1. getting a reading from the SDK,
2. moving it into the Unity world on Unity's main thread,
3. handing it to the publisher.

So whenever the state is anything other than `PublishingValidGaze`, a second log
line follows with counts for all three:

- **Reading at the current time**: how many times it asked, how many came back
  empty, and how many were too old. This is how a session starts, and how the app
  gets readings while catching up is paused.
- **Catching up** ("Drain" in the log): how many times it asked, how many readings it got, how many
  were empty, how many failed inside the SDK, how many steps were skipped as too
  soon, whether catching up is paused right now, and how many times it has been
  paused.
- **Accepted**: how many readings were accepted, how many were refused for not
  being newer than the last one, and how many are waiting to be converted.
- **Conversion**: how many passes Unity made, how many readings it converted,
  how many times it could not find the headset position, how many were dropped
  for belonging to an old tracker session, and how many are waiting to be sent.
- **Age of the last reading** the SDK offered, on the SDK's own clock. From the
  outside, a reading taken a moment ago and one taken minutes ago get refused the
  same way, so this tells them apart.
- **Delivery delay**: how long after taking a reading the tracker hands it over,
  measured as the youngest age any reading has been offered at in this session.
  Catching up waits this long plus one frame before asking again. So if the delay
  shows zero for a long time, that explains why catching up would fail on every
  step.

The headset's own timer is no longer in this line. It was there to test whether
`Stopwatch` is the clock behind `SystemRelativeTime`. It is not: the two tick at
different speeds and start from different points. Nothing on the gaze path
measures time with those ticks any more.

Read the counters in order:

- Nothing accepted means the SDK is not handing over readings.
- Readings accepted but nothing converted means Unity's main thread is not
  converting them. If the conversion pass count is zero, `Update` is not running
  on the gaze reader at all.
- Samples converted and waiting to be sent means the publisher is not picking
  them up.

The counters belong to the tracker session. When the tracker is found again, they
reset along with the queues and the reading-time check.
