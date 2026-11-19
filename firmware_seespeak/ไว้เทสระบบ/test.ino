#include <Arduino.h>
#include "esp_camera.h"
#include "ESP32_OV5640_AF.h"
#include <ESP_I2S.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

// ============================================================================
// 1. นิยามขา Pinout สำหรับ Seeed Studio XIAO ESP32S3 Sense
// ============================================================================
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    10
#define CAM_PIN_SIOD    40   // I2C SDA
#define CAM_PIN_SIOC    39   // I2C SCL
#define CAM_PIN_D7      48
#define CAM_PIN_D6      11
#define CAM_PIN_D5      12
#define CAM_PIN_D4      14
#define CAM_PIN_D3      16
#define CAM_PIN_D2      18
#define CAM_PIN_D1      17
#define CAM_PIN_D0      15
#define CAM_PIN_VSYNC   38
#define CAM_PIN_HREF    47
#define CAM_PIN_PCLK    13

// ขาไมโครโฟน PDM บนบอร์ด Sense
#define PDM_CLK_PIN     42
#define PDM_DATA_PIN    41

// ขา MicroSD Card บนบอร์ด Sense
#define SD_CS_PIN       21
#define SD_SCK_PIN      7
#define SD_MISO_PIN     8
#define SD_MOSI_PIN     9

// เรจิสเตอร์ภายในชิป OV5640
#define REG_CHIP_ID_H       0x300A
#define REG_CHIP_ID_L       0x300B
#define REG_SYSTEM_PLL1     0x3035
#define REG_SYSTEM_PLL2     0x3036
#define REG_POLARITY_CTRL   0x4740
#define REG_ISP_CTRL00      0x5000
#define REG_AEC_EXPO_H      0x3500
#define REG_AEC_EXPO_M      0x3501
#define REG_AEC_EXPO_L      0x3502
#define REG_AGC_ADJ_H       0x350A
#define REG_AGC_ADJ_L       0x350B
#define REG_AVG_LUMINANCE   0x56A1
#define REG_AF_CMD_MAIN     0x3022
#define REG_AF_CMD_ACK      0x3023
#define REG_AF_FW_STATUS    0x3029

OV5640 ov5640;

struct DiagSummary {
  bool psramOk      = false;
  bool micOk        = false;
  bool sdCardOk     = false;
  bool i2cBusOk     = false;
  bool cameraInitOk = false;
  bool ov5640IdOk   = false;
  bool afMotorOk    = false;
  bool jpegValidOk  = false;
} diagState;

// ============================================================================
// ฟังก์ชันจัดรูปแบบ Log
// ============================================================================
void logSection(const char* title) {
  Serial.println("\n======================================================================");
  Serial.printf(" >>> %s\n", title);
  Serial.println("======================================================================");
}

void logPass(const char* tag, const char* msg) { Serial.printf("[PASS] [%-12s] %s\n", tag, msg); }
void logInfo(const char* tag, const char* msg) { Serial.printf("[INFO] [%-12s] %s\n", tag, msg); }
void logWarn(const char* tag, const char* msg) { Serial.printf("[WARN] [%-12s] %s\n", tag, msg); }
void logFail(const char* tag, const char* msg) { Serial.printf("[FAIL] [%-12s] %s\n", tag, msg); }

// ============================================================================
// MODULE 1: ตรวจสอบ CPU, อุณหภูมิ และ OPI PSRAM
// ============================================================================
void auditSoCandMemory() {
  logSection("1. ตรวจสอบชิปประมวลผล, อุณหภูมิ และหน่วยความจำ (SoC & Memory Audit)");

  char buf[160];
  snprintf(buf, sizeof(buf), "Chip: %s Rev %d | CPU: %u MHz | Temp: %.1f °C | Free SRAM: %u KB",
           ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz(),
           temperatureRead(), ESP.getFreeHeap() / 1024);
  logInfo("SOC_STAT", buf);

  if (!psramFound()) {
    logFail("PSRAM", "ตรวจไม่พบ PSRAM! กรุณาเปิด Tools -> PSRAM -> OPI PSRAM");
    diagState.psramOk = false;
    return;
  }

  const size_t testBytes = 512 * 1024;
  uint32_t *testBuf = (uint32_t *)ps_malloc(testBytes);
  if (!testBuf) {
    logFail("PSRAM_TEST", "จองพื้นที่ PSRAM ไม่สำเร็จ");
    diagState.psramOk = false;
    return;
  }

  size_t words = testBytes / sizeof(uint32_t);
  uint32_t t0 = micros();
  for (size_t i = 0; i < words; i++) testBuf[i] = (i & 1) ? 0xAA55AA55UL : 0x55AA55AAUL;
  uint32_t wUs = micros() - t0;

  size_t errCnt = 0;
  t0 = micros();
  for (size_t i = 0; i < words; i++) {
    if (testBuf[i] != ((i & 1) ? 0xAA55AA55UL : 0x55AA55AAUL)) errCnt++;
  }
  uint32_t rUs = micros() - t0;
  free(testBuf);

  float wMB = (testBytes / (1024.0f * 1024.0f)) / (wUs / 1000000.0f);
  float rMB = (testBytes / (1024.0f * 1024.0f)) / (rUs / 1000000.0f);

  if (errCnt == 0) {
    snprintf(buf, sizeof(buf), "OPI PSRAM %u KB สมบูรณ์ (Error: 0) | Write: %.1f MB/s | Read: %.1f MB/s",
             ESP.getPsramSize() / 1024, wMB, rMB);
    logPass("PSRAM_R/W", buf);
    diagState.psramOk = true;
  } else {
    logFail("PSRAM_R/W", "พบข้อมูลเพี้ยนใน PSRAM!");
    diagState.psramOk = false;
  }
}

