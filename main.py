"""
Astra26 - ESP32 Multi-Sensor Telemetry & Camera Hub Backend
FastAPI production server designed for local execution and deployment on Render.
"""

import os
import json
import time
import asyncio
from typing import List, Optional, Dict, Any
from datetime import datetime
from collections import deque

from fastapi import FastAPI, Request, HTTPException, UploadFile, File, Form, status
from fastapi.responses import HTMLResponse, JSONResponse, StreamingResponse, FileResponse
from fastapi.staticfiles import StaticFiles
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field

# Application Initialization
app = FastAPI(
    title="Astra26 ESP32 Telemetry & Camera Hub",
    description="Real-time multi-sensor telemetry collector and monitoring platform",
    version="1.0.0"
)

# Enable CORS for all origins (permits ESP32, local dev, and hosted domains)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# In-Memory Telemetry Storage (Keeps up to 1000 recent readings)
MAX_HISTORY = 1000
telemetry_history: deque = deque(maxlen=MAX_HISTORY)
latest_telemetry: Optional[Dict[str, Any]] = None
latest_camera_info: Dict[str, Any] = {
    "available": False,
    "timestamp": None,
    "filename": "latest_camera.jpg",
    "size_bytes": 0
}

# Real-time SSE (Server-Sent Events) Client Queues
sse_clients: List[asyncio.Queue] = []
server_start_time = time.time()
total_packets_received = 0

# Static directory paths
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
PUBLIC_DIR = os.path.join(BASE_DIR, "public")
ASSETS_DIR = os.path.join(PUBLIC_DIR, "assets")
os.makedirs(ASSETS_DIR, exist_ok=True)
CAMERA_FILE_PATH = os.path.join(ASSETS_DIR, "latest_camera.jpg")


# Pydantic Model for incoming ESP32 sensor telemetry
class SensorPayload(BaseModel):
    deviceId: Optional[str] = "esp32-node-01"
    temperature: Optional[float] = Field(25.0, description="Waterproof sensor temperature in Celsius")
    rainLevel: Optional[float] = Field(0.0, description="Rain / Water level percentage 0-100%")
    rainRaw: Optional[int] = Field(4095, description="Raw ADC value from rain sensor (0-4095)")
    ldrValue: Optional[float] = Field(50.0, description="LDR Light level percentage 0-100%")
    ldrRaw: Optional[int] = Field(2048, description="Raw ADC value from LDR sensor (0-4095)")
    tiltState: Optional[int] = Field(0, description="Tilt sensor state: 0 = Level/Normal, 1 = Tilted/Triggered")
    voltage: Optional[float] = Field(12.4, description="Measured supply or battery voltage in Volts")
    rssi: Optional[int] = Field(-60, description="WiFi signal strength in dBm")
    uptime: Optional[int] = Field(None, description="ESP32 uptime in seconds")


def compute_derived_metrics(data: Dict[str, Any]) -> Dict[str, Any]:
    """Calculate percentages, alerts, and descriptive statuses for UI displays."""
    temp = float(data.get("temperature", 0.0))
    rain = float(data.get("rainLevel", 0.0))
    ldr = float(data.get("ldrValue", 0.0))
    tilt = int(data.get("tiltState", 0))
    volt = float(data.get("voltage", 0.0))

    # Derive Rain Status
    if rain < 5:
        rain_status = "Dry"
    elif rain < 35:
        rain_status = "Light Drops"
    elif rain < 70:
        rain_status = "Moderate Rain"
    else:
        rain_status = "Heavy Rain / High Water"

    # Derive Light Status
    if ldr < 15:
        light_status = "Dark / Night"
    elif ldr < 45:
        light_status = "Dim / Indoor"
    elif ldr < 75:
        light_status = "Daylight"
    else:
        light_status = "Bright Sunlight"

    # Derive Tilt Status
    tilt_status = "TILT DETECTED" if tilt != 0 else "Level (Normal)"

    # Derive Battery/Voltage percentage
    # Auto-detect standard voltage systems: 12V system (nominal 10.5V - 13.0V), 
    # 3.7V Li-ion (3.0V - 4.2V), or 5V rail (4.0V - 5.5V)
    if volt >= 8.0:
        # Assume 12V battery system
        v_min, v_max = 10.5, 12.8
        battery_pct = max(0, min(100, int(((volt - v_min) / (v_max - v_min)) * 100)))
        system_type = "12V Battery Pack"
    elif volt >= 2.5 and volt < 6.0:
        # Assume 1S Li-ion or 5V rail
        if volt <= 4.3:
            v_min, v_max = 3.2, 4.2
            battery_pct = max(0, min(100, int(((volt - v_min) / (v_max - v_min)) * 100)))
            system_type = "3.7V Li-Ion Cell"
        else:
            v_min, v_max = 4.2, 5.2
            battery_pct = max(0, min(100, int(((volt - v_min) / (v_max - v_min)) * 100)))
            system_type = "5V Regulated Rail"
    else:
        battery_pct = max(0, min(100, int((volt / 15.0) * 100)))
        system_type = "DC Source"

    now = datetime.now()
    return {
        **data,
        "temperatureF": round(temp * 9.0 / 5.0 + 32.0, 1),
        "rainStatus": rain_status,
        "lightStatus": light_status,
        "tiltStatus": tilt_status,
        "tiltAlert": bool(tilt != 0),
        "batteryPercent": battery_pct,
        "systemType": system_type,
        "timestamp": now.isoformat(),
        "timeFormatted": now.strftime("%H:%M:%S"),
        "dateFormatted": now.strftime("%Y-%m-%d"),
    }


