using UnityEngine;
using LSL;
using System;
using System.Threading.Tasks;

namespace GazeLSL
{
    /*
    Pushes HoloLens 2 eye gaze data to an LSL outlet.

    Channels:
    - combined gaze
    - per-eye gaze
    - vergence

    Sampling is intentionally decoupled from Unity's render loop so the
    eye tracker can operate closer to its native acquisition rate.
    */
    public class GazeLSLOutlet : MonoBehaviour
    {
        [SerializeField] private GazeLSLConfig config;
        [SerializeField] private GazeDataProvider gazeProvider;

        private StreamOutlet outlet;
        private StreamInfo info;
        private double[] sample;

        private int pushedSampleCount = 0;
        private bool samplingLoopRunning = false;
        private double nextSampleTime;

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

            StartSamplingLoop();
        }

        private async void StartSamplingLoop()
        {
            samplingLoopRunning = true;

            double interval = 1.0 / Math.Max(1, config.TargetFrameRate);
            nextSampleTime = Time.realtimeSinceStartupAsDouble;

            while (samplingLoopRunning)
            {
                double now = Time.realtimeSinceStartupAsDouble;

                if (now >= nextSampleTime)
                {
                    PushOneSample();

                    nextSampleTime += interval;

                    if (now - nextSampleTime > interval)
                    {
                        nextSampleTime = now + interval;
                    }
                }

                await Task.Yield();
            }
        }

        private void PushOneSample()
        {
            if (outlet == null || gazeProvider == null)
            {
                return;
            }

            var frame = gazeProvider.GetCurrentFrame();
            double timestamp = LSL.LSL.local_clock();

            sample[0] = frame.CombinedOrigin.x;
            sample[1] = frame.CombinedOrigin.y;
            sample[2] = frame.CombinedOrigin.z;
            sample[3] = frame.CombinedDirection.x;
            sample[4] = frame.CombinedDirection.y;
            sample[5] = frame.CombinedDirection.z;
            sample[6] = frame.CombinedValid ? 1.0 : 0.0;

            sample[7] = frame.LeftEyeOrigin.x;
            sample[8] = frame.LeftEyeOrigin.y;
            sample[9] = frame.LeftEyeOrigin.z;
            sample[10] = frame.LeftEyeDirection.x;
            sample[11] = frame.LeftEyeDirection.y;
            sample[12] = frame.LeftEyeDirection.z;
            sample[13] = frame.LeftEyeValid ? 1.0 : 0.0;

            sample[14] = frame.RightEyeOrigin.x;
            sample[15] = frame.RightEyeOrigin.y;
            sample[16] = frame.RightEyeOrigin.z;
            sample[17] = frame.RightEyeDirection.x;
            sample[18] = frame.RightEyeDirection.y;
            sample[19] = frame.RightEyeDirection.z;
            sample[20] = frame.RightEyeValid ? 1.0 : 0.0;

            sample[21] = frame.VergenceValid ? frame.VergenceDistance : double.NaN;

            outlet.push_sample(sample, timestamp);

            pushedSampleCount++;

            if (pushedSampleCount % 300 == 0)
            {
                Debug.Log(
                    $"LSL samples pushed: {pushedSampleCount}, " +
                    $"CombinedValid={frame.CombinedValid}, " +
                    $"LeftValid={frame.LeftEyeValid}, " +
                    $"RightValid={frame.RightEyeValid}"
                );
            }
        }

        private void AppendChannel(XMLElement parent, string label, string unit)
        {
            XMLElement ch = parent.append_child("channel");
            ch.append_child_value("label", label);
            ch.append_child_value("unit", unit);
        }

        private void OnDestroy()
        {
            samplingLoopRunning = false;
            outlet = null;
            info = null;
            Debug.Log("LSL outlet closed");
        }
    }
}
