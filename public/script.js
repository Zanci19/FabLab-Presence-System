// =============================================================================
// FabLab Management System — script.js
// =============================================================================

// --- Test data (kept for simulation panel) ---
const TEST_USERS = [
  { id: 'NFC001', name: 'Ana', surname: 'Novak', gender: 'female' },
  { id: 'NFC002', name: 'Bor', surname: 'Kranjc', gender: 'male' },
  { id: 'NFC003', name: 'Cene', surname: 'Zupan', gender: 'male' },
];

// Real NFC tag UIDs from physical cards (UID normalised: no colons, uppercase)
const REAL_NFC_TAGS = [
  { id: '04271B02B61290', label: 'Kartica A' },
  { id: '04431C12B61290', label: 'Kartica B' },
];

const ACTIVITIES = [
  '3D tiskanje',
  'Programiranje',
  'Modeliranje',
  'Zabušavanje',
  'Učenje',
  'Maintenance',
  'Drugo',
];

// --- App state ---
let currentUser      = null;   // { id, name, gender }
let activeSessionId  = null;   // DB session id of the current open session
let pendingNfcId     = null;   // NFC ID waiting for name entry
let helperMessageTimeout  = null;
let nfcFlowInProgress     = false;
let adminPassword    = null;   // set after successful admin login
let adminAddUserMode = false;  // true when admin is waiting to scan a card for registration
let adminLogsPage    = 1;
let adminLogsTotal   = 0;
const ADMIN_LOGS_PER_PAGE = 20;
const DEFAULT_HELPER_TEXT = 'Prisloni ključ za vstop/izstop...';
const DEFAULT_CONFIG = Object.freeze({
  animationsEnabled: true,
  logPanelEnabled: true,
});
let appConfig = { ...DEFAULT_CONFIG };
let nameEntryFlow = {
  mode: 'idle', // idle | confirm-create | collect-name | collect-gender
  context: 'user', // user | admin
  fullName: '',
};

// =============================================================================
// Audio
// =============================================================================
const KEY_PRESS_SOUND_PATHS = Array.from(
  { length: 15 },
  (_, idx) => 'audio/key press' + (idx + 1) + '.ogg'
);
const keyPressSoundPool = KEY_PRESS_SOUND_PATHS.map(path => {
  const audio = new Audio(path);
  audio.preload = 'auto';
  return audio;
});

function playSound(id) {
  const el = document.getElementById('audio-' + id);
  if (!el) {
    console.warn('[AUDIO] Element not found for id:', id);
    return;
  }
  el.currentTime = 0;
  el.play().catch(err =>
    console.warn('[AUDIO] Could not play "' + id + '":', err.message)
  );
  console.log('[AUDIO] Playing:', id);
}

function playRandomKeyPressSound() {
  if (!keyPressSoundPool.length) return;
  const randomIdx = Math.floor(Math.random() * keyPressSoundPool.length);
  const baseAudio = keyPressSoundPool[randomIdx];
  const audio = baseAudio.cloneNode();
  audio.play().catch(err =>
    console.warn('[AUDIO] Could not play key press:', err.message)
  );
}

const FALLBACK_ERROR_SOUND_DURATION_MS = 900;
let errorShakeTimeout = null;

function shakeScreenError(durationMs = FALLBACK_ERROR_SOUND_DURATION_MS) {
  document.body.style.setProperty('--error-shake-duration', durationMs + 'ms');
  document.body.classList.remove('screen-shake-error');
  void document.body.offsetWidth;
  document.body.classList.add('screen-shake-error');

  if (errorShakeTimeout) {
    clearTimeout(errorShakeTimeout);
  }
  errorShakeTimeout = setTimeout(() => {
    document.body.classList.remove('screen-shake-error');
  }, durationMs);
}

function playErrorBeep() {
  const wrongSound = document.getElementById('audio-wrong');
  if (!wrongSound) {
    console.warn('[AUDIO] Missing #audio-wrong element.');
    shakeScreenError();
    return;
  }

  wrongSound.currentTime = 0;
  const durationMs = Number.isFinite(wrongSound.duration) && wrongSound.duration > 0
    ? Math.round(wrongSound.duration * 1000)
    : FALLBACK_ERROR_SOUND_DURATION_MS;

  shakeScreenError(durationMs);
  wrongSound.play().catch(err => {
    console.warn('[AUDIO] Could not play wrong sound:', err.message);
  });
  console.log('[AUDIO] Playing wrong/error sound');
}

// =============================================================================
// Screen management
// =============================================================================
function showScreen(screenId) {
  const currentlyActive = document.querySelector('.screen.active');
  const currentScreenId = currentlyActive?.id || '';
  const targetScreenDomId = 'screen-' + screenId;
  const isSameScreen = currentScreenId === targetScreenDomId;

  if (isSameScreen) {
    console.log('[SCREEN] Active screen unchanged:', screenId);
    return;
  }

  document.querySelectorAll('.screen').forEach(s => s.classList.remove('active'));
  const el = document.getElementById('screen-' + screenId);
  if (el) {
    if (appConfig.animationsEnabled) {
      el.classList.remove('screen-animated-entry');
      void el.offsetWidth;
      el.classList.add('screen-animated-entry');
    }
    el.classList.add('active');
  } else {
    console.error('[SCREEN] Unknown screen:', screenId);
  }
  console.log('[SCREEN] Active screen:', screenId);
}

