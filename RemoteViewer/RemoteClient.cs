using NAudio.Wave;
using System.Collections.Concurrent;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;

namespace RemoteViewer;

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

internal sealed class VideoFrame : IDisposable
{
    private SecurePacket? _packet;
    public int RemoteWidth { get; }
    public int RemoteHeight { get; }

    internal VideoFrame(SecurePacket packet, int width, int height)
    {
        _packet = packet;
        RemoteWidth = width;
        RemoteHeight = height;
    }

    public byte[] Buffer => _packet?.Buffer ?? throw new ObjectDisposedException(nameof(VideoFrame));
    public int JpegOffset => (_packet?.PayloadOffset ?? 0) + 8;
    public int JpegLength => Math.Max(0, (_packet?.PayloadLength ?? 0) - 8);

    public void Dispose()
    {
        SecurePacket? packet = Interlocked.Exchange(ref _packet, null);
        packet?.Dispose();
    }
}

internal readonly record struct ControlMessage(byte Type, byte[] Payload);

internal sealed class RemoteClient : IDisposable
{
    private const uint HostPrefix = 0x48535433;
    private const uint ViewerPrefix = 0x56575233;

    private readonly object _stateSync = new();
    private TcpClient? _dataClient;
    private TcpClient? _controlClient;
    private TcpClient? _audioClient;
    private SecureChannel? _dataChannel;
    private SecureChannel? _controlChannel;
    private SecureChannel? _audioChannel;
    private CancellationTokenSource? _cts;
    private int _connected;
    private int _disconnectNotified = 1;
    private long _activeSessionId;
    private long _nextSessionId;

    private readonly ConcurrentQueue<ControlMessage> _controlQueue = new();
    private readonly SemaphoreSlim _controlSignal = new(0);
    private int _mousePending;
    private int _latestMouseX;
    private int _latestMouseY;

    private WaveOutEvent? _waveOut;
    private BufferedWaveProvider? _audioBuffer;
    private readonly object _audioSync = new();
    private bool _audioPlaybackEnabled = true;

    public bool IsConnected => Volatile.Read(ref _connected) == 1;
    public int RemoteWidth { get; private set; }
    public int RemoteHeight { get; private set; }

    public bool AudioPlaybackEnabled
    {
        get => _audioPlaybackEnabled;
        set
        {
            _audioPlaybackEnabled = value;
            if (!value)
            {
                lock (_audioSync)
                    _audioBuffer?.ClearBuffer();
            }
        }
    }

    public event Action<VideoFrame>? FrameReceived;
    public event Action<byte>? CursorChanged;
    public event Action<string>? StatusChanged;
    public event Action? Disconnected;

    public async Task ConnectAsync(string host, int port, string password)
    {
        DisconnectInternal(false);
        long sessionId = Interlocked.Increment(ref _nextSessionId);
        if (port < 1024 || port > 65531)
            throw new ArgumentOutOfRangeException(nameof(port), "Базовый порт должен быть 1024–65531.");

        var cts = new CancellationTokenSource();
        TcpClient? dataClient = null;
        TcpClient? controlClient = null;
        TcpClient? audioClient = null;
        SecureChannel? dataChannel = null;
        SecureChannel? controlChannel = null;
        SecureChannel? audioChannel = null;

        try
        {
            StatusChanged?.Invoke("Подключение экрана...");
            dataClient = CreateTcpClient();
            await dataClient.ConnectAsync(host, port, cts.Token).ConfigureAwait(false);
            byte[] dataKey = await AuthenticateClientAsync(dataClient.GetStream(), password, cts.Token).ConfigureAwait(false);
            dataChannel = new SecureChannel(dataClient.GetStream(), dataKey, ViewerPrefix, HostPrefix);

            StatusChanged?.Invoke("Подключение управления...");
            controlClient = CreateTcpClient();
            await controlClient.ConnectAsync(host, port + 1, cts.Token).ConfigureAwait(false);
            byte[] controlKey = await AuthenticateClientAsync(controlClient.GetStream(), password, cts.Token).ConfigureAwait(false);
            controlChannel = new SecureChannel(controlClient.GetStream(), controlKey, ViewerPrefix, HostPrefix);

            try
            {
                StatusChanged?.Invoke("Подключение звука...");
                using var audioConnectCts = CancellationTokenSource.CreateLinkedTokenSource(cts.Token);
                audioConnectCts.CancelAfter(TimeSpan.FromSeconds(4));
                audioClient = CreateTcpClient();
                await audioClient.ConnectAsync(host, port + 3, audioConnectCts.Token).ConfigureAwait(false);
                byte[] audioKey = await AuthenticateClientAsync(audioClient.GetStream(), password, audioConnectCts.Token).ConfigureAwait(false);
                audioChannel = new SecureChannel(audioClient.GetStream(), audioKey, ViewerPrefix, HostPrefix);
            }
            catch (Exception ex)
            {
                AppLog.Write(ex, "Audio channel connect");
                try { audioClient?.Close(); } catch { }
                audioClient = null;
                audioChannel?.Dispose();
                audioChannel = null;
            }

            lock (_stateSync)
            {
                _cts = cts;
                _dataClient = dataClient;
                _controlClient = controlClient;
                _audioClient = audioClient;
                _dataChannel = dataChannel;
                _controlChannel = controlChannel;
                _audioChannel = audioChannel;
                _activeSessionId = sessionId;
                Volatile.Write(ref _disconnectNotified, 0);
                Volatile.Write(ref _connected, 1);
            }

            ClearControlQueue();
            StatusChanged?.Invoke(audioChannel != null
                ? "Подключено (экран, управление, звук)"
                : "Подключено (звук недоступен)");
            AppLog.Write($"Connected to {host}:{port}");

            _ = ReceiveLoopAsync(dataChannel, cts.Token, sessionId);
            _ = ControlSendLoopAsync(controlChannel, cts.Token, sessionId);
            if (audioChannel != null)
                _ = AudioReceiveLoopAsync(audioChannel, cts.Token, sessionId);
        }
        catch
        {
            try { cts.Cancel(); } catch { }
            try { dataClient?.Close(); } catch { }
            try { controlClient?.Close(); } catch { }
            try { audioClient?.Close(); } catch { }
            dataChannel?.Dispose();
            controlChannel?.Dispose();
            audioChannel?.Dispose();
            cts.Dispose();
            throw;
        }
    }

