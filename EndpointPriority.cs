using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;

namespace RemoteViewer;

internal static class EndpointPriority
{
    public static int Score(string address)
    {
        if (!IPAddress.TryParse(address, out IPAddress? ip) || ip.AddressFamily != AddressFamily.InterNetwork)
            return 0;

        if (IsOnPhysicalLocalSubnet(ip)) return 1000;
        byte[] b = ip.GetAddressBytes();
        if (b[0] == 26) return 700; // Radmin VPN: ниже прямого LAN, но выше чужих виртуальных private-сетей.
        if (b[0] == 100 && b[1] >= 64 && b[1] <= 127) return 650; // Tailscale / CGNAT
        if (IsPrivateLan(b)) return 500;
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
        try
        {
            byte[] rb = remote.GetAddressBytes();
            foreach (NetworkInterface nic in NetworkInterface.GetAllNetworkInterfaces())
            {
                if (nic.OperationalStatus != OperationalStatus.Up || nic.NetworkInterfaceType == NetworkInterfaceType.Loopback)
                    continue;
                if (IsLikelyVirtual(nic)) continue;

                foreach (UnicastIPAddressInformation u in nic.GetIPProperties().UnicastAddresses)
                {
                    if (u.Address.AddressFamily != AddressFamily.InterNetwork || u.IPv4Mask == null) continue;
                    byte[] lb = u.Address.GetAddressBytes();
                    byte[] mask = u.IPv4Mask.GetAddressBytes();
                    bool same = true;
                    for (int i = 0; i < 4; i++)
                    {
                        if ((lb[i] & mask[i]) != (rb[i] & mask[i])) { same = false; break; }
                    }
                    if (same) return true;
                }
            }
        }
        catch { }
        return false;
    }

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
