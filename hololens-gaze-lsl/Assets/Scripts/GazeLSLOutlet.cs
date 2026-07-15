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
        private const int ChannelCount = GazeStreamContract.ChannelCount;
        private const int StopTimeoutMilliseconds = 500;

        [SerializeField] private GazeLSLConfig config;
        [SerializeField] private GazeDataProvider gazeProvider;

        private StreamInfo info;
        private StreamOutlet outlet;
        private GazePublisherWorker worker;
        private uint nominalRate;
        private long sessionGeneration;
        private bool failureReported;
        private bool startRequested;
        private bool stopWarningReported;
        private int reportedProviderExceptionCount;

        private void Start()
        {
            if (!ValidateReferences())
            {
                enabled = false;
                return;
            }

            startRequested = true;
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
            AppendAcquisitionMetadata(
                info.desc().append_child("acquisition"),
                info.desc().append_child("synchronization"),
                nominalRate);

            outlet = new StreamOutlet(info);

            Debug.Log($"LSL outlet created: {config.StreamName}, {ChannelCount} channels, nominal {nominalRate} Hz");
        }

        private static void AppendChannelMetadata(XMLElement channels)
        {
            for (int i = 0; i < ChannelCount; i++)
            {
                XMLElement channel = channels.append_child("channel");
                channel.append_child_value("label", GazeStreamContract.Labels[i]);
                channel.append_child_value("unit", GazeStreamContract.Units[i]);
            }
        }

        private static void AppendAcquisitionMetadata(
            XMLElement acquisition,
            XMLElement synchronization,
            uint rate)
        {
            acquisition.append_child_value("device", "HoloLens2");
            acquisition.append_child_value("sdk", "Microsoft.MixedReality.EyeTracking");
            acquisition.append_child_value("nominal_srate", rate.ToString());
            acquisition.append_child_value("acquisition_mode", "worker_thread_rate_limited");
            acquisition.append_child_value("timestamp", "mapped_system_relative_time");
            acquisition.append_child_value("clock_domain", "lsl_local_clock");
            acquisition.append_child_value("timestamp_fallback", "immediate_push_time");

            synchronization.append_child_value("clock_domain", "lsl_local_clock");
            synchronization.append_child_value(
                "timestamp_origin", "system_relative_eye_time_mapped_with_qpc_lsl_midpoint");
            synchronization.append_child_value("offset_mean", "0");
            synchronization.append_child_value("can_drop_samples", "true");
        }

        private void Update()
        {
            if (worker != null &&
                worker.ProviderExceptionCount > reportedProviderExceptionCount)
            {
                reportedProviderExceptionCount = worker.ProviderExceptionCount;
                Exception providerException = worker.LastProviderException;
                Debug.LogWarning(
                    $"Eye tracker read threw an exception; publisher will retry - {providerException}"
                );
            }

            if (worker != null && worker.Failure != null)
            {
                if (!failureReported)
                {
                    failureReported = true;
                    Debug.LogError($"Gaze LSL publisher stopped after an error - {worker.Failure}");
                }

                if (StopPublishing())
                {
                    enabled = false;
                }
                return;
            }

            uint effectiveRate;
            long currentSessionGeneration;
            bool rateReady = gazeProvider.TryGetEffectiveFrameRate(
                out effectiveRate,
                out currentSessionGeneration);
            if (worker != null &&
                (!rateReady ||
                 effectiveRate != nominalRate ||
                 currentSessionGeneration != sessionGeneration))
            {
                if (!StopPublishing())
                {
                    return;
                }
            }

            if (startRequested && worker == null && rateReady)
            {
                StartPublishing(effectiveRate, currentSessionGeneration);
            }
        }

        private void StartPublishing(uint effectiveRate, long currentSessionGeneration)
        {
            try
            {
                nominalRate = effectiveRate;
                sessionGeneration = currentSessionGeneration;
                CreateOutlet();
                worker = new GazePublisherWorker(
                    gazeProvider,
                    new LslSampleOutlet(outlet),
                    nominalRate,
                    () => LSL.LSL.local_clock()
                );
                worker.Start();
                failureReported = false;
                reportedProviderExceptionCount = 0;
            }
            catch (Exception e)
            {
                worker = null;
                outlet = null;
                info = null;
                nominalRate = 0u;
                sessionGeneration = 0L;
                Debug.LogError($"Could not start gaze LSL publishing - {e.Message}");
                enabled = false;
            }
        }

        private bool StopPublishing()
        {
            GazePublisherWorker currentWorker = worker;
            if (currentWorker != null && !currentWorker.Stop(StopTimeoutMilliseconds))
            {
                if (!stopWarningReported)
                {
                    stopWarningReported = true;
                    Debug.LogWarning("Gaze LSL worker did not stop before timeout; waiting before replacing its outlet.");
                }
                return false;
            }

            int pushedSampleCount = currentWorker != null ? currentWorker.PushedSampleCount : 0;
            worker = null;
            outlet = null;
            info = null;
            nominalRate = 0u;
            sessionGeneration = 0L;
            failureReported = false;
            stopWarningReported = false;
            reportedProviderExceptionCount = 0;

            if (currentWorker != null)
            {
                Debug.Log($"LSL outlet closed after pushing {pushedSampleCount} gaze samples");
            }
            return true;
        }

        private sealed class LslSampleOutlet : IGazeSampleOutlet
        {
            private readonly StreamOutlet streamOutlet;

            public LslSampleOutlet(StreamOutlet streamOutlet)
            {
                this.streamOutlet = streamOutlet;
            }

            public void PushSample(double[] sample, double timestamp)
            {
                streamOutlet.push_sample(sample, timestamp);
            }
        }

        private void OnDestroy()
        {
            startRequested = false;
            StopPublishing();
        }
    }
}
