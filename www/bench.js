// The Bench page: buttons that make the rusEFI ECU test its outputs (coils, injectors, relays,
// fans...) or start/stop the engine, the table of those commands, and for checking the dash
// itself: the live signals (values can be pinned by a slider), the demo generator and a CAN frame
// sent by hand. A pinned value lasts hold_ms on the dash and is repeated while the page is open.
const $ = (id) => document.getElementById(id);

const RUNNING_RPM = 100;   // LND_BENCH_RUNNING_RPM: above it the engine counts as running
let cmds = [];             // the command table
let rpm = null;            // the rpm signal now, null - no data
let signals = [];          // the last reply
let demo = false;
let holdMs = 6000;
const held = new Map();    // name -> the value pinned from this page
let names = '';            // which rows are on the page now

function fmt(v, s) {
  if (v === null || v === undefined) return '--';
  const span = s.max - s.min;
  return span >= 100 ? String(Math.round(v)) : v.toFixed(span >= 10 ? 1 : 2);
}

function renderRows() {
  const key = signals.map((s) => s.name + s.min + s.max).join('|');
  if (key === names) return;                 // the same rows: only the numbers change (update())
  names = key;
  $('empty').style.display = signals.length ? 'none' : '';
  $('rows').innerHTML = signals.map((s, i) => {
    const step = (s.max - s.min) >= 100 ? 1 : 0.05;
    return `<tr data-i="${i}"><td>${s.name}</td>` +
      `<td><b data-v>--</b></td><td class="text-muted" data-age>-</td>` +
      `<td><input type="range" class="form-range" min="${s.min}" max="${s.max}" step="${step}" value="${(s.min + s.max) / 2}" data-slider></td>` +
      `<td><input class="form-check-input" type="checkbox" data-hold></td></tr>`;
  }).join('');
  stackLabels($('sigTable'));
}

function update() {
  const rows = $('rows').children;
  signals.forEach((s, i) => {
    const tr = rows[i];
    if (!tr) return;
    tr.querySelector('[data-v]').textContent = fmt(s.value, s);
    tr.querySelector('[data-age]').textContent = s.value === null ? t('bench_nodata') : (s.age < 1000 ? s.age + ' ' + t('u_ms') : Math.round(s.age / 1000) + ' ' + t('u_sec'));
    const hold = tr.querySelector('[data-hold]');
    if (!held.has(s.name)) hold.checked = s.held;          // pinned from another page: show it
  });
  $('demoText').textContent = t(demo ? 'bench_demo_off' : 'bench_demo_on');
  $('demo').className = 'btn btn-sm ms-auto ' + (demo ? 'btn-accent' : 'btn-outline-light');
}

async function poll() {
  const r = await fetch('/api/bench', { cache: 'no-store' }).then((x) => (x.ok ? x.json() : null)).catch(() => null);
  $('conn').textContent = r ? '' : t('bench_offline');
  $('conn').className = 'small text-warning';
  if (r) {
    signals = r.signals; demo = r.demo; holdMs = r.hold_ms;
    const rs = signals.find((s) => s.name === 'rpm');
    rpm = rs && rs.value !== null ? rs.value : null;
    renderRows(); update(); updateEngine();
  }
}

function pin(name, value) { return api('POST', '/api/bench/hold', { name, value }); }
function release(name) { held.delete(name); return api('POST', '/api/bench/hold', { name, release: true }); }

$('rows').addEventListener('input', (e) => {
  if (!e.target.matches('[data-slider]')) return;
  const tr = e.target.closest('tr'), s = signals[Number(tr.dataset.i)];
  held.set(s.name, Number(e.target.value));
  tr.querySelector('[data-hold]').checked = true;
  pin(s.name, Number(e.target.value));
});
$('rows').addEventListener('change', (e) => {
  if (!e.target.matches('[data-hold]')) return;
  const tr = e.target.closest('tr'), s = signals[Number(tr.dataset.i)];
  if (e.target.checked) {
    const v = Number(tr.querySelector('[data-slider]').value);
    held.set(s.name, v);
    pin(s.name, v);
  } else {
    release(s.name);
  }
});

// Keep the pins alive while the page is open.
setInterval(() => { for (const [name, v] of held) pin(name, v); }, Math.max(1000, holdMs / 3));

