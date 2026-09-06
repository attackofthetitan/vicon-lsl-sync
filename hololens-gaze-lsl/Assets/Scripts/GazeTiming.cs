using System;
using System.Collections.Generic;

namespace GazeLSL
{
    // The Extended Eye Tracking SDK exposes SystemRelativeTime as a TimeSpan, but
    // its Ticks value is a system-relative count whose rate this runtime does not
    // publish. Measured on a HoloLens 2, it runs at neither the TimeSpan 10 MHz
    // tick rate nor the rate of any timer available here: a reading offered 0.020 s
    // ago on the SDK's own clock read 231 s in the future against Stopwatch, and
    // the gap grew by seconds over seconds of wall time, so the two domains differ
    // in rate as well as epoch.
    //
    // So those ticks are never converted to a duration. They are an opaque cursor
    // for ordering readings and the argument SpatialGraphNode.TryLocate expects,
    // and nothing else. Every duration here is taken in the LSL clock domain,
    // which every reading already carries as GazeSample.Timestamp.
    internal static class GazeTiming
    {
        // Must stay above a full drain batch: 32 readings at 90 Hz span 355 ms, and
        // a tighter budget would collapse the queue part-way through a batch,
        // discarding the readings the drain just recovered.
        public const double MaxBacklogSpanSeconds = 0.500;

        // Only a reading fetched for "now" is judged on age; later ones are reached
        // by walking the cursor forward rather than by asking for the current time.
        public const double MaxSeedCaptureAgeSeconds = 0.050;

        // Ceiling on work done under the tracker lock in one acquisition call.
        public const int MaxReadingsPerAcquire = 32;

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
    //
    // Fed the LSL capture time rather than the SDK tick count: a rate is a duration,
    // and dividing those ticks by a frequency this runtime never agreed to scaled
    // every measurement here by an unknown factor.
    internal sealed class GazeRateEstimator
    {
        // One second at 90 Hz.
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

    // The SDK call that walks the reading cursor forward offers no cheap way to
    // ask whether another reading exists: a drain finds out by reaching an empty
    // result. On this HoloLens 2 runtime that empty result is not a null return
    // but a NullReferenceException thrown inside the SDK's own projection, which
    // also leaves behind an object whose finalizer throws again. A few of those
    // in a session are survivable; one per publishing step is not. So this decides
    // when asking is worth anything, and when the drain has to be put down for a
    // while in favour of reading at the current time.
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

        private int failedEmptyResultsSinceResume;
        private int suspensions;
        private double resumeTimeSeconds;
        private bool suspended;
        private double minimumReadingAgeSeconds = double.PositiveInfinity;

        public int Suspensions => suspensions;

        // How long after a capture the SDK will part with the reading. Zero until
        // a reading has been seen, which only makes the drain ask as eagerly as it
        // did before this was measured.
        public double PublicationLatencySeconds =>
            double.IsPositiveInfinity(minimumReadingAgeSeconds)
                ? 0.0
                : minimumReadingAgeSeconds;

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
                failedEmptyResultsSinceResume = 0;
                return false;
            }

            return true;
        }

        // The freshest a reading has ever been offered is the floor of the delay
        // between capture and availability. Taken as a minimum because a reading
        // recovered by a drain that is catching up is arbitrarily old, and that
        // says nothing about how quickly the tracker parts with a new one.
        public void NoteReadingAge(double ageSeconds)
        {
            if (double.IsNaN(ageSeconds) || double.IsInfinity(ageSeconds))
            {
                return;
            }

            // A reading can be offered a hair ahead of the query; that is clock
            // jitter between the two wall clocks, not a negative delay.
            double age = ageSeconds > 0.0 ? ageSeconds : 0.0;
            if (age < minimumReadingAgeSeconds)
            {
                minimumReadingAgeSeconds = age;
            }
        }

        // No reading can be had until a frame period has passed since the last one
        // was captured AND the tracker has parted with it, so asking before then
        // only buys an empty result. This is also the drain's stop rule: once the
        // newest accepted reading is the current one, the step has caught up and
        // must not ask for one more.
        //
        // The publication delay is what the frame period alone got wrong. Readings
        // arrive about 20 ms after capture on this device against an 11 ms frame
        // period, so a reading the drain has just taken always looked old enough to
        // justify one more ask -- the ask that cannot be answered, and that on this
        // runtime throws and leaks rather than returning null. Counted over a
        // session that was one failure per drain step.
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
                // An unusable capture time says nothing either way, so ask rather
                // than stall acquisition on it.
                return true;
            }

            // An unusable delay leaves the frame period on its own, which asks too
            // eagerly rather than not at all.
            double latencySeconds =
                publicationLatencySeconds > 0.0 &&
                !double.IsPositiveInfinity(publicationLatencySeconds)
                    ? publicationLatencySeconds
                    : 0.0;

            return elapsedSeconds >= framePeriodSeconds + latencySeconds;
        }

        // Only the failing kind of empty result is counted; a clean null return is
        // ordinary and costs nothing. Returns true on the failure that suspends.
        //
        // Counted since the drain last resumed rather than consecutively. A run
        // that reads one reading and then fails never accumulates two failures in
        // a row, so forgiving the count on every reading kept a drain failing once
        // per step alive for a whole session -- thousands of leaked objects, where
        // this budget costs three per suspension.
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

    // A queue may contain a normal small batch, but it must never retain a
    // batch whose capture-time span exceeds the freshness budget.  The newest
    // item is retained so a delayed consumer resumes at the current pose.
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

        // Written as a rejected "within budget" so an unusable span collapses the
        // queue: one stale batch is the cost of dropping it, an unbounded backlog
        // the cost of keeping it. A span a hair below zero is ordinary jitter
        // between the two wall clocks behind a capture time and is within budget;
        // a tracker session that could restart the clock outright clears both
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
