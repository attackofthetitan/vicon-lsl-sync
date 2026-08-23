using System;

internal static partial class Program
{
    private static int Main()
    {
        var tests = new Action[]
        {
            ModelTargetPoseEncoding,
            GazeTrackerRayTransformsIntoWorld,
            GazeSampleEncodingMatchesContract,
            GazeTimingConvertsSystemRelativeTicks,
            GazeTimingRejectsStaleAndInvalidCaptures,
            GazeReadingGateRejectsDuplicateAndRegression,
            GazeBacklogDropsStaleSamples,
            GazePublisherPreservesExplicitTimestamp,
            GazePublisherRejectsInvalidCaptureTimestamp,
            GazePublisherInvalidTimestampKeepsCadence,
            GazePublisherCancellation,
            GazePublisherRecoversFromTransientProviderException,
            GazePublisherRequestsRecoveryAfterPersistentProviderExceptions,
            GazePublisherOutletException,
            GazePublisherTimeoutRetainsOwnership
        };

        int failures = 0;
        foreach (Action test in tests)
        {
            try
            {
                test();
                Console.WriteLine("PASS " + test.Method.Name);
            }
            catch (Exception e)
            {
                failures++;
                Console.Error.WriteLine("FAIL " + test.Method.Name + ": " + e.Message);
            }
        }

        Console.WriteLine($"{tests.Length - failures}/{tests.Length} tests passed");
        return failures == 0 ? 0 : 1;
    }
}