$('demo').onclick = async () => {
  const r = await api('POST', '/api/bench/demo', { on: !demo });
  if (r.ok) { demo = !demo; update(); } else if (r.status !== 401) toast(t('failed') + ': ' + errText(r), false);
};

// 0x1A8, 424 ...
function parseId(text) {
  const s = text.trim().toLowerCase();
  const n = s.startsWith('0x') ? parseInt(s.slice(2), 16) : Number(s);
  return Number.isInteger(n) && n >= 0 ? n : NaN;
}

$('canSend').onclick = async () => {
  const id = parseId($('canId').value), data = $('canData').value.replace(/\s+/g, '').toLowerCase();
  if (Number.isNaN(id) || !/^([0-9a-f]{2}){0,8}$/.test(data)) { toast(t('bench_can_bad'), false); return; }
  const r = await api('POST', '/api/bench/can', { id, ext: $('canExt').checked, data });
  if (r.ok) toast(t(r.data.sent ? 'bench_sent' : 'bench_not_sent'), r.data.sent);
  else if (r.status !== 401) toast(t('failed') + ': ' + errText(r), false);
};

// ---------------------------------------------------------------------------
// Commands: the buttons
// ---------------------------------------------------------------------------
const running = () => rpm !== null && rpm > RUNNING_RPM;
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

function renderCmds() {
  const groups = [];
  for (const c of cmds) {
    let g = groups.find((x) => x.name === c.group);
    if (!g) groups.push(g = { name: c.group, list: [] });
    g.list.push(c);
  }
  $('noCmds').style.display = cmds.length ? 'none' : '';
  $('groups').innerHTML = groups.map((g) =>
    `<div class="mb-3"><div class="small text-muted mb-1">${esc(g.name || '-')}</div><div class="d-flex flex-wrap gap-2">` +
    g.list.map((c) => `<button class="btn btn-sm btn-outline-light" data-cmd="${esc(c.name)}">${esc(c.label)}</button>`).join('') +
    '</div></div>').join('');
  updateEngine();
}

// A "stopped only" command is disabled while the engine runs (the dash refuses it too).
function updateEngine() {
  const el = $('engine');
  el.textContent = rpm === null ? t('bench_engine_nodata') : running() ? t('bench_engine_running', { n: Math.round(rpm) }) : t('bench_engine_stopped');
  el.className = 'small ' + (running() ? 'text-warning' : 'text-muted');
  document.querySelectorAll('[data-cmd]').forEach((b) => {
    const c = cmds.find((x) => x.name === b.dataset.cmd);
    b.disabled = !!(c && c.stopped_only && running());
    b.title = b.disabled ? t('bench_refused_running') : '';
  });
}

$('groups').addEventListener('click', async (e) => {
  const b = e.target.closest('[data-cmd]');
  if (!b || b.disabled) return;
  const c = cmds.find((x) => x.name === b.dataset.cmd);
  b.disabled = true;
  const r = await api('POST', '/api/bench/send', { name: c.name });
  b.disabled = false;
  updateEngine();
  if (r.ok && r.data.sent) toast(t('bench_cmd_sent', { n: c.label }));
  else if (r.ok) toast(t('bench_not_sent'), false);
  else if (r.status === 409) toast(t('bench_refused_running'), false);
  else if (r.status !== 401) toast(t('failed') + ': ' + errText(r), false);
});

// ---------------------------------------------------------------------------
// Commands: the table
// ---------------------------------------------------------------------------
const hex = (n) => '0x' + n.toString(16).toUpperCase();
const parseNum = (s) => { const x = String(s).trim().toLowerCase(); const n = x.startsWith('0x') ? parseInt(x.slice(2), 16) : Number(x); return Number.isInteger(n) && n >= 0 ? n : NaN; };