// =============================================================================
// NFC feedback overlay
// =============================================================================
function applyAnimationMode() {
  document.body.classList.toggle('animations-off', !appConfig.animationsEnabled);
  console.log('[CONFIG] Animations enabled:', appConfig.animationsEnabled);
}

function applyLogPanelMode() {
  const logPanel = document.getElementById('clock-log-panel');
  if (!logPanel) return;
  logPanel.style.display = appConfig.logPanelEnabled ? 'flex' : 'none';
  console.log('[CONFIG] Log panel enabled:', appConfig.logPanelEnabled);
}

function coerceBoolean(value, fallback) {
  if (typeof value === 'boolean') return value;
  if (typeof value === 'string') {
    const normalized = value.trim().toLowerCase();
    if (normalized === 'false') return false;
    if (normalized === 'true') return true;
  }
  return fallback;
}

async function loadConfig() {
  const parseConfig = data => ({
    ...DEFAULT_CONFIG,
    ...data,
    animationsEnabled: coerceBoolean(data?.animationsEnabled, DEFAULT_CONFIG.animationsEnabled),
    logPanelEnabled: coerceBoolean(data?.logPanelEnabled, DEFAULT_CONFIG.logPanelEnabled),
  });

  const configUrls = ['/api/settings', './settings.json'];

  try {
    let configData = null;
    let lastError  = null;

    for (const url of configUrls) {
      try {
        const response = await fetch(url, { cache: 'no-store' });
        if (!response.ok) {
          throw new Error('HTTP ' + response.status);
        }
        configData = await response.json();
        console.log('[CONFIG] Loaded settings from:', url);
        break;
      } catch (error) {
        lastError = error;
        console.warn('[CONFIG] Failed to load settings from', url + ':', error.message);
      }
    }

    if (!configData) {
      throw lastError || new Error('No settings source available.');
    }

    appConfig = parseConfig(configData);
  } catch (error) {
    console.warn('[CONFIG] Could not load settings, using defaults:', error.message);
    appConfig = { ...DEFAULT_CONFIG };
  }

  applyAnimationMode();
  applyLogPanelMode();
}

function randomChars(length = 16) {
  const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()_+-=';
  let output = '';
  for (let i = 0; i < length; i++) {
    output += alphabet[Math.floor(Math.random() * alphabet.length)];
  }
  return output;
}

function runNfcScramble(durationMs = 500, tickMs = 50) {
  if (!appConfig.animationsEnabled) {
    return Promise.resolve();
  }

  const helper = document.getElementById('clock-helper');
  helper.textContent = randomChars(16);

  return new Promise(resolve => {
    const interval = setInterval(() => {
      helper.textContent = randomChars(16);
    }, tickMs);

    setTimeout(() => {
      clearInterval(interval);
      resolve();
    }, durationMs);
  });
}

function clearNfcFeedback() {
  const fb = document.getElementById('nfc-feedback');
  fb.className = '';
  fb.textContent = '';
}

function flashScanCrescendo(durationMs = 380) {
  const el = document.getElementById('scan-crescendo');
  if (!el) return;
  el.classList.remove('scan-crescendo-active');
  void el.offsetWidth;
  el.classList.add('scan-crescendo-active');
  setTimeout(() => {
    el.classList.remove('scan-crescendo-active');
  }, durationMs);
}

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function isFemaleUser(user) {
  return user?.gender === 'female';
}

function buildFullName(user) {
  if (!user) return '';
  if (user.surname) return (user.name + ' ' + user.surname).trim();
  return String(user.name || '').trim();
}

function buildFirstName(user) {
  return String(user?.name || '').trim();
}

function splitFullName(fullName) {
  const parts = fullName.trim().replace(/\s+/g, ' ').split(' ');
  const name = parts.shift() || '';
  const surname = parts.join(' ').trim();
  return { name, surname };
}

function setClockState(state = '') {
  const clockScreen = document.getElementById('screen-clock');
  clockScreen.classList.remove('success-state', 'error-state', 'logout-state');
  if (state) {
    clockScreen.classList.add(state);
  }
}

function setClockHelperMessage(message, color = 'white', durationMs = 0) {
  const helper = document.getElementById('clock-helper');

  if (helperMessageTimeout) {
    clearTimeout(helperMessageTimeout);
    helperMessageTimeout = null;
  }

  helper.textContent = message;
  setClockState('');
  if (color === 'red') {
    helper.style.color = 'var(--red)';
    helper.style.textShadow = '0 0 12px rgba(255,34,34,0.45)';
  } else if (color === 'orange') {
    helper.style.color = 'var(--orange)';
    helper.style.textShadow = '0 0 12px rgba(255,140,0,0.45)';
  } else {
    helper.style.color = 'var(--white)';
    helper.style.textShadow = '0 0 8px rgba(224,224,224,0.3)';
  }

  if (durationMs > 0) {
    helperMessageTimeout = setTimeout(() => {
      setClockHelperMessage(DEFAULT_HELPER_TEXT);
    }, durationMs);
  }
}

