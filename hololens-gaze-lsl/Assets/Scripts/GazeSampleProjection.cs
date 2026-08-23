using System;
using System.Numerics;

namespace GazeLSL
{
    internal struct TrackerSpaceRay
    {
        internal bool IsValid;
        internal Vector3 Origin;
        internal Vector3 Direction;
    }

    internal struct GazeProjectionContext
    {
        internal bool IsValid;
        internal Vector3 PlayspaceFromTrackerPosition;
        internal Quaternion PlayspaceFromTrackerRotation;
        internal Vector3 WorldFromPlayspacePosition;
        internal Quaternion WorldFromPlayspaceRotation;
        internal Vector3 WorldFromPlayspaceScale;
    }

    // Platform-neutral validation, projection, and flat-sample assembly. Unity
    // and WinRT adapters only need to supply System.Numerics values.
    internal static class GazeSampleProjection
    {
        private const float MinimumDirectionMagnitudeSquared = 0.000001f;

        internal static TrackerSpaceRay CreateTrackerSpaceRay(
            bool sourceValid,
            Vector3 origin,
            Vector3 direction)
        {
            return new TrackerSpaceRay
            {
                IsValid = sourceValid &&
                          IsFinite(origin) &&
                          IsFinite(direction) &&
                          direction.LengthSquared() > MinimumDirectionMagnitudeSquared,
                Origin = origin,
                Direction = direction
            };
        }

        internal static GazeProjectionContext CreateProjectionContext(
            Vector3 playspaceFromTrackerPosition,
            Quaternion playspaceFromTrackerRotation,
            Vector3 worldFromPlayspacePosition,
            Quaternion worldFromPlayspaceRotation,
            Vector3 worldFromPlayspaceScale)
        {
            if (!IsFinite(playspaceFromTrackerPosition) ||
                !IsFinite(playspaceFromTrackerRotation) ||
                !IsFinite(worldFromPlayspacePosition) ||
                !IsFinite(worldFromPlayspaceRotation) ||
                !IsFinite(worldFromPlayspaceScale) ||
                playspaceFromTrackerRotation.LengthSquared() <=
                    MinimumDirectionMagnitudeSquared ||
                worldFromPlayspaceRotation.LengthSquared() <=
                    MinimumDirectionMagnitudeSquared)
            {
                return default(GazeProjectionContext);
            }

            return new GazeProjectionContext
            {
                IsValid = true,
                PlayspaceFromTrackerPosition = playspaceFromTrackerPosition,
                PlayspaceFromTrackerRotation = Quaternion.Normalize(
                    playspaceFromTrackerRotation),
                WorldFromPlayspacePosition = worldFromPlayspacePosition,
                WorldFromPlayspaceRotation = Quaternion.Normalize(
                    worldFromPlayspaceRotation),
                WorldFromPlayspaceScale = worldFromPlayspaceScale
            };
        }

        internal static GazeSample CreateInvalidSample(double timestamp)
        {
            return new GazeSample
            {
                Timestamp = timestamp,
                CombinedOriginX = double.NaN,
                CombinedOriginY = double.NaN,
                CombinedOriginZ = double.NaN,
                CombinedDirectionX = double.NaN,
                CombinedDirectionY = double.NaN,
                CombinedDirectionZ = double.NaN,
                LeftEyeOriginX = double.NaN,
                LeftEyeOriginY = double.NaN,
                LeftEyeOriginZ = double.NaN,
                LeftEyeDirectionX = double.NaN,
                LeftEyeDirectionY = double.NaN,
                LeftEyeDirectionZ = double.NaN,
                RightEyeOriginX = double.NaN,
                RightEyeOriginY = double.NaN,
                RightEyeOriginZ = double.NaN,
                RightEyeDirectionX = double.NaN,
                RightEyeDirectionY = double.NaN,
                RightEyeDirectionZ = double.NaN
            };
        }

        internal static GazeSample ProjectSample(
            double timestamp,
            TrackerSpaceRay combined,
            TrackerSpaceRay left,
            TrackerSpaceRay right,
            GazeProjectionContext context)
        {
            GazeSample sample = CreateInvalidSample(timestamp);
            WriteCombinedRay(combined, context, ref sample);
            WriteLeftRay(left, context, ref sample);
            WriteRightRay(right, context, ref sample);
            return sample;
        }

