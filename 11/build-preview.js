// Builds preview.html from the real firmware UI (../XeroxHostDNS/WebPage.h) + mock.js.
// Run again after changing WebPage.h:   node build-preview.js
const fs = require('fs');
const path = require('path');

const src = fs.readFileSync(path.join(__dirname, '..', 'XeroxHostDNS', 'WebPage.h'), 'utf8');
const html = src.split('INDEX_HTML[] PROGMEM = R"rawliteral(')[1].split(')rawliteral"')[0];
const mock = fs.readFileSync(path.join(__dirname, 'mock.js'), 'utf8');

const banner =
  '<div style="background:#fff3df;color:#7a4a00;border-bottom:1px solid #f0d9b0;padding:8px 16px;font-size:13px">' +
  '<b>โหมดตัวอย่าง (Preview)</b> – หน้านี้เหมือนหน้าเว็บใน ESP32 ทุกอย่าง แต่ใช้ข้อมูลจำลอง ' +
  'ไม่ได้เชื่อมต่ออุปกรณ์จริง กดเพิ่ม/แก้ไข/ลบได้ (ข้อมูลหายเมื่อรีเฟรชหน้า)</div>';

const out = html
  .replace('<title>Xerox Host DNS</title>', '<title>Xerox Host DNS – Preview</title>')
  .replace('<body>', '<body>' + banner)
  .replace('<script>', '<script>\n' + mock + '\n</script>\n<script>');

fs.writeFileSync(path.join(__dirname, 'preview.html'), out);
console.log('preview.html written (' + out.length + ' bytes)');
