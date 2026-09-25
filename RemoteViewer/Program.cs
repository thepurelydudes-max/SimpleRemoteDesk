namespace RemoteViewer;

internal static class Program
{
    private static Mutex? _singleInstanceMutex;
    private static EventWaitHandle? _showEvent;

    [STAThread]
    static void Main()
    {
        _singleInstanceMutex = new Mutex(initiallyOwned: true, name: @"Local\SimpleRemoteDesk.RemoteViewer", createdNew: out bool createdNew);
        _showEvent = new EventWaitHandle(false, EventResetMode.AutoReset, @"Local\SimpleRemoteDesk.RemoteViewer.Show");

        if (!createdNew)
        {
            try { _showEvent.Set(); } catch { }
            _showEvent.Dispose();
            _singleInstanceMutex.Dispose();
            _showEvent = null;
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
                using var form = new ViewerForm();
                var activationThread = new Thread(() => ActivationLoop(form))
                {
                    IsBackground = true,
                    Name = "Viewer activation listener"
                };
                activationThread.Start();
                Application.Run(form);
            }
            catch (Exception ex)
            {
                AppLog.Write(ex, "Viewer startup failed");
                MessageBox.Show(
                    $"Simple Remote Viewer не смог запуститься.\n\n{ex.Message}\n\nЛог:\n{AppLog.FilePath}",
                    "Ошибка запуска Simple Remote Viewer",
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
                    $"Ошибка инициализации Simple Remote Viewer.\n\n{ex.Message}\n\nЛог:\n{AppLog.FilePath}",
                    "Simple Remote Viewer",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
            }
            catch { }
        }
        finally
        {
            _showEvent?.Dispose();
            _showEvent = null;
            try { _singleInstanceMutex?.ReleaseMutex(); } catch { }
            _singleInstanceMutex?.Dispose();
            _singleInstanceMutex = null;
        }
    }

    private static void ActivationLoop(ViewerForm form)
    {
        while (!form.IsDisposed)
        {
            try
            {
                EventWaitHandle? signal = _showEvent;
                if (signal == null || !signal.WaitOne(500)) continue;
                if (form.IsDisposed) break;
                form.BeginInvoke(new Action(() =>
                {
                    if (form.IsDisposed) return;
                    if (!form.Visible) form.Show();
                    if (form.WindowState == FormWindowState.Minimized) form.WindowState = FormWindowState.Normal;
                    form.BringToFront();
                    form.Activate();
                }));
            }
            catch
            {
                if (form.IsDisposed) break;
            }
        }
    }
}
