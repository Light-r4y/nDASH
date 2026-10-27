// CAN page: bus settings, the CAN table as an editable list (format: common/config/can_map.h),
// a bit map of the selected entry, and the sniffer.

const $ = (id) => document.getElementById(id);
const MAP_MAX = 24;
const BITRATES = [50000, 100000, 125000, 250000, 500000, 1000000];
const STATE_KEYS = ['can_state_active', 'can_state_warning', 'can_state_passive', 'can_state_bus_off'];
const STATE_CLASS = ['text-bg-success', 'text-bg-warning', 'text-bg-warning', 'text-bg-danger'];

let entries = [];     // the table being edited
let loaded = '';      // as on the dash
let selRow = -1;
const openRows = new Set();   // the cards that are opened
let frames = {};      // sniffer: key "id:ext" -> {bytes}
let lastCan = null;   // last /api/can reply

const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

// ---------------------------------------------------------------------------
// Bits, as in can_map.c
// ---------------------------------------------------------------------------
function nextBit(pos, motorola) {
  if (!motorola) return pos + 1;
  return pos % 8 === 0 ? pos + 15 : pos - 1;
}
// The bit positions of a field in order (first = the first bit read), or null if it leaves the 8 bytes.
function fieldBits(start, len, motorola) {
  const out = [];
  let pos = start;
  for (let i = 0; i < len; i++) {
    if (pos < 0 || pos > 63) return null;
    out.push(pos);
    pos = nextBit(pos, motorola);
  }
  return out;
}
function extract(e, bytes) {
  const bits = fieldBits(e.start, e.len, e.order === 'motorola');
  if (!bits) return NaN;
  let raw = 0;
  bits.forEach((pos, i) => {
    const bit = ((bytes[pos >> 3] || 0) >> (pos & 7)) & 1;
    if (e.order === 'motorola') raw = raw * 2 + bit; else raw += bit * Math.pow(2, i);
  });
  if (e.signed && raw >= Math.pow(2, e.len - 1)) raw -= Math.pow(2, e.len);
  return raw * e.factor + e.offset;
}

function parseId(s) {
  s = String(s).trim();
  if (s === '') return NaN;
  const n = /^0x/i.test(s) ? parseInt(s, 16) : (/^[0-9a-f]+$/i.test(s) && /[a-f]/i.test(s) ? parseInt(s, 16) : parseInt(s, 10));
  return Number.isFinite(n) ? n : NaN;
}

// Problems of every row: {rowIndex: [text, ...]}
function validate() {
  const bad = {};
  const names = {};
  entries.forEach((e, i) => {
    const p = [];
    if (!e.signal) p.push(t('can_e_signal'));
    else if (e.signal.length > 15) p.push(t('can_e_name_len'));
    else if (names[e.signal] !== undefined) p.push(t('can_e_dup'));
    if (e.signal && names[e.signal] === undefined) names[e.signal] = i;
    const id = parseId(e.id);
    if (Number.isNaN(id) || id < 0 || id > 0x1FFFFFFF) p.push(t('can_e_id'));
    else if (!e.ext && id > 0x7FF) p.push(t('can_e_ext'));
    if (!(e.start >= 0 && e.start <= 63)) p.push(t('can_e_start'));
    if (!(e.len >= 1 && e.len <= 32)) p.push(t('can_e_len'));
    else if (e.start >= 0 && e.start <= 63 && !fieldBits(e.start, e.len, e.order === 'motorola')) p.push(t('can_e_fit'));
    if (!(e.timeout >= 100 && e.timeout <= 60000)) p.push(t('can_e_timeout'));
    if (p.length) bad[i] = p;
  });
  return bad;
}

function toJson() {
  return JSON.stringify({
    version: 1,
    entries: entries.map((e) => ({
      signal: e.signal, id: '0x' + parseId(e.id).toString(16).toUpperCase(), ext: !!e.ext, start: e.start, len: e.len,
      order: e.order, signed: !!e.signed, factor: e.factor, offset: e.offset, timeout: e.timeout,
    })),
  });
}

