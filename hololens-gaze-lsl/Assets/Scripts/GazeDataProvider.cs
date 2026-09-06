using System;
using System.Collections.Generic;
using UnityEngine;

#if ENABLE_WINMD_SUPPORT
using Microsoft.MixedReality.EyeTracking;
using Microsoft.MixedReality.OpenXR;
#endif

namespace GazeLSL
{
    // Counts every stage between the SDK and a sample waiting to be published.
    // Each stage can discard a reading without any other trace, so a stream that
    // publishes nothing looks identical from outside whichever one is at fault.
    public struct GazeAcquisitionSnapshot
    {
        public int SeedAttempts;
        public int SeedEmptyResults;
        public int SeedStaleReadings;
        public double LastReadingAgeSeconds;
        public double PublicationLatencySeconds;
        public int DrainStepsSkippedAsTooSoon;
        public int DrainRequests;
        public int DrainReadings;
        public int DrainEmptyResults;
        public int DrainFailedEmptyResults;
        public bool DrainSuspended;
        public int DrainSuspensions;
        public int ReadingsAccepted;
        public int ReadingsRejectedAsNotNewer;
        public int PendingRawReadings;
        public int TransformPasses;
        public int SamplesConverted;
        public int LocateFailures;
        public int ReadingsDroppedByGeneration;
        public int PendingSamples;
    }

    // Minimal HoloLens 2 Extended Eye Tracking provider.
    // Rays are located in the Unity/OpenXR scene before they are published.
    public sealed class GazeDataProvider : MonoBehaviour, IGazeSampleProvider
    {
        private const uint RequiredFrameRate = 90u;
        private const int MaxTransformsPerUpdate = 32;
        private const int MaxQueuedSamples = 360;

#if ENABLE_WINMD_SUPPORT
        private struct RawGazeReading
        {
            public long Generation;
            public long SystemRelativeTimeTicks;
            public double Timestamp;
            public TrackerSpaceRay Combined;
            public TrackerSpaceRay Left;
            public TrackerSpaceRay Right;
        }

        private readonly object trackerGate = new object();
        private readonly Queue<RawGazeReading> pendingRawReadings =
            new Queue<RawGazeReading>();
        private readonly Queue<GazeSample> pendingSamples =
            new Queue<GazeSample>();

        private EyeGazeTrackerWatcher watcher;
        private EyeGazeTracker tracker;
        private SpatialGraphNode trackerNode;
        private Transform mixedRealityPlayspace;
        private readonly GazeReadingGate readingGate = new GazeReadingGate();
        private readonly GazeDrainPolicy drainPolicy = new GazeDrainPolicy();
        private readonly GazeRateEstimator rateEstimator = new GazeRateEstimator();
        private uint selectedFrameRate;
        private bool calibrationValid;
        private bool hasCalibrationState;
        private int calibrationChangeCount;
        private long sessionGeneration;
        private long watcherGeneration;
        private long trackerLifecycleGeneration;
        private bool includeIndividualEyes;
        private bool restartInProgress;
        private int consecutiveLocateFailures;
        private double lastAcceptedCaptureLslTime;
        private GazeAcquisitionSnapshot counters;
        private volatile bool destroyed;
#endif

        private async void Start()
        {
#if ENABLE_WINMD_SUPPORT
            Camera mainCamera = Camera.main;
            mixedRealityPlayspace = mainCamera != null
                ? mainCamera.transform.parent
                : null;
            try
            {
                await StartWatcherAsync();
            }
            catch (Exception e)
            {
                Debug.LogError($"Extended eye tracking unavailable - {e.Message}");
            }
#else
            Debug.LogError("Extended eye tracking requires a HoloLens/UWP device build.");
            await System.Threading.Tasks.Task.CompletedTask;
#endif
        }

        public bool TryGetEffectiveFrameRate(out uint rate, out long generation)
        {
            rate = 0u;
            generation = 0L;

#if ENABLE_WINMD_SUPPORT
            lock (trackerGate)
            {
                if (tracker == null || trackerNode == null)
                {
                    return false;
                }

                if (selectedFrameRate == 0u)
                {
                    return false;
                }

                rate = selectedFrameRate;
                generation = sessionGeneration;
                return true;
            }
#else
            return false;
#endif
        }

