using System;
using System.Collections.Generic;
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
        Equal(
            (long)Math.Round(runtimeFrequency * 0.050, MidpointRounding.AwayFromZero),
            GazeTiming.MaxSeedCaptureAgeTicks);
        // A drained batch must fit the span budget, or acquisition would recover
        // readings only for the queue policy to throw them away again.
        True(
            GazeTiming.MaxReadingsPerAcquire * runtimeFrequency / 90L <
                GazeTiming.MaxBacklogSpanTicks,
            "A full 90 Hz drain batch must fit inside the backlog span budget.");
    }

    private static void GazeTimingRejectsStaleAndInvalidCaptures()
    {
        long now = GazeTiming.SystemRelativeTicksPerSecond;
        True(
            GazeTiming.IsFreshCaptureTimestamp(
                now - GazeTiming.MaxBacklogSpanTicks,
                now,
                GazeTiming.MaxBacklogSpanTicks),
            "A capture exactly at the freshness boundary should be accepted.");
        False(
            GazeTiming.IsFreshCaptureTimestamp(
                now - GazeTiming.MaxBacklogSpanTicks - 1L,
                now,
                GazeTiming.MaxBacklogSpanTicks),
            "A stalled tracker reading beyond the freshness boundary should be rejected.");
        False(
            GazeTiming.IsFreshCaptureTimestamp(0L, now, GazeTiming.MaxBacklogSpanTicks),
            "A nonpositive capture timestamp should be rejected.");
        False(
            GazeTiming.IsFreshCaptureTimestamp(-1L, now, GazeTiming.MaxBacklogSpanTicks),
            "A negative capture timestamp should be rejected.");
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
