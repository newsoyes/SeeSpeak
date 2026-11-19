// ============================================================================
// โปรเจกต์: SeeSpeak - อุปกรณ์เสริมแว่นตาอัจฉริยะสำหรับผู้พิการทางสายตา
// บอร์ด: Seeed Studio XIAO ESP32S3 Sense + กล้อง OV5640 / OV2640
// ระบบเสียง: MAX98357A I2S Amplifier + ลำโพง 8 Ohm 1W (20mm)
// ----------------------------------------------------------------------------
// ฟีเจอร์พิเศษสำหรับใช้งานจริง (ไม่ต้องแก้โค้ดเมื่อเปลี่ยนสถานที่/ผู้ใช้งาน):
//   - มีระบบจำค่า Wi-Fi และ Gemini API Key ลงหน่วยความจำถาวร (NVS Flash)
//   - หากต่อ Wi-Fi ไม่ติด หรือ กดปุ่ม 3 ครั้งติดกัน (Triple Click) เครื่องจะเปิด
//     Wi-Fi ชื่อ "SeeSpeak-Setup" พร้อมหน้าเว็บตั้งค่าอัตโนมัติ (รองรับ TalkBack/VoiceOver)
// ----------------------------------------------------------------------------
// โหมดการทำงานของปุ่มกดเดียว (ไฟแฟลช LED ติดอัตโนมัติทุกโหมดขณะถ่ายภาพ):
//   - เปิดสวิตช์เครื่อง    : สั่นต้อนรับ + เชื่อมต่อ Wi-Fi + พูดต้อนรับและบอกวิธีใช้ทุกโหมด
//   - กดปุ่ม 1 ครั้ง       : โหมดที่ 1 อ่านข้อความ หนังสือ ป้าย ฉลากยา (Smart OCR ไทย/อังกฤษ)
//   - กดปุ่ม 2 ครั้ง       : โหมดที่ 2 ดูสิ่งของและสภาพแวดล้อมด้านหน้า (Spatial Scene Description)
//   - กดปุ่มค้าง (>1 วินาที): โหมดที่ 3 ตรวจสอบและนับเงิน ทั้งธนบัตรและเหรียญไทย (พูดสรุปยอดรวมขึ้นก่อนเสมอ)
//   - กดปุ่ม 3 ครั้งติดกัน  : เปิดโหมดตั้งค่า Wi-Fi และ API Key ผ่านมือถือ ("SeeSpeak-Setup")
// ============================================================================
// การตั้งค่าใน Arduino IDE ก่อนอัปโหลด:
//   1. Board: "XIAO_ESP32S3" (ในชุด esp32 โดย Espressif Systems)
//   2. PSRAM: "OPI PSRAM" (สำคัญมาก! ต้องเปิดไม่งั้นกล้องและระบบเสียงทำงานไม่ได้)
//   3. USB CDC On Boot: "Enabled" (เพื่อให้แสดงข้อความใน Serial Monitor ผ่าน USB-C)
//   4. Partition Scheme: "Default with spiffs (3MB APP/1.5MB SPIFFS)"
//   5. ติดตั้งไลบรารี:
//      - "ArduinoJson" (โดย Benoit Blanchon)
//      - "ESP32-audioI2S" / "ESP32-audioI2S-master" (โดย Schreibfaul1 สำหรับขับเสียง I2S และ Google TTS ภาษาไทย)
// ============================================================================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include "esp_sleep.h"
#include "base64.h"
#include "Audio.h" // ไลบรารี ESP32-audioI2S สำหรับส่งเสียงออก MAX98357A

// ----------------------------------------------------------------------------
// 1. ค่าเริ่มต้นหลักของเครื่อง (ใส่ API Key ของคุณไว้ให้เรียบร้อยแล้ว ใช้งานได้ทันที)
// ----------------------------------------------------------------------------
const char* DEFAULT_WIFI_SSID      = "SeeSpeak_Hotspot";
const char* DEFAULT_WIFI_PASSWORD  = "12345678";
const char* DEFAULT_GEMINI_API_KEY = "AQ.Ab8RN6KDQo8_l4HeQmHjtfD1JbUwtojtXuPwbBubsxLXtbuzVg";

const char* SETUP_AP_NAME          = "SeeSpeak-Setup"; // ชื่อ Wi-Fi ที่แว่นปล่อยออกมาตอนตั้งค่า
const char* GEMINI_ENDPOINT        = "https://generativelanguage.googleapis.com/v1beta/models/gemini-flash-latest:generateContent?key=";