        // TryGetEffectiveFrameRate reports what the device was asked for and cannot
        // reveal a tracker that has throttled itself; this measures what arrived.
        public bool TryGetMeasuredFrameRate(out double samplesPerSecond, out long generation)
        {
            samplesPerSecond = 0.0;
            generation = 0L;

#if ENABLE_WINMD_SUPPORT
            lock (trackerGate)
            {
                if (tracker == null || trackerNode == null)
                {
                    return false;
                }

                generation = sessionGeneration;
                return rateEstimator.TryGetRate(out samplesPerSecond);
            }
#else
            return false;
#endif
        }

        // Shortest and longest gap between accepted captures, in milliseconds.
        public bool TryGetCaptureIntervals(out double minMilliseconds, out double maxMilliseconds)
        {
            minMilliseconds = 0.0;
            maxMilliseconds = 0.0;

#if ENABLE_WINMD_SUPPORT
            lock (trackerGate)
            {
                if (tracker == null || trackerNode == null)
                {
                    return false;
                }

                return rateEstimator.TryGetIntervalSummary(
                    out minMilliseconds,
                    out maxMilliseconds);
            }
#else
            return false;
#endif
        }

        // changeCount rises on every transition, so a caller polling slowly still
        // learns that calibration moved even if it missed the intermediate value.
        public bool TryGetCalibrationState(out bool valid, out int changeCount)
        {
            valid = false;
            changeCount = 0;

#if ENABLE_WINMD_SUPPORT
            lock (trackerGate)
            {
                if (!hasCalibrationState)
                {
                    return false;
                }

                valid = calibrationValid;
                changeCount = calibrationChangeCount;
                return true;
            }
#else
            return false;
#endif
        }

        // A stream that publishes nothing is the same log line whether acquisition,
        // main-thread conversion, or delivery is the stage losing the reading.
        public bool TryGetAcquisitionSnapshot(out GazeAcquisitionSnapshot snapshot)
        {
#if ENABLE_WINMD_SUPPORT
            lock (trackerGate)
            {
                snapshot = counters;
                snapshot.PublicationLatencySeconds = drainPolicy.PublicationLatencySeconds;
                snapshot.DrainSuspended = drainPolicy.IsSuspended(LSL.LSL.local_clock());
                snapshot.DrainSuspensions = drainPolicy.Suspensions;
                snapshot.PendingRawReadings = pendingRawReadings.Count;
                snapshot.PendingSamples = pendingSamples.Count;
                return true;
            }
#else
            snapshot = default(GazeAcquisitionSnapshot);
            return false;
#endif
        }

        private void Update()
        {
#if ENABLE_WINMD_SUPPORT
            TransformReadingsOnMainThread();
#endif
        }

        public bool TryGetNextSample(out GazeSample sample)
        {
            sample = default(GazeSample);

#if ENABLE_WINMD_SUPPORT
            System.Runtime.ExceptionServices.ExceptionDispatchInfo acquireFailure = null;
            lock (trackerGate)
            {
                if (tracker != null && trackerNode != null)
                {
                    try
                    {
                        AcquireRawReadingLocked();
                    }
                    catch (Exception e)
                    {
                        // Samples already converted on the main thread are still
                        // good. Letting a failed read out from here would strand
                        // them behind it, which is how a tracker that was capturing
                        // normally ended up publishing nothing at all.
                        acquireFailure =
                            System.Runtime.ExceptionServices.ExceptionDispatchInfo
                                .Capture(e);
                    }
                }

                GazeBacklogPolicy.CollapseIfOverSpan(
                    pendingSamples,
                    GetSampleTimestamp,
                    GazeTiming.MaxBacklogSpanSeconds);

                if (pendingSamples.Count > 0)
                {
                    sample = pendingSamples.Dequeue();
                    return true;
                }
            }

            if (acquireFailure != null)
            {
                // Nothing was left to deliver, so the caller still has to see the
                // failure and count it towards re-enumerating the tracker.
                acquireFailure.Throw();
            }

            return false;
#else
            return false;
#endif
        }

