using System;
using System.Collections.Generic;
using System.Threading;
using GazeLSL;

internal static class Program
{
    private static int Main()
    {
        var tests = new Action[]
        {
            ModelTargetPoseEncoding,
            GazeSampleEncodingMatchesContract,
            GazePublisherCancellation,
            GazePublisherProviderException,
            GazePublisherOutletException,
            GazePublisherTimeoutRetainsOwnership,
            TrackerGenerationRejectsLateOpen,
            TrackerSameIdentityGenerationSharesLifetime,
            TrackerCloseWaitsForReadLease,
            TrackerDestroyInvalidatesPendingOpen
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

    private static void ModelTargetPoseEncoding()
    {
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

    private static void GazeSampleEncodingMatchesContract()
    {
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

    private static void GazePublisherCancellation()
    {
        var worker = new GazePublisherWorker(new EmptyProvider(), new CountingOutlet(), 1000);
        worker.Start();
        True(worker.Stop(1000), "A responsive worker should stop within the timeout.");
        False(worker.IsRunning, "Stopped worker still reports running.");
        True(worker.Failure == null, "Cancellation should not be reported as a failure.");
    }

    private static void GazePublisherProviderException()
    {
        var expected = new InvalidOperationException("provider failed");
        var worker = new GazePublisherWorker(new ThrowingProvider(expected), new CountingOutlet(), 1000);
        worker.Start();
        WaitUntilStopped(worker);
        Same(expected, worker.Failure);
        True(worker.Stop(1000), "Failed provider worker should remain joinable.");
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

    private static void TrackerGenerationRejectsLateOpen()
    {
        var closed = new List<FakeTracker>();
        var coordinator = new TrackerSessionCoordinator<FakeTracker, FakeState>(closed.Add);
        var older = new FakeTracker("older");
        var newer = new FakeTracker("newer");
        var olderAttempt = coordinator.BeginOpen(older);
        var newerAttempt = coordinator.BeginOpen(newer);

        False(coordinator.TryActivate(olderAttempt, new FakeState()), "Older asynchronous open replaced a newer generation.");
        True(closed.Contains(older), "Rejected older tracker was not closed.");
        True(coordinator.TryActivate(newerAttempt, new FakeState()), "Newest tracker generation did not activate.");
        coordinator.Abandon(olderAttempt);
        False(coordinator.Retire(older), "Late removal retired a different active tracker.");
        True(coordinator.HasActiveSession, "Late callback cleared the active generation.");
        True(coordinator.Retire(newer), "Active tracker could not be retired.");
        True(closed.Contains(newer), "Retired tracker was not closed.");
    }

    private static void TrackerCloseWaitsForReadLease()
    {
        var closed = new List<FakeTracker>();
        var coordinator = new TrackerSessionCoordinator<FakeTracker, FakeState>(closed.Add);
        var tracker = new FakeTracker("active");
        var attempt = coordinator.BeginOpen(tracker);
        True(coordinator.TryActivate(attempt, new FakeState()), "Tracker did not activate.");

        TrackerSessionCoordinator<FakeTracker, FakeState>.ReadLease lease;
        True(coordinator.TryAcquireRead(out lease), "Could not acquire an active read lease.");
        True(coordinator.Retire(tracker), "Could not retire tracker with an active reader.");
        False(closed.Contains(tracker), "Tracker closed while a read was active.");
        lease.Dispose();
        True(closed.Contains(tracker), "Tracker did not close after its last read completed.");
    }

    private static void TrackerSameIdentityGenerationSharesLifetime()
    {
        var closed = new List<FakeTracker>();
        var coordinator = new TrackerSessionCoordinator<FakeTracker, FakeState>(closed.Add);
        var tracker = new FakeTracker("reused");
        var firstState = new FakeState();
        var secondState = new FakeState();

        var firstAttempt = coordinator.BeginOpen(tracker);
        True(coordinator.TryActivate(firstAttempt, firstState), "First tracker generation did not activate.");
        TrackerSessionCoordinator<FakeTracker, FakeState>.ReadLease firstLease;
        True(coordinator.TryAcquireRead(out firstLease), "Could not acquire a read from the first generation.");

        var secondAttempt = coordinator.BeginOpen(tracker);
        True(coordinator.TryActivate(secondAttempt, secondState), "Reused tracker generation did not activate.");
        False(closed.Contains(tracker), "Replacing a generation closed the tracker reused by its successor.");

        TrackerSessionCoordinator<FakeTracker, FakeState>.ReadLease secondLease;
        True(coordinator.TryAcquireRead(out secondLease), "Could not acquire a read from the successor generation.");
        Same(secondState, secondLease.State);

        True(coordinator.Retire(tracker), "Could not retire the reused tracker.");
        False(closed.Contains(tracker), "Reused tracker closed while generation reads were active.");
        firstLease.Dispose();
        False(closed.Contains(tracker), "Reused tracker closed before the successor read completed.");
        secondLease.Dispose();
        Equal(1, closed.Count);
        Same(tracker, closed[0]);
    }

    private static void TrackerDestroyInvalidatesPendingOpen()
    {
        var closed = new List<FakeTracker>();
        var coordinator = new TrackerSessionCoordinator<FakeTracker, FakeState>(closed.Add);
        var tracker = new FakeTracker("pending");
        var attempt = coordinator.BeginOpen(tracker);
        coordinator.Destroy();
        True(closed.Contains(tracker), "Destroy did not close a pending tracker.");
        False(coordinator.TryActivate(attempt, new FakeState()), "A pending open activated after destruction.");

        var lateTracker = new FakeTracker("late");
        var lateAttempt = coordinator.BeginOpen(lateTracker);
        False(lateAttempt.IsValid, "Open attempt remained valid after destruction.");
        True(closed.Contains(lateTracker), "Tracker arriving after destruction was not closed.");
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

    private static void True(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);
    }

    private static void False(bool value, string message) => True(!value, message);

    private static void Equal(double expected, double actual)
    {
        if (expected != actual) throw new InvalidOperationException($"Expected {expected}, got {actual}.");
    }

    private static void Same(object expected, object actual)
    {
        if (!ReferenceEquals(expected, actual)) throw new InvalidOperationException("Objects are not identical.");
    }

    private sealed class EmptyProvider : IGazeSampleProvider
    {
        public bool TryGetNextSample(out GazeSample sample) { sample = default(GazeSample); return false; }
    }

    private sealed class OneSampleProvider : IGazeSampleProvider
    {
        public bool TryGetNextSample(out GazeSample sample) { sample = default(GazeSample); return true; }
    }

    private sealed class ThrowingProvider : IGazeSampleProvider
    {
        private readonly Exception failure;
        public ThrowingProvider(Exception failure) { this.failure = failure; }
        public bool TryGetNextSample(out GazeSample sample) { sample = default(GazeSample); throw failure; }
    }

    private sealed class BlockingProvider : IGazeSampleProvider
    {
        public readonly ManualResetEventSlim Entered = new ManualResetEventSlim(false);
        public readonly ManualResetEventSlim Release = new ManualResetEventSlim(false);
        public bool TryGetNextSample(out GazeSample sample)
        {
            sample = default(GazeSample);
            Entered.Set();
            Release.Wait();
            return false;
        }
    }

    private sealed class CountingOutlet : IGazeSampleOutlet
    {
        public int Count;
        public void PushSample(double[] sample) { Interlocked.Increment(ref Count); }
    }

    private sealed class ThrowingOutlet : IGazeSampleOutlet
    {
        private readonly Exception failure;
        public ThrowingOutlet(Exception failure) { this.failure = failure; }
        public void PushSample(double[] sample) { throw failure; }
    }

    private sealed class FakeTracker
    {
        public FakeTracker(string name) { Name = name; }
        public string Name { get; }
    }

    private sealed class FakeState { }
}
