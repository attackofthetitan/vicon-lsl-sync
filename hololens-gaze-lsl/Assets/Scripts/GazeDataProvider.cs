using System;
using UnityEngine;

#if ENABLE_WINMD_SUPPORT
using Microsoft.MixedReality.EyeTracking;
using Microsoft.MixedReality.OpenXR;
using Windows.Perception;
#endif

namespace GazeLSL
{
    /*
    Reads eye tracking data from HoloLens 2 Extended Eye Tracking SDK
    with fallback to standard OpenXR / Unity XR eye tracking.

    World-space version:
    - Uses Microsoft.MixedReality.OpenXR.SpatialGraphNode
    - Does NOT use SpatialGraphInteropPreview
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
        private SpatialGraphNode _trackerNode;
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
                Debug.Log("Falling back to standard OpenXR / Unity XR eye tracking");
            }
#else
            Debug.Log("Extended eye tracking only available on HoloLens/UWP device builds");
            Debug.Log("Using standard OpenXR / Unity XR eye tracking fallback");
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

                _trackerNode = SpatialGraphNode.FromDynamicNodeId(tracker.TrackerSpaceLocatorNodeId);

                if (_trackerNode == null)
                {
                    Debug.LogError("Failed to create SpatialGraphNode for eye tracker. World-space extended gaze will not be available.");
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
                _trackerNode = null;
                IsExtendedTrackingAvailable = false;
                IsTrackingAvailable = false;
            }
        }

        private void OnTrackerRemoved(object sender, EyeGazeTracker tracker)
        {
            if (_tracker == tracker)
            {
                _tracker = null;
                _trackerNode = null;

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

                if (Physics.Raycast(
                        gazeRay,
                        out RaycastHit hit,
                        config.MaxRaycastDistance,
                        config.RaycastLayerMask))
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
            if (_tracker == null || _trackerNode == null)
            {
                ReadStandardEyeTracking();
                return;
            }

            var timestamp = PerceptionTimestampHelper.FromHistoricalTargetTime(DateTimeOffset.Now);
            var reading = _tracker.TryGetReadingAtTimestamp(timestamp);

            if (reading == null)
            {
                return;
            }

            if (!_trackerNode.TryLocate(FrameTime.OnUpdate, out Pose trackerPose))
            {
                return;
            }

            if (reading.TryGetCombinedEyeGazeInTrackerSpace(
                    out System.Numerics.Vector3 combinedOrigin,
                    out System.Numerics.Vector3 combinedDirection))
            {
                _currentFrame.CombinedOrigin =
                    TransformTrackerPointToWorld(combinedOrigin, trackerPose);

                _currentFrame.CombinedDirection =
                    TransformTrackerDirectionToWorld(combinedDirection, trackerPose);

                _currentFrame.CombinedValid = true;
            }

            if (AreIndividualEyeGazesSupported &&
                reading.TryGetLeftEyeGazeInTrackerSpace(
                    out System.Numerics.Vector3 leftOrigin,
                    out System.Numerics.Vector3 leftDirection))
            {
                _currentFrame.LeftEyeOrigin =
                    TransformTrackerPointToWorld(leftOrigin, trackerPose);

                _currentFrame.LeftEyeDirection =
                    TransformTrackerDirectionToWorld(leftDirection, trackerPose);

                _currentFrame.LeftEyeValid = true;
            }

            if (AreIndividualEyeGazesSupported &&
                reading.TryGetRightEyeGazeInTrackerSpace(
                    out System.Numerics.Vector3 rightOrigin,
                    out System.Numerics.Vector3 rightDirection))
            {
                _currentFrame.RightEyeOrigin =
                    TransformTrackerPointToWorld(rightOrigin, trackerPose);

                _currentFrame.RightEyeDirection =
                    TransformTrackerDirectionToWorld(rightDirection, trackerPose);

                _currentFrame.RightEyeValid = true;
            }

            if (IsVergenceSupported && reading.TryGetVergenceDistance(out float vergence))
            {
                _currentFrame.VergenceDistance = vergence;
                _currentFrame.VergenceValid = true;
            }
        }

        private Vector3 TransformTrackerPointToWorld(System.Numerics.Vector3 point, Pose trackerPose)
        {
            Vector3 unityPoint = new Vector3(point.X, point.Y, -point.Z);
            return trackerPose.position + trackerPose.rotation * unityPoint;
        }

        private Vector3 TransformTrackerDirectionToWorld(System.Numerics.Vector3 direction, Pose trackerPose)
        {
            Vector3 unityDirection = new Vector3(direction.X, direction.Y, -direction.Z).normalized;
            return (trackerPose.rotation * unityDirection).normalized;
        }
#endif

        private void ReadStandardEyeTracking()
        {
            Camera camera = Camera.main;

            if (camera != null)
            {
                _currentFrame.CombinedOrigin = camera.transform.position;
                _currentFrame.CombinedDirection = camera.transform.forward;
                _currentFrame.CombinedValid = true;
            }

            var centerEyeDevice = UnityEngine.XR.InputDevices.GetDeviceAtXRNode(UnityEngine.XR.XRNode.CenterEye);

            if (centerEyeDevice.isValid)
            {
                bool hasPosition = centerEyeDevice.TryGetFeatureValue(
                    UnityEngine.XR.CommonUsages.centerEyePosition,
                    out Vector3 position
                );

                bool hasRotation = centerEyeDevice.TryGetFeatureValue(
                    UnityEngine.XR.CommonUsages.centerEyeRotation,
                    out Quaternion rotation
                );

                if (hasPosition && hasRotation)
                {
                    _currentFrame.CombinedOrigin = position;
                    _currentFrame.CombinedDirection = rotation * Vector3.forward;
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
                _watcher.EyeGazeTrackerAdded -= OnTrackerAdded;
                _watcher.EyeGazeTrackerRemoved -= OnTrackerRemoved;
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
            _trackerNode = null;
#endif
        }
    }
}