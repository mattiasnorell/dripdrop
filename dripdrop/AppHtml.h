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
        
        // Initial load and refresh every 5 seconds
        fetchValves();
        setInterval(fetchValves, 5000);
    </script>
</body>
</html>
)=====";

#endif // DRIPDROP_APPHTML_H
