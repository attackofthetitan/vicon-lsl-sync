using System;
using System.Threading;
using LSL;
using UnityEngine;

namespace GazeLSL
{
    /*
    Publishes HoloLens 2 Extended Eye Tracking readings to LSL.

    The eye tracker owns the acquisition cadence. This component drains unread
    tracker readings and pushes exactly one LSL sample per available reading.
    */
    public sealed class GazeLSLOutlet : MonoBehaviour
    {
        private const int ChannelCount = 22;
        private const int IdleWaitMilliseconds = 1;
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
            "RightEyeValid",
            "VergenceDistance"
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
            "bool",
            "meters"
        };

        [SerializeField] private GazeLSLConfig config;
        [SerializeField] private GazeDataProvider gazeProvider;

        private StreamInfo info;
        private StreamOutlet outlet;
        private Thread workerThread;
        private ManualResetEventSlim stopSignal;
        private volatile bool workerRunning;
        private int pushedSampleCount;

        private void Start()
        {
            if (!ValidateReferences())
            {
                enabled = false;
                return;
            }

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
            uint nominalRate = Math.Max(1u, config.TargetFrameRate);

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

        private static void AppendAcquisitionMetadata(XMLElement acquisition, uint nominalRate)
        {
            acquisition.append_child_value("device", "HoloLens2");
            acquisition.append_child_value("sdk", "Microsoft.MixedReality.EyeTracking");
            acquisition.append_child_value("nominal_srate", nominalRate.ToString());
            acquisition.append_child_value("acquisition_mode", "tracker_buffer_drain");
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

            while (workerRunning && !stopSignal.IsSet)
            {
                bool pushedAny = false;

                while (workerRunning && gazeProvider.TryGetNextSample(out GazeDataProvider.GazeSample gazeSample))
                {
                    WriteSampleBuffer(gazeSample, sampleBuffer);

                    StreamOutlet currentOutlet = outlet;
                    if (currentOutlet == null)
                    {
                        return;
                    }

                    currentOutlet.push_sample(sampleBuffer, LSL.LSL.local_clock());
                    Interlocked.Increment(ref pushedSampleCount);
                    pushedAny = true;
                }

                if (!pushedAny)
                {
                    stopSignal.Wait(IdleWaitMilliseconds);
                }
            }
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

            sample[21] = frame.VergenceValid ? frame.VergenceDistance : double.NaN;
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
