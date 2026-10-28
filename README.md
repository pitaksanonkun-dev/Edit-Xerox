# Xerox Host DNS Bridge (WT32-ETH01 / ESP32-ETH01 V1.4)

แก้ปัญหาเครื่อง **Xerox AltaLink C8155** ส่ง Scan to SMB แบบ **Address Type = Host Name** ไม่ได้
และตามไม่ทันเมื่อเครื่องคอมพิวเตอร์ปลายทางเปลี่ยน IPv4 / IPv6

## หลักการทำงาน

```
 Xerox C8155 ──DNS query "PC-ACC01"──▶ ESP32-ETH01 (DNS Server :53)
                                         │  ├─ ชื่ออยู่ในรายการ → ตอบ IP ปัจจุบันของ PC (A / AAAA)
                                         │  └─ ชื่ออื่น ๆ      → ส่งต่อไป DNS เดิมของบริษัท
                                         │
                                         └─ ตรวจหา IP ของ PC ทุก 30 วินาที ด้วย
                                            NetBIOS (137) / LLMNR (5355) / mDNS (5353)
                                            + ดักฟังการประกาศชื่อตอน PC เปิดเครื่อง
```

เครื่อง Xerox ถามหา Host Name ผ่าน DNS เท่านั้น (ไม่ใช้ NetBIOS แบบที่ Windows ใช้)
ESP32 จึงทำหน้าที่ DNS Server ที่ "รู้" ว่าตอนนี้ชื่อนั้นอยู่ที่ IP ไหน

### โหมด Relay (แนะนำ – ไม่ต้องแก้ DNS ของ Xerox)
```
 Xerox (กี่เครื่องก็ได้) ──SMB──▶ ESP32 :4451 ──▶ PC-ACC01 (IP ล่าสุด) :445
                          ──SMB──▶ ESP32 :4452 ──▶ PC-HR01  (IP ล่าสุด) :445  ← อยู่คนละวง LAN ก็ได้
```
- ESP32 ไม่เก็บไฟล์ แค่ส่งต่อข้อมูลแบบ stream (ไฟล์ใหญ่เท่าไรก็ได้) และ Login SMB ยังเป็นของ Xerox กับ PC โดยตรง
- เปิด Relay ได้สูงสุด **6 Host** และส่งพร้อมกันได้ **2 งาน** (งานที่ 3 รอคิวอัตโนมัติ) เพราะ ESP32 core มี socket ได้ 16 ตัว
- Host ที่ไม่ได้เปิด Relay ยังใช้โหมด DNS ได้ (สูงสุด 64 Host)

### ค้นหาด้วย MAC Address (แม่นยำที่สุด)
- ใส่ MAC ในหน้าเพิ่ม Host แล้ว ESP32 จะตามหาเครื่องด้วย **ARP** ซึ่ง Windows Firewall ปิดกั้นไม่ได้
  และไม่ต้องเปิด NetBIOS / LLMNR / mDNS (ใช้กับเครื่องในวง LAN เดียวกับ ESP32)
- ทุกรอบจะตรวจว่า MAC ยังอยู่ที่ IP เดิมไหม ถ้าไม่ใช่จะสแกนทั้งวงทันที (~10 วินาทีสำหรับ /24)
  และถ้า Relay ส่งไม่สำเร็จจะสแกนหาใหม่ก่อน Xerox ลองส่งซ้ำ
- เครื่องต่างวง LAN ใช้ MAC จากคำตอบ NetBIOS (ต้องเปิด NetBIOS และ Firewall อนุญาต UDP 137)
- ในหน้าเพิ่ม Host มีปุ่ม **🔍 สแกนหาเครื่องในเครือข่าย** (เลือกเครื่อง / Export JSON)
  และปุ่ม **📂 นำเข้าไฟล์ JSON** – ไฟล์ต้องมีอย่างน้อย `name` และ `mac`
- โปรแกรม **MacCollector.exe** (โฟลเดอร์ `MacCollector`) – ดับเบิลคลิกบน PC ปลายทาง แสดงการ์ดแลน/MAC/IP
  แก้ข้อมูลได้ เลือกโฟลเดอร์แล้วกด Export จะได้ไฟล์ `<ชื่อเครื่อง>.json` สำหรับนำเข้าได้ทันที
- Wi-Fi ที่เปิด "Random hardware addresses" จะเปลี่ยน MAC ได้ ควรปิด หรือใช้สาย LAN