async def broadcast_event(event_type: str, data: Any):
    """Broadcast an SSE event payload to all connected browser clients."""
    payload = f"event: {event_type}\ndata: {json.dumps(data)}\n\n"
    dead_clients = []
    for client in sse_clients:
        try:
            client.put_nowait(payload)
        except asyncio.QueueFull:
            dead_clients.append(client)
        except Exception:
            dead_clients.append(client)
    for dc in dead_clients:
        if dc in sse_clients:
            sse_clients.remove(dc)


# =====================================================================
# REST API ENDPOINTS
# =====================================================================

@app.post("/api/telemetry")
async def ingest_telemetry(payload: SensorPayload):
    """
    Ingest real-time sensor metrics sent by ESP32 over WiFi HTTP POST.
    """
    global latest_telemetry, total_packets_received
    total_packets_received += 1

    # Convert payload and compute high-level statuses
    raw_data = payload.model_dump()
    enriched = compute_derived_metrics(raw_data)

    latest_telemetry = enriched
    telemetry_history.append(enriched)

    # Push instantly to web dashboard through Server-Sent Events
    await broadcast_event("telemetry", enriched)

    return {
        "status": "success",
        "message": "Telemetry processed successfully",
        "timestamp": enriched["timestamp"],
        "receivedCount": total_packets_received
    }


@app.get("/api/telemetry/latest")
async def get_latest_telemetry():
    """Retrieve the single most recent telemetry packet."""
    if latest_telemetry is None:
        # Return fallback mock reading if hardware has not transmitted yet
        mock_reading = compute_derived_metrics({
            "deviceId": "esp32-node-01 (Standby)",
            "temperature": 26.5,
            "rainLevel": 0.0,
            "rainRaw": 4095,
            "ldrValue": 55.0,
            "ldrRaw": 2250,
            "tiltState": 0,
            "voltage": 12.45,
            "rssi": -65,
            "uptime": int(time.time() - server_start_time)
        })
        return {
            "status": "standby",
            "message": "Waiting for initial ESP32 telemetry packet",
            "data": mock_reading,
            "camera": latest_camera_info
        }
    return {
        "status": "active",
        "data": latest_telemetry,
        "camera": latest_camera_info
    }


@app.get("/api/telemetry/history")
async def get_telemetry_history(limit: int = 100):
    """Retrieve historical telemetry logs for charting and tabular display."""
    limit = max(1, min(limit, MAX_HISTORY))
    records = list(telemetry_history)[-limit:]
    return {
        "status": "success",
        "count": len(records),
        "data": records
    }


@app.post("/api/telemetry/simulate")
async def simulate_telemetry(payload: Optional[SensorPayload] = None):
    """
    Allows developers or users to test the dashboard directly from browser UI
    or curl without physical ESP32 powered on.
    """
    t = time.time()
    import math
    if payload is None:
        sim_temp = round(25.0 + 3.0 * math.sin(t / 10.0), 1)
        sim_rain = round(max(0.0, 50.0 + 40.0 * math.cos(t / 8.0)), 1)
        sim_ldr = round(max(5.0, 60.0 + 30.0 * math.sin(t / 12.0)), 1)
        sim_tilt = 1 if (int(t) % 20 > 15) else 0
        sim_volt = round(12.3 + 0.3 * math.sin(t / 15.0), 2)
        payload = SensorPayload(
            deviceId="esp32-simulator",
            temperature=sim_temp,
            rainLevel=sim_rain,
            rainRaw=int(4095 - (sim_rain * 40.95)),
            ldrValue=sim_ldr,
            ldrRaw=int(sim_ldr * 40.95),
            tiltState=sim_tilt,
            voltage=sim_volt,
            rssi=-58,
            uptime=int(t - server_start_time)
        )
    return await ingest_telemetry(payload)