// ============================================================================
// MODULE 2: ตรวจสอบไมโครโฟน PDM บนบอร์ด Sense (รองรับ ESP32 Core 3.x)
// ============================================================================
void auditPDMMicrophone() {
  logSection("2. ตรวจสอบไมโครโฟนดิจิทัล PDM บนบอร์ด Sense (ESP_I2S Core 3.x Audit)");

  I2SClass i2s;
  i2s.setPinsPdmRx(PDM_CLK_PIN, PDM_DATA_PIN);

  if (!i2s.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    logFail("PDM_INIT", "เริ่มต้นบัส I2S PDM ไม่สำเร็จ");
    diagState.micOk = false;
    return;
  }

  delay(100); // รอให้สัญญาณคล็อก PDM นิ่ง
  const int sampleCount = 512;
  int16_t samples[sampleCount];
  size_t bytesRead = i2s.readBytes((char *)samples, sizeof(samples));
  i2s.end();

  char buf[160];
  if (bytesRead > 0) {
    int count = bytesRead / sizeof(int16_t);
    int16_t minVal = 32767, maxVal = -32768;
    double sumSq = 0;

    for (int i = 0; i < count; i++) {
      if (samples[i] < minVal) minVal = samples[i];
      if (samples[i] > maxVal) maxVal = samples[i];
      sumSq += (double)samples[i] * samples[i];
    }
    int32_t p2p = (int32_t)maxVal - (int32_t)minVal;
    double rms = sqrt(sumSq / count);

    snprintf(buf, sizeof(buf), "อ่านเสียงได้ %d จุด | Min: %d | Max: %d | Peak-to-Peak: %d | RMS: %.1f",
             count, minVal, maxVal, p2p, rms);
    logInfo("PDM_SIGNAL", buf);

    if (p2p > 10 && minVal != maxVal) {
      logPass("PDM_MIC", "ไมโครโฟนบนบอร์ดขยาย Sense ทำงานปกติ! (แปลว่าบอร์ดขยาย Sense เสียบแน่นแล้ว)");
      diagState.micOk = true;
    } else {
      logFail("PDM_MIC", "สัญญาณไมค์เป็นเส้นตรง (0 หรือค่าคงที่) -> บอร์ดขยาย Sense ด้านบนยังเสียบไม่แน่น!");
      diagState.micOk = false;
    }
  } else {
    logFail("PDM_MIC", "ไม่มีข้อมูลส่งมาจากไมโครโฟน PDM");
    diagState.micOk = false;
  }
}

// ============================================================================
// MODULE 3: ตรวจสอบสล็อต MicroSD Card บนบอร์ด Sense
// ============================================================================
void auditMicroSD() {
  logSection("3. ตรวจสอบสล็อต MicroSD Card บนบอร์ด Sense (Storage Audit)");

  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN)) {
    logWarn("SD_SLOT", "ไม่ได้ใส่การ์ด MicroSD (หรือการ์ดไม่ได้ฟอร์แมตเป็น FAT32)");
    diagState.sdCardOk = false;
    return;
  }

  uint64_t cardMB = SD.cardSize() / (1024ULL * 1024ULL);
  char buf[128];
  snprintf(buf, sizeof(buf), "พบการ์ด MicroSD ความจุ: %llu MB พร้อมใช้งาน", cardMB);
  logPass("SD_DETECT", buf);
  diagState.sdCardOk = true;
}

