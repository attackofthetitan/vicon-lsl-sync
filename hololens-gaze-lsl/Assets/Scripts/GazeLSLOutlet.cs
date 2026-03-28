using UnityEngine;
using LSL;

namespace GazeLSL
{
    /*
    Pushes HoloLens 2 eye gaze data to an LSL outlet.
    26 channels: combined gaze, per-eye gaze, hit point, vergence.
    Attach to a GameObject alongside GazeDataProvider.
    */
    public class GazeLSLOutlet : MonoBehaviour
    {
        [SerializeField] private GazeLSLConfig config;
        [SerializeField] private GazeDataProvider gazeProvider;

        private liblsl.StreamOutlet outlet;
        private liblsl.StreamInfo info;
        private double[] sample;

        /*
        0-2 combined origin, 3-5 combined direction, 6 combined valid,
        7-9 left origin, 10-12 left direction, 13 left valid,
        14-16 right origin, 17-19 right direction, 20 right valid,
        21-23 hit point, 24 hit valid, 25 vergence distance
        */
        private const int ChannelCount = 26;

        private void Start()
        {
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

            info = new liblsl.StreamInfo(
                config.StreamName,
                config.StreamType,
                ChannelCount,
                liblsl.IRREGULAR_RATE,
                liblsl.channel_format_t.cf_double64,
                config.SourceId
            );

            liblsl.XMLElement channels = info.desc().append_child("channels");
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
            AppendChannel(channels, "HitPointX", "meters");
            AppendChannel(channels, "HitPointY", "meters");
            AppendChannel(channels, "HitPointZ", "meters");
            AppendChannel(channels, "HitValid", "bool");
            AppendChannel(channels, "VergenceDistance", "meters");

            liblsl.XMLElement meta = info.desc().append_child("acquisition");
            meta.append_child_value("device", "HoloLens2");
            meta.append_child_value("sdk", "ExtendedEyeTracking");

            sample = new double[ChannelCount];
            outlet = new liblsl.StreamOutlet(info);

            Debug.Log($"LSL outlet created - {config.StreamName}, {ChannelCount} channels");
        }

        private void LateUpdate()
        {
            if (outlet == null || gazeProvider == null) return;

            var frame = gazeProvider.GetCurrentFrame();
            double timestamp = liblsl.local_clock();

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

            sample[21] = frame.HitPoint.x;
            sample[22] = frame.HitPoint.y;
            sample[23] = frame.HitPoint.z;
            sample[24] = frame.HitValid ? 1.0 : 0.0;

            sample[25] = frame.VergenceValid ? frame.VergenceDistance : double.NaN;

            outlet.push_sample(sample, timestamp);
        }

        private void AppendChannel(liblsl.XMLElement parent, string label, string unit)
        {
            liblsl.XMLElement ch = parent.append_child("channel");
            ch.append_child_value("label", label);
            ch.append_child_value("unit", unit);
        }

        private void OnDestroy()
        {
            outlet = null;
            info = null;
            Debug.Log("LSL outlet closed");
        }
    }
}
