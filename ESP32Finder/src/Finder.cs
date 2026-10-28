// Finds Xerox Host DNS Bridge boards on the LAN. The board's web server answers every request with
// "WWW-Authenticate: Basic realm="Xerox Host DNS"", so a sweep of TCP 80 identifies it without
// logging in and without any change to the firmware.
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;

namespace ESP32Finder
{
    class Device
    {
        public string Ip, Mac = "", How = "", Fw = "", Hostname = "", Mode = "", Link = "", Uptime = "", Hosts = "", Note = "", PrevIp = "";
        public bool LoggedIn;
    }

    class IpRange
    {
        public uint First, Last;
        public string Label;
        public int Count { get { return (int)(Last - First + 1); } }
    }

    static class Finder
    {
        public const string Realm = "Xerox Host DNS";
        public const string DefaultHostname = "xerox-dns";
        const int MaxRange = 1024;   // larger subnets are narrowed to the /24 around this PC

        static readonly string KnownFile = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "ESP32Finder", "known.txt");

        public static uint ToUint(IPAddress a)
        {
            byte[] b = a.GetAddressBytes();
            return (uint)b[0] << 24 | (uint)b[1] << 16 | (uint)b[2] << 8 | b[3];
        }

        public static string ToIp(uint v) { return (v >> 24) + "." + (v >> 16 & 255) + "." + (v >> 8 & 255) + "." + (v & 255); }

        // ---------- what to scan ----------
        public static List<IpRange> LocalRanges(List<string> ownIps)
        {
            var list = new List<IpRange>();
            foreach (NetworkInterface ni in NetworkInterface.GetAllNetworkInterfaces())
            {
                if (ni.OperationalStatus != OperationalStatus.Up || ni.NetworkInterfaceType == NetworkInterfaceType.Loopback) continue;
                foreach (UnicastIPAddressInformation u in ni.GetIPProperties().UnicastAddresses)
                {
                    if (u.Address.AddressFamily != AddressFamily.InterNetwork || u.IPv4Mask == null) continue;
                    string s = u.Address.ToString();
                    if (s.StartsWith("169.254.")) continue;
                    ownIps.Add(s);
                    uint ip = ToUint(u.Address), mask = ToUint(u.IPv4Mask);
                    if (mask == 0xFFFFFFFF) continue;
                    if (~mask + 1 > MaxRange) mask = 0xFFFFFF00;
                    var r = new IpRange { First = (ip & mask) + 1, Last = (ip | ~mask) - 1, Label = ni.Name };
                    if (!list.Any(x => x.First == r.First && x.Last == r.Last)) list.Add(r);
                }
            }
            return list;
        }

        // "192.168.30.0/24", "192.168.30.10-60", "192.168.30.10-192.168.30.60" or one IP
        public static IpRange ParseRange(string text)
        {
            text = text.Trim();
            IPAddress a, b;
            int slash = text.IndexOf('/'), dash = text.IndexOf('-');
            if (slash > 0)
            {
                int bits;
                if (!IPAddress.TryParse(text.Substring(0, slash), out a) || !int.TryParse(text.Substring(slash + 1), out bits) || bits < 22 || bits > 32)
                    throw new FormatException("รูปแบบวงไม่ถูกต้อง (ใช้ /22 – /32): " + text);
                uint mask = bits == 32 ? 0xFFFFFFFF : ~(0xFFFFFFFFu >> bits), ip = ToUint(a);
                return bits >= 31 ? new IpRange { First = ip, Last = ip, Label = text }
                                  : new IpRange { First = (ip & mask) + 1, Last = (ip | ~mask) - 1, Label = text };
            }
            if (dash > 0)
            {
                string left = text.Substring(0, dash), right = text.Substring(dash + 1);
                if (!IPAddress.TryParse(left, out a)) throw new FormatException("IP ไม่ถูกต้อง: " + left);
                uint first = ToUint(a), last;
                int n;
                if (int.TryParse(right, out n) && n >= 0 && n <= 255) last = (first & 0xFFFFFF00) | (uint)n;
                else if (IPAddress.TryParse(right, out b)) last = ToUint(b);
                else throw new FormatException("ช่วง IP ไม่ถูกต้อง: " + text);
                if (last < first || last - first >= MaxRange) throw new FormatException("ช่วง IP ไม่ถูกต้องหรือกว้างเกิน " + MaxRange + " IP: " + text);
                return new IpRange { First = first, Last = last, Label = text };
            }
            if (!IPAddress.TryParse(text, out a) || a.AddressFamily != AddressFamily.InterNetwork) throw new FormatException("IP ไม่ถูกต้อง: " + text);
            uint v = ToUint(a);
            return new IpRange { First = v, Last = v, Label = text };
        }

        // ---------- probes ----------
        public static async Task<bool> PortOpen(string ip, int port, int timeoutMs)
        {
            var c = new TcpClient();
            try
            {
                Task t = c.ConnectAsync(ip, port);
                if (await Task.WhenAny(t, Task.Delay(timeoutMs)) != t)
                {
                    var observed = t.ContinueWith(x => { var e = x.Exception; }, TaskContinuationOptions.OnlyOnFaulted);   // late connect errors
                    return false;
                }
                await t;
                return c.Connected;
            }
            catch { return false; }
            finally { c.Close(); }
        }

