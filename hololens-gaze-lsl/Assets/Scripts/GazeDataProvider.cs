using System;
using System.Text;
using UnityEngine;

#if ENABLE_WINMD_SUPPORT
using Microsoft.MixedReality.EyeTracking;
using Windows.Perception;
using Windows.Perception.Spatial;
using Windows.Perception.Spatial.Preview;
#endif

namespace GazeLSL
{
    /*
    Reads HoloLens 2 Extended Eye Tracking data and converts tracker readings
    into Unity-compatible world-space gaze samples.

    The Microsoft.MixedReality.EyeTracking API is buffer-based. This provider
    returns each acquired tracker reading. Missing or invalid gaze rays are encoded
    as NaN values with their valid flag set to 0 by the outlet.
    */
    public sealed class GazeDataProvider : MonoBehaviour
    {
        public struct GazeSample
        {
            public DateTime TrackerTimestamp;
            public TimeSpan TrackerSystemRelativeTime;
            public bool CalibrationValid;

            public double CombinedOriginX;
            public double CombinedOriginY;
            public double CombinedOriginZ;
            public double CombinedDirectionX;
            public double CombinedDirectionY;
            public double CombinedDirectionZ;
            public bool CombinedValid;

            public double LeftEyeOriginX;
            public double LeftEyeOriginY;
            public double LeftEyeOriginZ;
            public double LeftEyeDirectionX;
            public double LeftEyeDirectionY;
            public double LeftEyeDirectionZ;
            public bool LeftEyeValid;

            public double RightEyeOriginX;
            public double RightEyeOriginY;
            public double RightEyeOriginZ;
            public double RightEyeDirectionX;
            public double RightEyeDirectionY;
            public double RightEyeDirectionZ;
            public bool RightEyeValid;

            public double VergenceDistance;
            public bool VergenceValid;
        }

        [SerializeField] private GazeLSLConfig config;

        public bool IsTrackingAvailable { get; private set; }
        public bool AreIndividualEyeGazesSupported { get; private set; }
        public bool IsVergenceSupported { get; private set; }

#if ENABLE_WINMD_SUPPORT
        private readonly object trackerLock = new object();
        private EyeGazeTrackerWatcher watcher;
        private EyeGazeTracker tracker;
        private SpatialLocator trackerLocator;
        private SpatialCoordinateSystem worldCoordinateSystem;
        private DateTime lastConsumedReadingTimestamp;
#endif

        private bool isDestroying;

        private async void Start()
        {
#if ENABLE_WINMD_SUPPORT
            try
            {
                watcher = new EyeGazeTrackerWatcher();
                watcher.EyeGazeTrackerAdded += OnTrackerAdded;
                watcher.EyeGazeTrackerRemoved += OnTrackerRemoved;

                await watcher.StartAsync();

                if (!isDestroying)
                {
                    Debug.Log("Eye tracker watcher started");
                }
            }
            catch (Exception e)
            {
                SetTrackingUnavailable();
                Debug.LogError($"Extended eye tracking unavailable - {e.Message}");
            }
#else
            SetTrackingUnavailable();
            Debug.LogError("Extended eye tracking requires a HoloLens/UWP device build.");
            await System.Threading.Tasks.Task.CompletedTask;
#endif
        }

#if ENABLE_WINMD_SUPPORT
        private async void OnTrackerAdded(object sender, EyeGazeTracker newTracker)
        {
            try
            {
                Debug.Log("Eye tracker found, opening with restricted access");

                await newTracker.OpenAsync(true);

                if (isDestroying)
                {
                    CloseTracker(newTracker);
                    return;
                }

                SpatialLocator newTrackerLocator = SpatialGraphInteropPreview.CreateLocatorForNode(
                    newTracker.TrackerSpaceLocatorNodeId
                );

                if (newTrackerLocator == null)
                {
                    CloseTracker(newTracker);
                    SetTrackingUnavailable();
                    Debug.LogError("Failed to create SpatialLocator for eye tracker.");
                    return;
                }

                SpatialCoordinateSystem newWorldCoordinateSystem = CreateWorldCoordinateSystem();
                if (newWorldCoordinateSystem == null)
                {
                    CloseTracker(newTracker);
                    SetTrackingUnavailable();
                    Debug.LogError("Failed to create world coordinate system.");
                    return;
                }

                ConfigureFrameRate(newTracker);

                EyeGazeTracker oldTracker;
                lock (trackerLock)
                {
                    oldTracker = tracker;

                    tracker = newTracker;
                    trackerLocator = newTrackerLocator;
                    worldCoordinateSystem = newWorldCoordinateSystem;

                    // Start from now so old buffered readings are not published when tracking starts.
                    lastConsumedReadingTimestamp = DateTime.Now;

                    AreIndividualEyeGazesSupported = newTracker.AreLeftAndRightGazesSupported;
                    IsVergenceSupported = newTracker.IsVergenceDistanceSupported;
                    IsTrackingAvailable = true;
                }

                if (oldTracker != newTracker)
                {
                    CloseTracker(oldTracker);
                }

                Debug.Log(
                    $"Extended eye tracking ready. " +
                    $"Per-eye: {AreIndividualEyeGazesSupported}, " +
                    $"Vergence: {IsVergenceSupported}"
                );
            }
            catch (Exception e)
            {
                CloseTracker(newTracker);
                SetTrackingUnavailable();
                Debug.LogError($"Failed to open eye tracker - {e.Message}");
            }
        }

