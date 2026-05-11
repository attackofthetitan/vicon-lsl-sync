using UnityEngine;
using LSL;
using System;
using Stopwatch = System.Diagnostics.Stopwatch;
using System.Threading;

namespace GazeLSL
{
    /*
    Pushes HoloLens 2 eye gaze data to an LSL outlet.

    Channels:
    - combined gaze
    - per-eye gaze
    - vergence

    Sampling runs on a dedicated worker thread instead of Unity's player loop
    so the eye tracker can operate closer to its native acquisition rate.
    */
    public class GazeLSLOutlet : MonoBehaviour
    {
        [SerializeField] private GazeLSLConfig config;
        [SerializeField] private GazeDataProvider gazeProvider;

        private StreamOutlet outlet;
        private StreamInfo info;
        private double[] sample;

        private int pushedSampleCount = 0;

        private Thread samplingThread;
        private volatile bool samplingThreadRunning;

        /*
        0-2 combined origin
        3-5 combined direction
        6 combined valid

        7-9 left origin
        10-12 left direction
        13 left valid

        14-16 right origin
        17-19 right direction
        20 right valid

        21 vergence distance
        */
        private const int ChannelCount = 22;

        private void Start()
        {
            Debug.Log("GazeLSLOutlet Start() reached");

            if (gazeProvider == null)
            {
                gazeProvider = GetComponent<GazeDataProvider>();
            }

            if (config == null)
            {
                Debug.LogError("GazeLSLOutlet - config not assigned");
                enabled = false;
                return;
            }

            Debug.Log($"GazeLSLOutlet config loaded. StreamName={config.StreamName}");

            info = new StreamInfo(
                config.StreamName,
                config.StreamType,
                ChannelCount,
                config.TargetFrameRate,
                channel_format_t.cf_double64,
                config.SourceId
            );

            XMLElement channels = info.desc().append_child("channels");

            AppendChannel(channels, "CombinedOriginX", "meters");
            AppendChannel(channels, "CombinedOriginY", "meters");
            AppendChannel(channels, "CombinedOriginZ", "meters");
            AppendChannel(channels, "CombinedDirectionX", "normalized");
            AppendChannel(channels, "CombinedDirectionY", "normalized");
            AppendChannel(channels, "CombinedDirectionZ", "normalized");
            AppendChannel(channels, "CombinedValid", "bool");

            AppendChannel(channels, "LeftEyeOriginX", "meters");
            AppendChannel(channels, "LeftEyeOriginY", "meters");
            AppendChannel(channels, "LeftEyeOriginZ", "meters");
            AppendChannel(channels, "LeftEyeDirectionX", "normalized");
            AppendChannel(channels, "LeftEyeDirectionY", "normalized");
            AppendChannel(channels, "LeftEyeDirectionZ", "normalized");
            AppendChannel(channels, "LeftEyeValid", "bool");

            AppendChannel(channels, "RightEyeOriginX", "meters");
            AppendChannel(channels, "RightEyeOriginY", "meters");
            AppendChannel(channels, "RightEyeOriginZ", "meters");
            AppendChannel(channels, "RightEyeDirectionX", "normalized");
            AppendChannel(channels, "RightEyeDirectionY", "normalized");
            AppendChannel(channels, "RightEyeDirectionZ", "normalized");
            AppendChannel(channels, "RightEyeValid", "bool");

            AppendChannel(channels, "VergenceDistance", "meters");

            XMLElement meta = info.desc().append_child("acquisition");
            meta.append_child_value("device", "HoloLens2");
            meta.append_child_value("sdk", "ExtendedEyeTracking");
            meta.append_child_value("nominal_srate", config.TargetFrameRate.ToString());

            sample = new double[ChannelCount];
            outlet = new StreamOutlet(info);

            Debug.Log($"LSL outlet created - {config.StreamName}, {ChannelCount} channels");

            StartSamplingThread();
        }

        private void StartSamplingThread()
        {
            samplingThreadRunning = true;

            samplingThread = new Thread(SamplingThreadLoop);
            samplingThread.IsBackground = true;
            samplingThread.Priority = System.Threading.ThreadPriority.AboveNormal;
            samplingThread.Start();
        }

        private void SamplingThreadLoop()
        {
            double intervalMilliseconds = 1000.0 / Math.Max(1, config.TargetFrameRate);

            Stopwatch stopwatch = Stopwatch.StartNew();
            double nextTimeMilliseconds = stopwatch.Elapsed.TotalMilliseconds;

            while (samplingThreadRunning)
            {
                double nowMilliseconds = stopwatch.Elapsed.TotalMilliseconds;

                if (nowMilliseconds >= nextTimeMilliseconds)
                {
                    PushOneSampleThreaded();

                    nextTimeMilliseconds += intervalMilliseconds;

                    if (nowMilliseconds - nextTimeMilliseconds > intervalMilliseconds)
                    {
                        nextTimeMilliseconds = nowMilliseconds + intervalMilliseconds;
                    }
                }
                else
                {
                    Thread.Sleep(0);
                }
            }
        }

        private void PushOneSampleThreaded()
        {
            if (outlet == null || gazeProvider == null)
            {
                return;
            }

            if (!gazeProvider.TryGetCurrentSample(out GazeDataProvider.GazeSample frame))
            {
                return;
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

            sample[21] = frame.VergenceValid ? frame.VergenceDistance : double.NaN;

            outlet.push_sample(sample, LSL.LSL.local_clock());

            pushedSampleCount++;
        }

        private void AppendChannel(XMLElement parent, string label, string unit)
        {
            XMLElement ch = parent.append_child("channel");
            ch.append_child_value("label", label);
            ch.append_child_value("unit", unit);
        }

        private void OnDestroy()
        {
            samplingThreadRunning = false;

            if (samplingThread != null)
            {
                try
                {
                    samplingThread.Join(100);
                }
                catch
                {
                }

                samplingThread = null;
            }

            outlet = null;
            info = null;

            Debug.Log("LSL outlet closed");
        }
    }
}
