using System;
using System.Collections.Generic;

namespace GazeLSL
{
    // A fixed stair reference remains useful in the same Unity world after
    // Vuforia is deliberately disabled. Tracking loss alone never publishes it.
    public sealed class ModelTargetPoseReference
    {
        public const int RequiredSamples = 20;
        public const double TranslationToleranceMeters = 0.02;
        public const double RotationToleranceDegrees = 3.0;
        public const double FrozenState = 2.0;

        private readonly List<double[]> window = new List<double[]>(RequiredSamples);
        private double[] reference;
        private bool wasPaused;

        public bool HasReference => reference != null;

        public void Reset()
        {
            window.Clear();
            reference = null;
            wasPaused = false;
        }

        // sample is already encoded in the shared right-handed world. Never
        // reflect the saved pose again, or use a disabled target's current pose.
        public void Update(bool paused, double[] sample)
        {
            if (sample == null || sample.Length < ModelTargetPoseEncoder.ChannelCount)
            {
                throw new ArgumentException("A complete target sample is required.", nameof(sample));
            }
            if (paused)
            {
                wasPaused = true;
                if (reference != null)
                {
                    Array.Copy(reference, sample, 7);
                    sample[7] = FrozenState;
                }
                else
                {
                    Invalidate(sample);
                }
                return;
            }
            if (wasPaused) Reset(); // Resuming requires a new stable acquisition.
            if (sample[7] != 1.0)
            {
                window.Clear();
                Invalidate(sample);
                return;
            }

            double normSquared = 0.0;
            for (int i = 0; i < 7; ++i)
            {
                if (double.IsNaN(sample[i]) || double.IsInfinity(sample[i]))
                {
                    Reset();
                    Invalidate(sample);
                    return;
                }
                if (i >= 3) normSquared += sample[i] * sample[i];
            }
            if (double.IsInfinity(normSquared) || normSquared <= 1e-24)
            {
                Reset();
                Invalidate(sample);
                return;
            }
            var pose = new double[7];
            Array.Copy(sample, pose, 7);
            double norm = Math.Sqrt(normSquared);
            for (int i = 3; i < 7; ++i) pose[i] /= norm;
            if (reference != null && !WithinTolerance(reference, pose)) reference = null;
            if (window.Count > 0 && !WithinTolerance(window[0], pose)) window.Clear();
            window.Add(pose);
            if (window.Count < RequiredSamples) return;

            var average = new double[7];
            foreach (double[] item in window)
            {
                double dot = 0.0;
                for (int i = 3; i < 7; ++i) dot += window[0][i] * item[i];
                for (int i = 0; i < 3; ++i) average[i] += item[i] / window.Count;
                for (int i = 3; i < 7; ++i) average[i] += (dot < 0.0 ? -1.0 : 1.0) * item[i];
            }
            normSquared = 0.0;
            for (int i = 3; i < 7; ++i) normSquared += average[i] * average[i];
            norm = Math.Sqrt(normSquared);
            for (int i = 3; i < 7; ++i) average[i] /= norm;
            reference = average;
            window.RemoveAt(0);
        }

        private static bool WithinTolerance(double[] left, double[] right)
        {
            double distanceSquared = 0.0, dot = 0.0;
            for (int i = 0; i < 3; ++i) distanceSquared += (left[i] - right[i]) * (left[i] - right[i]);
            for (int i = 3; i < 7; ++i) dot += left[i] * right[i];
            double angle = 2.0 * Math.Acos(Math.Min(1.0, Math.Abs(dot))) * 180.0 / Math.PI;
            return distanceSquared <= TranslationToleranceMeters * TranslationToleranceMeters &&
                   angle <= RotationToleranceDegrees;
        }

        private static void Invalidate(double[] sample)
        {
            for (int i = 0; i < 7; ++i) sample[i] = double.NaN;
            sample[7] = 0.0;
        }
    }
}
