using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;

namespace RemoteViewer;

internal static class UiTheme
{
    public static readonly Color Bg = Color.FromArgb(10, 20, 36);
    public static readonly Color Header = Color.FromArgb(13, 27, 46);
    public static readonly Color Surface = Color.FromArgb(17, 34, 58);
    public static readonly Color Surface2 = Color.FromArgb(23, 43, 71);
    public static readonly Color Surface3 = Color.FromArgb(31, 57, 92);
    public static readonly Color Border = Color.FromArgb(66, 106, 151);
    public static readonly Color BorderSoft = Color.FromArgb(49, 79, 115);
    public static readonly Color Accent = Color.FromArgb(30, 132, 255);
    public static readonly Color AccentHover = Color.FromArgb(54, 151, 255);
    public static readonly Color AccentPressed = Color.FromArgb(14, 106, 224);
    public static readonly Color Text = Color.FromArgb(245, 248, 253);
    public static readonly Color Muted = Color.FromArgb(156, 177, 207);
    public static readonly Color Muted2 = Color.FromArgb(118, 143, 177);
    public static readonly Color Success = Color.FromArgb(51, 211, 153);
    public static readonly Color Offline = Color.FromArgb(126, 145, 174);
    public static readonly Color Danger = Color.FromArgb(246, 87, 100);
    public static readonly Color DangerHover = Color.FromArgb(255, 108, 120);

    public static void StyleForm(Form form)
    {
        form.BackColor = Bg;
        form.ForeColor = Text;
        form.Font = new Font("Segoe UI", 9.5F);
        form.AutoScaleMode = AutoScaleMode.Dpi;
        form.HandleCreated += (_, _) => EnableDarkTitleBar(form.Handle);
    }

