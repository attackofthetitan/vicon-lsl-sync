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
        private struct RawTrackerRay
        {
            public bool IsValid;
            public System.Numerics.Vector3 Origin;
            public System.Numerics.Vector3 Direction;
        }

        private struct RawGazeReading
        {
            public long Generation;
            public long SystemRelativeTimeTicks;
            public double Timestamp;
            public RawTrackerRay Combined;
            public RawTrackerRay Left;
            public RawTrackerRay Right;
        }

        private readonly object trackerGate = new object();
        private readonly Queue<RawGazeReading> pendingRawReadings =
            new Queue<RawGazeReading>();
        private readonly Queue<GazeSample> pendingSamples = new Queue<GazeSample>();

        private EyeGazeTrackerWatcher watcher;
        private EyeGazeTracker tracker;
        private SpatialGraphNode trackerNode;
        private Transform mixedRealityPlayspace;
        private DateTime lastReadingTimestamp = DateTime.MinValue;
        private DateTime lastWallClock = DateTime.MinValue;
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

                if (pendingSamples.Count == 0)
                {
                    return false;
                }
                sample = pendingSamples.Dequeue();
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
            DateTime wallClockNow = DateTime.Now;
            if (wallClockNow < lastWallClock)
            {
                throw new InvalidOperationException(
                    "System clock moved backwards; restart the eye tracker session.");
            }
            lastWallClock = wallClockNow;

            EyeGazeTrackerReading reading =
                tracker.TryGetReadingAfterTimestamp(lastReadingTimestamp);
            if (reading == null || reading.Timestamp <= lastReadingTimestamp)
            {
                return;
            }

            lastReadingTimestamp = reading.Timestamp;
            RawGazeReading raw = new RawGazeReading
            {
                Generation = sessionGeneration,
                SystemRelativeTimeTicks = reading.SystemRelativeTime.Ticks,
                Timestamp = LSL.LSL.local_clock()
            };

            raw.Combined = ReadCombinedRay(reading);
            if (includeIndividualEyes)
            {
                raw.Left = ReadLeftRay(reading);
                raw.Right = ReadRightRay(reading);
            }

            while (pendingRawReadings.Count >= MaxQueuedSamples)
            {
                pendingRawReadings.Dequeue();
            }
            pendingRawReadings.Enqueue(raw);
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

                GazeSample sample = CreateEmptySample();
                sample.Timestamp = raw.Timestamp;

                try
                {
                    if (node.TryLocate(raw.SystemRelativeTimeTicks, out Pose trackerPose))
                    {
                        consecutiveLocateFailures = 0;
                        PopulateWorldSpaceRays(
                            raw,
                            trackerPose,
                            mixedRealityPlayspace,
                            ref sample);
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
                    while (pendingSamples.Count >= MaxQueuedSamples)
                    {
                        pendingSamples.Dequeue();
                    }
                    pendingSamples.Enqueue(sample);
                }
            }

            if (locateFailure != null)
            {
                Debug.LogWarning(
                    $"Eye tracker locate failed; re-enumerating the tracker - {locateFailure.Message}");
                RestartTrackingSession();
            }
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
                        // Skip readings buffered before this 90 Hz session became active.
                        lastReadingTimestamp = DateTime.Now;
                        lastWallClock = lastReadingTimestamp;
                        includeIndividualEyes = perEye;
                        pendingRawReadings.Clear();
                        pendingSamples.Clear();
                        consecutiveLocateFailures = 0;
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
                    tracker = null;
                    trackerNode = null;
                    includeIndividualEyes = false;
                    lastReadingTimestamp = DateTime.MinValue;
                    lastWallClock = DateTime.MinValue;
                    pendingRawReadings.Clear();
                    pendingSamples.Clear();
                    consecutiveLocateFailures = 0;
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

        private static GazeSample CreateEmptySample()
        {
            return new GazeSample
            {
                CombinedOriginX = double.NaN,
                CombinedOriginY = double.NaN,
                CombinedOriginZ = double.NaN,
                CombinedDirectionX = double.NaN,
                CombinedDirectionY = double.NaN,
                CombinedDirectionZ = double.NaN,
                LeftEyeOriginX = double.NaN,
                LeftEyeOriginY = double.NaN,
                LeftEyeOriginZ = double.NaN,
                LeftEyeDirectionX = double.NaN,
                LeftEyeDirectionY = double.NaN,
                LeftEyeDirectionZ = double.NaN,
                RightEyeOriginX = double.NaN,
                RightEyeOriginY = double.NaN,
                RightEyeOriginZ = double.NaN,
                RightEyeDirectionX = double.NaN,
                RightEyeDirectionY = double.NaN,
                RightEyeDirectionZ = double.NaN
            };
        }

        private static RawTrackerRay ReadCombinedRay(EyeGazeTrackerReading reading)
        {
            RawTrackerRay ray = default(RawTrackerRay);
            ray.IsValid = reading.TryGetCombinedEyeGazeInTrackerSpace(
                    out ray.Origin,
                    out ray.Direction) &&
                IsUsableRay(ray.Origin, ray.Direction);
            return ray;
        }

        private static RawTrackerRay ReadLeftRay(EyeGazeTrackerReading reading)
        {
            RawTrackerRay ray = default(RawTrackerRay);
            ray.IsValid = reading.TryGetLeftEyeGazeInTrackerSpace(
                    out ray.Origin,
                    out ray.Direction) &&
                IsUsableRay(ray.Origin, ray.Direction);
            return ray;
        }

        private static RawTrackerRay ReadRightRay(EyeGazeTrackerReading reading)
        {
            RawTrackerRay ray = default(RawTrackerRay);
            ray.IsValid = reading.TryGetRightEyeGazeInTrackerSpace(
                    out ray.Origin,
                    out ray.Direction) &&
                IsUsableRay(ray.Origin, ray.Direction);
            return ray;
        }

        private static void PopulateWorldSpaceRays(
            RawGazeReading reading,
            Pose trackerPose,
            Transform playspace,
            ref GazeSample sample)
        {
            System.Numerics.Vector3 playspaceFromTrackerPosition =
                ToNumerics(trackerPose.position);
            System.Numerics.Quaternion playspaceFromTrackerRotation =
                ToNumerics(trackerPose.rotation);
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

            if (reading.Combined.IsValid)
            {
                sample.CombinedValid = WriteWorldSpaceRay(
                    reading.Combined.Origin,
                    reading.Combined.Direction,
                    playspaceFromTrackerPosition,
                    playspaceFromTrackerRotation,
                    worldFromPlayspacePosition,
                    worldFromPlayspaceRotation,
                    worldFromPlayspaceScale,
                    out sample.CombinedOriginX,
                    out sample.CombinedOriginY,
                    out sample.CombinedOriginZ,
                    out sample.CombinedDirectionX,
                    out sample.CombinedDirectionY,
                    out sample.CombinedDirectionZ);
            }

            if (reading.Left.IsValid)
            {
                sample.LeftEyeValid = WriteWorldSpaceRay(
                    reading.Left.Origin,
                    reading.Left.Direction,
                    playspaceFromTrackerPosition,
                    playspaceFromTrackerRotation,
                    worldFromPlayspacePosition,
                    worldFromPlayspaceRotation,
                    worldFromPlayspaceScale,
                    out sample.LeftEyeOriginX,
                    out sample.LeftEyeOriginY,
                    out sample.LeftEyeOriginZ,
                    out sample.LeftEyeDirectionX,
                    out sample.LeftEyeDirectionY,
                    out sample.LeftEyeDirectionZ);
            }

            if (reading.Right.IsValid)
            {
                sample.RightEyeValid = WriteWorldSpaceRay(
                    reading.Right.Origin,
                    reading.Right.Direction,
                    playspaceFromTrackerPosition,
                    playspaceFromTrackerRotation,
                    worldFromPlayspacePosition,
                    worldFromPlayspaceRotation,
                    worldFromPlayspaceScale,
                    out sample.RightEyeOriginX,
                    out sample.RightEyeOriginY,
                    out sample.RightEyeOriginZ,
                    out sample.RightEyeDirectionX,
                    out sample.RightEyeDirectionY,
                    out sample.RightEyeDirectionZ);
            }
        }

        private static bool IsUsableRay(
            System.Numerics.Vector3 origin,
            System.Numerics.Vector3 direction)
        {
            return IsFinite(origin.X) && IsFinite(origin.Y) && IsFinite(origin.Z) &&
                   IsFinite(direction.X) && IsFinite(direction.Y) && IsFinite(direction.Z) &&
                   DirectionMagnitudeSquared(direction) > 0.000001f;
        }

        private static bool IsFinite(float value)
        {
            return !float.IsNaN(value) && !float.IsInfinity(value);
        }

        private static float DirectionMagnitudeSquared(System.Numerics.Vector3 value)
        {
            return value.X * value.X + value.Y * value.Y + value.Z * value.Z;
        }

        private static bool WriteWorldSpaceRay(
            System.Numerics.Vector3 origin,
            System.Numerics.Vector3 direction,
            System.Numerics.Vector3 playspaceFromTrackerPosition,
            System.Numerics.Quaternion playspaceFromTrackerRotation,
            System.Numerics.Vector3 worldFromPlayspacePosition,
            System.Numerics.Quaternion worldFromPlayspaceRotation,
            System.Numerics.Vector3 worldFromPlayspaceScale,
            out double originX,
            out double originY,
            out double originZ,
            out double directionX,
            out double directionY,
            out double directionZ)
        {
            System.Numerics.Vector3 worldOrigin;
            System.Numerics.Vector3 worldDirection;
            if (!GazeCoordinateTransform.TryTransformTrackerRayToSharedWorld(
                    origin,
                    direction,
                    playspaceFromTrackerPosition,
                    playspaceFromTrackerRotation,
                    worldFromPlayspacePosition,
                    worldFromPlayspaceRotation,
                    worldFromPlayspaceScale,
                    out worldOrigin,
                    out worldDirection))
            {
                originX = originY = originZ = double.NaN;
                directionX = directionY = directionZ = double.NaN;
                return false;
            }

            originX = worldOrigin.X;
            originY = worldOrigin.Y;
            originZ = worldOrigin.Z;
            directionX = worldDirection.X;
            directionY = worldDirection.Y;
            directionZ = worldDirection.Z;
            return true;
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

        private void StopWatcherAndTracker()
        {
            EyeGazeTrackerWatcher currentWatcher;
            EyeGazeTracker currentTracker;
            lock (trackerGate)
            {
                currentWatcher = watcher;
                watcher = null;
                currentTracker = tracker;
                tracker = null;
                trackerNode = null;
                includeIndividualEyes = false;
                lastReadingTimestamp = DateTime.MinValue;
                lastWallClock = DateTime.MinValue;
                pendingRawReadings.Clear();
                pendingSamples.Clear();
                consecutiveLocateFailures = 0;
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
