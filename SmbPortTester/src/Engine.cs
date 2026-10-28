// Applies / reverts the Windows settings that let this PC's SMB server (445) answer on extra
// TCP ports: netsh portproxy 0.0.0.0:<port> -> 127.0.0.1:445, one firewall rule limited to the
// sender (Xerox) IPs, and the optional LanmanServer / LSA registry values from the old .bat.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.ServiceProcess;
using System.Text;
using System.Threading;
using Microsoft.Win32;

namespace SmbPortTester
{
    enum LogKind { Info, Cmd, Ok, Warn, Error }

    class Proxy
    {
        public string ListenAddr, ConnectAddr;
        public int ListenPort, ConnectPort;
        public bool Managed { get { return ConnectPort == 445 && (ConnectAddr == "127.0.0.1" || ConnectAddr.Equals("localhost", StringComparison.OrdinalIgnoreCase)); } }
    }

    class FwRule { public string Name, Ports, Remote; public int Profiles; public bool Enabled; }

    class Options
    {
        public List<int> Ports = new List<int>();
        public List<string> Senders = new List<string>();
        public bool PrivateOnly = true, StrictName = true, Loopback, MakePrivate;
    }

    class Snapshot
    {
        public List<string> LocalIps = new List<string>();
        public List<Proxy> Proxies = new List<Proxy>();
        public HashSet<int> Listening = new HashSet<int>();
        public List<FwRule> Rules = new List<FwRule>();
        public string IpHelper = "?";
        public int? Strict, Loopback;
        public int Profiles;                                           // active firewall profiles: 1 domain, 2 private, 4 public
        public bool HasState;
        public string FwError;
    }

    class Engine
    {
        public const string RuleName = "SMB Port Tester";
        static readonly string[] LegacyRules = { "Allow SMB 4451" };   // created by the old .bat script
        const string ProxyKey = @"SYSTEM\CurrentControlSet\Services\PortProxy\v4tov4\tcp";
        const string ServerKey = @"SYSTEM\CurrentControlSet\Services\LanmanServer\Parameters";
        const string LsaKey = @"SYSTEM\CurrentControlSet\Control\Lsa";
        public const string StrictValue = "DisableStrictNameChecking", LoopValue = "DisableLoopbackCheck";
        const int ProfilesPrivate = 1 | 2, ProfilesAll = 0x7FFFFFFF;   // NET_FW_PROFILE2_DOMAIN | PRIVATE
        static readonly string StateFile = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "SmbPortTester", "state.txt");
        static readonly string Netsh = Path.Combine(Environment.SystemDirectory, "netsh.exe");
        static readonly string Sc = Path.Combine(Environment.SystemDirectory, "sc.exe");
        static readonly string PowerShell = Path.Combine(Environment.SystemDirectory, @"WindowsPowerShell\v1.0\powershell.exe");

        public event Action<LogKind, string> Log;
        void L(LogKind k, string s) { Action<LogKind, string> h = Log; if (h != null) h(k, s); }

        // ---------- input parsing ----------
        public static List<int> ParsePorts(string text)
        {
            var list = new List<int>();
            foreach (string raw in text.Split(new[] { ',', ' ', ';' }, StringSplitOptions.RemoveEmptyEntries))
            {
                string[] ab = raw.Split('-');
                int a, b;
                if (ab.Length > 2 || !int.TryParse(ab[0], out a) || !int.TryParse(ab[ab.Length - 1], out b))
                    throw new FormatException("รูปแบบพอร์ตไม่ถูกต้อง: " + raw);
                if (b < a) { int t = a; a = b; b = t; }
                if (a < 1 || b > 65535) throw new FormatException("พอร์ตต้องอยู่ระหว่าง 1 – 65535: " + raw);
                if (b - a >= 200) throw new FormatException("ช่วงพอร์ตกว้างเกินไป (สูงสุด 200 พอร์ต): " + raw);
                for (int p = a; p <= b; p++)
                {
                    if (p == 445 || p == 139) throw new FormatException("พอร์ต " + p + " เป็นพอร์ต SMB ของ Windows อยู่แล้ว ไม่ต้องเปิดเพิ่ม");
                    if (!list.Contains(p)) list.Add(p);
                }
            }
            if (list.Count == 0) throw new FormatException("กรุณาระบุพอร์ตอย่างน้อย 1 พอร์ต");
            if (list.Count > 200) throw new FormatException("เปิดได้สูงสุด 200 พอร์ตต่อครั้ง");
            list.Sort();
            return list;
        }

