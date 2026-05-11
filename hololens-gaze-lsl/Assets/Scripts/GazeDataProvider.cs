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

        [SerializeField] private GazeLSLConfig config;

        public bool IsTrackingAvailable { get; private set; }
        public bool IsExtendedTrackingAvailable { get; private set; }
        public bool AreIndividualEyeGazesSupported { get; private set; }
        public bool IsVergenceSupported { get; private set; }

#if ENABLE_WINMD_SUPPORT
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

                _tracker = tracker;
                IsExtendedTrackingAvailable = true;
                IsTrackingAvailable = true;

                AreIndividualEyeGazesSupported = tracker.AreLeftAndRightGazesSupported;
                IsVergenceSupported = tracker.IsVergenceDistanceSupported;

                SetBestSupportedFrameRate(tracker);

                _trackerLocator = SpatialGraphInteropPreview.CreateLocatorForNode(
                    tracker.TrackerSpaceLocatorNodeId
                );

                if (_trackerLocator == null)
                {
                    Debug.LogError("Failed to create SpatialLocator for eye tracker.");
                    IsExtendedTrackingAvailable = false;
                    IsTrackingAvailable = false;
                    return;
                }

                SpatialLocator spatialLocator = SpatialLocator.GetDefault();
                if (spatialLocator != null)
                {
                    _worldCoordinateSystem = spatialLocator
                        .CreateStationaryFrameOfReferenceAtCurrentLocation()
                        .CoordinateSystem;
                }

                if (_worldCoordinateSystem == null)
                {
                    Debug.LogError("Failed to create world coordinate system.");
                    IsExtendedTrackingAvailable = false;
                    IsTrackingAvailable = false;
                    return;
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

                _tracker = null;
                _trackerLocator = null;
                _worldCoordinateSystem = null;

                IsExtendedTrackingAvailable = false;
                IsTrackingAvailable = false;
            }
        }

        private void OnTrackerRemoved(object sender, EyeGazeTracker tracker)
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
            if (IsExtendedTrackingAvailable && _tracker != null)
            {
                ReadExtendedEyeTracking();
            }
#else
            return _currentFrame;
#endif

            return _currentFrame;
        }

#if ENABLE_WINMD_SUPPORT
        private void ReadExtendedEyeTracking()
        {
            if (_tracker == null || _trackerLocator == null || _worldCoordinateSystem == null)
            {
                return;
            }

            DateTimeOffset targetTime = DateTimeOffset.Now;

            var reading = _tracker.TryGetReadingAtTimestamp(targetTime);
            if (reading == null)
            {
                return;
            }

            PerceptionTimestamp perceptionTimestamp =
                PerceptionTimestampHelper.FromHistoricalTargetTime(targetTime);

            SpatialLocation trackerLocation =
                _trackerLocator.TryLocateAtTimestamp(perceptionTimestamp, _worldCoordinateSystem);

            if (trackerLocation == null)
            {
                return;
            }

            if (reading.TryGetCombinedEyeGazeInTrackerSpace(
                    out System.Numerics.Vector3 combinedOrigin,
                    out System.Numerics.Vector3 combinedDirection))
            {
                _currentFrame.CombinedOrigin =
                    TransformTrackerPointToUnityWorld(combinedOrigin, trackerLocation);

                _currentFrame.CombinedDirection =
                    TransformTrackerDirectionToUnityWorld(combinedDirection, trackerLocation);

                _currentFrame.CombinedValid = true;
            }

            if (AreIndividualEyeGazesSupported &&
                reading.TryGetLeftEyeGazeInTrackerSpace(
                    out System.Numerics.Vector3 leftOrigin,
                    out System.Numerics.Vector3 leftDirection))
            {
                _currentFrame.LeftEyeOrigin =
                    TransformTrackerPointToUnityWorld(leftOrigin, trackerLocation);

                _currentFrame.LeftEyeDirection =
                    TransformTrackerDirectionToUnityWorld(leftDirection, trackerLocation);

                _currentFrame.LeftEyeValid = true;
            }

            if (AreIndividualEyeGazesSupported &&
                reading.TryGetRightEyeGazeInTrackerSpace(
                    out System.Numerics.Vector3 rightOrigin,
                    out System.Numerics.Vector3 rightDirection))
            {
                _currentFrame.RightEyeOrigin =
                    TransformTrackerPointToUnityWorld(rightOrigin, trackerLocation);

                _currentFrame.RightEyeDirection =
                    TransformTrackerDirectionToUnityWorld(rightDirection, trackerLocation);

                _currentFrame.RightEyeValid = true;
            }

            if (IsVergenceSupported && reading.TryGetVergenceDistance(out float vergence))
            {
                _currentFrame.VergenceDistance = vergence;
                _currentFrame.VergenceValid = true;
            }
        }

        private Vector3 TransformTrackerPointToUnityWorld(
            System.Numerics.Vector3 trackerPoint,
            SpatialLocation trackerLocation)
        {
            System.Numerics.Vector3 position = trackerLocation.Position;
            System.Numerics.Quaternion rotation = trackerLocation.Orientation;

            System.Numerics.Vector3 transformed =
                System.Numerics.Vector3.Transform(trackerPoint, rotation) + position;

            return new Vector3(
                transformed.X,
                transformed.Y,
                -transformed.Z
            );
        }

        private Vector3 TransformTrackerDirectionToUnityWorld(
            System.Numerics.Vector3 trackerDirection,
            SpatialLocation trackerLocation)
        {
            System.Numerics.Quaternion rotation = trackerLocation.Orientation;

            System.Numerics.Vector3 transformed =
                System.Numerics.Vector3.Transform(trackerDirection, rotation);

            return new Vector3(
                transformed.X,
                transformed.Y,
                -transformed.Z
            ).normalized;
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

            if (_tracker != null)
            {
                try
                {
                    _tracker.Close();
                }
                catch (Exception e)
                {
                    Debug.LogWarning($"Error closing eye tracker - {e.Message}");
                }
            }

            _tracker = null;
            _trackerLocator = null;
            _worldCoordinateSystem = null;
#endif
        }
    }
}
