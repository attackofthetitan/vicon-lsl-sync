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

        // Must stay above a full drain batch: 32 readings at 90 Hz span 355 ms, and
        // a tighter budget would collapse the queue part-way through a batch,
        // discarding the readings the drain just recovered.
        public static readonly long MaxBacklogSpanTicks =
            (long)Math.Round(Stopwatch.Frequency * 0.500, MidpointRounding.AwayFromZero);

        // Only the reading that seeds the drain cursor is judged on age; later ones
        // are reached by walking forward rather than by asking for "now".
        public static readonly long MaxSeedCaptureAgeTicks =
            (long)Math.Round(Stopwatch.Frequency * 0.050, MidpointRounding.AwayFromZero);

        // Ceiling on work done under the tracker lock in one acquisition call.
        public const int MaxReadingsPerAcquire = 32;

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

        // The accepted timestamp doubles as the drain cursor.
        public bool HasReading => hasLastTimestamp;

        public long LastTimestampTicks => lastTimestampTicks;

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

    // The rate that arrived, measured from capture timestamps. A throttled tracker
    // keeps reporting its configured rate, so only this shows a short recording.
    internal sealed class GazeRateEstimator
    {
        // One second at 90 Hz.
        public const int WindowSize = 90;
        private const int MinimumSamples = 16;

        private readonly long[] captureTicks = new long[WindowSize];
        private int count;
        private int next;

        public void Add(long systemRelativeTimeTicks)
        {
            captureTicks[next] = systemRelativeTimeTicks;
            next = (next + 1) % WindowSize;
            if (count < WindowSize)
            {
                count++;
            }
        }

        public bool TryGetRate(out double samplesPerSecond)
        {
            samplesPerSecond = 0.0;
            if (count < MinimumSamples)
            {
                return false;
            }

            long newest = captureTicks[(next - 1 + WindowSize) % WindowSize];
            long oldest = captureTicks[(next - count + WindowSize) % WindowSize];
            long spanTicks = newest - oldest;
            if (spanTicks <= 0L)
            {
                return false;
            }

            samplesPerSecond =
                (count - 1) * (double)GazeTiming.SystemRelativeTicksPerSecond / spanTicks;
            return true;
        }

        // A steady slow tracker shows the same min and max; readings lost at full
        // rate show a low min beside a high max.
        public bool TryGetIntervalSummary(out double minMilliseconds, out double maxMilliseconds)
        {
            minMilliseconds = 0.0;
            maxMilliseconds = 0.0;
            if (count < MinimumSamples)
            {
                return false;
            }

            long minTicks = long.MaxValue;
            long maxTicks = long.MinValue;
            int start = (next - count + WindowSize) % WindowSize;
            for (int i = 1; i < count; i++)
            {
                long delta = captureTicks[(start + i) % WindowSize] -
                             captureTicks[(start + i - 1) % WindowSize];
                if (delta < minTicks) minTicks = delta;
                if (delta > maxTicks) maxTicks = delta;
            }

            double ticksPerMillisecond = GazeTiming.SystemRelativeTicksPerSecond / 1000.0;
            minMilliseconds = minTicks / ticksPerMillisecond;
            maxMilliseconds = maxTicks / ticksPerMillisecond;
            return true;
        }

        public void Reset()
        {
            count = 0;
            next = 0;
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
