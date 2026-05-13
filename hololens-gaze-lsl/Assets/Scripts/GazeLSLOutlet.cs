using System;
using LSL;
using UnityEngine;

namespace GazeLSL
{
    /*
    Publishes HoloLens 2 Extended Eye Tracking readings to LSL from Unity's
    frame loop. Application.targetFrameRate is set from the config, and Update()
    attempts to push one fresh tracker reading per frame.
    */
    public sealed class GazeLSLOutlet : MonoBehaviour
    {
        private const int ChannelCount = 22;

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
        private double[] sampleBuffer;
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
            ConfigureFrameLoop(nominalRate);

            sampleBuffer = new double[ChannelCount];
            CreateOutlet();
        }

        private void Update()
        {
            if (outlet == null || sampleBuffer == null)
            {
                return;
            }

            if (!gazeProvider.TryGetNextSample(out GazeDataProvider.GazeSample gazeSample))
            {
                return;
            }

            WriteSampleBuffer(gazeSample, sampleBuffer);
            outlet.push_sample(sampleBuffer, EstimateLslTimestamp(gazeSample));
            pushedSampleCount++;
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

        private static void ConfigureFrameLoop(uint rate)
        {
            QualitySettings.vSyncCount = 0;
            Application.targetFrameRate = (int)rate;
            Debug.Log($"Unity frame loop target set to {rate} FPS");
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
            acquisition.append_child_value("acquisition_mode", "unity_update_loop");
            acquisition.append_child_value("timestamp", "lsl_clock_backdated_to_tracker_reading_time");
        }

        private static double EstimateLslTimestamp(GazeDataProvider.GazeSample gazeSample)
        {
            double nowLsl = LSL.LSL.local_clock();
            double ageSeconds = Math.Max(0.0, (DateTime.Now - gazeSample.TrackerTimestamp).TotalSeconds);
            return nowLsl - ageSeconds;
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
            outlet = null;
            info = null;
            sampleBuffer = null;

            Debug.Log($"LSL outlet closed after pushing {pushedSampleCount} gaze samples");
        }
    }
}