        // Called by the outlet only after a persistent SDK read failure.
        public async void RestartTrackingSession()
        {
#if ENABLE_WINMD_SUPPORT
            if (destroyed || restartInProgress)
            {
                return;
            }

            restartInProgress = true;
            try
            {
                StopWatcherAndTracker();
                await StartWatcherAsync();
            }
            catch (Exception e)
            {
                Debug.LogError($"Could not restart extended eye tracking - {e.Message}");
            }
            finally
            {
                restartInProgress = false;
            }
#else
            await System.Threading.Tasks.Task.CompletedTask;
#endif
        }

#if ENABLE_WINMD_SUPPORT
        // Drains every reading published since the last accepted one. Asking for the
        // reading at "now" returns only one, so a late poll loses the frames between;
        // walking the cursor makes capture independent of poll punctuality.
        private void AcquireRawReadingLocked()
        {
            DateTime queryTime = DateTime.Now;
            double queryLslTime = LSL.LSL.local_clock();

            if (!readingGate.HasReading || drainPolicy.IsSuspended(queryLslTime))
            {
                AcquireReadingAtTimestampLocked(queryTime, queryLslTime);
                return;
            }

            for (int index = 0; index < GazeTiming.MaxReadingsPerAcquire; index++)
            {
                // Stops the step before it starts when the tracker cannot have
                // parted with a reading since the last accepted capture, and again
                // after each reading that brings it back to the current one. Either
                // way the SDK is not asked for a reading it cannot yet hand over,
                // which is the request this runtime cannot answer without failing.
                if (!GazeDrainPolicy.CouldHaveNewerReading(
                        queryLslTime,
                        lastAcceptedCaptureLslTime,
                        NominalFramePeriodSecondsLocked(),
                        drainPolicy.PublicationLatencySeconds))
                {
                    counters.DrainStepsSkippedAsTooSoon++;
                    return;
                }

                EyeGazeTrackerReading reading;
                counters.DrainRequests++;
                try
                {
                    reading = tracker.TryGetReadingAfterSystemRelativeTime(
                        TimeSpan.FromTicks(readingGate.LastTimestampTicks));
                }
                catch (NullReferenceException)
                {
                    // This runtime's projection of the SDK marshals the "no newer
                    // reading" result without checking it, so an ordinary empty
                    // result arrives as a NullReferenceException and leaves an
                    // object whose finalizer then throws as well. It ends the step,
                    // and a few of them put the drain down for ten seconds.
                    counters.DrainFailedEmptyResults++;
                    NoteFailedDrainEmptyResultLocked(queryLslTime);
                    return;
                }

                if (reading == null)
                {
                    counters.DrainEmptyResults++;
                    return;
                }

                counters.DrainReadings++;

                if (!EnqueueReadingLocked(reading, queryTime, queryLslTime))
                {
                    // The cursor did not advance, so this would loop on one reading.
                    return;
                }
            }
        }

        // Each failure leaks a broken SDK object whose finalizer throws on the GC
        // thread, so the drain is put down quickly rather than repeated. Only the
        // first suspension is announced; the count is in the acquisition counters,
        // and a warning every ten seconds would say nothing the first did not.
        private void NoteFailedDrainEmptyResultLocked(double queryLslTime)
        {
            if (!drainPolicy.NoteFailedEmptyResult(queryLslTime) ||
                drainPolicy.Suspensions > 1)
            {
                return;
            }

            Debug.LogWarning(
                "This device's eye tracking SDK cannot report that it has no newer " +
                $"reading: {GazeDrainPolicy.FailedEmptyResultsBeforeSuspend} attempts " +
                "to drain readings forward failed inside the SDK itself. Suspending " +
                $"the drain for {GazeDrainPolicy.SuspensionSeconds:F0} s at a time " +
                "and reading at the current time meanwhile, which takes at most one " +
                "reading per step and so may not keep up with the tracker.");
        }

        private double NominalFramePeriodSecondsLocked()
        {
            uint rate = selectedFrameRate != 0u ? selectedFrameRate : RequiredFrameRate;
            return 1.0 / rate;
        }

