// =============================================================================
// FabLab Presence System — Storage layer header
// LittleFS + ArduinoJson — users, sessions, settings
// =============================================================================

#pragma once
#include <Arduino.h>
#include <vector>

// ---------------------------------------------------------------------------
// Data structures (mirror the Node.js/SQLite schema)
// ---------------------------------------------------------------------------
struct User {
    String nfc_id;
    String name;
    String surname;
    String gender;          // "male" | "female"
    String registered_at;   // ISO-8601
    String last_seen;       // ISO-8601 or ""
    int    scan_count = 0;
};

struct Session {
    int    id          = 0;
    String nfc_id;
    String name;
    String date;            // YYYY-MM-DD
    String login_time;      // ISO-8601
    String logout_time;     // ISO-8601 or ""
    String activity;        // user-selected activity or ""
    int    duration_sec = 0;
};

struct AppSettings {
    bool   animations_enabled = true;
    bool   log_panel_enabled  = true;
    String admin_password     = "admin";
    String wifi_ssid;
    String wifi_password;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
bool storage_init();                    // Mount LittleFS, create default files

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
AppSettings storage_load_settings();
bool        storage_save_settings(const AppSettings &s);

// ---------------------------------------------------------------------------
// Users
// ---------------------------------------------------------------------------
bool              storage_find_user(const String &nfc_id, User &out);
bool              storage_create_user(const User &u);
bool              storage_delete_user(const String &nfc_id);
bool              storage_update_user_seen(const String &nfc_id,
                                           const String &iso_now);
std::vector<User> storage_list_users();   // sorted surname, name

// ---------------------------------------------------------------------------
// Sessions
// ---------------------------------------------------------------------------
// Returns new session id (≥1) or -1 on error
int  storage_create_session(const String &nfc_id, const String &name,
                             const String &login_iso, const String &date);
bool storage_set_activity(int session_id, const String &activity);
// Fills duration_sec and returns true on success
bool storage_close_session(int session_id, const String &logout_iso,
                            int &duration_sec_out);
bool              storage_find_active_session(const String &nfc_id,
                                              const String &date,
                                              Session &out);
std::vector<Session> storage_recent_sessions(int limit);
std::vector<Session> storage_list_sessions(int page, int per, int &total_out);
String               storage_export_csv();

void storage_purge_old_sessions();      // remove sessions older than 30 days
