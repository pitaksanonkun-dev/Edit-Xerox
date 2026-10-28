#pragma once
#include <Arduino.h>

// Login page, served on "/" until the browser has a session cookie.
static const char LOGIN_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="th"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Xerox Host DNS – เข้าสู่ระบบ</title>
<style>
*{box-sizing:border-box}body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px;
 font-family:system-ui,"Segoe UI",Tahoma,sans-serif;background:#f4f6f9;color:#1d2433;font-size:14px}
.box{background:#fff;border:1px solid #e2e6ee;border-radius:10px;width:min(360px,100%);overflow:hidden;box-shadow:0 6px 24px rgba(0,0,0,.08)}
.top{background:#14213d;color:#fff;padding:16px 20px}.top h1{font-size:17px;margin:0;font-weight:600}.top div{font-size:12px;opacity:.8;margin-top:3px}
form{padding:18px 20px 20px}label{display:block;font-size:12px;color:#6b7385;margin:10px 0 4px}
input{width:100%;padding:9px 10px;border:1px solid #e2e6ee;border-radius:6px;font-size:14px}
button{width:100%;margin-top:16px;padding:10px;border:0;border-radius:6px;background:#0b62d6;color:#fff;font-size:14px;font-weight:600;cursor:pointer}
button:disabled{opacity:.6}#err{color:#c62828;font-size:13px;margin-top:10px;min-height:18px}
</style></head><body>
<div class="box"><div class="top"><h1>Xerox Host DNS Bridge</h1><div>เข้าสู่ระบบผู้ดูแล (Admin)</div></div>
<form id="f"><label>ชื่อผู้ใช้</label><input id="u" autocomplete="username" required autofocus>
<label>รหัสผ่าน</label><input id="p" type="password" autocomplete="current-password" required>
<button id="b">เข้าสู่ระบบ</button><div id="err"></div></form></div>
<script>
const g=i=>document.getElementById(i);
g('f').onsubmit=async e=>{e.preventDefault();g('b').disabled=true;g('err').textContent='';
 try{const r=await fetch('/api/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({user:g('u').value,pass:g('p').value})});
  const j=await r.json().catch(()=>({}));if(r.ok&&j.ok)location.reload();else{g('err').textContent=j.error||('HTTP '+r.status);g('p').select()}}
 catch(x){g('err').textContent='เชื่อมต่อไม่ได้: '+x.message}g('b').disabled=false}
</script></body></html>)rawliteral";

// Single-page web UI (Thai). Talks to the JSON API in WebUI.cpp.
static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="th"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Xerox Host DNS</title>
<style>
:root{--bg:#f4f6f9;--card:#fff;--fg:#1d2433;--mut:#6b7385;--line:#e2e6ee;--pri:#0b62d6;--ok:#14883e;--bad:#c62828;--warn:#b26a00}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,"Segoe UI",Tahoma,sans-serif;background:var(--bg);color:var(--fg);font-size:14px}
header{background:#14213d;color:#fff;padding:12px 16px;display:flex;flex-wrap:wrap;gap:8px 16px;align-items:center}
header h1{font-size:17px;margin:0;font-weight:600}header .meta{font-size:12px;opacity:.8}
nav{display:flex;gap:4px;padding:8px 16px 0;flex-wrap:wrap;border-bottom:1px solid var(--line);background:#fff}
nav button{border:0;background:none;padding:10px 14px;cursor:pointer;font-size:14px;color:var(--mut);border-bottom:2px solid transparent}
nav button.on{color:var(--pri);border-color:var(--pri);font-weight:600}
main{padding:16px;max-width:1280px;margin:auto}.tab{display:none}.tab.on{display:block}
.card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:14px 16px;margin-bottom:14px}
.card h2{font-size:15px;margin:0 0 10px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:12px}
.kpi .v{font-size:24px;font-weight:600}.kpi .l{color:var(--mut);font-size:12px}
.tw{overflow-x:auto}table{border-collapse:collapse;width:100%;min-width:640px}th,td{text-align:left;padding:7px 8px;border-bottom:1px solid var(--line);vertical-align:top}
th{font-size:12px;color:var(--mut);font-weight:600;background:#fafbfc}td.mono,.mono{font-family:Consolas,monospace;font-size:13px}
.b{display:inline-block;padding:2px 8px;border-radius:10px;font-size:12px;font-weight:600}
.b.ok{background:#e3f4e8;color:var(--ok)}.b.bad{background:#fde8e8;color:var(--bad)}.b.warn{background:#fff3df;color:var(--warn)}.b.mut{background:#eef0f4;color:var(--mut)}
.btn{border:1px solid var(--line);background:#fff;border-radius:6px;padding:6px 12px;cursor:pointer;font-size:13px}
.btn.p{background:var(--pri);color:#fff;border-color:var(--pri)}.btn.d{color:var(--bad)}.btn.s{padding:3px 8px;font-size:12px}
.bar{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:10px;align-items:center}
.note{background:#eef5ff;border:1px solid #cfe0fb;border-radius:8px;padding:10px 14px;margin-bottom:14px;line-height:1.6}
.note b.ip{font-family:Consolas,monospace;font-size:15px}
label{display:block;font-size:12px;color:var(--mut);margin:8px 0 3px}input[type=text],input[type=number],input[type=password]{width:100%;padding:7px 9px;border:1px solid var(--line);border-radius:6px;font-size:14px}
.chk{display:flex;gap:6px;align-items:center;color:var(--fg);font-size:13px;margin:8px 0}.chk input{margin:0}
.f2{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:0 14px}
.hint{font-size:12px;color:var(--mut);margin-top:3px}
dialog{border:0;border-radius:10px;padding:0;width:min(720px,94vw);box-shadow:0 10px 40px rgba(0,0,0,.25)}
dialog form{padding:16px 18px;max-height:90vh;overflow:auto}
.vl{margin-top:10px;border:1px solid var(--line);border-radius:8px;overflow:hidden}.vh{padding:9px 12px;font-weight:600;font-size:13px}
.vh.ok{background:#e3f4e8;color:var(--ok)}.vh.warn{background:#fff3df;color:var(--warn)}.vh.fail{background:#fde8e8;color:var(--bad)}.vh.running{background:#eef5ff;color:var(--pri)}
.vr{display:flex;gap:10px;padding:8px 12px;border-top:1px solid var(--line);font-size:13px}.vr .ic{width:20px;height:20px;border-radius:50%;flex:none;display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:700;color:#fff}
.ic.ok{background:var(--ok)}.ic.warn{background:var(--warn)}.ic.fail{background:var(--bad)}.ic.wait,.ic.skip{background:#cbd5e1}.ic.run{border:2px solid #cfe0fb;border-top-color:var(--pri);background:none;animation:sp 0.8s linear infinite}
@keyframes sp{to{transform:rotate(360deg)}}
.vr .d{color:var(--mut);font-size:12px;margin-top:2px}dialog h3{margin:0 0 6px;font-size:16px}.dbtn{display:flex;justify-content:flex-end;gap:8px;margin-top:14px}
#toast{position:fixed;right:16px;bottom:16px;background:#14213d;color:#fff;padding:10px 14px;border-radius:8px;display:none;max-width:90vw}
.sm{font-size:12px;color:var(--mut)}.pick{margin-top:10px;border:1px solid var(--line);border-radius:8px;padding:8px 10px;background:#fafbfc}
.acct{margin-left:auto;position:relative}.acct>button{background:rgba(255,255,255,.12);color:#fff;border:1px solid rgba(255,255,255,.25);border-radius:6px;padding:5px 10px;cursor:pointer;font-size:13px}
.menu{position:absolute;right:0;top:calc(100% + 6px);background:#fff;border:1px solid var(--line);border-radius:8px;box-shadow:0 8px 24px rgba(0,0,0,.15);min-width:190px;z-index:5;display:none;overflow:hidden}
.menu.on{display:block}.menu button{display:block;width:100%;text-align:left;border:0;background:none;padding:10px 14px;font-size:13px;cursor:pointer;color:var(--fg)}.menu button:hover{background:#f1f4f9}
.menu .who{padding:9px 14px;font-size:12px;color:var(--mut);border-bottom:1px solid var(--line)}
.imp td{padding:4px 5px}.imp input[type=text]{padding:4px 6px;font-size:13px;min-width:90px}.imp tr.off td{opacity:.45}
.pick table{min-width:0}.pick .tw{max-height:260px;overflow:auto}.pick td,.pick th{padding:5px 6px}ol{margin:6px 0 0 18px;padding:0}li{margin:3px 0}
</style></head><body>
<header><h1>Xerox Host DNS Bridge</h1><span class="meta" id="hdr">กำลังโหลด…</span>
<div class="acct"><button type="button" onclick="$('#menu').classList.toggle('on')">👤 <span id="acctName">admin</span> ▾</button>
<div class="menu" id="menu"><div class="who">เข้าสู่ระบบเป็น <b id="acctWho">admin</b></div>
<button type="button" onclick="changePass()">🔑 เปลี่ยนรหัสผ่าน</button><button type="button" onclick="logout()">⎋ ออกจากระบบ</button></div></div></header>
<nav id="nav">
<button data-t="dash" class="on">ภาพรวม</button><button data-t="hosts">เครื่องปลายทาง Scan</button>
<button data-t="printers">เครื่องพิมพ์ Xerox</button><button data-t="logs">บันทึก (Log)</button><button data-t="set">ตั้งค่า</button>
</nav>
<main>
<div class="note" id="dnsNote"></div>

<section class="tab on" id="t-dash">
 <div class="grid" id="kpis"></div><br>
 <div class="card"><h2>กำลังส่งไฟล์ Scan ผ่าน Relay</h2><div class="tw"><table><thead><tr><th>จากเครื่องพิมพ์</th><th>ไปยัง Host</th><th>IP ปลายทาง</th><th>สถานะ</th><th>ข้อมูล</th><th>เวลา</th></tr></thead><tbody id="sesRows"></tbody></table></div></div>
 <div class="card"><h2>อุปกรณ์ ESP32-ETH01</h2><div class="tw"><table id="devTbl"></table></div></div>
 <div class="card"><h2>ทดสอบชื่อ</h2>
  <div class="bar"><input type="text" id="testName" placeholder="เช่น PC-ACC01 หรือ PC-ACC01.company.local" style="max-width:380px"><button class="btn" onclick="testName()">ทดสอบ</button></div>
  <div id="testOut" class="sm"></div></div>
</section>

<section class="tab" id="t-hosts">
 <div class="card"><h2>รายการ Scan ปลายทาง (อ้างอิงชื่อ Scan ในเครื่อง Xerox)</h2>
 <div class="bar"><button class="btn p" onclick="editHost(-1)">+ เพิ่ม Host</button>
 <label class="btn" style="margin:0;color:inherit">📄 เพิ่ม Host จากไฟล์ CSV<input type="file" accept=".csv,text/csv" style="display:none" onchange="importCsv(this)"></label>
 <button class="btn" onclick="csvTemplate()" title="ไฟล์ตัวอย่างรูปแบบเดียวกับ Address Book ที่ Export จาก Xerox">⬇ Template CSV</button>
 <button class="btn" onclick="resolveAll()">ค้นหา IP ใหม่ทั้งหมด</button>
 <span class="sm" id="intv"></span></div>
 <div class="tw"><table><thead><tr><th>ชื่อ Scan (ใน Xerox)</th><th>สถานะ</th><th>IPv4</th><th>IPv6</th><th>พบโดย</th><th>พบล่าสุด</th><th>Relay (กรอกใน Xerox)</th><th>DNS</th><th></th></tr></thead><tbody id="hostRows"></tbody></table></div></div>
</section>

<section class="tab" id="t-printers">
 <div class="card"><h2>เครื่องพิมพ์ Xerox ที่ใช้งานผ่าน ESP32 นี้ (DNS / Relay)</h2>
 <div class="bar"><button class="btn p" onclick="editPrinter(-1)">+ เพิ่มเครื่องพิมพ์</button></div>
 <div class="tw"><table><thead><tr><th>ชื่อ</th><th>IP</th><th>รุ่น / ที่ตั้ง</th><th>สถานะ</th><th>DNS Query</th><th>Query ล่าสุด</th><th></th></tr></thead><tbody id="prnRows"></tbody></table></div></div>
 <div class="card"><h2>วิธีตั้งค่า Xerox AltaLink C8155 – โหมด Relay (แนะนำ)</h2><ol id="guideRelay"></ol></div>
 <div class="card"><h2>วิธีตั้งค่า Xerox AltaLink C8155 – โหมด DNS (Address Type = Host Name)</h2><ol id="guide"></ol></div>
</section>

<section class="tab" id="t-logs">
 <div class="card"><h2>การจัดเก็บ Log ในหน่วยความจำ Flash</h2>
  <div id="logUse" class="sm" style="margin-bottom:6px"></div>
  <div style="background:#eef0f4;border-radius:6px;height:12px;overflow:hidden"><div id="logBar" style="height:100%;width:0;background:var(--pri)"></div></div>
  <div class="hint" style="margin-top:6px">บันทึกเป็นไฟล์รายวัน เขียนรวดเดียวตามรอบเพื่อถนอม Flash · ลบไฟล์ที่เกินอายุอัตโนมัติทุกชั่วโมง ·
   ถ้าเกินโควตาจะลบวันที่เก่าที่สุดก่อน (DNS Query ถูกลบก่อนเสมอ)</div>
  <div class="f2" style="margin-top:6px">
   <div><label>เก็บประวัติเปลี่ยน IP (วัน)</label><input type="number" id="l_changeDays" min="1" max="365"></div>
   <div><label>เก็บประวัติส่งไฟล์ Relay (วัน)</label><input type="number" id="l_relayDays" min="1" max="365"></div>
   <div><label>เก็บ DNS Query (วัน)</label><input type="number" id="l_queryDays" min="1" max="30"></div>
   <div><label>โควตาพื้นที่ Log (KB)</label><input type="number" id="l_maxKB" min="64" max="800"><div class="hint" id="l_maxHint"></div></div>
   <div><label>เขียนลง Flash ทุก ๆ (นาที)</label><input type="number" id="l_flushMin" min="1" max="60"><div class="hint">ค่าน้อย = ข้อมูลหายน้อยเมื่อไฟดับ แต่เขียน Flash บ่อยขึ้น</div></div></div>
  <div class="chk"><input type="checkbox" id="l_queryPersist"><span>บันทึก DNS Query ลง Flash (เฉพาะคำตอบชื่อ Host ของระบบ ไม่รวมที่ส่งต่อ Upstream)</span></div>
  <div class="bar" style="margin-top:8px"><button class="btn p" onclick="saveLogCfg()">บันทึกการตั้งค่า Log</button>
   <button class="btn" onclick="setLogDefaults()">ใช้ค่าแนะนำ</button><button class="btn" onclick="flushLogs()">เขียนลง Flash ตอนนี้</button></div></div>
 <div class="card"><h2>ดู Log ย้อนหลัง</h2>
  <div class="bar"><select id="hType" onchange="fillDays()" class="btn"><option value="r">ประวัติส่งไฟล์ Relay</option><option value="c">ประวัติเปลี่ยน IP</option><option value="q">DNS Query</option></select>
   <select id="hDay" class="btn"></select><button class="btn" onclick="loadHistory()">แสดง</button><button class="btn" onclick="dlHistory()">ดาวน์โหลดไฟล์</button></div>
  <div class="tw" id="hOut"><div class="sm">เลือกประเภทและวันที่ แล้วกด แสดง</div></div></div>
 <div class="bar"><button class="btn d" onclick="clearLogs()">ล้าง Log ล่าสุด (หน้าจอ)</button><button class="btn d" onclick="clearLogs(true)">ลบ Log ทั้งหมดใน Flash</button></div>
 <div class="card"><h2>ประวัติการส่งไฟล์ผ่าน Relay</h2><div class="tw"><table><thead><tr><th>เวลา</th><th>จากเครื่องพิมพ์</th><th>Host</th><th>ปลายทาง</th><th>ผล</th><th>ข้อมูล</th><th>ใช้เวลา</th></tr></thead><tbody id="rlRows"></tbody></table></div></div>
 <div class="card"><h2>ประวัติการเปลี่ยน IP</h2><div class="tw"><table><thead><tr><th>เวลา</th><th>Host</th><th>รายละเอียด</th></tr></thead><tbody id="chgRows"></tbody></table></div></div>
 <div class="card"><h2>DNS Query ล่าสุด</h2><div class="tw"><table><thead><tr><th>เวลา</th><th>ผู้ถาม</th><th>ชื่อที่ถาม</th><th>ชนิด</th><th>ผลลัพธ์</th></tr></thead><tbody id="qRows"></tbody></table></div></div>
</section>

<section class="tab" id="t-set">
<form id="setForm" onsubmit="saveSettings(event)">
 <div class="card"><h2>เครือข่ายของ ESP32</h2>
  <div class="chk"><input type="checkbox" id="s_dhcp"><span>รับ IP อัตโนมัติ (DHCP)</span></div>
  <div class="hint">IP ของ ESP32 ต้องคงที่ เพราะเครื่อง Xerox ส่งงานมาที่ IP นี้ (Relay) หรือถาม DNS ที่ IP นี้ · เลือกได้ 2 แบบ: ตั้ง Static IP ที่นี่ หรือเปิด DHCP ไว้แล้วจอง IP ใน Router (แนะนำถ้า Switch เปิด DHCP Snooping / Dynamic ARP Inspection)</div>
  <div class="hint" id="netAuto" style="display:none;color:var(--ok)"></div>
  <div class="chk"><input type="checkbox" id="s_autoFallback"><span><b>ถ้าย้ายไปเสียบวงอื่น ให้ใช้ DHCP แทนชั่วคราว (แนะนำ)</b> – ทุกครั้งที่เสียบสาย ESP32 ตรวจว่ามี Gateway หรืออุปกรณ์ในวงของ Static IP ตอบภายใน 90 วินาที ถ้าไม่มีจะใช้ DHCP จนกว่าจะรีบูต (ค่า Static ยังเก็บไว้) แล้วเปิดหน้าเว็บได้ที่ <span class="mono">http://ชื่ออุปกรณ์/</span> หรือ ESP32Finder</span></div>
  <div class="chk"><input type="checkbox" id="s_conflictFallback"><span>ถ้ามีอุปกรณ์อื่นใช้ IP ซ้ำ ให้เปลี่ยนไปใช้ DHCP ด้วย – <b>ไม่แนะนำ</b>: ถ้าปิดไว้ ESP32 จะใช้ IP เดิมต่อและแจ้งเตือนพร้อม MAC ของเครื่องที่ชน (ไม่ย้าย IP ไปมา และ Xerox ยังส่งมาที่ IP เดิม)</span></div>
  <div class="f2"><div><label>IP Address</label><input type="text" id="s_ip"><div class="hint">กรอกเอง: IP ที่ว่างในวงนี้ และอยู่นอกช่วงที่ Router แจก DHCP</div></div><div><label>Subnet Mask</label><input type="text" id="s_mask"></div>
  <div><label>Gateway</label><input type="text" id="s_gw"></div><div><label>DNS ของ ESP32 เอง</label><input type="text" id="s_dns"><div class="hint">แนะนำให้ใส่: ใช้ซิงก์เวลา (NTP) ให้ Log และเป็น DNS ต้นทางเมื่อช่อง Upstream DNS ว่าง · ใส่ DNS เดียวกับเครื่อง PC ในสำนักงาน (DNS ของ AD / Router) · ห้ามใส่ IP ของ ESP32 เอง</div></div>
  <div><label>ชื่ออุปกรณ์ (hostname)</label><input type="text" id="s_hostname" maxlength="15"><div class="hint">เปิดหน้าเว็บด้วยชื่อได้โดยไม่ต้องรู้ IP: <span class="mono">http://ชื่อนี้/</span> หรือ <span class="mono">http://ชื่อนี้.local/</span> (PC ในวง LAN เดียวกัน) · ใช้ a-z 0-9 - ไม่เกิน 15 ตัว</div></div></div></div>
 <div class="card"><h2>DNS Server</h2><div class="f2">
  <div><label>Upstream DNS 1 (เว้นว่าง = ใช้ค่าจาก DHCP)</label><input type="text" id="s_upstream1"><div class="hint">ควรเป็น DNS เดิมของบริษัท เช่น Domain Controller</div></div>
  <div><label>Upstream DNS 2</label><input type="text" id="s_upstream2"></div>
  <div><label>TTL ของคำตอบ (วินาที)</label><input type="number" id="s_ttl" min="1"></div>
  <div><label>Domain suffix ของเครื่องพิมพ์ (ถ้ามี)</label><input type="text" id="s_domain" placeholder="company.local"></div></div>
  <div class="chk"><input type="checkbox" id="s_anySuffix"><span>จับคู่ชื่อแม้มีโดเมนต่อท้าย เช่น PC01.xxx.yyy → PC01</span></div>
  <div class="chk"><input type="checkbox" id="s_answerAAAA"><span>ตอบ IPv6 (AAAA record)</span></div>
  <div class="chk"><input type="checkbox" id="s_restrict"><span>ให้บริการเฉพาะเครื่องพิมพ์ที่อยู่ในรายการเท่านั้น</span></div></div>
 <div class="card"><h2>การค้นหา IP ของ Host</h2><div class="f2">
  <div><label>ตรวจเบื้องหลังทุก ๆ (วินาที)</label><input type="number" id="s_interval" min="5" max="86400"><div class="hint">1800 = ทุก 30 นาที (แนะนำ) · 3600 = ชั่วโมงละครั้ง · ค่าน้อยทำให้มีแพ็กเก็ตค้นหาในเครือข่ายบ่อยขึ้น</div></div>
  <div><label>เวลารอคำตอบต่อรอบ (ms)</label><input type="number" id="s_timeoutMs" min="200"></div>
  <div><label>Broadcast address เพิ่มเติม (คั่นด้วย ,)</label><input type="text" id="s_extraBcast" placeholder="192.168.2.255"></div></div>
  <div class="chk"><input type="checkbox" id="s_onDemand"><span><b>ค้นหา IP เมื่อ Xerox ส่งงานเท่านั้น (แนะนำ)</b> – ถ้าไม่ได้ยืนยัน IP ของ PC ภายใน 2 นาที จะถามก่อนส่งต่อ (ช้าลงประมาณ 1 วินาทีเฉพาะการเชื่อมต่อแรกของงาน) และไม่สแกนเครือข่ายตามรอบ</span></div>
  <div class="chk"><input type="checkbox" id="s_nbns"><span>NetBIOS Name Service (UDP 137)</span></div>
  <div class="chk"><input type="checkbox" id="s_llmnr"><span>LLMNR (UDP 5355) – รองรับ IPv4/IPv6</span></div>
  <div class="chk"><input type="checkbox" id="s_mdns"><span>mDNS (UDP 5353, ชื่อ.local) – รองรับ IPv4/IPv6</span></div>
  <div class="chk"><input type="checkbox" id="s_passive"><span>ดักฟังการประกาศชื่อ (รู้ IP ใหม่ทันทีที่เครื่องเปิด)</span></div></div>
 <div class="card"><h2>เครื่องปลายทางที่อยู่ต่างวง LAN (ต่าง Subnet / VLAN)</h2>
  <div class="hint" style="margin-bottom:6px">Broadcast/multicast ข้าม router ไม่ได้ ใช้ 2 วิธีนี้แทน (Relay ส่งข้ามวงได้ถ้า router อนุญาตให้ ESP32 เข้าถึงพอร์ต 445 ของวงนั้น)</div>
  <label>สแกน NetBIOS ใน Subnet อื่น (คั่นด้วย ,)</label><input type="text" id="s_scanSubnets" placeholder="192.168.30.0/24, 10.10.5.0/24">
  <div class="hint">ส่งคำถามแบบ unicast ไปทุก IP ในวงนั้นทุกรอบ (/22–/30) – Firewall ของ PC ต้องอนุญาต UDP 137 จากวงของ ESP32</div>
  <div class="chk"><input type="checkbox" id="s_dnsLookup"><span>ถาม DNS ของบริษัท (Active Directory) ด้วยชื่อ Host + Domain suffix ด้านบน</span></div>
  <div class="hint">เหมาะกับเครื่องที่ join domain และลงทะเบียน DNS อัตโนมัติ ต้องกรอก Upstream DNS เป็น DNS ของ AD และ Domain suffix</div></div>
 <div class="card"><h2>เวลา</h2><div class="f2">
  <div><label>Time zone (POSIX)</label><input type="text" id="s_tz"></div><div><label>NTP Server</label><input type="text" id="s_ntp"></div></div>
  <div class="hint">เปลี่ยนชื่อผู้ใช้ / รหัสผ่านผู้ดูแลที่เมนู 👤 มุมขวาบน → เปลี่ยนรหัสผ่าน</div></div>
 <div class="bar"><button class="btn p" type="submit">บันทึกการตั้งค่า</button></div>
</form>
 <div class="card"><h2>สำรอง / กู้คืน / ระบบ</h2><div class="bar">
  <a class="btn" href="/api/backup">ดาวน์โหลด Backup</a>
  <label class="btn" style="margin:0;color:inherit">กู้คืนจากไฟล์<input type="file" id="rf" accept=".json" style="display:none" onchange="restore(this)"></label>
  <button class="btn" onclick="reboot()">รีบูต</button><button class="btn d" onclick="factory()">คืนค่าโรงงาน</button></div></div>
</section>
</main>

<dialog id="dlg"><form method="dialog" id="dlgForm"><h3 id="dlgTitle"></h3><div id="dlgBody"></div>
<div class="dbtn"><button class="btn" value="cancel" formnovalidate>ยกเลิก</button><button class="btn p" id="dlgOk" value="ok">บันทึก</button></div></form></dialog>
<div id="toast"></div>

<script>
const $=s=>document.querySelector(s);let S=null,C=null;
const esc=v=>String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function toast(m,ms){const t=$('#toast');t.textContent=m;t.style.display='block';clearTimeout(t._h);t._h=setTimeout(()=>t.style.display='none',ms||3500)}
async function api(p,o){const r=await fetch(p,o);const j=await r.json().catch(()=>({}));if(r.status===401&&j.login){location.reload();throw new Error(j.error)}
 if(!r.ok||j.ok===false)throw new Error(j.error||('HTTP '+r.status));return j}
const post=(p,b)=>api(p,{method:'POST',headers:{'Content-Type':'application/json'},body:b===undefined?'{}':JSON.stringify(b)});
document.querySelectorAll('#nav button').forEach(b=>b.onclick=()=>{document.querySelectorAll('#nav button').forEach(x=>x.classList.toggle('on',x===b));
 document.querySelectorAll('.tab').forEach(t=>t.classList.toggle('on',t.id==='t-'+b.dataset.t));if(b.dataset.t==='set')loadSettings();if(b.dataset.t==='logs')loadLogTab()});
function badge(s){return{online:'<span class="b ok">ออนไลน์</span>',offline:'<span class="b bad">ไม่พบ</span>',pending:'<span class="b warn">กำลังค้นหา</span>',disabled:'<span class="b mut">ปิดใช้งาน</span>'}[s]||s}
function fmtIntv(s){s=Number(s)||0;return s%3600===0?s/3600+' ชั่วโมง':s%60===0?s/60+' นาที':s+' วินาที'}
function fmtB(b){b=Number(b)||0;return b<1024?b+' B':b<1048576?(b/1024).toFixed(1)+' KB':b<1073741824?(b/1048576).toFixed(1)+' MB':(b/1073741824).toFixed(2)+' GB'}
function ago(s){if(s<0)return'';if(s<60)return s+' วินาทีก่อน';if(s<3600)return Math.floor(s/60)+' นาทีก่อน';if(s<86400)return Math.floor(s/3600)+' ชม.ก่อน';return Math.floor(s/86400)+' วันก่อน'}

async function refresh(){try{S=await api('/api/status');render()}catch(e){$('#hdr').textContent='เชื่อมต่อไม่ได้: '+e.message}}
function render(){const d=S.device,st=S.stats;$('#acctName').textContent=$('#acctWho').textContent=d.user||'admin';
 $('#intv').textContent=(d.onDemand?'ค้นหา IP เมื่อ Xerox ส่งงาน · ตรวจเบื้องหลังทุก ':'ตรวจหา IP อัตโนมัติทุก ')+fmtIntv(d.interval)+' · รับฟังการประกาศชื่อจาก PC ตลอดเวลา';
 $('#hdr').textContent=`IP ${d.ip||'-'} · ${d.link?'Link '+d.speed+'Mbps':'สายแลนไม่เชื่อมต่อ'} · ${d.time||'uptime '+d.uptime} · v${d.fw}`;
 $('#dnsNote').innerHTML=`<b>โหมด Relay (แนะนำ):</b> ใน Xerox เลือก Address Type = <b>IPv4 Address</b> ใส่ IP <b class="ip">${esc(d.ip)}</b> และ Port ตามคอลัมน์ "Relay" ของแต่ละ Host<br>`+
  `<b>โหมด DNS:</b> ตั้ง DNS Server ของ Xerox เป็น <b class="ip">${esc(d.ip)}</b> แล้วใช้ Address Type = Host Name`+(d.dhcp?' <span class="b warn">ESP32 ใช้ DHCP อยู่ – แนะนำตั้ง Static IP</span>':'')+
  (d.netFallback?`<div style="margin-top:6px;color:var(--bad)"><b>⚠ ใช้ DHCP สำรองอยู่ (IP ตอนนี้ ${esc(d.ip)})</b> – ${esc(d.netReason)} · Xerox ที่ตั้งไว้เป็น IP เดิมจะส่งไม่ได้ ให้แก้ Static IP ในแท็บ ตั้งค่า ให้ตรงวงนี้ หรือย้ายกลับวงเดิมแล้วรีบูต</div>`:'')+
  (d.ipConflict&&!d.netFallback?`<div style="margin-top:6px;color:var(--bad)"><b>⚠ IP ${esc(d.ip)} ชนกับอุปกรณ์ MAC ${esc(d.ipConflict)}</b> – ให้เครื่องนั้นเปลี่ยน IP (ESP32 ยังใช้ IP เดิม) แล้วรีบูต ESP32 เพื่อตรวจใหม่</div>`:'');
 const hOn=S.hosts.filter(h=>h.status==='online').length,pOn=S.printers.filter(p=>p.online).length,jobs=S.hosts.reduce((a,h)=>a+h.relayJobs,0);
 $('#kpis').innerHTML=[[`${hOn}/${S.hosts.length}`,'Host ที่พบ IP'],[`${pOn}/${S.printers.length}`,'เครื่องพิมพ์ออนไลน์'],[jobs,'งานที่ส่งผ่าน Relay'],[S.sessions.length,'กำลังส่งตอนนี้'],[st.local,'คำตอบ DNS ชื่อ Host'],[d.uptime,'Uptime']]
  .map(k=>`<div class="card kpi"><div class="v">${esc(k[0])}</div><div class="l">${k[1]}</div></div>`).join('');
 $('#devTbl').innerHTML=[['IP / Mask',`${d.ip} / ${d.mask}`],['Gateway',d.gw],['DNS ของ ESP32',d.dns],['MAC',d.mac],['ชื่ออุปกรณ์',d.hostname],['โหมด IP',d.dhcp?'DHCP':d.netFallback?'DHCP สำรอง (Static ใช้ไม่ได้ในวงนี้)':'Static'],['ลิงก์',d.link?`${d.speed} Mbps ${d.duplex}`:'ไม่เชื่อมต่อ'],['หน่วยความจำว่าง',d.heap+' bytes'],['DNS query ทั้งหมด',`${st.queries} (ปฏิเสธ ${st.refused}, ล้มเหลว ${st.failed})`]]
  .map(r=>`<tr><th style="width:200px">${r[0]}</th><td class="mono">${esc(r[1])}</td></tr>`).join('');
 $('#hostRows').innerHTML=S.hosts.length?S.hosts.map((h,i)=>`<tr><td>${h.scanName?`<b>${esc(h.scanName)}</b>`:'<span class="b mut">ยังไม่มีชื่อ Scan – กด แก้ไข</span>'}<div class="sm">Host: <span class="mono" style="color:var(--fg)">${esc(h.name)}</span></div>${h.mac?`<div class="sm mono">MAC ${esc(h.mac)}</div>`:''}${h.aliases?`<div class="sm">ชื่อแฝง: ${esc(h.aliases)}</div>`:''}${h.note?`<div class="sm">${esc(h.note)}</div>`:''}</td>
  <td>${badge(h.status)}${h.fails>1?`<div class="sm">ไม่ตอบ ${h.fails} รอบ</div>`:''}</td>
  <td class="mono">${esc(h.ipv4||(h.fallback4?h.fallback4+' (สำรอง)':'-'))}</td><td class="mono">${h.ipv6.length?h.ipv6.map(esc).join('<br>'):'-'}</td>
  <td>${esc(h.method)}</td><td>${esc(h.lastSeen)}<div class="sm">${ago(h.ago)}</div></td>
  <td>${h.relayPort?`<span class="mono"><b>${esc(d.ip)} : ${h.relayPort}</b></span><div class="sm">ส่งแล้ว ${h.relayJobs} ไฟล์${h.relayFails?`, ล้มเหลว ${h.relayFails}`:''} · ${fmtB(h.relayBytes)}</div>`:'<span class="sm">ไม่ใช้</span>'}</td><td>${h.dnsHits}</td>
  <td style="white-space:nowrap"><button class="btn s" onclick="resolveOne(${i})">ค้นหา</button> <button class="btn s" onclick="editHost(${i})">แก้ไข</button> <button class="btn s d" onclick="delHost(${i})">ลบ</button></td></tr>`).join('')
  :'<tr><td colspan="9" class="sm">ยังไม่มี Host – กด "+ เพิ่ม Host"</td></tr>';
 $('#sesRows').innerHTML=S.sessions.map(s=>`<tr><td>${esc(s.client)}</td><td><b>${esc(s.host)}</b> <span class="sm">(พอร์ต ${s.port})</span></td><td class="mono">${esc(s.target)}:445</td>
  <td>${s.state==='resolving'?'<span class="b warn">กำลังค้นหา PC</span>':s.state==='connecting'?'<span class="b warn">กำลังเชื่อมต่อ</span>':'<span class="b ok">กำลังส่ง</span>'}${s.file?`<div class="sm mono">${esc(s.file)}</div>`:''}</td><td>${fmtB(s.bytes)}</td><td>${s.secs} วินาที</td></tr>`).join('')||'<tr><td colspan="6" class="sm">ไม่มีการส่งในขณะนี้</td></tr>';
 $('#rlRows').innerHTML=S.relays.map(r=>`<tr><td>${esc(r.time)}</td><td>${esc(r.client)}</td><td>${esc(r.host)}</td><td class="mono">${esc(r.target)}</td>
  <td>${r.ok?'<span class="b ok">':'<span class="b bad">'}${esc(r.result)}</span></td><td>${fmtB(r.bytes)}</td><td>${r.secs} วินาที</td></tr>`).join('')||'<tr><td colspan="7" class="sm">ยังไม่มีการส่งผ่าน Relay</td></tr>';
 $('#prnRows').innerHTML=S.printers.length?S.printers.map((p,i)=>`<tr><td><b>${esc(p.name)}</b>${p.note?`<div class="sm">${esc(p.note)}</div>`:''}</td><td class="mono">${esc(p.ip)}</td>
  <td>${esc(p.model)}<div class="sm">${esc(p.location)}</div></td><td>${!p.checked?'<span class="b mut">รอตรวจ</span>':p.online?'<span class="b ok">ออนไลน์</span>':'<span class="b bad">ออฟไลน์</span>'}</td>
  <td>${p.dnsQueries}${p.dnsQueries?'':'<div class="sm">ยังไม่เคยใช้ DNS นี้</div>'}</td><td class="mono">${esc(p.lastQuery)}<div class="sm">${esc(p.lastQueryTime)}</div></td>
  <td style="white-space:nowrap"><a class="btn s" href="http://${esc(p.ip)}" target="_blank">เปิดหน้าเครื่อง</a> <button class="btn s" onclick="editPrinter(${i})">แก้ไข</button> <button class="btn s d" onclick="delPrinter(${i})">ลบ</button></td></tr>`).join('')
  :'<tr><td colspan="7" class="sm">ยังไม่มีเครื่องพิมพ์ – กด "+ เพิ่มเครื่องพิมพ์"</td></tr>';
 $('#chgRows').innerHTML=S.changes.map(c=>`<tr><td>${esc(c.time)}</td><td>${esc(c.host)}</td><td class="mono">${esc(c.text)}</td></tr>`).join('')||'<tr><td colspan="3" class="sm">ยังไม่มีการเปลี่ยนแปลง</td></tr>';
 $('#qRows').innerHTML=S.queries.map(q=>`<tr><td>${esc(q.time)}</td><td>${esc(q.client)}</td><td class="mono">${esc(q.name)}</td><td>${esc(q.type)}</td><td class="mono">${esc(q.result)}</td></tr>`).join('')||'<tr><td colspan="5" class="sm">ยังไม่มี query</td></tr>';
 const rl=S.hosts.filter(h=>h.relayPort).map(h=>`<span class="mono">${esc(h.name)} → ${esc(d.ip)} : ${h.relayPort}</span>`).join(', ')||'(ยังไม่ได้กำหนดพอร์ต Relay ให้ Host ใด)';
 $('#guideRelay').innerHTML=[`ตั้งพอร์ต Relay ให้ Host ในแท็บ "เครื่องปลายทาง Scan" (เช่น 4451, 4452 …) ตอนนี้มี: ${rl}`,
  `ในเครื่อง Xerox ทุกเครื่อง ไปที่ <b>Address Book → Scan To Destination</b>: Protocol = <b>SMB</b>, Address Type = <b>IPv4 Address</b>`,
  `<b>IP Address : Port</b> = <b class="mono">${esc(d.ip)}</b> : <b>พอร์ต Relay ของ Host นั้น</b> แล้วกรอก Share / Login Name / Password ของเครื่อง PC ตามเดิม`,
  `ทุกครั้งที่แก้ไขรายการ ต้องพิมพ์ <b>Password</b> ใหม่และติ๊ก <b>Select to save new password</b> ไม่เช่นนั้น Xerox จะส่งรหัสว่างและบัญชีบน PC อาจถูกล็อก`,
  `กด <b>Destination Test</b> แล้วดูผลที่แท็บ บันทึก (Log) → ประวัติการส่งไฟล์ผ่าน Relay (แสดงชื่อไฟล์ ขนาด และสาเหตุถ้าส่งไม่ผ่าน)`,
  `ไม่ต้องแก้ DNS ของเครื่อง Xerox และใช้กับ Xerox ได้หลายเครื่องพร้อมกันโดยใช้พอร์ตเดียวกัน`].map(x=>`<li>${x}</li>`).join('');
 $('#guide').innerHTML=[`เปิด Embedded Web Server ของเครื่อง Xerox (พิมพ์ IP เครื่องใน browser) แล้ว Login เป็น Admin`,
  `ไปที่ <b>Properties → Connectivity → Setup → Network (Ethernet) → IPv4</b> (บางเฟิร์มแวร์อยู่ที่ <b>Network → TCP/IP → DNS</b>)`,
  `ยกเลิก "Obtain DNS Server Address Automatically / DHCP" แล้วตั้ง <b>Preferred DNS Server</b> = <b class="mono">${esc(S.device.ip)}</b> (Alternate DNS ให้เว้นว่าง หรือใส่ ESP32 ตัวที่ 2)`,
  `ถ้าเปิด IPv6 บนเครื่องพิมพ์ ให้ปิด "Obtain DNS via DHCPv6" เพื่อให้เครื่องถาม DNS ผ่าน IPv4 มาที่ ESP32`,
  `รีสตาร์ตเครื่องพิมพ์ให้ค่า DNS มีผล`,
  `ไปที่ <b>Address Book → Scan To Destination</b>: Protocol = <b>SMB</b>, Address Type = <b>Host Name</b>, ช่อง <b>Host Name : Port</b> ใส่ชื่อเครื่อง เช่น <span class="mono">PC-ACC01</span> : <span class="mono">445</span> (ต้องตรงกับ Host ในแท็บ "เครื่องปลายทาง Scan") แล้วกรอก Share / Login Name / Password ตามเดิม`,
  `กด <b>Destination Test</b> – ถ้าผ่าน จะเห็นชื่อนั้นในแท็บ Log → DNS Query พร้อมผล LOCAL → IP ของเครื่อง`,
  `เพิ่มเครื่องพิมพ์ในหน้านี้เพื่อดูว่าเครื่องเริ่มถาม DNS แล้วหรือยัง (คอลัมน์ DNS Query)`].map(x=>`<li>${x}</li>`).join('');
}

function dialog(title,fields,vals,onOk,extra){$('#dlgTitle').textContent=title;$('#dlg').style.width='';$('#dlgOk').textContent='บันทึก';
 $('#dlgBody').innerHTML=(extra||'')+fields.map(f=>f.t==='chk'?`<div class="chk"><input type="checkbox" id="f_${f.k}" ${vals[f.k]!==false?'checked':''}><span>${f.l}</span></div>`
  :`<label>${f.l}${f.req?' *':''}</label><input type="text" id="f_${f.k}" value="${esc(vals[f.k]||f.def||'')}" ${f.req?'required':''} placeholder="${esc(f.ph||'')}">${f.h?`<div class="hint">${f.h}</div>`:''}`).join('');
 const d=$('#dlg');d.onclose=async()=>{if(d.returnValue!=='ok')return;const o={};fields.forEach(f=>{const e=$('#f_'+f.k);o[f.k]=f.t==='chk'?e.checked:e.value.trim()});
  try{await onOk(o);toast('บันทึกแล้ว');refresh()}catch(e){toast('ผิดพลาด: '+e.message,6000)}};d.returnValue='';d.showModal()}
const hostCfg=()=>S.hosts.map(h=>({name:h.name,scanName:h.scanName,mac:h.mac,aliases:h.aliases,fallback4:h.fallback4,fallback6:h.fallback6,note:h.note,enabled:h.enabled,relayPort:h.relayPort}));
const prnCfg=()=>S.printers.map(p=>({name:p.name,ip:p.ip,model:p.model,location:p.location,note:p.note}));
function editHost(i){const v=i<0?{}:S.hosts[i];dialog(i<0?'เพิ่ม Host':'แก้ไข Host',[
 {k:'scanName',l:'ชื่อ Scan (ในเครื่อง Xerox)',ph:'เช่น test (Xerox ชั้น 2)',h:'พิมพ์ Nickname ของรายการนี้ในสมุดที่อยู่ของเครื่อง Xerox – ใช้ดูว่า Host นี้ตรงกับรายการ Scan ไหน'},
 {k:'name',l:'Host Name (ชื่อเครื่องคอมพิวเตอร์)',req:1,ph:'PC-ACC01',h:'ควรตรงกับ Computer name ของ Windows (ถ้าใส่ MAC ด้านล่าง จะตั้งชื่ออะไรก็ได้)'},
 {k:'mac',l:'MAC Address (แนะนำ – ค้นหาแม่นยำที่สุด)',ph:'AA:BB:CC:DD:EE:FF',h:'ESP32 ตามหาเครื่องจาก MAC ด้วย ARP ซึ่ง Firewall ของ Windows ปิดกั้นไม่ได้ (เครื่องในวง LAN เดียวกัน)'},
 {k:'aliases',l:'ชื่อแฝง (คั่นด้วย ,)',ph:'scan-acc, pc-acc01.company.local',h:'ชื่ออื่นที่ตั้งไว้ในเครื่อง Xerox ให้ชี้ไปเครื่องเดียวกัน'},
 {k:'relayPort',l:'พอร์ต Relay (เว้นว่าง = ไม่ใช้)',def:i<0&&S.hosts.filter(h=>h.relayPort).length<S.device.maxRelay?nextPort():'',ph:'4451',h:`เครื่อง Xerox ส่งมาที่ ${esc(S.device.ip)}:พอร์ตนี้ แล้ว ESP32 ส่งต่อไปที่ Host นี้พอร์ต 445 (สูงสุด ${S.device.maxRelay} Host)`},
 {k:'fallback4',l:'IPv4 สำรอง',ph:'192.168.1.50',h:'ใช้เมื่อยังค้นหาไม่พบเลยเท่านั้น'},{k:'fallback6',l:'IPv6 สำรอง'},{k:'note',l:'หมายเหตุ',ph:'แผนกบัญชี'},{k:'enabled',l:'เปิดใช้งาน',t:'chk'}],v,
 async o=>{o.relayPort=Number(o.relayPort)||0;o.mac=normMac(o.mac)||o.mac;const l=hostCfg();if(i<0)l.push(o);else l[i]=o;await post('/api/hosts',l)},
 `<div class="bar" style="margin:4px 0 0"><button type="button" class="btn" onclick="scanNet()">🔍 สแกนหาเครื่องในเครือข่าย</button>
  <label class="btn" style="margin:0;color:inherit">📂 นำเข้าไฟล์ JSON<input type="file" accept=".json,application/json" style="display:none" onchange="importMacJson(this)"></label>
  <button type="button" class="btn p" onclick="verifyHost()">✔ Verify</button>
  <span class="sm">ไฟล์จากโปรแกรม MacCollector – ต้องมีชื่อเครื่องและ MAC อย่างน้อย · Verify = ตรวจว่า ESP32 ติดต่อคอมพิวเตอร์และเครื่องถ่ายเอกสารได้จริง</span></div>
  <div id="verify"></div><div id="pick"></div>`)}

// ---- Verify: ESP32 checks the PC (MAC / name / SMB 445) and every copier ----
async function verifyHost(){const v={name:$('#f_name').value.trim(),mac:normMac($('#f_mac').value)||$('#f_mac').value.trim(),
  aliases:$('#f_aliases').value.trim(),fallback4:$('#f_fallback4').value.trim(),relayPort:Number($('#f_relayPort').value)||0};
 $('#verify').innerHTML='<div class="vl"><div class="vh running">กำลังเริ่มตรวจสอบ…</div></div>';
 try{await post('/api/verify',v)}catch(e){$('#verify').innerHTML=`<div class="vl"><div class="vh fail">${esc(e.message)}</div></div>`;return}
 for(let k=0;k<90;k++){await sleep(k?700:300);let r;try{r=await api('/api/verify')}catch(e){continue}
  renderVerify(r);if(!r.running)break}}
function renderVerify(r){const IC={ok:'✓',warn:'!',fail:'✕',skip:'–',wait:'',run:''};
 const head={running:'กำลังตรวจสอบ… (ESP32 ทดสอบจริงจากตัวอุปกรณ์)',ok:'✓ ผ่านทั้งหมด – พร้อมส่งไฟล์ Scan',warn:'ใช้งานได้ แต่มีข้อควรระวัง (ดูรายการสีส้ม)',fail:'พบปัญหา – ดูรายการสีแดงและวิธีแก้'}[r.result];
 $('#verify').innerHTML=`<div class="vl"><div class="vh ${r.result}">${head}</div>`+r.steps.map(s=>`<div class="vr"><span class="ic ${s.state}">${IC[s.state]||''}</span>
  <div><div>${esc(s.label)}</div>${s.detail?`<div class="d">${esc(s.detail)}</div>`:''}</div></div>`).join('')+'</div>'}

// ---- MAC helpers: network scan, JSON import / export ----
let PICK=[];
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
function normMac(s){const h=String(s||'').replace(/[^0-9a-fA-F]/g,'');return h.length===12?h.toUpperCase().match(/../g).join(':'):''}
function cleanName(s){return String(s||'').trim().replace(/[^A-Za-z0-9._-]+/g,'-').replace(/^[-.]+|[-.]+$/g,'').slice(0,63)}
// Fills the form. fromFile: every field present in the file overwrites the form;
// from a network scan only name (when empty), MAC and IPv4 are filled.
function pickDev(d,fromFile){const m=normMac(d.mac),set=(k,v)=>{const e=$('#f_'+k);if(e&&v!==undefined&&v!==null&&String(v)!=='')e.value=v};
 set('mac',m);
 if(fromFile||!$('#f_name').value)set('name',cleanName(d.name)||(m?'PC-'+m.replace(/:/g,'').slice(-6):''));
 if(fromFile){set('aliases',d.aliases);set('fallback4',d.fallback4);set('fallback6',d.fallback6);set('note',d.note);set('relayPort',Number(d.relayPort)||'')}
 else if(!$('#f_fallback4').value)set('fallback4',d.fallback4);
 const filled=['name','mac','aliases','fallback4','fallback6','note','relayPort'].filter(k=>fromFile?d[k]:['name','mac','fallback4'].includes(k)&&d[k]);
 $('#pick').innerHTML=`<div class="pick sm">กรอกข้อมูลจาก <b>${esc(d.name)}</b> (<span class="mono">${esc(m)}</span>) แล้ว: ${filled.map(esc).join(', ')} – ตรวจสอบแล้วกด บันทึก</div>`}
function showPick(list,title,note,fromFile){PICK=list;const used=new Set(S.hosts.map(h=>normMac(h.mac)).filter(Boolean));
 const q=normMac($('#f_mac').value)?'':String($('#f_mac').value||'').replace(/[^0-9a-fA-F]/g,'').toUpperCase();
 const rows=list.map((d,i)=>[d,i]).filter(([d])=>!q||normMac(d.mac).replace(/:/g,'').includes(q));
 $('#pick').innerHTML=`<div class="pick"><div class="bar" style="margin-bottom:6px"><b>${esc(title)}</b><span class="sm">${esc(note||'')} · ${rows.length} เครื่อง</span>
  <button type="button" class="btn s" onclick="exportMacJson()" ${list.length?'':'disabled'}>💾 Export JSON</button></div>
  <div class="tw"><table><thead><tr><th>ชื่อเครื่อง</th><th>IPv4</th><th>MAC</th><th>พบโดย</th><th></th></tr></thead><tbody>${rows.map(([d,i])=>{const m=normMac(d.mac);
  return`<tr><td>${esc(d.name||'-')}${d.host?`<div class="sm">ใช้กับ Host: ${esc(d.host)}</div>`:''}</td><td class="mono">${esc(d.fallback4||'')}</td><td class="mono">${esc(m)}</td><td class="sm">${esc(d.src||'')}</td>
  <td>${used.has(m)?'<span class="b mut">มีแล้ว</span>':`<button type="button" class="btn s p" onclick="pickDev(PICK[${i}],${!!fromFile})">เลือก</button>`}</td></tr>`}).join('')||'<tr><td colspan="5" class="sm">ไม่พบเครื่อง</td></tr>'}</tbody></table></div></div>`}
async function scanNet(){$('#pick').innerHTML='<div class="pick sm">กำลังสแกนเครือข่ายด้วย ARP…</div>';
 try{await post('/api/scan');for(let k=0;k<40;k++){await sleep(k?1500:600);const r=await api('/api/neighbors');
  const list=r.list.map(n=>({name:n.name,mac:n.mac,fallback4:n.ip,src:n.src,host:n.host}));
  showPick(list,'เครื่องที่พบในเครือข่าย',r.scanning?`กำลังสแกน ${r.progress}%`:'สแกนเสร็จแล้ว');if(!r.scanning&&k>0)break}}
 catch(e){$('#pick').innerHTML=`<div class="pick sm">สแกนไม่สำเร็จ: ${esc(e.message)}</div>`}}
// Accepts one host object {name, mac, ...}, an array of them, or {devices|hosts|list:[...]}.
// Field names follow the Add Host form; a few common alternatives are accepted too.
function macDevices(j){const a=Array.isArray(j)?j:(j.devices||j.hosts||j.list||((j.name||j.mac||j.computerName)?[j]:[]));
 const v6=x=>x.fallback6||(Array.isArray(x.ipv6)?x.ipv6.find(s=>!/^fe80/i.test(s)):x.ipv6)||'';
 return a.map(x=>({name:String(x.name||x.computerName||x.hostname||x.host||'').trim(),mac:normMac(x.mac||x.macAddress||x.MAC),
  aliases:x.aliases||'',fallback4:x.fallback4||x.ipv4||x.ip||'',fallback6:v6(x),note:x.note||'',relayPort:x.relayPort||'',src:x.src||x.adapter||'ไฟล์ JSON'}))}
async function importMacJson(inp){const f=inp.files[0];inp.value='';if(!f)return;
 try{const all=macDevices(JSON.parse(await f.text())),list=all.filter(x=>x.name&&x.mac);
  if(!list.length)throw new Error(all.length?'ข้อมูลต้องมีอย่างน้อย ชื่อเครื่อง (name) และ MAC Address (mac) ที่ถูกต้อง':'ไม่พบข้อมูลเครื่องในไฟล์');
  if(list.length===1)pickDev(list[0],true);
  else showPick(list,'จากไฟล์ '+f.name,`เลือกเครื่องที่ต้องการ${all.length>list.length?` (ข้าม ${all.length-list.length} รายการที่ไม่มีชื่อ/MAC)`:''}`,true)}
 catch(e){$('#pick').innerHTML=`<div class="pick sm">นำเข้าไม่สำเร็จ: ${esc(e.message)}</div>`}}
function exportMacJson(){const data={type:'xerox-host-dns-hosts',version:1,created:new Date().toISOString(),source:'ESP32 '+(S.device.ip||''),
  devices:PICK.map(d=>({name:d.name||'',mac:normMac(d.mac),fallback4:d.fallback4||'',src:d.src||''}))};
 const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)],{type:'application/json'}));
 a.download='mac-list-'+new Date().toISOString().slice(0,10)+'.json';a.click()}
function nextPort(){let p=4451;const u=S.hosts.map(h=>h.relayPort);while(u.includes(p))p++;return String(p)}
async function delHost(i){const h=S.hosts[i];if(!confirm('ลบรายการ '+(h.scanName?h.scanName+' (Host '+h.name+')':'Host '+h.name)+' ?'))return;const l=hostCfg();l.splice(i,1);try{await post('/api/hosts',l);refresh()}catch(e){toast(e.message)}}
function editPrinter(i){const v=i<0?{}:S.printers[i];dialog(i<0?'เพิ่มเครื่องพิมพ์':'แก้ไขเครื่องพิมพ์',[
 {k:'name',l:'ชื่อเครื่องพิมพ์',req:1,ph:'Xerox ชั้น 2'},{k:'ip',l:'IPv4 ของเครื่องพิมพ์',req:1,ph:'192.168.1.20'},
 {k:'model',l:'รุ่น',def:'Xerox AltaLink C8155'},{k:'location',l:'ที่ตั้ง'},{k:'note',l:'หมายเหตุ'}],v,
 async o=>{const l=prnCfg();if(i<0)l.push(o);else l[i]=o;await post('/api/printers',l)})}
async function delPrinter(i){if(!confirm('ลบเครื่องพิมพ์ '+S.printers[i].name+' ?'))return;const l=prnCfg();l.splice(i,1);try{await post('/api/printers',l);refresh()}catch(e){toast(e.message)}}
async function resolveOne(i){await post('/api/resolve?i='+i);toast('กำลังค้นหา '+S.hosts[i].name+' …');setTimeout(refresh,2000)}
async function resolveAll(){await post('/api/resolve');toast('เริ่มค้นหาใหม่ทั้งหมด');setTimeout(refresh,3000)}
async function clearLogs(flash){if(!confirm(flash?'ลบไฟล์ Log ทั้งหมดใน Flash? (กู้คืนไม่ได้)':'ล้าง Log ล่าสุดบนหน้าจอ?'))return;
 await post('/api/clearlogs'+(flash?'?flash=1':''));refresh();if(flash)loadLogTab()}

// ---- Log storage / history ----
let LI=null;const LOGF=['changeDays','relayDays','queryDays','maxKB','flushMin','queryPersist'];
const LOGDEF={changeDays:90,relayDays:30,queryDays:3,maxKB:512,flushMin:5,queryPersist:false};
async function loadLogTab(){try{const c=await api('/api/config');LOGF.forEach(k=>{const e=$('#l_'+k);if(e.type==='checkbox')e.checked=!!c.log[k];else e.value=c.log[k]})}catch(e){}
 await loadLogInfo()}
async function loadLogInfo(){try{LI=await api('/api/logs/info')}catch(e){toast(e.message);return}
 const kb=b=>(b/1024).toFixed(1)+' KB',pct=Math.min(100,LI.logBytes*100/Math.max(1,LI.quota));
 $('#logUse').innerHTML=`Log ใช้ไป <b>${kb(LI.logBytes)}</b> จากโควตา ${kb(LI.quota)} (${pct.toFixed(0)}%) · ${LI.files.length} ไฟล์ · Flash ทั้งหมดใช้ ${kb(LI.fsUsed)} / ${kb(LI.fsTotal)}`+
  (LI.pending?` · รอเขียน ${LI.pending} รายการ${LI.nextFlush?` (อีก ${LI.nextFlush} วินาที)`:''}`:'')+(LI.timeValid?'':' · <span class="b warn">ยังไม่ได้เวลาจาก NTP – พักข้อมูลไว้ใน RAM</span>');
 $('#logBar').style.width=pct+'%';$('#logBar').style.background=pct>90?'var(--bad)':pct>70?'var(--warn)':'var(--pri)';
 $('#l_maxHint').textContent=`สูงสุด ${Math.floor(LI.fsTotal*0.6/1024)} KB (60% ของ Flash) – แนะนำ 512 KB`;fillDays()}
function fillDays(){if(!LI)return;const t=$('#hType').value,days=LI.files.filter(f=>f.type===t);
 $('#hDay').innerHTML=days.map(f=>`<option value="${esc(f.name)}">${esc(f.day)} (${(f.size/1024).toFixed(1)} KB)</option>`).join('')||'<option value="">ไม่มีข้อมูล</option>'}
function setLogDefaults(){LOGF.forEach(k=>{const e=$('#l_'+k);if(e.type==='checkbox')e.checked=LOGDEF[k];else e.value=LOGDEF[k]})}
async function saveLogCfg(){const l={};LOGF.forEach(k=>{const e=$('#l_'+k);l[k]=e.type==='checkbox'?e.checked:Number(e.value)});
 try{await post('/api/settings',{log:l});toast('บันทึกการตั้งค่า Log แล้ว');loadLogTab()}catch(e){toast('ผิดพลาด: '+e.message,6000)}}
async function flushLogs(){await post('/api/logs/flush');toast('เขียน Log ลง Flash แล้ว');loadLogInfo()}
const HCOLS={r:['เวลา','จากเครื่องพิมพ์','Host','ปลายทาง','ผล','ข้อมูล','ใช้เวลา'],c:['เวลา','Host','รายละเอียด'],q:['เวลา','ผู้ถาม','ชื่อที่ถาม','ชนิด','ผลลัพธ์']};
async function loadHistory(){const n=$('#hDay').value;if(!n)return;$('#hOut').innerHTML='<div class="sm">กำลังโหลด…</div>';
 try{const r=await fetch('/api/logs/file?name='+encodeURIComponent(n));if(!r.ok)throw new Error('HTTP '+r.status);
  const t=n[0],rows=(await r.text()).split('\n').filter(Boolean).map(l=>l.split('\t')).reverse();
  const p2=v=>String(v).padStart(2,'0'),fmtT=s=>{const d=new Date(Number(s)*1000);return `${d.getFullYear()}-${p2(d.getMonth()+1)}-${p2(d.getDate())} ${p2(d.getHours())}:${p2(d.getMinutes())}:${p2(d.getSeconds())}`};
  const cell=(t,c)=>t==='r'?[fmtT(c[0]),c[1],c[2],c[3],`<span class="b ${c[7]==='1'?'ok':'bad'}">${esc(c[4])}</span>`,fmtB(c[5]),c[6]+' วินาที']:[fmtT(c[0]),...c.slice(1)];
  $('#hOut').innerHTML=`<div class="sm" style="margin-bottom:4px">${rows.length} รายการ${rows.length>500?' (แสดง 500 รายการล่าสุด – ดาวน์โหลดไฟล์เพื่อดูทั้งหมด)':''}</div><table><thead><tr>${HCOLS[t].map(h=>`<th>${h}</th>`).join('')}</tr></thead><tbody>`+
   rows.slice(0,500).map(c=>'<tr>'+cell(t,c).map((v,i)=>`<td${i&&t!=='r'||i===3?' class="mono"':''}>${t==='r'&&i===4?v:esc(v)}</td>`).join('')+'</tr>').join('')+'</tbody></table>'}
 catch(e){$('#hOut').innerHTML=`<div class="sm">โหลดไม่สำเร็จ: ${esc(e.message)}</div>`}}
function dlHistory(){const n=$('#hDay').value;if(n)location.href='/api/logs/file?dl=1&name='+encodeURIComponent(n)}
async function testName(){const n=$('#testName').value.trim();if(!n)return;const r=await api('/api/test?name='+encodeURIComponent(n));
 $('#testOut').innerHTML=r.match?`ตรงกับ Host <b>${esc(r.match)}</b> → IPv4 <b class="mono">${esc(r.ipv4||'ยังไม่ทราบ')}</b> (${r.status==='online'?'ออนไลน์':'ยังไม่พบในรอบล่าสุด'})`:'ไม่ตรงกับ Host ใดในรายการ — คำถามนี้จะถูกส่งต่อไปยัง Upstream DNS'}

const SF={net:['dhcp','ip','mask','gw','dns','hostname','autoFallback','conflictFallback'],dns:['upstream1','upstream2','ttl','domain','anySuffix','answerAAAA','restrict'],
 res:['onDemand','interval','timeoutMs','extraBcast','nbns','llmnr','mdns','passive','scanSubnets','dnsLookup'],time:['tz','ntp']};
async function loadSettings(){try{C=await api('/api/config')}catch(e){toast(e.message);return}
 for(const g in SF)SF[g].forEach(k=>{const e=$('#s_'+k);if(!e)return;const v=C[g]?.[k];if(e.type==='checkbox')e.checked=!!v;else e.value=v??''});togDhcp()}
// Switching DHCP off fills Mask / Gateway / DNS with what DHCP gave the ESP32 right now; the IP is left to the user.
function togDhcp(fromUser){const on=$('#s_dhcp').checked,d=S&&S.device,ok=v=>v&&v!=='0.0.0.0';
 ['ip','mask','gw','dns'].forEach(k=>$('#s_'+k).disabled=on);
 $('#s_ip').placeholder=d&&ok(d.ip)?'เช่น '+d.ip:'';
 const note=$('#netAuto');note.style.display='none';
 if(on||!fromUser||!d)return;
 if(ok(d.mask))$('#s_mask').value=d.mask;if(ok(d.gw))$('#s_gw').value=d.gw;
 $('#s_dns').value=ok(d.dns)&&d.dns!==d.ip?d.dns:ok(d.gw)?d.gw:'';
 $('#s_ip').value='';$('#s_ip').focus();
 note.textContent=`เติม Subnet Mask / Gateway / DNS จากค่าที่ ESP32 ได้รับจาก DHCP ตอนนี้แล้ว – กรอก IP Address เอง แล้วกด บันทึกการตั้งค่า`;note.style.display=''}
$('#s_dhcp').onchange=()=>togDhcp(true);
const isIp=v=>/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.test(v)&&v.split('.').every(n=>n<256);
async function saveSettings(ev){ev.preventDefault();const b={};
 if(!$('#s_dhcp').checked){const ip=$('#s_ip').value.trim(),gw=$('#s_gw').value.trim(),dns=$('#s_dns').value.trim(),m=$('#s_mask').value.trim();
  const bad=!isIp(ip)?'กรุณากรอก IP Address ให้ถูกต้อง':!isIp(m)?'Subnet Mask ไม่ถูกต้อง':!isIp(gw)?'Gateway ไม่ถูกต้อง':ip===gw?'IP Address ซ้ำกับ Gateway':
   dns&&!isIp(dns)?'DNS ไม่ถูกต้อง':dns===ip?'DNS ของ ESP32 ห้ามเป็น IP ของ ESP32 เอง (จะถามวนกลับมาที่ตัวเอง)':'';
  if(bad){toast(bad,6000);$('#s_ip').focus();return}}
 for(const g in SF){b[g]={};SF[g].forEach(k=>{const e=$('#s_'+k);b[g][k]=e.type==='checkbox'?e.checked:e.type==='number'?Number(e.value):e.value.trim()})}
 try{const r=await post('/api/settings',b);
  if(r.reboot){if(confirm('บันทึกแล้ว ต้องรีบูตเพื่อใช้ค่าเครือข่าย/เวลาใหม่ รีบูตเลยหรือไม่?'))reboot(true)}else toast('บันทึกการตั้งค่าแล้ว')}catch(e){toast('ผิดพลาด: '+e.message,6000)}}
async function reboot(nc){if(!nc&&!confirm('รีบูตอุปกรณ์?'))return;await post('/api/reboot');toast('กำลังรีบูต… รอสักครู่แล้วรีเฟรชหน้า (ถ้าเปลี่ยน IP ให้เปิดด้วย IP ใหม่)',10000)}
async function factory(){if(!confirm('คืนค่าโรงงาน? รายการ Host/เครื่องพิมพ์และการตั้งค่าทั้งหมดจะถูกลบ'))return;await post('/api/factory');toast('กำลังคืนค่าโรงงานและรีบูต…',10000)}
async function restore(inp){const f=inp.files[0];if(!f)return;try{const j=JSON.parse(await f.text());await post('/api/restore',j);toast('กู้คืนแล้ว กำลังรีบูต…',10000)}catch(e){toast('กู้คืนไม่สำเร็จ: '+e.message,6000)}inp.value=''}
// Dialog with its own content: onOk runs on the OK button and the dialog stays open if it throws.
function customDialog(title,width,okText,html,onOk,errId){const d=$('#dlg');$('#dlgTitle').textContent=title;d.style.width=width;$('#dlgOk').textContent=okText;
 $('#dlgBody').innerHTML=html;d.onclose=()=>{$('#dlgOk').onclick=null};d.returnValue='';d.showModal();
 $('#dlgOk').onclick=async ev=>{ev.preventDefault();$('#'+errId).textContent='';try{const m=await onOk();d.close();if(m)toast(m);refresh()}catch(e){$('#'+errId).textContent=e.message}}}

// ---- Admin account ----
document.addEventListener('click',e=>{if(!e.target.closest('.acct'))$('#menu').classList.remove('on')});
async function logout(){try{await post('/api/logout')}catch(e){}location.reload()}
function changePass(){$('#menu').classList.remove('on');
 customDialog('เปลี่ยนรหัสผ่านผู้ดูแล','min(440px,94vw)','บันทึก',`<label>ชื่อผู้ใช้</label><input type="text" id="pw_user" value="${esc(S.device.user||'admin')}" autocomplete="username">
  <label>รหัสผ่านปัจจุบัน *</label><input type="password" id="pw_old" autocomplete="current-password">
  <label>รหัสผ่านใหม่ * (อย่างน้อย 6 ตัวอักษร)</label><input type="password" id="pw_new" autocomplete="new-password">
  <label>ยืนยันรหัสผ่านใหม่ *</label><input type="password" id="pw_new2" autocomplete="new-password">
  <div class="hint">หลังเปลี่ยน เครื่องอื่นที่ล็อกอินค้างไว้จะต้องล็อกอินใหม่ · โปรแกรม ESP32Finder ใช้รหัสใหม่นี้ด้วย · ถ้าลืมรหัส ต้องคืนค่าโรงงาน (CFG→GND ตอนเปิดเครื่อง) ซึ่งจะกลับเป็น admin / admin และลบรายการทั้งหมด</div>
  <div id="pwErr" class="hint" style="color:var(--bad)"></div>`,
  async()=>{const u=$('#pw_user').value.trim(),o=$('#pw_old').value,n=$('#pw_new').value;
   const bad=!u?'กรุณากรอกชื่อผู้ใช้':!o?'กรุณากรอกรหัสผ่านปัจจุบัน':n.length<6?'รหัสผ่านใหม่ต้องมีอย่างน้อย 6 ตัวอักษร':n!==$('#pw_new2').value?'ยืนยันรหัสผ่านใหม่ไม่ตรงกัน':'';
   if(bad)throw new Error(bad);await post('/api/password',{user:u,old:o,pass:n});return 'เปลี่ยนรหัสผ่านแล้ว'},'pwErr');
 $('#pw_old').focus()}

// ---- Add hosts from a Xerox address book export (.csv) ----
// Columns used: ScanNickName / DisplayName, ScanServerAddress, ScanServerPort, ScanSMBShare, ScanLoginName.
// An entry pointing at this ESP32 is a relay entry (its port is the relay port); an entry with a
// host name is a PC addressed by name; any other IP becomes the host's fallback IPv4.
function parseCsv(t){const rows=[];let row=[],f='',q=false;
 const end=()=>{row.push(f);f='';if(row.some(x=>x.trim()!==''))rows.push(row);row=[]};
 for(let i=0;i<t.length;i++){const c=t[i];
  if(q){if(c==='"'){if(t[i+1]==='"'){f+='"';i++}else q=false}else f+=c}
  else if(c==='"')q=true;else if(c===','){row.push(f);f=''}
  else if(c==='\n'||c==='\r'){if(c==='\r'&&t[i+1]==='\n')i++;end()}
  else f+=c}
 end();return rows}
function decodeCsv(buf){const b=new Uint8Array(buf);
 if(b[0]===0xFF&&b[1]===0xFE)return new TextDecoder('utf-16le').decode(buf);
 if(b[0]===0xFE&&b[1]===0xFF)return new TextDecoder('utf-16be').decode(buf);
 try{return new TextDecoder('utf-8',{fatal:true}).decode(buf).replace(/^﻿/,'')}catch(e){return new TextDecoder('windows-874').decode(buf)}}
function mergeScan(old,add){const a=String(old||'').split(',').map(x=>x.trim()).filter(Boolean);
 String(add||'').split(',').map(x=>x.trim()).filter(Boolean).forEach(x=>{if(!a.some(y=>y.toLowerCase()===x.toLowerCase()))a.push(x)});return a.join(', ')}
// Template in the Xerox address book export format (the same file can also be imported into the copier).
const CSV_COLS='XrxAddressBookId,DisplayName,FirstName,LastName,Company,XrxAllFavoritesOrder,MemberOf,IsDL,XrxApplicableWorkflows,FaxNumber,XrxIsFaxFavorite,E-mailAddress,XrxIsEmailFavorite,InternetFaxAddress,ScanNickName,XrxIsScanFavorite,ScanTransferProtocol,ScanServerAddress,ScanServerPort,ScanDocumentPath,ScanLoginName,ScanLoginPassword,ScanSMBShare,ScanNDSTree,ScanNDSContext,ScanNDSVolume'.split(',');
function csvTemplate(){const ip=S.device.ip||'192.168.1.10',q=v=>/[",\r\n]/.test(v)?'"'+v.replace(/"/g,'""')+'"':v;
 const rows=[{XrxAddressBookId:1,DisplayName:'Accounting',FirstName:'Accounting',ScanNickName:'acc-scan',ScanServerAddress:ip,ScanServerPort:'4451',ScanLoginName:'scan',ScanSMBShare:'scan'},
  {XrxAddressBookId:2,DisplayName:'HR',FirstName:'HR',ScanNickName:'hr-scan',ScanServerAddress:ip,ScanServerPort:'',ScanLoginName:'scan',ScanSMBShare:'scan'},
  {XrxAddressBookId:3,DisplayName:'Sales',FirstName:'Sales',ScanNickName:'sales-scan',ScanServerAddress:'DESKTOP-SALES01',ScanServerPort:'',ScanLoginName:'scan',ScanSMBShare:'scan'}];
 const def=c=>/^(IsDL|XrxIsFaxFavorite|XrxIsEmailFavorite|XrxIsScanFavorite)$/.test(c)?0:c==='ScanTransferProtocol'?3:'';
 const txt='\uFEFF'+[CSV_COLS.join(','),...rows.map(r=>CSV_COLS.map(c=>q(String(r[c]??def(c)))).join(','))].join('\r\n')+'\r\n';
 const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([txt],{type:'text/csv;charset=utf-8'}));a.download='AddressBook_Template.csv';a.click();
 toast('ดาวน์โหลด Template แล้ว – ScanServerAddress = IP ของ ESP32 หรือชื่อเครื่อง PC · ScanServerPort เว้นว่างได้ (ระบบสร้างพอร์ต Relay ให้)',8000)}
let IMP=[];
async function importCsv(inp){const f=inp.files[0];inp.value='';if(!f)return;
 try{const rows=parseCsv(decodeCsv(await f.arrayBuffer()));
  const H=rows.length?rows[0].map(h=>h.trim().toLowerCase()):[];
  if(!H.includes('scanserveraddress'))throw new Error('ไม่ใช่ไฟล์ Address Book ของ Xerox (ไม่มีคอลัมน์ ScanServerAddress)');
  const col=(r,...n)=>{for(const k of n){const i=H.indexOf(k.toLowerCase());if(i>=0&&(r[i]||'').trim())return r[i].trim()}return''};
  const my=S.device.ip,byKey=new Map();IMP=[];
  for(const r of rows.slice(1)){const addr=col(r,'ScanServerAddress');if(!addr)continue;
   // blank ScanServerPort = give the entry a relay port (generated below)
   const nick=col(r,'ScanNickName','DisplayName','FirstName')||addr,raw=col(r,'ScanServerPort'),port=Number(raw)||0,ip=isIp(addr),mine=addr===my,relay=mine?port:0;
   const ex=relay?S.hosts.findIndex(h=>h.relayPort===relay)
    :mine?S.hosts.findIndex(h=>h.scanName.split(',').some(x=>x.trim().toLowerCase()===nick.toLowerCase()))
    :ip?S.hosts.findIndex(h=>h.fallback4===addr||h.ipv4===addr):S.hosts.findIndex(h=>h.name.toLowerCase()===addr.toLowerCase());
   const info={nick,display:col(r,'DisplayName'),share:col(r,'ScanSMBShare'),login:col(r,'ScanLoginName'),addr:addr+':'+(raw?port:'(ว่าง)')};
   const key=ex>=0?'h'+ex:relay?'p'+relay:mine?'n'+nick.toLowerCase():addr.toLowerCase();
   if(byKey.has(key)){byKey.get(key).src.push(info);continue}
   const old=ex>=0?S.hosts[ex].relayPort:0;
   const e={ex,relay,src:[info],on:true,name:ex>=0?S.hosts[ex].name:cleanName(ip?nick:addr),nameSure:ex>=0||!ip,
    mac:ex>=0?S.hosts[ex].mac:'',fallback4:ip&&!mine?addr:'',port:relay||old||'',gen:!raw&&!old};
   byKey.set(key,e);IMP.push(e)}
  if(!IMP.length)throw new Error('ไม่พบรายการ Scan ที่มี ScanServerAddress');
  const used=new Set([...S.hosts.map(h=>h.relayPort),...IMP.map(e=>Number(e.port))].filter(Boolean));
  IMP.forEach(e=>{if(!e.gen)return;let p=4451;while(used.has(p))p++;used.add(p);e.port=String(p)});
  IMP.forEach(e=>{e.scan=e.src.map(x=>x.nick).filter((v,i,a)=>a.indexOf(v)===i).join(', ');
   if(e.ex>=0){const h=S.hosts[e.ex];e.same=mergeScan(h.scanName,e.scan)===h.scanName&&(!e.port||Number(e.port)===h.relayPort);e.on=!e.same}});
  showImport(f.name)}
 catch(e){toast('นำเข้า CSV ไม่สำเร็จ: '+e.message,7000)}}
function showImport(fname){
 customDialog('เพิ่ม Host จากไฟล์ CSV (Address Book ของ Xerox)','min(1180px,97vw)','เพิ่ม / อัปเดตรายการที่เลือก',
  `<div class="hint" style="margin-bottom:6px">ไฟล์ <b>${esc(fname)}</b> · ${IMP.length} รายการ · ตรวจ <b>Host Name</b> ให้เป็น Computer name ของ Windows (หรือใส่ MAC แทน) ·
  รายการที่ชี้มาที่ ESP32 (<span class="mono">${esc(S.device.ip)}</span>) ใช้พอร์ตในไฟล์เป็นพอร์ต Relay · <b>ScanServerPort ว่าง</b> = สร้างพอร์ต Relay ให้ (4451 ขึ้นไปที่ยังว่าง) แล้วใน Xerox ตั้งปลายทางเป็น <span class="mono">${esc(S.device.ip)}</span> : พอร์ตนั้น · ไม่นำเข้ารหัสผ่าน</div>
  <div class="bar" style="margin-bottom:6px"><span class="sm">ต่อท้ายชื่อ Scan ด้วยชื่อเครื่อง Xerox:</span><select id="impPrn" class="btn s"><option value="">(ไม่ต่อท้าย)</option>${S.printers.map(p=>`<option>${esc(p.name)}</option>`).join('')}</select>
  <button type="button" class="btn s" onclick="impAutoPort()">ให้พอร์ต Relay อัตโนมัติกับแถวที่ยังว่าง</button></div>
  <div class="tw imp"><table><thead><tr><th></th><th>ชื่อ Scan (จากไฟล์)</th><th>ปลายทางในไฟล์</th><th>Share / ผู้ใช้</th><th>Host Name *</th><th>MAC (ถ้ามี)</th><th>พอร์ต Relay</th><th>ผล</th></tr></thead><tbody id="impRows"></tbody></table></div>
  <div id="impErr" class="hint" style="color:var(--bad)"></div>`,applyImport,'impErr');
 renderImp()}
function renderImp(){$('#impRows').innerHTML=IMP.map((e,i)=>{const h=e.ex>=0?S.hosts[e.ex]:null;
  const res=e.same?'<span class="b mut">มีแล้ว (ไม่เปลี่ยน)</span>':h?`<span class="b warn">อัปเดต Host ${esc(h.name)}</span>`:'<span class="b ok">เพิ่มใหม่</span>';
  return`<tr class="${e.on?'':'off'}"><td><input type="checkbox" ${e.on?'checked':''} onchange="IMP[${i}].on=this.checked;this.closest('tr').className=this.checked?'':'off'"></td>
  <td><b>${esc(e.scan)}</b>${e.src[0].display&&e.src[0].display!==e.src[0].nick?`<div class="sm">${esc(e.src[0].display)}</div>`:''}</td>
  <td class="mono">${[...new Set(e.src.map(x=>x.addr))].map(esc).join('<br>')}</td>
  <td class="sm">${e.src.map(x=>esc(x.share+(x.login?' / '+x.login:''))).join('<br>')}</td>
  <td><input type="text" value="${esc(e.name)}" ${h?'disabled':''} oninput="IMP[${i}].name=this.value.trim()">${!h&&!e.nameSure?'<div class="sm" style="color:var(--warn)">ไฟล์ไม่มีชื่อเครื่อง – ตรวจ/แก้ หรือใส่ MAC</div>':''}</td>
  <td><input type="text" value="${esc(e.mac)}" placeholder="AA:BB:CC:DD:EE:FF" ${h?'disabled':''} oninput="IMP[${i}].mac=this.value.trim()"></td>
  <td><input type="text" value="${esc(e.port)}" style="width:80px;min-width:0" ${e.relay?'disabled':''} oninput="IMP[${i}].port=this.value.trim()">${e.gen?'<div class="sm" style="color:var(--ok)">สร้างให้อัตโนมัติ</div>':''}</td><td>${res}</td></tr>`}).join('')}
function impAutoPort(){const used=new Set([...S.hosts.map(h=>h.relayPort),...IMP.map(e=>Number(e.port))].filter(Boolean));
 IMP.forEach(e=>{if(!e.on||e.port)return;let p=4451;while(used.has(p))p++;used.add(p);e.port=String(p)});renderImp()}
async function applyImport(){const l=hostCfg(),sel=IMP.filter(e=>e.on),prn=$('#impPrn').value;
 if(!sel.length)throw new Error('ยังไม่ได้เลือกรายการ');
 for(const e of sel){const scan=prn?e.scan.split(', ').map(x=>`${x} (${prn})`).join(', '):e.scan,port=Number(e.port)||0;
  if(e.ex>=0){const h=l[e.ex];h.scanName=mergeScan(h.scanName,scan);if(port)h.relayPort=port;continue}
  const name=cleanName(e.name);if(!name)throw new Error(`กรุณากรอก Host Name ของ "${e.scan}"`);
  if(e.mac&&!normMac(e.mac))throw new Error(`MAC ของ "${e.scan}" ไม่ถูกต้อง`);
  const d=e.src[0].display;
  l.push({name,scanName:scan,mac:normMac(e.mac),aliases:'',fallback4:e.fallback4,fallback6:'',note:d&&d!==e.src[0].nick?d:'',enabled:true,relayPort:port})}
 await post('/api/hosts',l);return `นำเข้าแล้ว ${sel.length} รายการ`}

refresh();setInterval(()=>{if(!$('#dlg').open)refresh()},5000);
</script></body></html>)rawliteral";
