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

            sample[0] = positionX;
            sample[1] = positionY;
            sample[2] = -positionZ;

            // Change basis with F = diag(1, 1, -1), matching GazeDataProvider.
            // Quaternion vector components are axial under this reflection.
            sample[3] = -rotationX;
            sample[4] = -rotationY;
            sample[5] = rotationZ;
            sample[6] = rotationW;
            sample[7] = 1.0;
        }
    }
}