    private static void EnableDarkTitleBar(IntPtr handle)
    {
        try
        {
            int enabled = 1;
            if (DwmSetWindowAttribute(handle, 20, ref enabled, sizeof(int)) != 0)
                DwmSetWindowAttribute(handle, 19, ref enabled, sizeof(int));
        }
        catch { }
    }

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int attrValue, int attrSize);

    private sealed class ButtonThemeState
    {
        public bool Primary;
        public bool Danger;
        public bool Wired;
        public bool Hovered;
        public bool Pressed;
    }

    public static void StyleButton(Button button, bool primary = false, bool danger = false)
    {
        ButtonThemeState state = button.Tag as ButtonThemeState ?? new ButtonThemeState();
        state.Primary = primary;
        state.Danger = danger;
        button.Tag = state;

        button.FlatStyle = FlatStyle.Flat;
        button.FlatAppearance.BorderSize = 0;
        button.FlatAppearance.MouseOverBackColor = Surface3;
        button.FlatAppearance.MouseDownBackColor = Surface3;
        button.UseVisualStyleBackColor = false;
        button.BackColor = danger ? Danger : primary ? Accent : Surface3;
        button.ForeColor = Text;
        button.Cursor = Cursors.Hand;
        button.Padding = new Padding(12, 0, 12, 0);
        button.Height = Math.Max(button.Height, 42);
        button.MinimumSize = new Size(104, 42);
        button.Font = new Font("Segoe UI Semibold", 9.25F, FontStyle.Bold);
        button.AutoEllipsis = false;
        button.TextAlign = ContentAlignment.MiddleCenter;
        button.UseCompatibleTextRendering = false;
        button.AutoSize = false;
        button.TabStop = true;
        // Do not clip Buttons with a WinForms Region. Region edges are integer/pixel
        // based and produce the jagged “stair-step” corners that were visible in
        // previous builds. The rounded shape is drawn with anti-aliasing instead.
        button.Region?.Dispose();
        button.Region = null;

        if (!state.Wired)
        {
            state.Wired = true;
            button.Resize += (_, _) => button.Invalidate();
            button.EnabledChanged += (_, _) => button.Invalidate();
            button.TextChanged += (_, _) => button.Invalidate();
            button.MouseEnter += (_, _) => { state.Hovered = true; button.Invalidate(); };
            button.MouseLeave += (_, _) => { state.Hovered = false; state.Pressed = false; button.Invalidate(); };
            button.MouseDown += (_, e) => { if (e.Button == MouseButtons.Left) { state.Pressed = true; button.Invalidate(); } };
            button.MouseUp += (_, _) => { state.Pressed = false; button.Invalidate(); };
            button.Paint += (_, e) => DrawButton(button, e.Graphics, state);
        }

        button.Invalidate();
    }

    private static void DrawButton(Button button, Graphics g, ButtonThemeState state)
    {
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.PixelOffsetMode = PixelOffsetMode.HighQuality;

        Color back;
        Color fore;
        Color border;
        float borderWidth;

        if (!button.Enabled)
        {
            back = Color.FromArgb(24, 42, 67);
            fore = Color.FromArgb(157, 176, 202);
            border = Color.FromArgb(57, 86, 119);
            borderWidth = 1.0F;
        }
        else if (state.Danger)
        {
            back = state.Pressed ? Color.FromArgb(211, 68, 82) : state.Hovered ? DangerHover : Danger;
            fore = Color.White;
            border = Color.FromArgb(255, 129, 139);
            borderWidth = 0F;
        }
        else if (state.Primary)
        {
            back = state.Pressed ? AccentPressed : state.Hovered ? AccentHover : Accent;
            fore = Color.White;
            border = Color.FromArgb(107, 183, 255);
            borderWidth = 0F;
        }
        else
        {
            back = state.Pressed ? Color.FromArgb(20, 41, 68) : state.Hovered ? Color.FromArgb(37, 68, 105) : Surface3;
            fore = Text;
            border = state.Hovered ? Color.FromArgb(92, 145, 207) : Color.FromArgb(72, 116, 166);
            borderWidth = 1.15F;
        }

        // Paint the complete control ourselves. Clearing with the parent background
        // keeps the four outside corners clean without an aliased Region mask.
        g.Clear(ResolveBackground(button));
        RectangleF rect = new(1.5F, 1.5F, Math.Max(1F, button.ClientSize.Width - 3F), Math.Max(1F, button.ClientSize.Height - 3F));
        using GraphicsPath path = RoundedRectF(rect, 11.5F);
        using SolidBrush brush = new(back);
        g.FillPath(brush, path);
        if (borderWidth > 0)
        {
            using Pen pen = new(border, borderWidth) { Alignment = PenAlignment.Center, LineJoin = LineJoin.Round };
            g.DrawPath(pen, path);
        }

        Rectangle textRect = Rectangle.Inflate(button.ClientRectangle, -12, -2);
        TextRenderer.DrawText(g, button.Text ?? string.Empty, button.Font, textRect, fore,
            TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter |
            TextFormatFlags.SingleLine | TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
    }

    private static Color ResolveBackground(Control control)
    {
        Control? parent = control.Parent;
        while (parent != null)
        {
            Color c = parent.BackColor;
            if (c != Color.Transparent && c.A == 255) return c;
            parent = parent.Parent;
        }
        return Surface;
    }

    public static void StyleTextBox(TextBox box)
    {
        box.BackColor = Surface2;
        box.ForeColor = Text;
        box.BorderStyle = BorderStyle.FixedSingle;
        box.Font = new Font("Segoe UI", 10F);
    }

    public static void StyleNumeric(NumericUpDown box)
    {
        box.BackColor = Surface2;
        box.ForeColor = Text;
        box.BorderStyle = BorderStyle.FixedSingle;
        box.Font = new Font("Segoe UI", 10F);
        box.TextAlign = HorizontalAlignment.Left;
    }

    public static void StyleCombo(ComboBox box)
    {
        box.BackColor = Surface2;
        box.ForeColor = Text;
        box.FlatStyle = FlatStyle.Flat;
        box.Font = new Font("Segoe UI", 9.5F);
    }

    public static Label Title(string text, float size = 16F) => new()
    {
        Text = text,
        AutoSize = true,
        ForeColor = Text,
        Font = new Font("Segoe UI Semibold", size, FontStyle.Bold),
        UseCompatibleTextRendering = false
    };

    public static Label MutedLabel(string text) => new()
    {
        Text = text,
        AutoSize = true,
        ForeColor = Muted,
        UseCompatibleTextRendering = false
    };

    public static Label SectionTitle(string text) => new()
    {
        Text = text,
        AutoSize = true,
        ForeColor = Text,
        Font = new Font("Segoe UI Semibold", 12F, FontStyle.Bold),
        UseCompatibleTextRendering = false
    };

    public static void ApplyRoundRegion(Control control, int radius)
    {
        if (control.Width <= 1 || control.Height <= 1) return;
        using GraphicsPath path = RoundedRect(new Rectangle(0, 0, control.Width, control.Height), radius);
        Region? old = control.Region;
        control.Region = new Region(path);
        old?.Dispose();
    }

    public static GraphicsPath RoundedRect(Rectangle r, int radius)
    {
        int d = Math.Max(2, radius * 2);
        var p = new GraphicsPath();
        p.AddArc(r.Left, r.Top, d, d, 180, 90);
        p.AddArc(r.Right - d, r.Top, d, d, 270, 90);
        p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
        p.AddArc(r.Left, r.Bottom - d, d, d, 90, 90);
        p.CloseFigure();
        return p;
    }

    internal static GraphicsPath RoundedRectF(RectangleF r, float radius)
    {
        float d = Math.Max(2F, radius * 2F);
        var p = new GraphicsPath();
        p.AddArc(r.Left, r.Top, d, d, 180, 90);
        p.AddArc(r.Right - d, r.Top, d, d, 270, 90);
        p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
        p.AddArc(r.Left, r.Bottom - d, d, d, 90, 90);
        p.CloseFigure();
        return p;
    }
}


