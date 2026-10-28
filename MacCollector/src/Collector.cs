// Data collection + JSON output (field names match the ESP32 "Add Host" form).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Management;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;
using System.Text.RegularExpressions;

namespace MacCollector
{
    class AdapterInfo
    {
        public string Name = "", Model = "", Mac = "";
        public List<string> IPv4 = new List<string>();
        public List<string> IPv6 = new List<string>();
        public List<string> Gateway = new List<string>();
        public bool Wireless;

        public string FirstGlobalIPv6()
        {
            foreach (string a in IPv6)
                if (!a.StartsWith("fe80", StringComparison.OrdinalIgnoreCase)) return a;
            return "";
        }
    }

    class HostRecord
    {
        public string Name = "", Mac = "", Fallback4 = "", Fallback6 = "", Aliases = "", Note = "", RelayPort = "";
        public List<AdapterInfo> Adapters = new List<AdapterInfo>();
    }

    static class Collector
    {
        public static string FormatMac(byte[] b)
        {
            if (b == null || b.Length != 6 || b.All(x => x == 0)) return null;
            return string.Join(":", b.Select(x => x.ToString("X2")));
        }

        public static string NormalizeMac(string s)
        {
            string hex = Regex.Replace(s ?? "", "[^0-9A-Fa-f]", "").ToUpperInvariant();
            if (hex.Length != 12) return null;
            return string.Join(":", Enumerable.Range(0, 6).Select(i => hex.Substring(i * 2, 2)));
        }

        // Connected physical adapters with an IPv4 address; best candidate first.
        public static List<AdapterInfo> GetAdapters()
        {
            var physical = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            try
            {
                using (var s = new ManagementObjectSearcher("SELECT MACAddress FROM Win32_NetworkAdapter WHERE PhysicalAdapter=True"))
                    foreach (ManagementObject o in s.Get())
                    {
                        string m = NormalizeMac(o["MACAddress"] as string);
                        if (m != null) physical.Add(m);
                    }
            }
            catch { /* WMI unavailable: fall back to interface types only */ }

            var list = new List<AdapterInfo>();
            foreach (NetworkInterface ni in NetworkInterface.GetAllNetworkInterfaces())
            {
                if (ni.OperationalStatus != OperationalStatus.Up) continue;
                if (ni.NetworkInterfaceType == NetworkInterfaceType.Loopback || ni.NetworkInterfaceType == NetworkInterfaceType.Tunnel) continue;
                string mac = FormatMac(ni.GetPhysicalAddress().GetAddressBytes());
                if (mac == null) continue;
                if (physical.Count > 0 && !physical.Contains(mac)) continue;   // skip VPN / Hyper-V / VMware adapters

                var a = new AdapterInfo { Name = ni.Name, Model = ni.Description, Mac = mac };
                a.Wireless = ni.NetworkInterfaceType == NetworkInterfaceType.Wireless80211;
                IPInterfaceProperties p = ni.GetIPProperties();
                foreach (UnicastIPAddressInformation u in p.UnicastAddresses)
                {
                    IPAddress ip = u.Address;
                    if (ip.AddressFamily == AddressFamily.InterNetwork)
                    {
                        string s = ip.ToString();
                        if (!s.StartsWith("169.254.")) a.IPv4.Add(s);
                    }
                    else if (ip.AddressFamily == AddressFamily.InterNetworkV6)
                    {
                        string s = ip.ToString();
                        int pct = s.IndexOf('%');
                        a.IPv6.Add(pct > 0 ? s.Substring(0, pct) : s);
                    }
                }
                foreach (GatewayIPAddressInformation g in p.GatewayAddresses)
                    if (g.Address.AddressFamily == AddressFamily.InterNetwork && !g.Address.Equals(IPAddress.Any))
                        a.Gateway.Add(g.Address.ToString());
                if (a.IPv4.Count == 0) continue;
                list.Add(a);
            }
            return list.OrderByDescending(a => a.Gateway.Count > 0).ThenBy(a => a.Wireless).ToList();
        }

