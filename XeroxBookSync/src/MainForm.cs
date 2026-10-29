// Main window: ESP32 + copier login on the left, the entries to add on the right, a log below.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace XeroxBookSync
{
    class Row
    {
        public Host Host;
        public bool Check, HasRelay;
        public string Nick = "", Share = "", Login = "", Path = "";
        public string InXerox = "", Result = "";
        public Color ResultColor = Theme.Muted;
    }

    class MainForm : Form
    {
        InputBox inEsp, inEspUser, inEspPass, inXrxUser, inXrxPass, inPcPass;
        ComboBox cbPrinter;
        CheckBox cbForce, cbRemember;
        ModernButton btnLoad, btnConnect, btnTest, btnAdd, btnAll, btnNone;
        DataGridView grid;
        TextBox logBox;
        Label status;
        readonly List<Row> rows = new List<Row>();
        Esp32 esp;
        Xerox xrx;
        bool busy;

        static readonly string SettingsFile = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "XeroxBookSync", "settings.txt");

        public MainForm()
        {
            AutoScaleDimensions = new SizeF(96f, 96f);
            AutoScaleMode = AutoScaleMode.Dpi;
            Text = "Xerox Address Book Sync – Xerox Host DNS Bridge";
            Font = Theme.F(9.5f);
            BackColor = Theme.Bg;
            Rectangle wa = Screen.PrimaryScreen.WorkingArea;
            ClientSize = new Size(Math.Min(1280, wa.Width - 40), Math.Min(780, wa.Height - 60));
            MinimumSize = new Size(1050, 640);
            StartPosition = FormStartPosition.CenterScreen;
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }

            var header = new Panel { Dock = DockStyle.Top, Height = 78, BackColor = Theme.Navy };
            header.Paint += (s, e) =>
            {
                Theme.DrawLogo(e.Graphics, new RectangleF(20, 15, 48, 48));
                using (Font f = Theme.F(15f, FontStyle.Bold))
                    TextRenderer.DrawText(e.Graphics, "Xerox Address Book Sync", f, new Point(82, 14), Color.White, TextFormatFlags.NoPadding);
                using (Font f = Theme.F(9.5f))
                    TextRenderer.DrawText(e.Graphics, "เพิ่มรายการ Scan จาก ESP32 เข้าสมุดที่อยู่ของ Xerox อัตโนมัติ: กรอกข้อมูล → Destination Test → Save", f,
                        new Point(84, 46), Color.FromArgb(191, 207, 230), TextFormatFlags.NoPadding);
                using (var p = new Pen(Theme.Gold, 3)) e.Graphics.DrawLine(p, 0, header.Height - 2, header.Width, header.Height - 2);
            };

            status = new Label { Dock = DockStyle.Bottom, Height = 30, Padding = new Padding(16, 7, 16, 0), BackColor = Color.White, ForeColor = Theme.Muted, Text = "พร้อม" };

            // ---- left: settings -------------------------------------------------
            var left = new FlowLayoutPanel { Dock = DockStyle.Left, Width = 372, FlowDirection = FlowDirection.TopDown, WrapContents = false, AutoScroll = true, Padding = new Padding(12, 12, 4, 12), BackColor = Theme.Bg };

            var cEsp = new Card("1. ESP32", "อ่านรายการ Host และพอร์ต Relay") { Width = 330, Height = 350 };
            var fEsp = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, BackColor = Color.White };
            inEsp = new InputBox("ที่อยู่ ESP32 (ชื่อหรือ IP)", true) { Width = 290 };
            inEsp.Box.Text = "xerox-dns.local";
            inEspUser = new InputBox("ชื่อผู้ใช้หน้าเว็บ ESP32") { Width = 290 };
            inEspUser.Box.Text = "admin";
            inEspPass = new InputBox("รหัสผ่านหน้าเว็บ ESP32") { Width = 290 };
            inEspPass.Box.UseSystemPasswordChar = true;
            btnLoad = new ModernButton("โหลดรายการจาก ESP32", ButtonKind.Primary) { Width = 290 };
            btnLoad.Click += (s, e) => LoadEsp();
            fEsp.Controls.AddRange(new Control[] { inEsp, inEspUser, inEspPass, btnLoad });
            cEsp.Controls.Add(fEsp);

            var cXrx = new Card("2. เครื่อง Xerox", "ต้องใช้บัญชี Admin ของเครื่อง (ไม่บันทึกรหัส)") { Width = 330, Height = 380 };
            var fXrx = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, BackColor = Color.White };
            var lbl = new Label { Text = "IP ของเครื่อง Xerox (เลือกจากรายการใน ESP32 หรือพิมพ์เอง)", Width = 290, Height = 22, ForeColor = Theme.Muted, Font = Theme.F(9f) };
            cbPrinter = new ComboBox { Width = 290, Font = Theme.Mono(10.5f), Margin = new Padding(0, 0, 0, 10) };
            inXrxUser = new InputBox("ชื่อผู้ใช้ Admin ของ Xerox") { Width = 290 };
            inXrxUser.Box.Text = "admin";
            inXrxPass = new InputBox("รหัสผ่าน Admin ของ Xerox") { Width = 290 };
            inXrxPass.Box.UseSystemPasswordChar = true;
            btnConnect = new ModernButton("เชื่อมต่อ Xerox", ButtonKind.Primary) { Width = 290 };
            btnConnect.Click += (s, e) => Connect();
            fXrx.Controls.AddRange(new Control[] { lbl, cbPrinter, inXrxUser, inXrxPass, btnConnect });
            cXrx.Controls.Add(fXrx);

            var cOpt = new Card("ตัวเลือก", "") { Width = 330, Height = 236 };
            var fOpt = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, BackColor = Color.White };
            inPcPass = new InputBox("รหัสผ่านของ PC (ถ้าแชร์ต้องใช้รหัส – ใช้กับทุกรายการ)") { Width = 290 };
            inPcPass.Box.UseSystemPasswordChar = true;
            cbForce = new CheckBox { Text = "บันทึกแม้ Destination Test ไม่ผ่าน", Width = 290, Height = 26, ForeColor = Theme.Text };
            cbRemember = new CheckBox { Text = "จำ Share / Login / Path ไว้ใน ESP32", Width = 290, Height = 26, Checked = true, ForeColor = Theme.Text };
            fOpt.Controls.AddRange(new Control[] { inPcPass, cbForce, cbRemember });
            cOpt.Controls.Add(fOpt);
            left.Controls.AddRange(new Control[] { cEsp, cXrx, cOpt });

            // ---- right: entries + log -------------------------------------------
            var right = new Panel { Dock = DockStyle.Fill, Padding = new Padding(4, 12, 12, 12), BackColor = Theme.Bg };
            var cList = new Card("3. รายการที่จะเพิ่มเข้า Xerox", "ติ๊กเลือก แก้ชื่อ Scan / Share / Login ได้ในตาราง  ·  Host ที่ไม่มีพอร์ต Relay เพิ่มไม่ได้") { Dock = DockStyle.Fill };
            var bar = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 50, BackColor = Color.White, Padding = new Padding(0, 0, 0, 8) };
            btnTest = new ModernButton("ทดสอบที่เลือก (Destination Test)", ButtonKind.Secondary);
            btnAdd = new ModernButton("เพิ่มเข้า Xerox  (ทดสอบ → Save)", ButtonKind.Primary);
            btnAll = new ModernButton("เลือกทั้งหมด", ButtonKind.Ghost);
            btnNone = new ModernButton("ไม่เลือก", ButtonKind.Ghost);
            btnTest.Click += (s, e) => Run(false);
            btnAdd.Click += (s, e) => Run(true);
            btnAll.Click += (s, e) => { foreach (Row r in rows) r.Check = r.HasRelay; grid.Invalidate(); };
            btnNone.Click += (s, e) => { foreach (Row r in rows) r.Check = false; grid.Invalidate(); };
            bar.Controls.AddRange(new Control[] { btnAdd, btnTest, btnAll, btnNone });

            grid = BuildGrid();
            logBox = new TextBox
            {
                Dock = DockStyle.Bottom, Height = 140, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical,
                BackColor = Theme.Code, ForeColor = Theme.CodeText, BorderStyle = BorderStyle.None, Font = Theme.F(9f)
            };
            var gap = new Panel { Dock = DockStyle.Bottom, Height = 10, BackColor = Color.White };
            cList.Controls.Add(grid);
            cList.Controls.Add(gap);
            cList.Controls.Add(logBox);
            cList.Controls.Add(bar);
            right.Controls.Add(cList);

            Controls.Add(right);
            Controls.Add(left);
            Controls.Add(status);
            Controls.Add(header);
            LoadSettings();
            SetBusy(false);
            FormClosing += (s, e) => SaveSettings();
            Shown += (s, e) => Log("1) ใส่รหัสหน้าเว็บ ESP32 แล้วกด \"โหลดรายการจาก ESP32\"   2) ใส่ IP และรหัส Admin ของ Xerox แล้วกด \"เชื่อมต่อ Xerox\"   3) ติ๊กรายการแล้วกด \"เพิ่มเข้า Xerox\"");
        }

        // ---- grid ---------------------------------------------------------------
        DataGridView BuildGrid()
        {
            var g = new DataGridView
            {
                Dock = DockStyle.Fill, VirtualMode = true, AllowUserToAddRows = false, AllowUserToDeleteRows = false, AllowUserToResizeRows = false,
                RowHeadersVisible = false, SelectionMode = DataGridViewSelectionMode.CellSelect, BackgroundColor = Color.White,
                BorderStyle = BorderStyle.None, GridColor = Theme.Border, EnableHeadersVisualStyles = false, ColumnHeadersHeight = 34,
                ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.DisableResizing, AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill
            };
            g.ColumnHeadersDefaultCellStyle.BackColor = Theme.Bg;
            g.ColumnHeadersDefaultCellStyle.ForeColor = Theme.Muted;
            g.ColumnHeadersDefaultCellStyle.Font = Theme.F(9f, FontStyle.Bold);
            g.DefaultCellStyle.Font = Theme.F(9.5f);
            g.DefaultCellStyle.SelectionBackColor = Theme.AccentBg;
            g.DefaultCellStyle.SelectionForeColor = Theme.Text;
            g.RowTemplate.Height = 30;
            g.Columns.Add(new DataGridViewCheckBoxColumn { HeaderText = "", FillWeight = 22 });
            string[] heads = { "ชื่อ Scan (First / Display / Nickname)", "Host (PC)", "ปลายทาง (IPv4 : Port)", "Share", "Login Name", "Document Path", "ใน Xerox", "ผลล่าสุด" };
            float[] w = { 150, 120, 130, 70, 90, 80, 90, 210 };
            for (int i = 0; i < heads.Length; i++)
                g.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = heads[i], FillWeight = w[i], SortMode = DataGridViewColumnSortMode.NotSortable });
            foreach (int i in new[] { 2, 3, 7, 8 }) g.Columns[i].ReadOnly = true;
            g.CellValueNeeded += (s, e) =>
            {
                if (e.RowIndex >= rows.Count) return;
                Row r = rows[e.RowIndex];
                switch (e.ColumnIndex)
                {
                    case 0: e.Value = r.Check; break;
                    case 1: e.Value = r.Nick; break;
                    case 2: e.Value = r.Host.Name; break;
                    case 3: e.Value = r.HasRelay ? esp.Ip + " : " + r.Host.RelayPort : "ไม่มีพอร์ต Relay"; break;
                    case 4: e.Value = r.Share; break;
                    case 5: e.Value = r.Login; break;
                    case 6: e.Value = r.Path; break;
                    case 7: e.Value = r.InXerox; break;
                    case 8: e.Value = r.Result; break;
                }
            };
            g.CellValuePushed += (s, e) =>
            {
                Row r = rows[e.RowIndex];
                string v = Convert.ToString(e.Value ?? "").Trim();
                switch (e.ColumnIndex)
                {
                    case 0: r.Check = r.HasRelay && (bool)e.Value; break;
                    case 1: r.Nick = v; break;
                    case 4: r.Share = v; break;
                    case 5: r.Login = v; break;
                    case 6: r.Path = v; break;
                }
            };
            g.CurrentCellDirtyStateChanged += (s, e) => { if (g.IsCurrentCellDirty && g.CurrentCell.ColumnIndex == 0) g.CommitEdit(DataGridViewDataErrorContexts.Commit); };
            g.CellFormatting += (s, e) =>
            {
                if (e.RowIndex >= rows.Count) return;
                Row r = rows[e.RowIndex];
                if (!r.HasRelay) e.CellStyle.ForeColor = Theme.Muted;
                if (e.ColumnIndex == 8) e.CellStyle.ForeColor = r.ResultColor;
                if (e.ColumnIndex == 7) e.CellStyle.ForeColor = r.InXerox.StartsWith("มีแล้ว") ? Theme.Warn : r.InXerox.StartsWith("เพิ่มแล้ว") ? Theme.Success : Theme.Muted;
                if (e.ColumnIndex == 3) e.CellStyle.Font = Theme.Mono(9.5f);
            };
            g.CellBeginEdit += (s, e) => { if (busy || !rows[e.RowIndex].HasRelay) e.Cancel = true; };
            return g;
        }

        void RefreshGrid()
        {
            grid.RowCount = 0;
            grid.RowCount = rows.Count;
            grid.Invalidate();
        }

        // ---- actions ------------------------------------------------------------
        void Log(string text)
        {
            if (InvokeRequired) { BeginInvoke((Action)(() => Log(text))); return; }
            logBox.AppendText(DateTime.Now.ToString("HH:mm:ss") + "  " + text + Environment.NewLine);
        }

        void Status(string text, Color c)
        {
            if (InvokeRequired) { BeginInvoke((Action)(() => Status(text, c))); return; }
            status.Text = text;
            status.ForeColor = c;
        }

        void SetBusy(bool b)
        {
            busy = b;
            btnLoad.Enabled = btnConnect.Enabled = !b;
            btnTest.Enabled = btnAdd.Enabled = !b && rows.Count > 0 && xrx != null;
            UseWaitCursor = b;
        }

        static string NickOf(Host h)
        {
            string first = (h.ScanName ?? "").Split(',')[0];
            first = Regex.Replace(first, "\\s*\\([^)]*\\)\\s*$", "").Trim();
            return first.Length > 0 ? first : h.Name;
        }

        async void LoadEsp()
        {
            SetBusy(true);
            Status("กำลังอ่านข้อมูลจาก ESP32 …", Theme.Accent);
            var e = new Esp32 { Address = inEsp.Box.Text.Trim(), User = inEspUser.Box.Text.Trim(), Pass = inEspPass.Box.Text };
            string err = await Task.Run(() => e.Load());
            if (err != null) { Status(err, Theme.Danger); Log("ESP32: " + err); SetBusy(false); return; }
            esp = e;
            rows.Clear();
            foreach (Host h in e.Hosts.Where(x => x.Enabled))
            {
                string nick = NickOf(h);
                rows.Add(new Row
                {
                    Host = h, HasRelay = h.RelayPort > 0, Check = h.RelayPort > 0, Nick = nick,
                    Share = h.Share.Length > 0 ? h.Share : "scan", Login = h.Login.Length > 0 ? h.Login : nick, Path = h.DocPath,
                    InXerox = xrx == null ? "ยังไม่ได้เชื่อมต่อ" : ""
                });
            }
            string keep = cbPrinter.Text;
            cbPrinter.Items.Clear();
            foreach (var p in e.Printers) cbPrinter.Items.Add(p.Value + "   (" + p.Key + ")");
            if (keep.Length > 0) cbPrinter.Text = keep;
            else if (cbPrinter.Items.Count > 0) cbPrinter.SelectedIndex = 0;
            RefreshGrid();
            Log("ESP32 " + e.Ip + " (v" + e.Fw + "): " + rows.Count + " Host, มีพอร์ต Relay " + rows.Count(r => r.HasRelay) + " รายการ");
            Status("โหลดจาก ESP32 แล้ว " + rows.Count + " Host – ต่อไปเชื่อมต่อ Xerox", Theme.Success);
            SetBusy(false);
            if (xrx != null) await MarkExisting();
        }

        string PrinterIp()
        {
            Match m = Regex.Match(cbPrinter.Text, "\\d{1,3}(\\.\\d{1,3}){3}");
            return m.Success ? m.Value : cbPrinter.Text.Trim();
        }

        async void Connect()
        {
            string ip = PrinterIp();
            if (ip.Length == 0) { Status("ใส่ IP ของเครื่อง Xerox", Theme.Danger); return; }
            SetBusy(true);
            Status("กำลังล็อกอิน Xerox " + ip + " …", Theme.Accent);
            var x = new Xerox(ip, inXrxUser.Box.Text.Trim(), inXrxPass.Box.Text);
            string err = await Task.Run(() => x.Login());
            if (err != null) { Status(err, Theme.Danger); Log("Xerox " + ip + ": " + err); xrx = null; SetBusy(false); return; }
            xrx = x;
            Log("ล็อกอิน Xerox " + ip + " สำเร็จ");
            await MarkExisting();
            SetBusy(false);
        }

        // Marks entries whose name is already in the copier's address book (and unticks them).
        async Task MarkExisting()
        {
            if (xrx == null) return;
            List<KeyValuePair<string, string>> list;
            try { list = await Task.Run(() => xrx.Contacts()); }
            catch (Exception ex) { Log("อ่านสมุดที่อยู่ไม่ได้: " + ex.Message); return; }
            var names = new HashSet<string>(list.Select(c => c.Value), StringComparer.OrdinalIgnoreCase);
            foreach (Row r in rows)
            {
                bool has = names.Contains(r.Nick);
                r.InXerox = has ? "มีแล้ว" : "ยังไม่มี";
                if (has) r.Check = false;
            }
            RefreshGrid();
            Log("สมุดที่อยู่ของ Xerox มี " + list.Count + " รายการ: " + string.Join(", ", list.Select(c => c.Value)));
            Status("เชื่อมต่อ Xerox " + xrx.Ip + " แล้ว – ติ๊กรายการแล้วกด \"เพิ่มเข้า Xerox\"", Theme.Success);
        }

        static string Explain(TestResult t)
        {
            var step = t.Steps.ToDictionary(s => s.Key, s => s.Value);
            string v;
            if (step.TryGetValue("Ping Test", out v) && v == "Failed") return "Xerox ping ESP32 ไม่ได้ – ตรวจ IP ของ ESP32 / สาย LAN";
            if (step.TryGetValue("Destination Server Test", out v) && v == "Failed") return "ต่อ SMB ไม่ได้ – พอร์ตไม่มีใน ESP32, PC ปิดอยู่ หรือชื่อ Share / Login ไม่ถูก (ดู Log Relay ใน ESP32)";
            if (step.TryGetValue("File Write Test", out v) && v == "Failed") return "เข้าแชร์ได้แต่เขียนไฟล์ไม่ได้ – ตรวจสิทธิ์เขียนของโฟลเดอร์";
            if (step.TryGetValue("File Remove Test", out v) && v == "Failed") return "เขียนไฟล์ได้แต่ลบไม่ได้ – ให้สิทธิ์ Modify กับโฟลเดอร์";
            return t.Failed();
        }

        async void Run(bool save)
        {
            if (xrx == null || esp == null) return;
            var todo = rows.Where(r => r.Check && r.HasRelay).ToList();
            if (todo.Count == 0) { Status("ยังไม่ได้ติ๊กรายการ", Theme.Warn); return; }
            Row bad = todo.FirstOrDefault(r => r.Nick.Length == 0 || r.Share.Length == 0 || r.Login.Length == 0);
            if (bad != null) { Status("Host " + bad.Host.Name + ": กรอกชื่อ Scan / Share / Login Name ให้ครบ", Theme.Danger); return; }
            if (save)
            {
                var dup = todo.Where(r => r.InXerox.StartsWith("มีแล้ว") || r.InXerox.StartsWith("เพิ่มแล้ว")).ToList();
                if (dup.Count > 0 && MessageBox.Show(this, "มีชื่อนี้ใน Xerox แล้ว " + dup.Count + " รายการ (" + string.Join(", ", dup.Select(r => r.Nick)) +
                    ")\nเพิ่มซ้ำอีกหรือไม่?", Text, MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
                    todo = todo.Except(dup).ToList();
                if (todo.Count == 0) return;
            }
            grid.EndEdit();
            SetBusy(true);
            string pcPass = inPcPass.Box.Text;
            bool force = cbForce.Checked;
            int ok = 0, fail = 0;
            bool espChanged = false;
            foreach (Row r in todo)
            {
                var e = new Entry { Nick = r.Nick, Ip = esp.Ip, Port = r.Host.RelayPort, Share = r.Share, Login = r.Login, Path = r.Path, Password = pcPass };
                r.Result = "กำลังทดสอบ …"; r.ResultColor = Theme.Accent; grid.Invalidate();
                Status((save ? "กำลังเพิ่ม " : "กำลังทดสอบ ") + r.Nick + " (" + e.Ip + ":" + e.Port + ") …", Theme.Accent);
                try
                {
                    TestResult t = await Task.Run(() => TestWithRelogin(e));
                    string detail = string.Join(" · ", t.Steps.Select(s => s.Key.Replace(" Test", "") + " " + s.Value));
                    Log(r.Nick + " → " + e.Ip + ":" + e.Port + "  Destination Test: " + t.Summary + "  [" + detail + "]");
                    if (!t.Valid)
                    {
                        r.Result = "ทดสอบไม่ผ่าน: " + Explain(t); r.ResultColor = Theme.Danger;
                        if (!save || !force) { fail++; grid.Invalidate(); continue; }
                    }
                    else { r.Result = "ทดสอบผ่าน"; r.ResultColor = Theme.Success; }
                    if (save)
                    {
                        await Task.Run(() => xrx.Add(e));
                        var list = await Task.Run(() => xrx.Contacts());
                        if (list.Any(c => string.Equals(c.Value, e.Nick, StringComparison.OrdinalIgnoreCase)))
                        {
                            r.InXerox = "เพิ่มแล้ว ✓"; r.Check = false;
                            r.Result = t.Valid ? "ทดสอบผ่าน → บันทึกแล้ว" : "บันทึกแล้ว (ทดสอบไม่ผ่าน)";
                            ok++;
                            Log("  บันทึก " + e.Nick + " ใน Xerox แล้ว");
                            if (r.Host.Share != r.Share || r.Host.Login != r.Login || r.Host.DocPath != r.Path || r.Host.ScanName.Length == 0)
                            {
                                r.Host.Share = r.Share; r.Host.Login = r.Login; r.Host.DocPath = r.Path;
                                if (r.Host.ScanName.Length == 0) r.Host.ScanName = r.Nick;
                                espChanged = true;
                            }
                        }
                        else { r.Result = "Save แล้วแต่ไม่พบในสมุดที่อยู่ – เปิดหน้าเว็บ Xerox ตรวจสอบ"; r.ResultColor = Theme.Danger; fail++; }
                    }
                    else ok += t.Valid ? 1 : 0;
                }
                catch (Exception ex)
                {
                    r.Result = "ผิดพลาด: " + ex.Message; r.ResultColor = Theme.Danger; fail++;
                    Log(r.Nick + ": " + ex.Message);
                }
                grid.Invalidate();
            }
            if (espChanged && cbRemember.Checked)
            {
                string err = await Task.Run(() => esp.SaveHosts());
                Log(err == null ? "จำ Share / Login / Path ไว้ใน ESP32 แล้ว" : "บันทึกกลับ ESP32 ไม่ได้: " + err);
            }
            Status((save ? "เพิ่มสำเร็จ " : "ทดสอบผ่าน ") + ok + " รายการ" + (fail > 0 ? " · ไม่ผ่าน " + fail + " รายการ (ดูคอลัมน์ผลล่าสุด)" : ""),
                fail > 0 ? Theme.Warn : Theme.Success);
            SetBusy(false);
        }

        // The copier ends idle sessions: log in again once if a request fails.
        TestResult TestWithRelogin(Entry e)
        {
            try { return xrx.Test(e); }
            catch (Exception)
            {
                string err = xrx.Login();
                if (err != null) throw new Exception(err);
                return xrx.Test(e);
            }
        }

        // ---- settings (no passwords) ------------------------------------------------
        void LoadSettings()
        {
            try
            {
                foreach (string line in File.ReadAllLines(SettingsFile))
                {
                    int i = line.IndexOf('=');
                    if (i < 0) continue;
                    string k = line.Substring(0, i), v = line.Substring(i + 1);
                    if (k == "esp") inEsp.Box.Text = v;
                    else if (k == "espUser") inEspUser.Box.Text = v;
                    else if (k == "xerox") cbPrinter.Text = v;
                    else if (k == "xeroxUser") inXrxUser.Box.Text = v;
                    else if (k == "remember") cbRemember.Checked = v == "1";
                }
            }
            catch { }
        }

        void SaveSettings()
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(SettingsFile));
                File.WriteAllLines(SettingsFile, new[] {
                    "esp=" + inEsp.Box.Text.Trim(), "espUser=" + inEspUser.Box.Text.Trim(), "xerox=" + cbPrinter.Text.Trim(),
                    "xeroxUser=" + inXrxUser.Box.Text.Trim(), "remember=" + (cbRemember.Checked ? "1" : "0") });
            }
            catch { }
        }
    }
}