        // Seeds the drain cursor, and is the whole of acquisition once the drain
        // has been abandoned. Only a reading fetched for "now" is judged on age; a
        // drained reading being old means the step is catching up instead.
        private void AcquireReadingAtTimestampLocked(DateTime queryTime, double queryLslTime)
        {
            counters.SeedAttempts++;
            EyeGazeTrackerReading reading =
                tracker.TryGetReadingAtTimestamp(queryTime);
            if (reading == null)
            {
                counters.SeedEmptyResults++;
                return;
            }

            // Recorded on every reading offered. A reading the tracker captured
            // moments ago and one it captured minutes ago are the same rejection
            // from outside.
            double ageSeconds = (queryTime - reading.Timestamp).TotalSeconds;
            counters.LastReadingAgeSeconds = ageSeconds;

            if (!GazeTiming.IsFreshSeedAge(ageSeconds))
            {
                counters.SeedStaleReadings++;
                return;
            }

            EnqueueReadingLocked(reading, queryTime, queryLslTime);
        }

        // False when the reading did not advance the cursor: the drain's stop
        // condition as well as the duplicate guard.
        private bool EnqueueReadingLocked(
            EyeGazeTrackerReading reading,
            DateTime queryTime,
            double queryLslTime)
        {
            long systemRelativeTimeTicks = reading.SystemRelativeTime.Ticks;
            if (!readingGate.TryAccept(systemRelativeTimeTicks))
            {
                counters.ReadingsRejectedAsNotNewer++;
                return false;
            }

            counters.ReadingsAccepted++;

            // One query pair anchors the batch; each reading keeps its capture time.
            double ageSeconds = (queryTime - reading.Timestamp).TotalSeconds;

            RawGazeReading raw = new RawGazeReading
            {
                Generation = sessionGeneration,
                SystemRelativeTimeTicks = systemRelativeTimeTicks,
                Timestamp = queryLslTime - ageSeconds
            };

            raw.Combined = ReadCombinedRay(reading);
            if (includeIndividualEyes)
            {
                raw.Left = ReadLeftRay(reading);
                raw.Right = ReadRightRay(reading);
            }

            GazeBacklogPolicy.Enqueue(
                pendingRawReadings,
                raw,
                GetRawTimestamp,
                MaxQueuedSamples,
                GazeTiming.MaxBacklogSpanSeconds);

            drainPolicy.NoteReadingAge(ageSeconds);
            rateEstimator.Add(raw.Timestamp);
            lastAcceptedCaptureLslTime = raw.Timestamp;

            bool readingCalibrationValid = reading.IsCalibrationValid;
            if (!hasCalibrationState || readingCalibrationValid != calibrationValid)
            {
                hasCalibrationState = true;
                calibrationValid = readingCalibrationValid;
                calibrationChangeCount++;
            }

            return true;
        }