        internal static bool TryProjectTrackerRay(
            TrackerSpaceRay ray,
            GazeProjectionContext context,
            out Vector3 sharedWorldOrigin,
            out Vector3 sharedWorldDirection)
        {
            sharedWorldOrigin = default(Vector3);
            sharedWorldDirection = default(Vector3);

            if (!ray.IsValid || !context.IsValid)
            {
                return false;
            }

            // The Eye Tracking SDK ray is right-handed while the tracker pose
            // is expressed in Unity's left-handed OpenXR playspace.
            Vector3 originInTrackerUnity =
                SharedWorldBasis.ReflectPolarVector(ray.Origin);
            Vector3 directionInTrackerUnity =
                SharedWorldBasis.ReflectPolarVector(ray.Direction);

            Vector3 originInPlayspace =
                Vector3.Transform(
                    originInTrackerUnity,
                    context.PlayspaceFromTrackerRotation) +
                context.PlayspaceFromTrackerPosition;
            Vector3 directionInPlayspace = Vector3.Transform(
                directionInTrackerUnity,
                context.PlayspaceFromTrackerRotation);

            Vector3 scaledOrigin = Multiply(
                originInPlayspace,
                context.WorldFromPlayspaceScale);
            Vector3 scaledDirection = Multiply(
                directionInPlayspace,
                context.WorldFromPlayspaceScale);
            if (scaledDirection.LengthSquared() <=
                MinimumDirectionMagnitudeSquared)
            {
                return false;
            }

            Vector3 originInUnityWorld = Vector3.Transform(
                    scaledOrigin,
                    context.WorldFromPlayspaceRotation) +
                context.WorldFromPlayspacePosition;
            Vector3 directionInUnityWorld = Vector3.Normalize(
                Vector3.Transform(
                    scaledDirection,
                    context.WorldFromPlayspaceRotation));

            sharedWorldOrigin =
                SharedWorldBasis.ReflectPolarVector(originInUnityWorld);
            sharedWorldDirection = Vector3.Normalize(
                SharedWorldBasis.ReflectPolarVector(directionInUnityWorld));
            return IsFinite(sharedWorldOrigin) &&
                   IsFinite(sharedWorldDirection);
        }

        private static void WriteCombinedRay(
            TrackerSpaceRay ray,
            GazeProjectionContext context,
            ref GazeSample sample)
        {
            Vector3 origin;
            Vector3 direction;
            if (!TryProjectTrackerRay(ray, context, out origin, out direction))
            {
                return;
            }

            sample.CombinedOriginX = origin.X;
            sample.CombinedOriginY = origin.Y;
            sample.CombinedOriginZ = origin.Z;
            sample.CombinedDirectionX = direction.X;
            sample.CombinedDirectionY = direction.Y;
            sample.CombinedDirectionZ = direction.Z;
            sample.CombinedValid = true;
        }

        private static void WriteLeftRay(
            TrackerSpaceRay ray,
            GazeProjectionContext context,
            ref GazeSample sample)
        {
            Vector3 origin;
            Vector3 direction;
            if (!TryProjectTrackerRay(ray, context, out origin, out direction))
            {
                return;
            }

            sample.LeftEyeOriginX = origin.X;
            sample.LeftEyeOriginY = origin.Y;
            sample.LeftEyeOriginZ = origin.Z;
            sample.LeftEyeDirectionX = direction.X;
            sample.LeftEyeDirectionY = direction.Y;
            sample.LeftEyeDirectionZ = direction.Z;
            sample.LeftEyeValid = true;
        }

        private static void WriteRightRay(
            TrackerSpaceRay ray,
            GazeProjectionContext context,
            ref GazeSample sample)
        {
            Vector3 origin;
            Vector3 direction;
            if (!TryProjectTrackerRay(ray, context, out origin, out direction))
            {
                return;
            }

            sample.RightEyeOriginX = origin.X;
            sample.RightEyeOriginY = origin.Y;
            sample.RightEyeOriginZ = origin.Z;
            sample.RightEyeDirectionX = direction.X;
            sample.RightEyeDirectionY = direction.Y;
            sample.RightEyeDirectionZ = direction.Z;
            sample.RightEyeValid = true;
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
            return IsFinite(value.X) &&
                   IsFinite(value.Y) &&
                   IsFinite(value.Z);
        }

        private static bool IsFinite(Quaternion value)
        {
            return IsFinite(value.X) &&
                   IsFinite(value.Y) &&
                   IsFinite(value.Z) &&
                   IsFinite(value.W);
        }

        private static bool IsFinite(float value)
        {
            return !float.IsNaN(value) && !float.IsInfinity(value);
        }
    }
}