### ปุ่ม ✔ Verify (หน้าเพิ่ม / แก้ไข Host)
ESP32 ทดสอบจริงจากตัวอุปกรณ์ทีละขั้น (ไม่ต้องกดบันทึกก่อน และไม่รบกวนงานส่งไฟล์ที่กำลังทำอยู่):
1. เครือข่ายของ ESP32 (สาย LAN / IP / Gateway)
2. ค้นหาเครื่องจาก MAC (ARP – ถ้าไม่เจอที่ IP เดิมจะสแกนทั้งวง)
3. ค้นหาเครื่องจากชื่อ (NetBIOS / LLMNR / mDNS)
4. IP ที่จะใช้ (แจ้งเตือนถ้าชื่อกับ MAC ชี้คนละ IP)
5. ESP32 → คอมพิวเตอร์: เชื่อมต่อ SMB TCP 445 (บอกสาเหตุ: ปิดพอร์ต / Firewall / เครื่องปิด)
6. ESP32 → เครื่องถ่ายเอกสารทุกเครื่อง (และแจ้งว่าเครื่องเคยถาม DNS มาที่ ESP32 หรือยัง)
7. พอร์ต Relay ว่าง / เปิดรับอยู่ / ชนกับ Host อื่น
8. ชื่อ Scan – เทียบกับสมุดที่อยู่ของเครื่องถ่ายเอกสารที่นำเข้าไว้ แล้วกรอกช่อง **ชื่อ Scan** ให้อัตโนมัติ
   (ถ้าค่าเปลี่ยนจากเดิมจะขึ้นกรอบสีเขียว – แก้เองได้ แล้วกด บันทึก)

### ชื่อ Scan จากสมุดที่อยู่ Xerox
- ที่ Xerox EWS: Address Book → Export เป็น CSV → ที่ ESP32 แท็บ **เครื่องพิมพ์ Xerox** กด **นำเข้า CSV** ของเครื่องนั้น
- อ่านเฉพาะรายการ Scan (Nickname / Server / Port / Protocol / Path) ไม่เก็บรหัสผ่าน · เก็บที่ `/book/<IP>.json`
- จับคู่เมื่อ Server = ชื่อเครื่อง / ชื่อแฝง / IP ของเครื่อง หรือ = IP ESP32 + พอร์ต Relay ของ Host นั้น
- ไฟล์ตัวอย่าง: `11\ตัวอย่าง-AddressBook-Xerox.csv`

### ส่งข้ามวง LAN (ต่าง Subnet / VLAN)
- Relay ส่งไปยัง PC ต่างวงได้ ถ้า router/firewall อนุญาตให้ ESP32 เข้าถึง TCP 445 ของ PC
- การหา IP ข้ามวง ตั้งที่แท็บ ตั้งค่า → "เครื่องปลายทางที่อยู่ต่างวง LAN":
  - **สแกน NetBIOS ใน Subnet อื่น** เช่น `192.168.30.0/24` (PC ต้องอนุญาต UDP 137 จากวงของ ESP32)
  - **ถาม DNS ของบริษัท (Active Directory)** สำหรับเครื่องที่ join domain (ใส่ Upstream DNS = DNS ของ AD และ Domain suffix)
- ถ้าวง LAN ไม่มีการ route ถึงกันเลย ให้ใช้ ESP32 วงละ 1 ตัว

## สิ่งที่ต้องมี
- บอร์ด WT32-ETH01 V1.4 + สาย LAN
- USB-TTL 3.3V (CP2102 / CH340) สำหรับอัปโหลดโปรแกรม (บอร์ดนี้ไม่มีพอร์ต USB)
- Arduino IDE 2.x + **esp32 by Espressif 3.x** (คอมไพล์ทดสอบแล้วกับ 3.3.12; โค้ดรองรับ 2.0.x ด้วยแต่ยังไม่ได้ทดสอบ) + ไลบรารี **ArduinoJson 7.x**

## วิธีอัปโหลดโปรแกรม
| USB-TTL | WT32-ETH01 |
|---|---|
| 5V | 5V |
| GND | GND |
| TX | RX0 (IO3) |
| RX | TX0 (IO1) |
| GND | IO0 (ต่อเฉพาะตอนอัปโหลด) |

1. เปิด `XeroxHostDNS/XeroxHostDNS.ino` ใน Arduino IDE
2. Board: **WT32-ETH01 Ethernet Module**, Partition Scheme: Default (มี SPIFFS/LittleFS)
3. ต่อ IO0 ลง GND แล้วกดรีเซ็ต (หรือเสียบไฟใหม่) → กด Upload
4. ปลดสาย IO0 ออกจาก GND แล้วรีเซ็ตบอร์ด
5. เปิด Serial Monitor 115200 จะเห็น IP ที่ได้ เช่น `[ETH] IP 192.168.20.10`

(ใช้ PlatformIO ก็ได้: `pio run -t upload` ที่โฟลเดอร์นี้)

## ตั้งค่าครั้งแรก
1. เปิด `http://<IP ของ ESP32>/` Login `admin` / `admin`
2. แท็บ **ตั้งค่า** → ตั้ง **Static IP** ให้ ESP32 (หรือจอง IP ใน DHCP) และเปลี่ยนรหัสผ่าน
   - **Upstream DNS** = DNS เดิมของบริษัท (เช่น Domain Controller / Router)
3. แท็บ **เครื่องปลายทาง Scan** → เพิ่ม Host = Computer name ของ Windows (เช่น `PC-ACC01`)
4. แท็บ **เครื่องพิมพ์ Xerox** → เพิ่มเครื่อง Xerox (ชื่อ + IP) จะเพิ่มกี่เครื่องก็ได้ (สูงสุด 32)

