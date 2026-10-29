// Theme + owner-drawn controls (flat, rounded, navy / gold business style).
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Windows.Forms;

namespace XeroxBookSync
{
    static class Theme
    {
        public static readonly Color Navy     = Color.FromArgb(11, 31, 58);
        public static readonly Color Navy2    = Color.FromArgb(22, 58, 107);
        public static readonly Color Gold     = Color.FromArgb(200, 162, 74);
        public static readonly Color Accent   = Color.FromArgb(29, 78, 216);
        public static readonly Color AccentHi = Color.FromArgb(30, 64, 175);
        public static readonly Color AccentBg = Color.FromArgb(239, 244, 255);
        public static readonly Color Bg       = Color.FromArgb(241, 245, 249);
        public static readonly Color Card     = Color.White;
        public static readonly Color Border   = Color.FromArgb(226, 232, 240);
        public static readonly Color Text     = Color.FromArgb(15, 23, 42);
        public static readonly Color Muted    = Color.FromArgb(100, 116, 139);
        public static readonly Color Success  = Color.FromArgb(21, 128, 61);
        public static readonly Color SuccessBg = Color.FromArgb(220, 252, 231);
        public static readonly Color Warn     = Color.FromArgb(180, 83, 9);
        public static readonly Color WarnBg   = Color.FromArgb(255, 247, 237);
        public static readonly Color Danger   = Color.FromArgb(185, 28, 28);
        public static readonly Color DangerBg = Color.FromArgb(254, 242, 242);
        public static readonly Color Code     = Color.FromArgb(15, 23, 42);
        public static readonly Color CodeText = Color.FromArgb(203, 213, 225);

        static string family;
        public static string Family
        {
            get
            {
                if (family == null)
                {
                    family = "Tahoma";
                    foreach (FontFamily f in FontFamily.Families)
                        if (f.Name == "Leelawadee UI") { family = f.Name; break; }   // Thai + Latin, Windows 10/11
                }
                return family;
            }
        }
        public static Font F(float size, FontStyle style = FontStyle.Regular) { return new Font(Family, size, style); }
        public static Font Mono(float size, FontStyle style = FontStyle.Regular) { return new Font("Consolas", size, style); }

        public static GraphicsPath Round(RectangleF r, float radius)
        {
            float d = radius * 2;
            var p = new GraphicsPath();
            p.AddArc(r.X, r.Y, d, d, 180, 90);
            p.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            p.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            p.CloseFigure();
            return p;
        }

        // App logo: navy tile, white address book with a gold spine and a blue check badge.
        public static void DrawLogo(Graphics g, RectangleF r)
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            float s = r.Width;
            using (var tile = Round(r, s * 0.22f))
            using (var br = new LinearGradientBrush(r, Navy2, Navy, 45f))
                g.FillPath(br, tile);
            var book = new RectangleF(r.X + s * 0.20f, r.Y + s * 0.16f, s * 0.50f, s * 0.60f);
            using (var bp = Round(book, s * 0.05f)) g.FillPath(Brushes.White, bp);
            using (var sp = new SolidBrush(Gold)) g.FillRectangle(sp, book.X, book.Y + s * 0.02f, s * 0.08f, book.Height - s * 0.04f);
            using (var lp = new Pen(Navy2, Math.Max(1f, s * 0.035f)))
                for (int i = 0; i < 3; i++)
                {
                    float y = book.Y + book.Height * (0.28f + i * 0.2f);
                    g.DrawLine(lp, book.X + s * 0.15f, y, book.Right - s * 0.08f, y);
                }
            var badge = new RectangleF(r.X + s * 0.55f, r.Y + s * 0.55f, s * 0.34f, s * 0.34f);
            using (var bb = new SolidBrush(Accent)) g.FillEllipse(bb, badge);
            using (var wp = new Pen(Color.White, Math.Max(1.2f, s * 0.05f)))
            {
                // check mark: entries are tested before they are saved
                wp.StartCap = wp.EndCap = LineCap.Round;
                wp.LineJoin = LineJoin.Round;
                g.DrawLines(wp, new[] {
                    new PointF(badge.X + badge.Width * 0.27f, badge.Y + badge.Height * 0.52f),
                    new PointF(badge.X + badge.Width * 0.44f, badge.Y + badge.Height * 0.68f),
                    new PointF(badge.X + badge.Width * 0.74f, badge.Y + badge.Height * 0.34f) });
            }
        }

