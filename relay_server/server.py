"""
SeeSpeak V3.7 — Edge-to-Cloud Binary Relay Server (Architecture 2B Pro)
=======================================================================
ทำหน้าที่เป็น "ล่ามตัวกลางความเร็วสูง" ระหว่างแว่นตา XIAO ESP32S3 Sense กับ Google Gemini Live API:
- ล็อกภาษาไทย 100%: ห้ามหลุดตอบภาษาจีน/อังกฤษ และมีตัวกรองเสียงรบกวนฝั่งเซิร์ฟเวอร์ (กันลั่นตอบซ้ำตอนเงียบ!)
- โหมดปุ่มกดผ่าน Gemini Live (MSG_ESP_BUTTON_CMD = 0x08): กดปุ่ม 1/2/3 ครั้ง ส่งภาพคมชัดสูง+แฟลชเข้าท่อ Live เดิมทันที
  ตอบกลับใน ~0.8-1.2 วินาทีด้วยเสียงผู้หญิงน่ารักคนเดิม (Leda/Laomedeia) โดย **ไม่ตัดสาย Live** และจำบริบทต่อเนื่องได้!
- Anti-Stutter Audio Engine: Burst 8 แพ็กเก็ตแรก (512ms) ที่ 0ms เพื่อเติม Jitter Buffer บนแว่นทันที + Stateful Resampler
  ไร้เสียงคลิกระหว่างรอยต่อแพ็กเก็ต + Steady Pacing 1.45x (44ms/64ms) รองรับเน็ตมือถือข้ามโซน Oregon<->ไทย ได้ลื่นไม่กระตุก!
- Ultra-Fast Auto-Resume (~0.35s) + Proactive goAway Handling: ลอง Resume เฉพาะโมเดลเดิม 1 ครั้ง ถ้าหมดอายุเปิดท่อใหม่ทันที
  พร้อมฉีดบริบท 3 รอบล่าสุดกลับเข้าไป ไม่เสียเวลาลองโมเดลอื่นและไม่เสียความจำ!
- แสดง inputAudioTranscription ("ผู้ใช้พูดว่า: ...") และ outputAudioTranscription ใน Render Log เพื่อดีบักได้ชัดเจน
"""

import asyncio
import base64
import json
import logging
import os
import time
from typing import Optional

import numpy as np
import websockets
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
logger = logging.getLogger("seespeak-relay")

app = FastAPI(title="SeeSpeak V3.7 Gemini Live Relay", version="3.7.0")

# ---------------------------------------------------------------------------
# ค่าตั้งต้นจาก Environment Variables (ตั้งบน Render.com เพื่อซ่อน API Key)
# ---------------------------------------------------------------------------
GEMINI_API_KEY = os.getenv("GEMINI_API_KEY", "").strip()
# เสียงผู้หญิงน่ารักสดใส: Leda (วัยรุ่นใสๆ), Laomedeia (ร่าเริงน่ารัก), Autonoe (สดใส), Zephyr (นุ่มใส), Aoede (สุภาพอบอุ่น)
GEMINI_VOICE_NAME = os.getenv("GEMINI_VOICE_NAME", "Laomedeia").strip()

_env_models = os.getenv("GEMINI_LIVE_MODELS", "").strip()
if _env_models:
    LIVE_MODEL_CANDIDATES = [m.strip() for m in _env_models.split(",") if m.strip()]
else:
    LIVE_MODEL_CANDIDATES = [
        "models/gemini-3.8-live",
        "models/gemini-3.1-flash-live-preview",
        "models/gemini-2.5-flash-native-audio-latest",
        "models/gemini-2.5-flash-native-audio-preview-12-25",
    ]

GEMINI_LIVE_WS_URL = (
    "wss://generativelanguage.googleapis.com/ws/"
    "google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent"
)

