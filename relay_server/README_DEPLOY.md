# 🚀 คู่มือเปิดใช้งาน SeeSpeak V3 Cloud Relay Server (สถาปัตยกรรม 2B — ฟรี!)

ตัว Relay Server ทำหน้าที่เชื่อมต่อระหว่างแว่น `XIAO ESP32S3` กับ **Google Gemini Live API** ทำให้แว่นคุยสดโต้ตอบด้วยเสียงภาษาไทยได้ภายใน **~0.9–1.5 วินาที** โดยไม่กระตุกและประหยัดแบตเตอรี่

---

## วิธีที่ 1: Deploy ฟรีบน Render.com (แนะนำที่สุดสำหรับวันแข่งที่เซียร์รังสิต — ไม่ต้องเปิดคอมทิ้งไว้!)

1. อัปโหลดโฟลเดอร์ `relay_server/` ขึ้น GitHub รีโป `https://github.com/newsoyes/SeeSpeak`
2. สมัครและเข้าสู่ระบบฟรีที่ **[https://render.com](https://render.com)** (ล็อกอินด้วย GitHub ได้เลย ไม่ต้องผูกบัตรเครดิต)
3. กดปุ่ม **New +** → เลือก **Web Service** → เชื่อมต่อรีโป **`newsoyes/SeeSpeak`**
4. ตั้งค่าดังนี้:
   * **Name:** `seespeak-relay`
   * **Root Directory:** `relay_server`
   * **Runtime:** `Python 3` (หรือ `Docker`)
   * **Build Command:** `pip install -r requirements.txt`
   * **Start Command:** `uvicorn server:app --host 0.0.0.0 --port $PORT`
   * **Instance Type:** `Free`
5. กดแท็บ **Environment Variables** แล้วเพิ่มตัวแปร:
   * `GEMINI_API_KEY` = *(วาง Gemini API Key ของคุณจาก aistudio.google.com)*
   * `GEMINI_VOICE_NAME` = `Laomedeia` *(เสียงผู้หญิงวัยรุ่นน่ารัก สดใส)*
6. กด **Create Web Service** รอประมาณ 2 นาที จะได้ลิงก์เช่น `https://your-seespeak-relay.onrender.com`
   * ทดสอบเปิด `https://your-seespeak-relay.onrender.com/health` ในเบราว์เซอร์ ถ้าขึ้น `"status": "ok"` แปลว่าพร้อมใช้งาน 100%!
7. นำ URL ของ WebSocket คือ **`wss://your-seespeak-relay.onrender.com/ws/seespeak`** ไปกรอกในหน้าเว็บตั้งค่า **`SeeSpeak-Setup`** ของแว่น (ช่อง "Cloud Relay URL") แล้วกดบันทึกได้เลย!

> 💡 **เคล็ดลับวันแข่ง:** Render แพ็กเกจฟรีจะหลับถ้าไม่มีคนเรียกเกิน 15 นาที ก่อนขึ้นเวทีโชว์กรรมการให้เปิดลิงก์ `https://your-seespeak-relay.onrender.com/health` บนมือถือไว้ 1 ครั้งก่อนเปิดแว่นเพื่อให้เซิร์ฟเวอร์ตื่นพร้อมทำงานทันที!

---

## วิธีที่ 2: รันทดสอบบนโน้ตบุ๊ก/คอมพิวเตอร์ในวง Wi-Fi เดียวกัน (เร็วที่สุดตอนซ้อม!)

```bash
cd relay_server
pip install -r requirements.txt
set GEMINI_API_KEY=ใส่คีย์ของคุณที่นี่
uvicorn server:app --host 0.0.0.0 --port 8080
```
จากนั้นดู IP ของคอมพิวเตอร์ (`ipconfig`) เช่น `192.168.1.50` แล้วนำ **`ws://192.168.1.50:8080/ws/seespeak`** ไปกรอกในหน้าเว็บตั้งค่า `SeeSpeak-Setup` ของแว่นได้เลย!