function renderTable() {
  $('cmdRows').innerHTML = cmds.map((c, i) =>
    `<tr data-i="${i}">` +
    `<td><input class="form-control form-control-sm" data-f="group" maxlength="15" value="${esc(c.group)}"></td>` +
    `<td><input class="form-control form-control-sm" data-f="label" maxlength="23" value="${esc(c.label)}"></td>` +
    `<td><input class="form-control form-control-sm" data-f="name" maxlength="15" value="${esc(c.name)}"></td>` +
    `<td><input class="form-control form-control-sm" data-f="id" value="${hex(c.id)}" style="width:7.5em"></td>` +
    `<td><input class="form-check-input" type="checkbox" data-f="ext" ${c.ext ? 'checked' : ''}></td>` +
    `<td><input class="form-control form-control-sm" data-f="sub" value="${hex(c.sub)}" style="width:5em"></td>` +
    `<td><input class="form-control form-control-sm" data-f="idx" value="${c.idx}" style="width:5em"></td>` +
    `<td><select class="form-select form-select-sm" data-f="dlc"><option ${c.dlc === 6 ? 'selected' : ''}>6</option><option ${c.dlc === 8 ? 'selected' : ''}>8</option></select></td>` +
    `<td><input class="form-check-input" type="checkbox" data-f="stopped_only" ${c.stopped_only ? 'checked' : ''}></td>` +
    `<td><button class="btn btn-sm btn-outline-secondary" data-del><i class="fas fa-trash"></i></button></td></tr>`).join('');
  stackLabels($('cmdTable'));
}

// The table on the page -> the command list (null and a message when something is wrong).
function readTable() {
  const out = [];
  for (const tr of $('cmdRows').children) {
    const v = (f) => tr.querySelector(`[data-f="${f}"]`);
    const c = { name: v('name').value.trim(), label: v('label').value.trim(), group: v('group').value.trim(),
      id: parseNum(v('id').value), ext: v('ext').checked, sub: parseNum(v('sub').value), idx: parseNum(v('idx').value),
      dlc: Number(v('dlc').value), stopped_only: v('stopped_only').checked };
    if (!/^[A-Za-z0-9_]{1,15}$/.test(c.name) || [c.id, c.sub, c.idx].some(Number.isNaN) || c.sub > 65535 || c.idx > 65535 ||
        c.id > (c.ext ? 0x1FFFFFFF : 0x7FF) || /["\\]/.test(c.label + c.group) || out.some((o) => o.name === c.name)) {
      toast(t('bench_err_row', { n: out.length + 1 }), false);
      return null;
    }
    out.push(c);
  }
  return out;
}

$('cmdAdd').onclick = () => {
  const keep = readTable();
  if (keep === null) return;
  let n = 1; while (keep.some((c) => c.name === 'cmd' + n)) n++;
  cmds = [...keep, { name: 'cmd' + n, label: 'Command ' + n, group: 'Custom', id: 0x77000C, ext: true, sub: 0x16, idx: 0, dlc: 6, stopped_only: true }];
  renderTable();
};
$('cmdRows').addEventListener('click', (e) => {
  const b = e.target.closest('[data-del]');
  if (!b) return;
  const keep = readTable();
  if (keep === null) return;
  keep.splice(Number(b.closest('tr').dataset.i), 1);
  cmds = keep;
  renderTable();
});
$('cmdSave').onclick = async () => {
  const list = readTable();
  if (list === null) return;
  const r = await api('POST', '/api/bench/cmds', { version: 1, cmds: list });
  if (!r.ok) { if (r.status !== 401) toast(t('bench_err_table') + ': ' + errText(r), false); return; }
  toast(t('bench_saved'));
  await loadCmds();
};
$('cmdReload').onclick = () => loadCmds();
$('cmdReset').onclick = async () => {
  if (!confirm(t('bench_confirm_preset'))) return;
  const r = await api('POST', '/api/bench/cmds/reset');
  if (r.ok) { toast(t('bench_saved')); loadCmds(); } else if (r.status !== 401) toast(t('failed') + ': ' + errText(r), false);
};

async function loadCmds() {
  const r = await api('GET', '/api/bench/cmds');
  if (!r.ok) { toast(t('bench_err_table') + ': ' + errText(r), false); return; }
  const d = typeof r.data === 'string' ? JSON.parse(r.data) : r.data;
  cmds = d.cmds || [];
  renderCmds();
  renderTable();
}

function onLangChange() { names = ''; renderRows(); update(); renderCmds(); }

navbar('/bench.html');
loadCmds();
poll();
setInterval(poll, 500);