// ============================================================================
// MODULE 4: ตรวจสอบสถานะไฟฟ้าขากล้องและบัส I2C (Electrical Pin & Bus Audit)
// ============================================================================
void auditCameraPinsElectrically() {
  logSection("4. ตรวจสอบสถานะไฟฟ้าขากล้องทุกขา (Pin Electrical & Short-Circuit Audit)");

  // 4.1 ตรวจขา I2C SDA (GPIO 40) และ SCL (GPIO 39) ว่ามีตัวต้านทาน Pull-up หรือช็อตลง GND หรือไม่
  pinMode(CAM_PIN_SIOD, INPUT_PULLDOWN);
  pinMode(CAM_PIN_SIOC, INPUT_PULLDOWN);
  delay(5);
  int sdaPulldown = digitalRead(CAM_PIN_SIOD);
  int sclPulldown = digitalRead(CAM_PIN_SIOC);

  pinMode(CAM_PIN_SIOD, INPUT_PULLUP);
  pinMode(CAM_PIN_SIOC, INPUT_PULLUP);
  delay(5);
  int sdaPullup = digitalRead(CAM_PIN_SIOD);
  int sclPullup = digitalRead(CAM_PIN_SIOC);

  char buf[180];
  snprintf(buf, sizeof(buf), "I2C Pin State -> SDA(GPIO40): [PullUp=%d, PullDown=%d] | SCL(GPIO39): [PullUp=%d, PullDown=%d]",
           sdaPullup, sdaPulldown, sclPullup, sclPulldown);
  logInfo("I2C_PINS", buf);

  if (sdaPullup == 0 || sclPullup == 0) {
    logFail("I2C_SHORT", "ขา SDA หรือ SCL ถูกดึงค้างลง GND (0V)! -> ขั้วสายแพรกล้อง OV5640 เสียบเหลื่อมขาหรือช็อต!");
    diagState.i2cBusOk = false;
  } else if (sdaPulldown == 0 || sclPulldown == 0) {
    logWarn("I2C_PULLUP", "ตรวจไม่พบไฟเลี้ยง Pull-up ภายนอกจากบอร์ดขยาย Sense -> เช็คว่ากดบอร์ด Sense และสายแพรกล้องแน่นหรือยัง");
    diagState.i2cBusOk = true; // ยังพอใช้ Internal Pull-up ทดสอบต่อได้
  } else {
    logPass("I2C_ELECTRICAL", "พบไฟเลี้ยง 3.3V Pull-up บนสายสัญญาณ SDA และ SCL สมบูรณ์!");
    diagState.i2cBusOk = true;
  }

  // 4.2 ตรวจขาสัญญาณภาพ DVP ทั้ง 11 ขาว่ามีขาไหนช็อตติด GND หรือไม่
  const int dvpPins[] = { CAM_PIN_D0, CAM_PIN_D1, CAM_PIN_D2, CAM_PIN_D3, CAM_PIN_D4,
                          CAM_PIN_D5, CAM_PIN_D6, CAM_PIN_D7, CAM_PIN_VSYNC, CAM_PIN_HREF, CAM_PIN_PCLK };
  const char* dvpNames[] = { "D0(15)", "D1(17)", "D2(18)", "D3(16)", "D4(14)",
                             "D5(12)", "D6(11)", "D7(48)", "VSYNC(38)", "HREF(47)", "PCLK(13)" };

  int shortedCount = 0;
  for (int i = 0; i < 11; i++) {
    pinMode(dvpPins[i], INPUT_PULLUP);
    delay(2);
    if (digitalRead(dvpPins[i]) == 0) {
      snprintf(buf, sizeof(buf), "ขา %s ถูกดึงลง GND (อาจเกิดจากชิปกล้องยังไม่ตื่น หรือสายแพรเอียง)", dvpNames[i]);
      logWarn("DVP_PIN", buf);
      shortedCount++;
    }
    pinMode(dvpPins[i], INPUT);
  }

  if (shortedCount == 0) {
    logPass("DVP_PINS", "ขาสัญญาณภาพ D0-D7, VSYNC, HREF, PCLK ทั้ง 11 ขา ไม่มีการช็อตลงกราวด์");
  }
}

