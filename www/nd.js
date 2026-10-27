// Shared code of the nDash pages: language (RU/EN), theme, the settings password
// (a lock button and an unlock dialog), API calls, a toast and the navigation bar.
// Texts live in i18n.js; static HTML marks them with data-i18n / data-i18n-title /
// data-i18n-placeholder.

// ---------------------------------------------------------------------------
// Language
// ---------------------------------------------------------------------------
const ND_LANG_KEY = 'nd_lang';
let ndLang = (() => {
  const saved = localStorage.getItem(ND_LANG_KEY);
  if (saved === 'ru' || saved === 'en') return saved;
  return (navigator.language || 'en').toLowerCase().startsWith('ru') ? 'ru' : 'en';
})();
document.documentElement.lang = ndLang;

// The text for `key` in the current language; {name} placeholders are filled from `p`.
function t(key, p) {
  const s = (ND_I18N[ndLang] && ND_I18N[ndLang][key]) || ND_I18N.en[key] || key;
  return p ? s.replace(/\{(\w+)\}/g, (_, k) => (p[k] === undefined ? '' : p[k])) : s;
}

function applyI18n(root = document) {
  root.querySelectorAll('[data-i18n]').forEach((e) => { e.textContent = t(e.dataset.i18n); });
  root.querySelectorAll('[data-i18n-html]').forEach((e) => { e.innerHTML = t(e.dataset.i18nHtml); });
  root.querySelectorAll('[data-i18n-title]').forEach((e) => { e.title = t(e.dataset.i18nTitle); });
  root.querySelectorAll('[data-i18n-placeholder]').forEach((e) => { e.placeholder = t(e.dataset.i18nPlaceholder); });
  document.title = t(document.body.dataset.title || 'title_main');
}