// =============================================================================
// Log panel
// =============================================================================
async function refreshLogPanel() {
  try {
    const data = await apiFetch('GET', '/api/sessions/recent?limit=7');
    const list = document.getElementById('clock-log-list');
    list.innerHTML = '';
    // Sessions are returned newest-first (ORDER BY id DESC) — index 0 is the latest
    (data.sessions || []).forEach((s, idx) => {
      const li = document.createElement('li');
      if (idx === 0) li.classList.add('log-entry-new');
      const d = new Date(s.login_time);
      const hh = String(d.getHours()).padStart(2, '0');
      const mm = String(d.getMinutes()).padStart(2, '0');
      const nameEl = document.createElement('span');
      nameEl.className = 'log-name';
      nameEl.textContent = s.name;
      const timeEl = document.createElement('span');
      timeEl.className = 'log-time';
      timeEl.textContent = hh + ':' + mm;
      li.appendChild(nameEl);
      li.appendChild(timeEl);
      list.appendChild(li);
    });
  } catch (err) {
    console.warn('[LOG PANEL] Could not refresh:', err.message);
  }
}

// =============================================================================
// Admin status bar
// =============================================================================
let adminStatusTimeout = null;
function showAdminStatus(msg, color, durationMs = 2500) {
  const bar = document.getElementById('admin-status-bar');
  bar.textContent = msg;
  bar.style.color = color === 'green' ? 'var(--green)'
    : color === 'red' ? 'var(--red)'
    : 'var(--orange)';
  bar.style.opacity = '1';
  if (adminStatusTimeout) clearTimeout(adminStatusTimeout);
  adminStatusTimeout = setTimeout(() => {
    bar.style.opacity = '0';
  }, durationMs);
}

// =============================================================================
// Date / time helpers
// =============================================================================
function getTodayKey() {
  const d = new Date();
  const y = d.getFullYear();
  const m = String(d.getMonth() + 1).padStart(2, '0');
  const day = String(d.getDate()).padStart(2, '0');
  return y + '-' + m + '-' + day;
}

function formatTime(date) {
  return (
    String(date.getHours()).padStart(2, '0') + ':' +
    String(date.getMinutes()).padStart(2, '0') + ':' +
    String(date.getSeconds()).padStart(2, '0')
  );
}

function formatDate(date) {
  const DAYS = [
    'Nedelja', 'Ponedeljek', 'Torek', 'Sreda',
    'Četrtek', 'Petek', 'Sobota',
  ];
  const MONTHS = [
    'Jan', 'Feb', 'Mar', 'Apr', 'Maj', 'Jun',
    'Jul', 'Avg', 'Sep', 'Okt', 'Nov', 'Dec',
  ];
  return (
    DAYS[date.getDay()] + ', ' +
    date.getDate() + '. ' +
    MONTHS[date.getMonth()] + ' ' +
    date.getFullYear()
  );
}

// =============================================================================
// API helpers
// =============================================================================
function normaliseNfcId(raw) {
  return String(raw).replace(/:/g, '').toUpperCase();
}

async function apiFetch(method, url, body, extraHeaders) {
  const opts = {
    method,
    headers: { 'Content-Type': 'application/json', ...extraHeaders },
  };
  if (body !== undefined) {
    opts.body = JSON.stringify(body);
  }
  const res = await fetch(url, opts);
  if (!res.ok) {
    const text = await res.text().catch(() => '');
    throw new Error('API ' + method + ' ' + url + ' → ' + res.status + ' ' + text);
  }
  return res.json();
}

// =============================================================================
// NFC read handler
// =============================================================================
async function handleNfcRead(nfcId) {
  const activeScreen = document.querySelector('.screen.active');
  if (activeScreen?.id === 'screen-greeting') {
    console.warn('[NFC] Scan ignored while activity picker is active.');
    playErrorBeep();
    return;
  }

  // --- Admin "add user" mode: capture this scan ---
  if (adminAddUserMode) {
    adminAddUserMode = false;
    const normId = normaliseNfcId(nfcId);
    console.log('[ADMIN] NFC scan in add-user mode. ID:', normId);
    await handleAdminAddUserScan(normId);
    return;
  }

  if (nfcFlowInProgress) {
    console.warn('[NFC] Ignoring read while previous flow is active.');
    return;
  }
  nfcFlowInProgress = true;

  const normId = normaliseNfcId(nfcId);
  console.log('[NFC] Card presented. Raw ID:', nfcId, '| Normalised:', normId);

  setClockState('');
  clearNfcFeedback();
  flashScanCrescendo();

  try {
    await runNfcScramble(500, 50);

    // Lookup user in DB
    let userResult;
    try {
      userResult = await apiFetch('GET', '/api/user/' + encodeURIComponent(normId));
    } catch (err) {
      console.error('[NFC] API error during user lookup:', err.message);
      playErrorBeep();
      setClockHelperMessage('Napaka strežnika. Poskusi znova.', 'red', 2500);
      setClockState('error-state');
      await sleep(1600);
      setClockState('');
      return;
    }

    if (!userResult.found) {
      // Unknown tag — prompt for account creation
      console.log('[NFC] Unknown card. ID:', normId, '— prompting for account creation.');
      playErrorBeep();
      pendingNfcId = normId;
      showUnknownUserPrompt();
      return;
    }

    const user = {
      id: normId,
      name: userResult.user.name,
      surname: userResult.user.surname || '',
      gender: userResult.user.gender || 'male',
    };
    console.log('[NFC] Recognised user:', user.name, '(', normId, ')');

    // Check for an active session today
    let activeResult;
    try {
      activeResult = await apiFetch('GET', '/api/session/active/' + encodeURIComponent(normId));
    } catch (err) {
      console.error('[NFC] API error during session lookup:', err.message);
      playErrorBeep();
      setClockHelperMessage('Napaka strežnika. Poskusi znova.', 'red', 2500);
      return;
    }

    const isLogoutFlow = activeResult.found;

    setClockHelperMessage(buildFullName(user), isLogoutFlow ? 'orange' : 'white');
    setClockState(isLogoutFlow ? 'logout-state' : 'success-state');
    playSound('login');
    await sleep(900);
    setClockState('');

    if (isLogoutFlow) {
      await doLogout(user, activeResult.session.id);
    } else {
      await doLogin(user);
    }
  } finally {
    nfcFlowInProgress = false;
  }
}

