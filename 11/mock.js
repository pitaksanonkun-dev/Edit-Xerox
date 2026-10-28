// ---------------------------------------------------------------------------
// Preview mode: simulates the ESP32 JSON API in the browser with demo data so
// the real web UI (XeroxHostDNS/WebPage.h) can be opened without the board.
// ---------------------------------------------------------------------------
(function () {
  const t0 = Date.now();
  const pad = n => String(n).padStart(2, '0');
  const fmt = d => `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
  const now = () => fmt(new Date());
  const ago = s => fmt(new Date(Date.now() - s * 1000));
  const uptime = () => {
    const s = Math.floor((Date.now() - t0) / 1000) + 3 * 86400 + 5 * 3600 + 17 * 60;
    return `${Math.floor(s / 86400)}d ${pad(Math.floor(s / 3600) % 24)}:${pad(Math.floor(s / 60) % 60)}:${pad(s % 60)}`;
  };
  const MAX_HOSTS = 100, MAX_PRINTERS = 32, MAX_RELAY = 100, MAX_SESSIONS = 3;

  const cfg = {
    net: { dhcp: false, ip: '192.168.20.10', mask: '255.255.255.0', gw: '192.168.20.1', dns: '192.168.20.5', hostname: 'xerox-dns', autoFallback: true, conflictFallback: false },
    dns: { upstream1: '192.168.20.5', upstream2: '8.8.8.8', ttl: 30, domain: 'company.local', anySuffix: true, answerAAAA: true, restrict: false },
    res: { onDemand: true, interval: 1800, timeoutMs: 800, nbns: true, llmnr: true, mdns: true, passive: true, extraBcast: '', scanSubnets: '192.168.30.0/24', dnsLookup: false },
    time: { tz: '<+07>-7', ntp: 'pool.ntp.org' },
    log: { changeDays: 90, relayDays: 30, queryDays: 3, queryPersist: false, maxKB: 512, flushMin: 5 },
    auth: { user: 'admin', pass: 'admin' }
  };

  const H = (name, o) => Object.assign({
    name, mac: '', aliases: '', fallback4: '', fallback6: '', note: '', enabled: true, relayPort: 0,
    ipv4: '', ipv6: [], method: '', seenMs: 0, fails: 0, rounds: 120, dnsHits: 0,
    relayJobs: 0, relayFails: 0, relayBytes: 0, relayLast: ''
  }, o);
  let hosts = [
    H('PC-ACC01', { scanName: 'test (Xerox ชั้น 2)', mac: 'A4:BB:6D:12:34:56', note: 'แผนกบัญชี', relayPort: 4451, ipv4: '192.168.20.86', ipv6: ['fd00:20::86', 'fe80::4a1:22ff:fe33:86'], method: 'ARP (MAC)', seenMs: Date.now() - 4000, dnsHits: 8, relayJobs: 37, relayBytes: 412e6, relayLast: ago(600) }),
    H('PC-HR01', { scanName: 'HR-Scan (Xerox ชั้น 3)', mac: '30:9C:23:AB:CD:01', note: 'ฝ่ายบุคคล (IP จาก DHCP เปลี่ยนบ่อย)', relayPort: 4452, ipv4: '192.168.20.131', method: 'LLMNR', seenMs: Date.now() - 12000, relayJobs: 15, relayFails: 1, relayBytes: 96e6, relayLast: ago(3400) }),
    H('PC-SALES03', { note: 'สาขา – อยู่คนละวง LAN (192.168.30.x)', relayPort: 4453, ipv4: '192.168.30.45', method: 'NetBIOS-Scan', seenMs: Date.now() - 21000, relayJobs: 6, relayBytes: 58e6, relayLast: ago(86000) }),
    H('NB-IT05', { scanName: 'IT-Notebook (Xerox ชั้น 2)', aliases: 'scan-it, nb-it05.company.local', note: 'Notebook IT', ipv4: '192.168.20.77', ipv6: ['fd00:20::77'], method: 'mDNS', seenMs: Date.now() - 9000, dnsHits: 23 }),
    H('PC-MGR', { note: 'ห้องผู้จัดการ (ปิดเครื่องอยู่)', ipv4: '192.168.20.140', method: 'NetBIOS', seenMs: Date.now() - 5 * 3600e3, fails: 412, dnsHits: 2 })
  ];
  hosts.forEach(h => h.lastSeen = h.seenMs ? fmt(new Date(h.seenMs)) : '');

  let printers = [
    { name: 'Xerox ชั้น 2', ip: '192.168.20.20', model: 'Xerox AltaLink C8155', location: 'ชั้น 2 หน้าห้องบัญชี', note: '', online: true, checked: true, dnsQueries: 31, lastQuery: 'nb-it05.company.local (A)', lastQueryTime: ago(900) },
    { name: 'Xerox ชั้น 3', ip: '192.168.20.21', model: 'Xerox AltaLink C8155', location: 'ชั้น 3 HR', note: '', online: true, checked: true, dnsQueries: 4, lastQuery: 'pc-acc01 (A)', lastQueryTime: ago(5000) },
    { name: 'Xerox สาขา', ip: '192.168.30.20', model: 'Xerox AltaLink C8155', location: 'สาขาบางนา', note: 'อยู่วง 192.168.30.x', online: false, checked: true, dnsQueries: 0, lastQuery: '', lastQueryTime: '' }
  ];

  const stats = { queries: 1284, local: 33, forwarded: 1247, refused: 0, failed: 4 };
  let changes = [
    { time: ago(40), host: 'PC-HR01', text: 'IPv4 192.168.20.118 → 192.168.20.131 [NetBIOS]' },
    { time: ago(7200), host: 'PC-ACC01', text: 'IPv6 + fd00:20::86 [LLMNR]' },
    { time: ago(90000), host: 'PC-SALES03', text: 'IPv4 192.168.30.61 → 192.168.30.45 [NetBIOS-Scan]' }
  ];
  let queries = [
    { time: ago(900), client: 'Xerox ชั้น 2 (192.168.20.20)', name: 'nb-it05.company.local', type: 'A', result: 'LOCAL → 192.168.20.77' },
    { time: ago(905), client: 'Xerox ชั้น 2 (192.168.20.20)', name: 'smtp.company.local', type: 'A', result: 'FORWARD → 192.168.20.5' },
    { time: ago(5000), client: 'Xerox ชั้น 3 (192.168.20.21)', name: 'pc-acc01', type: 'A', result: 'LOCAL → 192.168.20.86' }
  ];
  let relays = [
    { time: ago(600), client: 'Xerox ชั้น 2 (192.168.20.20)', host: 'PC-ACC01', target: '192.168.20.86:445', result: 'ส่งไฟล์ DOC_0012.pdf → scan · 17.5 MB', bytes: 18.4e6, secs: 9, ok: true },
    { time: ago(1500), client: 'Xerox ชั้น 2 (192.168.20.20)', host: 'PC-ACC01', target: '192.168.20.86:445', result: 'ล็อกอินไม่ผ่าน: Xerox ส่งรหัสผ่านว่าง – ใส่ Password ใหม่และติ๊ก Select to save new password (ผู้ใช้ scan)', bytes: 2.1e3, secs: 0, ok: false },
    { time: ago(3400), client: 'Xerox ชั้น 3 (192.168.20.21)', host: 'PC-HR01', target: '192.168.20.118:445', result: 'เชื่อมต่อ PC ไม่ทัน (PC ปิดอยู่หรือ IP เปลี่ยน)', bytes: 0, secs: 4, ok: false },
    { time: ago(3380), client: 'Xerox ชั้น 3 (192.168.20.21)', host: 'PC-HR01', target: '192.168.20.131:445', result: 'ส่งไฟล์ HR-2026-10.pdf → hr · 5.9 MB', bytes: 6.2e6, secs: 4, ok: true }
  ];
  let sessions = [];

  // ---- Simulation: scan jobs flowing through the relay, IP changes ---------
  function startJob(hostName) {
    const h = hosts.find(x => x.name === hostName);
    const p = printers.filter(x => x.online)[Math.floor(Math.random() * 2)] || printers[0];
    if (!h || sessions.length >= MAX_SESSIONS) return;
    sessions.push({ client: `${p.name} (${p.ip})`, host: h.name, target: h.ipv4, port: h.relayPort, bytes: 0,
      start: Date.now(), size: (3 + Math.random() * 25) * 1e6, state: 'connecting' });
  }
  setTimeout(() => startJob('PC-ACC01'), 1500);
  setInterval(() => {
    for (const s of sessions) {
      if (s.state === 'connecting') { s.state = 'transfer'; continue; }
      s.bytes = Math.min(s.size, s.bytes + 2.5e6 + Math.random() * 1.5e6);
    }
    sessions = sessions.filter(s => {
      if (s.bytes < s.size) return true;
      const h = hosts.find(x => x.name === s.host);
      const secs = Math.round((Date.now() - s.start) / 1000);
      relays.unshift({ time: now(), client: s.client, host: s.host, target: s.target + ':445', result: 'ส่งไฟล์ DOC_' + String(Math.floor(Math.random() * 9000) + 1000) + '.pdf → scan · ' + (s.bytes / 1048576).toFixed(1) + ' MB', bytes: Math.round(s.bytes), secs, ok: true });
      if (h) { h.relayJobs++; h.relayBytes += s.bytes; h.relayLast = now(); }
      return false;
    });
    hosts.forEach(h => { if (h.enabled && h.fails === 0 && Math.random() < 0.5) { h.seenMs = Date.now(); h.lastSeen = now(); } });
  }, 1000);
  setInterval(() => {
    const rh = hosts.filter(h => h.relayPort && h.enabled && h.fails === 0);
    if (rh.length && Math.random() < 0.6) startJob(rh[Math.floor(Math.random() * rh.length)].name);
  }, 15000);
  setTimeout(() => {                                     // demo: a PC gets a new DHCP address
    const h = hosts.find(x => x.name === 'PC-HR01');
    if (!h) return;
    const old = h.ipv4;
    h.ipv4 = '192.168.20.' + (150 + Math.floor(Math.random() * 50));
    h.method = 'ARP (MAC)'; h.seenMs = Date.now(); h.lastSeen = now();
    changes.unshift({ time: now(), host: h.name, text: `IPv4 ${old} → ${h.ipv4} [ARP (MAC)]` });
  }, 25000);

  // ---- Devices "found" by the ARP sweep ------------------------------------
  const neighborsAll = [
    ['192.168.20.20', '9C:93:4E:20:20:01', '', 'ARP'], ['192.168.20.21', '9C:93:4E:20:21:02', '', 'ARP'],
    ['192.168.20.33', 'B4:2E:99:0A:11:33', 'PC-STORE02', 'ARP'], ['192.168.20.45', '00:E0:4C:68:01:45', 'PC-ADMIN', 'ARP'],
    ['192.168.20.72', 'D8:5E:D3:32:07:41', 'DESKTOP-1PJRV9V', 'ARP'], ['192.168.20.77', '3C:22:FB:77:00:77', 'NB-IT05', 'ARP'],
    ['192.168.20.86', 'A4:BB:6D:12:34:56', 'PC-ACC01', 'ARP'], ['192.168.20.98', '64:00:6A:5D:98:10', 'PC-PURCHASE', 'ARP'],
    ['192.168.20.112', 'F0:2F:74:11:12:AA', '', 'ARP'], ['192.168.20.150', '18:C0:4D:9E:50:01', 'PC-QC01', 'ARP'],
    ['192.168.30.45', '70:85:C2:30:45:45', 'PC-SALES03', 'NetBIOS']
  ];
  let scanStart = 0;
  function neighbors() {
    const pct = scanStart ? Math.min(100, Math.floor((Date.now() - scanStart) / 60)) : 100;
    const shown = neighborsAll.slice(0, Math.max(3, Math.ceil(neighborsAll.length * pct / 100)));
    const hostOf = mac => (hosts.find(h => h.mac && h.mac.toUpperCase() === mac) || {}).name || '';
    const hr = hosts.find(h => h.name === 'PC-HR01');
    const list = shown.map(([ip, mac, name, src]) => ({ ip, mac, name, src, ago: 3, host: hostOf(mac) }));
    if (hr && hr.mac) list.push({ ip: hr.ipv4, mac: hr.mac, name: 'PC-HR01', src: 'ARP', ago: 2, host: 'PC-HR01' });
    if (pct >= 100) scanStart = 0;
    return { scanning: pct < 100, progress: pct, lastScan: 4, list: list.sort((a, b) => a.ip.localeCompare(b.ip, undefined, { numeric: true })) };
  }

  // ---- Daily log files on "flash" -----------------------------------------
  const FS_TOTAL = 1441792;
  const ymd = d => `${d.getFullYear()}${pad(d.getMonth() + 1)}${pad(d.getDate())}`;
  const logFiles = {};                                   // name -> TSV text
  (function seedLogs() {
    const pcs = [['PC-ACC01', '192.168.20.86'], ['PC-HR01', '192.168.20.131'], ['PC-SALES03', '192.168.30.45']];
    const prn = ['Xerox ชั้น 2 (192.168.20.20)', 'Xerox ชั้น 3 (192.168.20.21)'];
    for (let back = 0; back < 30; back++) {
      const day = new Date(Date.now() - back * 86400e3);
      if (day.getDay() === 0) continue;                  // no scans on Sundays
      let r = '';
      const jobs = 4 + Math.floor(Math.random() * 14);
      for (let j = 0; j < jobs; j++) {
        const t = new Date(day); t.setHours(8 + Math.floor(Math.random() * 10), Math.floor(Math.random() * 60), Math.floor(Math.random() * 60));
        if (t > new Date()) continue;
        const [h, ip] = pcs[Math.floor(Math.random() * pcs.length)];
        const ok = Math.random() > 0.06;
        r += [Math.floor(t / 1000), prn[j % 2], h, ip + ':445', ok ? 'สำเร็จ' : 'เชื่อมต่อ PC ไม่ทัน (PC ปิดอยู่หรือ IP เปลี่ยน)',
          ok ? Math.floor(1e6 + Math.random() * 25e6) : 0, ok ? 2 + Math.floor(Math.random() * 12) : 4, ok ? 1 : 0].join('\t') + '\n';
      }
      if (r) logFiles['r' + ymd(day) + '.tsv'] = r;
      if (back % 4 === 0) {
        const t = new Date(day); t.setHours(8, 5, 0);
        logFiles['c' + ymd(day) + '.tsv'] = [Math.floor(t / 1000), 'PC-HR01', `IPv4 192.168.20.${100 + back} → 192.168.20.${131 + back} [ARP (MAC)]`].join('\t') + '\n';
      }
    }
  })();
  const utf8Len = s => new Blob([s]).size;
  function logInfo() {
    const files = Object.keys(logFiles).map(n => ({ name: n, type: n[0], day: `${n.slice(1, 5)}-${n.slice(5, 7)}-${n.slice(7, 9)}`, size: utf8Len(logFiles[n]) }))
      .sort((a, b) => a.day !== b.day ? (a.day < b.day ? 1 : -1) : (a.name < b.name ? -1 : 1));
    const logBytes = files.reduce((s, f) => s + f.size, 0);
    return { files, fsTotal: FS_TOTAL, fsUsed: 24576 + logBytes, logBytes, quota: Math.min(cfg.log.maxKB * 1024, Math.floor(FS_TOTAL * 0.6)),
      pending: sessions.length + 2, nextFlush: 180, timeValid: true };
  }

  // ---- Verify simulation ----------------------------------------------------
  let vj = null;
  function verifyStatus() {
    if (!vj) return { running: false, started: false, steps: [], result: 'ok' };
    const t = Date.now() - vj.t0, steps = [];
    const add = (key, label, at, state, detail) => steps.push(t >= at ? { key, label, state, detail } : { key, label, state: t >= at - 900 ? 'run' : 'wait', detail: t >= at - 900 ? 'กำลังตรวจสอบ…' : '' });
    const mac = normMac(vj.mac);
    const nb = mac && neighborsAll.find(n => n[1] === mac);
    const hostByMac = mac && hosts.find(h => normMac(h.mac) === mac);
    const macIp = nb ? nb[0] : hostByMac ? hostByMac.ipv4 : '';
    const nameHit = neighborsAll.find(n => n[2] && n[2].toLowerCase() === vj.name.toLowerCase()) ||
      hosts.filter(h => h.name.toLowerCase() === vj.name.toLowerCase() && h.fails === 0).map(h => [h.ipv4, h.mac, h.name])[0];
    const nameIp = nameHit ? nameHit[0] : '';
    const target = macIp || nameIp || vj.fallback4;
    const offline = hosts.some(h => h.name.toLowerCase() === vj.name.toLowerCase() && h.fails > 0);
    add('net', 'เครือข่ายของ ESP32', 300, 'ok', `IP ${cfg.net.ip} · Gateway ${cfg.net.gw} · Link 100 Mbps`);
    add('mac', 'ค้นหาเครื่องจาก MAC Address (ARP)', 1500, !mac ? 'skip' : macIp ? 'ok' : 'fail',
      !mac ? 'ไม่ได้ระบุ MAC – แนะนำให้ใส่เพื่อความแม่นยำสูงสุด' : macIp ? `พบที่ ${macIp} – ยืนยันด้วย ARP แล้ว` : 'ไม่พบ MAC นี้ในวง LAN – เครื่องปิดอยู่ / อยู่ต่างวง LAN / MAC ไม่ถูกต้อง');
    add('name', 'ค้นหาเครื่องจากชื่อ (NetBIOS / LLMNR / mDNS)', 3000, nameIp ? 'ok' : macIp ? 'warn' : 'fail',
      nameIp ? `ตอบกลับจาก ${nameIp} (NetBIOS)` : macIp ? 'ไม่ตอบชื่อ (อาจปิด NetBIOS/LLMNR หรือ Firewall) – ไม่เป็นไร ระบบใช้ MAC แทน' : 'ไม่ตอบ – ตรวจชื่อเครื่อง / เปิด NetBIOS หรือใส่ MAC Address');
    add('target', 'IP ของเครื่องคอมพิวเตอร์ที่จะใช้', 3300, target ? (macIp || nameIp ? 'ok' : 'warn') : 'fail',
      target ? target + (macIp ? ' (จาก MAC)' : nameIp ? ' (จากชื่อเครื่อง)' : ' – ใช้ IPv4 สำรอง เพราะค้นหาเครื่องไม่พบ') : 'ไม่ทราบ IP ของเครื่อง');
    add('smb', 'ESP32 → คอมพิวเตอร์ : SMB (TCP 445) สำหรับรับไฟล์ Scan', 4300, !target ? 'skip' : offline || !(macIp || nameIp) ? 'fail' : 'ok',
      !target ? 'ข้าม – ไม่ทราบ IP' : offline || !(macIp || nameIp) ? 'ไม่ตอบภายใน 3 วินาที – เครื่องปิดอยู่ หรือ Firewall / Router บล็อก' : `เชื่อมต่อได้ (${3 + Math.floor(Math.random() * 6)} ms) – เครื่องพร้อมรับไฟล์ Scan`);
    printers.forEach((p, i) => add('p' + i, `ESP32 → เครื่องถ่ายเอกสาร ${p.name} (${p.ip})`, 4900 + i * 600, p.online ? 'ok' : 'fail',
      p.online ? `ติดต่อได้ (${2 + i} ms)` + (p.ip.startsWith('192.168.20.') ? '' : ' · ต่างวง LAN (ผ่าน Router)') + (p.dnsQueries ? ` · เคยถาม DNS มาที่ ESP32 แล้ว ${p.dnsQueries} ครั้ง` : '')
               : 'ไม่ตอบ – ตรวจว่าเครื่องเปิดอยู่ / IP ถูกต้อง / Router อนุญาต'));
    const port = Number(vj.relayPort) || 0, clash = port && hosts.find(h => h.relayPort === port && h.name.toLowerCase() !== vj.name.toLowerCase());
    const listening = port && hosts.some(h => h.relayPort === port && h.name.toLowerCase() === vj.name.toLowerCase());
    add('relay', 'เครื่องถ่ายเอกสาร → ESP32 : พอร์ต Relay', 5200 + printers.length * 600, !port ? 'skip' : clash ? 'fail' : listening ? 'ok' : 'warn',
      !port ? 'ไม่ได้ใช้ Relay (ใช้โหมด DNS – Address Type = Host Name)' : clash ? `พอร์ต ${port} ถูกใช้โดย Host ${clash.name} แล้ว` :
      listening ? `เปิดรับอยู่ – ใน Xerox ใส่ IPv4 ${cfg.net.ip} Port ${port}` : `ยังไม่เปิด – จะเริ่มรับหลังกด บันทึก (Xerox ใส่ ${cfg.net.ip} Port ${port})`);
    const running = steps.some(s => s.state === 'run' || s.state === 'wait');
    const result = running ? 'running' : steps.some(s => s.state === 'fail') ? 'fail' : steps.some(s => s.state === 'warn') ? 'warn' : 'ok';
    return { running, started: true, steps, result };
  }

  // ---- Helpers mirroring the firmware -------------------------------------
  const normMac = s => { const h = String(s || '').replace(/[^0-9a-fA-F]/g, ''); return h.length === 12 ? h.toUpperCase().match(/../g).join(':') : ''; };
  const isIp4 = s => /^(25[0-5]|2[0-4]\d|1?\d?\d)(\.(25[0-5]|2[0-4]\d|1?\d?\d)){3}$/.test(String(s).trim());
  function validateHosts(list) {
    if (list.length > MAX_HOSTS) return 'จำนวน Host เกิน ' + MAX_HOSTS;
    let relaysN = 0;
    for (let i = 0; i < list.length; i++) {
      const h = list[i];
      if (h.relayPort) {
        const p = h.relayPort;
        if ([53, 80, 137, 5353, 5355, 40445].includes(p) || p > 65535 || p < 1) return `พอร์ต Relay ${p} ใช้ไม่ได้ (ถูกใช้โดยระบบ)`;
        if (list.slice(0, i).some(x => x.relayPort === p)) return 'พอร์ต Relay ซ้ำ: ' + p;
        if (++relaysN > MAX_RELAY) return `เปิด Relay ได้สูงสุด ${MAX_RELAY} Host`;
      }
      if (!/^[A-Za-z0-9_-][A-Za-z0-9._-]{0,62}$/.test(h.name) || h.name.endsWith('.')) return `ชื่อ Host ไม่ถูกต้อง: '${h.name}' (ใช้ได้เฉพาะ A-Z 0-9 - _ .)`;
      if (list.slice(0, i).some(x => x.name.toLowerCase() === h.name.toLowerCase())) return 'ชื่อ Host ซ้ำ: ' + h.name;
      if (h.mac) {
        const m = normMac(h.mac);
        if (!m || m === '00:00:00:00:00:00' || (parseInt(m.slice(0, 2), 16) & 1)) return 'MAC Address ไม่ถูกต้อง: ' + h.mac;
        if (list.slice(0, i).some(x => normMac(x.mac) === m)) return 'MAC Address ซ้ำ: ' + h.mac;
        h.mac = m;
      }
      if (h.fallback4 && !isIp4(h.fallback4)) return 'IPv4 สำรองไม่ถูกต้อง: ' + h.fallback4;
    }
    return '';
  }
  function validatePrinters(list) {
    if (list.length > MAX_PRINTERS) return 'จำนวนเครื่องพิมพ์เกิน ' + MAX_PRINTERS;
    for (let i = 0; i < list.length; i++) {
      if (!list[i].name) return 'กรุณาระบุชื่อเครื่องพิมพ์';
      if (!isIp4(list[i].ip)) return 'IP ของเครื่องพิมพ์ไม่ถูกต้อง: ' + list[i].ip;
      if (list.slice(0, i).some(x => x.ip === list[i].ip)) return 'IP เครื่องพิมพ์ซ้ำ: ' + list[i].ip;
    }
    return '';
  }
  function matchHost(q) {
    q = q.toLowerCase();
    const [first, ...r] = q.split('.'), rest = r.join('.');
    return hosts.findIndex(h => h.enabled && [h.name, ...h.aliases.split(',')].map(s => s.trim().toLowerCase()).filter(Boolean)
      .some(n => q === n || (!n.includes('.') && first === n &&
        (!rest || rest === 'local' || rest === cfg.dns.domain.toLowerCase() || cfg.dns.anySuffix))));
  }
  function setHosts(list) {
    hosts = list.map(n => {
      const old = hosts.find(h => h.name.toLowerCase() === n.name.toLowerCase());
      const base = old || H(n.name, { rounds: 0, method: '', seenMs: 0 });
      if (!old) setTimeout(() => {                         // "discover" the new host shortly after
        base.rounds = 1; base.ipv4 = '192.168.20.' + (20 + Math.floor(Math.random() * 200));
        base.method = 'NetBIOS'; base.seenMs = Date.now(); base.lastSeen = now();
        changes.unshift({ time: now(), host: base.name, text: `IPv4 (ไม่ทราบ) → ${base.ipv4} [NetBIOS]` });
      }, 3000);
      return Object.assign(base, n);
    });
  }
  function status() {
    return {
      device: { fw: '1.6.0', hostname: cfg.net.hostname, mac: 'A8:03:2A:6F:12:34', link: true, speed: 100, duplex: 'Full',
        ip: cfg.net.ip, mask: cfg.net.mask, gw: cfg.net.gw, dns: cfg.net.dns, dhcp: cfg.net.dhcp, uptime: uptime(),
        time: now(), heap: 182344 - sessions.length * 9000, interval: cfg.res.interval, onDemand: cfg.res.onDemand, maxRelay: MAX_RELAY, user: cfg.auth.user, netFallback: false, netReason: '', ipConflict: '' },
      stats: Object.assign({}, stats),
      hosts: hosts.map(h => ({
        name: h.name, scanName: h.scanName || '', mac: h.mac, aliases: h.aliases, fallback4: h.fallback4, fallback6: h.fallback6, note: h.note, enabled: h.enabled,
        ipv4: h.ipv4, ipv6: h.ipv6, method: h.method, lastSeen: h.lastSeen, ago: h.seenMs ? Math.floor((Date.now() - h.seenMs) / 1000) : -1,
        fails: h.fails, rounds: h.rounds, dnsHits: h.dnsHits, relayPort: h.relayPort, relayJobs: h.relayJobs,
        relayFails: h.relayFails, relayBytes: Math.round(h.relayBytes), relayLast: h.relayLast,
        status: !h.enabled ? 'disabled' : h.fails === 0 && (h.rounds || h.lastSeen) ? 'online' : h.rounds === 0 ? 'pending' : 'offline'
      })),
      printers: printers.map(p => Object.assign({}, p)),
      sessions: sessions.map(s => ({ client: s.client, host: s.host, target: s.target, port: s.port, bytes: Math.round(s.bytes),
        secs: Math.round((Date.now() - s.start) / 1000), state: s.state })),
      relays: relays.slice(0, 40), changes: changes.slice(0, 60), queries: queries.slice(0, 60)
    };
  }

  // ---- fetch() replacement -------------------------------------------------
  const reply = (obj, code) => new Response(JSON.stringify(obj), { status: code || 200, headers: { 'Content-Type': 'application/json' } });
  const ok = reboot => reply({ ok: true, reboot: !!reboot });
  const err = m => reply({ ok: false, error: m }, 400);
  const clone = o => JSON.parse(JSON.stringify(o));

  window.fetch = async function (url, opt) {
    await new Promise(r => setTimeout(r, 120));
    const u = new URL(url, 'http://esp32.local/');
    const method = (opt && opt.method) || 'GET';
    const body = opt && opt.body ? JSON.parse(opt.body) : null;
    switch (method + ' ' + u.pathname) {
      case 'GET /api/status': return reply(status());
      case 'POST /api/login':
        if (body.user !== cfg.auth.user || body.pass !== cfg.auth.pass) return reply({ ok: false, error: 'ชื่อผู้ใช้หรือรหัสผ่านไม่ถูกต้อง' }, 401);
        loggedOut(false); return ok();
      case 'POST /api/logout': loggedOut(true); return ok();
      case 'POST /api/password': {
        if (body.old !== cfg.auth.pass) return err('รหัสผ่านปัจจุบันไม่ถูกต้อง');
        if (!String(body.user || '').trim()) return err('ชื่อผู้ใช้ต้องมี 1–32 ตัวอักษร');
        if (String(body.pass || '').length < 6) return err('รหัสผ่านใหม่ต้องมี 6–64 ตัวอักษร');
        cfg.auth.user = body.user.trim(); cfg.auth.pass = body.pass; return ok();
      }
      case 'GET /api/config': { const c = clone(cfg); delete c.auth.pass; return reply(c); }
      case 'GET /api/neighbors': return reply(neighbors());
      case 'POST /api/verify':
        if (!String(body.name || '').trim() && !String(body.mac || '').trim()) return err('กรุณาระบุชื่อเครื่องหรือ MAC Address ก่อน Verify');
        vj = { t0: Date.now(), name: String(body.name || '').trim(), aliases: body.aliases || '', mac: body.mac || '', fallback4: body.fallback4 || '', relayPort: body.relayPort || 0 };
        return ok();
      case 'GET /api/verify': return reply(verifyStatus());
      case 'POST /api/scan': if (!scanStart) scanStart = Date.now(); return ok();
      case 'GET /api/test': {
        const i = matchHost(u.searchParams.get('name') || '');
        return reply(i < 0 ? { match: '' } : { match: hosts[i].name, ipv4: hosts[i].ipv4, status: hosts[i].fails ? 'offline' : 'online' });
      }
      case 'POST /api/hosts': {
        const list = body.map(h => ({ name: String(h.name || '').trim(), scanName: String(h.scanName || '').trim(), mac: String(h.mac || '').trim(), aliases: h.aliases || '', fallback4: h.fallback4 || '',
          fallback6: h.fallback6 || '', note: h.note || '', enabled: h.enabled !== false, relayPort: Number(h.relayPort) || 0 }));
        const e = validateHosts(list);
        if (e) return err(e);
        setHosts(list);
        return ok();
      }
      case 'POST /api/printers': {
        const e = validatePrinters(body);
        if (e) return err(e);
        printers = body.map(n => Object.assign(printers.find(p => p.ip === n.ip) ||
          { online: true, checked: true, dnsQueries: 0, lastQuery: '', lastQueryTime: '' }, n));
        return ok();
      }
      case 'POST /api/settings': {
        if (body.net && !body.net.dhcp && !(isIp4(body.net.ip) && isIp4(body.net.mask) && isIp4(body.net.gw)))
          return err('Static IP ต้องระบุ IP, Subnet mask และ Gateway ให้ถูกต้อง');
        for (const s of String((body.res && body.res.scanSubnets) || '').split(',').map(x => x.trim()).filter(Boolean)) {
          const [ip, pre] = s.split('/');
          if (!isIp4(ip)) return err('Subnet ไม่ถูกต้อง: ' + s);
          const p = pre === undefined ? 24 : Number(pre);
          if (p < 22 || p > 30) return err('Subnet ต้องเป็น /22 ถึง /30: ' + s);
        }
        const oldNet = JSON.stringify(cfg.net) + cfg.time.tz + cfg.time.ntp;
        delete body.auth;
        if (body.log) {
          const c = (v, lo, hi) => Math.min(hi, Math.max(lo, Number(v) || lo));
          body.log = { changeDays: c(body.log.changeDays, 1, 365), relayDays: c(body.log.relayDays, 1, 365), queryDays: c(body.log.queryDays, 1, 30),
            maxKB: c(body.log.maxKB, 64, 800), flushMin: c(body.log.flushMin, 1, 60), queryPersist: !!body.log.queryPersist };
        }
        for (const g of ['net', 'dns', 'res', 'time', 'log']) Object.assign(cfg[g], body[g]);
        return ok(oldNet !== JSON.stringify(cfg.net) + cfg.time.tz + cfg.time.ntp);
      }
      case 'POST /api/resolve': {
        const i = u.searchParams.get('i');
        (i === null ? hosts : [hosts[+i]]).forEach(h => { if (h && h.fails === 0) { h.seenMs = Date.now(); h.lastSeen = now(); } });
        return ok();
      }
      case 'POST /api/clearlogs':
        changes = []; queries = []; relays = [];
        if (u.searchParams.get('flash') === '1') Object.keys(logFiles).forEach(k => delete logFiles[k]);
        return ok();
      case 'GET /api/logs/info': return reply(logInfo());
      case 'POST /api/logs/flush': return ok();
      case 'GET /api/logs/file': {
        const n = u.searchParams.get('name');
        if (!(n in logFiles)) return reply({ ok: false, error: 'ไม่พบไฟล์' }, 404);
        return new Response(logFiles[n], { status: 200, headers: { 'Content-Type': 'text/tab-separated-values; charset=utf-8' } });
      }
      case 'POST /api/reboot': return ok(true);
      case 'POST /api/factory': hosts = []; printers = []; changes = []; queries = []; relays = []; sessions = []; return ok(true);
      case 'POST /api/restore': {
        if (Array.isArray(body.printers) && !validatePrinters(body.printers)) printers = body.printers.map(p => Object.assign({ online: true, checked: true, dnsQueries: 0, lastQuery: '', lastQueryTime: '' }, p));
        if (Array.isArray(body.hosts) && !validateHosts(body.hosts)) setHosts(body.hosts);
        for (const g of ['net', 'dns', 'res', 'time']) if (body[g]) Object.assign(cfg[g], body[g]);
        return ok(true);
      }
    }
    return reply({ ok: false, error: 'Not found' }, 404);
  };

  // "Download log file" button: build the file in the browser
  window.addEventListener('load', () => {
    window.dlHistory = function () {
      const n = document.querySelector('#hDay').value;
      if (!n || !(n in logFiles)) return;
      const link = document.createElement('a');
      link.href = URL.createObjectURL(new Blob([logFiles[n]], { type: 'text/tab-separated-values' }));
      link.download = n;
      link.click();
    };
  });

  // Logout: like the board, the page is replaced by the login page (LOGIN_PAGE, from WebPage.h)
  // until the user logs in again. The state survives the reload through sessionStorage.
  function loggedOut(v) { try { v ? sessionStorage.setItem('xhd-out', '1') : sessionStorage.removeItem('xhd-out'); } catch (e) {} }
  let out = false;
  try { out = sessionStorage.getItem('xhd-out') === '1'; } catch (e) {}
  if (out && typeof LOGIN_PAGE === 'string') {
    document.addEventListener('DOMContentLoaded', () => {
      const f = document.createElement('iframe');
      f.style.cssText = 'position:fixed;inset:0;width:100%;height:100%;border:0;z-index:99999;background:#f4f6f9';
      f.srcdoc = LOGIN_PAGE.replace('<script>', '<script>window.fetch=(u,o)=>parent.fetch(u,o);')
        .replace('location.reload()', 'parent.location.reload()');
      document.body.appendChild(f);
    });
  }

  // "Download Backup" link: build the file in the browser
  document.addEventListener('click', ev => {
    const a = ev.target.closest && ev.target.closest('a[href="/api/backup"]');
    if (!a) return;
    ev.preventDefault();
    const data = clone(cfg);
    data.printers = printers.map(p => ({ name: p.name, ip: p.ip, model: p.model, location: p.location, note: p.note }));
    data.hosts = hosts.map(h => ({ name: h.name, mac: h.mac, aliases: h.aliases, fallback4: h.fallback4, fallback6: h.fallback6, note: h.note, enabled: h.enabled, relayPort: h.relayPort }));
    const link = document.createElement('a');
    link.href = URL.createObjectURL(new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' }));
    link.download = 'xerox-host-dns-backup.json';
    link.click();
  }, true);
})();
