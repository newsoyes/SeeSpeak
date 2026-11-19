// ============================================================================
// โปรเจกต์: SeeSpeak - อุปกรณ์เสริมแว่นตาอัจฉริยะสำหรับผู้พิการทางสายตา
// บอร์ด: Seeed Studio XIAO ESP32S3 Sense (พร้อมไมโครโฟนดิจิทัล PDM ในตัว)
// กล้อง: โมดูลกล้อง OV3660 ความละเอียด 3 ล้านพิกเซล (รองรับ OV2640 / OV5640 อัตโนมัติ)
// ระบบเสียง: MAX98357A I2S Amplifier + ลำโพงไมโครแบบเปิดหู (Open-Ear 8 Ohm 1W 20mm)
// น้ำหนักรวมชิ้นงานต้นแบบ: 16.5 กรัม (ช่วง 15–18 กรัม)
// ----------------------------------------------------------------------------
// ฟีเจอร์พิเศษสำหรับใช้งานจริง (ไม่ต้องแก้โค้ดเมื่อเปลี่ยนสถานที่/ผู้ใช้งาน):
//   - มีระบบจำค่า Wi-Fi และ Gemini API Key ลงหน่วยความจำถาวร (NVS Flash)
//   - หากต่อ Wi-Fi ไม่ติด หรือ กดปุ่ม 4 ครั้งติดกัน เครื่องจะเปิด
//     Wi-Fi ชื่อ "SeeSpeak-Setup" พร้อมหน้าเว็บตั้งค่าอัตโนมัติ (รองรับ TalkBack/VoiceOver)
//   - ขณะเครื่องกำลังพูดอ่านข้อความยาวๆ สามารถกดปุ่ม 1 ครั้งเพื่อ "หยุดพูดทันที" ได้
// ----------------------------------------------------------------------------
// โหมดการทำงานทั้ง 5 โหมดด้วยปุ่มกดเดียว (ตรงตามรายงานการวิจัย ว-สอศ-3 ทุกประการ):
//   - โหมดที่ 1 โหมดอ่านข้อความ (กดปุ่ม 1 ครั้ง)        : อ่านข้อความภาษาไทย/อังกฤษ ป้าย ฉลากยา เอกสาร
//   - โหมดที่ 2 โหมดอธิบายวัตถุ (กดปุ่ม 2 ครั้ง)        : อธิบายสิ่งของและทิศทางตรงหน้า (ซ้าย/กลาง/ขวา + ระยะห่าง)
//   - โหมดที่ 3 โหมดนับธนบัตร (กดปุ่ม 3 ครั้ง)         : ตรวจนับธนบัตรและเหรียญไทย (พูดสรุปยอดเงินรวมขึ้นก่อนเสมอ)
//   - โหมดที่ 4 โหมดถาม-ตอบด้วยเสียง (กดปุ่มค้างพร้อมพูด) : อัดเสียงคำถามจากไมโครโฟน PDM บนบอร์ด + ถ่ายภาพส่งให้ Gemini ตอบคำถาม
//   - โหมดที่ 5 โหมดต้อนรับ (เปิดสวิตช์เครื่อง)          : สั่นต้อนรับ + เชื่อมต่อ Wi-Fi + พูดต้อนรับและแนะนำวิธีใช้งานทุกโหมดอัตโนมัติ
//   * พิเศษ: กดปุ่ม 4 ครั้งติดกัน                       : เปิดโหมดตั้งค่า Wi-Fi และ API Key ผ่านมือถือ ("SeeSpeak-Setup")
// ============================================================================
// การตั้งค่าใน Arduino IDE ก่อนอัปโหลด:
//   1. Board: "XIAO_ESP32S3" (ในชุด esp32 โดย Espressif Systems)
//   2. PSRAM: "OPI PSRAM" (สำคัญมาก! ต้องเปิดไม่งั้นกล้อง ไมโครโฟน และระบบเสียงทำงานไม่ได้)
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
#include "esp_system.h"
#include "base64.h"
#include "Audio.h" // ไลบรารี ESP32-audioI2S สำหรับส่งเสียงออก MAX98357A
#include "offline_voice_mp3.h" // ไฟล์เสียงพูดภาษาไทยแบบออฟไลน์ในหน่วยความจำบอร์ด (ไม่ต้องใช้อินเทอร์เน็ต)

// รองรับการอ่านไมโครโฟนดิจิทัล PDM บนบอร์ด XIAO ESP32S3 Sense ทั้ง Arduino ESP32 Core v2.x และ v3.x
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  #include <ESP_I2S.h>
#else
  #include <driver/i2s.h>
#endif

// ----------------------------------------------------------------------------
// 1. ค่าเริ่มต้นหลักของเครื่อง (ใส่ API Key ไว้ให้เรียบร้อยแล้ว ใช้งานได้ทันที)
// ----------------------------------------------------------------------------
const char* DEFAULT_WIFI_SSID      = "SeeSpeak_Hotspot";
const char* DEFAULT_WIFI_PASSWORD  = "12345678";
const char* DEFAULT_GEMINI_API_KEY = "AQ.Ab8RN6KDQo8_l4HeQmHjtfD1JbUwtojtXuPwbBubsxLXtbuzVg";

const char* SETUP_AP_NAME = "SeeSpeak-Setup"; // ชื่อ Wi-Fi ที่แว่นปล่อยออกมาตอนตั้งค่า

// [Speed #1] รายการโมเดลความเร็วสูงสุดพร้อมคำสั่ง Effort ต่ำสุด (ปิดการคิดวิเคราะห์ซับซ้อน ตอบทันที)
// และระบบจำโมเดลที่ตอบสำเร็จล่าสุด (activeModelIdx) เพื่อไม่ให้เสียเวลายิงซ้ำตัวที่ไม่ตอบ
const int GEMINI_MODEL_COUNT = 3;
const char* GEMINI_ENDPOINTS[GEMINI_MODEL_COUNT] = {
    "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash-lite:generateContent?key=",
    "https://generativelanguage.googleapis.com/v1beta/models/gemini-3-flash-preview:generateContent?key=",
    "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key="
};
const char* GEMINI_THINKING_CONFIGS[GEMINI_MODEL_COUNT] = {
    "\"thinkingConfig\":{\"thinkingBudget\":0},",           // Effort ต่ำสุดสำหรับ Gemini 2.5 Flash-Lite (0 Thinking Tokens)
    "\"thinkingConfig\":{\"thinkingLevel\":\"minimal\"},",  // Effort ต่ำสุดสำหรับ Gemini 3 Flash Preview
    "\"thinkingConfig\":{\"thinkingBudget\":0},"            // Effort ต่ำสุดสำหรับ Gemini 2.5 Flash
};
int activeModelIdx = 0; // จำโมเดลที่ใช้งานได้เร็วที่สุดอัตโนมัติ

// ตั้งเป็น 0 เพื่อข้ามเสียงพูด "กำลัง..." ก่อนถ่ายภาพ (ลดเวลารอได้ ~2 วินาทีและถ่ายทันใจก่อนมือสั่น)
// หากอยากให้เครื่องพูดนำก่อนถ่ายภาพ ให้เปลี่ยนเป็น 1
#define SPEAK_WAITING_VOICE 0

// ----------------------------------------------------------------------------
// 2. กำหนดขาพิน (Pinout) สำหรับบัดกรีบนบอร์ด XIAO ESP32S3 Sense
// ----------------------------------------------------------------------------
#define PIN_BUTTON      1   // ขา D0 (GPIO1) -> ต่อปุ่มกด Tactile Switch (อีกขาต่อ GND)
#define PIN_FLASH_LED   2   // ขา D1 (GPIO2) -> ต่อผ่าน R 220 โอห์ม เข้าขา Base ทรานซิสเตอร์ตัวที่ 1 (ขับไฟแฟลช SMD LED)
#define PIN_VIB_MOTOR   3   // ขา D2 (GPIO3) -> ต่อผ่าน R 220 โอห์ม เข้าขา Base ทรานซิสเตอร์ตัวที่ 2 (ขับมอเตอร์สั่น 10x3mm)
#define PIN_BAT_ADC     4   // ขา D3 (GPIO4/A3) -> (ทางเลือก) ต่อวงจรแบ่งแรงดันวัดแบตจริง หากไม่ต่อสาย ระบบจะใช้ตัวคำนวณพลังงานอัจฉริยะแทนอัตโนมัติ

// ขาเชื่อมต่อโมดูลขยายเสียง MAX98357A I2S (ใช้พอร์ต I2S_NUM_1 เพื่อไม่ให้ชนกับไมโครโฟน PDM บน I2S_NUM_0)
#define I2S_BCLK        7   // ขา D8 (GPIO7) -> ต่อขา BCLK ของ MAX98357A
#define I2S_LRC         8   // ขา D9 (GPIO8) -> ต่อขา LRC ของ MAX98357A
#define I2S_DOUT        9   // ขา D10 (GPIO9) -> ต่อขา DIN ของ MAX98357A

