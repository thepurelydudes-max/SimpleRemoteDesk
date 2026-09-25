using System.Text.Json;

namespace RemoteHost;

internal sealed class HostSettings
{
    public int Port { get; set; } = 45900;
    public string ProtectedPassword { get; set; } = string.Empty;
    public int Fps { get; set; } = 30;
    public long JpegQuality { get; set; } = 95;
    public bool AudioEnabled { get; set; } = true;
    public string AudioDeviceId { get; set; } = string.Empty;
    public bool AutoStartWindows { get; set; } = false;
    public bool StartServerOnLaunch { get; set; } = true;
    public string HostId { get; set; } = Guid.NewGuid().ToString("N");
    public int SettingsVersion { get; set; } = 4;

    public string GetPassword() => DataProtection.Unprotect(ProtectedPassword);
    public void SetPassword(string password) => ProtectedPassword = DataProtection.Protect(password);
}

internal static class HostSettingsStore
{
    private static readonly string DirectoryPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "SimpleRemoteDesk");
    private static readonly string FilePath = Path.Combine(DirectoryPath, "host.json");

    public static HostSettings Load()
    {
        try
        {
            if (!File.Exists(FilePath)) return new HostSettings();
            string json = File.ReadAllText(FilePath);
            HostSettings settings = JsonSerializer.Deserialize<HostSettings>(json) ?? new HostSettings();

            // Миграция старой v2.1, где свойства SettingsVersion ещё не было и JPEG по умолчанию был 55.
            bool oldSettings = !json.Contains("\"SettingsVersion\"", StringComparison.OrdinalIgnoreCase) || settings.SettingsVersion < 4;
            if (oldSettings)
            {
                if (settings.JpegQuality <= 92) settings.JpegQuality = 95;
                settings.SettingsVersion = 4;
                settings.StartServerOnLaunch = true;
                Save(settings);
            }

            if (string.IsNullOrWhiteSpace(settings.HostId)) settings.HostId = Guid.NewGuid().ToString("N");
            return settings;
        }
        catch
        {
            return new HostSettings();
        }
    }

    public static void Save(HostSettings settings)
    {
        Directory.CreateDirectory(DirectoryPath);
        var options = new JsonSerializerOptions { WriteIndented = true };
        File.WriteAllText(FilePath, JsonSerializer.Serialize(settings, options));
    }
}