function toggleLang() {
  ndLang = ndLang === 'ru' ? 'en' : 'ru';
  localStorage.setItem(ND_LANG_KEY, ndLang);
  document.documentElement.lang = ndLang;
  applyI18n();
  renderNavbar();
  if (typeof onLangChange === 'function') onLangChange();   // a page redraws its generated parts
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
const ND_THEME_KEY = 'nd_theme';
(function initTheme() {
  const saved = localStorage.getItem(ND_THEME_KEY);
  const theme = saved === 'light' || saved === 'dark' ? saved
    : (window.matchMedia && window.matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark');
  document.documentElement.setAttribute('data-theme', theme);
})();

function toggleTheme() {
  const next = document.documentElement.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';
  localStorage.setItem(ND_THEME_KEY, next);
  document.documentElement.setAttribute('data-theme', next);
  renderNavbar();
}

// ---------------------------------------------------------------------------
// Settings password
// ---------------------------------------------------------------------------
const ND_AUTH_KEY = 'nd_auth_header';
let authRequired = false;

async function checkAuthStatus() {
  try {
    const res = await fetch('/api/auth/status');
    authRequired = res.ok && !!(await res.json()).enabled;
  } catch (e) { authRequired = false; }
  renderNavbar();
}
const authUnlocked = () => !authRequired || !!sessionStorage.getItem(ND_AUTH_KEY);

// Shows the password dialog; resolves true once the password has been accepted.
function askPassword() {
  return new Promise((resolve) => {
    let box = document.getElementById('unlock');
    if (!box) {
      box = document.createElement('div');
      box.id = 'unlock';
      box.className = 'unlock-overlay';
      box.innerHTML = `<div class="unlock-modal">
        <h6 class="mb-3"><i class="fas fa-lock me-2"></i><span data-i18n="auth_unlock_title"></span></h6>
        <input type="password" class="form-control form-control-sm mb-2" id="unlockPass" data-i18n-placeholder="auth_unlock_password">
        <div class="small text-danger mb-2" id="unlockErr" style="display:none"></div>
        <div class="d-flex gap-2">
          <button class="btn btn-sm btn-accent" id="unlockOk" data-i18n="auth_unlock_submit"></button>
          <button class="btn btn-sm btn-outline-secondary" id="unlockCancel" data-i18n="cancel"></button>
        </div></div>`;
      document.body.appendChild(box);
    }
    applyI18n(box);
    const pass = box.querySelector('#unlockPass'), err = box.querySelector('#unlockErr');
    pass.value = ''; err.style.display = 'none'; box.style.display = 'flex'; pass.focus();
    const done = (ok) => { box.style.display = 'none'; resolve(ok); };
    box.querySelector('#unlockCancel').onclick = () => done(false);
    box.onclick = (e) => { if (e.target === box) done(false); };
    const submit = async () => {
      if (!pass.value) return;
      const header = 'Basic ' + btoa('nd:' + pass.value);
      const res = await fetch('/api/auth/verify', { method: 'POST', headers: { Authorization: header } });
      if (res.ok) { sessionStorage.setItem(ND_AUTH_KEY, header); renderNavbar(); done(true); }
      else { err.textContent = t('auth_unlock_error'); err.style.display = 'block'; }
    };
    box.querySelector('#unlockOk').onclick = submit;
    pass.onkeyup = (e) => { if (e.key === 'Enter') submit(); };
  });
}

function toggleLock() {
  if (authUnlocked() && authRequired) { sessionStorage.removeItem(ND_AUTH_KEY); renderNavbar(); }
  else askPassword();
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
// Fetches `url`. Modifying requests carry the cached password; on a 401 the user is asked
// for it and the request is repeated. Returns {ok, status, data}: data is parsed JSON when
// the reply is JSON, otherwise the text.
async function api(method, url, body) {
  const send = async () => {
    const headers = {};
    const cached = sessionStorage.getItem(ND_AUTH_KEY);
    if (cached) headers['Authorization'] = cached;
    const opts = { method, headers };
    if (body !== undefined) {
      if (body instanceof ArrayBuffer) {         // a file: sent as it is
        headers['Content-Type'] = 'application/octet-stream';
        opts.body = body;
      } else {
        headers['Content-Type'] = 'application/json';
        opts.body = typeof body === 'string' ? body : JSON.stringify(body);
      }
    }
    return fetch(url, opts);
  };
  let res = await send();
  if (res.status === 401) {
    sessionStorage.removeItem(ND_AUTH_KEY);
    authRequired = true;
    renderNavbar();
    if (!(await askPassword())) return { ok: false, status: 401, data: t('auth_required') };
    res = await send();
  }
  const text = await res.text();
  let data = text;
  try { data = JSON.parse(text); } catch (e) { /* plain text */ }
  return { ok: res.ok, status: res.status, data };
}

let toastTimer = null;
function toast(msg, ok = true) {
  let el = document.getElementById('toast');
  if (!el) {
    el = document.createElement('div');
    el.id = 'toast';
    document.body.appendChild(el);
  }
  el.textContent = msg;
  el.className = ok ? 'ok' : 'err';
  el.style.display = 'block';
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.style.display = 'none'; }, 3500);
}

// Tables that turn into cards on a phone (class "stack"): every cell gets the text of its column
// header, which the CSS shows in front of the value. Call after the rows are redrawn.
function stackLabels(table) {
  const heads = [...table.tHead.rows[0].cells].map((th) => th.textContent.trim());
  table.tBodies[0].querySelectorAll('tr').forEach((tr) => [...tr.cells].forEach((td, i) => { td.dataset.label = heads[i] || ''; }));
}

// The reply of a failed request as a short text.
function errText(r) {
  if (typeof r.data === 'string' && r.data.length < 160) return r.data;
  return t('error') + ' ' + r.status;
}

// ---------------------------------------------------------------------------
// Navigation bar
// ---------------------------------------------------------------------------
let navActive = '/';
// The menu. The pages are in the order of the work: look at the dash, draw its screens, load
// pictures | CAN, the logs, the bench | access, update. On a computer it is the top bar; on a phone the
// top bar only names the page and the main sections sit in a tab bar at the bottom, the rest
// behind "More".
const NAV_PAGES = [
  { href: '/', key: 'nav_home', icon: 'fa-house' },
  { href: '/layout.html', key: 'nav_screens', icon: 'fa-display' },
  { href: '/media.html', key: 'nav_media', icon: 'fa-image' },
  { href: '/can.html', key: 'nav_can', icon: 'fa-plug' },
  { href: '/logs.html', key: 'nav_logs', icon: 'fa-sd-card' },
  { href: '/bench.html', key: 'nav_bench', icon: 'fa-screwdriver-wrench' },
  { href: '/access.html', key: 'nav_access', icon: 'fa-key' },
  { href: '/ota', key: 'nav_update', icon: 'fa-cloud-arrow-up' },
];
const NAV_DIVIDE_AFTER = [2, 5];   // thin lines between the groups of the top bar
const NAV_TABS = [0, 1, 3, 4];     // the tab bar of a phone; the others are under "More"

function renderNavbar() {
  const link = (pg, cls) => `<a class="${cls} ${pg.href === navActive ? 'active' : ''}" href="${pg.href}"><i class="fas ${pg.icon}"></i><span>${t(pg.key)}</span></a>`;
  const items = NAV_PAGES.map((pg, i) =>
    `<li class="nav-item">${link(pg, 'nav-link')}</li>` + (NAV_DIVIDE_AFTER.includes(i) ? '<li class="nav-sep"></li>' : '')).join('');
  const current = NAV_PAGES.find((pg) => pg.href === navActive) || NAV_PAGES[0];
  const dark = document.documentElement.getAttribute('data-theme') === 'dark';
  const lock = authRequired
    ? `<button class="btn btn-sm btn-outline-secondary" id="lockBtn" title="${t(authUnlocked() ? 'auth_lock_tooltip' : 'auth_unlock_tooltip')}">
         <i class="fas ${authUnlocked() ? 'fa-lock-open' : 'fa-lock'}"></i></button>` : '';
  document.getElementById('nav').innerHTML =
    `<nav class="navbar"><div class="container-fluid px-3 px-lg-4">
       <a class="navbar-brand" href="/"><i class="fas fa-gauge-high me-2"></i><span class="brand-name">nDash</span></a>
       <span class="page-title"><i class="fas ${current.icon} me-2"></i>${t(current.key)}</span>
       <ul class="navbar-nav ms-auto align-items-center">${items}</ul>
       <div class="d-flex align-items-center gap-2 ms-3">
         <button class="btn btn-sm btn-outline-secondary" id="themeBtn" title="${t('theme_toggle')}"><i class="fas ${dark ? 'fa-sun' : 'fa-moon'}"></i></button>
         <button class="btn btn-sm btn-outline-secondary" id="langBtn">${ndLang === 'ru' ? 'EN' : 'RU'}</button>
         ${lock}
       </div></div></nav>`;

  // the tab bar of a phone (it is in the page for every width; CSS shows it only on a phone)
  const inMore = !NAV_TABS.includes(NAV_PAGES.indexOf(current));
  let bar = document.getElementById('tabbar');
  if (!bar) { bar = document.createElement('div'); bar.id = 'tabbar'; document.body.appendChild(bar); }
  bar.innerHTML =
    '<div class="more-sheet" id="moreSheet" hidden>' +
      NAV_PAGES.filter((_, i) => !NAV_TABS.includes(i)).map((pg) => link(pg, 'more-link')).join('') + '</div>' +
    '<nav class="tabs">' + NAV_TABS.map((i) => link(NAV_PAGES[i], 'tab')).join('') +
      `<button type="button" class="tab ${inMore ? 'active' : ''}" id="moreBtn"><i class="fas fa-ellipsis"></i><span>${t('nav_more')}</span></button></nav>`;
  $moreBtn = document.getElementById('moreBtn');
  $moreBtn.onclick = (e) => { document.getElementById('moreSheet').hidden = !document.getElementById('moreSheet').hidden; e.stopPropagation(); };

  document.getElementById('themeBtn').onclick = toggleTheme;
  document.getElementById('langBtn').onclick = toggleLang;
  const lockBtn = document.getElementById('lockBtn');
  if (lockBtn) lockBtn.onclick = toggleLock;
}
let $moreBtn = null;
document.addEventListener('click', () => { const m = document.getElementById('moreSheet'); if (m) m.hidden = true; });

// Called by every page once its markup exists.
function navbar(active) {
  navActive = active;
  renderNavbar();
  applyI18n();
  checkAuthStatus();
}
