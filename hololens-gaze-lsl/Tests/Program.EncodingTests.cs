using System;
using System.Collections.Generic;
using System.Numerics;
using GazeLSL;

internal static partial class Program
{
    private static void ModelTargetPoseEncoding()
    {
        Equal(ModelTargetPoseEncoder.ChannelCount, ModelTargetStreamContract.ChannelCount);
        Equal(ModelTargetStreamContract.ChannelCount, ModelTargetStreamContract.Labels.Length);
        Equal(ModelTargetStreamContract.ChannelCount, ModelTargetStreamContract.Units.Length);
        Equal("HoloLensModelTargetPose", ModelTargetStreamContract.StreamName);
        Equal("Calibration", ModelTargetStreamContract.StreamType);
        Equal("hololens2_stair_model_target", ModelTargetStreamContract.SourceId);

        double[] sample = new double[ModelTargetPoseEncoder.ChannelCount];
        ModelTargetPoseEncoder.WriteSample(true, 1, 2, 3, 0.1, 0.2, 0.3, 0.4, sample);
        Equal(1.0, sample[0]);
        Equal(2.0, sample[1]);
        Equal(-3.0, sample[2]);
        Equal(-0.1, sample[3]);
        Equal(-0.2, sample[4]);
        Equal(0.3, sample[5]);
        Equal(0.4, sample[6]);
        Equal(1.0, sample[7]);

        ModelTargetPoseEncoder.WriteSample(false, 1, 2, 3, 0.1, 0.2, 0.3, 0.4, sample);
        for (int i = 0; i < 7; i++)
        {
            True(double.IsNaN(sample[i]), "Untracked pose values must be NaN.");
        }
        Equal(0.0, sample[7]);
    }

    private static void GazeTrackerRayTransformsIntoWorld()
    {
        Vector3 worldOrigin;
        Vector3 worldDirection;
        Quaternion quarterTurnAroundY = Quaternion.CreateFromAxisAngle(
            Vector3.UnitY,
            (float)(Math.PI / 2.0));

        True(
            GazeCoordinateTransform.TryTransformTrackerRayToSharedWorld(
                new Vector3(0, 0, 1),
                new Vector3(0, 0, 2),
                new Vector3(10, 20, 30),
                quarterTurnAroundY,
                Vector3.Zero,
                Quaternion.Identity,
                Vector3.One,
                out worldOrigin,
                out worldDirection),
            "A finite tracker ray should transform into world coordinates.");

        Near(9.0, worldOrigin.X);
        Near(20.0, worldOrigin.Y);
        Near(-30.0, worldOrigin.Z);
        Near(-1.0, worldDirection.X);
        Near(0.0, worldDirection.Y);
        Near(0.0, worldDirection.Z);
        Near(1.0, worldDirection.Length());

        TrackerSpaceRay combined = GazeSampleProjection.CreateTrackerSpaceRay(
            true,
            new Vector3(0, 0, 1),
            new Vector3(0, 0, 2));
        TrackerSpaceRay invalid = GazeSampleProjection.CreateTrackerSpaceRay(
            false,
            Vector3.Zero,
            Vector3.UnitZ);
        GazeProjectionContext context = GazeSampleProjection.CreateProjectionContext(
            new Vector3(10, 20, 30),
            quarterTurnAroundY,
            Vector3.Zero,
            Quaternion.Identity,
            Vector3.One);
        GazeSample projected = GazeSampleProjection.ProjectSample(
            123.5,
            combined,
            invalid,
            invalid,
            context);
        Equal(123.5, projected.Timestamp);
        True(projected.CombinedValid, "The combined ray should remain valid after projection.");
        False(projected.LeftEyeValid, "An invalid left ray must remain invalid.");
        False(projected.RightEyeValid, "An invalid right ray must remain invalid.");
        Near(9.0, projected.CombinedOriginX);
        Near(20.0, projected.CombinedOriginY);
        Near(-30.0, projected.CombinedOriginZ);
        True(double.IsNaN(projected.LeftEyeOriginX),
            "Invalid per-eye values must keep the fixed-schema NaN encoding.");

        True(
            GazeCoordinateTransform.TryTransformTrackerRayToSharedWorld(
                new Vector3(1, 2, 3),
                Vector3.UnitZ,
                Vector3.Zero,
                Quaternion.Identity,
                new Vector3(5, 0, 0),
                Quaternion.Identity,
                new Vector3(2, 2, 2),
                out worldOrigin,
                out worldDirection),
            "The playspace transform should be included in shared world coordinates.");
        Near(7.0, worldOrigin.X);
        Near(4.0, worldOrigin.Y);
        Near(6.0, worldOrigin.Z);
        Near(1.0, worldDirection.Z);
    }

    private static void GazeSampleEncodingMatchesContract()
    {
        Equal("HoloLensGaze", GazeStreamContract.StreamName);
        Equal("Gaze", GazeStreamContract.StreamType);
        Equal("hololens2_gaze", GazeStreamContract.SourceId);

        var frame = new GazeSample
        {
            CombinedOriginX = 1,
            CombinedOriginY = 2,
            CombinedOriginZ = 3,
            CombinedDirectionX = 4,
            CombinedDirectionY = 5,
            CombinedDirectionZ = 6,
            CombinedValid = true,
            LeftEyeOriginX = 8,
            LeftEyeOriginY = 9,
            LeftEyeOriginZ = 10,
            LeftEyeDirectionX = 11,
            LeftEyeDirectionY = 12,
            LeftEyeDirectionZ = 13,
            LeftEyeValid = false,
            RightEyeOriginX = 15,
            RightEyeOriginY = 16,
            RightEyeOriginZ = 17,
            RightEyeDirectionX = 18,
            RightEyeDirectionY = 19,
            RightEyeDirectionZ = 20,
            RightEyeValid = true
        };
        var expectedByLabel = new Dictionary<string, double>
        {
            { "CombinedOriginX", 1 },
            { "CombinedOriginY", 2 },
            { "CombinedOriginZ", 3 },
            { "CombinedDirectionX", 4 },
            { "CombinedDirectionY", 5 },
            { "CombinedDirectionZ", 6 },
            { "CombinedValid", 1 },
            { "LeftEyeOriginX", 8 },
            { "LeftEyeOriginY", 9 },
            { "LeftEyeOriginZ", 10 },
            { "LeftEyeDirectionX", 11 },
            { "LeftEyeDirectionY", 12 },
            { "LeftEyeDirectionZ", 13 },
            { "LeftEyeValid", 0 },
            { "RightEyeOriginX", 15 },
            { "RightEyeOriginY", 16 },
            { "RightEyeOriginZ", 17 },
            { "RightEyeDirectionX", 18 },
            { "RightEyeDirectionY", 19 },
            { "RightEyeDirectionZ", 20 },
            { "RightEyeValid", 1 }
        };

        Equal(expectedByLabel.Count, GazeStreamContract.ChannelCount);
        Equal(GazeStreamContract.ChannelCount, GazeStreamContract.Labels.Length);
        Equal(GazeStreamContract.ChannelCount, GazeStreamContract.Units.Length);

        double[] sample = new double[GazeSampleEncoder.ChannelCount];
        GazeSampleEncoder.WriteSample(frame, sample);
        for (int i = 0; i < GazeStreamContract.ChannelCount; i++)
        {
            double expected;
            True(
                expectedByLabel.TryGetValue(GazeStreamContract.Labels[i], out expected),
                "The generated contract contains an unmapped gaze channel.");
            Equal(expected, sample[i]);
        }
    }
}
