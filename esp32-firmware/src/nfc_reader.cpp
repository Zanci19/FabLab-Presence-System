// =============================================================================
// FabLab Presence System — NFC reader (MFRC522 via SPI)
// =============================================================================

#include "nfc_reader.h"
#include "config.h"

#include <SPI.h>
#include <MFRC522.h>

static MFRC522 s_mfrc;
static NfcScanCallback s_callback;
static String s_last_uid;
static unsigned long s_last_scan_ms = 0;

// ---------------------------------------------------------------------------
bool nfc_init()
{
    SPI.begin(NFC_PIN_SCK, NFC_PIN_MISO, NFC_PIN_MOSI, NFC_PIN_CS);
    s_mfrc.PCD_Init(NFC_PIN_CS, NFC_PIN_RST);
    delay(50);

    byte version = s_mfrc.PCD_ReadRegister(MFRC522::VersionReg);
    if (version == 0x00 || version == 0xFF) {
        Serial.println("[NFC] MFRC522 not detected — check wiring.");
        return false;
    }
    Serial.printf("[NFC] MFRC522 v%02X ready.\n", version);
    return true;
}

void nfc_set_callback(NfcScanCallback cb)
{
    s_callback = cb;
}

// ---------------------------------------------------------------------------
// Convert MFRC522 UID bytes to uppercase hex string
static String uid_to_hex(const MFRC522::Uid &uid)
{
    String hex;
    for (byte i = 0; i < uid.size; i++) {
        if (uid.uidByte[i] < 0x10) hex += '0';
        hex += String(uid.uidByte[i], HEX);
    }
    hex.toUpperCase();
    return hex;
}

// ---------------------------------------------------------------------------
String nfc_normalise_id(const String &raw)
{
    String input = raw;
    input.trim();
    if (input.isEmpty()) return "";

    // Handle grouped hex (04:27:1B:… or 04-27-… etc.)
    bool has_sep = (input.indexOf(':') >= 0 || input.indexOf('-') >= 0);
    if (has_sep) {
        String clean;
        for (char c : input) {
            if (isHexadecimalDigit(c)) {
                clean += (char)toupper(c);
            }
        }
        if ((int)clean.length() >= NFC_MIN_UID_BYTES * 2) return clean;
    }

    // Fallback: keep alphanumeric uppercase
    String clean;
    for (char c : input) {
        if (isAlphaNumeric(c)) clean += (char)toupper(c);
    }
    return clean;
}

// ---------------------------------------------------------------------------
void nfc_poll()
{
    if (!s_mfrc.PICC_IsNewCardPresent()) return;
    if (!s_mfrc.PICC_ReadCardSerial())   return;

    String uid = uid_to_hex(s_mfrc.uid);
    unsigned long now = millis();

    // Debounce: suppress the same card for NFC_RESCAN_DELAY_MS
    if (uid == s_last_uid && (now - s_last_scan_ms) < NFC_RESCAN_DELAY_MS) {
        s_mfrc.PICC_HaltA();
        s_mfrc.PCD_StopCrypto1();
        return;
    }
    s_last_uid     = uid;
    s_last_scan_ms = now;

    Serial.printf("[NFC] Card detected: %s\n", uid.c_str());
    s_mfrc.PICC_HaltA();
    s_mfrc.PCD_StopCrypto1();

    if ((int)uid.length() >= NFC_MIN_UID_BYTES * 2 && s_callback) {
        s_callback(uid);
    }
}
