// HTTP clients: the ESP32 (Xerox Host DNS Bridge JSON API) and the Xerox AltaLink web pages (EWS).
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Security;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

namespace XeroxBookSync
{
    class Host
    {
        public string Name = "", ScanName = "", Share = "", Login = "", DocPath = "";
        public int RelayPort;
        public bool Enabled = true;
        public Dictionary<string, object> Raw;     // every field as received, written back unchanged
    }

    class Esp32
    {
        public string Address = "", User = "", Pass = "";
        public string Ip = "", Fw = "";
        public List<Host> Hosts = new List<Host>();
        public List<KeyValuePair<string, string>> Printers = new List<KeyValuePair<string, string>>();   // name, ip

        static string S(Dictionary<string, object> d, string k)
        {
            object v;
            return d != null && d.TryGetValue(k, out v) && v != null ? Convert.ToString(v) : "";
        }

        HttpWebRequest Req(string path, string method)
        {
            var r = (HttpWebRequest)WebRequest.Create("http://" + Address + path);
            r.Method = method;
            r.Proxy = null;
            r.Timeout = r.ReadWriteTimeout = 10000;
            // Basic auth is accepted from tools (no browser User-Agent)
            r.Headers["Authorization"] = "Basic " + Convert.ToBase64String(Encoding.UTF8.GetBytes(User + ":" + Pass));
            return r;
        }

        static string Read(WebResponse resp)
        {
            using (resp)
            using (var sr = new StreamReader(resp.GetResponseStream(), Encoding.UTF8)) return sr.ReadToEnd();
        }

        static string Explain(WebException ex)
        {
            var r = ex.Response as HttpWebResponse;
            if (r != null && (int)r.StatusCode == 401) return "ชื่อผู้ใช้หรือรหัสผ่านหน้าเว็บ ESP32 ไม่ถูกต้อง";
            if (r != null)
            {
                try
                {
                    var j = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(Read(r));
                    if (S(j, "error").Length > 0) return S(j, "error");
                }
                catch { }
                return "ESP32 ตอบ HTTP " + (int)r.StatusCode;
            }
            return "ติดต่อ ESP32 ไม่ได้ (" + ex.Message + ")";
        }

        // null = OK, else a message
        public string Load()
        {
            try
            {
                string json = Read(Req("/api/status", "GET").GetResponse());
                var root = new JavaScriptSerializer { MaxJsonLength = int.MaxValue }.Deserialize<Dictionary<string, object>>(json);
                var dev = root["device"] as Dictionary<string, object>;
                Ip = S(dev, "ip");
                Fw = S(dev, "fw");
                Hosts.Clear();
                foreach (Dictionary<string, object> h in (ArrayList)root["hosts"])
                {
                    int port;
                    int.TryParse(S(h, "relayPort"), out port);
                    Hosts.Add(new Host
                    {
                        Name = S(h, "name"), ScanName = S(h, "scanName"), Share = S(h, "share"), Login = S(h, "login"),
                        DocPath = S(h, "docPath"), RelayPort = port, Enabled = S(h, "enabled") != "False", Raw = h
                    });
                }
                Printers.Clear();
                object pr;
                if (root.TryGetValue("printers", out pr) && pr is ArrayList)
                    foreach (Dictionary<string, object> p in (ArrayList)pr)
                        Printers.Add(new KeyValuePair<string, string>(S(p, "name"), S(p, "ip")));
                return null;
            }
            catch (WebException ex) { return Explain(ex); }
            catch (Exception ex) { return "อ่านข้อมูลจาก ESP32 ไม่ได้: " + ex.Message; }
        }

        // Newest failed relay entry of this host (the ESP32 decodes the SMB error, e.g. which login was refused).
        public string LastRelayError(string host)
        {
            try
            {
                string json = Read(Req("/api/status", "GET").GetResponse());
                var root = new JavaScriptSerializer { MaxJsonLength = int.MaxValue }.Deserialize<Dictionary<string, object>>(json);
                object rl;
                if (!root.TryGetValue("relays", out rl) || !(rl is ArrayList)) return "";
                foreach (Dictionary<string, object> r in (ArrayList)rl)       // newest first
                {
                    if (!string.Equals(S(r, "host"), host, StringComparison.OrdinalIgnoreCase)) continue;
                    DateTime t;
                    if (DateTime.TryParse(S(r, "time"), out t) && Math.Abs((DateTime.Now - t).TotalSeconds) > 90) return "";
                    return S(r, "ok") == "True" ? "" : S(r, "result");
                }
            }
            catch { }
            return "";
        }

        static readonly string[] HostFields = { "name", "scanName", "mac", "aliases", "fallback4", "fallback6", "note", "enabled", "relayPort", "share", "docPath", "login" };