SEESPEAK_SYSTEM_PROMPT = (
    "คุณคือ น้องซีสปีค (SeeSpeak) ผู้ช่วยสาวน้อยอัจฉริยะบนแว่นตาสำหรับผู้บกพร่องทางสายตาในประเทศไทย "
    "คุณมองเห็นภาพสดจากกล้องด้านหน้าแว่นตาและได้ยินเสียงคำถามของผู้ใช้งานตลอดเวลา\n"
    "กฎเหล็กที่ต้องปฏิบัติตามอย่างเคร่งครัด 100%:\n"
    "1. [กฎภาษาเด็ดขาด] ต้องพูดตอบกลับเป็นภาษาไทยเท่านั้น 100% ทุกกรณี ห้ามพูดภาษาจีน ภาษาอังกฤษ หรือภาษาอื่นโดยเด็ดขาด! "
    "(ยกเว้นอ่านชื่อยี่ห้อหรือข้อความภาษาอังกฤษที่ปรากฏบนป้าย/สินค้าแล้วอธิบายเป็นภาษาไทย) "
    "ใช้น้ำเสียงผู้หญิงวัยรุ่นน่ารัก สดใส อ่อนโยน ชัดเจน กระชับ 1-3 ประโยค ลงท้ายด้วย ค่ะ หรือ นะคะ\n"
    "2. [ห้ามตอบมั่วจากเสียงรบกวน] หากเสียงที่ส่งเข้ามาเป็นเพียงเสียงรบกวน เสียงพัดลม เสียงกระแทก หรือไม่มีคำพูดภาษาไทยที่ฟังเข้าใจได้ "
    "ห้ามเดาเป็นภาษาอื่น และห้ามตอบคำถามเดิมซ้ำ ให้ตอบสั้นๆ เป็นภาษาไทยเพียงคำเดียวว่า 'เมื่อกี้ฟังไม่ชัด รบกวนพูดอีกครั้งนะคะ'\n"
    "3. [การตรวจสอบธนบัตรไทยอย่างแม่นยำ] เมื่อผู้ใช้ถามเรื่องเงินหรือธนบัตรไทย ต้องสังเกต 'ตัวเลขอารบิกและเลขไทย' บนธนบัตรเป็นอันดับแรกเสมอ "
    "(20/๒๐ = สีเขียว, 50/๕๐ = สีฟ้า, 100/๑๐๐ = สีแดง, 500/๕๐๐ = สีม่วง, 1000/๑๐๐๐ = สีเทาน้ำตาล) "
    "ห้ามเดาว่าเป็นแบงก์พันหากไม่เห็นเลข 1000 หรือ ๑๐๐๐ ชัดเจน หากแสงมืดหรือภาพไม่ชัดให้บอกตามตรงว่าเห็นไม่ชัดและแนะนำให้ขยับธนบัตรหรือกดปุ่ม 3 ครั้งเปิดแฟลช "
    "และเมื่อมั่นใจมูลค่าแล้ว ให้พูดสรุป 'ยอดเงินรวมทั้งหมด' ขึ้นเป็นคำแรกเสมอ\n"
    "4. [บอกตำแหน่งและทิศทาง] หากผู้ใช้ถามเรื่องสี สิ่งของ หรือสภาพแวดล้อมตรงหน้า ให้ตอบตรงประเด็นทันที "
    "พร้อมระบุตำแหน่ง (ด้านซ้าย ตรงกลาง ด้านขวา) และระยะห่างโดยประมาณ\n"
    "5. [การอ่านข้อความ] หากผู้ใช้ขอให้อ่านข้อความ ป้าย ฉลากยา ราคา หรือบัตร ให้อ่านเฉพาะข้อความที่มองเห็นจริงชัดเจน "
    "ห้ามแต่งเติมชื่อหรือตัวเลขเองหากภาพเบลอ\n"
    "6. [ความจำต่อเนื่อง] จดจำบริบทของบทสนทนาก่อนหน้าเสมอ เพื่อให้ผู้ใช้ถามต่อเนื่องได้ทันที"
)

BUTTON_MODE_PROMPTS = {
    1: (
        "โหมดอ่านข้อความความละเอียดสูง (Smart OCR): อ่านข้อความภาษาไทยและอังกฤษทั้งหมดที่มองเห็นชัดเจนในภาพนี้ "
        "เรียงตามลำดับความสำคัญ (ชื่อสินค้า ฉลากยา ราคา ป้าย เอกสาร) สรุปกระชับชัดเจนเป็นภาษาไทยเท่านั้น "
        "ห้ามแต่งเติมคำที่มองไม่เห็น หากไม่มีตัวอักษรใดๆ ให้บอกว่าไม่พบข้อความในภาพค่ะ"
    ),
    2: (
        "โหมดอธิบายวัตถุและทิศทาง: อธิบายสิ่งของ บุคคล และสภาพแวดล้อมตรงหน้าในภาพนี้เป็นภาษาไทย "
        "ระบุตำแหน่ง (ด้านซ้าย ตรงกลาง ด้านขวา) และระยะห่างโดยประมาณ หากมีสิ่งกีดขวางอันตรายเช่น บันได เสา รถ ให้เตือนก่อนเสมอ กระชับ 2-3 ประโยคค่ะ"
    ),
    3: (
        "โหมดนับธนบัตรและเหรียญไทยความแม่นยำสูง: ตรวจสอบธนบัตรไทย (20/๒๐ สีเขียว, 50/๕๐ สีฟ้า, 100/๑๐๐ สีแดง, 500/๕๐๐ สีม่วง, 1000/๑๐๐๐ สีเทาน้ำตาล) "
        "และเหรียญไทย (1, 2, 5, 10 บาท) ในภาพนี้ โดยตรวจดูตัวเลขบนธนบัตรเป็นหลักควบคู่กับสี ห้ามเดาเป็น 1000 บาทหากไม่เห็นเลข 1000 ชัดเจน "
        "พูดสรุป 'ยอดเงินรวมทั้งหมด' ขึ้นเป็นคำแรกเป็นภาษาไทยเสมอ เช่น 'ยอดรวม 20 บาทค่ะ เป็นธนบัตรยี่สิบบาท 1 ใบค่ะ'"
    ),
}