// ----------------------------------------------------------------------------
// 2. กำหนดขาพิน (Pinout) สำหรับบัดกรีบนบอร์ด XIAO ESP32S3 Sense
// ----------------------------------------------------------------------------
#define PIN_BUTTON      1   // ขา D0 (GPIO1) -> ต่อปุ่มกด Tactile Switch (อีกขาต่อ GND)
#define PIN_FLASH_LED   2   // ขา D1 (GPIO2) -> ต่อผ่าน R 220 โอห์ม เข้าขา Base ทรานซิสเตอร์ตัวที่ 1 (ขับไฟแฟลช SMD LED)
#define PIN_VIB_MOTOR   3   // ขา D2 (GPIO3) -> ต่อผ่าน R 220 โอห์ม เข้าขา Base ทรานซิสเตอร์ตัวที่ 2 (ขับมอเตอร์สั่น 10x3mm)

// ขาเชื่อมต่อโมดูลขยายเสียง MAX98357A I2S
#define I2S_BCLK        7   // ขา D8 (GPIO7) -> ต่อขา BCLK ของ MAX98357A
#define I2S_LRC         8   // ขา D9 (GPIO8) -> ต่อขา LRC ของ MAX98357A
#define I2S_DOUT        9   // ขา D10 (GPIO9) -> ต่อขา DIN ของ MAX98357A

// ขาภายในของกล้องบนบอร์ดเสริม XIAO ESP32S3 Sense (ห้ามเปลี่ยน)
#define PWDN_GPIO_NUM   -1
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM   10
#define SIOD_GPIO_NUM   40
#define SIOC_GPIO_NUM   39
#define Y9_GPIO_NUM     48
#define Y8_GPIO_NUM     11
#define Y7_GPIO_NUM     12
#define Y6_GPIO_NUM     14
#define Y5_GPIO_NUM     16
#define Y4_GPIO_NUM     18
#define Y3_GPIO_NUM     17
#define Y2_GPIO_NUM     15
#define VSYNC_GPIO_NUM  38
#define HREF_GPIO_NUM   47
#define PCLK_GPIO_NUM   13

// ----------------------------------------------------------------------------
// 3. ตัวแปรระบบ, หน่วยความจำถาวร (Preferences) และเซิร์ฟเวอร์ตั้งค่า
// ----------------------------------------------------------------------------
Audio audio;
Preferences prefs;
WebServer server(80);
DNSServer dnsServer;

String savedSSID   = "";
String savedPass   = "";
String savedApiKey = "";

bool inSetupMode = false;
unsigned long lastActivityTime = 0;
const unsigned long AUTO_SLEEP_TIMEOUT_MS   = 180000; // หากไม่กดปุ่มเกิน 3 นาที (180,000 ms) จะเข้า Deep Sleep อัตโนมัติ
const unsigned long SETUP_PORTAL_TIMEOUT_MS = 300000; // เปิดหน้าเว็บตั้งค่ารอสูงสุด 5 นาที

// คำสั่ง Prompt สำหรับทั้ง 3 โหมดหลัก (ใช้คำลงท้าย "ค่ะ" ให้ตรงกับเสียงผู้หญิงของ Google TTS)
const char* PROMPT_MODE_1_OCR =
    "คุณคือผู้ช่วยคนตาบอด อ่านข้อความภาษาไทยและอังกฤษทั้งหมดที่เห็นในภาพนี้โดยเรียงตามลำดับความสำคัญ "
    "(เช่น ชื่อป้าย ฉลากยา ราคา เมนูอาหาร หน้าหนังสือ) สรุปให้กระชับชัดเจนเป็นธรรมชาติสำหรับฟังด้วยเสียงผู้หญิง (ลงท้ายด้วยคำว่า ค่ะ) "
    "หากภาพเบลอมากหรือมีนิ้วบังกล้อง ให้พูดว่า 'ภาพไม่ชัดหรือมีนิ้วบังกล้อง กรุณากดใหม่อีกครั้งค่ะ' "
    "หากข้อความตกขอบขวาหรือซ้าย ให้แนะนำทิศทางหันหน้าด้วย ห้ามใส่เครื่องหมายดอกจันหรือ Markdown เด็ดขาด";

