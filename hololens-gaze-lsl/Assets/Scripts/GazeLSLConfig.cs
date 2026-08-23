using UnityEngine;

namespace GazeLSL
{
    [CreateAssetMenu(fileName = "GazeLSLConfig", menuName = "LSL/Gaze Config")]
    public sealed class GazeLSLConfig : ScriptableObject
    {
        [Header("LSL Stream Settings")]
        public string StreamName = GazeStreamContract.StreamName;
        public string StreamType = GazeStreamContract.StreamType;
        public string SourceId = GazeStreamContract.SourceId;

        [Header("Vuforia Model Target LSL Stream")]
        public string ModelTargetStreamName = ModelTargetStreamContract.StreamName;
        public string ModelTargetStreamType = ModelTargetStreamContract.StreamType;
        public string ModelTargetSourceId = ModelTargetStreamContract.SourceId;
    }
}