// =============================================================================
// Login
// =============================================================================
async function doLogin(user) {
  const loginTime = new Date();
  console.log('[LOGIN] User:', buildFullName(user), '| Time:', formatTime(loginTime), '| Date:', getTodayKey());

  try {
    const result = await apiFetch('POST', '/api/session/login', {
      nfcId:     user.id,
      name:      buildFullName(user),
      userAgent: navigator.userAgent,
    });
    activeSessionId = result.session.id;
    console.log('[LOGIN] Session created in DB. ID:', activeSessionId);
  } catch (err) {
    console.error('[LOGIN] Failed to create session:', err.message);
  }

  currentUser = user;
  setClockHelperMessage(DEFAULT_HELPER_TEXT);
  showGreeting(user);
  refreshLogPanel();
}

// =============================================================================
// Logout
// =============================================================================
async function doLogout(user, sessionId) {
  const logoutTime = new Date();
  console.log('[LOGOUT] User:', user.name, '| Time:', formatTime(logoutTime), '| Date:', getTodayKey());

  try {
    const result = await apiFetch('PATCH', '/api/session/' + sessionId + '/logout');
    console.log('[LOGOUT] Session closed. Duration:', result.duration_sec, 's');
  } catch (err) {
    console.error('[LOGOUT] Failed to close session:', err.message);
  }

  currentUser = null;
  activeSessionId = null;
  playSound('logout');
  showScreen('clock');
  setClockHelperMessage(
    isFemaleUser(user) ? 'Izpisana si. Nasvidenje!' : 'Izpisan si. Nasvidenje!',
    'white',
    2500
  );
  refreshLogPanel();
}

// =============================================================================
// Greeting + activity selection screen
// =============================================================================
function showGreeting(user) {
  console.log('[GREETING] Showing greeting for:', buildFullName(user));
  setClockState('');
  setClockHelperMessage(DEFAULT_HELPER_TEXT);
  document.getElementById('greeting-text').innerHTML =
    'Živjo ' + buildFirstName(user) + '.<br>Kaj delaš danes?';
  showScreen('greeting');
}

async function selectActivity(activity) {
  console.log('[ACTIVITY SELECTED] User:', currentUser.name, '| Activity:', activity);

  if (activeSessionId !== null) {
    try {
      await apiFetch('PATCH', '/api/session/' + activeSessionId + '/activity', { activity });
      console.log('[ACTIVITY] Saved to DB — session:', activeSessionId);
    } catch (err) {
      console.error('[ACTIVITY] Failed to save activity:', err.message);
    }
  }

  playSound('topic-select');
  showScreen('clock');
  setClockHelperMessage(
    isFemaleUser(currentUser) ? 'Vpisana si v FabLab.' : 'Vpisan si v FabLab.',
    'white',
    2500
  );
  currentUser = null;
  activeSessionId = null;
}

// =============================================================================
// Name entry screen (for unknown NFC tags)
// =============================================================================
function showNameEntry() {
  const input = document.getElementById('name-entry-input');
  const submitBtn = document.getElementById('name-entry-submit');
  const cancelBtn = document.getElementById('name-entry-cancel');
  const title = document.getElementById('name-entry-title');

  if (nameEntryFlow.mode === 'confirm-create') {
    title.innerHTML = 'ID ključa ni povezan z računom.<br>Ustvari račun';
    input.style.display = 'none';
    submitBtn.textContent = 'DA';
    cancelBtn.textContent = 'NE';
  } else if (nameEntryFlow.mode === 'collect-gender') {
    title.textContent = 'Izberi spol';
    input.style.display = 'none';
    submitBtn.textContent = 'ŽENSKA';
    cancelBtn.textContent = 'MOŠKI';
  } else {
    title.textContent = 'Vpiši ime in priimek';
    input.style.display = '';
    input.placeholder = 'Ime in priimek...';
    submitBtn.textContent = 'POTRDI';
    cancelBtn.textContent = 'PREKLIČI';
  }

  showScreen('name-entry');
  if (nameEntryFlow.mode === 'collect-name') {
    input.value = '';
    setTimeout(() => input.focus(), 100);
  }
}

