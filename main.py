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

# Populate latest_camera_info if an image is already cached on disk
if os.path.exists(CAMERA_FILE_PATH) and os.path.getsize(CAMERA_FILE_PATH) > 0:
    try:
        mtime = os.path.getmtime(CAMERA_FILE_PATH)
        dt = datetime.fromtimestamp(mtime)
        latest_camera_info = {
            "available": True,
            "deviceId": "esp32-cam-01",
            "timestamp": dt.isoformat(),
            "timeFormatted": dt.strftime("%H:%M:%S"),
            "filename": "latest_camera.jpg",
            "size_bytes": os.path.getsize(CAMERA_FILE_PATH),
            "url": f"/assets/latest_camera.jpg?t={int(mtime)}"
        }
    except Exception:
        pass


# Pydantic Model for incoming ESP32 sensor telemetry
class SensorPayload(BaseModel):
    deviceId: Optional[str] = "esp32-node-01"
    temperature: Optional[float] = Field(25.0, description="Waterproof sensor temperature in Celsius")
    rainLevel: Optional[float] = Field(0.0, description="Rain / Water level percentage 0-100%")
    rainRaw: Optional[int] = Field(0, description="Raw ADC value from water/rain sensor (0-4095)")
    soilMoisture: Optional[float] = Field(45.0, description="Soil moisture level percentage 0-100%")
    soilRaw: Optional[int] = Field(2048, description="Raw ADC value from soil moisture sensor (0-4095)")
    soilMaxRaw: Optional[int] = Field(4095, description="Maximum scale for 12-bit ADC (4096 discrete levels)")
    ldrValue: Optional[float] = Field(50.0, description="LDR Light level percentage 0-100%")
    ldrRaw: Optional[int] = Field(2048, description="Raw ADC value from LDR sensor (0-4095)")
    tiltState: Optional[int] = Field(0, description="Tilt sensor state: 0 = Level/Normal, 1 = Tilted/Triggered")
    voltage: Optional[float] = Field(12.4, description="Measured supply or battery voltage in Volts")
    rssi: Optional[int] = Field(-60, description="WiFi signal strength in dBm")
    uptime: Optional[int] = Field(None, description="ESP32 uptime in seconds")
    mpuConnected: Optional[bool] = Field(False, description="Whether MPU6050 6-axis IMU is connected")
    pitch: Optional[float] = Field(0.0, description="Pitch inclination angle in degrees (-90 to +90)")
    roll: Optional[float] = Field(0.0, description="Roll inclination angle in degrees (-180 to +180)")
    vibration: Optional[float] = Field(0.02, description="Vibration intensity in g (dynamic acceleration magnitude)")
    vibrationPct: Optional[float] = Field(5.0, description="Vibration index percentage 0-100%")
    accelX: Optional[float] = Field(0.0, description="X-axis linear acceleration in g")
    accelY: Optional[float] = Field(0.0, description="Y-axis linear acceleration in g")
    accelZ: Optional[float] = Field(1.0, description="Z-axis linear acceleration in g")
    totalG: Optional[float] = Field(1.0, description="Total vector acceleration magnitude in g")
    gyroX: Optional[float] = Field(0.0, description="X-axis angular velocity in deg/s")
    gyroY: Optional[float] = Field(0.0, description="Y-axis angular velocity in deg/s")
    gyroZ: Optional[float] = Field(0.0, description="Z-axis angular velocity in deg/s")
    gyroMag: Optional[float] = Field(0.0, description="Rotational angular speed magnitude in deg/s")


