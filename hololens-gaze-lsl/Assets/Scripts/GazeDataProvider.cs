using System;
using UnityEngine;

#if ENABLE_WINMD_SUPPORT
using Microsoft.MixedReality.EyeTracking;
using Windows.Perception;
using Windows.Perception.Spatial;
#endif

namespace GazeLSL
{
    /*
    Reads eye tracking data from HoloLens 2 Extended Eye Tracking SDK
    with fallback to standard OpenXR eye tracking.
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

            public Vector3 HitPoint;
            public bool HitValid;

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
        private SpatialLocator _spatialLocator;
        private SpatialCoordinateSystem _spatialCoordinateSystem;
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
                Debug.LogWarning($"Extended eye tracking unavailable - {e.Message}");
                Debug.Log("Falling back to standard OpenXR eye tracking");
            }
#else
            Debug.Log("Extended eye tracking only available on device");
            Debug.Log("Using standard OpenXR eye tracking fallback");
            await System.Threading.Tasks.Task.CompletedTask;
#endif
        }

#if ENABLE_WINMD_SUPPORT
        private async void OnTrackerAdded(object sender, EyeGazeTracker tracker)
        {
            try
            {
                Debug.Log("Tracker found, opening with restricted access");
                await tracker.OpenAsync(true);

                _tracker = tracker;
                IsExtendedTrackingAvailable = true;
                AreIndividualEyeGazesSupported = tracker.AreLeftAndRightGazesSupported;
                IsVergenceSupported = tracker.IsVergenceDistanceSupported;

                tracker.SetTargetFrameRate(config != null ? config.TargetFrameRate : 90);

                _spatialLocator = SpatialLocator.GetDefault();
                if (_spatialLocator != null)
                {
                    _spatialCoordinateSystem = _spatialLocator.CreateStationaryFrameOfReferenceAtCurrentLocation()
                        .CoordinateSystem;
                }

                IsTrackingAvailable = true;

                Debug.Log($"Extended tracking ready, per-eye {AreIndividualEyeGazesSupported}, vergence {IsVergenceSupported}");
            }
            catch (Exception e)
            {
                Debug.LogError($"Failed to open tracker - {e.Message}");
            }
        }

        private void OnTrackerRemoved(object sender, EyeGazeTracker tracker)
        {
            if (_tracker == tracker)
            {
                _tracker = null;
                IsExtendedTrackingAvailable = false;
                IsTrackingAvailable = false;
                Debug.LogWarning("Tracker removed");
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
            else
            {
                ReadStandardEyeTracking();
            }
#else
            ReadStandardEyeTracking();
#endif

            if (config != null && config.IncludeHitPoint && _currentFrame.CombinedValid)
            {
                Ray gazeRay = new Ray(_currentFrame.CombinedOrigin, _currentFrame.CombinedDirection);
                if (Physics.Raycast(gazeRay, out RaycastHit hit, config.MaxRaycastDistance, config.RaycastLayerMask))
                {
                    _currentFrame.HitPoint = hit.point;
                    _currentFrame.HitValid = true;
                }
            }

            return _currentFrame;
        }

#if ENABLE_WINMD_SUPPORT
        private void ReadExtendedEyeTracking()
        {
            var timestamp = PerceptionTimestampHelper.FromHistoricalTargetTime(DateTimeOffset.Now);
            var reading = _tracker.TryGetReadingAtTimestamp(timestamp);
            if (reading == null) return;

            var nodeId = _tracker.TrackerSpaceLocatorNodeId;
            var trackerLocator = SpatialGraphInteropPreview.CreateLocatorForNode(nodeId);
            if (trackerLocator == null) return;

            var trackerLocation = trackerLocator.TryLocateAtTimestamp(timestamp, _spatialCoordinateSystem);
            if (trackerLocation == null) return;

            if (reading.TryGetCombinedEyeGazeInTrackerSpace(out System.Numerics.Vector3 origin, out System.Numerics.Vector3 direction))
            {
                _currentFrame.CombinedOrigin = TransformPoint(origin, trackerLocation);
                _currentFrame.CombinedDirection = TransformDirection(direction, trackerLocation);
                _currentFrame.CombinedValid = true;
            }

            if (AreIndividualEyeGazesSupported &&
                reading.TryGetLeftEyeGazeInTrackerSpace(out System.Numerics.Vector3 leftOrigin, out System.Numerics.Vector3 leftDir))
            {
                _currentFrame.LeftEyeOrigin = TransformPoint(leftOrigin, trackerLocation);
                _currentFrame.LeftEyeDirection = TransformDirection(leftDir, trackerLocation);
                _currentFrame.LeftEyeValid = true;
            }

            if (AreIndividualEyeGazesSupported &&
                reading.TryGetRightEyeGazeInTrackerSpace(out System.Numerics.Vector3 rightOrigin, out System.Numerics.Vector3 rightDir))
            {
                _currentFrame.RightEyeOrigin = TransformPoint(rightOrigin, trackerLocation);
                _currentFrame.RightEyeDirection = TransformDirection(rightDir, trackerLocation);
                _currentFrame.RightEyeValid = true;
            }

            if (IsVergenceSupported && reading.TryGetVergenceDistance(out float vergence))
            {
                _currentFrame.VergenceDistance = vergence;
                _currentFrame.VergenceValid = true;
            }
        }

        private Vector3 TransformPoint(System.Numerics.Vector3 point, SpatialLocation location)
        {
            var pos = location.Position;
            var rot = location.Orientation;
            var transformed = System.Numerics.Vector3.Transform(point, rot) +
                              new System.Numerics.Vector3(pos.X, pos.Y, pos.Z);
            return new Vector3(transformed.X, transformed.Y, -transformed.Z);
        }

        private Vector3 TransformDirection(System.Numerics.Vector3 direction, SpatialLocation location)
        {
            var rot = location.Orientation;
            var transformed = System.Numerics.Vector3.Transform(direction, rot);
            return new Vector3(transformed.X, transformed.Y, -transformed.Z);
        }
#endif

        private void ReadStandardEyeTracking()
        {
            var eyes = UnityEngine.InputSystem.InputSystem.GetDevice<UnityEngine.InputSystem.XR.XRHMD>();
            if (eyes != null)
            {
                var camera = Camera.main;
                if (camera != null)
                {
                    _currentFrame.CombinedOrigin = camera.transform.position;
                    _currentFrame.CombinedDirection = camera.transform.forward;
                    _currentFrame.CombinedValid = true;
                }
            }

            var xrInputSubsystem = UnityEngine.XR.InputDevices.GetDeviceAtXRNode(UnityEngine.XR.XRNode.CenterEye);
            if (xrInputSubsystem.isValid)
            {
                if (xrInputSubsystem.TryGetFeatureValue(UnityEngine.XR.CommonUsages.centerEyePosition, out Vector3 pos) &&
                    xrInputSubsystem.TryGetFeatureValue(UnityEngine.XR.CommonUsages.centerEyeRotation, out Quaternion rot))
                {
                    _currentFrame.CombinedOrigin = pos;
                    _currentFrame.CombinedDirection = rot * Vector3.forward;
                    _currentFrame.CombinedValid = true;
                }
            }
        }

        private void OnDestroy()
        {
#if ENABLE_WINMD_SUPPORT
            if (_watcher != null)
            {
                _watcher.Stop();
                _watcher = null;
            }
            _tracker = null;
#endif
        }
    }
}