    public void Disconnect() => DisconnectInternal(true);

    public void SendMouseMove(int x, int y)
    {
        if (!IsConnected) return;
        Volatile.Write(ref _latestMouseX, x);
        Volatile.Write(ref _latestMouseY, y);
        Interlocked.Exchange(ref _mousePending, 1);
        SignalControl();
    }

    public void SendMouseMoveImmediate(int x, int y)
    {
        byte[] payload = new byte[8];
        BitConverter.GetBytes(x).CopyTo(payload, 0);
        BitConverter.GetBytes(y).CopyTo(payload, 4);
        EnqueueControl(PacketType.MouseMove, payload);
    }

    public void SendMouseButton(byte button, bool down) =>
        EnqueueControl(PacketType.MouseButton, new[] { button, down ? (byte)1 : (byte)0 });

    public void SendMouseWheel(int delta) =>
        EnqueueControl(PacketType.MouseWheel, BitConverter.GetBytes(delta));

    public void SendKey(int virtualKey, bool down)
    {
        byte[] payload = new byte[5];
        BitConverter.GetBytes(virtualKey).CopyTo(payload, 0);
        payload[4] = down ? (byte)1 : (byte)0;
        EnqueueControl(PacketType.Key, payload);
    }

    public void SendKeyCombination(params int[] keys)
    {
        if (keys.Length == 0 || keys.Length > 16 || !IsConnected) return;
        byte[] payload = new byte[4 + keys.Length * 4];
        BitConverter.GetBytes(keys.Length).CopyTo(payload, 0);
        for (int i = 0; i < keys.Length; i++)
            BitConverter.GetBytes(keys[i]).CopyTo(payload, 4 + i * 4);
        EnqueueControl(PacketType.KeyCombination, payload);
    }

    private void EnqueueControl(byte type, byte[] payload)
    {
        if (!IsConnected) return;
        _controlQueue.Enqueue(new ControlMessage(type, payload));
        SignalControl();
    }

    private void SignalControl()
    {
        try { _controlSignal.Release(); } catch { }
    }

    private async Task ControlSendLoopAsync(SecureChannel channel, CancellationToken ct, long sessionId)
    {
        try
        {
            while (!ct.IsCancellationRequested)
            {
                await _controlSignal.WaitAsync(ct).ConfigureAwait(false);

                while (_controlQueue.TryDequeue(out ControlMessage message))
                    await channel.SendAsync(message.Type, message.Payload, ct).ConfigureAwait(false);

                if (Interlocked.Exchange(ref _mousePending, 0) != 0)
                {
                    byte[] payload = new byte[8];
                    BitConverter.GetBytes(Volatile.Read(ref _latestMouseX)).CopyTo(payload, 0);
                    BitConverter.GetBytes(Volatile.Read(ref _latestMouseY)).CopyTo(payload, 4);
                    await channel.SendAsync(PacketType.MouseMove, payload, ct).ConfigureAwait(false);
                }
            }
        }
        catch (OperationCanceledException) { }
        catch (Exception ex)
        {
            AppLog.Write(ex, "Control send loop");
            StatusChanged?.Invoke("Канал управления потерян: " + ex.Message);
        }
        finally
        {
            DisconnectInternal(true, sessionId);
        }
    }

