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
        private const int ChannelCount = ModelTargetPoseEncoder.ChannelCount;

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
        private readonly double[] sampleBuffer = new double[ChannelCount];

        private void Start()
        {
            if (config == null || modelTarget == null)
            {
                Debug.LogError("VuforiaModelTargetPoseOutlet requires a GazeLSLConfig and Vuforia Model Target.");
                enabled = false;
                return;
            }

            try
            {
                info = new StreamInfo(
                    config.ModelTargetStreamName,
                    config.ModelTargetStreamType,
                    ChannelCount,
                    LSL.LSL.IRREGULAR_RATE,
                    channel_format_t.cf_double64,
                    config.ModelTargetSourceId
                );
                AppendMetadata(info);
                outlet = new StreamOutlet(info);
                Debug.Log($"Model target LSL outlet created: {config.ModelTargetStreamName}, {ChannelCount} channels, irregular rate");
            }
            catch (Exception e)
            {
                DisableWithError($"Could not create model target LSL outlet - {e.Message}");
            }
        }

        private static void AppendMetadata(StreamInfo streamInfo)
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
            acquisition.append_child_value("nominal_srate", "0");
            acquisition.append_child_value("acquisition_mode", "unity_rendered_pose");
            acquisition.append_child_value("timestamp", "lsl_local_clock_at_transform_read");
            acquisition.append_child_value("clock_domain", "lsl_local_clock");

            XMLElement synchronization = streamInfo.desc().append_child("synchronization");
            synchronization.append_child_value("clock_domain", "lsl_local_clock");
            synchronization.append_child_value("timestamp_origin", "local_clock_at_transform_read");
            synchronization.append_child_value("offset_mean", "0");
            synchronization.append_child_value("can_drop_samples", "true");
        }

        private void LateUpdate()
        {
            if (outlet == null)
            {
                return;
            }

            bool tracked = IsTracked(modelTarget.TargetStatus.Status);
            Transform targetTransform = modelTarget.transform;
            Vector3 position = targetTransform.position;
            Quaternion rotation = targetTransform.rotation;
            double sampleTimestamp = LSL.LSL.local_clock();
            ModelTargetPoseEncoder.WriteSample(
                tracked,
                position.x,
                position.y,
                position.z,
                rotation.x,
                rotation.y,
                rotation.z,
                rotation.w,
                sampleBuffer
            );

            try
            {
                outlet.push_sample(sampleBuffer, sampleTimestamp);
            }
            catch (Exception e)
            {
                DisableWithError($"Could not publish model target pose - {e.Message}");
            }
        }

        private static bool IsTracked(Status status)
        {
            return status == Status.TRACKED || status == Status.EXTENDED_TRACKED;
        }

        private void DisableWithError(string message)
        {
            Debug.LogError(message);
            enabled = false;
        }

        private void OnDestroy()
        {
            outlet = null;
            info = null;
        }
    }
}