        private void OnTrackerRemoved(object sender, EyeGazeTracker removedTracker)
        {
            EyeGazeTracker trackerToClose = null;

            lock (trackerLock)
            {
                if (tracker != removedTracker)
                {
                    return;
                }

                trackerToClose = tracker;
                ClearTrackerStateLocked();
            }

            CloseTracker(trackerToClose);
            Debug.LogWarning("Eye tracker removed");
        }
#endif

        public bool TryGetNextSample(out GazeSample sample)
        {
            sample = default(GazeSample);

#if ENABLE_WINMD_SUPPORT
            EyeGazeTracker currentTracker;
            SpatialLocator currentTrackerLocator;
            SpatialCoordinateSystem currentWorldCoordinateSystem;
            DateTime previousTimestamp;
            bool includeIndividualEyes;
            bool includeVergence;

            lock (trackerLock)
            {
                currentTracker = tracker;
                currentTrackerLocator = trackerLocator;
                currentWorldCoordinateSystem = worldCoordinateSystem;
                previousTimestamp = lastConsumedReadingTimestamp;
                includeIndividualEyes = AreIndividualEyeGazesSupported;
                includeVergence = IsVergenceSupported;
            }

            if (currentTracker == null || currentTrackerLocator == null || currentWorldCoordinateSystem == null)
            {
                return false;
            }

            EyeGazeTrackerReading reading = currentTracker.TryGetReadingAfterTimestamp(previousTimestamp);
            if (reading == null)
            {
                return false;
            }

            MarkReadingConsumed(currentTracker, reading.Timestamp);

            sample = CreateEmptySample(reading);

            PerceptionTimestamp perceptionTimestamp =
                PerceptionTimestampHelper.FromHistoricalTargetTime(new DateTimeOffset(reading.Timestamp));

            SpatialLocation trackerLocation =
                currentTrackerLocator.TryLocateAtTimestamp(perceptionTimestamp, currentWorldCoordinateSystem);

            if (trackerLocation != null)
            {
                PopulateGazeSample(reading, trackerLocation, includeIndividualEyes, includeVergence, ref sample);
            }

            // Return true for every acquired tracker reading. Invalid gaze is represented
            // by NaNs and valid flags, not by dropping the whole sample.
            return true;
#else
            return false;
#endif
        }

#if ENABLE_WINMD_SUPPORT
        private static GazeSample CreateEmptySample(EyeGazeTrackerReading reading)
        {
            GazeSample sample = new GazeSample
            {
                TrackerTimestamp = reading.Timestamp,
                TrackerSystemRelativeTime = reading.SystemRelativeTime,
                CalibrationValid = reading.IsCalibrationValid,

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
                RightEyeDirectionZ = double.NaN,

                VergenceDistance = double.NaN
            };

            return sample;
        }

