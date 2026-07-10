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
    public sealed class GazeDataProvider : MonoBehaviour, IGazeSampleProvider
    {
        [SerializeField] private GazeLSLConfig config;

        public bool IsTrackingAvailable
        {
            get
            {
#if ENABLE_WINMD_SUPPORT
                return trackerSessions.HasActiveSession;
#else
                return false;
#endif
            }
        }

        public bool AreIndividualEyeGazesSupported
        {
            get
            {
#if ENABLE_WINMD_SUPPORT
                TrackerSessionState state;
                return trackerSessions.TryGetActiveState(out state) && state.IncludeIndividualEyes;
#else
                return false;
#endif
            }
        }

#if ENABLE_WINMD_SUPPORT
        private sealed class TrackerSessionState
        {
            public SpatialLocator Locator;
            public SpatialCoordinateSystem WorldCoordinateSystem;
            public DateTime LastConsumedReadingTimestamp;
            public bool IncludeIndividualEyes;
        }

        private readonly TrackerSessionCoordinator<EyeGazeTracker, TrackerSessionState> trackerSessions =
            new TrackerSessionCoordinator<EyeGazeTracker, TrackerSessionState>(CloseTracker);
        private EyeGazeTrackerWatcher watcher;
#endif

        private async void Start()
        {
#if ENABLE_WINMD_SUPPORT
            try
            {
                watcher = new EyeGazeTrackerWatcher();
                watcher.EyeGazeTrackerAdded += OnTrackerAdded;
                watcher.EyeGazeTrackerRemoved += OnTrackerRemoved;

                await watcher.StartAsync();

                Debug.Log("Eye tracker watcher started");
            }
            catch (Exception e)
            {
#if ENABLE_WINMD_SUPPORT
                trackerSessions.Destroy();
#endif
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
            TrackerSessionCoordinator<EyeGazeTracker, TrackerSessionState>.OpenAttempt attempt =
                trackerSessions.BeginOpen(newTracker);
            if (!attempt.IsValid)
            {
                return;
            }

            try
            {
                Debug.Log("Eye tracker found, opening with restricted access");

                await newTracker.OpenAsync(true);

                SpatialLocator newTrackerLocator = SpatialGraphInteropPreview.CreateLocatorForNode(
                    newTracker.TrackerSpaceLocatorNodeId
                );

                if (newTrackerLocator == null)
                {
                    trackerSessions.Abandon(attempt);
                    Debug.LogError("Failed to create SpatialLocator for eye tracker.");
                    return;
                }

                SpatialCoordinateSystem newWorldCoordinateSystem = CreateWorldCoordinateSystem();
                if (newWorldCoordinateSystem == null)
                {
                    trackerSessions.Abandon(attempt);
                    Debug.LogError("Failed to create world coordinate system.");
                    return;
                }

                ConfigureFrameRate(newTracker);

                TrackerSessionState state = new TrackerSessionState
                {
                    Locator = newTrackerLocator,
                    WorldCoordinateSystem = newWorldCoordinateSystem,
                    // Start from now so old buffered readings are not published when tracking starts.
                    LastConsumedReadingTimestamp = DateTime.Now,
                    IncludeIndividualEyes = newTracker.AreLeftAndRightGazesSupported
                };

                if (!trackerSessions.TryActivate(attempt, state))
                {
                    return;
                }

                Debug.Log(
                    $"Extended eye tracking ready. " +
                    $"Per-eye: {state.IncludeIndividualEyes}"
                );
            }
            catch (Exception e)
            {
                trackerSessions.Abandon(attempt);
                Debug.LogError($"Failed to open eye tracker - {e.Message}");
            }
        }

        private void OnTrackerRemoved(object sender, EyeGazeTracker removedTracker)
        {
            if (trackerSessions.Retire(removedTracker))
            {
                Debug.LogWarning("Eye tracker removed");
            }
        }
#endif

        public bool TryGetNextSample(out GazeSample sample)
        {
            sample = default(GazeSample);

#if ENABLE_WINMD_SUPPORT
            TrackerSessionCoordinator<EyeGazeTracker, TrackerSessionState>.ReadLease lease;
            if (!trackerSessions.TryAcquireRead(out lease))
            {
                return false;
            }

            using (lease)
            {
                TrackerSessionState state = lease.State;
                EyeGazeTrackerReading reading = lease.Tracker.TryGetReadingAfterTimestamp(
                    state.LastConsumedReadingTimestamp
                );
                if (reading == null)
                {
                    return false;
                }

                if (reading.Timestamp > state.LastConsumedReadingTimestamp)
                {
                    state.LastConsumedReadingTimestamp = reading.Timestamp;
                }

                sample = CreateEmptySample();

                PerceptionTimestamp perceptionTimestamp =
                    PerceptionTimestampHelper.FromHistoricalTargetTime(new DateTimeOffset(reading.Timestamp));

                SpatialLocation trackerLocation = state.Locator.TryLocateAtTimestamp(
                    perceptionTimestamp,
                    state.WorldCoordinateSystem
                );

                if (trackerLocation != null)
                {
                    PopulateGazeSample(reading, trackerLocation, state.IncludeIndividualEyes, ref sample);
                }

                // Return true for every acquired tracker reading. Invalid gaze is represented
                // by NaNs and valid flags, not by dropping the whole sample.
                return true;
            }
#else
            return false;
#endif
        }

#if ENABLE_WINMD_SUPPORT
        private static GazeSample CreateEmptySample()
        {
            GazeSample sample = new GazeSample
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

        private static void PopulateGazeSample(
            EyeGazeTrackerReading reading,
            SpatialLocation trackerLocation,
            bool includeIndividualEyes,
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
            trackerSessions.RetireActive();
#endif
        }

#if ENABLE_WINMD_SUPPORT
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
            if (watcher != null)
            {
                watcher.EyeGazeTrackerAdded -= OnTrackerAdded;
                watcher.EyeGazeTrackerRemoved -= OnTrackerRemoved;
                watcher.Stop();
                watcher = null;
            }

            trackerSessions.Destroy();
#endif
        }
    }
}
