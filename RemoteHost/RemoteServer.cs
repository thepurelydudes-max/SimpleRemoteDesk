using System.Buffers.Binary;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace RemoteHost;

internal static class PacketType
{
    public const byte Screen = 1;
    public const byte MouseMove = 2;
    public const byte MouseButton = 3;
    public const byte MouseWheel = 4;
    public const byte Key = 5;
    public const byte AudioFormat = 6;
    public const byte Audio = 7;
    public const byte KeyCombination = 8;
    public const byte Preview = 9;
    public const byte Cursor = 10;
    public const byte FileClipboardGet = 20;
    public const byte FileManifest = 21;
    public const byte FileChunk = 22;
    public const byte FileTransferEnd = 23;
    public const byte FileClipboardPut = 24;
    public const byte FileTransferAck = 25;
}

internal sealed class RemoteServer : IDisposable
{
    private const uint HostPrefix = 0x48535433;   // HST3
    private const uint ViewerPrefix = 0x56575233; // VWR3
    private readonly DiscoveryBeacon _beacon = new();
    private readonly object _sessionSync = new();

    private TcpListener? _dataListener;
    private TcpListener? _controlListener;
    private TcpListener? _previewListener;
    private TcpListener? _audioListener;
    private TcpListener? _fileListener;
    private CancellationTokenSource? _serverCts;
    private CancellationTokenSource? _sessionCts;
    private TcpClient? _dataClient;
    private TcpClient? _controlClient;
    private TcpClient? _audioClient;
    private string? _sessionRemoteIp;

    public int Port { get; set; } = 45900;
    public string Password { get; set; } = "change-me";
    public int Fps { get; set; } = 30;
    public long JpegQuality { get; set; } = 95;
    public bool AudioEnabled { get; set; } = true;
    public string AudioDeviceId { get; set; } = string.Empty;
    public string HostId { get; set; } = string.Empty;

    public bool IsRunning => _dataListener != null;
    public event Action<string>? StatusChanged;
    public event Action<string?>? ClientChanged;

    public Task StartAsync()
    {
        if (_dataListener != null) return Task.CompletedTask;
        if (Port < 1024 || Port > 65531)
            throw new ArgumentOutOfRangeException(nameof(Port), "Базовый порт должен быть 1024–65531.");

        _serverCts = new CancellationTokenSource();
        _dataListener = new TcpListener(IPAddress.Any, Port);
        _controlListener = new TcpListener(IPAddress.Any, Port + 1);
        _previewListener = new TcpListener(IPAddress.Any, Port + 2);
        _audioListener = new TcpListener(IPAddress.Any, Port + 3);
        _fileListener = new TcpListener(IPAddress.Any, Port + 4);

        try
        {
            _dataListener.Start(8);
            _controlListener.Start(8);
            _previewListener.Start(8);
            _audioListener.Start(8);
            _fileListener.Start(4);
        }
        catch
        {
            StopListenersOnly();
            _serverCts.Dispose();
            _serverCts = null;
            throw;
        }

        _beacon.Start(HostId, Port);
        StatusChanged?.Invoke($"Сервер запущен. Экран {Port}, управление {Port + 1}, превью {Port + 2}, звук {Port + 3}, файлы {Port + 4}.");
        _ = AcceptDataLoopAsync(_serverCts.Token);
        _ = AcceptControlLoopAsync(_serverCts.Token);
        _ = AcceptPreviewLoopAsync(_serverCts.Token);
        _ = AcceptAudioLoopAsync(_serverCts.Token);
        _ = AcceptFileLoopAsync(_serverCts.Token);
        return Task.CompletedTask;
    }

    public void Stop()
    {
        _beacon.Stop();
        try { _serverCts?.Cancel(); } catch { }
        EndSession();
        StopListenersOnly();
        _serverCts?.Dispose();
        _serverCts = null;
        ClientChanged?.Invoke(null);
        StatusChanged?.Invoke("Сервер остановлен");
    }

    private void StopListenersOnly()
    {
        try { _dataListener?.Stop(); } catch { }
        try { _controlListener?.Stop(); } catch { }
        try { _previewListener?.Stop(); } catch { }
        try { _audioListener?.Stop(); } catch { }
        try { _fileListener?.Stop(); } catch { }
        _dataListener = null;
        _controlListener = null;
        _previewListener = null;
        _audioListener = null;
        _fileListener = null;
    }