async function submitNameEntry() {
  const input = document.getElementById('name-entry-input');
  if (!pendingNfcId) {
    console.warn('[NAME ENTRY] No pending NFC ID — aborting.');
    showScreen('clock');
    return;
  }

  if (nameEntryFlow.mode === 'confirm-create') {
    nameEntryFlow.mode = 'collect-name';
    showNameEntry();
    return;
  }

  if (nameEntryFlow.mode === 'collect-name') {
    const fullName = input.value.trim().replace(/\s+/g, ' ');
    if (!fullName || fullName.split(' ').length < 2) {
      input.focus();
      setClockHelperMessage('Vnesi ime in priimek.', 'orange', 1800);
      return;
    }
    nameEntryFlow.fullName = fullName;
    nameEntryFlow.mode = 'collect-gender';
    showNameEntry();
    return;
  }

  if (nameEntryFlow.mode === 'collect-gender') {
    await registerPendingUser(nameEntryFlow.fullName, 'female');
    return;
  }

  console.warn('[NAME ENTRY] Unexpected mode:', nameEntryFlow.mode);
}

async function registerPendingUser(fullName, gender) {
  const nfcId = pendingNfcId;
  const flowContext = nameEntryFlow.context;
  const { name, surname } = splitFullName(fullName);
  console.log('[NAME ENTRY] Registering:', fullName, '(' + nfcId + ')', '| gender:', gender);

  try {
    await apiFetch('POST', '/api/user', { nfcId, name, surname, gender });
  } catch (err) {
    console.error('[NAME ENTRY] Failed to register user:', err.message);
    playErrorBeep();
    pendingNfcId = null;
    nfcFlowInProgress = false;
    nameEntryFlow = { mode: 'idle', context: 'user', fullName: '' };
    if (adminPassword) {
      showAdminPanel();
      showAdminStatus('Napaka pri registraciji.', 'red');
    } else {
      showScreen('clock');
      setClockHelperMessage('Napaka pri registraciji.', 'red', 2500);
    }
    return;
  }

  const user = { id: nfcId, name, surname, gender };
  pendingNfcId = null;
  nameEntryFlow = { mode: 'idle', context: 'user', fullName: '' };

  // If in admin context, return to admin panel
  if (adminPassword || flowContext === 'admin') {
    nfcFlowInProgress = false;
    showAdminPanel();
    showAdminStatus('Uporabnik ' + buildFullName(user) + ' dodan!', 'green');
    console.log('[ADMIN] User registered:', buildFullName(user));
    return;
  }

  setClockHelperMessage(buildFullName(user), 'white');
  setClockState('success-state');
  playSound('login');

  // Brief visual feedback on clock screen before greeting
  showScreen('clock');
  await sleep(900);
  setClockState('');

  await doLogin(user);
  nfcFlowInProgress = false;
}

async function cancelNameEntry() {
  if (nameEntryFlow.mode === 'confirm-create') {
    pendingNfcId = null;
    nfcFlowInProgress = false;
    nameEntryFlow = { mode: 'idle', context: 'user', fullName: '' };
    showScreen('clock');
    setClockHelperMessage(DEFAULT_HELPER_TEXT);
    return;
  }

  if (nameEntryFlow.mode === 'collect-gender') {
    await registerPendingUser(nameEntryFlow.fullName, 'male');
    return;
  }

  pendingNfcId = null;
  nfcFlowInProgress = false;
  nameEntryFlow = { mode: 'idle', context: 'user', fullName: '' };
  // Return to admin panel if we were in admin context
  if (adminPassword) {
    showAdminPanel();
  } else {
    showScreen('clock');
    setClockHelperMessage(DEFAULT_HELPER_TEXT);
  }
}

function showUnknownUserPrompt() {
  nameEntryFlow = { mode: 'confirm-create', context: 'user', fullName: '' };
  showNameEntry();
}

// =============================================================================
// Clock
// =============================================================================
function updateClock() {
  const now = new Date();
  const h = String(now.getHours()).padStart(2, '0');
  const m = String(now.getMinutes()).padStart(2, '0');
  const s = String(now.getSeconds()).padStart(2, '0');
  document.getElementById('clock-display').textContent = h + ':' + m + ':' + s;
  document.getElementById('clock-date').textContent = formatDate(now);
}

// =============================================================================
// Intro animation
// =============================================================================
function runIntro() {
  console.log('[INTRO] Starting intro animation');
  playSound('intro');

  const lines = ['FABLAB', 'MANAGEMENT', 'SYSTEM'];
  const container = document.getElementById('intro-text');
  container.innerHTML = '';

  let lineIdx = 0;
  let charIdx = 0;
  let currentLineEl = null;

  const cursor = document.createElement('span');
  cursor.className = 'cursor-blink';
  cursor.textContent = '█';

  function typeNext() {
    if (lineIdx >= lines.length) {
      cursor.remove();
      // All lines typed — wait, then show clock
      setTimeout(() => {
        console.log('[INTRO] Complete — transitioning to clock screen');
        showScreen('clock');
      }, 1600);
      return;
    }

    const line = lines[lineIdx];

    if (charIdx === 0) {
      // Start a new line, move cursor into it
      currentLineEl = document.createElement('div');
      currentLineEl.className = 'intro-line';
      container.appendChild(currentLineEl);
      currentLineEl.appendChild(cursor); // appendChild moves cursor if already in DOM
    }

    if (charIdx < line.length) {
      const textNode = document.createTextNode(line[charIdx]);
      currentLineEl.insertBefore(textNode, cursor);
      playRandomKeyPressSound();
      charIdx++;
      setTimeout(typeNext, 55 + Math.random() * 65);
    } else {
      // Line complete — move to next
      lineIdx++;
      charIdx = 0;
      setTimeout(typeNext, 300);
    }
  }

  setTimeout(typeNext, 550);
}