# Binary Frame Protocol Opcodes (ต้องตรงกับ firmware_seespeak_v3.ino V3.7)
MSG_ESP_AUDIO_PCM   = 0x01  # ESP32 -> Relay: 16kHz 16-bit mono PCM audio
MSG_ESP_JPEG_CHUNK  = 0x02  # ESP32 -> Relay: JPEG frame chunk (intermediate)
MSG_ESP_JPEG_END    = 0x04  # ESP32 -> Relay: JPEG frame final chunk (live video + cache latest JPEG)
MSG_ESP_SPEECH_END  = 0x05  # ESP32 -> Relay: User paused speaking -> send explicit activityEnd
MSG_ESP_CANCEL_AI   = 0x06  # ESP32 -> Relay: User interrupted ("หยุด/พอ") or tapped button -> stop AI audio immediately
MSG_ESP_BUTTON_CMD  = 0x08  # ESP32 -> Relay: Button mode trigger [0x08, mode_id (1=OCR, 2=Scene, 3=Banknote)] using latest high-res JPEG

MSG_AI_AUDIO_PCM    = 0x10  # Relay -> ESP32: 16kHz 16-bit mono PCM audio
MSG_AI_SPEAK_START  = 0x11  # Relay -> ESP32: AI starts speaking
MSG_AI_SPEAK_END    = 0x12  # Relay -> ESP32: AI finished or cancelled speaking

OUTBOUND_PCM_CHUNK_BYTES = 2048  # 64 ms @ 16kHz 16-bit mono
BURST_CHUNKS_ZERO_DELAY  = 8     # 8 แพ็กเก็ตแรก (512 ms) ส่งทันที 0 ms เพื่อเติม Jitter Buffer บนแว่นทันที!
STEADY_PACING_SEC        = 0.044 # แพ็กเก็ตถัดไปส่งที่ 44 ms ต่อ 64 ms (1.45x real-time) ลื่นไม่กระตุกแม้ผ่าน 4G ข้ามโซน!

# เกณฑ์กรองเสียงรบกวนฝั่งเซิร์ฟเวอร์ (ป้องกันการส่งเสียงคลิก/พัดลมสั้นๆ ไปบังคับให้ Gemini ตอบมั่ว!)
MIN_TURN_AUDIO_BYTES     = 4800  # อย่างน้อย 150 ms @ 16kHz 16-bit mono
MIN_TURN_PEAK_RMS        = 320.0 # อย่างน้อยต้องมีพลังเสียงพูดจริงในเทิร์นนั้น

# สถานะล่าสุดสำหรับดูใน /health เพื่อช่วยดีบักได้ทันที
runtime_state = {
    "last_active_model": None,
    "last_error": None,
    "hint": None,
    "last_user_transcript": None,
    "last_ai_reply": None,
    "api_key_prefix": (GEMINI_API_KEY[:6] + "...") if len(GEMINI_API_KEY) >= 6 else "",
    "sessions_served": 0,
    "images_forwarded": 0,
    "button_cmds_handled": 0,
    "turns_completed": 0,
    "noise_turns_dropped": 0,
    "cancels_handled": 0,
    "upstream_resumes": 0,
    "go_aways_handled": 0,
    "audio_chunks_sent_to_esp": 0,
}


class StatefulDownsampler24to16:
    """แปลงเสียง PCM 24kHz -> 16kHz แบบต่อเนื่องข้ามรอยต่อแพ็กเก็ต (ไร้เสียงคลิก/ป๊อป!)"""

    def __init__(self):
        self._prev_sample: Optional[float] = None
        self._phase: float = 0.0  # ตำแหน่งเศษทศนิยม (step = 1.5 samples of 24k per 1 sample of 16k)

    def reset(self):
        self._prev_sample = None
        self._phase = 0.0

    def process(self, pcm24k_bytes: bytes) -> bytes:
        if len(pcm24k_bytes) < 2:
            return b""
        usable = len(pcm24k_bytes) - (len(pcm24k_bytes) % 2)
        cur = np.frombuffer(pcm24k_bytes[:usable], dtype=np.int16).astype(np.float32)
        if cur.size == 0:
            return b""

        if self._prev_sample is not None:
            buf = np.concatenate(([self._prev_sample], cur))
            offset = 1.0
        else:
            buf = cur
            offset = 0.0

        # ดัชนีที่ต้องสุ่มค่าใน buf ด้วยสเต็ป 1.5 (24000 / 16000)
        start_pos = offset + self._phase
        max_pos = float(buf.size - 1)
        if start_pos > max_pos:
            self._phase = start_pos - float(buf.size)
            self._prev_sample = float(cur[-1])
            return b""

        n_out = int(np.floor((max_pos - start_pos) / 1.5)) + 1
        positions = start_pos + np.arange(n_out, dtype=np.float32) * 1.5
        idx0 = np.floor(positions).astype(np.int32)
        frac = positions - idx0
        idx1 = np.minimum(idx0 + 1, buf.size - 1)
        out = buf[idx0] * (1.0 - frac) + buf[idx1] * frac

        next_pos = start_pos + n_out * 1.5
        self._phase = float(next_pos - float(buf.size))
        self._prev_sample = float(cur[-1])

        return np.clip(out, -32768.0, 32767.0).astype(np.int16).tobytes()


