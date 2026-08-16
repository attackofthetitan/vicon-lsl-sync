using System;
using System.Collections.Generic;
using System.Diagnostics;

namespace GazeLSL
{
    // The Extended Eye Tracking SDK exposes SystemRelativeTime as a Windows
    // Runtime TimeSpan. Its ticks express the QPC-derived system-relative
    // clock in 100 ns units. liblsl uses the same QPC-backed steady clock but
    // may expose it through a different duration unit, so both are converted
    // to seconds before publication.
    internal static class GazeTiming
    {
        public const long SystemRelativeTicksPerSecond = TimeSpan.TicksPerSecond;
        public const long MaxBacklogSpanTicks = 25L * TimeSpan.TicksPerMillisecond;
        private const long MaximumFutureLeadTicks = TimeSpan.TicksPerMillisecond;

        public static long CurrentSystemRelativeTimeTicks()
        {
            return QpcTicksToSystemRelativeTicks(
                Stopwatch.GetTimestamp(),
                Stopwatch.Frequency);
        }

        public static long QpcTicksToSystemRelativeTicks(
            long qpcTicks,
            long qpcFrequency)
        {
            if (qpcFrequency <= 0L)
            {
                throw new ArgumentOutOfRangeException(nameof(qpcFrequency));
            }

            // Do the conversion before constructing the TimeSpan passed to the
            // WinRT API.  Double precision is sufficient here: the conversion
            // only needs 100 ns resolution and QPC values on supported devices
            // remain well below the 53-bit exact-integer limit for normal
            // device lifetimes.
            double systemRelativeTicks =
                qpcTicks * (double)SystemRelativeTicksPerSecond / qpcFrequency;
            if (systemRelativeTicks > long.MaxValue ||
                systemRelativeTicks < long.MinValue)
            {
                throw new OverflowException("The QPC timestamp does not fit in TimeSpan ticks.");
            }

            return (long)Math.Round(systemRelativeTicks, MidpointRounding.AwayFromZero);
        }

        public static double SystemRelativeTicksToLslTimestamp(long systemRelativeTimeTicks)
        {
            return systemRelativeTimeTicks / (double)SystemRelativeTicksPerSecond;
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