// ============================================================================
// MODULE 5: เริ่มต้นกล้อง OV5640 และตรวจสอบเรจิสเตอร์ + มอเตอร์ Auto-Focus
// ============================================================================
bool initCameraAndAuditOV5640() {
  logSection("5. เริ่มต้นกล้องและตรวจสอบเรจิสเตอร์ OV5640 + มอเตอร์ AF");

  // เปิด Internal Pull-up ช่วยขา I2C ก่อนเรียก esp_camera_init
  pinMode(CAM_PIN_SIOD, INPUT_PULLUP);
  pinMode(CAM_PIN_SIOC, INPUT_PULLUP);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = CAM_PIN_D0;
  config.pin_d1       = CAM_PIN_D1;
  config.pin_d2       = CAM_PIN_D2;
  config.pin_d3       = CAM_PIN_D3;
  config.pin_d4       = CAM_PIN_D4;
  config.pin_d5       = CAM_PIN_D5;
  config.pin_d6       = CAM_PIN_D6;
  config.pin_d7       = CAM_PIN_D7;
  config.pin_xclk     = CAM_PIN_XCLK;
  config.pin_pclk     = CAM_PIN_PCLK;
  config.pin_vsync    = CAM_PIN_VSYNC;
  config.pin_href     = CAM_PIN_HREF;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_pwdn     = CAM_PIN_PWDN;
  config.pin_reset    = CAM_PIN_RESET;

  config.xclk_freq_hz = 16000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode    = CAMERA_GRAB_LATEST;
  config.frame_size   = psramFound() ? FRAMESIZE_QSXGA : FRAMESIZE_SVGA;
  config.jpeg_quality = 10;
  config.fb_count     = psramFound() ? 2 : 1;
  config.fb_location  = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

  char buf[180];
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    snprintf(buf, sizeof(buf), "esp_camera_init ล้มเหลว (Error: 0x%X) -> กล้องยังไม่ตอบสนองบนบัส I2C!", err);
    logFail("CAM_INIT", buf);
    logInfo("TROUBLESHOOT", "วิธีแก้: 1) ถอดสาย USB 2) กดบอร์ดขยาย Sense ให้แน่น 3) กดขั้วสายแพรกล้อง OV5640 ให้ลงล็อกดังคลิก");
    diagState.cameraInitOk = false;
    return false;
  }

  logPass("CAM_INIT", "เชื่อมต่อบัสกล้อง DVP & SCCB สำเร็จ!");
  diagState.cameraInitOk = true;

  sensor_t *s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_SVGA);
  s->set_vflip(s, 1);

  int idH = s->get_reg(s, REG_CHIP_ID_H, 0xFF);
  int idL = s->get_reg(s, REG_CHIP_ID_L, 0xFF);
  uint16_t pid = ((idH & 0xFF) << 8) | (idL & 0xFF);

  snprintf(buf, sizeof(buf), "อ่านค่า Chip ID จากเรจิสเตอร์ [0x300A:0x300B] = 0x%04X", pid);
  if (pid == 0x5640) {
    logPass("OV5640_ID", buf);
    diagState.ov5640IdOk = true;
  } else {
    logFail("OV5640_ID", buf);
    return false;
  }

  // ทดสอบโหลดเฟิร์มแวร์และขยับมอเตอร์ Auto-Focus
  ov5640.start(s);
  uint32_t t0 = millis();
  if (ov5640.focusInit() == 0) {
    snprintf(buf, sizeof(buf), "อัปโหลดเฟิร์มแวร์ VCM Auto-Focus สำเร็จใน %lu ms", millis() - t0);
    logPass("AF_FIRMWARE", buf);

    s->set_reg(s, REG_AF_CMD_ACK, 0xFF, 0x01);
    s->set_reg(s, REG_AF_CMD_MAIN, 0xFF, 0x03); // Single AF
    uint32_t tAf = millis();
    bool locked = false;
    while (millis() - tAf < 3000) {
      if (s->get_reg(s, REG_AF_FW_STATUS, 0xFF) == 0x10) {
        locked = true;
        break;
      }
      delay(25);
    }
    if (locked) {
      snprintf(buf, sizeof(buf), "มอเตอร์ VCM ขยับเลนส์และล็อกโฟกัสสำเร็จใน %lu ms!", millis() - tAf);
      logPass("AF_MOTOR", buf);
    } else {
      logWarn("AF_MOTOR", "มอเตอร์ทำงานแต่ยังล็อกเป้าไม่ได้ (ลองหันกล้องไปที่วัตถุที่มีแสงสว่าง)");
    }
    ov5640.autoFocusMode();
    diagState.afMotorOk = true;
  } else {
    logFail("AF_FIRMWARE", "อัปโหลดเฟิร์มแวร์ Auto-Focus ไม่สำเร็จ");
  }

  return true;
}

