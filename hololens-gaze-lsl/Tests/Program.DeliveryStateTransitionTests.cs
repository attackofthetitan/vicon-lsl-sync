using System;
using System.Threading;
using GazeLSL;

internal static partial class Program
{
    private sealed class ValidThenInvalidRayProvider : IGazeSampleProvider
    {
        private int calls;
        public readonly ManualResetEventSlim UseInvalidRay = new ManualResetEventSlim(false);

        public bool TryGetNextSample(out GazeSample sample)
        {
            int call = Interlocked.Increment(ref calls);
            bool valid = !UseInvalidRay.IsSet;
            sample = new GazeSample
            {
                Timestamp = call,
                CombinedValid = valid,
                CombinedDirectionZ = valid ? 1.0 : 0.0
            };
            return true;
        }
    }

    private static void GazePublisherLatestSampleCanReportValidityLoss()
    {
        var provider = new ValidThenInvalidRayProvider();
        var worker = new GazePublisherWorker(
            provider, new CountingOutlet(), 20);
        worker.Start();

        GazeDeliverySnapshot snapshot = WaitForDeliveryState(
            worker, GazeDeliveryState.PublishingValidGaze);
        True(snapshot.PushedValidGazeSampleCount > 0,
            "Valid gaze was never observed before the validity-loss check.");

        provider.UseInvalidRay.Set();
        snapshot = WaitForDeliveryState(
            worker, GazeDeliveryState.PublishingSamplesWithoutValidRays);
        True(snapshot.PushedSampleCount > snapshot.PushedValidGazeSampleCount,
            "The latest invalid-ray sample was not counted after valid gaze.");
        True(worker.Stop(1000), "Validity-loss worker did not stop.");
    }
}