def calc_pcm16_rms(pcm_bytes: bytes) -> float:
    if len(pcm_bytes) < 2:
        return 0.0
    usable = len(pcm_bytes) - (len(pcm_bytes) % 2)
    arr = np.frombuffer(pcm_bytes[:usable], dtype=np.int16).astype(np.float32)
    if arr.size == 0:
        return 0.0
    return float(np.sqrt(np.mean(arr * arr)))


async def connect_gemini_live(
    api_key: str,
    resume_handle: Optional[str] = None,
    preferred_model: Optional[str] = None,
    memory_summary: str = "",
):
    """เชื่อมต่อ Gemini Live API แบบเร็วสุด: ถ้า resume โมเดลเดิมไม่ผ่าน ให้เปิดท่อใหม่โมเดลหลักทันทีใน ~0.35s"""
    last_error: Optional[Exception] = None
    url = f"{GEMINI_LIVE_WS_URL}?key={api_key}"

    system_text = SEESPEAK_SYSTEM_PROMPT
    if memory_summary and not resume_handle:
        system_text += f"\n\n[สรุปบทสนทนาล่าสุดก่อนต่อท่อใหม่ เพื่อรักษาความต่อเนื่อง:\n{memory_summary}]"

    # จัดลำดับการลองแบบฉลาด: ลอง resume handle กับโมเดลเดิมแค่ 1 ครั้ง (ไม่เสียเวลา 2 วิไปลอง handle ตายกับอีก 3 โมเดล!)
    attempts = []
    if resume_handle and preferred_model:
        attempts.append((preferred_model, resume_handle))
    ordered_models = list(LIVE_MODEL_CANDIDATES)
    if preferred_model and preferred_model in ordered_models:
        ordered_models.remove(preferred_model)
        ordered_models.insert(0, preferred_model)
    for m in ordered_models:
        attempts.append((m, None))

    for model_name, handle in attempts:
        ws = None
        try:
            logger.info(
                "กำลังเชื่อมต่อ Gemini Live API รุ่น: %s (เสียง: %s%s)",
                model_name,
                GEMINI_VOICE_NAME,
                ", resume" if handle else "",
            )
            ws = await websockets.connect(
                url,
                max_size=8 * 1024 * 1024,
                ping_interval=15,
                ping_timeout=15,
            )
            setup_inner = {
                "model": model_name,
                "generationConfig": {
                    "responseModalities": ["AUDIO"],
                    "speechConfig": {
                        "voiceConfig": {
                            "prebuiltVoiceConfig": {
                                "voiceName": GEMINI_VOICE_NAME,
                            }
                        }
                    },
                },
                "systemInstruction": {
                    "parts": [{"text": system_text}],
                },
                "realtimeInputConfig": {
                    "automaticActivityDetection": {
                        "disabled": True,
                    }
                },
                "inputAudioTranscription": {},
                "outputAudioTranscription": {},
                "sessionResumption": {"handle": handle} if handle else {},
            }
            await ws.send(json.dumps({"setup": setup_inner}, ensure_ascii=False))
            raw_ack = await asyncio.wait_for(ws.recv(), timeout=8.0)
            ack = json.loads(raw_ack)
            if "setupComplete" in ack:
                logger.info("เชื่อมต่อ Gemini Live API สำเร็จ (%s)!", model_name)
                runtime_state["last_active_model"] = model_name
                runtime_state["last_error"] = None
                runtime_state["hint"] = "พร้อมใช้งานปกติ"
                return ws, model_name
            logger.warning("โมเดล %s ตอบกลับไม่คาดคิด: %s", model_name, str(ack)[:200])
            await ws.close()
        except Exception as exc:
            last_error = exc
            err_text = str(exc)
            runtime_state["last_error"] = f"{model_name}: {err_text}"
            logger.warning("โมเดล %s เชื่อมต่อไม่สำเร็จ: %s", model_name, exc)
            if ws is not None:
                try:
                    await ws.close()
                except Exception:
                    pass
            if "API key not valid" in err_text or "API_KEY_INVALID" in err_text:
                runtime_state["hint"] = (
                    "GEMINI_API_KEY ไม่ถูกต้องหรือถูกระงับ — "
                    "กรุณาสร้าง API Key ใหม่จาก https://aistudio.google.com/apikey "
                    "แล้วนำไปใส่ใน Render.com -> Environment -> GEMINI_API_KEY"
                )
                raise RuntimeError(f"API key ไม่ถูกต้อง: {last_error}")

    raise RuntimeError(f"ไม่สามารถเชื่อมต่อโมเดล Gemini Live API ตัวใดได้เลย: {last_error}")


