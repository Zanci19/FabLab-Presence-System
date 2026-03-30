// =============================================================================
// FabLab Presence System — LVGL UI header
// =============================================================================

#pragma once
#include <lvgl.h>

// Call once from setup() after display_init() and storage_init().
void ui_init();

// Called from the main loop to update the clock display every second.
void ui_clock_tick();

// Inject an NFC scan (from hardware reader or WebSocket).
// Must be called from the LVGL task context (e.g. inside lv_timer callback
// or directly in loop() before lv_timer_handler()).
void ui_nfc_scan(const char *normalised_uid);