        private void TransformReadingsOnMainThread()
        {
            Exception locateFailure = null;
            lock (trackerGate)
            {
                // A pass count that stays at zero says Unity is not calling Update
                // on this component at all, which no other line here would show.
                counters.TransformPasses++;
            }

            for (int index = 0; index < MaxTransformsPerUpdate; index++)
            {
                RawGazeReading raw;
                SpatialGraphNode node;
                lock (trackerGate)
                {
                    GazeBacklogPolicy.CollapseIfOverSpan(
                        pendingRawReadings,
                        GetRawTimestamp,
                        GazeTiming.MaxBacklogSpanSeconds);
                    if (pendingRawReadings.Count == 0)
                    {
                        break;
                    }

                    raw = pendingRawReadings.Dequeue();
                    if (raw.Generation != sessionGeneration || trackerNode == null)
                    {
                        counters.ReadingsDroppedByGeneration++;
                        continue;
                    }
                    node = trackerNode;
                }

                GazeSample sample =
                    GazeSampleProjection.CreateInvalidSample(raw.Timestamp);

                try
                {
                    if (node.TryLocate(raw.SystemRelativeTimeTicks, out Pose trackerPose))
                    {
                        consecutiveLocateFailures = 0;
                        GazeProjectionContext projectionContext =
                            CreateProjectionContext(
                                trackerPose,
                                mixedRealityPlayspace);
                        sample = GazeSampleProjection.ProjectSample(
                            raw.Timestamp,
                            raw.Combined,
                            raw.Left,
                            raw.Right,
                            projectionContext);
                    }
                    else
                    {
                        counters.LocateFailures++;
                        consecutiveLocateFailures++;
                        if (consecutiveLocateFailures == 1 ||
                            consecutiveLocateFailures % (int)RequiredFrameRate == 0)
                        {
                            Debug.LogWarning(
                                "Could not locate the extended eye tracker in the " +
                                "OpenXR playspace at its reading timestamp.");
                        }
                    }
                }
                catch (Exception e)
                {
                    locateFailure = e;
                    break;
                }

                lock (trackerGate)
                {
                    if (raw.Generation != sessionGeneration)
                    {
                        counters.ReadingsDroppedByGeneration++;
                        continue;
                    }

                    counters.SamplesConverted++;
                    GazeBacklogPolicy.Enqueue(
                        pendingSamples,
                        sample,
                        GetSampleTimestamp,
                        MaxQueuedSamples,
                        GazeTiming.MaxBacklogSpanSeconds);
                }
            }

            if (locateFailure != null)
            {
                Debug.LogWarning(
                    $"Eye tracker locate failed; re-enumerating the tracker - {locateFailure.Message}");
                RestartTrackingSession();
            }
        }

        private static double GetRawTimestamp(RawGazeReading reading)
        {
            return reading.Timestamp;
        }

        private static double GetSampleTimestamp(GazeSample sample)
        {
            return sample.Timestamp;
        }

        private async System.Threading.Tasks.Task StartWatcherAsync()
        {
            Windows.UI.Input.GazeInputAccessStatus access =
                await Windows.Perception.People.EyesPose.RequestAccessAsync();

            if (destroyed)
            {
                return;
            }

            if (access != Windows.UI.Input.GazeInputAccessStatus.Allowed)
            {
                throw new InvalidOperationException($"Eye tracking permission was not granted: {access}");
            }

            EyeGazeTrackerWatcher newWatcher = new EyeGazeTrackerWatcher();
            newWatcher.EyeGazeTrackerAdded += OnTrackerAdded;
            newWatcher.EyeGazeTrackerRemoved += OnTrackerRemoved;

            long watcherToken = 0L;
            bool shouldAbort;
            lock (trackerGate)
            {
                shouldAbort = destroyed;
                if (!shouldAbort)
                {
                    watcher = newWatcher;
                    watcherToken = ++watcherGeneration;
                    trackerLifecycleGeneration++;
                }
            }

            if (shouldAbort)
            {
                StopWatcher(newWatcher);
                return;
            }

            try
            {
                await newWatcher.StartAsync();
            }
            catch
            {
                bool isCurrent;
                lock (trackerGate)
                {
                    isCurrent = ReferenceEquals(watcher, newWatcher) &&
                                watcherGeneration == watcherToken;
                }

                if (isCurrent)
                {
                    StopWatcherAndTracker();
                }
                else
                {
                    StopWatcher(newWatcher);
                }
                throw;
            }

            bool stillCurrent;
            lock (trackerGate)
            {
                stillCurrent = !destroyed &&
                               ReferenceEquals(watcher, newWatcher) &&
                               watcherGeneration == watcherToken;
            }

            if (!stillCurrent)
            {
                StopWatcher(newWatcher);
                return;
            }

            Debug.Log("Eye tracker watcher started");
        }

