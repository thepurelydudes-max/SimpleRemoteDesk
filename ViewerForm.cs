namespace RemoteViewer;

internal sealed class ViewerForm : Form
{
    private const int WM_SETREDRAW = 0x000B;
    private const int WS_EX_COMPOSITED = 0x02000000;

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);

    protected override CreateParams CreateParams
    {
        get
        {
            CreateParams cp = base.CreateParams;
            cp.ExStyle |= WS_EX_COMPOSITED;
            return cp;
        }
    }

    private readonly DiscoveryListener _discovery = new();
    private readonly List<ConnectionProfile> _profiles = ViewerSettingsStore.Load();

    // Один Host может быть одновременно виден через LAN, Radmin VPN и другие интерфейсы.
    // Храним все endpoint'ы и каждый раз выбираем самый прямой: физический LAN > private LAN > VPN.
    private readonly Dictionary<string, Dictionary<string, DiscoveredHost>> _endpoints = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, bool> _onlineStates = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, Label> _savedStatusLabels = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, StatusDot> _savedStatusDots = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, Label> _savedAddressLabels = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, Label> _savedKindLabels = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, PictureBox> _savedPictures = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, PremiumPanel> _savedCards = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, Label> _savedNameLabels = new(StringComparer.OrdinalIgnoreCase);
    private readonly HashSet<string> _snapshotRequested = new(StringComparer.OrdinalIgnoreCase);
    private readonly CancellationTokenSource _thumbnailCts = new();

    private readonly Panel _topBar = new()
    {
        Dock = DockStyle.Top,
        Height = 58,
        BackColor = UiTheme.Header
    };
    private readonly FlowLayoutPanel _navStrip = new()
    {
        Dock = DockStyle.Fill,
        BackColor = UiTheme.Header,
        FlowDirection = FlowDirection.LeftToRight,
        WrapContents = false,
        AutoScroll = true,
        Padding = new Padding(18, 0, 12, 0)
    };
    private readonly TableLayoutPanel _sessionActions = new()
    {
        Dock = DockStyle.Right,
        Width = 570,
        Height = 58,
        BackColor = UiTheme.Header,
        ColumnCount = 5,
        RowCount = 1,
        Padding = new Padding(8, 8, 12, 8),
        Margin = Padding.Empty,
        Visible = false
    };
    private readonly Label _sessionStatus = new() { Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleRight };
    private readonly CheckBox _sessionSound = new() { Text = "Звук", Dock = DockStyle.Fill, Checked = true, TextAlign = ContentAlignment.MiddleLeft };
    private readonly Button _sessionHotkeys = new() { Text = "Клавиши ▾", Dock = DockStyle.Fill, Height = 40 };
    private readonly Button _sessionFullscreen = new() { Text = "На весь экран", Dock = DockStyle.Fill, Height = 40 };
    private readonly Button _sessionDisconnect = new() { Text = "Отключиться", Dock = DockStyle.Fill, Height = 40 };
    private readonly Panel _fullscreenOverlay = new() { Width = 240, Height = 50, BackColor = UiTheme.Header, Visible = false };
    private readonly Button _exitFullscreen = new() { Text = "Выйти из полного экрана", Width = 220, Height = 38 };
    private readonly System.Windows.Forms.Timer _fullscreenHideTimer = new() { Interval = 1600 };
    private bool _fullScreen;
    private Rectangle _normalBounds;
    private FormBorderStyle _normalBorderStyle;
    private FormWindowState _normalWindowState;
    private readonly Panel _contentHost = new() { Dock = DockStyle.Fill, BackColor = UiTheme.Bg };
    private readonly Panel _computersPage = new() { Dock = DockStyle.Fill, BackColor = UiTheme.Bg };
    private readonly NavigationTab _computersNav = new("Компьютеры", false);
    private readonly Dictionary<NavigationTab, Control> _views = new();
    private readonly Dictionary<string, (NavigationTab Tab, RemoteSessionPage Page)> _sessions = new(StringComparer.OrdinalIgnoreCase);
    private NavigationTab? _selectedNav;

    private readonly Panel _computerScroll = new() { Dock = DockStyle.Fill, AutoScroll = true, BackColor = UiTheme.Bg };
    private readonly Panel _computerBody = new() { BackColor = UiTheme.Bg };
    private readonly BufferedFlowLayoutPanel _savedFlow = new() { WrapContents = true, AutoScroll = false, BackColor = UiTheme.Bg, Padding = new Padding(0) };
    private readonly BufferedFlowLayoutPanel _discoveredFlow = new() { WrapContents = true, AutoScroll = false, BackColor = UiTheme.Bg, Padding = new Padding(0) };
    private readonly Label _savedHeading = UiTheme.SectionTitle("Мои компьютеры");
    private readonly Label _networkHeading = UiTheme.SectionTitle("Доступные в сети");
    private readonly Label _networkHint = UiTheme.MutedLabel("Локальная сеть и VPN");
    private readonly TextBox _search = new() { Width = 250 };
    private readonly System.Windows.Forms.Timer _presenceTimer = new() { Interval = 2000 };
    private bool _closing;

    public ViewerForm()
    {
        Text = "Simple Remote Viewer";
        Icon = AppBrand.LoadIcon();
        StartPosition = FormStartPosition.CenterScreen;
        WindowState = FormWindowState.Maximized;
        MinimumSize = new Size(1180, 720);
        KeyPreview = true;
        UiTheme.StyleForm(this);
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        DoubleBuffered = true;

        BuildNavigation();
        BuildComputersPage();
        Controls.Add(_contentHost);
        Controls.Add(_topBar);
        Controls.Add(_fullscreenOverlay);
        _fullscreenOverlay.BringToFront();

        BuildSavedCards();
        BuildDiscoveredCards();
        SelectView(_computersNav);

        _discovery.HostSeen += OnHostSeen;
        _presenceTimer.Tick += (_, _) => RefreshPresence();
        _presenceTimer.Start();

        KeyDown += (_, e) =>
        {
            if (ActiveSession()?.HandleKeyDown(e) == true)
            {
                e.Handled = true;
                e.SuppressKeyPress = true;
            }
        };
        KeyUp += (_, e) =>
        {
            if (ActiveSession()?.HandleKeyUp(e) == true)
            {
                e.Handled = true;
                e.SuppressKeyPress = true;
            }
        };

        Shown += (_, _) =>
        {
            try { _discovery.Start(); }
            catch (Exception ex)
            {
                MessageBox.Show(this, "Автообнаружение в сети не запустилось: " + ex.Message,
                    "Обнаружение компьютеров", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        };

        FormClosing += (_, _) =>
        {
            _closing = true;
            _presenceTimer.Stop();
            _presenceTimer.Dispose();
            _fullscreenHideTimer.Stop();
            _fullscreenHideTimer.Dispose();
            _discovery.Dispose();
            try { _thumbnailCts.Cancel(); } catch { }
            foreach (var pair in _sessions.Values.ToArray()) pair.Page.Dispose();
            _sessions.Clear();
            _thumbnailCts.Dispose();
            ViewerSettingsStore.Save(_profiles);
        };
    }

    private void BuildNavigation()
    {
        _computersNav.SelectedRequested += tab => SelectView(tab);
        _navStrip.Controls.Add(_computersNav);
        _views[_computersNav] = _computersPage;
        _contentHost.Controls.Add(_computersPage);

        _sessionStatus.ForeColor = UiTheme.Muted;
        _sessionStatus.Font = new Font("Segoe UI Semibold", 9F, FontStyle.Bold);
        _sessionSound.ForeColor = UiTheme.Text;
        _sessionSound.BackColor = UiTheme.Header;
        UiTheme.StyleButton(_sessionHotkeys);
        UiTheme.StyleButton(_sessionFullscreen);
        UiTheme.StyleButton(_sessionDisconnect, danger: true);

        _sessionActions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 90));
        _sessionActions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 66));
        _sessionActions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 112));
        _sessionActions.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 132));
        _sessionActions.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        _sessionStatus.Margin = new Padding(0, 0, 6, 0);
        _sessionSound.Margin = new Padding(6, 0, 6, 0);
        _sessionHotkeys.Margin = new Padding(4, 1, 4, 1);
        _sessionFullscreen.Margin = new Padding(4, 1, 4, 1);
        _sessionDisconnect.Margin = new Padding(4, 1, 0, 1);
        _sessionActions.Controls.Add(_sessionStatus, 0, 0);
        _sessionActions.Controls.Add(_sessionSound, 1, 0);
        _sessionActions.Controls.Add(_sessionHotkeys, 2, 0);
        _sessionActions.Controls.Add(_sessionFullscreen, 3, 0);
        _sessionActions.Controls.Add(_sessionDisconnect, 4, 0);

        _sessionSound.CheckedChanged += (_, _) =>
        {
            RemoteSessionPage? session = ActiveSession();
            if (session != null) session.AudioPlaybackEnabled = _sessionSound.Checked;
        };
        _sessionHotkeys.Click += (_, _) => ActiveSession()?.ShowHotkeyMenu(_sessionHotkeys);
        _sessionDisconnect.Click += (_, _) => ActiveSession()?.Disconnect();
        _sessionFullscreen.Click += (_, _) => ToggleFullScreen();

        _exitFullscreen.Location = new Point(10, 6);
        UiTheme.StyleButton(_exitFullscreen);
        _exitFullscreen.Click += (_, _) => ToggleFullScreen();
        _fullscreenOverlay.Controls.Add(_exitFullscreen);
        _fullscreenOverlay.Resize += (_, _) => _exitFullscreen.Left = Math.Max(10, (_fullscreenOverlay.ClientSize.Width - _exitFullscreen.Width) / 2);
        _fullscreenOverlay.MouseEnter += (_, _) => _fullscreenHideTimer.Stop();
        _fullscreenOverlay.MouseLeave += (_, _) => { if (_fullScreen) _fullscreenHideTimer.Start(); };
        _fullscreenHideTimer.Tick += (_, _) =>
        {
            _fullscreenHideTimer.Stop();
            if (_fullScreen) _fullscreenOverlay.Visible = false;
        };

        _topBar.Controls.Add(_navStrip);
        _topBar.Controls.Add(_sessionActions);
    }

    private void BuildComputersPage()
    {
        var header = new Panel { Dock = DockStyle.Top, Height = 112, BackColor = UiTheme.Bg, Padding = new Padding(32, 20, 32, 12) };
        var logo = AppBrand.CreateLogo(50);
        logo.Location = new Point(32, 25);
        header.Controls.Add(logo);

        var title = UiTheme.Title("Simple Remote Viewer", 18F);
        title.Location = new Point(98, 22);
        header.Controls.Add(title);
        var subtitle = UiTheme.MutedLabel("Удалённый доступ к вашим компьютерам");
        subtitle.Location = new Point(99, 56);
        header.Controls.Add(subtitle);

        var add = new Button { Text = "+  Добавить компьютер", Width = 214, Height = 42, Anchor = AnchorStyles.Top | AnchorStyles.Right };
        UiTheme.StyleButton(add, primary: true);
        add.Click += (_, _) => AddManualProfile();
        header.Controls.Add(add);

        var searchWrap = new PremiumPanel
        {
            Width = 276,
            Height = 42,
            Radius = 10,
            PanelColor = UiTheme.Surface,
            BorderColor = UiTheme.Border,
            Anchor = AnchorStyles.Top | AnchorStyles.Right
        };
        var searchIcon = new Label
        {
            Text = "⌕",
            ForeColor = UiTheme.Muted,
            Font = new Font("Segoe UI Symbol", 15F),
            Width = 30,
            Height = 36,
            TextAlign = ContentAlignment.MiddleCenter,
            Location = new Point(8, 2)
        };
        _search.BorderStyle = BorderStyle.None;
        _search.BackColor = UiTheme.Surface;
        _search.ForeColor = UiTheme.Text;
        _search.Font = new Font("Segoe UI", 10F);
        _search.PlaceholderText = "Поиск компьютеров...";
        _search.Location = new Point(40, 11);
        _search.Width = 222;
        _search.TextChanged += (_, _) => { BuildSavedCards(); BuildDiscoveredCards(); };
        searchWrap.Controls.Add(searchIcon);
        searchWrap.Controls.Add(_search);
        header.Controls.Add(searchWrap);

        header.Resize += (_, _) =>
        {
            add.Location = new Point(header.ClientSize.Width - add.Width - 32, 28);
            searchWrap.Location = new Point(add.Left - searchWrap.Width - 12, 28);
        };

        _networkHeading.AutoSize = false;
        _networkHeading.Size = new Size(160, 28);
        _networkHeading.TextAlign = ContentAlignment.MiddleLeft;
        _networkHint.AutoSize = false;
        _networkHint.Size = new Size(210, 28);
        _networkHint.TextAlign = ContentAlignment.MiddleLeft;

        _computerBody.Controls.Add(_savedHeading);
        _computerBody.Controls.Add(_savedFlow);
        _computerBody.Controls.Add(_networkHeading);
        _computerBody.Controls.Add(_networkHint);
        _computerBody.Controls.Add(_discoveredFlow);
        _computerScroll.Controls.Add(_computerBody);
        _computerScroll.ClientSizeChanged += (_, _) => LayoutComputerContent();

        _computersPage.Controls.Add(_computerScroll);
        _computersPage.Controls.Add(header);
    }

    private void LayoutComputerContent()
    {
        if (_computerScroll.IsDisposed) return;
        int width = Math.Max(860, _computerScroll.ClientSize.Width - 2);
        _computerBody.Location = Point.Empty;
        _computerBody.Width = width;

        int left = 32;
        int innerWidth = Math.Max(700, width - 64);
        int y = 18;

        _savedHeading.Location = new Point(left, y);
        y += 48;
        _savedFlow.Location = new Point(left - 8, y);
        _savedFlow.Width = innerWidth + 16;

        int savedHeight = LayoutSavedCards(innerWidth + 16);
        _savedFlow.Height = savedHeight;
        y += savedHeight + 34;

        _networkHeading.Location = new Point(left, y);
        _networkHint.Location = new Point(left + 165, y);
        y += 50;
        _discoveredFlow.Location = new Point(left - 8, y);
        _discoveredFlow.Width = innerWidth + 16;
        _discoveredFlow.Height = CalculateDiscoveredHeight(_discoveredFlow);
        y += _discoveredFlow.Height + 64;

        _computerBody.Height = Math.Max(y, _computerScroll.ClientSize.Height);
    }

    private int LayoutSavedCards(int availableWidth)
    {
        Control[] cards = _savedFlow.Controls.Cast<Control>()
            .Where(c => !string.Equals(c.Tag as string, "empty", StringComparison.Ordinal))
            .ToArray();
        if (cards.Length == 0) return 86;

        const int marginEachSide = 8;
        const int minCardWidth = 246;
        int possibleColumns = Math.Max(1, availableWidth / (minCardWidth + marginEachSide * 2));
        int columns = Math.Min(5, possibleColumns);
        int cardWidth = Math.Max(minCardWidth, availableWidth / columns - marginEachSide * 2);

        int maxHeight = 0;
        _savedFlow.SuspendLayout();
        try
        {
            foreach (Control card in cards)
            {
                card.Width = cardWidth;
                maxHeight = Math.Max(maxHeight, card.Height + card.Margin.Vertical);
            }
        }
        finally { _savedFlow.ResumeLayout(true); }

        int rows = (int)Math.Ceiling(cards.Length / (double)columns);
        return Math.Max(86, rows * Math.Max(1, maxHeight) + 4);
    }

    private static int CalculateDiscoveredHeight(FlowLayoutPanel flow)
    {
        if (flow.Controls.Count == 0) return 86;
        if (flow.Controls.Count == 1 && string.Equals(flow.Controls[0].Tag as string, "empty", StringComparison.Ordinal)) return 86;
        int total = 4;
        foreach (Control c in flow.Controls)
            total += c.Height + c.Margin.Vertical;
        return Math.Max(86, total);
    }

    private void RunComputerUiBatch(Action action)
    {
        bool redrawSuspended = _computerScroll.IsHandleCreated;
        if (redrawSuspended) SendMessage(_computerScroll.Handle, WM_SETREDRAW, IntPtr.Zero, IntPtr.Zero);
        _computerScroll.SuspendLayout();
        _computerBody.SuspendLayout();
        _savedFlow.SuspendLayout();
        _discoveredFlow.SuspendLayout();
        try
        {
            action();
        }
        finally
        {
            _discoveredFlow.ResumeLayout(false);
            _savedFlow.ResumeLayout(false);
            _computerBody.ResumeLayout(false);
            _computerScroll.ResumeLayout(false);
            if (redrawSuspended) SendMessage(_computerScroll.Handle, WM_SETREDRAW, new IntPtr(1), IntPtr.Zero);
            _computerScroll.Invalidate(true);
            _computerScroll.Update();
        }
    }

    private void ResetSavedCardVisualStates()
    {
        foreach (PremiumPanel card in _savedCards.Values.ToArray())
        {
            if (card.IsDisposed) continue;
            card.PanelColor = UiTheme.Surface;
            card.BorderColor = UiTheme.BorderSoft;
            card.Invalidate(false);
        }
    }

    private void AddManualProfile()
    {
        var profile = new ConnectionProfile { Name = "Новый компьютер" };
        if (!ProfileDialog.Edit(this, profile)) return;
        _profiles.Add(profile);
        ViewerSettingsStore.Save(_profiles);
        RunComputerUiBatch(() => AddSavedCardIncremental(profile));
    }

    private void AddDiscoveredProfile(DiscoveredHost host)
    {
        if (_profiles.Any(p => string.Equals(p.HostId, host.HostId, StringComparison.OrdinalIgnoreCase))) return;
        var profile = new ConnectionProfile
        {
            HostId = host.HostId,
            Name = host.Name,
            Host = host.Address,
            Port = host.Port
        };
        if (!ProfileDialog.Edit(this, profile, host.Name, host.Address, host.Port)) return;
        _profiles.Add(profile);
        ViewerSettingsStore.Save(_profiles);
        RunComputerUiBatch(() =>
        {
            AddSavedCardIncremental(profile);
            RemoveDiscoveredHostCard(host.HostId);
        });
        RequestSnapshotOnce(profile, host.Address, host.Port);
    }

    private void EditProfile(ConnectionProfile profile)
    {
        string oldHost = profile.Host;
        int oldPort = profile.Port;
        if (!ProfileDialog.Edit(this, profile)) return;
        if (!string.Equals(oldHost, profile.Host, StringComparison.OrdinalIgnoreCase) || oldPort != profile.Port)
            profile.HostId = string.Empty;
        ViewerSettingsStore.Save(_profiles);

        RunComputerUiBatch(() =>
        {
            if (string.IsNullOrWhiteSpace(_search.Text))
            {
                UpdateSavedCard(profile);
                UpdateSavedStatuses();
                LayoutComputerContent();
            }
            else
            {
                BuildSavedCards();
            }
        });
    }

    private void DeleteProfile(ConnectionProfile profile)
    {
        if (MessageBox.Show(this, $"Удалить «{profile.Name}» из списка?", "Удаление компьютера",
            MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return;

        if (_sessions.ContainsKey(profile.Id)) CloseSession(profile.Id);
        _profiles.Remove(profile);
        ViewerSettingsStore.DeleteThumbnail(profile.Id);
        ViewerSettingsStore.Save(_profiles);

        if (_savedCards.TryGetValue(profile.Id, out PremiumPanel? card))
        {
            _savedFlow.SuspendLayout();
            try
            {
                _savedFlow.Controls.Remove(card);
                card.Dispose();
                _savedCards.Remove(profile.Id);
                _savedNameLabels.Remove(profile.Id);
                _savedStatusLabels.Remove(profile.Id);
                _savedStatusDots.Remove(profile.Id);
                _savedAddressLabels.Remove(profile.Id);
                _savedKindLabels.Remove(profile.Id);
                _savedPictures.Remove(profile.Id);
            }
            finally { _savedFlow.ResumeLayout(false); }

            if (_savedFlow.Controls.Count == 0)
                _savedFlow.Controls.Add(CreateEmptyState("Добавьте первый компьютер — он появится здесь.", 760));
            LayoutComputerContent();
        }
        else
        {
            BuildSavedCards();
        }

        if (!string.IsNullOrWhiteSpace(profile.HostId))
            AddDiscoveredHostCardIfOnline(profile.HostId);
    }

    private void AddSavedCardIncremental(ConnectionProfile profile)
    {
        if (!string.IsNullOrWhiteSpace(_search.Text))
        {
            BuildSavedCards();
            return;
        }

        _savedFlow.SuspendLayout();
        try
        {
            foreach (Control c in _savedFlow.Controls.Cast<Control>().Where(c => string.Equals(c.Tag as string, "empty", StringComparison.Ordinal)).ToArray())
            {
                _savedFlow.Controls.Remove(c);
                c.Dispose();
            }
            Control card = CreateSavedCard(profile);
            _savedFlow.Controls.Add(card);
        }
        finally { _savedFlow.ResumeLayout(false); }
        UpdateSavedStatuses();
        LayoutComputerContent();
    }

    private void RemoveDiscoveredHostCard(string hostId)
    {
        Control? row = _discoveredFlow.Controls.Cast<Control>()
            .FirstOrDefault(c => string.Equals(c.Tag as string, hostId, StringComparison.OrdinalIgnoreCase));
        if (row != null)
        {
            _discoveredFlow.Controls.Remove(row);
            row.Dispose();
        }
        if (_discoveredFlow.Controls.Count == 0)
            _discoveredFlow.Controls.Add(CreateEmptyState("Новые Host в локальной сети или Radmin VPN появятся здесь автоматически.", 760));
        LayoutComputerContent();
    }

    private void AddDiscoveredHostCardIfOnline(string hostId)
    {
        if (_profiles.Any(p => string.Equals(p.HostId, hostId, StringComparison.OrdinalIgnoreCase))) return;
        DateTime cutoff = DateTime.UtcNow.AddSeconds(-7);
        List<DiscoveredHost> endpoints = OnlineEndpoints(hostId, cutoff);
        if (endpoints.Count == 0) return;
        if (_discoveredFlow.Controls.Cast<Control>().Any(c => string.Equals(c.Tag as string, hostId, StringComparison.OrdinalIgnoreCase))) return;

        foreach (Control c in _discoveredFlow.Controls.Cast<Control>().Where(c => string.Equals(c.Tag as string, "empty", StringComparison.Ordinal)).ToArray())
        {
            _discoveredFlow.Controls.Remove(c);
            c.Dispose();
        }
        _discoveredFlow.Controls.Add(CreateDiscoveredRow(endpoints));
        LayoutComputerContent();
    }

    private void UpdateSavedCard(ConnectionProfile profile)
    {
        if (_savedNameLabels.TryGetValue(profile.Id, out Label? name))
            name.Text = profile.Name;
        if (_savedAddressLabels.TryGetValue(profile.Id, out Label? address))
            address.Text = $"{profile.Host}:{profile.Port}";
        UpdateProfileEndpointLabels(profile);
        if (_savedCards.TryGetValue(profile.Id, out PremiumPanel? card))
            card.Invalidate(false);
    }

    private void BuildSavedCards()
    {
        DisposeChildren(_savedFlow);
        _savedStatusLabels.Clear();
        _savedStatusDots.Clear();
        _savedAddressLabels.Clear();
        _savedKindLabels.Clear();
        _savedPictures.Clear();
        _savedCards.Clear();
        _savedNameLabels.Clear();

        string filter = _search.Text.Trim();
        IEnumerable<ConnectionProfile> profiles = _profiles.OrderBy(p => p.Name, StringComparer.CurrentCultureIgnoreCase);
        if (!string.IsNullOrWhiteSpace(filter))
            profiles = profiles.Where(p => p.Name.Contains(filter, StringComparison.CurrentCultureIgnoreCase) ||
                                           p.Host.Contains(filter, StringComparison.OrdinalIgnoreCase));

        foreach (ConnectionProfile profile in profiles)
            _savedFlow.Controls.Add(CreateSavedCard(profile));

        if (_savedFlow.Controls.Count == 0)
            _savedFlow.Controls.Add(CreateEmptyState(_profiles.Count == 0 ? "Добавьте первый компьютер — он появится здесь." : "По запросу ничего не найдено.", 760));

        UpdateSavedStatuses();
        LayoutComputerContent();
    }

    private Control CreateSavedCard(ConnectionProfile profile)
    {
        const int defaultW = 300;
        const int side = 12;

        var card = new PremiumPanel
        {
            Width = defaultW,
            Height = 246,
            Margin = new Padding(8),
            Radius = 16,
            PanelColor = UiTheme.Surface,
            BorderColor = UiTheme.BorderSoft,
            Cursor = Cursors.Hand
        };

        var pictureFrame = new PremiumPanel
        {
            Left = side, Top = side,
            Radius = 11, PanelColor = Color.FromArgb(8, 16, 28), BorderColor = UiTheme.BorderSoft,
            Cursor = Cursors.Hand
        };
        var picture = new PictureBox
        {
            Dock = DockStyle.Fill,
            Margin = Padding.Empty,
            SizeMode = PictureBoxSizeMode.Zoom,
            BackColor = Color.FromArgb(8, 16, 28),
            Image = LoadThumbnailOrPlaceholder(profile.Id, profile.Name),
            Cursor = Cursors.Hand
        };
        pictureFrame.Padding = new Padding(1);
        pictureFrame.Controls.Add(picture);

        var name = new Label
        {
            Text = profile.Name,
            ForeColor = UiTheme.Text,
            BackColor = Color.Transparent,
            Font = new Font("Segoe UI Semibold", 9.6F, FontStyle.Bold),
            AutoEllipsis = true,
            TextAlign = ContentAlignment.MiddleLeft,
            Cursor = Cursors.Hand
        };
        var dot = new StatusDot { DotColor = UiTheme.Offline };
        var status = new Label
        {
            BackColor = Color.Transparent,
            TextAlign = ContentAlignment.MiddleLeft,
            Font = new Font("Segoe UI Semibold", 8.7F, FontStyle.Bold),
            Cursor = Cursors.Hand
        };
        var address = new Label
        {
            Text = $"{profile.Host}:{profile.Port}", ForeColor = UiTheme.Muted,
            BackColor = Color.Transparent,
            AutoEllipsis = true, TextAlign = ContentAlignment.MiddleLeft,
            Font = new Font("Segoe UI", 8.25F), Cursor = Cursors.Hand
        };
        var kind = new Label
        {
            Text = EndpointPriority.Kind(profile.Host), ForeColor = UiTheme.Muted2,
            BackColor = Color.Transparent,
            AutoEllipsis = true, TextAlign = ContentAlignment.MiddleRight,
            Font = new Font("Segoe UI", 8.2F), Cursor = Cursors.Hand
        };

        var menu = new ContextMenuStrip
        {
            BackColor = UiTheme.Surface2,
            ForeColor = UiTheme.Text,
            ShowImageMargin = false,
            ShowCheckMargin = false,
            Padding = new Padding(2),
            Renderer = new ToolStripProfessionalRenderer(new DarkMenuColorTable())
        };
        var connectItem = menu.Items.Add("Подключиться");
        var editItem = menu.Items.Add("Изменить");
        menu.Items.Add(new ToolStripSeparator());
        var deleteItem = menu.Items.Add("Удалить");
        foreach (ToolStripItem item in menu.Items)
        {
            if (item is ToolStripSeparator) continue;
            item.ForeColor = UiTheme.Text;
            item.Padding = new Padding(12, 6, 14, 6);
            item.Margin = new Padding(0, 1, 0, 1);
        }
        connectItem.Click += async (_, _) => { SetHovered(false); await ConnectProfileAsync(profile); };
        editItem.Click += (_, _) => { SetHovered(false); EditProfile(profile); };
        deleteItem.Click += (_, _) => { SetHovered(false); DeleteProfile(profile); };
        menu.Opening += (_, _) => SetHovered(false);
        menu.Closed += (_, _) =>
        {
            try { BeginInvoke(new Action(() => SetHovered(false))); } catch { }
        };
        card.Disposed += (_, _) => menu.Dispose();

        Rectangle EllipsisHitRect() => new(Math.Max(0, picture.ClientSize.Width - 34), 4, 32, 38);
        picture.Paint += (_, e) =>
        {
            e.Graphics.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
            using var brush = new SolidBrush(Color.FromArgb(248, 251, 255));
            float x = picture.ClientSize.Width - 15F;
            foreach (float y in new[] { 12F, 19F, 26F })
                e.Graphics.FillEllipse(brush, x - 2.15F, y - 2.15F, 4.3F, 4.3F);
        };
        picture.MouseClick += async (_, e) =>
        {
            if (EllipsisHitRect().Contains(e.Location))
            {
                SetHovered(false);
                menu.Show(picture, new Point(Math.Max(0, picture.Width - 8), 34));
                return;
            }
            SetHovered(false);
            await ConnectProfileAsync(profile);
        };
        picture.MouseMove += (_, e) =>
        {
            picture.Cursor = EllipsisHitRect().Contains(e.Location) ? Cursors.Hand : Cursors.Hand;
        };

        async void ConnectFromCard(object? _, EventArgs __) { SetHovered(false); await ConnectProfileAsync(profile); }
        foreach (Control c in new Control[] { card, pictureFrame, name, dot, status, address, kind })
            c.Click += ConnectFromCard;

        void UpdatePictureClip()
        {
            if (picture.Width <= 2 || picture.Height <= 2) return;
            using var path = UiTheme.RoundedRect(new Rectangle(0, 0, picture.Width, picture.Height), 10);
            Region? old = picture.Region;
            picture.Region = new Region(path);
            old?.Dispose();
        }

        void LayoutCard()
        {
            if (card.IsDisposed) return;
            int pictureW = Math.Max(210, card.ClientSize.Width - side * 2);
            int pictureH = (int)Math.Round(pictureW * 9.0 / 16.0);
            pictureFrame.SetBounds(side, side, pictureW, pictureH);
            UpdatePictureClip();

            int infoTop = side + pictureH + 7;
            const int statusArea = 92;
            name.SetBounds(14, infoTop, Math.Max(70, card.ClientSize.Width - 28 - statusArea), 23);
            dot.SetBounds(card.ClientSize.Width - 87, infoTop + 6, 12, 12);
            status.SetBounds(card.ClientSize.Width - 73, infoTop, 66, 23);

            int secondTop = infoTop + 25;
            address.SetBounds(14, secondTop, Math.Max(90, card.ClientSize.Width - 126), 20);
            kind.SetBounds(card.ClientSize.Width - 108, secondTop, 96, 20);

            card.Height = secondTop + 32;
            picture.Invalidate();
        }

        void SetHovered(bool hovered)
        {
            if (card.IsDisposed) return;
            card.PanelColor = hovered ? UiTheme.Surface2 : UiTheme.Surface;
            card.BorderColor = hovered ? UiTheme.Accent : UiTheme.BorderSoft;
            card.Invalidate(false);
        }

        void WireHover(Control root)
        {
            root.MouseEnter += (_, _) => SetHovered(true);
            root.MouseLeave += (_, _) =>
            {
                try
                {
                    Point p = card.PointToClient(Cursor.Position);
                    if (!card.ClientRectangle.Contains(p)) SetHovered(false);
                }
                catch { }
            };
            foreach (Control child in root.Controls) WireHover(child);
        }

        card.Controls.AddRange(new Control[] { pictureFrame, name, dot, status, address, kind });
        card.Resize += (_, _) => LayoutCard();
        picture.Resize += (_, _) => UpdatePictureClip();
        LayoutCard();
        WireHover(card);

        _savedCards[profile.Id] = card;
        _savedNameLabels[profile.Id] = name;
        _savedStatusLabels[profile.Id] = status;
        _savedStatusDots[profile.Id] = dot;
        _savedAddressLabels[profile.Id] = address;
        _savedKindLabels[profile.Id] = kind;
        _savedPictures[profile.Id] = picture;
        return card;
    }

    private void BuildDiscoveredCards()
    {
        DisposeChildren(_discoveredFlow);
        DateTime cutoff = DateTime.UtcNow.AddSeconds(-7);
        var savedIds = _profiles.Where(p => !string.IsNullOrWhiteSpace(p.HostId))
            .Select(p => p.HostId).ToHashSet(StringComparer.OrdinalIgnoreCase);
        string filter = _search.Text.Trim();

        var hosts = _endpoints.Keys
            .Where(id => !savedIds.Contains(id))
            .Select(id => new
            {
                HostId = id,
                Endpoints = OnlineEndpoints(id, cutoff)
            })
            .Where(x => x.Endpoints.Count > 0)
            .OrderBy(x => x.Endpoints[0].Name, StringComparer.CurrentCultureIgnoreCase)
            .ToList();

        if (!string.IsNullOrWhiteSpace(filter))
        {
            hosts = hosts.Where(x =>
                x.Endpoints[0].Name.Contains(filter, StringComparison.CurrentCultureIgnoreCase) ||
                x.Endpoints.Any(h => h.Address.Contains(filter, StringComparison.OrdinalIgnoreCase) ||
                                     EndpointPriority.Kind(h.Address).Contains(filter, StringComparison.CurrentCultureIgnoreCase)))
                .ToList();
        }

        foreach (var host in hosts)
            _discoveredFlow.Controls.Add(CreateDiscoveredRow(host.Endpoints));

        if (_discoveredFlow.Controls.Count == 0)
            _discoveredFlow.Controls.Add(CreateEmptyState("Новые Host в локальной сети или Radmin VPN появятся здесь автоматически.", 760));

        LayoutComputerContent();
    }

    private Control CreateDiscoveredRow(IReadOnlyList<DiscoveredHost> endpoints)
    {
        DiscoveredHost preferred = endpoints[0];
        DiscoveredHost? lan = endpoints.FirstOrDefault(h => string.Equals(EndpointPriority.Kind(h.Address), "LAN", StringComparison.CurrentCultureIgnoreCase));
        DiscoveredHost? radmin = endpoints.FirstOrDefault(h => string.Equals(EndpointPriority.Kind(h.Address), "Radmin VPN", StringComparison.CurrentCultureIgnoreCase));

        var card = new PremiumPanel
        {
            Width = 620, Height = 116, Margin = new Padding(8), Radius = 14,
            PanelColor = UiTheme.Surface, BorderColor = UiTheme.BorderSoft,
            Tag = preferred.HostId
        };

        var icon = new PictureBox
        {
            Left = 18, Top = 37, Width = 42, Height = 42,
            SizeMode = PictureBoxSizeMode.Zoom,
            Image = CreateSmallMonitorIcon()
        };
        var name = new Label
        {
            Left = 76, Top = 12, Width = 310, Height = 24, Text = preferred.Name,
            ForeColor = UiTheme.Text, BackColor = Color.Transparent,
            Font = new Font("Segoe UI Semibold", 10F, FontStyle.Bold), AutoEllipsis = true,
            TextAlign = ContentAlignment.MiddleLeft
        };

        var lanKind = new Label
        {
            Left = 76, Top = 41, Width = 82, Height = 22, Text = "LAN",
            ForeColor = lan != null ? UiTheme.Muted : UiTheme.Muted2, BackColor = Color.Transparent,
            TextAlign = ContentAlignment.MiddleLeft, Font = new Font("Segoe UI", 8.8F)
        };
        var lanAddress = new Label
        {
            Left = 158, Top = 41, Width = 230, Height = 22,
            Text = lan != null ? $"{lan.Address}:{lan.Port}" : "—",
            ForeColor = lan != null ? UiTheme.Muted : UiTheme.Muted2, BackColor = Color.Transparent,
            AutoEllipsis = true, TextAlign = ContentAlignment.MiddleLeft, Font = new Font("Segoe UI", 8.8F)
        };
        var vpnKind = new Label
        {
            Left = 76, Top = 66, Width = 82, Height = 22, Text = "Radmin VPN",
            ForeColor = radmin != null ? UiTheme.Muted : UiTheme.Muted2, BackColor = Color.Transparent,
            TextAlign = ContentAlignment.MiddleLeft, Font = new Font("Segoe UI", 8.8F)
        };
        var vpnAddress = new Label
        {
            Left = 158, Top = 66, Width = 230, Height = 22,
            Text = radmin != null ? $"{radmin.Address}:{radmin.Port}" : "—",
            ForeColor = radmin != null ? UiTheme.Muted : UiTheme.Muted2, BackColor = Color.Transparent,
            AutoEllipsis = true, TextAlign = ContentAlignment.MiddleLeft, Font = new Font("Segoe UI", 8.8F)
        };

        int lanTextRight = 158 + TextRenderer.MeasureText(lanAddress.Text, lanAddress.Font, Size.Empty, TextFormatFlags.NoPadding).Width;
        int vpnTextRight = 158 + TextRenderer.MeasureText(vpnAddress.Text, vpnAddress.Font, Size.Empty, TextFormatFlags.NoPadding).Width;
        int actionLeft = Math.Clamp(Math.Max(lanTextRight, vpnTextRight) + 14, 300, 386);

        var dot = new StatusDot { Left = actionLeft, Top = 22, DotColor = UiTheme.Success };
        var online = new Label
        {
            Left = actionLeft + 14, Top = 16, Width = 66, Height = 24, Text = "Онлайн",
            ForeColor = UiTheme.Success, BackColor = Color.Transparent,
            Font = new Font("Segoe UI Semibold", 8.7F, FontStyle.Bold),
            TextAlign = ContentAlignment.MiddleLeft
        };
        var add = new Button { Left = actionLeft, Top = 52, Width = 164, Height = 42, Text = "+  Добавить" };
        UiTheme.StyleButton(add, primary: true);
        add.Click += (_, _) => AddDiscoveredProfile(preferred);

        card.Controls.AddRange(new Control[] { icon, name, lanKind, lanAddress, vpnKind, vpnAddress, dot, online, add });
        return card;
    }

    private static Control CreateEmptyState(string text, int width)
    {
        return new Label
        {
            Width = width,
            Height = 64,
            Margin = new Padding(10),
            Text = text,
            ForeColor = UiTheme.Muted2,
            Font = new Font("Segoe UI", 9.5F),
            TextAlign = ContentAlignment.MiddleLeft,
            Padding = new Padding(8, 0, 0, 0),
            Tag = "empty"
        };
    }

    private void OnHostSeen(DiscoveredHost incoming)
    {
        SafeUi(() =>
        {
            DateTime cutoff = DateTime.UtcNow.AddSeconds(-7);
            bool hostWasOnline = BestEndpoint(incoming.HostId, cutoff) != null;
            string? oldBest = BestEndpoint(incoming.HostId, cutoff)?.Address;

            if (!_endpoints.TryGetValue(incoming.HostId, out Dictionary<string, DiscoveredHost>? byAddress))
            {
                byAddress = new Dictionary<string, DiscoveredHost>(StringComparer.OrdinalIgnoreCase);
                _endpoints[incoming.HostId] = byAddress;
            }
            bool isNewEndpoint = !byAddress.ContainsKey(incoming.Address);
            byAddress[incoming.Address] = incoming;

            DiscoveredHost? best = BestEndpoint(incoming.HostId, cutoff);
            if (best == null) return;
            bool bestChanged = !string.Equals(oldBest, best.Address, StringComparison.OrdinalIgnoreCase);

            ConnectionProfile? profile = _profiles.FirstOrDefault(p => string.Equals(p.HostId, incoming.HostId, StringComparison.OrdinalIgnoreCase));
            profile ??= _profiles.FirstOrDefault(p => string.IsNullOrWhiteSpace(p.HostId) && p.Port == incoming.Port &&
                string.Equals(p.Host, incoming.Address, StringComparison.OrdinalIgnoreCase));

            if (profile != null)
            {
                bool changed = false;
                if (string.IsNullOrWhiteSpace(profile.HostId))
                {
                    profile.HostId = incoming.HostId;
                    changed = true;
                }

                // Важное правило: если появился физический LAN endpoint, переключаем профиль на него
                // даже если Radmin VPN уже был Online. При исчезновении LAN автоматически вернёмся к VPN.
                if (!string.Equals(profile.Host, best.Address, StringComparison.OrdinalIgnoreCase) || profile.Port != best.Port)
                {
                    profile.Host = best.Address;
                    profile.Port = best.Port;
                    changed = true;
                }
                if (changed) ViewerSettingsStore.Save(_profiles);
                UpdateProfileEndpointLabels(profile);
            }

            UpdateSavedStatuses();
            if (!hostWasOnline || bestChanged || isNewEndpoint) BuildDiscoveredCards();
        });
    }

    private void RefreshPresence()
    {
        DateTime cutoff = DateTime.UtcNow.AddSeconds(-7);
        bool changed = false;

        foreach ((string hostId, Dictionary<string, DiscoveredHost> addresses) in _endpoints.ToArray())
        {
            string? before = BestEndpoint(hostId, DateTime.MinValue)?.Address;
            foreach (string address in addresses.Where(kv => kv.Value.LastSeenUtc < cutoff).Select(kv => kv.Key).ToArray())
            {
                addresses.Remove(address);
                changed = true;
            }
            if (addresses.Count == 0)
            {
                _endpoints.Remove(hostId);
                continue;
            }
            string? after = BestEndpoint(hostId, cutoff)?.Address;
            if (!string.Equals(before, after, StringComparison.OrdinalIgnoreCase)) changed = true;
        }

        // Если LAN пропал, но VPN остался, обновляем сохранённый адрес на лучший оставшийся endpoint.
        foreach (ConnectionProfile profile in _profiles)
        {
            DiscoveredHost? best = FindOnlineHost(profile);
            if (best != null && (!string.Equals(profile.Host, best.Address, StringComparison.OrdinalIgnoreCase) || profile.Port != best.Port))
            {
                profile.Host = best.Address;
                profile.Port = best.Port;
                ViewerSettingsStore.Save(_profiles);
                UpdateProfileEndpointLabels(profile);
            }
        }

        UpdateSavedStatuses();
        if (changed) BuildDiscoveredCards();
    }

    private void UpdateSavedStatuses()
    {
        foreach (ConnectionProfile profile in _profiles)
        {
            bool online = FindOnlineHost(profile) != null;
            bool previous = _onlineStates.TryGetValue(profile.Id, out bool state) && state;

            if (_savedStatusLabels.TryGetValue(profile.Id, out Label? label))
            {
                label.Text = online ? "Онлайн" : "Оффлайн";
                label.ForeColor = online ? UiTheme.Success : UiTheme.Offline;
            }
            if (_savedStatusDots.TryGetValue(profile.Id, out StatusDot? dot))
            {
                dot.DotColor = online ? UiTheme.Success : UiTheme.Offline;
                dot.Invalidate();
            }

            if (previous && !online)
            {
                // После настоящего Online -> Offline разрешаем ровно один новый снимок при следующем появлении.
                _snapshotRequested.Remove(profile.Id);
            }
            else if (!previous && online)
            {
                DiscoveredHost? host = FindOnlineHost(profile);
                if (host != null) RequestSnapshotOnce(profile, host.Address, host.Port);
            }

            _onlineStates[profile.Id] = online;
        }
    }

    private void UpdateProfileEndpointLabels(ConnectionProfile profile)
    {
        if (_savedAddressLabels.TryGetValue(profile.Id, out Label? address))
            address.Text = $"{profile.Host}:{profile.Port}";

        if (_savedKindLabels.TryGetValue(profile.Id, out Label? kind))
        {
            string routeText = EndpointPriority.Kind(profile.Host);
            if (!string.IsNullOrWhiteSpace(profile.HostId))
            {
                DateTime cutoff = DateTime.UtcNow.AddSeconds(-7);
                string[] kinds = OnlineEndpoints(profile.HostId, cutoff)
                    .Select(h => EndpointPriority.Kind(h.Address))
                    .Distinct(StringComparer.CurrentCultureIgnoreCase)
                    .ToArray();
                if (kinds.Length > 0) routeText = kinds.Contains("LAN", StringComparer.CurrentCultureIgnoreCase) && kinds.Contains("Radmin VPN", StringComparer.CurrentCultureIgnoreCase)
                    ? "LAN + Radmin"
                    : string.Join(" + ", kinds);
            }
            kind.Text = routeText;
        }
    }

    private DiscoveredHost? FindOnlineHost(ConnectionProfile profile)
    {
        DateTime cutoff = DateTime.UtcNow.AddSeconds(-7);
        if (!string.IsNullOrWhiteSpace(profile.HostId))
        {
            DiscoveredHost? byId = BestEndpoint(profile.HostId, cutoff);
            if (byId != null) return byId;
        }

        foreach (Dictionary<string, DiscoveredHost> addresses in _endpoints.Values)
        {
            if (addresses.TryGetValue(profile.Host, out DiscoveredHost? exact) && exact.LastSeenUtc >= cutoff && exact.Port == profile.Port)
                return exact;
        }
        return null;
    }

    private List<DiscoveredHost> OnlineEndpoints(string hostId, DateTime cutoff)
    {
        if (!_endpoints.TryGetValue(hostId, out Dictionary<string, DiscoveredHost>? addresses))
            return new List<DiscoveredHost>();

        // Не схлопываем LAN и Radmin VPN: Viewer должен видеть оба реально доступных маршрута.
        // При этом сортировка оставляет LAN первым, поэтому он остаётся маршрутом подключения по умолчанию.
        return addresses.Values
            .Where(h => h.LastSeenUtc >= cutoff)
            .GroupBy(h => h.Address, StringComparer.OrdinalIgnoreCase)
            .Select(g => g.OrderByDescending(x => x.LastSeenUtc).First())
            .OrderByDescending(h => EndpointPriority.Score(h.Address))
            .ThenBy(h => h.Address, StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    private DiscoveredHost? BestEndpoint(string hostId, DateTime cutoff)
    {
        return OnlineEndpoints(hostId, cutoff).FirstOrDefault();
    }

    private async Task ConnectProfileAsync(ConnectionProfile profile)
    {
        ResetSavedCardVisualStates();
        string password = profile.GetPassword();
        if (string.IsNullOrEmpty(password))
        {
            EditProfile(profile);
            password = profile.GetPassword();
            if (string.IsNullOrEmpty(password)) return;
        }

        DiscoveredHost? best = FindOnlineHost(profile);
        if (best != null)
        {
            profile.Host = best.Address;
            profile.Port = best.Port;
            ViewerSettingsStore.Save(_profiles);
            UpdateProfileEndpointLabels(profile);
        }

        if (_sessions.TryGetValue(profile.Id, out var existing))
        {
            SelectView(existing.Tab);
            existing.Page.FocusScreen();
            if (!existing.Page.IsConnected)
            {
                try { await existing.Page.ConnectAsync(profile.Host, profile.Port); }
                catch (Exception ex) { MessageBox.Show(this, ex.Message, "Ошибка подключения", MessageBoxButtons.OK, MessageBoxIcon.Error); }
            }
            return;
        }

        var page = new RemoteSessionPage(profile);
        page.UiStateChanged += SessionUiStateChanged;
        page.TopEdgeHovered += SessionTopEdgeHovered;
        var tab = new NavigationTab(profile.Name, true);
        tab.SelectedRequested += t => SelectView(t);
        tab.CloseRequested += _ => CloseSession(profile.Id);
        _sessions[profile.Id] = (tab, page);
        _views[tab] = page;
        _navStrip.Controls.Add(tab);
        _contentHost.Controls.Add(page);
        page.Visible = false;
        SelectView(tab);

        try
        {
            await page.ConnectAsync(profile.Host, profile.Port);
            page.FocusScreen();
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Ошибка подключения", MessageBoxButtons.OK, MessageBoxIcon.Error);
            CloseSession(profile.Id);
        }
    }

    private void CloseSession(string profileId)
    {
        if (!_sessions.TryGetValue(profileId, out var entry)) return;
        bool selected = ReferenceEquals(_selectedNav, entry.Tab);
        _sessions.Remove(profileId);
        _views.Remove(entry.Tab);
        _navStrip.Controls.Remove(entry.Tab);
        _contentHost.Controls.Remove(entry.Page);
        entry.Page.UiStateChanged -= SessionUiStateChanged;
        entry.Page.TopEdgeHovered -= SessionTopEdgeHovered;
        entry.Page.Dispose();
        entry.Tab.Dispose();
        if (selected) SelectView(_computersNav);
    }

    private void SelectView(NavigationTab tab)
    {
        if (!_views.TryGetValue(tab, out Control? view)) return;
        foreach ((NavigationTab nav, Control control) in _views)
        {
            bool selected = ReferenceEquals(nav, tab);
            nav.Selected = selected;
            control.Visible = selected;
        }
        _selectedNav = tab;
        view.BringToFront();
        if (view is RemoteSessionPage session)
        {
            BindSessionActions(session);
            session.FocusScreen();
        }
        else
        {
            _sessionActions.Visible = false;
            ResetSavedCardVisualStates();
        }
    }

    private void SessionUiStateChanged(RemoteSessionPage session)
    {
        if (ReferenceEquals(session, ActiveSession())) BindSessionActions(session);
    }

    private void BindSessionActions(RemoteSessionPage session)
    {
        _sessionActions.Visible = true;
        _sessionStatus.Text = session.DisplayStatus;
        _sessionStatus.ForeColor = session.IsConnected ? UiTheme.Success : UiTheme.Muted;
        if (_sessionSound.Checked != session.AudioPlaybackEnabled) _sessionSound.Checked = session.AudioPlaybackEnabled;
        _sessionHotkeys.Enabled = session.IsConnected;
        _sessionDisconnect.Enabled = session.IsConnected;
        _sessionFullscreen.Enabled = session.IsConnected;
        _sessionFullscreen.Text = _fullScreen ? "Обычный режим" : "На весь экран";
    }

    private void SessionTopEdgeHovered(RemoteSessionPage session)
    {
        if (!_fullScreen || !ReferenceEquals(session, ActiveSession())) return;
        ShowFullscreenOverlay();
    }

    private void ShowFullscreenOverlay()
    {
        if (!_fullScreen) return;
        _fullscreenOverlay.Left = Math.Max(0, (ClientSize.Width - _fullscreenOverlay.Width) / 2);
        _fullscreenOverlay.Top = 0;
        _fullscreenOverlay.Visible = true;
        _fullscreenOverlay.BringToFront();
        _fullscreenHideTimer.Stop();
        _fullscreenHideTimer.Start();
    }

    private void ToggleFullScreen()
    {
        RemoteSessionPage? session = ActiveSession();
        if (session == null) return;

        if (!_fullScreen)
        {
            _fullScreen = true;
            _normalBounds = Bounds;
            _normalBorderStyle = FormBorderStyle;
            _normalWindowState = WindowState;
            WindowState = FormWindowState.Normal;
            FormBorderStyle = FormBorderStyle.None;
            Bounds = Screen.FromControl(this).Bounds;
            _topBar.Visible = false;
            ShowFullscreenOverlay();
        }
        else
        {
            _fullScreen = false;
            _fullscreenHideTimer.Stop();
            _fullscreenOverlay.Visible = false;
            FormBorderStyle = _normalBorderStyle;
            Bounds = _normalBounds;
            WindowState = _normalWindowState;
            _topBar.Visible = true;
            BindSessionActions(session);
        }
        session.FocusScreen();
    }

    private RemoteSessionPage? ActiveSession()
    {
        if (_selectedNav == null || !_views.TryGetValue(_selectedNav, out Control? control)) return null;
        return control as RemoteSessionPage;
    }

    protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
    {
        if (keyData == Keys.F11 && ActiveSession() != null)
        {
            ToggleFullScreen();
            return true;
        }
        if (_fullScreen && keyData == Keys.Escape)
        {
            ToggleFullScreen();
            return true;
        }
        RemoteSessionPage? session = ActiveSession();
        if (session?.ProcessShortcut(keyData) == true) return true;
        return base.ProcessCmdKey(ref msg, keyData);
    }

    private void RequestSnapshotOnce(ConnectionProfile profile, string host, int port)
    {
        if (_closing || string.IsNullOrWhiteSpace(profile.Id)) return;
        string password = profile.GetPassword();
        if (string.IsNullOrWhiteSpace(password)) return;
        if (!_snapshotRequested.Add(profile.Id)) return;
        _ = RefreshOneThumbnailAsync(profile, host, port, password, _thumbnailCts.Token);
    }

    private async Task RefreshOneThumbnailAsync(ConnectionProfile profile, string host, int port, string password, CancellationToken ct)
    {
        try
        {
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
            timeout.CancelAfter(TimeSpan.FromSeconds(6));
            byte[]? jpeg = await ThumbnailClient.FetchAsync(host, port, password, timeout.Token).ConfigureAwait(false);
            if (jpeg == null || jpeg.Length == 0) return;

            string path = ViewerSettingsStore.GetThumbnailPath(profile.Id);
            string temp = path + ".tmp";
            await File.WriteAllBytesAsync(temp, jpeg, timeout.Token).ConfigureAwait(false);
            File.Move(temp, path, true);

            Bitmap bitmap;
            using (var ms = new MemoryStream(jpeg, false))
            using (var image = Image.FromStream(ms, false, false))
                bitmap = new Bitmap(image);

            SafeUi(() =>
            {
                if (!_savedPictures.TryGetValue(profile.Id, out PictureBox? picture) || picture.IsDisposed)
                {
                    bitmap.Dispose();
                    return;
                }
                Image? old = picture.Image;
                picture.Image = bitmap;
                old?.Dispose();
            });
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { AppLog.Write(ex, $"Snapshot {profile.Name} ({host}:{port})"); }
    }

    private void SafeUi(Action action)
    {
        if (_closing || IsDisposed) return;
        try { if (InvokeRequired) BeginInvoke(action); else action(); }
        catch { }
    }

    private static void DisposeChildren(Control parent)
    {
        foreach (Control child in parent.Controls.Cast<Control>().ToArray())
        {
            foreach (PictureBox picture in EnumeratePictures(child))
            {
                Image? image = picture.Image;
                picture.Image = null;
                image?.Dispose();
            }
            child.Dispose();
        }
        parent.Controls.Clear();
    }

    private static IEnumerable<PictureBox> EnumeratePictures(Control root)
    {
        foreach (Control child in root.Controls)
        {
            if (child is PictureBox p) yield return p;
            foreach (PictureBox nested in EnumeratePictures(child)) yield return nested;
        }
    }

    private static Image LoadThumbnailOrPlaceholder(string profileId, string name)
    {
        try
        {
            string path = ViewerSettingsStore.GetThumbnailPath(profileId);
            if (File.Exists(path))
            {
                using var stream = File.OpenRead(path);
                using var image = Image.FromStream(stream);
                return new Bitmap(image);
            }
        }
        catch { }
        return CreateMonitorPlaceholder(name);
    }

    private static Bitmap CreateMonitorPlaceholder(string text)
    {
        var bmp = new Bitmap(384, 216);
        using Graphics g = Graphics.FromImage(bmp);
        g.Clear(Color.FromArgb(5, 10, 18));
        using var glow = new SolidBrush(Color.FromArgb(15, 40, 72));
        g.FillEllipse(glow, 72, 12, 240, 162);
        using var pen = new Pen(UiTheme.Muted2, 3);
        g.DrawRectangle(pen, 80, 44, 224, 102);
        g.DrawLine(pen, 168, 150, 216, 150);
        g.DrawLine(pen, 192, 148, 192, 170);
        g.DrawLine(pen, 156, 171, 228, 171);
        using var font = new Font("Segoe UI Semibold", 11F, FontStyle.Bold);
        using var brush = new SolidBrush(UiTheme.Text);
        var rect = new RectangleF(96, 82, 192, 32);
        using var format = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center, Trimming = StringTrimming.EllipsisCharacter };
        g.DrawString(text, font, brush, rect, format);
        return bmp;
    }

    private static Bitmap CreateSmallMonitorIcon()
    {
        var bmp = new Bitmap(42, 42);
        using Graphics g = Graphics.FromImage(bmp);
        g.Clear(Color.Transparent);
        g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
        using var pen = new Pen(UiTheme.AccentHover, 2.2F);
        g.DrawRectangle(pen, 5, 6, 32, 23);
        g.DrawLine(pen, 21, 29, 21, 35);
        g.DrawLine(pen, 14, 35, 28, 35);
        return bmp;
    }
}