// ขาภายในของไมโครโฟนดิจิทัล PDM (MSM261D3526H1CPM) บนบอร์ดเสริม XIAO ESP32S3 Sense
#define PIN_PDM_CLK          42    // ขา CLK ของไมโครโฟน PDM บนบอร์ด Sense
#define PIN_PDM_DATA         41    // ขา DATA ของไมโครโฟน PDM บนบอร์ด Sense
#define MIC_SAMPLE_RATE      16000 // ความถี่สุ่มสัญญาณไมโครโฟน PDM
#define VOICE_WAV_RATE       8000  // ความถี่ไฟล์เสียง WAV ที่กรองความถี่สูงและย่อขนาดส่งขึ้น Gemini (ประหยัด RAM 50% และส่งไวขึ้น)
#define MIC_MAX_MS           8000  // [Speed #5] อัดเสียงตราบเท่าที่กดปุ่มค้างไว้ (ปล่อยปุ่มปุ๊บส่งปั๊บทันที สูงสุดไม่เกิน 8 วินาที)

// ขาภายในของโมดูลกล้อง OV3660 บนบอร์ดเสริม XIAO ESP32S3 Sense (ห้ามเปลี่ยน)
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

// ปรับทิศทางภาพของโมดูลกล้อง OV3660 ให้ตรงกับการวางโมดูลในเคส 3D (ค่าที่ใช้ได้: 0 หรือ 1)
#define CAMERA_VFLIP    0
#define CAMERA_HMIRROR  1

// ----------------------------------------------------------------------------
// 3. ตัวแปรระบบ, หน่วยความจำถาวร (Preferences) และเซิร์ฟเวอร์ตั้งค่า
// ----------------------------------------------------------------------------
// ใช้ I2S พอร์ต 1 (I2S_NUM_1) สำหรับขับลำโพง MAX98357A เพื่อให้ I2S_NUM_0 ว่างสำหรับไมโครโฟน PDM เสมอ
Audio audio(I2S_NUM_1);
Preferences prefs;
WebServer server(80);
DNSServer dnsServer;

// [Speed #2] เปิดท่อ SSL/HTTPS ค้างไว้ (Persistent Keep-Alive Connection) ไม่ต้องเสียเวลา Handshake ใหม่ทุกครั้งที่กดปุ่ม
WiFiClientSecure geminiClient;
HTTPClient geminiHttp;
bool geminiClientInitialized = false;

String savedSSID   = "";
String savedPass   = "";
String savedApiKey = "";
const int DEFAULT_SPEAKER_VOLUME = 18; // ความดังเริ่มต้น (ระดับปานกลาง 18 จากสูงสุด 21)
int savedVolume    = DEFAULT_SPEAKER_VOLUME;

bool inSetupMode = false;
bool speechInterrupted = false;
unsigned long lastActivityTime = 0;
unsigned long lastBatteryTickMs = 0;
const unsigned long AUTO_SLEEP_TIMEOUT_MS   = 180000; // หากไม่กดปุ่มเกิน 3 นาที (180,000 ms) จะเข้า Deep Sleep อัตโนมัติ
const unsigned long SETUP_PORTAL_TIMEOUT_MS = 300000; // เปิดหน้าเว็บตั้งค่ารอสูงสุด 5 นาที

// ตัวแปรระบบตรวจจับและแจ้งเตือนแบตเตอรี่ใกล้หมด 3 ชั้น (เก็บค่าข้ามโหมด Deep Sleep ด้วย RTC_DATA_ATTR)
RTC_DATA_ATTR float rtcUsedMah         = 0.0f;  // พลังงานแบตเตอรี่สะสมที่ใช้ไป (mAh) สำหรับแบตเตอรี่ LiPo 130 mAh
RTC_DATA_ATTR bool  rtcLowBatWarned    = false; // แจ้งเตือนแบตใกล้หมดระดับที่ 1 (เหลือ ~20%) ไปแล้วหรือยัง
RTC_DATA_ATTR bool  rtcCritBatWarned   = false; // แจ้งเตือนแบตใกล้หมดระดับวิกฤต (เหลือ <8%) ไปแล้วหรือยัง
bool hasHardwareBatAdc                 = false; // ตรวจพบวงจรแบ่งแรงดันที่ขา D3 หรือไม่ (เช็กอัตโนมัติตอนเปิดเครื่อง)
const float BATTERY_WARN_MAH           = 90.0f; // เตือน "แบตเตอรี่ใกล้หมด กรุณาชาร์จแบต" เมื่อใช้พลังงานสะสมถึง 90 mAh (จาก 110 mAh ที่ใช้งานได้จริง)
const float BATTERY_CRIT_MAH           = 105.0f;// ระดับวิกฤตก่อนตัดเข้า Deep Sleep เพื่อถนอมแบตเตอรี่ LiPo 401030

// คำสั่ง Prompt สำหรับทั้ง 4 โหมดหลักที่ส่งไปยัง Gemini API (ใช้คำลงท้าย "ค่ะ" ให้ตรงกับเสียงผู้หญิงของ Google TTS)
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
    "คุณคือระบบตรวจสอบเงินสำหรับคนตาบอด ตรวจสอบธนบัตรไทย (20, 50, 100, 500, 1000 บาท) โดยดูที่สี ซึ่งสีเขียว คือแบงค์ 20 สีฟ้าคือแบงค์ 50 สีแดงคือแบงค์ 100 สีม่วง คือแบงค์ 500 และสุดท้าย สีเทาหรือน้ำตาลอ่อน คือแบงค์ 1000 และเหรียญกษาปณ์ไทย (1, 2, 5, 10 บาท) ในภาพนี้ "
    "กฎเหล็ก: ต้องพูดสรุป 'ยอดเงินรวมทั้งหมด' ขึ้นเป็นคำแรกเสมอ เช่น 'รวม 125 บาทค่ะ มีแบงค์ร้อย 1 ใบ แบงค์ยี่สิบ 1 ใบ และเหรียญห้าบาท 1 เหรียญ' "
    "หากไม่พบธนบัตรหรือเหรียญในภาพ หรือเห็นไม่ชัด ให้พูดว่า 'ไม่พบธนบัตรหรือเหรียญ หรือภาพไม่ชัดเจน กรุณาถือเงินห่างระดับอกแล้วกดสามครั้งใหม่อีกครั้งค่ะ' "
    "ห้ามเดาสุ่มและห้ามใส่เครื่องหมายดอกจันหรือ Markdown เด็ดขาด";

const char* PROMPT_MODE_4_QA =
    "คุณคือผู้ช่วยอัจฉริยะสำหรับคนตาบอด ฟังคำถามภาษาไทยของผู้ใช้จากไฟล์เสียงที่แนบมานี้ "
    "แล้วตอบคำถามโดยดูจากภาพถ่ายตรงหน้าอย่างถูกต้อง กระชับ ชัดเจน เข้าใจง่ายภายใน 1-3 ประโยค "
    "ด้วยน้ำเสียงผู้หญิงที่สุภาพ (ลงท้ายด้วยคำว่า ค่ะ) หากเสียงเบาหรือฟังคำถามไม่ถนัด ให้อธิบายรายละเอียดสำคัญของสิ่งที่อยู่ในภาพและตอบสิ่งที่ผู้ใช้ต้องการทราบมากที่สุด "
    "ห้ามใส่เครื่องหมายดอกจันหรือ Markdown เด็ดขาด";

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
    savedVolume = prefs.getInt("vol", DEFAULT_SPEAKER_VOLUME);
    prefs.end();

    if (savedSSID.length() == 0) savedSSID = DEFAULT_WIFI_SSID;
    if (savedApiKey.length() == 0) savedApiKey = DEFAULT_GEMINI_API_KEY;
    if (savedVolume < 6 || savedVolume > 21) savedVolume = DEFAULT_SPEAKER_VOLUME;
}

void saveSettings(String newSSID, String newPass, String newApiKey, int newVolume) {
    newSSID.trim();
    newPass.trim();
    newApiKey.trim();
    if (newVolume < 6) newVolume = 6;
    if (newVolume > 21) newVolume = 21;

    prefs.begin("seespeak", false); // โหมดเขียนข้อมูล
    if (newSSID.length() > 0) {
        prefs.putString("ssid", newSSID);
    }
    prefs.putString("pass", newPass);
    if (newApiKey.length() > 0) {
        prefs.putString("apikey", newApiKey);
    }
    prefs.putInt("vol", newVolume);
    prefs.end();

    savedVolume = newVolume;
    audio.setVolume(savedVolume);
}