        private async void OnTrackerAdded(object sender, EyeGazeTracker newTracker)
        {
            long openGeneration;
            lock (trackerGate)
            {
                if (!ReferenceEquals(sender, watcher) || destroyed)
                {
                    return;
                }

                openGeneration = trackerLifecycleGeneration;
            }

            try
            {
                await newTracker.OpenAsync(true);

                uint activeFrameRate = TrySelectRequiredFrameRate(newTracker);
                if (activeFrameRate == 0u)
                {
                    Debug.LogError("This eye tracker does not expose the required 90 Hz mode.");
                    CloseTracker(newTracker);
                    return;
                }

                SpatialGraphNode newTrackerNode =
                    SpatialGraphNode.FromDynamicNodeId(
                        newTracker.TrackerSpaceLocatorNodeId);
                if (newTrackerNode == null)
                {
                    Debug.LogError(
                        "Could not locate the eye tracker in the Unity/OpenXR scene.");
                    CloseTracker(newTracker);
                    return;
                }

                bool perEye = newTracker.AreLeftAndRightGazesSupported;
                EyeGazeTracker previousTracker;
                bool canActivate;
                lock (trackerGate)
                {
                    canActivate = !destroyed &&
                                  ReferenceEquals(sender, watcher) &&
                                  trackerLifecycleGeneration == openGeneration;
                    if (canActivate)
                    {
                        previousTracker = tracker;
                        tracker = newTracker;
                        trackerNode = newTrackerNode;
                        // Start the integer capture-time gate at the new tracker
                        // lifecycle; readings from the previous SDK session must
                        // never be compared with this session's clock.
                        ResetReadingPipelineLocked();
                        selectedFrameRate = activeFrameRate;
                        includeIndividualEyes = perEye;
                        sessionGeneration++;
                    }
                    else
                    {
                        previousTracker = null;
                    }
                }

                if (!canActivate)
                {
                    CloseTracker(newTracker);
                    return;
                }

                if (previousTracker != null && !ReferenceEquals(previousTracker, newTracker))
                {
                    CloseTracker(previousTracker);
                }

                Debug.Log(
                    $"Extended eye tracking ready at 90 Hz. Per-eye: {perEye}");
            }
            catch (Exception e)
            {
                CloseTracker(newTracker);
                Debug.LogError($"Failed to open eye tracker - {e.Message}");
            }
        }

        private void OnTrackerRemoved(object sender, EyeGazeTracker removedTracker)
        {
            bool wasActive = false;
            lock (trackerGate)
            {
                if (!ReferenceEquals(sender, watcher))
                {
                    return;
                }

                // Invalidate any OpenAsync continuation that started before removal.
                trackerLifecycleGeneration++;
                if (ReferenceEquals(tracker, removedTracker))
                {
                    ClearActiveTrackerLocked();
                    wasActive = true;
                }
            }

            CloseTracker(removedTracker);
            if (wasActive)
            {
                Debug.LogWarning("Eye tracker removed");
            }
        }

        // Returns the rate the device reported for the mode it accepted, or zero if
        // the required mode is not offered, so nominal describes the actual mode.
        private static uint TrySelectRequiredFrameRate(EyeGazeTracker currentTracker)
        {
            var supportedRates = currentTracker.SupportedTargetFrameRates;
            if (supportedRates == null)
            {
                return 0u;
            }

            for (int i = 0; i < supportedRates.Count; i++)
            {
                if (supportedRates[i].FramesPerSecond == RequiredFrameRate)
                {
                    currentTracker.SetTargetFrameRate(supportedRates[i]);
                    return supportedRates[i].FramesPerSecond;
                }
            }

            return 0u;
        }

        private static TrackerSpaceRay ReadCombinedRay(
            EyeGazeTrackerReading reading)
        {
            System.Numerics.Vector3 origin;
            System.Numerics.Vector3 direction;
            bool sourceValid = reading.TryGetCombinedEyeGazeInTrackerSpace(
                out origin,
                out direction);
            return GazeSampleProjection.CreateTrackerSpaceRay(
                sourceValid,
                origin,
                direction);
        }

        private static TrackerSpaceRay ReadLeftRay(
            EyeGazeTrackerReading reading)
        {
            System.Numerics.Vector3 origin;
            System.Numerics.Vector3 direction;
            bool sourceValid = reading.TryGetLeftEyeGazeInTrackerSpace(
                out origin,
                out direction);
            return GazeSampleProjection.CreateTrackerSpaceRay(
                sourceValid,
                origin,
                direction);
        }

