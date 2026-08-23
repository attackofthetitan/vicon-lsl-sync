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
            (long)Math.Round(runtimeFrequency * 0.025, MidpointRounding.AwayFromZero),
            GazeTiming.MaxBacklogSpanTicks);
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
