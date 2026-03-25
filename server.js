// =============================================================================
// FabLab Presence System — server.js
// Node.js + Express + better-sqlite3
// =============================================================================

'use strict';

const path       = require('path');
const express    = require('express');
const rateLimit  = require('express-rate-limit');
const Database   = require('better-sqlite3');

// ---------------------------------------------------------------------------
// Database setup
// ---------------------------------------------------------------------------
const DB_PATH = path.join(__dirname, 'fablab.db');
const db = new Database(DB_PATH);

// Performance pragmas
db.pragma('journal_mode = WAL');
db.pragma('foreign_keys = ON');

// Create tables
db.exec(`
  CREATE TABLE IF NOT EXISTS users (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    nfc_id       TEXT    NOT NULL UNIQUE,
    name         TEXT    NOT NULL,
    registered_at TEXT   NOT NULL,
    last_seen    TEXT,
    scan_count   INTEGER NOT NULL DEFAULT 0
  );

  CREATE TABLE IF NOT EXISTS sessions (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id        INTEGER REFERENCES users(id),
    nfc_id         TEXT    NOT NULL,
    name           TEXT    NOT NULL,
    date           TEXT    NOT NULL,
    login_time     TEXT    NOT NULL,
    logout_time    TEXT,
    activity       TEXT,
    duration_sec   INTEGER,
    user_agent     TEXT,
    ip_address     TEXT
  );

  CREATE INDEX IF NOT EXISTS idx_sessions_nfc_date ON sessions(nfc_id, date);
  CREATE INDEX IF NOT EXISTS idx_sessions_login    ON sessions(login_time);
`);

// ---------------------------------------------------------------------------
// Housekeeping: delete sessions older than 30 days (runs at startup)
// ---------------------------------------------------------------------------
function purgeOldSessions() {
  const cutoff = new Date();
  cutoff.setDate(cutoff.getDate() - 30);
  const result = db.prepare(
    "DELETE FROM sessions WHERE login_time < ?"
  ).run(cutoff.toISOString());
  if (result.changes > 0) {
    console.log('[DB] Purged', result.changes, 'session(s) older than 30 days.');
  }
}

// Seed test users so the simulation panel works out of the box
db.exec(`
  INSERT OR IGNORE INTO users (nfc_id, name, registered_at, last_seen, scan_count)
  VALUES ('NFC001', 'Ana',  datetime('now'), NULL, 0);
  INSERT OR IGNORE INTO users (nfc_id, name, registered_at, last_seen, scan_count)
  VALUES ('NFC002', 'Bor',  datetime('now'), NULL, 0);
  INSERT OR IGNORE INTO users (nfc_id, name, registered_at, last_seen, scan_count)
  VALUES ('NFC003', 'Cene', datetime('now'), NULL, 0);
`);

purgeOldSessions();
// Run purge once a day (86400000 ms)
setInterval(purgeOldSessions, 86_400_000);

// ---------------------------------------------------------------------------
// Prepared statements
// ---------------------------------------------------------------------------
const stmtFindUser          = db.prepare('SELECT * FROM users WHERE nfc_id = ?');
const stmtInsertUser        = db.prepare(
  'INSERT INTO users (nfc_id, name, registered_at, last_seen, scan_count) VALUES (?, ?, ?, ?, 1)'
);
const stmtUpdateUserSeen    = db.prepare(
  'UPDATE users SET last_seen = ?, scan_count = scan_count + 1 WHERE nfc_id = ?'
);
const stmtFindActiveSession = db.prepare(
  "SELECT * FROM sessions WHERE nfc_id = ? AND date = ? AND logout_time IS NULL ORDER BY id DESC LIMIT 1"
);
const stmtInsertSession     = db.prepare(
  'INSERT INTO sessions (user_id, nfc_id, name, date, login_time, user_agent, ip_address) VALUES (?, ?, ?, ?, ?, ?, ?)'
);
const stmtSetActivity       = db.prepare(
  'UPDATE sessions SET activity = ? WHERE id = ?'
);
const stmtLogout            = db.prepare(
  'UPDATE sessions SET logout_time = ?, duration_sec = ? WHERE id = ?'
);

// ---------------------------------------------------------------------------
// Rate limiter (express-rate-limit)
// ---------------------------------------------------------------------------
const apiLimiter = rateLimit({
  windowMs: 60_000,  // 1 minute
  max:      60,      // max 60 requests per IP per minute
  standardHeaders: true,
  legacyHeaders:   false,
  message: { error: 'Too many requests. Please slow down.' },
});

const app = express();
app.use(express.json());

// Serve static frontend files from the dedicated public/ directory
// (keeps server-side source files out of the web root)
app.use(express.static(path.join(__dirname, 'public'), { index: 'index.html' }));

// ---------------------------------------------------------------------------
// Helper: normalise NFC ID (strip colons, uppercase)
// ---------------------------------------------------------------------------
function normaliseNfcId(raw) {
  return String(raw).replace(/:/g, '').toUpperCase();
}

// Helper: get client IP
function clientIp(req) {
  return (
    req.headers['x-forwarded-for'] ||
    req.socket.remoteAddress ||
    null
  );
}

