using System.Numerics;

namespace GazeLSL
{
    // Shared HoloLens/Vicon output uses the right-handed reflection of Unity
    // world space across Z. Keep the polar-vector and rotation rules together
    // so every publisher applies the same basis change.
    internal static class SharedWorldBasis
    {
        internal static Vector3 ReflectPolarVector(Vector3 value)
        {
            return new Vector3(value.X, value.Y, -value.Z);
        }

        internal static void ReflectPolarVector(
            double x,
            double y,
            double z,
            out double reflectedX,
            out double reflectedY,
            out double reflectedZ)
        {
            reflectedX = x;
            reflectedY = y;
            reflectedZ = -z;
        }

        internal static void ReflectRotation(
            double x,
            double y,
            double z,
            double w,
            out double reflectedX,
            out double reflectedY,
            out double reflectedZ,
            out double reflectedW)
        {
            // For F = diag(1, 1, -1), F * R * F maps quaternion vector
            // components as an axial vector while preserving the scalar part.
            reflectedX = -x;
            reflectedY = -y;
            reflectedZ = z;
            reflectedW = w;
        }
    }
}