        private static SpatialCoordinateSystem CreateWorldCoordinateSystem()
        {
            SpatialLocator spatialLocator = SpatialLocator.GetDefault();
            if (spatialLocator == null)
            {
                return null;
            }

            return spatialLocator
                .CreateStationaryFrameOfReferenceAtCurrentLocation()
                .CoordinateSystem;
        }

        private void ConfigureFrameRate(EyeGazeTracker currentTracker)
        {
            try
            {
                uint requestedFrameRate = config != null ? config.TargetFrameRate : 90;
                var supportedRates = currentTracker.SupportedTargetFrameRates;

                if (supportedRates == null || supportedRates.Count == 0)
                {
                    Debug.LogWarning("No supported eye tracker frame rates reported.");
                    return;
                }

                int highestIndex = 0;
                int bestAtOrBelowRequestIndex = -1;

                StringBuilder supportedRatesLog = new StringBuilder();
                supportedRatesLog.Append("Supported eye tracker frame rates:");

                for (int i = 0; i < supportedRates.Count; i++)
                {
                    uint framesPerSecond = supportedRates[i].FramesPerSecond;
                    supportedRatesLog.Append(' ').Append(framesPerSecond).Append("Hz");

                    if (framesPerSecond > supportedRates[highestIndex].FramesPerSecond)
                    {
                        highestIndex = i;
                    }

                    if (framesPerSecond <= requestedFrameRate &&
                        (bestAtOrBelowRequestIndex < 0 ||
                         framesPerSecond > supportedRates[bestAtOrBelowRequestIndex].FramesPerSecond))
                    {
                        bestAtOrBelowRequestIndex = i;
                    }
                }

                int selectedIndex = bestAtOrBelowRequestIndex >= 0
                    ? bestAtOrBelowRequestIndex
                    : highestIndex;

                var selectedRate = supportedRates[selectedIndex];
                currentTracker.SetTargetFrameRate(selectedRate);

                Debug.Log(supportedRatesLog.ToString());
                Debug.Log(
                    $"Requested eye tracker frame rate: {requestedFrameRate} Hz; " +
                    $"selected: {selectedRate.FramesPerSecond} Hz"
                );
            }
            catch (Exception e)
            {
                Debug.LogWarning($"Could not set eye tracker frame rate - {e.Message}");
            }
        }

        private void MarkReadingConsumed(EyeGazeTracker sourceTracker, DateTime readingTimestamp)
        {
            lock (trackerLock)
            {
                if (tracker != sourceTracker)
                {
                    return;
                }

                if (readingTimestamp > lastConsumedReadingTimestamp)
                {
                    lastConsumedReadingTimestamp = readingTimestamp;
                }
            }
        }

        private static void PopulateGazeSample(
            EyeGazeTrackerReading reading,
            SpatialLocation trackerLocation,
            bool includeIndividualEyes,
            bool includeVergence,
            ref GazeSample sample)
        {
            if (reading.TryGetCombinedEyeGazeInTrackerSpace(
                    out System.Numerics.Vector3 combinedOrigin,
                    out System.Numerics.Vector3 combinedDirection) &&
                IsUsableRay(combinedOrigin, combinedDirection))
            {
                WriteRay(combinedOrigin, combinedDirection, trackerLocation,
                    out sample.CombinedOriginX, out sample.CombinedOriginY, out sample.CombinedOriginZ,
                    out sample.CombinedDirectionX, out sample.CombinedDirectionY, out sample.CombinedDirectionZ);

                sample.CombinedValid = true;
            }

            if (includeIndividualEyes &&
                reading.TryGetLeftEyeGazeInTrackerSpace(
                    out System.Numerics.Vector3 leftOrigin,
                    out System.Numerics.Vector3 leftDirection) &&
                IsUsableRay(leftOrigin, leftDirection))
            {
                WriteRay(leftOrigin, leftDirection, trackerLocation,
                    out sample.LeftEyeOriginX, out sample.LeftEyeOriginY, out sample.LeftEyeOriginZ,
                    out sample.LeftEyeDirectionX, out sample.LeftEyeDirectionY, out sample.LeftEyeDirectionZ);

                sample.LeftEyeValid = true;
            }

            if (includeIndividualEyes &&
                reading.TryGetRightEyeGazeInTrackerSpace(
                    out System.Numerics.Vector3 rightOrigin,
                    out System.Numerics.Vector3 rightDirection) &&
                IsUsableRay(rightOrigin, rightDirection))
            {
                WriteRay(rightOrigin, rightDirection, trackerLocation,
                    out sample.RightEyeOriginX, out sample.RightEyeOriginY, out sample.RightEyeOriginZ,
                    out sample.RightEyeDirectionX, out sample.RightEyeDirectionY, out sample.RightEyeDirectionZ);

                sample.RightEyeValid = true;
            }

            if (includeVergence && reading.TryGetVergenceDistance(out float vergenceDistance))
            {
                sample.VergenceDistance = vergenceDistance;
                sample.VergenceValid = true;
            }
        }

