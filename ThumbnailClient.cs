using System.Net.Sockets;

namespace RemoteViewer;

internal static class ThumbnailClient
{
    private const uint HostPrefix = 0x48535433;
    private const uint ViewerPrefix = 0x56575233;

    public static async Task<byte[]?> FetchAsync(string host, int basePort, string password, CancellationToken ct)
    {
        if (string.IsNullOrWhiteSpace(host) || string.IsNullOrEmpty(password)) return null;
        if (basePort < 1024 || basePort > 65531) return null;
        using var client = new TcpClient
        {
            NoDelay = true,
            SendBufferSize = 512 * 1024,
            ReceiveBufferSize = 2 * 1024 * 1024
        };

        await client.ConnectAsync(host, basePort + 2, ct).ConfigureAwait(false);
        NetworkStream stream = client.GetStream();
        byte[] key = await RemoteClient.AuthenticateClientAsync(stream, password, ct).ConfigureAwait(false);
        using var channel = new SecureChannel(stream, key, ViewerPrefix, HostPrefix);
        using SecurePacket packet = await channel.ReceiveAsync(ct).ConfigureAwait(false);
        if (packet.Type != PacketType.Preview || packet.PayloadLength <= 8) return null;

        int jpegLength = packet.PayloadLength - 8;
        byte[] jpeg = new byte[jpegLength];
        Buffer.BlockCopy(packet.Buffer, packet.PayloadOffset + 8, jpeg, 0, jpegLength);
        return jpeg;
    }
}
