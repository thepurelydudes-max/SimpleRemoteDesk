using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;

namespace RemoteHost;

internal sealed class DiscoveryBeacon : IDisposable
{
    public const int DiscoveryPort = 45901;
    private CancellationTokenSource? _cts;
    private UdpClient? _udp;

    public void Start(string hostId, int servicePort)
    {
        Stop();
        _cts = new CancellationTokenSource();
        _udp = new UdpClient { EnableBroadcast = true };
        _ = LoopAsync(hostId, servicePort, _cts.Token);
    }

    private async Task LoopAsync(string hostId, int servicePort, CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try
            {
                string payload = $"SRD3|{hostId}|{Environment.MachineName}|{servicePort}";
                byte[] data = Encoding.UTF8.GetBytes(payload);
                if (_udp != null)
                {
                    foreach (IPEndPoint target in GetBroadcastTargets())
                    {
                        try { await _udp.SendAsync(data, data.Length, target); }
                        catch { }
                    }
                }
            }
            catch { }

            try { await Task.Delay(2000, ct); }
            catch (OperationCanceledException) { break; }
        }
    }

    private static IReadOnlyList<IPEndPoint> GetBroadcastTargets()
    {
        var addresses = new HashSet<IPAddress>();
        try
        {
            foreach (NetworkInterface nic in NetworkInterface.GetAllNetworkInterfaces())
            {
                if (nic.OperationalStatus != OperationalStatus.Up || nic.NetworkInterfaceType == NetworkInterfaceType.Loopback)
                    continue;

                string nicText = (nic.Name + " " + nic.Description).ToLowerInvariant();
                bool isRadmin = nicText.Contains("radmin");
                bool isOtherVirtual = IsVirtualNonRadmin(nicText, nic.NetworkInterfaceType);
                if (isOtherVirtual && !isRadmin)
                    continue;

                foreach (UnicastIPAddressInformation unicast in nic.GetIPProperties().UnicastAddresses)
                {
                    if (unicast.Address.AddressFamily != AddressFamily.InterNetwork || unicast.IPv4Mask == null)
                        continue;

                    byte[] ip = unicast.Address.GetAddressBytes();
                    byte[] mask = unicast.IPv4Mask.GetAddressBytes();
                    if (ip.Length != 4 || mask.Length != 4) continue;

                    byte[] broadcast = new byte[4];
                    for (int i = 0; i < 4; i++)
                        broadcast[i] = (byte)(ip[i] | (~mask[i] & 0xFF));

                    addresses.Add(new IPAddress(broadcast));
                }
            }
        }
        catch { }

        return addresses.Select(a => new IPEndPoint(a, DiscoveryPort)).ToArray();
    }

    private static bool IsVirtualNonRadmin(string nicText, NetworkInterfaceType type)
    {
        if (type == NetworkInterfaceType.Tunnel) return true;
        string[] markers =
        {
            "hyper-v", "vethernet", "virtual", "vmware", "virtualbox", "docker", "wsl",
            "tailscale", "zerotier", "wireguard", "hamachi", "tap", "tun", "vpn"
        };
        return markers.Any(nicText.Contains);
    }

    public void Stop()
    {
        try { _cts?.Cancel(); } catch { }
        try { _udp?.Close(); } catch { }
        _udp?.Dispose();
        _udp = null;
        _cts?.Dispose();
        _cts = null;
    }

    public void Dispose() => Stop();
}
