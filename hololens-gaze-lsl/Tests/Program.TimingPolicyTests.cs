using System;
using System.Collections.Generic;
using System.Reflection;
using GazeLSL;

internal static partial class Program
{
    private static void GazeTimingTakesDurationsInOneDomain()
    {
        // A duration must never be taken from SystemRelativeTime ticks. Their rate
        // is not the rate of any timer this runtime offers: on a HoloLens 2 the age
        // of one reading came out as 0.020 s on the SDK's own clock and -231 s
        // against Stopwatch, and the two drifted apart by seconds over seconds.
        // Every member that made that conversion available is gone.
        foreach (string name in new[]
                 {
                     "SystemRelativeTicksPerSecond",
                     "SystemRelativeTicksToLslTimestamp",
                     "CurrentSystemRelativeTimeTicks",
                     "MaxBacklogSpanTicks"
                 })
        {
            True(
                typeof(GazeTiming).GetMember(
                    name,
                    BindingFlags.Public | BindingFlags.NonPublic |
                    BindingFlags.Static | BindingFlags.Instance).Length == 0,
                $"GazeTiming.{name} turns SDK ticks into a duration.");
        }

        // A drained batch must fit the span budget, or acquisition would recover
        // readings only for the queue policy to throw them away again. In seconds
        // this is the plain arithmetic it always read as: 32 readings at 90 Hz.
        True(
            GazeTiming.MaxReadingsPerAcquire / 90.0 < GazeTiming.MaxBacklogSpanSeconds,
            "A full 90 Hz drain batch must fit inside the backlog span budget.");
    }

    private static void GazeTimingJudgesSeedAgeOnOneClock()
    {
        // The age must be taken in the SDK's own wall-clock domain. Comparing the
        // reading's tick count against a timer of ours rejected every reading the
        // tracker offered, on a device where nothing was wrong with the readings.
        foreach (string name in new[] { "IsFreshCaptureTimestamp", "MaxSeedCaptureAgeTicks" })
        {
            True(
                typeof(GazeTiming).GetMember(
                    name,
                    BindingFlags.Public | BindingFlags.NonPublic |
                    BindingFlags.Static | BindingFlags.Instance).Length == 0,
                $"GazeTiming.{name} judges a reading's age across two clocks.");
        }

        True(
            GazeTiming.IsFreshSeedAge(0.0),
            "A reading captured at the query time should be accepted.");
        True(
            GazeTiming.IsFreshSeedAge(GazeTiming.MaxSeedCaptureAgeSeconds),
            "A reading exactly at the age boundary should be accepted.");
        False(
            GazeTiming.IsFreshSeedAge(GazeTiming.MaxSeedCaptureAgeSeconds + 0.001),
            "A stalled tracker reading beyond the boundary should be rejected.");

        // The reading returned for "now" can be captured a frame either side of the
        // query, so a small lead must not be read as a broken clock.
        True(
            GazeTiming.IsFreshSeedAge(-0.011),
            "A reading one 90 Hz frame ahead of the query should be accepted.");
        False(
            GazeTiming.IsFreshSeedAge(-GazeTiming.MaxSeedCaptureAgeSeconds - 0.001),
            "A reading far ahead of the query should be rejected.");

        False(GazeTiming.IsFreshSeedAge(double.NaN), "An unusable age should be rejected.");
        False(
            GazeTiming.IsFreshSeedAge(double.PositiveInfinity),
            "An infinite age should be rejected.");
    }

    private static void GazeReadingGateRejectsDuplicateAndRegression()
    {
        var gate = new GazeReadingGate();
        True(gate.TryAccept(100L), "The first SDK reading should be accepted.");
        False(gate.TryAccept(100L), "A duplicate SDK tick should be rejected.");
        False(gate.TryAccept(99L), "A regressing SDK tick should be rejected.");
        True(gate.TryAccept(101L), "A newer SDK tick should be accepted.");

        gate.Reset();
        True(gate.TryAccept(1L), "Reset should allow a new tracker clock sequence.");
    }

    private static void GazeReadingGateExposesDrainCursor()
    {
        var gate = new GazeReadingGate();
        False(gate.HasReading, "A fresh gate has no cursor to drain from.");

        True(gate.TryAccept(500L), "The first SDK reading should be accepted.");
        True(gate.HasReading, "Accepting a reading should establish the cursor.");
        Equal(500L, gate.LastTimestampTicks);

        False(gate.TryAccept(500L), "A duplicate SDK tick should be rejected.");
        Equal(500L, gate.LastTimestampTicks);

        True(gate.TryAccept(511L), "A newer SDK tick should be accepted.");
        Equal(511L, gate.LastTimestampTicks);

        gate.Reset();
        False(gate.HasReading, "Reset should clear the cursor with the sequence.");
        Equal(0L, gate.LastTimestampTicks);
    }

