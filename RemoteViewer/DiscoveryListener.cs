using System.Net;
using System.Net.Sockets;
using System.Text;

namespace RemoteViewer;

internal sealed class DiscoveredHost
{
    public string HostId { get; init; } = string.Empty;
    public string Name { get; init; } = string.Empty;
    public string Address { get; init; } = string.Empty;
    public int Port { get; init; }
    public DateTime LastSeenUtc { get; set; } = DateTime.UtcNow;
}

internal sealed class DiscoveryListener : IDisposable
{
    public const int DiscoveryPort = 45901;
    private UdpClient? _udp;
    private CancellationTokenSource? _cts;

    public event Action<DiscoveredHost>? HostSeen;

    public void Start()
    {
        if (_udp != null) return;
        _cts = new CancellationTokenSource();
        _udp = new UdpClient();
        _udp.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
        _udp.Client.Bind(new IPEndPoint(IPAddress.Any, DiscoveryPort));
        _ = LoopAsync(_cts.Token);
    }

    private async Task LoopAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested && _udp != null)
        {
            try
            {
                UdpReceiveResult result = await _udp.ReceiveAsync(ct);
                string text = Encoding.UTF8.GetString(result.Buffer);
                string[] parts = text.Split('|');
                if (parts.Length != 4 || parts[0] != "SRD3" || !int.TryParse(parts[3], out int port))
                    continue;

                HostSeen?.Invoke(new DiscoveredHost
                {
                    HostId = parts[1],
                    Name = parts[2],
                    Address = result.RemoteEndPoint.Address.ToString(),
                    Port = port,
                    LastSeenUtc = DateTime.UtcNow
                });
            }
            catch (OperationCanceledException) { break; }
            catch (ObjectDisposedException) { break; }
            catch
            {
                try { await Task.Delay(500, ct); } catch { break; }
            }
        }
    }

    public void Dispose()
    {
        try { _cts?.Cancel(); } catch { }
        try { _udp?.Close(); } catch { }
        _udp?.Dispose();
        _udp = null;
        _cts?.Dispose();
        _cts = null;
    }
}
