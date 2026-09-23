using System.Numerics;

namespace GazeLSL
{
    // The shared HoloLens/Vicon output is the Unity world with Z flipped, which
    // makes it right-handed. Keep the position and rotation rules together so
    // every publisher flips the same way.
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
            // Flipping Z on a rotation (F * R * F with F = diag(1, 1, -1)) negates
            // the quaternion's X and Y and keeps Z and W.
            reflectedX = -x;
            reflectedY = -y;
            reflectedZ = z;
            reflectedW = w;
        }
    }
}
