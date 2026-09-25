using System.Collections.Specialized;
using System.Net.Sockets;
using System.Text.Json;

namespace RemoteViewer;

internal static class FileClipboardTransfer
{
    private const uint HostPrefix = 0x48535433;
    private const uint ViewerPrefix = 0x56575233;

    public sealed class TransferEntry
    {
        public string RelativePath { get; set; } = string.Empty;
        public bool IsDirectory { get; set; }
        public long Length { get; set; }
    }

    public sealed class TransferManifest
    {
        public List<TransferEntry> Entries { get; set; } = new();
        public List<string> Roots { get; set; } = new();
    }

    public static string[] GetLocalClipboardFiles()
    {
        if (!Clipboard.ContainsFileDropList()) return Array.Empty<string>();
        return Clipboard.GetFileDropList().Cast<string>()
            .Where(p => File.Exists(p) || Directory.Exists(p)).Take(64).ToArray();
    }

    public static async Task<int> DownloadRemoteClipboardAsync(string host, int port, string password, CancellationToken ct)
    {
        using TcpClient tcp = CreateTcp();
        await tcp.ConnectAsync(host, port + 4, ct).ConfigureAwait(false);
        using NetworkStream stream = tcp.GetStream();
        byte[] key = await RemoteClient.AuthenticateClientAsync(stream, password, ct).ConfigureAwait(false);
        using var channel = new SecureChannel(stream, key, ViewerPrefix, HostPrefix);
        await channel.SendAsync(PacketType.FileClipboardGet, ReadOnlyMemory<byte>.Empty, ct).ConfigureAwait(false);

        using SecurePacket manifestPacket = await channel.ReceiveAsync(ct).ConfigureAwait(false);
        if (manifestPacket.Type != PacketType.FileManifest) throw new InvalidDataException("File manifest expected.");
        var manifest = JsonSerializer.Deserialize<TransferManifest>(manifestPacket.PayloadSpan) ?? new TransferManifest();
        if (manifest.Entries.Count == 0) return 0;
        if (manifest.Entries.Count > 10000) throw new InvalidDataException("Слишком много файлов.");

        string root = Path.Combine(Path.GetTempPath(), "SimpleRemoteDesk", "Clipboard", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        foreach (TransferEntry entry in manifest.Entries.Where(e => e.IsDirectory))
            Directory.CreateDirectory(SafeDestination(root, entry.RelativePath));

        FileStream? current = null;
        int currentIndex = -1;
        try
        {
            while (true)
            {
                using SecurePacket packet = await channel.ReceiveAsync(ct).ConfigureAwait(false);
                if (packet.Type == PacketType.FileTransferEnd) break;
                if (packet.Type != PacketType.FileChunk || packet.PayloadLength < 4) continue;
                int index = BitConverter.ToInt32(packet.Buffer, packet.PayloadOffset);
                if (index < 0 || index >= manifest.Entries.Count) throw new InvalidDataException("Invalid file index.");
                TransferEntry entry = manifest.Entries[index];
                if (entry.IsDirectory) continue;
                if (currentIndex != index)
                {
                    if (current != null) await current.DisposeAsync().ConfigureAwait(false);
                    current = null;
                    currentIndex = index;
                    string dest = SafeDestination(root, entry.RelativePath);
                    Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
                    current = new FileStream(dest, FileMode.Create, FileAccess.Write, FileShare.None, 1024 * 1024, true);
                }
                await current.WriteAsync(packet.Buffer.AsMemory(packet.PayloadOffset + 4, packet.PayloadLength - 4), ct).ConfigureAwait(false);
            }
        }
        finally
        {
            if (current != null) await current.DisposeAsync().ConfigureAwait(false);
        }

        var files = new StringCollection();
        foreach (string rel in manifest.Roots.Distinct(StringComparer.OrdinalIgnoreCase))
        {
            string path = SafeDestination(root, rel);
            if (File.Exists(path) || Directory.Exists(path)) files.Add(path);
        }
        await SetClipboardFilesAsync(files).ConfigureAwait(false);
        return files.Count;
    }

    public static async Task<int> UploadLocalClipboardAsync(string host, int port, string password, string[] sourceRoots, CancellationToken ct)
    {
        var manifest = BuildManifest(sourceRoots, out Dictionary<string, string> sourceMap);
        if (manifest.Entries.Count == 0) return 0;

        using TcpClient tcp = CreateTcp();
        await tcp.ConnectAsync(host, port + 4, ct).ConfigureAwait(false);
        using NetworkStream stream = tcp.GetStream();
        byte[] key = await RemoteClient.AuthenticateClientAsync(stream, password, ct).ConfigureAwait(false);
        using var channel = new SecureChannel(stream, key, ViewerPrefix, HostPrefix);
        await channel.SendAsync(PacketType.FileClipboardPut, ReadOnlyMemory<byte>.Empty, ct).ConfigureAwait(false);
        await channel.SendAsync(PacketType.FileManifest, JsonSerializer.SerializeToUtf8Bytes(manifest), ct).ConfigureAwait(false);

        const int chunkSize = 1024 * 1024;
        byte[] chunk = new byte[chunkSize + 4];
        for (int index = 0; index < manifest.Entries.Count; index++)
        {
            TransferEntry entry = manifest.Entries[index];
            if (entry.IsDirectory) continue;
            if (!sourceMap.TryGetValue(entry.RelativePath, out string? source) || !File.Exists(source)) continue;
            await using FileStream fs = new(source, FileMode.Open, FileAccess.Read, FileShare.ReadWrite, chunkSize, true);
            while (true)
            {
                int read = await fs.ReadAsync(chunk.AsMemory(4, chunkSize), ct).ConfigureAwait(false);
                if (read <= 0) break;
                BitConverter.GetBytes(index).CopyTo(chunk, 0);
                await channel.SendAsync(PacketType.FileChunk, chunk.AsMemory(0, read + 4), ct).ConfigureAwait(false);
            }
        }
        await channel.SendAsync(PacketType.FileTransferEnd, ReadOnlyMemory<byte>.Empty, ct).ConfigureAwait(false);
        using SecurePacket ack = await channel.ReceiveAsync(ct).ConfigureAwait(false);
        if (ack.Type != PacketType.FileTransferAck) throw new IOException("Host did not acknowledge file transfer.");
        return manifest.Roots.Count;
    }

    private static TransferManifest BuildManifest(string[] roots, out Dictionary<string, string> sourceMap)
    {
        var manifest = new TransferManifest();
        sourceMap = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var used = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string source in roots.Where(p => File.Exists(p) || Directory.Exists(p)).Take(64))
        {
            string baseName = Path.GetFileName(source.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
            if (string.IsNullOrWhiteSpace(baseName)) baseName = "item";
            string rootName = baseName;
            int suffix = 2;
            while (!used.Add(rootName)) rootName = $"{baseName} ({suffix++})";
            manifest.Roots.Add(rootName);
            if (File.Exists(source))
            {
                var entry = new TransferEntry { RelativePath = rootName, Length = new FileInfo(source).Length };
                manifest.Entries.Add(entry); sourceMap[entry.RelativePath] = source;
            }
            else
            {
                manifest.Entries.Add(new TransferEntry { RelativePath = rootName, IsDirectory = true });
                foreach (string dir in Directory.EnumerateDirectories(source, "*", SearchOption.AllDirectories))
                    manifest.Entries.Add(new TransferEntry { RelativePath = Path.Combine(rootName, Path.GetRelativePath(source, dir)), IsDirectory = true });
                foreach (string file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
                {
                    var entry = new TransferEntry { RelativePath = Path.Combine(rootName, Path.GetRelativePath(source, file)), Length = new FileInfo(file).Length };
                    manifest.Entries.Add(entry); sourceMap[entry.RelativePath] = file;
                }
            }
            if (manifest.Entries.Count > 10000) throw new InvalidOperationException("Слишком много файлов для буфера обмена.");
        }
        return manifest;
    }

    private static string SafeDestination(string root, string relative)
    {
        string fullRoot = Path.GetFullPath(root) + Path.DirectorySeparatorChar;
        string full = Path.GetFullPath(Path.Combine(root, relative));
        if (!full.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Недопустимый путь файла.");
        return full;
    }

    private static Task SetClipboardFilesAsync(StringCollection files)
    {
        var tcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        var thread = new Thread(() =>
        {
            try { Clipboard.SetFileDropList(files); tcs.SetResult(true); }
            catch (Exception ex) { tcs.SetException(ex); }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.IsBackground = true;
        thread.Start();
        return tcs.Task;
    }

    private static TcpClient CreateTcp() => new()
    {
        NoDelay = true,
        SendBufferSize = 2 * 1024 * 1024,
        ReceiveBufferSize = 2 * 1024 * 1024
    };
}