function fromJson(text) {
  const d = typeof text === 'string' ? JSON.parse(text) : text;
  return (d.entries || []).map((e) => ({
    signal: e.signal || '', id: typeof e.id === 'number' ? '0x' + e.id.toString(16).toUpperCase() : String(e.id),
    ext: !!e.ext, start: e.start || 0, len: e.len === undefined ? 8 : e.len, order: e.order || 'intel', signed: !!e.signed,
    factor: e.factor === undefined ? 1 : e.factor, offset: e.offset || 0, timeout: e.timeout === undefined ? 2000 : e.timeout,
  }));
}

// Is the table different from what is on the dash? A row that cannot be serialised counts as different.
function isDirty() {
  try { return loaded !== toJson(); } catch (e) { return true; }
}

function updateMapState() {
  const dirty = isDirty();
  $('mapState').textContent = t(dirty ? 'ed_unsaved' : 'ed_saved');
  $('mapState').className = 'small ' + (dirty ? 'text-warning' : 'text-muted');
}

// ---------------------------------------------------------------------------
// Table
// ---------------------------------------------------------------------------
function liveText(e) {
  const f = frames[`${parseId(e.id)}:${e.ext ? 1 : 0}`];
  if (!f) return '<span class="text-muted">-</span>';
  const v = extract(e, f.bytes);
  return Number.isNaN(v) ? '<span class="text-muted">-</span>' : (Math.round(v * 1000) / 1000).toString();
}

function showProblems(bad) {
  $('problems').innerHTML = Object.keys(bad).map((i) => `${t('can_row')} ${Number(i) + 1}: ${esc(bad[i].join('; '))}`).join('<br>');
}

// One entry = a card: the line with the main facts (always visible) and the fields below it
// (opened with the arrow; <details> keeps the page light when there are many signals).
function summaryOf(e) {
  const order = e.order === 'motorola' ? 'Motorola' : 'Intel';
  return `<span class="sig-name">${esc(e.signal) || '-'}</span>` +
    `<span class="sig-id">${esc(e.id)}${e.ext ? ' · 29' : ''}</span>` +
    `<span class="sig-bits">${t('can_bits_sum', { s: esc(e.start), l: esc(e.len) })} · ${order}</span>`;
}

function renderRows() {
  const bad = validate();
  $('mapEmpty').style.display = entries.length ? 'none' : 'block';
  $('rows').innerHTML = entries.map((e, i) => {
    const n = (k, label, extra = '') => `<label class="sig-f"><span>${label}</span><input class="form-control form-control-sm" data-r="${i}:${k}" value="${esc(e[k])}" ${extra}></label>`;
    const chk = (k, label) => `<label class="sig-f sig-chk"><input type="checkbox" class="form-check-input" data-r="${i}:${k}" ${e[k] ? 'checked' : ''}><span>${label}</span></label>`;
    return `<details class="sig ${i === selRow ? 'sel' : ''} ${bad[i] ? 'bad' : ''}" data-row="${i}" ${openRows.has(i) ? 'open' : ''} title="${bad[i] ? esc(bad[i].join('; ')) : ''}">` +
      `<summary><span class="sig-sum">${summaryOf(e)}</span><span class="sig-live live" data-live="${i}">${liveText(e)}</span>` +
      `<button class="btn btn-sm btn-outline-secondary" data-del="${i}" title="${t('can_delete')}"><i class="fas fa-trash"></i></button></summary>` +
      `<div class="sig-body">` +
      `<label class="sig-f"><span>${t('can_signal')}</span><input class="form-control form-control-sm" list="sigNames" data-r="${i}:signal" value="${esc(e.signal)}" maxlength="15"></label>` +
      n('id', t('can_id')) + chk('ext', t('can_29bit')) +
      n('start', t('can_start'), 'type="number" min="0" max="63"') + n('len', t('can_len'), 'type="number" min="1" max="32"') +
      `<label class="sig-f"><span>${t('can_order')}</span><select class="form-select form-select-sm" data-r="${i}:order">` +
      `<option value="intel" ${e.order === 'intel' ? 'selected' : ''}>Intel</option><option value="motorola" ${e.order === 'motorola' ? 'selected' : ''}>Motorola</option></select></label>` +
      chk('signed', t('can_signed')) +
      n('factor', t('can_factor'), 'type="number" step="any"') + n('offset', t('can_offset'), 'type="number" step="any"') +
      n('timeout', t('can_timeout'), 'type="number" min="100" max="60000" step="100"') +
      `</div></details>`;
  }).join('');
  showProblems(bad);
  renderBits();
  updateMapState();
  updateSniffer();
}

