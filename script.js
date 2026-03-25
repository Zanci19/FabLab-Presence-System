// =============================================================================
// FabLab Management System — script.js
// =============================================================================

// --- Test data (replace with SQLite reads in final product) ---
const TEST_USERS = [
  { id: 'NFC001', name: 'Ana' },
  { id: 'NFC002', name: 'Bor' },
  { id: 'NFC003', name: 'Cene' },
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

// --- In-memory session store (key: "userId-YYYY-MM-DD") ---
// Will be replaced with SQLite in final product
const sessions = {};

// --- App state ---
let currentUser = null;
let helperMessageTimeout = null;
let nfcFlowInProgress = false;
const DEFAULT_HELPER_TEXT = 'Prisloni ključ za vstop/izstop...';
const DEFAULT_CONFIG = Object.freeze({ animationsEnabled: true });
let appConfig = { ...DEFAULT_CONFIG };

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

// =============================================================================
// Screen management
// =============================================================================
function showScreen(screenId) {
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

async function loadConfig() {
  try {
    const response = await fetch('settings.json', { cache: 'no-store' });
    if (!response.ok) {
      throw new Error('HTTP ' + response.status);
    }

    const data = await response.json();
    appConfig = {
      ...DEFAULT_CONFIG,
      ...data,
      animationsEnabled: data.animationsEnabled !== false,
    };
  } catch (error) {
    console.warn('[CONFIG] Could not load settings.json, using defaults:', error.message);
    appConfig = { ...DEFAULT_CONFIG };
  }

  applyAnimationMode();
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

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
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
// NFC read handler
// =============================================================================
async function handleNfcRead(nfcId) {
  if (nfcFlowInProgress) {
    console.warn('[NFC] Ignoring read while previous flow is active.');
    return;
  }
  nfcFlowInProgress = true;

  console.log('[NFC] Card presented. Raw ID:', nfcId);

  const user = TEST_USERS.find(u => u.id === nfcId);
  setClockState('');
  clearNfcFeedback();

  try {
    await runNfcScramble(500, 50);

    if (!user) {
      console.log('[NFC] Unknown card — not in database. ID:', nfcId);
      setClockHelperMessage('NFC ni uspel. Poskusi znova.');
      setClockState('error-state');
      await sleep(1600);
      setClockState('');
      setClockHelperMessage(DEFAULT_HELPER_TEXT);
      return;
    }

    console.log('[NFC] Recognized user:', user.name, '(', nfcId, ')');

    const today = getTodayKey();
    const sessionKey = user.id + '-' + today;
    const session = sessions[sessionKey];
    const isLogoutFlow = Boolean(session && session.loginTime && !session.logoutTime);

    setClockHelperMessage(user.name, isLogoutFlow ? 'orange' : 'white');
    setClockState(isLogoutFlow ? 'logout-state' : 'success-state');
    playSound('login');
    await sleep(900);
    setClockState('');

    if (isLogoutFlow) {
      doLogout(user, sessionKey);
    } else {
      doLogin(user, sessionKey);
    }
  } finally {
    nfcFlowInProgress = false;
  }
}

// =============================================================================
// Login
// =============================================================================
function doLogin(user, sessionKey) {
  const loginTime = new Date();
  console.log('[LOGIN] User:', user.name, '| Time:', formatTime(loginTime), '| Date:', getTodayKey());

  // Pre-create session with login time; activity will be filled on selection
  sessions[sessionKey] = {
    userId: user.id,
    name: user.name,
    date: getTodayKey(),
    loginTime: loginTime,
    activity: null,
    logoutTime: null,
  };
  console.log('[SESSION CREATED]', JSON.stringify(sessions[sessionKey], null, 2));

  currentUser = user;
  setClockHelperMessage(DEFAULT_HELPER_TEXT);
  showGreeting(user);
}

// =============================================================================
// Logout
// =============================================================================
function doLogout(user, sessionKey) {
  if (!sessions[sessionKey]) {
    console.warn('[LOGOUT] No session found for key:', sessionKey, '— aborting logout');
    showScreen('clock');
    currentUser = null;
    return;
  }

  const logoutTime = new Date();
  sessions[sessionKey].logoutTime = logoutTime;

  console.log('[LOGOUT] User:', user.name, '| Time:', formatTime(logoutTime), '| Date:', getTodayKey());
  console.log('[SESSION COMPLETE]', JSON.stringify(sessions[sessionKey], null, 2));

  playSound('logout');
  currentUser = null;
  showScreen('clock');
  setClockHelperMessage('Izpisan si. Nasvidenje!', 'white', 2500);
}

// =============================================================================
// Greeting + activity selection screen
// =============================================================================
function showGreeting(user) {
  console.log('[GREETING] Showing greeting for:', user.name);
  setClockState('');
  setClockHelperMessage(DEFAULT_HELPER_TEXT);
  document.getElementById('greeting-text').innerHTML =
    'Živjo ' + user.name + '.<br>Kaj delaš danes?';
  showScreen('greeting');
}

function selectActivity(activity) {
  const today = getTodayKey();
  const sessionKey = currentUser.id + '-' + today;

  sessions[sessionKey].activity = activity;

  console.log('[ACTIVITY SELECTED] User:', currentUser.name,
    '| Activity:', activity,
    '| Date:', today,
    '| Login time:', formatTime(sessions[sessionKey].loginTime));
  console.log('[SESSION UPDATED]', JSON.stringify(sessions[sessionKey], null, 2));

  playSound('topic-select');
  showScreen('clock');
  setClockHelperMessage('Vpisan si v FabLab.', 'white', 2500);
  currentUser = null;
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
  if (!appConfig.animationsEnabled) {
    document.getElementById('intro-text').innerHTML =
      '<div class="intro-line">FABLAB</div><div class="intro-line">MANAGEMENT</div><div class="intro-line">SYSTEM</div>';
    setTimeout(() => showScreen('clock'), 400);
    return;
  }
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
    btn.textContent = 'NFC: ' + user.name;
    btn.addEventListener('click', () => {
      console.log('[SIM] Simulating NFC press — user:', user.name, '(', user.id, ')');
      handleNfcRead(user.id);
    });
    panel.appendChild(btn);
  });

  // Button that simulates an unrecognised card
  const failBtn = document.createElement('button');
  failBtn.className = 'sim-btn sim-btn-fail';
  failBtn.textContent = 'NFC: ???';
  failBtn.addEventListener('click', () => {
    console.log('[SIM] Simulating unknown NFC card (ID: UNKNOWN999)');
    handleNfcRead('UNKNOWN999');
  });
  panel.appendChild(failBtn);

  console.log('[INIT] Simulation panel built (' + TEST_USERS.length + ' users + 1 fail button)');
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

  setInterval(updateClock, 1000);
  updateClock();

  const startButton = document.getElementById('start-button');
  startButton.addEventListener('click', () => {
    showScreen('intro');
    runIntro();
  });
});
