using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;

namespace RemoteViewer;

internal static class EndpointPriority
{
    // IMPORTANT: EndpointPriority.Score/Kind are called from WinForms UI code very often:
    // presence timer, every discovery beacon and card/status refresh. Enumerating Windows
    // network adapters there used to block the UI message loop and made all forms pause
    // during continuous dragging. Keep an immutable subnet snapshot and refresh it only
    // on a ThreadPool thread.
    private readonly record struct PhysicalSubnet(uint Network, uint Mask);

    private static PhysicalSubnet[] _physicalSubnets = Array.Empty<PhysicalSubnet>();
    private static int _snapshotReady;
    private static int _refreshing;
    private static readonly System.Threading.Timer _refreshTimer;

    static EndpointPriority()
    {
        // Prime without blocking the UI. Network topology changes are rare, but keeping the
        // snapshot fresh preserves automatic LAN/VPN route switching without touching the UI thread.
        QueueRefresh();
        _refreshTimer = new System.Threading.Timer(
            _ => QueueRefresh(),
            null,
            TimeSpan.FromSeconds(30),
            TimeSpan.FromSeconds(30));
        NetworkChange.NetworkAddressChanged += (_, _) => QueueRefresh();
    }

    public static void WarmUp() => QueueRefresh();

    public static int Score(string address)
    {
        if (!IPAddress.TryParse(address, out IPAddress? ip) || ip.AddressFamily != AddressFamily.InterNetwork)
            return 0;

        if (IsOnPhysicalLocalSubnet(ip)) return 1000;
        byte[] b = ip.GetAddressBytes();
        if (b[0] == 26) return 700; // Radmin VPN: ниже прямого LAN, но выше чужих виртуальных private-сетей.
        if (b[0] == 100 && b[1] >= 64 && b[1] <= 127) return 650; // Tailscale / CGNAT

        // While the first background snapshot is still being built, prefer a normal RFC1918 LAN
        // over Radmin so startup routing does not briefly flip to VPN. After the snapshot is ready,
        // private addresses not present on a physical subnet retain the original lower priority.
        if (IsPrivateLan(b)) return Volatile.Read(ref _snapshotReady) == 0 ? 800 : 500;
        if (b[0] == 169 && b[1] == 254) return 300;
        return 200;
    }

    public static string Kind(string address)
    {
        if (!IPAddress.TryParse(address, out IPAddress? ip) || ip.AddressFamily != AddressFamily.InterNetwork)
            return "Сеть";
        if (IsOnPhysicalLocalSubnet(ip)) return "LAN";
        byte[] b = ip.GetAddressBytes();
        if (b[0] == 26) return "Radmin VPN";
        if (b[0] == 100 && b[1] >= 64 && b[1] <= 127) return "VPN";
        if (IsPrivateLan(b)) return "LAN";
        return "Сеть";
    }

    private static bool IsPrivateLan(byte[] b) =>
        b[0] == 10 ||
        (b[0] == 172 && b[1] >= 16 && b[1] <= 31) ||
        (b[0] == 192 && b[1] == 168);

    private static bool IsOnPhysicalLocalSubnet(IPAddress remote)
    {
        uint remoteValue = ToUInt32(remote.GetAddressBytes());
        PhysicalSubnet[] snapshot = Volatile.Read(ref _physicalSubnets);
        foreach (PhysicalSubnet subnet in snapshot)
        {
            if ((remoteValue & subnet.Mask) == subnet.Network)
                return true;
        }
        return false;
    }

    private static void QueueRefresh()
    {
        if (Interlocked.CompareExchange(ref _refreshing, 1, 0) != 0)
            return;

        ThreadPool.QueueUserWorkItem(_ =>
        {
            try
            {
                PhysicalSubnet[] snapshot = BuildPhysicalSubnetSnapshot();
                Volatile.Write(ref _physicalSubnets, snapshot);
                Volatile.Write(ref _snapshotReady, 1);
            }
            catch
            {
                // Keep the last valid snapshot. Route classification must never break discovery.
            }
            finally
            {
                Volatile.Write(ref _refreshing, 0);
            }
        });
    }

    private static PhysicalSubnet[] BuildPhysicalSubnetSnapshot()
    {
        var result = new HashSet<PhysicalSubnet>();
        foreach (NetworkInterface nic in NetworkInterface.GetAllNetworkInterfaces())
        {
            if (nic.OperationalStatus != OperationalStatus.Up || nic.NetworkInterfaceType == NetworkInterfaceType.Loopback)
                continue;
            if (IsLikelyVirtual(nic)) continue;

            foreach (UnicastIPAddressInformation u in nic.GetIPProperties().UnicastAddresses)
            {
                if (u.Address.AddressFamily != AddressFamily.InterNetwork || u.IPv4Mask == null)
                    continue;

                byte[] address = u.Address.GetAddressBytes();
                byte[] maskBytes = u.IPv4Mask.GetAddressBytes();
                if (address.Length != 4 || maskBytes.Length != 4)
                    continue;

                uint mask = ToUInt32(maskBytes);
                uint network = ToUInt32(address) & mask;
                result.Add(new PhysicalSubnet(network, mask));
            }
        }
        return result.ToArray();
    }

    private static uint ToUInt32(byte[] b) =>
        ((uint)b[0] << 24) | ((uint)b[1] << 16) | ((uint)b[2] << 8) | b[3];

    private static bool IsLikelyVirtual(NetworkInterface nic)
    {
        string text = (nic.Name + " " + nic.Description).ToLowerInvariant();
        string[] markers =
        {
            "radmin", "vpn", "tailscale", "zerotier", "wireguard", "hamachi",
            "hyper-v", "vethernet", "vmware", "virtualbox", "docker", "wsl", "virtual", "tap", "tun"
        };
        return markers.Any(text.Contains) || nic.NetworkInterfaceType == NetworkInterfaceType.Tunnel;
    }
}
