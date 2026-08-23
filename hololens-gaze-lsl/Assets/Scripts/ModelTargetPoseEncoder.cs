using System;

namespace GazeLSL
{
    public static class ModelTargetPoseEncoder
    {
        public const int ChannelCount = 8;

        public static void WriteSample(
            bool tracked,
            double positionX,
            double positionY,
            double positionZ,
            double rotationX,
            double rotationY,
            double rotationZ,
            double rotationW,
            double[] sample)
        {
            if (sample == null)
            {
                throw new ArgumentNullException(nameof(sample));
            }

            if (sample.Length < ChannelCount)
            {
                throw new ArgumentException("The pose sample buffer must contain at least 8 elements.", nameof(sample));
            }

            if (!tracked)
            {
                for (int i = 0; i < ChannelCount - 1; i++)
                {
                    sample[i] = double.NaN;
                }

                sample[7] = 0.0;
                return;
            }

            SharedWorldBasis.ReflectPolarVector(
                positionX,
                positionY,
                positionZ,
                out sample[0],
                out sample[1],
                out sample[2]);
            SharedWorldBasis.ReflectRotation(
                rotationX,
                rotationY,
                rotationZ,
                rotationW,
                out sample[3],
                out sample[4],
                out sample[5],
                out sample[6]);
            sample[7] = 1.0;
        }
    }
}
