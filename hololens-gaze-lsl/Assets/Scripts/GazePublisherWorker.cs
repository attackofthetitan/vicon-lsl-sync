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
        void PushSample(double[] sample);
    }

    public static class GazeSampleEncoder
    {
        public const int ChannelCount = GazeStreamContract.ChannelCount;

        public static void WriteSample(GazeSample frame, double[] sample)
        {
            if (sample == null)
            {
                throw new ArgumentNullException(nameof(sample));
            }

            if (sample.Length < ChannelCount)
            {
                throw new ArgumentException("The gaze sample buffer must contain at least 21 elements.", nameof(sample));
            }

            sample[0] = frame.CombinedOriginX;
            sample[1] = frame.CombinedOriginY;
            sample[2] = frame.CombinedOriginZ;
            sample[3] = frame.CombinedDirectionX;
            sample[4] = frame.CombinedDirectionY;
            sample[5] = frame.CombinedDirectionZ;
            sample[6] = frame.CombinedValid ? 1.0 : 0.0;

            sample[7] = frame.LeftEyeOriginX;
            sample[8] = frame.LeftEyeOriginY;
            sample[9] = frame.LeftEyeOriginZ;
            sample[10] = frame.LeftEyeDirectionX;
            sample[11] = frame.LeftEyeDirectionY;
            sample[12] = frame.LeftEyeDirectionZ;
            sample[13] = frame.LeftEyeValid ? 1.0 : 0.0;

            sample[14] = frame.RightEyeOriginX;
            sample[15] = frame.RightEyeOriginY;
            sample[16] = frame.RightEyeOriginZ;
            sample[17] = frame.RightEyeDirectionX;
            sample[18] = frame.RightEyeDirectionY;
            sample[19] = frame.RightEyeDirectionZ;
            sample[20] = frame.RightEyeValid ? 1.0 : 0.0;
        }
    }

    public sealed class GazePublisherWorker
    {
        private readonly object lifecycleLock = new object();
        private readonly IGazeSampleProvider provider;
        private readonly IGazeSampleOutlet outlet;
        private readonly uint nominalRate;

        private Thread thread;
        private ManualResetEventSlim stopSignal;
        private Exception failure;
        private int running;
        private int pushedSampleCount;

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
                        outlet.PushSample(sampleBuffer);
                        Interlocked.Increment(ref pushedSampleCount);
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
    }
}
