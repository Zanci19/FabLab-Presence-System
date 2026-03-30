// =============================================================================
// FabLab Presence System — LVGL UI (all screens)
//
// Screens match the HTML project 1-to-1:
//   start → intro → clock → greeting+activity → name-entry → admin-pass → admin
//
// CRT terminal colour palette (same CSS variables):
//   bg      #000000   orange  #FF8C00   green  #00FF41
//   red     #FF2222   white   #E0E0E0   dim    #555555
// =============================================================================

#include "ui.h"
#include "config.h"
#include "storage.h"
#include "nfc_reader.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <time.h>
#include <vector>

// ---------------------------------------------------------------------------
// Colour palette (RGB565 via lv_color_hex)
// ---------------------------------------------------------------------------
#define C_BG     lv_color_hex(0x000000)
#define C_ORANGE lv_color_hex(0xFF8C00)
#define C_GREEN  lv_color_hex(0x00FF41)
#define C_RED    lv_color_hex(0xFF2222)
#define C_WHITE  lv_color_hex(0xE0E0E0)
#define C_DIM    lv_color_hex(0x555555)
#define C_DARK   lv_color_hex(0x111111)
#define C_BORDER lv_color_hex(0x2A2A2A)

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void show_screen(int id);
static void handle_nfc_read(const char *nfc_id);

// ---------------------------------------------------------------------------
// Screen IDs
// ---------------------------------------------------------------------------
enum {
    SCR_START = 0,
    SCR_INTRO,
    SCR_CLOCK,
    SCR_GREETING,
    SCR_NAME_ENTRY,
    SCR_ADMIN_PASS,
    SCR_ADMIN,
};

// ---------------------------------------------------------------------------
// Application state (mirrors script.js globals)
// ---------------------------------------------------------------------------
static User     g_current_user;
static bool     g_logged_in         = false;
static int      g_active_session_id = -1;
static String   g_pending_nfc_id;
static bool     g_nfc_flow_active   = false;
static bool     g_admin_add_mode    = false;
static String   g_admin_password;
static bool     g_admin_logged_in   = false;
static int      g_admin_logs_page   = 1;

// Name-entry flow state
enum NameEntryMode { NE_CONFIRM_CREATE, NE_COLLECT_NAME, NE_COLLECT_GENDER };
static NameEntryMode g_ne_mode       = NE_CONFIRM_CREATE;
static bool          g_ne_admin_ctx  = false;
static String        g_ne_full_name;

// Config
static AppSettings g_settings;

// Activities list (matches script.js)
static const char *ACTIVITIES[] = {
    "3D tiskanje", "Programiranje", "Modeliranje",
    "Zabušavanje",  "Učenje",        "Maintenance",
    "Drugo",
};
static const int ACTIVITY_COUNT = 7;

// ---------------------------------------------------------------------------
// LVGL screen objects
// ---------------------------------------------------------------------------
static lv_obj_t *scr[7];  // one entry per SCR_xxx constant

// --- START screen ---
static lv_obj_t *btn_start;

// --- INTRO screen ---
static lv_obj_t *cont_intro;
static lv_obj_t *lbl_intro_lines[3];
static int        intro_line_idx  = 0;
static int        intro_char_idx  = 0;
static lv_timer_t *intro_timer    = nullptr;
static const char *INTRO_TEXT[3]  = { "FABLAB", "MANAGEMENT", "SYSTEM" };

// --- CLOCK screen ---
static lv_obj_t *lbl_clock;
static lv_obj_t *lbl_date;
static lv_obj_t *lbl_helper;
static lv_obj_t *cont_log;
static lv_obj_t *lbl_log_entries[7];
static lv_timer_t *helper_timer = nullptr;
static lv_timer_t *log_timer    = nullptr;

// --- GREETING screen ---
static lv_obj_t *lbl_greeting;
static lv_obj_t *cont_activity;

// --- NAME-ENTRY screen ---
static lv_obj_t *lbl_ne_title;
static lv_obj_t *ta_ne;
static lv_obj_t *kbd_ne;
static lv_obj_t *btn_ne_submit;
static lv_obj_t *btn_ne_cancel;

// --- ADMIN-PASS screen ---
static lv_obj_t *ta_ap;
static lv_obj_t *kbd_ap;

// --- ADMIN screen ---
static lv_obj_t *lbl_admin_status;
static lv_obj_t *cont_menu;
static lv_obj_t *cont_adduser;
static lv_obj_t *lbl_adduser_status;
static lv_obj_t *cont_deluser;
static lv_obj_t *list_deluser;
static lv_obj_t *cont_logs;
static lv_obj_t *table_logs;
static lv_obj_t *lbl_logs_page;

// ---------------------------------------------------------------------------
// Style helpers
// ---------------------------------------------------------------------------
static lv_style_t st_screen, st_label_white, st_label_orange,
                  st_label_green, st_label_red, st_label_dim,
                  st_btn_crt, st_btn_orange, st_btn_danger,
                  st_ta_crt, st_kbd_crt, st_table_crt, st_list_crt;

