using System;
using System.Collections.Generic;
using UnityEngine;

#if ENABLE_WINMD_SUPPORT
using Microsoft.MixedReality.EyeTracking;
using Microsoft.MixedReality.OpenXR;
#endif

namespace GazeLSL
{
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

        private struct QueuedGazeSample
        {
            public long SystemRelativeTimeTicks;
            public GazeSample Sample;
        }

        private readonly object trackerGate = new object();
        private readonly Queue<RawGazeReading> pendingRawReadings =
            new Queue<RawGazeReading>();
        private readonly Queue<QueuedGazeSample> pendingSamples =
            new Queue<QueuedGazeSample>();

        private EyeGazeTrackerWatcher watcher;
        private EyeGazeTracker tracker;
        private SpatialGraphNode trackerNode;
        private Transform mixedRealityPlayspace;
        private readonly GazeReadingGate readingGate = new GazeReadingGate();
        private long sessionGeneration;
        private long watcherGeneration;
        private long trackerLifecycleGeneration;
        private bool includeIndividualEyes;
        private bool restartInProgress;
        private int consecutiveLocateFailures;
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

                rate = RequiredFrameRate;
                generation = sessionGeneration;
                return true;
            }
#else
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
            lock (trackerGate)
            {
                if (tracker != null && trackerNode != null)
                {
                    AcquireRawReadingLocked();
                }

                GazeBacklogPolicy.CollapseIfOverSpan(
                    pendingSamples,
                    GetSampleTimestampTicks,
                    GazeTiming.MaxBacklogSpanTicks);

                if (pendingSamples.Count == 0)
                {
                    return false;
                }
                sample = pendingSamples.Dequeue().Sample;
                return true;
            }
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
        private void AcquireRawReadingLocked()
        {
            DateTime queryTime = DateTime.Now;
            double queryLslTime = LSL.LSL.local_clock();

            EyeGazeTrackerReading reading =
                tracker.TryGetReadingAtTimestamp(queryTime);
            if (reading == null)
            {
                return;
            }

            double ageSeconds =
                (queryTime - reading.Timestamp).TotalSeconds;
            if (Math.Abs(ageSeconds) > 0.050)
            {
                return;
            }

            long systemRelativeTimeTicks = reading.SystemRelativeTime.Ticks;
            if (!readingGate.TryAccept(systemRelativeTimeTicks))
            {
                return;
            }

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
                GetRawTimestampTicks,
                MaxQueuedSamples,
                GazeTiming.MaxBacklogSpanTicks);
        }

        private void TransformReadingsOnMainThread()
        {
            Exception locateFailure = null;

            for (int index = 0; index < MaxTransformsPerUpdate; index++)
            {
                RawGazeReading raw;
                SpatialGraphNode node;
                lock (trackerGate)
                {
                    GazeBacklogPolicy.CollapseIfOverSpan(
                        pendingRawReadings,
                        GetRawTimestampTicks,
                        GazeTiming.MaxBacklogSpanTicks);
                    if (pendingRawReadings.Count == 0)
                    {
                        break;
                    }

                    raw = pendingRawReadings.Dequeue();
                    if (raw.Generation != sessionGeneration || trackerNode == null)
                    {
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
                        continue;
                    }
                    GazeBacklogPolicy.Enqueue(
                        pendingSamples,
                        new QueuedGazeSample
                        {
                            SystemRelativeTimeTicks = raw.SystemRelativeTimeTicks,
                            Sample = sample
                        },
                        GetSampleTimestampTicks,
                        MaxQueuedSamples,
                        GazeTiming.MaxBacklogSpanTicks);
                }
            }

            if (locateFailure != null)
            {
                Debug.LogWarning(
                    $"Eye tracker locate failed; re-enumerating the tracker - {locateFailure.Message}");
                RestartTrackingSession();
            }
        }

        private static long GetRawTimestampTicks(RawGazeReading reading)
        {
            return reading.SystemRelativeTimeTicks;
        }

        private static long GetSampleTimestampTicks(QueuedGazeSample queuedSample)
        {
            return queuedSample.SystemRelativeTimeTicks;
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

                if (!TrySelect90Hz(newTracker))
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

        private static bool TrySelect90Hz(EyeGazeTracker currentTracker)
        {
            var supportedRates = currentTracker.SupportedTargetFrameRates;
            if (supportedRates == null)
            {
                return false;
            }

            for (int i = 0; i < supportedRates.Count; i++)
            {
                if (supportedRates[i].FramesPerSecond == RequiredFrameRate)
                {
                    currentTracker.SetTargetFrameRate(supportedRates[i]);
                    return true;
                }
            }

            return false;
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
            pendingRawReadings.Clear();
            pendingSamples.Clear();
            consecutiveLocateFailures = 0;
        }

        // The caller must hold trackerGate. Generation counters are deliberately
        // advanced by each lifecycle operation at its existing call site.
        private void ClearActiveTrackerLocked()
        {
            tracker = null;
            trackerNode = null;
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
