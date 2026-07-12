using System;
using System.Diagnostics;
using System.Threading;

namespace GazeLSL
{
    public interface IGazeSampleProvider
    {
        bool TryGetNextSample(out GazeSample sample);
    }

    public interface IGazeSampleOutlet
    {
        void PushSample(double[] sample, double timestamp);
    }

    public static class GazeSampleEncoder
    {
        public const int ChannelCount = GazeStreamContract.ChannelCount;

        public static void WriteSample(GazeSample frame, double[] sample)
        {
            GazeStreamContract.WriteSample(frame, sample);
        }
    }

    public sealed class GazePublisherWorker
    {
        private readonly object lifecycleLock = new object();
        private readonly IGazeSampleProvider provider;
        private readonly IGazeSampleOutlet outlet;
        private readonly uint nominalRate;
        private readonly Func<double> fallbackTimestampProvider;

        private Thread thread;
        private ManualResetEventSlim stopSignal;
        private Exception failure;
        private int running;
        private int pushedSampleCount;

        public GazePublisherWorker(
            IGazeSampleProvider provider,
            IGazeSampleOutlet outlet,
            uint nominalRate,
            Func<double> fallbackTimestampProvider = null)
        {
            this.provider = provider ?? throw new ArgumentNullException(nameof(provider));
            this.outlet = outlet ?? throw new ArgumentNullException(nameof(outlet));
            this.nominalRate = Math.Max(1u, nominalRate);
            this.fallbackTimestampProvider = fallbackTimestampProvider;
        }

        public bool IsRunning => Volatile.Read(ref running) != 0;
        public int PushedSampleCount => Volatile.Read(ref pushedSampleCount);
        public Exception Failure => failure;

        public void Start()
        {
            lock (lifecycleLock)
            {
                if (thread != null)
                {
                    throw new InvalidOperationException("The gaze publisher worker has already been started.");
                }

                stopSignal = new ManualResetEventSlim(false);
                Volatile.Write(ref running, 1);
                thread = new Thread(WorkerLoop)
                {
                    IsBackground = true,
                    Priority = ThreadPriority.AboveNormal,
                    Name = "HoloLens Gaze LSL"
                };
                thread.Start();
            }
        }

        public bool Stop(int timeoutMilliseconds)
        {
            Thread threadToJoin;
            ManualResetEventSlim signal;
            lock (lifecycleLock)
            {
                threadToJoin = thread;
                signal = stopSignal;
            }

            if (threadToJoin == null)
            {
                return true;
            }

            signal.Set();
            if (!threadToJoin.Join(timeoutMilliseconds))
            {
                // The worker may still be inside a provider or outlet call. Retain every
                // dependency and the cancellation primitive until that call returns.
                return false;
            }

            lock (lifecycleLock)
            {
                if (ReferenceEquals(thread, threadToJoin))
                {
                    thread = null;
                    stopSignal = null;
                    signal.Dispose();
                }
            }

            return true;
        }

        private void WorkerLoop()
        {
            try
            {
                double[] sampleBuffer = new double[GazeSampleEncoder.ChannelCount];
                double intervalMilliseconds = 1000.0 / nominalRate;
                Stopwatch stopwatch = Stopwatch.StartNew();
                double nextSampleMilliseconds = stopwatch.Elapsed.TotalMilliseconds;

                while (!stopSignal.IsSet)
                {
                    double nowMilliseconds = stopwatch.Elapsed.TotalMilliseconds;
                    if (nowMilliseconds < nextSampleMilliseconds)
                    {
                        WaitForNextSample(nextSampleMilliseconds - nowMilliseconds);
                        continue;
                    }

                    GazeSample sample;
                    if (provider.TryGetNextSample(out sample))
                    {
                        GazeSampleEncoder.WriteSample(sample, sampleBuffer);
                        double timestamp = sample.Timestamp;
                        if ((!IsFinite(timestamp) || timestamp <= 0.0) &&
                            fallbackTimestampProvider != null)
                        {
                            timestamp = fallbackTimestampProvider();
                        }
                        if (IsFinite(timestamp) && timestamp > 0.0)
                        {
                            outlet.PushSample(sampleBuffer, timestamp);
                            Interlocked.Increment(ref pushedSampleCount);
                        }
                        // Invalid timestamps are dropped, but cadence still advances below.
                    }

                    nextSampleMilliseconds += intervalMilliseconds;
                    if (nowMilliseconds - nextSampleMilliseconds > intervalMilliseconds)
                    {
                        nextSampleMilliseconds = nowMilliseconds + intervalMilliseconds;
                    }
                }
            }
            catch (Exception e)
            {
                Interlocked.CompareExchange(ref failure, e, null);
            }
            finally
            {
                Volatile.Write(ref running, 0);
            }
        }

        private void WaitForNextSample(double remainingMilliseconds)
        {
            if (remainingMilliseconds > 1.0)
            {
                stopSignal.Wait(TimeSpan.FromMilliseconds(remainingMilliseconds - 0.25));
            }
            else
            {
                Thread.Yield();
            }
        }

        private static bool IsFinite(double value)
        {
            return !double.IsNaN(value) && !double.IsInfinity(value);
        }
    }
}