    private async Task ReceiveLoopAsync(SecureChannel channel, CancellationToken ct, long sessionId)
    {
        try
        {
            while (!ct.IsCancellationRequested)
            {
                SecurePacket? packet = await channel.ReceiveAsync(ct).ConfigureAwait(false);
                try
                {
                    byte[] b = packet.Buffer;
                    int o = packet.PayloadOffset;
                    int n = packet.PayloadLength;

                    if (packet.Type == PacketType.Screen && n > 8)
                    {
                        int width = BitConverter.ToInt32(b, o);
                        int height = BitConverter.ToInt32(b, o + 4);
                        if (width <= 0 || height <= 0) continue;
                        RemoteWidth = width;
                        RemoteHeight = height;

                        var frame = new VideoFrame(packet, width, height);
                        packet = null;
                        Action<VideoFrame>? handler = FrameReceived;
                        if (handler != null)
                        {
                            try { handler(frame); }
                            catch (Exception ex)
                            {
                                AppLog.Write(ex, "FrameReceived handler");
                                frame.Dispose();
                            }
                        }
                        else
                        {
                            frame.Dispose();
                        }
                    }
                    else if (packet.Type == PacketType.Cursor && n >= 1)
                    {
                        try { CursorChanged?.Invoke(b[o]); }
                        catch (Exception ex) { AppLog.Write(ex, "CursorChanged handler"); }
                    }
                }
                finally
                {
                    packet?.Dispose();
                }
            }
        }
        catch (OperationCanceledException) { }
        catch (Exception ex)
        {
            AppLog.Write(ex, "Receive loop");
            StatusChanged?.Invoke("Соединение потеряно: " + ex.Message);
        }
        finally
        {
            DisconnectInternal(true, sessionId);
        }
    }

    private async Task AudioReceiveLoopAsync(SecureChannel channel, CancellationToken ct, long sessionId)
    {
        try
        {
            while (!ct.IsCancellationRequested)
            {
                using SecurePacket packet = await channel.ReceiveAsync(ct).ConfigureAwait(false);
                byte[] b = packet.Buffer;
                int o = packet.PayloadOffset;
                int n = packet.PayloadLength;

                if (packet.Type == PacketType.AudioFormat && n >= 16)
                {
                    ConfigureAudio(
                        BitConverter.ToInt32(b, o),
                        BitConverter.ToInt32(b, o + 4),
                        BitConverter.ToInt32(b, o + 8),
                        BitConverter.ToInt32(b, o + 12));
                }
                else if (packet.Type == PacketType.Audio && _audioPlaybackEnabled)
                {
                    lock (_audioSync)
                    {
                        try { _audioBuffer?.AddSamples(b, o, n); } catch { }
                    }
                }
            }
        }
        catch (OperationCanceledException) { }
        catch (Exception ex)
        {
            AppLog.Write(ex, "Audio receive loop");
            SafeDisposeAudioChannel(sessionId);
            if (IsConnected) StatusChanged?.Invoke("Подключено (звук потерян)");
        }
    }

    private void SafeDisposeAudioChannel(long sessionId)
    {
        TcpClient? client = null;
        SecureChannel? channel = null;
        lock (_stateSync)
        {
            if (_activeSessionId != sessionId) return;
            client = _audioClient;
            channel = _audioChannel;
            _audioClient = null;
            _audioChannel = null;
        }
        try { client?.Close(); } catch { }
        try { channel?.Dispose(); } catch { }
        DisposeAudio();
    }

    private void ConfigureAudio(int sampleRate, int bitsPerSample, int channels, int encoding)
    {
        if (sampleRate < 8000 || sampleRate > 192000 || channels < 1 || channels > 8)
            return;

        try
        {
            WaveFormat format;
            if (encoding == (int)WaveFormatEncoding.IeeeFloat && bitsPerSample == 32)
                format = WaveFormat.CreateIeeeFloatWaveFormat(sampleRate, channels);
            else if (bitsPerSample is 8 or 16 or 24 or 32)
                format = new WaveFormat(sampleRate, bitsPerSample, channels);
            else
                return;

            lock (_audioSync)
            {
                DisposeAudioUnsafe();
                _audioBuffer = new BufferedWaveProvider(format)
                {
                    BufferDuration = TimeSpan.FromMilliseconds(400),
                    DiscardOnBufferOverflow = true,
                    ReadFully = true
                };
                _waveOut = new WaveOutEvent { DesiredLatency = 80, NumberOfBuffers = 3 };
                _waveOut.Init(_audioBuffer);
                _waveOut.Play();
                AppLog.Write($"Audio playback configured: {sampleRate} Hz, {bitsPerSample} bit, {channels} ch");
            }
        }
        catch (Exception ex)
        {
            AppLog.Write(ex, "ConfigureAudio");
        }
    }