function renderBits() {
  const e = entries[selRow];
  if (!e) { $('bitsBox').innerHTML = ''; return; }
  const bits = fieldBits(e.start, e.len, e.order === 'motorola');
  const on = new Set(bits || []);
  const first = bits ? bits[0] : -1, last = bits ? bits[bits.length - 1] : -1;
  let h = '<div class="small text-muted mb-1">' + t('can_bits_of') + ' <b>' + esc(e.signal) + '</b>. ' +
    (bits ? t(e.order === 'motorola' ? 'can_bits_read_msb' : 'can_bits_read_lsb', { a: first, b: last }) : t('can_e_fit')) + '</div><div class="bits">';
  h += '<div class="hdr"></div>' + [7, 6, 5, 4, 3, 2, 1, 0].map((b) => `<div class="hdr">${b}</div>`).join('');
  for (let byte = 0; byte < 8; byte++) {
    h += `<div class="byte">B${byte}</div>`;
    for (let bit = 7; bit >= 0; bit--) {
      const pos = byte * 8 + bit;
      h += `<div class="${on.has(pos) ? 'on' : ''} ${pos === first ? 'msb' : ''}">${pos}</div>`;
    }
  }
  $('bitsBox').innerHTML = h + '</div>';
}

// Updates only the live column (so typing in a field is not interrupted).
function updateLive() {
  entries.forEach((e, i) => { const c = document.querySelector(`[data-live="${i}"]`); if (c) c.innerHTML = liveText(e); });
}

$('rows').addEventListener('input', (ev) => {
  const k = ev.target.dataset && ev.target.dataset.r;
  if (!k) return;
  const [i, key] = k.split(':');
  const e = entries[Number(i)];
  if (ev.target.type === 'checkbox') e[key] = ev.target.checked;
  else if (ev.target.type === 'number') e[key] = ev.target.value === '' ? NaN : Number(ev.target.value);
  else e[key] = ev.target.value;
  selRow = Number(i);
  const bad = validate();
  document.querySelectorAll('#rows [data-row]').forEach((card) => {
    const r = Number(card.dataset.row);
    card.classList.toggle('bad', !!bad[r]);
    card.classList.toggle('sel', r === selRow);
    card.title = bad[r] ? bad[r].join('; ') : '';
  });
  const sum = document.querySelector(`#rows [data-row="${selRow}"] .sig-sum`);
  if (sum) sum.innerHTML = summaryOf(e);
  showProblems(bad);
  renderBits(); updateMapState(); updateLive(); updateSniffer();
});
$('rows').addEventListener('click', (ev) => {
  const del = ev.target.closest('[data-del]');
  if (del) {
    ev.preventDefault();                       // the button is inside <summary>: do not open or close the card
    const k = Number(del.dataset.del);
    entries.splice(k, 1);
    const kept = [...openRows].filter((x) => x !== k).map((x) => (x > k ? x - 1 : x));
    openRows.clear(); kept.forEach((x) => openRows.add(x));
    selRow = -1; renderRows();
  }
});
// A card opened or closed: remember it (a redraw keeps the state) and show its bits.
$('rows').addEventListener('toggle', (ev) => {
  const card = ev.target.closest && ev.target.closest('[data-row]');
  if (!card) return;
  const r = Number(card.dataset.row);
  if (card.open) { openRows.add(r); selRow = r; } else openRows.delete(r);
  document.querySelectorAll('#rows [data-row]').forEach((x) => x.classList.toggle('sel', Number(x.dataset.row) === selRow));
  renderBits();
}, true);
$('mapExpand').onclick = () => { entries.forEach((_, i) => openRows.add(i)); renderRows(); };
$('mapCollapse').onclick = () => { openRows.clear(); renderRows(); };

