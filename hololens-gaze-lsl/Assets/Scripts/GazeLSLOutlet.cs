using UnityEngine;
using LSL;
using System;
using System.Globalization;
using System.Text;
using System.Threading.Tasks;
#if ENABLE_WINMD_SUPPORT
using Windows.Networking;
using Windows.Networking.Sockets;
using Windows.Storage.Streams;
#else
using System.Net;
using System.Net.Sockets;
#endif

namespace GazeLSL
{
    public class GazeLSLOutlet : MonoBehaviour
    {
        [SerializeField] private GazeLSLConfig config;
        [SerializeField] private GazeDataProvider gazeProvider;

        private enum TransportMode
        {
            None,
            Lsl,
            Udp
        }

        private TransportMode transportMode = TransportMode.None;
        private liblsl.StreamOutlet outlet;
        private liblsl.StreamInfo info;

#if ENABLE_WINMD_SUPPORT
        private DatagramSocket udpSocket;
        private HostName relayHost;
        private string relayPort;
#else
        private UdpClient udpClient;
        private IPEndPoint relayEndpoint;
#endif

        private double[] sample;
        private readonly StringBuilder packetBuilder = new StringBuilder(512);

        private bool samplingLoopRunning;
        private double nextSampleTime;

        private const int ChannelCount = 26;

        private static readonly string[] ChannelLabels =
        {
            "CombinedOriginX", "CombinedOriginY", "CombinedOriginZ",
            "CombinedDirectionX", "CombinedDirectionY", "CombinedDirectionZ", "CombinedValid",
            "LeftEyeOriginX", "LeftEyeOriginY", "LeftEyeOriginZ",
            "LeftEyeDirectionX", "LeftEyeDirectionY", "LeftEyeDirectionZ", "LeftEyeValid",
            "RightEyeOriginX", "RightEyeOriginY", "RightEyeOriginZ",
            "RightEyeDirectionX", "RightEyeDirectionY", "RightEyeDirectionZ", "RightEyeValid",
            "HitPointX", "HitPointY", "HitPointZ", "HitValid",
            "VergenceDistance"
        };

        private static readonly string[] ChannelUnits =
        {
            "meters", "meters", "meters",
            "normalized", "normalized", "normalized", "bool",
            "meters", "meters", "meters",
            "normalized", "normalized", "normalized", "bool",
            "meters", "meters", "meters",
            "normalized", "normalized", "normalized", "bool",
            "meters", "meters", "meters", "bool",
            "meters"
        };

        private void Start()
        {
            if (gazeProvider == null)
            {
                gazeProvider = GetComponent<GazeDataProvider>();
            }

            if (config == null)
            {
                Debug.LogError("GazeLSLOutlet - config not assigned");
                enabled = false;
                return;
            }

            sample = new double[ChannelCount];

            if (TryInitializeLsl())
            {
                transportMode = TransportMode.Lsl;
                Debug.Log($"LSL outlet created - {config.StreamName}, {ChannelCount} channels");
            }
            else
            {
                InitializeUdp();
                transportMode = TransportMode.Udp;
                Debug.Log($"Gaze UDP fallback ready - {config.RelayHost}:{config.RelayPort}, {ChannelCount} channels");
            }

            StartSamplingLoop();
        }

        private bool TryInitializeLsl()
        {
            try
            {
                info = new liblsl.StreamInfo(
                    config.StreamName,
                    config.StreamType,
                    ChannelCount,
                    config.TargetFrameRate,
                    liblsl.channel_format_t.cf_double64,
                    config.SourceId
                );

                liblsl.XMLElement channels = info.desc().append_child("channels");

                for (int i = 0; i < ChannelLabels.Length; i++)
                {
                    liblsl.XMLElement ch = channels.append_child("channel");
                    ch.append_child_value("label", ChannelLabels[i]);
                    ch.append_child_value("unit", ChannelUnits[i]);
                }

                liblsl.XMLElement meta = info.desc().append_child("acquisition");
                meta.append_child_value("device", "HoloLens2");
                meta.append_child_value("sdk", "ExtendedEyeTracking");
                meta.append_child_value("nominal_srate", config.TargetFrameRate.ToString());

                outlet = new liblsl.StreamOutlet(info);
                liblsl.local_clock();
                return true;
            }
            catch (Exception e) when (IsLslLoadFailure(e))
            {
                Debug.LogWarning($"LSL unavailable, falling back to UDP - {e.GetType().Name}: {e.Message}");
                outlet = null;
                info = null;
                return false;
            }
        }

        private void InitializeUdp()
        {
#if ENABLE_WINMD_SUPPORT
            udpSocket = new DatagramSocket();
            relayHost = new HostName(config.RelayHost);
            relayPort = config.RelayPort.ToString(CultureInfo.InvariantCulture);
#else
            udpClient = new UdpClient();
            relayEndpoint = new IPEndPoint(
                Dns.GetHostAddresses(config.RelayHost)[0],
                config.RelayPort
            );
#endif
        }