    private static void GazeDrainAsksOnlyWhenAReadingCouldExist()
    {
        double framePeriod = 1.0 / 90.0;
        double latency = 0.020;

        // A punctual step lands between tracker frames. Asking then costs a failed
        // SDK read on the device and returns nothing, so it must not ask at all.
        False(
            GazeDrainPolicy.CouldHaveNewerReading(1000.005, 1000.0, framePeriod, latency),
            "A step inside one frame of the last capture should not ask.");

        // The stop rule that matters. A reading the drain has just taken is already
        // as old as the delay the tracker publishes at, so measured against the
        // frame period alone it always looked stale enough to justify one more ask
        // -- the ask that cannot be answered, thrown and leaked once per drain step
        // for a whole session.
        False(
            GazeDrainPolicy.CouldHaveNewerReading(
                1000.0 + latency, 1000.0, framePeriod, latency),
            "A step holding the reading the tracker has just published should stop.");
        True(
            GazeDrainPolicy.CouldHaveNewerReading(
                1000.0 + latency, 1000.0, framePeriod, 0.0),
            "Without the delay that same step asks, which is the regression.");

        // Not tested exactly at the boundary: capture times are LSL clock values
        // large enough that adding a frame period and subtracting it again lands a
        // hair either side. A step that misses by that much simply asks on the
        // next one, about 9 ms later.
        True(
            GazeDrainPolicy.CouldHaveNewerReading(
                1000.0 + (framePeriod + latency) * 1.01, 1000.0, framePeriod, latency),
            "A step a frame past the published reading should ask.");
        True(
            GazeDrainPolicy.CouldHaveNewerReading(1000.5, 1000.0, framePeriod, latency),
            "A late step should ask, and keep asking while it catches up.");

        // Nothing usable to reason from must not stall acquisition. An unusable
        // delay leaves the frame period on its own, which asks too eagerly rather
        // than not at all.
        True(
            GazeDrainPolicy.CouldHaveNewerReading(1000.005, 1000.0, 0.0, latency),
            "An unknown frame period should not block the drain.");
        True(
            GazeDrainPolicy.CouldHaveNewerReading(
                double.NaN, 1000.0, framePeriod, latency),
            "An unusable query time should not block the drain.");
        True(
            GazeDrainPolicy.CouldHaveNewerReading(
                1000.0 + latency, 1000.0, framePeriod, double.NaN),
            "An unusable delay should not block the drain.");
        True(
            GazeDrainPolicy.CouldHaveNewerReading(
                1000.0 + latency, 1000.0, framePeriod, double.PositiveInfinity),
            "An infinite delay should not block the drain.");
        True(
            GazeDrainPolicy.CouldHaveNewerReading(
                1000.0 + latency, 1000.0, framePeriod, -1.0),
            "A negative delay should not block the drain.");
    }

    private static void GazeDrainLearnsHowLateAReadingArrives()
    {
        var policy = new GazeDrainPolicy();
        Equal(0.0, policy.PublicationLatencySeconds);

        policy.NoteReadingAge(0.020);
        Near(0.020, policy.PublicationLatencySeconds);

        // A reading recovered by a drain that is catching up is arbitrarily old and
        // says nothing about how quickly the tracker parts with a new one.
        policy.NoteReadingAge(0.200);
        Near(0.020, policy.PublicationLatencySeconds);

        policy.NoteReadingAge(0.012);
        Near(0.012, policy.PublicationLatencySeconds);

        policy.NoteReadingAge(double.NaN);
        policy.NoteReadingAge(double.PositiveInfinity);
        Near(0.012, policy.PublicationLatencySeconds);

        // A reading offered a hair ahead of the query is jitter between the two
        // wall clocks behind a capture time, not a negative delay.
        policy.NoteReadingAge(-0.001);
        Equal(0.0, policy.PublicationLatencySeconds);

        policy.NoteReadingAge(0.020);
        policy.Reset();
        Equal(0.0, policy.PublicationLatencySeconds);
    }