static void styles_init()
{
    // Screen (black background)
    lv_style_init(&st_screen);
    lv_style_set_bg_color(&st_screen, C_BG);
    lv_style_set_bg_opa(&st_screen, LV_OPA_COVER);
    lv_style_set_border_width(&st_screen, 0);
    lv_style_set_pad_all(&st_screen, 0);

    // White label
    lv_style_init(&st_label_white);
    lv_style_set_text_color(&st_label_white, C_WHITE);

    // Orange label
    lv_style_init(&st_label_orange);
    lv_style_set_text_color(&st_label_orange, C_ORANGE);

    // Green label
    lv_style_init(&st_label_green);
    lv_style_set_text_color(&st_label_green, C_GREEN);

    // Red label
    lv_style_init(&st_label_red);
    lv_style_set_text_color(&st_label_red, C_RED);

    // Dim label
    lv_style_init(&st_label_dim);
    lv_style_set_text_color(&st_label_dim, C_DIM);

    // CRT terminal button
    lv_style_init(&st_btn_crt);
    lv_style_set_bg_color(&st_btn_crt, C_BG);
    lv_style_set_bg_opa(&st_btn_crt, LV_OPA_COVER);
    lv_style_set_border_color(&st_btn_crt, C_BORDER);
    lv_style_set_border_width(&st_btn_crt, 1);
    lv_style_set_radius(&st_btn_crt, 0);
    lv_style_set_text_color(&st_btn_crt, C_DIM);
    lv_style_set_text_font(&st_btn_crt, &lv_font_montserrat_16);
    lv_style_set_pad_hor(&st_btn_crt, 16);
    lv_style_set_pad_ver(&st_btn_crt, 10);

    // Orange accent button (VSTOPI, POTRDI)
    lv_style_init(&st_btn_orange);
    lv_style_set_bg_color(&st_btn_orange, C_ORANGE);
    lv_style_set_bg_opa(&st_btn_orange, LV_OPA_COVER);
    lv_style_set_border_color(&st_btn_orange, C_ORANGE);
    lv_style_set_border_width(&st_btn_orange, 1);
    lv_style_set_radius(&st_btn_orange, 0);
    lv_style_set_text_color(&st_btn_orange, C_BG);
    lv_style_set_text_font(&st_btn_orange, &lv_font_montserrat_16);

    // Danger button (PREKLIČI / IZBRIŠI)
    lv_style_init(&st_btn_danger);
    lv_style_set_bg_color(&st_btn_danger, C_BG);
    lv_style_set_bg_opa(&st_btn_danger, LV_OPA_COVER);
    lv_style_set_border_color(&st_btn_danger, C_RED);
    lv_style_set_border_width(&st_btn_danger, 1);
    lv_style_set_radius(&st_btn_danger, 0);
    lv_style_set_text_color(&st_btn_danger, C_RED);
    lv_style_set_text_font(&st_btn_danger, &lv_font_montserrat_16);

    // Text area
    lv_style_init(&st_ta_crt);
    lv_style_set_bg_color(&st_ta_crt, C_BG);
    lv_style_set_bg_opa(&st_ta_crt, LV_OPA_COVER);
    lv_style_set_border_color(&st_ta_crt, C_ORANGE);
    lv_style_set_border_width(&st_ta_crt, 1);
    lv_style_set_radius(&st_ta_crt, 0);
    lv_style_set_text_color(&st_ta_crt, C_WHITE);
    lv_style_set_text_font(&st_ta_crt, &lv_font_montserrat_20);

    // Table
    lv_style_init(&st_table_crt);
    lv_style_set_bg_color(&st_table_crt, C_BG);
    lv_style_set_bg_opa(&st_table_crt, LV_OPA_COVER);
    lv_style_set_text_color(&st_table_crt, C_WHITE);
    lv_style_set_text_font(&st_table_crt, &lv_font_montserrat_14);
    lv_style_set_border_color(&st_table_crt, C_BORDER);
    lv_style_set_border_width(&st_table_crt, 1);

    // List
    lv_style_init(&st_list_crt);
    lv_style_set_bg_color(&st_list_crt, C_BG);
    lv_style_set_bg_opa(&st_list_crt, LV_OPA_COVER);
    lv_style_set_border_width(&st_list_crt, 0);
}

// ---------------------------------------------------------------------------
// Utility: set all screens' background to black
// ---------------------------------------------------------------------------
static void screen_base(lv_obj_t *scr_obj)
{
    lv_obj_add_style(scr_obj, &st_screen, 0);
    lv_obj_set_scrollbar_mode(scr_obj, LV_SCROLLBAR_MODE_OFF);
}

// ---------------------------------------------------------------------------
// Helper: text button
// ---------------------------------------------------------------------------
static lv_obj_t *make_btn(lv_obj_t *parent, const char *text, bool orange,
                           lv_coord_t w, lv_coord_t h,
                           lv_event_cb_t cb, void *user)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_add_style(btn, orange ? &st_btn_orange : &st_btn_crt, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
    return btn;
}

// ---------------------------------------------------------------------------
// Helper: create a centered label
// ---------------------------------------------------------------------------
static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                              const lv_font_t *font, lv_style_t *style)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    if (font)  lv_obj_set_style_text_font(l, font, 0);
    if (style) lv_obj_add_style(l, style, 0);
    return l;
}

// ---------------------------------------------------------------------------
// Helper: ISO-8601 now string
// ---------------------------------------------------------------------------
static String iso_now_str()
{
    time_t t = time(nullptr);
    if (t < 1000000) {
        // Time not synced; fallback to millis-based approximate
        return "1970-01-01T00:00:00Z";
    }
    struct tm *tm = gmtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm);
    return String(buf);
}

static String today_key_str()
{
    time_t t = time(nullptr);
    struct tm *tm = gmtime(&t);
    char buf[12];
    strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
    return String(buf);
}

static String build_full_name(const User &u)
{
    String fn = u.name;
    if (u.surname.length() > 0) fn += " " + u.surname;
    return fn;
}

