using System;
using System.Threading;
using LSL;
using UnityEngine;
using UnityEngine.InputSystem;
using Stopwatch = System.Diagnostics.Stopwatch;

namespace GazeLSL
{
    /*
    Publishes HoloLens 2 Extended Eye Tracking readings to LSL from a dedicated
    worker thread so gaze publishing is not limited by the Unity render frame rate.
    */
    public sealed class GazeLSLOutlet : MonoBehaviour
    {
        private const int ChannelCount = 21;
        private const int StopTimeoutMilliseconds = 500;

        private static readonly string[] ChannelLabels =
        {
            "CombinedOriginX", "CombinedOriginY", "CombinedOriginZ",
            "CombinedDirectionX", "CombinedDirectionY", "CombinedDirectionZ",
            "CombinedValid",
            "LeftEyeOriginX", "LeftEyeOriginY", "LeftEyeOriginZ",
            "LeftEyeDirectionX", "LeftEyeDirectionY", "LeftEyeDirectionZ",
            "LeftEyeValid",
            "RightEyeOriginX", "RightEyeOriginY", "RightEyeOriginZ",
            "RightEyeDirectionX", "RightEyeDirectionY", "RightEyeDirectionZ",
            "RightEyeValid"
        };

        private static readonly string[] ChannelUnits =
        {
            "meters", "meters", "meters",
            "normalized", "normalized", "normalized",
            "bool",
            "meters", "meters", "meters",
            "normalized", "normalized", "normalized",
            "bool",
            "meters", "meters", "meters",
            "normalized", "normalized", "normalized",
            "bool"
        };

        [SerializeField] private GazeLSLConfig config;
        [SerializeField] private GazeDataProvider gazeProvider;

        private StreamInfo info;
        private StreamOutlet outlet;
        private Thread workerThread;
        private ManualResetEventSlim stopSignal;
        private volatile bool workerRunning;
        private int pushedSampleCount;
        private uint nominalRate;

        private void Start()
        {
            if (!ValidateReferences())
            {
                enabled = false;
                return;
            }

            nominalRate = Math.Max(1u, config.TargetFrameRate);

            CreateOutlet();
            StartWorker();
        }

        private bool ValidateReferences()
        {
            if (gazeProvider == null)
            {
                gazeProvider = GetComponent<GazeDataProvider>();
            }

            if (config == null)
            {
                Debug.LogError("GazeLSLOutlet requires a GazeLSLConfig asset.");
                return false;
            }

            if (gazeProvider == null)
            {
                Debug.LogError("GazeLSLOutlet requires a GazeDataProvider on the same GameObject or assigned in the inspector.");
                return false;
            }

            return true;
        }

        private void CreateOutlet()
        {
            info = new StreamInfo(
                config.StreamName,
                config.StreamType,
                ChannelCount,
                nominalRate,
                channel_format_t.cf_double64,
                config.SourceId
            );

            AppendChannelMetadata(info.desc().append_child("channels"));
            AppendAcquisitionMetadata(info.desc().append_child("acquisition"), nominalRate);

            outlet = new StreamOutlet(info);

            Debug.Log($"LSL outlet created: {config.StreamName}, {ChannelCount} channels, nominal {nominalRate} Hz");
        }

        private static void AppendChannelMetadata(XMLElement channels)
        {
            for (int i = 0; i < ChannelCount; i++)
            {
                XMLElement channel = channels.append_child("channel");
                channel.append_child_value("label", ChannelLabels[i]);
                channel.append_child_value("unit", ChannelUnits[i]);
            }
        }

        private static void AppendAcquisitionMetadata(XMLElement acquisition, uint rate)
        {
            acquisition.append_child_value("device", "HoloLens2");
            acquisition.append_child_value("sdk", "Microsoft.MixedReality.EyeTracking");
            acquisition.append_child_value("nominal_srate", rate.ToString());
            acquisition.append_child_value("acquisition_mode", "worker_thread_rate_limited");
            acquisition.append_child_value("timestamp", "lsl_local_clock_at_push");
        }

        private void StartWorker()
        {
            stopSignal = new ManualResetEventSlim(false);
            workerRunning = true;

            workerThread = new Thread(WorkerLoop)
            {
                IsBackground = true,
                Priority = System.Threading.ThreadPriority.AboveNormal,
                Name = "HoloLens Gaze LSL"
            };

            workerThread.Start();
        }

        private void WorkerLoop()
        {
            double[] sampleBuffer = new double[ChannelCount];
            double intervalMilliseconds = 1000.0 / nominalRate;

            Stopwatch stopwatch = Stopwatch.StartNew();
            double nextSampleMilliseconds = stopwatch.Elapsed.TotalMilliseconds;

            while (workerRunning && !stopSignal.IsSet)
            {
                double nowMilliseconds = stopwatch.Elapsed.TotalMilliseconds;

                if (nowMilliseconds < nextSampleMilliseconds)
                {
                    WaitForNextSample(nextSampleMilliseconds - nowMilliseconds);
                    continue;
                }

                TryPushOneSample(sampleBuffer);

                nextSampleMilliseconds += intervalMilliseconds;

                if (nowMilliseconds - nextSampleMilliseconds > intervalMilliseconds)
                {
                    nextSampleMilliseconds = nowMilliseconds + intervalMilliseconds;
                }
            }
        }

        private void WaitForNextSample(double remainingMilliseconds)
        {
            if (remainingMilliseconds > 1.0)
            {
                stopSignal.Wait(TimeSpan.FromMilliseconds(remainingMilliseconds - 0.25));
                return;
            }

            Thread.Yield();
        }

        private void TryPushOneSample(double[] sampleBuffer)
        {
            if (!gazeProvider.TryGetNextSample(out GazeDataProvider.GazeSample gazeSample))
            {
                return;
            }

            WriteSampleBuffer(gazeSample, sampleBuffer);

            StreamOutlet currentOutlet = outlet;
            if (currentOutlet == null)
            {
                return;
            }

            currentOutlet.push_sample(sampleBuffer);
            Interlocked.Increment(ref pushedSampleCount);
        }


        private static void WriteSampleBuffer(GazeDataProvider.GazeSample frame, double[] sample)
        {
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

        private void OnDestroy()
        {
            workerRunning = false;
            stopSignal?.Set();

            if (workerThread != null)
            {
                if (!workerThread.Join(StopTimeoutMilliseconds))
                {
                    Debug.LogWarning("Gaze LSL worker did not stop cleanly before timeout.");
                }

                workerThread = null;
            }

            stopSignal?.Dispose();
            stopSignal = null;

            outlet = null;
            info = null;

            Debug.Log($"LSL outlet closed after pushing {pushedSampleCount} gaze samples");
        }
    }
}
