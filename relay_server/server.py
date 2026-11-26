"""
SeeSpeak V3 — Edge-to-Cloud Binary Relay Server (Architecture 2B)
=================================================================
ทำหน้าที่เป็น "ล่ามตัวกลางความเร็วสูง" ระหว่างแว่นตา XIAO ESP32S3 Sense กับ Google Gemini Live API:
- เสียงผู้หญิงวัยรุ่นน่ารัก สดใส เป็นกันเอง (ค่าเริ่มต้น: Laomedeia — ปรับผ่าน GEMINI_VOICE_NAME ได้)
- Seamless Upstream Auto-Resume (v3.6.0): หากฝั่ง Google ปิดท่อชั่วคราว (เช่น ครบ 10 นาที หรือสะดุด)
  เซิร์ฟเวอร์จะต่อท่อ Gemini ใหม่พร้อม Session Resumption ภายใน ~0.3 วิ โดย **ไม่ตัดการเชื่อมต่อแว่น ESP32** และยังจำบทสนทนาเดิมได้!
- ตอบเร็วสุด: 6 แพ็กเก็ตแรก (384 ms) ส่งทันที 0 ms ไม่หน่วง แล้วคุมจังหวะส่งที่ 1.14x (56 ms/64 ms)
  เพื่อรองรับการพูดประโยคยาวมาก (เช่น ท่อง ก-ฮ หรืออ่านฉลากยา) โดยไม่ล้นบัฟเฟอร์และไม่เร่งสปีด!
- รองรับคำสั่งหยุดพูดกลางคัน (MSG_ESP_CANCEL_AI = 0x06) ทั้งจากการพูดแทรก ("หยุดๆ/พอๆ") และการกดปุ่ม 1 ครั้ง
- มีระบบ Keepalive ส่งเสียงเงียบ 100ms ทุก 18 วินาที กัน Gemini ตัดสายเมื่อเงียบเกิน 1 นาที (1008 inactivity)
- มีหน้า /test-key สำหรับตรวจสอบว่า GEMINI_API_KEY บน Render.com เชื่อมต่อ Gemini Live API ได้จริงทันที
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

app = FastAPI(title="SeeSpeak V3 Gemini Live Relay", version="3.6.0")

# ---------------------------------------------------------------------------
# ค่าตั้งต้นจาก Environment Variables (ตั้งบน Render / Cloud Run เพื่อซ่อน API Key อย่างปลอดภัย)
# ---------------------------------------------------------------------------
GEMINI_API_KEY = os.getenv("GEMINI_API_KEY", "").strip()
# เสียงผู้หญิงสดใสน่ารัก: Laomedeia (ร่าเริงน่ารัก), Leda (วัยรุ่นใสๆ), Autonoe (สดใส), Zephyr (นุ่มใส), Aoede (สุภาพอบอุ่น)
GEMINI_VOICE_NAME = os.getenv("GEMINI_VOICE_NAME", "Laomedeia").strip()

_env_models = os.getenv("GEMINI_LIVE_MODELS", "").strip()
if _env_models:
    LIVE_MODEL_CANDIDATES = [m.strip() for m in _env_models.split(",") if m.strip()]
else:
    LIVE_MODEL_CANDIDATES = [
        "models/gemini-3.8-live",
        "models/gemini-3.1-flash-live-preview",
        "models/gemini-2.5-flash-native-audio-latest",
        "models/gemini-2.5-flash-native-audio-preview-12-2025",
    ]

GEMINI_LIVE_WS_URL = (
    "wss://generativelanguage.googleapis.com/ws/"
    "google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent"
)

SEESPEAK_SYSTEM_PROMPT = (
    "คุณคือ น้องซีสปีค (SeeSpeak) ผู้ช่วยสาวน้อยอัจฉริยะบนแว่นตาสำหรับผู้บกพร่องทางสายตาในประเทศไทย "
    "คุณมองเห็นภาพสดจากกล้องด้านหน้าแว่นตาและได้ยินเสียงคำถามของผู้ใช้งานตลอดเวลา "
    "หลักการตอบที่สำคัญมาก:\n"
    "1. ตอบเป็นภาษาไทยด้วยน้ำเสียงผู้หญิงวัยรุ่นที่น่ารัก สดใส อ่อนโยน ร่าเริง เป็นกันเอง ชัดเจน และกระชับทันทีภายใน 1-3 ประโยค (ลงท้ายด้วยคำว่า ค่ะ หรือ นะคะ อย่างเป็นธรรมชาติ)\n"
    "2. หากผู้ใช้ถามเรื่องสี สิ่งของ หรือสภาพแวดล้อมตรงหน้า ให้บอกคำตอบตรงประเด็นทันที พร้อมระบุตำแหน่งทิศทาง (ด้านซ้าย ตรงกลาง ด้านขวา) และระยะห่างโดยประมาณ\n"
    "3. หากผู้ใช้ขอให้อ่านข้อความ ป้าย ฉลากยา ราคา หรือเอกสาร ให้อ่านข้อความที่มองเห็นทั้งหมดอย่างชัดเจนทันทีแม้ภาพจะเอียงหรือเบลอเล็กน้อย\n"
    "4. หากผู้ใช้ถามเรื่องเงินหรือธนบัตรไทย (20 สีเขียว, 50 สีฟ้า, 100 สีแดง, 500 สีม่วง, 1000 สีเทาน้ำตาล) และเหรียญ ให้พูดสรุป 'ยอดเงินรวมทั้งหมด' ขึ้นเป็นคำแรกเสมอ\n"
    "5. จดจำบริบทของบทสนทนาก่อนหน้าเสมอ เพื่อให้ผู้ใช้ถามต่อเนื่องได้เหมือนคุยกับผู้ช่วยส่วนตัว\n"
    "6. หากได้ยินคำพูดของผู้ใช้แม้จะสั้นหรือเบา ให้พยายามตอบอย่างสุภาพน่ารักเสมอ จะเงียบไว้เฉพาะกรณีที่เป็นเสียงรบกวนที่ไม่มีคำพูดเลยเท่านั้น"
)

# Binary Frame Protocol Opcodes (ต้องตรงกับ firmware_seespeak_v3.ino)
MSG_ESP_AUDIO_PCM   = 0x01  # ESP32 -> Relay: 16kHz 16-bit mono PCM audio
MSG_ESP_JPEG_CHUNK  = 0x02  # ESP32 -> Relay: JPEG frame chunk (intermediate)
MSG_ESP_JPEG_END    = 0x04  # ESP32 -> Relay: JPEG frame final chunk
MSG_ESP_SPEECH_END  = 0x05  # ESP32 -> Relay: User paused speaking -> send explicit activityEnd
MSG_ESP_CANCEL_AI   = 0x06  # ESP32 -> Relay: User interrupted ("หยุด/พอ") or tapped button -> stop AI audio immediately

MSG_AI_AUDIO_PCM    = 0x10  # Relay -> ESP32: 16kHz 16-bit mono PCM audio
MSG_AI_SPEAK_START  = 0x11  # Relay -> ESP32: AI starts speaking
MSG_AI_SPEAK_END    = 0x12  # Relay -> ESP32: AI finished or cancelled speaking

OUTBOUND_PCM_CHUNK_BYTES = 2048  # 64 ms @ 16kHz 16-bit mono
BURST_CHUNKS_ZERO_DELAY  = 6     # 6 แพ็กเก็ตแรก (384 ms) ส่งทันที 0 ms เพื่อเริ่มพูดไวสุด!
STEADY_PACING_SEC        = 0.056 # แพ็กเก็ตถัดไปส่งที่ 56 ms ต่อ 64 ms (1.14x real-time) ไม่มีวันล้นบัฟเฟอร์/เร่งสปีด!
KEEPALIVE_INTERVAL_SEC   = 18.0  # ส่งเสียงเงียบ 100ms ทุก 18 วิ กัน Gemini ปิดเซสชัน (1008 inactivity)
SILENCE_KEEPALIVE_B64    = base64.b64encode(b"\x00\x00" * 1600).decode("ascii")  # 100 ms silence

# สถานะล่าสุดสำหรับดูใน /health เพื่อช่วยดีบักได้ทันที
runtime_state = {
    "last_active_model": None,
    "last_error": None,
    "hint": None,
    "last_ai_reply": None,
    "api_key_prefix": (GEMINI_API_KEY[:6] + "...") if len(GEMINI_API_KEY) >= 6 else "",
    "sessions_served": 0,
    "images_forwarded": 0,
    "turns_completed": 0,
    "cancels_handled": 0,
    "upstream_resumes": 0,
    "keepalives_sent": 0,
    "audio_chunks_sent_to_esp": 0,
}


def downsample_24k_to_16k(pcm24k_bytes: bytes) -> bytes:
    """แปลงเสียง PCM 24kHz 16-bit mono จาก Gemini Live API เป็น 16kHz 16-bit mono สำหรับ ESP32 MAX98357A"""
    if len(pcm24k_bytes) < 4:
        return b""
    usable = len(pcm24k_bytes) - (len(pcm24k_bytes) % 2)
    samples_24k = np.frombuffer(pcm24k_bytes[:usable], dtype=np.int16).astype(np.float32)
    n_in = samples_24k.shape[0]
    n_out = max(1, int(round(n_in * 16000.0 / 24000.0)))
    x_old = np.linspace(0.0, 1.0, n_in, endpoint=False)
    x_new = np.linspace(0.0, 1.0, n_out, endpoint=False)
    samples_16k = np.interp(x_new, x_old, samples_24k)
    return np.clip(samples_16k, -32768, 32767).astype(np.int16).tobytes()


async def connect_gemini_live(api_key: str, resume_handle: Optional[str] = None):
    """เชื่อมต่อ Gemini Live API (รองรับ Session Resumption เพื่อต่อท่อเดิมโดยไม่เสียความจำ)"""
    last_error: Optional[Exception] = None
    url = f"{GEMINI_LIVE_WS_URL}?key={api_key}"

    # หากมี resume_handle ให้ลองต่อเซสชันเดิมก่อน ถ้าไม่สำเร็จค่อยเปิดเซสชันใหม่สะอาด
    handles_to_try = [resume_handle, None] if resume_handle else [None]

    for handle in handles_to_try:
        for model_name in LIVE_MODEL_CANDIDATES:
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
                    ping_interval=20,
                    ping_timeout=20,
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
                        "parts": [{"text": SEESPEAK_SYSTEM_PROMPT}],
                    },
                    "realtimeInputConfig": {
                        "automaticActivityDetection": {
                            "disabled": True,
                        }
                    },
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
            "version": "3.6.0",
            "service": "SeeSpeak V3 Gemini Live Relay (2B)",
            "api_key_configured": bool(GEMINI_API_KEY),
            "voice": GEMINI_VOICE_NAME,
            "models": LIVE_MODEL_CANDIDATES,
            "runtime": runtime_state,
        }
    )


@app.get("/test-key")
async def test_api_key():
    """กดเปิดลิงก์นี้ในเบราว์เซอร์เพื่อทดสอบได้ทันทีว่า GEMINI_API_KEY บน Render เชื่อมต่อ Gemini Live API ได้จริงหรือไม่"""
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

    # เก็บสถานะท่อ Gemini ฝั่งเซิร์ฟเวอร์เพื่อให้สลับต่อใหม่ได้แบบไร้รอยต่อโดยไม่ตัดสายแว่น!
    session = {
        "ws": initial_ws,
        "model": active_model,
        "handle": None,
    }
    esp_closed = asyncio.Event()
    jpeg_buffer = bytearray()
    ai_is_speaking = False
    user_activity_open = False
    drop_current_turn_audio = False
    last_gemini_send_ts = time.monotonic()
    gemini_send_lock = asyncio.Lock()
    gemini_reconnect_lock = asyncio.Lock()

    # คิวส่งเสียงขนาดใหญ่ (~160 วินาทีของเสียง 16kHz) เพื่อรองรับคำตอบยาวๆ เช่น ก-ฮ ได้สบายๆ
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

    async def reconnect_upstream(reason: str) -> bool:
        """ต่อท่อ Gemini Live ใหม่เงียบๆ ภายในเซิร์ฟเวอร์โดยไม่ตัดสายแว่น ESP32"""
        nonlocal user_activity_open, ai_is_speaking, drop_current_turn_audio
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
            logger.warning("ท่อ Gemini สะดุด (%s) -> กำลังต่อใหม่อัตโนมัติแบบไม่ตัดสายแว่น...", reason)
            for attempt in range(3):
                if esp_closed.is_set():
                    return False
                try:
                    new_ws, new_model = await connect_gemini_live(api_key, resume_handle=session["handle"])
                    session["ws"] = new_ws
                    session["model"] = new_model
                    user_activity_open = False
                    ai_is_speaking = False
                    drop_current_turn_audio = False
                    runtime_state["upstream_resumes"] += 1
                    logger.info("ต่อท่อ Gemini ใหม่สำเร็จแบบไร้รอยต่อ (%s)!", new_model)
                    return True
                except Exception as exc:
                    logger.warning("ลองต่อท่อ Gemini ใหม่ครั้งที่ %d ไม่สำเร็จ: %s", attempt + 1, exc)
                    await asyncio.sleep(0.5)
            return False

    async def send_gemini_json(payload: dict):
        nonlocal last_gemini_send_ts
        async with gemini_send_lock:
            ws = session["ws"]
            if ws is None:
                return
            try:
                await ws.send(json.dumps(payload, ensure_ascii=False))
                last_gemini_send_ts = time.monotonic()
            except Exception as exc:
                logger.warning("send_gemini_json พบสายหลุด: %s -> รอท่อใหม่", exc)

    async def esp_to_gemini():
        """รับเสียง PCM ดิบ, ภาพ JPEG, และสัญญาณหยุดพูด (0x06) จากแว่น ESP32-S3"""
        nonlocal user_activity_open, ai_is_speaking, drop_current_turn_audio
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
                    runtime_state["cancels_handled"] += 1
                    logger.info("ได้รับคำสั่งหยุดเสียง AI (0x06) -> ล้างคิวทิ้ง %d แพ็กเก็ตทันที!", drained)
                    await outbound_q.put(bytes([MSG_AI_SPEAK_END]))

                elif opcode == MSG_ESP_AUDIO_PCM and payload:
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
                    if len(jpeg_buffer) < 250_000:
                        jpeg_buffer.extend(payload)

                elif opcode == MSG_ESP_JPEG_END:
                    if payload and len(jpeg_buffer) < 250_000:
                        jpeg_buffer.extend(payload)
                    if len(jpeg_buffer) > 500:
                        img_b64 = base64.b64encode(bytes(jpeg_buffer)).decode("ascii")
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

                elif opcode == MSG_ESP_SPEECH_END:
                    if user_activity_open:
                        user_activity_open = False
                        drop_current_turn_audio = False
                        await send_gemini_json({"realtimeInput": {"activityEnd": {}}})
                        logger.info("ผู้ใช้พูดจบ -> ส่ง activityEnd ให้ %s ตอบเสียงสดทันที", session["model"])

        except WebSocketDisconnect:
            logger.info("แว่น SeeSpeak ตัดการเชื่อมต่อ")
        except Exception as exc:
            runtime_state["last_error"] = f"esp_to_gemini: {exc}"
            logger.warning("esp_to_gemini หยุดทำงาน: %s", exc)
        finally:
            esp_closed.set()

    async def gemini_to_queue():
        """อ่านข้อมูลจาก Gemini Live API เต็มความเร็ว พร้อม Auto-Resume อัตโนมัติหากท่อ Google ปิดชั่วคราว"""
        nonlocal ai_is_speaking, drop_current_turn_audio
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

                    # เก็บ sessionResumption handle ล่าสุดไว้ต่อความจำอัตโนมัติหาก Google รีเซ็ตท่อ
                    resump = packet.get("sessionResumptionUpdate")
                    if resump and resump.get("resumable") and resump.get("newHandle"):
                        session["handle"] = resump["newHandle"]

                    server_content = packet.get("serverContent")
                    if not server_content:
                        continue

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
                                    await outbound_q.put(bytes([MSG_AI_SPEAK_START]))

                                pcm24k = base64.b64decode(inline_data["data"])
                                pcm16k = downsample_24k_to_16k(pcm24k)
                                for offset in range(0, len(pcm16k), OUTBOUND_PCM_CHUNK_BYTES):
                                    if drop_current_turn_audio:
                                        break
                                    chunk = pcm16k[offset : offset + OUTBOUND_PCM_CHUNK_BYTES]
                                    await outbound_q.put(bytes([MSG_AI_AUDIO_PCM]) + chunk)

                    if server_content.get("turnComplete") or server_content.get("interrupted"):
                        if current_reply_parts:
                            reply_text = "".join(current_reply_parts).strip()
                            runtime_state["last_ai_reply"] = reply_text
                            logger.info("SeeSpeak ตอบแล้ว: %s", reply_text)
                            current_reply_parts.clear()
                        runtime_state["turns_completed"] += 1
                        was_dropped = drop_current_turn_audio
                        drop_current_turn_audio = False
                        if ai_is_speaking and not was_dropped:
                            ai_is_speaking = False
                            await outbound_q.put(bytes([MSG_AI_SPEAK_END]))
                        else:
                            ai_is_speaking = False

                # หากหลุดออกจาก loop โดยที่แว่นยังต่ออยู่ -> ต่อท่อ Gemini ใหม่แบบไร้รอยต่อ!
                if not esp_closed.is_set():
                    if ai_is_speaking:
                        ai_is_speaking = False
                        await outbound_q.put(bytes([MSG_AI_SPEAK_END]))
                    ok = await reconnect_upstream("upstream closed cleanly")
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
        """ส่งเสียงจากคิวลงแว่น: 6 แพ็กเก็ตแรกส่งทันที 0ms (ตอบไวสุด) จากนั้นคุมที่ 1.14x (ไม่มีวันเร่งสปีด!)"""
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

    async def gemini_keepalive():
        """ส่งเสียงเงียบ 100ms ทุก 18 วินาทีเมื่อว่าง ป้องกัน Gemini ตัดสายด้วย 1008 inactivity"""
        try:
            while not esp_closed.is_set():
                await asyncio.sleep(5.0)
                idle_sec = time.monotonic() - last_gemini_send_ts
                if (
                    session["ws"] is not None
                    and idle_sec >= KEEPALIVE_INTERVAL_SEC
                    and not user_activity_open
                    and not ai_is_speaking
                ):
                    await send_gemini_json(
                        {
                            "realtimeInput": {
                                "audio": {
                                    "mimeType": "audio/pcm;rate=16000",
                                    "data": SILENCE_KEEPALIVE_B64,
                                }
                            }
                        }
                    )
                    runtime_state["keepalives_sent"] += 1
        except Exception as exc:
            logger.warning("gemini_keepalive หยุดทำงาน: %s", exc)

    tasks = {
        asyncio.create_task(esp_to_gemini()),
        asyncio.create_task(gemini_to_queue()),
        asyncio.create_task(queue_to_esp()),
        asyncio.create_task(gemini_keepalive()),
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
