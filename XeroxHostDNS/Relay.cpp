#include "Relay.h"
#include "AppState.h"
#include "Discovery.h"
#include "PortMux.h"
#include "Resolver.h"
#include "Util.h"
#include <lwip/sockets.h>
#include <lwip/tcp.h>
#include <fcntl.h>
#include <errno.h>

static const int      RELAY_BUF          = 4096;
static const uint32_t CONNECT_TIMEOUT_MS = 4000;
static const uint32_t RESOLVE_WAIT_MS    = 3000;   // on-demand lookup before connecting to the PC
static const uint32_t IDLE_TIMEOUT_MS    = 120000;

// One listener serves every relay port: PortMux redirects them all to PORTMUX_PORT.
static int listenFd = -1;

struct Pipe {
  uint8_t buf[RELAY_BUF];
  int len = 0, off = 0;
  bool eof = false, shut = false;
};

// SMB2 header inspection (the data itself is relayed untouched). A copier opens many short
// connections per job (connection test, folder check, lock file, the document), so a session
// is only logged when it writes a file or SMB reports an error - with the file name, size and
// a readable reason (empty password, locked account, wrong share ...).
static const int SMB_CAP = 384;    // bytes kept from the start of each message (header + names)

struct SmbStream {                 // NetBIOS framing state for one direction
  uint8_t  nb[4];
  uint8_t  nbn = 0;
  uint32_t len = 0, got = 0;
  uint8_t  cap[SMB_CAP];
  uint16_t capn = 0;
};

struct Session {
  bool used = false;
  int cfd = -1, sfd = -1;
  bool connecting = false, resolving = false;
  String host, client;
  uint32_t roundsAt = 0, connStart = 0;   // lookup round counter when the copier connected; connect() time
  uint32_t clientIp = 0, target = 0;
  uint16_t port = 0;
  uint32_t start = 0, lastAct = 0;
  uint64_t up = 0, down = 0;
  Pipe c2s, s2c;
  SmbStream mc, ms;                // client->server, server->client
  bool smb = false, encrypted = false, emptyPw = false, removed = false, authed = false;
  bool pcSeen = false;              // the PC answered SMB: host status -> online
  String user, share, opened, file;
  uint64_t written = 0;
  uint16_t writes = 0;
  uint32_t err = 0;                // first SMB error status
  uint16_t errCmd = 0;
};
static Session sess[MAX_RELAY_SESSIONS];

// ---------------------------------------------------------------------------
static void setNonBlock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }

static void setNoDelay(int fd) {
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

static int hostIndex(const String& name) {
  for (size_t i = 0; i < cfg.hosts.size(); i++)
    if (cfg.hosts[i].name.equalsIgnoreCase(name)) return i;
  return -1;
}

// ---- SMB2 inspection ---------------------------------------------------------
static uint16_t rd16(const uint8_t* p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t* p) { return rd16(p) | (uint32_t)rd16(p + 2) << 16; }

static String utf16(const uint8_t* p, int bytes) {      // UTF-16LE -> UTF-8 (BMP only)
  String o;
  for (int i = 0; i + 1 < bytes; i += 2) {
    uint16_t c = rd16(p + i);
    if (c < 0x80) o += (char)c;
    else if (c < 0x800) { o += (char)(0xC0 | c >> 6); o += (char)(0x80 | (c & 0x3F)); }
    else { o += (char)(0xE0 | c >> 12); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
  }
  return o;
}

static String fmtSize(uint64_t b) {
  if (b < 1024) return String((uint32_t)b) + " B";
  if (b < 1048576) return String(b / 1024.0, 1) + " KB";
  return String(b / 1048576.0, 1) + " MB";
}

enum { SMB_SESSION_SETUP = 1, SMB_TREE_CONNECT = 3, SMB_CREATE = 5, SMB_WRITE = 9, SMB_SET_INFO = 17 };

static void smbRequest(Session& s, uint16_t cmd, const uint8_t* m, int n) {
  const uint8_t* b = m + 64;
  switch (cmd) {
    case SMB_SESSION_SETUP:
      for (int i = 64; i + 52 <= n; i++) {
        if (memcmp(m + i, "NTLMSSP", 8) || rd32(m + i + 8) != 3) continue;   // AUTHENTICATE message
        s.emptyPw = rd16(m + i + 20) == 0;                                     // NT response length
        uint16_t ul = rd16(m + i + 36);
        uint32_t uo = rd32(m + i + 40);
        if (ul && i + uo + ul <= (uint32_t)n) s.user = utf16(m + i + uo, ul);
        break;
      }
      break;
    case SMB_TREE_CONNECT:
      if (n >= 72) {
        uint16_t po = rd16(b + 4), pl = rd16(b + 6);
        if (po + pl <= n) {
          String p = utf16(m + po, pl);
          String sh = p.substring(p.lastIndexOf('\\') + 1);
          if (!sh.equalsIgnoreCase("IPC$")) s.share = sh;
        }
      }
      break;
    case SMB_CREATE:
      if (n >= 64 + 48) {
        uint16_t no = rd16(b + 44), nl = rd16(b + 46);
        if (nl && no + nl <= n) s.opened = utf16(m + no, nl);
      }
      break;
    case SMB_WRITE:
      if (n >= 64 + 8) {
        s.written += rd32(b + 4);
        s.writes++;
        s.file = s.opened;
      }
      break;
    case SMB_SET_INFO:
      if (n >= 64 + 16 && b[2] == 1) {                       // file information
        if (b[3] == 13) s.removed = true;                     // FileDispositionInformation (delete)
        else if (b[3] == 10) {                                // FileRenameInformation: temp name -> final name
          uint16_t bo = rd16(b + 8);
          if (bo + 20 <= n) {
            uint32_t fl = rd32(m + bo + 16);
            if (fl && bo + 20 + fl <= (uint32_t)n) s.file = utf16(m + bo + 20, fl);
          }
        }
      }
      break;
  }
}

static void smbResponse(Session& s, uint16_t cmd, uint32_t status) {
  if (!s.pcSeen) {
    s.pcSeen = true;
    resolverSeen(hostIndex(s.host), s.target, "Relay (SMB)");
  }
  if (cmd == SMB_SESSION_SETUP && status == 0) s.authed = true;
  if ((status & 0xC0000000) != 0xC0000000 || s.err) return;  // only the first real error counts
  if (status == 0xC0000016) return;                           // MORE_PROCESSING_REQUIRED (NTLM step)
  bool track = cmd == SMB_SESSION_SETUP || cmd == SMB_TREE_CONNECT || cmd == SMB_WRITE ||
               (cmd == SMB_CREATE && status != 0xC0000034 && status != 0xC000003A && status != 0xC0000035);   // name/path probes are normal
  if (!track) return;
  s.err = status;
  s.errCmd = cmd;
}

static void smbMessage(Session& s, bool request, const uint8_t* m, int n) {
  if (n >= 4 && m[0] == 0xFD && m[1] == 'S') { s.encrypted = true; return; }   // SMB3 encrypted: content hidden
  if (n < 64 || m[0] != 0xFE || m[1] != 'S' || m[2] != 'M' || m[3] != 'B') return;
  s.smb = true;
  for (int off = 0; off + 64 <= n; ) {                         // compound requests: follow NextCommand
    const uint8_t* h = m + off;
    uint16_t cmd = rd16(h + 12);
    if (request) smbRequest(s, cmd, h, n - off);
    else smbResponse(s, cmd, rd32(h + 8));
    uint32_t next = rd32(h + 20);
    if (!next) break;
    off += next;
  }
}

static void smbFeed(Session& s, bool request, const uint8_t* d, int n) {
  SmbStream& t = request ? s.mc : s.ms;
  while (n > 0) {
    if (t.nbn < 4) {                                           // 4-byte NetBIOS session header
      t.nb[t.nbn++] = *d++;
      n--;
      if (t.nbn == 4) {
        t.len = (uint32_t)t.nb[1] << 16 | t.nb[2] << 8 | t.nb[3];
        t.got = t.capn = 0;
        if (!t.len) t.nbn = 0;
      }
      continue;
    }
    uint32_t take = min((uint32_t)n, t.len - t.got);
    if (t.capn < SMB_CAP) {
      uint16_t c = min<uint32_t>(take, SMB_CAP - t.capn);
      memcpy(t.cap + t.capn, d, c);
      t.capn += c;
    }
    t.got += take;
    d += take;
    n -= take;
    if (t.got == t.len) {
      if (t.nb[0] == 0) smbMessage(s, request, t.cap, t.capn);
      t.nbn = 0;
    }
  }
}

static String smbReason(const Session& s) {
  String who = s.user.length() ? " (ผู้ใช้ " + s.user + ")" : "";
  switch (s.err) {
    // Blank password: a PC that shares to Everyone lets unknown names in as Guest, but a name that is
    // a real account on the PC must come with that account's password.
    case 0xC000006D: return s.emptyPw ? "ล็อกอินไม่ผ่าน: ไม่มีรหัสผ่าน แต่ Login Name นี้เป็นบัญชีจริงบน PC – ใส่รหัสของบัญชีนี้ใน Xerox (ติ๊ก Select to save new password) หรือเปลี่ยน Login Name เป็นชื่อที่ไม่มีบน PC เช่น xerox" + who
                                      : "ล็อกอินไม่ผ่าน: ชื่อผู้ใช้หรือรหัสผ่านไม่ถูกต้อง" + who;
    case 0xC0000234: return "บัญชีถูกล็อกเพราะใส่รหัสผิดหลายครั้ง – รอประมาณ 10 นาทีหรือปลดล็อกที่ PC" + who;
    case 0xC0000072: return "บัญชีถูกปิดใช้งานที่ PC" + who;
    case 0xC0000071: return "รหัสผ่านของบัญชีหมดอายุ" + who;
    case 0xC000006E:
    case 0xC000015B: return "บัญชีไม่ได้รับสิทธิ์ล็อกอินผ่านเครือข่าย" + who;
    // The copier asks for \\<ESP32-IP>\share; a PC that validates the server name answers the same way.
    case 0xC00000CC: return "ไม่พบ Share \"" + s.share + "\" บน PC – ถ้าชื่อ Share ถูกต้อง ให้ตั้ง DisableStrictNameChecking=1 ที่ PC แล้วรีสตาร์ท";
    case 0xC0000022: return s.errCmd == SMB_TREE_CONNECT ? "ไม่มีสิทธิ์เข้า Share \"" + s.share + "\"" + who
                                                       : "ไม่มีสิทธิ์เขียนไฟล์ในโฟลเดอร์" + who;
    case 0xC000007F: return "พื้นที่ดิสก์ของ PC เต็ม";
    case 0xC0000043: return "ไฟล์ถูกเปิดใช้งานอยู่ (Sharing violation)";
  }
  char b[48];
  snprintf(b, sizeof(b), "SMB error 0x%08X (คำสั่ง %u)", (unsigned)s.err, s.errCmd);
  return String(b) + who;
}

// ---- Sessions ------------------------------------------------------------------
// counted = false: the session is closed without a log entry (a copier's probe connection).
static void endSession(Session& s, bool ok, const String& result, bool counted = true) {
  if (s.cfd >= 0) close(s.cfd);
  if (s.sfd >= 0) close(s.sfd);
  uint32_t secs = (millis() - s.start) / 1000;
  int idx = hostIndex(s.host);
  if (counted) {
    RelayEntry e{nowStr(), s.client, s.host, ip4Str(s.target) + ":" + String(RELAY_TARGET_PORT),
                 result, s.writes ? s.written : s.up + s.down, secs, ok};
    addRelayLog(e);
    if (idx >= 0 && idx < (int)hostState.size()) {
      HostState& h = hostState[idx];
      if (ok) h.relayJobs++;
      else {
        h.relayFails++;
        if (!s.smb) {                                        // never reached the PC: it may have moved
          resolverPrioritize(idx);
          if (cfg.hosts[idx].mac.length()) discRequestScan();
        }
      }
      h.relayLast = e.time;
    }
  }
  if (idx >= 0 && idx < (int)hostState.size()) hostState[idx].relayBytes += s.up + s.down;
  // reset field by field: a temporary Session() would put 8 KB of buffers on the loop stack
  s.used = s.connecting = s.resolving = false;
  s.cfd = s.sfd = -1;
  s.host = s.client = "";
  s.clientIp = s.target = 0;
  s.up = s.down = 0;
  for (Pipe* p : {&s.c2s, &s.s2c}) { p->len = p->off = 0; p->eof = p->shut = false; }
  for (SmbStream* t : {&s.mc, &s.ms}) { t->nbn = 0; t->len = t->got = t->capn = 0; }
  s.smb = s.encrypted = s.emptyPw = s.removed = s.authed = s.pcSeen = false;
  s.user = s.share = s.opened = s.file = "";
  s.written = 0;
  s.writes = s.errCmd = 0;
  s.err = 0;
}

// Normal end of a session (closed by either side): decide what it was from the SMB traffic.
static void finishSession(Session& s, const String& closeNote) {
  if (s.err) { endSession(s, false, smbReason(s)); return; }
  if (s.writes) {
    String f = s.file.length() ? s.file : "(ไม่ทราบชื่อไฟล์)";
    String share = s.share.length() ? " → " + s.share : "";
    endSession(s, true, (s.removed ? "ทดสอบเขียนไฟล์ " : "ส่งไฟล์ ") + f + share + " · " + fmtSize(s.written));
    return;
  }
  if (s.encrypted) { endSession(s, true, "ส่งข้อมูลแบบเข้ารหัส SMB3 (ไม่เห็นชื่อไฟล์)"); return; }
  if (s.smb && s.authed) { endSession(s, true, "", false); return; }   // login / folder check only
  if (s.smb) { endSession(s, false, "การเชื่อมต่อจบก่อนล็อกอินสำเร็จ (" + closeNote + ")"); return; }
  endSession(s, false, s.up + s.down ? closeNote : "PC ไม่ตอบ SMB");
}

static bool printerAllowed(uint32_t ip) {
  if (!cfg.dns.restrict) return true;
  for (auto& p : printerState) if (p.ip == ip) return true;
  return false;
}

static void connectTarget(Session& s, int idx);

static int relayHostIndex(uint16_t port) {
  for (size_t i = 0; i < cfg.hosts.size(); i++)
    if (cfg.hosts[i].relayPort == port) return i;
  return -1;
}

static void startSession(int cfd, const struct sockaddr_in& from) {
  Session* s = nullptr;
  for (auto& x : sess) if (!x.used) { s = &x; break; }
  if (!s) { close(cfd); return; }
  uint16_t port = portMuxOrigPort(from.sin_addr.s_addr, ntohs(from.sin_port));
  int hi = port ? relayHostIndex(port) : -1;
  s->used = true;
  s->cfd = cfd;
  s->host = hi >= 0 ? cfg.hosts[hi].name : String("พอร์ต ") + port;
  s->port = port;
  s->clientIp = from.sin_addr.s_addr;
  s->client = clientLabel(s->clientIp);
  s->start = s->lastAct = millis();
  setNonBlock(cfd);
  setNoDelay(cfd);

  if (!printerAllowed(s->clientIp)) { endSession(*s, false, "ปฏิเสธ: ไม่อยู่ในรายการเครื่องพิมพ์"); return; }
  int idx = hostIndex(s->host);
  if (idx < 0 || !cfg.hosts[idx].enabled) { endSession(*s, false, "Host ถูกลบหรือปิดใช้งาน"); return; }
  if (!resolverFresh(idx)) {          // on-demand: ask the network where the PC is now, then connect
    s->resolving = true;
    s->roundsAt = hostState[idx].rounds;
    resolverPrioritize(idx);
    return;
  }
  connectTarget(*s, idx);
}

static void connectTarget(Session& s, int idx) {
  uint32_t ip = hostState[idx].v4;
  if (!ip) parseIp4(cfg.hosts[idx].fallback4, ip);
  if (!ip) { endSession(s, false, "ยังไม่ทราบ IP ของ Host"); return; }   // endSession starts a search
  s.target = ip;
  s.connStart = millis();

  s.sfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s.sfd < 0) { endSession(s, false, "socket เต็ม"); return; }
  setNonBlock(s.sfd);
  setNoDelay(s.sfd);
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(RELAY_TARGET_PORT);
  a.sin_addr.s_addr = ip;
  int r = connect(s.sfd, (struct sockaddr*)&a, sizeof(a));
  if (r == 0) s.connecting = false;
  else if (errno == EINPROGRESS) s.connecting = true;
  else endSession(s, false, "เชื่อมต่อไม่ได้ (errno " + String(errno) + ")");
}

// Moves data from -> to through pipe p. Returns false on a socket error.
static bool pump(Session& s, int from, int to, Pipe& p, uint64_t& counter) {
  if (p.len == 0 && !p.eof) {
    int n = recv(from, p.buf, RELAY_BUF, 0);
    if (n > 0) { p.len = n; p.off = 0; s.lastAct = millis(); smbFeed(s, &p == &s.c2s, p.buf, n); }
    else if (n == 0) p.eof = true;
    else if (errno != EAGAIN && errno != EWOULDBLOCK) return false;
  }
  if (p.off < p.len) {
    int m = send(to, p.buf + p.off, p.len - p.off, 0);
    if (m > 0) {
      p.off += m;
      counter += m;
      s.lastAct = millis();
      if (p.off >= p.len) p.len = p.off = 0;
    } else if (m < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      return false;
    }
  }
  if (p.eof && p.len == 0 && !p.shut) {     // forward the half-close once everything is sent
    shutdown(to, SHUT_WR);
    p.shut = true;
  }
  return true;
}

static void serviceSession(Session& s) {
  uint32_t now = millis();
  if (s.resolving) {                  // wait for the lookup round started by startSession()
    int idx = hostIndex(s.host);
    if (idx < 0) { endSession(s, false, "Host ถูกลบหรือปิดใช้งาน"); return; }
    if (hostState[idx].rounds == s.roundsAt && now - s.start < RESOLVE_WAIT_MS) return;
    s.resolving = false;
    connectTarget(s, idx);
    return;
  }
  if (s.connecting) {
    fd_set wf;
    FD_ZERO(&wf);
    FD_SET(s.sfd, &wf);
    struct timeval tv = {0, 0};
    if (select(s.sfd + 1, nullptr, &wf, nullptr, &tv) > 0) {
      int err = 0;
      socklen_t l = sizeof(err);
      getsockopt(s.sfd, SOL_SOCKET, SO_ERROR, &err, &l);
      if (err) { endSession(s, false, err == ECONNREFUSED ? "PC ปฏิเสธการเชื่อมต่อพอร์ต 445" : "เชื่อมต่อ PC ไม่ได้ (" + String(err) + ")"); return; }
      s.connecting = false;
      s.lastAct = now;
    } else if (now - s.connStart > CONNECT_TIMEOUT_MS) {
      endSession(s, false, "เชื่อมต่อ PC ไม่ทัน (PC ปิดอยู่หรือ IP เปลี่ยน)");
    }
    return;
  }
  // Windows answers the copier's close with a reset, so a socket error after SMB traffic is a normal end.
  if (!pump(s, s.cfd, s.sfd, s.c2s, s.up) || !pump(s, s.sfd, s.cfd, s.s2c, s.down)) { finishSession(s, "การเชื่อมต่อถูกตัด"); return; }
  if (s.c2s.shut && s.s2c.shut) { finishSession(s, "สำเร็จ"); return; }
  // pump() sets lastAct to a later millis() than `now`, so measure idle time from a fresh reading
  if (millis() - s.lastAct > IDLE_TIMEOUT_MS) finishSession(s, "ปิดเมื่อไม่มีการใช้งาน");
}

// ---- Public API ------------------------------------------------------------
static void closeListener() {
  if (listenFd >= 0) close(listenFd);
  listenFd = -1;
}

static bool openListener() {
  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) { Serial.println("[RELAY] no socket for the listener"); return false; }
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(PORTMUX_PORT);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(fd, (struct sockaddr*)&a, sizeof(a)) < 0 || listen(fd, 4) < 0) {
    Serial.printf("[RELAY] cannot listen on port %u (errno %d)\n", PORTMUX_PORT, errno);
    close(fd);
    return false;
  }
  setNonBlock(fd);
  listenFd = fd;
  return true;
}