// ----------------------------------------------------------------------------
// ฟังก์ชันพูดออกลำโพงภาษาไทย (รองรับการกดปุ่ม 1 ครั้งเพื่อหยุดพูดกลางคัน)
// ----------------------------------------------------------------------------
void speakThaiChunk(String chunk) {
    chunk.trim();
    if (chunk.length() == 0 || speechInterrupted) return;

    unsigned long chunkStart = millis();
    audio.connecttospeech(chunk.c_str(), "th");
    while (audio.isRunning()) {
        audio.loop();
        // หากพูดไปแล้วเกิน 400 ms และผู้ใช้กดปุ่ม 1 ครั้ง ให้หยุดพูดทันทีเพื่อพร้อมรับคำสั่งใหม่
        if (millis() - chunkStart > 400 && digitalRead(PIN_BUTTON) == LOW) {
            delay(30);
            if (digitalRead(PIN_BUTTON) == LOW) {
                audio.stopSong();
                speechInterrupted = true;
                vibrate(1, 80, 0);
                while (digitalRead(PIN_BUTTON) == LOW) {
                    delay(10);
                }
                break;
            }
        }
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
    speechInterrupted = false;

    // [Speed #4] เทคนิค Fast First-Sentence TTS:
    // ก้อนแรกสุดให้ตัดประโยคสั้นๆ (~90-135 bytes หรือวรรคแรก เช่น "รวม 120 บาทค่ะ") ส่งให้ Google TTS พูดออกลำโพงทันที
    // เพื่อให้ผู้ใช้ได้ยินเสียงคำตอบแรกเร็วที่สุด จากนั้นก้อนถัดไปส่งครั้งละ 360 bytes ตามปกติ
    bool isFirstChunk = true;
    while (text.length() > 0 && !speechInterrupted) {
        int maxLen = isFirstChunk ? 120 : 360;
        if (text.length() <= maxLen) {
            speakThaiChunk(text);
            break;
        }
        int splitIdx = text.lastIndexOf(' ', maxLen);
        if (isFirstChunk) {
            // หากมีเว้นวรรคจบวลีแรกตั้งแต่ 24 bytes ขึ้นไป ให้ตัดพูดวลีแรกออกลำโพงก่อนทันที
            int firstSpace = text.indexOf(' ', 24);
            if (firstSpace > 0 && firstSpace <= 150) {
                splitIdx = firstSpace;
            }
        }
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
        isFirstChunk = false;
        text = text.substring(splitIdx);
        text.trim();
    }
    lastActivityTime = millis();
}

// ----------------------------------------------------------------------------
// หน้าเว็บตั้งค่า Wi-Fi, API Key และความดังลำโพง (พร้อมรูปแว่น SeeSpeak แบบออฟไลน์)
// ออกแบบรองรับ TalkBack / VoiceOver สำหรับผู้พิการทางสายตาและสวยงามสำหรับกรรมการ
// ----------------------------------------------------------------------------
String buildSetupHtmlPage() {
    int n = WiFi.scanNetworks();
    String networkOptions = "";
    for (int i = 0; i < n; ++i) {
        String ssidItem = WiFi.SSID(i);
        ssidItem.replace("\"", "&quot;");
        networkOptions += "<button type='button' class='net-btn' onclick=\"document.getElementById('ssid').value='" + ssidItem + "';\">"
                          "📶 เลือกเครือข่าย: <b>" + ssidItem + "</b> <span style='color:#93C5FD;font-size:0.9rem;'>(" + String(WiFi.RSSI(i)) + " dBm)</span></button>";
    }
    if (n <= 0) {
        networkOptions = "<p class='hint'>ไม่พบเครือข่ายรอบข้าง กรุณาพิมพ์ชื่อฮอตสปอตในช่องด้านบนโดยตรง</p>";
    }

    String maskedKey = "";
    if (savedApiKey.length() > 10) {
        maskedKey = savedApiKey.substring(0, 8) + "••••••••";
    }

    String volStr = String(savedVolume);

    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="th">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ตั้งค่าแว่นอัจฉริยะ SeeSpeak</title>
<link rel="icon" type="image/png" href="/seespeak.png">
<style>
  * { box-sizing: border-box; }
  body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    background: radial-gradient(circle at top, #1E293B 0%, #0F172A 100%);
    color: #F8FAFC; margin: 0; padding: 16px 12px 36px; line-height: 1.5;
  }
  .card {
    position: relative; overflow: hidden;
    max-width: 480px; margin: 0 auto;
    background: rgba(30, 41, 59, 0.92);
    padding: 22px 20px 26px; border-radius: 22px;
    border: 1.5px solid rgba(96, 165, 250, 0.28);
    box-shadow: 0 20px 45px rgba(0, 0, 0, 0.55);
  }
  /* ลวดลายพื้นหลังรูปแว่นจางๆ ด้านหลังการ์ด */
  .bg-watermark {
    position: absolute; right: -28px; bottom: 110px; width: 230px;
    opacity: 0.05; pointer-events: none; transform: rotate(-10deg);
  }
  .hero-box {
    position: relative; text-align: center;
    background: linear-gradient(145deg, rgba(37, 99, 235, 0.22), rgba(15, 23, 42, 0.75));
    border: 1px solid rgba(147, 197, 253, 0.25);
    border-radius: 18px; padding: 16px 12px 14px; margin-bottom: 18px;
    overflow: hidden;
  }
  .hero-glow {
    position: absolute; top: 50%; left: 50%; width: 200px; height: 110px;
    background: radial-gradient(circle, rgba(56, 189, 248, 0.38) 0%, transparent 72%);
    transform: translate(-50%, -55%); pointer-events: none;
  }
  @keyframes floatGlasses {
    0%, 100% { transform: translateY(0px); }
    50% { transform: translateY(-5px); }
  }
  .hero-img {
    position: relative; width: 220px; max-width: 82%; height: auto;
    filter: drop-shadow(0 12px 18px rgba(0, 0, 0, 0.55));
    animation: floatGlasses 3.6s ease-in-out infinite;
  }
  .badge-pill {
    display: inline-flex; align-items: center; gap: 6px;
    background: rgba(37, 99, 235, 0.35); border: 1px solid rgba(96, 165, 250, 0.45);
    color: #BAE6FD; font-size: 0.82rem; font-weight: 600;
    padding: 4px 12px; border-radius: 999px; margin-top: 6px;
  }
  .title-row {
    display: flex; align-items: center; justify-content: center; gap: 10px; margin: 8px 0 4px;
  }
  .mini-icon {
    width: 36px; height: auto; background: rgba(255,255,255,0.1);
    padding: 4px; border-radius: 8px; border: 1px solid rgba(255,255,255,0.18);
  }
  h1 { font-size: 1.45rem; margin: 0; color: #60A5FA; letter-spacing: 0.2px; }
  .subtitle { font-size: 0.95rem; color: #CBD5E1; margin: 4px 0 0; }
  label {
    display: block; font-size: 1.1rem; font-weight: bold;
    margin-top: 18px; margin-bottom: 6px; color: #F1F5F9;
  }
  input[type=text], input[type=password] {
    width: 100%; padding: 13px 14px; font-size: 1.1rem; border-radius: 12px;
    border: 2px solid #475569; background: #0F172A; color: #FFF;
    transition: border-color 0.2s;
  }
  input[type=text]:focus, input[type=password]:focus {
    outline: none; border-color: #38BDF8;
  }
  .hint { font-size: 0.92rem; color: #94A3B8; margin-top: 5px; }
  .net-btn {
    width: 100%; text-align: left; padding: 11px 14px; margin-top: 7px; font-size: 1rem;
    background: #334155; color: #F8FAFC; border: 1px solid #475569;
    border-radius: 10px; cursor: pointer; transition: background 0.15s;
  }
  .net-btn:active { background: #2563EB; }
  .check-row { display: flex; align-items: center; gap: 10px; margin-top: 10px; font-size: 1rem; }
  input[type=checkbox] { width: 22px; height: 22px; accent-color: #3B82F6; }

  /* ส่วนเลือกความดังลำโพง (เบา / ปานกลาง / ดังมากสำหรับเดินริมถนน) */
  .vol-box {
    background: rgba(15, 23, 42, 0.75); border: 1.5px solid #334155;
    border-radius: 14px; padding: 14px; margin-top: 6px;
  }
  .vol-grid {
    display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-bottom: 12px;
  }
  .vol-preset {
    padding: 11px 6px; border-radius: 11px; border: 2px solid #475569;
    background: #1E293B; color: #E2E8F0; font-size: 0.95rem; font-weight: bold;
    cursor: pointer; text-align: center; transition: all 0.15s;
  }
  .vol-preset small { display: block; font-weight: normal; font-size: 0.78rem; color: #94A3B8; margin-top: 2px; }
  .vol-preset.active {
    background: rgba(37, 99, 235, 0.35); border-color: #38BDF8; color: #FFF;
    box-shadow: 0 0 12px rgba(56, 189, 248, 0.3);
  }
  .vol-preset.active small { color: #BAE6FD; }
  .slider-row { display: flex; align-items: center; gap: 12px; }
  input[type=range] {
    flex: 1; height: 8px; accent-color: #38BDF8; cursor: pointer;
  }
  .vol-val-badge {
    min-width: 78px; text-align: center; background: #1E3A8A; color: #BAE6FD;
    padding: 5px 8px; border-radius: 8px; font-weight: bold; font-size: 0.92rem;
  }

  .btn-save {
    width: 100%; margin-top: 24px; padding: 16px; font-size: 1.2rem; font-weight: bold;
    background: linear-gradient(135deg, #2563EB 0%, #0284C7 100%);
    color: #FFF; border: none; border-radius: 14px; cursor: pointer;
    box-shadow: 0 8px 20px rgba(37, 99, 235, 0.4);
  }
  .btn-save:active { transform: scale(0.99); }
</style>
</head>
<body>
<main class="card" role="main" aria-label="หน้าตั้งค่าการเชื่อมต่อและความดังเสียงของแว่นตาอัจฉริยะ SeeSpeak">
  <img src="/seespeak.png" alt="" class="bg-watermark" aria-hidden="true">

  <div class="hero-box">
    <div class="hero-glow"></div>
    <img src="/seespeak.png" alt="รูปอุปกรณ์เสริมแว่นตาอัจฉริยะ SeeSpeak" class="hero-img">
    <div class="title-row">
      <img src="/seespeak.png" alt="" class="mini-icon" aria-hidden="true">
      <h1>ตั้งค่าแว่น SeeSpeak</h1>
    </div>
    <p class="subtitle">อุปกรณ์เสริมแว่นอัจฉริยะสำหรับผู้พิการทางสายตา</p>
    <div class="badge-pill">✨ รองรับ TalkBack &amp; VoiceOver • น้ำหนักเบา 16.5 กรัม</div>
  </div>

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

    <label for="vol">3. ระดับความดังลำโพงเสียงพูด (จำค่าถาวร):</label>
    <div class="vol-box" role="group" aria-label="เลือกระดับความดังลำโพง">
      <div class="vol-grid">
        <button type="button" id="btnVolLow" class="vol-preset" onclick="setVol(12)" aria-label="ความดังระดับเบา สำหรับห้องเงียบ">
          🔈 เบา<small>ห้องเงียบ (12)</small>
        </button>
        <button type="button" id="btnVolMid" class="vol-preset" onclick="setVol(18)" aria-label="ความดังระดับปานกลาง แนะนำทั่วไป">
          🔉 ปานกลาง<small>แนะนำ (18)</small>
        </button>
        <button type="button" id="btnVolHigh" class="vol-preset" onclick="setVol(21)" aria-label="ความดังระดับดังมาก สำหรับเดินริมถนน">
          🔊 ดังมาก<small>เดินริมถนน (21)</small>
        </button>
      </div>
      <div class="slider-row">
        <input type="range" id="vol" name="vol" min="6" max="21" value=")rawliteral" + volStr + R"rawliteral(" oninput="syncVolUI(this.value)" onchange="previewVol(this.value)" aria-label="แถบเลื่อนปรับความดังลำโพงตั้งแต่ 6 ถึง 21">
        <span id="volLabel" class="vol-val-badge">ระดับ )rawliteral" + volStr + R"rawliteral(/21</span>
      </div>
      <div class="hint">แตะปุ่ม <b>เบา / ปานกลาง / ดังมาก</b> หรือเลื่อนแถบเพื่อปรับความดังตามสภาพแวดล้อม</div>
    </div>

    <label for="apikey">4. Gemini API Key (เว้นว่างได้หากใช้คีย์ของเครื่อง):</label>
    <input type="text" id="apikey" name="apikey" placeholder="เว้นว่างไว้เพื่อใช้คีย์เดิมของเครื่อง">
    <div class="hint">สถานะคีย์ปัจจุบัน: )rawliteral" + (maskedKey.length() > 0 ? maskedKey : "ใช้คีย์เริ่มต้นของเครื่อง") + R"rawliteral(</div>

    <button type="submit" class="btn-save" aria-label="บันทึกการตั้งค่าทั้งหมดและเริ่มใช้งานแว่นตา">✅ บันทึกและเริ่มใช้งานทันที</button>
  </form>
</main>

<script>
function syncVolUI(val) {
  val = parseInt(val, 10);
  document.getElementById('vol').value = val;
  var desc = (val <= 14) ? 'เบา' : (val <= 19 ? 'ปานกลาง' : 'ดังมาก');
  document.getElementById('volLabel').innerText = desc + ' (' + val + '/21)';
  document.getElementById('btnVolLow').className  = 'vol-preset' + (val <= 14 ? ' active' : '');
  document.getElementById('btnVolMid').className  = 'vol-preset' + (val > 14 && val <= 19 ? ' active' : '');
  document.getElementById('btnVolHigh').className = 'vol-preset' + (val >= 20 ? ' active' : '');
}
function previewVol(val) {
  fetch('/set_vol?vol=' + encodeURIComponent(val)).catch(function(){});
}
function setVol(val) {
  syncVolUI(val);
  previewVol(val);
}
syncVolUI()rawliteral" + volStr + R"rawliteral();
</script>
</body>
</html>
)rawliteral";
    return html;
}

// ----------------------------------------------------------------------------
// ฟังก์ชันเล่นเสียงพูดภาษาไทยแบบออฟไลน์จากหน่วยความจำบอร์ด (ไม่ต้องใช้อินเทอร์เน็ต/Wi-Fi)
//   - /setup.mp3  : "เข้าสู่การตั้งค่าไวไฟ โดยเข้าไปที่ไวไฟ ซีสปีค เซ็ตอัป ค่ะ"
//   - /lowbat.mp3 : "แบตเตอรี่ใกล้หมด กรุณาชาร์จแบตค่ะ"
//   - /nowifi.mp3 : "ไม่สามารถเชื่อมต่ออินเทอร์เน็ตได้ กรุณาตรวจสอบไวไฟค่ะ"
// ----------------------------------------------------------------------------
void playOfflineVoice(const char* virtualPath, const char* logText) {
    static fs::FS offlineMemFS(std::make_shared<SeeSpeakMemFSImpl>());
    Serial.println(String("[Offline TTS พูด]: ") + logText);

    unsigned long startMs = millis();
    if (audio.connecttoFS(offlineMemFS, virtualPath)) {
        while (audio.isRunning()) {
            audio.loop();
            if (inSetupMode) {
                dnsServer.processNextRequest();
                server.handleClient();
            }
            // หากพูดไปแล้วเกิน 400 ms และผู้ใช้กดปุ่ม 1 ครั้ง ให้หยุดพูดได้ทันที
            if (millis() - startMs > 400 && digitalRead(PIN_BUTTON) == LOW) {
                delay(30);
                if (digitalRead(PIN_BUTTON) == LOW) {
                    audio.stopSong();
                    while (digitalRead(PIN_BUTTON) == LOW) {
                        delay(10);
                    }
                    break;
                }
            }
            delay(1);
        }
    }
    lastActivityTime = millis();
}

void playOfflineSetupVoice() {
    playOfflineVoice("/setup.mp3", "เข้าสู่การตั้งค่าไวไฟ โดยเข้าไปที่ไวไฟ ซีสปีค เซ็ตอัป ค่ะ");
}

void playOfflineLowBatteryVoice() {
    // สั่นเตือนจังหวะพิเศษ (สั้น-ยาว-สั้น) แล้วพูดเตือนแบตเตอรี่ใกล้หมดแบบออฟไลน์
    vibrate(1, 120, 80);
    delay(80);
    vibrate(1, 350, 80);
    delay(80);
    vibrate(1, 120, 0);
    playOfflineVoice("/lowbat.mp3", "แบตเตอรี่ใกล้หมด กรุณาชาร์จแบตค่ะ");
}

void playOfflineNoWiFiVoice() {
    vibrate(2, 140, 100);
    playOfflineVoice("/nowifi.mp3", "ไม่สามารถเชื่อมต่ออินเทอร์เน็ตได้ กรุณาตรวจสอบไวไฟค่ะ");
}

// ----------------------------------------------------------------------------
// ระบบตรวจสอบและแจ้งเตือนแบตเตอรี่ใกล้หมดอัจฉริยะ 3 ชั้น (3-Layer Smart Battery Monitor)
//   ชั้นที่ 1: ตรวจจับไฟตก Brownout ของชิป ESP32-S3 เมื่อแบตอ่อนตอนดึงกระแสสูง
//   ชั้นที่ 2: คำนวณพลังงานสะสม (mAh Fuel Gauge ใน RTC Memory) โดยไม่ต้องบัดกรีสายเพิ่ม
//   ชั้นที่ 3: รองรับการวัดแรงดันจริงผ่านขา D3 (GPIO4) อัตโนมัติหากต่อตัวต้านทานแบ่งแรงดัน
// ----------------------------------------------------------------------------
void initBatteryMonitor() {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_POWERON) {
        // เมื่อเปิดสวิตช์เครื่องใหม่หลังจากชาร์จแบต ให้รีเซ็ตตัวนับพลังงานสะสม
        rtcUsedMah       = 0.0f;
        rtcLowBatWarned  = false;
        rtcCritBatWarned = false;
    }

    // ตรวจสอบอัตโนมัติว่ามีการต่อวงจรแบ่งแรงดันเข้าที่ขา D3 (GPIO4) หรือไม่
    // โดยเปิด INPUT_PULLDOWN ชั่วครู่: หากไม่ได้ต่อสาย ค่าจะถูกดึงลงใกล้ 0 mV (< 500 mV) จึงไม่เกิดการเตือนมั่วจากขาลอย
    pinMode(PIN_BAT_ADC, INPUT_PULLDOWN);
    delay(10);
    uint32_t sumMv = 0;
    for (int i = 0; i < 8; i++) {
        sumMv += analogReadMilliVolts(PIN_BAT_ADC);
        delay(2);
    }
    uint32_t avgMv = sumMv / 8;
    if (avgMv >= 1200 && avgMv <= 2350) {
        hasHardwareBatAdc = true;
        pinMode(PIN_BAT_ADC, INPUT);
        Serial.printf("[Battery] ตรวจพบวงจรวัดแรงดันแบตเตอรี่ที่ขา D3 (แรงดันแบตประมาณ %.2f V)\n", (avgMv * 2.0f) / 1000.0f);
    } else {
        hasHardwareBatAdc = false;
        Serial.println("[Battery] ใช้ระบบคำนวณพลังงานแบตเตอรี่อัจฉริยะ (Smart Fuel Gauge 130 mAh + Brownout Guard)");
    }
    lastBatteryTickMs = millis();
}

uint32_t readHardwareBatteryMv() {
    if (!hasHardwareBatAdc) return 4000;
    uint32_t sumMv = 0;
    for (int i = 0; i < 8; i++) {
        sumMv += analogReadMilliVolts(PIN_BAT_ADC);
    }
    return (sumMv / 8) * 2; // คูณ 2 สำหรับวงจรแบ่งแรงดัน 1:2
}

void enterDeepSleep(); // Forward declaration

void checkBatteryAndWarn() {
    unsigned long now = millis();
    if (now - lastBatteryTickMs >= 5000) {
        float elapsedHours = (now - lastBatteryTickMs) / 3600000.0f;
        lastBatteryTickMs = now;
        // กินกระแสขณะเปิดสแตนด์บายเชื่อมต่อ Wi-Fi ประมาณ 65 mA
        rtcUsedMah += 65.0f * elapsedHours;
    }

    bool isLowBat  = (rtcUsedMah >= BATTERY_WARN_MAH);
    bool isCritBat = (rtcUsedMah >= BATTERY_CRIT_MAH);

    if (hasHardwareBatAdc) {
        uint32_t vBatMv = readHardwareBatteryMv();
        if (vBatMv > 2500 && vBatMv <= 3480) isLowBat = true;
        if (vBatMv > 2500 && vBatMv <= 3320) isCritBat = true;
    }

    if (isCritBat && !rtcCritBatWarned) {
        rtcCritBatWarned = true;
        rtcLowBatWarned  = true;
        Serial.printf("[Battery] แบตเตอรี่อยู่ในระดับวิกฤต (ใช้ไป %.1f mAh) -> พูดเตือนและเข้าโหมดประหยัดพลังงาน\n", rtcUsedMah);
        playOfflineLowBatteryVoice();
        enterDeepSleep();
    } else if (isLowBat && !rtcLowBatWarned) {
        rtcLowBatWarned = true;
        Serial.printf("[Battery] แบตเตอรี่ใกล้หมด (ใช้ไป %.1f mAh) -> พูดแจ้งเตือนผู้ใช้\n", rtcUsedMah);
        playOfflineLowBatteryVoice();
    }
}

// ----------------------------------------------------------------------------
// ฟังก์ชันตรวจสอบและต่อ Wi-Fi ใหม่อัตโนมัติหากสัญญาณฮอตสปอตหลุดกลางคัน
// ----------------------------------------------------------------------------
bool ensureWiFiConnected() {
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }
    Serial.println("[Wi-Fi] สัญญาณหลุด กำลังพยายามเชื่อมต่อใหม่อัตโนมัติ...");
    WiFi.disconnect();
    WiFi.begin(savedSSID.c_str(), savedPass.c_str());

    unsigned long waitStart = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - waitStart < 5500)) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[Wi-Fi] เชื่อมต่อกลับคืนสำเร็จ!");
        return true;
    }

    // หากต่อไม่ติด ให้พูดแจ้งเตือนด้วยเสียงออฟไลน์ทันที (ไม่ปล่อยให้เครื่องเงียบ)
    playOfflineNoWiFiVoice();
    return false;
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

    // ส่งรูปแว่น SeeSpeak พื้นหลังใสจากหน่วยความจำบอร์ด (PROGMEM) ให้หน้าเว็บแสดงผลแบบออฟไลน์
    server.on("/seespeak.png", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "public, max-age=3600");
        server.send_P(200, "image/png", (const char*)SEESPEAK_LOGO_PNG, SEESPEAK_LOGO_PNG_LEN);
    });

    // ปรับความดังลำโพงทันทีเมื่อผู้ใช้แตะเลือกปุ่ม เบา / ปานกลาง / ดังมาก บนหน้าเว็บพร้อมสั่นตอบรับเบาๆ
    server.on("/set_vol", HTTP_GET, []() {
        if (server.hasArg("vol")) {
            int v = server.arg("vol").toInt();
            if (v >= 6 && v <= 21) {
                savedVolume = v;
                audio.setVolume(savedVolume);
                vibrate(1, 45, 0);
            }
        }
        server.send(200, "text/plain", "OK");
    });

    server.on("/save", HTTP_POST, []() {
        String newSSID   = server.arg("ssid");
        String newPass   = server.arg("pass");
        String newApiKey = server.arg("apikey");
        int newVol       = server.hasArg("vol") ? server.arg("vol").toInt() : savedVolume;

        saveSettings(newSSID, newPass, newApiKey, newVol);

        String doneHtml = R"rawliteral(
<!DOCTYPE html><html lang="th"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>บันทึกสำเร็จ</title>
<style>body{font-family:sans-serif;background:#0F172A;color:#FFF;padding:24px;text-align:center;}
.box{background:linear-gradient(145deg,#065F46,#047857);padding:26px 20px;border-radius:20px;max-width:420px;margin:20px auto;border:1.5px solid #34D399;box-shadow:0 15px 35px rgba(0,0,0,0.5);}
.hero{width:170px;height:auto;margin-bottom:10px;filter:drop-shadow(0 8px 14px rgba(0,0,0,0.45));}</style></head>
<body><div class="box" role="alert">
<img src="/seespeak.png" alt="SeeSpeak" class="hero">
<h2>✅ บันทึกข้อมูลเรียบร้อยแล้ว!</h2>
<p>บันทึกชื่อ Wi-Fi และระดับความดังลำโพงลงหน่วยความจำถาวรสำเร็จ<br>แว่นตา SeeSpeak กำลังรีสตาร์ทและเชื่อมต่อเข้ากับ Wi-Fi ของคุณภายใน 3 วินาที...</p></div></body></html>
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

    // พูดแจ้งเตือนออกลำโพงแบบออฟไลน์ทันที (ไม่ต้องมีอินเทอร์เน็ตก็พูดได้)
    playOfflineSetupVoice();
    lastActivityTime = millis();
}

// ----------------------------------------------------------------------------
// ฟังก์ชันเริ่มต้นโมดูลกล้อง OV3660 (ความละเอียด 3 ล้านพิกเซล)
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
    // [Speed #3] ใช้ความละเอียด XGA (1024x768) คมชัดสูงสำหรับอ่านฉลากยาและธนบัตร แต่ขนาดไฟล์เล็กลงกว่าครึ่ง ส่งขึ้น Gemini ไวขึ้นมาก
    config.frame_size   = FRAMESIZE_XGA;
    config.jpeg_quality = 12;             // ค่า 10-12 เหมาะสุดสำหรับ OV3660 (ภาพคม ไฟล์ไม่ใหญ่เกิน ส่ง Gemini ไว)
    config.fb_count     = 2;              // ใช้ 2 บัฟเฟอร์ใน PSRAM เพื่อป้องกันเฟรมล้น

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
        Serial.printf("[Camera] ตรวจพบเซนเซอร์โมดูลกล้อง PID: 0x%04X\n", s->id.PID);
        if (s->id.PID == OV3660_PID) {
            // ปรับจูนเฉพาะสำหรับโมดูลกล้อง OV3660 ความละเอียด 3MP ตามงานอ่านข้อความและนับธนบัตร
            s->set_vflip(s, CAMERA_VFLIP);          // OV3660 ค่าเริ่มต้นภาพจะกลับหัวในแนวตั้ง
            s->set_hmirror(s, CAMERA_HMIRROR);      // ตั้งค่าสลับด้านซ้าย-ขวาให้ตรงความจริง
            s->set_brightness(s, 1);                // เพิ่มความสว่างเล็กน้อย ช่วยอ่านรายละเอียดในเงามืด
            s->set_contrast(s, 1);                  // เพิ่มคอนทราสต์ให้ตัวหนังสือชัดขึ้น
            s->set_saturation(s, -2);               // ลดสีสดเกินจริงของ OV3660 (ไม่ให้สีธนบัตรแดง/ส้มเพี้ยน)
            s->set_sharpness(s, 1);                 // ความคมระดับ 1 กำลังดี
            s->set_gainceiling(s, GAINCEILING_4X);  // จำกัด Noise ในที่มืด
            s->set_whitebal(s, 1);                  // เปิด Auto White Balance
            s->set_awb_gain(s, 1);                  // เปิด AWB Gain ปรับโทนสีเข้ากับไฟแฟลช LED
            s->set_exposure_ctrl(s, 1);             // เปิด Auto Exposure
            s->set_aec2(s, 1);                      // เปิด Night/DSP AE ช่วยวัดแสงในร่มร่วมกับไฟแฟลชได้แม่นยำขึ้น
        } else {
            s->set_vflip(s, CAMERA_VFLIP);
            s->set_hmirror(s, CAMERA_HMIRROR);
            s->set_brightness(s, 1);
            s->set_contrast(s, 1);
            s->set_sharpness(s, 1);
        }
    }
    return true;
}

// ----------------------------------------------------------------------------
// ฟังก์ชันถ่ายภาพจากกล้อง OV3660 พร้อมเปิดไฟแฟลช LED ส่องสว่างทุกครั้ง
// ----------------------------------------------------------------------------
camera_fb_t* captureSmartPhoto() {
    // 1. เปิดไฟแฟลช LED ด้านหน้าเสมอเมื่อสั่งถ่ายภาพทุกโหมด
    digitalWrite(PIN_FLASH_LED, HIGH);
    delay(120);

    // 2. ทิ้งเฟรมค้างในบัฟเฟอร์ 2 เฟรม เพื่อให้โมดูลกล้อง OV3660 ปรับแสง (AE/AWB) เข้ากับไฟแฟลช
    for (int i = 0; i < 2; i++) {
        camera_fb_t* flush_fb = esp_camera_fb_get();
        if (flush_fb != NULL) {
            esp_camera_fb_return(flush_fb);
        }
        delay(40);
    }

    // 3. ถ่ายภาพจริงที่จะส่งให้ AI แล้วปิดไฟแฟลชทันทีเพื่อประหยัดพลังงาน
    camera_fb_t* fb = esp_camera_fb_get();
    digitalWrite(PIN_FLASH_LED, LOW);
    return fb;
}

// ----------------------------------------------------------------------------
// ฟังก์ชันสร้างส่วนหัวของไฟล์เสียง WAV (44 bytes, 8kHz, 16-bit, Mono PCM)
// ----------------------------------------------------------------------------
void writeWavHeader(uint8_t* header, uint32_t pcmDataSize, uint32_t sampleRate = VOICE_WAV_RATE) {
    uint32_t fileSize   = pcmDataSize + 36;
    uint16_t numCh      = 1;
    uint16_t bitsPerSmp = 16;
    uint32_t byteRate   = sampleRate * numCh * (bitsPerSmp / 8);
    uint16_t blockAlign = numCh * (bitsPerSmp / 8);

    memcpy(header, "RIFF", 4);
    memcpy(header + 4, &fileSize, 4);
    memcpy(header + 8, "WAVEfmt ", 8);
    uint32_t subchunk1Size = 16;
    uint16_t audioFormat   = 1; // PCM
    memcpy(header + 16, &subchunk1Size, 4);
    memcpy(header + 20, &audioFormat, 2);
    memcpy(header + 22, &numCh, 2);
    memcpy(header + 24, &sampleRate, 4);
    memcpy(header + 28, &byteRate, 4);
    memcpy(header + 32, &blockAlign, 2);
    memcpy(header + 34, &bitsPerSmp, 2);
    memcpy(header + 36, "data", 4);
    memcpy(header + 40, &pcmDataSize, 4);
}

// ----------------------------------------------------------------------------
// ฟังก์ชันอัดเสียงคำถามของผู้ใช้จากไมโครโฟนดิจิทัล PDM บนบอร์ด XIAO ESP32S3 Sense
// พร้อมตัวกรองเฉลี่ยความถี่ต่ำ (2-Sample Low-Pass Filter 16kHz -> 8kHz) ลดเสียงซ่าและประหยัด RAM 50%
// คืนค่าเป็น String ที่เข้ารหัส Base64 ของไฟล์ WAV พร้อมส่งให้ Gemini API
// ----------------------------------------------------------------------------
String recordQuestionWavBase64() {
    const uint32_t maxOutPcmBytes = (VOICE_WAV_RATE * 2 * MIC_MAX_MS) / 1000;
    const uint32_t wavBufSize     = 44 + maxOutPcmBytes;

    uint8_t* wavBuf = (uint8_t*)ps_malloc(wavBufSize);
    if (!wavBuf) {
        wavBuf = (uint8_t*)malloc(wavBufSize);
    }
    if (!wavBuf) {
        Serial.println("[PDM Mic] ไม่สามารถจองหน่วยความจำสำหรับอัดเสียงได้");
        return "";
    }

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    I2SClass pdmMic;
    pdmMic.setPinsPdmRx(PIN_PDM_CLK, PIN_PDM_DATA);
    if (!pdmMic.begin(I2S_MODE_PDM_RX, MIC_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        Serial.println("[PDM Mic] เริ่มต้นไมโครโฟน PDM ไม่สำเร็จ (Core v3)");
        free(wavBuf);
        return "";
    }
#else
    i2s_config_t i2s_config = {};
    i2s_config.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    i2s_config.sample_rate          = MIC_SAMPLE_RATE;
    i2s_config.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_config.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
    i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_config.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    i2s_config.dma_buf_count        = 8;
    i2s_config.dma_buf_len          = 256;
    i2s_config.use_apll             = false;

    i2s_pin_config_t pin_config = {};
    pin_config.mck_io_num   = I2S_PIN_NO_CHANGE;
    pin_config.bck_io_num   = I2S_PIN_NO_CHANGE;
    pin_config.ws_io_num    = PIN_PDM_CLK;
    pin_config.data_out_num = I2S_PIN_NO_CHANGE;
    pin_config.data_in_num  = PIN_PDM_DATA;

    if (i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL) != ESP_OK) {
        free(wavBuf);
        return "";
    }
    if (i2s_set_pin(I2S_NUM_0, &pin_config) != ESP_OK) {
        i2s_driver_uninstall(I2S_NUM_0);
        free(wavBuf);
        return "";
    }
#endif

    // ทิ้งข้อมูลช่วง ~120 ms แรกเพื่อตัดเสียง Pop ตอนเริ่มเปิดไมโครโฟน PDM
    int16_t rawBlock[512]; // 512 ตัวอย่าง (1024 bytes) ที่ 16kHz
    size_t bytesRead = 0;
    for (int i = 0; i < 6; i++) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
        bytesRead = pdmMic.readBytes((char*)rawBlock, sizeof(rawBlock));
#else
        i2s_read(I2S_NUM_0, rawBlock, sizeof(rawBlock), &bytesRead, pdMS_TO_TICKS(50));
#endif
    }

    Serial.println("[PDM Mic] เริ่มบันทึกเสียงคำถาม... (พูดขณะกดปุ่มค้างไว้ได้เลย ปล่อยปุ่มเมื่อพูดจบ)");
    unsigned long recStart = millis();
    uint32_t totalPcmBytes = 0;
    int16_t* outSamplesPtr = (int16_t*)(wavBuf + 44);

    // [Speed #5] บันทึกเสียงตามการกดค้างและปล่อยปุ่มของผู้ใช้โดยตรง (True Push-to-Talk):
    // ตราบใดที่ยังกดปุ่มค้างอยู่ (LOW) จะอัดเสียงต่อเนื่อง และทันทีที่ปล่อยปุ่ม (HIGH) จะหยุดอัดและส่งรูป+เสียงไปถาม Gemini ทันที!
    while (totalPcmBytes + 512 <= maxOutPcmBytes) {
        unsigned long elapsed = millis() - recStart;
        if (digitalRead(PIN_BUTTON) == HIGH) {
            delay(15); // Debounce การปล่อยปุ่ม
            if (digitalRead(PIN_BUTTON) == HIGH) {
                break;
            }
        }
        if (elapsed >= MIC_MAX_MS) {
            break;
        }

        bytesRead = 0;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
        bytesRead = pdmMic.readBytes((char*)rawBlock, sizeof(rawBlock));
#else
        i2s_read(I2S_NUM_0, rawBlock, sizeof(rawBlock), &bytesRead, pdMS_TO_TICKS(100));
#endif
        if (bytesRead >= 4) {
            // 1. คำนวณค่าเฉลี่ย DC Offset ของบล็อก
            size_t sampleCount = bytesRead / 2;
            int32_t dcSum = 0;
            for (size_t s = 0; s < sampleCount; s++) {
                dcSum += rawBlock[s];
            }
            int32_t dcOffset = (sampleCount > 0) ? (dcSum / (int32_t)sampleCount) : 0;

            // 2. กรองความถี่สูง (Average 2 ตัวอย่างติดกัน 16kHz -> 8kHz) และขยายเสียงพูด (Gain x4)
            size_t outIdx = totalPcmBytes / 2;
            for (size_t s = 0; s + 1 < sampleCount; s += 2) {
                int32_t s1 = (int32_t)rawBlock[s]     - dcOffset;
                int32_t s2 = (int32_t)rawBlock[s + 1] - dcOffset;
                int32_t amplified = ((s1 + s2) / 2) * 4;
                if (amplified > 32767) amplified = 32767;
                if (amplified < -32768) amplified = -32768;
                outSamplesPtr[outIdx++] = (int16_t)amplified;
            }
            totalPcmBytes = outIdx * 2;
        }
    }

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    pdmMic.end();
#else
    i2s_driver_uninstall(I2S_NUM_0);
#endif

    Serial.printf("[PDM Mic] บันทึกเสียงเสร็จสิ้น: %u bytes (%lu ms)\n", totalPcmBytes, millis() - recStart);
    if (totalPcmBytes < 800) {
        free(wavBuf);
        return "";
    }

    writeWavHeader(wavBuf, totalPcmBytes, VOICE_WAV_RATE);
    String base64Wav = base64::encode(wavBuf, 44 + totalPcmBytes);
    free(wavBuf);
    return base64Wav;
}

// ----------------------------------------------------------------------------
// [Speed #2] ฟังก์ชันย่อยสำหรับยิง HTTPS POST ไปยัง Gemini API แบบ Keep-Alive (Reuse SSL Session)
// ----------------------------------------------------------------------------
int postGeminiRequest(const char* endpointBase, const String& payload, String& responseOut) {
    if (!geminiClientInitialized) {
        geminiClient.setInsecure();
        geminiClient.setTimeout(12);
        geminiClientInitialized = true;
    }

    geminiHttp.setReuse(true); // เปิดโหมด Keep-Alive ใช้ท่อ SSL เดิมซ้ำในการกดปุ่มครั้งถัดไป
    geminiHttp.setTimeout(12000);

    String url = String(endpointBase) + savedApiKey;
    if (!geminiHttp.begin(geminiClient, url)) {
        return -100;
    }

    geminiHttp.addHeader("Content-Type", "application/json");
    geminiHttp.addHeader("Connection", "keep-alive");

    int httpCode = geminiHttp.POST(payload);
    // หากท่อ Keep-Alive เดิมหมดเวลาหลังจากไม่ได้กดปุ่มนาน ให้เปิดท่อใหม่และส่งซ้ำทันที 1 ครั้ง
    if (httpCode < 0) {
        geminiHttp.end();
        geminiClient.stop();
        if (geminiHttp.begin(geminiClient, url)) {
            geminiHttp.addHeader("Content-Type", "application/json");
            geminiHttp.addHeader("Connection", "keep-alive");
            httpCode = geminiHttp.POST(payload);
        }
    }

    if (httpCode > 0) {
        responseOut = geminiHttp.getString();
    } else {
        geminiHttp.end();
    }
    return httpCode;
}

// ----------------------------------------------------------------------------
// ฟังก์ชันประกอบ JSON Payload ตามรุ่นโมเดล (จับคู่คำสั่ง Effort ต่ำสุดให้ตรงกับแต่ละโมเดล)
// ----------------------------------------------------------------------------
void buildGeminiPayload(String& payloadOut, const char* modePrompt, const String& base64Image, const String& base64AudioWav, int modelIdx) {
    payloadOut = "";
    payloadOut.reserve(base64Image.length() + base64AudioWav.length() + 1024);
    payloadOut = "{\"contents\":[{\"parts\":["
                 "{\"text\":\"";
    payloadOut += modePrompt;
    payloadOut += "\"},";
    if (base64AudioWav.length() > 0) {
        payloadOut += "{\"inline_data\":{\"mime_type\":\"audio/wav\",\"data\":\"";
        payloadOut += base64AudioWav;
        payloadOut += "\"}},";
    }
    payloadOut += "{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"";
    payloadOut += base64Image;
    payloadOut += "\"}}]}],"
                  "\"generationConfig\":{";
    payloadOut += GEMINI_THINKING_CONFIGS[modelIdx];
    payloadOut += "\"maxOutputTokens\":130,"
                  "\"temperature\":0.2"
                  "}}";
}

// ----------------------------------------------------------------------------
// ฟังก์ชันส่งภาพ (และเสียงคำถามในโหมดที่ 4) ไปประมวลผลที่ Gemini API แล้วนำคำตอบมาพูดออกลำโพง
// ----------------------------------------------------------------------------
void processWithGemini(const char* modePrompt, const char* waitingVoiceText, String base64AudioWav = "") {
    unsigned long tStart = millis();

    if (!ensureWiFiConnected()) {
        return;
    }

    // บันทึกพลังงานที่ใช้ในรอบการถ่ายภาพ + เปิดไฟแฟลช + ส่งข้อมูล AI + ขับลำโพง (~1.2 mAh ต่อครั้ง)
    rtcUsedMah += (base64AudioWav.length() > 0) ? 1.5f : 1.2f;

#if SPEAK_WAITING_VOICE
    if (waitingVoiceText != NULL && strlen(waitingVoiceText) > 0) {
        speakThai(waitingVoiceText);
    }
#else
    (void)waitingVoiceText;
#endif

    camera_fb_t* fb = captureSmartPhoto();
    if (!fb) {
        speakThai("ไม่สามารถถ่ายภาพได้ กรุณาลองใหม่อีกครั้งค่ะ");
        return;
    }

    if (fb->len < 4500) {
        Serial.printf("[Camera] ภาพเล็ก/มืดเกินไป (%u bytes)\n", fb->len);
        esp_camera_fb_return(fb);
        speakThai("ภาพมืดเกินไปหรือมีนิ้วบังหน้ากล้อง กรุณาขยับมือแล้วกดใหม่อีกครั้งค่ะ");
        return;
    }
    Serial.printf("[Camera] ถ่ายภาพสำเร็จ ขนาด %u bytes\n", fb->len);

    // สั่นสั้นๆ 1 ทีให้ผู้ใช้รู้ว่าถ่ายภาพเสร็จแล้ว สามารถขยับมือ/ศีรษะได้
    vibrate(1, 60, 0);

    String base64Image = base64::encode(fb->buf, fb->len);
    esp_camera_fb_return(fb);

    unsigned long tBeforeApi = millis();
    String payload;
    String response;
    int httpCode = -1;

    // [Speed #1] ยิงไปที่โมเดลที่ตอบเร็วที่สุดเป็นลำดับแรก (พร้อมจำโมเดลที่สำเร็จไว้ใช้ในครั้งถัดไปทันที)
    for (int attempt = 0; attempt < GEMINI_MODEL_COUNT; attempt++) {
        int idx = (activeModelIdx + attempt) % GEMINI_MODEL_COUNT;
        buildGeminiPayload(payload, modePrompt, base64Image, base64AudioWav, idx);
        response = "";
        httpCode = postGeminiRequest(GEMINI_ENDPOINTS[idx], payload, response);
        if (httpCode == HTTP_CODE_OK) {
            if (activeModelIdx != idx) {
                Serial.printf("[Gemini] ล็อกโมเดลความเร็วสูงตัวที่ %d เป็นตัวหลักอัตโนมัติ\n", idx);
                activeModelIdx = idx;
            }
            break;
        }
        Serial.printf("[Gemini] โมเดลลำดับ %d ติดขัด (HTTP %d) กำลังสลับโมเดลถัดไป...\n", idx, httpCode);
    }

    // คืนหน่วยความจำทันทีหลังส่งเสร็จ
    payload = "";
    base64Image = "";
    base64AudioWav = "";
    unsigned long tAfterApi = millis();
    Serial.printf("[Timing] เตรียมข้อมูล: %lu ms | รอ Gemini ตอบ: %lu ms\n",
                  tBeforeApi - tStart, tAfterApi - tBeforeApi);

    if (httpCode == HTTP_CODE_OK) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, response);
        if (!error) {
            String aiAnswer;
            JsonArray parts = doc["candidates"][0]["content"]["parts"];
            for (JsonObject part : parts) {
                if (!part["thought"].as<bool>() && part["text"].is<const char*>()) {
                    aiAnswer += part["text"].as<const char*>();
                }
            }
            aiAnswer.trim();

            if (aiAnswer.length() > 0) {
                speakThai(aiAnswer);
            } else {
                speakThai("ไม่สามารถอ่านผลลัพธ์ได้ กรุณากดใหม่อีกครั้งค่ะ");
            }
        } else {
            Serial.printf("[JSON Error]: %s\n", error.c_str());
            speakThai("เกิดข้อผิดพลาดในการแปลผลข้อมูลค่ะ");
        }
    } else {
        Serial.printf("[HTTP Error]: %d\n", httpCode);
        if (response.length() > 0) {
            Serial.println("[HTTP Body]: " + response);
        }
        if (httpCode == 429) {
            speakThai("ใช้งานถี่เกินไปชั่วคราว กรุณารอสักครู่แล้วกดใหม่ค่ะ");
        } else {
            speakThai("ไม่สามารถเชื่อมต่อเซิร์ฟเวอร์เอไอได้ กรุณาตรวจสอบอินเทอร์เน็ตค่ะ");
        }
    }
    // หลังจบการประมวลผลและพูดคำตอบแล้ว ให้เช็กสถานะแบตเตอรี่และเตือนหากแบตเตอรี่ใกล้หมด
    checkBatteryAndWarn();
}

// ----------------------------------------------------------------------------
// ฟังก์ชันดำเนินงานโหมดที่ 4: โหมดถาม-ตอบด้วยเสียง (Voice Q&A ผ่านไมโครโฟน PDM + กล้อง OV3660)
// ----------------------------------------------------------------------------
void handleVoiceQAMode() {
    if (!ensureWiFiConnected()) {
        return;
    }

    // สั่นเตือนสั้นกระชับ 1 ครั้งให้ผู้พิการทางสายตารู้ว่าไมโครโฟนเริ่มฟังแล้ว พูดขณะกดปุ่มค้างไว้ได้เลย
    vibrate(1, 150, 0);

    String wavBase64 = recordQuestionWavBase64();
    if (wavBase64.length() == 0) {
        Serial.println("[Voice Q&A] ไม่ได้รับข้อมูลเสียงจากไมโครโฟน PDM ใช้โหมดอธิบายภาพแทน");
    }

    processWithGemini(PROMPT_MODE_4_QA, "กำลังประมวลผลคำถามค่ะ", wavBase64);
}

// ----------------------------------------------------------------------------
// ฟังก์ชันตรวจจับการกดปุ่มเดียวสำหรับทุกรูปแบบการทำงาน:
//   Return 1 = กดปุ่ม 1 ครั้ง (โหมดที่ 1: โหมดอ่านข้อความ - Smart OCR)
//   Return 2 = กดปุ่ม 2 ครั้ง (โหมดที่ 2: โหมดอธิบายวัตถุและทิศทางตรงหน้า)
//   Return 3 = กดปุ่ม 3 ครั้ง (โหมดที่ 3: โหมดนับธนบัตรไทย)
//   Return 4 = กดปุ่มค้างไว้ >= 0.75 วินาที (โหมดที่ 4: โหมดถาม-ตอบด้วยเสียงผ่านไมโครโฟน PDM)
//   Return 5 = กดปุ่ม 4 ครั้งติดกัน (เปิดโหมดตั้งค่า Wi-Fi / API Key "SeeSpeak-Setup")
// ----------------------------------------------------------------------------
int readButtonAction() {
    if (digitalRead(PIN_BUTTON) == HIGH) {
        return 0;
    }

    unsigned long pressStart = millis();
    delay(30); // Debounce

    while (digitalRead(PIN_BUTTON) == LOW) {
        if (millis() - pressStart >= 750) {
            // เมื่อกดค้างถึง 0.75 วินาที ให้เข้าสู่โหมดที่ 4 (ถาม-ตอบด้วยเสียง) ทันทีขณะที่ผู้ใช้ยังกดปุ่มและพูดอยู่
            return 4;
        }
        delay(10);
    }

    // นับจำนวนครั้งที่กดต่อเนื่องภายในหน้าต่างเวลา 420 ms
    int clickCount = 1;
    unsigned long lastRelease = millis();

    while (millis() - lastRelease < 420) {
        if (digitalRead(PIN_BUTTON) == LOW) {
            delay(30);
            while (digitalRead(PIN_BUTTON) == LOW) {
                delay(10);
            }
            clickCount++;
            lastRelease = millis();
            if (clickCount >= 4) {
                return 5; // กด 4 ครั้งติดกัน -> เข้าโหมดตั้งค่า Wi-Fi ("SeeSpeak-Setup")
            }
        }
        delay(10);
    }

    return clickCount; // คืนค่า 1, 2 หรือ 3 ครั้ง
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
// SETUP: ทำงานทันทีเมื่อสับสวิตช์เปิดเครื่อง (โหมดที่ 5: โหมดต้อนรับ) หรือตื่นจาก Deep Sleep
// ----------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(PIN_FLASH_LED, OUTPUT);
    pinMode(PIN_VIB_MOTOR, OUTPUT);
    digitalWrite(PIN_FLASH_LED, LOW);
    digitalWrite(PIN_VIB_MOTOR, LOW);

    // เริ่มต้นระบบเสียง I2S (MAX98357A บนพอร์ต I2S_NUM_1) ก่อนเพื่อให้พูดเตือนฉุกเฉินแบบออฟไลน์ได้ตั้งแต่ต้น
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(18); // ความดังชัดเจนและไม่ดึงกระแสแบตเกินพิกัด (ปรับได้ 0 - 21)

    // ชั้นที่ 1: หากบอร์ดรีเซ็ตเพราะไฟแบตเตอรี่ตก (Brownout Detector ทำงานเนื่องจากแบตอ่อนมาก)
    // ให้พูดเตือน "แบตเตอรี่ใกล้หมด กรุณาชาร์จแบตค่ะ" แบบออฟไลน์ทันทีแล้วตัดเข้าโหมดหลับลึกเพื่อไม่ให้เครื่องรีบูตวนซ้ำ
    if (esp_reset_reason() == ESP_RST_BROWNOUT) {
        Serial.println("[Battery] ตรวจพบไฟแบตเตอรี่ตก (Brownout) -> พูดเตือนแบตเตอรี่ใกล้หมดและพักเครื่อง");
        audio.setVolume(15);
        playOfflineLowBatteryVoice();
        WiFi.mode(WIFI_OFF);
        esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BUTTON, 0);
        esp_deep_sleep_start();
        return;
    }

    bool wokeFromSleep = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0);

    // 1. สั่นต้อนรับทันทีที่เปิดเครื่องให้ผู้ใช้รู้ว่าไฟเข้าแล้ว
    vibrate(1, 180, 0);

    // หากตื่นจากโหมดประหยัดพลังงาน (Deep Sleep) ด้วยการกดปุ่ม ให้รอจนผู้ใช้ปล่อยนิ้วออกจากปุ่มก่อน
    // เพื่อป้องกันไม่ให้การกดปลุกเครื่องถูกเข้าใจผิดว่าเป็นการกดค้างเข้าโหมดตั้งค่า Wi-Fi
    if (digitalRead(PIN_BUTTON) == LOW) {
        unsigned long releaseWait = millis();
        while (digitalRead(PIN_BUTTON) == LOW && (millis() - releaseWait < 1200)) {
            delay(20);
        }
    }

    // 2. เริ่มต้นระบบตรวจจับแบตเตอรี่ 3 ชั้น และโหลดค่า Wi-Fi / API Key / ความดังลำโพง จาก NVS Flash
    initBatteryMonitor();
    loadSettings();
    audio.setVolume(savedVolume); // ใช้ระดับความดังลำโพงที่ผู้ใช้บันทึกไว้ในหน่วยความจำถาวร (NVS)

    // 3. เริ่มต้นโมดูลกล้อง OV3660
    bool camOk = initCamera();

    // 4. เชื่อมต่อ Wi-Fi ที่บันทึกไว้ (รอประมาณ 10 วินาที) พร้อมเปิดระบบต่อใหม่อัตโนมัติ
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // ปิดโหมดพัก Wi-Fi เพื่อให้ส่งภาพ/เสียงขึ้น Gemini และสตรีมเสียง TTS ได้ต่อเนื่อง
    WiFi.setAutoReconnect(true);
    WiFi.begin(savedSSID.c_str(), savedPass.c_str());
    Serial.println("กำลังเชื่อมต่อ Wi-Fi: " + savedSSID);

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 25) {
        // หากระหว่างรอ ผู้ใช้ตั้งใจกดปุ่มค้างไว้เกิน 1.2 วินาที ให้ข้ามไปเปิดโหมดตั้งค่า Wi-Fi ทันที
        if (digitalRead(PIN_BUTTON) == LOW) {
            delay(1200);
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
        } else if (wokeFromSleep) {
            // กรณีตื่นจากโหมดพักเครื่อง (Deep Sleep) พูดสั้นกระชับเพื่อให้พร้อมใช้งานต่อได้ทันที
            vibrate(2, 100, 80);
            speakThai("แว่นตาซีสปีคพร้อมใช้งานค่ะ");
        } else {
            // โหมดที่ 5 (โหมดต้อนรับ): ส่งเสียงต้อนรับและแนะนำวิธีใช้งานทุกโหมดทันทีเมื่อสับสวิตช์เปิดเครื่อง
            vibrate(2, 100, 80);
            speakThai("สวัสดีค่ะ แว่นตาอัจฉริยะซีสปีคพร้อมใช้งาน กดปุ่มหนึ่งครั้งเพื่ออ่านข้อความ กดสองครั้งเพื่ออธิบายวัตถุ กดสามครั้งเพื่อนับธนบัตร และกดค้างพร้อมพูดเพื่อถามตอบค่ะ");
        }
        checkBatteryAndWarn();
    } else {
        // หากเชื่อมต่อ Wi-Fi ไม่ติด ให้เปิดโหมดตั้งค่า Wi-Fi "SeeSpeak-Setup" พร้อมพูดแจ้งเตือนแบบออฟไลน์ทันที
        startSetupPortal();
    }

    lastActivityTime = millis();
}

