using System;
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
            sharedWorldOrigin = default(Vector3);
            sharedWorldDirection = default(Vector3);

            if (!IsFinite(trackerOrigin) ||
                !IsFinite(trackerDirection) ||
                !IsFinite(playspaceFromTrackerPosition) ||
                !IsFinite(playspaceFromTrackerRotation) ||
                !IsFinite(worldFromPlayspacePosition) ||
                !IsFinite(worldFromPlayspaceRotation) ||
                !IsFinite(worldFromPlayspaceScale) ||
                trackerDirection.LengthSquared() <= 0.000001f ||
                playspaceFromTrackerRotation.LengthSquared() <= 0.000001f ||
                worldFromPlayspaceRotation.LengthSquared() <= 0.000001f)
            {
                return false;
            }

            Quaternion trackerRotation = Quaternion.Normalize(
                playspaceFromTrackerRotation);
            Quaternion playspaceRotation = Quaternion.Normalize(
                worldFromPlayspaceRotation);

            // The Eye Tracking SDK returns a right-handed tracker ray. The
            // Mixed Reality OpenXR SpatialGraphNode pose is expressed in
            // Unity's left-handed playspace, so mirror Z before applying it.
            Vector3 originInTrackerUnity = ReflectZ(trackerOrigin);
            Vector3 directionInTrackerUnity = ReflectZ(trackerDirection);

            Vector3 originInPlayspace =
                Vector3.Transform(originInTrackerUnity, trackerRotation) +
                playspaceFromTrackerPosition;
            Vector3 directionInPlayspace =
                Vector3.Transform(directionInTrackerUnity, trackerRotation);

            Vector3 scaledOrigin = Multiply(
                originInPlayspace,
                worldFromPlayspaceScale);
            Vector3 scaledDirection = Multiply(
                directionInPlayspace,
                worldFromPlayspaceScale);
            if (scaledDirection.LengthSquared() <= 0.000001f)
            {
                return false;
            }

            Vector3 originInUnityWorld =
                Vector3.Transform(scaledOrigin, playspaceRotation) +
                worldFromPlayspacePosition;
            Vector3 directionInUnityWorld = Vector3.Normalize(
                Vector3.Transform(scaledDirection, playspaceRotation));

            // ModelTargetPoseEncoder publishes the right-handed reflection of
            // Unity world space. Apply the same basis change to the gaze ray.
            sharedWorldOrigin = ReflectZ(originInUnityWorld);
            sharedWorldDirection = Vector3.Normalize(
                ReflectZ(directionInUnityWorld));
            return IsFinite(sharedWorldOrigin) && IsFinite(sharedWorldDirection);
        }

        private static Vector3 ReflectZ(Vector3 value)
        {
            return new Vector3(value.X, value.Y, -value.Z);
        }

        private static Vector3 Multiply(Vector3 left, Vector3 right)
        {
            return new Vector3(
                left.X * right.X,
                left.Y * right.Y,
                left.Z * right.Z);
        }

        private static bool IsFinite(Vector3 value)
        {
            return IsFinite(value.X) && IsFinite(value.Y) && IsFinite(value.Z);
        }

        private static bool IsFinite(Quaternion value)
        {
            return IsFinite(value.X) && IsFinite(value.Y) &&
                   IsFinite(value.Z) && IsFinite(value.W);
        }

        private static bool IsFinite(float value)
        {
            return !float.IsNaN(value) && !float.IsInfinity(value);
        }
    }
}