    private static void GazeDrainIsSuspendedAndRecoversAfterTheSdkFailsToReportEmpty()
    {
        var policy = new GazeDrainPolicy();
        double now = 1000.0;
        False(policy.IsSuspended(now), "A fresh policy should use the drain.");

        for (int i = 1; i < GazeDrainPolicy.FailedEmptyResultsBeforeSuspend; i++)
        {
            False(
                policy.NoteFailedEmptyResult(now),
                "The drain should survive the first few SDK failures.");
            False(policy.IsSuspended(now), "The drain should not be suspended yet.");
        }

        True(
            policy.NoteFailedEmptyResult(now),
            "The failure that reaches the limit should suspend the drain.");
        True(policy.IsSuspended(now), "The drain should be suspended.");
        Equal(1, policy.Suspensions);

        // Announced once, not on every failure while it stays down.
        False(
            policy.NoteFailedEmptyResult(now),
            "A suspended drain should not report itself suspended again.");
        Equal(1, policy.Suspensions);

        True(
            policy.IsSuspended(now + GazeDrainPolicy.SuspensionSeconds - 0.001),
            "The drain should stay down for the whole suspension.");

        // The tracker publishing nothing yet is exactly when empty results happen,
        // and it starts publishing later. Giving the drain up for the session would
        // leave the run on a fallback that cannot keep up.
        False(
            policy.IsSuspended(now + GazeDrainPolicy.SuspensionSeconds),
            "The drain should be tried again once the suspension is over.");

        // The budget starts over, so one failure after recovery does not re-suspend.
        False(
            policy.NoteFailedEmptyResult(now + 20.0),
            "A single failure after recovery should not suspend the drain.");

        policy.Reset();
        False(policy.IsSuspended(now), "A new tracker session should use the drain.");
        Equal(0, policy.Suspensions);
    }

    private static void GazeDrainReadingsDoNotForgiveSdkFailures()
    {
        // A drain that reads one reading and then fails never accumulates two
        // failures in a row. Counting consecutively, a session took thousands of
        // failures -- each one a leaked SDK object whose finalizer throws -- and
        // suspended the drain eleven times. The budget runs to the next suspension,
        // not to the next reading.
        var policy = new GazeDrainPolicy();
        double now = 1000.0;

        for (int i = 1; i < GazeDrainPolicy.FailedEmptyResultsBeforeSuspend; i++)
        {
            policy.NoteReadingAge(0.020);
            False(
                policy.NoteFailedEmptyResult(now + i),
                "The drain should survive the first few SDK failures.");
        }

        policy.NoteReadingAge(0.020);
        True(
            policy.NoteFailedEmptyResult(now + GazeDrainPolicy.FailedEmptyResultsBeforeSuspend),
            "A drain reading one reading per failed ask should still be suspended.");
        Equal(1, policy.Suspensions);
    }

    private static void GazeRateEstimatorMeasuresDeliveredRate()
    {
        var estimator = new GazeRateEstimator();
        double rate;

        False(estimator.TryGetRate(out rate), "An empty estimator has no rate.");

        // Fewer readings than the minimum must not produce a rate, or a run would
        // be judged low the instant it starts.
        double captureTime = 1000.0;
        for (int i = 0; i < 8; i++)
        {
            estimator.Add(captureTime);
            captureTime += 1.0 / 90.0;
        }
        False(estimator.TryGetRate(out rate), "A partial window has no rate.");

        estimator = new GazeRateEstimator();
        captureTime = 1000.0;
        for (int i = 0; i < GazeRateEstimator.WindowSize; i++)
        {
            estimator.Add(captureTime);
            captureTime += 1.0 / 90.0;
        }
        True(estimator.TryGetRate(out rate), "A full window should produce a rate.");
        True(Math.Abs(rate - 90.0) < 0.5, $"Expected about 90 Hz, got {rate}.");

        // The window must slide, so a collapse to 10 Hz is visible without waiting
        // for the tracker session to restart.
        for (int i = 0; i < GazeRateEstimator.WindowSize; i++)
        {
            estimator.Add(captureTime);
            captureTime += 1.0 / 10.0;
        }
        True(estimator.TryGetRate(out rate), "A refilled window should produce a rate.");
        True(Math.Abs(rate - 10.0) < 0.5, $"Expected about 10 Hz, got {rate}.");

        estimator.Reset();
        False(estimator.TryGetRate(out rate), "Reset should clear the window.");
    }

