// =============================================================================
// FabLab Presence System — Async HTTP server + WebSocket header
// Mirrors the Node.js /api/* endpoints so the same script.js works in-browser.
// =============================================================================

#pragma once
#include <Arduino.h>

// Initialise and start the AsyncWebServer (port 80).
// Call after WiFi is connected and LittleFS is mounted.
void api_server_init();

// Push an NFC-scan event to all connected WebSocket clients.
// Payload: { "type": "nfc", "uid": "<normalised_uid>" }
void api_server_push_nfc(const String &normalised_uid);