// ============================================================================
// MODULE 6: ทดสอบดึงภาพจริงและตรวจสอบ JPEG Marker
// ============================================================================
void auditImageCapture() {
  logSection("6. ทดสอบดึงภาพจากเซนเซอร์และตรวจความสมบูรณ์ของไฟล์ JPEG");

  sensor_t *s = esp_camera_sensor_get();
  if (!s) return;

  framesize_t sizes[] = { FRAMESIZE_QVGA, FRAMESIZE_SVGA, FRAMESIZE_FHD, FRAMESIZE_QSXGA };
  const char* names[] = { "QVGA (320x240)", "SVGA (800x600)", "FHD (1920x1080)", "5MP QSXGA (2592x1944)" };

  int passCount = 0;
  char buf[160];

  for (int i = 0; i < 4; i++) {
    s->set_framesize(s, sizes[i]);
    delay(150);
    camera_fb_t *flush = esp_camera_fb_get();
    if (flush) esp_camera_fb_return(flush);

    uint32_t t0 = millis();
    camera_fb_t *fb = esp_camera_fb_get();
    uint32_t dt = millis() - t0;

    if (fb && fb->len > 4 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8) {
      snprintf(buf, sizeof(buf), "%-22s -> ขนาด: %6.1f KB | เวลา: %4lu ms (%.1f FPS) | JPEG Valid",
               names[i], fb->len / 1024.0f, dt, 1000.0f / dt);
      logPass("CAPTURE", buf);
      passCount++;
    } else {
      snprintf(buf, sizeof(buf), "%-22s -> ดึงภาพล้มเหลว!", names[i]);
      logFail("CAPTURE", buf);
    }
    if (fb) esp_camera_fb_return(fb);
  }

  s->set_framesize(s, FRAMESIZE_SVGA);
  diagState.jpegValidOk = (passCount == 4);
}

void printSummary() {
  logSection("สรุปผลการตรวจวินิจฉัยฮาร์ดแวร์ (DIAGNOSTIC V2 SUMMARY)");
  Serial.printf("  1. หน่วยความจำ OPI PSRAM (8MB)           : %s\n", diagState.psramOk      ? "[ PASS ]" : "[ FAIL ]");
  Serial.printf("  2. การเชื่อมต่อบอร์ดขยาย Sense (เช็คผ่านไมค์) : %s\n", diagState.micOk        ? "[ PASS - บอร์ด Sense เสียบแน่น ]" : "[ FAIL - บอร์ด Sense หลวม! ]");
  Serial.printf("  3. สัญญาณไฟฟ้าขากล้อง I2C (SDA/SCL)       : %s\n", diagState.i2cBusOk     ? "[ PASS - ไม่ช็อต ]" : "[ FAIL - ขาสายแพรช็อต GND ]");
  Serial.printf("  4. การเชื่อมต่อตัวกล้อง OV5640 (SCCB/DVP)  : %s\n", diagState.cameraInitOk ? "[ PASS - พบกล้อง ]" : "[ FAIL - เช็คขั้วสายแพรกล้อง ]");
  Serial.printf("  5. มอเตอร์เลนส์ Auto-Focus (VCM)          : %s\n", diagState.afMotorOk    ? "[ PASS ]" : "[ FAIL ]");
  Serial.printf("  6. การถ่ายภาพจริงถึงระดับ 5MP (QSXGA)      : %s\n", diagState.jpegValidOk  ? "[ PASS ]" : "[ FAIL ]");
  Serial.println("======================================================================");
  Serial.println(" พิมพ์ [a] แล้วกด Enter เพื่อรันการตรวจสอบซ้ำอีกครั้งหลังขยับสายแพร");
}

void setup() {
  Serial.begin(115200);
  uint32_t tWait = millis();
  while (!Serial && (millis() - tWait < 4000)) delay(50);
  delay(500);

  Serial.println("\n######################################################################");
  Serial.println("#   XIAO ESP32S3 SENSE + OV5640 AF ELECTRICAL & BUS DIAGNOSTIC V2    #");
  Serial.println("######################################################################");

  auditSoCandMemory();
  auditPDMMicrophone();
  auditMicroSD();
  auditCameraPinsElectrically();

  if (initCameraAndAuditOV5640()) {
    auditImageCapture();
  }

  printSummary();
}

void loop() {
  if (Serial.available()) {
    char c = tolower(Serial.read());
    if (c == 'a') {
      auditSoCandMemory();
      auditPDMMicrophone();
      auditMicroSD();
      auditCameraPinsElectrically();
      if (initCameraAndAuditOV5640()) {
        auditImageCapture();
      }
      printSummary();
    }
  }
  delay(20);
}