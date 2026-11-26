"""
SeeSpeak V3 — Edge-to-Cloud Binary Relay Server (Architecture 2B)
=================================================================
ทำหน้าที่เป็น "ล่ามตัวกลางความเร็วสูง" ระหว่างแว่นตา XIAO ESP32S3 Sense กับ Google Gemini Live API:
- ฝั่งแว่น ESP32-S3 ส่ง-รับเฉพาะ Raw Binary (PCM 16kHz / JPEG) ช่วยลดภาระ CPU/RAM บนบอร์ดกว่า 70%
- ฝั่ง Relay เชื่อมต่อ Gemini Live API (gemini-3.8-live เป็นหลัก) ให้สำเร็จก่อนตอบรับแว่น (ไม่มีลูปเชื่อม-หลุดซ้ำ)
- ใช้รูปแบบมาตรฐาน v1beta `realtimeInput.mediaChunks` สำหรับทั้งเสียง PCM 16kHz และภาพ JPEG
- เมื่อแว่นส่งสัญญาณจบคำพูด (0x05) จะเติมช่วงเงียบสะอาด 500 ms ให้ VAD ฝั่ง Google ตอบกลับทันทีโดยไม่ตัดท่อ WebSocket
- แปลงเสียงตอบกลับ 24kHz -> 16kHz แบบเรียลไทม์ พร้อมสัญญาณกันเสียงสะท้อน (Echo Gate)
- มีหน้า /test-key สำหรับตรวจสอบว่า GEMINI_API_KEY บน Render.com เชื่อมต่อ Gemini Live API ได้จริงทันที
"""

import asyncio
import base64
import json
import logging
import os
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

app = FastAPI(title="SeeSpeak V3 Gemini Live Relay", version="3.2.0")

# ---------------------------------------------------------------------------
# ค่าตั้งต้นจาก Environment Variables (ตั้งบน Render / Cloud Run เพื่อซ่อน API Key อย่างปลอดภัย)
# ---------------------------------------------------------------------------
GEMINI_API_KEY = os.getenv("GEMINI_API_KEY", "").strip()
GEMINI_VOICE_NAME = os.getenv("GEMINI_VOICE_NAME", "Aoede").strip()

_env_models = os.getenv("GEMINI_LIVE_MODELS", "").strip()
if _env_models:
    LIVE_MODEL_CANDIDATES = [m.strip() for m in _env_models.split(",") if m.strip()]
else:
    LIVE_MODEL_CANDIDATES = [
        "models/gemini-3.8-live",
        "models/gemini-3.1-flash-live-preview",
        "models/gemini-live-2.5-flash-native-audio",
        "models/gemini-2.5-flash-native-audio-preview-12-2025",
    ]

GEMINI_LIVE_WS_URL = (
    "wss://generativelanguage.googleapis.com/ws/"
    "google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent"
)

SEESPEAK_SYSTEM_PROMPT = (
    "คุณคือ ซีสปีค (SeeSpeak) ผู้ช่วยอัจฉริยะบนแว่นตาสำหรับผู้บกพร่องทางสายตาในประเทศไทย "
    "คุณมองเห็นภาพสดจากกล้องด้านหน้าแว่นตาและได้ยินเสียงคำถามของผู้ใช้งานตลอดเวลา "
    "หลักการตอบที่สำคัญมาก:\n"
    "1. ตอบเป็นภาษาไทยด้วยน้ำเสียงผู้หญิงที่สุภาพ อ่อนโยน ชัดเจน และกระชับทันทีภายใน 1-3 ประโยค (ลงท้ายด้วยคำว่า ค่ะ)\n"
    "2. หากผู้ใช้ถามเรื่องสี สิ่งของ หรือสภาพแวดล้อมตรงหน้า ให้บอกคำตอบตรงประเด็นทันที พร้อมระบุตำแหน่งทิศทาง (ด้านซ้าย ตรงกลาง ด้านขวา) และระยะห่างโดยประมาณ\n"
    "3. หากผู้ใช้ขอให้อ่านข้อความ ป้าย ฉลากยา ราคา หรือเอกสาร ให้อ่านข้อความที่มองเห็นทั้งหมดอย่างชัดเจนทันทีแม้ภาพจะเอียงหรือเบลอเล็กน้อย\n"
    "4. หากผู้ใช้ถามเรื่องเงินหรือธนบัตรไทย (20 สีเขียว, 50 สีฟ้า, 100 สีแดง, 500 สีม่วง, 1000 สีเทาน้ำตาล) และเหรียญ ให้พูดสรุป 'ยอดเงินรวมทั้งหมด' ขึ้นเป็นคำแรกเสมอ\n"
    "5. จดจำบริบทของบทสนทนาก่อนหน้าเสมอ เพื่อให้ผู้ใช้ถามต่อเนื่องได้เหมือนคุยกับผู้ช่วยส่วนตัว\n"
    "6. หากได้ยินเพียงเสียงรบกวนรอบข้างเบาๆ ที่ไม่ใช่คำถามถึงคุณ ให้เงียบไว้ไม่ต้องตอบ"
)

