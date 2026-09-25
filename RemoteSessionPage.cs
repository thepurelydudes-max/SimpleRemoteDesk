using System.Diagnostics;

namespace RemoteViewer;

internal sealed class RemoteSessionPage : UserControl
{
    private readonly RemoteClient _client = new();
    private readonly PictureBox _screen = new()
    {
        Dock = DockStyle.Fill,
        BackColor = Color.Black,
        SizeMode = PictureBoxSizeMode.Zoom,
        TabStop = true
    };
    private bool _audioPlaybackEnabled = true;
    private string _displayStatus = "Не подключено";
    private readonly ContextMenuStrip _hotkeyMenu = new();
    private readonly Stopwatch _mouseThrottle = Stopwatch.StartNew();
    private readonly System.Windows.Forms.Timer _frameTimer = new() { Interval = 16 };
    private readonly HashSet<int> _pressedKeys = new();
    private VideoFrame? _latestFrame;
    private int _decodeBusy;
    private bool _disposed;
    private bool _fileTransferBusy;

    public ConnectionProfile Profile { get; }
    public string ProfileId => Profile.Id;
    public string SessionTitle => Profile.Name;
    public bool IsConnected => _client.IsConnected;
    public bool KeyboardTarget => _screen.Focused;
    public bool AudioPlaybackEnabled
    {
        get => _audioPlaybackEnabled;
        set
        {
            _audioPlaybackEnabled = value;
            _client.AudioPlaybackEnabled = value;
        }
    }
    public string DisplayStatus => _displayStatus;

    public event Action<RemoteSessionPage>? UiStateChanged;
    public event Action<RemoteSessionPage>? TopEdgeHovered;

    public RemoteSessionPage(ConnectionProfile profile)
    {
        Profile = profile;
        Dock = DockStyle.Fill;
        BackColor = UiTheme.Bg;
        Font = new Font("Segoe UI", 9.5F);

        BuildUi();
        BuildHotkeyMenu();

        _client.StatusChanged += text => SafeUi(() => SetStatus(text));
        _client.Disconnected += () => SafeUi(OnDisconnected);
        _client.FrameReceived += OnFrameReceived;
        _client.CursorChanged += kind => SafeUi(() => ApplyRemoteCursor(kind));

        _screen.MouseEnter += (_, _) => _screen.Focus();
        _screen.MouseMove += (_, e) =>
        {
            if (e.Y <= 8) TopEdgeHovered?.Invoke(this);
            if (!_client.IsConnected || _mouseThrottle.ElapsedMilliseconds < 8) return;
            if (TryMapToRemote(e.Location, out int x, out int y))
            {
                _mouseThrottle.Restart();
                _client.SendMouseMove(x, y);
            }
        };
        _screen.MouseDown += (_, e) =>
        {
            if (!TryMapToRemote(e.Location, out int x, out int y)) return;
            _client.SendMouseMoveImmediate(x, y);
            byte button = MapButton(e.Button);
            if (button != 0) _client.SendMouseButton(button, true);
            _screen.Focus();
        };
        _screen.MouseUp += (_, e) =>
        {
            byte button = MapButton(e.Button);
            if (button != 0) _client.SendMouseButton(button, false);
        };
        _screen.MouseWheel += (_, e) => _client.SendMouseWheel(e.Delta);

        _frameTimer.Tick += (_, _) => StartDecodeLatestFrame();
        _frameTimer.Start();
    }

    private void BuildUi()
    {
        // Session controls live in the Viewer top tab bar.  The session page itself
        // is intentionally only the remote desktop so no vertical space is wasted.
        var stage = new Panel
        {
            Dock = DockStyle.Fill,
            BackColor = Color.Black,
            Padding = Padding.Empty,
            Margin = Padding.Empty
        };
        stage.Controls.Add(_screen);
        Controls.Add(stage);
    }

    public void ShowHotkeyMenu(Control owner)
    {
        if (!_client.IsConnected || owner.IsDisposed) return;
        _hotkeyMenu.Show(owner, new Point(0, owner.Height + 2));
    }