@app.post("/api/telemetry/clear")
async def clear_history():
    """Clear memory logs."""
    telemetry_history.clear()
    return {"status": "success", "message": "History cleared"}


@app.post("/api/camera/upload")
async def upload_camera_frame(
    file: UploadFile = File(...),
    deviceId: str = Form("esp32-cam-01")
):
    """
    Receives JPEG camera snapshot frames uploaded by ESP32-CAM board.
    """
    global latest_camera_info
    try:
        content = await file.read()
        if not content:
            raise HTTPException(status_code=400, detail="Empty image file received")

        with open(CAMERA_FILE_PATH, "wb") as f:
            f.write(content)

        now = datetime.now()
        latest_camera_info = {
            "available": True,
            "deviceId": deviceId,
            "timestamp": now.isoformat(),
            "timeFormatted": now.strftime("%H:%M:%S"),
            "filename": "latest_camera.jpg",
            "size_bytes": len(content),
            "url": f"/assets/latest_camera.jpg?t={int(time.time())}"
        }

        # Broadcast camera snapshot alert to all connected dashboards
        await broadcast_event("camera_frame", latest_camera_info)

        return {
            "status": "success",
            "message": "Camera snapshot frame stored successfully",
            "camera": latest_camera_info
        }
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/api/camera/latest")
async def get_latest_camera():
    """Serve the latest camera image, or default fallback preview."""
    if os.path.exists(CAMERA_FILE_PATH) and latest_camera_info.get("available"):
        return FileResponse(CAMERA_FILE_PATH, media_type="image/jpeg")
    return JSONResponse(status_code=404, content={"status": "not_found", "message": "No camera frame uploaded yet"})


@app.get("/api/status")
async def server_status():
    """System health check endpoint for monitoring & Render keep-alive."""
    now = time.time()
    last_seen_sec = None
    if latest_telemetry and "timestamp" in latest_telemetry:
        try:
            last_dt = datetime.fromisoformat(latest_telemetry["timestamp"])
            last_seen_sec = round((datetime.now() - last_dt).total_seconds(), 1)
        except Exception:
            pass

    return {
        "status": "healthy",
        "service": "Astra26 ESP32 Telemetry Server",
        "serverUptimeSeconds": int(now - server_start_time),
        "totalPacketsReceived": total_packets_received,
        "activeSseClients": len(sse_clients),
        "lastSeenSecondsAgo": last_seen_sec,
        "deviceConnected": (last_seen_sec is not None and last_seen_sec < 20)
    }


# =====================================================================
# SERVER-SENT EVENTS (SSE) STREAM
# =====================================================================

@app.get("/api/events")
async def sse_event_stream(request: Request):
    """
    High-performance real-time streaming endpoint for web clients.
    Instantly pushes new sensor telemetry and camera frames without polling.
    """
    queue = asyncio.Queue(maxsize=30)
    sse_clients.append(queue)

    async def event_generator():
        try:
            # Send initial state immediately upon browser connection
            if latest_telemetry:
                yield f"event: telemetry\ndata: {json.dumps(latest_telemetry)}\n\n"
            if latest_camera_info.get("available"):
                yield f"event: camera_frame\ndata: {json.dumps(latest_camera_info)}\n\n"

            while True:
                # Disconnect if client tab closes
                if await request.is_disconnected():
                    break

                try:
                    # Wait for next event or send heartbeat keep-alive
                    payload = await asyncio.wait_for(queue.get(), timeout=12.0)
                    yield payload
                except asyncio.TimeoutError:
                    # Heartbeat comment to keep connection alive through cloud proxies (Render, Cloudflare)
                    yield f": keep-alive {int(time.time())}\n\n"
        except asyncio.CancelledError:
            pass
        finally:
            if queue in sse_clients:
                sse_clients.remove(queue)

    return StreamingResponse(
        event_generator(),
        media_type="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            "Connection": "keep-alive",
            "X-Accel-Buffering": "no"
        }
    )


# =====================================================================
# STATIC ASSETS & SINGLE PAGE APPLICATION SERVING
# =====================================================================

if os.path.exists(PUBLIC_DIR):
    app.mount("/", StaticFiles(directory=PUBLIC_DIR, html=True), name="static")

if __name__ == "__main__":
    import uvicorn
    port = int(os.environ.get("PORT", 8000))
    print(f"[*] Starting Astra26 ESP32 Telemetry Server on 0.0.0.0:{port}")
    uvicorn.run("main:app", host="0.0.0.0", port=port)