# Binary Frame Protocol Opcodes (ต้องตรงกับ firmware_seespeak_v3.ino)
MSG_ESP_AUDIO_PCM   = 0x01  # ESP32 -> Relay: 16kHz 16-bit mono PCM audio
MSG_ESP_JPEG_CHUNK  = 0x02  # ESP32 -> Relay: JPEG frame chunk (intermediate)
MSG_ESP_JPEG_END    = 0x04  # ESP32 -> Relay: JPEG frame final chunk
MSG_ESP_SPEECH_END  = 0x05  # ESP32 -> Relay: User paused speaking -> feed clean silence for fast VAD trigger

MSG_AI_AUDIO_PCM    = 0x10  # Relay -> ESP32: 16kHz 16-bit mono PCM audio
MSG_AI_SPEAK_START  = 0x11  # Relay -> ESP32: Mute mic (prevent echo)
MSG_AI_SPEAK_END    = 0x12  # Relay -> ESP32: Unmute mic after playback

OUTBOUND_PCM_CHUNK_BYTES = 2048  # 64 ms @ 16kHz 16-bit mono
SILENCE_FRAME_16K_B64 = base64.b64encode(b"\x00\x00" * 1600).decode("ascii")  # 100 ms clean silence

# สถานะล่าสุดสำหรับดูใน /health เพื่อช่วยดีบักได้ทันที
runtime_state = {
    "last_active_model": None,
    "last_error": None,
    "hint": None,
    "api_key_prefix": (GEMINI_API_KEY[:4] + "...") if len(GEMINI_API_KEY) >= 4 else "",
    "sessions_served": 0,
    "images_forwarded": 0,
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


async def connect_gemini_live(api_key: str):
    """เชื่อมต่อ Gemini Live API พร้อมสลับโมเดลสำรองอัตโนมัติ (หยุดทันทีหาก API Key ไม่ถูกต้อง)"""
    last_error: Optional[Exception] = None
    url = f"{GEMINI_LIVE_WS_URL}?key={api_key}"

    for model_name in LIVE_MODEL_CANDIDATES:
        ws = None
        try:
            logger.info("กำลังเชื่อมต่อ Gemini Live API รุ่น: %s", model_name)
            ws = await websockets.connect(
                url,
                max_size=8 * 1024 * 1024,
                ping_interval=20,
                ping_timeout=20,
            )
            setup_payload = {
                "setup": {
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
                }
            }
            await ws.send(json.dumps(setup_payload, ensure_ascii=False))
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
            # หยุดทันทีหาก API Key ไม่ถูกต้องหรือถูกปิดใช้งาน (ไม่ต้องลองวนครบทุกโมเดลให้ช้า)
            if "API key not valid" in err_text or "API_KEY_INVALID" in err_text:
                runtime_state["hint"] = (
                    "GEMINI_API_KEY ไม่ถูกต้องหรือถูกระงับ/เป็นคีย์คนละประเภท — "
                    "กรุณาสร้าง API Key ใหม่ (AIzaSy...) จาก https://aistudio.google.com/apikey "
                    "แล้วนำไปใส่ใน Render.com -> Environment -> GEMINI_API_KEY"
                )
                break

    raise RuntimeError(f"ไม่สามารถเชื่อมต่อโมเดล Gemini Live API ตัวใดได้เลย: {last_error}")


@app.get("/")
@app.get("/health")
async def health_check():
    return JSONResponse(
        {
            "status": "ok",
            "version": "3.2.0",
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

    # เชื่อมต่อ Gemini Live ให้สำเร็จก่อนตอบรับแว่น (หากคีย์ผิด แว่นจะไม่สั่นหลอกและไม่วนถ่ายภาพซ้ำ)
    try:
        gemini_ws, active_model = await connect_gemini_live(api_key)
    except Exception as exc:
        runtime_state["last_error"] = str(exc)
        logger.error("เปิดเซสชัน Gemini Live ไม่สำเร็จ: %s", exc)
        await esp_ws.close(code=1011, reason="Gemini Live unavailable")
        return

    await esp_ws.accept()
    logger.info("เปิดท่อคุยสด SeeSpeak <-> %s เรียบร้อยแล้ว!", active_model)

    jpeg_buffer = bytearray()
    ai_is_speaking = False

    async def esp_to_gemini():
        """รับเสียง PCM ดิบและภาพ JPEG จากแว่น ESP32-S3 ส่งต่อไปยัง Gemini Live API ด้วย mediaChunks"""
        try:
            while True:
                msg = await esp_ws.receive()
                if msg["type"] == "websocket.disconnect":
                    break
                data = msg.get("bytes")
                if not data:
                    continue

                opcode = data[0]
                payload = data[1:]

                if opcode == MSG_ESP_AUDIO_PCM and payload:
                    audio_b64 = base64.b64encode(payload).decode("ascii")
                    await gemini_ws.send(
                        json.dumps(
                            {
                                "realtimeInput": {
                                    "mediaChunks": [
                                        {
                                            "mimeType": "audio/pcm;rate=16000",
                                            "data": audio_b64,
                                        }
                                    ]
                                }
                            }
                        )
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
                        logger.info("ส่งภาพกล้องสด (%d bytes) ขึ้น %s", len(jpeg_buffer), active_model)
                        await gemini_ws.send(
                            json.dumps(
                                {
                                    "realtimeInput": {
                                        "mediaChunks": [
                                            {
                                                "mimeType": "image/jpeg",
                                                "data": img_b64,
                                            }
                                        ]
                                    }
                                }
                            )
                        )
                    jpeg_buffer.clear()

                elif opcode == MSG_ESP_SPEECH_END:
                    # เติมช่วงเงียบดิจิทัลสะอาด 500 ms (5 x 100 ms) เพื่อให้ VAD ฝั่ง Gemini ปิดเทิร์นและตอบกลับทันทีโดยไม่ตัดท่อ WS
                    for _ in range(5):
                        await gemini_ws.send(
                            json.dumps(
                                {
                                    "realtimeInput": {
                                        "mediaChunks": [
                                            {
                                                "mimeType": "audio/pcm;rate=16000",
                                                "data": SILENCE_FRAME_16K_B64,
                                            }
                                        ]
                                    }
                                }
                            )
                        )
                        await asyncio.sleep(0.02)

        except WebSocketDisconnect:
            logger.info("แว่น SeeSpeak ตัดการเชื่อมต่อ")
        except Exception as exc:
            runtime_state["last_error"] = f"esp_to_gemini: {exc}"
            logger.warning("esp_to_gemini หยุดทำงาน: %s", exc)

    async def gemini_to_esp():
        """รับเสียงตอบกลับจาก Gemini Live API แปลงเป็น PCM 16kHz ส่งกลับออกลำโพงแว่นทันที"""
        nonlocal ai_is_speaking
        try:
            async for raw_msg in gemini_ws:
                if isinstance(raw_msg, bytes):
                    raw_msg = raw_msg.decode("utf-8", errors="ignore")
                packet = json.loads(raw_msg)
                server_content = packet.get("serverContent")
                if not server_content:
                    continue

                model_turn = server_content.get("modelTurn")
                if model_turn:
                    for part in model_turn.get("parts", []):
                        inline_data = part.get("inlineData")
                        if not inline_data:
                            continue
                        mime = inline_data.get("mimeType", "")
                        if mime.startswith("audio/pcm"):
                            if not ai_is_speaking:
                                ai_is_speaking = True
                                await esp_ws.send_bytes(bytes([MSG_AI_SPEAK_START]))

                            pcm24k = base64.b64decode(inline_data["data"])
                            pcm16k = downsample_24k_to_16k(pcm24k)
                            for offset in range(0, len(pcm16k), OUTBOUND_PCM_CHUNK_BYTES):
                                chunk = pcm16k[offset : offset + OUTBOUND_PCM_CHUNK_BYTES]
                                await esp_ws.send_bytes(bytes([MSG_AI_AUDIO_PCM]) + chunk)
                                runtime_state["audio_chunks_sent_to_esp"] += 1

                if server_content.get("turnComplete") or server_content.get("interrupted"):
                    if ai_is_speaking:
                        ai_is_speaking = False
                        await esp_ws.send_bytes(bytes([MSG_AI_SPEAK_END]))

        except Exception as exc:
            runtime_state["last_error"] = f"gemini_to_esp: {exc}"
            logger.warning("gemini_to_esp หยุดทำงาน: %s", exc)

    task_up = asyncio.create_task(esp_to_gemini())
    task_down = asyncio.create_task(gemini_to_esp())
    done, pending = await asyncio.wait({task_up, task_down}, return_when=asyncio.FIRST_COMPLETED)
    for t in pending:
        t.cancel()
    try:
        await gemini_ws.close()
    except Exception:
        pass
    try:
        await esp_ws.close()
    except Exception:
        pass
    logger.info("จบเซสชันคุยสด SeeSpeak เรียบร้อย")
