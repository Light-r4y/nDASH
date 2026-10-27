// Graphical screen editor. The model is the layout JSON itself (common/config/layout.h);
// every edit changes `doc` and redraws. Fields left out of a widget take the firmware's
// defaults, which DEFAULTS repeats (layout.c, parse_widget) so the preview matches.

const SCREEN_W = 800, SCREEN_H = 480;
const SNAP = 5;   // dragging moves in steps of 5 px; hold Shift for 1 px
const AMBER = '#f5b800';
const NAME_RE = /^[A-Za-z0-9_]{1,15}$/;

// Fonts: name -> [pixel height, CSS used for the preview, digits only]. The display uses the
// real fonts (tasks/ui/fonts.c); the browser can only imitate them.
const SYS = 'system-ui,sans-serif';
const FONT_INFO = {
  '14': [14, SYS], '20': [20, SYS], '26': [26, SYS], '36': [36, SYS], '48': [48, SYS],
  digits18: [18, SYS, true], tick10: [10, SYS], label16: [16, SYS], digits26: [26, SYS, true], speed160: [160, SYS, true],
  orbitron_24: [24, 'Orbitron,' + SYS + ';font-weight:700'], orbitron_36: [36, 'Orbitron,' + SYS + ';font-weight:700'],
  orbitron_56: [56, 'Orbitron,' + SYS + ';font-weight:700', true], orbitron_96: [96, 'Orbitron,' + SYS + ';font-weight:700', true],
  michroma_24: [24, 'Michroma,' + SYS], michroma_36: [36, 'Michroma,' + SYS],
  russo_28: [28, '"Russo One",' + SYS + ';font-weight:700'], russo_40: [40, '"Russo One",' + SYS + ';font-weight:700'], russo_64: [64, '"Russo One",' + SYS + ';font-weight:700', true],
  jura_24: [24, 'Jura,' + SYS + ';font-weight:600'], jura_36: [36, 'Jura,' + SYS + ';font-weight:600'],
  blackops_28: [28, '"Black Ops One",' + SYS + ';font-weight:700;font-style:italic'], blackops_40: [40, '"Black Ops One",' + SYS + ';font-weight:700;font-style:italic'],
  blackops_56: [56, '"Black Ops One",' + SYS + ';font-weight:700;font-style:italic', true], blackops_96: [96, '"Black Ops One",' + SYS + ';font-weight:700;font-style:italic', true],
  seg7_40: [40, 'monospace', true], seg7_72: [72, 'monospace', true], seg7_120: [120, 'monospace', true],
};
// The groups that are always there; the RD fonts and the uploaded ones are listed from the media partition (fontGroups()).
const FONT_GROUPS = [
  ['ed_fonts_builtin', ['14', '20', '26', '36', '48']],
  ['ed_fonts_other', ['label16', 'tick10', 'digits18', 'digits26', 'speed160']],
];
const FONT_FAMILY_GROUPS = { orbitron: 'ed_fonts_orbitron', russo: 'ed_fonts_russo', michroma: 'ed_fonts_michroma', jura: 'ed_fonts_jura', blackops: 'ed_fonts_blackops', seg7: 'ed_fonts_seg7' };
const finfo = (n) => FONT_INFO[n] || FONT_INFO[String(n).replace(/\.fnt$/, '')];   // a font can be named with the extension
const fontPx = (n) => (finfo(n) || [14])[0];
function fontCss(n) { const i = finfo(n) || [14, SYS]; return `font:${i[0]}px/${Math.round(i[0] * 1.2)}px ${i[1]}`; }

const DEFAULTS = {
  tile: { w: 240, h: 104, font: '48', color: AMBER, bg_color: '#3a3a3a', decimals: -1, div: 1, round: 0, zone_color: false,
          title_font: '14', unit_font: '14', title_color: '#ffffff', unit_color: AMBER, show_title: true, show_unit: true,
          border_w: 2, border_color: '#5a5a5a', radius: 8, align: 'center', pad: 10, value_dy: 2, value_dx: 0, title_dx: 0, title_dy: 0, unit_dx: 0, unit_dy: 0,
          accent: 'none', accent_w: 26, accent_color: '#9a9a9a', zone_bg: false, zone_accent: false, zone_border: false },
  value: { w: 150, font: '48', color: AMBER, decimals: -1, div: 1, round: 0, zone_color: false },
  text: { font: '20', color: '#ffffff' },
  dial: { w: 460, bg_color: '#1c1c1c', angle: 270, rotation: 90, ticks: 41, major_every: 5, label_div: 1, label_font: '36',
          label_color: AMBER, tick_color: '#ffffff', minor_color: '#808080', bezel_color: '#555555', needle_color: '#c00000',
          show_labels: true, label_gap: 0, major_len: 24, minor_len: 12, major_w: 4, minor_w: 2, bezel_w: 14,
          needle_width: 8, needle_margin: 70, show_hub: true, hub_d: 30, zone_w: 66, zone_offset: 0, zone_opa: 255,
          center_value: false, cv_font: '36', cv_color: '#ffffff', cv_dx: 0, cv_dy: 0, center_text: '', ct_font: '20', ct_color: '#9e9e9e', ct_dx: 0, ct_dy: 0,
          needle_image: '', bg_image: '', pivot_x: 34, pivot_y: 22, needle_scale: 256,
          decimals: -1, div: 1, round: 0, zone_color: false },
  bar: { w: 300, h: 40, color: '#e0ff00', bg_color: '#393839', vertical: false, border_w: 3, border_color: '#ffffff', radius: 0,
         bg_opa: 0, mark_step: 0, mark_div: 1, mark_font: '26', mark_color: '#ffffff', mark_gap: 6, zone_color: false },
  segments: { w: 400, h: 24, color: '#35d07f', count: 14, gap: 4, radius: 3, start: 0, end: 0, off_color: '#2a2a2a', flash_above: 0, zone_color: false },
};

// Pictures: the needles built into the firmware and the .bin files of the media partition (loaded in load()).
const NEEDLES = ['classic', 'neon', 'stripe'];
let mediaFiles = [];
let mediaFonts = [];       // the uploaded fonts (.fnt)
const mediaSizes = {};   // name -> bytes: a big picture (a background) is fetched at half the size for the preview
const imgCache = {};     // name -> { url, w, h } once loaded, null while loading or when missing

// The picture for the preview; null until it has arrived (then the stage is redrawn).
function previewImage(name) {
  if (!name) return null;
  if (name in imgCache) return imgCache[name];
  imgCache[name] = null;
  if (NEEDLES.includes(name)) {
    const im = new Image();
    im.onload = () => { imgCache[name] = { url: im.src, w: im.naturalWidth, h: im.naturalHeight }; renderStage(); };
    im.src = '/needles/' + name + '.png';
  } else {
    fetch('/media/' + encodeURIComponent(name) + (mediaSizes[name] > 100000 ? '?s=2' : ''), { cache: 'no-store' }).then((r) => (r.ok ? r.arrayBuffer() : null)).then((buf) => {
      const c = buf && LvBin.decode(buf);
      if (c) { imgCache[name] = { url: c.toDataURL('image/png'), w: c.width, h: c.height }; renderStage(); }
    }).catch(() => {});
  }
  return null;
}

