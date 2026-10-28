// Main window: header with steps, machine summary, adapter cards, editable fields,
// save location and a JSON preview.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;
using System.Windows.Forms;

namespace MacCollector
{
    // Big "this computer" panel: name, MAC, IPv4 and a readiness chip.
    class SummaryView : Control
    {
        public string HostName = "", Mac = "", IPv4 = "", Owner = "", Chip = "", ChipKind = "ok";
        public SummaryView()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
            BackColor = Color.White;
        }
        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Color.White);
            using (Font small = Theme.F(8.8f))
            using (Font big = Theme.F(19f, FontStyle.Bold))
            using (Font mac = Theme.Mono(16f, FontStyle.Bold))
            using (Font mid = Theme.F(9.5f))
            {
                TextRenderer.DrawText(g, "ชื่อเครื่อง (Computer Name)", small, new Point(0, 0), Theme.Muted, TextFormatFlags.NoPadding);
                TextRenderer.DrawText(g, HostName.Length > 0 ? HostName : "-", big, new Rectangle(0, 18, Width, 38), Theme.Navy, TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
                TextRenderer.DrawText(g, "MAC Address", small, new Point(0, 62), Theme.Muted, TextFormatFlags.NoPadding);
                TextRenderer.DrawText(g, Mac.Length > 0 ? Mac : "-", mac, new Point(0, 80), Theme.Accent, TextFormatFlags.NoPadding);
                TextRenderer.DrawText(g, IPv4, mid, new Rectangle(0, 112, Width, 20), Theme.Text, TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
                TextRenderer.DrawText(g, Owner, small, new Rectangle(0, 132, Width, 18), Theme.Muted, TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
            }
            if (Chip.Length > 0)
                using (Font f = Theme.F(8.8f, FontStyle.Bold))
                {
                    Size cs = TextRenderer.MeasureText(Chip, f);
                    var cr = new RectangleF(0, Height - 28, cs.Width + 30, 26);
                    Color bg = ChipKind == "ok" ? Theme.SuccessBg : Theme.WarnBg, fg = ChipKind == "ok" ? Theme.Success : Theme.Warn;
                    using (var p = Theme.Round(cr, 13)) using (var b = new SolidBrush(bg)) g.FillPath(b, p);
                    using (var b = new SolidBrush(fg)) g.FillEllipse(b, cr.X + 11, cr.Y + 10, 7, 7);
                    TextRenderer.DrawText(g, Chip, f, new Rectangle((int)cr.X + 22, (int)cr.Y, cs.Width + 6, (int)cr.Height), fg, TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding);
                }
        }
    }

    class MainForm : Form
    {
        Panel header, footer;
        SummaryView summary;
        FlowLayoutPanel adapterList;
        InputBox inName, inMac, inPort, in4, in6, inAliases, inNote, inFolder, inFile;
        Label warn, footerText, savedText;
        LinkLabel openLink;
        TextBox json;
        List<AdapterInfo> adapters = new List<AdapterInfo>();
        List<AdapterCard> cards = new List<AdapterCard>();
        AdapterInfo current;
        string owner = "";
        string lastSaved = "";
        int step = 1;
        bool loading;
        Color footerDot = Theme.Success;

        static readonly string SettingsFile = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "MacCollector", "lastdir.txt");

        public MainForm()
        {
            AutoScaleDimensions = new SizeF(96f, 96f);
            AutoScaleMode = AutoScaleMode.Dpi;
            Text = "MacCollector – Xerox Host DNS Bridge";
            Font = Theme.F(9.5f);
            BackColor = Theme.Bg;
            Rectangle wa = Screen.PrimaryScreen.WorkingArea;
            ClientSize = new Size(Math.Min(1120, wa.Width - 40), Math.Min(900, wa.Height - 60));   // fits 1366x768 laptops too
            MinimumSize = new Size(1000, 700);
            StartPosition = FormStartPosition.CenterScreen;
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }

            BuildHeader();
            BuildFooter();

            var body = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1, Padding = new Padding(12, 10, 12, 6), BackColor = Theme.Bg };
            body.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 410));
            body.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            body.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            Controls.Add(body);
            body.BringToFront();

            // ---- left column ----
            var left = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 2, Margin = new Padding(0), BackColor = Theme.Bg };
            left.RowStyles.Add(new RowStyle(SizeType.Absolute, 272));
            left.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            body.Controls.Add(left, 0, 0);

            var cSum = new Card("เครื่องนี้", null) { Dock = DockStyle.Fill };
            summary = new SummaryView { Dock = DockStyle.Fill };
            cSum.Controls.Add(summary);
            var copyMac = new ModernButton("คัดลอก MAC", ButtonKind.Secondary, 32) { Anchor = AnchorStyles.Top | AnchorStyles.Right };
            copyMac.Location = new Point(cSum.Width - copyMac.Width - 16, 12);
            copyMac.Click += (s, e) => { if (inMac.Box.Text.Length > 0) { Clipboard.SetText(inMac.Box.Text); SetStatus("คัดลอก MAC Address แล้ว", Theme.Success); } };
            cSum.Controls.Add(copyMac);
            copyMac.BringToFront();
            left.Controls.Add(cSum, 0, 0);

            var cAd = new Card("การ์ดแลนที่เชื่อมต่ออยู่", "คลิกเพื่อเลือกการ์ดที่ใช้ต่อเข้าวงเดียวกับเครื่อง Xerox") { Dock = DockStyle.Fill };
            adapterList = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, AutoScroll = true, BackColor = Color.White };
            adapterList.Resize += (s, e) => FitCards();
            cAd.Controls.Add(adapterList);
            var refresh = new ModernButton("ตรวจใหม่", ButtonKind.Secondary, 32) { Anchor = AnchorStyles.Top | AnchorStyles.Right };
            refresh.Location = new Point(cAd.Width - refresh.Width - 16, 12);
            refresh.Click += (s, e) => LoadData();
            cAd.Controls.Add(refresh);
            refresh.BringToFront();
            left.Controls.Add(cAd, 0, 1);

            // ---- right column ----
            var right = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 3, Margin = new Padding(0), BackColor = Theme.Bg };
            right.RowStyles.Add(new RowStyle(SizeType.Absolute, 300));
            right.RowStyles.Add(new RowStyle(SizeType.Absolute, 246));
            right.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            body.Controls.Add(right, 1, 0);

            var cF = new Card("ข้อมูลที่จะ Export", "ตรวจสอบและแก้ไขได้ · ต้องมี ชื่อเครื่อง และ MAC Address อย่างน้อย") { Dock = DockStyle.Fill };
            var grid = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3, RowCount = 4, BackColor = Color.White };
            for (int i = 0; i < 3; i++) grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 33.33f));
            for (int i = 0; i < 3; i++) grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 70));
            grid.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            inName = Field(grid, "ชื่อเครื่อง *", 0, 0, false);
            inMac = Field(grid, "MAC Address *", 1, 0, true);
            inPort = Field(grid, "พอร์ต Relay (ถ้ามี)", 2, 0, false);
            in4 = Field(grid, "IPv4 สำรอง", 0, 1, true);
            in6 = Field(grid, "IPv6 สำรอง", 1, 1, true);
            inAliases = Field(grid, "ชื่อแฝง (คั่นด้วย ,)", 2, 1, false);
            inNote = Field(grid, "หมายเหตุ", 0, 2, false);
            grid.SetColumnSpan(inNote, 3);
            warn = new Label { Dock = DockStyle.Top, AutoSize = false, Height = 30, BackColor = Theme.WarnBg, ForeColor = Theme.Warn, Font = Theme.F(9f), TextAlign = ContentAlignment.MiddleLeft, Padding = new Padding(10, 0, 0, 0), Visible = false };
            grid.Controls.Add(warn, 0, 3);
            grid.SetColumnSpan(warn, 3);
            cF.Controls.Add(grid);
            right.Controls.Add(cF, 0, 0);

            var cS = new Card("บันทึกไฟล์", "เลือกโฟลเดอร์ปลายทาง เช่น Flash drive หรือ Shared folder แล้วกด Export") { Dock = DockStyle.Fill };
            var sg = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3, RowCount = 3, BackColor = Color.White };
            sg.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 62));
            sg.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            sg.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 38));
            sg.RowStyles.Add(new RowStyle(SizeType.Absolute, 70));
            sg.RowStyles.Add(new RowStyle(SizeType.Absolute, 48));
            sg.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            inFolder = new InputBox("โฟลเดอร์ปลายทาง") { Dock = DockStyle.Fill, Margin = new Padding(0, 0, 8, 0) };
            sg.Controls.Add(inFolder, 0, 0);
            var browse = new ModernButton("เลือกโฟลเดอร์…", ButtonKind.Secondary, 38) { Margin = new Padding(0, 22, 14, 0) };
            browse.Click += (s, e) => BrowseFolder();
            sg.Controls.Add(browse, 1, 0);
            inFile = new InputBox("ชื่อไฟล์") { Dock = DockStyle.Fill, Margin = new Padding(0) };
            sg.Controls.Add(inFile, 2, 0);
            var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, BackColor = Color.White, WrapContents = false, Margin = new Padding(0, 4, 0, 0) };
            var export = new ModernButton("Export ไฟล์ JSON", ButtonKind.Primary, 42);
            export.Width += 20;
            export.Click += (s, e) => Export(Path.Combine(inFolder.Box.Text.Trim(), inFile.Box.Text.Trim()), false);
            var saveAs = new ModernButton("บันทึกเป็น…", ButtonKind.Secondary, 42);
            saveAs.Click += (s, e) => SaveAs();
            var copy = new ModernButton("คัดลอก JSON", ButtonKind.Ghost, 42);
            copy.Click += (s, e) => { Clipboard.SetText(Collector.ToJson(Record())); SetStatus("คัดลอก JSON ไปที่ Clipboard แล้ว", Theme.Success); };
            var open = new ModernButton("เปิดโฟลเดอร์", ButtonKind.Ghost, 42);
            open.Click += (s, e) => OpenFolder(null);
            actions.Controls.AddRange(new Control[] { export, saveAs, copy, open });
            sg.Controls.Add(actions, 0, 1);
            sg.SetColumnSpan(actions, 3);
            var savedRow = new FlowLayoutPanel { Dock = DockStyle.Fill, BackColor = Color.White, WrapContents = false, Margin = new Padding(0, 6, 0, 0) };
            savedText = new Label { AutoSize = true, ForeColor = Theme.Success, Font = Theme.F(9.2f, FontStyle.Bold), Margin = new Padding(0, 3, 6, 0) };
            openLink = new LinkLabel { AutoSize = true, Text = "เปิดตำแหน่งไฟล์", Font = Theme.F(9.2f), LinkColor = Theme.Accent, ActiveLinkColor = Theme.Navy, Margin = new Padding(0, 3, 0, 0), Visible = false };
            openLink.LinkClicked += (s, e) => OpenFolder(lastSaved);
            savedRow.Controls.Add(savedText);
            savedRow.Controls.Add(openLink);
            sg.Controls.Add(savedRow, 0, 2);
            sg.SetColumnSpan(savedRow, 3);
            cS.Controls.Add(sg);
            right.Controls.Add(cS, 0, 1);

            var cJ = new Card("ตัวอย่างไฟล์ JSON", null) { Dock = DockStyle.Fill };
            var code = new Panel { Dock = DockStyle.Fill, BackColor = Color.White, Padding = new Padding(14, 12, 6, 12) };
            code.Paint += (s, e) =>
            {
                e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
                using (var p = Theme.Round(new RectangleF(0, 0, code.Width - 1, code.Height - 1), 10))
                using (var b = new SolidBrush(Theme.Code)) e.Graphics.FillPath(b, p);
            };
            code.Resize += (s, e) => code.Invalidate();
            json = new TextBox { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, BorderStyle = BorderStyle.None, ScrollBars = ScrollBars.None, WordWrap = false, Font = Theme.Mono(9.5f) };   // scroll with the mouse wheel
            json.BackColor = Theme.Code;
            json.ForeColor = Theme.CodeText;
            code.Controls.Add(json);
            cJ.Controls.Add(code);
            right.Controls.Add(cJ, 0, 2);

            foreach (InputBox ib in new[] { inName, inMac, inPort, in4, in6, inAliases, inNote })
                ib.Box.TextChanged += (s, e) => { if (!loading) { SetStep(2); UpdateViews(); } };
            inName.Box.TextChanged += (s, e) => { if (!loading) inFile.Box.Text = SafeFile(inName.Box.Text) + ".json"; };

            inFolder.Box.Text = LoadLastFolder();
            Load += (s, e) => LoadData();
        }

        // ---- layout helpers ----
        static InputBox Field(TableLayoutPanel t, string caption, int col, int row, bool mono)
        {
            var ib = new InputBox(caption, mono) { Dock = DockStyle.Fill };
            t.Controls.Add(ib, col, row);
            return ib;
        }

        void BuildHeader()
        {
            header = new Panel { Dock = DockStyle.Top, Height = 92 };
            typeof(Panel).GetProperty("DoubleBuffered", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance).SetValue(header, true, null);
            header.Paint += PaintHeader;
            header.Resize += (s, e) => header.Invalidate();
            Controls.Add(header);
        }

        void PaintHeader(object sender, PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            var r = header.ClientRectangle;
            using (var br = new LinearGradientBrush(r, Theme.Navy, Theme.Navy2, 0f)) g.FillRectangle(br, r);
            using (var gold = new SolidBrush(Theme.Gold)) g.FillRectangle(gold, 0, r.Height - 3, r.Width, 3);
            Theme.DrawLogo(g, new RectangleF(24, 17, 56, 56));
            using (Font t = Theme.F(18f, FontStyle.Bold))
                TextRenderer.DrawText(g, "MacCollector", t, new Point(94, 14), Color.White, TextFormatFlags.NoPadding);
            using (Font s = Theme.F(9.5f))
                TextRenderer.DrawText(g, "เก็บข้อมูลเครื่องปลายทาง Scan สำหรับระบบ Xerox Host DNS Bridge", s, new Point(96, 52), Color.FromArgb(191, 207, 230), TextFormatFlags.NoPadding);

            // steps (right aligned)
            string[] steps = { "ตรวจสอบเครื่อง", "ยืนยันข้อมูล", "Export ไฟล์" };
            using (Font f = Theme.F(9.5f, FontStyle.Bold))
            using (Font n = Theme.F(9f, FontStyle.Bold))
            {
                int x = r.Width - 24;
                for (int i = steps.Length - 1; i >= 0; i--)
                {
                    int w = TextRenderer.MeasureText(steps[i], f).Width;
                    x -= w;
                    bool done = i + 1 < step, now = i + 1 == step;
                    Color tc = now ? Color.White : done ? Color.FromArgb(191, 207, 230) : Color.FromArgb(120, 145, 180);
                    TextRenderer.DrawText(g, steps[i], f, new Point(x, 35), tc, TextFormatFlags.NoPadding);
                    var c = new RectangleF(x - 32, 31, 24, 24);
                    using (var b = new SolidBrush(now ? Theme.Gold : done ? Color.FromArgb(56, 92, 140) : Color.FromArgb(30, 55, 92))) g.FillEllipse(b, c);
                    TextRenderer.DrawText(g, done ? "✓" : (i + 1).ToString(), n, Rectangle.Round(c), now ? Theme.Navy : Color.White,
                        TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding);
                    x -= 32;
                    if (i > 0)
                    {
                        using (var pen = new Pen(Color.FromArgb(60, 90, 130), 2f)) g.DrawLine(pen, x - 30, 43, x - 8, 43);
                        x -= 38;
                    }
                }
            }
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
                    TextRenderer.DrawText(e.Graphics, "Xerox Host DNS Bridge  ·  MacCollector v1.1", f,
                        new Rectangle(0, 0, footer.Width - 18, footer.Height), Theme.Muted, TextFormatFlags.Right | TextFormatFlags.VerticalCenter);
            };
            footerText = new Label { AutoSize = true, Location = new Point(32, 9), ForeColor = Theme.Text, Font = Theme.F(9f), BackColor = Color.White };
            footer.Controls.Add(footerText);
            Controls.Add(footer);
        }

        void FitCards()
        {
            int w = adapterList.ClientSize.Width - 4;
            foreach (AdapterCard c in cards) c.Width = Math.Max(200, w);
        }

        static string SafeFile(string s)
        {
            foreach (char c in Path.GetInvalidFileNameChars()) s = s.Replace(c, '_');
            return s.Trim().Length > 0 ? s.Trim() : "host";
        }

        void SetStatus(string text, Color dot)
        {
            footerText.Text = text;
            footerDot = dot;
            footer.Invalidate();
        }

        void SetStep(int s)
        {
            if (step == s) return;
            step = s;
            header.Invalidate();
        }

        // ---- data ----
        void LoadData()
        {
            Cursor = Cursors.WaitCursor;
            try
            {
                adapters = Collector.GetAdapters();
                adapterList.Controls.Clear();
                cards.Clear();
                foreach (AdapterInfo a in adapters)
                {
                    var c = new AdapterCard(a);
                    c.Picked += (s, e) => PickAdapter(((AdapterCard)s).Info);
                    cards.Add(c);
                    adapterList.Controls.Add(c);
                }
                FitCards();
                HostRecord r = Collector.Collect(adapters.FirstOrDefault(), adapters);
                owner = r.Note.Contains(" | ") ? r.Note.Substring(r.Note.IndexOf(" | ") + 3) : "";
                loading = true;
                inName.Box.Text = r.Name;
                inFile.Box.Text = SafeFile(r.Name) + ".json";
                inNote.Box.Text = r.Note;
                loading = false;
                if (adapters.Count > 0) PickAdapter(adapters[0]);
                else
                {
                    current = null;
                    loading = true;
                    inMac.Box.Text = in4.Box.Text = in6.Box.Text = "";
                    loading = false;
                    UpdateViews();
                }
                SetStep(1);
                SetStatus(adapters.Count > 0 ? "พบการ์ดแลนที่เชื่อมต่ออยู่ " + adapters.Count + " ตัว – ตรวจสอบข้อมูลแล้วกด Export" :
                                               "ไม่พบการ์ดแลนที่เชื่อมต่อ – เสียบสาย LAN หรือเชื่อม Wi-Fi แล้วกด ตรวจใหม่",
                          adapters.Count > 0 ? Theme.Success : Theme.Warn);
            }
            catch (Exception ex)
            {
                SetStatus("อ่านข้อมูลไม่สำเร็จ: " + ex.Message, Color.Firebrick);
            }
            finally { loading = false; Cursor = Cursors.Default; }
        }

        void PickAdapter(AdapterInfo a)
        {
            current = a;
            foreach (AdapterCard c in cards) c.Selected = c.Info == a;
            loading = true;
            inMac.Box.Text = a.Mac;
            in4.Box.Text = a.IPv4[0];
            in6.Box.Text = a.FirstGlobalIPv6();
            inNote.Box.Text = a.Name + (owner.Length > 0 ? " | " + owner : "");
            loading = false;
            UpdateViews();
        }

        HostRecord Record()
        {
            string mac = Collector.NormalizeMac(inMac.Box.Text);
            return new HostRecord
            {
                Name = inName.Box.Text.Trim(), Mac = mac ?? inMac.Box.Text.Trim(), Fallback4 = in4.Box.Text.Trim(), Fallback6 = in6.Box.Text.Trim(),
                Aliases = inAliases.Box.Text.Trim(), Note = inNote.Box.Text.Trim(), RelayPort = inPort.Box.Text.Trim(), Adapters = adapters
            };
        }

        void UpdateViews()
        {
            HostRecord r = Record();
            json.Text = Collector.ToJson(r).Replace("\n", "\r\n");
            string err = Collector.Validate(r);
            var warns = new List<string>();
            if (current != null && current.Wireless) warns.Add("การ์ดนี้เป็น Wi-Fi – ถ้าเปิด \"Random hardware addresses\" MAC อาจเปลี่ยน แนะนำใช้สาย LAN");
            if (current != null && current.Gateway.Count == 0) warns.Add("การ์ดนี้ไม่มี Gateway – ตรวจว่าต่อเข้าวงเดียวกับเครื่อง Xerox");
            warn.Text = warns.Count > 0 ? "⚠  " + string.Join("   ·   ", warns) : "";
            warn.Visible = warns.Count > 0;

            summary.HostName = r.Name;
            summary.Mac = r.Mac;
            summary.IPv4 = current != null ? "IPv4 " + string.Join(", ", current.IPv4) + "   ·   " + (current.Wireless ? "Wi-Fi" : "สาย LAN") : "";
            summary.Owner = owner;
            summary.Chip = err == null ? "พร้อม Export" : err;
            summary.ChipKind = err == null ? "ok" : "warn";
            summary.Invalidate();
        }

        // ---- save ----
        void BrowseFolder()
        {
            using (var d = new FolderBrowserDialog { Description = "เลือกโฟลเดอร์ที่จะบันทึกไฟล์ JSON", ShowNewFolderButton = true })
            {
                if (Directory.Exists(inFolder.Box.Text)) d.SelectedPath = inFolder.Box.Text;
                if (d.ShowDialog(this) == DialogResult.OK) inFolder.Box.Text = d.SelectedPath;
            }
        }

        void SaveAs()
        {
            using (var d = new SaveFileDialog { Filter = "JSON (*.json)|*.json", FileName = inFile.Box.Text, Title = "บันทึกไฟล์ JSON" })
            {
                if (Directory.Exists(inFolder.Box.Text)) d.InitialDirectory = inFolder.Box.Text;
                if (d.ShowDialog(this) != DialogResult.OK) return;
                inFolder.Box.Text = Path.GetDirectoryName(d.FileName);
                inFile.Box.Text = Path.GetFileName(d.FileName);
                Export(d.FileName, true);
            }
        }

        void Export(string path, bool confirmed)
        {
            HostRecord r = Record();
            string err = Collector.Validate(r);
            if (err != null) { MessageBox.Show(this, err, "ข้อมูลยังไม่ครบ", MessageBoxButtons.OK, MessageBoxIcon.Warning); return; }
            string dir = Path.GetDirectoryName(path);
            if (string.IsNullOrWhiteSpace(inFolder.Box.Text) || string.IsNullOrEmpty(dir) || !Directory.Exists(dir))
            {
                MessageBox.Show(this, "กรุณาเลือกโฟลเดอร์ปลายทางที่มีอยู่จริง", "โฟลเดอร์ไม่ถูกต้อง", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            if (!path.EndsWith(".json", StringComparison.OrdinalIgnoreCase)) path += ".json";
            if (!confirmed && File.Exists(path) &&
                MessageBox.Show(this, "มีไฟล์นี้อยู่แล้ว ต้องการเขียนทับหรือไม่?\n\n" + path, "ยืนยันการเขียนทับ", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
                return;
            try
            {
                Collector.Save(path, Collector.ToJson(r));
                SaveLastFolder(dir);
                lastSaved = path;
                savedText.Text = "✓  บันทึกแล้ว: " + path;
                openLink.Visible = true;
                SetStep(3);
                SetStatus("Export สำเร็จ – นำไฟล์ไปกด \"นำเข้าไฟล์ JSON\" ในหน้าเพิ่ม Host ของ ESP32", Theme.Success);
            }
            catch (Exception ex)
            {
                SetStatus("บันทึกไม่สำเร็จ: " + ex.Message, Color.Firebrick);
                MessageBox.Show(this, "บันทึกไม่สำเร็จ:\n" + ex.Message, "ผิดพลาด", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        void OpenFolder(string file)
        {
            if (!string.IsNullOrEmpty(file) && File.Exists(file)) Process.Start("explorer.exe", "/select,\"" + file + "\"");
            else if (Directory.Exists(inFolder.Box.Text)) Process.Start("explorer.exe", "\"" + inFolder.Box.Text + "\"");
        }

        static string LoadLastFolder()
        {
            try
            {
                if (File.Exists(SettingsFile))
                {
                    string d = File.ReadAllText(SettingsFile).Trim();
                    if (Directory.Exists(d)) return d;
                }
            }
            catch { }
            return Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
        }

        static void SaveLastFolder(string dir)
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(SettingsFile));
                File.WriteAllText(SettingsFile, dir);
            }
            catch { }
        }
    }
}