    public void DisconnectClient() => EndSession();

    private async Task AcceptDataLoopAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested && _dataListener != null)
        {
            try
            {
                TcpClient client = await _dataListener.AcceptTcpClientAsync(ct).ConfigureAwait(false);
                ConfigureTcp(client);
                lock (_sessionSync)
                {
                    if (_dataClient != null)
                    {
                        client.Close();
                        continue;
                    }
                    _dataClient = client;
                }
                _ = HandleDataClientAsync(client, ct);
            }
            catch (OperationCanceledException) { break; }
            catch (ObjectDisposedException) { break; }
            catch (Exception ex)
            {
                AppLog.Write(ex, "AcceptDataLoop");
                StatusChanged?.Invoke("Ошибка приёма соединения: " + ex.Message);
                try { await Task.Delay(300, ct).ConfigureAwait(false); } catch { break; }
            }
        }
    }

    private async Task AcceptControlLoopAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested && _controlListener != null)
        {
            try
            {
                TcpClient client = await _controlListener.AcceptTcpClientAsync(ct).ConfigureAwait(false);
                ConfigureTcp(client);
                _ = HandleControlClientAsync(client, ct);
            }
            catch (OperationCanceledException) { break; }
            catch (ObjectDisposedException) { break; }
            catch (Exception ex)
            {
                AppLog.Write(ex, "AcceptControlLoop");
                try { await Task.Delay(300, ct).ConfigureAwait(false); } catch { break; }
            }
        }
    }

    private async Task AcceptPreviewLoopAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested && _previewListener != null)
        {
            try
            {
                TcpClient client = await _previewListener.AcceptTcpClientAsync(ct).ConfigureAwait(false);
                ConfigureTcp(client);
                _ = HandlePreviewClientAsync(client, ct);
            }
            catch (OperationCanceledException) { break; }
            catch (ObjectDisposedException) { break; }
            catch (Exception ex)
            {
                AppLog.Write(ex, "AcceptPreviewLoop");
                try { await Task.Delay(300, ct).ConfigureAwait(false); } catch { break; }
            }
        }
    }

    private async Task AcceptAudioLoopAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested && _audioListener != null)
        {
            try
            {
                TcpClient client = await _audioListener.AcceptTcpClientAsync(ct).ConfigureAwait(false);
                ConfigureTcp(client);
                _ = HandleAudioClientAsync(client, ct);
            }
            catch (OperationCanceledException) { break; }
            catch (ObjectDisposedException) { break; }
            catch (Exception ex)
            {
                AppLog.Write(ex, "AcceptAudioLoop");
                try { await Task.Delay(300, ct).ConfigureAwait(false); } catch { break; }
            }
        }
    }

    private async Task AcceptFileLoopAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested && _fileListener != null)
        {
            try
            {
                TcpClient client = await _fileListener.AcceptTcpClientAsync(ct).ConfigureAwait(false);
                ConfigureTcp(client);
                _ = HandleFileClientAsync(client, ct);
            }
            catch (OperationCanceledException) { break; }
            catch (ObjectDisposedException) { break; }
            catch (Exception ex)
            {
                AppLog.Write(ex, "AcceptFileLoop");
                try { await Task.Delay(300, ct).ConfigureAwait(false); } catch { break; }
            }
        }
    }

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

    private async Task HandleFileClientAsync(TcpClient client, CancellationToken serverCt)
    {
        using (client)
        {
            try
            {
                using NetworkStream stream = client.GetStream();
                using var timeout = CancellationTokenSource.CreateLinkedTokenSource(serverCt);
                timeout.CancelAfter(TimeSpan.FromMinutes(30));
                byte[] key = await AuthenticateServerAsync(stream, Password, timeout.Token).ConfigureAwait(false);
                using var channel = new SecureChannel(stream, key, HostPrefix, ViewerPrefix);
                using SecurePacket command = await channel.ReceiveAsync(timeout.Token).ConfigureAwait(false);

                if (command.Type == PacketType.FileClipboardGet)
                    await SendClipboardFilesAsync(channel, timeout.Token).ConfigureAwait(false);
                else if (command.Type == PacketType.FileClipboardPut)
                    await ReceiveClipboardFilesAsync(channel, timeout.Token).ConfigureAwait(false);
            }
            catch (OperationCanceledException) { }
            catch (Exception ex) { AppLog.Write(ex, "File transfer"); }
        }
    }

    private async Task SendClipboardFilesAsync(SecureChannel channel, CancellationToken ct)
    {
        string[] roots = GetClipboardFilesSta();
        var existing = roots.Where(p => File.Exists(p) || Directory.Exists(p)).Take(64).ToArray();
        var manifest = BuildTransferManifest(existing, out Dictionary<string, string> sourceMap);
        byte[] json = JsonSerializer.SerializeToUtf8Bytes(manifest);
        await channel.SendAsync(PacketType.FileManifest, json, ct).ConfigureAwait(false);

        const int chunkSize = 1024 * 1024;
        byte[] chunk = new byte[chunkSize + 4];
        for (int index = 0; index < manifest.Entries.Count; index++)
        {
            TransferEntry entry = manifest.Entries[index];
            if (entry.IsDirectory) continue;
            if (!sourceMap.TryGetValue(entry.RelativePath, out string? full) || !File.Exists(full)) continue;
            await using FileStream fs = new(full, FileMode.Open, FileAccess.Read, FileShare.ReadWrite, chunkSize, true);
            while (true)
            {
                int read = await fs.ReadAsync(chunk.AsMemory(4, chunkSize), ct).ConfigureAwait(false);
                if (read <= 0) break;
                BitConverter.GetBytes(index).CopyTo(chunk, 0);
                await channel.SendAsync(PacketType.FileChunk, chunk.AsMemory(0, read + 4), ct).ConfigureAwait(false);
            }
        }
        await channel.SendAsync(PacketType.FileTransferEnd, ReadOnlyMemory<byte>.Empty, ct).ConfigureAwait(false);
    }

    private async Task ReceiveClipboardFilesAsync(SecureChannel channel, CancellationToken ct)
    {
        using SecurePacket manifestPacket = await channel.ReceiveAsync(ct).ConfigureAwait(false);
        if (manifestPacket.Type != PacketType.FileManifest) throw new InvalidDataException("File manifest expected.");
        var manifest = JsonSerializer.Deserialize<TransferManifest>(manifestPacket.PayloadSpan) ?? new TransferManifest();
        if (manifest.Entries.Count > 10000) throw new InvalidDataException("Слишком много файлов.");

        string root = Path.Combine(Path.GetTempPath(), "SimpleRemoteDesk", "Incoming", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        FileStream? current = null;
        int currentIndex = -1;
        try
        {
            foreach (TransferEntry entry in manifest.Entries.Where(e => e.IsDirectory))
                Directory.CreateDirectory(SafeDestination(root, entry.RelativePath));

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

        var top = new System.Collections.Specialized.StringCollection();
        foreach (string rel in manifest.Roots.Distinct(StringComparer.OrdinalIgnoreCase))
        {
            string path = SafeDestination(root, rel);
            if (File.Exists(path) || Directory.Exists(path)) top.Add(path);
        }
        SetClipboardFilesSta(top);
        // Give Explorer / the shell a moment to observe the new HDROP clipboard data,
        // then paste into whatever remote window currently owns keyboard focus.
        await Task.Delay(180, ct).ConfigureAwait(false);
        HostInput.KeyCombination(new[] { (int)Keys.ControlKey, (int)Keys.V });
        await channel.SendAsync(PacketType.FileTransferAck, Encoding.UTF8.GetBytes("OK"), ct).ConfigureAwait(false);
    }

    private static TransferManifest BuildTransferManifest(string[] sourceRoots, out Dictionary<string, string> sourceMap)
    {
        var manifest = new TransferManifest();
        sourceMap = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var usedRoots = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string source in sourceRoots)
        {
            string baseName = Path.GetFileName(source.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
            if (string.IsNullOrWhiteSpace(baseName)) baseName = "item";
            string rootName = baseName;
            int suffix = 2;
            while (!usedRoots.Add(rootName)) rootName = $"{baseName} ({suffix++})";
            manifest.Roots.Add(rootName);
            if (File.Exists(source))
            {
                var entry = new TransferEntry { RelativePath = rootName, Length = new FileInfo(source).Length };
                manifest.Entries.Add(entry); sourceMap[entry.RelativePath] = source;
            }
            else if (Directory.Exists(source))
            {
                manifest.Entries.Add(new TransferEntry { RelativePath = rootName, IsDirectory = true });
                foreach (string dir in Directory.EnumerateDirectories(source, "*", SearchOption.AllDirectories))
                    manifest.Entries.Add(new TransferEntry { RelativePath = Path.Combine(rootName, Path.GetRelativePath(source, dir)), IsDirectory = true });
                foreach (string file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
                {
                    var entry = new TransferEntry { RelativePath = Path.Combine(rootName, Path.GetRelativePath(source, file)), Length = new FileInfo(file).Length };
                    manifest.Entries.Add(entry);
                    sourceMap[entry.RelativePath] = file;
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
        if (!full.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase) && !string.Equals(full.TrimEnd(Path.DirectorySeparatorChar), root.TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Недопустимый путь файла.");
        return full;
    }

    private static string[] GetClipboardFilesSta()
    {
        string[] result = Array.Empty<string>();
        Exception? error = null;
        var t = new Thread(() =>
        {
            try
            {
                if (Clipboard.ContainsFileDropList())
                    result = Clipboard.GetFileDropList().Cast<string>().ToArray();
            }
            catch (Exception ex) { error = ex; }
        });
        t.SetApartmentState(ApartmentState.STA);
        t.Start(); t.Join();
        if (error != null) throw error;
        return result;
    }

    private static void SetClipboardFilesSta(System.Collections.Specialized.StringCollection files)
    {
        Exception? error = null;
        var t = new Thread(() =>
        {
            try { Clipboard.SetFileDropList(files); }
            catch (Exception ex) { error = ex; }
        });
        t.SetApartmentState(ApartmentState.STA);
        t.Start(); t.Join();
        if (error != null) throw error;
    }

    private async Task HandleDataClientAsync(TcpClient client, CancellationToken serverCt)
    {
        string endpoint = client.Client.RemoteEndPoint?.ToString() ?? "неизвестный клиент";
        try
        {
            using NetworkStream stream = client.GetStream();
            byte[] key = await AuthenticateServerAsync(stream, Password, serverCt).ConfigureAwait(false);
            using var channel = new SecureChannel(stream, key, HostPrefix, ViewerPrefix);

            CancellationTokenSource sessionCts;
            CancellationToken sessionToken;
            lock (_sessionSync)
            {
                if (!ReferenceEquals(_dataClient, client)) return;
                sessionCts = CancellationTokenSource.CreateLinkedTokenSource(serverCt);
                _sessionCts = sessionCts;
                _sessionRemoteIp = GetRemoteIp(client);
                sessionToken = sessionCts.Token;
            }

            ClientChanged?.Invoke(endpoint);
            StatusChanged?.Invoke("Клиент подключён: " + endpoint);
            AppLog.Write("Session connected: " + endpoint);

            try { await CaptureLoopAsync(channel, sessionToken).ConfigureAwait(false); }
            finally
            {
                try { sessionCts.Cancel(); } catch { }
                sessionCts.Dispose();
            }
        }
        catch (CryptographicException ex)
        {
            AppLog.Write(ex, "Data authentication/encryption");
            StatusChanged?.Invoke("Ошибка авторизации/шифрования от " + endpoint);
        }
        catch (OperationCanceledException) { }
        catch (Exception ex)
        {
            AppLog.Write(ex, "Data session");
            StatusChanged?.Invoke("Соединение завершено: " + ex.Message);
        }
        finally
        {
            EndSession(client);
        }
    }

    private async Task HandleControlClientAsync(TcpClient client, CancellationToken serverCt)
    {
        bool joinedSession = false;
        try
        {
            string remoteIp = GetRemoteIp(client);
            CancellationToken sessionToken = await WaitForSessionAsync(remoteIp, serverCt).ConfigureAwait(false);

            lock (_sessionSync)
            {
                if (_sessionCts == null || !string.Equals(_sessionRemoteIp, remoteIp, StringComparison.OrdinalIgnoreCase) || _controlClient != null)
                {
                    client.Close();
                    return;
                }
                _controlClient = client;
                joinedSession = true;
                sessionToken = _sessionCts.Token;
            }

            using NetworkStream stream = client.GetStream();
            byte[] key = await AuthenticateServerAsync(stream, Password, serverCt).ConfigureAwait(false);
            using var channel = new SecureChannel(stream, key, HostPrefix, ViewerPrefix);

            StatusChanged?.Invoke("Подключено: экран и управление активны");
            await InputLoopAsync(channel, sessionToken).ConfigureAwait(false);
        }
        catch (CryptographicException ex) { AppLog.Write(ex, "Control authentication/encryption"); }
        catch (OperationCanceledException) { }
        catch (Exception ex) { AppLog.Write(ex, "Control session"); }
        finally
        {
            if (joinedSession) EndSession(expectedControlClient: client);
            else { try { client.Close(); } catch { } }
        }
    }

    private async Task HandleAudioClientAsync(TcpClient client, CancellationToken serverCt)
    {
        try
        {
            string remoteIp = GetRemoteIp(client);
            CancellationToken sessionToken = await WaitForSessionAsync(remoteIp, serverCt).ConfigureAwait(false);

            lock (_sessionSync)
            {
                if (_sessionCts == null || !string.Equals(_sessionRemoteIp, remoteIp, StringComparison.OrdinalIgnoreCase) || _audioClient != null)
                {
                    client.Close();
                    return;
                }
                _audioClient = client;
                sessionToken = _sessionCts.Token;
            }

            using NetworkStream stream = client.GetStream();
            byte[] key = await AuthenticateServerAsync(stream, Password, serverCt).ConfigureAwait(false);
            using var channel = new SecureChannel(stream, key, HostPrefix, ViewerPrefix);

            if (AudioEnabled)
                await AudioSendLoopSafeAsync(channel, sessionToken).ConfigureAwait(false);
            else
                await Task.Delay(Timeout.Infinite, sessionToken).ConfigureAwait(false);
        }
        catch (CryptographicException ex) { AppLog.Write(ex, "Audio authentication/encryption"); }
        catch (OperationCanceledException) { }
        catch (Exception ex) { AppLog.Write(ex, "Audio session"); }
        finally
        {
            lock (_sessionSync)
            {
                if (ReferenceEquals(_audioClient, client)) _audioClient = null;
            }
            try { client.Close(); } catch { }
        }
    }

    private async Task HandlePreviewClientAsync(TcpClient client, CancellationToken serverCt)
    {
        using (client)
        {
            try
            {
                using NetworkStream stream = client.GetStream();
                using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(serverCt);
                timeoutCts.CancelAfter(TimeSpan.FromSeconds(6));
                byte[] key = await AuthenticateServerAsync(stream, Password, timeoutCts.Token).ConfigureAwait(false);
                using var channel = new SecureChannel(stream, key, HostPrefix, ViewerPrefix);
                using var capture = new ScreenCaptureEncoder(78, 1, maxWidth: 640);
                ArraySegment<byte> frame = capture.Capture();

                byte[] dimensions = new byte[8];
                BinaryPrimitives.WriteInt32LittleEndian(dimensions.AsSpan(0, 4), capture.EncodedWidth);
                BinaryPrimitives.WriteInt32LittleEndian(dimensions.AsSpan(4, 4), capture.EncodedHeight);
                await channel.SendPartsAsync(PacketType.Preview, dimensions,
                    new ReadOnlyMemory<byte>(frame.Array!, frame.Offset, frame.Count), timeoutCts.Token).ConfigureAwait(false);
            }
            catch (OperationCanceledException) { }
            catch (CryptographicException) { }
            catch (Exception ex) { AppLog.Write(ex, "Preview client"); }
        }
    }

    private async Task<CancellationToken> WaitForSessionAsync(string remoteIp, CancellationToken serverCt)
    {
        for (int i = 0; i < 40 && !serverCt.IsCancellationRequested; i++)
        {
            lock (_sessionSync)
            {
                if (_sessionCts != null && string.Equals(_sessionRemoteIp, remoteIp, StringComparison.OrdinalIgnoreCase))
                    return _sessionCts.Token;
            }
            await Task.Delay(50, serverCt).ConfigureAwait(false);
        }
        throw new InvalidOperationException("Основная сессия не найдена.");
    }

    private async Task CaptureLoopAsync(SecureChannel channel, CancellationToken ct)
    {
        int targetFps = Math.Clamp(Fps, 1, 60);
        double frameIntervalMs = 1000.0 / targetFps;
        using var capture = new ScreenCaptureEncoder(JpegQuality, targetFps);
        byte[] dimensions = new byte[8];
        BinaryPrimitives.WriteInt32LittleEndian(dimensions.AsSpan(0, 4), capture.Width);
        BinaryPrimitives.WriteInt32LittleEndian(dimensions.AsSpan(4, 4), capture.Height);

        var clock = Stopwatch.StartNew();
        double nextFrameAt = clock.Elapsed.TotalMilliseconds;
        int captureErrors = 0;
        RemoteCursorKind lastCursor = RemoteCursorKind.Unknown;
        byte[] cursorPayload = new byte[1];

        while (!ct.IsCancellationRequested)
        {
            ArraySegment<byte> frame;
            try
            {
                frame = capture.Capture();
                captureErrors = 0;
            }
            catch (Exception ex)
            {
                captureErrors++;
                AppLog.Write(ex, "Screen capture");
                if (captureErrors >= 20) throw;
                await Task.Delay(50, ct).ConfigureAwait(false);
                continue;
            }

            RemoteCursorKind currentCursor = CursorState.GetCurrentKind();
            if (currentCursor != lastCursor)
            {
                cursorPayload[0] = (byte)currentCursor;
                await channel.SendAsync(PacketType.Cursor, cursorPayload, ct).ConfigureAwait(false);
                lastCursor = currentCursor;
            }

            await channel.SendPartsAsync(PacketType.Screen, dimensions,
                new ReadOnlyMemory<byte>(frame.Array!, frame.Offset, frame.Count), ct).ConfigureAwait(false);

            nextFrameAt += frameIntervalMs;
            double remaining = nextFrameAt - clock.Elapsed.TotalMilliseconds;
            if (remaining >= 1)
                await Task.Delay((int)remaining, ct).ConfigureAwait(false);
            else if (remaining < -250)
                nextFrameAt = clock.Elapsed.TotalMilliseconds;
        }
    }

    private async Task AudioSendLoopSafeAsync(SecureChannel channel, CancellationToken ct)
    {
        try
        {
            using var audio = new AudioCapture(AudioDeviceId);
            audio.Start();
            if (audio.Format == null) return;

            byte[] format = new byte[16];
            BinaryPrimitives.WriteInt32LittleEndian(format.AsSpan(0, 4), audio.Format.SampleRate);
            BinaryPrimitives.WriteInt32LittleEndian(format.AsSpan(4, 4), audio.Format.BitsPerSample);
            BinaryPrimitives.WriteInt32LittleEndian(format.AsSpan(8, 4), audio.Format.Channels);
            BinaryPrimitives.WriteInt32LittleEndian(format.AsSpan(12, 4), audio.Format.Encoding);
            await channel.SendAsync(PacketType.AudioFormat, format, ct).ConfigureAwait(false);

            await foreach (byte[] block in audio.Reader.ReadAllAsync(ct))
                await channel.SendAsync(PacketType.Audio, block, ct).ConfigureAwait(false);
        }
        catch (OperationCanceledException) { }
        catch (Exception ex)
        {
            AppLog.Write(ex, "Audio capture");
            StatusChanged?.Invoke("Звук недоступен; экран и управление продолжают работу");
            try { await Task.Delay(Timeout.Infinite, ct).ConfigureAwait(false); } catch { }
        }
    }

    private async Task InputLoopAsync(SecureChannel channel, CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            using SecurePacket packet = await channel.ReceiveAsync(ct).ConfigureAwait(false);
            byte[] b = packet.Buffer;
            int o = packet.PayloadOffset;
            int n = packet.PayloadLength;

            switch (packet.Type)
            {
                case PacketType.MouseMove when n >= 8:
                    HostInput.MoveMouse(BitConverter.ToInt32(b, o), BitConverter.ToInt32(b, o + 4));
                    break;
                case PacketType.MouseButton when n >= 2:
                    HostInput.MouseButton(b[o], b[o + 1] != 0);
                    break;
                case PacketType.MouseWheel when n >= 4:
                    HostInput.MouseWheel(BitConverter.ToInt32(b, o));
                    break;
                case PacketType.Key when n >= 5:
                    HostInput.Key(BitConverter.ToInt32(b, o), b[o + 4] != 0);
                    break;
                case PacketType.KeyCombination when n >= 4:
                {
                    int count = BitConverter.ToInt32(b, o);
                    if (count >= 1 && count <= 16 && n >= 4 + count * 4)
                    {
                        int[] keys = new int[count];
                        for (int i = 0; i < count; i++) keys[i] = BitConverter.ToInt32(b, o + 4 + i * 4);
                        HostInput.KeyCombination(keys);
                    }
                    break;
                }
            }
        }
    }

    private void EndSession(TcpClient? onlyIfDataClient = null, TcpClient? expectedControlClient = null)
    {
        CancellationTokenSource? sessionCts;
        TcpClient? data;
        TcpClient? control;
        TcpClient? audio;
        bool hadSession;

        lock (_sessionSync)
        {
            if (onlyIfDataClient != null && !ReferenceEquals(_dataClient, onlyIfDataClient)) return;
            if (expectedControlClient != null && !ReferenceEquals(_controlClient, expectedControlClient)) return;
            hadSession = _dataClient != null || _controlClient != null || _audioClient != null || _sessionCts != null;
            sessionCts = _sessionCts;
            data = _dataClient;
            control = _controlClient;
            audio = _audioClient;
            _sessionCts = null;
            _dataClient = null;
            _controlClient = null;
            _audioClient = null;
            _sessionRemoteIp = null;
        }

        try { sessionCts?.Cancel(); } catch { }
        try { data?.Close(); } catch { }
        try { control?.Close(); } catch { }
        try { audio?.Close(); } catch { }

        if (hadSession)
        {
            ClientChanged?.Invoke(null);
            if (_dataListener != null) StatusChanged?.Invoke("Ожидание подключения...");
        }
    }

    private static void ConfigureTcp(TcpClient client)
    {
        client.NoDelay = true;
        client.SendBufferSize = 2 * 1024 * 1024;
        client.ReceiveBufferSize = 2 * 1024 * 1024;
    }

    private static string GetRemoteIp(TcpClient client) =>
        (client.Client.RemoteEndPoint as IPEndPoint)?.Address.ToString() ?? string.Empty;

    private static async Task<byte[]> AuthenticateServerAsync(Stream stream, string password, CancellationToken ct)
    {
        byte[] magic = Encoding.ASCII.GetBytes("SRD3");
        byte[] salt = RandomNumberGenerator.GetBytes(16);
        byte[] challenge = RandomNumberGenerator.GetBytes(32);

        await stream.WriteAsync(magic, ct).ConfigureAwait(false);
        await stream.WriteAsync(salt, ct).ConfigureAwait(false);
        await stream.WriteAsync(challenge, ct).ConfigureAwait(false);
        await stream.FlushAsync(ct).ConfigureAwait(false);

        using var pbkdf2 = new Rfc2898DeriveBytes(password, salt, 120_000, HashAlgorithmName.SHA256);
        byte[] key = pbkdf2.GetBytes(32);
        using var hmac = new HMACSHA256(key);
        byte[] expected = hmac.ComputeHash(challenge);
        byte[] received = await ReadExactlyAsync(stream, 32, ct).ConfigureAwait(false);

        bool ok = CryptographicOperations.FixedTimeEquals(expected, received);
        await stream.WriteAsync(new[] { ok ? (byte)1 : (byte)0 }, ct).ConfigureAwait(false);
        await stream.FlushAsync(ct).ConfigureAwait(false);
        if (!ok)
        {
            CryptographicOperations.ZeroMemory(key);
            throw new CryptographicException("Неверный пароль.");
        }
        return key;
    }

    private static async Task<byte[]> ReadExactlyAsync(Stream stream, int count, CancellationToken ct)
    {
        byte[] buffer = new byte[count];
        int offset = 0;
        while (offset < count)
        {
            int read = await stream.ReadAsync(buffer.AsMemory(offset, count - offset), ct).ConfigureAwait(false);
            if (read == 0) throw new EndOfStreamException();
            offset += read;
        }
        return buffer;
    }

    public void Dispose()
    {
        Stop();
        _beacon.Dispose();
    }
}