        private async void StartSamplingLoop()
        {
            samplingLoopRunning = true;

            double interval = 1.0 / Math.Max(1, config.TargetFrameRate);
            nextSampleTime = Time.realtimeSinceStartupAsDouble;

            while (samplingLoopRunning)
            {
                double now = Time.realtimeSinceStartupAsDouble;

                if (now >= nextSampleTime)
                {
                    PushOneSample();
                    nextSampleTime += interval;

                    if (now - nextSampleTime > interval)
                    {
                        nextSampleTime = now + interval;
                    }
                }

                await Task.Yield();
            }
        }

        private void PushOneSample()
        {
            if (transportMode == TransportMode.None || gazeProvider == null)
            {
                return;
            }

            var frame = gazeProvider.GetCurrentFrame();

            sample[0] = frame.CombinedOrigin.x;
            sample[1] = frame.CombinedOrigin.y;
            sample[2] = frame.CombinedOrigin.z;
            sample[3] = frame.CombinedDirection.x;
            sample[4] = frame.CombinedDirection.y;
            sample[5] = frame.CombinedDirection.z;
            sample[6] = frame.CombinedValid ? 1.0 : 0.0;

            sample[7] = frame.LeftEyeOrigin.x;
            sample[8] = frame.LeftEyeOrigin.y;
            sample[9] = frame.LeftEyeOrigin.z;
            sample[10] = frame.LeftEyeDirection.x;
            sample[11] = frame.LeftEyeDirection.y;
            sample[12] = frame.LeftEyeDirection.z;
            sample[13] = frame.LeftEyeValid ? 1.0 : 0.0;

            sample[14] = frame.RightEyeOrigin.x;
            sample[15] = frame.RightEyeOrigin.y;
            sample[16] = frame.RightEyeOrigin.z;
            sample[17] = frame.RightEyeDirection.x;
            sample[18] = frame.RightEyeDirection.y;
            sample[19] = frame.RightEyeDirection.z;
            sample[20] = frame.RightEyeValid ? 1.0 : 0.0;

            sample[21] = frame.HitPoint.x;
            sample[22] = frame.HitPoint.y;
            sample[23] = frame.HitPoint.z;
            sample[24] = frame.HitValid ? 1.0 : 0.0;

            sample[25] = frame.VergenceValid ? frame.VergenceDistance : double.NaN;

            if (transportMode == TransportMode.Lsl)
            {
                try
                {
                    outlet.push_sample(sample, liblsl.local_clock());
                }
                catch (Exception e) when (IsLslLoadFailure(e))
                {
                    Debug.LogWarning($"LSL push failed, switching to UDP - {e.GetType().Name}: {e.Message}");
                    outlet = null;
                    info = null;
                    InitializeUdp();
                    transportMode = TransportMode.Udp;
                    SendUdpSample(Time.realtimeSinceStartupAsDouble, sample);
                }
            }
            else
            {
                SendUdpSample(Time.realtimeSinceStartupAsDouble, sample);
            }
        }

        private static bool IsLslLoadFailure(Exception e)
        {
            return e is DllNotFoundException ||
                   e is EntryPointNotFoundException ||
                   e is BadImageFormatException ||
                   e is TypeInitializationException ||
                   e is TypeLoadException ||
                   e is InvalidOperationException;
        }

        private void SendUdpSample(double timestamp, double[] values)
        {
            packetBuilder.Clear();
            packetBuilder.Append("HLGAZE1,");
            packetBuilder.Append(timestamp.ToString("R", CultureInfo.InvariantCulture));

            for (int i = 0; i < values.Length; i++)
            {
                packetBuilder.Append(',');
                packetBuilder.Append(values[i].ToString("R", CultureInfo.InvariantCulture));
            }

            string packet = packetBuilder.ToString();

#if ENABLE_WINMD_SUPPORT
            SendUwpPacket(packet);
#else
            byte[] bytes = Encoding.ASCII.GetBytes(packet);
            udpClient.Send(bytes, bytes.Length, relayEndpoint);
#endif
        }

#if ENABLE_WINMD_SUPPORT
        private async void SendUwpPacket(string packet)
        {
            try
            {
                using (IOutputStream stream = await udpSocket.GetOutputStreamAsync(relayHost, relayPort))
                using (DataWriter writer = new DataWriter(stream))
                {
                    writer.WriteString(packet);
                    await writer.StoreAsync();
                    await writer.FlushAsync();
                }
            }
            catch (System.Exception e)
            {
                Debug.LogWarning($"Failed to send gaze UDP packet - {e.Message}");
            }
        }
#endif

        private void OnDestroy()
        {
            samplingLoopRunning = false;

            outlet = null;
            info = null;

#if ENABLE_WINMD_SUPPORT
            if (udpSocket != null)
            {
                udpSocket.Dispose();
                udpSocket = null;
            }
#else
            if (udpClient != null)
            {
                udpClient.Close();
                udpClient = null;
            }
#endif

            transportMode = TransportMode.None;
        }
    }
}
