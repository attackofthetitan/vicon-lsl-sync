using UnityEngine;
using System.Globalization;
#if ENABLE_WINMD_SUPPORT
using Windows.Networking;
using Windows.Networking.Sockets;
using Windows.Storage.Streams;
#else
using System.Net;
using System.Net.Sockets;
#endif
using System.Text;

namespace GazeLSL
{
    public class GazeLSLOutlet : MonoBehaviour
    {
        [SerializeField] private GazeLSLConfig config;
        [SerializeField] private GazeDataProvider gazeProvider;

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

        private const int ChannelCount = 26;

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

            Debug.Log($"Gaze UDP sender ready - {config.RelayHost}:{config.RelayPort}, {ChannelCount} channels");
        }

        private void LateUpdate()
        {
#if ENABLE_WINMD_SUPPORT
            if (udpSocket == null || gazeProvider == null) return;
#else
            if (udpClient == null || gazeProvider == null) return;
#endif

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

            SendSample(Time.realtimeSinceStartupAsDouble, sample);
        }

        private void SendSample(double timestamp, double[] values)
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

            Debug.Log("Gaze UDP sender closed");
        }
    }
}
