# Stage 1: Build the React dashboard
FROM node:20-alpine AS webapp

ARG WEBAPP_REPO
RUN apk add --no-cache git
RUN git clone "$WEBAPP_REPO" /webapp
WORKDIR /webapp
RUN npm install && npm run build
# Output expected at /webapp/dist (standard Vite default)

# Stage 2: Build ESP32 firmware + LittleFS image
FROM python:3.12-slim AS firmware

RUN pip install --no-cache-dir platformio

WORKDIR /project

# Layer-cache friendly: install toolchain before copying source
COPY platformio.ini ./
RUN pio pkg install -e esp32dev

COPY src/ ./src/
COPY data/ ./data/

# Overlay React build output into data/ for LittleFS packaging
COPY --from=webapp /webapp/dist/ ./data/

# Build firmware binary
RUN pio run -e esp32dev

# Build LittleFS image (for first-time USB flash only)
RUN pio run -e esp32dev -t buildfs
