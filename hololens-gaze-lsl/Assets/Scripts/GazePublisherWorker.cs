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

    public enum GazeDeliveryState
    {
        WaitingForProviderSample,
        RejectingInvalidTimestamp,
        PublishingSamplesWithoutValidRays,
        PublishingValidGaze
    }

    public readonly struct GazeDeliverySnapshot
    {
        public GazeDeliverySnapshot(
            GazeDeliveryState state,
            int providerCallCount,
            int providerEmptyCount,
            int rejectedTimestampCount,
            int pushedSampleCount,
            int pushedValidGazeSampleCount)
        {
            State = state;
            ProviderCallCount = providerCallCount;
            ProviderEmptyCount = providerEmptyCount;
            RejectedTimestampCount = rejectedTimestampCount;
            PushedSampleCount = pushedSampleCount;
            PushedValidGazeSampleCount = pushedValidGazeSampleCount;
        }

        public GazeDeliveryState State { get; }
        public int ProviderCallCount { get; }
        public int ProviderEmptyCount { get; }
        public int RejectedTimestampCount { get; }
        public int PushedSampleCount { get; }
        public int PushedValidGazeSampleCount { get; }
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
        // Poll faster than the tracker so queued readings drain after a brief delay.
        private const double PublishOversample = 1.25;

        private readonly object lifecycleLock = new object();
        private readonly IGazeSampleProvider provider;
        private readonly IGazeSampleOutlet outlet;
        private readonly uint nominalRate;
        private Thread thread;
        private ManualResetEventSlim stopSignal;
        private Exception failure;
        private Exception providerFailure;
        private Exception lastProviderException;
        private int running;
        private int deliveryState = (int)GazeDeliveryState.WaitingForProviderSample;
        private int pushedSampleCount;
        private int pushedValidGazeSampleCount;
        private int rejectedTimestampCount;
        private int providerCallCount;
        private int providerEmptyCount;
        private int providerExceptionCount;

        public GazePublisherWorker(
            IGazeSampleProvider provider,
            IGazeSampleOutlet outlet,
            uint nominalRate)
        {
            this.provider = provider ?? throw new ArgumentNullException(nameof(provider));
            this.outlet = outlet ?? throw new ArgumentNullException(nameof(outlet));
            this.nominalRate = Math.Max(1u, nominalRate);
        }

        public bool IsRunning => Volatile.Read(ref running) != 0;
        public int PushedSampleCount => Volatile.Read(ref pushedSampleCount);
        public int ProviderExceptionCount => Volatile.Read(ref providerExceptionCount);
        public Exception LastProviderException => Volatile.Read(ref lastProviderException);
        public Exception ProviderFailure => Volatile.Read(ref providerFailure);
        public Exception Failure => Volatile.Read(ref failure);

        public GazeDeliverySnapshot DeliverySnapshot => new GazeDeliverySnapshot(
            (GazeDeliveryState)Volatile.Read(ref deliveryState),
            Volatile.Read(ref providerCallCount),
            Volatile.Read(ref providerEmptyCount),
            Volatile.Read(ref rejectedTimestampCount),
            Volatile.Read(ref pushedSampleCount),
            Volatile.Read(ref pushedValidGazeSampleCount));

        public void Start()
        {
            lock (lifecycleLock)
            {
                if (thread != null)
                {
                    throw new InvalidOperationException("The gaze publisher worker has already been started.");
                }

                stopSignal = new ManualResetEventSlim(false);
                thread = new Thread(WorkerLoop)
                {
                    IsBackground = true,
                    Priority = ThreadPriority.AboveNormal,
                    Name = "HoloLens Gaze LSL"
                };
                try
                {
#if ENABLE_WINMD_SUPPORT
                    // The Extended Eye Tracking SDK is WinRT. Unity's raw worker thread
                    // must enter an MTA before it touches EyeGazeTracker objects.
                    thread.SetApartmentState(ApartmentState.MTA);
#endif
                    Volatile.Write(ref running, 1);
                    thread.Start();
                }
                catch
                {
                    Volatile.Write(ref running, 0);
                    thread = null;
                    ManualResetEventSlim failedSignal = stopSignal;
                    stopSignal = null;
                    failedSignal.Dispose();
                    throw;
                }
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
                // dependency and the stop signal until that call returns.
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
                double stepRate = nominalRate * PublishOversample;
                double intervalMilliseconds = 1000.0 / stepRate;
                Stopwatch stopwatch = Stopwatch.StartNew();
                double nextSampleMilliseconds = stopwatch.Elapsed.TotalMilliseconds;
                int consecutiveProviderFailures = 0;
                int providerFailureLimit = (int)Math.Max(3.0, stepRate);

                while (!stopSignal.IsSet)
                {
                    double nowMilliseconds = stopwatch.Elapsed.TotalMilliseconds;
                    if (nowMilliseconds < nextSampleMilliseconds)
                    {
                        WaitForNextSample(nextSampleMilliseconds - nowMilliseconds);
                        continue;
                    }

                    GazeSample sample;
                    bool hasSample;
                    try
                    {
                        Interlocked.Increment(ref providerCallCount);
                        hasSample = provider.TryGetNextSample(out sample);
                        consecutiveProviderFailures = 0;
                        if (!hasSample)
                        {
                            // Empty polls between tracker readings leave the last
                            // delivery state unchanged.
                            Interlocked.Increment(ref providerEmptyCount);
                        }
                    }
                    catch (Exception e)
                    {
                        Volatile.Write(ref lastProviderException, e);
                        Interlocked.Increment(ref providerExceptionCount);
                        consecutiveProviderFailures++;
                        if (consecutiveProviderFailures >= providerFailureLimit)
                        {
                            // Ask the main thread to reconnect to the tracker.
                            Volatile.Write(ref providerFailure, e);
                            break;
                        }

                        // Focus changes can briefly interrupt the tracker. Try again
                        // at the next scheduled read.
                        hasSample = false;
                        sample = default(GazeSample);
                    }

                    if (hasSample)
                    {
                        PublishSample(sample, sampleBuffer);
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

        private void PublishSample(GazeSample sample, double[] buffer)
        {
            GazeSampleEncoder.WriteSample(sample, buffer);
            double timestamp = sample.Timestamp;
            if (!IsFinite(timestamp) || timestamp <= 0.0)
            {
                Interlocked.Increment(ref rejectedTimestampCount);
                Volatile.Write(ref deliveryState, (int)GazeDeliveryState.RejectingInvalidTimestamp);
                return;
            }

            outlet.PushSample(buffer, timestamp);
            Interlocked.Increment(ref pushedSampleCount);
            bool hasValidRay = sample.CombinedValid || sample.LeftEyeValid || sample.RightEyeValid;
            if (hasValidRay) Interlocked.Increment(ref pushedValidGazeSampleCount);
            Volatile.Write(ref deliveryState, (int)(hasValidRay
                ? GazeDeliveryState.PublishingValidGaze
                : GazeDeliveryState.PublishingSamplesWithoutValidRays));
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