const char* PROMPT_MODE_2_SCENE =
    "คุณคือผู้ช่วยนำทางคนตาบอด อธิบายสิ่งของและสภาพแวดล้อมตรงหน้าในภาพนี้ โดยต้องระบุ 'ตำแหน่งทิศทาง' "
    "(ด้านซ้าย ตรงกลาง ด้านขวา) และ 'ระยะห่างโดยประมาณ' (เช่น ระยะเอื้อมมือถึง หรือห่างออกไปกี่เมตร) "
    "พูดให้กระชับไม่เกิน 2-3 ประโยคด้วยน้ำเสียงผู้หญิงที่สุภาพ (ลงท้ายด้วยคำว่า ค่ะ) ห้ามใส่เครื่องหมายดอกจันหรือ Markdown เด็ดขาด";

const char* PROMPT_MODE_3_BANKNOTE =
    "คุณคือระบบตรวจสอบเงินสำหรับคนตาบอด ตรวจสอบธนบัตรไทย (20, 50, 100, 500, 1000 บาท) และเหรียญกษาปณ์ไทย (1, 2, 5, 10 บาท) ในภาพนี้ "
    "กฎเหล็ก: ต้องพูดสรุป 'ยอดเงินรวมทั้งหมด' ขึ้นเป็นคำแรกเสมอ เช่น 'รวม 125 บาทค่ะ มีแบงค์ร้อย 1 ใบ แบงค์ยี่สิบ 1 ใบ และเหรียญห้าบาท 1 เหรียญ' "
    "หากไม่พบธนบัตรหรือเหรียญในภาพ หรือเห็นไม่ชัด ให้พูดว่า 'ไม่พบธนบัตรหรือเหรียญ หรือภาพไม่ชัดเจน กรุณาถือเงินห่างระดับอกแล้วกดค้างอีกครั้งค่ะ' "
    "ห้ามเดาสุ่มและห้ามใส่เครื่องหมายดอกจันหรือ Markdown เด็ดขาด";

// ----------------------------------------------------------------------------
// ฟังก์ชันสั่งมอเตอร์สั่นที่ขมับ (Haptic Feedback)
// ----------------------------------------------------------------------------
void vibrate(int count, int durationMs = 120, int pauseMs = 100) {
    for (int i = 0; i < count; i++) {
        digitalWrite(PIN_VIB_MOTOR, HIGH);
        delay(durationMs);
        digitalWrite(PIN_VIB_MOTOR, LOW);
        if (i < count - 1) {
            delay(pauseMs);
        }
    }
}

// ----------------------------------------------------------------------------
// ฟังก์ชันโหลดและบันทึกค่าลงหน่วยความจำถาวร (NVS Flash)
// ----------------------------------------------------------------------------
void loadSettings() {
    prefs.begin("seespeak", true); // โหมดอ่านอย่างเดียว
    savedSSID   = prefs.getString("ssid", DEFAULT_WIFI_SSID);
    savedPass   = prefs.getString("pass", DEFAULT_WIFI_PASSWORD);
    savedApiKey = prefs.getString("apikey", DEFAULT_GEMINI_API_KEY);
    prefs.end();

    if (savedSSID.length() == 0) savedSSID = DEFAULT_WIFI_SSID;
    if (savedApiKey.length() == 0) savedApiKey = DEFAULT_GEMINI_API_KEY;
}

void saveSettings(String newSSID, String newPass, String newApiKey) {
    newSSID.trim();
    newPass.trim();
    newApiKey.trim();

    prefs.begin("seespeak", false); // โหมดเขียนข้อมูล
    if (newSSID.length() > 0) {
        prefs.putString("ssid", newSSID);
    }
    prefs.putString("pass", newPass);
    if (newApiKey.length() > 0) {
        prefs.putString("apikey", newApiKey);
    }
    prefs.end();
}

// ----------------------------------------------------------------------------
// ฟังก์ชันพูดออกลำโพงภาษาไทย (ใช้เสียงผู้หญิงภาษาไทยของ Google Translate TTS)
// ----------------------------------------------------------------------------
void speakThaiChunk(String chunk) {
    chunk.trim();
    if (chunk.length() == 0) return;

    audio.connecttospeech(chunk.c_str(), "th");
    while (audio.isRunning()) {
        audio.loop();
        delay(1);
    }
}

