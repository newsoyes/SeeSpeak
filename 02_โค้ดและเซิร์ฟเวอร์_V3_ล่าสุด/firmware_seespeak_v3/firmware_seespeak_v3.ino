// ============================================================================
// โปรเจกต์: SeeSpeak V3 — รุ่นพัฒนาต่อยอดระดับภาค (เซียร์รังสิต 2569)
// บอร์ด: Seeed Studio XIAO ESP32S3 Sense (8MB OPI PSRAM + ไมโครโฟนดิจิทัล PDM ในตัว)
// กล้อง: รองรับ OV5640 (5MP 68° Fixed Focus) และ OV3660 (3MP) อัตโนมัติ
// แบตเตอรี่: LiPo 3.7V ความจุ 600 mAh + ระบบวัดแบตอัจฉริยะ 3 ชั้น (รองรับ ADC ขา D3)
// ระบบเสียง: MAX98357A I2S Amplifier (I2S_NUM_1) + ลำโพง Open-Ear 8Ω 1W 20mm
//   * หมายเหตุฮาร์ดแวร์: ห้ามเสียบ MicroSD Card ในช่องบนบอร์ด เนื่องจากขา SD (D8/D9/D10)
//     ใช้ร่วมกับแอมป์ลำโพง MAX98357A (BCLK/LRC/DIN)
// ----------------------------------------------------------------------------
// ฟีเจอร์ใหม่ระดับภาค (SeeSpeak V3.7 — Architecture 2B Pro Hybrid):
//   1. 🗣️ โหมดคุยสดไร้สัมผัส (Hands-Free Live Voice AI — Architecture 2B Pro):
//      - เชื่อมต่อ Cloud Relay v3.7 -> Google Gemini Live API ผ่าน Binary WebSocket (เร็ว + ไม่ตัดสาย)
//      - ล็อกภาษาไทย 100% + Jitter Pre-Buffer 320ms กันเสียงกระตุกข้ามโซน + กรองเสียงรบกวน 2 ชั้น (ไม่ลั่นตอบเองตอนเงียบ!)
//      - กล้อง SVGA (800x600) สีสมจริง (Saturation +1) แยกสีธนบัตรไทย 20/50/100/500/1000 ได้แม่นยำ!
//   2. 🔘 ระบบปุ่มกดความเร็วสูง Hybrid Live-First + Auto REST Fallback (ไม่ตัดสาย Live!):
//      - กด 1 ครั้ง: โหมดอ่านข้อความ (แฟลช + XGA ผ่าน Gemini Live ~0.8-1.2 วิ / หรือกดหยุดเสียงพูด)
//      - กด 2 ครั้ง: โหมดอธิบายวัตถุและทิศทางตรงหน้า (แฟลช + XGA ผ่าน Gemini Live)
//      - กด 3 ครั้ง: โหมดนับธนบัตรและเหรียญไทย (แฟลช + XGA ผ่าน Gemini Live — สรุปยอดรวมขึ้นก่อนเสมอ)
//      - กดค้างแล้วพูด (ปล่อยปุ่มเมื่อจบ): Push-to-Talk ผ่าน Gemini Live ทะลุ Noise Gate ในฮอลล์เสียงดัง + แฟลช XGA!
//        * หากพักโหมดคุยสด (กด 5 ครั้ง) หรือ Relay ออฟไลน์ -> สลับไปยิงตรง REST API (gemini-3.5-flash-lite) อัตโนมัติ 100%!
//      - เปิดสวิตช์เครื่อง: โหมดต้อนรับ + แจ้งเปอร์เซ็นต์แบต + เชื่อมต่อ Wi-Fi ด่วนอัตโนมัติ
//      - กด 4 ครั้ง: เข้าโหมดตั้งค่าผ่านมือถือ "SeeSpeak-Setup"
//      - กด 5 ครั้ง: เปิด/ปิดโหมดคุยสดไร้สัมผัส (Hands-Free Live Mode) ชั่วคราวในที่เสียงดัง
//   3. 📡 Multi-WiFi Failover: จำชื่อ Wi-Fi ได้สูงสุด 3 เครือข่าย สลับไปตัวสำรองอัตโนมัติเมื่อเน็ตหลุด
//   4. 🧠 Conversation Memory: จำบริบทบทสนทนาต่อเนื่องได้ทั้งโหมดคุยสดและโหมดปุ่มกด
//   5. 🔊 Offline Thai Voice MP3: พูดเตือนตั้งค่า Wi-Fi / เน็ตหลุด / แบตใกล้หมด ได้แม้ไม่มีเน็ต
// ============================================================================
// การตั้งค่าใน Arduino IDE 2.x ก่อนกด Upload:
//   1. Board: "XIAO_ESP32S3" (ในชุด esp32 โดย Espressif Systems v3.x)
//   2. PSRAM: "OPI PSRAM" (สำคัญมาก!)
//   3. USB CDC On Boot: "Enabled"
//   4. Partition Scheme: "Default with spiffs (3MB APP/1.5MB SPIFFS)"
//   5. ติดตั้งไลบรารี 3 ตัวใน Library Manager:
//      - "ArduinoJson" (โดย Benoit Blanchon)
//      - "ESP32-audioI2S" (โดย Schreibfaul1)
//      - "WebSockets" (โดย Markus Sattler — เวอร์ชัน 2.7.1 ขึ้นไป)
//   6. วางไฟล์ offline_voice_mp3.h (ตัวเดียวกับ V2) ไว้ในโฟลเดอร์ firmware_seespeak_v3/ นี้ด้วย
// ============================================================================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include "esp_camera.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_log.h"
#include "base64.h"
#include "Audio.h"
#include "offline_voice_mp3.h"

// ใช้งาน I2S Core v3:
// - I2S_NUM_0 (ESP_I2S)       -> ไมโครโฟนดิจิทัล PDM (GPIO42/GPIO41)
// - I2S_NUM_1 (driver/i2s_std)-> ลำโพง MAX98357A (GPIO7/8/9) สำหรับโหมดคุยสด Live PCM 16kHz
#include <ESP_I2S.h>
#include <driver/i2s_std.h>

// ----------------------------------------------------------------------------
// 1. ค่าเริ่มต้นของเครื่อง (สามารถเปลี่ยนทุกค่าผ่านหน้าเว็บ SeeSpeak-Setup ได้โดยไม่ต้องแก้โค้ด)
// ----------------------------------------------------------------------------
const char* DEFAULT_WIFI_SSID_1    = "SeeSpeak_Hotspot";
const char* DEFAULT_WIFI_PASS_1    = "YOUR_WIFI_PASSWORD";
const char* DEFAULT_GEMINI_API_KEY = "YOUR_GEMINI_API_KEY"; // ใส่ Gemini API Key ของคุณที่นี่ หรือตั้งค่าผ่านหน้าเว็บ SeeSpeak-Setup
const char* DEFAULT_RELAY_URL      = "wss://your-your-seespeak-relay.onrender.com/ws/seespeak"; // ใส่ลิงก์ Cloud Relay บน Render.com ของคุณ

const char* SETUP_AP_NAME = "SeeSpeak-Setup";

// โมเดลสำรองโหมดปุ่มกดเมื่อไม่ได้เชื่อม Relay (ยิงตรงเข้า Google REST API)
const int GEMINI_MODEL_COUNT = 2;
const char* GEMINI_ENDPOINTS[GEMINI_MODEL_COUNT] = {
    "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash-lite:generateContent?key=",
    "https://generativelanguage.googleapis.com/v1beta/models/gemini-3-flash-preview:generateContent?key="
};
const char* GEMINI_THINKING_CONFIGS[GEMINI_MODEL_COUNT] = {
    "\"thinkingConfig\":{\"thinkingLevel\":\"minimal\"},",
    "\"thinkingConfig\":{\"thinkingLevel\":\"minimal\"},"
};

// ----------------------------------------------------------------------------
// 2. กำหนดขาพินฮาร์ดแวร์ (Pinout บน XIAO ESP32S3 Sense)
// ----------------------------------------------------------------------------
#define PIN_BUTTON      1   // ขา D0 (GPIO1)
#define PIN_FLASH_LED   2   // ขา D1 (GPIO2) -> ผ่าน R 220Ω -> S8050 -> SMD LED
#define PIN_VIB_MOTOR   3   // ขา D2 (GPIO3) -> ผ่าน R 220Ω -> S8050 -> มอเตอร์สั่น + 1N4148
#define PIN_BAT_ADC     4   // ขา D3 (GPIO4) -> ต่อ R 200kΩ+200kΩ จาก BAT+ เพื่อวัดแบตจริง (ไม่ต่อก็ทำงานได้อัตโนมัติ)

// แอมป์ลำโพง MAX98357A (พอร์ต I2S_NUM_1)
#define I2S_BCLK        7   // ขา D8 (GPIO7)
#define I2S_LRC         8   // ขา D9 (GPIO8)
#define I2S_DOUT        9   // ขา D10 (GPIO9)

// ไมโครโฟนดิจิทัล PDM บนบอร์ด Sense (พอร์ต I2S_NUM_0)
#define PIN_PDM_CLK          42
#define PIN_PDM_DATA         41
#define MIC_SAMPLE_RATE      16000
#define VOICE_WAV_RATE       8000
#define MIC_MAX_MS           8000

// ขากล้อง OV5640 / OV3660 บนบอร์ด XIAO ESP32S3 Sense (ห้ามเปลี่ยน)
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

// ปรับทิศทางภาพให้ตรงกับการวางเลนส์ในเคส 3D (ค่า 0 หรือ 1)
#define CAMERA_VFLIP    0
#define CAMERA_HMIRROR  1

// Binary Protocol Opcodes สำหรับ Architecture 2B Pro (ต้องตรงกับ relay_server/server.py v3.7.0)
#define MSG_ESP_AUDIO_PCM   0x01
#define MSG_ESP_JPEG_CHUNK  0x02
#define MSG_ESP_JPEG_END    0x04
#define MSG_ESP_SPEECH_END  0x05
#define MSG_ESP_CANCEL_AI   0x06  // สั่งหยุดเสียง AI กลางคัน (จากการพูดแทรก "หยุด/พอ" หรือกดปุ่ม 1 ครั้ง)
#define MSG_ESP_BUTTON_CMD  0x08  // สั่งโหมดปุ่มกด (1=OCR, 2=Scene, 3=Banknote) ผ่านท่อ Live เดิมแบบไม่ตัดสาย!

#define MSG_AI_AUDIO_PCM    0x10
#define MSG_AI_SPEAK_START  0x11
#define MSG_AI_SPEAK_END    0x12

// เกณฑ์ตัวกรองเสียงรบกวนในฮอลล์แข่ง + กันลั่นตอนเงียบ + Jitter Buffer กันเสียงกระตุก (V3.7)
#define DEFAULT_NOISE_GATE_RMS     420   // ขั้นต่ำในห้องเงียบ (ขยับขึ้นอัตโนมัติตามพื้นเสียงพัดลม/ฮอลล์แข่ง)
#define LIVE_MIN_VOICE_HITS        5     // ต้องมีเสียงพูดต่อเนื่อง >= 5 บล็อก (160 ms) จึงเริ่มส่ง (กันคลิก/ไอ/พัดลมลั่น 100%, Pre-Roll เก็บต้นคำครบ!)
#define LIVE_SILENCE_END_MS        480   // เงียบครบ 0.48 วิ หลังพูด -> ส่งสัญญาณให้ AI ตอบทันที
#define LIVE_MAX_SPEECH_BLOCKS     375   // จำกัดความยาวพูดต่อคำถามไม่เกิน 12 วินาที
#define LIVE_ECHO_COOLDOWN_MS      500   // พักไมค์ 500 ms หลังลำโพงหยุดพูดเพื่อกันเสียงก้องห้องลั่นซ้ำ
#define LIVE_JITTER_PRIME_BYTES    10240 // เก็บ Jitter Buffer 320 ms (5 แพ็กเก็ต) ก่อนเริ่มเล่น -> กันเสียงกระตุกข้ามโซน Oregon<->ไทย!
#define LIVE_JITTER_REPRIME_BYTES  6400  // หากเน็ตมือถือสะดุดกลางคำ -> เก็บใหม่ 200 ms แทนการกระตุกรัวๆ

// ----------------------------------------------------------------------------
// 3. ตัวแปรระบบ, Multi-WiFi (3 ชื่อ), แบตเตอรี่ 600mAh และความจำบทสนทนา
// ----------------------------------------------------------------------------
Audio* audio = nullptr; // ใช้ Pointer เพื่อสลับคืนพอร์ต I2S_NUM_1 ให้โหมด Live Streaming ได้สะอาด 100%
i2s_chan_handle_t liveTxHandle = nullptr;
I2SClass livePdmMic;
bool liveMicRunning = false;

Preferences prefs;
WebServer server(80);
DNSServer dnsServer;
WebSocketsClient liveWS;

WiFiClientSecure geminiClient;
HTTPClient geminiHttp;
bool geminiClientInitialized = false;

// ข้อมูลการเชื่อมต่อที่จำไว้ใน NVS Flash (รองรับ Wi-Fi สูงสุด 3 ชื่อ)
String savedSSID[3]    = {"", "", ""};
String savedPass[3]    = {"", "", ""};
String savedApiKey     = "";
String savedRelayUrl   = "";
bool   handsFreeEnabled = true;  // เปิดโหมดคุยสดไร้สัมผัสอัตโนมัติเมื่อมีการตั้งค่า Relay URL
const int DEFAULT_SPEAKER_VOLUME = 18;
int    savedVolume     = DEFAULT_SPEAKER_VOLUME;
int    noiseGateRms    = DEFAULT_NOISE_GATE_RMS;