        // True when the web server on ip is a Xerox Host DNS Bridge board.
        public static bool IsBridge(string ip)
        {
            try
            {
                var req = (HttpWebRequest)WebRequest.Create("http://" + ip + "/");
                req.Proxy = null;
                req.Timeout = req.ReadWriteTimeout = 3000;
                req.AllowAutoRedirect = false;
                using (var resp = (HttpWebResponse)req.GetResponse())
                using (var sr = new StreamReader(resp.GetResponseStream(), Encoding.UTF8))
                    return sr.ReadToEnd().Contains("Xerox Host DNS Bridge");   // only if login is ever disabled
            }
            catch (WebException ex)
            {
                var r = ex.Response as HttpWebResponse;
                if (r == null) return false;
                using (r)
                {
                    string auth = r.Headers["WWW-Authenticate"] ?? "";
                    return (int)r.StatusCode == 401 && auth.IndexOf(Realm, StringComparison.OrdinalIgnoreCase) >= 0;
                }
            }
            catch { return false; }
        }

        public static async Task<List<string>> ResolveName(string name, int timeoutMs)
        {
            var list = new List<string>();
            try
            {
                Task<IPAddress[]> t = Dns.GetHostAddressesAsync(name);
                if (await Task.WhenAny(t, Task.Delay(timeoutMs)) == t)
                    list.AddRange(t.Result.Where(a => a.AddressFamily == AddressFamily.InterNetwork).Select(a => a.ToString()));
            }
            catch { }
            return list;
        }

        [DllImport("iphlpapi.dll")] static extern int SendARP(uint destIp, uint srcIp, byte[] mac, ref int len);

        // MAC from ARP (same subnet only)
        public static string MacOf(string ip)
        {
            try
            {
                var mac = new byte[6];
                int len = mac.Length;
                uint dest = BitConverter.ToUInt32(IPAddress.Parse(ip).GetAddressBytes(), 0);
                if (SendARP(dest, 0, mac, ref len) != 0 || len < 6) return "";
                return string.Join(":", mac.Select(x => x.ToString("X2")));
            }
            catch { return ""; }
        }

        static string Str(Dictionary<string, object> d, string k)
        {
            object v;
            return d != null && d.TryGetValue(k, out v) && v != null ? Convert.ToString(v) : "";
        }

        // Reads /api/status with the web login. Returns null on success, else a message.
        public static string LoadStatus(Device d, string user, string pass)
        {
            try
            {
                var req = (HttpWebRequest)WebRequest.Create("http://" + d.Ip + "/api/status");
                req.Proxy = null;
                req.Timeout = req.ReadWriteTimeout = 8000;
                req.Headers["Authorization"] = "Basic " + Convert.ToBase64String(Encoding.UTF8.GetBytes(user + ":" + pass));
                string json;
                using (WebResponse resp = req.GetResponse())
                using (var sr = new StreamReader(resp.GetResponseStream(), Encoding.UTF8))
                    json = sr.ReadToEnd();
                var root = new JavaScriptSerializer { MaxJsonLength = int.MaxValue }.Deserialize<Dictionary<string, object>>(json);
                object devObj;
                root.TryGetValue("device", out devObj);
                var dev = devObj as Dictionary<string, object>;
                d.Fw = Str(dev, "fw");
                d.Hostname = Str(dev, "hostname");
                if (Str(dev, "mac").Length > 0) d.Mac = Str(dev, "mac").ToUpperInvariant();
                d.Mode = Str(dev, "dhcp") == "True" ? "DHCP" : "Static IP";
                d.Link = Str(dev, "link") == "True" ? Str(dev, "speed") + " Mbps " + Str(dev, "duplex") : "ไม่มี Link";
                d.Uptime = Str(dev, "uptime");
                object hosts;
                if (root.TryGetValue("hosts", out hosts) && hosts is ArrayList) d.Hosts = ((ArrayList)hosts).Count + " Host";
                d.LoggedIn = true;
                return null;
            }
            catch (WebException ex)
            {
                var r = ex.Response as HttpWebResponse;
                if (r != null && (int)r.StatusCode == 401) return "ชื่อผู้ใช้หรือรหัสผ่านหน้าเว็บไม่ถูกต้อง";
                return ex.Message;
            }
            catch (Exception ex) { return ex.Message; }
        }

        // ---------- remembered boards (MAC -> last IP) ----------
        public static Dictionary<string, string> LoadKnown()
        {
            var d = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            try
            {
                if (File.Exists(KnownFile))
                    foreach (string line in File.ReadAllLines(KnownFile))
                    {
                        string[] p = line.Split(' ');
                        if (p.Length == 2) d[p[0]] = p[1];
                    }
            }
            catch { }
            return d;
        }

        public static void SaveKnown(Dictionary<string, string> d)
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(KnownFile));
                File.WriteAllLines(KnownFile, d.Select(kv => kv.Key + " " + kv.Value));
            }
            catch { }
        }
    }
}