void speakThai(String text) {
    if (WiFi.status() != WL_CONNECTED) return;

    text.replace("*", "");
    text.replace("#", "");
    text.replace("\n", " ");
    text.trim();
    if (text.length() == 0) return;

    Serial.println("[TTS พูด]: " + text);

    int maxLen = 180;
    while (text.length() > 0) {
        if (text.length() <= maxLen) {
            speakThaiChunk(text);
            break;
        }
        int splitIdx = text.lastIndexOf(' ', maxLen);
        if (splitIdx <= 0) {
            splitIdx = maxLen;
            // ถอยกลับไม่ให้ตัดกลางตัวอักษรภาษาไทยในระบบ UTF-8 (byte ต่อเนื่องขึ้นต้นด้วย 10xxxxxx)
            while (splitIdx > 0 && (((uint8_t)text[splitIdx] & 0xC0) == 0x80)) {
                splitIdx--;
            }
            if (splitIdx == 0) splitIdx = maxLen;
        }
        String sub = text.substring(0, splitIdx);
        speakThaiChunk(sub);
        text = text.substring(splitIdx);
        text.trim();
    }
    lastActivityTime = millis();
}

// ----------------------------------------------------------------------------
// หน้าเว็บตั้งค่า Wi-Fi และ API Key (ออกแบบรองรับ TalkBack / VoiceOver สำหรับคนตาบอด)
// ----------------------------------------------------------------------------
String buildSetupHtmlPage() {
    int n = WiFi.scanNetworks();
    String networkOptions = "";
    for (int i = 0; i < n; ++i) {
        String ssidItem = WiFi.SSID(i);
        ssidItem.replace("\"", "&quot;");
        networkOptions += "<button type='button' class='net-btn' onclick=\"document.getElementById('ssid').value='" + ssidItem + "';\">"
                          "📶 เลือกเครือข่าย: " + ssidItem + " (" + String(WiFi.RSSI(i)) + " dBm)</button>";
    }
    if (n <= 0) {
        networkOptions = "<p>ไม่พบเครือข่ายรอบข้าง กรุณาพิมพ์ชื่อฮอตสปอตในช่องด้านล่างโดยตรง</p>";
    }

    String maskedKey = "";
    if (savedApiKey.length() > 10) {
        maskedKey = savedApiKey.substring(0, 8) + "••••••••";
    }

    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="th">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ตั้งค่าแว่นอัจฉริยะ SeeSpeak</title>
<style>
  body { font-family: sans-serif; background: #111827; color: #F9FAFB; margin: 0; padding: 16px; line-height: 1.5; }
  .card { max-width: 480px; margin: 0 auto; background: #1F2937; padding: 22px; border-radius: 16px; border: 2px solid #374151; }
  h1 { font-size: 1.5rem; margin-top: 0; color: #60A5FA; }
  label { display: block; font-size: 1.15rem; font-weight: bold; margin-top: 16px; margin-bottom: 6px; color: #E5E7EB; }
  input[type=text], input[type=password] {
    width: 100%; padding: 14px; font-size: 1.15rem; border-radius: 10px;
    border: 2px solid #4B5563; background: #111827; color: #FFF; box-sizing: border-box;
  }
  .hint { font-size: 0.95rem; color: #9CA3AF; margin-top: 4px; }
  .btn-save {
    width: 100%; margin-top: 24px; padding: 16px; font-size: 1.25rem; font-weight: bold;
    background: #2563EB; color: #FFF; border: none; border-radius: 12px; cursor: pointer;
  }
  .btn-save:active { background: #1D4ED8; }
  .net-btn {
    width: 100%; text-align: left; padding: 12px; margin-top: 8px; font-size: 1.05rem;
    background: #374151; color: #FFF; border: 1px solid #4B5563; border-radius: 8px; cursor: pointer;
  }
  .check-row { display: flex; align-items: center; gap: 10px; margin-top: 10px; font-size: 1.05rem; }
  input[type=checkbox] { width: 22px; height: 22px; }
</style>
</head>
<body>
<main class="card" role="main" aria-label="หน้าตั้งค่าการเชื่อมต่อแว่นตาอัจฉริยะ SeeSpeak">
  <h1>👓 ตั้งค่าแว่น SeeSpeak</h1>
  <p>เชื่อมต่ออินเทอร์เน็ตและตั้งค่าระบบด้วยตัวเอง (รองรับระบบอ่านหน้าจอ TalkBack และ VoiceOver)</p>

  <form action="/save" method="POST">
    <label for="ssid">1. ชื่อ Wi-Fi หรือ ฮอตสปอตมือถือ (2.4 GHz):</label>
    <input type="text" id="ssid" name="ssid" value=")rawliteral" + savedSSID + R"rawliteral(" required aria-required="true" placeholder="พิมพ์ชื่อ Wi-Fi หรือแตะเลือกด้านล่าง">
    <div class="hint">แตะปุ่มด้านล่างเพื่อเลือกชื่อ Wi-Fi ที่ตรวจพบ หรือพิมพ์ชื่อฮอตสปอตมือถือของคุณ:</div>
    )rawliteral" + networkOptions + R"rawliteral(

    <label for="pass">2. รหัสผ่าน Wi-Fi / ฮอตสปอต:</label>
    <input type="password" id="pass" name="pass" value=")rawliteral" + savedPass + R"rawliteral(" placeholder="กรอกรหัสผ่าน Wi-Fi">
    <div class="check-row">
      <input type="checkbox" id="showpass" onchange="document.getElementById('pass').type = this.checked ? 'text' : 'password';">
      <label for="showpass" style="margin:0; font-weight:normal;">แสดงรหัสผ่านเป็นตัวอักษร</label>
    </div>

    <label for="apikey">3. Gemini API Key (เว้นว่างได้หากใช้คีย์ของเครื่อง):</label>
    <input type="text" id="apikey" name="apikey" placeholder="เว้นว่างไว้เพื่อใช้คีย์เดิมของเครื่อง">
    <div class="hint">สถานะคีย์ปัจจุบัน: )rawliteral" + (maskedKey.length() > 0 ? maskedKey : "ใช้คีย์เริ่มต้นของเครื่อง") + R"rawliteral( (ขอคีย์ฟรีได้ที่ aistudio.google.com)</div>

    <button type="submit" class="btn-save" aria-label="บันทึกการตั้งค่าและเริ่มใช้งานแว่นตา">✅ บันทึกและเริ่มใช้งานทันที</button>
  </form>
</main>
</body>
</html>
)rawliteral";
    return html;
}

void startSetupPortal() {
    inSetupMode = true;
    Serial.println("\n=== เข้าสู่โหมดตั้งค่า Wi-Fi (SeeSpeak-Setup) ===");

    // สั่นแจ้งเตือน 3 ครั้ง (สั้น-สั้น-ยาว) ให้ผู้พิการทางสายตารู้ว่าเครื่องปล่อยสัญญาณตั้งค่าแล้ว
    vibrate(2, 120, 100);
    delay(120);
    vibrate(1, 400, 0);

    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(SETUP_AP_NAME); // เปิด Wi-Fi ชื่อ "SeeSpeak-Setup" แบบไม่มีรหัสผ่าน

    IPAddress apIP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(apIP);

    // เปิด DNS Captive Portal ให้เด้งหน้าเว็บตั้งค่าอัตโนมัติเมื่อมือถือเกาะ Wi-Fi
    dnsServer.start(53, "*", apIP);

    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html; charset=utf-8", buildSetupHtmlPage());
    });

    server.on("/save", HTTP_POST, []() {
        String newSSID   = server.arg("ssid");
        String newPass   = server.arg("pass");
        String newApiKey = server.arg("apikey");

        saveSettings(newSSID, newPass, newApiKey);

        String doneHtml = R"rawliteral(
<!DOCTYPE html><html lang="th"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>บันทึกสำเร็จ</title>
<style>body{font-family:sans-serif;background:#111827;color:#FFF;padding:24px;text-align:center;}
.box{background:#065F46;padding:24px;border-radius:16px;max-width:420px;margin:20px auto;}</style></head>
<body><div class="box" role="alert"><h2>✅ บันทึกข้อมูลเรียบร้อยแล้ว!</h2>
<p>แว่นตา SeeSpeak กำลังรีสตาร์ทและเชื่อมต่อเข้ากับ Wi-Fi ของคุณภายใน 3 วินาที...</p></div></body></html>
)rawliteral";
        server.send(200, "text/html; charset=utf-8", doneHtml);
        vibrate(2, 150, 100);
        delay(2000);
        ESP.restart();
    });

    // หากมือถือเรียก URL อื่นเพื่อเช็ก Captive Portal ให้ส่งหน้าตั้งค่ากลับไปทั้งหมด
    server.onNotFound([]() {
        server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
        server.send(302, "text/plain", "");
    });

    server.begin();
    lastActivityTime = millis();
}

// ----------------------------------------------------------------------------
// ฟังก์ชันเริ่มต้นกล้อง OV5640 / OV2640
// ----------------------------------------------------------------------------
bool initCamera() {
    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode    = CAMERA_GRAB_LATEST;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    config.frame_size   = FRAMESIZE_UXGA; // 1600x1200 คมชัดสูงและส่งผ่าน Wi-Fi รวดเร็ว
    config.jpeg_quality = 10;             // คุณภาพสูง (ตัวเลขยิ่งน้อยยิ่งชัด)
    config.fb_count     = 2;

    if (!psramFound()) {
        Serial.println("คำเตือน: ตรวจไม่พบ PSRAM! กรุณาตั้งค่า Tools -> PSRAM -> OPI PSRAM");
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return false;
    }

    sensor_t* s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_brightness(s, 1);
        s->set_contrast(s, 1);
        s->set_sharpness(s, 2);
    }
    return true;
}

// ----------------------------------------------------------------------------
// ฟังก์ชันถ่ายภาพพร้อมเปิดไฟแฟลช LED ส่องสว่างทุกครั้ง (ติดทุกโหมด)
// ----------------------------------------------------------------------------
camera_fb_t* captureSmartPhoto() {
    // 1. เปิดไฟแฟลช LED ด้านหน้าเสมอเมื่อสั่งถ่ายภาพทุกโหมด
    digitalWrite(PIN_FLASH_LED, HIGH);
    delay(200);

    // 2. ทิ้งเฟรมแรก 1 เฟรมเพื่อให้เซนเซอร์กล้องปรับแสง (Auto Exposure) เข้ากับไฟแฟลช
    camera_fb_t* flush_fb = esp_camera_fb_get();
    if (flush_fb != NULL) {
        esp_camera_fb_return(flush_fb);
    }
    delay(80);

    // 3. ถ่ายภาพจริงที่จะส่งให้ AI แล้วปิดไฟแฟลชทันทีเพื่อประหยัดพลังงาน
    camera_fb_t* fb = esp_camera_fb_get();
    digitalWrite(PIN_FLASH_LED, LOW);
    return fb;
}

// ----------------------------------------------------------------------------
// ฟังก์ชันส่งภาพไปประมวลผลที่ Gemini Flash API แล้วนำคำตอบมาพูดออกลำโพง
// ----------------------------------------------------------------------------
void processWithGemini(const char* modePrompt, const char* waitingVoiceText) {
    speakThai(waitingVoiceText);

    if (WiFi.status() != WL_CONNECTED) {
        vibrate(3, 120, 100);
        return;
    }

    camera_fb_t* fb = captureSmartPhoto();
    if (!fb) {
        speakThai("ไม่สามารถถ่ายภาพได้ กรุณาลองใหม่อีกครั้งค่ะ");
        return;
    }

    if (fb->len < 6000) {
        esp_camera_fb_return(fb);
        speakThai("ภาพมืดเกินไปหรือมีนิ้วบังหน้ากล้อง กรุณาขยับมือแล้วกดใหม่อีกครั้งค่ะ");
        return;
    }

    String base64Image = base64::encode(fb->buf, fb->len);
    esp_camera_fb_return(fb);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient https;
    String url = String(GEMINI_ENDPOINT) + savedApiKey;

    if (https.begin(client, url)) {
        https.addHeader("Content-Type", "application/json");
        https.setTimeout(20000);

        String payload = "{\"contents\":[{\"parts\":["
                         "{\"text\":\"" + String(modePrompt) + "\"},"
                         "{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"" + base64Image + "\"}}"
                         "]}]}";

        int httpCode = https.POST(payload);
        if (httpCode == HTTP_CODE_OK) {
            String response = https.getString();
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, response);
            if (!error) {
                const char* aiAnswer = doc["candidates"][0]["content"]["parts"][0]["text"];
                if (aiAnswer != NULL) {
                    vibrate(1, 80, 0);
                    speakThai(String(aiAnswer));
                } else {
                    speakThai("ไม่สามารถอ่านผลลัพธ์ได้ กรุณากดใหม่อีกครั้งค่ะ");
                }
            } else {
                speakThai("เกิดข้อผิดพลาดในการแปลผลข้อมูลค่ะ");
            }
        } else {
            Serial.printf("[HTTP Error]: %d\n", httpCode);
            speakThai("ไม่สามารถเชื่อมต่อเซิร์ฟเวอร์เอไอได้ กรุณาตรวจสอบอินเทอร์เน็ตค่ะ");
        }
        https.end();
    }
}

// ----------------------------------------------------------------------------
// ฟังก์ชันตรวจจับการกดปุ่มเดียว 4 รูปแบบ:
//   Return 1 = กด 1 ครั้ง (Single Click -> อ่านข้อความ)
//   Return 2 = กด 2 ครั้ง (Double Click -> สำรวจสิ่งของและสภาพแวดล้อมตรงหน้า)
//   Return 3 = กดค้างเกิน 1 วินาที (Long Press -> ตรวจนับธนบัตรและเหรียญ)
//   Return 4 = กด 3 ครั้งติดกัน (Triple Click -> เปิดโหมดตั้งค่า Wi-Fi / API Key)
// ----------------------------------------------------------------------------
int readButtonAction() {
    if (digitalRead(PIN_BUTTON) == HIGH) {
        return 0;
    }

    unsigned long pressStart = millis();
    delay(30); // Debounce

    while (digitalRead(PIN_BUTTON) == LOW) {
        if (millis() - pressStart >= 1000) {
            vibrate(1, 350, 0);
            while (digitalRead(PIN_BUTTON) == LOW) {
                delay(10);
            }
            return 3; // กดค้าง (>1 วินาที)
        }
        delay(10);
    }

    // นับจำนวนครั้งที่กดต่อเนื่องภายในจังหวะ 400 ms
    int clickCount = 1;
    unsigned long lastRelease = millis();

    while (millis() - lastRelease < 400) {
        if (digitalRead(PIN_BUTTON) == LOW) {
            delay(30);
            while (digitalRead(PIN_BUTTON) == LOW) {
                delay(10);
            }
            clickCount++;
            lastRelease = millis();
            if (clickCount >= 3) {
                return 4; // กด 3 ครั้งติดกัน -> เข้าโหมดตั้งค่า Wi-Fi
            }
        }
        delay(10);
    }

    return clickCount; // 1 หรือ 2 ครั้ง
}

// ----------------------------------------------------------------------------
// ฟังก์ชันเข้าสู่โหมดหลับลึก (Deep Sleep) เพื่อเซฟแบตเตอรี่และช่วยให้ชาร์จแบตได้เร็ว
// ----------------------------------------------------------------------------
void enterDeepSleep() {
    if (WiFi.status() == WL_CONNECTED) {
        speakThai("เข้าสู่โหมดประหยัดพลังงาน กดปุ่มหนึ่งครั้งเพื่อปลุกเครื่องค่ะ");
    }
    vibrate(2, 80, 80);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // ตั้งให้ขา D0 (GPIO1) ปลุกบอร์ดขึ้นมาทันทีเมื่อกดปุ่ม (LOW)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BUTTON, 0);
    esp_deep_sleep_start();
}

// ----------------------------------------------------------------------------
// SETUP: ทำงานทันทีเมื่อสับสวิตช์เปิดเครื่อง หรือตื่นจากโหมด Deep Sleep
// ----------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(PIN_FLASH_LED, OUTPUT);
    pinMode(PIN_VIB_MOTOR, OUTPUT);
    digitalWrite(PIN_FLASH_LED, LOW);
    digitalWrite(PIN_VIB_MOTOR, LOW);

    // 1. สั่นต้อนรับทันทีที่เปิดเครื่องให้ผู้ใช้รู้ว่าไฟเข้าแล้ว
    vibrate(1, 180, 0);

    // 2. โหลดค่า Wi-Fi และ API Key ที่บันทึกไว้ในหน่วยความจำถาวร (NVS Flash)
    loadSettings();

    // 3. เริ่มต้นระบบเสียง I2S (MAX98357A)
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(18); // ความดังชัดเจนและไม่ดึงกระแสแบตเกินพิกัด (ปรับได้ 0 - 21)

    // 4. เริ่มต้นกล้อง
    bool camOk = initCamera();

    // 5. เชื่อมต่อ Wi-Fi ที่บันทึกไว้ (รอประมาณ 10 วินาที)
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPass.c_str());
    Serial.println("กำลังเชื่อมต่อ Wi-Fi: " + savedSSID);

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 25) {
        // หากระหว่างรอ ผู้ใช้กดปุ่มค้างไว้ ให้ข้ามไปเปิดโหมดตั้งค่า Wi-Fi ทันที
        if (digitalRead(PIN_BUTTON) == LOW) {
            delay(800);
            if (digitalRead(PIN_BUTTON) == LOW) {
                startSetupPortal();
                return;
            }
        }
        delay(400);
        Serial.print(".");
        retry++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        if (!camOk) {
            speakThai("ตรวจไม่พบโมดูลกล้อง กรุณาตรวจสอบสายแพกล้องค่ะ");
        } else {
            // เสียงต้อนรับและแนะนำวิธีใช้งานทุกโหมดทันทีที่เปิดเครื่องสำเร็จ
            vibrate(2, 100, 80);
            speakThai("สวัสดีค่ะ แว่นตาอัจฉริยะซีสปีคพร้อมใช้งาน กดปุ่มหนึ่งครั้งเพื่ออ่านข้อความ กดสองครั้งเพื่อสำรวจสิ่งของตรงหน้า และกดค้างเพื่อตรวจนับธนบัตรและเหรียญค่ะ");
        }
    } else {
        // หากเชื่อมต่อ Wi-Fi ไม่ติด (เช่น เพิ่งนำเครื่องกลับไปใช้ที่บ้านครั้งแรก)
        // ให้เปิดโหมดตั้งค่า Wi-Fi "SeeSpeak-Setup" อัตโนมัติทันที!
        startSetupPortal();
    }

    lastActivityTime = millis();
}