// The property panel: groups of [key, label key, kind, min, max, step]. The first group is open.
const N = (k, l, a, b, s) => [k, l, 'num', a, b, s];
const SCHEMA = {
  tile: [
    ['ed_g_content', [['signal', 'ed_signal', 'signal'], ['text', 'ed_tile_title', 'text'], ['unit', 'ed_tile_unit', 'text'],
      ['decimals', 'ed_decimals', 'num', -1, 3], ['div', 'ed_div', 'num', null, null, 'any'], ['round', 'ed_round', 'num', 0, null, 'any']]],
    ['ed_g_size', [N('w', 'ed_width', 40, 800), N('h', 'ed_height', 20, 480), N('pad', 'ed_pad', 0, 100), N('radius', 'ed_radius', 0, 100),
      ['bg_color', 'ed_panel_color', 'color'], ['zone_bg', 'ed_zone_bg', 'bool']]],
    ['ed_g_value', [['font', 'ed_value_font', 'font'], ['color', 'ed_value_color', 'color'], ['zone_color', 'ed_zone_color', 'bool'], ['align', 'ed_align', 'select', ['left', 'center', 'right'], 'ed_align_'],
      N('value_dx', 'ed_dx', -300, 300), N('value_dy', 'ed_value_dy', -100, 100)]],
    ['ed_g_labels', [['show_title', 'ed_show_title', 'bool'], ['title_font', 'ed_title_font', 'font'], ['title_color', 'ed_title_color', 'color'],
      ['show_unit', 'ed_show_unit', 'bool'], ['unit_font', 'ed_unit_font', 'font'], ['unit_color', 'ed_unit_color', 'color'],
      N('title_dx', 'ed_title_dx', -300, 300), N('title_dy', 'ed_title_dy', -300, 300), N('unit_dx', 'ed_unit_dx', -300, 300), N('unit_dy', 'ed_unit_dy', -300, 300)]],
    ['ed_g_frame', [N('border_w', 'ed_border_w', 0, 20), ['border_color', 'ed_border_color', 'color'], ['zone_border', 'ed_zone_border', 'bool'],
      ['accent', 'ed_side_bar', 'select', ['none', 'left', 'right', 'top', 'bottom'], 'ed_side_'], N('accent_w', 'ed_accent_w', 1, 200), ['accent_color', 'ed_accent_color', 'color'], ['zone_accent', 'ed_zone_accent', 'bool']]],
  ],
  value: [
    ['ed_g_content', [['signal', 'ed_signal', 'signal'], ['decimals', 'ed_decimals', 'num', -1, 3], ['div', 'ed_div', 'num', null, null, 'any'], ['round', 'ed_round', 'num', 0, null, 'any'], ['zone_color', 'ed_zone_color', 'bool']]],
    ['ed_g_value', [N('w', 'ed_width', 20, 800), ['font', 'ed_font', 'font'], ['color', 'ed_color', 'color']]],
  ],
  text: [['ed_g_content', [['text', 'ed_text', 'text'], ['font', 'ed_font', 'font'], ['color', 'ed_color', 'color']]]],
  dial: [
    ['ed_g_content', [['signal', 'ed_signal', 'signal'], N('w', 'ed_diameter', 100, 480), ['bg_color', 'ed_disc_color', 'color'],
      ['bg_image', 'ed_bg_image', 'image', []], N('bezel_w', 'ed_bezel_w', 0, 60), ['bezel_color', 'ed_bezel_color', 'color']]],
    ['ed_g_scale', [N('angle', 'ed_sweep_deg', 10, 360), N('rotation', 'ed_start_angle', -360, 360), N('ticks', 'ed_ticks', 2, 101), N('major_every', 'ed_major', 1, 50),
      N('major_len', 'ed_major_len', 0, 100), N('minor_len', 'ed_minor_len', 0, 100), N('major_w', 'ed_major_w', 1, 20), N('minor_w', 'ed_minor_w', 1, 20),
      ['tick_color', 'ed_tick_color', 'color'], ['minor_color', 'ed_minor_color', 'color']]],
    ['ed_g_dlabels', [['show_labels', 'ed_show_labels', 'bool'], N('label_div', 'ed_label_div', null, null, 'any'), ['label_font', 'ed_label_font', 'font'],
      ['label_color', 'ed_label_color', 'color'], N('label_gap', 'ed_label_gap', -100, 100)]],
    ['ed_g_zones', [N('zone_w', 'ed_zone_w', 1, 150), N('zone_offset', 'ed_zone_offset', -100, 100), N('zone_opa', 'ed_zone_opa', 0, 255)]],
    ['ed_g_needle', [['needle_image', 'ed_needle_image', 'image', NEEDLES], N('pivot_x', 'ed_pivot_x', -500, 500), N('pivot_y', 'ed_pivot_y', -500, 500),
      N('needle_scale', 'ed_needle_scale', 32, 1024), ['needle_color', 'ed_needle_color', 'color'], N('needle_width', 'ed_needle_width', 1, 30), N('needle_margin', 'ed_needle_margin', 0, 200),
      ['show_hub', 'ed_show_hub', 'bool'], N('hub_d', 'ed_hub_d', 2, 100)]],
    ['ed_g_center', [['center_value', 'ed_center_value', 'bool'], ['cv_font', 'ed_cv_font', 'font'], ['cv_color', 'ed_cv_color', 'color'], N('cv_dx', 'ed_cv_dx', -300, 300), N('cv_dy', 'ed_cv_dy', -300, 300),
      N('decimals', 'ed_decimals', -1, 3), N('div', 'ed_div', null, null, 'any'), N('round', 'ed_round', 0, null, 'any'), ['zone_color', 'ed_zone_color', 'bool'],
      ['center_text', 'ed_center_text', 'text'], ['ct_font', 'ed_ct_font', 'font'], ['ct_color', 'ed_ct_color', 'color'], N('ct_dx', 'ed_ct_dx', -300, 300), N('ct_dy', 'ed_ct_dy', -300, 300)]],
  ],
  bar: [
    ['ed_g_content', [['signal', 'ed_signal', 'signal'], ['vertical', 'ed_vertical', 'bool'], ['zone_color', 'ed_zone_color_fill', 'bool']]],
    ['ed_g_size', [N('w', 'ed_width', 10, 800), N('h', 'ed_height', 10, 480), N('radius', 'ed_radius', 0, 100)]],
    ['ed_g_frame', [['color', 'ed_fill_color', 'color'], ['bg_color', 'ed_empty_color', 'color'], N('bg_opa', 'ed_empty_opa', 0, 255),
      N('border_w', 'ed_border_w', 0, 20), ['border_color', 'ed_border_color', 'color']]],
    ['ed_g_marks', [N('mark_step', 'ed_mark_step', 0, null, 'any'), N('mark_div', 'ed_mark_div', null, null, 'any'), ['mark_font', 'ed_font', 'font'],
      ['mark_color', 'ed_color', 'color'], N('mark_gap', 'ed_mark_gap', -100, 100)]],
  ],
  segments: [
    ['ed_g_content', [['signal', 'ed_signal', 'signal'], N('count', 'ed_count', 1, 32), ['zone_color', 'ed_seg_zone', 'bool']]],
    ['ed_g_size', [N('w', 'ed_width', 10, 800), N('h', 'ed_height', 4, 480), N('gap', 'ed_gap', 0, 50), N('radius', 'ed_radius', 0, 50)]],
    ['ed_g_lamps', [['color', 'ed_lit_color', 'color'], ['off_color', 'ed_off_color', 'color'], N('start', 'ed_seg_start', null, null, 'any'),
      N('end', 'ed_seg_end', null, null, 'any'), N('flash_above', 'ed_flash_above', null, null, 'any')]],
  ],
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
let doc = null;          // the layout being edited
let loaded = '';         // JSON as it is on the dash
let cur = 0;             // current screen
let sel = -1;            // selected widget
let tab = 'widget';
let sample = 0.6;        // 0..1, preview value of every signal
let history = [];
const openGroups = {};   // property groups the user opened

const $ = (id) => document.getElementById(id);
const stage = $('stage');
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

const screen = () => doc.screens[cur];
const widgets = () => screen().widgets;
const val = (w, k) => (w[k] !== undefined ? w[k] : (DEFAULTS[w.type] || {})[k]);
const signalDef = (name) => (doc.signals || []).find((s) => s.name === name);
const typeName = (type) => t('ed_t_' + type);
const countUse = (name) => doc.screens.reduce((n, sc) => n + sc.widgets.filter((w) => w.signal === name).length, 0);

function serialise() { return JSON.stringify(doc); }
const isDirty = () => serialise() !== loaded;

// The limits of the firmware (lnd_layout_check, common/config/layout.c): beyond them the frame rate
// is no longer comfortable. Numbers and texts have no background of their own, so over a dial the
// disc, the scale and the picture under them would be redrawn with every change.
const MAX_LIVE = 14, MAX_DIALS = 2;
const nameDigits = (name, dflt) => { const m = /(\d+)$/.exec(name || ''); return m ? Number(m[1]) : dflt; };

// The first problem of a layout: { key, screen, widget } or null.
function layoutProblem(d) {
  for (let si = 0; si < d.screens.length; si++) {
    const ws = d.screens[si].widgets || [];
    let live = 0, dials = 0;
    for (let i = 0; i < ws.length; i++) {
      live += ws[i].type !== 'text';
      dials += ws[i].type === 'dial';
      if (live > MAX_LIVE) return { key: 'ed_err_too_many', screen: si, widget: i };
      if (dials > MAX_DIALS) return { key: 'ed_err_two_dials', screen: si, widget: i };
    }
    for (let i = 0; i < ws.length; i++) {
      const w = ws[i];
      if (w.type !== 'value' && w.type !== 'text') continue;
      const px = nameDigits(w.font, w.type === 'value' ? 48 : 20);
      const width = w.type === 'value' ? (w.w !== undefined ? w.w : DEFAULTS.value.w) : (w.text || '').length * px * 0.6;
      const height = px * 1.25, x = w.x || 0, y = w.y || 0;
      for (const dl of ws) {
        if (dl.type !== 'dial') continue;
        const r = (dl.w !== undefined ? dl.w : DEFAULTS.dial.w) / 2, cx = (dl.x || 0) + r, cy = (dl.y || 0) + r;
        const nx = Math.min(Math.max(cx, x), x + width), ny = Math.min(Math.max(cy, y), y + height);
        if ((nx - cx) ** 2 + (ny - cy) ** 2 < r * r) return { key: 'ed_err_label_dial', screen: si, widget: i };
      }
    }
  }
  return null;
}

// Called after every edit. An edit that breaks a limit is taken back.
function changed(redraw = true) {
  const now = serialise();
  const last = history[history.length - 1];
  if (last !== undefined && last !== now) {
    const p = layoutProblem(doc);
    if (p && !layoutProblem(JSON.parse(last))) {
      doc = JSON.parse(last);
      sel = Math.min(sel, widgets().length - 1);
      toast(t(p.key, { max: MAX_LIVE }), false);
      renderAll();
      return;
    }
  }
  if (history.length === 0 || history[history.length - 1] !== now) {
    history.push(now);
    if (history.length > 60) history.shift();
  }
  updateState();
  if (redraw) { renderStage(); renderPanel(); }
}

function undo() {
  if (history.length < 2) return;
  history.pop();
  doc = JSON.parse(history[history.length - 1]);
  cur = Math.min(cur, doc.screens.length - 1);
  sel = Math.min(sel, widgets().length - 1);
  updateState(); renderAll();
}

function updateState() {
  const live = widgets().filter((w) => w.type !== 'text').length;
  $('live').textContent = t('ed_live', { n: live, max: MAX_LIVE });
  $('live').className = 'small ms-2 ' + (live >= MAX_LIVE ? 'text-warning' : 'text-muted');
  const dirty = isDirty();
  $('state').textContent = t(dirty ? 'ed_unsaved' : 'ed_saved');
  $('state').className = 'small ms-2 ' + (dirty ? 'text-warning' : 'text-muted');
}

// ---------------------------------------------------------------------------
// Preview
// ---------------------------------------------------------------------------
function sampleOf(sd) { return sd ? sd.min + (sd.max - sd.min) * sample : 0; }
function zoneOf(sd, v) {
  for (const z of (sd && sd.zones) || []) if (v >= z.from && v <= z.to) return z;
  return null;
}
function zoneColour(sd, v) { const z = zoneOf(sd, v); return z ? z.color : null; }
// The pressure units of the dash (Home page): the signals marked "pressure" are shown in them, as on the dash (units.c).
let units = { baro: 'kpa', boost: 'bar' };
function pconv(sd, kpa) {
  if (!sd || !sd.pressure) return null;
  if (sd.pressure === 'abs') return units.baro === 'inhg' ? { v: kpa * 0.2952999, unit: 'inHg', dec: 2 } : { v: kpa, unit: 'kPa', dec: 0 };
  const g = kpa - 101.325;
  if (units.boost === 'psi') return g >= 0 ? { v: g * 0.1450377, unit: 'psi', dec: 1 } : { v: g * 0.2952999, unit: 'inHg', dec: 1 };
  return { v: g / 100, unit: 'bar', dec: 2 };
}
function fmt(sd, w) {
  const p = pconv(sd, sampleOf(sd));
  if (p) return p.v.toFixed(p.dec);
  const dec = val(w, 'decimals') >= 0 ? val(w, 'decimals') : (sd ? (sd.decimals || 0) : 0);
  let x = sampleOf(sd) / (val(w, 'div') || 1);
  const step = val(w, 'round');
  if (step > 0) x = Math.round(x / step) * step;
  return x.toFixed(dec);
}
const frac = (sd, v) => (sd ? Math.max(0, Math.min(1, (v - sd.min) / (sd.max - sd.min))) : 0);

function el(tag, css, text) {
  const e = document.createElement(tag);
  if (css) e.style.cssText = css;
  if (text !== undefined) e.textContent = text;
  return e;
}

function buildTile(w) {
  const sd = signalDef(w.signal);
  const g = (k) => val(w, k);
  const W = g('w'), H = g('h'), bw = g('border_w');
  const zo = zoneOf(sd, sampleOf(sd)), zc = zo ? zo.color : null;   // the zone the sample value is in, and its colour
  const fills = !!(g('zone_bg') && zo && zo.fill !== false);          // only a zone with "fill" paints the panel
  const box = el('div', `width:${W}px;height:${H}px;border-radius:${g('radius')}px;background:${fills ? zc : g('bg_color')};` +
    `border:${bw}px solid ${g('zone_border') && zc ? zc : g('border_color')};box-sizing:border-box;position:absolute;overflow:hidden`);
  let left = g('pad'), right = g('pad'), top = 0, bottom = 0;
  const side = g('accent'), aw = g('accent_w');
  if (side !== 'none') {
    const pos = { left: `top:0;left:0;width:${aw}px;height:100%`, right: `top:0;right:0;width:${aw}px;height:100%`,
                  top: `top:0;left:0;height:${aw}px;width:100%`, bottom: `bottom:0;left:0;height:${aw}px;width:100%` }[side];
    box.appendChild(el('div', `position:absolute;${pos};background:${g('zone_accent') && zc ? zc : g('accent_color')}`));
    if (side === 'left') left += aw; else if (side === 'right') right += aw; else if (side === 'top') top += aw; else bottom += aw;
  }
  const colour = (g('zone_color') && !fills && zc) || g('color');   // on a panel the zone fills the number keeps its colour
  box.appendChild(el('div', `position:absolute;left:${left + g('value_dx')}px;width:${W - 2 * bw - left - right}px;top:${top + g('value_dy')}px;text-align:${g('align')};` +
    `${fontCss(g('font'))};color:${colour};white-space:nowrap;overflow:hidden`, sd ? fmt(sd, w) : '--'));
  if (g('show_title')) box.appendChild(el('div', `position:absolute;left:${left + g('title_dx')}px;bottom:${bottom + 4 - g('title_dy')}px;${fontCss(g('title_font'))};color:${g('title_color')};white-space:nowrap`,
    w.text || (sd ? sd.title : '')));
  if (g('show_unit')) box.appendChild(el('div', `position:absolute;right:${right - g('unit_dx')}px;bottom:${bottom + 4 - g('unit_dy')}px;${fontCss(g('unit_font'))};color:${g('unit_color')};white-space:nowrap`,
    pconv(sd, sampleOf(sd)) ? pconv(sd, sampleOf(sd)).unit : (w.unit || (sd ? sd.unit : ''))));
  return box;
}

function buildValue(w) {
  const sd = signalDef(w.signal);
  const colour = (val(w, 'zone_color') && zoneColour(sd, sampleOf(sd))) || val(w, 'color');
  return el('div', `width:${val(w, 'w')}px;text-align:center;${fontCss(val(w, 'font'))};color:${colour};white-space:nowrap;overflow:hidden`, sd ? fmt(sd, w) : '--');
}

function buildText(w) {
  return el('div', `${fontCss(val(w, 'font'))};color:${val(w, 'color')};white-space:nowrap`, w.text || '');
}

const NS = 'http://www.w3.org/2000/svg';
function svg(tag, attrs) {
  const e = document.createElementNS(NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  return e;
}

function buildDial(w) {
  const g = (k) => val(w, k);
  const sd = signalDef(w.signal);
  const D = g('w'), c = D / 2, bz = g('bezel_w'), inner = D - 2 * bz, r = inner / 2;
  const sweep = g('angle'), rot = g('rotation');
  const pt = (rad, ang) => [c + rad * Math.cos(ang * Math.PI / 180), c + rad * Math.sin(ang * Math.PI / 180)];
  const root = svg('svg', { width: D, height: D, viewBox: `0 0 ${D} ${D}`, style: 'position:absolute;overflow:visible' });
  root.appendChild(svg('circle', { cx: c, cy: c, r: Math.max(1, c - bz / 2), fill: g('bg_color'), stroke: g('bezel_color'), 'stroke-width': bz }));
  const bgImg = previewImage(g('bg_image'));
  if (bgImg) {                                        // the picture fills the disc, under the bezel
    const cp = svg('clipPath', { id: 'bgclip' });
    cp.appendChild(svg('circle', { cx: c, cy: c, r: c }));
    root.appendChild(cp);
    root.appendChild(svg('image', { href: bgImg.url, x: 0, y: 0, width: D, height: D, preserveAspectRatio: 'none', 'clip-path': 'url(#bgclip)' }));
    root.appendChild(svg('circle', { cx: c, cy: c, r: Math.max(1, c - bz / 2), fill: 'none', stroke: g('bezel_color'), 'stroke-width': bz }));
  }

  for (const z of (sd && sd.zones) || []) {          // coloured bands behind the labels
    const a0 = rot + sweep * frac(sd, z.from), a1 = rot + sweep * frac(sd, z.to);
    const rr = r + g('zone_offset') - g('zone_w') / 2;
    if (rr <= 0) continue;
    const [x0, y0] = pt(rr, a0), [x1, y1] = pt(rr, a1);
    root.appendChild(svg('path', { d: `M ${x0} ${y0} A ${rr} ${rr} 0 ${a1 - a0 > 180 ? 1 : 0} 1 ${x1} ${y1}`,
      fill: 'none', stroke: z.color, 'stroke-width': g('zone_w'), 'stroke-opacity': g('zone_opa') / 255 }));
  }
  const ticks = g('ticks'), every = g('major_every'), div = g('label_div') || 1;
  const lfs = fontPx(g('label_font'));
  for (let i = 0; i < ticks; i++) {
    const ang = rot + sweep * i / (ticks - 1), major = i % every === 0;
    const len = major ? g('major_len') : g('minor_len');
    const [x0, y0] = pt(r, ang), [x1, y1] = pt(r - len, ang);
    root.appendChild(svg('line', { x1: x0, y1: y0, x2: x1, y2: y1, stroke: major ? g('tick_color') : g('minor_color'),
      'stroke-width': major ? g('major_w') : g('minor_w') }));
    if (major && sd && g('show_labels')) {
      const raw = sd.min + (sd.max - sd.min) * i / (ticks - 1), pc = pconv(sd, raw);
      const v = pc ? pc.v : raw / div;
      const [tx, ty] = pt(r - g('major_len') - 15 - g('label_gap') - lfs * 0.3, ang);
      const tt = svg('text', { x: tx, y: ty, 'text-anchor': 'middle', 'dominant-baseline': 'central', fill: g('label_color'),
        style: fontCss(g('label_font')).replace(/\/\d+px/, '') });
      tt.textContent = Math.abs(v - Math.round(v)) < 0.05 ? Math.round(v) : v.toFixed(1);
      root.appendChild(tt);
    }
  }
  const holder = el('div', `width:${D}px;height:${D}px;position:relative`);
  holder.appendChild(root);
  if (g('center_value')) {
    const colour = (g('zone_color') && zoneColour(sd, sampleOf(sd))) || g('cv_color');
    const h = fontPx(g('cv_font'));
    holder.appendChild(el('div', `position:absolute;left:${c - inner / 4 + g('cv_dx')}px;width:${inner / 2}px;top:${c + g('cv_dy') - h * 0.6}px;text-align:center;${fontCss(g('cv_font'))};color:${colour};white-space:nowrap`,
      sd ? fmt(sd, w) : '--'));
  }
  if (g('center_text')) {
    const h = fontPx(g('ct_font'));
    holder.appendChild(el('div', `position:absolute;left:${c - inner / 4 + g('ct_dx')}px;width:${inner / 2}px;top:${c + g('ct_dy') - h * 0.6}px;text-align:center;${fontCss(g('ct_font'))};color:${g('ct_color')};white-space:nowrap`,
      g('center_text')));
  }
  const nang = rot + sweep * frac(sd, sampleOf(sd));
  const [nx, ny] = pt(D / 2 - g('needle_margin'), nang);
  const top = svg('svg', { width: D, height: D, viewBox: `0 0 ${D} ${D}`, style: 'position:absolute;left:0;top:0;overflow:visible' });
  const nImg = previewImage(g('needle_image'));
  if (nImg) {                                         // a picture turned about its pivot
    const k = g('needle_scale') / 256, px = g('pivot_x'), py = g('pivot_y');
    top.appendChild(svg('image', { href: nImg.url, x: c - px * k, y: c - py * k, width: nImg.w * k, height: nImg.h * k,
      transform: `rotate(${nang} ${c} ${c})` }));
  } else {
    top.appendChild(svg('line', { x1: c, y1: c, x2: nx, y2: ny, stroke: g('needle_color'), 'stroke-width': g('needle_width'), 'stroke-linecap': 'round' }));
  }
  if (g('show_hub')) top.appendChild(svg('circle', { cx: c, cy: c, r: g('hub_d') / 2, fill: g('needle_color'), stroke: '#300000', 'stroke-width': 3 }));
  holder.appendChild(top);
  return holder;
}

function buildBar(w) {
  const g = (k) => val(w, k);
  const sd = signalDef(w.signal);
  const W = g('w'), H = g('h'), vert = g('vertical');
  const wrap = el('div', `position:relative;width:${W}px;height:${H}px`);
  const bg = g('bg_color'), op = g('bg_opa') / 255;
  const track = el('div', `position:absolute;inset:0;box-sizing:border-box;border:${g('border_w')}px solid ${g('border_color')};border-radius:${g('radius')}px;overflow:hidden`);
  track.appendChild(el('div', `position:absolute;inset:0;background:${bg};opacity:${op}`));
  const f = frac(sd, sampleOf(sd));
  const colour = (g('zone_color') && zoneColour(sd, sampleOf(sd))) || g('color');
  const bw = g('border_w');
  track.appendChild(el('div', vert ? `position:absolute;left:0;right:0;bottom:0;height:${f * (H - 2 * bw)}px;background:${colour}`
                                   : `position:absolute;top:0;bottom:0;left:0;width:${f * (W - 2 * bw)}px;background:${colour}`));
  wrap.appendChild(track);
  const step = g('mark_step');
  if (sd && step > 0 && sd.max > sd.min) {
    let n = 0;
    for (let m = (Math.floor(sd.min / step) + 1) * step; m < sd.max && n < 24; m += step, n++) {
      if (m <= sd.min) continue;
      const fr = (m - sd.min) / (sd.max - sd.min), pc = pconv(sd, m), v = pc ? pc.v : m / (g('mark_div') || 1);
      const txt = Math.abs(v - Math.round(v)) < 0.05 ? Math.round(v) : v.toFixed(1);
      const css = vert ? `left:${W + g('mark_gap')}px;top:${(1 - fr) * H}px;transform:translateY(-50%)`
                       : `left:${fr * W}px;top:${H + g('mark_gap')}px;transform:translateX(-50%)`;
      wrap.appendChild(el('div', `position:absolute;${css};${fontCss(g('mark_font'))};color:${g('mark_color')};white-space:nowrap`, String(txt)));
    }
  }
  return wrap;
}

function buildSegments(w) {
  const g = (k) => val(w, k);
  const sd = signalDef(w.signal);
  const n = g('count'), W = g('w'), H = g('h'), gap = g('gap');
  const start = g('end') > g('start') ? g('start') : (sd ? sd.min : 0), end = g('end') > g('start') ? g('end') : (sd ? sd.max : 1);
  const sw = Math.max(1, Math.floor((W - (n - 1) * gap) / n));
  const wrap = el('div', `position:relative;width:${W}px;height:${H}px`);
  const v = sampleOf(sd);
  for (let i = 0; i < n; i++) {
    const lit = sd && v > start + (end - start) * i / n;
    const z = g('zone_color') ? zoneColour(sd, start + (end - start) * (i + 1) / n) : null;
    wrap.appendChild(el('div', `position:absolute;left:${i * (sw + gap)}px;top:0;width:${sw}px;height:${H}px;border-radius:${g('radius')}px;` +
      `background:${lit ? (z || g('color')) : g('off_color')}`));
  }
  return wrap;
}

const BUILD = { tile: buildTile, value: buildValue, text: buildText, dial: buildDial, bar: buildBar, segments: buildSegments };

function widgetSize(w) {
  const g = (k) => val(w, k);
  if (w.type === 'tile' || w.type === 'bar' || w.type === 'segments') return [g('w'), g('h')];
  if (w.type === 'dial') return [g('w'), g('w')];
  const h = Math.round(fontPx(g('font')) * 1.2);
  if (w.type === 'value') return [g('w'), h];
  return [Math.max(30, (w.text || '').length * fontPx(g('font')) * 0.6), h];
}

function fitStage() {
  const wrap = $('stageWrap');
  const k = Math.min(1, wrap.clientWidth / SCREEN_W);
  stage.style.transform = `scale(${k})`;
  wrap.style.height = Math.round(SCREEN_H * k) + 'px';
  stage.dataset.k = k;
}

function renderStage() {
  stage.innerHTML = '';
  stage.style.background = screen().bg_color || '#000000';
  widgets().forEach((w, i) => {
    const wrap = el('div', `position:absolute;left:${w.x || 0}px;top:${w.y || 0}px;cursor:move`);
    wrap.dataset.i = i;
    wrap.appendChild((BUILD[w.type] || buildText)(w));
    if (i === sel) {
      const [W, H] = widgetSize(w);
      wrap.appendChild(el('div', `position:absolute;left:0;top:0;width:${W}px;height:${H}px;outline:2px dashed #35d0ff;pointer-events:none`));
      const hs = Math.max(14, 30 / (Number(stage.dataset.k) || 1));   // 30 screen px even when the stage is scaled down
      const h = el('div', `position:absolute;left:${W - hs / 2}px;top:${H - hs / 2}px;width:${hs}px;height:${hs}px;background:#35d0ff;cursor:nwse-resize;border-radius:${hs / 4}px`);
      h.dataset.handle = '1';
      wrap.appendChild(h);
    }
    stage.appendChild(wrap);
  });
  fitStage();
}

// The sweep: the sample value runs up and down by itself, so zones and the needle can be judged.
let sweepTimer = null, sweepDir = 1;
$('sweep').addEventListener('change', (e) => {
  clearInterval(sweepTimer);
  if (e.target.checked) {
    sweepTimer = setInterval(() => {
      if (drag) return;
      sample += sweepDir * 0.01;
      if (sample >= 1) { sample = 1; sweepDir = -1; } else if (sample <= 0) { sample = 0; sweepDir = 1; }
      $('sample').value = Math.round(sample * 100);
      renderStage();
    }, 50);
  }
});

// ---------------------------------------------------------------------------
// Dragging
// ---------------------------------------------------------------------------
let drag = null;
const snap = (v, free) => (free ? v : Math.round(v / SNAP) * SNAP);

stage.addEventListener('pointerdown', (e) => {
  const wrap = e.target.closest('[data-i]');
  if (!wrap) { sel = -1; renderStage(); renderPanel(); return; }
  const i = Number(wrap.dataset.i);
  const resize = e.target.dataset && e.target.dataset.handle === '1';
  if (sel !== i) { sel = i; renderStage(); renderPanel(); }
  const w = widgets()[i];
  drag = { i, resize, sx: e.clientX, sy: e.clientY, x: w.x || 0, y: w.y || 0, w: widgetSize(w)[0], h: widgetSize(w)[1] };
  stage.setPointerCapture(e.pointerId);
  e.preventDefault();
});
stage.addEventListener('pointermove', (e) => {
  if (!drag) return;
  const k = Number(stage.dataset.k) || 1;
  const dx = Math.round((e.clientX - drag.sx) / k), dy = Math.round((e.clientY - drag.sy) / k);
  const w = widgets()[drag.i];
  if (drag.resize) {
    if (w.type === 'tile' || w.type === 'bar' || w.type === 'segments') { w.w = Math.max(10, snap(drag.w + dx, e.shiftKey)); w.h = Math.max(4, snap(drag.h + dy, e.shiftKey)); }
    else if (w.type === 'dial') w.w = Math.max(100, snap(drag.w + Math.max(dx, dy), e.shiftKey));
    else if (w.type === 'value') w.w = Math.max(20, snap(drag.w + dx, e.shiftKey));
  } else { w.x = snap(drag.x + dx, e.shiftKey); w.y = snap(drag.y + dy, e.shiftKey); }
  renderStage();
});
stage.addEventListener('pointerup', () => { if (drag) { drag = null; changed(); } });

document.addEventListener('keydown', (e) => {
  if (e.target.matches('input, textarea, select')) return;
  if ((e.ctrlKey || e.metaKey) && e.key === 'z') { undo(); e.preventDefault(); return; }
  if (sel < 0) return;
  const step = e.shiftKey ? 10 : 1, w = widgets()[sel];
  const moves = { ArrowLeft: ['x', -step], ArrowRight: ['x', step], ArrowUp: ['y', -step], ArrowDown: ['y', step] };
  if (moves[e.key]) { w[moves[e.key][0]] = (w[moves[e.key][0]] || 0) + moves[e.key][1]; changed(); e.preventDefault(); }
  else if (e.key === 'Delete') { removeWidget(sel); e.preventDefault(); }
});

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------
function addWidget(type) {
  const firstSignal = (doc.signals && doc.signals[0]) ? doc.signals[0].name : '';
  const w = { type, x: 20, y: 20 };
  if (type !== 'text') w.signal = firstSignal; else w.text = typeName('text');
  widgets().push(w);
  sel = widgets().length - 1;
  tab = 'widget';
  changed();
}
function removeWidget(i) {
  widgets().splice(i, 1);
  sel = Math.min(sel, widgets().length - 1);
  changed();
}
function moveWidget(i, d) {
  const a = widgets(), j = i + d;
  if (j < 0 || j >= a.length) return;
  [a[i], a[j]] = [a[j], a[i]];
  sel = j;
  changed();
}

// ---------------------------------------------------------------------------
// Panels
// ---------------------------------------------------------------------------
function field(label, inner) {
  return `<div class="mb-2"><label class="form-label small mb-1">${label}</label>${inner}</div>`;
}

// The name of a font as the list shows it: "Montserrat 20", "Orbitron 36", "Segment7 120 (digits only)".
const FONT_FAMILY_NAMES = { orbitron: 'Orbitron', russo: 'Russo One', michroma: 'Michroma', jura: 'Jura', blackops: 'Black Ops One', seg7: 'Segment7' };
function fontLabel(f) {
  const base = f.replace(/\.fnt$/, '');
  let name = base;
  if (/^\d+$/.test(base)) name = 'Montserrat ' + base;
  else {
    const m = /^([a-z0-9]+)_(\d+)$/.exec(base);
    if (m && FONT_FAMILY_NAMES[m[1]]) name = FONT_FAMILY_NAMES[m[1]] + ' ' + m[2];
    else if (finfo(base) && !/^(label16|tick10|digits\d+|speed160)$/.test(base)) name = base + ' ' + finfo(base)[0];   // an uploaded font: with its size
  }
  return name + ((finfo(base) || [])[2] ? ' (' + t('ed_digits_only') + ')' : '');
}

// The groups of the font list: Montserrat, the families found in the media partition, the other uploaded fonts, the service fonts.
function fontGroups() {
  const groups = {};
  for (const file of mediaFonts) {
    const base = file.replace(/\.fnt$/, '');
    const key = FONT_FAMILY_GROUPS[base.replace(/_\d+$/, '')] || 'ed_fonts_uploaded';
    (groups[key] = groups[key] || []).push(base);
  }
  const bySize = (a, b) => (Number((/_(\d+)$/.exec(a) || [0, 0])[1]) - Number((/_(\d+)$/.exec(b) || [0, 0])[1])) || a.localeCompare(b);
  const order = [...Object.values(FONT_FAMILY_GROUPS), 'ed_fonts_uploaded'].filter((k) => groups[k]);
  return [FONT_GROUPS[0], ...order.map((k) => [k, groups[k].sort(bySize)]), FONT_GROUPS[1]];
}

function fontSelect(id, v) {
  const all = fontGroups();
  const known = all.some(([, list]) => list.includes(v) || list.includes(String(v).replace(/\.fnt$/, '')));
  const cur = String(v).replace(/\.fnt$/, '');
  const groups = all.map(([gk, list]) =>
    `<optgroup label="${t(gk)}">` + list.map((f) => `<option value="${f}" ${f === v || f === cur ? 'selected' : ''}>${fontLabel(f)}</option>`).join('') + '</optgroup>').join('');
  return `<select class="form-select form-select-sm" data-k="${id}">${known ? '' : `<option selected>${esc(v)}</option>`}${groups}</select>`;
}

function inputFor(spec, w, bind) {
  const [key, labelKey, kind, a, b, step] = spec;
  const label = t(labelKey);
  const v = val(w, key);
  const id = `${bind}:${key}`;
  if (kind === 'signal') {
    const opts = (doc.signals || []).map((s) => `<option ${s.name === w.signal ? 'selected' : ''}>${esc(s.name)}</option>`).join('');
    const missing = w.signal && !signalDef(w.signal) ? `<option selected>${esc(w.signal)}</option>` : '';
    return field(label, `<select class="form-select form-select-sm" data-k="${id}">${missing}${opts}</select>` +
      (missing ? `<div class="form-text text-warning">${t('ed_sig_missing')}</div>` : ''));
  }
  if (kind === 'text') return field(label, `<input class="form-control form-control-sm" data-k="${id}" value="${esc(w[key] !== undefined ? w[key] : (v || ''))}">`);
  if (kind === 'num') {
    return field(label, `<input type="number" class="form-control form-control-sm" data-k="${id}" value="${v === undefined ? '' : v}"` +
      `${a !== null && a !== undefined ? ` min="${a}"` : ''}${b !== null && b !== undefined ? ` max="${b}"` : ''} step="${step || 1}">`);
  }
  if (kind === 'color') return field(label, `<input type="color" class="form-control form-control-color" data-k="${id}" value="${esc(v || '#ffffff')}">`);
  if (kind === 'font') return field(label, fontSelect(id, v));
  if (kind === 'image') {
    const names = [...a, ...mediaFiles.filter((n) => !a.includes(n))];
    if (v && !names.includes(v)) names.push(v);
    return field(label, `<select class="form-select form-select-sm" data-k="${id}"><option value="">${t('ed_img_none')}</option>` +
      names.map((n) => `<option value="${esc(n)}" ${n === v ? 'selected' : ''}>${esc(n)}${!a.includes(n) && !mediaFiles.includes(n) ? ' (' + t('ed_img_missing') + ')' : ''}</option>`).join('') + '</select>');
  }
  if (kind === 'select') {
    return field(label, `<select class="form-select form-select-sm" data-k="${id}">` +
      a.map((o) => `<option value="${o}" ${o === v ? 'selected' : ''}>${t(spec[4] + o)}</option>`).join('') + '</select>');
  }
  if (kind === 'bool') {
    return `<div class="form-check mb-2"><input class="form-check-input" type="checkbox" data-k="${id}" ${v ? 'checked' : ''}>` +
      `<label class="form-check-label small">${label}</label></div>`;
  }
  return '';
}

function panelWidget() {
  if (sel < 0 || !widgets()[sel]) return `<p class="text-muted small mb-0">${t('ed_select_hint')}</p>`;
  const w = widgets()[sel];
  let h = `<div class="d-flex align-items-center mb-2"><b>${typeName(w.type)}</b>` +
    `<div class="ms-auto btn-group btn-group-sm">` +
    `<button class="btn btn-outline-light" data-act="up" title="${t('ed_layer_down')}"><i class="fas fa-layer-group"></i> -</button>` +
    `<button class="btn btn-outline-light" data-act="down" title="${t('ed_layer_up')}"><i class="fas fa-layer-group"></i> +</button>` +
    `<button class="btn btn-outline-light" data-act="dup" title="${t('ed_duplicate')}"><i class="fas fa-clone"></i></button>` +
    `<button class="btn btn-outline-light" data-act="del" title="${t('ed_remove')}"><i class="fas fa-trash"></i></button></div></div>`;
  h += `<div class="row g-2"><div class="col-6">${inputFor(['x', 'ed_x', 'num', -2000, 2000], w, 'w')}</div>` +
    `<div class="col-6">${inputFor(['y', 'ed_y', 'num', -2000, 2000], w, 'w')}</div></div>`;
  (SCHEMA[w.type] || []).forEach(([gk, fields], gi) => {
    const key = w.type + ':' + gk;
    const open = openGroups[key] !== undefined ? openGroups[key] : gi === 0;
    h += `<details class="prop-group" data-group="${key}" ${open ? 'open' : ''}><summary>${t(gk)}</summary><div class="row g-2">` +
      fields.map((s) => `<div class="${s[2] === 'num' || s[2] === 'color' || s[2] === 'select' || s[2] === 'font' ? 'col-6' : 'col-12'}">${inputFor(s, w, 'w')}</div>`).join('') +
      `</div></details>`;
  });
  return h;
}

function panelList() {
  const items = widgets().map((w, i) => {
    const what = w.type === 'text' ? esc(w.text || '') : esc(w.signal || '');
    return `<a href="#" class="list-group-item list-group-item-action py-1 ${i === sel ? 'active' : ''}" data-pick="${i}">` +
      `<b>${typeName(w.type)}</b> <span class="small opacity-75">${what}</span>` +
      `<span class="float-end small opacity-75">${w.x || 0}, ${w.y || 0}</span></a>`;
  }).join('');
  return `<div class="list-group list-group-flush">${items || `<span class="text-muted small">${t('ed_no_widgets')}</span>`}</div>` +
    `<div class="form-text mt-2">${t('ed_order_hint')}</div>`;
}

function panelScreen() {
  return field(t('ed_bg_color'), `<input type="color" class="form-control form-control-color" data-k="s:bg_color" value="${esc(screen().bg_color || '#000000')}">`) +
    `<div class="d-flex gap-2"><button class="btn btn-sm btn-outline-light" data-act="screenDup"><i class="fas fa-clone me-1"></i>${t('ed_screen_dup')}</button></div>` +
    `<div class="form-text mt-2">${t('ed_screens_hint')}</div>`;
}

function panelSignals() {
  const names = {};
  const rows = (doc.signals || []).map((s, i) => {
    const used = countUse(s.name);
    const zones = (s.zones || []).map((z, k) =>
      `<div class="d-flex gap-1 mb-1 align-items-center"><span class="small text-muted" style="width:3.6rem">${t('ed_zone')} ${k + 1}</span>` +
      `<input type="number" step="any" class="form-control form-control-sm" data-k="z:${i}:${k}:from" value="${z.from}" title="${t('ed_from')}">` +
      `<input type="number" step="any" class="form-control form-control-sm" data-k="z:${i}:${k}:to" value="${z.to}" title="${t('ed_to')}">` +
      `<input type="color" class="form-control form-control-color p-0" data-k="z:${i}:${k}:color" value="${esc(z.color)}" style="width:2.4rem">` +
      `<label class="small text-muted d-flex align-items-center gap-1 mb-0" title="${t('ed_zone_fill_hint')}"><input type="checkbox" class="form-check-input mt-0" data-k="z:${i}:${k}:fill" ${z.fill === false ? '' : 'checked'}>${t('ed_zone_fill')}</label>` +
      `<button class="btn btn-sm btn-outline-light" data-act="zoneDel:${i}:${k}"><i class="fas fa-xmark"></i></button></div>`).join('');
    const nameBad = !NAME_RE.test(s.name) || names[s.name];
    names[s.name] = true;
    return `<div class="border rounded p-2 mb-2" style="border-color:var(--nd-border)!important">` +
      `<div class="d-flex align-items-center mb-1"><b>${esc(s.name)}</b><span class="small text-muted ms-2">${t('ed_used', { n: used })}</span>` +
      `<button class="btn btn-sm btn-outline-light ms-auto" data-act="sigDel:${i}"><i class="fas fa-trash"></i></button></div>` +
      `<div class="row g-1">` +
      `<div class="col-6"><input class="form-control form-control-sm ${nameBad ? 'is-invalid' : ''}" data-k="g:${i}:name" value="${esc(s.name)}" title="${t('ed_sig_name')}" maxlength="15"></div>` +
      `<div class="col-6"><input class="form-control form-control-sm" data-k="g:${i}:title" value="${esc(s.title || '')}" title="${t('ed_title')}" maxlength="23" placeholder="${t('ed_title')}"></div>` +
      `<div class="col-3"><input class="form-control form-control-sm" data-k="g:${i}:unit" value="${esc(s.unit || '')}" title="${t('ed_unit')}" maxlength="11" placeholder="${t('ed_unit')}"></div>` +
      `<div class="col-3"><input type="number" step="any" class="form-control form-control-sm" data-k="g:${i}:min" value="${s.min}" title="${t('ed_min')}"></div>` +
      `<div class="col-3"><input type="number" step="any" class="form-control form-control-sm" data-k="g:${i}:max" value="${s.max}" title="${t('ed_max')}"></div>` +
      `<div class="col-3"><input type="number" min="0" max="3" class="form-control form-control-sm" data-k="g:${i}:decimals" value="${s.decimals || 0}" title="${t('ed_decimals_short')}"></div>` +
      `<div class="col-12"><select class="form-select form-select-sm" data-k="g:${i}:pressure" title="${t('ed_pressure')}">` +
      ['', 'abs', 'boost'].map((k) => `<option value="${k}" ${(s.pressure || '') === k ? 'selected' : ''}>${t('ed_pressure_' + (k || 'none'))}</option>`).join('') + `</select></div>` +
      `</div>${nameBad ? `<div class="small text-danger mt-1">${t('ed_err_sig_name')}</div>` : ''}<div class="mt-2">${zones}` +
      `${(s.zones || []).length < 4 ? `<button class="btn btn-sm btn-outline-light" data-act="zoneAdd:${i}"><i class="fas fa-plus me-1"></i>${t('ed_add_zone')}</button>` : ''}</div></div>`;
  }).join('');
  return rows + `<button class="btn btn-sm btn-outline-light" data-act="sigAdd"><i class="fas fa-plus me-1"></i>${t('ed_add_signal')}</button>` +
    `<div class="form-text mt-2">${t('ed_signals_hint')}</div>`;
}

function panelJson() {
  return `<textarea class="form-control json" id="jsonBox" spellcheck="false" style="min-height:45vh">${esc(JSON.stringify(doc, null, 2))}</textarea>` +
    `<div class="mt-2"><button class="btn btn-sm btn-outline-light" data-act="jsonApply"><i class="fas fa-check me-1"></i>${t('ed_json_load')}</button></div>`;
}

function renderPanel() {
  const body = { widget: panelWidget, list: panelList, signals: panelSignals, screen: panelScreen, json: panelJson }[tab]();
  $('panel').innerHTML = body;
  document.querySelectorAll('#tabs .nav-link').forEach((a) => a.classList.toggle('active', a.dataset.tab === tab));
}

function renderScreenTabs() {
  $('screenTabs').innerHTML = doc.screens.map((_, i) =>
    `<button class="btn btn-sm ${i === cur ? 'btn-accent' : 'btn-outline-light'}" data-screen="${i}">${t('ed_screen')} ${i + 1}</button>`).join('');
}

function renderAll() { renderScreenTabs(); renderStage(); renderPanel(); updateState(); }

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
function setField(key, el) {
  const parts = key.split(':');
  let v = el.type === 'checkbox' ? el.checked : el.value;
  if (el.type === 'number') v = el.value === '' ? undefined : Number(el.value);
  if (parts[0] === 'w') {
    const w = widgets()[sel], k = parts[1];
    const def = (DEFAULTS[w.type] || {})[k];
    if (k === 'x' || k === 'y') w[k] = v === undefined ? 0 : v;
    else if (v === undefined || v === '' || v === def) delete w[k];
    else w[k] = v;
  } else if (parts[0] === 's') {
    screen()[parts[1]] = v;
  } else if (parts[0] === 'g') {
    const s = doc.signals[Number(parts[1])];
    const old = s.name;
    if (parts[2] === 'name') {
      // latin letters, digits and _, up to 15, unique: otherwise the old name stays
      const bad = !NAME_RE.test(v) || doc.signals.some((o, i) => i !== Number(parts[1]) && o.name === v);
      el.classList.toggle('is-invalid', bad);
      if (bad) return false;
      s.name = v;
      if (v !== old) doc.screens.forEach((sc) => sc.widgets.forEach((w) => { if (w.signal === old) w.signal = v; }));   // widgets follow the rename
    } else if (parts[2] === 'min' || parts[2] === 'max' || parts[2] === 'decimals') s[parts[2]] = v === undefined ? 0 : v;
    else if (parts[2] === 'pressure') { if (v) s.pressure = v; else delete s.pressure; }   // empty - no conversion
    else s[parts[2]] = v;
  } else if (parts[0] === 'z') {
    const z = doc.signals[Number(parts[1])].zones[Number(parts[2])];
    if (parts[3] === 'fill') { if (v) delete z.fill; else z.fill = false; }   // filling is the default
    else z[parts[3]] = parts[3] === 'color' ? v : (v === undefined ? 0 : v);
  }
  return true;
}

$('panel').addEventListener('toggle', (e) => {
  if (e.target.dataset && e.target.dataset.group) openGroups[e.target.dataset.group] = e.target.open;
}, true);

$('panel').addEventListener('input', (e) => {
  const k = e.target.dataset && e.target.dataset.k;
  if (!k) return;
  if (setField(k, e.target) === false) return;
  renderStage();
  updateState();
});
$('panel').addEventListener('change', (e) => {
  const k = e.target.dataset && e.target.dataset.k;
  if (!k) return;
  if (setField(k, e.target) === false) { renderPanel(); return; }      // an invalid signal name: back to the old one
  const typing = e.target.matches('input[type=number], input:not([type])');
  changed(!typing);                                                     // keep the focus while typing in a field
  if (typing) renderStage();
});

$('panel').addEventListener('click', (e) => {
  const pick = e.target.closest('[data-pick]');
  if (pick) { sel = Number(pick.dataset.pick); tab = 'widget'; renderStage(); renderPanel(); e.preventDefault(); return; }
  const btn = e.target.closest('[data-act]');
  if (!btn) return;
  const [act, a, b] = btn.dataset.act.split(':');
  if (act === 'up') moveWidget(sel, -1);
  else if (act === 'down') moveWidget(sel, 1);
  else if (act === 'dup') { const c = JSON.parse(JSON.stringify(widgets()[sel])); c.x = (c.x || 0) + 20; c.y = (c.y || 0) + 20; widgets().push(c); sel = widgets().length - 1; changed(); }
  else if (act === 'del') removeWidget(sel);
  else if (act === 'screenDup') {
    if (doc.screens.length >= 8) return toast(t('ed_max_screens'), false);
    doc.screens.splice(cur + 1, 0, JSON.parse(JSON.stringify(screen()))); cur++; sel = -1; changed(); renderScreenTabs();
  } else if (act === 'sigAdd') {
    if ((doc.signals || []).length >= 16) return toast(t('ed_max_signals'), false);
    let n = 1; while (signalDef('signal' + n)) n++;
    (doc.signals = doc.signals || []).push({ name: 'signal' + n, title: 'Signal ' + n, unit: '', min: 0, max: 100, decimals: 0 });
    changed();
  } else if (act === 'sigDel') {
    const s = doc.signals[Number(a)];
    const used = countUse(s.name);
    if (used && !confirm(t('ed_confirm_sig_used', { n: s.name, c: used }))) return;
    doc.screens.forEach((sc) => { sc.widgets = sc.widgets.filter((w) => w.signal !== s.name); });
    doc.signals.splice(Number(a), 1);
    sel = Math.min(sel, widgets().length - 1);
    changed();
  } else if (act === 'zoneAdd') {
    const s = doc.signals[Number(a)];
    (s.zones = s.zones || []).push({ from: s.min, to: s.max, color: '#ff0000' });
    changed();
  } else if (act === 'zoneDel') {
    doc.signals[Number(a)].zones.splice(Number(b), 1);
    changed();
  } else if (act === 'jsonApply') {
    try {
      const d = JSON.parse($('jsonBox').value);
      if (!d || !Array.isArray(d.screens) || !d.screens.length) throw new Error(t('ed_err_json'));
      doc = d; cur = Math.min(cur, doc.screens.length - 1); sel = -1; changed(); renderScreenTabs();
    } catch (err) { toast(t('ed_json_error') + ': ' + err.message, false); }
  }
});

$('tabs').addEventListener('click', (e) => {
  const a = e.target.closest('[data-tab]');
  if (!a) return;
  tab = a.dataset.tab; renderPanel(); e.preventDefault();
});
document.querySelectorAll('[data-add]').forEach((b) => { b.onclick = () => addWidget(b.dataset.add); });
$('screenTabs').addEventListener('click', (e) => {
  const b = e.target.closest('[data-screen]');
  if (b) { cur = Number(b.dataset.screen); sel = -1; renderAll(); }
});
$('screenAdd').onclick = () => {
  if (doc.screens.length >= 8) return toast(t('ed_max_screens'), false);
  doc.screens.push({ bg_color: '#000000', widgets: [] }); cur = doc.screens.length - 1; sel = -1; changed(); renderScreenTabs();
};
$('screenDel').onclick = () => {
  if (doc.screens.length < 2) return toast(t('ed_min_screens'), false);
  if (!confirm(t('ed_confirm_screen_del'))) return;
  doc.screens.splice(cur, 1); cur = Math.min(cur, doc.screens.length - 1); sel = -1; changed(); renderScreenTabs();
};
$('sample').addEventListener('input', (e) => { sample = Number(e.target.value) / 100; renderStage(); });
$('undo').onclick = undo;
window.addEventListener('resize', fitStage);

// ---------------------------------------------------------------------------
// Load / apply / reset
// ---------------------------------------------------------------------------
async function load() {
  const r = await api('GET', '/api/layout');
  if (!r.ok) { toast(t('ed_err_load') + ': ' + errText(r), false); return; }
  doc = typeof r.data === 'string' ? JSON.parse(r.data) : r.data;
  const d = await api('GET', '/api/display');
  if (d.ok) units = { baro: d.data.baro || 'kpa', boost: d.data.boost || 'bar' };
  const m = await api('GET', '/api/media');
  mediaFiles = m.ok && m.data.files ? m.data.files.map((f) => f.name).filter((n) => n.endsWith('.bin')) : [];
  (m.ok && m.data.files ? m.data.files : []).forEach((f) => { mediaSizes[f.name] = f.size; });
  mediaFonts = (m.ok && m.data.files ? m.data.files : []).map((f) => f.name).filter((n) => n.endsWith('.fnt'));
  // the size of an uploaded font (for the preview): font_size in the header of the file
  for (const n of mediaFonts) {
    const base = n.replace(/\.fnt$/, '');
    if (FONT_INFO[base]) continue;
    fetch('/media/' + encodeURIComponent(n) + '?n=32', { cache: 'no-store' }).then((r) => (r.ok ? r.arrayBuffer() : null)).then((buf) => {
      const u8 = buf && new Uint8Array(buf);
      if (u8 && u8.length >= 16 && String.fromCharCode(u8[4], u8[5], u8[6], u8[7]) === 'head') { FONT_INFO[base] = [u8[14] | (u8[15] << 8), SYS]; renderStage(); }
    }).catch(() => {});
  }
  for (const k of Object.keys(imgCache)) if (!NEEDLES.includes(k)) delete imgCache[k];   // pictures may have been replaced
  loaded = JSON.stringify(doc);
  history = [loaded];
  cur = 0; sel = -1;
  renderAll();
}

$('reload').onclick = () => {
  if (isDirty() && !confirm(t('ed_confirm_discard'))) return;
  load();
};
$('apply').onclick = async () => {
  const bad = layoutProblem(doc);
  if (bad) { toast(t('ed_screen') + ' ' + (bad.screen + 1) + ': ' + t(bad.key, { max: MAX_LIVE }), false); return; }
  const problems = [];
  doc.screens.forEach((sc, i) => sc.widgets.forEach((w) => {
    if (w.type !== 'text' && !signalDef(w.signal)) problems.push(t('ed_skip_item', { s: i + 1, t: typeName(w.type), n: w.signal }));
  }));
  if (problems.length && !confirm(t('ed_skip_confirm', { list: problems.slice(0, 6).join('\n') }))) return;
  const text = serialise();
  const r = await api('POST', '/api/layout', text);
  if (r.ok) { loaded = text; updateState(); toast(t('ed_applied')); }
  else toast(t('ed_err_apply') + ': ' + errText(r), false);
};
$('reset').onclick = async () => {
  if (!confirm(t('ed_confirm_reset'))) return;
  const r = await api('POST', '/api/layout/reset');
  if (r.ok) { toast(t('ed_reset_done')); load(); } else toast(t('ed_err_apply') + ': ' + errText(r), false);
};
window.addEventListener('beforeunload', (e) => {
  if (doc && isDirty()) { e.preventDefault(); e.returnValue = ''; }
});

// The page language changed: redraw everything that was generated.
function onLangChange() { if (doc) renderAll(); }

navbar('/layout.html');
load();