def compute_derived_metrics(data: Dict[str, Any]) -> Dict[str, Any]:
    """Calculate percentages, alerts, and descriptive statuses for UI displays."""
    import math

    temp = float(data.get("temperature", 0.0))
    rain = float(data.get("rainLevel", 0.0))
    soil = float(data.get("soilMoisture", 0.0))
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

    # Derive Soil Moisture Status & Soil Health Recommendation
    # ESP32 12-bit ADC has 4096 discrete levels: 0 to 4095
    if soil < 15:
        soil_status = "Very Dry / Desert"
        soil_health = "Critical Drought: Immediate Irrigation Needed"
    elif soil < 35:
        soil_status = "Dry Soil"
        soil_health = "Low Moisture: Watering Recommended"
    elif soil < 70:
        soil_status = "Optimal Moisture"
        soil_health = "Healthy Soil Condition: Balanced Hydration"
    elif soil < 85:
        soil_status = "Wet / Saturated"
        soil_health = "Abundant Moisture: Irrigation Sufficient"
    else:
        soil_status = "Waterlogged / Flooded"
        soil_health = "Over-saturated: Risk of Root Asphyxiation"

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
        v_min, v_max = 10.5, 12.8
        battery_pct = max(0, min(100, int(((volt - v_min) / (v_max - v_min)) * 100)))
        system_type = "12V Battery Pack"
    elif volt >= 2.5 and volt < 6.0:
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

    # Derive MPU6050 Motion, Angles & Vibration metrics
    pitch = float(data.get("pitch", 0.0))
    roll = float(data.get("roll", 0.0))
    ax = float(data.get("accelX", 0.0))
    ay = float(data.get("accelY", 0.0))
    az = float(data.get("accelZ", 1.0))
    gx = float(data.get("gyroX", 0.0))
    gy = float(data.get("gyroY", 0.0))
    gz = float(data.get("gyroZ", 0.0))
    mpu_connected = bool(data.get("mpuConnected", False))

    total_g = float(data.get("totalG", round(math.sqrt(ax * ax + ay * ay + az * az), 2)))
    gyro_mag = float(data.get("gyroMag", round(math.sqrt(gx * gx + gy * gy + gz * gz), 1)))

    # Vibration intensity: dynamic deviation from 1.0g gravity
    vib_g = float(data.get("vibration", round(abs(total_g - 1.0), 2)))
    vib_pct = float(data.get("vibrationPct", round(max(0.0, min(100.0, (vib_g / 1.5) * 100.0)), 1)))

    if vib_g < 0.08:
        vib_status = "Smooth / Stable"
    elif vib_g < 0.25:
        vib_status = "Low Vibration"
    elif vib_g < 0.60:
        vib_status = "Moderate Vibration"
    else:
        vib_status = "Severe Shock / High Vibration"

    if mpu_connected:
        if vib_g >= 0.60:
            motion_status = "Severe Shock Alert"
        elif abs(pitch) > 25.0 or abs(roll) > 25.0:
            motion_status = "Incline Warning"
        elif vib_g >= 0.25:
            motion_status = "Moderate Vibration"
        elif gyro_mag > 15.0 or abs(total_g - 1.0) > 0.25:
            motion_status = "Dynamic Motion"
        else:
            motion_status = "Level Horizon"
    else:
        motion_status = "Standby"

    now = datetime.now()
    return {
        **data,
        "temperatureF": round(temp * 9.0 / 5.0 + 32.0, 1),
        "rainStatus": rain_status,
        "soilStatus": soil_status,
        "soilHealth": soil_health,
        "soilRawMax": 4095,
        "soilAdcLevels": 4096,
        "lightStatus": light_status,
        "tiltStatus": tilt_status,
        "tiltAlert": bool(tilt != 0),
        "batteryPercent": battery_pct,
        "systemType": system_type,
        "pitch": pitch,
        "roll": roll,
        "accelX": ax,
        "accelY": ay,
        "accelZ": az,
        "gyroX": gx,
        "gyroY": gy,
        "gyroZ": gz,
        "totalG": total_g,
        "gyroMag": gyro_mag,
        "vibration": vib_g,
        "vibrationPct": vib_pct,
        "vibrationStatus": vib_status,
        "motionStatus": motion_status,
        "mpuConnected": mpu_connected,
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
            "rainRaw": 0,
            "soilMoisture": 52.0,
            "soilRaw": 2260,
            "soilMaxRaw": 4095,
            "ldrValue": 55.0,
            "ldrRaw": 2250,
            "tiltState": 0,
            "voltage": 12.45,
            "rssi": -65,
            "uptime": int(time.time() - server_start_time),
            "mpuConnected": False,
            "pitch": 0.0,
            "roll": 0.0,
            "vibration": 0.02,
            "vibrationPct": 1.5,
            "accelX": 0.0,
            "accelY": 0.0,
            "accelZ": 1.0,
            "totalG": 1.0,
            "gyroX": 0.0,
            "gyroY": 0.0,
            "gyroZ": 0.0,
            "gyroMag": 0.0
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
        sim_soil = round(max(5.0, min(95.0, 55.0 + 35.0 * math.sin(t / 11.0))), 1)
        # 12-bit ADC raw value: 0 to 4095 (4096 levels)
        sim_soil_raw = int(3200 - (sim_soil / 100.0) * (3200 - 1400))
        sim_ldr = round(max(5.0, 60.0 + 30.0 * math.sin(t / 12.0)), 1)
        sim_tilt = 1 if (int(t) % 20 > 15) else 0
        sim_volt = round(12.3 + 0.3 * math.sin(t / 15.0), 2)
        sim_pitch = round(12.0 * math.sin(t / 6.0), 1)
        sim_roll = round(18.0 * math.cos(t / 5.0), 1)
        sim_ax = round(0.2 * math.sin(t / 4.0), 2)
        sim_ay = round(0.15 * math.cos(t / 4.0), 2)
        sim_az = round(0.98 + 0.08 * math.sin(t / 6.0), 2)
        sim_total_g = round(math.sqrt(sim_ax**2 + sim_ay**2 + sim_az**2), 2)
        # Simulate vibration spike during periodic cycles
        sim_vib = round(abs(sim_total_g - 1.0) + (0.45 if (int(t) % 18 > 14) else 0.02), 2)
        sim_gx = round(3.5 * math.cos(t / 3.0), 1)
        sim_gy = round(2.0 * math.sin(t / 3.0), 1)
        sim_gz = round(1.2 * math.cos(t / 2.0), 1)
        sim_gyro_mag = round(math.sqrt(sim_gx**2 + sim_gy**2 + sim_gz**2), 1)
        payload = SensorPayload(
            deviceId="esp32-simulator",
            temperature=sim_temp,
            rainLevel=sim_rain,
            rainRaw=int(sim_rain * 28.0),
            soilMoisture=sim_soil,
            soilRaw=sim_soil_raw,
            soilMaxRaw=4095,
            ldrValue=sim_ldr,
            ldrRaw=int(sim_ldr * 38.0),
            tiltState=sim_tilt,
            voltage=sim_volt,
            rssi=-58,
            uptime=int(t - server_start_time),
            mpuConnected=True,
            pitch=sim_pitch,
            roll=sim_roll,
            vibration=sim_vib,
            vibrationPct=round(max(0.0, min(100.0, (sim_vib / 1.5) * 100.0)), 1),
            accelX=sim_ax,
            accelY=sim_ay,
            accelZ=sim_az,
            totalG=sim_total_g,
            gyroX=sim_gx,
            gyroY=sim_gy,
            gyroZ=sim_gz,
            gyroMag=sim_gyro_mag
        )
    return await ingest_telemetry(payload)


@app.post("/api/telemetry/clear")
async def clear_history():
    """Clear memory logs."""
    telemetry_history.clear()
    return {"status": "success", "message": "History cleared"}


@app.post("/api/camera/upload")
async def upload_camera_frame(
    request: Request,
    file: Optional[UploadFile] = File(None),
    deviceId: Optional[str] = Form(None)
):
    """
    Receives JPEG camera snapshot frames uploaded by ESP32-CAM board.
    Supports both multipart/form-data ('file' or 'image' field) and raw binary JPEG stream.
    """
    global latest_camera_info
    try:
        content = None
        dev_id = deviceId or request.headers.get("x-device-id") or request.query_params.get("deviceId", "esp32-cam-01")

        # 1. Try reading from multipart file upload if present
        if file is not None:
            content = await file.read()
        else:
            content_type = request.headers.get("content-type", "")
            if "multipart/form-data" in content_type:
                try:
                    form = await request.form()
                    for key in ["file", "image", "frame", "upload", "snapshot"]:
                        if key in form and hasattr(form[key], "read"):
                            content = await form[key].read()
                            break
                    if not content and "file" in form and isinstance(form["file"], (bytes, bytearray)):
                        content = bytes(form["file"])
                    if not dev_id and "deviceId" in form:
                        dev_id = str(form["deviceId"])
                except Exception:
                    pass

        # 2. If not multipart or form had no file, read raw body directly
        if not content:
            body = await request.body()
            if body and len(body) > 100:
                content = body

        if not content or len(content) < 100:
            raise HTTPException(status_code=400, detail="Empty or invalid image data received (must be at least 100 bytes)")

        with open(CAMERA_FILE_PATH, "wb") as f:
            f.write(content)

        now = datetime.now()
        latest_camera_info = {
            "available": True,
            "deviceId": str(dev_id),
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
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/api/camera/latest")
async def get_latest_camera():
    """Serve the latest camera image, or default standby preview."""
    no_cache_headers = {
        "Cache-Control": "no-cache, no-store, must-revalidate",
        "Pragma": "no-cache",
        "Expires": "0"
    }
    if os.path.exists(CAMERA_FILE_PATH) and os.path.getsize(CAMERA_FILE_PATH) > 0:
        return FileResponse(CAMERA_FILE_PATH, media_type="image/jpeg", headers=no_cache_headers)
    
    standby_path = os.path.join(ASSETS_DIR, "camera_standby.jpg")
    if os.path.exists(standby_path):
        return FileResponse(standby_path, media_type="image/jpeg", headers=no_cache_headers)
        
    return JSONResponse(status_code=404, content={"status": "not_found", "message": "No camera frame uploaded yet"})


@app.get("/api/camera/info")
async def get_camera_info():
    """Return JSON metadata about camera frame status."""
    has_file = os.path.exists(CAMERA_FILE_PATH) and os.path.getsize(CAMERA_FILE_PATH) > 0
    return {
        "status": "success",
        "available": bool(latest_camera_info.get("available") or has_file),
        "camera": latest_camera_info
    }


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
