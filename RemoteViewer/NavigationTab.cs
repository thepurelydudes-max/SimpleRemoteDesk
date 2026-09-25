using System.Drawing.Drawing2D;

namespace RemoteViewer;

internal sealed class NavigationTab : Control
{
    private bool _selected;
    private bool _hover;
    private readonly bool _closable;

    public string Caption { get; set; }
    public bool Selected
    {
        get => _selected;
        set { if (_selected == value) return; _selected = value; Invalidate(); }
    }

    public event Action<NavigationTab>? SelectedRequested;
    public event Action<NavigationTab>? CloseRequested;

    public NavigationTab(string caption, bool closable)
    {
        Caption = caption;
        _closable = closable;
        Width = closable ? 205 : 160;
        Height = 46;
        Margin = new Padding(0, 5, 8, 0);
        Cursor = Cursors.Hand;
        Font = new Font("Segoe UI Semibold", 9.5F, FontStyle.Bold);
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.ResizeRedraw | ControlStyles.UserPaint, true);
    }

    protected override void OnMouseEnter(EventArgs e) { _hover = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { _hover = false; Invalidate(); base.OnMouseLeave(e); }

    protected override void OnMouseUp(MouseEventArgs e)
    {
        base.OnMouseUp(e);
        if (e.Button == MouseButtons.Middle && _closable)
        {
            CloseRequested?.Invoke(this);
            return;
        }
        if (e.Button != MouseButtons.Left) return;
        Rectangle close = CloseRect();
        if (_closable && close.Contains(e.Location)) CloseRequested?.Invoke(this);
        else SelectedRequested?.Invoke(this);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
        Color bg = Selected ? UiTheme.Surface2 : _hover ? Color.FromArgb(15, 30, 51) : UiTheme.Header;
        using (var b = new SolidBrush(bg)) e.Graphics.FillRectangle(b, ClientRectangle);

        if (Selected)
        {
            using var a = new SolidBrush(UiTheme.Accent);
            e.Graphics.FillRectangle(a, 0, Height - 3, Width, 3);
        }

        // Маленький системный монитор слева.
        using (var pen = new Pen(Selected ? UiTheme.AccentHover : UiTheme.Muted2, 1.7f))
        {
            e.Graphics.DrawRectangle(pen, 15, 14, 17, 12);
            e.Graphics.DrawLine(pen, 23, 27, 23, 31);
            e.Graphics.DrawLine(pen, 18, 31, 28, 31);
        }

        Rectangle textRect = new(42, 0, Width - 50 - (_closable ? 28 : 0), Height);
        TextRenderer.DrawText(e.Graphics, Caption, Font, textRect,
            Selected ? UiTheme.Text : UiTheme.Muted,
            TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine);

        if (_closable)
        {
            Rectangle r = CloseRect();
            Color xColor = _hover ? UiTheme.Text : UiTheme.Muted2;
            using var pen = new Pen(xColor, 1.7f) { StartCap = LineCap.Round, EndCap = LineCap.Round };
            e.Graphics.DrawLine(pen, r.Left + 5, r.Top + 5, r.Right - 5, r.Bottom - 5);
            e.Graphics.DrawLine(pen, r.Right - 5, r.Top + 5, r.Left + 5, r.Bottom - 5);
        }
    }

    private Rectangle CloseRect() => new(Width - 28, (Height - 22) / 2, 22, 22);
}