// สถานะโหมดคุยสด (2B Live Streaming)
bool liveWsConnected                  = false;
volatile bool liveAiSpeaking          = false;
volatile bool liveAiStreamOpen        = false;
volatile bool livePlaybackPrimed      = false; // รอเติม Jitter Buffer 320ms ก่อนเล่นเพื่อกันเสียงกระตุก
volatile bool liveIgnoreAudioUntilNew = false; // ทิ้งแพ็กเก็ตค้างท่อเมื่อสั่งหยุด AI กลางคัน
volatile unsigned long liveAiEndMs    = 0;
unsigned long liveAiSpeakStartMs      = 0;
unsigned long lastAiCancelMs          = 0;     // หน่วงกันเผลอกดซ้ำไปติดโหมด OCR หลังสั่งหยุด AI
float liveEchoPeakRms                 = 0.0f;  // เรียนรู้ความดังเสียงสะท้อนของลำโพงเองเพื่อตรวจจับการพูดแทรก ("หยุด/พอ")
float ambientNoiseRms                 = 260.0f;// พื้นเสียงรบกวนรอบข้างอัตโนมัติ (ปรับตามเสียงพัดลม/แอร์/ฮอลล์แข่งตลอดเวลา)
float liveTurnPeakRms                 = 0.0f;
int   liveBargeHitCount               = 0;
bool liveUserSpeaking                 = false;
bool liveFrameCapturedForTurn         = false;
bool liveExpectedDisconnect           = false; // แยกการพักโหมดปุ่มกดออกจากการหลุดจริง
int  liveVoiceHitCount                = 0;     // ต้องดังเกินเกณฑ์ต่อเนื่อง 2 บล็อก (~64 ms) เพื่อตัดเสียงป๊อปสั้นๆ
int  liveSpokenBlocks                 = 0;
unsigned long liveLastVoiceMs         = 0;
unsigned long liveLastFrameMs         = 0;
unsigned long liveConnectedMs         = 0;
int  liveFailStreak                   = 0;
bool liveAnnouncedConnect             = false;
String detectedCamName                = "Camera";

// Pre-Roll Audio Buffer (10 บล็อก = ~320 ms) เก็บเสียงก่อนเริ่มพูด เพื่อให้ AI ได้ยินพยางค์แรกครบ 100% ไม่ขาดหาย!
const int PRE_ROLL_BLOCKS       = 10;
uint8_t preRollBuf[PRE_ROLL_BLOCKS][1024];
size_t  preRollLens[PRE_ROLL_BLOCKS];
int     preRollIdx              = 0;
int     preRollFilled           = 0;

// บัฟเฟอร์เสียงวงแหวน 1 MB ใน PSRAM (~32.7 วินาที @ 16kHz 16-bit) + Task เล่นเสียงแยกคอร์
// เมื่อทำงานคู่กับ Pacing 1.14x บน Relay จะรองรับการพูดต่อเนื่องยาวกว่า 3 นาที (เช่น ก-ฮ) โดยไม่ล้นและไม่เร่งสปีด!
const size_t LIVE_AUDIO_RB_SIZE  = 1048576;
uint8_t* liveAudioRb             = nullptr;
volatile size_t liveRbHead       = 0;
volatile size_t liveRbTail       = 0;
volatile size_t liveRbCount      = 0;
portMUX_TYPE liveAudioMux        = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t liveSpeakerTaskHdl  = nullptr;
volatile bool liveSpeakerTaskRun = false;

// สถานะระบบทั่วไป
bool inSetupMode       = false;
bool speechInterrupted = false;
unsigned long lastActivityTime  = 0;
unsigned long lastBatteryTickMs = 0;
const unsigned long AUTO_SLEEP_TIMEOUT_MS   = 600000; // พักเครื่องเมื่อไม่ใช้งานเกิน 10 นาที (ให้พร้อมโชว์กรรมการได้ต่อเนื่อง)
const unsigned long SETUP_PORTAL_TIMEOUT_MS = 300000; // เปิดหน้าเว็บตั้งค่ารอ 5 นาที

// ระบบตรวจจับและแจ้งเตือนแบตเตอรี่ 600 mAh (จำค่าข้าม Deep Sleep)
RTC_DATA_ATTR float rtcUsedMah       = 0.0f;
RTC_DATA_ATTR bool  rtcLowBatWarned  = false;
RTC_DATA_ATTR bool  rtcCritBatWarned = false;
bool hasHardwareBatAdc               = false;
const float BATTERY_CAPACITY_MAH     = 600.0f;
const float BATTERY_WARN_MAH         = 480.0f; // เตือนเมื่อแบตเหลือ ~20% (ใช้ไป 480 mAh จาก 600 mAh)
const float BATTERY_CRIT_MAH         = 555.0f; // ระดับวิกฤตเหลือ ~7.5%

// ความจำบทสนทนาต่อเนื่อง 3 รอบล่าสุด (สำหรับโหมดปุ่มกด V2)
const int MEMORY_SLOTS = 3;
String convMemory[MEMORY_SLOTS] = {"", "", ""};
int convMemoryCount = 0;

// สถานะ High-Pass Filter สำหรับตัดเสียงรบกวนความถี่ต่ำ (< 250 Hz) ในฮอลล์แข่ง
float hpfPrevIn  = 0.0f;
float hpfPrevOut = 0.0f;
float agcGain    = 3.5f;

// คำสั่ง Prompt สำหรับโหมดปุ่มกดทั้ง 4 โหมด
const char* PROMPT_MODE_1_OCR =
    "คุณคือผู้ช่วยคนตาบอด อ่านข้อความภาษาไทยและอังกฤษทั้งหมดที่พออ่านได้ในภาพนี้โดยเรียงตามลำดับความสำคัญ "
    "(เช่น ชื่อสินค้า ป้าย ฉลากยา ราคา เมนูอาหาร เอกสาร) สรุปให้กระชับชัดเจนเป็นธรรมชาติด้วยเสียงผู้หญิง (ลงท้ายด้วยคำว่า ค่ะ) "
    "กฎสำคัญ: แม้ภาพจะเบลอเล็กน้อย เอียง หรือมีนิ้วมือบังบางส่วน ให้พยายามอ่านข้อความทุกคำที่มองเห็นออกมาก่อนเสมอ ห้ามปฏิเสธการอ่านหากยังมีตัวอักษรที่พออ่านได้ "
    "จะพูดว่า 'ไม่พบข้อความในภาพ กรุณากดใหม่อีกครั้งค่ะ' ก็ต่อเมื่อไม่มีตัวอักษรใดๆ เลยจริงๆ เท่านั้น ห้ามใส่เครื่องหมายดอกจันหรือ Markdown เด็ดขาด";

const char* PROMPT_MODE_2_SCENE =
    "คุณคือผู้ช่วยนำทางคนตาบอด อธิบายสิ่งของ บุคคล และสภาพแวดล้อมตรงหน้าในภาพนี้ โดยต้องระบุ 'ตำแหน่งทิศทาง' "
    "(ด้านซ้าย ตรงกลาง ด้านขวา) และ 'ระยะห่างโดยประมาณ' (เช่น ระยะเอื้อมมือถึง หรือห่างออกไปกี่เมตร) "
    "หากมีสิ่งกีดขวางอันตรายเช่น บันได เสา หรือรถ ให้เตือนก่อนเสมอ พูดกระชับ 2-3 ประโยคด้วยเสียงผู้หญิงสุภาพ (ลงท้ายด้วย ค่ะ) ห้ามใส่เครื่องหมายดอกจันหรือ Markdown เด็ดขาด";

const char* PROMPT_MODE_3_BANKNOTE =
    "คุณคือระบบตรวจสอบเงินสำหรับคนตาบอด ตรวจสอบธนบัตรไทย (20 บาทสีเขียว, 50 บาทสีฟ้า, 100 บาทสีแดง, 500 บาทสีม่วง, 1000 บาทสีเทาน้ำตาล) "
    "และเหรียญกษาปณ์ไทย (1, 2, 5, 10 บาท) ในภาพนี้ สังเกตจากตัวเลข สี ขนาด และลวดลาย แม้ธนบัตรจะเอียง พับ หรือมีมือจับบังบางส่วนก็ให้ระบุชนิดและมูลค่าให้ได้ "
    "กฎเหล็ก: ต้องพูดสรุป 'ยอดเงินรวมทั้งหมด' ขึ้นเป็นคำแรกเสมอ เช่น 'รวม 20 บาทค่ะ มีแบงค์ยี่สิบ 1 ใบค่ะ' "
    "จะพูดว่า 'ไม่พบธนบัตรหรือเหรียญในภาพ กรุณาถือเงินด้านหน้ากล้องแล้วกดสามครั้งใหม่ค่ะ' ก็ต่อเมื่อไม่มีเงินในภาพเลยจริงๆ เท่านั้น ห้ามใส่เครื่องหมายดอกจันหรือ Markdown เด็ดขาด";

const char* PROMPT_MODE_4_QA =
    "คุณคือผู้ช่วยอัจฉริยะสำหรับคนตาบอด ฟังคำถามภาษาไทยของผู้ใช้จากไฟล์เสียงที่แนบมานี้ "
    "แล้วตอบคำถามโดยดูจากภาพถ่ายตรงหน้าและบริบทบทสนทนาก่อนหน้า (ถ้ามี) อย่างถูกต้อง กระชับ ชัดเจนภายใน 1-3 ประโยค "
    "ด้วยน้ำเสียงผู้หญิงที่สุภาพ (ลงท้ายด้วย ค่ะ) หากเสียงเบาให้ช่วยอธิบายจุดเด่นสำคัญในภาพแทน ห้ามใส่เครื่องหมายดอกจันหรือ Markdown เด็ดขาด";

// Forward Declarations
void enterDeepSleep();
void stopLiveMode();
void startLiveModeIfConfigured();

// ----------------------------------------------------------------------------
// 4. ฟังก์ชันจัดการเสียง I2S_NUM_1 แบบไม่ชนกัน (PSRAM Ring Buffer + auto_clear กันเสียงค้าง 100%)
// ----------------------------------------------------------------------------
void clearLiveAudioRing() {
    taskENTER_CRITICAL(&liveAudioMux);
    liveRbHead  = 0;
    liveRbTail  = 0;
    liveRbCount = 0;
    taskEXIT_CRITICAL(&liveAudioMux);
    livePlaybackPrimed = false;
}

void pushLiveAudioRing(const uint8_t* data, size_t len) {
    if (liveAudioRb == nullptr || len == 0) return;
    taskENTER_CRITICAL(&liveAudioMux);
    size_t head  = liveRbHead;
    size_t tail  = liveRbTail;
    size_t count = liveRbCount;
    if (len >= LIVE_AUDIO_RB_SIZE) {
        data += (len - LIVE_AUDIO_RB_SIZE);
        len = LIVE_AUDIO_RB_SIZE;
        head = tail = count = 0;
    } else if (count + len > LIVE_AUDIO_RB_SIZE) {
        // เลื่อนหัวอ่านทิ้งข้อมูลเก่าสุดเฉพาะกรณีล้นเกิน 16 วินาที (รักษาการจัดเรียง 2-byte PCM)
        size_t drop = (count + len) - LIVE_AUDIO_RB_SIZE;
        if (drop & 1) drop++;
        if (drop > count) drop = count;
        tail = (tail + drop) % LIVE_AUDIO_RB_SIZE;
        count -= drop;
    }
    size_t spaceToEnd = LIVE_AUDIO_RB_SIZE - head;
    size_t firstPart  = (len < spaceToEnd) ? len : spaceToEnd;
    memcpy(liveAudioRb + head, data, firstPart);
    if (len > firstPart) {
        memcpy(liveAudioRb, data + firstPart, len - firstPart);
    }
    liveRbHead  = (head + len) % LIVE_AUDIO_RB_SIZE;
    liveRbTail  = tail;
    liveRbCount = count + len;
    taskEXIT_CRITICAL(&liveAudioMux);
}

size_t popLiveAudioRing(uint8_t* out, size_t maxLen) {
    if (liveAudioRb == nullptr || maxLen < 2) return 0;
    size_t toRead = 0;
    taskENTER_CRITICAL(&liveAudioMux);
    size_t tail  = liveRbTail;
    size_t count = liveRbCount;
    toRead = (maxLen < count) ? maxLen : count;
    toRead &= ~((size_t)1); // จัดแนว 16-bit (2 bytes/sample) เสมอ
    if (toRead > 0) {
        size_t dataToEnd = LIVE_AUDIO_RB_SIZE - tail;
        size_t firstPart = (toRead < dataToEnd) ? toRead : dataToEnd;
        memcpy(out, liveAudioRb + tail, firstPart);
        if (toRead > firstPart) {
            memcpy(out + firstPart, liveAudioRb, toRead - firstPart);
        }
        liveRbTail  = (tail + toRead) % LIVE_AUDIO_RB_SIZE;
        liveRbCount = count - toRead;
    }
    taskEXIT_CRITICAL(&liveAudioMux);
    return toRead;
}