    public async Task ConnectAsync(string host, int port)
    {
        string password = Profile.GetPassword();
        if (string.IsNullOrWhiteSpace(password))
            throw new InvalidOperationException("Для подключения не задан пароль.");

        _client.AudioPlaybackEnabled = _audioPlaybackEnabled;
        SetStatus("Подключение...");
        await _client.ConnectAsync(host, port, password);
        SafeUi(() =>
        {
            _screen.Focus();
            UiStateChanged?.Invoke(this);
        });
    }

    public void Disconnect()
    {
        ReleasePressedKeys();
        _client.Disconnect();
    }

    public void FocusScreen()
    {
        if (!_disposed) _screen.Focus();
    }

    public bool HandleKeyDown(KeyEventArgs e)
    {
        if (!_client.IsConnected || !_screen.Focused) return false;
        int key = (int)e.KeyCode;
        if (_pressedKeys.Add(key)) _client.SendKey(key, true);
        return true;
    }

    public bool HandleKeyUp(KeyEventArgs e)
    {
        if (!_client.IsConnected) return false;
        int key = (int)e.KeyCode;
        _pressedKeys.Remove(key);
        _client.SendKey(key, false);
        return true;
    }

    private async Task DownloadRemoteClipboardFilesAsync()
    {
        if (_fileTransferBusy || !_client.IsConnected) return;
        _fileTransferBusy = true;
        try
        {
            SetStatus("Получение файлов из буфера...");
            await Task.Delay(650);
            using var cts = new CancellationTokenSource(TimeSpan.FromMinutes(30));
            int count = await FileClipboardTransfer.DownloadRemoteClipboardAsync(Profile.Host, Profile.Port, Profile.GetPassword(), cts.Token);
            SafeUi(() =>
            {
                SetStatus("Подключено");
                if (count == 0)
                    MessageBox.Show(this, "В удалённом буфере обмена нет файлов или Проводник ещё не успел выполнить Ctrl+C.",
                        "Передача файлов", MessageBoxButtons.OK, MessageBoxIcon.Information);
            });
        }
        catch (Exception ex)
        {
            AppLog.Write(ex, "Remote clipboard download");
            SafeUi(() =>
            {
                SetStatus("Подключено");
                MessageBox.Show(this, "Не удалось получить файлы с удалённого компьютера: " + ex.Message,
                    "Передача файлов", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            });
        }
        finally { _fileTransferBusy = false; }
    }

    private async Task UploadClipboardFilesAsync(string[] files)
    {
        if (_fileTransferBusy || !_client.IsConnected) return;
        _fileTransferBusy = true;
        try
        {
            SetStatus("Передача файлов на удалённый компьютер...");
            using var cts = new CancellationTokenSource(TimeSpan.FromMinutes(30));
            await FileClipboardTransfer.UploadLocalClipboardAsync(Profile.Host, Profile.Port, Profile.GetPassword(), files, cts.Token);
            SafeUi(() => SetStatus("Подключено"));
        }
        catch (Exception ex)
        {
            AppLog.Write(ex, "Remote clipboard upload");
            SafeUi(() =>
            {
                SetStatus("Подключено");
                MessageBox.Show(this, "Не удалось передать файлы: " + ex.Message,
                    "Передача файлов", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            });
        }
        finally { _fileTransferBusy = false; }
    }

    public bool ProcessShortcut(Keys keyData)
    {
        if (!_client.IsConnected || !_screen.Focused) return false;

        if (keyData == (Keys.Control | Keys.C))
        {
            if (!_fileTransferBusy)
            {
                // Send an atomic remote Ctrl+C first, then read the remote file clipboard.
                SendCombo((int)Keys.ControlKey, (int)Keys.C);
                _ = DownloadRemoteClipboardFilesAsync();
            }
            return true;
        }

        if (keyData == (Keys.Control | Keys.V))
        {
            string[] files;
            try { files = FileClipboardTransfer.GetLocalClipboardFiles(); }
            catch { files = Array.Empty<string>(); }

            if (files.Length > 0 && !_fileTransferBusy)
                _ = UploadClipboardFilesAsync(files);
            else
                SendCombo((int)Keys.ControlKey, (int)Keys.V);
            return true;
        }

        if (keyData == (Keys.Alt | Keys.Tab))
        {
            SendCombo((int)Keys.Menu, (int)Keys.Tab);
            return true;
        }
        if (keyData == (Keys.Control | Keys.Shift | Keys.Escape))
        {
            SendCombo((int)Keys.ControlKey, (int)Keys.ShiftKey, (int)Keys.Escape);
            return true;
        }
        return false;
    }

    private void SetStatus(string text)
    {
        _displayStatus = text.StartsWith("Подключено", StringComparison.OrdinalIgnoreCase) ? "Подключено" : text;
        UiStateChanged?.Invoke(this);
    }

    private void OnDisconnected()
    {
        if (_disposed) return;
        ReleasePressedKeys();
        SetStatus("Не подключено");
        _screen.Cursor = Cursors.Default;
        VideoFrame? pending = Interlocked.Exchange(ref _latestFrame, null);
        pending?.Dispose();
        Image? old = _screen.Image;
        _screen.Image = null;
        old?.Dispose();
    }

    private void OnFrameReceived(VideoFrame frame)
    {
        if (_disposed || !_client.IsConnected)
        {
            frame.Dispose();
            return;
        }
        VideoFrame? old = Interlocked.Exchange(ref _latestFrame, frame);
        old?.Dispose();
    }

    private void StartDecodeLatestFrame()
    {
        if (_disposed || !_client.IsConnected || !Visible) return;
        if (Interlocked.CompareExchange(ref _decodeBusy, 1, 0) != 0) return;

        VideoFrame? frame = Interlocked.Exchange(ref _latestFrame, null);
        if (frame == null)
        {
            Interlocked.Exchange(ref _decodeBusy, 0);
            return;
        }

        _ = Task.Run(() =>
        {
            Bitmap? bmp = null;
            try
            {
                using var ms = new MemoryStream(frame.Buffer, frame.JpegOffset, frame.JpegLength, writable: false, publiclyVisible: true);
                using var temp = Image.FromStream(ms, false, false);
                bmp = new Bitmap(temp);
            }
            catch (Exception ex) { AppLog.Write(ex, "Session JPEG decode"); }
            finally { frame.Dispose(); }

            if (_disposed)
            {
                bmp?.Dispose();
                Interlocked.Exchange(ref _decodeBusy, 0);
                return;
            }

            try
            {
                BeginInvoke(new Action(() =>
                {
                    try
                    {
                        if (bmp != null && _client.IsConnected && Visible)
                        {
                            Image? old = _screen.Image;
                            _screen.Image = bmp;
                            old?.Dispose();
                        }
                        else bmp?.Dispose();
                    }
                    finally { Interlocked.Exchange(ref _decodeBusy, 0); }
                }));
            }
            catch
            {
                bmp?.Dispose();
                Interlocked.Exchange(ref _decodeBusy, 0);
            }
        });
    }

    private void BuildHotkeyMenu()
    {
        _hotkeyMenu.BackColor = UiTheme.Surface2;
        _hotkeyMenu.ForeColor = UiTheme.Text;
        _hotkeyMenu.ShowImageMargin = false;
        _hotkeyMenu.ShowCheckMargin = false;
        _hotkeyMenu.Padding = new Padding(2);
        _hotkeyMenu.Renderer = new ToolStripProfessionalRenderer(new DarkMenuColorTable());
        _hotkeyMenu.Items.Add("Alt + Tab", null, (_, _) => SendCombo((int)Keys.Menu, (int)Keys.Tab));
        _hotkeyMenu.Items.Add("Ctrl + Shift + Esc", null, (_, _) => SendCombo((int)Keys.ControlKey, (int)Keys.ShiftKey, (int)Keys.Escape));
        _hotkeyMenu.Items.Add("Win + R", null, (_, _) => SendCombo((int)Keys.LWin, (int)Keys.R));
        _hotkeyMenu.Items.Add("Ctrl + Alt + End", null, (_, _) => SendCombo((int)Keys.ControlKey, (int)Keys.Menu, (int)Keys.End));
        _hotkeyMenu.Items.Add(new ToolStripSeparator());
        _hotkeyMenu.Items.Add("Ctrl + Alt + Del", null, (_, _) => MessageBox.Show(this,
            "Ctrl+Alt+Del является защищённой последовательностью Windows и не передаётся обычным приложением через SendInput.",
            "Системная комбинация", MessageBoxButtons.OK, MessageBoxIcon.Information));
        foreach (ToolStripItem item in _hotkeyMenu.Items)
        {
            if (item is ToolStripSeparator) continue;
            item.Padding = new Padding(10, 5, 12, 5);
            item.Margin = new Padding(0, 1, 0, 1);
        }
    }

    private void SendCombo(params int[] keys)
    {
        if (!_client.IsConnected) return;
        _client.SendKeyCombination(keys);
        _screen.Focus();
    }

    private bool TryMapToRemote(Point p, out int x, out int y)
    {
        x = y = 0;
        int rw = _client.RemoteWidth;
        int rh = _client.RemoteHeight;
        if (rw <= 0 || rh <= 0 || _screen.ClientSize.Width <= 0 || _screen.ClientSize.Height <= 0) return false;

        double scale = Math.Min((double)_screen.ClientSize.Width / rw, (double)_screen.ClientSize.Height / rh);
        double drawW = rw * scale;
        double drawH = rh * scale;
        double offsetX = (_screen.ClientSize.Width - drawW) / 2.0;
        double offsetY = (_screen.ClientSize.Height - drawH) / 2.0;
        if (p.X < offsetX || p.Y < offsetY || p.X >= offsetX + drawW || p.Y >= offsetY + drawH) return false;
        x = Math.Clamp((int)((p.X - offsetX) / scale), 0, rw - 1);
        y = Math.Clamp((int)((p.Y - offsetY) / scale), 0, rh - 1);
        return true;
    }

    private static byte MapButton(MouseButtons button) => button switch
    {
        MouseButtons.Left => 1,
        MouseButtons.Right => 2,
        MouseButtons.Middle => 3,
        _ => 0
    };

    private void ApplyRemoteCursor(byte kind)
    {
        _screen.Cursor = kind switch
        {
            1 => Cursors.IBeam,
            2 => Cursors.WaitCursor,
            3 => Cursors.Cross,
            4 => Cursors.UpArrow,
            5 => Cursors.SizeNWSE,
            6 => Cursors.SizeNESW,
            7 => Cursors.SizeWE,
            8 => Cursors.SizeNS,
            9 => Cursors.SizeAll,
            10 => Cursors.No,
            11 => Cursors.Hand,
            12 => Cursors.AppStarting,
            13 => Cursors.Help,
            _ => Cursors.Default
        };
    }

    private void ReleasePressedKeys()
    {
        if (_pressedKeys.Count == 0) return;
        int[] keys = _pressedKeys.ToArray();
        _pressedKeys.Clear();
        foreach (int key in keys) _client.SendKey(key, false);
    }

    private void SafeUi(Action action)
    {
        if (_disposed || IsDisposed) return;
        try
        {
            if (InvokeRequired) BeginInvoke(action); else action();
        }
        catch { }
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing && !_disposed)
        {
            _disposed = true;
            _frameTimer.Stop();
            _frameTimer.Dispose();
            ReleasePressedKeys();
            _client.Dispose();
            _hotkeyMenu.Dispose();
            VideoFrame? pending = Interlocked.Exchange(ref _latestFrame, null);
            pending?.Dispose();
            Image? image = _screen.Image;
            _screen.Image = null;
            image?.Dispose();
        }
        base.Dispose(disposing);
    }
}
