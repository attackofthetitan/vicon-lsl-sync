using System;
using LSL;
using UnityEngine;

namespace GazeLSL
{
    /*
    Publishes HoloLens 2 Extended Eye Tracking readings to LSL from a dedicated
    worker thread so gaze publishing is not limited by the Unity render frame rate.
    */
    public sealed class GazeLSLOutlet : MonoBehaviour
    {
        private const int ChannelCount = GazeSampleEncoder.ChannelCount;
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
        private GazePublisherWorker worker;
        private uint nominalRate;
        private bool failureReported;

        private void Start()
        {
            if (!ValidateReferences())
            {
                enabled = false;
                return;
            }

            nominalRate = Math.Max(1u, config.TargetFrameRate);

            try
            {
                CreateOutlet();
                worker = new GazePublisherWorker(
                    gazeProvider,
                    new LslSampleOutlet(outlet),
                    nominalRate
                );
                worker.Start();
            }
            catch (Exception e)
            {
                Debug.LogError($"Could not start gaze LSL publishing - {e.Message}");
                enabled = false;
            }
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

        private void Update()
        {
            if (!failureReported && worker != null && worker.Failure != null)
            {
                failureReported = true;
                Debug.LogError($"Gaze LSL publisher stopped after an error - {worker.Failure.Message}");
                enabled = false;
            }
        }

        private sealed class LslSampleOutlet : IGazeSampleOutlet
        {
            private readonly StreamOutlet streamOutlet;

            public LslSampleOutlet(StreamOutlet streamOutlet)
            {
                this.streamOutlet = streamOutlet;
            }

            public void PushSample(double[] sample)
            {
                streamOutlet.push_sample(sample);
            }
        }

        private void OnDestroy()
        {
            GazePublisherWorker currentWorker = worker;
            if (currentWorker != null && !currentWorker.Stop(StopTimeoutMilliseconds))
            {
                Debug.LogWarning("Gaze LSL worker did not stop before timeout; its resources remain owned until it exits.");
                return;
            }

            int pushedSampleCount = currentWorker != null ? currentWorker.PushedSampleCount : 0;
            worker = null;
            outlet = null;
            info = null;

            Debug.Log($"LSL outlet closed after pushing {pushedSampleCount} gaze samples");
        }
    }
}
