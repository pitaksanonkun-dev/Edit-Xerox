// Main window: scan settings on the left, boards found on the right.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;
using System.Net;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace ESP32Finder
{
    // One board found on the network.
    class DeviceCard : Control
    {
        public readonly Device Dev;
        bool selected, hover;
        public event EventHandler Picked;
        public bool Selected { get { return selected; } set { selected = value; Invalidate(); } }

        public DeviceCard(Device d)
        {
            Dev = d;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
            Height = 150;
            Margin = new Padding(0, 0, 0, 12);
            Cursor = Cursors.Hand;
            BackColor = Color.White;
        }
        protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
        protected override void OnMouseLeave(EventArgs e) { hover = false; Invalidate(); base.OnMouseLeave(e); }
        protected override void OnClick(EventArgs e) { base.OnClick(e); if (Picked != null) Picked(this, EventArgs.Empty); }
        protected override void OnDoubleClick(EventArgs e) { base.OnDoubleClick(e); MainForm.OpenWeb(Dev.Ip); }

        static void Chip(Graphics g, string text, float right, float y, Color bg, Color fg, out float left)
        {
            using (Font f = Theme.F(8.5f, FontStyle.Bold))
            {
                Size cs = TextRenderer.MeasureText(text, f);
                var r = new RectangleF(right - cs.Width - 18, y, cs.Width + 18, 22);
                using (var p = Theme.Round(r, 11)) using (var b = new SolidBrush(bg)) g.FillPath(b, p);
                TextRenderer.DrawText(g, text, f, Rectangle.Round(r), fg, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
                left = r.X;
            }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Color.White);
            var r = new RectangleF(1, 1, Width - 3, Height - 3);
            using (var p = Theme.Round(r, 10))
            {
                using (var b = new SolidBrush(selected ? Theme.AccentBg : hover ? Color.FromArgb(248, 250, 252) : Color.White)) g.FillPath(b, p);
                using (var pen = new Pen(selected ? Theme.Accent : Theme.Border, selected ? 2f : 1f)) g.DrawPath(pen, p);
            }
            using (Font f = Theme.F(9f))
                TextRenderer.DrawText(g, "Xerox Host DNS Bridge" + (Dev.Fw.Length > 0 ? "  v" + Dev.Fw : ""), f, new Point(18, 14), Theme.Muted, TextFormatFlags.NoPadding);
            using (Font f = Theme.Mono(22f, FontStyle.Bold))
                TextRenderer.DrawText(g, Dev.Ip, f, new Point(16, 36), Theme.Navy, TextFormatFlags.NoPadding);

            float x = Width - 16, left;
            Chip(g, "พบแล้ว", x, 14, Theme.SuccessBg, Theme.Success, out left);
            if (Dev.Mode == "DHCP") Chip(g, "DHCP – IP อาจเปลี่ยน", left - 8, 14, Theme.WarnBg, Theme.Warn, out left);
            else if (Dev.Mode.Length > 0) Chip(g, Dev.Mode, left - 8, 14, Theme.AccentBg, Theme.Accent, out left);

            string line1 = "MAC " + (Dev.Mac.Length > 0 ? Dev.Mac : "-") + "   ·   พบจาก " + Dev.How;
            var parts = new List<string>();
            if (Dev.Hostname.Length > 0) parts.Add("ชื่อ " + Dev.Hostname);
            if (Dev.Link.Length > 0) parts.Add("LAN " + Dev.Link);
            if (Dev.Uptime.Length > 0) parts.Add("เปิดมาแล้ว " + Dev.Uptime);
            if (Dev.Hosts.Length > 0) parts.Add(Dev.Hosts);
            string line2 = parts.Count > 0 ? string.Join("   ·   ", parts) : Dev.Note;
            using (Font f = Theme.F(9.5f))   // Consolas has no Thai glyphs, so this line uses the UI font
                TextRenderer.DrawText(g, line1, f, new Rectangle(18, 82, Width - 36, 18), Theme.Text, TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
            using (Font f = Theme.F(9f))
                TextRenderer.DrawText(g, line2, f, new Rectangle(18, 104, Width - 36, 18), Dev.LoggedIn ? Theme.Text : Theme.Muted, TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
            if (Dev.PrevIp.Length > 0)
                using (Font f = Theme.F(9f, FontStyle.Bold))
                    TextRenderer.DrawText(g, "IP เปลี่ยนจากครั้งก่อน: " + Dev.PrevIp + " → " + Dev.Ip, f, new Point(18, 126), Theme.Warn, TextFormatFlags.NoPadding);
        }
    }

    class MainForm : Form
    {
        Panel header, footer;
        Label footerText, netText, progressText;
        InputBox inExtra, inUser, inPass;
        CheckBox cbLogin;
        ModernButton btnScan, btnOpen, btnCopy;
        FlowLayoutPanel results;
        Label empty;
        readonly List<DeviceCard> cards = new List<DeviceCard>();
        DeviceCard current;
        Color footerDot = Theme.Muted;
        bool scanning;

        static readonly string SettingsFile = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "ESP32Finder", "settings.txt");

        public MainForm()
        {
            AutoScaleDimensions = new SizeF(96f, 96f);
            AutoScaleMode = AutoScaleMode.Dpi;
            Text = "ESP32 Finder – Xerox Host DNS Bridge";
            Font = Theme.F(9.5f);
            BackColor = Theme.Bg;
            Rectangle wa = Screen.PrimaryScreen.WorkingArea;
            ClientSize = new Size(Math.Min(1040, wa.Width - 40), Math.Min(700, wa.Height - 60));
            MinimumSize = new Size(900, 600);
            StartPosition = FormStartPosition.CenterScreen;
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }
            ServicePointManager.DefaultConnectionLimit = 64;

            BuildHeader();
            BuildFooter();

            var body = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1, Padding = new Padding(12, 10, 12, 6), BackColor = Theme.Bg };
            body.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 390));
            body.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            body.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            Controls.Add(body);
            body.BringToFront();

            // ---- left: what to scan ----
            var cSet = new Card("ขอบเขตการค้นหา", "ค้นหาในวง LAN ของเครื่องนี้โดยอัตโนมัติ") { Dock = DockStyle.Fill };
            var form = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 9, BackColor = Color.White };
            foreach (int h in new[] { 22, 64, 66, 22, 30, 66, 66, 60 }) form.RowStyles.Add(new RowStyle(SizeType.Absolute, h));
            form.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            form.Controls.Add(new Label { Text = "วง LAN ของเครื่องนี้", Dock = DockStyle.Fill, ForeColor = Theme.Muted, Font = Theme.F(9f), BackColor = Color.White }, 0, 0);
            netText = new Label { Dock = DockStyle.Fill, ForeColor = Theme.Text, Font = Theme.Mono(9.5f), BackColor = Color.White, AutoEllipsis = true };
            form.Controls.Add(netText, 0, 1);
            inExtra = new InputBox("วงเพิ่มเติม (ถ้ามี) เช่น 192.168.30.0/24", true) { Dock = DockStyle.Fill, Margin = new Padding(0) };
            form.Controls.Add(inExtra, 0, 2);
            form.Controls.Add(new Label { Text = "คั่นด้วย , ได้หลายวง · ใช้เมื่อ ESP32 อยู่คนละวงกับเครื่องนี้", Dock = DockStyle.Fill, ForeColor = Theme.Muted, Font = Theme.F(8.5f), BackColor = Color.White }, 0, 3);
            cbLogin = new CheckBox { Text = "อ่านรายละเอียดด้วยรหัสผ่านหน้าเว็บ ESP32", Checked = true, Dock = DockStyle.Fill, Font = Theme.F(9.5f), BackColor = Color.White, Cursor = Cursors.Hand };
            form.Controls.Add(cbLogin, 0, 4);
            var cred = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1, BackColor = Color.White, Margin = new Padding(0) };
            cred.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
            cred.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
            inUser = new InputBox("ชื่อผู้ใช้", false) { Dock = DockStyle.Fill, Margin = new Padding(0, 0, 8, 0) };
            inPass = new InputBox("รหัสผ่าน", false) { Dock = DockStyle.Fill, Margin = new Padding(0) };
            inPass.Box.UseSystemPasswordChar = true;
            cred.Controls.Add(inUser, 0, 0);
            cred.Controls.Add(inPass, 1, 0);
            form.Controls.Add(cred, 0, 5);
            cbLogin.CheckedChanged += (s, e) => cred.Enabled = cbLogin.Checked;

            btnScan = new ModernButton("ค้นหา ESP32", ButtonKind.Primary, 46) { Dock = DockStyle.Fill, Margin = new Padding(0, 12, 0, 0) };
            btnScan.Click += async (s, e) => await Scan();
            form.Controls.Add(btnScan, 0, 6);
            progressText = new Label { Dock = DockStyle.Fill, ForeColor = Theme.Muted, Font = Theme.F(9f), BackColor = Color.White, Padding = new Padding(0, 8, 0, 0) };
            form.Controls.Add(progressText, 0, 7);
            cSet.Controls.Add(form);
            body.Controls.Add(cSet, 0, 0);

            // ---- right: results ----
            var cRes = new Card("อุปกรณ์ที่พบ", "คลิกเพื่อเลือก · ดับเบิลคลิกเพื่อเปิดหน้าเว็บ") { Dock = DockStyle.Fill };
            results = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, AutoScroll = true, BackColor = Color.White };
            results.Resize += (s, e) => FitCards();
            empty = new Label { Text = "ยังไม่ได้ค้นหา – กด \"ค้นหา ESP32\"", AutoSize = true, ForeColor = Theme.Muted, Font = Theme.F(10f), Margin = new Padding(4, 10, 0, 0) };
            results.Controls.Add(empty);
            cRes.Controls.Add(results);
            var tools = new FlowLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, BackColor = Color.White, WrapContents = false, Anchor = AnchorStyles.Top | AnchorStyles.Right };
            btnOpen = new ModernButton("เปิดหน้าเว็บ", ButtonKind.Secondary, 32) { Enabled = false };
            btnOpen.Click += (s, e) => { if (current != null) OpenWeb(current.Dev.Ip); };
            btnCopy = new ModernButton("คัดลอก IP", ButtonKind.Secondary, 32) { Enabled = false, Margin = new Padding(0) };
            btnCopy.Click += (s, e) => { if (current != null) { Clipboard.SetText(current.Dev.Ip); SetStatus("คัดลอก " + current.Dev.Ip + " แล้ว", Theme.Success); } };
            tools.Controls.Add(btnOpen);
            tools.Controls.Add(btnCopy);
            cRes.Controls.Add(tools);
            tools.BringToFront();
            cRes.Resize += (s, e) => tools.Location = new Point(cRes.Width - tools.Width - 16, 12);
            body.Controls.Add(cRes, 1, 0);

            LoadSettings();
            Shown += async (s, e) => { ShowNetworks(); await Scan(); };
            FormClosing += (s, e) => SaveSettings();
        }

        // ---- layout ----
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
                    TextRenderer.DrawText(g, "ESP32 Finder", t, new Point(94, 14), Color.White, TextFormatFlags.NoPadding);
                using (Font f = Theme.F(9.5f))
                    TextRenderer.DrawText(g, "ค้นหา IP ปัจจุบันของ Xerox Host DNS Bridge (ESP32-ETH01) ในเครือข่าย", f, new Point(96, 52), Color.FromArgb(191, 207, 230), TextFormatFlags.NoPadding);
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
                    TextRenderer.DrawText(e.Graphics, "Xerox Host DNS Bridge  ·  ESP32 Finder v1.0", f,
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

        void FitCards()
        {
            int w = results.ClientSize.Width - 6;
            foreach (DeviceCard c in cards) c.Width = Math.Max(300, w);
        }

        void ShowNetworks()
        {
            var own = new List<string>();
            List<IpRange> ranges = Finder.LocalRanges(own);
            netText.Text = ranges.Count == 0 ? "ไม่พบการเชื่อมต่อเครือข่าย"
                : string.Join("\n", ranges.Select(r => Finder.ToIp(r.First) + " – " + Finder.ToIp(r.Last) + "  (" + r.Label + ")"));
        }

        public static void OpenWeb(string ip)
        {
            try { Process.Start("http://" + ip + "/"); } catch { }
        }

        void Pick(DeviceCard c)
        {
            current = c;
            foreach (DeviceCard x in cards) x.Selected = x == c;
            btnOpen.Enabled = btnCopy.Enabled = c != null;
        }

        // ---- scan ----
        async Task Scan()
        {
            if (scanning) return;
            var own = new List<string>();
            List<IpRange> ranges = Finder.LocalRanges(own);
            try
            {
                foreach (string part in inExtra.Box.Text.Split(new[] { ',', ';', ' ' }, StringSplitOptions.RemoveEmptyEntries))
                    ranges.Add(Finder.ParseRange(part));
            }
            catch (FormatException ex)
            {
                MessageBox.Show(this, ex.Message, "วงเพิ่มเติมไม่ถูกต้อง", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            scanning = true;
            btnScan.Enabled = false;
            results.Controls.Clear();
            cards.Clear();
            Pick(null);
            empty.Text = "กำลังค้นหา…";
            results.Controls.Add(empty);
            SaveSettings();

            Dictionary<string, string> known = Finder.LoadKnown();
            var order = new List<string>();
            order.AddRange(known.Values);                                         // where it was last time
            SetStatus("กำลังถามชื่อ " + Finder.DefaultHostname + " …", Theme.Accent);
            order.AddRange(await Finder.ResolveName(Finder.DefaultHostname, 1500)); // DHCP hostname, if the router registers it
            foreach (IpRange r in ranges)
                for (uint v = r.First; v <= r.Last; v++) order.Add(Finder.ToIp(v));
            List<string> ips = order.Distinct().Where(ip => !own.Contains(ip)).ToList();
            var hinted = new HashSet<string>(known.Values);

            var found = new List<Device>();
            int done = 0;
            var sem = new SemaphoreSlim(64);
            var timer = new System.Windows.Forms.Timer { Interval = 150 };
            timer.Tick += (s, e) => progressText.Text = "ตรวจแล้ว " + done + " / " + ips.Count + " IP · พบ " + found.Count + " เครื่อง";
            timer.Start();
            SetStatus("กำลังค้นหาใน " + ips.Count + " IP …", Theme.Accent);
            var sw = Stopwatch.StartNew();

            IEnumerable<Task> jobs = ips.Select(async ip =>
            {
                await sem.WaitAsync();
                try
                {
                    if (!await Finder.PortOpen(ip, 80, 800)) return;
                    if (!await Task.Run(() => Finder.IsBridge(ip))) return;
                    var d = new Device { Ip = ip, How = hinted.Contains(ip) ? "IP ครั้งก่อน" : "การกวาดวง LAN", Note = "กำลังอ่านรายละเอียด…" };
                    lock (found) found.Add(d);
                    AddCard(d);
                }
                finally { sem.Release(); Interlocked.Increment(ref done); }
            });
            await Task.WhenAll(jobs.ToList());
            timer.Stop();

            // details: MAC by ARP, then the status page when a login is given
            string user = inUser.Box.Text.Trim(), pass = inPass.Box.Text;
            foreach (Device d in found)
            {
                Device dev = d;
                string msg = await Task.Run(() =>
                {
                    dev.Mac = Finder.MacOf(dev.Ip);
                    return cbLogin.Checked ? Finder.LoadStatus(dev, user, pass) : null;
                });
                dev.Note = msg ?? (cbLogin.Checked ? "" : "ติ๊ก \"อ่านรายละเอียด\" เพื่อดูเวอร์ชัน / DHCP / ความเร็ว LAN");
                string prev;
                if (dev.Mac.Length > 0 && known.TryGetValue(dev.Mac, out prev) && prev != dev.Ip) dev.PrevIp = prev;
                if (dev.Mac.Length > 0) known[dev.Mac] = dev.Ip;
                foreach (DeviceCard c in cards) c.Invalidate();
            }
            Finder.SaveKnown(known);

            progressText.Text = "ตรวจ " + ips.Count + " IP ใน " + (sw.ElapsedMilliseconds / 1000.0).ToString("0.0") + " วินาที · พบ " + found.Count + " เครื่อง";
            if (found.Count == 0)
            {
                empty.Text = "ไม่พบ ESP32 ในวงที่ค้นหา\n\n• ตรวจไฟเลี้ยงและสาย LAN ของ ESP32 (รอ 20 วินาทีหลังเปิดเครื่อง)\n• ถ้า ESP32 อยู่คนละวง ให้ใส่ \"วงเพิ่มเติม\"\n• Firewall / VLAN อาจกั้นพอร์ต 80 ระหว่างวง";
                SetStatus("ไม่พบ ESP32", Theme.Warn);
            }
            else
            {
                Pick(cards[0]);
                SetStatus("พบ ESP32 " + found.Count + " เครื่อง · IP " + string.Join(", ", found.Select(x => x.Ip)), Theme.Success);
            }
            btnScan.Enabled = true;
            scanning = false;
        }

        void AddCard(Device d)
        {
            if (results.Controls.Contains(empty)) results.Controls.Remove(empty);
            var c = new DeviceCard(d);
            c.Picked += (s, e) => Pick((DeviceCard)s);
            cards.Add(c);
            results.Controls.Add(c);
            FitCards();
        }

        // ---- settings (the password is not stored) ----
        void LoadSettings()
        {
            inUser.Box.Text = "admin";
            inPass.Box.Text = "admin";
            try
            {
                if (!File.Exists(SettingsFile)) return;
                string[] l = File.ReadAllLines(SettingsFile);
                if (l.Length > 0) inExtra.Box.Text = l[0];
                if (l.Length > 1) inUser.Box.Text = l[1];
                if (l.Length > 2) cbLogin.Checked = l[2] == "1";
            }
            catch { }
        }

        void SaveSettings()
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(SettingsFile));
                File.WriteAllLines(SettingsFile, new[] { inExtra.Box.Text.Trim(), inUser.Box.Text.Trim(), cbLogin.Checked ? "1" : "0" });
            }
            catch { }
        }
    }
}
