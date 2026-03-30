// =============================================================================
// FabLab Presence System — Async HTTP server + WebSocket
// Serves /www/* static files from LittleFS and all /api/* REST endpoints.
// =============================================================================

#include "api_server.h"
#include "config.h"
#include "storage.h"
#include "nfc_reader.h"

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>

static AsyncWebServer s_server(80);
static AsyncWebSocket s_ws("/ws");

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static String iso_now()
{
    time_t t = time(nullptr);
    struct tm *tm = gmtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm);
    return String(buf);
}

static String today_str()
{
    time_t t = time(nullptr);
    struct tm *tm = gmtime(&t);
    char buf[12];
    strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
    return String(buf);
}

static bool check_admin(AsyncWebServerRequest *req)
{
    AppSettings s = storage_load_settings();
    String pass = s.admin_password;
    // Accept header or query param
    if (req->hasHeader("X-Admin-Password") &&
        req->getHeader("X-Admin-Password")->value() == pass) return true;
    if (req->hasParam("adminPassword") &&
        req->getParam("adminPassword")->value() == pass) return true;
    return false;
}

static void send_json(AsyncWebServerRequest *req, int code,
                      const JsonDocument &doc)
{
    String body;
    serializeJson(doc, body);
    req->send(code, "application/json", body);
}

static void send_error(AsyncWebServerRequest *req, int code, const char *msg)
{
    JsonDocument doc;
    doc["error"] = msg;
    send_json(req, code, doc);
}

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------
static void on_ws_event(AsyncWebSocket *srv, AsyncWebSocketClient *client,
                        AwsEventType type, void *, uint8_t *, size_t)
{
    if (type == WS_EVT_CONNECT)
        Serial.printf("[WS] Client #%u connected\n", client->id());
    else if (type == WS_EVT_DISCONNECT)
        Serial.printf("[WS] Client #%u disconnected\n", client->id());
}

void api_server_push_nfc(const String &uid)
{
    JsonDocument doc;
    doc["type"] = "nfc";
    doc["uid"]  = uid;
    String msg;
    serializeJson(doc, msg);
    s_ws.textAll(msg);
    Serial.printf("[WS] Pushed NFC event: %s\n", uid.c_str());
}

