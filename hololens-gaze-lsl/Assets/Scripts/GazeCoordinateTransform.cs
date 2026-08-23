using System.Numerics;

namespace GazeLSL
{
    public static class GazeCoordinateTransform
    {
        public static bool TryTransformTrackerRayToSharedWorld(
            Vector3 trackerOrigin,
            Vector3 trackerDirection,
            Vector3 playspaceFromTrackerPosition,
            Quaternion playspaceFromTrackerRotation,
            Vector3 worldFromPlayspacePosition,
            Quaternion worldFromPlayspaceRotation,
            Vector3 worldFromPlayspaceScale,
            out Vector3 sharedWorldOrigin,
            out Vector3 sharedWorldDirection)
        {
            TrackerSpaceRay ray = GazeSampleProjection.CreateTrackerSpaceRay(
                true,
                trackerOrigin,
                trackerDirection);
            GazeProjectionContext context =
                GazeSampleProjection.CreateProjectionContext(
                    playspaceFromTrackerPosition,
                    playspaceFromTrackerRotation,
                    worldFromPlayspacePosition,
                    worldFromPlayspaceRotation,
                    worldFromPlayspaceScale);
            return GazeSampleProjection.TryProjectTrackerRay(
                ray,
                context,
                out sharedWorldOrigin,
                out sharedWorldDirection);
        }
    }
}
