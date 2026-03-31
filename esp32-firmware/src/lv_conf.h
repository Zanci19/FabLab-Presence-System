// =============================================================================
// FabLab Presence System — LVGL 8.3 configuration
// Place this file next to main.cpp so -DLV_CONF_INCLUDE_SIMPLE finds it.
// Only the settings that differ from the LVGL defaults are listed here.
// =============================================================================

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

// --- Colour depth ------------------------------------------------------------
#define LV_COLOR_DEPTH 16         // RGB565 — native format of the RGB LCD

// --- Memory ------------------------------------------------------------------
// Use heap allocation instead of LVGL's static RAM arena.
// This removes a large .bss allocation from internal DRAM and lets the
// allocator place blocks in PSRAM when available.
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE "stdlib.h"
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC realloc
#define LV_MEM_CUSTOM_GET_SIZE 0

// --- HAL tick ----------------------------------------------------------------
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE  "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

// --- Display resolution ------------------------------------------------------
#define LV_HOR_RES_MAX 800
#define LV_VER_RES_MAX 480

// --- Logging -----------------------------------------------------------------
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

// --- Fonts (built-in Montserrat, keep only sizes we actually use) -----------
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

// Unscii 8 px pixel font — good fallback for terminal/CRT aesthetic
#define LV_FONT_UNSCII_8  1
#define LV_FONT_UNSCII_16 1

// --- Widgets ----------------------------------------------------------------
#define LV_USE_BTN      1
#define LV_USE_LABEL    1
#define LV_USE_TEXTAREA 1
#define LV_USE_KEYBOARD 1
#define LV_USE_TABLE    1
#define LV_USE_LIST     1
#define LV_USE_CONT     1
#define LV_USE_OBJ      1
#define LV_USE_SPINNER  1
#define LV_USE_MSGBOX   1
#define LV_USE_WIN      1
#define LV_USE_TABVIEW  0   // not used
#define LV_USE_TILEVIEW 0

// --- Animation ---------------------------------------------------------------
#define LV_USE_ANIMATION 1

// --- GPU / DMA ---------------------------------------------------------------
#define LV_USE_GPU_ESP32S3_DMA2D 0   // set 1 if you enable DMA2D in sdkconfig

// --- Misc --------------------------------------------------------------------
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1      // dark theme base — we override everything
#define LV_USE_THEME_MONO    0
#define LV_USE_THEME_BASIC   0

#endif /* LV_CONF_H */