        // Writes share / login / path / scan name of our hosts back. The ESP32 replaces its whole host list
        // on save, so the current list is read again first and only those fields of hosts with the same
        // name are changed: hosts added, edited or removed meanwhile in the web UI are kept as they are.
        public string SaveHosts()
        {
            var mine = Hosts.ToDictionary(h => h.Name, StringComparer.OrdinalIgnoreCase);
            var fresh = new Esp32 { Address = Address, User = User, Pass = Pass };
            string err = fresh.Load();
            if (err != null) return err;
            if (fresh.Hosts.Count == 0) return "ESP32 ไม่มี Host – ไม่บันทึก";
            var list = new List<Dictionary<string, object>>();
            foreach (Host cur in fresh.Hosts)
            {
                var o = new Dictionary<string, object>();
                foreach (string k in HostFields)
                {
                    object v;
                    if (cur.Raw.TryGetValue(k, out v)) o[k] = v;
                }
                Host h;
                if (mine.TryGetValue(cur.Name, out h))
                {
                    o["share"] = h.Share; o["login"] = h.Login; o["docPath"] = h.DocPath;
                    if (cur.ScanName.Length == 0) o["scanName"] = h.ScanName;
                }
                list.Add(o);
            }
            try
            {
                byte[] body = Encoding.UTF8.GetBytes(new JavaScriptSerializer().Serialize(list));
                var r = Req("/api/hosts", "POST");
                r.ContentType = "application/json";
                r.ContentLength = body.Length;
                using (Stream s = r.GetRequestStream()) s.Write(body, 0, body.Length);
                Read(r.GetResponse());
                return null;
            }
            catch (WebException ex) { return Explain(ex); }
            catch (Exception ex) { return ex.Message; }
        }
    }

    class Entry
    {
        public string Nick = "", Ip = "", Share = "", Login = "", Path = "", Password = "";
        public int Port;
    }

    class TestResult
    {
        public bool Valid;
        public string Summary = "";                          // "Destination setup is valid."
        public List<KeyValuePair<string, string>> Steps = new List<KeyValuePair<string, string>>();
        public string Failed()
        {
            var bad = Steps.Where(s => s.Value != "Passed" && s.Value != "Not Applicable").Select(s => s.Key + " " + s.Value);
            return string.Join(", ", bad);
        }
    }

    // Talks to the AltaLink web pages exactly like the browser does (same form fields), so nothing
    // has to be enabled on the copier. Verified on AltaLink C8155.
    class Xerox
    {
        public readonly string Ip;
        readonly string user, pass;
        CookieContainer jar;
        string token;

        public Xerox(string ip, string user, string pass)
        {
            Ip = ip; this.user = user; this.pass = pass;
        }

        static Xerox()
        {
            ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
            // the copier uses a certificate from the Xerox device CA: accept it for the copier only
            ServicePointManager.ServerCertificateValidationCallback = (sender, cert, chain, errors) =>
            {
                var req = sender as HttpWebRequest;
                return errors == SslPolicyErrors.None || (req != null && Allowed.Contains(req.RequestUri.Host));
            };
        }
        static readonly HashSet<string> Allowed = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        string Url(string path) { return "https://" + Ip + path; }

        string Send(string path, IList<KeyValuePair<string, string>> form, out string finalUrl)
        {
            Allowed.Add(Ip);
            var r = (HttpWebRequest)WebRequest.Create(Url(path));
            r.Proxy = null;
            r.CookieContainer = jar;
            r.Timeout = r.ReadWriteTimeout = 60000;
            r.UserAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) XeroxBookSync/1.0";
            r.Referer = Url("/addressbook/viewContact.php");
            r.Headers["Origin"] = "https://" + Ip;
            if (form != null)
            {
                r.Method = "POST";
                r.ContentType = "application/x-www-form-urlencoded";
                string body = string.Join("&", form.Select(p => Uri.EscapeDataString(p.Key) + "=" + Uri.EscapeDataString(p.Value ?? "")));
                byte[] b = Encoding.UTF8.GetBytes(body);
                r.ContentLength = b.Length;
                using (Stream s = r.GetRequestStream()) s.Write(b, 0, b.Length);
            }
            using (var resp = (HttpWebResponse)r.GetResponse())
            using (var sr = new StreamReader(resp.GetResponseStream(), Encoding.UTF8))
            {
                finalUrl = resp.ResponseUri.AbsolutePath;
                return sr.ReadToEnd();
            }
        }
        string Get(string path) { string u; return Send(path, null, out u); }

        static List<KeyValuePair<string, string>> F(params string[] kv)
        {
            var l = new List<KeyValuePair<string, string>>();
            for (int i = 0; i + 1 < kv.Length; i += 2) l.Add(new KeyValuePair<string, string>(kv[i], kv[i + 1]));
            return l;
        }

        static string Token(string html)
        {
            Match m = Regex.Match(html, "name=\"CSRFToken\" value=\"([^\"]+)\"");
            if (!m.Success) throw new Exception("ไม่พบ CSRFToken ในหน้าเว็บของ Xerox");
            return m.Groups[1].Value;
        }