        public static string DomainInfo()
        {
            try
            {
                using (var s = new ManagementObjectSearcher("SELECT PartOfDomain, Domain, Workgroup FROM Win32_ComputerSystem"))
                    foreach (ManagementObject o in s.Get())
                    {
                        bool dom = o["PartOfDomain"] is bool && (bool)o["PartOfDomain"];
                        return dom ? "domain: " + o["Domain"] : "workgroup: " + o["Workgroup"];
                    }
            }
            catch { }
            return "domain: " + Environment.UserDomainName;
        }

        public static HostRecord Collect(AdapterInfo primary, List<AdapterInfo> all)
        {
            var r = new HostRecord { Name = Environment.MachineName, Adapters = all };
            if (primary != null)
            {
                r.Mac = primary.Mac;
                r.Fallback4 = primary.IPv4[0];
                r.Fallback6 = primary.FirstGlobalIPv6();
                r.Note = primary.Name + " | user: " + Environment.UserDomainName + "\\" + Environment.UserName + " | " + DomainInfo();
            }
            return r;
        }

        static string Q(string s)
        {
            var sb = new StringBuilder("\"");
            foreach (char c in s ?? "")
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default:
                        if (c < 0x20) sb.Append("\\u").Append(((int)c).ToString("x4"));
                        else sb.Append(c);
                        break;
                }
            }
            return sb.Append('"').ToString();
        }

        static string Arr(IEnumerable<string> items) { return "[" + string.Join(", ", items.Select(Q)) + "]"; }

        public static string ToJson(HostRecord r)
        {
            var lines = new List<string>();
            lines.Add("  \"type\": \"xerox-host-dns-host\"");
            lines.Add("  \"version\": 1");
            lines.Add("  \"created\": " + Q(DateTime.Now.ToString("yyyy-MM-ddTHH:mm:ss")));
            lines.Add("  \"name\": " + Q(r.Name));
            lines.Add("  \"mac\": " + Q(r.Mac));
            if (r.Aliases.Length > 0) lines.Add("  \"aliases\": " + Q(r.Aliases));
            lines.Add("  \"fallback4\": " + Q(r.Fallback4));
            lines.Add("  \"fallback6\": " + Q(r.Fallback6));
            lines.Add("  \"note\": " + Q(r.Note));
            int port;
            if (int.TryParse(r.RelayPort, out port) && port > 0) lines.Add("  \"relayPort\": " + port);
            var ad = r.Adapters.Select(a =>
                "    {\n" +
                "      \"adapter\": " + Q(a.Name) + ",\n" +
                "      \"model\": " + Q(a.Model) + ",\n" +
                "      \"mac\": " + Q(a.Mac) + ",\n" +
                "      \"ipv4\": " + Arr(a.IPv4) + ",\n" +
                "      \"ipv6\": " + Arr(a.IPv6) + ",\n" +
                "      \"gateway\": " + Arr(a.Gateway) + ",\n" +
                "      \"wireless\": " + (a.Wireless ? "true" : "false") + "\n" +
                "    }");
            lines.Add("  \"adapters\": [\n" + string.Join(",\n", ad) + "\n  ]");
            return "{\n" + string.Join(",\n", lines) + "\n}\n";
        }

        public static string Validate(HostRecord r)
        {
            if (!Regex.IsMatch(r.Name, "^[A-Za-z0-9_][A-Za-z0-9_.-]{0,62}$"))
                return "ชื่อเครื่องไม่ถูกต้อง (ใช้ได้เฉพาะ A-Z 0-9 - _ . ไม่เกิน 63 ตัว)";
            if (NormalizeMac(r.Mac) == null) return "MAC Address ไม่ถูกต้อง";
            int port;
            if (r.RelayPort.Length > 0 && (!int.TryParse(r.RelayPort, out port) || port < 1 || port > 65535))
                return "พอร์ต Relay ต้องเป็นตัวเลข 1-65535";
            return null;
        }

        public static void Save(string path, string json)
        {
            File.WriteAllText(path, json, new UTF8Encoding(false));   // no BOM: the web UI parses it directly
        }
    }
}
