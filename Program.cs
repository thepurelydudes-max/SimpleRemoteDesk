using RemoteHost;
using RemoteViewer;

namespace SimpleRemoteDesk;

internal static class Program
{
    private const string MutexName = @"Local\SimpleRemoteDesk.Unified";
    private const string ShowViewerEventName = @"Local\SimpleRemoteDesk.ShowViewer";
    private const string ShowSettingsEventName = @"Local\SimpleRemoteDesk.ShowSettings";

    private static Mutex? _singleInstanceMutex;
    private static EventWaitHandle? _showViewerEvent;
    private static EventWaitHandle? _showSettingsEvent;

    [STAThread]
    private static void Main(string[] args)
    {
        bool autostart = args.Any(a => string.Equals(a, "--autostart", StringComparison.OrdinalIgnoreCase));
        bool settings = args.Any(a => string.Equals(a, "--settings", StringComparison.OrdinalIgnoreCase));

        _singleInstanceMutex = new Mutex(true, MutexName, out bool createdNew);
        _showViewerEvent = new EventWaitHandle(false, EventResetMode.AutoReset, ShowViewerEventName);
        _showSettingsEvent = new EventWaitHandle(false, EventResetMode.AutoReset, ShowSettingsEventName);

        if (!createdNew)
        {
            try
            {
                if (settings) _showSettingsEvent.Set();
                else if (!autostart) _showViewerEvent.Set();
            }
            catch { }
            CleanupHandles(false);
            return;
        }

        ApplicationConfiguration.Initialize();
        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
        Application.ThreadException += (_, e) =>
        {
            try { RemoteHost.AppLog.Write(e.Exception, "Unified UI exception"); } catch { }
            try { RemoteViewer.AppLog.Write(e.Exception, "Unified UI exception"); } catch { }
        };
        AppDomain.CurrentDomain.UnhandledException += (_, e) =>
        {
            if (e.ExceptionObject is not Exception ex) return;
            try { RemoteHost.AppLog.Write(ex, "Unified unhandled exception"); } catch { }
            try { RemoteViewer.AppLog.Write(ex, "Unified unhandled exception"); } catch { }
        };

        try
        {
            using var context = new DeskApplicationContext(autostart, settings, _showViewerEvent, _showSettingsEvent);
            Application.Run(context);
        }
        catch (Exception ex)
        {
            try
            {
                MessageBox.Show(
                    $"Simple Remote Desk не смог запуститься.\n\n{ex.Message}",
                    "Ошибка запуска Simple Remote Desk",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
            }
            catch { }
        }
        finally
        {
            CleanupHandles(true);
        }
    }

    private static void CleanupHandles(bool releaseMutex)
    {
        try { _showViewerEvent?.Dispose(); } catch { }
        try { _showSettingsEvent?.Dispose(); } catch { }
        _showViewerEvent = null;
        _showSettingsEvent = null;

        if (releaseMutex)
        {
            try { _singleInstanceMutex?.ReleaseMutex(); } catch { }
        }
        try { _singleInstanceMutex?.Dispose(); } catch { }
        _singleInstanceMutex = null;
    }
}

internal sealed class DeskApplicationContext : ApplicationContext
{
    private readonly HostForm _hostForm;
    private ViewerForm? _viewerForm;
    private readonly EventWaitHandle _showViewerEvent;
    private readonly EventWaitHandle _showSettingsEvent;
    private readonly System.Windows.Forms.Timer _activationTimer;
    private bool _exiting;

    public DeskApplicationContext(bool autostart, bool showSettingsAtLaunch,
        EventWaitHandle showViewerEvent, EventWaitHandle showSettingsEvent)
    {
        _showViewerEvent = showViewerEvent;
        _showSettingsEvent = showSettingsEvent;

        _hostForm = new HostForm(autostart);
        _hostForm.ViewerRequested += ShowViewer;
        _hostForm.FormClosed += (_, _) =>
        {
            if (!_exiting && !Application.MessageLoop) ExitThread();
        };

        _activationTimer = new System.Windows.Forms.Timer { Interval = 180 };
        _activationTimer.Tick += (_, _) =>
        {
            try
            {
                if (_showViewerEvent.WaitOne(0)) ShowViewer();
                if (_showSettingsEvent.WaitOne(0)) _hostForm.ShowSettings();
            }
            catch { }
        };
        _activationTimer.Start();

        _ = _hostForm.InitializeBackgroundAsync();

        if (showSettingsAtLaunch)
            _hostForm.ShowSettings();
        else if (!autostart)
            ShowViewer();
    }

    private void ShowViewer()
    {
        if (_exiting) return;

        if (_viewerForm == null || _viewerForm.IsDisposed)
        {
            _viewerForm = new ViewerForm();
            _viewerForm.FormClosed += (_, _) => _viewerForm = null;
            _viewerForm.Show();
            return;
        }

        if (!_viewerForm.Visible) _viewerForm.Show();
        if (_viewerForm.WindowState == FormWindowState.Minimized)
            _viewerForm.WindowState = FormWindowState.Normal;
        _viewerForm.BringToFront();
        _viewerForm.Activate();
    }

    protected override void ExitThreadCore()
    {
        if (_exiting)
        {
            base.ExitThreadCore();
            return;
        }

        _exiting = true;
        try { _activationTimer.Stop(); } catch { }
        try { _activationTimer.Dispose(); } catch { }
        try
        {
            if (_viewerForm != null && !_viewerForm.IsDisposed)
                _viewerForm.Close();
        }
        catch { }
        try
        {
            if (!_hostForm.IsDisposed)
                _hostForm.Dispose();
        }
        catch { }

        base.ExitThreadCore();
    }
}
