using System;
using System.Collections.Generic;

namespace GazeLSL
{
    // SystemRelativeTime.Ticks runs at an unknown speed on the HoloLens, so it is
    // only used to put readings in order and for SpatialGraphNode.TryLocate, and
    // every length of time uses the LSL time in GazeSample.Timestamp instead.
    internal static class GazeTiming
    {
        // Must be longer than one full catch-up batch (32 readings at 90 Hz is
        // 355 ms), or the queue would throw away readings it just caught up on.
        public const double MaxBacklogSpanSeconds = 0.500;

        // Only a reading fetched for "now" is judged on age, because later ones are
        // fetched by working forward from the last reading.
        public const double MaxSeedCaptureAgeSeconds = 0.050;

        // Limits how much work is done while holding the tracker lock.
        public const int MaxReadingsPerAcquire = 32;

        // Allows an age of either sign, because the reading for "now" can be taken
        // one frame before or after we ask.
        public static bool IsFreshSeedAge(double ageSeconds)
        {
            return !double.IsNaN(ageSeconds) &&
                   !double.IsInfinity(ageSeconds) &&
                   Math.Abs(ageSeconds) <= MaxSeedCaptureAgeSeconds;
        }
    }

    // Compares the SDK's whole-number timestamps, not converted times, and a new
    // tracker session calls Reset so readings from two sessions are never compared.
    internal sealed class GazeReadingGate
    {
        private bool hasLastTimestamp;
        private long lastTimestampTicks;

        // The last accepted time is also where the next catch-up starts from.
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

    // The rate that actually arrived, measured from LSL capture times, because a
    // tracker that slows itself down still reports the rate it was set to.
    internal sealed class GazeRateEstimator
    {
        public const int WindowSize = 90;
        private const int MinimumSamples = 16;

        private readonly double[] captureTimes = new double[WindowSize];
        private int count;
        private int next;

        public void Add(double captureLslTime)
        {
            captureTimes[next] = captureLslTime;
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

            double newest = captureTimes[(next - 1 + WindowSize) % WindowSize];
            double oldest = captureTimes[(next - count + WindowSize) % WindowSize];
            double spanSeconds = newest - oldest;
            if (!(spanSeconds > 0.0))
            {
                return false;
            }

            samplesPerSecond = (count - 1) / spanSeconds;
            return true;
        }

        // A tracker that is just slow shows a similar min and max, while one that
        // runs at full speed but loses readings shows a low min and a high max.
        public bool TryGetIntervalSummary(out double minMilliseconds, out double maxMilliseconds)
        {
            minMilliseconds = 0.0;
            maxMilliseconds = 0.0;
            if (count < MinimumSamples)
            {
                return false;
            }

            double minSeconds = double.MaxValue;
            double maxSeconds = double.MinValue;
            int start = (next - count + WindowSize) % WindowSize;
            for (int i = 1; i < count; i++)
            {
                double delta = captureTimes[(start + i) % WindowSize] -
                               captureTimes[(start + i - 1) % WindowSize];
                if (delta < minSeconds) minSeconds = delta;
                if (delta > maxSeconds) maxSeconds = delta;
            }

            minMilliseconds = minSeconds * 1000.0;
            maxMilliseconds = maxSeconds * 1000.0;
            return true;
        }

        public void Reset()
        {
            count = 0;
            next = 0;
        }
    }

    // Decides when to ask for a newer reading and when to pause catching up,
    // because on the HoloLens 2 an empty answer throws inside the SDK and leaves a
    // broken object behind, and one of those per step crashes the app.
    internal sealed class GazeDrainPolicy
    {
        // Small on purpose, because each failure leaks a broken SDK object and
        // catching up has to stop well before they pile up.
        public const int FailedEmptyResultsBeforeSuspend = 3;

        // Long enough that a catch-up that cannot work fails only about once every
        // three seconds, but short enough that the session soon gets back to full rate.
        public const double SuspensionSeconds = 10.0;

        private int failedEmptyResultsSinceResume;
        private int suspensions;
        private double resumeTimeSeconds;
        private bool suspended;
        private double minimumReadingAgeSeconds = double.PositiveInfinity;

        public int Suspensions => suspensions;

        // How long after a reading is taken the SDK hands it over, or zero before
        // the first reading, which just means catching up asks as often as it used to.
        public double PublicationLatencySeconds =>
            double.IsPositiveInfinity(minimumReadingAgeSeconds)
                ? 0.0
                : minimumReadingAgeSeconds;

        // Catching up only ever pauses, because empty results are normal while the
        // tracker is not making readings, and the fallback cannot keep up once it starts.
        public bool IsSuspended(double nowSeconds)
        {
            if (!suspended)
            {
                return false;
            }

            if (double.IsNaN(nowSeconds) || nowSeconds >= resumeTimeSeconds)
            {
                suspended = false;
                failedEmptyResultsSinceResume = 0;
                return false;
            }

            return true;
        }