        // Writes a multi-size .ico (PNG entries) of the logo - used by build.bat.
        public static void WriteIcon(string path)
        {
            int[] sizes = { 16, 24, 32, 48, 64, 256 };
            var pngs = new byte[sizes.Length][];
            for (int i = 0; i < sizes.Length; i++)
            {
                using (var bmp = new Bitmap(sizes[i], sizes[i], PixelFormat.Format32bppArgb))
                {
                    using (Graphics g = Graphics.FromImage(bmp)) DrawLogo(g, new RectangleF(0, 0, sizes[i], sizes[i]));
                    using (var ms = new MemoryStream()) { bmp.Save(ms, ImageFormat.Png); pngs[i] = ms.ToArray(); }
                }
            }
            using (var w = new BinaryWriter(File.Create(path)))
            {
                w.Write((short)0); w.Write((short)1); w.Write((short)sizes.Length);
                int offset = 6 + 16 * sizes.Length;
                for (int i = 0; i < sizes.Length; i++)
                {
                    w.Write((byte)(sizes[i] >= 256 ? 0 : sizes[i])); w.Write((byte)(sizes[i] >= 256 ? 0 : sizes[i]));
                    w.Write((byte)0); w.Write((byte)0); w.Write((short)1); w.Write((short)32);
                    w.Write(pngs[i].Length); w.Write(offset);
                    offset += pngs[i].Length;
                }
                foreach (byte[] p in pngs) w.Write(p);
            }
        }
    }

    // White rounded card with a title.
    class Card : Panel
    {
        public string Title = "", Subtitle = "";
        public Card(string title, string subtitle)
        {
            Title = title; Subtitle = subtitle ?? "";
            DoubleBuffered = true; ResizeRedraw = true;
            BackColor = Theme.Bg;
            Padding = new Padding(18, Subtitle.Length > 0 ? 62 : 46, 18, 16);
            Margin = new Padding(8);
        }
        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            var r = new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f);
            using (var sh = Theme.Round(new RectangleF(r.X, r.Y + 2, r.Width, r.Height), 12))
            using (var sb = new SolidBrush(Color.FromArgb(10, 15, 23, 42))) g.FillPath(sb, sh);
            using (var p = Theme.Round(new RectangleF(r.X, r.Y, r.Width, r.Height - 2), 12))
            {
                g.FillPath(Brushes.White, p);
                using (var pen = new Pen(Theme.Border)) g.DrawPath(pen, p);
            }
            using (var bar = new SolidBrush(Theme.Gold)) g.FillRectangle(bar, 18, 20, 3, 18);
            using (Font f = Theme.F(11.5f, FontStyle.Bold))
                TextRenderer.DrawText(g, Title, f, new Point(28, 16), Theme.Text, TextFormatFlags.NoPadding);
            if (Subtitle.Length > 0)
                using (Font f = Theme.F(9f))
                    TextRenderer.DrawText(g, Subtitle, f, new Rectangle(28, 40, Width - 46, 20), Theme.Muted, TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
        }
    }

    enum ButtonKind { Primary, Secondary, Danger, Ghost }

    class ModernButton : Control
    {
        public ButtonKind Kind;
        bool hover, down;
        public ModernButton(string text, ButtonKind kind, int height = 40)
        {
            Kind = kind; Text = text;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint |
                     ControlStyles.SupportsTransparentBackColor | ControlStyles.ResizeRedraw, true);
            BackColor = Color.White;
            Font = Theme.F(kind == ButtonKind.Primary || kind == ButtonKind.Danger ? 10.5f : 9.5f, kind == ButtonKind.Primary || kind == ButtonKind.Danger ? FontStyle.Bold : FontStyle.Regular);
            Cursor = Cursors.Hand;
            Height = height;
            Width = TextRenderer.MeasureText(text, Font).Width + 36;
            Margin = new Padding(0, 0, 10, 0);
        }
        protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
        protected override void OnMouseLeave(EventArgs e) { hover = down = false; Invalidate(); base.OnMouseLeave(e); }
        protected override void OnMouseDown(MouseEventArgs e) { down = true; Invalidate(); base.OnMouseDown(e); }
        protected override void OnMouseUp(MouseEventArgs e) { down = false; Invalidate(); base.OnMouseUp(e); }
        protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }
        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Parent != null ? Parent.BackColor : BackColor);
            Color fill, border, text;
            if (Kind == ButtonKind.Primary)
            {
                fill = down ? Theme.Navy : hover ? Theme.AccentHi : Theme.Accent; border = fill; text = Color.White;
            }
            else if (Kind == ButtonKind.Secondary)
            {
                fill = down ? Theme.AccentBg : hover ? Color.FromArgb(248, 250, 252) : Color.White;
                border = hover ? Theme.Accent : Theme.Border; text = Theme.Accent;
            }
            else if (Kind == ButtonKind.Danger)
            {
                fill = down ? Color.FromArgb(254, 226, 226) : hover ? Theme.DangerBg : Color.White;
                border = hover ? Theme.Danger : Color.FromArgb(252, 165, 165); text = Theme.Danger;
            }
            else
            {
                fill = hover ? Theme.Bg : Color.White; border = fill; text = Theme.Text;
            }
            if (!Enabled) { fill = Theme.Bg; border = Theme.Border; text = Theme.Muted; }
            using (var p = Theme.Round(new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f), 8))
            {
                using (var b = new SolidBrush(fill)) g.FillPath(b, p);
                using (var pen = new Pen(border)) g.DrawPath(pen, p);
            }
            TextRenderer.DrawText(g, Text, Font, ClientRectangle, text,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
        }
    }

    // Label above a borderless text box inside a rounded frame that lights up on focus.
    class InputBox : Panel
    {
        public readonly TextBox Box;
        readonly Panel frame;
        readonly Label label;
        public InputBox(string caption, bool mono = false)
        {
            BackColor = Color.White;
            Height = 66;
            Margin = new Padding(0, 0, 14, 8);
            label = new Label { Text = caption, Dock = DockStyle.Top, Height = 22, ForeColor = Theme.Muted, Font = Theme.F(9f), BackColor = Color.White };
            frame = new Panel { Dock = DockStyle.Top, Height = 38, Padding = new Padding(10, 9, 10, 4), BackColor = Color.White };
            Box = new TextBox { BorderStyle = BorderStyle.None, Dock = DockStyle.Fill, Font = mono ? Theme.Mono(10.5f) : Theme.F(10.5f), ForeColor = Theme.Text, BackColor = Color.White };
            frame.Controls.Add(Box);
            frame.Paint += PaintFrame;
            Box.Enter += (s, e) => frame.Invalidate();
            Box.Leave += (s, e) => frame.Invalidate();
            frame.Click += (s, e) => Box.Focus();
            Controls.Add(frame);
            Controls.Add(label);
        }
        void PaintFrame(object sender, PaintEventArgs e)
        {
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            bool focus = Box.Focused;
            using (var p = Theme.Round(new RectangleF(0.5f, 0.5f, frame.Width - 1.5f, frame.Height - 1.5f), 7))
            using (var pen = new Pen(focus ? Theme.Accent : Theme.Border, focus ? 1.6f : 1f))
                e.Graphics.DrawPath(pen, p);
        }
    }

}