// ----------------------------------------------------------------------------
// LOOP: รอรับการกดปุ่มสั่งงานทั้ง 5 โหมด หรือรับค่าจากหน้าเว็บตั้งค่า
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
    checkBatteryAndWarn();

    int action = readButtonAction();
    if (action == 1) {
        // กดปุ่ม 1 ครั้ง: โหมดที่ 1 โหมดอ่านข้อความ (Smart OCR)
        vibrate(1, 100, 0);
        processWithGemini(PROMPT_MODE_1_OCR, "กำลังอ่านข้อความค่ะ");
    } else if (action == 2) {
        // กดปุ่ม 2 ครั้ง: โหมดที่ 2 โหมดอธิบายวัตถุและทิศทางตรงหน้า
        vibrate(2, 100, 80);
        processWithGemini(PROMPT_MODE_2_SCENE, "กำลังสำรวจสิ่งตรงหน้าค่ะ");
    } else if (action == 3) {
        // กดปุ่ม 3 ครั้ง: โหมดที่ 3 โหมดนับธนบัตรไทย (สรุปยอดเงินรวมขึ้นก่อนเสมอ)
        vibrate(3, 100, 80);
        processWithGemini(PROMPT_MODE_3_BANKNOTE, "กำลังตรวจสอบธนบัตรค่ะ");
    } else if (action == 4) {
        // กดปุ่มค้างไว้ขณะพูดคำถาม: โหมดที่ 4 โหมดถาม-ตอบด้วยเสียง (PDM Mic + กล้อง OV3660)
        handleVoiceQAMode();
    } else if (action == 5) {
        // กดปุ่ม 4 ครั้งติดกัน: เปิดโหมดตั้งค่า Wi-Fi / API Key ("SeeSpeak-Setup") พร้อมพูดแจ้งเตือนแบบออฟไลน์
        startSetupPortal();
        return;
    }

    // หากไม่ได้กดปุ่มเกิน 3 นาที ให้เข้าโหมดประหยัดพลังงาน Deep Sleep อัตโนมัติ
    if (millis() - lastActivityTime > AUTO_SLEEP_TIMEOUT_MS) {
        enterDeepSleep();
    }

    delay(10);
}
