using System;

namespace GazeLSL
{
    // Maps the Windows system-relative QPC domain used by eye-tracker readings
    // into the LSL local_clock domain. The offset is measured with a midpoint
    // QPC pair around one local_clock read so scheduler/interop latency does
    // not become part of the mapping.
    public sealed class GazeClockMapping
    {
        private readonly double qpcFrequency;
        private readonly Func<long> qpcNow;
        private readonly Func<double> localClockNow;
        private double offsetSeconds;
        private bool calibrated;

        public GazeClockMapping(
            double qpcFrequency,
            Func<long> qpcNow,
            Func<double> localClockNow)
        {
            if (!(qpcFrequency > 0.0) || double.IsNaN(qpcFrequency) || double.IsInfinity(qpcFrequency))
            {
                throw new ArgumentOutOfRangeException(nameof(qpcFrequency));
            }

            this.qpcFrequency = qpcFrequency;
            this.qpcNow = qpcNow ?? throw new ArgumentNullException(nameof(qpcNow));
            this.localClockNow = localClockNow ?? throw new ArgumentNullException(nameof(localClockNow));
        }

        public bool IsCalibrated => calibrated;

        public bool TryCalibrate()
        {
            long qpcBefore = qpcNow();
            double localClockAtPair = localClockNow();
            long qpcAfter = qpcNow();

            double qpcBeforeSeconds = qpcBefore / qpcFrequency;
            double qpcAfterSeconds = qpcAfter / qpcFrequency;
            double qpcMidpointSeconds = (qpcBeforeSeconds + qpcAfterSeconds) * 0.5;
            if (!IsFinite(localClockAtPair) ||
                !IsFinite(qpcMidpointSeconds))
            {
                calibrated = false;
                return false;
            }

            double candidateOffset = localClockAtPair - qpcMidpointSeconds;
            if (!IsFinite(candidateOffset))
            {
                calibrated = false;
                return false;
            }

            offsetSeconds = candidateOffset;
            calibrated = true;
            return true;
        }

        public bool TryMap(TimeSpan systemRelativeTime, out double localClock)
        {
            localClock = 0.0;
            if (!calibrated || systemRelativeTime < TimeSpan.Zero)
            {
                return false;
            }

            double candidate = systemRelativeTime.TotalSeconds + offsetSeconds;
            if (!IsFinite(candidate))
            {
                return false;
            }

            localClock = candidate;
            return true;
        }

        // Exposed for deterministic, platform-neutral tests and for callers
        // that already have a paired midpoint measurement.
        public static bool TryMap(
            TimeSpan systemRelativeTime,
            long qpcMidpoint,
            double localClockAtMidpoint,
            double qpcFrequency,
            out double localClock)
        {
            localClock = 0.0;
            if (systemRelativeTime < TimeSpan.Zero ||
                !(qpcFrequency > 0.0) ||
                double.IsNaN(qpcFrequency) || double.IsInfinity(qpcFrequency) ||
                !IsFinite(localClockAtMidpoint))
            {
                return false;
            }

            double offset = localClockAtMidpoint - qpcMidpoint / qpcFrequency;
            double candidate = systemRelativeTime.TotalSeconds + offset;
            if (!IsFinite(offset) || !IsFinite(candidate))
            {
                return false;
            }

            localClock = candidate;
            return true;
        }

        private static bool IsFinite(double value)
        {
            return !double.IsNaN(value) && !double.IsInfinity(value);
        }
    }
}
