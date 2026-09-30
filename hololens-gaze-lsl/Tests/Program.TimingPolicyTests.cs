using System;
using System.Collections.Generic;
using System.Reflection;
using GazeLSL;

internal static partial class Program
{
    private static void GazeTimingTakesDurationsInOneDomain()
    {
        // Nothing may turn SystemRelativeTime ticks into seconds, because on a
        // HoloLens 2 they drift away from Stopwatch by more every second.
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

        // A full catch-up batch (32 readings at 90 Hz) must fit in the queue's
        // time limit, or the queue would throw away readings just caught up.
        True(
            GazeTiming.MaxReadingsPerAcquire / 90.0 < GazeTiming.MaxBacklogSpanSeconds,
            "A full 90 Hz drain batch must fit inside the backlog span budget.");
    }

    private static void GazeTimingJudgesSeedAgeOnOneClock()
    {
        // The age must come from the SDK's own clock, because comparing a reading's
        // ticks with one of our timers once refused every good reading.
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

        // The reading for "now" can be taken one frame before or after we ask, so
        // being slightly ahead does not mean the clock is broken.
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

        // A step that runs on time lands between tracker frames, where asking only
        // fails inside the SDK on the device, so it must not ask at all.
        False(
            GazeDrainPolicy.CouldHaveNewerReading(1000.005, 1000.0, framePeriod, latency),
            "A step inside one frame of the last capture should not ask.");

        // The key stop rule: a reading just taken is already as old as the delivery
        // delay, so checking the frame time alone let one failing ask through on
        // every step.
        False(
            GazeDrainPolicy.CouldHaveNewerReading(
                1000.0 + latency, 1000.0, framePeriod, latency),
            "A step holding the reading the tracker has just published should stop.");
        True(
            GazeDrainPolicy.CouldHaveNewerReading(
                1000.0 + latency, 1000.0, framePeriod, 0.0),
            "Without the delay that same step asks, which is the regression.");

        // Not tested exactly at the edge, because rounding on large capture times
        // can land very slightly either side, and a step that just misses simply
        // asks on the next one, about 9 ms later.
        True(
            GazeDrainPolicy.CouldHaveNewerReading(
                1000.0 + (framePeriod + latency) * 1.01, 1000.0, framePeriod, latency),
            "A step a frame past the published reading should ask.");
        True(
            GazeDrainPolicy.CouldHaveNewerReading(1000.5, 1000.0, framePeriod, latency),
            "A late step should ask, and keep asking while it catches up.");

        // Broken input must not stop readings being fetched, so without a usable
        // delay the frame time alone is used, which asks too often rather than not
        // at all.
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

        // A reading picked up while catching up can be any age, and says nothing
        // about how quickly the tracker hands over a new one.
        policy.NoteReadingAge(0.200);
        Near(0.020, policy.PublicationLatencySeconds);

        policy.NoteReadingAge(0.012);
        Near(0.012, policy.PublicationLatencySeconds);

        policy.NoteReadingAge(double.NaN);
        policy.NoteReadingAge(double.PositiveInfinity);
        Near(0.012, policy.PublicationLatencySeconds);

        // A reading very slightly newer than the ask is a tiny clock difference, not
        // a negative delay.
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

        // Reported once, not on every failure while paused.
        False(
            policy.NoteFailedEmptyResult(now),
            "A suspended drain should not report itself suspended again.");
        Equal(1, policy.Suspensions);

        True(
            policy.IsSuspended(now + GazeDrainPolicy.SuspensionSeconds - 0.001),
            "The drain should stay down for the whole suspension.");

        // Empty results are normal before the tracker starts, so giving up for the
        // whole session would leave the run on a fallback that cannot keep up.
        False(
            policy.IsSuspended(now + GazeDrainPolicy.SuspensionSeconds),
            "The drain should be tried again once the suspension is over.");

        // The count starts over, so one failure after resuming does not pause again.
        False(
            policy.NoteFailedEmptyResult(now + 20.0),
            "A single failure after recovery should not suspend the drain.");

        policy.Reset();
        False(policy.IsSuspended(now), "A new tracker session should use the drain.");
        Equal(0, policy.Suspensions);
    }

    private static void GazeDrainReadingsDoNotForgiveSdkFailures()
    {
        // The count lasts until the next pause, not the next reading, because
        // counting only failures in a row once let a session leak thousands of SDK
        // objects with just eleven pauses.
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

        // Too few readings must not give a rate, or every run would look slow the
        // moment it starts.
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

        // The window must move with time, so a drop to 10 Hz shows up without
        // waiting for the tracker to restart.
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

        // Full speed with every ninth reading missing must not look like a
        // tracker that really runs at 10 Hz.
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
        // A caught-up batch must survive the queue limits, or catching up is pointless.
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
        // Capture times mix our LSL clock with the SDK's clock, so a batch can land
        // very slightly out of order, which is not a backlog and must not be dropped.
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

        // A span that cannot be worked out counts as over the limit: dropping one
        // old batch is better than a backlog that never ends.
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
