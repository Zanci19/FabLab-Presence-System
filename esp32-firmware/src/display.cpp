// =============================================================================
// FabLab Presence System — Display driver
// RGB-LCD (esp_lcd) + GT911 touch (I²C) + LVGL 8.3
// =============================================================================

#include "display.h"
#include "config.h"

#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_idf_version.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>

// ---------------------------------------------------------------------------
// Frame buffer (PSRAM)
// Two full-screen buffers for flicker-free rendering
// ---------------------------------------------------------------------------
static lv_color_t *s_buf1 = nullptr;
static lv_color_t *s_buf2 = nullptr;
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t      s_disp_drv;
static lv_indev_drv_t     s_touch_drv;

static esp_lcd_panel_handle_t s_panel = nullptr;

// ---------------------------------------------------------------------------
// LVGL flush callback
// ---------------------------------------------------------------------------
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                          lv_color_t *color_p)
{
    esp_lcd_panel_draw_bitmap(s_panel,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              color_p);
    lv_disp_flush_ready(drv);
}

// ---------------------------------------------------------------------------
// GT911 touch — minimal I²C driver
// ---------------------------------------------------------------------------
static const uint8_t GT911_ADDR        = TOUCH_I2C_ADDR;
static const uint16_t GT911_REG_STATUS = 0x814E;
static const uint16_t GT911_REG_PT1    = 0x8150;

static void gt911_write_reg(uint16_t reg, uint8_t val)
{
    Wire.beginTransmission(GT911_ADDR);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    Wire.write(val);
    Wire.endTransmission();
}

static uint8_t gt911_read_reg(uint16_t reg)
{
    Wire.beginTransmission(GT911_ADDR);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)GT911_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
}

static bool gt911_read_touch(lv_coord_t *x_out, lv_coord_t *y_out)
{
    uint8_t status = gt911_read_reg(GT911_REG_STATUS);
    if (!(status & 0x80) || (status & 0x0F) == 0) {
        return false;   // no new data or no touches
    }

    // Read 6 bytes: track-id(1) + x(2) + y(2) + size(1)
    Wire.beginTransmission(GT911_ADDR);
    Wire.write(GT911_REG_PT1 >> 8);
    Wire.write(GT911_REG_PT1 & 0xFF);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)GT911_ADDR, (uint8_t)6);

    uint8_t buf[6] = {};
    for (int i = 0; i < 6 && Wire.available(); i++) buf[i] = Wire.read();

    // Clear the buffer-ready flag
    gt911_write_reg(GT911_REG_STATUS, 0);

    *x_out = (lv_coord_t)(buf[1] | (buf[2] << 8));
    *y_out = (lv_coord_t)(buf[3] | (buf[4] << 8));

    // Clamp to display bounds
    if (*x_out < 0) *x_out = 0;
    if (*x_out >= LCD_WIDTH)  *x_out = LCD_WIDTH  - 1;
    if (*y_out < 0) *y_out = 0;
    if (*y_out >= LCD_HEIGHT) *y_out = LCD_HEIGHT - 1;

    return true;
}

