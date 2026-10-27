// The Logs page: recording of the signals to the microSD card (tasks/logger), the conditions to start,
// the list of files.
const $ = (id) => document.getElementById(id);
const MODES = ['manual', 'boot', 'condition'];

let st = { state: 'no_card', error: '', file: '', records: 0, bytes: 0, seconds: 0, total_kb: 0, free_kb: 0 };
let cfg = { mode: 'manual', signal: '', op: '>', value: 500, stop_delay: 10 };
let signals = [];
let files = [];
let filesLoaded = false;
let dirty = false;
let busy = false;
let checking = true;   // the card is being looked for (when the page opens)

const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const fmtBytes = (n) => (n >= 1048576 ? (n / 1048576).toFixed(1) + ' MB' : n >= 1024 ? Math.round(n / 1024) + ' KB' : n + ' B');
const fmtKb = (kb) => (kb >= 1048576 ? (kb / 1048576).toFixed(1) + ' GB' : kb >= 1024 ? Math.round(kb / 1024) + ' MB' : kb + ' KB');
const fmtTime = (unix) => (unix > 1700000000 ? new Date(unix * 1000).toLocaleString() : '-');

// The board has no clock with its own power: the time of the browser goes to it, for the dates of the files.
function syncClock() {
  fetch('/api/log/time', { method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ unix: Math.floor(Date.now() / 1000) }) }).catch(() => {});
}

// ---- drawing ---------------------------------------------------------------------------------------
function drawState() {
  const b = $('stateBadge');
  const looking = checking && st.state === 'no_card';
  b.textContent = looking ? t('log_checking') : t('log_state_' + st.state);
  b.className = 'badge ' + (looking ? 'bg-info text-dark' : ({ no_card: 'bg-secondary', idle: 'bg-success', recording: 'bg-danger' }[st.state] || 'bg-secondary'));
  const sec = st.seconds, dur = Math.floor(sec / 60) + ':' + String(sec % 60).padStart(2, '0');
  $('recText').textContent = st.state === 'recording' ? st.file + ' - ' + dur + ', ' + fmtBytes(st.bytes) : '';
  $('freeText').textContent = st.state !== 'no_card' && st.total_kb ? t('log_free') + ': ' + fmtKb(st.free_kb) + ' / ' + fmtKb(st.total_kb) : '';
  const err = st.error ? t('log_err_' + st.error) : '';
  $('errText').style.display = err ? '' : 'none';
  $('errText').innerHTML = err ? '<i class="fas fa-triangle-exclamation me-1"></i>' + esc(err) : '';
  $('start').disabled = busy || st.state === 'recording';
  $('stop').disabled = busy || st.state !== 'recording';
  $('rescan').disabled = busy || checking || st.state === 'recording';
  $('reloadFiles').disabled = st.state === 'no_card';
}

function drawCfg() {
  $('modes').innerHTML = MODES.map((m) =>
    `<div class="col-12 col-md-4"><button type="button" class="btn btn-sm w-100 text-start lh-sm ${cfg.mode === m ? 'btn-accent' : 'btn-outline-light'}" data-mode="${m}">` +
    `<span class="fw-semibold">${t('log_mode_' + m)}</span><br><small>${t('log_mode_' + m + '_hint')}</small></button></div>`).join('');
  $('condBox').style.display = cfg.mode === 'condition' ? '' : 'none';
  const names = signals.includes(cfg.signal) || !cfg.signal ? signals : [...signals, cfg.signal];
  $('condSignal').innerHTML = (cfg.signal ? '' : '<option value="" selected>-</option>') +
    names.map((n) => `<option value="${esc(n)}" ${n === cfg.signal ? 'selected' : ''}>${esc(n)}${signals.includes(n) ? '' : ' ⚠'}</option>`).join('');
  $('condOp').value = cfg.op;
  if (document.activeElement !== $('condValue')) $('condValue').value = Number.isNaN(cfg.value) ? '' : cfg.value;
  if (document.activeElement !== $('condDelay')) $('condDelay').value = Number.isNaN(cfg.stop_delay) ? '' : cfg.stop_delay;
  $('condText').textContent = cfg.signal && !Number.isNaN(cfg.value)
    ? t('log_cond_text', { cond: cfg.signal + ' ' + cfg.op + ' ' + cfg.value, n: Number.isNaN(cfg.stop_delay) ? '?' : Math.round(cfg.stop_delay) }) : '';
  $('dirty').style.display = dirty ? '' : 'none';
  $('apply').disabled = !dirty || cfgInvalid();
}

function cfgInvalid() {
  if (Number.isNaN(cfg.value) || Number.isNaN(cfg.stop_delay) || cfg.stop_delay < 0 || cfg.stop_delay > 3600) return true;
  return cfg.mode === 'condition' && !cfg.signal;
}

function drawFiles() {
  $('fileCount').textContent = filesLoaded ? '(' + files.length + ')' : '';
  $('filesNote').textContent = st.state === 'no_card' ? t('log_no_card_files') : filesLoaded && !files.length ? t('log_empty') : '';
  $('files').innerHTML = files.map((f) => {
    const active = st.state === 'recording' && st.file.toLowerCase() === f.name;
    return `<div class="file-row"><div class="flex-grow-1"><div class="font-monospace">${esc(f.name)}</div>` +
      `<div class="small text-muted">${fmtTime(f.time)} - ${fmtBytes(f.size)}</div></div>` +
      (active ? `<span class="badge bg-danger">${t('log_recording_badge')}</span>` : '') +
      `<a class="btn btn-sm btn-outline-secondary" href="/log/${encodeURIComponent(f.name)}" title="${t('log_download')}"><i class="fas fa-download"></i></a>` +
      `<button class="btn btn-sm btn-outline-secondary" data-del="${esc(f.name)}" ${active ? 'disabled' : ''} title="${t('log_delete')}"><i class="fas fa-trash"></i></button></div>`;
  }).join('');
}