        // null = OK, else a message
        public string Login()
        {
            jar = new CookieContainer();     // a fresh session: the copier refuses a login on top of an old one
            try
            {
                string page = Get("/properties/authentication/login.php");
                Match next = Regex.Match(page, "name=\"NextPage\"\\s*value=\"([^\"]*)\"");
                string u;
                Send("/userpost/xerox.set", F("_fun_function", "HTTP_Authenticate_fn", "NextPage", next.Success ? next.Groups[1].Value : "",
                    "frmwebUsername", user, "frmwebPassword", pass, "frmaltDomain", "default", "CSRFToken", Token(page)), out u);
                string add = Send("/addressbook/modifyContact.php?operation=add", null, out u);
                if (u.Contains("login") || u.Contains("no_modify") || !add.Contains("frmFriendlyName"))
                    return "ล็อกอิน Xerox ไม่สำเร็จ – ตรวจชื่อผู้ใช้ / รหัสผ่าน Admin";
                token = Token(add);
                return null;
            }
            catch (WebException ex)
            {
                var r = ex.Response as HttpWebResponse;
                return r != null ? "Xerox ตอบ HTTP " + (int)r.StatusCode : "ติดต่อ Xerox " + Ip + " ไม่ได้ (" + ex.Message + ")";
            }
            catch (Exception ex) { return ex.Message; }
        }

        // id -> display name
        public List<KeyValuePair<string, string>> Contacts()
        {
            string page = Get("/addressbook/viewContact.php");
            var l = new List<KeyValuePair<string, string>>();
            foreach (Match m in Regex.Matches(page, "href\\s*=\\s*\"/addressbook/viewContact\\.php\\?contactId=(\\d+)&selectedLink=1\">\\s*(?:<img[^>]*>)?\\s*([^<]*?)\\s*</a>"))
                l.Add(new KeyValuePair<string, string>(m.Groups[1].Value, Clean(m.Groups[2].Value)));
            return l;
        }

        public static string Clean(string s) { return WebUtility.HtmlDecode(s).Replace('\u00a0', ' ').Trim(); }

        // Destination Test (same as the button in the Add Contact page)
        public TestResult Test(Entry e)
        {
            string u;
            Send("/dummypost/xerox.set", F("NextPage", "/netscan/desttestresult.php", "_fun_function", "HTTP_Repositary_StartTest_fn",
                "ReposFriendlyname", e.Nick, "ReposProtocolType", "SMB", "ReposServerType", "", "ReposServer", e.Ip + ":" + e.Port,
                "ReposVolume", e.Share, "ReposNdsTree", "", "ReposNdscontext", "", "ReposDocPath", e.Path, "ReposHttpScript", "",
                "ReposHttpValidate", "", "Reposcredsourcetype", "", "CSRFToken", token, "ReposUsername", e.Login, "ReposPassword", e.Password), out u);
            string page = Get("/netscan/desttestresult.php");
            string text = Regex.Replace(Clean(Regex.Replace(Regex.Replace(page, "<(script|style)[^>]*>.*?</\\1>", " ", RegexOptions.Singleline), "<[^>]+>", " ")), "\\s+", " ");
            var res = new TestResult();
            Match h = Regex.Match(text, "Test Result:\\s*([^.]*\\.)");
            if (!h.Success) throw new Exception("อ่านผล Destination Test ไม่ได้ (หน้าเว็บของ Xerox อาจต่างรุ่น)");
            res.Summary = h.Groups[1].Value;
            res.Valid = res.Summary.IndexOf("is valid", StringComparison.OrdinalIgnoreCase) >= 0;
            foreach (Match m in Regex.Matches(text, "(Ping Test|Authentication Server Test|Destination Server Test|File Write Test|File Remove Test)\\s+(Passed|Failed|Not Applicable|[A-Z][a-z]+)"))
                res.Steps.Add(new KeyValuePair<string, string>(m.Groups[1].Value, m.Groups[2].Value));
            return res;
        }

        // Save = "Add Contact": First Name = Display Name = Nickname, SMB, IPv4 address + port.
        public void Add(Entry e)
        {
            string u;
            string add = Send("/addressbook/modifyContact.php?operation=add", null, out u);
            token = Token(add);
            Match cid = Regex.Match(add, "name=\"contactID\" value=\"(\\d+)\"");
            Send("/dummypost/xerox.set", F(
                "_fun_function", "HTTP_Set_Config_Non_Sec_fn", "_fun_function", "HTTP_Add_Contacts_To_Address_Book_Record_fn",
                "NextPage", "/addressbook/modifyContact.php?operation=add", "contactID", cid.Success ? cid.Groups[1].Value : "0",
                "IsGlobalFavorite", "0", "IsEmailFavorite", "0", "IsScanFavorite", "0", "IsFaxFavorite", "0", "CertificatePath", "",
                "CSRFToken", token, "security.FIPS.SMBEnabled", "TRUE",
                "FirstName", e.Nick, "LastName", "", "Company", "", "DisplayName", e.Nick, "Email", "", "FaxNumber", "", "FriendlyName", e.Nick,
                "ServerName", "", "ServerVolume", "", "SmbShare", e.Share, "DocumentPath", e.Path, "LoginName", e.Login, "IfaxAddress", "",
                "AddNew", "No", "Protocol", "SMB", "groupcount", "0", "Protocol", "2", "ServerVolume", e.Share,
                "ServerName", e.Ip + ":" + e.Port, "ServerType", "1", "LoginName", e.Login, "XrxPsswd", e.Password), out u);
        }
    }
}
