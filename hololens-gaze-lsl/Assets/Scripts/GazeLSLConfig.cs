using UnityEngine;

namespace GazeLSL
{
    [CreateAssetMenu(fileName = "GazeLSLConfig", menuName = "LSL/Gaze Config")]
    public sealed class GazeLSLConfig : ScriptableObject
    {
        [Header("LSL Stream Settings")]
        public string StreamName = "HoloLensGaze";
        public string StreamType = "Gaze";
        public string SourceId = "hololens2_gaze";

        [Header("Eye Tracking Settings")]
        [Tooltip("Requested Extended Eye Tracking frame rate. HoloLens 2 commonly supports 30, 60, and 90 Hz.")]
        public uint TargetFrameRate = 90;
    }
}
