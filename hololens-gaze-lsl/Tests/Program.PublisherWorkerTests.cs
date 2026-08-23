using System;
using System.Threading;
using GazeLSL;

internal static partial class Program
{
    private static void GazePublisherPreservesExplicitTimestamp()
    {
        var outlet = new CountingOutlet();
        var worker = new GazePublisherWorker(
            new TimestampProvider(42.5), outlet, 1000);
        worker.Start();
        var deadline = DateTime.UtcNow.AddSeconds(1);
        while (outlet.Count == 0 && DateTime.UtcNow < deadline)
        {
            Thread.Sleep(1);
        }
        True(outlet.Count > 0, "Timestamped provider was never published.");
        True(worker.Stop(1000), "Timestamped worker did not stop.");
        Equal(42.5, outlet.LastTimestamp);
    }

    private static void GazePublisherRejectsInvalidCaptureTimestamp()
    {
        var outlet = new CountingOutlet();
        var worker = new GazePublisherWorker(
            new InvalidTimestampProvider(), outlet, 1000);
        worker.Start();
        Thread.Sleep(10);
        True(worker.Stop(1000), "Worker with an invalid capture timestamp did not stop.");
        Equal(0.0, outlet.Count);
    }

    private static void GazePublisherInvalidTimestampKeepsCadence()
    {
        var provider = new CadenceProbeProvider();
        var worker = new GazePublisherWorker(
            provider, new CountingOutlet(), 10);
        worker.Start();
        True(provider.FirstCall.Wait(1000), "Invalid-timestamp provider was never called.");
        False(provider.SecondCall.Wait(30),
            "Invalid timestamp bypassed the configured publishing cadence.");
        True(provider.SecondCall.Wait(500),
            "Worker did not advance to the next scheduled provider call.");
        True(worker.Stop(1000), "Invalid-timestamp cadence worker did not stop.");
    }

    private static void GazePublisherCancellation()
    {
        var worker = new GazePublisherWorker(new EmptyProvider(), new CountingOutlet(), 1000);
        worker.Start();
        True(worker.Stop(1000), "A responsive worker should stop within the timeout.");
        False(worker.IsRunning, "Stopped worker still reports running.");
        True(worker.Failure == null, "Cancellation should not be reported as a failure.");
    }

    private static void GazePublisherRecoversFromTransientProviderException()
    {
        var expected = new NullReferenceException("transient WinRT read failure");
        var outlet = new CountingOutlet();
        var worker = new GazePublisherWorker(
            new ThrowOnceProvider(expected), outlet, 1000);
        worker.Start();

        var deadline = DateTime.UtcNow.AddSeconds(1);
        while (outlet.Count == 0 && DateTime.UtcNow < deadline)
        {
            Thread.Sleep(1);
        }

        True(outlet.Count > 0, "Worker did not recover after a transient provider exception.");
        True(worker.Failure == null, "Transient provider exception stopped the worker.");
        Equal(1.0, worker.ProviderExceptionCount);
        Same(expected, worker.LastProviderException);
        True(worker.Stop(1000), "Recovered provider worker did not stop.");
    }

    private static void GazePublisherRequestsRecoveryAfterPersistentProviderExceptions()
    {
        var expected = new InvalidOperationException("provider failed");
        var worker = new GazePublisherWorker(new ThrowingProvider(expected), new CountingOutlet(), 20);
        worker.Start();
        var deadline = DateTime.UtcNow.AddSeconds(2);
        while (worker.IsRunning && DateTime.UtcNow < deadline)
        {
            Thread.Sleep(1);
        }
        False(worker.IsRunning,
            "Persistent tracker read failures did not request a fresh SDK session.");
        True(worker.ProviderExceptionCount >= 20,
            "The worker requested recovery before one second of consecutive failures.");
        Same(expected, worker.ProviderFailure);
        True(worker.Failure == null, "A recoverable tracker failure became an outlet failure.");
        Same(expected, worker.LastProviderException);
        True(worker.Stop(1000), "Recovering provider worker did not remain joinable.");
    }

    private static void GazePublisherOutletException()
    {
        var expected = new InvalidOperationException("outlet failed");
        var worker = new GazePublisherWorker(new OneSampleProvider(), new ThrowingOutlet(expected), 1000);
        worker.Start();
        WaitUntilStopped(worker);
        Same(expected, worker.Failure);
        True(worker.Stop(1000), "Failed outlet worker should remain joinable.");
    }

    private static void GazePublisherTimeoutRetainsOwnership()
    {
        var provider = new BlockingProvider();
        var outlet = new CountingOutlet();
        var worker = new GazePublisherWorker(provider, outlet, 1000);
        worker.Start();
        True(provider.Entered.Wait(1000), "Provider was never called.");
        False(worker.Stop(10), "Stop unexpectedly succeeded while the provider was blocked.");
        True(worker.IsRunning, "Timed-out worker lost its running state before the provider returned.");

        provider.Release.Set();
        True(worker.Stop(1000), "Worker did not remain joinable after a timed-out stop.");
        False(worker.IsRunning, "Worker still reports running after its retained thread exited.");
    }

    private static void WaitUntilStopped(GazePublisherWorker worker)
    {
        var deadline = DateTime.UtcNow.AddSeconds(1);
        while (worker.IsRunning && DateTime.UtcNow < deadline)
        {
            Thread.Sleep(1);
        }
        False(worker.IsRunning, "Worker did not stop after an exception.");
    }
}