        public static List<string> ParseSenders(string text)
        {
            var list = new List<string>();
            foreach (string raw in text.Split(new[] { ',', ' ', ';' }, StringSplitOptions.RemoveEmptyEntries))
            {
                string ip = raw;
                int bits = -1;
                int slash = raw.IndexOf('/');
                if (slash > 0)
                {
                    ip = raw.Substring(0, slash);
                    if (!int.TryParse(raw.Substring(slash + 1), out bits) || bits < 8 || bits > 32)
                        throw new FormatException("Subnet ไม่ถูกต้อง (ใช้ /8 – /32): " + raw);
                }
                IPAddress a;
                if (ip.Split('.').Length != 4 || !IPAddress.TryParse(ip, out a) || a.AddressFamily != AddressFamily.InterNetwork)
                    throw new FormatException("IP เครื่องผู้ส่งไม่ถูกต้อง: " + raw);
                string norm = a + (bits >= 0 ? "/" + bits : "");
                if (!list.Contains(norm)) list.Add(norm);
            }
            return list;
        }

        // 4451,4452,4453,4460 -> "4451-4453,4460" (format accepted by the firewall)
        public static string PortsText(IList<int> ports)
        {
            var parts = new List<string>();
            for (int i = 0; i < ports.Count; )
            {
                int j = i;
                while (j + 1 < ports.Count && ports[j + 1] == ports[j] + 1) j++;
                parts.Add(j > i ? ports[i] + "-" + ports[j] : ports[i].ToString());
                i = j + 1;
            }
            return string.Join(",", parts);
        }