    private static void GazeRateEstimatorSeparatesSlowTrackerFromLostReadings()
    {
        double minMilliseconds;
        double maxMilliseconds;

        var slow = new GazeRateEstimator();
        double captureTime = 1000.0;
        for (int i = 0; i < GazeRateEstimator.WindowSize; i++)
        {
            slow.Add(captureTime);
            captureTime += 1.0 / 10.0;
        }
        True(slow.TryGetIntervalSummary(out minMilliseconds, out maxMilliseconds),
            "A full window should summarise its capture gaps.");
        True(Math.Abs(minMilliseconds - 100.0) < 1.0, $"Expected a 100 ms floor, got {minMilliseconds}.");
        True(Math.Abs(maxMilliseconds - 100.0) < 1.0, $"Expected a 100 ms ceiling, got {maxMilliseconds}.");

        // Full-rate captures with every ninth reading missing must not look like a
        // tracker that is genuinely publishing at 10 Hz.
        var lossy = new GazeRateEstimator();
        captureTime = 1000.0;
        for (int i = 0; i < GazeRateEstimator.WindowSize; i++)
        {
            lossy.Add(captureTime);
            captureTime += (i % 9 == 0) ? 1.0 / 10.0 : 1.0 / 90.0;
        }
        True(lossy.TryGetIntervalSummary(out minMilliseconds, out maxMilliseconds),
            "A lossy window should summarise its capture gaps.");
        True(Math.Abs(minMilliseconds - 11.1) < 1.0, $"Expected an 11 ms floor, got {minMilliseconds}.");
        True(Math.Abs(maxMilliseconds - 100.0) < 1.0, $"Expected a 100 ms ceiling, got {maxMilliseconds}.");
    }

    private static void GazeBacklogKeepsDrainedBatch()
    {
        // A recovered batch must survive the queue policy, or draining is pointless.
        double step = 1.0 / 90.0;
        var queue = new Queue<double>();

        for (int i = 0; i < GazeTiming.MaxReadingsPerAcquire; i++)
        {
            GazeBacklogPolicy.Enqueue(
                queue,
                1000.0 + i * step,
                captureTime => captureTime,
                360,
                GazeTiming.MaxBacklogSpanSeconds);
        }

        Equal(GazeTiming.MaxReadingsPerAcquire, queue.Count);
        Near(1000.0, queue.Peek());
    }

    private static void GazeBacklogDropsStaleSamples()
    {
        var queue = new Queue<double>();
        GazeBacklogPolicy.Enqueue(
            queue,
            1000.0,
            captureTime => captureTime,
            10,
            GazeTiming.MaxBacklogSpanSeconds);
        GazeBacklogPolicy.Enqueue(
            queue,
            1000.0 + GazeTiming.MaxBacklogSpanSeconds,
            captureTime => captureTime,
            10,
            GazeTiming.MaxBacklogSpanSeconds);
        Equal(2, queue.Count);

        GazeBacklogPolicy.Enqueue(
            queue,
            1000.0 + GazeTiming.MaxBacklogSpanSeconds + 0.001,
            captureTime => captureTime,
            10,
            GazeTiming.MaxBacklogSpanSeconds);
        Equal(1, queue.Count);
        Near(1000.0 + GazeTiming.MaxBacklogSpanSeconds + 0.001, queue.Peek());

        queue.Enqueue(1002.0);
        True(
            GazeBacklogPolicy.CollapseIfOverSpan(
                queue,
                captureTime => captureTime,
                GazeTiming.MaxBacklogSpanSeconds),
            "A delayed consumer should collapse an already stale queue.");
        Equal(1, queue.Count);
        Near(1002.0, queue.Peek());
    }

    private static void GazeBacklogToleratesJitterButNotAnUnusableSpan()
    {
        // Capture times come from the LSL clock read to fetch a reading and the
        // wall clock the SDK stamped it with, so consecutive batches can land a
        // hair out of order. That is not a backlog, and dropping one for it would
        // discard readings that are perfectly fresh.
        var jittery = new Queue<double>();
        jittery.Enqueue(1000.010);
        jittery.Enqueue(1000.009);
        False(
            GazeBacklogPolicy.CollapseIfOverSpan(
                jittery,
                captureTime => captureTime,
                GazeTiming.MaxBacklogSpanSeconds),
            "A capture time a hair out of order is within budget.");
        Equal(2, jittery.Count);

        // A span that cannot be judged at all is treated as over budget: one stale
        // batch is the cost of dropping it, an unbounded backlog the cost of not.
        var unusable = new Queue<double>();
        unusable.Enqueue(1000.0);
        unusable.Enqueue(double.NaN);
        True(
            GazeBacklogPolicy.CollapseIfOverSpan(
                unusable,
                captureTime => captureTime,
                GazeTiming.MaxBacklogSpanSeconds),
            "An unusable capture time should collapse the queue.");
        Equal(1, unusable.Count);
    }
}