        // Keeps the smallest reading age seen, which is how long the tracker takes
        // to hand over a new reading, since a reading found while catching up can
        // be any age.
        public void NoteReadingAge(double ageSeconds)
        {
            if (double.IsNaN(ageSeconds) || double.IsInfinity(ageSeconds))
            {
                return;
            }

            // A reading can look very slightly newer than the ask because of tiny
            // clock differences, which is not a negative delay.
            double age = ageSeconds > 0.0 ? ageSeconds : 0.0;
            if (age < minimumReadingAgeSeconds)
            {
                minimumReadingAgeSeconds = age;
            }
        }

        // A new reading cannot exist until one frame (about 11 ms) plus the delivery
        // delay (about 20 ms) has passed since the last capture, so this also
        // decides when catching up should stop.
        public static bool CouldHaveNewerReading(
            double queryTimeSeconds,
            double lastCaptureTimeSeconds,
            double framePeriodSeconds,
            double publicationLatencySeconds)
        {
            if (!(framePeriodSeconds > 0.0))
            {
                return true;
            }

            double elapsedSeconds = queryTimeSeconds - lastCaptureTimeSeconds;
            if (double.IsNaN(elapsedSeconds))
            {
                // A broken capture time tells us nothing, so ask rather than stall.
                return true;
            }

            // Without a usable delay, use the frame time alone, which asks too
            // often rather than not at all.
            double latencySeconds =
                publicationLatencySeconds > 0.0 &&
                !double.IsPositiveInfinity(publicationLatencySeconds)
                    ? publicationLatencySeconds
                    : 0.0;

            return elapsedSeconds >= framePeriodSeconds + latencySeconds;
        }

        // Counts empty results that threw (a clean null is fine) since catching up
        // last resumed, not just failures in a row, and returns true on the one
        // that starts a pause.
        public bool NoteFailedEmptyResult(double nowSeconds)
        {
            if (suspended)
            {
                return false;
            }

            failedEmptyResultsSinceResume++;
            if (failedEmptyResultsSinceResume < FailedEmptyResultsBeforeSuspend)
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
            failedEmptyResultsSinceResume = 0;
            suspensions = 0;
            resumeTimeSeconds = 0.0;
            suspended = false;
            minimumReadingAgeSeconds = double.PositiveInfinity;
        }
    }

    // Lets a queue hold a normal small batch but never more time than the limit,
    // keeping the newest item so a reader that fell behind starts again from now.
    internal static class GazeBacklogPolicy
    {
        public static void Enqueue<T>(
            Queue<T> queue,
            T item,
            Func<T, double> timestampSelector,
            int maximumCount,
            double maximumSpanSeconds)
        {
            if (queue == null) throw new ArgumentNullException(nameof(queue));
            if (timestampSelector == null)
            {
                throw new ArgumentNullException(nameof(timestampSelector));
            }
            if (maximumCount <= 0) throw new ArgumentOutOfRangeException(nameof(maximumCount));
            if (!(maximumSpanSeconds >= 0.0))
            {
                throw new ArgumentOutOfRangeException(nameof(maximumSpanSeconds));
            }

            while (queue.Count >= maximumCount)
            {
                queue.Dequeue();
            }

            if (queue.Count > 0 &&
                SpanExceedsLimit(queue, item, timestampSelector, maximumSpanSeconds))
            {
                queue.Clear();
            }

            queue.Enqueue(item);
        }

        public static bool CollapseIfOverSpan<T>(
            Queue<T> queue,
            Func<T, double> timestampSelector,
            double maximumSpanSeconds)
        {
            if (queue == null) throw new ArgumentNullException(nameof(queue));
            if (timestampSelector == null)
            {
                throw new ArgumentNullException(nameof(timestampSelector));
            }
            if (!(maximumSpanSeconds >= 0.0))
            {
                throw new ArgumentOutOfRangeException(nameof(maximumSpanSeconds));
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

            if (!SpanExceedsLimit(queue, newest, timestampSelector, maximumSpanSeconds))
            {
                return false;
            }

            queue.Clear();
            queue.Enqueue(newest);
            return true;
        }

        // Written as "not within the limit" so a NaN span also empties the queue,
        // while a span just below zero comes from tiny clock differences and counts
        // as within the limit.
        private static bool SpanExceedsLimit<T>(
            Queue<T> queue,
            T newest,
            Func<T, double> timestampSelector,
            double maximumSpanSeconds)
        {
            double oldestSeconds = timestampSelector(queue.Peek());
            double newestSeconds = timestampSelector(newest);
            return !(newestSeconds - oldestSeconds <= maximumSpanSeconds);
        }
    }
}
