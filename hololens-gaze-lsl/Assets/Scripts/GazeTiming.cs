using System;
using System.Collections.Generic;

namespace GazeLSL
{
    // We do not know how fast SystemRelativeTime.Ticks runs on the HoloLens. It
    // is not the usual TimeSpan speed, and it does not match Stopwatch either:
    // on the device, a reading 0.020 s old by the SDK's clock was 231 s in the
    // future by Stopwatch, and the gap kept growing.
    //
    // So those ticks are only used to put readings in order and to pass to
    // SpatialGraphNode.TryLocate. Every length of time is measured on the LSL
    // clock instead, which each reading carries as GazeSample.Timestamp.
    internal static class GazeTiming
    {
        // Must be longer than one full catch-up batch (32 readings at 90 Hz is
        // 355 ms). A shorter limit would empty the queue partway through a batch
        // and throw away the readings that were just caught up.
        public const double MaxBacklogSpanSeconds = 0.500;

        // Only a reading fetched for "now" is judged on age. Later readings are
        // fetched by working forward from the last one, not by asking for "now".
        public const double MaxSeedCaptureAgeSeconds = 0.050;

        // Limits how much work is done while holding the tracker lock.
        public const int MaxReadingsPerAcquire = 32;

        // The age comes from the SDK's own reading time and the clock time we
        // asked at, which are on the same clock. Never work it out from
        // SystemRelativeTime ticks and one of our timers: they do not agree, so
        // every reading would look far too old and none would be accepted.
        //
        // Either sign is allowed, because the reading for "now" can be taken one
        // frame before or after we ask.
        public static bool IsFreshSeedAge(double ageSeconds)
        {
            return !double.IsNaN(ageSeconds) &&
                   !double.IsInfinity(ageSeconds) &&
                   Math.Abs(ageSeconds) <= MaxSeedCaptureAgeSeconds;
        }
    }

    // Compares the SDK's whole-number timestamps, not converted times. A new
    // tracker session calls Reset, so readings from different sessions are
    // never compared.
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

    // The rate that actually arrived, measured from capture times. A tracker that
    // slows itself down still reports its set rate, so only this shows the drop.
    // It uses LSL capture times, not SDK ticks, because we do not know how fast
    // the ticks run.
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

        // A tracker that is just slow shows a similar min and max. One that runs
        // at full speed but loses readings shows a low min and a high max.
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

    // The only way to learn there is no newer reading is to ask and get nothing
    // back. On the HoloLens 2, "nothing" is not a null: the SDK throws a
    // NullReferenceException inside itself and leaves behind an object that
    // throws again later. A few of those are fine; one per step crashes the app.
    // So this decides when it is worth asking, and when to stop catching up for
    // a while and just read the reading at the current time.
    internal sealed class GazeDrainPolicy
    {
        // Small on purpose. Each failure leaks a broken SDK object, so catching up
        // has to stop well before they pile up.
        public const int FailedEmptyResultsBeforeSuspend = 3;

        // Long enough that a catch-up that cannot work costs about one failure
        // every three seconds instead of one per step. Short enough that the
        // session gets back to the full rate instead of staying on the fallback.
        public const double SuspensionSeconds = 10.0;

        private int failedEmptyResultsSinceResume;
        private int suspensions;
        private double resumeTimeSeconds;
        private bool suspended;
        private double minimumReadingAgeSeconds = double.PositiveInfinity;

        public int Suspensions => suspensions;

        // How long after a reading is taken the SDK hands it over. Zero until a
        // reading has been seen, which just means catching up asks as often as it
        // did before this was measured.
        public double PublicationLatencySeconds =>
            double.IsPositiveInfinity(minimumReadingAgeSeconds)
                ? 0.0
                : minimumReadingAgeSeconds;

        // Empty results are expected while the tracker is not making readings.
        // Pausing for that is right, but giving up for the whole session is not:
        // the tracker may start later, and the fallback cannot keep up with it.
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

        // The youngest age any reading has been offered at is the shortest time
        // the tracker takes to hand one over. Use the minimum, because a reading
        // picked up while catching up can be any age and says nothing about how
        // quickly a new one arrives.
        public void NoteReadingAge(double ageSeconds)
        {
            if (double.IsNaN(ageSeconds) || double.IsInfinity(ageSeconds))
            {
                return;
            }

            // A reading can look very slightly newer than the ask. That is small
            // clock jitter, not a negative delay.
            double age = ageSeconds > 0.0 ? ageSeconds : 0.0;
            if (age < minimumReadingAgeSeconds)
            {
                minimumReadingAgeSeconds = age;
            }
        }

        // A new reading cannot exist until one frame has passed since the last
        // capture AND the tracker has had time to hand it over. Asking sooner only
        // gets an empty result. This is also when catching up stops: once the
        // newest reading has been taken, do not ask for one more.
        //
        // The delivery delay matters. Readings arrive about 20 ms after they are
        // taken, while one frame is about 11 ms. Checking only the frame time let
        // one extra ask through on every step, and each of those threw and leaked.
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

            // Without a usable delay, use the frame time alone. That asks too
            // often rather than not at all.
            double latencySeconds =
                publicationLatencySeconds > 0.0 &&
                !double.IsPositiveInfinity(publicationLatencySeconds)
                    ? publicationLatencySeconds
                    : 0.0;

            return elapsedSeconds >= framePeriodSeconds + latencySeconds;
        }

        // Only counts empty results that threw. A clean null is normal and free.
        // Returns true on the failure that starts a pause.
        //
        // Counts every failure since catching up last resumed, not just failures
        // in a row. A step that gets one reading and then fails never has two
        // failures in a row, so resetting on each reading once let a failing
        // catch-up run all session and leak thousands of objects.
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

    // A queue can hold a normal small batch, but never one that covers more time
    // than the limit. The newest item is kept, so a reader that fell behind
    // starts again from the current position.
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

        // Written as "not within the limit" so a span that is NaN also empties the
        // queue: dropping one old batch is better than a backlog that never ends.
        // A span just below zero is normal clock jitter and counts as within the
        // limit. A new tracker session, which could reset the clock, empties both
        // queues itself.
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
