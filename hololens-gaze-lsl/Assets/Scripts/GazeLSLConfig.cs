using UnityEngine;

namespace GazeLSL
{
    [CreateAssetMenu(fileName = "GazeLSLConfig", menuName = "LSL/Gaze Config")]
    public class GazeLSLConfig : ScriptableObject
    {
        [Header("Desktop Bridge Settings")]
        [Tooltip("IP address or hostname of the desktop running vicon-lsl-bridge")]
        public string RelayHost = "192.168.1.100";
        public int RelayPort = 16571;

        [Header("Eye Tracking Settings")]
        [Tooltip("Target frame rate for Extended Eye Tracking (30, 60, or 90)")]
        public uint TargetFrameRate = 90;

        [Header("Gaze Raycast Settings")]
        public bool IncludeHitPoint = true;
        public float MaxRaycastDistance = 10.0f;
        public LayerMask RaycastLayerMask = ~0;
    }
}
