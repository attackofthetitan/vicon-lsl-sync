using System;
using System.Collections.Generic;
using System.Diagnostics;

namespace GazeLSL
{
    // The Extended Eye Tracking SDK exposes SystemRelativeTime as a TimeSpan,
    // but its Ticks value is the system-relative QPC count used to locate the
    // tracker pose. QPC frequency is device-dependent, so never assume the
    // TimeSpan 10 MHz tick rate when converting these values to seconds.
    internal static class GazeTiming
    {
        public static readonly long SystemRelativeTicksPerSecond = Stopwatch.Frequency;
        public static readonly long MaxBacklogSpanTicks =
            (long)Math.Round(Stopwatch.Frequency * 0.025, MidpointRounding.AwayFromZero);
        private static readonly long MaximumFutureLeadTicks =
            (long)Math.Round(Stopwatch.Frequency * 0.001, MidpointRounding.AwayFromZero);

        public static long CurrentSystemRelativeTimeTicks()
        {
            return Stopwatch.GetTimestamp();
        }

        public static double SystemRelativeTicksToLslTimestamp(long systemRelativeTimeTicks)
        {
            return systemRelativeTimeTicks / (double)Stopwatch.Frequency;
        }

        public static bool IsFreshCaptureTimestamp(
            long captureTicks,
            long queryTicks,
            long maximumAgeTicks)
        {
            if (captureTicks <= 0L || queryTicks <= 0L || maximumAgeTicks < 0L)
            {
                return false;
            }

            if (captureTicks > queryTicks)
            {
                return captureTicks - queryTicks <= MaximumFutureLeadTicks;
            }

            return queryTicks - captureTicks <= maximumAgeTicks;
        }
    }

    // Tracks the integer SDK timestamp, rather than a floating-point or wall
    // clock representation. A new tracker session calls Reset so readings
    // from separate tracker lifecycles are never compared.
    internal sealed class GazeReadingGate
    {
        private bool hasLastTimestamp;
        private long lastTimestampTicks;

        public bool TryAccept(long systemRelativeTimeTicks)
        {
            if (hasLastTimestamp && systemRelativeTimeTicks <= lastTimestampTicks)
            {
                return false;
            }

            hasLastTimestamp = true;
            lastTimestampTicks = systemRelativeTimeTicks;
            return true;
        }

        public void Reset()
        {
            hasLastTimestamp = false;
            lastTimestampTicks = 0L;
        }
    }

    // A queue may contain a normal small batch, but it must never retain a
    // batch whose capture-time span exceeds the freshness budget.  The newest
    // item is retained so a delayed consumer resumes at the current pose.
    internal static class GazeBacklogPolicy
    {
        public static void Enqueue<T>(
            Queue<T> queue,
            T item,
            Func<T, long> timestampSelector,
            int maximumCount,
            long maximumSpanTicks)
        {
            if (queue == null) throw new ArgumentNullException(nameof(queue));
            if (timestampSelector == null)
            {
                throw new ArgumentNullException(nameof(timestampSelector));
            }
            if (maximumCount <= 0) throw new ArgumentOutOfRangeException(nameof(maximumCount));
            if (maximumSpanTicks < 0L)
            {
                throw new ArgumentOutOfRangeException(nameof(maximumSpanTicks));
            }

            while (queue.Count >= maximumCount)
            {
                queue.Dequeue();
            }

            if (queue.Count > 0 &&
                SpanExceedsLimit(queue, item, timestampSelector, maximumSpanTicks))
            {
                queue.Clear();
            }

            queue.Enqueue(item);
        }

        public static bool CollapseIfOverSpan<T>(
            Queue<T> queue,
            Func<T, long> timestampSelector,
            long maximumSpanTicks)
        {
            if (queue == null) throw new ArgumentNullException(nameof(queue));
            if (timestampSelector == null)
            {
                throw new ArgumentNullException(nameof(timestampSelector));
            }
            if (maximumSpanTicks < 0L)
            {
                throw new ArgumentOutOfRangeException(nameof(maximumSpanTicks));
            }
            if (queue.Count < 2)
            {
                return false;
            }

            T newest = default(T);
            foreach (T item in queue)
            {
                newest = item;
            }

            if (!SpanExceedsLimit(queue, newest, timestampSelector, maximumSpanTicks))
            {
                return false;
            }

            queue.Clear();
            queue.Enqueue(newest);
            return true;
        }

        private static bool SpanExceedsLimit<T>(
            Queue<T> queue,
            T newest,
            Func<T, long> timestampSelector,
            long maximumSpanTicks)
        {
            long oldestTicks = timestampSelector(queue.Peek());
            long newestTicks = timestampSelector(newest);
            long span = newestTicks - oldestTicks;
            return span < 0L || span > maximumSpanTicks;
        }
    }
}
