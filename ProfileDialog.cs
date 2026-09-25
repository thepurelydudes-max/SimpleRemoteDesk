namespace RemoteViewer;

internal static class ProfileDialog
{
    public static bool Edit(IWin32Window owner, ConnectionProfile profile, string? suggestedName = null,
        string? suggestedHost = null, int? suggestedPort = null)
    {
        bool isNew = string.IsNullOrWhiteSpace(profile.Host);
        using var form = new NoFlashDialogForm
        {
            Text = isNew ? "Добавить компьютер" : "Изменить компьютер",
            Icon = AppBrand.LoadIcon(),
            ClientSize = new Size(660, 540),
            StartPosition = FormStartPosition.CenterParent,
            FormBorderStyle = FormBorderStyle.FixedDialog,
            MaximizeBox = false,
            MinimizeBox = false
        };
        UiTheme.StyleForm(form);

        var name = new TextBox { Dock = DockStyle.Fill, Text = suggestedName ?? profile.Name };
        var host = new TextBox { Dock = DockStyle.Fill, Text = suggestedHost ?? profile.Host, PlaceholderText = "Например: 192.168.1.100" };
        var port = new NumericUpDown { Minimum = 1024, Maximum = 65531, Value = suggestedPort ?? profile.Port, Dock = DockStyle.Left, Width = 170 };
        var password = new TextBox { Dock = DockStyle.Fill, UseSystemPasswordChar = true, Text = profile.GetPassword() };
        UiTheme.StyleTextBox(name);
        UiTheme.StyleTextBox(host);
        UiTheme.StyleTextBox(password);
        UiTheme.StyleNumeric(port);

        var showPassword = new CheckBox
        {
            Text = "Показать пароль", AutoSize = true, ForeColor = UiTheme.Text,
            BackColor = Color.Transparent, Margin = new Padding(0, 8, 0, 0)
        };
        showPassword.CheckedChanged += (_, _) => password.UseSystemPasswordChar = !showPassword.Checked;

        var ok = new Button { Text = isNew ? "Добавить" : "Сохранить", Width = 144, Height = 42, Margin = new Padding(10, 0, 0, 0) };
        var cancel = new Button { Text = "Отмена", Width = 144, Height = 42, DialogResult = DialogResult.Cancel, Margin = Padding.Empty };
        UiTheme.StyleButton(ok, primary: true);
        UiTheme.StyleButton(cancel);

        var header = new Panel { Dock = DockStyle.Top, Height = 96, BackColor = UiTheme.Header, Padding = new Padding(24, 18, 24, 12) };
        var logo = AppBrand.CreateLogo(46);
        logo.Location = new Point(24, 24);
        header.Controls.Add(logo);
        var title = UiTheme.Title(isNew ? "Добавить компьютер" : "Параметры подключения", 16F);
        title.Location = new Point(86, 20);
        header.Controls.Add(title);
        var hint = UiTheme.MutedLabel("Укажите адрес Simple Remote Host и пароль доступа");
        hint.Location = new Point(87, 54);
        header.Controls.Add(hint);

        var outer = new Panel { Dock = DockStyle.Fill, BackColor = UiTheme.Bg, Padding = new Padding(24, 22, 24, 24) };
        var card = new PremiumPanel
        {
            Dock = DockStyle.Fill,
            Padding = new Padding(26, 24, 26, 22),
            Radius = 16,
            PanelColor = UiTheme.Surface,
            BorderColor = UiTheme.Border
        };

        var grid = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            BackColor = Color.Transparent,
            ColumnCount = 2,
            RowCount = 6,
            Margin = Padding.Empty
        };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 150));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 56));
        grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 56));
        grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 56));
        grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 56));
        grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
        grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 62));

        AddRow(grid, 0, "Название", name);
        AddRow(grid, 1, "IP / Host", host);
        AddRow(grid, 2, "Базовый порт", port);
        AddRow(grid, 3, "Пароль", password);
        grid.Controls.Add(showPassword, 1, 4);

        var buttons = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.RightToLeft,
            WrapContents = false,
            BackColor = Color.Transparent,
            Padding = new Padding(0, 10, 0, 0),
            Margin = Padding.Empty
        };
        buttons.Controls.Add(cancel);
        buttons.Controls.Add(ok);
        grid.Controls.Add(buttons, 0, 5);
        grid.SetColumnSpan(buttons, 2);

        card.Controls.Add(grid);
        outer.Controls.Add(card);
        form.Controls.Add(outer);
        form.Controls.Add(header);
        form.AcceptButton = ok;
        form.CancelButton = cancel;

        bool accepted = false;
        ok.Click += (_, _) =>
        {
            if (string.IsNullOrWhiteSpace(name.Text) || string.IsNullOrWhiteSpace(host.Text))
            {
                MessageBox.Show(form, "Укажите название и адрес компьютера.", "Параметры подключения",
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            if (password.Text.Length < 6)
            {
                MessageBox.Show(form, "Пароль должен содержать хотя бы 6 символов.", "Параметры подключения",
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            profile.Name = name.Text.Trim();
            profile.Host = host.Text.Trim();
            profile.Port = (int)port.Value;
            profile.SetPassword(password.Text);
            accepted = true;
            form.DialogResult = DialogResult.OK;
            form.Close();
        };

        return form.ShowPrepared(owner) == DialogResult.OK && accepted;
    }

    private sealed class NoFlashDialogForm : Form
    {
        private const int WS_EX_COMPOSITED = 0x02000000;

        public NoFlashDialogForm()
        {
            BackColor = UiTheme.Bg;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                     ControlStyles.ResizeRedraw | ControlStyles.UserPaint, true);
            DoubleBuffered = true;
        }

        protected override CreateParams CreateParams
        {
            get
            {
                CreateParams cp = base.CreateParams;
                cp.ExStyle |= WS_EX_COMPOSITED;
                return cp;
            }
        }

        public DialogResult ShowPrepared(IWin32Window owner)
        {
            Opacity = 0;
            ShowInTaskbar = false;
            if (!IsHandleCreated) CreateControl();
            PerformLayout();

            EventHandler? visibleHandler = null;
            visibleHandler = (_, _) =>
            {
                if (!Visible || IsDisposed) return;
                BeginInvoke(new Action(() =>
                {
                    if (IsDisposed) return;
                    Invalidate(true);
                    Update();
                    Opacity = 1;
                }));
            };
            VisibleChanged += visibleHandler;
            try
            {
                return ShowDialog(owner);
            }
            finally
            {
                VisibleChanged -= visibleHandler;
                if (!IsDisposed) Opacity = 1;
            }
        }
    }

    private static void AddRow(TableLayoutPanel grid, int row, string label, Control control)
    {
        var caption = new Label
        {
            Text = label,
            Dock = DockStyle.Fill,
            ForeColor = UiTheme.Muted,
            TextAlign = ContentAlignment.MiddleLeft,
            Margin = new Padding(0, 0, 12, 0)
        };
        control.Margin = new Padding(0, 9, 0, 9);
        grid.Controls.Add(caption, 0, row);
        grid.Controls.Add(control, 1, row);
    }
}
