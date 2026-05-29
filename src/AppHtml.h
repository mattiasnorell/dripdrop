/**
 * DripDrop - Embedded HTML Template
 *
 * This file contains the HTML template for the web interface.
 * It's stored in PROGMEM to save RAM on the ESP32.
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
    <title>DripDrop</title>
    <style>
        body { font-family: sans-serif; max-width: 400px; margin: 60px auto; padding: 0 16px; }
        h1 { font-size: 1.5rem; margin-bottom: 4px; }
        p.sub { color: #555; margin: 0 0 24px; }
        label { display: block; margin-bottom: 4px; font-size: 0.9rem; font-weight: 600; }
        input { width: 100%; box-sizing: border-box; padding: 8px 10px; font-size: 1rem; border: 1px solid #ccc; border-radius: 6px; margin-bottom: 14px; }
        button { width: 100%; padding: 10px; font-size: 1rem; background: #2563eb; color: #fff; border: none; border-radius: 6px; cursor: pointer; }
        button:disabled { background: #93c5fd; cursor: default; }
        .msg { margin-top: 14px; padding: 10px 12px; border-radius: 6px; font-size: 0.9rem; }
        .msg.ok  { background: #dcfce7; color: #166534; }
        .msg.err { background: #fee2e2; color: #991b1b; }
    </style>
</head>
<body>
    <h1>DripDrop</h1>
    <p class="sub" id="subtitle"></p>
    <div id="wifi-form" style="display:none">
        <label for="ssid">Wi-Fi Network (SSID)</label>
        <input id="ssid" type="text" placeholder="Network name" autocomplete="off">
        <label for="password">Password</label>
        <input id="password" type="password" placeholder="Leave blank if open network" autocomplete="off">
        <button id="connect-btn" onclick="connectWifi()">Connect</button>
        <div id="msg" class="msg" style="display:none"></div>
    </div>
    <script>
        fetch('/system/status')
            .then(function(r){ return r.json(); })
            .then(function(s){
                if (s.apMode) {
                    document.getElementById('subtitle').textContent = 'Connect to Wi-Fi to get started.';
                    document.getElementById('wifi-form').style.display = '';
                }
            })
            .catch(function(){});

        function connectWifi() {
            var ssid = document.getElementById('ssid').value.trim();
            var password = document.getElementById('password').value;
            var btn = document.getElementById('connect-btn');
            var msg = document.getElementById('msg');

            if (!ssid) { showMsg('Please enter a network name.', false); return; }

            btn.disabled = true;
            btn.textContent = 'Connecting\u2026';
            msg.style.display = 'none';

            fetch('/system/wifi', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ ssid: ssid, password: password })
            })
            .then(function(r){ return r.json(); })
            .then(function(d){
                if (d.message === 'ok') {
                    showMsg('Credentials saved. The device will now connect to ' + ssid + '.', true);
                } else {
                    showMsg(d.error || 'Unexpected response.', false);
                    btn.disabled = false;
                    btn.textContent = 'Connect';
                }
            })
            .catch(function(){
                showMsg('Request failed. Check your connection and try again.', false);
                btn.disabled = false;
                btn.textContent = 'Connect';
            });
        }

        function showMsg(text, ok) {
            var msg = document.getElementById('msg');
            msg.textContent = text;
            msg.className = 'msg ' + (ok ? 'ok' : 'err');
            msg.style.display = '';
        }
    </script>
</body>
</html>
)=====";

#endif // DRIPDROP_APPHTML_H