internal sealed class BufferedFlowLayoutPanel : FlowLayoutPanel
{
    public BufferedFlowLayoutPanel()
    {
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.ResizeRedraw | ControlStyles.UserPaint, true);
        DoubleBuffered = true;
        UpdateStyles();
    }
}

internal sealed class PremiumPanel : Panel
{
    private Color _panelColor = UiTheme.Surface;

    public int Radius { get; set; } = 14;
    public Color PanelColor
    {
        get => _panelColor;
        set
        {
            _panelColor = value;
            base.BackColor = value;
            Invalidate(true);
        }
    }
    public Color BorderColor { get; set; } = UiTheme.BorderSoft;
    public int BorderWidth { get; set; } = 1;

    public PremiumPanel()
    {
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.ResizeRedraw | ControlStyles.UserPaint | ControlStyles.SupportsTransparentBackColor, true);
        DoubleBuffered = true;
        base.BackColor = _panelColor;
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
        e.Graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
        Color parentColor = Parent?.BackColor ?? UiTheme.Bg;
        e.Graphics.Clear(parentColor);
        RectangleF r = new(1.2F, 1.2F, Math.Max(1F, Width - 2.4F), Math.Max(1F, Height - 2.4F));
        using GraphicsPath path = UiTheme.RoundedRectF(r, Radius + 0.5F);
        using SolidBrush brush = new(PanelColor);
        using Pen pen = new(BorderColor, BorderWidth) { Alignment = PenAlignment.Center, LineJoin = LineJoin.Round };
        e.Graphics.FillPath(brush, path);
        if (BorderWidth > 0) e.Graphics.DrawPath(pen, path);
    }
}

internal sealed class StatusDot : Control
{
    public Color DotColor { get; set; } = UiTheme.Offline;
    public StatusDot()
    {
        Size = new Size(12, 12);
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.ResizeRedraw | ControlStyles.UserPaint | ControlStyles.SupportsTransparentBackColor, true);
        BackColor = Color.Transparent;
        DoubleBuffered = true;
    }
    protected override void OnPaint(PaintEventArgs e)
    {
        e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
        using var b = new SolidBrush(DotColor);
        e.Graphics.FillEllipse(b, 2, 2, Math.Max(1, Width - 4), Math.Max(1, Height - 4));
    }
}

internal sealed class DarkMenuColorTable : ProfessionalColorTable
{
    public override Color ToolStripDropDownBackground => UiTheme.Surface2;
    public override Color ImageMarginGradientBegin => UiTheme.Surface2;
    public override Color ImageMarginGradientMiddle => UiTheme.Surface2;
    public override Color ImageMarginGradientEnd => UiTheme.Surface2;
    public override Color MenuItemSelected => Color.FromArgb(31, 44, 61);
    public override Color MenuItemBorder => Color.FromArgb(54, 88, 126);
    public override Color MenuBorder => Color.FromArgb(52, 66, 83);
    public override Color MenuItemSelectedGradientBegin => Color.FromArgb(31, 44, 61);
    public override Color MenuItemSelectedGradientEnd => Color.FromArgb(31, 44, 61);
    public override Color MenuItemPressedGradientBegin => Color.FromArgb(27, 39, 54);
    public override Color MenuItemPressedGradientMiddle => Color.FromArgb(27, 39, 54);
    public override Color MenuItemPressedGradientEnd => Color.FromArgb(27, 39, 54);
    public override Color SeparatorDark => UiTheme.BorderSoft;
    public override Color SeparatorLight => UiTheme.BorderSoft;
}