function addEntry(base) {
  if (entries.length >= MAP_MAX) { toast(t('can_max_rows', { n: MAP_MAX }), false); return; }
  entries.push(Object.assign({ signal: '', id: '0x200', ext: false, start: 0, len: 16, order: 'intel', signed: false, factor: 1, offset: 0, timeout: 2000 }, base));
  selRow = entries.length - 1;
  openRows.add(selRow);                     // a new entry is filled in at once
  renderRows();
}
$('mapAdd').onclick = () => addEntry({});

async function loadMap() {
  const r = await api('GET', '/api/can/map');
  if (!r.ok) { toast(t('can_err_load') + ': ' + errText(r), false); return; }
  entries = fromJson(r.data);
  loaded = toJson();
  selRow = -1;
  openRows.clear();
  renderRows();
}
$('mapReload').onclick = () => {
  if (isDirty() && !confirm(t('ed_confirm_discard'))) return;
  loadMap();
};
$('mapApply').onclick = async () => {
  const bad = validate();
  if (Object.keys(bad).length) {
    Object.keys(bad).forEach((i) => openRows.add(Number(i)));   // show what has to be fixed
    renderRows();
    toast(t('can_fix_rows'), false);
    return;
  }
  const text = toJson();
  const r = await api('POST', '/api/can/map', text);
  if (r.ok) { loaded = text; updateMapState(); toast(t('can_map_applied')); } else toast(t('can_err_apply') + ': ' + errText(r), false);
};
$('mapReset').onclick = async () => {
  if (!confirm(t('can_preset_confirm'))) return;
  const r = await api('POST', '/api/can/map/reset');
  if (r.ok) { toast(t('can_preset_done')); loadMap(); } else toast(t('can_err_apply') + ': ' + errText(r), false);
};
window.addEventListener('beforeunload', (e) => {
  if (entries.length && isDirty()) { e.preventDefault(); e.returnValue = ''; }
});

// ---------------------------------------------------------------------------
// Bus settings and state
// ---------------------------------------------------------------------------
async function loadSettings() {
  const r = await api('GET', '/api/can');
  if (!r.ok) return;
  // A value outside the presets (set earlier by other means) stays selectable.
  const list = BITRATES.includes(r.data.bitrate) ? BITRATES : [...BITRATES, r.data.bitrate].sort((a, b) => a - b);
  $('bitrate').innerHTML = list.map((b) => `<option value="${b}">${b / 1000} kbit/s</option>`).join('');
  $('bitrate').value = r.data.bitrate;
  $('mode').value = r.data.mode;
  $('heartbeat').checked = r.data.heartbeat;
  $('demo').checked = r.data.demo;
}

let prevRx = null, prevRxT = 0;
function renderState() {
  const s = lastCan;
  if (!s) return;
  const now = performance.now();
  const fps = prevRx !== null && now > prevRxT ? Math.max(0, Math.round((s.rx - prevRx) * 1000 / (now - prevRxT))) : 0;
  const key = !s.node_up ? 'can_state_down' : STATE_KEYS[s.state] || 'can_state_down';
  const badge = $('stateBadge');
  badge.textContent = t(key);
  badge.className = 'badge ms-auto ' + (!s.node_up ? 'text-bg-secondary' : STATE_CLASS[s.state] || 'text-bg-secondary');
  const rows = [
    [t('can_fps'), fps], [t('can_rx_total'), s.rx], [t('can_dropped'), s.dropped],
    [t('can_errors'), t('can_err_counters', { tx: s.tx_err, rx: s.rx_err, bus: s.bus_err })], [t('can_busoff'), s.busoff],
  ];
  $('state').innerHTML = rows.map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join('');
  prevRx = s.rx; prevRxT = now;
}
async function loadState() {
  const r = await api('GET', '/api/can');
  if (!r.ok) return;
  lastCan = r.data;
  renderState();
}
$('save').onclick = async () => {
  const r = await api('POST', '/api/can', {
    bitrate: Number($('bitrate').value), mode: $('mode').value, heartbeat: $('heartbeat').checked, demo: $('demo').checked,
  });
  toast(r.ok ? t('can_cfg_applied') : t('can_err_apply') + ': ' + errText(r), r.ok);
};