        // ---------- read current state ----------
        static RegistryKey Hklm() { return RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64); }

        public static List<Proxy> ListProxies()
        {
            var list = new List<Proxy>();
            using (RegistryKey root = Hklm())
            using (RegistryKey k = root.OpenSubKey(ProxyKey))
            {
                if (k == null) return list;
                foreach (string name in k.GetValueNames())   // "0.0.0.0/4451" = "127.0.0.1/445"
                {
                    string val = k.GetValue(name) as string;
                    int a = name.LastIndexOf('/'), b = val == null ? -1 : val.LastIndexOf('/');
                    int lp, cp;
                    if (a < 0 || b < 0 || !int.TryParse(name.Substring(a + 1), out lp) || !int.TryParse(val.Substring(b + 1), out cp)) continue;
                    list.Add(new Proxy { ListenAddr = name.Substring(0, a), ListenPort = lp, ConnectAddr = val.Substring(0, b), ConnectPort = cp });
                }
            }
            return list.OrderBy(p => p.ListenPort).ToList();
        }

        public static HashSet<int> ListeningPorts()
        {
            var set = new HashSet<int>();
            foreach (IPEndPoint ep in IPGlobalProperties.GetIPGlobalProperties().GetActiveTcpListeners())
                if (ep.AddressFamily == AddressFamily.InterNetwork) set.Add(ep.Port);
            return set;
        }

        public static List<string> LocalIps()
        {
            var list = new List<string>();
            foreach (NetworkInterface ni in NetworkInterface.GetAllNetworkInterfaces())
            {
                if (ni.OperationalStatus != OperationalStatus.Up || ni.NetworkInterfaceType == NetworkInterfaceType.Loopback) continue;
                IPInterfaceProperties ip = ni.GetIPProperties();
                bool gw = ip.GatewayAddresses.Any(g => g.Address.AddressFamily == AddressFamily.InterNetwork && !g.Address.Equals(IPAddress.Any));
                foreach (UnicastIPAddressInformation u in ip.UnicastAddresses)
                {
                    if (u.Address.AddressFamily != AddressFamily.InterNetwork) continue;
                    string s = u.Address.ToString();
                    if (s.StartsWith("169.254.")) continue;
                    if (gw) list.Insert(0, s); else list.Add(s);   // adapters with a gateway first
                }
            }
            return list;
        }

        static int? GetDword(string key, string name)
        {
            using (RegistryKey root = Hklm())
            using (RegistryKey k = root.OpenSubKey(key))
            {
                object v = k == null ? null : k.GetValue(name);
                return v is int ? (int?)(int)v : null;
            }
        }

        static dynamic FwPolicy() { return Activator.CreateInstance(Type.GetTypeFromProgID("HNetCfg.FwPolicy2")); }

        static FwRule FindRule(dynamic policy, string name)
        {
            try
            {
                dynamic r = policy.Rules.Item(name);
                return new FwRule { Name = name, Ports = r.LocalPorts, Remote = r.RemoteAddresses, Profiles = r.Profiles, Enabled = r.Enabled };
            }
            catch { return null; }
        }

        public static Snapshot Read()
        {
            var s = new Snapshot();
            try { s.LocalIps = LocalIps(); } catch { }
            try { s.Proxies = ListProxies(); } catch { }
            try { s.Listening = ListeningPorts(); } catch { }
            try
            {
                dynamic pol = FwPolicy();
                s.Profiles = pol.CurrentProfileTypes;
                foreach (string n in new[] { RuleName }.Concat(LegacyRules))
                {
                    FwRule r = FindRule(pol, n);
                    if (r != null) s.Rules.Add(r);
                }
            }
            catch (Exception ex) { s.FwError = ex.Message; }
            try { using (var sc = new ServiceController("iphlpsvc")) s.IpHelper = sc.Status.ToString(); } catch { s.IpHelper = "ไม่พบ"; }
            try { s.Strict = GetDword(ServerKey, StrictValue); } catch { }
            try { s.Loopback = GetDword(LsaKey, LoopValue); } catch { }
            s.HasState = File.Exists(StateFile);
            return s;
        }

        // ---------- helpers that change the system ----------
        static readonly Encoding Oem = GetOem();
        static Encoding GetOem()
        {
            try { return Encoding.GetEncoding(CultureInfo.InstalledUICulture.TextInfo.OEMCodePage); }
            catch { return Encoding.Default; }
        }

        static int Exec(string exe, string args, out string output)
        {
            var psi = new ProcessStartInfo(exe, args)
            {
                UseShellExecute = false, CreateNoWindow = true,
                RedirectStandardOutput = true, RedirectStandardError = true,
                StandardOutputEncoding = Oem, StandardErrorEncoding = Oem
            };
            using (Process p = Process.Start(psi))
            {
                output = (p.StandardOutput.ReadToEnd() + p.StandardError.ReadToEnd()).Trim();
                p.WaitForExit();
                return p.ExitCode;
            }
        }

        int Run(string exe, string args, bool quiet = false)
        {
            if (!quiet) L(LogKind.Cmd, Path.GetFileNameWithoutExtension(exe) + " " + args);
            string o;
            int code = Exec(exe, args, out o);
            if (code != 0 && o.Length > 0) L(LogKind.Warn, o);
            return code;
        }

        // Interface aliases whose network category is Public (firewall rules for Domain/Private do not apply there).
        public static List<string> PublicAdapters()
        {
            string o;
            Exec(PowerShell, "-NoProfile -Command \"Get-NetConnectionProfile | ? NetworkCategory -eq 'Public' | % InterfaceAlias\"", out o);
            return o.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries).Select(x => x.Trim()).Where(x => x.Length > 0).ToList();
        }

        void SetCategory(string alias, string category)
        {
            Run(PowerShell, "-NoProfile -Command \"Set-NetConnectionProfile -InterfaceAlias '" + alias.Replace("'", "''") + "' -NetworkCategory " + category + "\"");
        }

        void AddProxy(int port)
        {
            Run(Netsh, "interface portproxy add v4tov4 listenport=" + port + " listenaddress=0.0.0.0 connectport=445 connectaddress=127.0.0.1");
        }

        void DeleteProxy(Proxy p)
        {
            Run(Netsh, "interface portproxy delete v4tov4 listenport=" + p.ListenPort + " listenaddress=" + p.ListenAddr);
        }

        int RemoveRules(dynamic policy, string name)
        {
            int n = 0;
            while (n < 50 && FindRule(policy, name) != null) { policy.Rules.Remove(name); n++; }   // Remove() deletes one match per call
            return n;
        }

        void EnsureIpHelper()
        {
            using (var sc = new ServiceController("iphlpsvc"))
            {
                if (sc.Status == ServiceControllerStatus.Running) return;
                L(LogKind.Info, "บริการ IP Helper (iphlpsvc) ยังไม่ทำงาน — กำลังเปิด (portproxy ต้องใช้บริการนี้)");
                Run(Sc, "config iphlpsvc start= auto");
                sc.Start();
                sc.WaitForStatus(ServiceControllerStatus.Running, TimeSpan.FromSeconds(15));
            }
        }

        void RestartIpHelper()
        {
            using (var sc = new ServiceController("iphlpsvc"))
            {
                L(LogKind.Info, "รีสตาร์ทบริการ IP Helper เพื่อให้รับพอร์ตใหม่");
                if (sc.Status != ServiceControllerStatus.Stopped) { sc.Stop(); sc.WaitForStatus(ServiceControllerStatus.Stopped, TimeSpan.FromSeconds(20)); }
                sc.Start();
                sc.WaitForStatus(ServiceControllerStatus.Running, TimeSpan.FromSeconds(20));
            }
        }

        static Dictionary<string, string> LoadState()
        {
            var d = new Dictionary<string, string>();
            if (!File.Exists(StateFile)) return d;
            foreach (string line in File.ReadAllLines(StateFile))
            {
                int i = line.IndexOf('=');
                if (i > 0) d[line.Substring(0, i)] = line.Substring(i + 1);
            }
            return d;
        }

        static void SaveState(Dictionary<string, string> d)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(StateFile));
            File.WriteAllLines(StateFile, d.Select(kv => kv.Key + "=" + kv.Value));
        }

        static string Fmt(int? v) { return v.HasValue ? v.Value.ToString() : "absent"; }

        // Sets a DWORD to 1, or puts back the value saved before the first Apply ("absent" = delete).
        bool SetFlag(string key, string name, bool on, string original)
        {
            int? now = GetDword(key, name);
            int? want = on ? 1 : (original == null || original == "absent" ? (int?)null : int.Parse(original));
            if (now == want) return false;
            using (RegistryKey root = Hklm())
            using (RegistryKey k = root.CreateSubKey(key))
            {
                if (want.HasValue) k.SetValue(name, want.Value, RegistryValueKind.DWord);
                else k.DeleteValue(name, false);
            }
            L(LogKind.Cmd, "reg " + (want.HasValue ? "set " : "delete ") + "HKLM\\" + key + " " + name + (want.HasValue ? " = " + want.Value : ""));
            return true;
        }

        // ---------- SMB probe ----------
        // Sends an SMB2 NEGOTIATE through 127.0.0.1:<port> and returns the dialect Windows answered with.
        public static string SmbProbe(int port, out string error)
        {
            error = null;
            try
            {
                using (var c = new TcpClient())
                {
                    IAsyncResult ar = c.BeginConnect(IPAddress.Loopback, port, null, null);
                    if (!ar.AsyncWaitHandle.WaitOne(2000)) { error = "เชื่อมต่อไม่ได้ (หมดเวลา)"; return null; }
                    c.EndConnect(ar);
                    c.ReceiveTimeout = c.SendTimeout = 3000;
                    NetworkStream s = c.GetStream();
                    byte[] req = Negotiate();
                    s.Write(req, 0, req.Length);
                    var buf = new byte[512];
                    int n = 0;
                    while (n < 74)
                    {
                        int r = s.Read(buf, n, buf.Length - n);
                        if (r <= 0) break;
                        n += r;
                    }
                    if (n >= 74 && buf[4] == 0xFE && buf[5] == 'S' && buf[6] == 'M' && buf[7] == 'B')
                    {
                        int d = buf[72] | buf[73] << 8;
                        return d == 0x0202 ? "SMB 2.0.2" : d == 0x0210 ? "SMB 2.1" : d == 0x0300 ? "SMB 3.0" : d == 0x0302 ? "SMB 3.0.2" : "SMB 0x" + d.ToString("X4");
                    }
                    error = "พอร์ตเปิดแต่ไม่มีคำตอบจาก SMB (ตรวจว่าบริการ Server ของ Windows ทำงานอยู่)";
                    return null;
                }
            }
            catch (Exception ex) { error = ex.Message; return null; }
        }

        static byte[] Negotiate()
        {
            ushort[] dialects = { 0x0202, 0x0210, 0x0300, 0x0302 };
            int body = 36 + dialects.Length * 2, len = 64 + body;
            var b = new byte[4 + len];
            b[2] = (byte)(len >> 8); b[3] = (byte)len;                 // NetBIOS session message length
            b[4] = 0xFE; b[5] = (byte)'S'; b[6] = (byte)'M'; b[7] = (byte)'B';
            b[8] = 64;                                                 // header StructureSize
            b[18] = 1;                                                 // CreditRequest
            int o = 4 + 64;
            b[o] = 36;                                                 // NEGOTIATE StructureSize
            b[o + 2] = (byte)dialects.Length;
            b[o + 4] = 1;                                              // SecurityMode: signing enabled
            Guid.NewGuid().ToByteArray().CopyTo(b, o + 12);            // ClientGuid
            for (int i = 0; i < dialects.Length; i++)
            {
                b[o + 36 + i * 2] = (byte)dialects[i];
                b[o + 37 + i * 2] = (byte)(dialects[i] >> 8);
            }
            return b;
        }

        public void TestPorts(IList<int> ports)
        {
            if (ports.Count == 0) { L(LogKind.Warn, "ยังไม่มีพอร์ตทดสอบที่เปิดอยู่"); return; }
            int ok = 0;
            foreach (int p in ports.Take(20))
            {
                string err;
                string d = SmbProbe(p, out err);
                if (d != null) { ok++; L(LogKind.Ok, "พอร์ต " + p + " → SMB ของเครื่องนี้ตอบกลับ (" + d + ")"); }
                else L(LogKind.Error, "พอร์ต " + p + " → " + err);
            }
            if (ports.Count > 20) L(LogKind.Info, "ทดสอบ 20 พอร์ตแรกจากทั้งหมด " + ports.Count + " พอร์ต");
            if (ok > 0) L(LogKind.Info, "การทดสอบนี้วิ่งภายในเครื่อง (ไม่ผ่าน Firewall) — ขั้นต่อไปให้กด Destination Test ที่เครื่อง Xerox");
        }

        // ---------- main actions ----------
        public bool Apply(Options o)
        {
            string portText = PortsText(o.Ports);
            L(LogKind.Info, "เริ่มเปิดพอร์ต " + portText + " → SMB 445 ของเครื่องนี้");

            List<Proxy> proxies = ListProxies();
            var ours = new HashSet<int>(proxies.Where(p => p.Managed).Select(p => p.ListenPort));
            HashSet<int> busy = ListeningPorts();
            List<int> conflict = o.Ports.Where(p => !ours.Contains(p) && (busy.Contains(p) || proxies.Any(x => x.ListenPort == p))).ToList();
            if (conflict.Count > 0)
            {
                L(LogKind.Error, "พอร์ตต่อไปนี้มีโปรแกรมอื่นใช้อยู่แล้ว: " + PortsText(conflict) + " — กรุณาเลือกพอร์ตอื่น");
                return false;
            }

            EnsureIpHelper();

            Dictionary<string, string> st = LoadState();
            if (!st.ContainsKey(StrictValue))   // remember the original values only once
            {
                st[StrictValue] = Fmt(GetDword(ServerKey, StrictValue));
                st[LoopValue] = Fmt(GetDword(LsaKey, LoopValue));
            }
            st["ports"] = portText;
            st["senders"] = string.Join(",", o.Senders);
            SaveState(st);

            foreach (Proxy p in proxies.Where(p => p.Managed && !o.Ports.Contains(p.ListenPort))) DeleteProxy(p);
            foreach (int port in o.Ports) AddProxy(port);

            dynamic pol = FwPolicy();
            RemoveRules(pol, RuleName);
            foreach (string n in LegacyRules)
                if (RemoveRules(pol, n) > 0) L(LogKind.Info, "ลบกฎ Firewall เดิม \"" + n + "\" (เปิดให้ทุก IP)");
            dynamic rule = Activator.CreateInstance(Type.GetTypeFromProgID("HNetCfg.FWRule"));
            rule.Name = RuleName;
            rule.Description = "Xerox SMB test: TCP " + portText + " -> 127.0.0.1:445 (SmbPortTester)";
            rule.Grouping = RuleName;
            rule.Protocol = 6;                                         // TCP - must be set before ports
            rule.LocalPorts = portText;
            rule.RemoteAddresses = o.Senders.Count > 0 ? string.Join(",", o.Senders) : "*";
            rule.Direction = 1;                                        // inbound
            rule.Action = 1;                                           // allow
            rule.Profiles = o.PrivateOnly ? ProfilesPrivate : ProfilesAll;
            rule.Enabled = true;
            pol.Rules.Add(rule);
            L(LogKind.Cmd, "firewall add \"" + RuleName + "\" TCP " + portText + " from " + (o.Senders.Count > 0 ? string.Join(", ", o.Senders) : "ทุก IP") +
                (o.PrivateOnly ? " (Domain/Private)" : " (ทุกเครือข่าย)"));

            if (o.MakePrivate)
            {
                List<string> pub = PublicAdapters();
                if (pub.Count > 0)
                {
                    string prev;
                    st.TryGetValue("public", out prev);   // remembered so Revert can set them back to Public
                    st["public"] = string.Join("|", (prev ?? "").Split(new[] { '|' }, StringSplitOptions.RemoveEmptyEntries).Union(pub));
                    SaveState(st);
                    foreach (string a in pub) SetCategory(a, "Private");
                    L(LogKind.Ok, "เปลี่ยนประเภทเครือข่าย " + string.Join(", ", pub) + " เป็น Private (จะคืนเป็น Public เมื่อกดปิดพอร์ตและคืนค่า)");
                }
            }

            bool reg = SetFlag(ServerKey, StrictValue, o.StrictName, st[StrictValue]);
            reg |= SetFlag(LsaKey, LoopValue, o.Loopback, st[LoopValue]);

            Thread.Sleep(700);
            List<int> missing = o.Ports.Where(p => !ListeningPorts().Contains(p)).ToList();
            if (missing.Count > 0)
            {
                RestartIpHelper();
                Thread.Sleep(700);
                missing = o.Ports.Where(p => !ListeningPorts().Contains(p)).ToList();
            }
            if (missing.Count > 0) L(LogKind.Error, "Windows ยังไม่รับพอร์ต " + PortsText(missing) + " — ลองรีสตาร์ทเครื่องแล้วกดเปิดพอร์ตอีกครั้ง");
            else L(LogKind.Ok, "เปิดพอร์ต " + portText + " สำเร็จ — Windows กำลังรอรับการเชื่อมต่อ");

            TestPorts(o.Ports);
            if (reg) L(LogKind.Warn, "ค่า Registry จะมีผลหลังรีสตาร์ทเครื่อง (การทดสอบด้วย IP ใช้งานได้ทันทีโดยไม่ต้องรีสตาร์ท)");
            return missing.Count == 0;
        }

        public bool Revert()
        {
            L(LogKind.Info, "เริ่มปิดพอร์ตทดสอบและคืนค่าเดิม");
            Dictionary<string, string> st = LoadState();
            var statePorts = new HashSet<int>();
            string sp;
            if (st.TryGetValue("ports", out sp) && sp.Length > 0)
                try { statePorts = new HashSet<int>(ParsePorts(sp.Replace(",", " "))); } catch { }

            int removed = 0;
            foreach (Proxy p in ListProxies().Where(p => p.Managed || (statePorts.Contains(p.ListenPort) && p.ConnectPort == 445)))
            {
                DeleteProxy(p);
                removed++;
            }
            L(removed > 0 ? LogKind.Ok : LogKind.Info, removed > 0 ? "ปิด Port Proxy แล้ว " + removed + " รายการ" : "ไม่มี Port Proxy ที่ต้องปิด");

            dynamic pol = FwPolicy();
            int rules = RemoveRules(pol, RuleName);
            foreach (string n in LegacyRules) rules += RemoveRules(pol, n);
            L(rules > 0 ? LogKind.Ok : LogKind.Info, rules > 0 ? "ลบกฎ Firewall แล้ว " + rules + " กฎ" : "ไม่มีกฎ Firewall ที่ต้องลบ");

            string s1, s2;
            st.TryGetValue(StrictValue, out s1);
            st.TryGetValue(LoopValue, out s2);
            if (s1 == null) L(LogKind.Info, "ไม่พบค่าเดิมที่บันทึกไว้ — ลบค่า Registry ที่สคริปต์เดิมตั้งไว้ (กลับเป็นค่าเริ่มต้นของ Windows)");
            bool reg = SetFlag(ServerKey, StrictValue, false, s1);
            reg |= SetFlag(LsaKey, LoopValue, false, s2);
            string pubs;
            if (st.TryGetValue("public", out pubs) && pubs.Length > 0)
            {
                foreach (string a in pubs.Split('|')) SetCategory(a, "Public");
                L(LogKind.Ok, "คืนประเภทเครือข่าย " + pubs.Replace("|", ", ") + " เป็น Public ตามเดิม");
            }
            if (File.Exists(StateFile)) File.Delete(StateFile);

            if (ListeningPorts().Contains(445)) L(LogKind.Ok, "คืนค่าเรียบร้อย — เหลือเฉพาะ SMB พอร์ต 445 ตามปกติของ Windows");
            else L(LogKind.Warn, "คืนค่าแล้ว แต่ไม่พบ SMB พอร์ต 445 ทำงานอยู่ (ตรวจบริการ Server / File and Printer Sharing)");
            if (reg) L(LogKind.Warn, "ค่า Registry จะกลับเป็นค่าเดิมอย่างสมบูรณ์หลังรีสตาร์ทเครื่อง");
            return true;
        }
    }
}