void relayBegin() {
  std::vector<uint16_t> ports;
  for (auto& h : cfg.hosts)
    if (h.enabled && h.relayPort) ports.push_back(h.relayPort);
  portMuxSetPorts(ports);
  if (!g_netReady || ports.empty() || !portMuxBegin()) { closeListener(); return; }
  if (listenFd < 0 && !openListener()) return;
  Serial.printf("[RELAY] %u relay ports -> PC:%u\n", (unsigned)ports.size(), RELAY_TARGET_PORT);
}

void relayStop() {
  portMuxSetPorts({});
  closeListener();
  for (auto& s : sess) if (s.used) endSession(s, false, "เครือข่ายหลุด");
}

bool relayListening(uint16_t port) {
  return listenFd >= 0 && portMuxHas(port);
}

bool relayBusy() {
  for (auto& s : sess) if (s.used) return true;
  return false;
}

void relayLoop() {
  bool free = false;
  for (auto& s : sess) if (!s.used) free = true;
  if (listenFd >= 0 && free) {            // otherwise leave the connection in the backlog until a slot frees up
    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    int cfd = accept(listenFd, (struct sockaddr*)&from, &fl);
    if (cfd >= 0) startSession(cfd, from);
  }
  for (auto& s : sess) if (s.used) serviceSession(s);
}

void relayStatusJson(JsonArray out) {
  uint32_t now = millis();
  for (auto& s : sess) {
    if (!s.used) continue;
    JsonObject o = out.add<JsonObject>();
    o["client"] = s.client;
    o["host"] = s.host;
    o["target"] = ip4Str(s.target);
    o["port"] = s.port;
    o["bytes"] = s.up + s.down;
    o["file"] = s.writes ? s.file : s.opened;
    o["secs"] = (now - s.start) / 1000;
    o["state"] = s.resolving ? "resolving" : s.connecting ? "connecting" : "transfer";
  }
}