## ตั้งค่าเครื่อง Xerox AltaLink C8155 – โหมด Relay
1. ในหน้า ESP32 แท็บ **เครื่องปลายทาง Scan** แก้ไข Host แล้วใส่ **พอร์ต Relay** (เช่น 4451)
2. Xerox → **Address Book → Scan To Destination**
   - Protocol: **SMB**
   - Address Type: **IPv4 Address**
   - IP Address : Port = `IP ของ ESP32` : `4451`
   - Share / Document Path / Login Name / Password = ของเครื่อง PC ตามเดิม
   - ทุกครั้งที่แก้ไขรายการ ต้องพิมพ์ Password ใหม่และติ๊ก **Select to save new password**
     ถ้าไม่ติ๊ก Xerox จะส่งรหัสผ่านว่าง และบัญชีบน PC อาจถูกล็อก (Windows ล็อกบัญชีเมื่อใส่รหัสผิด 10 ครั้ง)
3. กด **Destination Test** แล้วดูผลที่แท็บ **บันทึก (Log) → ประวัติการส่งไฟล์ผ่าน Relay**
   ตั้งแต่ v1.1.0 ระบบอ่านหัวข้อความ SMB เพื่อแสดงชื่อไฟล์และขนาดของงานที่ส่ง (เช่น `ส่งไฟล์ DOC.pdf → scan · 80.2 KB`)
   ถ้าส่งไม่ผ่านจะแสดงสาเหตุ เช่น รหัสผ่านว่าง, บัญชีถูกล็อก, ไม่พบ Share, ไม่มีสิทธิ์เขียน
   การเชื่อมต่อสั้น ๆ ที่ Xerox ใช้ตรวจโฟลเดอร์จะไม่ถูกบันทึกแยก

> ทดสอบแล้ว 27 ต.ค. 2569: AltaLink C8155 ส่ง SMB 3.1.1 ไปพอร์ตอื่นนอกจาก 445 (4452) ผ่านตัวส่งต่อถึง PC ได้ครบ
> (ล็อกอิน, เข้า Share, เขียนไฟล์, ลบไฟล์ทดสอบ) และได้ไฟล์ Scan จริง
> ถ้า Windows ตั้ง SMB hardening ให้ตรวจชื่อเซิร์ฟเวอร์ (SmbServerNameHardeningLevel) อาจปฏิเสธการเชื่อมต่อผ่าน Relay
> ค่าเริ่มต้นของ Windows ไม่ได้เปิดไว้
> คู่มือลงโปรแกรมด้วย Arduino IDE: `คู่มือติดตั้ง-ESP32-ETH01-ArduinoIDE.pdf`

## ตั้งค่าเครื่อง Xerox AltaLink C8155 – โหมด DNS
1. Embedded Web Server → **Properties → Connectivity → Setup → Network → IPv4 (DNS)**
   ยกเลิกการรับ DNS จาก DHCP แล้วตั้ง **Preferred DNS Server = IP ของ ESP32**
   (ถ้าเปิด IPv6 ให้ปิดการรับ DNS ผ่าน DHCPv6)
2. รีสตาร์ตเครื่องพิมพ์
3. **Address Book → Scan To Destination**
   - Protocol: **SMB**
   - Address Type: **Host Name**
   - Host Name : Port = `PC-ACC01` : `445`
   - Share / Document Path / Login Name / Password ตามเดิม
4. กด **Destination Test** แล้วดูผลในแท็บ **บันทึก (Log)** ของ ESP32 จะเห็น `LOCAL → 192.168.x.x`

## ฝั่งเครื่อง Windows ปลายทาง
ต้องเปิดอย่างน้อย 1 วิธีให้ ESP32 ถามชื่อได้:
- **NetBIOS over TCP/IP** (Network adapter → IPv4 → Advanced → WINS → Enable NetBIOS) – แนะนำ
- หรือ **LLMNR** / **mDNS** (Windows 10/11 เปิดเป็นค่าเริ่มต้น ถ้าไม่ถูกปิดด้วย GPO)
- Firewall ต้องอนุญาต File and Printer Sharing (NB-Name-In UDP 137) / LLMNR (UDP 5355) / mDNS (UDP 5353)

ทดสอบจาก PC เครื่องอื่น: `nslookup PC-ACC01 <IP ของ ESP32>`

## หมายเหตุ
- ESP32 ต้องอยู่ VLAN/Subnet เดียวกับเครื่อง PC (broadcast/multicast ไม่ข้าม router)
  ถ้า PC อยู่อีก subnet ใส่ **IPv4 สำรอง** หรือ Broadcast address เพิ่มเติม (ถ้า router อนุญาต)
- ชื่อที่ไม่ได้อยู่ในรายการจะถูกส่งต่อไป Upstream DNS ทั้งหมด เครื่องพิมพ์จึงใช้งาน SMTP/NTP/ฯลฯ ได้ตามปกติ
- IP ล่าสุดของแต่ละ Host ถูกบันทึกไว้ ใช้ต่อได้หลังรีบูต
- **คืนค่าโรงงาน:** ต่อขา IO32 (CFG) ลง GND ค้างไว้ขณะเปิดเครื่อง 3 วินาที หรือกดปุ่มในหน้า ตั้งค่า