// ----------------------------------------------------------------------------
// LOOP: รอรับการกดปุ่มสั่งงาน หรือรับค่าจากหน้าเว็บตั้งค่า
// ----------------------------------------------------------------------------
void loop() {
    // กรณีอยู่ในโหมดตั้งค่า Wi-Fi ผ่านมือถือ ("SeeSpeak-Setup")
    if (inSetupMode) {
        dnsServer.processNextRequest();
        server.handleClient();

        // หากอยู่ในโหมดตั้งค่าแล้วกดปุ่ม 1 ครั้ง ให้รีสตาร์ทเพื่อลองเชื่อมต่อใหม่
        if (readButtonAction() == 1) {
            vibrate(1, 150, 0);
            ESP.restart();
        }

        // หากเปิดโหมดตั้งค่าทิ้งไว้เกิน 5 นาทีโดยไม่มีคนเชื่อมต่อ ให้หลับลึกเพื่อเซฟแบต
        if (millis() - lastActivityTime > SETUP_PORTAL_TIMEOUT_MS) {
            enterDeepSleep();
        }
        delay(5);
        return;
    }

    // กรณีทำงานปกติ
    audio.loop();

    int action = readButtonAction();
    if (action == 1) {
        // กด 1 ครั้ง: โหมดที่ 1 อ่านข้อความตรงหน้า (Smart OCR)
        vibrate(1, 100, 0);
        processWithGemini(PROMPT_MODE_1_OCR, "กำลังอ่านข้อความค่ะ");
    } else if (action == 2) {
        // กด 2 ครั้ง: โหมดที่ 2 สำรวจสิ่งของและทิศทางตรงหน้า
        vibrate(2, 100, 80);
        processWithGemini(PROMPT_MODE_2_SCENE, "กำลังสำรวจสิ่งตรงหน้าค่ะ");
    } else if (action == 3) {
        // กดค้าง (> 1 วินาที): โหมดที่ 3 ตรวจนับธนบัตรและเหรียญไทย
        processWithGemini(PROMPT_MODE_3_BANKNOTE, "กำลังตรวจสอบธนบัตรและเหรียญค่ะ");
    } else if (action == 4) {
        // กด 3 ครั้งติดกัน: สั่งเปิดโหมดตั้งค่า Wi-Fi / API Key ใหม่ได้ทุกเมื่อ
        speakThai("เข้าสู่โหมดตั้งค่า กรุณาเชื่อมต่อไวไฟชื่อ ซีสปีค เซ็ตอัป บนมือถือค่ะ");
        startSetupPortal();
        return;
    }

    // หากไม่ได้กดปุ่มเกิน 3 นาที ให้เข้าโหมดประหยัดพลังงาน Deep Sleep อัตโนมัติ
    if (millis() - lastActivityTime > AUTO_SLEEP_TIMEOUT_MS) {
        enterDeepSleep();
    }

    delay(10);
}
