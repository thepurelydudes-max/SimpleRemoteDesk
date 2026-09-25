namespace RemoteHost;

internal static class AppBrand
{
    public static Icon LoadIcon()
    {
        try { return Icon.ExtractAssociatedIcon(Application.ExecutablePath) ?? (Icon)SystemIcons.Application.Clone(); }
        catch { return (Icon)SystemIcons.Application.Clone(); }
    }

    public static PictureBox CreateLogo(int size = 36)
    {
        Bitmap image;
        try
        {
            using Stream? stream = typeof(AppBrand).Assembly.GetManifestResourceStream("BrandLogo.png");
            if (stream == null) throw new InvalidOperationException();
            using Image source = Image.FromStream(stream);
            image = new Bitmap(source, new Size(size, size));
        }
        catch
        {
            using Icon icon = LoadIcon();
            image = new Bitmap(icon.ToBitmap(), new Size(size, size));
        }

        return new PictureBox
        {
            Width = size,
            Height = size,
            SizeMode = PictureBoxSizeMode.Zoom,
            Image = image,
            Margin = new Padding(0, 0, 10, 0)
        };
    }
}
