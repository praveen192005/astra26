/**
 * ASTRA-26 REAL-TIME TELEMETRY ENGINE
 * Manages Server-Sent Events (SSE), Chart.js graphs, hardware simulators,
 * audio alarms, and DOM bindings.
 */

(function () {
  'use strict';

  // --- STATE ---
  let isCelsius = true;
  let soundEnabled = true;
  let autoSimInterval = null;
  let maxChartPoints = 20;
  let packetCounter = 0;
  let minTemp = Infinity;
  let maxTemp = -Infinity;
  let audioCtx = null;
  let historicalRecords = [];

  // --- DOM REFERENCES ---
  const el = {
    // Header
    deviceStatusPill: document.getElementById('deviceStatusPill'),
    deviceStatusText: document.getElementById('deviceStatusText'),
    wifiRssiVal: document.getElementById('wifiRssiVal'),
    packetCountVal: document.getElementById('packetCountVal'),
    btnToggleSound: document.getElementById('btnToggleSound'),
    soundIcon: document.getElementById('soundIcon'),
    btnExportCsv: document.getElementById('btnExportCsv'),

    // Global Alert Banner
    globalAlertBanner: document.getElementById('globalAlertBanner'),
    alertBannerTitle: document.getElementById('alertBannerTitle'),
    alertBannerDesc: document.getElementById('alertBannerDesc'),
    btnDismissAlert: document.getElementById('btnDismissAlert'),

    // Temperature Card
    tempValue: document.getElementById('tempValue'),
    tempUnitLabel: document.getElementById('tempUnitLabel'),
    tempProgressBar: document.getElementById('tempProgressBar'),
    tempMinVal: document.getElementById('tempMinVal'),
    tempMaxVal: document.getElementById('tempMaxVal'),
    tempStatusBadge: document.getElementById('tempStatusBadge'),
    btnToggleTempUnit: document.getElementById('btnToggleTempUnit'),
    unitC: document.getElementById('unitC'),
    unitF: document.getElementById('unitF'),

    // Rain Card
    rainValue: document.getElementById('rainValue'),
    rainRawAdc: document.getElementById('rainRawAdc'),
    rainProgressBar: document.getElementById('rainProgressBar'),
    rainStatusBadge: document.getElementById('rainStatusBadge'),
    floodRiskBadge: document.getElementById('floodRiskBadge'),

    // Soil Moisture Card
    soilValue: document.getElementById('soilValue'),
    soilRawAdc: document.getElementById('soilRawAdc'),
    soilProgressBar: document.getElementById('soilProgressBar'),
    soilStatusBadge: document.getElementById('soilStatusBadge'),
    soilHealthText: document.getElementById('soilHealthText'),

    // LDR Card
    ldrValue: document.getElementById('ldrValue'),
    ldrRawAdc: document.getElementById('ldrRawAdc'),
    ldrProgressBar: document.getElementById('ldrProgressBar'),
    ldrStatusBadge: document.getElementById('ldrStatusBadge'),
    ldrLuxVal: document.getElementById('ldrLuxVal'),
    ldrIcon: document.getElementById('ldrIcon'),

    // Tilt Card
    cardTilt: document.getElementById('cardTilt'),
    tiltStateBadge: document.getElementById('tiltStateBadge'),
    tiltBubble: document.getElementById('tiltBubble'),
    tiltStatusText: document.getElementById('tiltStatusText'),
    tiltSubtext: document.getElementById('tiltSubtext'),
    tamperAlarmStatus: document.getElementById('tamperAlarmStatus'),
    lastTiltEventTime: document.getElementById('lastTiltEventTime'),

    // Voltage Card
    voltageValue: document.getElementById('voltageValue'),
    batteryFillBar: document.getElementById('batteryFillBar'),
    batteryPctVal: document.getElementById('batteryPctVal'),
    batteryTypeBadge: document.getElementById('batteryTypeBadge'),
    voltageHealthBadge: document.getElementById('voltageHealthBadge'),
    busVoltageRef: document.getElementById('busVoltageRef'),

    // MPU Card
    cardMpu: document.getElementById('cardMpu'),
    mpuStatusBadge: document.getElementById('mpuStatusBadge'),
    mpuPitchVal: document.getElementById('mpuPitchVal'),
    mpuPitchLevel: document.getElementById('mpuPitchLevel'),
    mpuRollVal: document.getElementById('mpuRollVal'),
    mpuRollLevel: document.getElementById('mpuRollLevel'),
    vibrationVal: document.getElementById('vibrationVal'),
    vibrationStatusBadge: document.getElementById('vibrationStatusBadge'),
    vibrationProgressBar: document.getElementById('vibrationProgressBar'),
    gyroMagVal: document.getElementById('gyroMagVal'),
    accelXVal: document.getElementById('accelXVal'),
    accelYVal: document.getElementById('accelYVal'),
    accelZVal: document.getElementById('accelZVal'),
    totalGVal: document.getElementById('totalGVal'),
    motionStatusBadge: document.getElementById('motionStatusBadge'),

    // Camera Card
    cameraImageFeed: document.getElementById('cameraImageFeed'),
    camTimestamp: document.getElementById('camTimestamp'),
    btnRefreshCam: document.getElementById('btnRefreshCam'),
    btnExpandCam: document.getElementById('btnExpandCam'),

    // Table & Logs
    telemetryTableBody: document.getElementById('telemetryTableBody'),
    logCounter: document.getElementById('logCounter'),
    btnClearLogs: document.getElementById('btnClearLogs'),

    // Modals
    modalSimulator: document.getElementById('modalSimulator'),
    btnOpenSimulator: document.getElementById('btnOpenSimulator'),
    btnCloseSimulator: document.getElementById('btnCloseSimulator'),
    modalWiring: document.getElementById('modalWiring'),
    btnOpenWiring: document.getElementById('btnOpenWiring'),
    btnCloseWiring: document.getElementById('btnCloseWiring'),
    modalCameraExpanded: document.getElementById('modalCameraExpanded'),
    expandedCamImage: document.getElementById('expandedCamImage'),
    expandedCamTime: document.getElementById('expandedCamTime'),
    btnCloseExpandedCam: document.getElementById('btnCloseExpandedCam'),

    // Simulator inputs
    simTemp: document.getElementById('simTemp'),
    simTempLabel: document.getElementById('simTempLabel'),
    simRain: document.getElementById('simRain'),
    simRainLabel: document.getElementById('simRainLabel'),
    simSoil: document.getElementById('simSoil'),
    simSoilLabel: document.getElementById('simSoilLabel'),
    simLdr: document.getElementById('simLdr'),
    simLdrLabel: document.getElementById('simLdrLabel'),
    simVolt: document.getElementById('simVolt'),
    simVoltLabel: document.getElementById('simVoltLabel'),
    simTiltToggle: document.getElementById('simTiltToggle'),
    simTiltStateLabel: document.getElementById('simTiltStateLabel'),
    simPitch: document.getElementById('simPitch'),
    simPitchLabel: document.getElementById('simPitchLabel'),
    simRoll: document.getElementById('simRoll'),
    simRollLabel: document.getElementById('simRollLabel'),
    simVib: document.getElementById('simVib'),
    simVibLabel: document.getElementById('simVibLabel'),
    btnSimulatePacket: document.getElementById('btnSimulatePacket'),
    btnToggleAutoSim: document.getElementById('btnToggleAutoSim'),
    autoSimIcon: document.getElementById('autoSimIcon'),
    autoSimText: document.getElementById('autoSimText'),
  };

  // --- CHART.JS CONFIGURATIONS ---
  let chartTempRain = null;
  let chartLightVolt = null;

  function initCharts() {
    const commonChartOptions = {
      responsive: true,
      maintainAspectRatio: false,
      animation: { duration: 350 },
      interaction: { mode: 'index', intersect: false },
      plugins: {
        legend: {
          labels: {
            color: '#94a3b8',
            font: { family: "'Outfit', sans-serif", size: 12 }
          }
        },
        tooltip: {
          backgroundColor: 'rgba(13, 19, 34, 0.9)',
          borderColor: 'rgba(255, 255, 255, 0.1)',
          borderWidth: 1,
          titleFont: { family: "'JetBrains Mono', monospace" },
          bodyFont: { family: "'Outfit', sans-serif" }
        }
      },
      scales: {
        x: {
          grid: { color: 'rgba(255, 255, 255, 0.04)' },
          ticks: { color: '#64748b', font: { family: "'JetBrains Mono', monospace", size: 10 } }
        }
      }
    };

    // Chart 1: Temperature & Rain
    const ctx1 = document.getElementById('chartTempRain').getContext('2d');
    chartTempRain = new Chart(ctx1, {
      type: 'line',
      data: {
        labels: [],
        datasets: [
          {
            label: 'DS18B20 Temp (°C)',
            data: [],
            borderColor: '#00f2fe',
            backgroundColor: 'rgba(0, 242, 254, 0.1)',
            borderWidth: 2,
            tension: 0.35,
            fill: true,
            yAxisID: 'yTemp'
          },
          {
            label: 'Rain Level (%)',
            data: [],
            borderColor: '#3b82f6',
            backgroundColor: 'rgba(59, 130, 246, 0.1)',
            borderWidth: 2,
            tension: 0.35,
            fill: true,
            yAxisID: 'yRain'
          },
          {
            label: 'Soil Moisture (%)',
            data: [],
            borderColor: '#10b981',
            backgroundColor: 'rgba(16, 185, 129, 0.1)',
            borderWidth: 2,
            tension: 0.35,
            fill: false,
            yAxisID: 'yRain'
          }
        ]
      },
      options: {
        ...commonChartOptions,
        scales: {
          ...commonChartOptions.scales,
          yTemp: {
            type: 'linear',
            position: 'left',
            title: { display: true, text: 'Temperature (°C)', color: '#00f2fe' },
            ticks: { color: '#00f2fe' },
            grid: { color: 'rgba(255, 255, 255, 0.05)' }
          },
          yRain: {
            type: 'linear',
            position: 'right',
            min: 0,
            max: 100,
            title: { display: true, text: 'Rain Level (%)', color: '#60a5fa' },
            ticks: { color: '#60a5fa' },
            grid: { drawOnChartArea: false }
          }
        }
      }
    });

    // Chart 2: LDR Light & Voltage
    const ctx2 = document.getElementById('chartLightVolt').getContext('2d');
    chartLightVolt = new Chart(ctx2, {
      type: 'line',
      data: {
        labels: [],
        datasets: [
          {
            label: 'Light Level (%)',
            data: [],
            borderColor: '#f59e0b',
            backgroundColor: 'rgba(245, 158, 11, 0.1)',
            borderWidth: 2,
            tension: 0.35,
            fill: true,
            yAxisID: 'yLdr'
          },
          {
            label: 'DC Voltage (V)',
            data: [],
            borderColor: '#a855f7',
            backgroundColor: 'rgba(168, 85, 247, 0.1)',
            borderWidth: 2,
            tension: 0.35,
            fill: true,
            yAxisID: 'yVolt'
          }
        ]
      },
      options: {
        ...commonChartOptions,
        scales: {
          ...commonChartOptions.scales,
          yLdr: {
            type: 'linear',
            position: 'left',
            min: 0,
            max: 100,
            title: { display: true, text: 'Ambient Light (%)', color: '#fbbf24' },
            ticks: { color: '#fbbf24' },
            grid: { color: 'rgba(255, 255, 255, 0.05)' }
          },
          yVolt: {
            type: 'linear',
            position: 'right',
            min: 0,
            title: { display: true, text: 'DC Voltage (V)', color: '#c084fc' },
            ticks: { color: '#c084fc' },
            grid: { drawOnChartArea: false }
          }
        }
      }
    });
  }

  // --- AUDIO SYNTHESIZER FOR SENSOR ALARMS ---
  function playAlertBeep(frequency = 880, duration = 0.25, type = 'sine') {
    if (!soundEnabled) return;
    try {
      if (!audioCtx) {
        audioCtx = new (window.AudioContext || window.webkitAudioContext)();
      }
      if (audioCtx.state === 'suspended') {
        audioCtx.resume();
      }
      const osc = audioCtx.createOscillator();
      const gain = audioCtx.createGain();
      osc.type = type;
      osc.frequency.setValueAtTime(frequency, audioCtx.currentTime);
      gain.gain.setValueAtTime(0.15, audioCtx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + duration);
      osc.connect(gain);
      gain.connect(audioCtx.destination);
      osc.start();
      osc.stop(audioCtx.currentTime + duration);
    } catch (e) {
      console.warn('Audio autoplay blocked or unsupported:', e);
    }
  }

  // --- UPDATE UI WITH INCOMING TELEMETRY ---
  function updateDashboard(data) {
    packetCounter++;
    el.packetCountVal.textContent = `${packetCounter} pkts`;

    // 1. Connection Pill
    el.deviceStatusPill.className = 'status-pill status-online';
    el.deviceStatusText.textContent = data.deviceId || 'ESP32 Online';

    // WiFi RSSI
    if (data.rssi !== undefined) {
      el.wifiRssiVal.textContent = `${data.rssi} dBm`;
    }

    // 2. Temperature (DS18B20)
    const tempC = Number(data.temperature) || 0;
    const tempF = Number(data.temperatureF) || (tempC * 9 / 5 + 32);

    if (tempC < minTemp) minTemp = tempC;
    if (tempC > maxTemp) maxTemp = tempC;

    renderTemperature(tempC, tempF);

    // 3. Rain / Water Level
    const rain = Math.max(0, Math.min(100, Number(data.rainLevel) || 0));
    el.rainValue.textContent = rain.toFixed(1);
    el.rainProgressBar.style.width = `${rain}%`;
    el.rainRawAdc.textContent = `ADC: ${data.rainRaw !== undefined ? data.rainRaw : '----'}`;
    el.rainStatusBadge.textContent = data.rainStatus || (rain > 70 ? 'Heavy Flood' : rain > 20 ? 'Rain Drops' : 'Dry Surface');

    if (rain > 75) {
      el.floodRiskBadge.textContent = 'HIGH RISK';
      el.floodRiskBadge.className = 'status-badge badge-red';
      triggerAlarmBanner('HEAVY RAIN / FLOOD WARNING', `Water level reaches ${rain.toFixed(1)}%! Check drainage.`);
      playAlertBeep(620, 0.35, 'triangle');
    } else {
      el.floodRiskBadge.textContent = rain > 35 ? 'Moderate' : 'Minimal';
      el.floodRiskBadge.className = 'status-badge badge-gray';
    }

    // 3.5 Soil Moisture (0 to 4095 12-Bit ADC Scale)
    const soil = Math.max(0, Math.min(100, Number(data.soilMoisture) || 0));
    const soilRaw = data.soilRaw !== undefined ? Number(data.soilRaw) : Math.round(3200 - (soil / 100) * (3200 - 1400));
    if (el.soilValue) el.soilValue.textContent = soil.toFixed(1);
    if (el.soilProgressBar) el.soilProgressBar.style.width = `${soil}%`;
    if (el.soilRawAdc) el.soilRawAdc.textContent = `ADC: ${soilRaw} / 4095`;
    if (el.soilStatusBadge) {
      el.soilStatusBadge.textContent = data.soilStatus || (soil < 20 ? 'Very Dry' : soil > 80 ? 'Waterlogged' : 'Optimal Moisture');
      if (soil < 20) {
        el.soilStatusBadge.className = 'status-badge badge-red';
      } else if (soil < 35) {
        el.soilStatusBadge.className = 'status-badge badge-amber';
      } else if (soil > 85) {
        el.soilStatusBadge.className = 'status-badge badge-blue';
      } else {
        el.soilStatusBadge.className = 'status-badge badge-green';
      }
    }
    if (el.soilHealthText) {
      el.soilHealthText.textContent = data.soilHealth || (soil < 25 ? 'Water Required: Soil is dry' : soil > 85 ? 'Over-saturated: Stop watering' : 'Balanced Hydration: Optimal condition');
    }

    // 4. LDR Ambient Light
    const ldr = Math.max(0, Math.min(100, Number(data.ldrValue) || 0));
    el.ldrValue.textContent = ldr.toFixed(1);
    el.ldrProgressBar.style.width = `${ldr}%`;
    el.ldrRawAdc.textContent = `ADC: ${data.ldrRaw !== undefined ? data.ldrRaw : '----'}`;
    el.ldrStatusBadge.textContent = data.lightStatus || (ldr > 60 ? 'Daylight' : 'Dim');
    const estLux = Math.round(ldr * 8.5);
    el.ldrLuxVal.textContent = `~ ${estLux} lx`;

    if (ldr > 40) {
      el.ldrIcon.className = 'fa-solid fa-sun';
      el.ldrProgressBar.style.background = 'linear-gradient(90deg, #f59e0b, #fbbf24)';
    } else {
      el.ldrIcon.className = 'fa-solid fa-moon';
      el.ldrProgressBar.style.background = 'linear-gradient(90deg, #6366f1, #818cf8)';
    }

    // 5. Tilt & Incline Sensor
    const isTilted = Boolean(data.tiltState === 1 || data.tiltState === true || data.tiltAlert);
    if (isTilted) {
      el.cardTilt.classList.add('alert-tilt');
      el.tiltBubble.parentElement.parentElement.classList.add('tilt-tilted');
      el.tiltStateBadge.textContent = 'TILT ALARM';
      el.tiltStateBadge.className = 'status-badge badge-red';
      el.tiltStatusText.textContent = 'TILT DETECTED!';
      el.tiltStatusText.style.color = '#f87171';
      el.tiltSubtext.textContent = 'Tamper detected: unit off-balance';
      el.tamperAlarmStatus.textContent = 'TRIGGERED';
      el.tamperAlarmStatus.style.color = '#f87171';
      el.lastTiltEventTime.textContent = data.timeFormatted || new Date().toLocaleTimeString();

      triggerAlarmBanner('TAMPER ALERT: UNIT TILTED', 'Tilt switch triggered! Physical device inclination changed.');
      playAlertBeep(980, 0.4, 'sawtooth');
    } else {
      el.cardTilt.classList.remove('alert-tilt');
      el.tiltBubble.parentElement.parentElement.classList.remove('tilt-tilted');
      el.tiltStateBadge.textContent = 'Normal';
      el.tiltStateBadge.className = 'status-badge badge-green';
      el.tiltStatusText.textContent = 'LEVEL (NORMAL)';
      el.tiltStatusText.style.color = 'var(--text-main)';
      el.tiltSubtext.textContent = 'Unit upright & balanced';
      el.tamperAlarmStatus.textContent = 'ARMED';
      el.tamperAlarmStatus.style.color = 'var(--text-secondary)';
    }

    // 5.5 MPU-6050 6-Axis Motion & Vibration
    const pitch = Number(data.pitch) || 0;
    const roll = Number(data.roll) || 0;
    const ax = Number(data.accelX) || 0;
    const ay = Number(data.accelY) || 0;
    const az = data.accelZ !== undefined ? Number(data.accelZ) : 1.0;
    const totalG = Number(data.totalG) || Math.sqrt(ax * ax + ay * ay + az * az);
    const mpuConnected = Boolean(data.mpuConnected);
    const vib = data.vibration !== undefined ? Number(data.vibration) : Math.abs(totalG - 1.0);
    const vibPct = data.vibrationPct !== undefined ? Number(data.vibrationPct) : Math.min(100, (vib / 1.5) * 100);
    const gyroMag = data.gyroMag !== undefined ? Number(data.gyroMag) : 0.0;

    if (el.mpuPitchVal) el.mpuPitchVal.textContent = `${pitch.toFixed(1)}°`;
    if (el.mpuRollVal) el.mpuRollVal.textContent = `${roll.toFixed(1)}°`;
    if (el.accelXVal) el.accelXVal.textContent = `${ax.toFixed(2)} g`;
    if (el.accelYVal) el.accelYVal.textContent = `${ay.toFixed(2)} g`;
    if (el.accelZVal) el.accelZVal.textContent = `${az.toFixed(2)} g`;
    if (el.totalGVal) el.totalGVal.textContent = `${totalG.toFixed(2)} g`;

    // Render Vibration Metrics
    if (el.vibrationVal) el.vibrationVal.textContent = `${vib.toFixed(2)} g`;
    if (el.vibrationProgressBar) el.vibrationProgressBar.style.width = `${Math.max(2, Math.min(100, vibPct))}%`;
    if (el.gyroMagVal) el.gyroMagVal.textContent = `${gyroMag.toFixed(1)} °/s`;
    if (el.vibrationStatusBadge) {
      el.vibrationStatusBadge.textContent = data.vibrationStatus || (vib >= 0.60 ? 'Severe Shock' : vib >= 0.25 ? 'Moderate Vib' : 'Smooth / Stable');
      if (vib >= 0.60) {
        el.vibrationStatusBadge.className = 'status-badge badge-red';
        triggerAlarmBanner('EXCESSIVE VIBRATION / SHOCK ALERT', `Vibration reached ${vib.toFixed(2)}g! Mechanical disturbance detected.`);
        playAlertBeep(1100, 0.4, 'sawtooth');
      } else if (vib >= 0.25) {
        el.vibrationStatusBadge.className = 'status-badge badge-amber';
      } else {
        el.vibrationStatusBadge.className = 'status-badge badge-green';
      }
    }

    // Animate Horizon levels
    if (el.mpuPitchLevel) {
      const pitchPct = Math.max(-50, Math.min(50, (pitch / 90) * 50));
      if (pitchPct >= 0) {
        el.mpuPitchLevel.style.left = '50%';
        el.mpuPitchLevel.style.width = `${pitchPct}%`;
      } else {
        el.mpuPitchLevel.style.left = `${50 + pitchPct}%`;
        el.mpuPitchLevel.style.width = `${Math.abs(pitchPct)}%`;
      }
    }
    if (el.mpuRollLevel) {
      const rollPct = Math.max(-50, Math.min(50, (roll / 180) * 50));
      if (rollPct >= 0) {
        el.mpuRollLevel.style.left = '50%';
        el.mpuRollLevel.style.width = `${rollPct}%`;
      } else {
        el.mpuRollLevel.style.left = `${50 + rollPct}%`;
        el.mpuRollLevel.style.width = `${Math.abs(rollPct)}%`;
      }
    }

    // Dynamic tilt bubble offset from MPU
    if (mpuConnected && el.tiltBubble) {
      const bubbleX = Math.max(-20, Math.min(20, (roll / 45) * 20));
      const bubbleY = Math.max(-20, Math.min(20, (-pitch / 45) * 20));
      el.tiltBubble.style.transform = `translate(${bubbleX}px, ${bubbleY}px)`;
    }

    if (el.mpuStatusBadge) {
      if (mpuConnected) {
        el.mpuStatusBadge.textContent = 'MPU Active';
        el.mpuStatusBadge.className = 'status-badge badge-green';
      } else {
        el.mpuStatusBadge.textContent = 'Standby';
        el.mpuStatusBadge.className = 'status-badge badge-gray';
      }
    }

    if (el.motionStatusBadge) {
      el.motionStatusBadge.textContent = data.motionStatus || (Math.abs(pitch) > 25 || Math.abs(roll) > 25 ? 'Incline Warning' : 'Level Horizon');
      el.motionStatusBadge.className = (Math.abs(pitch) > 25 || Math.abs(roll) > 25 || vib >= 0.60) ? 'status-badge badge-red' : 'status-badge badge-cyan';
    }

    // 6. Voltage & Battery Health
    const volts = Number(data.voltage) || 0;
    el.voltageValue.textContent = volts.toFixed(2);
    const battPct = Math.max(0, Math.min(100, data.batteryPercent !== undefined ? data.batteryPercent : 85));
    el.batteryFillBar.style.width = `${battPct}%`;
    el.batteryPctVal.textContent = `${battPct}%`;
    el.batteryTypeBadge.textContent = data.systemType || (volts > 8.0 ? '12V Battery' : '5V Rail');

    if (battPct > 40) {
      el.batteryFillBar.style.background = 'linear-gradient(90deg, #10b981, #34d399)';
      el.voltageHealthBadge.textContent = 'Nominal';
      el.voltageHealthBadge.className = 'status-badge badge-emerald';
    } else if (battPct > 15) {
      el.batteryFillBar.style.background = 'linear-gradient(90deg, #f59e0b, #fbbf24)';
      el.voltageHealthBadge.textContent = 'Low Charge';
      el.voltageHealthBadge.className = 'status-badge badge-amber';
    } else {
      el.batteryFillBar.style.background = 'linear-gradient(90deg, #ef4444, #f87171)';
      el.voltageHealthBadge.textContent = 'Critical Low';
      el.voltageHealthBadge.className = 'status-badge badge-red';
    }

    // 7. Update Charts
    const timeLabel = data.timeFormatted || new Date().toLocaleTimeString([], { hour12: false });
    appendChartData(timeLabel, tempC, rain, soil, ldr, volts);

    // 8. Update Table
    prependTableRow(data);
    historicalRecords.push(data);
    el.logCounter.textContent = `${historicalRecords.length} entries recorded`;
  }

  function renderTemperature(tempC, tempF) {
    if (isCelsius) {
      el.tempValue.textContent = tempC.toFixed(1);
      el.tempUnitLabel.textContent = '°C';
      el.tempMinVal.textContent = `${minTemp.toFixed(1)} °C`;
      el.tempMaxVal.textContent = `${maxTemp.toFixed(1)} °C`;
      el.unitC.className = 'unit-active';
      el.unitF.className = '';
    } else {
      el.tempValue.textContent = tempF.toFixed(1);
      el.tempUnitLabel.textContent = '°F';
      el.tempMinVal.textContent = `${(minTemp * 9 / 5 + 32).toFixed(1)} °F`;
      el.tempMaxVal.textContent = `${(maxTemp * 9 / 5 + 32).toFixed(1)} °F`;
      el.unitC.className = '';
      el.unitF.className = 'unit-active';
    }

    // Gauge bar (0 to 50°C scale)
    const pct = Math.max(0, Math.min(100, (tempC / 50.0) * 100));
    el.tempProgressBar.style.width = `${pct}%`;

    if (tempC < 18) {
      el.tempStatusBadge.textContent = 'Cool';
      el.tempStatusBadge.className = 'status-badge badge-blue';
    } else if (tempC <= 32) {
      el.tempStatusBadge.textContent = 'Optimal';
      el.tempStatusBadge.className = 'status-badge badge-cyan';
    } else if (tempC <= 42) {
      el.tempStatusBadge.textContent = 'Warm';
      el.tempStatusBadge.className = 'status-badge badge-amber';
    } else {
      el.tempStatusBadge.textContent = 'High Temp';
      el.tempStatusBadge.className = 'status-badge badge-red';
    }
  }

  function appendChartData(timeLabel, temp, rain, soil, ldr, volt) {
    if (!chartTempRain || !chartLightVolt) return;

    // Chart 1 (Temp, Rain, Soil)
    chartTempRain.data.labels.push(timeLabel);
    chartTempRain.data.datasets[0].data.push(temp);
    chartTempRain.data.datasets[1].data.push(rain);
    if (chartTempRain.data.datasets[2]) {
      chartTempRain.data.datasets[2].data.push(soil);
    }

    if (chartTempRain.data.labels.length > maxChartPoints) {
      chartTempRain.data.labels.shift();
      chartTempRain.data.datasets[0].data.shift();
      chartTempRain.data.datasets[1].data.shift();
      if (chartTempRain.data.datasets[2]) {
        chartTempRain.data.datasets[2].data.shift();
      }
    }
    chartTempRain.update('none');

    // Chart 2 (Light, Voltage)
    chartLightVolt.data.labels.push(timeLabel);
    chartLightVolt.data.datasets[0].data.push(ldr);
    chartLightVolt.data.datasets[1].data.push(volt);

    if (chartLightVolt.data.labels.length > maxChartPoints) {
      chartLightVolt.data.labels.shift();
      chartLightVolt.data.datasets[0].data.shift();
      chartLightVolt.data.datasets[1].data.shift();
    }
    chartLightVolt.update('none');
  }

  function prependTableRow(data) {
    // Remove placeholder empty row if present
    const emptyRow = el.telemetryTableBody.querySelector('.empty-row');
    if (emptyRow) emptyRow.remove();

    const tr = document.createElement('tr');
    const tiltBadge = (data.tiltState === 1 || data.tiltAlert)
      ? `<span class="status-badge badge-red">TILTED</span>`
      : `<span class="status-badge badge-green">LEVEL</span>`;

    const soilPct = Number(data.soilMoisture !== undefined ? data.soilMoisture : 0).toFixed(1);
    const soilRaw = data.soilRaw !== undefined ? data.soilRaw : Math.round(3200 - (Number(data.soilMoisture || 0) / 100) * (3200 - 1400));
    const soilCell = `<span class="font-mono" style="color:#34d399;" title="Raw ADC: ${soilRaw} / 4095 levels (12-bit ADC)">${soilPct}% <small style="color:#94a3b8;">(${soilRaw})</small></span>`;

    const vibVal = data.vibration !== undefined ? Number(data.vibration).toFixed(2) : '0.00';
    const mpuOrientation = (data.mpuConnected || data.pitch !== undefined)
      ? `<span class="font-mono" style="color:#f472b6;">P:${Number(data.pitch || 0).toFixed(1)}° R:${Number(data.roll || 0).toFixed(1)}° <span style="color:#38bdf8;" title="Dynamic vibration magnitude">~${vibVal}g</span></span>`
      : `<span style="color:#64748b;">Standby</span>`;

    tr.innerHTML = `
      <td>${data.timeFormatted || new Date().toLocaleTimeString()}</td>
      <td>${data.deviceId || 'esp32'}</td>
      <td>${Number(data.temperature).toFixed(1)} °C</td>
      <td>${Number(data.rainLevel).toFixed(1)}%</td>
      <td>${soilCell}</td>
      <td>${Number(data.ldrValue).toFixed(1)}%</td>
      <td>${tiltBadge}</td>
      <td>${mpuOrientation}</td>
      <td>${Number(data.voltage).toFixed(2)} V</td>
      <td>${data.batteryPercent || 80}%</td>
      <td>${data.rssi || -60} dBm</td>
    `;

    el.telemetryTableBody.insertBefore(tr, el.telemetryTableBody.firstChild);

    // Limit in-memory table to 100 rows for smooth DOM performance
    while (el.telemetryTableBody.children.length > 100) {
      el.telemetryTableBody.removeChild(el.telemetryTableBody.lastChild);
    }
  }

  function triggerAlarmBanner(title, desc) {
    el.alertBannerTitle.textContent = title;
    el.alertBannerDesc.textContent = desc;
    el.globalAlertBanner.classList.remove('hidden');
  }

  // --- CAMERA SNAPSHOT UPDATE ---
  function updateCameraFeed(camData) {
    const timestamp = Date.now();
    el.cameraImageFeed.src = `/api/camera/latest?t=${timestamp}`;
    el.expandedCamImage.src = `/api/camera/latest?t=${timestamp}`;
    const timeStr = camData && camData.timeFormatted ? camData.timeFormatted : new Date().toLocaleTimeString();
    el.camTimestamp.textContent = timeStr;
    el.expandedCamTime.textContent = `Timestamp: ${timeStr}`;
  }

  // --- SERVER-SENT EVENTS (SSE) STREAMING ---
  function initEventSource() {
    const sse = new EventSource('/api/events');

    sse.addEventListener('telemetry', (e) => {
      try {
        const payload = JSON.parse(e.data);
        updateDashboard(payload);
      } catch (err) {
        console.error('Failed to parse telemetry event:', err);
      }
    });

    sse.addEventListener('camera_frame', (e) => {
      try {
        const camData = JSON.parse(e.data);
        updateCameraFeed(camData);
      } catch (err) {
        console.error('Failed to parse camera frame event:', err);
      }
    });

    sse.onopen = () => {
      console.log('[SSE] Connected to real-time event pipeline');
      if (packetCounter === 0) {
        el.deviceStatusPill.className = 'status-pill status-standby';
        el.deviceStatusText.textContent = 'Connected (Awaiting Pkts)';
      }
    };

    sse.onerror = (err) => {
      console.warn('[SSE] Connection interrupted, auto-reconnecting...', err);
      el.deviceStatusPill.className = 'status-pill status-offline';
      el.deviceStatusText.textContent = 'Reconnecting...';
    };
  }

  // --- INITIAL DATA FETCH ---
  async function loadInitialData() {
    try {
      const res = await fetch('/api/telemetry/history?limit=30');
      const json = await res.json();
      if (json.status === 'success' && Array.isArray(json.data) && json.data.length > 0) {
        json.data.forEach(item => updateDashboard(item));
      } else {
        // Fetch latest default reading
        const latestRes = await fetch('/api/telemetry/latest');
        const latestJson = await latestRes.json();
        if (latestJson.data) {
          updateDashboard(latestJson.data);
        }
      }
    } catch (e) {
      console.warn('Initial telemetry fetch failed:', e);
    }
  }

  // --- HARDWARE SIMULATOR HANDLERS ---
  function setupSimulator() {
    // Slider label live updating
    el.simTemp.addEventListener('input', () => {
      el.simTempLabel.textContent = `${el.simTemp.value} °C`;
    });
    el.simRain.addEventListener('input', () => {
      el.simRainLabel.textContent = `${el.simRain.value} %`;
    });
    if (el.simSoil) {
      el.simSoil.addEventListener('input', () => {
        const sVal = parseFloat(el.simSoil.value);
        const sRaw = Math.round(3200 - (sVal / 100.0) * (3200 - 1400));
        el.simSoilLabel.textContent = `${sVal.toFixed(1)} % (ADC: ${sRaw})`;
      });
    }
    el.simLdr.addEventListener('input', () => {
      el.simLdrLabel.textContent = `${el.simLdr.value} %`;
    });
    el.simVolt.addEventListener('input', () => {
      el.simVoltLabel.textContent = `${el.simVolt.value} V`;
    });
    if (el.simPitch) {
      el.simPitch.addEventListener('input', () => {
        el.simPitchLabel.textContent = `${el.simPitch.value} °`;
      });
    }
    if (el.simRoll) {
      el.simRoll.addEventListener('input', () => {
        el.simRollLabel.textContent = `${el.simRoll.value} °`;
      });
    }
    if (el.simVib) {
      el.simVib.addEventListener('input', () => {
        const vVal = parseFloat(el.simVib.value);
        const vStatus = vVal >= 0.60 ? 'Shock Alert' : vVal >= 0.25 ? 'Moderate' : 'Smooth';
        el.simVibLabel.textContent = `${vVal.toFixed(2)} g (${vStatus})`;
      });
    }
    el.simTiltToggle.addEventListener('change', () => {
      el.simTiltStateLabel.textContent = el.simTiltToggle.checked ? '1 (TILTED)' : '0 (Level)';
    });

    // Transmit one packet
    el.btnSimulatePacket.addEventListener('click', async () => {
      await sendSimulatedPacket();
    });

    // Toggle Auto-streaming
    el.btnToggleAutoSim.addEventListener('click', () => {
      if (autoSimInterval) {
        clearInterval(autoSimInterval);
        autoSimInterval = null;
        el.autoSimIcon.className = 'fa-solid fa-play';
        el.autoSimText.textContent = 'Auto-Stream (Every 3s)';
      } else {
        sendSimulatedPacket();
        autoSimInterval = setInterval(() => {
          // Add organic random walk to sliders
          const currentT = parseFloat(el.simTemp.value);
          el.simTemp.value = (currentT + (Math.random() - 0.5)).toFixed(1);
          el.simTempLabel.textContent = `${el.simTemp.value} °C`;

          const currentR = parseFloat(el.simRain.value);
          el.simRain.value = Math.max(0, Math.min(100, Math.round(currentR + (Math.random() * 6 - 3))));
          el.simRainLabel.textContent = `${el.simRain.value} %`;

          if (el.simSoil) {
            const currentS = parseFloat(el.simSoil.value);
            el.simSoil.value = Math.max(0, Math.min(100, Math.round(currentS + (Math.random() * 4 - 2))));
            const sRaw = Math.round(3200 - (parseFloat(el.simSoil.value) / 100.0) * (3200 - 1400));
            el.simSoilLabel.textContent = `${parseFloat(el.simSoil.value).toFixed(1)} % (ADC: ${sRaw})`;
          }

          if (el.simPitch) {
            const currentP = parseFloat(el.simPitch.value);
            el.simPitch.value = Math.max(-90, Math.min(90, Math.round(currentP + (Math.random() * 4 - 2))));
            el.simPitchLabel.textContent = `${el.simPitch.value} °`;
          }
          if (el.simRoll) {
            const currentRo = parseFloat(el.simRoll.value);
            el.simRoll.value = Math.max(-180, Math.min(180, Math.round(currentRo + (Math.random() * 6 - 3))));
            el.simRollLabel.textContent = `${el.simRoll.value} °`;
          }
          if (el.simVib) {
            const currentV = Math.random() < 0.2 ? (0.3 + Math.random() * 0.5) : (0.02 + Math.random() * 0.05);
            el.simVib.value = currentV.toFixed(2);
            const vStatus = currentV >= 0.60 ? 'Shock Alert' : currentV >= 0.25 ? 'Moderate' : 'Smooth';
            el.simVibLabel.textContent = `${currentV.toFixed(2)} g (${vStatus})`;
          }

          sendSimulatedPacket();
        }, 3000);
        el.autoSimIcon.className = 'fa-solid fa-pause';
        el.autoSimText.textContent = 'Pause Auto-Stream';
      }
    });
  }

  async function sendSimulatedPacket() {
    const pVal = el.simPitch ? parseFloat(el.simPitch.value) : 0;
    const rVal = el.simRoll ? parseFloat(el.simRoll.value) : 0;
    const sVal = el.simSoil ? parseFloat(el.simSoil.value) : 50.0;
    const sRaw = Math.round(3200 - (sVal / 100.0) * (3200 - 1400));
    const vVal = el.simVib ? parseFloat(el.simVib.value) : 0.02;

    const payload = {
      deviceId: 'esp32-simulated',
      temperature: parseFloat(el.simTemp.value),
      rainLevel: parseFloat(el.simRain.value),
      rainRaw: Math.round(parseFloat(el.simRain.value) * 28.0),
      soilMoisture: sVal,
      soilRaw: sRaw,
      soilMaxRaw: 4095,
      ldrValue: parseFloat(el.simLdr.value),
      ldrRaw: Math.round(parseFloat(el.simLdr.value) * 38.0),
      tiltState: el.simTiltToggle.checked ? 1 : 0,
      voltage: parseFloat(el.simVolt.value),
      rssi: -55 - Math.floor(Math.random() * 10),
      mpuConnected: true,
      pitch: pVal,
      roll: rVal,
      vibration: vVal,
      vibrationPct: Math.min(100, Math.round((vVal / 1.5) * 100)),
      accelX: parseFloat((-Math.sin((pVal * Math.PI) / 180)).toFixed(2)),
      accelY: parseFloat((Math.sin((rVal * Math.PI) / 180)).toFixed(2)),
      accelZ: parseFloat((Math.cos((pVal * Math.PI) / 180)).toFixed(2)),
      gyroX: parseFloat((Math.random() * 2 - 1).toFixed(1)),
      gyroY: parseFloat((Math.random() * 2 - 1).toFixed(1)),
      gyroZ: parseFloat((Math.random() * 2 - 1).toFixed(1)),
      gyroMag: parseFloat((Math.random() * 4 + 1).toFixed(1))
    };

    try {
      const res = await fetch('/api/telemetry', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      });
      if (res.ok) {
        // Visual feedback on button
        el.btnSimulatePacket.style.transform = 'scale(0.96)';
        setTimeout(() => { el.btnSimulatePacket.style.transform = ''; }, 150);
      }
    } catch (e) {
      console.error('Failed to post simulated telemetry:', e);
    }
  }

  // --- CSV EXPORT ---
  function exportCsv() {
    if (!historicalRecords.length) {
      alert('No telemetry records available to export.');
      return;
    }

    const headers = ['Timestamp', 'Device ID', 'Temperature (C)', 'Rain Level (%)', 'Soil Moisture (%)', 'Soil Raw (0-4095)', 'LDR Level (%)', 'Tilt State', 'Pitch (deg)', 'Roll (deg)', 'Vibration (g)', 'Total G', 'Voltage (V)', 'Battery (%)', 'RSSI (dBm)'];
    const rows = historicalRecords.map(r => [
      r.timestamp || new Date().toISOString(),
      r.deviceId || 'esp32-node-01',
      r.temperature,
      r.rainLevel,
      r.soilMoisture !== undefined ? r.soilMoisture : '',
      r.soilRaw !== undefined ? r.soilRaw : '',
      r.ldrValue,
      r.tiltState,
      r.pitch !== undefined ? r.pitch : 0.0,
      r.roll !== undefined ? r.roll : 0.0,
      r.vibration !== undefined ? r.vibration : 0.0,
      r.totalG !== undefined ? r.totalG : 1.0,
      r.voltage,
      r.batteryPercent,
      r.rssi
    ]);

    let csvContent = 'data:text/csv;charset=utf-8,' + [headers.join(','), ...rows.map(e => e.join(','))].join('\n');
    const encodedUri = encodeURI(csvContent);
    const link = document.createElement('a');
    link.setAttribute('href', encodedUri);
    link.setAttribute('download', `astra26_telemetry_${Date.now()}.csv`);
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
  }

  // --- UI INTERACTION HANDLERS ---
  function setupUIHandlers() {
    // Temp unit toggle
    el.btnToggleTempUnit.addEventListener('click', () => {
      isCelsius = !isCelsius;
      const currentTempC = parseFloat(el.tempValue.textContent) || 25.0;
      renderTemperature(currentTempC, currentTempC * 9 / 5 + 32);
    });

    // Sound toggle
    el.btnToggleSound.addEventListener('click', () => {
      soundEnabled = !soundEnabled;
      if (soundEnabled) {
        el.soundIcon.className = 'fa-solid fa-volume-high';
        playAlertBeep(880, 0.15);
      } else {
        el.soundIcon.className = 'fa-solid fa-volume-xmark';
      }
    });

    // Export CSV
    el.btnExportCsv.addEventListener('click', exportCsv);

    // Dismiss Alert Banner
    el.btnDismissAlert.addEventListener('click', () => {
      el.globalAlertBanner.classList.add('hidden');
    });

    // Clear Logs
    el.btnClearLogs.addEventListener('click', async () => {
      historicalRecords = [];
      el.telemetryTableBody.innerHTML = `
        <tr class="empty-row">
          <td colspan="10"><i class="fa-solid fa-check"></i> Logs cleared. Waiting for next telemetry packet...</td>
        </tr>
      `;
      el.logCounter.textContent = '0 entries recorded';
      await fetch('/api/telemetry/clear', { method: 'POST' }).catch(() => {});
    });

    // Refresh Camera Feed
    el.btnRefreshCam.addEventListener('click', () => {
      updateCameraFeed();
    });

    // Modals open / close
    el.btnOpenSimulator.addEventListener('click', () => el.modalSimulator.classList.remove('hidden'));
    el.btnCloseSimulator.addEventListener('click', () => el.modalSimulator.classList.add('hidden'));

    el.btnOpenWiring.addEventListener('click', () => el.modalWiring.classList.remove('hidden'));
    el.btnCloseWiring.addEventListener('click', () => el.modalWiring.classList.add('hidden'));

    el.btnExpandCam.addEventListener('click', () => el.modalCameraExpanded.classList.remove('hidden'));
    el.btnCloseExpandedCam.addEventListener('click', () => el.modalCameraExpanded.classList.add('hidden'));

    // Close on outside click
    [el.modalSimulator, el.modalWiring, el.modalCameraExpanded].forEach(modal => {
      modal.addEventListener('click', (e) => {
        if (e.target === modal) modal.classList.add('hidden');
      });
    });

    // Chart range filters
    document.querySelectorAll('.chart-filter-btn').forEach(btn => {
      btn.addEventListener('click', (e) => {
        document.querySelectorAll('.chart-filter-btn').forEach(b => b.classList.remove('active'));
        e.target.classList.add('active');
        maxChartPoints = parseInt(e.target.dataset.range, 10);
      });
    });
  }

  // --- BOOTSTRAP ---
  document.addEventListener('DOMContentLoaded', () => {
    initCharts();
    setupSimulator();
    setupUIHandlers();
    loadInitialData();
    initEventSource();
  });

})();