void liveSpeakerTask(void* param) {
    static uint8_t rawBuf[1024];
    static int16_t scaledBuf[512];
    static uint8_t silenceBuf[1024] = {0};

    while (liveSpeakerTaskRun) {
        size_t avail = liveRbCount;
        if (!livePlaybackPrimed) {
            if (avail == 0) {
                if (!liveAiStreamOpen && liveAiSpeaking) {
                    liveAiSpeaking = false;
                    liveAiEndMs    = millis();
                }
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            // รอสะสม Jitter Buffer ~320 ms (10,240 bytes) ก่อนเริ่มเล่น (ยกเว้นคำตอบสั้นที่ส่งจบแล้ว)
            if (liveAiStreamOpen && avail < LIVE_JITTER_PRIME_BYTES) {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            livePlaybackPrimed = true;
        }

        size_t n = popLiveAudioRing(rawBuf, sizeof(rawBuf));
        if (n >= 2 && liveTxHandle != nullptr) {
            float volScale = (float)savedVolume / 21.0f;
            size_t sampleCount = n / 2;
            const int16_t* inSamples = (const int16_t*)rawBuf;
            for (size_t i = 0; i < sampleCount; i++) {
                int32_t v = (int32_t)(inSamples[i] * volScale);
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                scaledBuf[i] = (int16_t)v;
            }
            size_t written = 0;
            i2s_channel_write(liveTxHandle, scaledBuf, n, &written, pdMS_TO_TICKS(100));
            liveAiSpeaking   = true;
            liveAiEndMs      = millis();
            lastActivityTime = millis();
        } else {
            if (liveAiStreamOpen) {
                // หากเน็ตมือถือสะดุดชั่วคราวกลางประโยค -> พักเติมเสียง 200ms แทนการกระตุกถี่ๆ
                if (liveTxHandle != nullptr) {
                    size_t written = 0;
                    i2s_channel_write(liveTxHandle, silenceBuf, sizeof(silenceBuf), &written, pdMS_TO_TICKS(40));
                }
                unsigned long waitStart = millis();
                while (liveSpeakerTaskRun && liveAiStreamOpen && liveRbCount < LIVE_JITTER_REPRIME_BYTES && (millis() - waitStart < 450)) {
                    vTaskDelay(pdMS_TO_TICKS(5));
                }
            } else {
                if (liveAiSpeaking) {
                    // ล้างท้ายเสียงด้วยความเงียบสะอาด 32ms ก่อนเปิดไมค์รับคำถามถัดไป
                    if (liveTxHandle != nullptr) {
                        size_t written = 0;
                        i2s_channel_write(liveTxHandle, silenceBuf, sizeof(silenceBuf), &written, pdMS_TO_TICKS(60));
                    }
                    liveAiSpeaking = false;
                    liveAiEndMs    = millis();
                }
                livePlaybackPrimed = false;
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        }
    }
    liveSpeakerTaskHdl = nullptr;
    vTaskDelete(nullptr);
}

void stopLiveSpeakerI2S() {
    liveSpeakerTaskRun = false;
    liveAiStreamOpen   = false;
    liveAiSpeaking     = false;
    clearLiveAudioRing();

    unsigned long waitStart = millis();
    while (liveSpeakerTaskHdl != nullptr && (millis() - waitStart < 400)) {
        delay(10);
    }

    if (liveTxHandle != nullptr) {
        i2s_channel_disable(liveTxHandle);
        i2s_del_channel(liveTxHandle);
        liveTxHandle = nullptr;
    }
}

void ensureAudioLibReady() {
    stopLiveSpeakerI2S();
    if (audio == nullptr) {
        audio = new Audio(I2S_NUM_1);
        audio->setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
        audio->setVolume(savedVolume);
    }
}

void releaseAudioLib() {
    if (audio != nullptr) {
        audio->stopSong();
        delete audio;
        audio = nullptr;
        delay(20);
    }
}

bool startLiveSpeakerI2S() {
    releaseAudioLib();
    if (liveAudioRb == nullptr) {
        liveAudioRb = (uint8_t*)ps_malloc(LIVE_AUDIO_RB_SIZE);
        if (liveAudioRb == nullptr) {
            Serial.println("[Live Speaker] จัดสรร PSRAM Ring Buffer ไม่สำเร็จ!");
            return false;
        }
    }
    if (liveTxHandle != nullptr && liveSpeakerTaskHdl != nullptr) return true;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num        = 12;
    chan_cfg.dma_frame_num       = 512;
    chan_cfg.auto_clear_after_cb = true; // สำคัญมาก! เคลียร์ DMA เป็นศูนย์อัตโนมัติ กันเสียงค้างรัวๆ 100%
    if (i2s_new_channel(&chan_cfg, &liveTxHandle, nullptr) != ESP_OK) {
        Serial.println("[Live Speaker] สร้าง I2S_NUM_1 TX ไม่สำเร็จ");
        liveTxHandle = nullptr;
        return false;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)I2S_BCLK,
            .ws   = (gpio_num_t)I2S_LRC,
            .dout = (gpio_num_t)I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { false, false, false },
        },
    };
    if (i2s_channel_init_std_mode(liveTxHandle, &std_cfg) != ESP_OK ||
        i2s_channel_enable(liveTxHandle) != ESP_OK) {
        Serial.println("[Live Speaker] เริ่มต้น I2S_NUM_1 ไม่สำเร็จ");
        stopLiveSpeakerI2S();
        return false;
    }

    clearLiveAudioRing();
    liveSpeakerTaskRun = true;
    xTaskCreatePinnedToCore(liveSpeakerTask, "live_spk", 4096, nullptr, 3, &liveSpeakerTaskHdl, 0);
    return true;
}

// ----------------------------------------------------------------------------
// 5. มอเตอร์สั่น (Haptic) และความจำบทสนทนา (Conversation Memory)
// ----------------------------------------------------------------------------
void vibrate(int count, int durationMs = 120, int pauseMs = 100) {
    for (int i = 0; i < count; i++) {
        digitalWrite(PIN_VIB_MOTOR, HIGH);
        delay(durationMs);
        digitalWrite(PIN_VIB_MOTOR, LOW);
        if (i < count - 1) delay(pauseMs);
    }
}

void rememberAnswer(const String& answer) {
    String clean = answer;
    clean.replace("\"", "'");
    clean.replace("\n", " ");
    clean.trim();
    if (clean.length() == 0) return;
    if (clean.length() > 240) clean = clean.substring(0, 240);

    if (convMemoryCount < MEMORY_SLOTS) {
        convMemory[convMemoryCount++] = clean;
    } else {
        for (int i = 0; i < MEMORY_SLOTS - 1; i++) {
            convMemory[i] = convMemory[i + 1];
        }
        convMemory[MEMORY_SLOTS - 1] = clean;
    }
}

String buildMemoryContextString() {
    if (convMemoryCount == 0) return "";
    String ctx = " [บริบทคำตอบก่อนหน้าล่าสุด: ";
    for (int i = 0; i < convMemoryCount; i++) {
        ctx += String(i + 1) + ") " + convMemory[i] + " ";
    }
    ctx += "]";
    return ctx;
}

// ----------------------------------------------------------------------------
// 6. โหลดและบันทึกค่าถาวร (NVS Flash: Wi-Fi 3 ชื่อ, API Key, Relay URL, ความดัง, Noise Gate)
// ----------------------------------------------------------------------------
void loadSettings() {
    prefs.begin("seespeak_v3", true);
    savedSSID[0]     = prefs.getString("ssid1", DEFAULT_WIFI_SSID_1);
    savedPass[0]     = prefs.getString("pass1", DEFAULT_WIFI_PASS_1);
    savedSSID[1]     = prefs.getString("ssid2", "");
    savedPass[1]     = prefs.getString("pass2", "");
    savedSSID[2]     = prefs.getString("ssid3", "");
    savedPass[2]     = prefs.getString("pass3", "");
    savedApiKey      = prefs.getString("apikey", DEFAULT_GEMINI_API_KEY);
    savedRelayUrl    = prefs.getString("relay", DEFAULT_RELAY_URL);
    savedVolume      = prefs.getInt("vol", DEFAULT_SPEAKER_VOLUME);
    handsFreeEnabled = prefs.getBool("live_on", true);
    noiseGateRms     = prefs.getInt("ngate", DEFAULT_NOISE_GATE_RMS);
    prefs.end();

    if (savedSSID[0].length() == 0) savedSSID[0] = DEFAULT_WIFI_SSID_1;
    if (savedApiKey.length() == 0) {
        savedApiKey = DEFAULT_GEMINI_API_KEY;
    }
    if (savedVolume < 6 || savedVolume > 21) savedVolume = DEFAULT_SPEAKER_VOLUME;
    // อัปเกรดค่า Noise Gate เก่า (650 หรือ 380) เป็นค่าใหม่ V3.7 (420) ที่กันคลิก/พัดลมลั่นแต่ยังรับเสียงพูดได้ไว
    if (noiseGateRms == 650 || noiseGateRms == 380 || noiseGateRms < 200 || noiseGateRms > 2500) {
        noiseGateRms = DEFAULT_NOISE_GATE_RMS;
        prefs.begin("seespeak_v3", false);
        prefs.putInt("ngate", noiseGateRms);
        prefs.end();
    }
}

void saveSettings(String s1, String p1, String s2, String p2, String s3, String p3,
                  String newApiKey, String newRelayUrl, int newVol, bool newLiveOn, int newNGate) {
    s1.trim(); p1.trim(); s2.trim(); p2.trim(); s3.trim(); p3.trim();
    newApiKey.trim(); newRelayUrl.trim();
    if (newVol < 6) newVol = 6;
    if (newVol > 21) newVol = 21;
    if (newNGate < 200) newNGate = 200;
    if (newNGate > 2500) newNGate = 2500;

    prefs.begin("seespeak_v3", false);
    if (s1.length() > 0) prefs.putString("ssid1", s1);
    prefs.putString("pass1", p1);
    prefs.putString("ssid2", s2);
    prefs.putString("pass2", p2);
    prefs.putString("ssid3", s3);
    prefs.putString("pass3", p3);
    if (newApiKey.length() > 0) prefs.putString("apikey", newApiKey);
    prefs.putString("relay", newRelayUrl);
    prefs.putInt("vol", newVol);
    prefs.putBool("live_on", newLiveOn);
    prefs.putInt("ngate", newNGate);
    prefs.end();

    savedVolume = newVol;
    if (audio != nullptr) audio->setVolume(savedVolume);
}

// ----------------------------------------------------------------------------
// 7. ฟังก์ชันพูดภาษาไทย (Fast First-Sentence TTS) และเสียงออฟไลน์ในตัวบอร์ด
// ----------------------------------------------------------------------------
void speakThaiChunk(String chunk) {
    chunk.trim();
    if (chunk.length() == 0 || speechInterrupted) return;
    ensureAudioLibReady();

    unsigned long chunkStart = millis();
    audio->connecttospeech(chunk.c_str(), "th");
    while (audio->isRunning()) {
        audio->loop();
        if (millis() - chunkStart > 400 && digitalRead(PIN_BUTTON) == LOW) {
            delay(30);
            if (digitalRead(PIN_BUTTON) == LOW) {
                audio->stopSong();
                speechInterrupted = true;
                vibrate(1, 80, 0);
                while (digitalRead(PIN_BUTTON) == LOW) delay(10);
                unsigned long calmStart = millis();
                while (millis() - calmStart < 350) {
                    if (digitalRead(PIN_BUTTON) == LOW) calmStart = millis();
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

    bool isFirstChunk = true;
    while (text.length() > 0 && !speechInterrupted) {
        int maxLen = isFirstChunk ? 120 : 360;
        if (text.length() <= maxLen) {
            speakThaiChunk(text);
            break;
        }
        int splitIdx = text.lastIndexOf(' ', maxLen);
        if (isFirstChunk) {
            int firstSpace = text.indexOf(' ', 24);
            if (firstSpace > 0 && firstSpace <= 150) splitIdx = firstSpace;
        }
        if (splitIdx <= 0) {
            splitIdx = maxLen;
            while (splitIdx > 0 && (((uint8_t)text[splitIdx] & 0xC0) == 0x80)) splitIdx--;
            if (splitIdx == 0) splitIdx = maxLen;
        }
        speakThaiChunk(text.substring(0, splitIdx));
        isFirstChunk = false;
        text = text.substring(splitIdx);
        text.trim();
    }
    lastActivityTime = millis();
}

void playOfflineVoice(const char* virtualPath, const char* logText) {
    static fs::FS offlineMemFS(std::make_shared<SeeSpeakMemFSImpl>());
    ensureAudioLibReady();
    Serial.println(String("[Offline TTS พูด]: ") + logText);

    unsigned long startMs = millis();
    if (audio->connecttoFS(offlineMemFS, virtualPath)) {
        while (audio->isRunning()) {
            audio->loop();
            if (inSetupMode) {
                dnsServer.processNextRequest();
                server.handleClient();
            }
            if (millis() - startMs > 400 && digitalRead(PIN_BUTTON) == LOW) {
                delay(30);
                if (digitalRead(PIN_BUTTON) == LOW) {
                    audio->stopSong();
                    while (digitalRead(PIN_BUTTON) == LOW) delay(10);
                    break;
                }
            }
            delay(1);
        }
    }
    lastActivityTime = millis();
}

void playOfflineSetupVoice()      { playOfflineVoice("/setup.mp3",  "เข้าสู่การตั้งค่าไวไฟ โดยเข้าไปที่ไวไฟ ซีสปีค เซ็ตอัป ค่ะ"); }
void playOfflineNoWiFiVoice()     { vibrate(2, 140, 100); playOfflineVoice("/nowifi.mp3", "ไม่สามารถเชื่อมต่ออินเทอร์เน็ตได้ กรุณาตรวจสอบไวไฟค่ะ"); }
void playOfflineLowBatteryVoice() {
    vibrate(1, 120, 80); delay(80); vibrate(1, 350, 80); delay(80); vibrate(1, 120, 0);
    playOfflineVoice("/lowbat.mp3", "แบตเตอรี่ใกล้หมด กรุณาชาร์จแบตค่ะ");
}

// ----------------------------------------------------------------------------
// 8. ระบบตรวจจับแบตเตอรี่ 600 mAh อัจฉริยะ 3 ชั้น (Brownout + Fuel Gauge + ขา D3 ADC)
// ----------------------------------------------------------------------------
void initBatteryMonitor() {
    if (esp_reset_reason() == ESP_RST_POWERON) {
        rtcUsedMah       = 0.0f;
        rtcLowBatWarned  = false;
        rtcCritBatWarned = false;
    }

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
        Serial.printf("[Battery 600mAh] ตรวจพบวงจรวัดแรงดันขา D3 (%.2f V)\n", (avgMv * 2.0f) / 1000.0f);
    } else {
        hasHardwareBatAdc = false;
        Serial.println("[Battery 600mAh] ใช้ Smart Fuel Gauge 600 mAh + Brownout Guard");
    }
    lastBatteryTickMs = millis();
}

uint32_t readHardwareBatteryMv() {
    if (!hasHardwareBatAdc) return 4000;
    uint32_t sumMv = 0;
    for (int i = 0; i < 8; i++) sumMv += analogReadMilliVolts(PIN_BAT_ADC);
    return (sumMv / 8) * 2;
}

int getBatteryPercent() {
    if (hasHardwareBatAdc) {
        int mv = (int)readHardwareBatteryMv();
        int pct = map(constrain(mv, 3300, 4180), 3300, 4180, 0, 100);
        return pct;
    }
    float remain = max(0.0f, BATTERY_CAPACITY_MAH - rtcUsedMah);
    return constrain((int)round((remain / BATTERY_CAPACITY_MAH) * 100.0f), 0, 100);
}

void checkBatteryAndWarn() {
    unsigned long now = millis();
    if (now - lastBatteryTickMs >= 5000) {
        float elapsedHours = (now - lastBatteryTickMs) / 3600000.0f;
        lastBatteryTickMs = now;
        float estMa = liveWsConnected ? 135.0f : 70.0f;
        rtcUsedMah += estMa * elapsedHours;
    }

    bool isLowBat  = (rtcUsedMah >= BATTERY_WARN_MAH);
    bool isCritBat = (rtcUsedMah >= BATTERY_CRIT_MAH);

    if (hasHardwareBatAdc) {
        uint32_t vBatMv = readHardwareBatteryMv();
        if (vBatMv > 2500 && vBatMv <= 3480) isLowBat  = true;
        if (vBatMv > 2500 && vBatMv <= 3320) isCritBat = true;
    }

    if (isCritBat && !rtcCritBatWarned) {
        rtcCritBatWarned = true;
        rtcLowBatWarned  = true;
        stopLiveMode();
        playOfflineLowBatteryVoice();
        enterDeepSleep();
    } else if (isLowBat && !rtcLowBatWarned) {
        rtcLowBatWarned = true;
        bool wasLive = liveWsConnected;
        if (wasLive) stopLiveMode();
        playOfflineLowBatteryVoice();
        if (wasLive) startLiveModeIfConfigured();
    }
}

// ----------------------------------------------------------------------------
// 9. ระบบเชื่อมต่อ Multi-WiFi Failover (สลับเครือข่ายสำรองอัตโนมัติสูงสุด 3 ชื่อ)
// ----------------------------------------------------------------------------
bool tryConnectSingleWiFi(const String& ssid, const String& pass, unsigned long timeoutMs) {
    if (ssid.length() == 0) return false;
    Serial.println("[Wi-Fi] กำลังเชื่อมต่อ: " + ssid);
    WiFi.disconnect(false);
    WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs < timeoutMs)) {
        if (digitalRead(PIN_BUTTON) == LOW) {
            delay(1200);
            if (digitalRead(PIN_BUTTON) == LOW) {
                return false; // ให้ผู้เรียกพาเข้า Setup Portal
            }
        }
        delay(300);
        Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[Wi-Fi] เชื่อมต่อสำเร็จ -> " + ssid + " (IP: " + WiFi.localIP().toString() + ")");
        return true;
    }
    return false;
}

bool connectBestWiFi(unsigned long perNetworkTimeoutMs = 5500) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);

    // 1. ลองต่อ Wi-Fi หลักทันทีโดยไม่เสียเวลารอสแกน (~2.5 วิ) -> บูตเชื่อมไวสุด!
    if (savedSSID[0].length() > 0) {
        if (tryConnectSingleWiFi(savedSSID[0], savedPass[0], min(perNetworkTimeoutMs, 4500UL))) {
            return true;
        }
    }

    // 2. หาก Wi-Fi หลักไม่ติด ค่อยสแกนหาเครือข่ายสำรองที่อยู่ในพื้นที่จริง (Multi-WiFi Failover)
    int foundCount = WiFi.scanNetworks();
    bool tried[3] = {true, false, false};

    for (int slot = 1; slot < 3; slot++) {
        if (savedSSID[slot].length() == 0) continue;
        bool visible = false;
        for (int i = 0; i < foundCount; i++) {
            if (WiFi.SSID(i) == savedSSID[slot]) {
                visible = true;
                break;
            }
        }
        if (visible) {
            tried[slot] = true;
            if (tryConnectSingleWiFi(savedSSID[slot], savedPass[slot], perNetworkTimeoutMs)) {
                return true;
            }
        }
    }

    // เผื่อฮอตสปอตสำรองซ่อนชื่อ (Hidden SSID)
    for (int slot = 1; slot < 3; slot++) {
        if (!tried[slot] && savedSSID[slot].length() > 0) {
            if (tryConnectSingleWiFi(savedSSID[slot], savedPass[slot], 4000)) {
                return true;
            }
        }
    }
    return false;
}

