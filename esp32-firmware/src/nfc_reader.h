// =============================================================================
// FabLab Presence System — NFC reader header (MFRC522 via SPI)
// =============================================================================

#pragma once
#include <Arduino.h>
#include <functional>

using NfcScanCallback = std::function<void(const String &normalised_uid)>;

// Initialise the MFRC522 over SPI.  Returns true on success.
bool nfc_init();

// Register callback invoked (from main-loop context) when a card is detected.
void nfc_set_callback(NfcScanCallback cb);

// Call from the main loop (or a dedicated FreeRTOS task) to poll for cards.
void nfc_poll();

// Normalise a raw UID / keyboard-wedge string into uppercase hex (no colons).
String nfc_normalise_id(const String &raw);