// ---------------------------------------------------------------------------
// Sniffer
// ---------------------------------------------------------------------------
const hexId = (n, ext) => '0x' + n.toString(16).toUpperCase().padStart(ext ? 8 : 3, '0');
const prevBytes = {}, prevCount = {};
let sniffRows = [];   // last reply, for redrawing on filter / table edits
let sniffT = 0;

function mappedIds() {
  const set = new Set();
  entries.forEach((e) => { const id = parseId(e.id); if (!Number.isNaN(id)) set.add(`${id}:${e.ext ? 1 : 0}`); });
  return set;
}

function updateSniffer() {
  const q = $('filter').value.trim().replace(/^0x/i, '').toLowerCase();
  const mapped = mappedIds();
  const list = sniffRows.filter((f) => !q || f.id.toString(16).toLowerCase().includes(q));
  $('framesEmpty').style.display = sniffRows.length ? 'none' : 'block';
  $('frames').innerHTML = list.map((f) => {
    const cells = f.cells.map((c) => `<span class="${c.changed ? 'changed' : ''}">${c.b}</span>`).join(' ');
    const has = mapped.has(`${f.id}:${f.ext ? 1 : 0}`);
    return `<tr data-fid="${f.id}" data-ext="${f.ext ? 1 : 0}" style="cursor:pointer"><td>${hexId(f.id, f.ext)}</td><td>${f.dlc}</td><td>${cells}</td>` +
      `<td>${f.period === null ? '-' : f.period + ' ' + t('u_ms')}</td><td>${f.count}</td>` +
      `<td>${has ? '<i class="fas fa-check text-success"></i>' : '<i class="fas fa-plus text-muted"></i>'}</td></tr>`;
  }).join('');
}

async function loadFrames() {
  const r = await api('GET', '/api/can/frames');
  if (!r.ok) return;
  const now = performance.now(), dt = now - sniffT;
  frames = {};
  sniffRows = r.data.map((f) => {
    const bytes = f.data.match(/../g) || [];
    const key = `${f.id}:${f.ext ? 1 : 0}`;
    frames[key] = { bytes: bytes.map((b) => parseInt(b, 16)) };
    const old = prevBytes[key] || [];
    const cells = bytes.map((b, i) => ({ b, changed: old.length > 0 && old[i] !== b }));
    // the period: time between polls divided by the frames that came in between
    let period = null;
    if (prevCount[key] !== undefined && f.count > prevCount[key] && sniffT) period = Math.max(1, Math.round(dt / (f.count - prevCount[key])));
    prevBytes[key] = bytes; prevCount[key] = f.count;
    return Object.assign({}, f, { cells, period });
  });
  sniffT = now;
  updateSniffer();
  updateLive();
}
$('filter').addEventListener('input', updateSniffer);
$('frames').addEventListener('click', (ev) => {
  const tr = ev.target.closest('[data-fid]');
  if (!tr) return;
  const ext = tr.dataset.ext === '1', id = Number(tr.dataset.fid);
  if (mappedIds().has(`${id}:${ext ? 1 : 0}`)) return;        // already has a row
  addEntry({ id: hexId(id, ext), ext });
  document.querySelector(`[data-r="${selRow}:signal"]`).focus();
});

// ---------------------------------------------------------------------------
async function loadSignalNames() {
  const r = await api('GET', '/api/layout');
  if (!r.ok) return;
  const d = typeof r.data === 'string' ? JSON.parse(r.data) : r.data;
  $('sigNames').innerHTML = (d.signals || []).map((s) => `<option value="${esc(s.name)}">${esc(s.title || '')}</option>`).join('');
}

function onLangChange() { renderState(); renderRows(); }

navbar('/can.html');
loadSettings(); loadState(); loadMap(); loadFrames(); loadSignalNames();
setInterval(loadState, 2000);
setInterval(loadFrames, 1000);