bool ensureWiFiConnected() {
    if (WiFi.status() == WL_CONNECTED) return true;
    Serial.println("[Wi-Fi] สัญญาณหลุด กำลังสลับหาเครือข่ายสำรองอัตโนมัติ...");
    if (connectBestWiFi(4500)) return true;
    playOfflineNoWiFiVoice();
    return false;
}

// ----------------------------------------------------------------------------
// 10. หน้าเว็บตั้งค่า SeeSpeak-Setup V3 (Multi-WiFi 3 ชื่อ + Cloud Relay 2B + ความดัง + แบตเตอรี่)
// ----------------------------------------------------------------------------
String buildSetupHtmlPage() {
    int n = WiFi.scanNetworks();
    String networkOptions = "";
    for (int i = 0; i < n; ++i) {
        String ssidItem = WiFi.SSID(i);
        ssidItem.replace("\"", "&quot;");
        networkOptions += "<button type='button' class='net-btn' onclick=\"document.getElementById('ssid1').value='" + ssidItem + "';\">"
                          "📶 เลือกเป็น Wi-Fi หลัก: <b>" + ssidItem + "</b> <span style='color:#93C5FD;font-size:0.88rem;'>(" + String(WiFi.RSSI(i)) + " dBm)</span></button>";
    }
    if (n <= 0) networkOptions = "<p class='hint'>ไม่พบเครือข่ายรอบข้าง กรุณาพิมพ์ชื่อฮอตสปอตโดยตรง</p>";

    String maskedKey = (savedApiKey.length() > 10 && savedApiKey != "YOUR_GEMINI_API_KEY")
                           ? (savedApiKey.substring(0, 8) + "••••••••")
                           : "ยังไม่ได้ตั้งค่าคีย์";
    String volStr   = String(savedVolume);
    String batStr   = String(getBatteryPercent());
    String ngateStr = String(noiseGateRms);

    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="th">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ตั้งค่าแว่นอัจฉริยะ SeeSpeak V3</title>
<link rel="icon" type="image/png" href="/seespeak.png">
<style>
  * { box-sizing: border-box; }
  body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
         background: radial-gradient(circle at top, #1E293B 0%, #0F172A 100%);
         color: #F8FAFC; margin: 0; padding: 14px 12px 36px; line-height: 1.5; }
  .card { position: relative; overflow: hidden; max-width: 490px; margin: 0 auto;
          background: rgba(30, 41, 59, 0.94); padding: 20px 18px 26px; border-radius: 22px;
          border: 1.5px solid rgba(96, 165, 250, 0.3); box-shadow: 0 20px 45px rgba(0,0,0,0.55); }
  .hero-box { text-align: center; background: linear-gradient(145deg, rgba(37,99,235,0.22), rgba(15,23,42,0.8));
              border: 1px solid rgba(147,197,253,0.25); border-radius: 18px; padding: 14px 12px; margin-bottom: 16px; }
  .hero-img { width: 205px; max-width: 78%; height: auto; filter: drop-shadow(0 10px 16px rgba(0,0,0,0.55)); }
  h1 { font-size: 1.4rem; margin: 6px 0 2px; color: #60A5FA; }
  .badge-pill { display: inline-block; background: rgba(16,185,129,0.2); border: 1px solid rgba(52,211,153,0.45);
                color: #A7F3D0; font-size: 0.84rem; font-weight: 600; padding: 4px 12px; border-radius: 999px; margin-top: 6px; }
  label { display: block; font-size: 1.05rem; font-weight: bold; margin-top: 15px; margin-bottom: 5px; color: #F1F5F9; }
  input[type=text], input[type=password], input[type=number] {
    width: 100%; padding: 12px 13px; font-size: 1.05rem; border-radius: 11px;
    border: 2px solid #475569; background: #0F172A; color: #FFF; }
  .hint { font-size: 0.88rem; color: #94A3B8; margin-top: 4px; }
  .net-btn { width: 100%; text-align: left; padding: 10px 12px; margin-top: 6px; font-size: 0.96rem;
             background: #334155; color: #F8FAFC; border: 1px solid #475569; border-radius: 9px; cursor: pointer; }
  .box-sec { background: rgba(15,23,42,0.7); border: 1.5px solid #334155; border-radius: 14px; padding: 12px; margin-top: 10px; }
  .vol-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-bottom: 10px; }
  .vol-preset { padding: 10px 6px; border-radius: 10px; border: 2px solid #475569; background: #1E293B;
                color: #E2E8F0; font-size: 0.92rem; font-weight: bold; cursor: pointer; text-align: center; }
  .vol-preset.active { background: rgba(37,99,235,0.38); border-color: #38BDF8; color: #FFF; }
  .check-row { display: flex; align-items: center; gap: 10px; margin-top: 10px; font-size: 1rem; }
  input[type=checkbox] { width: 22px; height: 22px; accent-color: #38BDF8; }
  .pw-wrap { position: relative; margin-top: 6px; }
  .pw-wrap input { padding-right: 48px; }
  .pw-eye { position: absolute; right: 8px; top: 50%; transform: translateY(-50%);
            background: rgba(51,65,85,0.85); border: 1px solid #64748B; color: #F8FAFC;
            border-radius: 8px; padding: 5px 9px; font-size: 1.05rem; cursor: pointer; }
  .btn-save { width: 100%; margin-top: 22px; padding: 15px; font-size: 1.18rem; font-weight: bold;
              background: linear-gradient(135deg, #2563EB 0%, #0284C7 100%); color: #FFF; border: none; border-radius: 14px; cursor: pointer; }
</style>
</head>
<body>
<main class="card" role="main">
  <div class="hero-box">
    <img src="/seespeak.png" alt="SeeSpeak V3" class="hero-img">
    <h1>ตั้งค่าแว่น SeeSpeak V3</h1>
    <div class="badge-pill">🔋 แบตเตอรี่คงเหลือ )rawliteral" + batStr + R"rawliteral(% (600 mAh) • )rawliteral" + detectedCamName + R"rawliteral(</div>
  </div>

  <form action="/save" method="POST">
    <label for="ssid1">1. Wi-Fi หลัก (2.4 GHz):</label>
    <input type="text" id="ssid1" name="ssid1" value=")rawliteral" + savedSSID[0] + R"rawliteral(" required>
    <div class="pw-wrap">
      <input type="password" id="pass1" name="pass1" value=")rawliteral" + savedPass[0] + R"rawliteral(" placeholder="รหัสผ่าน Wi-Fi หลัก">
      <button type="button" class="pw-eye" onclick="togglePw('pass1', this)" aria-label="แสดงหรือซ่อนรหัสผ่าน Wi-Fi หลัก">👁️</button>
    </div>
    )rawliteral" + networkOptions + R"rawliteral(

    <div class="box-sec">
      <label style="margin-top:0;">2. Wi-Fi สำรองอัตโนมัติ (Multi-WiFi Failover สำหรับงานแข่ง):</label>
      <input type="text" name="ssid2" value=")rawliteral" + savedSSID[1] + R"rawliteral(" placeholder="ชื่อ Wi-Fi สำรอง 1 (เช่น ฮอตสปอตเครื่องสำรอง)">
      <div class="pw-wrap">
        <input type="password" id="pass2" name="pass2" value=")rawliteral" + savedPass[1] + R"rawliteral(" placeholder="รหัสผ่าน Wi-Fi สำรอง 1">
        <button type="button" class="pw-eye" onclick="togglePw('pass2', this)" aria-label="แสดงหรือซ่อนรหัสผ่าน Wi-Fi สำรอง 1">👁️</button>
      </div>
      <input type="text" name="ssid3" value=")rawliteral" + savedSSID[2] + R"rawliteral(" placeholder="ชื่อ Wi-Fi สำรอง 2 (เช่น Pocket Wi-Fi)" style="margin-top:10px;">
      <div class="pw-wrap">
        <input type="password" id="pass3" name="pass3" value=")rawliteral" + savedPass[2] + R"rawliteral(" placeholder="รหัสผ่าน Wi-Fi สำรอง 2">
        <button type="button" class="pw-eye" onclick="togglePw('pass3', this)" aria-label="แสดงหรือซ่อนรหัสผ่าน Wi-Fi สำรอง 2">👁️</button>
      </div>
    </div>

    <div class="box-sec">
      <label for="relay" style="margin-top:0;">3. โหมดคุยสดไร้สัมผัส (Cloud Relay 2B — Gemini Live API):</label>
      <input type="text" id="relay" name="relay" value=")rawliteral" + savedRelayUrl + R"rawliteral(" placeholder="wss://your-seespeak-relay.onrender.com/ws/seespeak">
      <div class="check-row">
        <input type="checkbox" id="live_on" name="live_on" value="1" )rawliteral" + String(handsFreeEnabled ? "checked" : "") + R"rawliteral(>
        <label for="live_on" style="margin:0;font-weight:normal;">เปิดโหมดคุยสดไร้สัมผัสอัตโนมัติ (พูดถามได้เลยไม่ต้องกดปุ่ม)</label>
      </div>
      <label for="ngate" style="font-size:0.95rem;margin-top:10px;">ระดับตัดเสียงรบกวนในฮอลล์แข่ง (Noise Gate RMS: 300=เงียบ, 800=ฮอลล์ดัง):</label>
      <input type="number" id="ngate" name="ngate" min="200" max="2500" value=")rawliteral" + ngateStr + R"rawliteral(">
    </div>

    <div class="box-sec">
      <label for="vol" style="margin-top:0;">4. ระดับความดังลำโพงเสียงพูด:</label>
      <div class="vol-grid">
        <button type="button" id="bL" class="vol-preset" onclick="setVol(12)">🔈 เบา (12)</button>
        <button type="button" id="bM" class="vol-preset" onclick="setVol(18)">🔉 ปานกลาง (18)</button>
        <button type="button" id="bH" class="vol-preset" onclick="setVol(21)">🔊 ดังมาก (21)</button>
      </div>
      <input type="range" id="vol" name="vol" min="6" max="21" value=")rawliteral" + volStr + R"rawliteral(" style="width:100%;" oninput="syncVol(this.value)" onchange="fetch('/set_vol?vol='+this.value).catch(function(){})">
    </div>

    <label for="apikey">5. Gemini API Key (สำหรับโหมดปุ่มกดสำรอง 5 โหมด):</label>
    <input type="text" id="apikey" name="apikey" placeholder="เว้นว่างไว้หากใช้คีย์เดิม">
    <div class="hint">สถานะคีย์ปัจจุบัน: )rawliteral" + maskedKey + R"rawliteral(</div>

    <button type="submit" class="btn-save">✅ บันทึกและเริ่มใช้งาน SeeSpeak V3</button>
  </form>
</main>
<script>
function togglePw(id, btn){
  var el=document.getElementById(id);
  if(el.type==='password'){el.type='text';btn.innerText='🙈';}
  else{el.type='password';btn.innerText='👁️';}
}
function syncVol(v){v=parseInt(v,10);document.getElementById('vol').value=v;
  document.getElementById('bL').className='vol-preset'+(v<=14?' active':'');
  document.getElementById('bM').className='vol-preset'+(v>14&&v<=19?' active':'');
  document.getElementById('bH').className='vol-preset'+(v>=20?' active':'');}
function setVol(v){syncVol(v);fetch('/set_vol?vol='+v).catch(function(){});}
syncVol()rawliteral" + volStr + R"rawliteral();
</script>
</body></html>
)rawliteral";
    return html;
}

void startSetupPortal() {
    stopLiveMode();
    inSetupMode = true;
    Serial.println("\n=== เข้าสู่โหมดตั้งค่า SeeSpeak-Setup V3 ===");

    vibrate(2, 120, 100);
    delay(120);
    vibrate(1, 400, 0);

    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(SETUP_AP_NAME);
    IPAddress apIP = WiFi.softAPIP();
    dnsServer.start(53, "*", apIP);

    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html; charset=utf-8", buildSetupHtmlPage());
    });
    server.on("/seespeak.png", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "public, max-age=3600");
        server.send_P(200, "image/png", (const char*)SEESPEAK_LOGO_PNG, SEESPEAK_LOGO_PNG_LEN);
    });
    server.on("/set_vol", HTTP_GET, []() {
        if (server.hasArg("vol")) {
            int v = constrain(server.arg("vol").toInt(), 6, 21);
            savedVolume = v;
            if (audio != nullptr) audio->setVolume(savedVolume);
            vibrate(1, 45, 0);
        }
        server.send(200, "text/plain", "OK");
    });
    server.on("/save", HTTP_POST, []() {
        saveSettings(
            server.arg("ssid1"), server.arg("pass1"),
            server.arg("ssid2"), server.arg("pass2"),
            server.arg("ssid3"), server.arg("pass3"),
            server.arg("apikey"), server.arg("relay"),
            server.hasArg("vol") ? server.arg("vol").toInt() : savedVolume,
            server.hasArg("live_on"),
            server.hasArg("ngate") ? server.arg("ngate").toInt() : noiseGateRms
        );
        server.send(200, "text/html; charset=utf-8",
            "<!DOCTYPE html><html lang='th'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'></head>"
            "<body style='font-family:sans-serif;background:#0F172A;color:#FFF;text-align:center;padding:30px;'>"
            "<h2>✅ บันทึกการตั้งค่า SeeSpeak V3 สำเร็จ!</h2><p>กำลังรีสตาร์ทและเชื่อมต่อภายใน 3 วินาที...</p></body></html>");
        vibrate(2, 150, 100);
        delay(1800);
        ESP.restart();
    });
    server.onNotFound([]() {
        server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
        server.send(302, "text/plain", "");
    });
    server.begin();

    playOfflineSetupVoice();
    lastActivityTime = millis();
}

// ----------------------------------------------------------------------------
// 11. โมดูลกล้อง OV5640 (5MP 68° Fixed Focus) และ OV3660 (Auto-Detect)
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
    config.xclk_freq_hz = 12000000; // 12 MHz ลดภาระบัส PSRAM ร่วมกับ Wi-Fi/I2S ป้องกัน cam_hal: FB-OVF และลดความร้อน
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode    = CAMERA_GRAB_LATEST;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    config.frame_size   = FRAMESIZE_UXGA; // จองบัฟเฟอร์ PSRAM ใหญ่พอสำหรับโหมด OCR (1600x1200) แล้วพักที่ VGA ด้านล่าง
    config.jpeg_quality = 12;
    config.fb_count     = 2;

    esp_log_level_set("cam_hal", ESP_LOG_ERROR); // ปิดคำเตือน FB-OVF ชั่วคราวตอนบัส Wi-Fi หนาแน่น
    if (esp_camera_init(&config) != ESP_OK) {
        Serial.println("[Camera] เริ่มต้นกล้องไม่สำเร็จ!");
        return false;
    }

    sensor_t* s = esp_camera_sensor_get();
    if (s != nullptr) {
        Serial.printf("[Camera] ตรวจพบเซนเซอร์ PID: 0x%04X\n", s->id.PID);
        s->set_vflip(s, CAMERA_VFLIP);
        s->set_hmirror(s, CAMERA_HMIRROR);
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_exposure_ctrl(s, 1);
        s->set_aec2(s, 1);

        if (s->id.PID == OV5640_PID) {
            detectedCamName = "OV5640 5MP";
            Serial.println("[Camera] ปรับจูนโปรไฟล์เฉพาะสำหรับ OV5640 5MP (สีตรงธนบัตร + คมชัด)");
            s->set_brightness(s, 0);
            s->set_contrast(s, 1);
            s->set_saturation(s, 1); // สีสดชัดเจนสำหรับแยกธนบัตรไทย (เขียว/ฟ้า/แดง/ม่วง/เทา)
            s->set_sharpness(s, 2);
            s->set_gainceiling(s, GAINCEILING_4X);
        } else if (s->id.PID == OV3660_PID) {
            detectedCamName = "OV3660 3MP";
            Serial.println("[Camera] ปรับจูนโปรไฟล์สำหรับ OV3660 3MP (แก้สีเพี้ยนธนบัตร Saturation=+1)");
            s->set_brightness(s, 0);
            s->set_contrast(s, 1);
            s->set_saturation(s, 1); // แก้บั๊กเดิม (-2 ทำให้แบงก์ 20 สีเขียวซีดเป็นสีเทาน้ำตาลเหมือนแบงก์ 1000!)
            s->set_sharpness(s, 2);
            s->set_gainceiling(s, GAINCEILING_4X);
        }

        // พักความละเอียดที่ SVGA (800x600 quality 10) -> ภาพคมชัดกว่า VGA มาก เห็นเลขบนธนบัตรและตัวอักษรชัดขึ้น 2 เท่า!
        s->set_framesize(s, FRAMESIZE_SVGA);
        s->set_quality(s, 10);
    }
    return true;
}

camera_fb_t* captureSmartPhoto(bool highResMode = true, bool useFlash = true, framesize_t targetHighRes = FRAMESIZE_UXGA) {
    sensor_t* s = esp_camera_sensor_get();
    bool resized = false;
    if (s != nullptr) {
        framesize_t target = highResMode ? targetHighRes : FRAMESIZE_SVGA;
        int targetQ = highResMode ? ((targetHighRes == FRAMESIZE_UXGA) ? 12 : 10) : 10;
        if (s->status.framesize != target) {
            s->set_framesize(s, target);
            resized = true;
        }
        s->set_quality(s, targetQ);
    }

    if (useFlash) {
        digitalWrite(PIN_FLASH_LED, HIGH);
        delay(resized ? 140 : 100);
        for (int i = 0; i < 2; i++) {
            camera_fb_t* flush_fb = esp_camera_fb_get();
            if (flush_fb != nullptr) esp_camera_fb_return(flush_fb);
            delay(35);
        }
    } else {
        if (resized) delay(70);
        int flushCount = resized ? 2 : 1;
        for (int i = 0; i < flushCount; i++) {
            camera_fb_t* flush_fb = esp_camera_fb_get();
            if (flush_fb != nullptr) esp_camera_fb_return(flush_fb);
        }
    }

    camera_fb_t* fb = esp_camera_fb_get();
    digitalWrite(PIN_FLASH_LED, LOW);
    return fb;
}

// ----------------------------------------------------------------------------
// 12. ตัวกรองเสียงรบกวนในฮอลล์แข่ง (Hall Noise Filter: HPF 250Hz + AGC + RMS Detector)
// ----------------------------------------------------------------------------
float processHallAudioBlock(int16_t* samples, size_t count) {
    if (count == 0) return 0.0f;

    // 1. ตัด DC Offset เฉลี่ยของบล็อกไมโครโฟน PDM
    int32_t dcSum = 0;
    for (size_t i = 0; i < count; i++) dcSum += samples[i];
    float dcOffset = (float)dcSum / (float)count;

    // 2. High-Pass Filter (~250 Hz @ 16kHz, alpha = 0.905) ตัดเสียงฮัมแอร์/เสียงลม/เสียงก้องฮอลล์
    const float alpha = 0.905f;
    double sumSq = 0.0;
    float peak = 1.0f;

    for (size_t i = 0; i < count; i++) {
        float x = (float)samples[i] - dcOffset;
        float y = alpha * (hpfPrevOut + x - hpfPrevIn);
        hpfPrevIn  = x;
        hpfPrevOut = y;

        // ขยายเสียงด้วย Auto Gain Control (AGC) + Soft Limiter กันเสียงแตก
        float amplified = y * agcGain;
        if (amplified > 32000.0f)  amplified = 32000.0f;
        if (amplified < -32000.0f) amplified = -32000.0f;

        float absVal = fabsf(amplified);
        if (absVal > peak) peak = absVal;
        sumSq += (double)amplified * (double)amplified;
        samples[i] = (int16_t)amplified;
    }

    // ปรับค่า AGC Gain เฉพาะช่วงที่มีเสียงพูดจริง (ตอนเงียบล็อกไว้ที่ 2.8x ไม่ให้เร่งเสียงพัดลมขึ้นมาลั่นเอง!)
    if (peak > 24000.0f && agcGain > 1.8f) {
        agcGain -= 0.18f;
    } else if (peak >= 1800.0f && peak < 9000.0f && agcGain < 4.5f) {
        agcGain += 0.04f;
    } else if (peak < 1800.0f && agcGain > 2.8f) {
        agcGain -= 0.02f;
    }

    return (float)sqrt(sumSq / (double)count);
}

// ----------------------------------------------------------------------------
// 13. โหมดคุยสดไร้สัมผัส (Architecture 2B: Binary WebSocket -> Cloud Relay -> Gemini Live API)
// ----------------------------------------------------------------------------
bool sendLiveJpegFrame(bool highResWithFlash = false) {
    if (!liveWsConnected) return false;
    camera_fb_t* fb = highResWithFlash
        ? captureSmartPhoto(true, true, FRAMESIZE_XGA)   // โหมดปุ่มกด: เปิดแฟลช + XGA (1024x768) คมกริบ!
        : captureSmartPhoto(false, false, FRAMESIZE_SVGA); // โหมดคุยสด: SVGA (800x600) คมชัดและเร็ว!
    if (!fb || fb->len < 2000) {
        if (fb) esp_camera_fb_return(fb);
        return false;
    }

    const size_t MAX_CHUNK = 7500;
    static uint8_t txBuf[MAX_CHUNK + 1];
    size_t sent = 0;
    while (sent < fb->len) {
        size_t chunkLen = min(MAX_CHUNK, fb->len - sent);
        bool isLast = (sent + chunkLen >= fb->len);
        txBuf[0] = isLast ? MSG_ESP_JPEG_END : MSG_ESP_JPEG_CHUNK;
        memcpy(txBuf + 1, fb->buf + sent, chunkLen);
        liveWS.sendBIN(txBuf, chunkLen + 1);
        liveWS.loop(); // ปั๊ม WebSocket ระหว่างส่งภาพ กันสายค้าง/เสียงกระตุก
        sent += chunkLen;
    }
    Serial.printf("[Live 2B] ส่งภาพ%s %s (%u bytes) ขึ้น Relay สำเร็จ\n",
                  highResWithFlash ? "คมชัดสูง+แฟลช XGA" : "สด SVGA",
                  detectedCamName.c_str(), fb->len);
    esp_camera_fb_return(fb);

    // หลังส่งภาพ XGA โหมดปุ่มกด ให้สลับกล้องพักที่ SVGA ทันทีเพื่อประหยัดบัส PSRAM
    if (highResWithFlash) {
        if (sensor_t* s = esp_camera_sensor_get()) {
            s->set_framesize(s, FRAMESIZE_SVGA);
            s->set_quality(s, 10);
        }
    }
    liveLastFrameMs = millis();
    return true;
}

void cancelLiveAiSpeech(const char* reason) {
    clearLiveAudioRing();
    liveAiStreamOpen        = false;
    liveAiSpeaking          = false;
    liveIgnoreAudioUntilNew = true; // ทิ้งแพ็กเก็ตเสียงที่ยังลอยค้างในอากาศจนกว่าจะเริ่มเทิร์นใหม่
    liveAiEndMs             = millis();
    lastAiCancelMs          = millis();
    liveBargeHitCount       = 0;
    liveEchoPeakRms         = 0.0f;
    preRollFilled           = 0;
    preRollIdx              = 0;
    if (liveWsConnected) {
        uint8_t cancelSig[1] = { MSG_ESP_CANCEL_AI };
        liveWS.sendBIN(cancelSig, 1);
    }
    vibrate(1, 45, 0);
    Serial.printf("[Live 2B] %s -> สั่งหยุดเสียง AI และล้างคิวทันที!\n", reason);
}

void onLiveWsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            liveWsConnected         = true;
            liveAiStreamOpen        = false;
            liveAiSpeaking          = false;
            liveIgnoreAudioUntilNew = false;
            liveUserSpeaking        = false;
            liveFrameCapturedForTurn = false;
            liveBargeHitCount       = 0;
            liveEchoPeakRms         = 0.0f;
            clearLiveAudioRing();
            liveConnectedMs  = millis();
            liveFailStreak   = 0;
            liveWS.setReconnectInterval(1500); // เชื่อมใหม่เร็ว 1.5 วินาทีหากสัญญาณสะดุด
            Serial.println("[Live 2B] เชื่อมต่อ Cloud Relay v3.7 -> Gemini Live API สำเร็จ! พูดถามได้เลยไม่ต้องกดปุ่ม");
            if (!liveAnnouncedConnect) {
                liveAnnouncedConnect = true;
                vibrate(1, 90, 0);
            }
            // ส่งภาพมุมมองแรก SVGA ขึ้นไปทันทีเพื่อให้ AI เห็นสิ่งตรงหน้าล่วงหน้า
            sendLiveJpegFrame(false);
            break;

        case WStype_DISCONNECTED: {
            bool wasExpected = liveExpectedDisconnect;
            bool wasStable   = liveWsConnected && (millis() - liveConnectedMs > 15000);
            liveExpectedDisconnect  = false;
            liveWsConnected         = false;
            liveAiStreamOpen        = false;
            liveAiSpeaking          = false;
            liveIgnoreAudioUntilNew = false;
            liveUserSpeaking        = false;
            liveBargeHitCount       = 0;
            clearLiveAudioRing(); // หยุดเสียงค้างทันทีเมื่อสายหลุด
            if (wasExpected) {
                Serial.println("[Live 2B] พักท่อคุยสดชั่วคราว (สลับไปทำงานโหมดปุ่มกด)");
            } else if (wasStable) {
                liveFailStreak = 0;
                liveWS.setReconnectInterval(1500);
                Serial.println("[Live 2B] การเชื่อมต่อ Relay หลุด (กำลังลองต่อใหม่อัตโนมัติใน 1.5 วิ...)");
            } else {
                liveFailStreak++;
                if (liveFailStreak >= 3) {
                    liveWS.setReconnectInterval(15000);
                    Serial.println("[Live 2B] เชื่อมต่อ Relay ไม่สำเร็จต่อเนื่อง -> ตรวจ GEMINI_API_KEY บน Render.com (/test-key) พักรอ 15 วิ...");
                } else {
                    Serial.println("[Live 2B] การเชื่อมต่อ Relay หลุด (กำลังลองต่อใหม่อัตโนมัติ...)");
                }
            }
            break;
        }

        case WStype_BIN:
            if (length == 0) break;
            if (payload[0] == MSG_AI_SPEAK_START) {
                liveIgnoreAudioUntilNew = false;
                liveAiStreamOpen        = true;
                liveAiSpeaking          = true;
                liveAiSpeakStartMs      = millis();
                liveEchoPeakRms         = 0.0f;
                liveBargeHitCount       = 0;
                liveUserSpeaking        = false;
                lastActivityTime        = millis();
            } else if (payload[0] == MSG_AI_AUDIO_PCM && length > 1) {
                if (liveIgnoreAudioUntilNew) break; // ทิ้งแพ็กเก็ตเก่าที่สั่งหยุดไปแล้ว 100%
                liveAiStreamOpen = true;
                liveAiSpeaking   = true;
                // เขียนลง PSRAM Ring Buffer ทันที (< 50 us) ไม่บล็อก WebSocket loop แม้พูดยาว ก-ฮ!
                pushLiveAudioRing(payload + 1, length - 1);
                lastActivityTime = millis();
            } else if (payload[0] == MSG_AI_SPEAK_END) {
                // ปิดสถานะรับสตรีม แต่ปล่อยให้ liveSpeakerTask เล่นจนหมดบัฟเฟอร์ก่อนค่อยเปิดไมค์
                liveAiStreamOpen        = false;
                liveIgnoreAudioUntilNew = false;
                lastActivityTime        = millis();
            }
            break;

        default:
            break;
    }
}

bool parseRelayUrl(const String& url, bool& useSsl, String& host, uint16_t& port, String& path) {
    String u = url;
    u.trim();
    if (u.startsWith("wss://")) {
        useSsl = true; port = 443; u = u.substring(6);
    } else if (u.startsWith("ws://")) {
        useSsl = false; port = 80; u = u.substring(5);
    } else {
        return false;
    }
    int slashIdx = u.indexOf('/');
    String hostPort = (slashIdx >= 0) ? u.substring(0, slashIdx) : u;
    path = (slashIdx >= 0) ? u.substring(slashIdx) : "/ws/seespeak";

    int colonIdx = hostPort.indexOf(':');
    if (colonIdx >= 0) {
        host = hostPort.substring(0, colonIdx);
        port = (uint16_t)hostPort.substring(colonIdx + 1).toInt();
    } else {
        host = hostPort;
    }
    return host.length() > 0;
}

void startLiveModeIfConfigured() {
    if (!handsFreeEnabled || savedRelayUrl.length() < 8 || WiFi.status() != WL_CONNECTED) return;

    bool useSsl = true;
    String host, path;
    uint16_t port = 443;
    if (!parseRelayUrl(savedRelayUrl, useSsl, host, port, path)) {
        Serial.println("[Live 2B] รูปแบบ Relay URL ไม่ถูกต้อง (ต้องขึ้นต้นด้วย wss:// หรือ ws://)");
        return;
    }

    if (!startLiveSpeakerI2S()) return;

    if (!liveMicRunning) {
        livePdmMic.setPinsPdmRx(PIN_PDM_CLK, PIN_PDM_DATA);
        if (livePdmMic.begin(I2S_MODE_PDM_RX, MIC_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
            liveMicRunning = true;
            // ทิ้งเสียง Pop ตอนเปิดไมค์ PDM (~250 ms) และรีเซ็ตฟิลเตอร์เพื่อไม่ให้ลั่นพูดเองตอนเริ่มเชื่อม
            char dummy[512];
            for (int i = 0; i < 8; i++) livePdmMic.readBytes(dummy, sizeof(dummy));
            hpfPrevIn = 0.0f;
            hpfPrevOut = 0.0f;
            agcGain = 2.6f;
            ambientNoiseRms = 260.0f;
            liveTurnPeakRms = 0.0f;
            liveVoiceHitCount = 0;
            liveSpokenBlocks = 0;
            preRollIdx = 0;
            preRollFilled = 0;
        }
    }

    if (useSsl) {
        liveWS.beginSSL(host.c_str(), port, path.c_str());
    } else {
        liveWS.begin(host.c_str(), port, path.c_str());
    }
    liveAnnouncedConnect   = false;
    liveExpectedDisconnect = false;
    liveWS.onEvent(onLiveWsEvent);
    liveWS.setReconnectInterval(1500);
    liveWS.enableHeartbeat(20000, 15000, 4); // ปิงทุก 20 วิ รอ Pong 15 วิ รองรับ 4G มือถือและไม่ตัดสายง่าย!
    Serial.printf("[Live 2B] กำลังเชื่อม Relay %s:%u%s\n", host.c_str(), port, path.c_str());
}

void stopLiveMode() {
    liveExpectedDisconnect  = true;
    liveWS.disconnect();
    liveWsConnected         = false;
    liveAiStreamOpen        = false;
    liveAiSpeaking          = false;
    liveIgnoreAudioUntilNew = false;
    liveUserSpeaking        = false;
    liveFrameCapturedForTurn = false;
    liveBargeHitCount       = 0;
    liveAnnouncedConnect    = false;
    if (liveMicRunning) {
        livePdmMic.end();
        liveMicRunning = false;
    }
    stopLiveSpeakerI2S();
}

void serviceLiveHandsFreeLoop() {
    if (!handsFreeEnabled || savedRelayUrl.length() < 8 || WiFi.status() != WL_CONNECTED) return;

    // ปั๊ม WebSocket หลายรอบเมื่อ AI กำลังพูดเพื่อดูดแพ็กเก็ตเสียงลง PSRAM Ring Buffer ทันที (แก้เสียงกระตุก!)
    int wsPump = (liveAiSpeaking || liveAiStreamOpen) ? 6 : 2;
    for (int i = 0; i < wsPump; i++) {
        liveWS.loop();
    }

    if (!liveWsConnected || !liveMicRunning) return;

    // 1. อ่านเสียงจากไมค์ PDM: ขณะ AI พูดอ่านทีละ 512 bytes (16ms) เพื่อไม่บล็อก Core 1 นานจนรับแพ็กเก็ตเสียงไม่ทัน!
    static uint8_t audioPacket[1025];
    int16_t* samples = (int16_t*)(audioPacket + 1);
    size_t reqBytes = (liveAiSpeaking || liveAiStreamOpen) ? 512 : 1024;
    size_t bytesRead = livePdmMic.readBytes((char*)samples, reqBytes);

    // ปั๊ม WebSocket อีกรอบทันทีหลังอ่านไมค์จบ
    if (liveAiSpeaking || liveAiStreamOpen) {
        for (int i = 0; i < 4; i++) liveWS.loop();
    } else {
        liveWS.loop();
    }

    if (bytesRead < 256) return;

    // 2. ผ่านตัวกรองเสียงรบกวนในฮอลล์แข่ง (HPF 250Hz + AGC + วัดความดัง RMS)
    float rms = processHallAudioBlock(samples, bytesRead / 2);

    // 3. ขณะ AI กำลังพูด -> ระบบ Voice Barge-In แบบเรียนรู้อัตโนมัติ (พูดแทรกดังชัดว่า "หยุดๆ/พอๆ" แล้วหยุดทันที!)
    if (liveAiSpeaking) {
        preRollFilled     = 0;
        preRollIdx        = 0;
        liveVoiceHitCount = 0;
        unsigned long speakingFor = millis() - liveAiSpeakStartMs;
        if (speakingFor < 650) {
            // ช่วง 0.65 วินาทีแรก เรียนรู้ระดับเสียงสะท้อนของลำโพงเองที่ระดับความดังปัจจุบัน
            if (rms > liveEchoPeakRms) liveEchoPeakRms = rms;
        } else {
            // หลัง 0.65 วิ หากเสียงผู้ใช้ดังทะลุเสียงสะท้อนของลำโพง (≥ 1.7 เท่า และ ≥ 1600 RMS) ต่อเนื่อง ~112 ms (7x16ms) -> สั่งหยุดทันที!
            float bargeThreshold = max(1600.0f, liveEchoPeakRms * 1.70f);
            if (rms >= bargeThreshold) {
                liveBargeHitCount++;
                if (liveBargeHitCount >= 7) {
                    cancelLiveAiSpeech("ตรวจพบเสียงพูดแทรก (Voice Barge-In)");
                }
            } else {
                liveBargeHitCount = 0;
            }
        }
        return;
    }

    // ช่วงพักกันเสียงสะท้อนหลังลำโพงหยุดพูด (500 ms)
    if (millis() - liveAiEndMs < LIVE_ECHO_COOLDOWN_MS) {
        preRollFilled     = 0;
        preRollIdx        = 0;
        liveVoiceHitCount = 0;
        return;
    }

    // 4. Adaptive Noise Floor: เรียนรู้พื้นเสียงรบกวนจริง (พัดลม/แอร์/ฮอลล์แข่ง) เพื่อไม่ให้ลั่นตอบเองตอนเงียบ!
    if (!liveUserSpeaking) {
        if (rms < ambientNoiseRms * 1.45f) {
            ambientNoiseRms = ambientNoiseRms * 0.94f + rms * 0.06f;
        } else {
            ambientNoiseRms = ambientNoiseRms * 0.992f + rms * 0.008f;
        }
        ambientNoiseRms = constrain(ambientNoiseRms, 130.0f, 1600.0f);
    }

    // เกณฑ์เริ่มพูดเข้มงวดขึ้น (2.4x + 220 RMS และต้องดังต่อเนื่อง >= 160 ms) ป้องกันเสียงลม/พัดลม/คลิก ลั่นถาม AI ซ้ำ!
    float startThresh = max((float)noiseGateRms, ambientNoiseRms * 2.4f + 220.0f);
    float holdThresh  = max((float)noiseGateRms * 0.85f, ambientNoiseRms * 1.55f + 110.0f);
    bool isSpeechNow  = liveUserSpeaking ? (rms >= holdThresh) : (rms >= startThresh);

    if (isSpeechNow) {
        liveVoiceHitCount++;
        if (rms > liveTurnPeakRms) liveTurnPeakRms = rms;
        if (!liveUserSpeaking) {
            // เก็บลง Pre-Roll ระหว่างรอครบ 5 บล็อก (160 ms) เพื่อให้พยางค์แรกครบถ้วนและกรองเสียงกระแทกสั้นๆ ทิ้ง!
            memcpy(preRollBuf[preRollIdx], samples, bytesRead);
            preRollLens[preRollIdx] = bytesRead;
            preRollIdx = (preRollIdx + 1) % PRE_ROLL_BLOCKS;
            if (preRollFilled < PRE_ROLL_BLOCKS) preRollFilled++;

            if (liveVoiceHitCount >= LIVE_MIN_VOICE_HITS) {
                liveUserSpeaking         = true;
                liveFrameCapturedForTurn = false;
                liveSpokenBlocks         = 0;
                liveTurnPeakRms          = rms;
                liveLastVoiceMs          = millis();
                lastActivityTime         = millis();
                // ส่งเสียง Pre-Roll (~320 ms) ขึ้นไปก่อนทันที (ไม่หยุดถ่ายรูปกลางคำพูด!)
                int startIdx = (preRollIdx - preRollFilled + PRE_ROLL_BLOCKS) % PRE_ROLL_BLOCKS;
                for (int k = 0; k < preRollFilled; k++) {
                    int idx = (startIdx + k) % PRE_ROLL_BLOCKS;
                    if (preRollLens[idx] > 0) {
                        audioPacket[0] = MSG_ESP_AUDIO_PCM;
                        memcpy(audioPacket + 1, preRollBuf[idx], preRollLens[idx]);
                        liveWS.sendBIN(audioPacket, preRollLens[idx] + 1);
                        liveSpokenBlocks++;
                    }
                }
                preRollFilled = 0;
                preRollIdx    = 0;
            }
        } else {
            liveLastVoiceMs          = millis();
            liveFrameCapturedForTurn = false; // ผู้ใช้ยังพูดต่อ รอถ่ายภาพล่าสุดตอนหยุดพูด
            audioPacket[0] = MSG_ESP_AUDIO_PCM;
            liveWS.sendBIN(audioPacket, bytesRead + 1);
            liveSpokenBlocks++;
            // ป้องกันการอัดค้างเกิน 12 วินาที (375 บล็อก) ในพื้นที่เสียงดังมาก
            if (liveSpokenBlocks >= LIVE_MAX_SPEECH_BLOCKS) {
                liveUserSpeaking         = false;
                liveFrameCapturedForTurn = false;
                if (millis() - liveLastFrameMs > 250) sendLiveJpegFrame(false);
                uint8_t endSig[1] = { MSG_ESP_SPEECH_END };
                liveWS.sendBIN(endSig, 1);
                Serial.printf("[Live 2B] พูดครบระยะสูงสุด (%d บล็อก, พื้นเสียง=%.0f, พีค=%.0f) -> ส่งให้ AI ตอบ!\n",
                              liveSpokenBlocks, ambientNoiseRms, liveTurnPeakRms);
                liveSpokenBlocks = 0;
            }
        }
    } else {
        liveVoiceHitCount = 0;
        if (!liveUserSpeaking) {
            // เก็บเสียงเงียบ/ต้นคำลง Pre-Roll วนไว้ตลอดเวลา (~320 ms)
            memcpy(preRollBuf[preRollIdx], samples, bytesRead);
            preRollLens[preRollIdx] = bytesRead;
            preRollIdx = (preRollIdx + 1) % PRE_ROLL_BLOCKS;
            if (preRollFilled < PRE_ROLL_BLOCKS) preRollFilled++;
        } else {
            // ส่งเสียงช่วงท้ายคำต่ออีกเล็กน้อย
            audioPacket[0] = MSG_ESP_AUDIO_PCM;
            liveWS.sendBIN(audioPacket, bytesRead + 1);
            liveSpokenBlocks++;

            unsigned long silentFor = millis() - liveLastVoiceMs;
            // ถ่ายภาพสด SVGA คมชัด (สีสมจริง) ซ้อนทับในช่วงเงียบ 150ms เพื่อให้ AI เห็นวัตถุ/ธนบัตรล่าสุดเป๊ะ!
            if (!liveFrameCapturedForTurn && silentFor >= 150) {
                liveFrameCapturedForTurn = true;
                if (millis() - liveLastFrameMs > 250) {
                    sendLiveJpegFrame(false);
                }
            }
            if (silentFor >= LIVE_SILENCE_END_MS) {
                liveUserSpeaking         = false;
                liveFrameCapturedForTurn = false;
                uint8_t endSig[1] = { MSG_ESP_SPEECH_END };
                liveWS.sendBIN(endSig, 1);
                Serial.printf("[Live 2B] พูดจบแล้ว (%d บล็อก / %.1f วิ, พื้นเสียง=%.0f, พีค=%.0f) -> รอ Gemini Live ตอบเสียงสด...\n",
                              liveSpokenBlocks, (liveSpokenBlocks * 32.0f) / 1000.0f, ambientNoiseRms, liveTurnPeakRms);
                liveSpokenBlocks = 0;
            }
        }
    }
}

// ----------------------------------------------------------------------------
// 14. โหมดปุ่มกดสำรอง 5 โหมด (Hybrid Fallback — ยิงตรงเข้า Gemini 3.5 Flash-Lite)
// ----------------------------------------------------------------------------
void writeWavHeader(uint8_t* header, uint32_t pcmDataSize, uint32_t sampleRate = VOICE_WAV_RATE) {
    uint32_t fileSize   = pcmDataSize + 36;
    uint16_t numCh      = 1;
    uint16_t bitsPerSmp = 16;
    uint32_t byteRate   = sampleRate * numCh * (bitsPerSmp / 8);
    uint16_t blockAlign = numCh * (bitsPerSmp / 8);
    memcpy(header, "RIFF", 4);        memcpy(header + 4, &fileSize, 4);
    memcpy(header + 8, "WAVEfmt ", 8);
    uint32_t sub1 = 16; uint16_t fmt = 1;
    memcpy(header + 16, &sub1, 4);    memcpy(header + 20, &fmt, 2);
    memcpy(header + 22, &numCh, 2);   memcpy(header + 24, &sampleRate, 4);
    memcpy(header + 28, &byteRate, 4);memcpy(header + 32, &blockAlign, 2);
    memcpy(header + 34, &bitsPerSmp, 2);
    memcpy(header + 36, "data", 4);   memcpy(header + 40, &pcmDataSize, 4);
}

String recordQuestionWavBase64() {
    const uint32_t maxOutPcmBytes = (VOICE_WAV_RATE * 2 * MIC_MAX_MS) / 1000;
    uint8_t* wavBuf = (uint8_t*)ps_malloc(44 + maxOutPcmBytes);
    if (!wavBuf) return "";

    I2SClass pdmMic;
    pdmMic.setPinsPdmRx(PIN_PDM_CLK, PIN_PDM_DATA);
    if (!pdmMic.begin(I2S_MODE_PDM_RX, MIC_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        free(wavBuf);
        return "";
    }

    int16_t rawBlock[512];
    for (int i = 0; i < 6; i++) pdmMic.readBytes((char*)rawBlock, sizeof(rawBlock));

    unsigned long recStart = millis();
    uint32_t totalPcmBytes = 0;
    int16_t* outSamplesPtr = (int16_t*)(wavBuf + 44);

    while (totalPcmBytes + 512 <= maxOutPcmBytes) {
        if (digitalRead(PIN_BUTTON) == HIGH) {
            delay(15);
            if (digitalRead(PIN_BUTTON) == HIGH) break;
        }
        if (millis() - recStart >= MIC_MAX_MS) break;

        size_t bytesRead = pdmMic.readBytes((char*)rawBlock, sizeof(rawBlock));
        if (bytesRead >= 4) {
            size_t sampleCount = bytesRead / 2;
            // กรองเสียงรบกวนในฮอลล์แข่ง (HPF 250Hz + AGC) แล้วลดอัตราเป็น 8kHz
            processHallAudioBlock(rawBlock, sampleCount);
            size_t outIdx = totalPcmBytes / 2;
            for (size_t s = 0; s + 1 < sampleCount; s += 2) {
                outSamplesPtr[outIdx++] = (int16_t)(((int32_t)rawBlock[s] + (int32_t)rawBlock[s + 1]) / 2);
            }
            totalPcmBytes = outIdx * 2;
        }
    }
    pdmMic.end();

    if (totalPcmBytes < 800) { free(wavBuf); return ""; }
    writeWavHeader(wavBuf, totalPcmBytes, VOICE_WAV_RATE);
    String base64Wav = base64::encode(wavBuf, 44 + totalPcmBytes);
    free(wavBuf);
    return base64Wav;
}

int postGeminiRequest(const char* endpointBase, const String& payload, String& responseOut) {
    if (!geminiClientInitialized) {
        geminiClient.setInsecure();
        geminiClient.setTimeout(12);
        geminiClientInitialized = true;
    }
    geminiHttp.setReuse(true);
    geminiHttp.setTimeout(12000);

    String url = String(endpointBase) + savedApiKey;
    if (!geminiHttp.begin(geminiClient, url)) return -100;
    geminiHttp.addHeader("Content-Type", "application/json");
    geminiHttp.addHeader("Connection", "keep-alive");

    int httpCode = geminiHttp.POST(payload);
    if (httpCode < 0) {
        geminiHttp.end();
        geminiClient.stop();
        if (geminiHttp.begin(geminiClient, url)) {
            geminiHttp.addHeader("Content-Type", "application/json");
            geminiHttp.addHeader("Connection", "keep-alive");
            httpCode = geminiHttp.POST(payload);
        }
    }
    if (httpCode > 0) responseOut = geminiHttp.getString();
    else geminiHttp.end();
    return httpCode;
}

void processWithGemini(const char* modePrompt, String base64AudioWav = "", bool includeMemory = false) {
    // พักโหมด Live ชั่วคราวเพื่อให้กล้องความละเอียดสูง UXGA และลำโพง TTS ทำงานเต็มที่
    bool wasLiveActive = (handsFreeEnabled && savedRelayUrl.length() >= 8);
    if (wasLiveActive) stopLiveMode();

    if (!ensureWiFiConnected()) {
        if (wasLiveActive) startLiveModeIfConfigured();
        return;
    }

    rtcUsedMah += (base64AudioWav.length() > 0) ? 1.5f : 1.2f;

    camera_fb_t* fb = captureSmartPhoto(true, true, FRAMESIZE_UXGA);
    if (!fb || fb->len < 4500) {
        if (fb) esp_camera_fb_return(fb);
        speakThai("ภาพมืดเกินไปหรือมีนิ้วบังหน้ากล้อง กรุณากดใหม่อีกครั้งค่ะ");
        if (wasLiveActive) startLiveModeIfConfigured();
        return;
    }
    vibrate(1, 60, 0);

    String base64Image = base64::encode(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    // สลับกล้องพักไว้ที่ SVGA คมชัด (ประหยัดบัส PSRAM และพร้อมสำหรับ Live ทันที)
    if (sensor_t* s = esp_camera_sensor_get()) {
        s->set_framesize(s, FRAMESIZE_SVGA);
        s->set_quality(s, 10);
    }

    String fullPrompt = String(modePrompt);
    if (includeMemory) fullPrompt += buildMemoryContextString();

    String payload, response;
    int httpCode = -1;
    for (int idx = 0; idx < GEMINI_MODEL_COUNT; idx++) {
        payload = "";
        payload.reserve(base64Image.length() + base64AudioWav.length() + 1024);
        payload = "{\"contents\":[{\"parts\":[{\"text\":\"";
        payload += fullPrompt;
        payload += "\"},";
        if (base64AudioWav.length() > 0) {
            payload += "{\"inline_data\":{\"mime_type\":\"audio/wav\",\"data\":\"";
            payload += base64AudioWav;
            payload += "\"}},";
        }
        payload += "{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"";
        payload += base64Image;
        payload += "\"}}]}],\"generationConfig\":{";
        payload += GEMINI_THINKING_CONFIGS[idx];
        payload += "\"maxOutputTokens\":250,\"temperature\":0.2}}";

        response = "";
        httpCode = postGeminiRequest(GEMINI_ENDPOINTS[idx], payload, response);
        if (httpCode == HTTP_CODE_OK) break;
    }
    payload = ""; base64Image = ""; base64AudioWav = "";

    if (httpCode == HTTP_CODE_OK) {
        JsonDocument doc;
        if (!deserializeJson(doc, response)) {
            String aiAnswer;
            JsonArray parts = doc["candidates"][0]["content"]["parts"];
            for (JsonObject part : parts) {
                if (!part["thought"].as<bool>() && part["text"].is<const char*>()) {
                    aiAnswer += part["text"].as<const char*>();
                }
            }
            aiAnswer.trim();
            if (aiAnswer.length() > 0) {
                rememberAnswer(aiAnswer);
                speakThai(aiAnswer);
            } else {
                speakThai("ไม่สามารถอ่านผลลัพธ์ได้ กรุณากดใหม่อีกครั้งค่ะ");
            }
        }
    } else {
        Serial.printf("[Gemini REST] เชื่อมต่อไม่สำเร็จ HTTP %d\n", httpCode);
        if (httpCode == 401 || httpCode == 403) {
            speakThai("คีย์เอไอไม่ถูกต้องหรือหมดอายุ กรุณาอัปเดตคีย์ใหม่ในหน้าตั้งค่าซีสปีคค่ะ");
        } else if (httpCode == 429) {
            speakThai("ใช้งานถี่เกินไปชั่วคราว กรุณารอสักครู่แล้วกดใหม่ค่ะ");
        } else {
            speakThai("ไม่สามารถเชื่อมต่อเซิร์ฟเวอร์เอไอได้ กรุณาตรวจสอบอินเทอร์เน็ตค่ะ");
        }
    }

    checkBatteryAndWarn();
    if (wasLiveActive) startLiveModeIfConfigured();
}

// สั่งงานปุ่มกดโหมด 1/2/3 แบบ Hybrid Live-First:
// - หากเชื่อม Gemini Live อยู่: ถ่ายภาพคมชัดสูง+แฟลช XGA แล้วส่งเข้าท่อ Live ทันที (~0.8-1.2 วิ ไม่ตัดสาย เสียงน่ารักคนเดิม!)
// - หากพักโหมด Live (กด 5 ครั้ง) หรือ Relay ออฟไลน์: สลับไปยิงตรง Gemini REST API อัตโนมัติ 100%!
void triggerLiveButtonMode(uint8_t modeId, const char* fallbackPrompt) {
    lastActivityTime = millis();
    if (handsFreeEnabled && liveWsConnected) {
        if (liveAiSpeaking) {
            cancelLiveAiSpeech("กดเปลี่ยนโหมดปุ่มกด");
            unsigned long waitStart = millis();
            while (millis() - waitStart < 40) { liveWS.loop(); delay(5); }
        }
        liveUserSpeaking         = false;
        liveFrameCapturedForTurn = false;
        liveVoiceHitCount        = 0;
        preRollFilled            = 0;
        preRollIdx               = 0;

        rtcUsedMah += 0.9f;
        if (sendLiveJpegFrame(true) && liveWsConnected) {
            uint8_t cmd[2] = { MSG_ESP_BUTTON_CMD, modeId };
            liveWS.sendBIN(cmd, 2);
            liveWS.loop();
            vibrate(1, 50, 0);
            Serial.printf("[Live 2B] สั่งโหมดปุ่มกด %u ผ่าน Gemini Live (แฟลช+XGA ไม่ตัดสาย!)\n", modeId);
            return;
        }
        Serial.printf("[Live 2B] ส่งภาพโหมด %u ไม่สำเร็จ -> สลับไปใช้ REST Fallback อัตโนมัติ\n", modeId);
    }
    processWithGemini(fallbackPrompt);
}

void handleVoiceQAMode() {
    lastActivityTime = millis();
    // 1. Live Push-to-Talk (Zero-Disconnect): กดค้างส่งเสียงสดตรงเข้า Gemini Live ทะลุ Noise Gate + แฟลช XGA ตอนปล่อยปุ่ม!
    if (handsFreeEnabled && liveWsConnected && liveMicRunning) {
        if (liveAiSpeaking) {
            cancelLiveAiSpeech("กดค้างถามใหม่");
            unsigned long waitStart = millis();
            while (millis() - waitStart < 40) { liveWS.loop(); delay(5); }
        }
        liveUserSpeaking         = false;
        liveFrameCapturedForTurn = false;
        liveVoiceHitCount        = 0;
        preRollFilled            = 0;
        preRollIdx               = 0;

        vibrate(1, 100, 0);
        delay(25);
        // ทิ้งเสียงมอเตอร์สั่น 2 บล็อกก่อนเริ่มส่งเสียงพูดจริง
        static uint8_t pttPacket[1025];
        int16_t* samples = (int16_t*)(pttPacket + 1);
        for (int i = 0; i < 2; i++) livePdmMic.readBytes((char*)samples, 1024);

        unsigned long recStart = millis();
        size_t totalSentBytes = 0;
        while (millis() - recStart < MIC_MAX_MS && liveWsConnected) {
            liveWS.loop();
            if (digitalRead(PIN_BUTTON) == HIGH) {
                delay(15);
                if (digitalRead(PIN_BUTTON) == HIGH) break;
            }
            size_t bytesRead = livePdmMic.readBytes((char*)samples, 1024);
            if (bytesRead >= 256) {
                processHallAudioBlock(samples, bytesRead / 2);
                pttPacket[0] = MSG_ESP_AUDIO_PCM;
                liveWS.sendBIN(pttPacket, bytesRead + 1);
                totalSentBytes += bytesRead;
            }
        }
        if (totalSentBytes >= 4800 && liveWsConnected) {
            rtcUsedMah += 1.0f;
            sendLiveJpegFrame(true); // แชะภาพคมชัดสูง+แฟลช XGA ตรงจังหวะปล่อยปุ่ม!
            uint8_t endSig[1] = { MSG_ESP_SPEECH_END };
            liveWS.sendBIN(endSig, 1);
            liveWS.loop();
            vibrate(1, 50, 0);
            Serial.printf("[Live 2B] Push-to-Talk ส่งเสียง %u bytes + ภาพแฟลช XGA สำเร็จ!\n", (unsigned)totalSentBytes);
            return;
        }
        if (liveWsConnected) {
            // กดสั้นเกินไปจนยังไม่ได้พูดอะไร
            uint8_t endSig[1] = { MSG_ESP_SPEECH_END };
            liveWS.sendBIN(endSig, 1);
            return;
        }
    }

    // 2. Auto REST Fallback เมื่อพักโหมด Live หรือ Relay ออฟไลน์
    bool wasLiveActive = (handsFreeEnabled && savedRelayUrl.length() >= 8);
    if (wasLiveActive) stopLiveMode();
    if (!ensureWiFiConnected()) return;

    vibrate(1, 150, 0);
    String wavBase64 = recordQuestionWavBase64();
    processWithGemini(PROMPT_MODE_4_QA, wavBase64, true); // ส่งพร้อมความจำบทสนทนา 3 รอบล่าสุด
}

// ----------------------------------------------------------------------------
// 15. ตรวจจับการกดปุ่มเดียว (1/2/3/4/5 คลิก และกดค้าง) + โหมด Deep Sleep
// ----------------------------------------------------------------------------
int readButtonAction() {
    if (digitalRead(PIN_BUTTON) == HIGH) return 0;

    unsigned long pressStart = millis();
    delay(30);
    while (digitalRead(PIN_BUTTON) == LOW) {
        if (millis() - pressStart >= 750) return 4; // กดค้าง -> โหมด 4 Push-to-Talk
        delay(10);
    }

    int clickCount = 1;
    unsigned long lastRelease = millis();
    while (millis() - lastRelease < 480) {
        if (digitalRead(PIN_BUTTON) == LOW) {
            delay(30);
            while (digitalRead(PIN_BUTTON) == LOW) delay(10);
            clickCount++;
            lastRelease = millis();
            if (clickCount >= 5) return 6; // กด 5 ครั้ง -> เปิด/ปิดโหมดคุยสด Hands-Free
        }
        delay(10);
    }
    return (clickCount == 4) ? 5 : clickCount;
}

void enterDeepSleep() {
    stopLiveMode();
    if (WiFi.status() == WL_CONNECTED) {
        speakThai("เข้าสู่โหมดประหยัดพลังงาน กดปุ่มหนึ่งครั้งเพื่อปลุกเครื่องค่ะ");
    }
    releaseAudioLib();
    vibrate(2, 80, 80);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BUTTON, 0);
    esp_deep_sleep_start();
}

// ----------------------------------------------------------------------------
// 16. SETUP & LOOP
// ----------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(PIN_FLASH_LED, OUTPUT);
    pinMode(PIN_VIB_MOTOR, OUTPUT);
    digitalWrite(PIN_FLASH_LED, LOW);
    digitalWrite(PIN_VIB_MOTOR, LOW);

    if (esp_reset_reason() == ESP_RST_BROWNOUT) {
        savedVolume = 15;
        playOfflineLowBatteryVoice();
        releaseAudioLib();
        WiFi.mode(WIFI_OFF);
        esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BUTTON, 0);
        esp_deep_sleep_start();
        return;
    }

    bool wokeFromSleep = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0);
    vibrate(1, 180, 0);

    if (digitalRead(PIN_BUTTON) == LOW) {
        unsigned long releaseWait = millis();
        while (digitalRead(PIN_BUTTON) == LOW && (millis() - releaseWait < 1200)) delay(20);
    }

    initBatteryMonitor();
    loadSettings();
    ensureAudioLibReady();
    bool camOk = initCamera();

    if (connectBestWiFi(6500)) {
        configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com"); // ซิงค์เวลาให้ TLS WebSocket (wss://) เสถียร
        int batPct = getBatteryPercent();
        if (!camOk) {
            speakThai("ตรวจไม่พบโมดูลกล้อง กรุณาตรวจสอบสายแพกล้องค่ะ");
        } else if (wokeFromSleep) {
            vibrate(2, 100, 80);
            speakThai("ซีสปีคพร้อมใช้งาน แบตเตอรี่คงเหลือ " + String(batPct) + " เปอร์เซ็นต์ค่ะ");
        } else {
            vibrate(2, 100, 80);
            String msg = "สวัสดีค่ะ แว่นตาอัจฉริยะซีสปีคพร้อมใช้งาน แบตเตอรี่คงเหลือ " + String(batPct) + " เปอร์เซ็นต์ ";
            if (handsFreeEnabled && savedRelayUrl.length() >= 8) {
                msg += "เปิดโหมดคุยสดไร้สัมผัสแล้ว พูดถามได้เลย หรือกดปุ่ม 1 ครั้งอ่านข้อความ 2 ครั้งอธิบายวัตถุ 3 ครั้งนับธนบัตร กดค้างถามตอบค่ะ";
            } else {
                msg += "กดปุ่ม 1 ครั้งอ่านข้อความ 2 ครั้งอธิบายวัตถุ 3 ครั้งนับธนบัตร และกดค้างพร้อมพูดเพื่อถามตอบค่ะ";
            }
            speakThai(msg);
        }
        checkBatteryAndWarn();
        startLiveModeIfConfigured();
    } else {
        startSetupPortal();
    }

    lastActivityTime = millis();
}

void loop() {
    if (inSetupMode) {
        dnsServer.processNextRequest();
        server.handleClient();
        if (readButtonAction() == 1) { vibrate(1, 150, 0); ESP.restart(); }
        if (millis() - lastActivityTime > SETUP_PORTAL_TIMEOUT_MS) enterDeepSleep();
        delay(5);
        return;
    }

    if (audio != nullptr) audio->loop();
    serviceLiveHandsFreeLoop();
    checkBatteryAndWarn();

    int action = readButtonAction();
    if (action == 1) {
        if (liveAiSpeaking) {
            // กด 1 ครั้งขณะ AI กำลังพูดสด -> สั่งหยุดเสียงและล้างคิวทั้งฝั่งแว่นและเซิร์ฟเวอร์ทันที!
            cancelLiveAiSpeech("กดปุ่ม 1 ครั้ง");
        } else if (millis() - lastAiCancelMs < 900) {
            // หน่วงกันเผลอกดซ้ำหลังหยุด AI ไม่ให้หลุดไปสั่งโหมด OCR โดยไม่ตั้งใจ
            Serial.println("[Live 2B] ข้ามการกดซ้ำหลังหยุดเสียง AI");
        } else {
            vibrate(1, 90, 0);
            delay(140);
            triggerLiveButtonMode(1, PROMPT_MODE_1_OCR);
        }
    } else if (action == 2) {
        vibrate(2, 90, 70);
        delay(140);
        triggerLiveButtonMode(2, PROMPT_MODE_2_SCENE);
    } else if (action == 3) {
        vibrate(3, 90, 70);
        delay(140);
        triggerLiveButtonMode(3, PROMPT_MODE_3_BANKNOTE);
    } else if (action == 4) {
        handleVoiceQAMode();
    } else if (action == 5) {
        startSetupPortal();
        return;
    } else if (action == 6) {
        // กด 5 ครั้ง: เปิด/ปิด โหมดคุยสดไร้สัมผัส (Hands-Free) ชั่วคราว (สะดวกมากเวลาเดินเข้าโซนเสียงดังในฮอลล์แข่ง)
        handsFreeEnabled = !handsFreeEnabled;
        if (handsFreeEnabled) {
            speakThai("เปิดโหมดคุยสดไร้สัมผัสแล้วค่ะ");
            startLiveModeIfConfigured();
        } else {
            stopLiveMode();
            speakThai("พักโหมดคุยสดชั่วคราว สามารถใช้งานผ่านปุ่มกดได้ตามปกติค่ะ");
        }
    }

    if (millis() - lastActivityTime > AUTO_SLEEP_TIMEOUT_MS) {
        enterDeepSleep();
    }
    if (liveWsConnected) yield();
    else delay(5);
}
