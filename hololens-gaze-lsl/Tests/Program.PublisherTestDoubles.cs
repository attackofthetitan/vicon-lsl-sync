using System;
using System.Threading;
using GazeLSL;

internal static partial class Program
{
    private sealed class EmptyProvider : IGazeSampleProvider
    {
        public bool TryGetNextSample(out GazeSample sample) { sample = default(GazeSample); return false; }
    }

    private sealed class OneSampleProvider : IGazeSampleProvider
    {
        public bool TryGetNextSample(out GazeSample sample)
        {
            sample = new GazeSample { Timestamp = 1.0 };
            return true;
        }
    }

    private sealed class ThrowingProvider : IGazeSampleProvider
    {
        private readonly Exception failure;
        public ThrowingProvider(Exception failure) { this.failure = failure; }
        public bool TryGetNextSample(out GazeSample sample) { sample = default(GazeSample); throw failure; }
    }

    private sealed class ThrowOnceProvider : IGazeSampleProvider
    {
        private readonly Exception failure;
        private int calls;
        public ThrowOnceProvider(Exception failure) { this.failure = failure; }
        public bool TryGetNextSample(out GazeSample sample)
        {
            sample = new GazeSample { Timestamp = 1.0 };
            if (Interlocked.Increment(ref calls) == 1) throw failure;
            return true;
        }
    }

    private sealed class BlockingProvider : IGazeSampleProvider
    {
        public readonly ManualResetEventSlim Entered = new ManualResetEventSlim(false);
        public readonly ManualResetEventSlim Release = new ManualResetEventSlim(false);
        public bool TryGetNextSample(out GazeSample sample)
        {
            sample = default(GazeSample);
            Entered.Set();
            Release.Wait();
            return false;
        }
    }

    private sealed class CountingOutlet : IGazeSampleOutlet
    {
        public int Count;
        public double LastTimestamp;
        public void PushSample(double[] sample, double timestamp)
        {
            LastTimestamp = timestamp;
            Interlocked.Increment(ref Count);
        }
    }

    private sealed class TimestampProvider : IGazeSampleProvider
    {
        private readonly double timestamp;
        public TimestampProvider(double timestamp) { this.timestamp = timestamp; }
        public bool TryGetNextSample(out GazeSample sample)
        {
            sample = new GazeSample { Timestamp = timestamp };
            return true;
        }
    }

    private sealed class InvalidTimestampProvider : IGazeSampleProvider
    {
        public bool TryGetNextSample(out GazeSample sample)
        {
            sample = new GazeSample { Timestamp = double.NaN };
            return true;
        }
    }

    private sealed class CadenceProbeProvider : IGazeSampleProvider
    {
        private int calls;
        public readonly ManualResetEventSlim FirstCall = new ManualResetEventSlim(false);
        public readonly ManualResetEventSlim SecondCall = new ManualResetEventSlim(false);

        public bool TryGetNextSample(out GazeSample sample)
        {
            sample = new GazeSample { Timestamp = double.NaN };
            int call = Interlocked.Increment(ref calls);
            if (call == 1) FirstCall.Set();
            if (call == 2) SecondCall.Set();
            return true;
        }
    }

    private sealed class ThrowingOutlet : IGazeSampleOutlet
    {
        private readonly Exception failure;
        public ThrowingOutlet(Exception failure) { this.failure = failure; }
        public void PushSample(double[] sample, double timestamp) { throw failure; }
    }
}