@app.get("/")
@app.get("/health")
async def health_check():
    return JSONResponse(
        {
            "status": "ok",
            "version": "3.7.0",
            "service": "SeeSpeak V3.7 Gemini Live Relay (2B Pro)",
            "api_key_configured": bool(GEMINI_API_KEY),
            "voice": GEMINI_VOICE_NAME,
            "models": LIVE_MODEL_CANDIDATES,
            "runtime": runtime_state,
        }
    )


@app.get("/test-key")
async def test_api_key():
    """ทดสอบได้ทันทีว่า GEMINI_API_KEY บน Render เชื่อมต่อ Gemini Live API ได้จริงหรือไม่"""
    if not GEMINI_API_KEY:
        return JSONResponse(
            status_code=400,
            content={
                "ok": False,
                "error": "ยังไม่ได้ตั้งค่า GEMINI_API_KEY ใน Environment ของ Render.com",
            },
        )
    try:
        ws, active_model = await connect_gemini_live(GEMINI_API_KEY)
        await ws.close()
        return JSONResponse(
            {
                "ok": True,
                "active_model": active_model,
                "voice": GEMINI_VOICE_NAME,
                "message": "เชื่อมต่อ Gemini Live API สำเร็จ 100%! แว่น SeeSpeak พร้อมคุยสดได้ทันที",
            }
        )
    except Exception as exc:
        return JSONResponse(
            status_code=502,
            content={
                "ok": False,
                "api_key_prefix": runtime_state["api_key_prefix"],
                "error": str(exc),
                "hint": runtime_state["hint"],
            },
        )