// =============================================================================
// Build activity grid buttons
// =============================================================================
function buildActivityGrid() {
  const grid = document.getElementById('activity-grid');
  ACTIVITIES.forEach(activity => {
    const btn = document.createElement('button');
    btn.className = 'activity-btn';
    btn.textContent = activity;
    btn.addEventListener('click', () => {
      console.log('[UI] Activity button pressed:', activity);
      selectActivity(activity);
    });
    grid.appendChild(btn);
  });
  console.log('[INIT] Activity grid built with', ACTIVITIES.length, 'options');
}

// =============================================================================
// Build simulation panel (REMOVE IN FINAL PRODUCT)
// =============================================================================
function buildSimPanel() {
  const panel = document.getElementById('sim-panel');

  TEST_USERS.forEach(user => {
    const btn = document.createElement('button');
    btn.className = 'sim-btn';
    btn.textContent = 'NFC: ' + buildFullName(user);
    btn.addEventListener('click', () => {
      console.log('[SIM] Simulating NFC press — user:', buildFullName(user), '(', user.id, ')');
      handleNfcRead(user.id);
    });
    panel.appendChild(btn);
  });

  // Buttons for real physical NFC cards
  REAL_NFC_TAGS.forEach(tag => {
    const btn = document.createElement('button');
    btn.className = 'sim-btn';
    btn.textContent = 'NFC: ' + tag.label;
    btn.addEventListener('click', () => {
      console.log('[SIM] Simulating real NFC card:', tag.label, '(', tag.id, ')');
      handleNfcRead(tag.id);
    });
    panel.appendChild(btn);
  });

  // Button that simulates user-not-found (unknown tag) flow
  const missingUserBtn = document.createElement('button');
  missingUserBtn.className = 'sim-btn';
  missingUserBtn.textContent = 'NFC: NOT FOUND';
  missingUserBtn.addEventListener('click', () => {
    console.log('[SIM] Simulating unknown NFC card (not found)');
    handleNfcRead('UNKNOWN999');
  });
  panel.appendChild(missingUserBtn);

  // Button that simulates an NFC read/server failure
  const failBtn = document.createElement('button');
  failBtn.className = 'sim-btn sim-btn-fail';
  failBtn.textContent = 'NFC: ERROR';
  failBtn.addEventListener('click', () => {
    console.log('[SIM] Simulating NFC error flow');
    simulateNfcReadError();
  });
  panel.appendChild(failBtn);

  console.log('[INIT] Simulation panel built (' +
    TEST_USERS.length + ' test users + ' +
    REAL_NFC_TAGS.length + ' real cards + ' +
    '1 not-found + 1 error button)');
}

async function simulateNfcReadError() {
  if (nfcFlowInProgress) {
    console.warn('[SIM] Ignoring NFC error simulation while flow is active.');
    return;
  }
  nfcFlowInProgress = true;
  setClockState('');
  clearNfcFeedback();
  flashScanCrescendo();
  await runNfcScramble(350, 50);
  playErrorBeep();
  setClockHelperMessage('Napaka NFC branja. Poskusi znova.', 'red', 2500);
  setClockState('error-state');
  await sleep(1600);
  setClockState('');
  nfcFlowInProgress = false;
}

// =============================================================================
// Admin mode — password screen
// =============================================================================
function showAdminPasswordScreen() {
  const input = document.getElementById('admin-pass-input');
  input.value = '';
  showScreen('admin-password');
  setTimeout(() => input.focus(), 100);
}

async function submitAdminPassword() {
  const input    = document.getElementById('admin-pass-input');
  const password = input.value;

  if (!password) {
    input.focus();
    return;
  }

  try {
    const result = await apiFetch('POST', '/api/admin/verify', { adminPassword: password });
    if (!result.ok) {
      playErrorBeep();
      input.value = '';
      input.focus();
      return;
    }
  } catch (err) {
    playErrorBeep();
    console.error('[ADMIN] Password verify error:', err.message);
    input.value = '';
    input.focus();
    return;
  }

  adminPassword = password;
  console.log('[ADMIN] Admin login successful.');
  showAdminPanel();
}

function cancelAdminPassword() {
  showScreen('clock');
}

// =============================================================================
// Admin mode — main panel
// =============================================================================
function showAdminPanel() {
  showAdminView('menu');
  showScreen('admin');
  console.log('[ADMIN] Admin panel open.');
}

function exitAdmin() {
  adminAddUserMode = false;
  adminPassword    = null;
  console.log('[ADMIN] Admin session ended.');
  showScreen('clock');
}