// ---------------------------------------------------------------------------
// API — Look up a user by NFC ID
// GET /api/user/:nfcId
// ---------------------------------------------------------------------------
app.get('/api/user/:nfcId', apiLimiter, (req, res) => {
  const nfcId = normaliseNfcId(req.params.nfcId);
  const user  = stmtFindUser.get(nfcId);
  if (!user) {
    return res.status(404).json({ found: false });
  }
  res.json({ found: true, user });
});

// ---------------------------------------------------------------------------
// API — Register a new user
// POST /api/user  { nfcId, name }
// ---------------------------------------------------------------------------
app.post('/api/user', apiLimiter, (req, res) => {
  const nfcId = normaliseNfcId(req.body.nfcId || '');
  const name  = String(req.body.name || '').trim();

  if (!nfcId || !name) {
    return res.status(400).json({ error: 'nfcId and name are required.' });
  }

  const now = new Date().toISOString();

  // Upsert: if the tag was already registered in the meantime, return it
  let user = stmtFindUser.get(nfcId);
  if (user) {
    return res.json({ created: false, user });
  }

  const info = stmtInsertUser.run(nfcId, name, now, now);
  user = db.prepare('SELECT * FROM users WHERE id = ?').get(info.lastInsertRowid);
  console.log('[DB] New user registered:', name, '(', nfcId, ')');
  res.status(201).json({ created: true, user });
});

// ---------------------------------------------------------------------------
// API — Login (create session)
// POST /api/session/login  { nfcId, name, userAgent }
// ---------------------------------------------------------------------------
app.post('/api/session/login', apiLimiter, (req, res) => {
  const nfcId     = normaliseNfcId(req.body.nfcId || '');
  const name      = String(req.body.name  || '').trim();
  const userAgent = String(req.body.userAgent || '').substring(0, 512);

  if (!nfcId || !name) {
    return res.status(400).json({ error: 'nfcId and name are required.' });
  }

  const now   = new Date();
  const today = now.toISOString().slice(0, 10); // YYYY-MM-DD
  const ip    = clientIp(req);

  // Update user last_seen / scan_count
  const user = stmtFindUser.get(nfcId);
  if (user) {
    stmtUpdateUserSeen.run(now.toISOString(), nfcId);
  }

  const info = stmtInsertSession.run(
    user ? user.id : null,
    nfcId,
    name,
    today,
    now.toISOString(),
    userAgent,
    ip
  );

  const session = db.prepare('SELECT * FROM sessions WHERE id = ?').get(info.lastInsertRowid);
  console.log('[DB] Session created — user:', name, '| NFC:', nfcId, '| IP:', ip);
  res.status(201).json({ session });
});

// ---------------------------------------------------------------------------
// API — Set activity on open session
// PATCH /api/session/:id/activity  { activity }
// ---------------------------------------------------------------------------
app.patch('/api/session/:id/activity', apiLimiter, (req, res) => {
  const id       = Number(req.params.id);
  const activity = String(req.body.activity || '').trim();

  if (!activity) {
    return res.status(400).json({ error: 'activity is required.' });
  }

  stmtSetActivity.run(activity, id);
  console.log('[DB] Activity set — session:', id, '| activity:', activity);
  res.json({ ok: true });
});

// ---------------------------------------------------------------------------
// API — Logout (close session)
// PATCH /api/session/:id/logout
// ---------------------------------------------------------------------------
app.patch('/api/session/:id/logout', apiLimiter, (req, res) => {
  const id = Number(req.params.id);
  const session = db.prepare('SELECT * FROM sessions WHERE id = ?').get(id);
  if (!session) {
    return res.status(404).json({ error: 'Session not found.' });
  }

  const now         = new Date();
  const loginTime   = new Date(session.login_time);
  const durationSec = Math.round((now - loginTime) / 1000);

  stmtLogout.run(now.toISOString(), durationSec, id);
  console.log('[DB] Session closed — user:', session.name, '| duration:', durationSec, 's');
  res.json({ ok: true, duration_sec: durationSec });
});

// ---------------------------------------------------------------------------
// API — Get active session for a NFC tag today
// GET /api/session/active/:nfcId
// ---------------------------------------------------------------------------
app.get('/api/session/active/:nfcId', apiLimiter, (req, res) => {
  const nfcId = normaliseNfcId(req.params.nfcId);
  const today = new Date().toISOString().slice(0, 10);
  const session = stmtFindActiveSession.get(nfcId, today);
  if (!session) {
    return res.json({ found: false });
  }
  res.json({ found: true, session });
});

// ---------------------------------------------------------------------------
// API — Get app settings (avoids CORS issues with direct file fetch)
// GET /api/settings
// ---------------------------------------------------------------------------
app.get('/api/settings', apiLimiter, (req, res) => {
  const settingsPath = path.join(__dirname, 'public', 'settings.json');
  try {
    const data = JSON.parse(require('fs').readFileSync(settingsPath, 'utf8'));
    res.json(data);
  } catch (err) {
    console.warn('[SETTINGS] Could not read settings.json:', err.message);
    res.json({ animationsEnabled: true });
  }
});

// ---------------------------------------------------------------------------
// Start
// ---------------------------------------------------------------------------
const PORT = process.env.PORT || 3000;
app.listen(PORT, () => {
  console.log('=== FabLab Presence System server running on port', PORT, '===');
  console.log('    Open: http://localhost:' + PORT);
});
