using System.Runtime.InteropServices;

namespace RemoteHost;

internal sealed class ScreenCaptureEncoder : IDisposable
{
    private IntPtr _native;
    private byte[] _buffer = new byte[1024 * 1024];
    private readonly int _encodedWidth;
    private readonly int _encodedHeight;
    private bool _disposed;

    public int Width { get; }
    public int Height { get; }
    public int EncodedWidth => _encodedWidth;
    public int EncodedHeight => _encodedHeight;

    public ScreenCaptureEncoder(long quality, int fps, int maxWidth = 0)
    {
        Rectangle bounds = Screen.PrimaryScreen?.Bounds ?? throw new InvalidOperationException("Экран не найден.");
        Width = bounds.Width;
        Height = bounds.Height;

        try
        {
            _native = NativeMethods.SRD_CaptureCreate((int)Math.Clamp(quality, 20, 100), maxWidth, out _encodedWidth, out _encodedHeight);
        }
        catch (DllNotFoundException ex)
        {
            throw new InvalidOperationException("Не найден нативный модуль SimpleRemoteDesk.Native.dll.", ex);
        }
        catch (EntryPointNotFoundException ex)
        {
            throw new InvalidOperationException("Нативный модуль SimpleRemoteDesk.Native.dll несовместим с этой версией программы.", ex);
        }

        if (_native == IntPtr.Zero)
            throw new InvalidOperationException("Не удалось инициализировать нативный захват экрана.");
    }

    public ArraySegment<byte> Capture()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (NativeMethods.SRD_CaptureFrame(_native, out IntPtr data, out int length) == 0 || data == IntPtr.Zero || length <= 0)
            throw new InvalidOperationException("Не удалось получить JPEG-кадр из нативного модуля.");

        if (_buffer.Length < length)
        {
            int next = _buffer.Length;
            while (next < length) next = checked(next * 2);
            _buffer = new byte[next];
        }

        Marshal.Copy(data, _buffer, 0, length);
        return new ArraySegment<byte>(_buffer, 0, length);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        IntPtr handle = Interlocked.Exchange(ref _native, IntPtr.Zero);
        if (handle != IntPtr.Zero)
            NativeMethods.SRD_CaptureDestroy(handle);
    }

    private static class NativeMethods
    {
        [DllImport("SimpleRemoteDesk.Native.dll", CallingConvention = CallingConvention.StdCall)]
        internal static extern IntPtr SRD_CaptureCreate(int quality, int maxWidth, out int width, out int height);

        [DllImport("SimpleRemoteDesk.Native.dll", CallingConvention = CallingConvention.StdCall)]
        internal static extern int SRD_CaptureFrame(IntPtr handle, out IntPtr data, out int length);

        [DllImport("SimpleRemoteDesk.Native.dll", CallingConvention = CallingConvention.StdCall)]
        internal static extern void SRD_CaptureDestroy(IntPtr handle);
    }
}
