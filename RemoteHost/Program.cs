namespace RemoteHost;

internal static class Program
{
    private static Mutex? _singleInstanceMutex;
    public static bool StartedFromWindows { get; private set; }

    [STAThread]
    static void Main(string[] args)
    {
        StartedFromWindows = args.Any(a => string.Equals(a, "--autostart", StringComparison.OrdinalIgnoreCase));

        _singleInstanceMutex = new Mutex(initiallyOwned: true, name: @"Local\SimpleRemoteDesk.RemoteHost", createdNew: out bool createdNew);
        if (!createdNew)
        {
            if (!StartedFromWindows)
                MessageBox.Show(
                    "Simple Remote Host уже запущен. Проверьте значок программы в системном трее или завершите старый RemoteHost.exe в Диспетчере задач перед запуском новой версии.",
                    "Simple Remote Host", MessageBoxButtons.OK, MessageBoxIcon.Information);
            _singleInstanceMutex.Dispose();
            _singleInstanceMutex = null;
            return;
        }

        try
        {
            ApplicationConfiguration.Initialize();
            Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
            Application.ThreadException += (_, e) => AppLog.Write(e.Exception, "UI exception");
            AppDomain.CurrentDomain.UnhandledException += (_, e) =>
            {
                if (e.ExceptionObject is Exception ex) AppLog.Write(ex, "Unhandled exception");
                else AppLog.Write("Unhandled non-Exception object");
            };

            try
            {
                Application.Run(new HostForm());
            }
            catch (Exception ex)
            {
                AppLog.Write(ex, "Host startup failed");
                MessageBox.Show(
                    $"Simple Remote Host не смог запуститься.\n\n{ex.Message}\n\nЛог:\n{AppLog.FilePath}",
                    "Ошибка запуска Simple Remote Host",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
            }
        }
        catch (Exception ex)
        {
            AppLog.Write(ex, "Application initialization failed");
            try
            {
                MessageBox.Show(
                    $"Ошибка инициализации Simple Remote Host.\n\n{ex.Message}\n\nЛог:\n{AppLog.FilePath}",
                    "Simple Remote Host",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
            }
            catch { }
        }
        finally
        {
            try { _singleInstanceMutex?.ReleaseMutex(); } catch { }
            _singleInstanceMutex?.Dispose();
            _singleInstanceMutex = null;
        }
    }
}
