using System.Diagnostics;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;

namespace RemoteHost;

internal sealed class HostForm : Form
{
    private readonly RemoteServer _server = new();
    private readonly HostSettings _settings;

    private readonly TextBox _password = new() { UseSystemPasswordChar = true };
    private readonly NumericUpDown _port = new() { Minimum = 1024, Maximum = 65531, Value = 45900 };
    private readonly NumericUpDown _fps = new() { Minimum = 1, Maximum = 60, Value = 30 };
    private readonly NumericUpDown _quality = new() { Minimum = 40, Maximum = 100, Value = 95 };
    private readonly CheckBox _audio = new() { Text = "Передавать системный звук", AutoSize = true };
    private readonly ComboBox _audioDevice = new() { DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly CheckBox _autostart = new() { Text = "Запускать вместе с Windows и сразу ждать подключение", AutoSize = true };

    private readonly Label _statusTitle = new() { Text = "Host остановлен", AutoSize = true };
    private readonly Label _statusHint = new() { Text = "Сервер не принимает подключения", AutoSize = true };
    private readonly Label _portState = new() { AutoSize = false, TextAlign = ContentAlignment.MiddleLeft };
    private readonly Label _clientState = new() { AutoSize = false, TextAlign = ContentAlignment.MiddleLeft };
    private readonly Label _audioState = new() { AutoSize = false, TextAlign = ContentAlignment.MiddleLeft };
    private readonly StatusDot _statusDot = new() { DotColor = UiTheme.Offline };

    private readonly Button _start = new() { Text = "Запустить Host", Width = 190, Height = 42 };
    private readonly Button _disconnect = new() { Text = "Отключить клиента", Width = 190, Height = 42, Enabled = false };
    private readonly NotifyIcon _tray = new();
    private readonly bool _startedFromWindows;
    private bool _hideOnFirstShown;
    private bool _allowExit;
    private bool _backgroundInitialized;

    public event Action? ViewerRequested;

    private sealed record AudioDeviceChoice(string Id, string Name)
    {
        public override string ToString() => Name;
    }

    private sealed record AddressInfo(string Label, string Address, int Order);

    public HostForm(bool startedFromWindows = false)
    {
        _startedFromWindows = startedFromWindows;
        _hideOnFirstShown = startedFromWindows;
        _settings = HostSettingsStore.Load();
        string savedPassword = _settings.GetPassword();
        if (string.IsNullOrWhiteSpace(savedPassword))
        {
            savedPassword = GeneratePassword();
            _settings.SetPassword(savedPassword);
            HostSettingsStore.Save(_settings);
        }

        Text = "Simple Remote Host";
        Icon = AppBrand.LoadIcon();
        ClientSize = new Size(1060, 862);
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;
        SizeGripStyle = SizeGripStyle.Hide;
        StartPosition = FormStartPosition.CenterScreen;
        UiTheme.StyleForm(this);
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        DoubleBuffered = true;

        if (_startedFromWindows)
        {
            ShowInTaskbar = false;
            WindowState = FormWindowState.Minimized;
        }

        _port.Value = Math.Clamp(_settings.Port, (int)_port.Minimum, (int)_port.Maximum);
        _password.Text = savedPassword;
        _fps.Value = Math.Clamp(_settings.Fps, (int)_fps.Minimum, (int)_fps.Maximum);
        _quality.Value = Math.Clamp(_settings.JpegQuality, (long)_quality.Minimum, (long)_quality.Maximum);
        _audio.Checked = _settings.AudioEnabled;
        _autostart.Checked = _settings.AutoStartWindows;

        UiTheme.StyleTextBox(_password);
        UiTheme.StyleNumeric(_port);
        UiTheme.StyleNumeric(_fps);
        UiTheme.StyleNumeric(_quality);
        UiTheme.StyleCombo(_audioDevice);
        _audio.ForeColor = UiTheme.Text;
        _audio.BackColor = UiTheme.Surface;
        // Keep the checkbox background fully owned by WinForms. With visual-style
        // background painting enabled, entering/leaving the audio controls can
        // trigger an extra themed background erase before the dark parent repaints.
        _audio.UseVisualStyleBackColor = false;
        _autostart.ForeColor = UiTheme.Text;
        _autostart.BackColor = UiTheme.Surface;
        _autostart.UseVisualStyleBackColor = false;
        _statusTitle.Font = new Font("Segoe UI Semibold", 15F, FontStyle.Bold);
        _statusTitle.ForeColor = UiTheme.Offline;
        _statusHint.ForeColor = UiTheme.Muted;
        foreach (Label label in new[] { _portState, _clientState, _audioState })
        {
            label.ForeColor = UiTheme.Text;
            label.Height = 28;
        }
        UiTheme.StyleButton(_start, primary: true);
        UiTheme.StyleButton(_disconnect);

        PopulateAudioDevices();
        UpdateAudioDeviceEnabled();
        BuildLayout();
        SetupTray();
        RefreshStateDetails();

        _start.Click += async (_, _) => await ToggleServerAsync();
        _disconnect.Click += (_, _) => _server.DisconnectClient();
        _audio.CheckedChanged += (_, _) =>
        {
            UpdateAudioDeviceEnabled();
            RefreshStateDetails();
        };
        _autostart.CheckedChanged += (_, _) =>
        {
            try
            {
                StartupManager.SetEnabled(_autostart.Checked);
                _settings.AutoStartWindows = _autostart.Checked;
                _settings.StartServerOnLaunch = true;
                SaveSettingsFromUi();
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Автозапуск", MessageBoxButtons.OK, MessageBoxIcon.Error);
                _autostart.Checked = false;
            }
        };

        _server.StatusChanged += text => SafeUi(() =>
        {
            bool running = _server.IsRunning;
            _statusTitle.Text = running ? "Host запущен" : "Host остановлен";
            _statusTitle.ForeColor = running ? UiTheme.Success : UiTheme.Offline;
            _statusDot.DotColor = running ? UiTheme.Success : UiTheme.Offline;
            _statusDot.Invalidate();
            _statusHint.Text = running ? "Сервер работает и ожидает подключения" : text;
            RefreshStateDetails();
        });
        _server.ClientChanged += value => SafeUi(() =>
        {
            _clientState.Text = value == null ? "Нет подключений" : value;
            _clientState.ForeColor = value == null ? UiTheme.Muted : UiTheme.Success;
            _disconnect.Enabled = value != null;
        });

        Resize += (_, _) =>
        {
            if (WindowState == FormWindowState.Minimized)
            {
                Hide();
                _tray.ShowBalloonTip(900, "Simple Remote Host", "Host продолжает работать в трее.", ToolTipIcon.Info);
            }
        };

        FormClosing += (_, e) =>
        {
            SaveSettingsFromUi();
            if (!_allowExit)
            {
                e.Cancel = true;
                Hide();
            }
        };

        Shown += async (_, _) =>
        {
            await InitializeBackgroundAsync();
            if (_hideOnFirstShown)
            {
                _hideOnFirstShown = false;
                Hide();
                _tray.ShowBalloonTip(900, "Simple Remote Host", "Host запущен и готов к подключению.", ToolTipIcon.Info);
            }
        };
    }

    private void BuildLayout()
    {
        var header = new Panel
        {
            Dock = DockStyle.Top,
            Height = 92,
            BackColor = UiTheme.Header,
            Padding = new Padding(28, 18, 28, 14)
        };
        var logo = AppBrand.CreateLogo(50);
        logo.Location = new Point(28, 22);
        header.Controls.Add(logo);
        var title = UiTheme.Title("Simple Remote Host", 18F);
        title.Location = new Point(94, 18);
        header.Controls.Add(title);
        var subtitle = UiTheme.MutedLabel("Постоянный удалённый доступ к этому компьютеру");
        subtitle.Location = new Point(95, 54);
        header.Controls.Add(subtitle);

        var body = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            BackColor = UiTheme.Bg,
            Padding = new Padding(24, 18, 24, 22),
            ColumnCount = 1,
            RowCount = 3
        };
        body.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        body.RowStyles.Add(new RowStyle(SizeType.Absolute, 266));
        body.RowStyles.Add(new RowStyle(SizeType.Absolute, 224));
        body.RowStyles.Add(new RowStyle(SizeType.Percent, 100));

        body.Controls.Add(BuildAccessCard(), 0, 0);
        body.Controls.Add(BuildQualityCard(), 0, 1);
        body.Controls.Add(BuildStatusCard(), 0, 2);

        Controls.Add(body);
        Controls.Add(header);
    }

