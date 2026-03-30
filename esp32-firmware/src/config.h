// =============================================================================
// FabLab Presence System — board & project configuration
// Edit WIFI_SSID / WIFI_PASS before flashing.
// =============================================================================

#pragma once

// --- WiFi credentials --------------------------------------------------------
#define WIFI_SSID   "YourNetworkName"
#define WIFI_PASS   "YourNetworkPassword"

// --- Admin -------------------------------------------------------------------
// Default admin password (also stored in /data/settings.json on LittleFS)
#define DEFAULT_ADMIN_PASSWORD  "admin"

// --- NTP ---------------------------------------------------------------------
#define NTP_SERVER      "pool.ntp.org"
#define NTP_UTC_OFFSET  3600    // UTC+1 (CET); change for your timezone
#define NTP_DST_OFFSET  3600    // additional hour for CEST

// --- Display (Sunton ESP32-8048S043 RGB-LCD 800×480) -------------------------
#define LCD_WIDTH   800
#define LCD_HEIGHT  480

// RGB data bus — 16-bit (B[4:0] | G[5:0] | R[4:0])
#define LCD_PIN_B0   1
#define LCD_PIN_B1   9
#define LCD_PIN_B2   46
#define LCD_PIN_B3   3
#define LCD_PIN_B4   8
#define LCD_PIN_G0   5
#define LCD_PIN_G1   6
#define LCD_PIN_G2   7
#define LCD_PIN_G3   15
#define LCD_PIN_G4   16
#define LCD_PIN_G5   4
#define LCD_PIN_R0   14
#define LCD_PIN_R1   21
#define LCD_PIN_R2   47
#define LCD_PIN_R3   48
#define LCD_PIN_R4   45

// Sync / control
#define LCD_PIN_HSYNC  39
#define LCD_PIN_VSYNC  41
#define LCD_PIN_DE     40
#define LCD_PIN_PCLK   42
#define LCD_PIN_BL     2     // Backlight PWM

// Pixel clock (Hz) — 16 MHz is stable on most Sunton panels
#define LCD_PCLK_HZ    (16 * 1000 * 1000)

// Horizontal timing (pixels)
#define LCD_HSYNC_PW   4
#define LCD_HSYNC_BP   8
#define LCD_HSYNC_FP   8

// Vertical timing (lines)
#define LCD_VSYNC_PW   4
#define LCD_VSYNC_BP   16
#define LCD_VSYNC_FP   16

// --- Touch (GT911, I²C) ------------------------------------------------------
#define TOUCH_I2C_PORT  0         // Wire / I2C_NUM_0
#define TOUCH_PIN_SDA   19
#define TOUCH_PIN_SCL   20
#define TOUCH_PIN_INT   18
#define TOUCH_PIN_RST   38
#define TOUCH_I2C_ADDR  0x14      // 0x14 when INT pulled LOW; 0x5D otherwise
#define TOUCH_I2C_FREQ  400000

// --- NFC (MFRC522, SPI) ------------------------------------------------------
// Connect your MFRC522 breakout to these pins:
//   MFRC522  →  ESP32-S3
//   SCK      →  GPIO 12
//   MISO     →  GPIO 13
//   MOSI     →  GPIO 11
//   SDA/CS   →  GPIO 10
//   RST      →  GPIO 17  (or tie to 3.3 V via 10 kΩ if unused)
//   3.3 V    →  3.3 V
//   GND      →  GND
#define NFC_PIN_SCK   12
#define NFC_PIN_MISO  13
#define NFC_PIN_MOSI  11
#define NFC_PIN_CS    10
#define NFC_PIN_RST   17

// Minimum UID byte length to be considered valid
#define NFC_MIN_UID_BYTES  4

// Debounce: ignore the same card for this many ms after a successful read
#define NFC_RESCAN_DELAY_MS  2000

// --- UI timings --------------------------------------------------------------
// How long (ms) the admin long-press must be held to open the password screen
#define ADMIN_LONG_PRESS_MS  5000

// Duration (ms) of the "helper" transient messages on the clock screen
#define HELPER_MSG_DURATION_MS  2500

// Log-panel refresh interval (ms)
#define LOG_PANEL_REFRESH_MS  10000

// Clock tick interval (ms)
#define CLOCK_TICK_MS  1000

// --- Storage paths -----------------------------------------------------------
#define STORAGE_USERS_PATH     "/users.json"
#define STORAGE_SESSIONS_PATH  "/sessions.json"
#define STORAGE_SETTINGS_PATH  "/settings.json"

// Maximum sessions kept in LittleFS before the oldest are purged
#define MAX_SESSIONS  2000

// Sessions older than this many days are purged on startup
#define SESSION_PURGE_DAYS  30
