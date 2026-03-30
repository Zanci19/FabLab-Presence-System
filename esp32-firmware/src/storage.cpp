// =============================================================================
// FabLab Presence System — Storage layer (LittleFS + ArduinoJson 7)
// =============================================================================

#include "storage.h"
#include "config.h"

#include <LittleFS.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <time.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static JsonDocument load_json(const char *path)
{
    JsonDocument doc;
    File f = LittleFS.open(path, "r");
    if (!f) return doc;
    deserializeJson(doc, f);
    f.close();
    return doc;
}

static bool save_json(const char *path, const JsonDocument &doc)
{
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
}

// ---------------------------------------------------------------------------
// storage_init
// ---------------------------------------------------------------------------
bool storage_init()
{
    if (!LittleFS.begin(true)) {
        Serial.println("[STORAGE] LittleFS mount failed — formatting…");
        return false;
    }
    Serial.println("[STORAGE] LittleFS mounted OK.");

    // Create default files if missing
    if (!LittleFS.exists(STORAGE_USERS_PATH)) {
        JsonDocument doc;
        doc.to<JsonArray>();
        save_json(STORAGE_USERS_PATH, doc);
    }
    if (!LittleFS.exists(STORAGE_SESSIONS_PATH)) {
        JsonDocument doc;
        doc.to<JsonArray>();
        save_json(STORAGE_SESSIONS_PATH, doc);
    }
    if (!LittleFS.exists(STORAGE_SETTINGS_PATH)) {
        JsonDocument doc;
        doc["animationsEnabled"] = true;
        doc["logPanelEnabled"]   = true;
        doc["adminPassword"]     = DEFAULT_ADMIN_PASSWORD;
        doc["wifiSSID"]          = WIFI_SSID;
        doc["wifiPassword"]      = WIFI_PASS;
        save_json(STORAGE_SETTINGS_PATH, doc);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
AppSettings storage_load_settings()
{
    AppSettings s;
    s.wifi_ssid     = WIFI_SSID;
    s.wifi_password = WIFI_PASS;

    JsonDocument doc = load_json(STORAGE_SETTINGS_PATH);
    if (doc.isNull()) return s;

    s.animations_enabled = doc["animationsEnabled"] | true;
    s.log_panel_enabled  = doc["logPanelEnabled"]   | true;
    s.admin_password     = doc["adminPassword"] | DEFAULT_ADMIN_PASSWORD;
    if (doc["wifiSSID"].is<const char*>())
        s.wifi_ssid = doc["wifiSSID"].as<String>();
    if (doc["wifiPassword"].is<const char*>())
        s.wifi_password = doc["wifiPassword"].as<String>();
    return s;
}

bool storage_save_settings(const AppSettings &s)
{
    JsonDocument doc;
    doc["animationsEnabled"] = s.animations_enabled;
    doc["logPanelEnabled"]   = s.log_panel_enabled;
    doc["adminPassword"]     = s.admin_password;
    doc["wifiSSID"]          = s.wifi_ssid;
    doc["wifiPassword"]      = s.wifi_password;
    return save_json(STORAGE_SETTINGS_PATH, doc);
}

// ---------------------------------------------------------------------------
// Users — helpers
// ---------------------------------------------------------------------------
static void user_from_json(const JsonObject &obj, User &u)
{
    u.nfc_id        = obj["nfc_id"]        | "";
    u.name          = obj["name"]          | "";
    u.surname       = obj["surname"]       | "";
    u.gender        = obj["gender"]        | "male";
    u.registered_at = obj["registered_at"] | "";
    u.last_seen     = obj["last_seen"]     | "";
    u.scan_count    = obj["scan_count"]    | 0;
}

static void user_to_json(JsonObject &obj, const User &u)
{
    obj["nfc_id"]        = u.nfc_id;
    obj["name"]          = u.name;
    obj["surname"]       = u.surname;
    obj["gender"]        = u.gender;
    obj["registered_at"] = u.registered_at;
    obj["last_seen"]     = u.last_seen;
    obj["scan_count"]    = u.scan_count;
}

// ---------------------------------------------------------------------------
// Users — public API
// ---------------------------------------------------------------------------
bool storage_find_user(const String &nfc_id, User &out)
{
    JsonDocument doc = load_json(STORAGE_USERS_PATH);
    for (JsonObject obj : doc.as<JsonArray>()) {
        if (obj["nfc_id"].as<String>() == nfc_id) {
            user_from_json(obj, out);
            return true;
        }
    }
    return false;
}

bool storage_create_user(const User &u)
{
    JsonDocument doc = load_json(STORAGE_USERS_PATH);
    JsonArray arr = doc.as<JsonArray>();
    // Reject duplicates
    for (JsonObject obj : arr) {
        if (obj["nfc_id"].as<String>() == u.nfc_id) return false;
    }
    JsonObject obj = arr.add<JsonObject>();
    user_to_json(obj, u);
    return save_json(STORAGE_USERS_PATH, doc);
}

bool storage_delete_user(const String &nfc_id)
{
    JsonDocument doc = load_json(STORAGE_USERS_PATH);
    JsonArray arr = doc.as<JsonArray>();
    JsonDocument newDoc;
    JsonArray newArr = newDoc.to<JsonArray>();
    bool found = false;
    for (JsonObject obj : arr) {
        if (obj["nfc_id"].as<String>() == nfc_id) { found = true; continue; }
        newArr.add(obj);
    }
    if (!found) return false;
    return save_json(STORAGE_USERS_PATH, newDoc);
}

bool storage_update_user_seen(const String &nfc_id, const String &iso_now)
{
    JsonDocument doc = load_json(STORAGE_USERS_PATH);
    for (JsonObject obj : doc.as<JsonArray>()) {
        if (obj["nfc_id"].as<String>() == nfc_id) {
            obj["last_seen"]  = iso_now;
            obj["scan_count"] = (int)(obj["scan_count"] | 0) + 1;
            return save_json(STORAGE_USERS_PATH, doc);
        }
    }
    return false;
}

std::vector<User> storage_list_users()
{
    JsonDocument doc = load_json(STORAGE_USERS_PATH);
    std::vector<User> users;
    for (JsonObject obj : doc.as<JsonArray>()) {
        User u;
        user_from_json(obj, u);
        users.push_back(u);
    }
    std::sort(users.begin(), users.end(), [](const User &a, const User &b) {
        int cmp = a.surname.compareTo(b.surname);
        return cmp != 0 ? cmp < 0 : a.name.compareTo(b.name) < 0;
    });
    return users;
}

// ---------------------------------------------------------------------------
// Sessions — helpers
// ---------------------------------------------------------------------------
static void session_from_json(const JsonObject &obj, Session &s)
{
    s.id           = obj["id"]           | 0;
    s.nfc_id       = obj["nfc_id"]       | "";
    s.name         = obj["name"]         | "";
    s.date         = obj["date"]         | "";
    s.login_time   = obj["login_time"]   | "";
    s.logout_time  = obj["logout_time"]  | "";
    s.activity     = obj["activity"]     | "";
    s.duration_sec = obj["duration_sec"] | 0;
}

static void session_to_json(JsonObject &obj, const Session &s)
{
    obj["id"]           = s.id;
    obj["nfc_id"]       = s.nfc_id;
    obj["name"]         = s.name;
    obj["date"]         = s.date;
    obj["login_time"]   = s.login_time;
    obj["logout_time"]  = s.logout_time;
    obj["activity"]     = s.activity;
    obj["duration_sec"] = s.duration_sec;
}

static int next_session_id(const JsonArray &arr)
{
    int max_id = 0;
    for (JsonObject obj : arr) {
        int id = obj["id"] | 0;
        if (id > max_id) max_id = id;
    }
    return max_id + 1;
}

// ---------------------------------------------------------------------------
// Sessions — public API
// ---------------------------------------------------------------------------
int storage_create_session(const String &nfc_id, const String &name,
                            const String &login_iso, const String &date)
{
    JsonDocument doc = load_json(STORAGE_SESSIONS_PATH);
    JsonArray arr = doc.as<JsonArray>();

    Session s;
    s.id         = next_session_id(arr);
    s.nfc_id     = nfc_id;
    s.name       = name;
    s.date       = date;
    s.login_time = login_iso;

    // Enforce MAX_SESSIONS cap: remove oldest entries if needed
    while ((int)arr.size() >= MAX_SESSIONS) {
        arr.remove(0);
    }

    JsonObject obj = arr.add<JsonObject>();
    session_to_json(obj, s);
    if (!save_json(STORAGE_SESSIONS_PATH, doc)) return -1;
    return s.id;
}

bool storage_set_activity(int session_id, const String &activity)
{
    JsonDocument doc = load_json(STORAGE_SESSIONS_PATH);
    for (JsonObject obj : doc.as<JsonArray>()) {
        if ((int)(obj["id"] | 0) == session_id) {
            obj["activity"] = activity;
            return save_json(STORAGE_SESSIONS_PATH, doc);
        }
    }
    return false;
}

bool storage_close_session(int session_id, const String &logout_iso,
                            int &duration_sec_out)
{
    JsonDocument doc = load_json(STORAGE_SESSIONS_PATH);
    for (JsonObject obj : doc.as<JsonArray>()) {
        if ((int)(obj["id"] | 0) == session_id) {
            // Calculate duration
            String login_str = obj["login_time"] | "";
            duration_sec_out = 0;
            if (!login_str.isEmpty()) {
                struct tm tm_login = {}, tm_logout = {};
                strptime(login_str.c_str(),  "%Y-%m-%dT%H:%M:%S", &tm_login);
                strptime(logout_iso.c_str(), "%Y-%m-%dT%H:%M:%S", &tm_logout);
                time_t t1 = mktime(&tm_login);
                time_t t2 = mktime(&tm_logout);
                if (t2 > t1) duration_sec_out = (int)(t2 - t1);
            }
            obj["logout_time"]  = logout_iso;
            obj["duration_sec"] = duration_sec_out;
            return save_json(STORAGE_SESSIONS_PATH, doc);
        }
    }
    return false;
}

bool storage_find_active_session(const String &nfc_id, const String &date,
                                  Session &out)
{
    JsonDocument doc = load_json(STORAGE_SESSIONS_PATH);
    // Scan in reverse to find the most recent open session for today
    JsonArray arr = doc.as<JsonArray>();
    int last_idx = -1;
    int idx = 0;
    for (JsonObject obj : arr) {
        if (obj["nfc_id"].as<String>() == nfc_id &&
            obj["date"].as<String>()   == date   &&
            obj["logout_time"].as<String>().isEmpty()) {
            last_idx = idx;
        }
        idx++;
    }
    if (last_idx < 0) return false;
    session_from_json(arr[last_idx], out);
    return true;
}

std::vector<Session> storage_recent_sessions(int limit)
{
    JsonDocument doc = load_json(STORAGE_SESSIONS_PATH);
    JsonArray arr = doc.as<JsonArray>();
    std::vector<Session> result;
    // arr is stored oldest-first; iterate in reverse
    int size = (int)arr.size();
    int start = size - limit;
    if (start < 0) start = 0;
    for (int i = size - 1; i >= start; i--) {
        Session s;
        session_from_json(arr[i], s);
        result.push_back(s);
    }
    return result;
}

std::vector<Session> storage_list_sessions(int page, int per, int &total_out)
{
    JsonDocument doc = load_json(STORAGE_SESSIONS_PATH);
    JsonArray arr = doc.as<JsonArray>();
    total_out = (int)arr.size();
    std::vector<Session> result;
    int offset = (page - 1) * per;
    // newest first
    int size = (int)arr.size();
    for (int i = size - 1 - offset; i >= 0 && (int)result.size() < per; i--) {
        Session s;
        session_from_json(arr[i], s);
        result.push_back(s);
    }
    return result;
}

String storage_export_csv()
{
    JsonDocument doc = load_json(STORAGE_SESSIONS_PATH);
    String csv = "id,nfc_id,name,date,login_time,logout_time,duration_sec,activity\n";
    for (JsonObject obj : doc.as<JsonArray>()) {
        csv += String(obj["id"] | 0) + ",";
        csv += "\"" + obj["nfc_id"].as<String>()  + "\",";
        csv += "\"" + obj["name"].as<String>()     + "\",";
        csv += "\"" + obj["date"].as<String>()     + "\",";
        csv += "\"" + obj["login_time"].as<String>()  + "\",";
        csv += "\"" + obj["logout_time"].as<String>() + "\",";
        csv += String(obj["duration_sec"] | 0) + ",";
        csv += "\"" + obj["activity"].as<String>() + "\"\n";
    }
    return csv;
}

void storage_purge_old_sessions()
{
    // Build cutoff date string (SESSION_PURGE_DAYS ago)
    time_t now = time(nullptr);
    if (now < 1000000) return;   // time not synced yet
    time_t cutoff = now - (time_t)SESSION_PURGE_DAYS * 86400;
    struct tm *tm_c = gmtime(&cutoff);
    char cutoff_str[24];
    strftime(cutoff_str, sizeof(cutoff_str), "%Y-%m-%dT%H:%M:%SZ", tm_c);

    JsonDocument doc = load_json(STORAGE_SESSIONS_PATH);
    JsonArray arr = doc.as<JsonArray>();
    JsonDocument newDoc;
    JsonArray newArr = newDoc.to<JsonArray>();
    int removed = 0;
    for (JsonObject obj : arr) {
        String lt = obj["login_time"] | "";
        if (lt < String(cutoff_str)) { removed++; continue; }
        newArr.add(obj);
    }
    if (removed > 0) {
        save_json(STORAGE_SESSIONS_PATH, newDoc);
        Serial.printf("[STORAGE] Purged %d session(s) older than %d days.\n",
                      removed, SESSION_PURGE_DAYS);
    }
}
