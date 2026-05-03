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
    <title>DripDrop Irrigation</title>
</head>
<body>
    <h1>DripDrop</h1>  
</body>
</html>
)=====";

#endif // DRIPDROP_APPHTML_H
