using UnityEngine;

namespace GazeLSL
{
    [CreateAssetMenu(fileName = "GazeLSLConfig", menuName = "LSL/Gaze Config")]
    public class GazeLSLConfig : ScriptableObject
    {
        [Header("LSL Stream Settings")]
        public string StreamName = "HoloLensGaze";
        public string StreamType = "Gaze";
        public string SourceId = "hololens2_gaze";

        [Header("Eye Tracking Settings")]
        [Tooltip("Target frame rate for Extended Eye Tracking (30, 60, or 90)")]
        public uint TargetFrameRate = 90;

        [Header("Gaze Raycast Settings")]
        public bool IncludeHitPoint = true;
        public float MaxRaycastDistance = 10.0f;
        public LayerMask RaycastLayerMask = ~0;
    }
}