// ---------------------------------------------------------------------------
// Admin status bar (bottom of admin screen)
// ---------------------------------------------------------------------------
static void show_admin_status(const char *msg, bool is_error)
{
    if (!lbl_admin_status) return;
    lv_label_set_text(lbl_admin_status, msg);
    lv_obj_set_style_text_color(lbl_admin_status,
                                 is_error ? C_RED : C_GREEN, 0);
    lv_obj_clear_flag(lbl_admin_status, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------------
// Helper message on clock screen
// ---------------------------------------------------------------------------
static void set_helper(const char *text, lv_color_t col, uint32_t duration_ms)
{
    if (!lbl_helper) return;
    lv_label_set_text(lbl_helper, text);
    lv_obj_set_style_text_color(lbl_helper, col, 0);

    if (helper_timer) { lv_timer_del(helper_timer); helper_timer = nullptr; }

    if (duration_ms > 0) {
        helper_timer = lv_timer_create([](lv_timer_t *t) {
            lv_label_set_text(lbl_helper, "Prisloni ključ za vstop/izstop...");
            lv_obj_set_style_text_color(lbl_helper, C_WHITE, 0);
            lv_timer_del(t);
            helper_timer = nullptr;
        }, duration_ms, nullptr);
        lv_timer_set_repeat_count(helper_timer, 1);
    }
}

// ---------------------------------------------------------------------------
// Log panel refresh
// ---------------------------------------------------------------------------
static void refresh_log_panel(lv_timer_t *)
{
    auto sessions = storage_recent_sessions(7);
    for (int i = 0; i < 7; i++) {
        if (!lbl_log_entries[i]) continue;
        if (i < (int)sessions.size()) {
            const Session &s = sessions[i];
            // Format: "Name    HH:MM"
            String login = s.login_time;
            int hh = 0, mm = 0;
            if (login.length() >= 16) {
                // ISO: YYYY-MM-DDTHH:MM:SSZ
                hh = login.substring(11, 13).toInt();
                mm = login.substring(14, 16).toInt();
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "%-18s %02d:%02d",
                     s.name.substring(0, 18).c_str(), hh, mm);
            lv_label_set_text(lbl_log_entries[i], buf);
            lv_obj_clear_flag(lbl_log_entries[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(lbl_log_entries[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ---------------------------------------------------------------------------
// Show / hide admin sub-views
// ---------------------------------------------------------------------------
enum AdminView { AV_MENU, AV_ADDUSER, AV_DELUSER, AV_LOGS };
static void show_admin_view(AdminView v)
{
    auto show = [](lv_obj_t *o, bool vis) {
        if (!o) return;
        if (vis) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
        else     lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    };
    show(cont_menu,    v == AV_MENU);
    show(cont_adduser, v == AV_ADDUSER);
    show(cont_deluser, v == AV_DELUSER);
    show(cont_logs,    v == AV_LOGS);
    if (lbl_admin_status) lv_obj_add_flag(lbl_admin_status, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------------
// Screen transition
// ---------------------------------------------------------------------------
static void show_screen(int id)
{
    if (id < 0 || id >= 7) return;
    lv_scr_load_anim(scr[id], LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
}

// ---------------------------------------------------------------------------
// NFC flow — forward declarations
// ---------------------------------------------------------------------------
static void do_login(const User &user);
static void do_logout(const User &user, int session_id);
static void show_greeting(const User &user);
static void show_name_entry();
static void submit_name_entry();
static void cancel_name_entry();
static void register_pending_user(const String &full_name, const char *gender);

// ===========================================================================
// SCREEN: START
// ===========================================================================
static void cb_start_click(lv_event_t *e)
{
    (void)e;
    Serial.println("[UI] START pressed — running intro");
    show_screen(SCR_INTRO);

    // Reset intro state
    intro_line_idx = 0;
    intro_char_idx = 0;
    for (int i = 0; i < 3; i++) lv_label_set_text(lbl_intro_lines[i], "");

    // Start typing timer (60 ms per character)
    if (intro_timer) { lv_timer_del(intro_timer); intro_timer = nullptr; }
    intro_timer = lv_timer_create([](lv_timer_t *t) {
        if (intro_line_idx >= 3) {
            lv_timer_del(t); intro_timer = nullptr;
            // Wait 1.6 s then show clock
            lv_timer_t *done = lv_timer_create([](lv_timer_t *td) {
                lv_timer_del(td);
                show_screen(SCR_CLOCK);
            }, 1600, nullptr);
            lv_timer_set_repeat_count(done, 1);
            return;
        }
        const char *line = INTRO_TEXT[intro_line_idx];
        int len = (int)strlen(line);
        if (intro_char_idx < len) {
            // Append next character
            char buf[32];
            strncpy(buf, line, intro_char_idx + 1);
            buf[intro_char_idx + 1] = '\0';
            lv_label_set_text(lbl_intro_lines[intro_line_idx], buf);
            intro_char_idx++;
        } else {
            // Move to next line
            lv_label_set_text(lbl_intro_lines[intro_line_idx], line);
            intro_line_idx++;
            intro_char_idx = 0;
        }
    }, 70, nullptr);  // 70 ms tick

    lv_timer_set_repeat_count(intro_timer, -1);
}

static void build_start_screen()
{
    lv_obj_t *s = scr[SCR_START];
    screen_base(s);

    btn_start = lv_btn_create(s);
    lv_obj_set_size(btn_start, LV_PCT(100), LV_PCT(100));
    lv_obj_add_style(btn_start, &st_screen, 0);
    lv_obj_set_style_shadow_width(btn_start, 0, 0);

    lv_obj_t *lbl = lv_label_create(btn_start);
    lv_label_set_text(lbl, "START");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl, C_WHITE, 0);
    lv_obj_set_style_text_letter_space(lbl, 20, 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn_start, cb_start_click, LV_EVENT_CLICKED, nullptr);
}

// ===========================================================================
// SCREEN: INTRO
// ===========================================================================
static void build_intro_screen()
{
    lv_obj_t *s = scr[SCR_INTRO];
    screen_base(s);

    cont_intro = lv_obj_create(s);
    lv_obj_set_size(cont_intro, LV_PCT(100), LV_PCT(100));
    lv_obj_add_style(cont_intro, &st_screen, 0);
    lv_obj_set_flex_flow(cont_intro, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_intro, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < 3; i++) {
        lbl_intro_lines[i] = lv_label_create(cont_intro);
        lv_label_set_text(lbl_intro_lines[i], "");
        lv_obj_set_style_text_font(lbl_intro_lines[i], &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(lbl_intro_lines[i], C_ORANGE, 0);
        lv_obj_set_style_text_letter_space(lbl_intro_lines[i], 14, 0);
    }
}

// ===========================================================================
// SCREEN: CLOCK
// ===========================================================================
static void cb_clock_long_press(lv_event_t *e)
{
    (void)e;
    Serial.println("[UI] Long press detected — opening admin password screen.");
    // Clear password field
    lv_textarea_set_text(ta_ap, "");
    show_screen(SCR_ADMIN_PASS);
}

static void build_clock_screen()
{
    lv_obj_t *s = scr[SCR_CLOCK];
    screen_base(s);

    // Root row container
    lv_obj_t *row = lv_obj_create(s);
    lv_obj_set_size(row, LV_PCT(100), LV_PCT(100));
    lv_obj_add_style(row, &st_screen, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_STRETCH, LV_FLEX_ALIGN_STRETCH);

    // --- Left (clock-main) ---
    lv_obj_t *left = lv_obj_create(row);
    lv_obj_set_flex_grow(left, 1);
    lv_obj_set_height(left, LV_PCT(100));
    lv_obj_add_style(left, &st_screen, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(left, 8, 0);

    // Long-press on the clock area → admin
    lv_obj_add_flag(left, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_long_press_time(left, ADMIN_LONG_PRESS_MS, 0);
    lv_obj_add_event_cb(left, cb_clock_long_press, LV_EVENT_LONG_PRESSED, nullptr);

    // Clock time label (use Montserrat 48 — see README for larger custom font)
    lbl_clock = lv_label_create(left);
    lv_label_set_text(lbl_clock, "00:00:00");
    lv_obj_set_style_text_font(lbl_clock, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_clock, C_WHITE, 0);
    lv_obj_set_style_text_letter_space(lbl_clock, 4, 0);

    // Date label
    lbl_date = lv_label_create(left);
    lv_label_set_text(lbl_date, "");
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_date, C_DIM, 0);
    lv_obj_set_style_text_letter_space(lbl_date, 6, 0);

    // Helper text
    lbl_helper = lv_label_create(left);
    lv_label_set_text(lbl_helper, "Prisloni ključ za vstop/izstop...");
    lv_obj_set_style_text_font(lbl_helper, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_helper, C_WHITE, 0);
    lv_obj_set_style_text_letter_space(lbl_helper, 3, 0);
    lv_label_set_long_mode(lbl_helper, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_helper, 540);

    // --- Right (log panel) ---
    lv_obj_t *right = lv_obj_create(row);
    lv_obj_set_size(right, 230, LV_PCT(100));
    lv_obj_add_style(right, &st_screen, 0);
    lv_obj_set_style_border_side(right, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(right, C_BORDER, 0);
    lv_obj_set_style_border_width(right, 1, 0);
    lv_obj_set_style_pad_all(right, 12, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(right, 6, 0);

    cont_log = right;

    lv_obj_t *log_title = lv_label_create(right);
    lv_label_set_text(log_title, "ZADNJI VPISI");
    lv_obj_set_style_text_font(log_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(log_title, C_DIM, 0);
    lv_obj_set_style_text_letter_space(log_title, 5, 0);

    for (int i = 0; i < 7; i++) {
        lbl_log_entries[i] = lv_label_create(right);
        lv_label_set_text(lbl_log_entries[i], "");
        lv_obj_set_style_text_font(lbl_log_entries[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl_log_entries[i], C_WHITE, 0);
        lv_obj_add_flag(lbl_log_entries[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_long_mode(lbl_log_entries[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(lbl_log_entries[i], 200);
    }

    // Periodic log refresh timer
    log_timer = lv_timer_create(refresh_log_panel, LOG_PANEL_REFRESH_MS, nullptr);
}

// ===========================================================================
// SCREEN: GREETING + ACTIVITY
// ===========================================================================
static void cb_activity_click(lv_event_t *e)
{
    const char *act = (const char *)lv_event_get_user_data(e);
    Serial.printf("[UI] Activity selected: %s\n", act);

    if (g_active_session_id >= 0)
        storage_set_activity(g_active_session_id, String(act));

    g_logged_in = false;
    g_active_session_id = -1;

    show_screen(SCR_CLOCK);
    set_helper(g_current_user.gender == "female"
               ? "Vpisana si v FabLab."
               : "Vpisan si v FabLab.", C_WHITE, HELPER_MSG_DURATION_MS);

    // Refresh log panel immediately
    refresh_log_panel(nullptr);
}

static void build_greeting_screen()
{
    lv_obj_t *s = scr[SCR_GREETING];
    screen_base(s);

    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s, 24, 0);

    lbl_greeting = lv_label_create(s);
    lv_label_set_text(lbl_greeting, "");
    lv_obj_set_style_text_font(lbl_greeting, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_greeting, C_WHITE, 0);
    lv_label_set_long_mode(lbl_greeting, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_greeting, 720);
    lv_obj_set_style_text_align(lbl_greeting, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(lbl_greeting, 4, 0);

    // Activity grid (3 columns)
    cont_activity = lv_obj_create(s);
    lv_obj_add_style(cont_activity, &st_screen, 0);
    lv_obj_set_size(cont_activity, 720, LV_SIZE_CONTENT);

    static lv_coord_t col_dsc[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST
    };
    static lv_coord_t row_dsc[] = {
        70, 70, 70, LV_GRID_TEMPLATE_LAST
    };
    lv_obj_set_grid_dsc_array(cont_activity, col_dsc, row_dsc);
    lv_obj_set_style_pad_column(cont_activity, 10, 0);
    lv_obj_set_style_pad_row(cont_activity, 10, 0);

    for (int i = 0; i < ACTIVITY_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(cont_activity);
        lv_obj_set_size(btn, LV_GRID_CONTENT, LV_GRID_CONTENT);
        lv_obj_add_style(btn, &st_btn_crt, 0);
        lv_obj_set_grid_cell(btn,
            LV_GRID_ALIGN_STRETCH, i % 3, 1,
            LV_GRID_ALIGN_STRETCH, i / 3, 1);
        lv_obj_set_style_text_color(btn, C_WHITE, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(btn, C_WHITE, LV_STATE_PRESSED);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, ACTIVITIES[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, C_WHITE, 0);
        lv_obj_set_style_text_letter_space(lbl, 2, 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, cb_activity_click, LV_EVENT_CLICKED,
                            (void *)ACTIVITIES[i]);
    }
}

// ===========================================================================
// SCREEN: NAME-ENTRY
// ===========================================================================
static void cb_ne_submit(lv_event_t *e) { (void)e; submit_name_entry(); }
static void cb_ne_cancel(lv_event_t *e) { (void)e; cancel_name_entry(); }

static void cb_ne_kbd(lv_event_t *e)
{
    lv_obj_t *kbd = (lv_obj_t *)lv_event_get_target(e);
    lv_keyboard_key_t key = lv_keyboard_get_selected_btn(kbd);
    if (key == LV_KEYBOARD_KEY_OK || key == LV_KEYBOARD_KEY_ENTER)
        submit_name_entry();
}

static void build_name_entry_screen()
{
    lv_obj_t *s = scr[SCR_NAME_ENTRY];
    screen_base(s);
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s, 20, 0);

    lbl_ne_title = lv_label_create(s);
    lv_label_set_text(lbl_ne_title, "Vpiši ime in priimek");
    lv_obj_set_style_text_font(lbl_ne_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_ne_title, C_ORANGE, 0);
    lv_obj_set_style_text_letter_space(lbl_ne_title, 4, 0);
    lv_label_set_long_mode(lbl_ne_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_ne_title, 700);
    lv_obj_set_style_text_align(lbl_ne_title, LV_TEXT_ALIGN_CENTER, 0);

    // Text area
    ta_ne = lv_textarea_create(s);
    lv_obj_add_style(ta_ne, &st_ta_crt, 0);
    lv_obj_set_size(ta_ne, 500, 55);
    lv_textarea_set_placeholder_text(ta_ne, "Ime in priimek...");
    lv_textarea_set_max_length(ta_ne, 64);
    lv_textarea_set_one_line(ta_ne, true);

    // Button row
    lv_obj_t *row = lv_obj_create(s);
    lv_obj_add_style(row, &st_screen, 0);
    lv_obj_set_size(row, 500, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    btn_ne_submit = make_btn(row, "POTRDI",  true,  220, 55, cb_ne_submit, nullptr);
    btn_ne_cancel = make_btn(row, "PREKLIČI", false, 220, 55, cb_ne_cancel, nullptr);
    lv_obj_add_style(btn_ne_cancel, &st_btn_danger, 0);

    // On-screen keyboard
    kbd_ne = lv_keyboard_create(s);
    lv_keyboard_set_textarea(kbd_ne, ta_ne);
    lv_obj_set_size(kbd_ne, LV_PCT(100), 220);
    lv_obj_set_style_bg_color(kbd_ne, C_BG, 0);
    lv_obj_set_style_text_color(kbd_ne, C_WHITE, 0);
    lv_obj_add_event_cb(kbd_ne, cb_ne_kbd, LV_EVENT_VALUE_CHANGED, nullptr);
}

// ---------------------------------------------------------------------------
// Name-entry logic
// ---------------------------------------------------------------------------
static void update_ne_ui()
{
    if (g_ne_mode == NE_CONFIRM_CREATE) {
        lv_label_set_text(lbl_ne_title,
            "ID ključa ni povezan z računom.\nUstvari račun?");
        lv_obj_add_flag(ta_ne,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(kbd_ne, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lv_obj_get_child(btn_ne_submit, 0), "DA");
        lv_label_set_text(lv_obj_get_child(btn_ne_cancel, 0), "NE");
    } else if (g_ne_mode == NE_COLLECT_NAME) {
        lv_label_set_text(lbl_ne_title, "Vpiši ime in priimek");
        lv_obj_clear_flag(ta_ne,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(kbd_ne, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(ta_ne, "");
        lv_label_set_text(lv_obj_get_child(btn_ne_submit, 0), "POTRDI");
        lv_label_set_text(lv_obj_get_child(btn_ne_cancel, 0), "PREKLIČI");
    } else if (g_ne_mode == NE_COLLECT_GENDER) {
        lv_label_set_text(lbl_ne_title, "Izberi spol");
        lv_obj_add_flag(ta_ne,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(kbd_ne, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lv_obj_get_child(btn_ne_submit, 0), "ŽENSKA");
        lv_label_set_text(lv_obj_get_child(btn_ne_cancel, 0), "MOŠKI");
    }
}

static void submit_name_entry()
{
    if (g_pending_nfc_id.isEmpty()) { show_screen(SCR_CLOCK); return; }

    if (g_ne_mode == NE_CONFIRM_CREATE) {
        g_ne_mode = NE_COLLECT_NAME;
        update_ne_ui();
        return;
    }
    if (g_ne_mode == NE_COLLECT_NAME) {
        String full = String(lv_textarea_get_text(ta_ne));
        full.trim();
        // Require at least "Name Surname"
        int sp = full.indexOf(' ');
        if (sp < 1 || sp == (int)full.length() - 1) {
            set_helper("Vnesi ime in priimek.", C_ORANGE, 1800);
            return;
        }
        g_ne_full_name = full;
        g_ne_mode = NE_COLLECT_GENDER;
        update_ne_ui();
        return;
    }
    if (g_ne_mode == NE_COLLECT_GENDER) {
        register_pending_user(g_ne_full_name, "female");
        return;
    }
}

static void cancel_name_entry()
{
    if (g_ne_mode == NE_CONFIRM_CREATE) {
        g_pending_nfc_id = "";
        g_nfc_flow_active = false;
        g_ne_mode = NE_CONFIRM_CREATE;
        show_screen(SCR_CLOCK);
        set_helper("Prisloni ključ za vstop/izstop...", C_WHITE, 0);
        return;
    }
    if (g_ne_mode == NE_COLLECT_GENDER) {
        register_pending_user(g_ne_full_name, "male");
        return;
    }
    // COLLECT_NAME cancel
    g_pending_nfc_id = "";
    g_nfc_flow_active = false;
    g_ne_mode = NE_CONFIRM_CREATE;
    if (g_admin_logged_in) {
        show_screen(SCR_ADMIN);
    } else {
        show_screen(SCR_CLOCK);
    }
}

static void register_pending_user(const String &full_name, const char *gender)
{
    // Split "Ime Priimek"
    int sp = full_name.indexOf(' ');
    String name    = full_name.substring(0, sp);
    String surname = full_name.substring(sp + 1);
    surname.trim();

    User u;
    u.nfc_id        = g_pending_nfc_id;
    u.name          = name;
    u.surname       = surname;
    u.gender        = String(gender);
    u.registered_at = iso_now_str();
    u.last_seen     = iso_now_str();
    u.scan_count    = 1;

    bool ok = storage_create_user(u);
    g_pending_nfc_id = "";
    g_ne_mode = NE_CONFIRM_CREATE;

    if (!ok) {
        if (g_admin_logged_in) {
            show_screen(SCR_ADMIN);
            show_admin_status("Napaka pri registraciji.", true);
        } else {
            show_screen(SCR_CLOCK);
            set_helper("Napaka pri registraciji.", C_RED, HELPER_MSG_DURATION_MS);
        }
        g_nfc_flow_active = false;
        return;
    }

    Serial.printf("[UI] User registered: %s %s (%s)\n",
                  name.c_str(), surname.c_str(), u.nfc_id.c_str());

    if (g_admin_logged_in || g_ne_admin_ctx) {
        g_nfc_flow_active = false;
        show_screen(SCR_ADMIN);
        char msg[64];
        snprintf(msg, sizeof(msg), "Dodan: %s %s",
                 name.c_str(), surname.c_str());
        show_admin_status(msg, false);
        return;
    }

    // Normal flow: auto-login the newly created user
    set_helper(build_full_name(u).c_str(), C_WHITE, 0);
    do_login(u);
    g_nfc_flow_active = false;
}

// ===========================================================================
// SCREEN: ADMIN PASSWORD
// ===========================================================================
static void cb_ap_submit(lv_event_t *e)
{
    (void)e;
    AppSettings s = storage_load_settings();
    String entered = String(lv_textarea_get_text(ta_ap));
    if (entered != s.admin_password) {
        lv_textarea_set_text(ta_ap, "");
        set_helper("Napačno geslo!", C_RED, HELPER_MSG_DURATION_MS);
        Serial.println("[ADMIN] Wrong password attempt.");
        return;
    }
    g_admin_password  = entered;
    g_admin_logged_in = true;
    Serial.println("[ADMIN] Admin logged in.");
    show_admin_view(AV_MENU);
    show_screen(SCR_ADMIN);
}

static void cb_ap_cancel(lv_event_t *e)
{
    (void)e;
    lv_textarea_set_text(ta_ap, "");
    show_screen(SCR_CLOCK);
}

static void cb_ap_kbd(lv_event_t *e)
{
    lv_obj_t *kbd = (lv_obj_t *)lv_event_get_target(e);
    lv_keyboard_key_t key = lv_keyboard_get_selected_btn(kbd);
    if (key == LV_KEYBOARD_KEY_OK || key == LV_KEYBOARD_KEY_ENTER)
        cb_ap_submit(e);
}

static void build_admin_pass_screen()
{
    lv_obj_t *s = scr[SCR_ADMIN_PASS];
    screen_base(s);
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s, 20, 0);

    lv_obj_t *t = lv_label_create(s);
    lv_label_set_text(t, "ADMIN MODE");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(t, C_ORANGE, 0);
    lv_obj_set_style_text_letter_space(t, 10, 0);

    lv_obj_t *hint = lv_label_create(s);
    lv_label_set_text(hint, "Vnesi geslo za dostop");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hint, C_DIM, 0);

    ta_ap = lv_textarea_create(s);
    lv_obj_add_style(ta_ap, &st_ta_crt, 0);
    lv_obj_set_size(ta_ap, 440, 55);
    lv_textarea_set_placeholder_text(ta_ap, "Geslo...");
    lv_textarea_set_password_mode(ta_ap, true);
    lv_textarea_set_max_length(ta_ap, 64);
    lv_textarea_set_one_line(ta_ap, true);

    lv_obj_t *row = lv_obj_create(s);
    lv_obj_add_style(row, &st_screen, 0);
    lv_obj_set_size(row, 440, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_btn(row, "VSTOPI",  true,  200, 55, cb_ap_submit, nullptr);
    lv_obj_t *cancel = make_btn(row, "PREKLIČI", false, 200, 55, cb_ap_cancel, nullptr);
    lv_obj_add_style(cancel, &st_btn_danger, 0);

    kbd_ap = lv_keyboard_create(s);
    lv_keyboard_set_textarea(kbd_ap, ta_ap);
    lv_obj_set_size(kbd_ap, LV_PCT(100), 220);
    lv_obj_set_style_bg_color(kbd_ap, C_BG, 0);
    lv_obj_set_style_text_color(kbd_ap, C_WHITE, 0);
    lv_obj_add_event_cb(kbd_ap, cb_ap_kbd, LV_EVENT_VALUE_CHANGED, nullptr);
}

// ===========================================================================
// SCREEN: ADMIN PANEL
// ===========================================================================
static void cb_admin_exit(lv_event_t *e)
{
    (void)e;
    g_admin_logged_in = false;
    g_admin_password  = "";
    g_admin_add_mode  = false;
    Serial.println("[ADMIN] Session ended.");
    show_screen(SCR_CLOCK);
}

static void cb_admin_adduser(lv_event_t *e)
{
    (void)e;
    g_admin_add_mode = true;
    lv_label_set_text(lbl_adduser_status, "PRISLONI KARTO ZA DODAJANJE...");
    show_admin_view(AV_ADDUSER);
    Serial.println("[ADMIN] Waiting for NFC to register...");
}

static void cb_admin_adduser_cancel(lv_event_t *e)
{
    (void)e;
    g_admin_add_mode = false;
    show_admin_view(AV_MENU);
}

static void cb_admin_deluser(lv_event_t *e)
{
    (void)e;
    show_admin_view(AV_DELUSER);

    lv_obj_clean(list_deluser);
    auto users = storage_list_users();
    if (users.empty()) {
        lv_obj_t *lbl = lv_list_add_text(list_deluser, "Ni registriranih kartic.");
        lv_obj_set_style_text_color(lbl, C_DIM, 0);
        return;
    }
    for (auto &u : users) {
        String display = build_full_name(u) + "  [" + u.nfc_id + "]";
        lv_obj_t *btn = lv_list_add_btn(list_deluser, nullptr, display.c_str());
        lv_obj_add_style(btn, &st_btn_crt, 0);
        lv_obj_set_style_text_color(btn, C_RED, 0);
        lv_obj_set_user_data(btn, (void *)strdup(u.nfc_id.c_str()));
        lv_obj_add_event_cb(btn, [](lv_event_t *ev) {
            const char *nfc_id = (const char *)lv_obj_get_user_data(
                (lv_obj_t *)lv_event_get_target(ev));
            if (!nfc_id) return;
            storage_delete_user(String(nfc_id));
            lv_obj_del((lv_obj_t *)lv_event_get_target(ev));
            char msg[64];
            snprintf(msg, sizeof(msg), "Kartica %s izbrisana.", nfc_id);
            show_admin_status(msg, false);
            Serial.printf("[ADMIN] Deleted user: %s\n", nfc_id);
        }, LV_EVENT_CLICKED, nullptr);
    }
}

static void cb_admin_logs(lv_event_t *e)
{
    (void)e;
    g_admin_logs_page = 1;
    show_admin_view(AV_LOGS);

    // Populate table
    lv_table_set_row_cnt(table_logs, 1);
    int total = 0;
    auto sessions = storage_list_sessions(g_admin_logs_page, 20, total);
    int pages = (total + 19) / 20;

    lv_table_set_row_cnt(table_logs, (lv_coord_t)(sessions.size() + 1));
    // Header row 0
    lv_table_set_cell_value(table_logs, 0, 0, "#");
    lv_table_set_cell_value(table_logs, 0, 1, "Ime");
    lv_table_set_cell_value(table_logs, 0, 2, "Datum");
    lv_table_set_cell_value(table_logs, 0, 3, "Vstop");
    lv_table_set_cell_value(table_logs, 0, 4, "Izstop");
    lv_table_set_cell_value(table_logs, 0, 5, "Aktivnost");

    for (int i = 0; i < (int)sessions.size(); i++) {
        const Session &s = sessions[i];
        char id_str[12]; snprintf(id_str, sizeof(id_str), "%d", s.id);
        lv_table_set_cell_value(table_logs, i + 1, 0, id_str);
        lv_table_set_cell_value(table_logs, i + 1, 1, s.name.substring(0,14).c_str());
        lv_table_set_cell_value(table_logs, i + 1, 2, s.date.c_str());

        // Login HH:MM
        char lt[8] = "--:--";
        if (s.login_time.length() >= 16)
            snprintf(lt, sizeof(lt), "%s", s.login_time.substring(11, 16).c_str());
        lv_table_set_cell_value(table_logs, i + 1, 3, lt);

        // Logout HH:MM
        char lo[8] = "—";
        if (s.logout_time.length() >= 16)
            snprintf(lo, sizeof(lo), "%s", s.logout_time.substring(11, 16).c_str());
        lv_table_set_cell_value(table_logs, i + 1, 4, lo);

        lv_table_set_cell_value(table_logs, i + 1, 5,
            s.activity.isEmpty() ? "—" : s.activity.c_str());
    }

    char page_str[24];
    snprintf(page_str, sizeof(page_str), "%d / %d", g_admin_logs_page, pages);
    lv_label_set_text(lbl_logs_page, page_str);
}

static void cb_admin_export(lv_event_t *e)
{
    (void)e;
    // On the embedded device, "export" saves a CSV to LittleFS /export.csv
    String csv = storage_export_csv();
    File f = LittleFS.open("/export.csv", "w");
    if (f) { f.print(csv); f.close(); }
    // Browser clients can download via GET /api/sessions/export
    show_admin_status("CSV shranjen na /export.csv  |  Prenesi prek /api/sessions/export", false);
    Serial.println("[ADMIN] CSV exported to LittleFS /export.csv");
}

static void build_admin_screen()
{
    lv_obj_t *s = scr[SCR_ADMIN];
    screen_base(s);
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_STRETCH, LV_FLEX_ALIGN_STRETCH);

    // Header
    lv_obj_t *header = lv_obj_create(s);
    lv_obj_add_style(header, &st_screen, 0);
    lv_obj_set_size(header, LV_PCT(100), 50);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, C_BORDER, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(header, 20, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "ADMIN PANEL");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, C_ORANGE, 0);
    lv_obj_set_style_text_letter_space(title, 8, 0);

    lv_obj_t *exit_btn = make_btn(header, "✕ IZHOD", false, 140, 36,
                                   cb_admin_exit, nullptr);
    lv_obj_add_style(exit_btn, &st_btn_danger, 0);

    // Status bar
    lbl_admin_status = lv_label_create(s);
    lv_label_set_text(lbl_admin_status, "");
    lv_obj_set_style_text_font(lbl_admin_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_hor(lbl_admin_status, 20, 0);
    lv_obj_set_style_pad_ver(lbl_admin_status, 4, 0);
    lv_obj_add_flag(lbl_admin_status, LV_OBJ_FLAG_HIDDEN);

    // Content area
    lv_obj_t *content = lv_obj_create(s);
    lv_obj_add_style(content, &st_screen, 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // === MENU view ===
    cont_menu = lv_obj_create(content);
    lv_obj_add_style(cont_menu, &st_screen, 0);
    lv_obj_set_size(cont_menu, 640, LV_SIZE_CONTENT);
    static lv_coord_t menu_col[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t menu_row[] = { 110, 110, LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(cont_menu, menu_col, menu_row);
    lv_obj_set_style_pad_all(cont_menu, 12, 0);

    const char *menu_labels[] = { "＋ DODAJ KARTO", "✕ IZBRIŠI KARTO",
                                   "≡ VPISI DNEVNIK", "↓ IZVOZI CSV" };
    lv_event_cb_t menu_cbs[]  = { cb_admin_adduser, cb_admin_deluser,
                                   cb_admin_logs,    cb_admin_export };

    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_btn_create(cont_menu);
        lv_obj_add_style(btn, &st_btn_crt, 0);
        lv_obj_set_grid_cell(btn,
            LV_GRID_ALIGN_STRETCH, i % 2, 1,
            LV_GRID_ALIGN_STRETCH, i / 2, 1);
        lv_obj_set_style_text_color(btn, C_ORANGE, LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(btn, C_ORANGE, LV_STATE_FOCUSED);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, menu_labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lbl, C_WHITE, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, menu_cbs[i], LV_EVENT_CLICKED, nullptr);
    }

    // === ADD-USER view ===
    cont_adduser = lv_obj_create(content);
    lv_obj_add_style(cont_adduser, &st_screen, 0);
    lv_obj_set_size(cont_adduser, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_adduser, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_adduser, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cont_adduser, 20, 0);
    lv_obj_add_flag(cont_adduser, LV_OBJ_FLAG_HIDDEN);

    lbl_adduser_status = lv_label_create(cont_adduser);
    lv_label_set_text(lbl_adduser_status, "PRISLONI KARTO ZA DODAJANJE...");
    lv_obj_set_style_text_font(lbl_adduser_status, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_adduser_status, C_ORANGE, 0);

    make_btn(cont_adduser, "PREKLIČI", false, 220, 55,
             cb_admin_adduser_cancel, nullptr);

    // === DELETE-USER view ===
    cont_deluser = lv_obj_create(content);
    lv_obj_add_style(cont_deluser, &st_screen, 0);
    lv_obj_set_size(cont_deluser, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_deluser, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_deluser, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cont_deluser, 8, 0);
    lv_obj_add_flag(cont_deluser, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *du_title = lv_label_create(cont_deluser);
    lv_label_set_text(du_title, "REGISTRIRANE KARTICE");
    lv_obj_set_style_text_font(du_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(du_title, C_ORANGE, 0);

    list_deluser = lv_list_create(cont_deluser);
    lv_obj_add_style(list_deluser, &st_list_crt, 0);
    lv_obj_set_size(list_deluser, 720, 280);

    lv_obj_t *back_du = make_btn(cont_deluser, "NAZAJ", false, 220, 50,
        [](lv_event_t *) { show_admin_view(AV_MENU); }, nullptr);
    (void)back_du;

    // === LOGS view ===
    cont_logs = lv_obj_create(content);
    lv_obj_add_style(cont_logs, &st_screen, 0);
    lv_obj_set_size(cont_logs, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont_logs, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_logs, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cont_logs, 8, 0);
    lv_obj_add_flag(cont_logs, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *log_title2 = lv_label_create(cont_logs);
    lv_label_set_text(log_title2, "VPISI DNEVNIK");
    lv_obj_set_style_text_font(log_title2, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(log_title2, C_ORANGE, 0);

    table_logs = lv_table_create(cont_logs);
    lv_obj_add_style(table_logs, &st_table_crt, 0);
    lv_obj_set_size(table_logs, 760, 300);
    lv_table_set_col_cnt(table_logs, 6);
    lv_table_set_col_width(table_logs, 0, 50);
    lv_table_set_col_width(table_logs, 1, 140);
    lv_table_set_col_width(table_logs, 2, 100);
    lv_table_set_col_width(table_logs, 3, 70);
    lv_table_set_col_width(table_logs, 4, 70);
    lv_table_set_col_width(table_logs, 5, 130);

    // Navigation row
    lv_obj_t *nav = lv_obj_create(cont_logs);
    lv_obj_add_style(nav, &st_screen, 0);
    lv_obj_set_size(nav, 400, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nav, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_btn(nav, "◄", false, 70, 44, [](lv_event_t *) {
        if (g_admin_logs_page > 1) { g_admin_logs_page--; cb_admin_logs(nullptr); }
    }, nullptr);

    lbl_logs_page = lv_label_create(nav);
    lv_label_set_text(lbl_logs_page, "1 / 1");
    lv_obj_set_style_text_color(lbl_logs_page, C_WHITE, 0);

    make_btn(nav, "►", false, 70, 44, [](lv_event_t *) {
        g_admin_logs_page++;
        cb_admin_logs(nullptr);
    }, nullptr);

    make_btn(nav, "NAZAJ", false, 120, 44, [](lv_event_t *) {
        show_admin_view(AV_MENU);
    }, nullptr);
}

// ===========================================================================
// NFC flow
// ===========================================================================
static void do_login(const User &user)
{
    String now   = iso_now_str();
    String today = today_key_str();
    int sid = storage_create_session(user.nfc_id, build_full_name(user), now, today);
    storage_update_user_seen(user.nfc_id, now);
    g_active_session_id = sid;
    g_logged_in         = true;
    g_current_user      = user;
    Serial.printf("[LOGIN] %s (sid=%d)\n", build_full_name(user).c_str(), sid);
    show_greeting(user);
    refresh_log_panel(nullptr);
}

static void do_logout(const User &user, int session_id)
{
    String now = iso_now_str();
    int dur = 0;
    storage_close_session(session_id, now, dur);
    g_logged_in         = false;
    g_active_session_id = -1;
    Serial.printf("[LOGOUT] %s — duration: %ds\n",
                  build_full_name(user).c_str(), dur);
    show_screen(SCR_CLOCK);
    const char *bye_msg = (user.gender == "female")
        ? "Izpisana si. Nasvidenje!"
        : "Izpisan si. Nasvidenje!";
    set_helper(bye_msg, C_WHITE, HELPER_MSG_DURATION_MS);
    refresh_log_panel(nullptr);
}

static void show_greeting(const User &user)
{
    char buf[80];
    snprintf(buf, sizeof(buf), "Živjo %s.\nKaj delaš danes?",
             user.name.c_str());
    lv_label_set_text(lbl_greeting, buf);
    show_screen(SCR_GREETING);
}

static void handle_nfc_read(const char *nfc_id)
{
    if (g_nfc_flow_active) {
        Serial.println("[NFC] Ignoring — previous flow active.");
        return;
    }

    String norm_id = nfc_normalise_id(String(nfc_id));
    if (norm_id.isEmpty()) return;

    Serial.printf("[NFC] Card: %s\n", norm_id.c_str());

    // Admin "add user" mode
    if (g_admin_add_mode) {
        g_admin_add_mode = false;
        User existing;
        if (storage_find_user(norm_id, existing)) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Kartica že registrirana: %s",
                     build_full_name(existing).c_str());
            show_admin_status(msg, true);
            show_admin_view(AV_MENU);
            return;
        }
        g_pending_nfc_id = norm_id;
        g_ne_mode        = NE_COLLECT_NAME;
        g_ne_admin_ctx   = true;
        update_ne_ui();
        show_screen(SCR_NAME_ENTRY);
        return;
    }

    // If activity screen is active, ignore
    if (lv_scr_act() == scr[SCR_GREETING]) {
        Serial.println("[NFC] Ignoring — activity screen active.");
        return;
    }

    g_nfc_flow_active = true;

    // Look up user
    User user;
    if (!storage_find_user(norm_id, user)) {
        Serial.printf("[NFC] Unknown card: %s\n", norm_id.c_str());
        g_pending_nfc_id = norm_id;
        g_ne_mode        = NE_CONFIRM_CREATE;
        g_ne_admin_ctx   = false;
        update_ne_ui();
        show_screen(SCR_NAME_ENTRY);
        // nfc_flow_active stays true until cancel/register clears it
        return;
    }

    // Check for open session today
    Session active;
    if (storage_find_active_session(norm_id, today_key_str(), active)) {
        set_helper(build_full_name(user).c_str(), C_ORANGE, 0);
        do_logout(user, active.id);
    } else {
        set_helper(build_full_name(user).c_str(), C_WHITE, 0);
        do_login(user);
    }
    g_nfc_flow_active = false;
}

// ===========================================================================
// Public API
// ===========================================================================
void ui_init()
{
    styles_init();

    // Create one screen per SCR_xxx constant
    for (int i = 0; i < 7; i++) {
        scr[i] = lv_obj_create(nullptr);  // bare screen object
    }

    build_start_screen();
    build_intro_screen();
    build_clock_screen();
    build_greeting_screen();
    build_name_entry_screen();
    build_admin_pass_screen();
    build_admin_screen();

    // Load settings
    g_settings = storage_load_settings();

    // Start on the start screen
    lv_scr_load(scr[SCR_START]);

    // Trigger an initial log panel populate (after WiFi / time is up)
    refresh_log_panel(nullptr);

    Serial.println("[UI] All screens built — showing START screen.");
}

void ui_clock_tick()
{
    if (!lbl_clock || !lbl_date) return;

    time_t t = time(nullptr);
    struct tm *tm = localtime(&t);

    // Time: HH:MM:SS
    char tbuf[12];
    snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d",
             tm->tm_hour, tm->tm_min, tm->tm_sec);
    lv_label_set_text(lbl_clock, tbuf);

    // Date: "Ponedeljek, 1. Jan 2025"
    static const char *DAYS[]   = { "Nedelja","Ponedeljek","Torek","Sreda",
                                     "Četrtek","Petek","Sobota" };
    static const char *MONTHS[] = { "Jan","Feb","Mar","Apr","Maj","Jun",
                                     "Jul","Avg","Sep","Okt","Nov","Dec" };
    char dbuf[64];
    snprintf(dbuf, sizeof(dbuf), "%s, %d. %s %d",
             DAYS[tm->tm_wday], tm->tm_mday,
             MONTHS[tm->tm_mon], tm->tm_year + 1900);
    lv_label_set_text(lbl_date, dbuf);
}

void ui_nfc_scan(const char *normalised_uid)
{
    handle_nfc_read(normalised_uid);
}
