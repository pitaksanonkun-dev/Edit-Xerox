// Main window: test settings (ports, sender IPs, options), live status of this PC and an activity log.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace SmbPortTester
{
    // Key / value rows with a colored dot: green = ok, amber = attention, red = problem, gray = not set.
    class StatusView : Control
    {
        public class Row { public string Key, Value; public Color Dot; }
        public List<Row> Rows = new List<Row>();
        public StatusView()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
            BackColor = Color.White;
        }
        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Color.White);
            int y = 0, kw = 196;
            using (Font kf = Theme.F(9f))
            using (Font vf = Theme.F(9.5f, FontStyle.Bold))
            using (var line = new Pen(Theme.Border))
                for (int i = 0; i < Rows.Count; i++)
                {
                    Row r = Rows[i];
                    using (var b = new SolidBrush(r.Dot)) g.FillEllipse(b, 2, y + 12, 9, 9);
                    TextRenderer.DrawText(g, r.Key, kf, new Rectangle(20, y, kw - 24, 34), Theme.Muted, TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
                    TextRenderer.DrawText(g, r.Value, vf, new Rectangle(kw, y, Width - kw, 34), Theme.Text, TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
                    y += 34;
                    if (i < Rows.Count - 1) g.DrawLine(line, 20, y - 1, Width, y - 1);
                }
        }
    }

    // Blue box that shows what to type into the Xerox address-book entry.
    class XeroxHint : Control
    {
        public string Ip = "-", Port = "-", Sender = "-";
        public XeroxHint()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
            BackColor = Color.White;
        }
        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Color.White);
            using (var p = Theme.Round(new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f), 10))
            {
                using (var b = new SolidBrush(Theme.AccentBg)) g.FillPath(b, p);
                using (var pen = new Pen(Color.FromArgb(191, 207, 250))) g.DrawPath(pen, p);
            }
            using (Font h = Theme.F(9f, FontStyle.Bold))
            using (Font k = Theme.F(8.5f))
            using (Font v = Theme.Mono(13f, FontStyle.Bold))
            {
                TextRenderer.DrawText(g, "ตั้งค่าในเครื่อง Xerox (SMB · Address Type = IPv4 Address)", h, new Point(14, 10), Theme.Navy, TextFormatFlags.NoPadding);
                int w = Width - 28;
                int[] xs = { 14, 14 + w * 44 / 100, 14 + w * 63 / 100, 14 + w };
                string[] keys = { "IP Address", "Port", "เครื่องผู้ส่งที่อนุญาต" };
                string[] vals = { Ip, Port, Sender };
                for (int i = 0; i < 3; i++)
                {
                    int x = xs[i], col = xs[i + 1] - xs[i];
                    TextRenderer.DrawText(g, keys[i], k, new Point(x, 36), Theme.Muted, TextFormatFlags.NoPadding);
                    using (Font vf = i == 2 ? Theme.Mono(10f, FontStyle.Bold) : null)
                        TextRenderer.DrawText(g, vals[i], vf ?? v, new Rectangle(x, 54, col - 6, 26), Theme.Accent, TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis | TextFormatFlags.VerticalCenter);
                }
            }
        }
    }

    class MainForm : Form
    {
        Panel header, footer;
        InputBox inPorts, inSenders;
        CheckBox cbPrivate, cbStrict, cbLoop;
        XeroxHint hint;
        StatusView status;
        RichTextBox log;
        Label footerText;
        Color footerDot = Theme.Muted;
        ModernButton btnApply, btnRevert, btnTest, btnRefresh;
        readonly Engine engine = new Engine();
        Snapshot snap = new Snapshot();
        bool busy;

        static readonly string SettingsFile = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "SmbPortTester", "last.txt");

        public MainForm()
        {
            AutoScaleDimensions = new SizeF(96f, 96f);
            AutoScaleMode = AutoScaleMode.Dpi;
            Text = "SMB Port Tester – Xerox Host DNS Bridge";
            Font = Theme.F(9.5f);
            BackColor = Theme.Bg;
            Rectangle wa = Screen.PrimaryScreen.WorkingArea;
            ClientSize = new Size(Math.Min(1100, wa.Width - 40), Math.Min(800, wa.Height - 60));
            MinimumSize = new Size(980, 680);
            StartPosition = FormStartPosition.CenterScreen;
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }

            BuildHeader();
            BuildFooter();

            var body = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1, Padding = new Padding(12, 10, 12, 6), BackColor = Theme.Bg };
            body.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 470));
            body.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            body.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            Controls.Add(body);
            body.BringToFront();

            // ---- left: settings ----
            var cSet = new Card("ตั้งค่าการทดสอบ", "เปิดพอร์ตเพิ่มให้ Xerox ส่ง Scan เข้า SMB (445) ของเครื่องนี้") { Dock = DockStyle.Fill };
            var form = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 10, BackColor = Color.White };
            foreach (int h in new[] { 66, 24, 66, 24, 30, 30, 30, 24, 96, 100 })
                form.RowStyles.Add(new RowStyle(SizeType.Absolute, h));
            form.RowStyles.Add(new RowStyle(SizeType.Percent, 100));

            inPorts = new InputBox("พอร์ตที่ต้องการเปิด", true) { Dock = DockStyle.Fill, Margin = new Padding(0) };
            form.Controls.Add(inPorts, 0, 0);
            form.Controls.Add(Hint("เช่น 4451   ·   หลายพอร์ต 4451,4452   ·   เป็นช่วง 4451-4460"), 0, 1);
            inSenders = new InputBox("IP เครื่องผู้ส่ง (Xerox) ที่อนุญาต", true) { Dock = DockStyle.Fill, Margin = new Padding(0) };
            form.Controls.Add(inSenders, 0, 2);
            form.Controls.Add(Hint("คั่นด้วย , ได้หลายเครื่อง หรือใส่เป็นวง เช่น 192.168.20.0/24   ·   เว้นว่าง = ทุก IP"), 0, 3);

            cbPrivate = Check("เปิดเฉพาะเครือข่าย Domain / Private (ไม่เปิดใน Public)", true);
            cbStrict = Check("ปิด Strict Name Checking (DisableStrictNameChecking)", true);
            cbLoop = Check("ปิด Loopback Check (DisableLoopbackCheck) – ไม่แนะนำ", false);
            cbLoop.ForeColor = Theme.Danger;
            cbLoop.CheckedChanged += (s, e) =>
            {
                if (cbLoop.Checked && MessageBox.Show(this,
                    "DisableLoopbackCheck จะปิดการป้องกันการโจมตีแบบ NTLM Reflection ของ Windows\n\n" +
                    "โดยปกติไม่จำเป็นสำหรับการทดสอบนี้ ให้เปิดเฉพาะเมื่อทดสอบแล้วล็อกอินไม่ผ่านเท่านั้น\n" +
                    "และควรกด \"ปิดพอร์ตและคืนค่า\" ทันทีเมื่อทดสอบเสร็จ\n\nต้องการเปิดตัวเลือกนี้หรือไม่?",
                    "คำเตือนด้านความปลอดภัย", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes)
                    cbLoop.Checked = false;
            };
            form.Controls.Add(cbPrivate, 0, 4);
            form.Controls.Add(cbStrict, 0, 5);
            form.Controls.Add(cbLoop, 0, 6);
            form.Controls.Add(Hint("ค่า Registry ทั้งสองมีผลหลังรีสตาร์ทเครื่อง · ทดสอบด้วย IP ไม่ต้องรีสตาร์ท"), 0, 7);

            hint = new XeroxHint { Dock = DockStyle.Fill, Margin = new Padding(0, 6, 0, 6) };
            form.Controls.Add(hint, 0, 8);

            var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, BackColor = Color.White, WrapContents = true, Margin = new Padding(0, 8, 0, 0) };
            btnApply = new ModernButton("เปิดพอร์ต", ButtonKind.Primary, 44);
            btnApply.Width = 150;
            btnApply.Click += (s, e) => DoApply();
            btnRevert = new ModernButton("ปิดพอร์ตและคืนค่า", ButtonKind.Danger, 44);
            btnRevert.Width = 200;
            btnRevert.Click += (s, e) => DoRevert();
            actions.Controls.Add(btnApply);
            actions.Controls.Add(btnRevert);
            form.Controls.Add(actions, 0, 9);
            cSet.Controls.Add(form);
            body.Controls.Add(cSet, 0, 0);

            // ---- right: status + log ----
            var right = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 2, Margin = new Padding(0), BackColor = Theme.Bg };
            right.RowStyles.Add(new RowStyle(SizeType.Absolute, 340));
            right.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            body.Controls.Add(right, 1, 0);

            var cStat = new Card("สถานะปัจจุบันของเครื่องนี้", null) { Dock = DockStyle.Fill };
            status = new StatusView { Dock = DockStyle.Fill };
            cStat.Controls.Add(status);
            var tools = new FlowLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, BackColor = Color.White, WrapContents = false, Anchor = AnchorStyles.Top | AnchorStyles.Right };
            btnTest = new ModernButton("ทดสอบ SMB", ButtonKind.Secondary, 32);
            btnTest.Click += (s, e) => DoTest();
            btnRefresh = new ModernButton("รีเฟรช", ButtonKind.Secondary, 32) { Margin = new Padding(0) };
            btnRefresh.Click += (s, e) => RefreshStatus();
            tools.Controls.Add(btnTest);
            tools.Controls.Add(btnRefresh);
            cStat.Controls.Add(tools);
            tools.BringToFront();
            cStat.Resize += (s, e) => tools.Location = new Point(cStat.Width - tools.Width - 16, 12);
            right.Controls.Add(cStat, 0, 0);

            var cLog = new Card("บันทึกการทำงาน", null) { Dock = DockStyle.Fill };
            var code = new Panel { Dock = DockStyle.Fill, BackColor = Color.White, Padding = new Padding(14, 12, 6, 12) };
            code.Paint += (s, e) =>
            {
                e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
                using (var p = Theme.Round(new RectangleF(0, 0, code.Width - 1, code.Height - 1), 10))
                using (var b = new SolidBrush(Theme.Code)) e.Graphics.FillPath(b, p);
            };
            code.Resize += (s, e) => code.Invalidate();
            log = new RichTextBox { Dock = DockStyle.Fill, ReadOnly = true, BorderStyle = BorderStyle.None, BackColor = Theme.Code, ForeColor = Theme.CodeText, Font = Theme.F(9.5f), ScrollBars = RichTextBoxScrollBars.Vertical, DetectUrls = false };
            code.Controls.Add(log);
            cLog.Controls.Add(code);
            var clear = new ModernButton("ล้าง", ButtonKind.Ghost, 30) { Anchor = AnchorStyles.Top | AnchorStyles.Right };
            clear.Click += (s, e) => log.Clear();
            cLog.Controls.Add(clear);
            clear.BringToFront();
            cLog.Resize += (s, e) => clear.Location = new Point(cLog.Width - clear.Width - 16, 12);
            right.Controls.Add(cLog, 0, 1);

            engine.Log += (k, s) => BeginInvoke((Action)(() => Append(k, s)));
            inPorts.Box.TextChanged += (s, e) => UpdateHint();
            inSenders.Box.TextChanged += (s, e) => UpdateHint();
            LoadLast();
            Load += (s, e) =>
            {
                RefreshStatus();
                Append(LogKind.Info, "พร้อมใช้งาน — ใส่พอร์ตและ IP เครื่อง Xerox แล้วกด \"เปิดพอร์ต\" จากนั้นกด Destination Test ที่เครื่อง Xerox");
            };
            FormClosing += (s, e) => SaveLast();
        }

        // ---- layout helpers ----
        static Label Hint(string text)
        {
            return new Label { Text = text, Dock = DockStyle.Fill, ForeColor = Theme.Muted, Font = Theme.F(8.5f), BackColor = Color.White, Margin = new Padding(0), Padding = new Padding(2, 2, 0, 0) };
        }

        static CheckBox Check(string text, bool on)
        {
            return new CheckBox { Text = text, Checked = on, Dock = DockStyle.Fill, Font = Theme.F(9.5f), ForeColor = Theme.Text, BackColor = Color.White, Margin = new Padding(0), Cursor = Cursors.Hand };
        }

        void BuildHeader()
        {
            header = new Panel { Dock = DockStyle.Top, Height = 92 };
            typeof(Panel).GetProperty("DoubleBuffered", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance).SetValue(header, true, null);
            header.Paint += (s, e) =>
            {
                Graphics g = e.Graphics;
                g.SmoothingMode = SmoothingMode.AntiAlias;
                Rectangle r = header.ClientRectangle;
                using (var br = new LinearGradientBrush(r, Theme.Navy, Theme.Navy2, 0f)) g.FillRectangle(br, r);
                using (var gold = new SolidBrush(Theme.Gold)) g.FillRectangle(gold, 0, r.Height - 3, r.Width, 3);
                Theme.DrawLogo(g, new RectangleF(24, 17, 56, 56));
                using (Font t = Theme.F(18f, FontStyle.Bold))
                    TextRenderer.DrawText(g, "SMB Port Tester", t, new Point(94, 14), Color.White, TextFormatFlags.NoPadding);
                using (Font f = Theme.F(9.5f))
                    TextRenderer.DrawText(g, "ทดสอบให้ Xerox ส่ง Scan (SMB) เข้าเครื่องนี้ผ่านพอร์ตอื่นนอกจาก 445 · คืนค่าได้ในคลิกเดียว", f, new Point(96, 52), Color.FromArgb(191, 207, 230), TextFormatFlags.NoPadding);
                bool admin = Program.IsAdmin;
                string chip = admin ? "Administrator" : "ไม่ใช่ Administrator";
                using (Font f = Theme.F(9f, FontStyle.Bold))
                {
                    Size cs = TextRenderer.MeasureText(chip, f);
                    var cr = new RectangleF(r.Width - cs.Width - 54, 32, cs.Width + 30, 28);
                    using (var p = Theme.Round(cr, 14)) using (var b = new SolidBrush(admin ? Color.FromArgb(40, 255, 255, 255) : Theme.DangerBg)) g.FillPath(b, p);
                    using (var b = new SolidBrush(admin ? Color.FromArgb(134, 239, 172) : Theme.Danger)) g.FillEllipse(b, cr.X + 11, cr.Y + 10, 8, 8);
                    TextRenderer.DrawText(g, chip, f, new Rectangle((int)cr.X + 24, (int)cr.Y, cs.Width + 4, (int)cr.Height), admin ? Color.White : Theme.Danger, TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding);
                }
            };
            header.Resize += (s, e) => header.Invalidate();
            Controls.Add(header);
        }

        void BuildFooter()
        {
            footer = new Panel { Dock = DockStyle.Bottom, Height = 34, BackColor = Color.White };
            footer.Paint += (s, e) =>
            {
                using (var pen = new Pen(Theme.Border)) e.Graphics.DrawLine(pen, 0, 0, footer.Width, 0);
                e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
                using (var b = new SolidBrush(footerDot)) e.Graphics.FillEllipse(b, 18, 13, 8, 8);
                using (Font f = Theme.F(8.5f))
                    TextRenderer.DrawText(e.Graphics, "Xerox Host DNS Bridge  ·  SMB Port Tester v1.0", f,
                        new Rectangle(0, 0, footer.Width - 18, footer.Height), Theme.Muted, TextFormatFlags.Right | TextFormatFlags.VerticalCenter);
            };
            footerText = new Label { AutoSize = true, Location = new Point(32, 9), ForeColor = Theme.Text, Font = Theme.F(9f), BackColor = Color.White };
            footer.Controls.Add(footerText);
            Controls.Add(footer);
        }

        void SetStatus(string text, Color dot)
        {
            footerText.Text = text;
            footerDot = dot;
            footer.Invalidate();
        }

        void Append(LogKind k, string text)
        {
            Color c = k == LogKind.Ok ? Color.FromArgb(134, 239, 172) : k == LogKind.Warn ? Color.FromArgb(253, 186, 116)
                    : k == LogKind.Error ? Color.FromArgb(252, 165, 165) : k == LogKind.Cmd ? Color.FromArgb(148, 163, 184) : Theme.CodeText;
            string mark = k == LogKind.Ok ? "[ผ่าน] " : k == LogKind.Warn ? "[!] " : k == LogKind.Error ? "[ไม่ผ่าน] " : k == LogKind.Cmd ? "> " : "• ";
            text = text.Replace("→", "->");
            log.SelectionStart = log.TextLength;
            log.SelectionColor = Color.FromArgb(100, 116, 139);
            log.SelectionFont = Theme.Mono(9f);
            log.AppendText(DateTime.Now.ToString("HH:mm:ss") + "  ");
            log.SelectionColor = c;
            log.SelectionFont = k == LogKind.Cmd ? Theme.Mono(9f) : Theme.F(9.5f, k == LogKind.Info ? FontStyle.Regular : FontStyle.Bold);
            log.AppendText(mark + text + "\n");
            log.ScrollToCaret();
        }

        // ---- status ----
        void RefreshStatus()
        {
            snap = Engine.Read();
            var rows = new List<StatusView.Row>();
            Color ok = Theme.Success, warn = Color.FromArgb(217, 119, 6), bad = Theme.Danger, off = Color.FromArgb(203, 213, 225);

            rows.Add(new StatusView.Row { Key = "IP เครื่องนี้", Value = snap.LocalIps.Count > 0 ? string.Join(", ", snap.LocalIps) : "ไม่พบเครือข่าย", Dot = snap.LocalIps.Count > 0 ? ok : bad });
            bool smb = snap.Listening.Contains(445);
            rows.Add(new StatusView.Row { Key = "SMB พอร์ต 445 (Windows)", Value = smb ? "ทำงานปกติ" : "ไม่ทำงาน", Dot = smb ? ok : bad });
            var prof = new List<string>();
            if ((snap.Profiles & 1) != 0) prof.Add("Domain");
            if ((snap.Profiles & 2) != 0) prof.Add("Private");
            if ((snap.Profiles & 4) != 0) prof.Add("Public");
            bool publicOnly = (snap.Profiles & 4) != 0 && (snap.Profiles & 3) == 0;
            rows.Add(new StatusView.Row
            {
                Key = "ประเภทเครือข่าย",
                Value = prof.Count == 0 ? "?" : string.Join(", ", prof) + (publicOnly ? "  – กฎแบบ Domain/Private จะไม่ทำงาน" : ""),
                Dot = publicOnly ? warn : ok
            });
            rows.Add(new StatusView.Row { Key = "บริการ IP Helper", Value = snap.IpHelper == "Running" ? "ทำงาน (Running)" : snap.IpHelper, Dot = snap.IpHelper == "Running" ? ok : warn });

            List<Proxy> ours = snap.Proxies.Where(p => p.Managed).ToList();
            if (ours.Count == 0) rows.Add(new StatusView.Row { Key = "พอร์ตทดสอบที่เปิดอยู่", Value = "ไม่มี (เหลือแค่ 445)", Dot = off });
            else
            {
                List<int> ports = ours.Select(p => p.ListenPort).Distinct().ToList();
                int up = ports.Count(p => snap.Listening.Contains(p));
                rows.Add(new StatusView.Row
                {
                    Key = "พอร์ตทดสอบที่เปิดอยู่",
                    Value = Engine.PortsText(ports) + " → 445   (" + (up == ports.Count ? "รอรับครบ " + up + " พอร์ต" : "รอรับ " + up + "/" + ports.Count) + ")",
                    Dot = up == ports.Count ? ok : warn
                });
            }

            FwRule rule = snap.Rules.FirstOrDefault(r => r.Name == Engine.RuleName);
            FwRule legacy = snap.Rules.FirstOrDefault(r => r.Name != Engine.RuleName);
            if (snap.FwError != null) rows.Add(new StatusView.Row { Key = "Firewall", Value = "อ่านไม่ได้: " + snap.FwError, Dot = warn });
            else if (rule != null)
                rows.Add(new StatusView.Row
                {
                    Key = "Firewall",
                    Value = "TCP " + rule.Ports + "  ·  จาก " + (rule.Remote == "*" ? "ทุก IP" : rule.Remote.Replace("/255.255.255.255", "")) + "  ·  " + ((rule.Profiles & 4) != 0 ? "ทุกเครือข่าย" : "Domain/Private"),
                    Dot = (rule.Profiles & snap.Profiles) == 0 ? bad : rule.Remote == "*" || (rule.Profiles & 4) != 0 ? warn : ok
                });
            else rows.Add(new StatusView.Row { Key = "Firewall", Value = "ไม่มีกฎทดสอบ", Dot = off });
            if (legacy != null) rows.Add(new StatusView.Row { Key = "กฎ Firewall เดิม", Value = "\"" + legacy.Name + "\" เปิดให้ทุก IP – กดคืนค่าเพื่อลบ", Dot = warn });

            rows.Add(new StatusView.Row { Key = "DisableStrictNameChecking", Value = snap.Strict.HasValue ? snap.Strict.Value + (snap.Strict == 1 ? " (ปิดการตรวจชื่อ)" : "") : "ค่าเริ่มต้น Windows", Dot = snap.Strict == 1 ? warn : off });
            rows.Add(new StatusView.Row { Key = "DisableLoopbackCheck", Value = snap.Loopback.HasValue ? snap.Loopback.Value + (snap.Loopback == 1 ? " (ลดความปลอดภัย)" : "") : "ค่าเริ่มต้น Windows", Dot = snap.Loopback == 1 ? bad : off });
            status.Rows = rows;
            status.Invalidate();
            UpdateHint();

            if (ours.Count > 0) SetStatus("เปิดพอร์ตทดสอบอยู่ — ทดสอบเสร็จแล้วกด \"ปิดพอร์ตและคืนค่า\"", warn);
            else SetStatus("ไม่มีพอร์ตทดสอบเปิดอยู่ · ใช้ SMB 445 ตามปกติ", ok);
        }

        void UpdateHint()
        {
            hint.Ip = snap.LocalIps.Count > 0 ? snap.LocalIps[0] : "-";
            try { hint.Port = Engine.PortsText(Engine.ParsePorts(inPorts.Box.Text)); } catch { hint.Port = "?"; }
            try
            {
                List<string> s = Engine.ParseSenders(inSenders.Box.Text);
                hint.Sender = s.Count > 0 ? string.Join(", ", s) : "ทุก IP";
            }
            catch { hint.Sender = "?"; }
            hint.Invalidate();
        }

        // ---- actions ----
        bool CheckAdmin()
        {
            if (Program.IsAdmin) return true;
            MessageBox.Show(this, "ต้องเปิดโปรแกรมแบบ Administrator\n(คลิกขวาที่ SmbPortTester.exe → Run as administrator)", "ต้องใช้สิทธิ์ Administrator", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return false;
        }

        void SetBusy(bool b)
        {
            busy = b;
            foreach (Control c in new Control[] { btnApply, btnRevert, btnTest, btnRefresh, inPorts, inSenders, cbPrivate, cbStrict, cbLoop }) c.Enabled = !b;
            Cursor = b ? Cursors.WaitCursor : Cursors.Default;
        }

        async void Run(Func<bool> work, string doing)
        {
            if (busy) return;
            SetBusy(true);
            SetStatus(doing, Theme.Accent);
            try { await Task.Run(work); }
            catch (Exception ex) { Append(LogKind.Error, "เกิดข้อผิดพลาด: " + ex.Message); }
            SetBusy(false);
            RefreshStatus();
        }

        void DoApply()
        {
            if (!CheckAdmin()) return;
            var o = new Options { PrivateOnly = cbPrivate.Checked, StrictName = cbStrict.Checked, Loopback = cbLoop.Checked };
            try
            {
                o.Ports = Engine.ParsePorts(inPorts.Box.Text);
                o.Senders = Engine.ParseSenders(inSenders.Box.Text);
            }
            catch (FormatException ex)
            {
                MessageBox.Show(this, ex.Message, "ข้อมูลไม่ถูกต้อง", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            if (o.Senders.Count == 0 && MessageBox.Show(this,
                "ไม่ได้ระบุ IP เครื่องผู้ส่ง — ทุกเครื่องในเครือข่ายจะเข้า SMB ผ่านพอร์ตนี้ได้\n\nต้องการดำเนินการต่อหรือไม่?",
                "ยืนยัน", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes) return;
            bool publicOnly = (snap.Profiles & 4) != 0 && (snap.Profiles & 3) == 0;
            if (o.PrivateOnly && publicOnly)
            {
                DialogResult r = MessageBox.Show(this,
                    "เครือข่ายของเครื่องนี้ตั้งเป็น \"Public\" — Windows Firewall จะบล็อก Xerox ถ้าเปิดกฎเฉพาะ Domain/Private\n\n" +
                    "Yes  =  เปลี่ยนเครือข่ายเป็น Private (แนะนำสำหรับ LAN ในสำนักงาน)\n" +
                    "           และคืนเป็น Public ให้เมื่อกด \"ปิดพอร์ตและคืนค่า\"\n" +
                    "No   =  คงเป็น Public แต่เปิดกฎใน Public ด้วย (ยังจำกัดเฉพาะ IP เครื่องผู้ส่ง)\n" +
                    "Cancel = ยกเลิก",
                    "เครือข่ายเป็น Public", MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);
                if (r == DialogResult.Cancel) return;
                if (r == DialogResult.Yes) o.MakePrivate = true;
                else o.PrivateOnly = false;
            }
            SaveLast();
            Run(() => engine.Apply(o), "กำลังเปิดพอร์ต…");
        }

        void DoRevert()
        {
            if (!CheckAdmin()) return;
            if (MessageBox.Show(this,
                "ปิดพอร์ตทดสอบทั้งหมด ลบกฎ Firewall และคืนค่า Registry กลับเป็นค่าเดิม\nเหลือเฉพาะ SMB พอร์ต 445 ตามปกติของ Windows\n\nดำเนินการหรือไม่?",
                "ปิดพอร์ตและคืนค่า", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return;
            Run(() => engine.Revert(), "กำลังคืนค่า…");
        }

        void DoTest()
        {
            List<int> ports = snap.Proxies.Where(p => p.Managed).Select(p => p.ListenPort).Distinct().ToList();
            Run(() => { engine.TestPorts(ports); return true; }, "กำลังทดสอบ SMB…");
        }

        // ---- last used values ----
        void LoadLast()
        {
            inPorts.Box.Text = "4451";
            inSenders.Box.Text = "192.168.20.109";
            try
            {
                if (!File.Exists(SettingsFile)) return;
                string[] l = File.ReadAllLines(SettingsFile);
                if (l.Length > 0) inPorts.Box.Text = l[0];
                if (l.Length > 1) inSenders.Box.Text = l[1];
                if (l.Length > 2) cbPrivate.Checked = l[2] == "1";
                if (l.Length > 3) cbStrict.Checked = l[3] == "1";
            }
            catch { }
        }

        void SaveLast()
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(SettingsFile));
                File.WriteAllLines(SettingsFile, new[] { inPorts.Box.Text.Trim(), inSenders.Box.Text.Trim(), cbPrivate.Checked ? "1" : "0", cbStrict.Checked ? "1" : "0" });
            }
            catch { }
        }
    }
}