        private static TrackerSpaceRay ReadRightRay(
            EyeGazeTrackerReading reading)
        {
            System.Numerics.Vector3 origin;
            System.Numerics.Vector3 direction;
            bool sourceValid = reading.TryGetRightEyeGazeInTrackerSpace(
                out origin,
                out direction);
            return GazeSampleProjection.CreateTrackerSpaceRay(
                sourceValid,
                origin,
                direction);
        }

        private static GazeProjectionContext CreateProjectionContext(
            Pose trackerPose,
            Transform playspace)
        {
            System.Numerics.Vector3 worldFromPlayspacePosition =
                playspace != null
                    ? ToNumerics(playspace.position)
                    : System.Numerics.Vector3.Zero;
            System.Numerics.Quaternion worldFromPlayspaceRotation =
                playspace != null
                    ? ToNumerics(playspace.rotation)
                    : System.Numerics.Quaternion.Identity;
            System.Numerics.Vector3 worldFromPlayspaceScale =
                playspace != null
                    ? ToNumerics(playspace.lossyScale)
                    : System.Numerics.Vector3.One;

            return GazeSampleProjection.CreateProjectionContext(
                ToNumerics(trackerPose.position),
                ToNumerics(trackerPose.rotation),
                worldFromPlayspacePosition,
                worldFromPlayspaceRotation,
                worldFromPlayspaceScale);
        }

        private static System.Numerics.Vector3 ToNumerics(Vector3 value)
        {
            return new System.Numerics.Vector3(value.x, value.y, value.z);
        }

        private static System.Numerics.Quaternion ToNumerics(Quaternion value)
        {
            return new System.Numerics.Quaternion(
                value.x,
                value.y,
                value.z,
                value.w);
        }

        // The caller must hold trackerGate so no acquisition can observe a
        // partially reset capture pipeline.
        private void ResetReadingPipelineLocked()
        {
            readingGate.Reset();
            drainPolicy.Reset();
            rateEstimator.Reset();
            calibrationValid = false;
            hasCalibrationState = false;
            calibrationChangeCount = 0;
            pendingRawReadings.Clear();
            pendingSamples.Clear();
            consecutiveLocateFailures = 0;
            lastAcceptedCaptureLslTime = 0.0;
            counters = default(GazeAcquisitionSnapshot);
        }

        // The caller must hold trackerGate. Generation counters are deliberately
        // advanced by each lifecycle operation at its existing call site.
        private void ClearActiveTrackerLocked()
        {
            tracker = null;
            trackerNode = null;
            selectedFrameRate = 0u;
            includeIndividualEyes = false;
            ResetReadingPipelineLocked();
        }

        private void StopWatcherAndTracker()
        {
            EyeGazeTrackerWatcher currentWatcher;
            EyeGazeTracker currentTracker;
            lock (trackerGate)
            {
                currentWatcher = watcher;
                watcher = null;
                currentTracker = tracker;
                ClearActiveTrackerLocked();
                watcherGeneration++;
                trackerLifecycleGeneration++;
            }

            StopWatcher(currentWatcher);
            CloseTracker(currentTracker);
        }

        private void StopWatcher(EyeGazeTrackerWatcher watcherToStop)
        {
            if (watcherToStop == null)
            {
                return;
            }

            watcherToStop.EyeGazeTrackerAdded -= OnTrackerAdded;
            watcherToStop.EyeGazeTrackerRemoved -= OnTrackerRemoved;
            try
            {
                watcherToStop.Stop();
            }
            catch (Exception e)
            {
                Debug.LogWarning($"Error stopping eye tracker watcher - {e.Message}");
            }
        }

        private static void CloseTracker(EyeGazeTracker trackerToClose)
        {
            if (trackerToClose == null)
            {
                return;
            }

            try
            {
                trackerToClose.Close();
            }
            catch (Exception e)
            {
                Debug.LogWarning($"Error closing eye tracker - {e.Message}");
            }
        }
#endif

        private void OnDestroy()
        {
#if ENABLE_WINMD_SUPPORT
            destroyed = true;
            StopWatcherAndTracker();
#endif
        }
    }
}
