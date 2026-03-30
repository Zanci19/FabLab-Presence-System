// =============================================================================
// FabLab Presence System — main.cpp (Arduino entry point)
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <LittleFS.h>
#include <lvgl.h>
#include <time.h>

#include "config.h"
#include "display.h"
#include "ui.h"
#include "storage.h"
#include "nfc_reader.h"
#include "api_server.h"

// ---------------------------------------------------------------------------
// FreeRTOS: NFC events queue (NFC is polled in a separate task)
// ---------------------------------------------------------------------------
static QueueHandle_t s_nfc_queue;

// NFC task — runs on core 0 (networking + NFC)
static void nfc_task(void *arg)
{
    (void)arg;
    while (true) {
        nfc_poll();
        vTaskDelay(pdMS_TO_TICKS(100));  // poll every 100 ms
    }
}

// ---------------------------------------------------------------------------
// WiFi connection helper
// ---------------------------------------------------------------------------
static bool wifi_connect(const AppSettings &settings, uint32_t timeout_ms = 15000)
{
    String ssid = settings.wifi_ssid.isEmpty() ? WIFI_SSID : settings.wifi_ssid;
    String pass = settings.wifi_password.isEmpty() ? WIFI_PASS : settings.wifi_password;

    if (ssid.isEmpty() || ssid == "YourNetworkName") {
        Serial.println("[WIFI] No credentials configured — skipping WiFi.");
        return false;
    }

    Serial.printf("[WIFI] Connecting to \"%s\"...\n", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeout_ms) {
            Serial.println("[WIFI] Connection timed out.");
            return false;
        }
        delay(250);
    }
    Serial.printf("[WIFI] Connected — IP: %s\n",
                  WiFi.localIP().toString().c_str());
    return true;
}

// ---------------------------------------------------------------------------
// NTP — synchronise system time
// ---------------------------------------------------------------------------
static WiFiUDP s_ntp_udp;
static NTPClient s_ntp(s_ntp_udp, NTP_SERVER, NTP_UTC_OFFSET + NTP_DST_OFFSET);

static void ntp_sync()
{
    if (WiFi.status() != WL_CONNECTED) return;
    s_ntp.begin();
    s_ntp.update();
    time_t epoch = (time_t)s_ntp.getEpochTime();
    struct timeval tv = { epoch, 0 };
    settimeofday(&tv, nullptr);
    Serial.printf("[NTP] Time synced: %s", ctime(&epoch));
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    delay(100);
    Serial.println("=== FabLab Presence System (ESP32-S3) starting ===");

    // 1. Display + LVGL
    display_init();

    // 2. Storage (LittleFS)
    if (!storage_init()) {
        Serial.println("[MAIN] Storage init failed — halting.");
        while (true) delay(1000);
    }

    // 3. Load settings
    AppSettings settings = storage_load_settings();

    // 4. NFC reader
    bool nfc_ok = nfc_init();
    if (nfc_ok) {
        nfc_set_callback([](const String &uid) {
            // Send to queue so LVGL task processes it safely
            char buf[32];
            strncpy(buf, uid.c_str(), sizeof(buf) - 1);
            xQueueSend(s_nfc_queue, buf, 0);
        });
    }

    // 5. Build LVGL UI (must be done after display_init + storage_init)
    ui_init();

    // 6. WiFi + NTP + HTTP server
    if (wifi_connect(settings)) {
        ntp_sync();
        storage_purge_old_sessions();  // purge after time is valid
        api_server_init();
        Serial.printf("[MAIN] Web UI available at http://%s/\n",
                      WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[MAIN] Running offline — NTP / web server unavailable.");
    }

    // 7. Create NFC FreeRTOS task on core 0
    s_nfc_queue = xQueueCreate(4, 32);
    if (nfc_ok) {
        xTaskCreatePinnedToCore(nfc_task, "nfc_task", 4096, nullptr, 1,
                                nullptr, 0);
    }

    Serial.println("[MAIN] Setup complete.");
}

// ---------------------------------------------------------------------------
// loop()
// ---------------------------------------------------------------------------
static uint32_t s_clock_tick_last   = 0;
static uint32_t s_log_refresh_last  = 0;
static uint32_t s_ntp_resync_last   = 0;
static const uint32_t NTP_RESYNC_INTERVAL_MS = 3600000UL;  // 1 hour

void loop()
{
    uint32_t now = millis();

    // Process LVGL timers and redraws
    lv_timer_handler();
    delay(5);

    // Process any NFC scans queued from the NFC task
    char nfc_buf[32];
    while (xQueueReceive(s_nfc_queue, nfc_buf, 0) == pdTRUE) {
        Serial.printf("[MAIN] NFC event: %s\n", nfc_buf);
        ui_nfc_scan(nfc_buf);
        api_server_push_nfc(String(nfc_buf));  // push to WebSocket browsers
    }

    // Clock update (every second)
    if (now - s_clock_tick_last >= (uint32_t)CLOCK_TICK_MS) {
        s_clock_tick_last = now;
        ui_clock_tick();
    }

    // Periodic NTP re-sync (every hour when WiFi is connected)
    if (WiFi.status() == WL_CONNECTED &&
        now - s_ntp_resync_last >= NTP_RESYNC_INTERVAL_MS) {
        s_ntp_resync_last = now;
        s_ntp.update();
        time_t epoch = (time_t)s_ntp.getEpochTime();
        struct timeval tv = { epoch, 0 };
        settimeofday(&tv, nullptr);
    }
}
