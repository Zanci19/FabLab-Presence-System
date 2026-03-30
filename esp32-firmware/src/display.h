// =============================================================================
// FabLab Presence System — Display driver header
// RGB-LCD + GT911 capacitive touch for Sunton ESP32-8048S043
// =============================================================================

#pragma once
#include <lvgl.h>

// Initialise the RGB LCD panel, GT911 touch controller, and register LVGL
// display + input drivers.  Call once from setup() before any LVGL code.
void display_init();

// Set backlight brightness (0–100 %).
void display_set_backlight(uint8_t percent);
