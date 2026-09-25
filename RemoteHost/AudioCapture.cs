using NAudio.CoreAudioApi;
using NAudio.Wave;
using System.Threading.Channels;

namespace RemoteHost;

internal sealed record NetworkAudioFormat(int SampleRate, int BitsPerSample, int Channels, int Encoding);
internal sealed record AudioOutputDevice(string Id, string Name);

internal sealed class AudioCapture : IDisposable
{
    private WasapiLoopbackCapture? _capture;
    private MMDeviceEnumerator? _enumerator;
    private MMDevice? _device;
    private WaveFormat? _sourceFormat;

    private readonly Channel<byte[]> _queue = Channel.CreateBounded<byte[]>(new BoundedChannelOptions(48)
    {
        SingleReader = true,
        SingleWriter = false,
        FullMode = BoundedChannelFullMode.DropOldest
    });

    private readonly string _deviceId;

    public NetworkAudioFormat? Format { get; private set; }
    public ChannelReader<byte[]> Reader => _queue.Reader;

    public AudioCapture(string? deviceId = null)
    {
        _deviceId = deviceId ?? string.Empty;
    }

    public static IReadOnlyList<AudioOutputDevice> GetOutputDevices()
    {
        var result = new List<AudioOutputDevice>();
        try
        {
            using var enumerator = new MMDeviceEnumerator();
            foreach (MMDevice device in enumerator.EnumerateAudioEndPoints(DataFlow.Render, DeviceState.Active))
            {
                try { result.Add(new AudioOutputDevice(device.ID, device.FriendlyName)); }
                finally { device.Dispose(); }
            }
        }
        catch (Exception ex)
        {
            AppLog.Write(ex, "Enumerate audio output devices");
        }
        return result;
    }

    public void Start()
    {
        _enumerator = new MMDeviceEnumerator();
        if (string.IsNullOrWhiteSpace(_deviceId))
        {
            _device = _enumerator.GetDefaultAudioEndpoint(DataFlow.Render, Role.Multimedia);
        }
        else
        {
            try { _device = _enumerator.GetDevice(_deviceId); }
            catch
            {
                AppLog.Write("Saved audio device is unavailable; falling back to the Windows default output device.");
                _device = _enumerator.GetDefaultAudioEndpoint(DataFlow.Render, Role.Multimedia);
            }
        }

        _capture = new WasapiLoopbackCapture(_device);
        _sourceFormat = _capture.WaveFormat;

        // В сеть всегда отправляем совместимый PCM 16-bit. Это значительно надёжнее,
        // чем передавать родной WASAPI float/extensible формат конкретного драйвера.
        Format = new NetworkAudioFormat(_sourceFormat.SampleRate, 16, _sourceFormat.Channels, (int)WaveFormatEncoding.Pcm);
        AppLog.Write($"Audio source: {_device.FriendlyName}; {_sourceFormat.Encoding}, {_sourceFormat.SampleRate} Hz, {_sourceFormat.BitsPerSample} bit, {_sourceFormat.Channels} ch -> PCM16");

        _capture.DataAvailable += OnDataAvailable;
        _capture.RecordingStopped += OnRecordingStopped;
        _capture.StartRecording();
    }

    private void OnDataAvailable(object? sender, WaveInEventArgs e)
    {
        if (e.BytesRecorded <= 0 || _sourceFormat == null) return;
        try
        {
            byte[] pcm16 = ConvertToPcm16(e.Buffer, e.BytesRecorded, _sourceFormat);
            if (pcm16.Length > 0) _queue.Writer.TryWrite(pcm16);
        }
        catch (Exception ex)
        {
            AppLog.Write(ex, "Audio PCM conversion");
        }
    }

    private void OnRecordingStopped(object? sender, StoppedEventArgs e)
    {
        if (e.Exception != null) AppLog.Write(e.Exception, "WASAPI loopback stopped");
    }

    private static byte[] ConvertToPcm16(byte[] source, int count, WaveFormat format)
    {
        int bits = format.BitsPerSample;
        bool float32 = format.Encoding == WaveFormatEncoding.IeeeFloat ||
                       (format.Encoding == WaveFormatEncoding.Extensible && bits == 32);

        if (bits == 16 && !float32)
        {
            byte[] copy = new byte[count];
            Buffer.BlockCopy(source, 0, copy, 0, count);
            return copy;
        }

        int bytesPerSample = Math.Max(1, bits / 8);
        int samples = count / bytesPerSample;
        byte[] output = new byte[samples * 2];

        for (int i = 0; i < samples; i++)
        {
            int si = i * bytesPerSample;
            short value;

            if (float32 && bytesPerSample >= 4)
            {
                float sample = BitConverter.ToSingle(source, si);
                if (float.IsNaN(sample) || float.IsInfinity(sample)) sample = 0f;
                sample = Math.Clamp(sample, -1f, 1f);
                value = (short)Math.Round(sample * 32767f);
            }
            else if (bits == 8)
            {
                value = (short)((source[si] - 128) << 8);
            }
            else if (bits == 24 && si + 2 < count)
            {
                int raw = source[si] | (source[si + 1] << 8) | (source[si + 2] << 16);
                if ((raw & 0x00800000) != 0) raw |= unchecked((int)0xFF000000);
                value = (short)(raw >> 8);
            }
            else if (bits == 32 && si + 3 < count)
            {
                int raw = BitConverter.ToInt32(source, si);
                value = (short)(raw >> 16);
            }
            else
            {
                value = 0;
            }

            int oi = i * 2;
            output[oi] = (byte)(value & 0xFF);
            output[oi + 1] = (byte)((value >> 8) & 0xFF);
        }

        return output;
    }

    public void Dispose()
    {
        try { _capture?.StopRecording(); } catch { }
        if (_capture != null)
        {
            _capture.DataAvailable -= OnDataAvailable;
            _capture.RecordingStopped -= OnRecordingStopped;
        }
        _capture?.Dispose();
        _capture = null;
        _device?.Dispose();
        _device = null;
        _enumerator?.Dispose();
        _enumerator = null;
        _queue.Writer.TryComplete();
    }
}