    private Control BuildAccessCard()
    {
        var card = CreateSection("Доступ", "Порт, пароль и адреса для подключения", SectionVisual.Access);
        var content = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            BackColor = Color.Transparent,
            ColumnCount = 2,
            RowCount = 2,
            Margin = Padding.Empty
        };
        content.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 46));
        content.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 54));
        content.RowStyles.Add(new RowStyle(SizeType.Absolute, 104));
        content.RowStyles.Add(new RowStyle(SizeType.Percent, 100));

        var portField = CreateField("Базовый порт", "Используется Viewer для подключения", _port);
        portField.Margin = new Padding(0, 0, 18, 0);
        content.Controls.Add(portField, 0, 0);

        var passwordField = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            BackColor = Color.Transparent,
            RowCount = 3,
            ColumnCount = 1,
            Margin = new Padding(18, 0, 0, 0)
        };
        passwordField.RowStyles.Add(new RowStyle(SizeType.Absolute, 22));
        passwordField.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));
        passwordField.RowStyles.Add(new RowStyle(SizeType.Absolute, 46));
        var passwordCaption = UiTheme.MutedLabel("Пароль");
        passwordCaption.Dock = DockStyle.Fill;
        passwordCaption.TextAlign = ContentAlignment.MiddleLeft;
        passwordField.Controls.Add(passwordCaption, 0, 0);
        _password.Dock = DockStyle.Fill;
        _password.Margin = new Padding(0, 2, 0, 4);
        passwordField.Controls.Add(_password, 0, 1);

        var passwordActions = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false,
            BackColor = Color.Transparent,
            Margin = Padding.Empty,
            Padding = Padding.Empty
        };
        var showPassword = new CheckBox
        {
            Text = "Показать пароль",
            AutoSize = true,
            ForeColor = UiTheme.Text,
            Margin = new Padding(0, 7, 18, 0)
        };
        showPassword.CheckedChanged += (_, _) => _password.UseSystemPasswordChar = !showPassword.Checked;
        var randomPassword = new Button { Text = "Новый пароль", Width = 186, Height = 42, Margin = new Padding(0, 0, 0, 0) };
        UiTheme.StyleButton(randomPassword);
        randomPassword.Click += (_, _) => _password.Text = GeneratePassword();
        passwordActions.Controls.Add(showPassword);
        passwordActions.Controls.Add(randomPassword);
        passwordField.Controls.Add(passwordActions, 0, 2);
        content.Controls.Add(passwordField, 1, 0);

        var addressTable = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            BackColor = Color.Transparent,
            ColumnCount = 2,
            RowCount = 1,
            Margin = new Padding(0, 4, 0, 0)
        };
        addressTable.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        addressTable.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        IReadOnlyList<AddressInfo> infos = GetAddressInfos();
        AddressInfo? lan = infos.FirstOrDefault(x => x.Label == "Локальная сеть (LAN)");
        Control lanChip = CreateAddressChip(lan ?? new AddressInfo("Локальная сеть (LAN)", "Не найдено", 0));
        lanChip.Margin = new Padding(0, 0, 8, 0);
        addressTable.Controls.Add(lanChip, 0, 0);

        AddressInfo? radmin = infos.FirstOrDefault(x => x.Label == "Radmin VPN");
        Control vpnChip = CreateAddressChip(radmin ?? new AddressInfo("Radmin VPN", "Не найдено", 20));
        vpnChip.Margin = new Padding(8, 0, 0, 0);
        addressTable.Controls.Add(vpnChip, 1, 0);

        content.Controls.Add(addressTable, 0, 1);
        content.SetColumnSpan(addressTable, 2);
        card.Controls.Add(content);
        return card;
    }

    private Control BuildQualityCard()
    {
        var card = CreateSection("Качество соединения", "Изображение, звук и запуск вместе с Windows", SectionVisual.Quality);
        var content = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            BackColor = Color.Transparent,
            ColumnCount = 2,
            RowCount = 1,
            Margin = Padding.Empty
        };
        content.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 46));
        content.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 54));

        var left = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            BackColor = Color.Transparent,
            ColumnCount = 2,
            RowCount = 2,
            Padding = new Padding(0, 2, 20, 0),
            Margin = Padding.Empty
        };
        left.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 58));
        left.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 42));
        left.RowStyles.Add(new RowStyle(SizeType.Absolute, 54));
        left.RowStyles.Add(new RowStyle(SizeType.Absolute, 54));
        AddInlineField(left, 0, "Кадры в секунду (FPS)", _fps);
        AddInlineField(left, 1, "Качество JPEG (%)", _quality);
        content.Controls.Add(left, 0, 0);

        var right = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            // Native CheckBox/ComboBox controls repaint their hot state on mouse hover.
            // An opaque host prevents those repaints from walking through a transparent
            // TableLayoutPanel into the custom-painted PremiumPanel underneath.
            BackColor = UiTheme.Surface,
            ColumnCount = 1,
            RowCount = 3,
            Padding = new Padding(20, 2, 0, 0),
            Margin = Padding.Empty
        };
        right.RowStyles.Add(new RowStyle(SizeType.Absolute, 38));
        right.RowStyles.Add(new RowStyle(SizeType.Absolute, 48));
        right.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
        _audio.Margin = new Padding(0, 7, 0, 0);
        right.Controls.Add(_audio, 0, 0);
        _audioDevice.Dock = DockStyle.Fill;
        _audioDevice.Margin = new Padding(0, 5, 0, 8);
        right.Controls.Add(_audioDevice, 0, 1);
        _autostart.Margin = new Padding(0, 8, 0, 0);
        right.Controls.Add(_autostart, 0, 2);
        content.Controls.Add(right, 1, 0);

        card.Controls.Add(content);
        return card;
    }

    private Control BuildStatusCard()
    {
        var card = CreateSection("Статус", "Состояние сервера и активного подключения", SectionVisual.Status);
        var content = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            BackColor = Color.Transparent,
            ColumnCount = 2,
            RowCount = 1,
            Margin = Padding.Empty
        };
        content.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 52));
        content.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 48));

        var left = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            BackColor = Color.Transparent,
            ColumnCount = 2,
            RowCount = 3,
            Margin = Padding.Empty
        };
        left.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 28));
        left.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        left.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));
        left.RowStyles.Add(new RowStyle(SizeType.Absolute, 32));
        left.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        _statusDot.Anchor = AnchorStyles.Left;
        _statusDot.Margin = new Padding(2, 5, 0, 0);
        left.Controls.Add(_statusDot, 0, 0);
        _statusTitle.Dock = DockStyle.Fill;
        _statusTitle.TextAlign = ContentAlignment.MiddleLeft;
        left.Controls.Add(_statusTitle, 1, 0);
        _statusHint.Dock = DockStyle.Fill;
        _statusHint.TextAlign = ContentAlignment.MiddleLeft;
        left.Controls.Add(_statusHint, 1, 1);

        var buttons = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false,
            BackColor = Color.Transparent,
            Margin = new Padding(0, 8, 0, 0),
            Padding = Padding.Empty
        };
        _start.Margin = new Padding(0, 0, 12, 0);
        _disconnect.Margin = Padding.Empty;
        buttons.Controls.Add(_start);
        buttons.Controls.Add(_disconnect);
        left.Controls.Add(buttons, 0, 2);
        left.SetColumnSpan(buttons, 2);
        content.Controls.Add(left, 0, 0);

        var details = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            BackColor = Color.Transparent,
            ColumnCount = 2,
            RowCount = 3,
            Padding = new Padding(28, 2, 0, 0),
            Margin = Padding.Empty
        };
        details.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 48));
        details.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 52));
        for (int i = 0; i < 3; i++) details.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));
        AddStateRow(details, 0, "Используемый порт", _portState);
        AddStateRow(details, 1, "Подключённый клиент", _clientState);
        AddStateRow(details, 2, "Передача звука", _audioState);
        content.Controls.Add(details, 1, 0);

        card.Controls.Add(content);
        return card;
    }

    private enum SectionVisual { Access, Quality, Status }

    private static PremiumPanel CreateSection(string titleText, string subtitleText, SectionVisual visual)
    {
        var card = new PremiumPanel
        {
            Dock = DockStyle.Fill,
            Margin = new Padding(0, 0, 0, 12),
            Padding = new Padding(24, 62, 24, 14),
            Radius = 16,
            PanelColor = UiTheme.Surface,
            BorderColor = UiTheme.BorderSoft
        };
        var icon = new PictureBox
        {
            Left = 22, Top = 12, Width = 42, Height = 42,
            SizeMode = PictureBoxSizeMode.Zoom,
            Image = CreateSectionIcon(visual)
        };
        card.Controls.Add(icon);
        var title = UiTheme.SectionTitle(titleText);
        title.Location = new Point(78, 12);
        card.Controls.Add(title);
        var subtitle = UiTheme.MutedLabel(subtitleText);
        subtitle.Location = new Point(79, 38);
        card.Controls.Add(subtitle);
        return card;
    }

    private static Bitmap CreateSectionIcon(SectionVisual visual)
    {
        var bmp = new Bitmap(42, 42);
        using Graphics g = Graphics.FromImage(bmp);
        g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
        Color accent = visual == SectionVisual.Status ? UiTheme.Success : visual == SectionVisual.Quality ? Color.FromArgb(50, 196, 193) : UiTheme.Accent;
        using var bg = new SolidBrush(Color.FromArgb(42, accent.R, accent.G, accent.B));
        using var border = new Pen(Color.FromArgb(170, accent.R, accent.G, accent.B), 1.4F);
        using var path = UiTheme.RoundedRect(new Rectangle(1, 1, 40, 40), 10);
        g.FillPath(bg, path);
        g.DrawPath(border, path);
        using var pen = new Pen(Color.FromArgb(235, 235, 246, 255), 2.3F);

        if (visual == SectionVisual.Access)
        {
            g.DrawEllipse(pen, 10, 9, 12, 12);
            g.DrawLine(pen, 20, 19, 31, 30);
            g.DrawLine(pen, 27, 26, 31, 22);
            g.DrawLine(pen, 30, 29, 34, 25);
        }
        else if (visual == SectionVisual.Quality)
        {
            g.DrawRectangle(pen, 9, 10, 24, 17);
            g.DrawLine(pen, 21, 28, 21, 33);
            g.DrawLine(pen, 15, 33, 27, 33);
        }
        else
        {
            g.DrawLine(pen, 11, 31, 11, 24);
            g.DrawLine(pen, 18, 31, 18, 18);
            g.DrawLine(pen, 25, 31, 25, 13);
            g.DrawLine(pen, 32, 31, 32, 8);
        }
        return bmp;
    }

    private static Control CreateField(string captionText, string hintText, Control control)
    {
        var panel = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            BackColor = Color.Transparent,
            RowCount = 3,
            ColumnCount = 1,
            Margin = Padding.Empty
        };
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 22));
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));

        var caption = UiTheme.MutedLabel(captionText);
        caption.Dock = DockStyle.Fill;
        caption.TextAlign = ContentAlignment.MiddleLeft;
        panel.Controls.Add(caption, 0, 0);

        control.Dock = DockStyle.Fill;
        control.Margin = new Padding(0, 2, 0, 4);
        panel.Controls.Add(control, 0, 1);

        var hint = new Label
        {
            Text = hintText,
            Dock = DockStyle.Fill,
            ForeColor = UiTheme.Muted2,
            Font = new Font("Segoe UI", 8.2F),
            TextAlign = ContentAlignment.MiddleLeft,
            AutoEllipsis = true
        };
        panel.Controls.Add(hint, 0, 2);
        return panel;
    }

    private static void AddInlineField(TableLayoutPanel table, int row, string caption, Control control)
    {
        var label = new Label
        {
            Text = caption,
            Dock = DockStyle.Fill,
            ForeColor = UiTheme.Muted,
            TextAlign = ContentAlignment.MiddleLeft,
            AutoEllipsis = true,
            Margin = Padding.Empty
        };
        control.Dock = DockStyle.Fill;
        control.Margin = new Padding(8, 9, 0, 9);
        table.Controls.Add(label, 0, row);
        table.Controls.Add(control, 1, row);
    }

    private static void AddStateRow(TableLayoutPanel table, int row, string caption, Label value)
    {
        var label = new Label { Text = caption + ":", Dock = DockStyle.Fill, ForeColor = UiTheme.Muted, TextAlign = ContentAlignment.MiddleLeft };
        value.Dock = DockStyle.Fill;
        table.Controls.Add(label, 0, row);
        table.Controls.Add(value, 1, row);
    }

    private static Control CreateAddressChip(AddressInfo info)
    {
        var chip = new PremiumPanel
        {
            Dock = DockStyle.Fill,
            Height = 62,
            Radius = 10,
            PanelColor = UiTheme.Surface2,
            BorderColor = UiTheme.BorderSoft,
            Padding = new Padding(14, 7, 14, 6)
        };
        var layout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            BackColor = Color.Transparent,
            RowCount = 2,
            ColumnCount = 1,
            Margin = Padding.Empty,
            Padding = Padding.Empty
        };
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 20));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        var label = new Label
        {
            Text = info.Label,
            Dock = DockStyle.Fill,
            ForeColor = UiTheme.Muted2,
            Font = new Font("Segoe UI", 8.4F),
            AutoEllipsis = true,
            TextAlign = ContentAlignment.MiddleLeft
        };
        var address = new Label
        {
            Text = info.Address,
            Dock = DockStyle.Fill,
            ForeColor = info.Address == "Не найдено" ? UiTheme.Muted2 : UiTheme.Text,
            Font = new Font("Segoe UI Semibold", 10F, FontStyle.Bold),
            AutoEllipsis = true,
            TextAlign = ContentAlignment.MiddleLeft
        };
        layout.Controls.Add(label, 0, 0);
        layout.Controls.Add(address, 0, 1);
        chip.Controls.Add(layout);
        return chip;
    }

    private void SetupTray()
    {
        var menu = new ContextMenuStrip
        {
            BackColor = UiTheme.Surface2,
            ForeColor = UiTheme.Text,
            ShowImageMargin = false,
            ShowCheckMargin = false,
            Padding = new Padding(2),
            Renderer = new ToolStripProfessionalRenderer(new DarkMenuColorTable())
        };
        menu.Items.Add("Открыть Viewer", null, (_, _) => LaunchViewer());
        menu.Items.Add("Настройки Host", null, (_, _) => ShowFromTray());
        ToolStripItem disconnectItem = menu.Items.Add("Отключить клиента", null, (_, _) => _server.DisconnectClient());
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("Выход", null, (_, _) => ExitApplication());
        foreach (ToolStripItem item in menu.Items)
        {
            if (item is ToolStripSeparator) continue;
            item.Padding = new Padding(10, 5, 12, 5);
            item.Margin = new Padding(0, 1, 0, 1);
        }
        menu.Opening += (_, _) => disconnectItem.Enabled = _server.IsRunning && !string.Equals(_clientState.Text, "Нет подключений", StringComparison.OrdinalIgnoreCase);

        _tray.Icon = (Icon)Icon.Clone();
        _tray.Text = "Simple Remote Desk";
        _tray.ContextMenuStrip = menu;
        _tray.Visible = true;
        _tray.MouseClick += (_, e) =>
        {
            if (e.Button == MouseButtons.Left) LaunchViewer();
        };
        _tray.DoubleClick += (_, _) => LaunchViewer();
    }

    private void LaunchViewer()
    {
        // In the unified build the Viewer is another window in the same application.
        // The fallback below keeps the legacy two-EXE projects usable as well.
        if (ViewerRequested != null)
        {
            ViewerRequested.Invoke();
            return;
        }

        try
        {
            string baseDir = AppContext.BaseDirectory;
            string[] candidates =
            {
                Path.Combine(baseDir, "SimpleRemoteViewer.exe"),
                Path.Combine(baseDir, "RemoteViewer.exe"),
                Path.Combine(baseDir, "Viewer", "RemoteViewer.exe"),
                Path.Combine(baseDir, "..", "Viewer", "RemoteViewer.exe")
            };
            string? viewer = candidates.FirstOrDefault(File.Exists);

            if (viewer == null)
            {
                MessageBox.Show(this,
                    "Simple Remote Viewer не найден. Используйте единую сборку SimpleRemoteDesk.exe или держите Viewer рядом с Host.",
                    "Simple Remote Desk", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            Process.Start(new ProcessStartInfo(Path.GetFullPath(viewer))
            {
                UseShellExecute = true,
                WorkingDirectory = Path.GetDirectoryName(Path.GetFullPath(viewer)) ?? baseDir
            });
        }
        catch (Exception ex)
        {
            AppLog.Write(ex, "Launch Viewer");
            MessageBox.Show(this, ex.Message, "Не удалось открыть Viewer", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    public async Task InitializeBackgroundAsync()
    {
        if (_backgroundInitialized) return;
        _backgroundInitialized = true;
        // Host is an always-on part of Simple Remote Desk. Every application launch
        // starts it automatically; the user can still stop it manually for this run.
        await StartServerAsync();
    }

    public void ShowSettings()
    {
        _hideOnFirstShown = false;
        ShowFromTray();
    }

    private void PopulateAudioDevices()
    {
        _audioDevice.Items.Clear();
        _audioDevice.Items.Add(new AudioDeviceChoice(string.Empty, "По умолчанию (устройство Windows)"));
        foreach (AudioOutputDevice device in AudioCapture.GetOutputDevices())
            _audioDevice.Items.Add(new AudioDeviceChoice(device.Id, device.Name));

        int selected = 0;
        for (int i = 0; i < _audioDevice.Items.Count; i++)
        {
            if (_audioDevice.Items[i] is AudioDeviceChoice choice && string.Equals(choice.Id, _settings.AudioDeviceId, StringComparison.OrdinalIgnoreCase))
            {
                selected = i;
                break;
            }
        }
        _audioDevice.SelectedIndex = selected;
    }

    private void UpdateAudioDeviceEnabled() => _audioDevice.Enabled = true;

    private async Task ToggleServerAsync()
    {
        if (_server.IsRunning)
        {
            _server.Stop();
            _start.Text = "Запустить Host";
            UiTheme.StyleButton(_start, primary: true);
            RefreshStateDetails();
            return;
        }
        await StartServerAsync();
    }

    private async Task StartServerAsync()
    {
        if (_server.IsRunning) return;
        if (string.IsNullOrWhiteSpace(_password.Text) || _password.Text.Length < 6)
        {
            MessageBox.Show("Пароль должен содержать хотя бы 6 символов.", "Пароль", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        SaveSettingsFromUi();
        _server.Port = (int)_port.Value;
        _server.Password = _password.Text;
        _server.Fps = (int)_fps.Value;
        _server.JpegQuality = (long)_quality.Value;
        _server.AudioEnabled = _audio.Checked;
        _server.AudioDeviceId = (_audioDevice.SelectedItem as AudioDeviceChoice)?.Id ?? string.Empty;
        _server.HostId = _settings.HostId;

        try
        {
            await _server.StartAsync();
            _start.Text = "Остановить Host";
            UiTheme.StyleButton(_start, danger: true);
            RefreshStateDetails();
        }
        catch (Exception ex)
        {
            MessageBox.Show(ex.Message, "Не удалось запустить Host", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void SaveSettingsFromUi()
    {
        _settings.Port = (int)_port.Value;
        _settings.SetPassword(_password.Text);
        _settings.Fps = (int)_fps.Value;
        _settings.JpegQuality = (long)_quality.Value;
        _settings.AudioEnabled = _audio.Checked;
        _settings.AudioDeviceId = (_audioDevice.SelectedItem as AudioDeviceChoice)?.Id ?? string.Empty;
        _settings.AutoStartWindows = _autostart.Checked;
        _settings.StartServerOnLaunch = true;
        if (string.IsNullOrWhiteSpace(_settings.HostId)) _settings.HostId = Guid.NewGuid().ToString("N");
        HostSettingsStore.Save(_settings);
    }

    private void RefreshStateDetails()
    {
        _portState.Text = (_server.IsRunning ? _server.Port : (int)_port.Value).ToString();
        _clientState.Text = _disconnect.Enabled ? _clientState.Text : "Нет подключений";
        _clientState.ForeColor = _disconnect.Enabled ? UiTheme.Success : UiTheme.Muted;
        _audioState.Text = _audio.Checked ? "Включена" : "Выключена";
        _audioState.ForeColor = _audio.Checked ? UiTheme.Success : UiTheme.Muted;
    }

    private void ShowFromTray()
    {
        if (Visible)
        {
            WindowState = FormWindowState.Normal;
            BringToFront();
            Activate();
            return;
        }

        // First paint happens while the window is fully transparent. This prevents
        // the native white background from flashing before the dark WinForms tree is drawn.
        Opacity = 0;
        BackColor = UiTheme.Bg;
        ShowInTaskbar = true;
        if (!IsHandleCreated) CreateControl();
        PerformLayout();
        WindowState = FormWindowState.Normal;
        Show();

        BeginInvoke(new Action(() =>
        {
            if (IsDisposed) return;
            Invalidate(true);
            Update();
            Opacity = 1;
            BringToFront();
            Activate();
        }));
    }

    private void ExitApplication()
    {
        _allowExit = true;
        SaveSettingsFromUi();
        _server.Dispose();
        _tray.Visible = false;
        _tray.Dispose();
        Close();
        Application.Exit();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            try { _server.Dispose(); } catch { }
            try { _tray.Visible = false; } catch { }
            try { _tray.Dispose(); } catch { }
        }
        base.Dispose(disposing);
    }

    private void SafeUi(Action action)
    {
        if (IsDisposed) return;
        try { if (InvokeRequired) BeginInvoke(action); else action(); }
        catch { }
    }

    private static IReadOnlyList<AddressInfo> GetAddressInfos()
    {
        // В интерфейсе намеренно показываем только два полезных адреса:
        // один адрес реальной локальной сети и один адрес Radmin VPN.
        // Виртуальные адаптеры Hyper-V/WSL/Docker/VMware сюда не попадают.
        var lanCandidates = new List<(AddressInfo Info, int Rank)>();
        var radminCandidates = new List<(AddressInfo Info, int Rank)>();

        try
        {
            foreach (NetworkInterface nic in NetworkInterface.GetAllNetworkInterfaces())
            {
                if (nic.OperationalStatus != OperationalStatus.Up || nic.NetworkInterfaceType == NetworkInterfaceType.Loopback)
                    continue;

                string nicText = (nic.Name + " " + nic.Description).ToLowerInvariant();
                bool isRadmin = nicText.Contains("radmin");
                bool virtualOther = IsVirtualNonRadmin(nicText, nic.NetworkInterfaceType);
                bool hasGateway = nic.GetIPProperties().GatewayAddresses.Any(g =>
                    g.Address.AddressFamily == AddressFamily.InterNetwork && !g.Address.Equals(IPAddress.Any));

                foreach (UnicastIPAddressInformation u in nic.GetIPProperties().UnicastAddresses)
                {
                    if (u.Address.AddressFamily != AddressFamily.InterNetwork || IPAddress.IsLoopback(u.Address)) continue;
                    byte[] b = u.Address.GetAddressBytes();
                    string address = u.Address.ToString();

                    if (isRadmin || b[0] == 26)
                    {
                        int rank = isRadmin ? 200 : 100;
                        radminCandidates.Add((new AddressInfo("Radmin VPN", address, 20), rank));
                        continue;
                    }

                    if (virtualOther || !IsPrivateLan(b)) continue;

                    int lanRank = 0;
                    if (hasGateway) lanRank += 100;
                    if (nic.NetworkInterfaceType == NetworkInterfaceType.Ethernet || nic.NetworkInterfaceType == NetworkInterfaceType.Wireless80211)
                        lanRank += 50;
                    if (b[0] == 192 && b[1] == 168) lanRank += 20;
                    else if (b[0] == 10) lanRank += 10;
                    lanCandidates.Add((new AddressInfo("Локальная сеть (LAN)", address, 0), lanRank));
                }
            }
        }
        catch { }

        var result = new List<AddressInfo>(2);
        AddressInfo? lan = lanCandidates.OrderByDescending(x => x.Rank).ThenBy(x => x.Info.Address).Select(x => x.Info).FirstOrDefault();
        AddressInfo? radmin = radminCandidates.OrderByDescending(x => x.Rank).ThenBy(x => x.Info.Address).Select(x => x.Info).FirstOrDefault();
        if (lan != null) result.Add(lan);
        if (radmin != null) result.Add(radmin);
        return result;
    }

    private static bool IsPrivateLan(byte[] b) =>
        b[0] == 10 ||
        (b[0] == 172 && b[1] >= 16 && b[1] <= 31) ||
        (b[0] == 192 && b[1] == 168);

    private static bool IsVirtualNonRadmin(string nicText, NetworkInterfaceType type)
    {
        if (type == NetworkInterfaceType.Tunnel) return true;
        string[] markers =
        {
            "hyper-v", "vethernet", "virtual", "vmware", "virtualbox", "docker", "wsl",
            "tailscale", "zerotier", "wireguard", "hamachi", "tap", "tun", "vpn"
        };
        return markers.Any(nicText.Contains);
    }

    private static string GeneratePassword()
    {
        const string chars = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
        byte[] data = System.Security.Cryptography.RandomNumberGenerator.GetBytes(12);
        return new string(data.Select(b => chars[b % chars.Length]).ToArray());
    }
}
