namespace RemoteViewer;

internal static class AppLog
{
    private static readonly object Sync = new();
    private static readonly string DirectoryPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "SimpleRemoteDesk");
    public static readonly string FilePath = Path.Combine(DirectoryPath, "viewer.log");

    public static void Write(string message)
    {
        try
        {
            lock (Sync)
            {
                Directory.CreateDirectory(DirectoryPath);
                File.AppendAllText(FilePath, $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} {message}{Environment.NewLine}");
            }
        }
        catch { }
    }

    public static void Write(Exception ex, string context) => Write($"{context}: {ex}");
}