function showAdminView(viewName) {
  ['menu', 'adduser', 'deluser', 'logs'].forEach(v => {
    const el = document.getElementById('admin-view-' + v);
    if (el) el.style.display = v === viewName ? '' : 'none';
  });
}

// =============================================================================
// Admin — Add user flow
// =============================================================================
function startAdminAddUser() {
  showAdminView('adduser');
  adminAddUserMode = true;
  document.getElementById('admin-adduser-status').textContent =
    'PRISLONI KARTO ZA DODAJANJE...';
  console.log('[ADMIN] Waiting for NFC card to add...');
}

function cancelAdminAddUser() {
  adminAddUserMode = false;
  showAdminView('menu');
}

async function handleAdminAddUserScan(normId) {
  // Check if already registered
  let existingUser;
  try {
    const r = await apiFetch('GET', '/api/user/' + encodeURIComponent(normId));
    existingUser = r.found ? r.user : null;
  } catch (err) {
    console.error('[ADMIN] Error checking user:', err.message);
    playErrorBeep();
    showAdminStatus('Napaka strežnika.', 'red');
    showAdminView('menu');
    return;
  }

  if (existingUser) {
    console.log('[ADMIN] Card already registered:', existingUser.name);
    showAdminStatus('Kartica že registrirana: ' + existingUser.name, 'red');
    showAdminView('menu');
    return;
  }

  // Unknown card — open name entry in admin context
  pendingNfcId = normId;
  nameEntryFlow = { mode: 'collect-name', context: 'admin', fullName: '' };
  showNameEntry();
}

// =============================================================================
// Admin — Delete user view
// =============================================================================
async function showAdminDeleteUsers() {
  showAdminView('deluser');
  const listEl = document.getElementById('admin-deluser-list');
  listEl.innerHTML = '<div style="color:var(--dim);letter-spacing:.15em;font-size:.8rem">Nalaganje...</div>';

  let users;
  try {
    const data = await apiFetch('GET', '/api/users', undefined,
      { 'X-Admin-Password': adminPassword });
    users = data.users || [];
  } catch (err) {
    console.error('[ADMIN] Failed to load users:', err.message);
    listEl.innerHTML = '<div style="color:var(--red)">Napaka pri nalaganju.</div>';
    return;
  }

  listEl.innerHTML = '';
  if (!users.length) {
    listEl.innerHTML = '<div style="color:var(--dim);letter-spacing:.12em;font-size:.8rem">Ni registriranih kartic.</div>';
    return;
  }

  users.forEach(user => {
    const row = document.createElement('div');
    row.className = 'admin-user-row';

    const nameEl = document.createElement('span');
    nameEl.className = 'admin-user-name';
    nameEl.textContent = buildFullName(user);

    const nfcEl = document.createElement('span');
    nfcEl.className = 'admin-user-nfc';
    nfcEl.textContent = user.nfc_id;

    const delBtn = document.createElement('button');
    delBtn.className = 'admin-user-delete';
    delBtn.textContent = 'IZBRIŠI';
    delBtn.addEventListener('click', () => confirmDeleteUser(user, row));

    row.appendChild(nameEl);
    row.appendChild(nfcEl);
    row.appendChild(delBtn);
    listEl.appendChild(row);
  });
}

async function confirmDeleteUser(user, rowEl) {
  const fullName = buildFullName(user);
  if (!confirm('Izbriši ' + fullName + ' (' + user.nfc_id + ')?')) return;

  try {
    await apiFetch('DELETE', '/api/user/' + encodeURIComponent(user.nfc_id),
      undefined, { 'X-Admin-Password': adminPassword });
    rowEl.remove();
    showAdminStatus(fullName + ' izbrisan.', 'green');
    console.log('[ADMIN] Deleted user:', fullName);
  } catch (err) {
    console.error('[ADMIN] Delete failed:', err.message);
    showAdminStatus('Napaka pri brisanju.', 'red');
  }
}

// =============================================================================
// Admin — Logs view
// =============================================================================
async function showAdminLogs(page) {
  showAdminView('logs');
  adminLogsPage = page || 1;
  const tableEl = document.getElementById('admin-logs-table');
  tableEl.innerHTML = '<tr><td style="color:var(--dim);letter-spacing:.12em">Nalaganje...</td></tr>';

  let data;
  try {
    data = await apiFetch(
      'GET',
      '/api/sessions?page=' + adminLogsPage + '&per=' + ADMIN_LOGS_PER_PAGE,
      undefined,
      { 'X-Admin-Password': adminPassword }
    );
  } catch (err) {
    console.error('[ADMIN] Failed to load sessions:', err.message);
    tableEl.innerHTML = '<tr><td style="color:var(--red)">Napaka.</td></tr>';
    return;
  }

  adminLogsTotal = data.total || 0;
  const totalPages = Math.max(1, Math.ceil(adminLogsTotal / ADMIN_LOGS_PER_PAGE));

  // Header
  tableEl.innerHTML = '';
  const thead = tableEl.createTHead();
  const hrow  = thead.insertRow();
  ['#', 'Ime', 'Datum', 'Vstop', 'Izstop', 'Min', 'Aktivnost'].forEach(h => {
    const th = document.createElement('th');
    th.textContent = h;
    hrow.appendChild(th);
  });

  const tbody = tableEl.createTBody();
  (data.sessions || []).forEach(s => {
    const tr = tbody.insertRow();
    const logoutTime = s.logout_time ? new Date(s.logout_time) : null;
    const loginTime  = new Date(s.login_time);
    const fmt = d => String(d.getHours()).padStart(2,'0') + ':' + String(d.getMinutes()).padStart(2,'0');
    [
      s.id,
      s.name,
      s.date,
      fmt(loginTime),
      logoutTime ? fmt(logoutTime) : '—',
      s.duration_sec ? Math.round(s.duration_sec / 60) : '—',
      s.activity || '—',
    ].forEach(val => {
      const td = tr.insertCell();
      td.textContent = val;
    });
  });

  // Navigation
  document.getElementById('admin-logs-page').textContent =
    adminLogsPage + ' / ' + totalPages;
  const prevBtn = document.getElementById('admin-logs-prev');
  const nextBtn = document.getElementById('admin-logs-next');
  prevBtn.disabled = adminLogsPage <= 1;
  nextBtn.disabled = adminLogsPage >= totalPages;
}

