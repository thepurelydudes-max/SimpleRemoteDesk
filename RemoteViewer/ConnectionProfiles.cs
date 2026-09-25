using System.Text.Json;

namespace RemoteViewer;

internal sealed class ConnectionProfile
{
    public string Id { get; set; } = Guid.NewGuid().ToString("N");
    public string HostId { get; set; } = string.Empty;
    public string Name { get; set; } = "Компьютер";
    public string Host { get; set; } = string.Empty;
    public int Port { get; set; } = 45900;
    public string ProtectedPassword { get; set; } = string.Empty;

    public string GetPassword() => DataProtection.Unprotect(ProtectedPassword);
    public void SetPassword(string password) => ProtectedPassword = DataProtection.Protect(password);
}

internal static class ViewerSettingsStore
{
    private static readonly string DirectoryPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "SimpleRemoteDesk");
    private static readonly string FilePath = Path.Combine(DirectoryPath, "viewer.json");
    private static readonly string ThumbnailDirectory = Path.Combine(DirectoryPath, "thumbnails");

    public static List<ConnectionProfile> Load()
    {
        try
        {
            if (!File.Exists(FilePath)) return new List<ConnectionProfile>();
            return JsonSerializer.Deserialize<List<ConnectionProfile>>(File.ReadAllText(FilePath)) ?? new List<ConnectionProfile>();
        }
        catch
        {
            return new List<ConnectionProfile>();
        }
    }

    public static void Save(IEnumerable<ConnectionProfile> profiles)
    {
        Directory.CreateDirectory(DirectoryPath);
        var options = new JsonSerializerOptions { WriteIndented = true };
        File.WriteAllText(FilePath, JsonSerializer.Serialize(profiles, options));
    }

    public static string GetThumbnailPath(string profileId)
    {
        Directory.CreateDirectory(ThumbnailDirectory);
        return Path.Combine(ThumbnailDirectory, profileId + ".jpg");
    }

    public static void DeleteThumbnail(string profileId)
    {
        try
        {
            string path = GetThumbnailPath(profileId);
            if (File.Exists(path)) File.Delete(path);
        }
        catch { }
    }
}
