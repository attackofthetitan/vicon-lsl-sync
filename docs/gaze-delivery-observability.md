# Gaze delivery observability

The HoloLens gaze LSL outlet can exist before the first gaze sample is available. The Unity log therefore reports the delivery state separately from outlet creation.

The publisher exposes four states:

- `WaitingForProviderSample`: the worker is polling, but the provider has not returned a gaze sample yet.
- `RejectingInvalidTimestamp`: provider samples are arriving, but the latest sample had an invalid capture timestamp and was not pushed to LSL.
- `PublishingSamplesWithoutValidRays`: LSL samples are being pushed, but the latest published sample had no valid combined, left-eye, or right-eye ray.
- `PublishingValidGaze`: the latest published sample contained at least one valid gaze ray.

Empty provider polls are counted but do not overwrite the last real sample classification, because the publisher intentionally polls faster than the nominal tracker rate to drain backlog.

Unity reports state transitions immediately, except that the normal initial waiting state gets a one-second grace period. Non-healthy states repeat every five seconds with cumulative counters. A valid-gaze transition is logged once.