// LVGL input-device read callback
static void lvgl_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    lv_coord_t x = 0, y = 0;
    if (gt911_read_touch(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state   = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// ---------------------------------------------------------------------------
// Backlight (LEDC PWM)
// ---------------------------------------------------------------------------
static const ledc_channel_t BL_CHANNEL = LEDC_CHANNEL_0;
static const ledc_timer_t   BL_TIMER   = LEDC_TIMER_0;

static void backlight_init()
{
    ledc_timer_config_t timer = {};
    timer.speed_mode       = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution  = LEDC_TIMER_8_BIT;
    timer.timer_num        = BL_TIMER;
    timer.freq_hz          = 5000;
    timer.clk_cfg          = LEDC_AUTO_CLK;
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {};
    channel.gpio_num   = LCD_PIN_BL;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel    = BL_CHANNEL;
    channel.timer_sel  = BL_TIMER;
    channel.duty       = 255;
    channel.hpoint     = 0;
    ledc_channel_config(&channel);
}

void display_set_backlight(uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint32_t duty = (uint32_t)percent * 255 / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
}

// ---------------------------------------------------------------------------
// display_init
// ---------------------------------------------------------------------------
void display_init()
{
    // --- I²C for GT911 touch ------------------------------------------------
    Wire.begin(TOUCH_PIN_SDA, TOUCH_PIN_SCL, TOUCH_I2C_FREQ);

    // Hard-reset GT911
    gpio_set_direction((gpio_num_t)TOUCH_PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)TOUCH_PIN_INT, GPIO_MODE_OUTPUT);
    // Pull INT low → I²C address 0x14
    gpio_set_level((gpio_num_t)TOUCH_PIN_INT, 0);
    gpio_set_level((gpio_num_t)TOUCH_PIN_RST, 0);
    delay(10);
    gpio_set_level((gpio_num_t)TOUCH_PIN_RST, 1);
    delay(100);
    gpio_set_direction((gpio_num_t)TOUCH_PIN_INT, GPIO_MODE_INPUT);

    // --- RGB LCD ------------------------------------------------------------
    esp_lcd_rgb_panel_config_t panel_cfg = {};
    panel_cfg.clk_src                  = LCD_CLK_SRC_XTAL;
    panel_cfg.timings.pclk_hz          = LCD_PCLK_HZ;
    panel_cfg.timings.h_res            = LCD_WIDTH;
    panel_cfg.timings.v_res            = LCD_HEIGHT;
    panel_cfg.timings.hsync_pulse_width = LCD_HSYNC_PW;
    panel_cfg.timings.hsync_back_porch  = LCD_HSYNC_BP;
    panel_cfg.timings.hsync_front_porch = LCD_HSYNC_FP;
    panel_cfg.timings.vsync_pulse_width = LCD_VSYNC_PW;
    panel_cfg.timings.vsync_back_porch  = LCD_VSYNC_BP;
    panel_cfg.timings.vsync_front_porch = LCD_VSYNC_FP;
    panel_cfg.timings.flags.pclk_active_neg = true;
    panel_cfg.data_width               = 16;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
    panel_cfg.num_fbs                  = 2;   // introduced in IDF 5.1
#elif ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    panel_cfg.flags.double_fb          = true; // IDF 5.0.x equivalent
#endif
    panel_cfg.sram_trans_align         = 8;
    panel_cfg.psram_trans_align        = 64;
    panel_cfg.hsync_gpio_num           = LCD_PIN_HSYNC;
    panel_cfg.vsync_gpio_num           = LCD_PIN_VSYNC;
    panel_cfg.de_gpio_num              = LCD_PIN_DE;
    panel_cfg.pclk_gpio_num            = LCD_PIN_PCLK;
    panel_cfg.disp_gpio_num            = GPIO_NUM_NC;
    panel_cfg.flags.fb_in_psram        = true;

    // Data pins: B[4:0], G[5:0], R[4:0] (LSB first)
    const int data_pins[16] = {
        LCD_PIN_B0, LCD_PIN_B1, LCD_PIN_B2, LCD_PIN_B3, LCD_PIN_B4,
        LCD_PIN_G0, LCD_PIN_G1, LCD_PIN_G2, LCD_PIN_G3, LCD_PIN_G4, LCD_PIN_G5,
        LCD_PIN_R0, LCD_PIN_R1, LCD_PIN_R2, LCD_PIN_R3, LCD_PIN_R4,
    };
    for (int i = 0; i < 16; i++) panel_cfg.data_gpio_nums[i] = data_pins[i];

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    // Backlight on (full brightness)
    backlight_init();
    display_set_backlight(100);

    // --- LVGL ---------------------------------------------------------------
    lv_init();

    // Allocate two full-frame PSRAM buffers
    size_t buf_size = (size_t)LCD_WIDTH * LCD_HEIGHT * sizeof(lv_color_t);
    s_buf1 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_buf2 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(s_buf1 && s_buf2 && "Failed to allocate LVGL frame buffers in PSRAM");

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, LCD_WIDTH * LCD_HEIGHT);

    // Register display driver
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res     = LCD_WIDTH;
    s_disp_drv.ver_res     = LCD_HEIGHT;
    s_disp_drv.flush_cb    = lvgl_flush_cb;
    s_disp_drv.draw_buf    = &s_draw_buf;
    s_disp_drv.full_refresh = 1;
    lv_disp_drv_register(&s_disp_drv);

    // Register touch input driver
    lv_indev_drv_init(&s_touch_drv);
    s_touch_drv.type    = LV_INDEV_TYPE_POINTER;
    s_touch_drv.read_cb = lvgl_touch_read_cb;
    lv_indev_drv_register(&s_touch_drv);

    Serial.println("[DISPLAY] RGB LCD + GT911 touch + LVGL ready.");
}