        private static bool IsUsableRay(
            System.Numerics.Vector3 origin,
            System.Numerics.Vector3 direction)
        {
            return IsFinite(origin.X) && IsFinite(origin.Y) && IsFinite(origin.Z) &&
                   IsFinite(direction.X) && IsFinite(direction.Y) && IsFinite(direction.Z) &&
                   SquaredMagnitude(direction) > 0.000001f;
        }

        private static bool IsFinite(float value)
        {
            return !float.IsNaN(value) && !float.IsInfinity(value);
        }

        private static float SquaredMagnitude(System.Numerics.Vector3 value)
        {
            return value.X * value.X + value.Y * value.Y + value.Z * value.Z;
        }

        private static void WriteRay(
            System.Numerics.Vector3 trackerOrigin,
            System.Numerics.Vector3 trackerDirection,
            SpatialLocation trackerLocation,
            out double originX,
            out double originY,
            out double originZ,
            out double directionX,
            out double directionY,
            out double directionZ)
        {
            System.Numerics.Vector3 worldOrigin =
                System.Numerics.Vector3.Transform(trackerOrigin, trackerLocation.Orientation) + trackerLocation.Position;

            System.Numerics.Vector3 worldDirection = System.Numerics.Vector3.Normalize(
                System.Numerics.Vector3.Transform(trackerDirection, trackerLocation.Orientation)
            );

            originX = worldOrigin.X;
            originY = worldOrigin.Y;
            originZ = -worldOrigin.Z;
            directionX = worldDirection.X;
            directionY = worldDirection.Y;
            directionZ = -worldDirection.Z;
        }
#endif

        private void SetTrackingUnavailable()
        {
#if ENABLE_WINMD_SUPPORT
            EyeGazeTracker trackerToClose;
            lock (trackerLock)
            {
                trackerToClose = tracker;
                ClearTrackerStateLocked();
            }

            CloseTracker(trackerToClose);
#endif

            AreIndividualEyeGazesSupported = false;
            IsVergenceSupported = false;
            IsTrackingAvailable = false;
        }

#if ENABLE_WINMD_SUPPORT
        private void ClearTrackerStateLocked()
        {
            tracker = null;
            trackerLocator = null;
            worldCoordinateSystem = null;
            lastConsumedReadingTimestamp = DateTime.Now;
            AreIndividualEyeGazesSupported = false;
            IsVergenceSupported = false;
            IsTrackingAvailable = false;
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
            isDestroying = true;

#if ENABLE_WINMD_SUPPORT
            if (watcher != null)
            {
                watcher.EyeGazeTrackerAdded -= OnTrackerAdded;
                watcher.EyeGazeTrackerRemoved -= OnTrackerRemoved;
                watcher.Stop();
                watcher = null;
            }

            EyeGazeTracker trackerToClose;
            lock (trackerLock)
            {
                trackerToClose = tracker;
                ClearTrackerStateLocked();
            }

            CloseTracker(trackerToClose);
#endif
        }
    }
}
