using System;
using GazeLSL;

internal static partial class Program
{
    private static double[] TargetSample(bool tracked = true, double x = 1.0,
                                         double qz = 0.0, double qw = 1.0)
    {
        var sample = new double[8];
        ModelTargetPoseEncoder.WriteSample(tracked, x, 2.0, 3.0, 0.0, 0.0, qz, qw, sample);
        return sample;
    }

    private static void AcquireTargetReference(ModelTargetPoseReference reference, double x = 1.0)
    {
        for (int i = 0; i < ModelTargetPoseReference.RequiredSamples; ++i)
            reference.Update(false, TargetSample(x: x));
    }

    private static void TargetReferenceSurvivesExplicitPauseOnly()
    {
        var reference = new ModelTargetPoseReference();
        AcquireTargetReference(reference);
        True(reference.HasReference, "A stable window should produce a reference.");
        double[] lost = TargetSample(false);
        reference.Update(false, lost);
        Equal(0.0, lost[7]);
        True(double.IsNaN(lost[0]), "Ordinary tracking loss must remain invalid.");
        for (int i = 0; i < 90; ++i)
        {
            // A disabled target's current transform is never the saved pose.
            double[] paused = TargetSample(x: 999.0);
            reference.Update(true, paused);
            Equal(2.0, paused[7]);
            Near(1.0, paused[0]);
            Near(2.0, paused[1]);
            Near(-3.0, paused[2]); // Reflection must happen exactly once.
            Near(1.0, paused[6]);
        }
    }

    private static void TargetReferenceNeedsStableWindow()
    {
        var reference = new ModelTargetPoseReference();
        for (int i = 0; i < ModelTargetPoseReference.RequiredSamples - 1; ++i)
            reference.Update(false, TargetSample());
        False(reference.HasReference, "A partial window must not become a calibration.");
        double[] paused = TargetSample();
        reference.Update(true, paused);
        Equal(0.0, paused[7]);
        for (int i = 0; i < 7; ++i) True(double.IsNaN(paused[i]), "Missing reference must stay invalid.");

        reference.Reset();
        for (int i = 0; i < 100; ++i)
            reference.Update(false, TargetSample(x: (i % 2 == 0 ? 1.0 : 2.0)));
        False(reference.HasReference, "Moving targets must not produce a stable reference.");
        reference.Update(false, TargetSample(false));
        for (int i = 0; i < 19; ++i) reference.Update(false, TargetSample());
        False(reference.HasReference, "Tracking loss resets consecutive sample collection.");
    }

    private static void TargetReferenceResumeAndResetDiscardOldWorldPose()
    {
        var reference = new ModelTargetPoseReference();
        AcquireTargetReference(reference);
        reference.Update(true, TargetSample(false));
        reference.Update(false, TargetSample(false));
        False(reference.HasReference, "Resuming must discard the previous reference.");
        AcquireTargetReference(reference, 7.0);
        double[] paused = TargetSample(false);
        reference.Update(true, paused);
        Near(7.0, paused[0]);
        Equal(2.0, paused[7]);
        reference.Reset();
        reference.Update(true, paused);
        Equal(0.0, paused[7]);
        True(double.IsNaN(paused[0]), "A reset must not reuse another world's pose.");
    }

    private static void TargetReferenceRejectsPoseJumpsAndInvalidRotations()
    {
        foreach (double[] bad in new[] {
            TargetSample(x: 10.0), TargetSample(qz: 1.0, qw: 0.0),
            TargetSample(qw: 0.0), TargetSample(x: double.NaN),
            TargetSample(qw: double.PositiveInfinity) })
        {
            var reference = new ModelTargetPoseReference();
            AcquireTargetReference(reference);
            reference.Update(false, bad);
            False(reference.HasReference, "A changed or invalid pose must invalidate the earlier reference.");
            double[] paused = TargetSample(false);
            reference.Update(true, paused);
            Equal(0.0, paused[7]);
        }
    }

    private static void TargetReferenceAveragesQuaternionSignsAndPosition()
    {
        var reference = new ModelTargetPoseReference();
        for (int i = 0; i < 20; ++i)
        {
            double sign = i % 2 == 0 ? 1.0 : -1.0;
            reference.Update(false, TargetSample(x: 1.0 + i * 0.0001,
                                                 qz: sign * Math.Sqrt(2), qw: sign * Math.Sqrt(2)));
        }
        double[] paused = TargetSample(false);
        reference.Update(true, paused);
        Equal(2.0, paused[7]);
        Near(1.00095, paused[0]);
        Near(Math.Sqrt(.5), paused[5]);
        Near(Math.Sqrt(.5), paused[6]);
    }
}