function drawAll() { drawState(); drawCfg(); drawFiles(); }

// ---- the board -------------------------------------------------------------------------------------
async function poll(withCfg) {
  if (document.hidden && !withCfg) return;
  try {
    const res = await fetch('/api/log', { cache: 'no-store' });
    if (!res.ok) return;
    const d = await res.json();
    const was = st.state;
    st = { state: d.state, error: d.error || '', file: d.file || '', records: d.records | 0, bytes: d.bytes | 0,
      seconds: d.seconds | 0, total_kb: d.total_kb || 0, free_kb: d.free_kb || 0 };
    if (withCfg || !dirty) {
      cfg = { mode: MODES.includes(d.mode) ? d.mode : 'manual', signal: d.signal || '', op: d.op === '<' ? '<' : '>',
        value: typeof d.value === 'number' ? d.value : 0, stop_delay: typeof d.stop_delay === 'number' ? d.stop_delay : 10 };
    }
    // a card appeared or a recording ended (also by the condition): the list changed
    if (!withCfg && ((was === 'no_card' && st.state !== 'no_card') || (was === 'recording') !== (st.state === 'recording'))) loadFiles();
    if (st.state === 'no_card') { files = []; filesLoaded = false; }
    drawAll();
  } catch (e) { /* the board is busy - the next poll */ }
}

async function loadSignals() {
  const r = await api('GET', '/api/layout');
  if (!r.ok) return;
  const d = typeof r.data === 'string' ? JSON.parse(r.data) : r.data;
  signals = (Array.isArray(d.signals) ? d.signals.map((s) => s.name) : []).filter(Boolean);
}

async function loadFiles() {
  try {
    const res = await fetch('/api/log/files', { cache: 'no-store' });
    if (res.status === 409) { files = []; filesLoaded = false; drawFiles(); return; }
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const d = await res.json();
    files = Array.isArray(d.files) ? d.files : [];
    filesLoaded = true;
    drawFiles();
  } catch (e) {
    toast(t('log_err_files') + ': ' + e.message, false);
  }
}

// A request that changes something; then the state is read again.
async function act(fn, wait) {
  if (busy) return;
  busy = true; drawState();
  try {
    const r = await fn();
    if (r && !r.ok && r.status !== 401) toast(t('failed') + ': ' + errText(r), false);
    if (r && r.ok && wait) {
      await new Promise((x) => setTimeout(x, wait));
      await poll(false);
    }
  } finally { busy = false; drawAll(); }
}

$('start').onclick = () => act(() => api('POST', '/api/log/start'), 400);
$('stop').onclick = () => act(() => api('POST', '/api/log/stop'), 400);
$('rescan').onclick = () => act(async () => {
  const r = await api('POST', '/api/log/rescan');
  if (r.ok) { await new Promise((x) => setTimeout(x, 1500)); await poll(false); if (st.state !== 'no_card') await loadFiles(); }
  return r.ok ? null : r;
});
$('reloadFiles').onclick = () => loadFiles();

$('modes').addEventListener('click', (e) => {
  const b = e.target.closest('[data-mode]');
  if (!b || cfg.mode === b.dataset.mode) return;
  cfg.mode = b.dataset.mode;
  if (cfg.mode === 'condition' && !cfg.signal) cfg.signal = signals.includes('rpm') ? 'rpm' : (signals[0] || '');
  dirty = true; drawCfg();
});
$('condSignal').onchange = (e) => { cfg.signal = e.target.value; dirty = true; drawCfg(); };
$('condOp').onchange = (e) => { cfg.op = e.target.value; dirty = true; drawCfg(); };
$('condValue').oninput = (e) => { cfg.value = e.target.value === '' ? NaN : parseFloat(e.target.value); dirty = true; drawCfg(); };
$('condDelay').oninput = (e) => { cfg.stop_delay = e.target.value === '' ? NaN : parseFloat(e.target.value); dirty = true; drawCfg(); };

$('apply').onclick = () => {
  if (cfgInvalid()) return;
  act(async () => {
    const r = await api('POST', '/api/log', { mode: cfg.mode, signal: cfg.signal, op: cfg.op, value: cfg.value, stop_delay: Math.round(cfg.stop_delay) });
    if (r.ok) { dirty = false; toast(t('log_applied')); return null; }
    return r;
  });
};

$('files').addEventListener('click', (e) => {
  const b = e.target.closest('[data-del]');
  if (!b) return;
  const name = b.dataset.del;
  if (!confirm(t('log_delete_confirm', { name }))) return;
  act(async () => {
    const r = await api('POST', '/api/log/delete?name=' + encodeURIComponent(name));
    if (r.ok) { await loadFiles(); await poll(false); return null; }
    return r;
  });
});

function onLangChange() { drawAll(); }

// The card is looked for as soon as the page opens (in the "manual" mode the dash does not do it by itself):
// a request to mount it, then the state is read again once the dash has had time to try.
async function checkCard() {
  checking = true; drawState();
  await api('POST', '/api/log/rescan');
  await new Promise((x) => setTimeout(x, 1800));
  checking = false;
  await poll(false);
  if (st.state !== 'no_card') loadFiles();
  drawAll();
}

navbar('/logs.html');
syncClock();
(async () => {
  await Promise.all([loadSignals(), poll(true)]);
  drawAll();
  await checkCard();
})();
setInterval(() => poll(false), 2000);
// back to the page (the card could have been put in meanwhile)
document.addEventListener('visibilitychange', () => { if (!document.hidden && st.state === 'no_card' && !checking) checkCard(); });
