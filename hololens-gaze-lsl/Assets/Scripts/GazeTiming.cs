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

        // Only a reading fetched for "now" is judged on age; later ones are reached
        // by walking the cursor forward rather than by asking for the current time.
        public const double MaxSeedCaptureAgeSeconds = 0.050;

        // Ceiling on work done under the tracker lock in one acquisition call.
        public const int MaxReadingsPerAcquire = 32;

        public static long CurrentSystemRelativeTimeTicks()
        {
            return Stopwatch.GetTimestamp();
        }

        public static double SystemRelativeTicksToLslTimestamp(long systemRelativeTimeTicks)
        {
            return systemRelativeTimeTicks / (double)Stopwatch.Frequency;
        }

        // Judged as the SDK's own reading timestamp against the wall clock read to
        // fetch it: one domain, and the only one here whose unit is established.
        // The tick rate behind SystemRelativeTime is not, so a reading's age must
        // never be taken by comparing those ticks with a timer of ours; if the two
        // disagree, every reading looks ancient and none is ever accepted.
        //
        // The tolerance is symmetric because the reading returned for "now" can be
        // captured a frame either side of the query.
        public static bool IsFreshSeedAge(double ageSeconds)
        {
            return !double.IsNaN(ageSeconds) &&
                   !double.IsInfinity(ageSeconds) &&
                   Math.Abs(ageSeconds) <= MaxSeedCaptureAgeSeconds;
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

    // The SDK call that walks the reading cursor forward offers no cheap way to
    // ask whether another reading exists: a drain finds out by reaching an empty
    // result. On this HoloLens 2 runtime that empty result is not a null return
    // but a NullReferenceException thrown inside the SDK's own projection, which
    // also leaves behind an object whose finalizer throws again. A few of those
    // in a session are survivable; one per publishing step is not. So this decides
    // when asking is worth anything, and when the drain has to be given up for the
    // session in favour of reading at the current time.
    internal sealed class GazeDrainPolicy
    {
        // Deliberately small. Each failure leaks a broken SDK object, so the drain
        // has to be put down well before those accumulate.
        public const int FailedEmptyResultsBeforeSuspend = 3;

        // Long enough that a drain which cannot work costs about a third of a
        // failure per second instead of one per publishing step, and short enough
        // that a session recovers the full rate rather than spending its life on
        // the fallback.
        public const double SuspensionSeconds = 10.0;

        private int consecutiveFailedEmptyResults;
        private int suspensions;
        private double resumeTimeSeconds;
        private bool suspended;

        public int Suspensions => suspensions;

        // Empty results are expected while the tracker has nothing newer to give,
        // which is exactly when it is not publishing. Suspending for that is right,
        // but abandoning the drain for the whole session is not: the tracker starts
        // publishing later and the fallback cannot keep up with it.
        public bool IsSuspended(double nowSeconds)
        {
            if (!suspended)
            {
                return false;
            }

            if (double.IsNaN(nowSeconds) || nowSeconds >= resumeTimeSeconds)
            {
                suspended = false;
                consecutiveFailedEmptyResults = 0;
                return false;
            }

            return true;
        }

        public void NoteReading()
        {
            consecutiveFailedEmptyResults = 0;
        }

        // No reading can exist until a frame period has passed since the last one
        // was captured, so asking before then only buys an empty result. This is
        // also the drain's stop rule: once the newest accepted reading is current,
        // the step has caught up and must not ask for one more.
        public static bool CouldHaveNewerReading(
            double queryTimeSeconds,
            double lastCaptureTimeSeconds,
            double framePeriodSeconds)
        {
            if (!(framePeriodSeconds > 0.0))
            {
                return true;
            }

            double elapsedSeconds = queryTimeSeconds - lastCaptureTimeSeconds;
            if (double.IsNaN(elapsedSeconds))
            {
                // An unusable capture time says nothing either way, so ask rather
                // than stall acquisition on it.
                return true;
            }

            return elapsedSeconds >= framePeriodSeconds;
        }

        // Only the failing kind of empty result is counted; a clean null return is
        // ordinary and costs nothing. Returns true on the failure that suspends.
        public bool NoteFailedEmptyResult(double nowSeconds)
        {
            if (suspended)
            {
                return false;
            }

            consecutiveFailedEmptyResults++;
            if (consecutiveFailedEmptyResults < FailedEmptyResultsBeforeSuspend)
            {
                return false;
            }

            suspended = true;
            suspensions++;
            resumeTimeSeconds = double.IsNaN(nowSeconds)
                ? double.NegativeInfinity
                : nowSeconds + SuspensionSeconds;
            return true;
        }

        public void Reset()
        {
            consecutiveFailedEmptyResults = 0;
            suspensions = 0;
            resumeTimeSeconds = 0.0;
            suspended = false;
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
