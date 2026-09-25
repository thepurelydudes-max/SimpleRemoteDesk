using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

namespace RemoteHost;

internal sealed class ScreenCaptureEncoder : IDisposable
{
    private readonly Rectangle _bounds;
    private readonly Bitmap _captureBitmap;
    private readonly Graphics _captureGraphics;
    private readonly Bitmap? _scaledBitmap;
    private readonly Graphics? _scaledGraphics;
    private readonly ImageCodecInfo _jpegCodec;
    private readonly EncoderParameters _encoderParameters;
    private readonly MemoryStream _stream = new(1024 * 1024);

    public int Width => _bounds.Width;
    public int Height => _bounds.Height;
    public int EncodedWidth => _scaledBitmap?.Width ?? _captureBitmap.Width;
    public int EncodedHeight => _scaledBitmap?.Height ?? _captureBitmap.Height;

    public ScreenCaptureEncoder(long quality, int fps, int maxWidth = 0)
    {
        _bounds = Screen.PrimaryScreen?.Bounds ?? throw new InvalidOperationException("Экран не найден.");
        _captureBitmap = new Bitmap(_bounds.Width, _bounds.Height, PixelFormat.Format24bppRgb);
        _captureGraphics = Graphics.FromImage(_captureBitmap);
        _captureGraphics.CompositingMode = CompositingMode.SourceCopy;

        if (maxWidth > 0 && _bounds.Width > maxWidth)
        {
            double scale = (double)maxWidth / _bounds.Width;
            int h = Math.Max(1, (int)Math.Round(_bounds.Height * scale));
            _scaledBitmap = new Bitmap(maxWidth, h, PixelFormat.Format24bppRgb);
            _scaledGraphics = Graphics.FromImage(_scaledBitmap);
            _scaledGraphics.CompositingMode = CompositingMode.SourceCopy;
            _scaledGraphics.CompositingQuality = CompositingQuality.HighQuality;
            _scaledGraphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
            _scaledGraphics.SmoothingMode = SmoothingMode.HighQuality;
            _scaledGraphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
        }

        _jpegCodec = ImageCodecInfo.GetImageEncoders().First(x => x.FormatID == ImageFormat.Jpeg.Guid);
        _encoderParameters = new EncoderParameters(1);
        _encoderParameters.Param[0] = new EncoderParameter(Encoder.Quality, Math.Clamp(quality, 20, 100));
    }

    public ArraySegment<byte> Capture()
    {
        _captureGraphics.CopyFromScreen(_bounds.Left, _bounds.Top, 0, 0, _bounds.Size, CopyPixelOperation.SourceCopy);

        Image imageToEncode = _captureBitmap;
        if (_scaledBitmap != null && _scaledGraphics != null)
        {
            _scaledGraphics.DrawImage(_captureBitmap,
                new Rectangle(0, 0, _scaledBitmap.Width, _scaledBitmap.Height),
                0, 0, _captureBitmap.Width, _captureBitmap.Height, GraphicsUnit.Pixel);
            imageToEncode = _scaledBitmap;
        }

        _stream.Position = 0;
        _stream.SetLength(0);
        imageToEncode.Save(_stream, _jpegCodec, _encoderParameters);
        if (!_stream.TryGetBuffer(out ArraySegment<byte> buffer))
            throw new InvalidOperationException("Не удалось получить JPEG-буфер.");
        return new ArraySegment<byte>(buffer.Array!, buffer.Offset, checked((int)_stream.Length));
    }

    public void Dispose()
    {
        _encoderParameters.Dispose();
        _scaledGraphics?.Dispose();
        _scaledBitmap?.Dispose();
        _captureGraphics.Dispose();
        _captureBitmap.Dispose();
        _stream.Dispose();
    }
}