// =============================================================================
// Admin — Export CSV
// =============================================================================
async function exportAdminCSV() {
  try {
    const res = await fetch('/api/sessions/export', {
      method: 'POST',
      headers: {
        'Content-Type':    'application/json',
        'X-Admin-Password': adminPassword,
      },
      body: JSON.stringify({ adminPassword }),
    });
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const blob = await res.blob();
    const url  = URL.createObjectURL(blob);
    const a    = document.createElement('a');
    a.href     = url;
    a.download = 'fablab-sessions.csv';
    a.click();
    URL.revokeObjectURL(url);
    showAdminStatus('CSV izvožen.', 'green');
    console.log('[ADMIN] CSV export complete.');
  } catch (err) {
    console.error('[ADMIN] CSV export failed:', err.message);
    showAdminStatus('Napaka pri izvozu.', 'red');
  }
}

// =============================================================================
// Initialisation
// =============================================================================
window.addEventListener('load', async () => {
  console.log('=== FabLab Management System starting ===');
  console.log('[INIT] Test users:', JSON.stringify(TEST_USERS, null, 2));
  console.log('[INIT] Activities:', ACTIVITIES.join(', '));

  await loadConfig();

  buildActivityGrid();
  buildSimPanel();

  // Wire up name-entry screen
  document.getElementById('name-entry-submit').addEventListener('click', submitNameEntry);
  document.getElementById('name-entry-cancel').addEventListener('click', cancelNameEntry);
  document.getElementById('name-entry-input').addEventListener('keydown', e => {
    if (e.key === 'Enter') submitNameEntry();
    if (e.key === 'Escape') cancelNameEntry();
  });

  // Wire up admin password screen
  document.getElementById('admin-pass-submit').addEventListener('click', submitAdminPassword);
  document.getElementById('admin-pass-cancel').addEventListener('click', cancelAdminPassword);
  document.getElementById('admin-pass-input').addEventListener('keydown', e => {
    if (e.key === 'Enter')  submitAdminPassword();
    if (e.key === 'Escape') cancelAdminPassword();
  });

  // Wire up admin panel
  document.getElementById('admin-exit').addEventListener('click', exitAdmin);
  document.getElementById('admin-btn-adduser').addEventListener('click', startAdminAddUser);
  document.getElementById('admin-btn-deluser').addEventListener('click', showAdminDeleteUsers);
  document.getElementById('admin-btn-logs').addEventListener('click', () => showAdminLogs(1));
  document.getElementById('admin-btn-export').addEventListener('click', exportAdminCSV);
  document.getElementById('admin-adduser-cancel').addEventListener('click', cancelAdminAddUser);
  document.getElementById('admin-deluser-back').addEventListener('click', () => showAdminView('menu'));
  document.getElementById('admin-logs-back').addEventListener('click', () => showAdminView('menu'));
  document.getElementById('admin-logs-prev').addEventListener('click', () => showAdminLogs(adminLogsPage - 1));
  document.getElementById('admin-logs-next').addEventListener('click', () => showAdminLogs(adminLogsPage + 1));

  // Global keyboard shortcut: press 'A' on clock screen to open admin
  document.addEventListener('keydown', e => {
    const activeScreen = document.querySelector('.screen.active');
    if (!activeScreen) return;
    const screenId = activeScreen.id;

    if (e.key === 'a' || e.key === 'A') {
      // Only trigger from clock screen and not when typing in inputs
      if (screenId === 'screen-clock' && document.activeElement.tagName !== 'INPUT') {
        e.preventDefault();
        console.log('[ADMIN] Admin key pressed — showing password screen.');
        showAdminPasswordScreen();
      }
    }
    if (e.key === 'Escape') {
      if (screenId === 'screen-admin' || screenId === 'screen-admin-password') {
        exitAdmin();
      }
    }
  });

  setInterval(updateClock, 1000);
  updateClock();

  const startButton = document.getElementById('start-button');
  startButton.addEventListener('click', () => {
    showScreen('intro');
    runIntro();
    refreshLogPanel();
  });
});