// ---------------------------------------------------------------------------
// Register all routes
// ---------------------------------------------------------------------------
static void register_routes()
{
    // --- Static files from LittleFS /www ------------------------------------
    s_server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");

    // === GET /api/settings ===================================================
    s_server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
        AppSettings s = storage_load_settings();
        JsonDocument doc;
        doc["animationsEnabled"] = s.animations_enabled;
        doc["logPanelEnabled"]   = s.log_panel_enabled;
        // Never return admin password to frontend
        send_json(req, 200, doc);
    });

    // === POST /api/admin/verify ==============================================
    s_server.on("/api/admin/verify", HTTP_POST,
        [](AsyncWebServerRequest *req) {},
        nullptr,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
            JsonDocument body;
            deserializeJson(body, data, len);
            AppSettings s = storage_load_settings();
            String provided = body["adminPassword"] | "";
            if (provided != s.admin_password) {
                send_error(req, 401, "Wrong password.");
                return;
            }
            JsonDocument ok;
            ok["ok"] = true;
            send_json(req, 200, ok);
        }
    );

    // === GET /api/user/:nfcId ================================================
    s_server.on("/api/user/*", HTTP_GET, [](AsyncWebServerRequest *req) {
        // Extract nfc_id from URL: /api/user/<nfc_id>
        String raw = req->url().substring(strlen("/api/user/"));
        raw.replace("%3A", ":");  // URL-decode common separators
        String nfc_id = nfc_normalise_id(raw);
        if (nfc_id.isEmpty()) { send_error(req, 400, "nfcId required"); return; }
        User u;
        JsonDocument doc;
        if (!storage_find_user(nfc_id, u)) {
            doc["found"] = false;
        } else {
            doc["found"] = true;
            JsonObject obj = doc["user"].to<JsonObject>();
            obj["nfc_id"]   = u.nfc_id;
            obj["name"]     = u.name;
            obj["surname"]  = u.surname;
            obj["gender"]   = u.gender;
        }
        send_json(req, 200, doc);
    });

    // === POST /api/user ======================================================
    s_server.on("/api/user", HTTP_POST,
        [](AsyncWebServerRequest *) {},
        nullptr,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
            JsonDocument body;
            deserializeJson(body, data, len);
            String nfc_id = nfc_normalise_id(body["nfcId"] | "");
            String name    = body["name"]    | "";
            String surname = body["surname"] | "";
            String gender  = body["gender"]  | "";
            if (nfc_id.isEmpty() || name.isEmpty() || surname.isEmpty() || gender.isEmpty()) {
                send_error(req, 400, "nfcId, name, surname and gender are required.");
                return;
            }
            // Normalise gender
            gender.toLowerCase();
            if (gender != "female") gender = "male";

            User existing;
            if (storage_find_user(nfc_id, existing)) {
                JsonDocument doc;
                doc["created"] = false;
                JsonObject obj = doc["user"].to<JsonObject>();
                obj["nfc_id"]  = existing.nfc_id;
                obj["name"]    = existing.name;
                obj["surname"] = existing.surname;
                obj["gender"]  = existing.gender;
                send_json(req, 200, doc);
                return;
            }
            User u;
            u.nfc_id        = nfc_id;
            u.name          = name;
            u.surname       = surname;
            u.gender        = gender;
            u.registered_at = iso_now();
            u.last_seen     = iso_now();
            u.scan_count    = 1;
            storage_create_user(u);

            JsonDocument doc;
            doc["created"] = true;
            JsonObject obj = doc["user"].to<JsonObject>();
            obj["nfc_id"]  = u.nfc_id;
            obj["name"]    = u.name;
            obj["surname"] = u.surname;
            obj["gender"]  = u.gender;
            send_json(req, 201, doc);
        }
    );

    // === DELETE /api/user/:nfcId (admin) =====================================
    s_server.on("/api/user/*", HTTP_DELETE, [](AsyncWebServerRequest *req) {
        if (!check_admin(req)) { send_error(req, 401, "Invalid admin password."); return; }
        String raw = req->url().substring(strlen("/api/user/"));
        raw.replace("%3A", ":");
        String nfc_id = nfc_normalise_id(raw);
        User u;
        if (!storage_find_user(nfc_id, u)) { send_error(req, 404, "User not found."); return; }
        storage_delete_user(nfc_id);
        JsonDocument doc;
        doc["ok"] = true;
        send_json(req, 200, doc);
    });

    // === GET /api/users (admin) ==============================================
    s_server.on("/api/users", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!check_admin(req)) { send_error(req, 401, "Invalid admin password."); return; }
        auto users = storage_list_users();
        JsonDocument doc;
        JsonArray arr = doc["users"].to<JsonArray>();
        for (auto &u : users) {
            JsonObject obj = arr.add<JsonObject>();
            obj["nfc_id"]   = u.nfc_id;
            obj["name"]     = u.name;
            obj["surname"]  = u.surname;
            obj["gender"]   = u.gender;
            obj["scan_count"] = u.scan_count;
        }
        send_json(req, 200, doc);
    });

    // === POST /api/session/login =============================================
    s_server.on("/api/session/login", HTTP_POST,
        [](AsyncWebServerRequest *) {},
        nullptr,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
            JsonDocument body;
            deserializeJson(body, data, len);
            String nfc_id = nfc_normalise_id(body["nfcId"] | "");
            String name   = body["name"] | "";
            if (nfc_id.isEmpty() || name.isEmpty()) {
                send_error(req, 400, "nfcId and name are required.");
                return;
            }
            String now   = iso_now();
            String today = today_str();
            storage_update_user_seen(nfc_id, now);
            int id = storage_create_session(nfc_id, name, now, today);
            if (id < 0) { send_error(req, 500, "Failed to create session."); return; }

            JsonDocument doc;
            JsonObject sess = doc["session"].to<JsonObject>();
            sess["id"]         = id;
            sess["nfc_id"]     = nfc_id;
            sess["name"]       = name;
            sess["date"]       = today;
            sess["login_time"] = now;
            send_json(req, 201, doc);
        }
    );

    // === GET /api/session/active/:nfcId =====================================
    s_server.on("/api/session/active/*", HTTP_GET, [](AsyncWebServerRequest *req) {
        String raw = req->url().substring(strlen("/api/session/active/"));
        raw.replace("%3A", ":");
        String nfc_id = nfc_normalise_id(raw);
        String today  = today_str();
        Session s;
        JsonDocument doc;
        if (!storage_find_active_session(nfc_id, today, s)) {
            doc["found"] = false;
        } else {
            doc["found"] = true;
            JsonObject obj = doc["session"].to<JsonObject>();
            obj["id"]         = s.id;
            obj["nfc_id"]     = s.nfc_id;
            obj["login_time"] = s.login_time;
        }
        send_json(req, 200, doc);
    });

    // === PATCH /api/session/:id/activity ====================================
    // URL pattern: /api/session/123/activity
    s_server.on("/api/session/*/activity", HTTP_PATCH,
        [](AsyncWebServerRequest *) {},
        nullptr,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
            // Extract session id from path: /api/session/<id>/activity
            String url   = req->url();
            int start    = strlen("/api/session/");
            int end      = url.indexOf('/', start);
            int id       = (end > 0 ? url.substring(start, end) : url.substring(start)).toInt();
            JsonDocument body;
            deserializeJson(body, data, len);
            String activity = body["activity"] | "";
            if (activity.isEmpty()) { send_error(req, 400, "activity required"); return; }
            storage_set_activity(id, activity);
            JsonDocument doc;
            doc["ok"] = true;
            send_json(req, 200, doc);
        }
    );

    // === PATCH /api/session/:id/logout =======================================
    // URL pattern: /api/session/123/logout
    s_server.on("/api/session/*/logout", HTTP_PATCH,
        [](AsyncWebServerRequest *req) {
            String url = req->url();
            int start  = strlen("/api/session/");
            int end    = url.indexOf('/', start);
            int id     = (end > 0 ? url.substring(start, end) : url.substring(start)).toInt();
            String now = iso_now();
            int dur    = 0;
            storage_close_session(id, now, dur);
            JsonDocument doc;
            doc["ok"]           = true;
            doc["duration_sec"] = dur;
            send_json(req, 200, doc);
        }
    );

    // === GET /api/sessions/recent ============================================
    s_server.on("/api/sessions/recent", HTTP_GET, [](AsyncWebServerRequest *req) {
        int limit = 7;
        if (req->hasParam("limit")) limit = req->getParam("limit")->value().toInt();
        if (limit < 1) limit = 1;
        if (limit > 20) limit = 20;
        auto sessions = storage_recent_sessions(limit);
        JsonDocument doc;
        JsonArray arr = doc["sessions"].to<JsonArray>();
        for (auto &s : sessions) {
            JsonObject obj = arr.add<JsonObject>();
            obj["name"]        = s.name;
            obj["login_time"]  = s.login_time;
            obj["logout_time"] = s.logout_time;
        }
        send_json(req, 200, doc);
    });

    // === GET /api/sessions (admin, paginated) ================================
    s_server.on("/api/sessions", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!check_admin(req)) { send_error(req, 401, "Invalid admin password."); return; }
        int page = 1, per = 30, total = 0;
        if (req->hasParam("page")) page = req->getParam("page")->value().toInt();
        if (req->hasParam("per"))  per  = req->getParam("per")->value().toInt();
        if (per > 100) per = 100;
        auto sessions = storage_list_sessions(page, per, total);
        JsonDocument doc;
        doc["total"] = total;
        doc["page"]  = page;
        doc["per"]   = per;
        JsonArray arr = doc["sessions"].to<JsonArray>();
        for (auto &s : sessions) {
            JsonObject obj = arr.add<JsonObject>();
            obj["id"]           = s.id;
            obj["nfc_id"]       = s.nfc_id;
            obj["name"]         = s.name;
            obj["date"]         = s.date;
            obj["login_time"]   = s.login_time;
            obj["logout_time"]  = s.logout_time;
            obj["duration_sec"] = s.duration_sec;
            obj["activity"]     = s.activity;
        }
        send_json(req, 200, doc);
    });

    // === POST /api/sessions/export (admin, CSV) ==============================
    s_server.on("/api/sessions/export", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            if (!check_admin(req)) { send_error(req, 401, "Invalid admin password."); return; }
            String csv = storage_export_csv();
            AsyncWebServerResponse *resp =
                req->beginResponse(200, "text/csv", csv);
            resp->addHeader("Content-Disposition",
                            "attachment; filename=\"fablab-sessions.csv\"");
            req->send(resp);
        }
    );

    // 404 fallback
    s_server.onNotFound([](AsyncWebServerRequest *req) {
        send_error(req, 404, "Not found.");
    });
}

// ---------------------------------------------------------------------------
// api_server_init
// ---------------------------------------------------------------------------
void api_server_init()
{
    s_ws.onEvent(on_ws_event);
    s_server.addHandler(&s_ws);
    register_routes();
    s_server.begin();
    Serial.println("[API] HTTP server started on port 80.");
}
