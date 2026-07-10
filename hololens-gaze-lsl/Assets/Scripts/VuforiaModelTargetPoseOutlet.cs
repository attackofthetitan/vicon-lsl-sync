using System;
using LSL;
using UnityEngine;
using Vuforia;

namespace GazeLSL
{
    /*
    Publishes the Vuforia stair model target pose as an independent LSL stream.
    The target and GazeDataProvider must run in the same Unity/XR world. Output
    coordinates use the existing gaze convention: metres, with Unity Z flipped.
    */
    public sealed class VuforiaModelTargetPoseOutlet : MonoBehaviour
    {
        private const int ChannelCount = 8;

        private static readonly string[] ChannelLabels =
        {
            "PositionX", "PositionY", "PositionZ",
            "RotationX", "RotationY", "RotationZ", "RotationW",
            "Tracked"
        };

        private static readonly string[] ChannelUnits =
        {
            "meters", "meters", "meters",
            "normalized", "normalized", "normalized", "normalized",
            "bool"
        };

        [SerializeField] private GazeLSLConfig config;
        [SerializeField] private ObserverBehaviour modelTarget;

        private StreamInfo info;
        private StreamOutlet outlet;
        private double nextPublishTime;
        private double publishIntervalSeconds;
        private readonly double[] sampleBuffer = new double[ChannelCount];

        private void Start()
        {
            if (config == null || modelTarget == null)
            {
                Debug.LogError("VuforiaModelTargetPoseOutlet requires a GazeLSLConfig and Vuforia Model Target.");
                enabled = false;
                return;
            }

            uint rate = Math.Max(1u, config.TargetFrameRate);
            publishIntervalSeconds = 1.0 / rate;
            info = new StreamInfo(
                config.ModelTargetStreamName,
                config.ModelTargetStreamType,
                ChannelCount,
                rate,
                channel_format_t.cf_double64,
                config.ModelTargetSourceId
            );
            AppendMetadata(info, rate);
            outlet = new StreamOutlet(info);
            Debug.Log($"Model target LSL outlet created: {config.ModelTargetStreamName}, {ChannelCount} channels, nominal {rate} Hz");
        }

        private static void AppendMetadata(StreamInfo streamInfo, uint rate)
        {
            XMLElement channels = streamInfo.desc().append_child("channels");
            for (int i = 0; i < ChannelCount; i++)
            {
                XMLElement channel = channels.append_child("channel");
                channel.append_child_value("label", ChannelLabels[i]);
                channel.append_child_value("unit", ChannelUnits[i]);
            }

            XMLElement acquisition = streamInfo.desc().append_child("acquisition");
            acquisition.append_child_value("device", "HoloLens2");
            acquisition.append_child_value("sdk", "Vuforia.ModelTarget");
            acquisition.append_child_value("reference_target", "stair_model_target");
            acquisition.append_child_value("coordinate_frame", "hololens_stationary_shared_with_gaze");
            acquisition.append_child_value("coordinate_units", "meters");
            acquisition.append_child_value("nominal_srate", rate.ToString());
            acquisition.append_child_value("timestamp", "lsl_local_clock_at_push");
        }

        private void Update()
        {
            double now = Time.unscaledTime;
            if (outlet == null || now < nextPublishTime)
            {
                return;
            }
            nextPublishTime = now + publishIntervalSeconds;

            bool tracked = IsTracked(modelTarget.TargetStatus.Status);
            if (tracked)
            {
                WriteTrackedPose(modelTarget.transform, sampleBuffer);
            }
            else
            {
                for (int i = 0; i < 7; i++)
                {
                    sampleBuffer[i] = double.NaN;
                }
                sampleBuffer[7] = 0.0;
            }
            outlet.push_sample(sampleBuffer);
        }

        private static bool IsTracked(Status status)
        {
            return status == Status.TRACKED || status == Status.EXTENDED_TRACKED;
        }

        private static void WriteTrackedPose(Transform targetTransform, double[] sample)
        {
            Vector3 position = targetTransform.position;
            Quaternion rotation = targetTransform.rotation;

            sample[0] = position.x;
            sample[1] = position.y;
            sample[2] = -position.z;

            // Change basis with F = diag(1, 1, -1), matching GazeDataProvider.
            // Quaternion vector components are axial under this reflection.
            sample[3] = -rotation.x;
            sample[4] = -rotation.y;
            sample[5] = rotation.z;
            sample[6] = rotation.w;
            sample[7] = 1.0;
        }

        private void OnDestroy()
        {
            outlet = null;
            info = null;
        }
    }
}
