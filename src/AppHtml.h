/**
 * DripDrop - Embedded HTML Template
 * 
 * This file contains the HTML template for the web interface.
 * It's stored in PROGMEM to save RAM on the ESP8266.
 * 
 * To use this, uncomment the include in dripdrop.ino and
 * add a route handler that serves this content.
 */

#ifndef DRIPDROP_APPHTML_H
#define DRIPDROP_APPHTML_H

#include <Arduino.h>

const char APP_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>DripDrop Irrigation</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
        }
        .container {
            max-width: 800px;
            margin: 0 auto;
            background: white;
            border-radius: 12px;
            box-shadow: 0 10px 40px rgba(0,0,0,0.2);
            padding: 30px;
        }
        h1 {
            color: #333;
            margin-bottom: 20px;
            display: flex;
            align-items: center;
            gap: 10px;
        }
        h1::before {
            content: '💧';
        }
        .valve-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
            gap: 15px;
            margin: 20px 0;
        }
        .valve-card {
            background: #f8f9fa;
            border-radius: 8px;
            padding: 20px;
            text-align: center;
            border: 2px solid #e9ecef;
            transition: all 0.3s;
        }
        .valve-card.active {
            border-color: #28a745;
            background: #d4edda;
        }
        .valve-name {
            font-weight: 600;
            color: #333;
            margin-bottom: 10px;
        }
        .valve-status {
            font-size: 0.9em;
            color: #666;
            margin-bottom: 15px;
        }
        .btn {
            padding: 10px 20px;
            border: none;
            border-radius: 6px;
            cursor: pointer;
            font-weight: 500;
            transition: all 0.2s;
        }
        .btn-on {
            background: #28a745;
            color: white;
        }
        .btn-off {
            background: #dc3545;
            color: white;
        }
        .btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 4px 12px rgba(0,0,0,0.15);
        }
        .status-bar {
            background: #f1f3f4;
            padding: 15px;
            border-radius: 8px;
            margin-top: 20px;
            font-size: 0.9em;
            color: #666;
        }
        .settings-section {
            margin-top: 30px;
            border-top: 1px solid #e9ecef;
            padding-top: 20px;
        }
        .settings-section h2 {
            color: #333;
            font-size: 1.1em;
            margin-bottom: 15px;
        }
        .form-group {
            margin-bottom: 12px;
        }
        .form-group label {
            display: block;
            font-size: 0.85em;
            color: #555;
            margin-bottom: 4px;
        }
        .form-group input[type="text"],
        .form-group input[type="number"],
        .form-group input[type="password"] {
            width: 100%;
            padding: 8px 10px;
            border: 1px solid #ccc;
            border-radius: 6px;
            font-size: 0.9em;
        }
        .toggle-row {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-bottom: 12px;
        }
        .toggle-row label {
            font-size: 0.9em;
            color: #333;
            font-weight: 500;
        }
        .switch {
            position: relative;
            width: 44px;
            height: 24px;
        }
        .switch input {
            opacity: 0;
            width: 0;
            height: 0;
        }
        .slider {
            position: absolute;
            cursor: pointer;
            inset: 0;
            background: #ccc;
            border-radius: 24px;
            transition: 0.3s;
        }
        .slider::before {
            content: '';
            position: absolute;
            height: 18px;
            width: 18px;
            left: 3px;
            bottom: 3px;
            background: white;
            border-radius: 50%;
            transition: 0.3s;
        }
        .switch input:checked + .slider {
            background: #28a745;
        }
        .switch input:checked + .slider::before {
            transform: translateX(20px);
        }
        .btn-save {
            background: #667eea;
            color: white;
            margin-top: 8px;
        }
        .mqtt-status {
            font-size: 0.85em;
            color: #666;
            margin-top: 8px;
        }
        .mqtt-status .dot {
            display: inline-block;
            width: 8px;
            height: 8px;
            border-radius: 50%;
            margin-right: 5px;
        }
        .dot-green { background: #28a745; }
        .dot-red { background: #dc3545; }
        .dot-gray { background: #adb5bd; }
    </style>
</head>
<body>
    <div class="container">
        <h1>DripDrop</h1>
        <p>Automated Irrigation System</p>
        
        <div class="valve-grid" id="valves">
            <div class="valve-card">
                <div class="valve-name">Loading...</div>
            </div>
        </div>
        
        <div class="status-bar" id="status">
            Connecting to system...
        </div>

        <div class="settings-section">
            <h2>MQTT Settings</h2>
            <div class="toggle-row">
                <label for="mqttEnabled">Enable MQTT</label>
                <label class="switch">
                    <input type="checkbox" id="mqttEnabled">
                    <span class="slider"></span>
                </label>
            </div>
            <div id="mqttFields">
                <div class="form-group">
                    <label for="mqttServer">Server</label>
                    <input type="text" id="mqttServer" placeholder="e.g. 192.168.1.100">
                </div>
                <div class="form-group">
                    <label for="mqttPort">Port</label>
                    <input type="number" id="mqttPort" value="1883">
                </div>
                <div class="form-group">
                    <label for="mqttUser">Username</label>
                    <input type="text" id="mqttUser" placeholder="(optional)">
                </div>
                <div class="form-group">
                    <label for="mqttPassword">Password</label>
                    <input type="password" id="mqttPassword" placeholder="(optional)">
                </div>
            </div>
            <button class="btn btn-save" onclick="saveMqtt()">Save</button>
            <div class="mqtt-status" id="mqttStatus"></div>
        </div>
    </div>
    
    <script>
        const API_BASE = window.location.origin;
        
        async function fetchValves() {
            try {
                const res = await fetch(`${API_BASE}/valves`);
                const valves = await res.json();
                renderValves(valves);
            } catch (e) {
                document.getElementById('status').textContent = 'Error: ' + e.message;
            }
        }
        
        function renderValves(valves) {
            const container = document.getElementById('valves');
            container.innerHTML = valves.map(v => `
                <div class="valve-card ${v.isOn ? 'active' : ''}">
                    <div class="valve-name">Valve ${v.id}</div>
                    <div class="valve-status">${v.isOn ? 'Running' : 'Off'}</div>
                    <button class="btn ${v.isOn ? 'btn-off' : 'btn-on'}" 
                            onclick="toggleValve(${v.id}, ${!v.isOn})">
                        ${v.isOn ? 'Turn Off' : 'Turn On'}
                    </button>
                </div>
            `).join('');
            
            document.getElementById('status').textContent = 
                `Last updated: ${new Date().toLocaleTimeString()}`;
        }
        
        async function toggleValve(id, turnOn) {
            try {
                await fetch(`${API_BASE}/valve/state/${turnOn ? 'on' : 'off'}`, {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({valveId: id})
                });
                fetchValves();
            } catch (e) {
                alert('Error: ' + e.message);
            }
        }
        
        async function fetchMqtt() {
            try {
                const res = await fetch(`${API_BASE}/system/mqtt`);
                const data = await res.json();
                document.getElementById('mqttEnabled').checked = data.enabled;
                document.getElementById('mqttServer').value = data.server || '';
                document.getElementById('mqttPort').value = data.port || 1883;
                document.getElementById('mqttUser').value = data.user || '';
                updateMqttStatus(data);
            } catch (e) {}
        }

        function updateMqttStatus(data) {
            const el = document.getElementById('mqttStatus');
            if (!data.enabled) {
                el.innerHTML = '<span class="dot dot-gray"></span>Disabled';
            } else if (data.connected) {
                el.innerHTML = '<span class="dot dot-green"></span>Connected';
            } else {
                el.innerHTML = '<span class="dot dot-red"></span>Disconnected';
            }
        }

        async function saveMqtt() {
            try {
                const body = {
                    enabled: document.getElementById('mqttEnabled').checked,
                    server: document.getElementById('mqttServer').value,
                    port: parseInt(document.getElementById('mqttPort').value) || 1883,
                    user: document.getElementById('mqttUser').value,
                    password: document.getElementById('mqttPassword').value
                };
                await fetch(`${API_BASE}/system/mqtt`, {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify(body)
                });
                fetchMqtt();
            } catch (e) {
                alert('Error: ' + e.message);
            }
        }

        // Initial load and refresh every 5 seconds
        fetchValves();
        fetchMqtt();
        setInterval(fetchValves, 5000);
    </script>
</body>
</html>
)=====";

#endif // DRIPDROP_APPHTML_H
