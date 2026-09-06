using System;
using System.Collections.Generic;
using System.Reflection;
using GazeLSL;

internal static partial class Program
{
    private static void GazeTimingConvertsSystemRelativeTicks()
    {
        long runtimeFrequency = GazeTiming.SystemRelativeTicksPerSecond;
        Near(
            1.25,
            GazeTiming.SystemRelativeTicksToLslTimestamp(
                (long)Math.Round(runtimeFrequency * 1.25, MidpointRounding.AwayFromZero)));
        Equal(
            (long)Math.Round(runtimeFrequency * 0.500, MidpointRounding.AwayFromZero),
            GazeTiming.MaxBacklogSpanTicks);
        // A drained batch must fit the span budget, or acquisition would recover
        // readings only for the queue policy to throw them away again.
        True(
            GazeTiming.MaxReadingsPerAcquire * runtimeFrequency / 90L <
                GazeTiming.MaxBacklogSpanTicks,
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

        // A punctual step lands between tracker frames. Asking then costs a failed
        // SDK read on the device and returns nothing, so it must not ask at all.
        False(
            GazeDrainPolicy.CouldHaveNewerReading(1000.005, 1000.0, framePeriod),
            "A step inside one frame of the last capture should not ask.");
        // Not tested exactly at the boundary: capture times are LSL clock values
        // large enough that adding a frame period and subtracting it again lands a
        // hair either side. A step that misses by that much simply asks on the
        // next one, about 9 ms later.
        True(
            GazeDrainPolicy.CouldHaveNewerReading(
                1000.0 + framePeriod * 1.01, 1000.0, framePeriod),
            "A step a full frame after the last capture should ask.");
        True(
            GazeDrainPolicy.CouldHaveNewerReading(1000.5, 1000.0, framePeriod),
            "A late step should ask, and keep asking while it catches up.");

        // Nothing usable to reason from must not stall acquisition.
        True(
            GazeDrainPolicy.CouldHaveNewerReading(1000.005, 1000.0, 0.0),
            "An unknown frame period should not block the drain.");
        True(
            GazeDrainPolicy.CouldHaveNewerReading(double.NaN, 1000.0, framePeriod),
            "An unusable query time should not block the drain.");
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

        // The count starts over, so one failure after recovery does not re-suspend.
        False(
            policy.NoteFailedEmptyResult(now + 20.0),
            "A single failure after recovery should not suspend the drain.");

        // A reading forgives the run of failures leading up to it.
        policy.NoteFailedEmptyResult(now + 21.0);
        policy.NoteReading();
        False(
            policy.NoteFailedEmptyResult(now + 22.0),
            "A reading should clear the failures counted before it.");
        False(policy.IsSuspended(now + 22.0), "The drain should still be in use.");

        policy.Reset();
        False(policy.IsSuspended(now), "A new tracker session should use the drain.");
        Equal(0, policy.Suspensions);
    }

    private static void GazeRateEstimatorMeasuresDeliveredRate()
    {
        long frequency = GazeTiming.SystemRelativeTicksPerSecond;
        var estimator = new GazeRateEstimator();
        double rate;

        False(estimator.TryGetRate(out rate), "An empty estimator has no rate.");

        // Fewer readings than the minimum must not produce a rate, or a run would
        // be judged low the instant it starts.
        long ticks = frequency;
        for (int i = 0; i < 8; i++)
        {
            estimator.Add(ticks);
            ticks += frequency / 90L;
        }
        False(estimator.TryGetRate(out rate), "A partial window has no rate.");

        estimator = new GazeRateEstimator();
        ticks = frequency;
        for (int i = 0; i < GazeRateEstimator.WindowSize; i++)
        {
            estimator.Add(ticks);
            ticks += frequency / 90L;
        }
        True(estimator.TryGetRate(out rate), "A full window should produce a rate.");
        True(Math.Abs(rate - 90.0) < 0.5, $"Expected about 90 Hz, got {rate}.");

        // The window must slide, so a collapse to 10 Hz is visible without waiting
        // for the tracker session to restart.
        for (int i = 0; i < GazeRateEstimator.WindowSize; i++)
        {
            estimator.Add(ticks);
            ticks += frequency / 10L;
        }
        True(estimator.TryGetRate(out rate), "A refilled window should produce a rate.");
        True(Math.Abs(rate - 10.0) < 0.5, $"Expected about 10 Hz, got {rate}.");

        estimator.Reset();
        False(estimator.TryGetRate(out rate), "Reset should clear the window.");
    }

    private static void GazeRateEstimatorSeparatesSlowTrackerFromLostReadings()
    {
        long frequency = GazeTiming.SystemRelativeTicksPerSecond;
        double minMilliseconds;
        double maxMilliseconds;

        var slow = new GazeRateEstimator();
        long ticks = frequency;
        for (int i = 0; i < GazeRateEstimator.WindowSize; i++)
        {
            slow.Add(ticks);
            ticks += frequency / 10L;
        }
        True(slow.TryGetIntervalSummary(out minMilliseconds, out maxMilliseconds),
            "A full window should summarise its capture gaps.");
        True(Math.Abs(minMilliseconds - 100.0) < 1.0, $"Expected a 100 ms floor, got {minMilliseconds}.");
        True(Math.Abs(maxMilliseconds - 100.0) < 1.0, $"Expected a 100 ms ceiling, got {maxMilliseconds}.");

        // Full-rate captures with every ninth reading missing must not look like a
        // tracker that is genuinely publishing at 10 Hz.
        var lossy = new GazeRateEstimator();
        ticks = frequency;
        for (int i = 0; i < GazeRateEstimator.WindowSize; i++)
        {
            lossy.Add(ticks);
            ticks += (i % 9 == 0) ? frequency / 10L : frequency / 90L;
        }
        True(lossy.TryGetIntervalSummary(out minMilliseconds, out maxMilliseconds),
            "A lossy window should summarise its capture gaps.");
        True(Math.Abs(minMilliseconds - 11.1) < 1.0, $"Expected an 11 ms floor, got {minMilliseconds}.");
        True(Math.Abs(maxMilliseconds - 100.0) < 1.0, $"Expected a 100 ms ceiling, got {maxMilliseconds}.");
    }

    private static void GazeBacklogKeepsDrainedBatch()
    {
        // A recovered batch must survive the queue policy, or draining is pointless.
        long frequency = GazeTiming.SystemRelativeTicksPerSecond;
        long step = frequency / 90L;
        var queue = new Queue<long>();

        for (int i = 0; i < GazeTiming.MaxReadingsPerAcquire; i++)
        {
            GazeBacklogPolicy.Enqueue(
                queue,
                i * step,
                ticks => ticks,
                360,
                GazeTiming.MaxBacklogSpanTicks);
        }

        Equal(GazeTiming.MaxReadingsPerAcquire, queue.Count);
        Equal(0L, queue.Peek());
    }

    private static void GazeBacklogDropsStaleSamples()
    {
        var queue = new Queue<long>();
        GazeBacklogPolicy.Enqueue(
            queue,
            0L,
            ticks => ticks,
            10,
            GazeTiming.MaxBacklogSpanTicks);
        GazeBacklogPolicy.Enqueue(
            queue,
            GazeTiming.MaxBacklogSpanTicks,
            ticks => ticks,
            10,
            GazeTiming.MaxBacklogSpanTicks);
        Equal(2, queue.Count);

        GazeBacklogPolicy.Enqueue(
            queue,
            GazeTiming.MaxBacklogSpanTicks + 1L,
            ticks => ticks,
            10,
            GazeTiming.MaxBacklogSpanTicks);
        Equal(1, queue.Count);
        Equal(GazeTiming.MaxBacklogSpanTicks + 1L, queue.Peek());

        queue.Enqueue(520_000L);
        True(
            GazeBacklogPolicy.CollapseIfOverSpan(
                queue,
                ticks => ticks,
                GazeTiming.MaxBacklogSpanTicks),
            "A delayed consumer should collapse an already stale queue.");
        Equal(1, queue.Count);
        Equal(520_000L, queue.Peek());
    }
}