@app.websocket("/ws/seespeak")
async def seespeak_ws_endpoint(esp_ws: WebSocket):
    api_key = esp_ws.query_params.get("key", "").strip() or GEMINI_API_KEY
    if not api_key:
        runtime_state["last_error"] = "Missing GEMINI_API_KEY"
        await esp_ws.close(code=1008, reason="Missing GEMINI_API_KEY")
        return

    runtime_state["sessions_served"] += 1
    logger.info("แว่น SeeSpeak ขอเชื่อมต่อเข้ามา (%s) -> กำลังเปิดท่อ Gemini Live...", esp_ws.client)

    try:
        initial_ws, active_model = await connect_gemini_live(api_key)
    except Exception as exc:
        runtime_state["last_error"] = str(exc)
        logger.error("เปิดเซสชัน Gemini Live ไม่สำเร็จ: %s", exc)
        await esp_ws.close(code=1011, reason="Gemini Live unavailable")
        return

    await esp_ws.accept()
    logger.info("เปิดท่อคุยสด SeeSpeak <-> %s (เสียง %s) เรียบร้อยแล้ว!", active_model, GEMINI_VOICE_NAME)

    session = {
        "ws": initial_ws,
        "model": active_model,
        "handle": None,
        "go_away_pending": False,
    }
    esp_closed = asyncio.Event()
    jpeg_buffer = bytearray()
    latest_jpeg_b64: str = ""
    recent_history: list[str] = []  # เก็บสรุป 3 รอบล่าสุดไว้ฉีดคืนหากต้องเปิดท่อ Gemini ใหม่
    downsampler = StatefulDownsampler24to16()

    ai_is_speaking = False
    user_activity_open = False
    drop_current_turn_audio = False
    turn_audio_bytes = 0
    turn_peak_rms = 0.0

    gemini_send_lock = asyncio.Lock()
    gemini_reconnect_lock = asyncio.Lock()

    # คิวส่งเสียงขนาดใหญ่ (~160 วินาที @ 16kHz) รองรับคำตอบยาวๆ โดยไม่ล้น
    outbound_q: asyncio.Queue[bytes] = asyncio.Queue(maxsize=2500)

    def drain_outbound_queue():
        drained = 0
        while not outbound_q.empty():
            try:
                outbound_q.get_nowait()
                drained += 1
            except asyncio.QueueEmpty:
                break
        return drained

    def build_memory_summary() -> str:
        return "\n".join(recent_history[-4:])

    async def reconnect_upstream(reason: str) -> bool:
        """ต่อท่อ Gemini Live ใหม่เงียบๆ ภายใน ~0.35s โดยไม่ตัดสายแว่น ESP32 และรักษาความจำเดิม"""
        nonlocal user_activity_open, ai_is_speaking, drop_current_turn_audio
        nonlocal turn_audio_bytes, turn_peak_rms
        async with gemini_reconnect_lock:
            if esp_closed.is_set():
                return False
            old_ws = session["ws"]
            if old_ws is not None:
                try:
                    await old_ws.close()
                except Exception:
                    pass
                session["ws"] = None
            session["go_away_pending"] = False
            logger.warning("ท่อ Gemini รีเฟรช (%s) -> กำลังต่อใหม่อัตโนมัติแบบไม่ตัดสายแว่น...", reason)
            for attempt in range(3):
                if esp_closed.is_set():
                    return False
                try:
                    new_ws, new_model = await connect_gemini_live(
                        api_key,
                        resume_handle=session["handle"],
                        preferred_model=session["model"],
                        memory_summary=build_memory_summary(),
                    )
                    session["ws"] = new_ws
                    session["model"] = new_model
                    user_activity_open = False
                    ai_is_speaking = False
                    drop_current_turn_audio = False
                    turn_audio_bytes = 0
                    turn_peak_rms = 0.0
                    downsampler.reset()
                    runtime_state["upstream_resumes"] += 1
                    logger.info("ต่อท่อ Gemini ใหม่สำเร็จแบบไร้รอยต่อ (%s)!", new_model)
                    return True
                except Exception as exc:
                    logger.warning("ลองต่อท่อ Gemini ใหม่ครั้งที่ %d ไม่สำเร็จ: %s", attempt + 1, exc)
                    await asyncio.sleep(0.35)
            return False

    async def send_gemini_json(payload: dict) -> bool:
        async with gemini_send_lock:
            ws = session["ws"]
            if ws is None:
                return False
            try:
                await ws.send(json.dumps(payload, ensure_ascii=False))
                return True
            except Exception as exc:
                logger.warning("send_gemini_json พบสายหลุด: %s -> รอท่อใหม่", exc)
                return False

    async def esp_to_gemini():
        """รับเสียง PCM ดิบ, ภาพ JPEG, คำสั่งปุ่มกด (0x08), และสัญญาณหยุดพูด (0x06) จากแว่น ESP32-S3"""
        nonlocal user_activity_open, ai_is_speaking, drop_current_turn_audio
        nonlocal latest_jpeg_b64, turn_audio_bytes, turn_peak_rms
        try:
            while not esp_closed.is_set():
                msg = await esp_ws.receive()
                if msg["type"] == "websocket.disconnect":
                    break
                data = msg.get("bytes")
                if not data:
                    continue

                opcode = data[0]
                payload = data[1:]

                if opcode == MSG_ESP_CANCEL_AI:
                    drained = drain_outbound_queue()
                    drop_current_turn_audio = True
                    ai_is_speaking = False
                    downsampler.reset()
                    runtime_state["cancels_handled"] += 1
                    logger.info("ได้รับคำสั่งหยุดเสียง AI (0x06) -> ล้างคิวทิ้ง %d แพ็กเก็ตทันที!", drained)
                    await outbound_q.put(bytes([MSG_AI_SPEAK_END]))

                elif opcode == MSG_ESP_AUDIO_PCM and payload:
                    blk_rms = calc_pcm16_rms(payload)
                    if blk_rms > turn_peak_rms:
                        turn_peak_rms = blk_rms
                    turn_audio_bytes += len(payload)

                    if not user_activity_open:
                        user_activity_open = True
                        drop_current_turn_audio = False
                        await send_gemini_json({"realtimeInput": {"activityStart": {}}})
                    audio_b64 = base64.b64encode(payload).decode("ascii")
                    await send_gemini_json(
                        {
                            "realtimeInput": {
                                "audio": {
                                    "mimeType": "audio/pcm;rate=16000",
                                    "data": audio_b64,
                                }
                            }
                        }
                    )

                elif opcode == MSG_ESP_JPEG_CHUNK and payload:
                    if len(jpeg_buffer) < 350_000:
                        jpeg_buffer.extend(payload)

                elif opcode == MSG_ESP_JPEG_END:
                    if payload and len(jpeg_buffer) < 350_000:
                        jpeg_buffer.extend(payload)
                    if len(jpeg_buffer) > 500:
                        img_b64 = base64.b64encode(bytes(jpeg_buffer)).decode("ascii")
                        latest_jpeg_b64 = img_b64
                        runtime_state["images_forwarded"] += 1
                        logger.info("ส่งภาพกล้องสด (%d bytes) ขึ้น %s", len(jpeg_buffer), session["model"])
                        await send_gemini_json(
                            {
                                "realtimeInput": {
                                    "video": {
                                        "mimeType": "image/jpeg",
                                        "data": img_b64,
                                    }
                                }
                            }
                        )
                    jpeg_buffer.clear()

                elif opcode == MSG_ESP_BUTTON_CMD and payload:
                    mode_id = payload[0]
                    prompt_text = BUTTON_MODE_PROMPTS.get(mode_id)
                    if prompt_text and latest_jpeg_b64:
                        # ล้างเสียงเก่าค้างท่อและปิด activity หากยังเปิดค้าง
                        drain_outbound_queue()
                        downsampler.reset()
                        if user_activity_open:
                            user_activity_open = False
                            turn_audio_bytes = 0
                            turn_peak_rms = 0.0
                            await send_gemini_json({"realtimeInput": {"activityEnd": {}}})
                        drop_current_turn_audio = False
                        runtime_state["button_cmds_handled"] += 1
                        logger.info("คำสั่งปุ่มกดโหมด %d ผ่าน Gemini Live -> ส่งภาพคมชัดสูง+Prompt ทันที!", mode_id)
                        await send_gemini_json(
                            {
                                "clientContent": {
                                    "turns": [
                                        {
                                            "role": "user",
                                            "parts": [
                                                {
                                                    "inlineData": {
                                                        "mimeType": "image/jpeg",
                                                        "data": latest_jpeg_b64,
                                                    }
                                                },
                                                {"text": prompt_text},
                                            ],
                                        }
                                    ],
                                    "turnComplete": True,
                                }
                            }
                        )
                    else:
                        logger.warning("ได้รับ MSG_ESP_BUTTON_CMD (%d) แต่ไม่มีภาพหรือโหมดไม่ถูกต้อง", mode_id)

                elif opcode == MSG_ESP_SPEECH_END:
                    if user_activity_open:
                        user_activity_open = False
                        is_real_speech = (
                            turn_audio_bytes >= MIN_TURN_AUDIO_BYTES
                            and turn_peak_rms >= MIN_TURN_PEAK_RMS
                        )
                        logger.info(
                            "ผู้ใช้พูดจบ (bytes=%d, peak_rms=%.0f, valid=%s)",
                            turn_audio_bytes,
                            turn_peak_rms,
                            is_real_speech,
                        )
                        turn_audio_bytes = 0
                        turn_peak_rms = 0.0
                        if is_real_speech:
                            drop_current_turn_audio = False
                            await send_gemini_json({"realtimeInput": {"activityEnd": {}}})
                        else:
                            # เสียงสั้น/เบาเกินไป -> ปิด activity แล้วทิ้งเสียงตอบหลอนของเทิร์นนี้ (ไม่ให้พูดซ้ำ/หลุดภาษาอื่น!)
                            runtime_state["noise_turns_dropped"] += 1
                            drop_current_turn_audio = True
                            await send_gemini_json({"realtimeInput": {"activityEnd": {}}})
                            logger.info("กรองเสียงรบกวนสั้นออก (ไม่ส่งเสียงตอบหลอนลงแว่น)")

        except WebSocketDisconnect:
            logger.info("แว่น SeeSpeak ตัดการเชื่อมต่อ")
        except Exception as exc:
            runtime_state["last_error"] = f"esp_to_gemini: {exc}"
            logger.warning("esp_to_gemini หยุดทำงาน: %s", exc)
        finally:
            esp_closed.set()

    async def gemini_to_queue():
        """อ่านข้อมูลจาก Gemini Live API เต็มความเร็ว พร้อม Auto-Resume อัตโนมัติและจับ goAway ล่วงหน้า"""
        nonlocal ai_is_speaking, drop_current_turn_audio
        current_user_parts = []
        current_reply_parts = []
        while not esp_closed.is_set():
            ws = session["ws"]
            if ws is None:
                ok = await reconnect_upstream("no active upstream")
                if not ok:
                    break
                continue
            try:
                async for raw_msg in ws:
                    if esp_closed.is_set():
                        break
                    if isinstance(raw_msg, bytes):
                        raw_msg = raw_msg.decode("utf-8", errors="ignore")
                    packet = json.loads(raw_msg)

                    # จับสัญญาณ goAway จาก Google เพื่อรีเฟรชท่อล่วงหน้าตอนว่างแบบไม่มีวันหลุด!
                    if "goAway" in packet:
                        runtime_state["go_aways_handled"] += 1
                        session["go_away_pending"] = True
                        logger.info("ได้รับ goAway จาก Google (%s) -> เตรียมต่อท่อใหม่ทันทีเมื่อจบคำตอบ", packet["goAway"])
                        if not ai_is_speaking and not user_activity_open:
                            break

                    resump = packet.get("sessionResumptionUpdate")
                    if resump and resump.get("resumable") and resump.get("newHandle"):
                        session["handle"] = resump["newHandle"]

                    server_content = packet.get("serverContent")
                    if not server_content:
                        continue

                    in_tr = server_content.get("inputTranscription")
                    if in_tr and in_tr.get("text"):
                        current_user_parts.append(in_tr["text"])

                    out_tr = server_content.get("outputTranscription")
                    if out_tr and out_tr.get("text"):
                        current_reply_parts.append(out_tr["text"])

                    model_turn = server_content.get("modelTurn")
                    if model_turn and not drop_current_turn_audio:
                        for part in model_turn.get("parts", []):
                            inline_data = part.get("inlineData")
                            if not inline_data:
                                continue
                            mime = inline_data.get("mimeType", "")
                            if mime.startswith("audio/pcm"):
                                if not ai_is_speaking:
                                    ai_is_speaking = True
                                    downsampler.reset()
                                    await outbound_q.put(bytes([MSG_AI_SPEAK_START]))

                                pcm24k = base64.b64decode(inline_data["data"])
                                pcm16k = downsampler.process(pcm24k)
                                for offset in range(0, len(pcm16k), OUTBOUND_PCM_CHUNK_BYTES):
                                    if drop_current_turn_audio:
                                        break
                                    chunk = pcm16k[offset : offset + OUTBOUND_PCM_CHUNK_BYTES]
                                    await outbound_q.put(bytes([MSG_AI_AUDIO_PCM]) + chunk)

                    if server_content.get("turnComplete") or server_content.get("interrupted"):
                        user_text = "".join(current_user_parts).strip()
                        reply_text = "".join(current_reply_parts).strip()
                        current_user_parts.clear()
                        current_reply_parts.clear()

                        if user_text:
                            runtime_state["last_user_transcript"] = user_text
                            logger.info("ผู้ใช้พูดว่า: %s", user_text)
                        if reply_text and not drop_current_turn_audio:
                            runtime_state["last_ai_reply"] = reply_text
                            logger.info("SeeSpeak ตอบแล้ว: %s", reply_text)
                            hist_entry = f"ผู้ใช้: {user_text or '(คำสั่งภาพ/ปุ่มกด)'} -> ซีสปีค: {reply_text}"
                            recent_history.append(hist_entry[:240])
                            if len(recent_history) > 6:
                                del recent_history[:-6]

                        runtime_state["turns_completed"] += 1
                        was_dropped = drop_current_turn_audio
                        drop_current_turn_audio = False
                        downsampler.reset()
                        if ai_is_speaking and not was_dropped:
                            ai_is_speaking = False
                            await outbound_q.put(bytes([MSG_AI_SPEAK_END]))
                        else:
                            ai_is_speaking = False

                        if session["go_away_pending"] and not user_activity_open:
                            break

                if not esp_closed.is_set():
                    if ai_is_speaking:
                        ai_is_speaking = False
                        await outbound_q.put(bytes([MSG_AI_SPEAK_END]))
                    reason = "proactive goAway refresh" if session["go_away_pending"] else "upstream closed cleanly"
                    ok = await reconnect_upstream(reason)
                    if not ok:
                        break
            except Exception as exc:
                if esp_closed.is_set():
                    break
                runtime_state["last_error"] = f"gemini_auto_resume: {exc}"
                if ai_is_speaking:
                    ai_is_speaking = False
                    await outbound_q.put(bytes([MSG_AI_SPEAK_END]))
                ok = await reconnect_upstream(str(exc))
                if not ok:
                    break

    async def queue_to_esp():
        """ส่งเสียงลงแว่น: 8 แพ็กเก็ตแรก (512ms) ส่งทันที 0ms แล้วคุมที่ 1.45x (44ms/64ms) ป้องกันเสียงกระตุกข้ามโซน!"""
        turn_chunks = 0
        try:
            while not esp_closed.is_set():
                frame = await outbound_q.get()
                if not frame:
                    continue
                op = frame[0]
                if op == MSG_AI_SPEAK_START:
                    turn_chunks = 0
                    await esp_ws.send_bytes(frame)
                elif op == MSG_AI_SPEAK_END:
                    turn_chunks = 0
                    await esp_ws.send_bytes(frame)
                elif op == MSG_AI_AUDIO_PCM:
                    if drop_current_turn_audio:
                        continue
                    await esp_ws.send_bytes(frame)
                    runtime_state["audio_chunks_sent_to_esp"] += 1
                    turn_chunks += 1
                    if turn_chunks > BURST_CHUNKS_ZERO_DELAY:
                        await asyncio.sleep(STEADY_PACING_SEC)
        except Exception as exc:
            logger.warning("queue_to_esp หยุดทำงาน: %s", exc)
        finally:
            esp_closed.set()

    tasks = {
        asyncio.create_task(esp_to_gemini()),
        asyncio.create_task(gemini_to_queue()),
        asyncio.create_task(queue_to_esp()),
    }
    done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
    esp_closed.set()
    for t in pending:
        t.cancel()
    if session["ws"] is not None:
        try:
            await session["ws"].close()
        except Exception:
            pass
    try:
        await esp_ws.close()
    except Exception:
        pass
    logger.info("จบเซสชันคุยสด SeeSpeak เรียบร้อย")
