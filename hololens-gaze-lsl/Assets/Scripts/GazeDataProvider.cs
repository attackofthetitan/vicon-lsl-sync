using System;
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
    Reads HoloLens 2 Extended Eye Tracking data and converts it into Unity world space.

    This version uses Microsoft.MixedReality.EyeTracking only. The OpenXR / Unity XR
    fallback and Unity Physics raycast hit-point enrichment have intentionally been
    removed so gaze sampling is not tied to Unity frame-rate or scene physics work.
    */
    public class GazeDataProvider : MonoBehaviour
    {
        public struct GazeFrame
        {
            public Vector3 CombinedOrigin;
            public Vector3 CombinedDirection;
            public bool CombinedValid;

            public Vector3 LeftEyeOrigin;
            public Vector3 LeftEyeDirection;
            public bool LeftEyeValid;

            public Vector3 RightEyeOrigin;
            public Vector3 RightEyeDirection;
            public bool RightEyeValid;

            public float VergenceDistance;
            public bool VergenceValid;
        }

        public struct GazeSample
        {
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
        public bool IsExtendedTrackingAvailable { get; private set; }
        public bool AreIndividualEyeGazesSupported { get; private set; }
        public bool IsVergenceSupported { get; private set; }

#if ENABLE_WINMD_SUPPORT
        private readonly object _trackerLock = new object();
        private EyeGazeTrackerWatcher _watcher;
        private EyeGazeTracker _tracker;
        private SpatialLocator _trackerLocator;
        private SpatialCoordinateSystem _worldCoordinateSystem;
#endif

        private GazeFrame _currentFrame;

        private async void Start()
        {
#if ENABLE_WINMD_SUPPORT
            try
            {
                _watcher = new EyeGazeTrackerWatcher();
                _watcher.EyeGazeTrackerAdded += OnTrackerAdded;
                _watcher.EyeGazeTrackerRemoved += OnTrackerRemoved;

                await _watcher.StartAsync();

                Debug.Log("Eye tracker watcher started");
            }
            catch (Exception e)
            {
                Debug.LogError($"Extended eye tracking unavailable - {e.Message}");
                IsExtendedTrackingAvailable = false;
                IsTrackingAvailable = false;
            }
#else
            Debug.LogError("Extended eye tracking requires a HoloLens/UWP device build. OpenXR / Unity XR fallback is disabled.");
            IsExtendedTrackingAvailable = false;
            IsTrackingAvailable = false;
            await System.Threading.Tasks.Task.CompletedTask;
#endif
        }

#if ENABLE_WINMD_SUPPORT
        private async void OnTrackerAdded(object sender, EyeGazeTracker tracker)
        {
            try
            {
                Debug.Log("Eye tracker found, opening with restricted access");

                await tracker.OpenAsync(true);

                AreIndividualEyeGazesSupported = tracker.AreLeftAndRightGazesSupported;
                IsVergenceSupported = tracker.IsVergenceDistanceSupported;

                SetBestSupportedFrameRate(tracker);

                SpatialLocator trackerLocator = SpatialGraphInteropPreview.CreateLocatorForNode(
                    tracker.TrackerSpaceLocatorNodeId
                );

                if (trackerLocator == null)
                {
                    Debug.LogError("Failed to create SpatialLocator for eye tracker.");
                    IsExtendedTrackingAvailable = false;
                    IsTrackingAvailable = false;
                    return;
                }

                SpatialCoordinateSystem worldCoordinateSystem = null;
                SpatialLocator spatialLocator = SpatialLocator.GetDefault();
                if (spatialLocator != null)
                {
                    worldCoordinateSystem = spatialLocator
                        .CreateStationaryFrameOfReferenceAtCurrentLocation()
                        .CoordinateSystem;
                }

                if (worldCoordinateSystem == null)
                {
                    Debug.LogError("Failed to create world coordinate system.");
                    IsExtendedTrackingAvailable = false;
                    IsTrackingAvailable = false;
                    return;
                }

                lock (_trackerLock)
                {
                    _tracker = tracker;
                    _trackerLocator = trackerLocator;
                    _worldCoordinateSystem = worldCoordinateSystem;
                    IsExtendedTrackingAvailable = true;
                    IsTrackingAvailable = true;
                }

                Debug.Log(
                    $"Extended tracking ready. " +
                    $"Per-eye: {AreIndividualEyeGazesSupported}, " +
                    $"Vergence: {IsVergenceSupported}"
                );
            }
            catch (Exception e)
            {
                Debug.LogError($"Failed to open eye tracker - {e.Message}");

                lock (_trackerLock)
                {
                    _tracker = null;
                    _trackerLocator = null;
                    _worldCoordinateSystem = null;
                    IsExtendedTrackingAvailable = false;
                    IsTrackingAvailable = false;
                }
            }
        }

        private void OnTrackerRemoved(object sender, EyeGazeTracker tracker)
        {
            lock (_trackerLock)
            {
                if (_tracker == tracker)
                {
                    _tracker = null;
                    _trackerLocator = null;
                    _worldCoordinateSystem = null;

                    IsExtendedTrackingAvailable = false;
                    IsTrackingAvailable = false;

                    Debug.LogWarning("Eye tracker removed");
                }
            }
        }

        private void SetBestSupportedFrameRate(EyeGazeTracker tracker)
        {
            try
            {
                uint requestedFrameRate = config != null ? config.TargetFrameRate : 90;

                var supportedRates = tracker.SupportedTargetFrameRates;

                if (supportedRates == null || supportedRates.Count == 0)
                {
                    Debug.LogWarning("No supported eye tracker frame rates reported.");
                    return;
                }

                var chosenRate = supportedRates[0];

                foreach (var rate in supportedRates)
                {
                    if (rate.FramesPerSecond <= requestedFrameRate)
                    {
                        chosenRate = rate;
                    }
                }

                tracker.SetTargetFrameRate(chosenRate);

                Debug.Log($"Eye tracker frame rate set to {chosenRate.FramesPerSecond} fps");
            }
            catch (Exception e)
            {
                Debug.LogWarning($"Could not set eye tracker frame rate - {e.Message}");
            }
        }
#endif

        public GazeFrame GetCurrentFrame()
        {
            _currentFrame = new GazeFrame();

#if ENABLE_WINMD_SUPPORT
            if (TryGetCurrentSample(out GazeSample sample))
            {
                _currentFrame.CombinedOrigin = new Vector3((float)sample.CombinedOriginX, (float)sample.CombinedOriginY, (float)sample.CombinedOriginZ);
                _currentFrame.CombinedDirection = new Vector3((float)sample.CombinedDirectionX, (float)sample.CombinedDirectionY, (float)sample.CombinedDirectionZ);
                _currentFrame.CombinedValid = sample.CombinedValid;

                _currentFrame.LeftEyeOrigin = new Vector3((float)sample.LeftEyeOriginX, (float)sample.LeftEyeOriginY, (float)sample.LeftEyeOriginZ);
                _currentFrame.LeftEyeDirection = new Vector3((float)sample.LeftEyeDirectionX, (float)sample.LeftEyeDirectionY, (float)sample.LeftEyeDirectionZ);
                _currentFrame.LeftEyeValid = sample.LeftEyeValid;

                _currentFrame.RightEyeOrigin = new Vector3((float)sample.RightEyeOriginX, (float)sample.RightEyeOriginY, (float)sample.RightEyeOriginZ);
                _currentFrame.RightEyeDirection = new Vector3((float)sample.RightEyeDirectionX, (float)sample.RightEyeDirectionY, (float)sample.RightEyeDirectionZ);
                _currentFrame.RightEyeValid = sample.RightEyeValid;

                _currentFrame.VergenceDistance = (float)sample.VergenceDistance;
                _currentFrame.VergenceValid = sample.VergenceValid;
            }
#else
            return _currentFrame;
#endif

            return _currentFrame;
        }

        public bool TryGetCurrentSample(out GazeSample sample)
        {
            sample = new GazeSample();

#if ENABLE_WINMD_SUPPORT
            EyeGazeTracker tracker;
            SpatialLocator trackerLocator;
            SpatialCoordinateSystem worldCoordinateSystem;

            lock (_trackerLock)
            {
                tracker = _tracker;
                trackerLocator = _trackerLocator;
                worldCoordinateSystem = _worldCoordinateSystem;
            }

            if (tracker == null || trackerLocator == null || worldCoordinateSystem == null)
            {
                return false;
            }

            DateTime targetTime = DateTime.Now;

            var reading = tracker.TryGetReadingAtTimestamp(targetTime);
            if (reading == null)
            {
                return false;
            }

            PerceptionTimestamp perceptionTimestamp =
                PerceptionTimestampHelper.FromHistoricalTargetTime(new DateTimeOffset(targetTime));

            SpatialLocation trackerLocation =
                trackerLocator.TryLocateAtTimestamp(perceptionTimestamp, worldCoordinateSystem);

            if (trackerLocation == null)
            {
                return false;
            }

            if (reading.TryGetCombinedEyeGazeInTrackerSpace(
                    out System.Numerics.Vector3 combinedOrigin,
                    out System.Numerics.Vector3 combinedDirection))
            {
                System.Numerics.Vector3 origin = TransformTrackerPointToWorld(combinedOrigin, trackerLocation);
                System.Numerics.Vector3 direction = TransformTrackerDirectionToWorld(combinedDirection, trackerLocation);

                sample.CombinedOriginX = origin.X;
                sample.CombinedOriginY = origin.Y;
                sample.CombinedOriginZ = -origin.Z;
                sample.CombinedDirectionX = direction.X;
                sample.CombinedDirectionY = direction.Y;
                sample.CombinedDirectionZ = -direction.Z;
                sample.CombinedValid = true;
            }

            if (AreIndividualEyeGazesSupported &&
                reading.TryGetLeftEyeGazeInTrackerSpace(
                    out System.Numerics.Vector3 leftOrigin,
                    out System.Numerics.Vector3 leftDirection))
            {
                System.Numerics.Vector3 origin = TransformTrackerPointToWorld(leftOrigin, trackerLocation);
                System.Numerics.Vector3 direction = TransformTrackerDirectionToWorld(leftDirection, trackerLocation);

                sample.LeftEyeOriginX = origin.X;
                sample.LeftEyeOriginY = origin.Y;
                sample.LeftEyeOriginZ = -origin.Z;
                sample.LeftEyeDirectionX = direction.X;
                sample.LeftEyeDirectionY = direction.Y;
                sample.LeftEyeDirectionZ = -direction.Z;
                sample.LeftEyeValid = true;
            }

            if (AreIndividualEyeGazesSupported &&
                reading.TryGetRightEyeGazeInTrackerSpace(
                    out System.Numerics.Vector3 rightOrigin,
                    out System.Numerics.Vector3 rightDirection))
            {
                System.Numerics.Vector3 origin = TransformTrackerPointToWorld(rightOrigin, trackerLocation);
                System.Numerics.Vector3 direction = TransformTrackerDirectionToWorld(rightDirection, trackerLocation);

                sample.RightEyeOriginX = origin.X;
                sample.RightEyeOriginY = origin.Y;
                sample.RightEyeOriginZ = -origin.Z;
                sample.RightEyeDirectionX = direction.X;
                sample.RightEyeDirectionY = direction.Y;
                sample.RightEyeDirectionZ = -direction.Z;
                sample.RightEyeValid = true;
            }

            if (IsVergenceSupported && reading.TryGetVergenceDistance(out float vergence))
            {
                sample.VergenceDistance = vergence;
                sample.VergenceValid = true;
            }

            return sample.CombinedValid || sample.LeftEyeValid || sample.RightEyeValid || sample.VergenceValid;
#else
            return false;
#endif
        }

#if ENABLE_WINMD_SUPPORT
        private System.Numerics.Vector3 TransformTrackerPointToWorld(
            System.Numerics.Vector3 trackerPoint,
            SpatialLocation trackerLocation)
        {
            return System.Numerics.Vector3.Transform(trackerPoint, trackerLocation.Orientation) + trackerLocation.Position;
        }

        private System.Numerics.Vector3 TransformTrackerDirectionToWorld(
            System.Numerics.Vector3 trackerDirection,
            SpatialLocation trackerLocation)
        {
            return System.Numerics.Vector3.Normalize(
                System.Numerics.Vector3.Transform(trackerDirection, trackerLocation.Orientation)
            );
        }
#endif

        private void OnDestroy()
        {
#if ENABLE_WINMD_SUPPORT
            if (_watcher != null)
            {
                _watcher.EyeGazeTrackerAdded -= OnTrackerAdded;
                _watcher.EyeGazeTrackerRemoved -= OnTrackerRemoved;
                _watcher.Stop();
                _watcher = null;
            }

            EyeGazeTracker trackerToClose = null;
            lock (_trackerLock)
            {
                trackerToClose = _tracker;
                _tracker = null;
                _trackerLocator = null;
                _worldCoordinateSystem = null;
            }

            if (trackerToClose != null)
            {
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
        }
    }
}