    private void DisconnectInternal(bool notify, long? expectedSessionId = null)
    {
        CancellationTokenSource? cts;
        TcpClient? dataClient;
        TcpClient? controlClient;
        TcpClient? audioClient;
        SecureChannel? dataChannel;
        SecureChannel? controlChannel;
        SecureChannel? audioChannel;
        bool shouldNotify = false;

        lock (_stateSync)
        {
            if (expectedSessionId.HasValue && _activeSessionId != expectedSessionId.Value)
                return;

            bool wasConnected = Volatile.Read(ref _connected) == 1;
            Volatile.Write(ref _connected, 0);

            cts = _cts;
            dataClient = _dataClient;
            controlClient = _controlClient;
            audioClient = _audioClient;
            dataChannel = _dataChannel;
            controlChannel = _controlChannel;
            audioChannel = _audioChannel;

            _cts = null;
            _dataClient = null;
            _controlClient = null;
            _audioClient = null;
            _dataChannel = null;
            _controlChannel = null;
            _audioChannel = null;
            _activeSessionId = 0;

            if (notify && wasConnected && Interlocked.Exchange(ref _disconnectNotified, 1) == 0)
                shouldNotify = true;
        }

        try { cts?.Cancel(); } catch { }
        try { dataClient?.Close(); } catch { }
        try { controlClient?.Close(); } catch { }
        try { audioClient?.Close(); } catch { }
        try { dataChannel?.Dispose(); } catch { }
        try { controlChannel?.Dispose(); } catch { }
        try { audioChannel?.Dispose(); } catch { }
        cts?.Dispose();
        ClearControlQueue();
        DisposeAudio();

        if (shouldNotify)
        {
            AppLog.Write("Disconnected");
            try { Disconnected?.Invoke(); }
            catch (Exception ex) { AppLog.Write(ex, "Disconnected handler"); }
        }
    }

    private void ClearControlQueue()
    {
        while (_controlQueue.TryDequeue(out _)) { }
        Interlocked.Exchange(ref _mousePending, 0);
        try { while (_controlSignal.Wait(0)) { } } catch { }
    }

    private void DisposeAudio()
    {
        lock (_audioSync)
            DisposeAudioUnsafe();
    }

    private void DisposeAudioUnsafe()
    {
        try { _waveOut?.Stop(); } catch { }
        _waveOut?.Dispose();
        _waveOut = null;
        _audioBuffer = null;
    }

    private static TcpClient CreateTcpClient() => new()
    {
        NoDelay = true,
        SendBufferSize = 2 * 1024 * 1024,
        ReceiveBufferSize = 2 * 1024 * 1024
    };

    internal static async Task<byte[]> AuthenticateClientAsync(Stream stream, string password, CancellationToken ct)
    {
        byte[] magic = await ReadExactlyAsync(stream, 4, ct).ConfigureAwait(false);
        if (Encoding.ASCII.GetString(magic) != "SRD3")
            throw new InvalidDataException("Несовместимая версия RemoteHost. Нужны Host и Viewer из одного архива v2.5.");

        byte[] salt = await ReadExactlyAsync(stream, 16, ct).ConfigureAwait(false);
        byte[] challenge = await ReadExactlyAsync(stream, 32, ct).ConfigureAwait(false);
        using var pbkdf2 = new Rfc2898DeriveBytes(password, salt, 120_000, HashAlgorithmName.SHA256);
        byte[] key = pbkdf2.GetBytes(32);
        using var hmac = new HMACSHA256(key);
        byte[] response = hmac.ComputeHash(challenge);

        await stream.WriteAsync(response, ct).ConfigureAwait(false);
        await stream.FlushAsync(ct).ConfigureAwait(false);
        byte[] result = await ReadExactlyAsync(stream, 1, ct).ConfigureAwait(false);
        if (result[0] != 1)
        {
            CryptographicOperations.ZeroMemory(key);
            throw new UnauthorizedAccessException("Неверный пароль.");
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

    public void Dispose() => DisconnectInternal(false);
}
