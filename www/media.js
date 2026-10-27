// The Media page: the pictures (dial backgrounds and needles) and the fonts on the dash. PNG/JPG files are
// converted to LVGL .bin right here and uploaded, a font is a ready LVGL .fnt file (lv_font_conv --format bin);
// the lists show what the layout uses.
const THUMB = 88;

let media = { files: [], total: 0, used: 0, max_file: 1000000 };
let usedNames = new Set();
let pending = null;     // { buf, w, h, alpha, kind: 'image' | 'font', px } of the picked file
const fontPx = {};      // name -> { size, px }: the size of an uploaded font, read from its header
const thumbs = {};      // name -> { size, url }

const $ = (id) => document.getElementById(id);

function fmtSize(b) {
  return b >= 1048576 ? (b / 1048576).toFixed(1) + ' MB' : b >= 1024 ? Math.round(b / 1024) + ' KB' : b + ' B';
}

function toThumb(canvas) {
  const k = Math.min(1, THUMB / Math.max(canvas.width, canvas.height));
  const c = document.createElement('canvas');
  c.width = Math.max(1, Math.round(canvas.width * k));
  c.height = Math.max(1, Math.round(canvas.height * k));
  c.getContext('2d').drawImage(canvas, 0, 0, c.width, c.height);
  return c.toDataURL('image/png');
}

// File names the layout refers to.
function collectUsed(layout) {
  const names = new Set();
  const walk = (o) => {
    if (Array.isArray(o)) o.forEach(walk);
    else if (o && typeof o === 'object') {
      for (const [k, v] of Object.entries(o)) {
        if ((k === 'bg_image' || k === 'needle_image') && typeof v === 'string' && v) names.add(v);
        else if (/font$/.test(k) && typeof v === 'string' && v) names.add(v.endsWith('.fnt') ? v : v + '.fnt');   // a font is named with or without the extension
        else walk(v);
      }
    }
  };
  walk(layout.screens || []);
  return names;
}

// The size (pixels) of a LVGL bin font from its header: 'head' at byte 4, font_size at byte 14. null - not a font.
function fontHeader(buf) {
  const u8 = new Uint8Array(buf);
  if (u8.length < 20 || String.fromCharCode(u8[4], u8[5], u8[6], u8[7]) !== 'head') return null;
  return { px: u8[14] | (u8[15] << 8) };
}

function rowFor(f, icon, extra) {
  return `<div class="file-row">${icon}` +
    `<div class="flex-grow-1"><div class="text-break">${f.name}</div><div class="small text-muted">${fmtSize(f.size)}${extra}</div></div>` +
    (usedNames.has(f.name) ? `<span class="badge bg-secondary">${t('media_in_use')}</span>` : '') +
    `<button class="btn btn-sm btn-outline-secondary" data-del="${f.name}"><i class="fas fa-trash"></i></button></div>`;
}

function render() {
  $('usage').textContent = fmtSize(media.used) + ' / ' + fmtSize(media.total);
  $('usageBar').style.width = (media.total ? Math.min(100, media.used * 100 / media.total) : 0) + '%';
  const files = media.files.filter((f) => f.name.endsWith('.bin'));
  $('count').textContent = '(' + files.length + ')';
  $('files').innerHTML = files.length ? files.map((f) => {
    const th = thumbs[f.name];
    return rowFor(f, `<div class="thumb">${th ? `<img src="${th.url}" alt="">` : '<i class="fas fa-image text-muted"></i>'}</div>`, '');
  }).join('') : `<div class="small text-muted">${t('media_empty')}</div>`;

  const fonts = media.files.filter((f) => f.name.endsWith('.fnt'));
  $('fontCount').textContent = '(' + fonts.length + ')';
  $('fonts').innerHTML = fonts.length ? fonts.map((f) => {
    const fi = fontPx[f.name];
    return rowFor(f, '<div class="thumb font"><i class="fas fa-font fa-2x text-muted"></i></div>', fi && fi.size === f.size ? ' - ' + t('media_font_px', { n: fi.px }) : '');
  }).join('') : `<div class="small text-muted">${t('media_fonts_empty')}</div>`;
}

// The size of every uploaded font: only the first bytes of the file are asked for.
async function loadFontInfo() {
  for (const f of media.files) {
    if (!f.name.endsWith('.fnt') || (fontPx[f.name] && fontPx[f.name].size === f.size)) continue;
    try {
      const res = await fetch('/media/' + encodeURIComponent(f.name) + '?n=32', { cache: 'no-store' });
      const h = res.ok ? fontHeader(await res.arrayBuffer()) : null;
      if (h) { fontPx[f.name] = { size: f.size, px: h.px }; render(); }
    } catch (e) { /* no size shown */ }
  }
}

async function loadThumbs() {
  for (const f of media.files) {
    if (!f.name.endsWith('.bin') || (thumbs[f.name] && thumbs[f.name].size === f.size)) continue;
    try {
      const res = await fetch('/media/' + encodeURIComponent(f.name) + (f.size > 100000 ? '?s=4' : ''), { cache: 'no-store' });  // a big picture: a quarter of its side is enough for the thumbnail
      const canvas = res.ok ? LvBin.decode(await res.arrayBuffer()) : null;
      if (canvas) { thumbs[f.name] = { size: f.size, url: toThumb(canvas) }; render(); }
    } catch (e) { /* the icon stays */ }
  }
}

async function loadAll() {
  const [m, l] = await Promise.all([api('GET', '/api/media'), api('GET', '/api/layout')]);
  if (m.ok) media = m.data; else toast(t('media_err_load') + ': ' + errText(m), false);
  if (l.ok) usedNames = collectUsed(typeof l.data === 'string' ? JSON.parse(l.data) : l.data);
  render();
  loadThumbs();
  loadFontInfo();
}

$('file').addEventListener('change', async (e) => {
  const file = e.target.files[0];
  pending = null;
  $('pending').style.display = 'none';
  $('pendingInfo').textContent = '';
  if (!file) return;
  try {
    let conv;
    const base = file.name.replace(/\.[^.]*$/, '').replace(/[^A-Za-z0-9_-]/g, '_').slice(0, 27);
    if (/\.fnt$/i.test(file.name)) {
      const buf = await file.arrayBuffer();
      const h = fontHeader(buf);
      if (!h) throw new Error(t('media_err_font'));
      conv = { buf, kind: 'font', px: h.px };
    } else if (/\.bin$/i.test(file.name)) {
      const buf = await file.arrayBuffer();
      if (!LvBin.decode(buf)) throw new Error(t('media_err_bin'));
      conv = { buf, w: 0, h: 0, alpha: false, kind: 'image' };
    } else {
      conv = Object.assign(await LvBin.fromImageFile(file), { kind: 'image' });
    }
    if (conv.buf.byteLength > media.max_file) throw new Error(t('media_too_large'));
    pending = conv;
    const font = conv.kind === 'font';
    $('pendingName').value = base + (font ? '.fnt' : '.bin');
    $('pendingThumb').style.display = font ? 'none' : '';
    $('pendingIcon').style.display = font ? '' : 'none';
    if (!font) $('pendingThumb').src = toThumb(LvBin.decode(conv.buf));
    $('pendingInfo').textContent = font ? t('media_font_px', { n: conv.px }) + ', ' + fmtSize(conv.buf.byteLength)
      : (conv.w ? conv.w + 'x' + conv.h + ', ' + (conv.alpha ? 'ARGB8888' : 'RGB565') + ', ' : '') + fmtSize(conv.buf.byteLength);
    $('pending').style.display = '';
  } catch (err) {
    toast(t('media_err_image') + ': ' + err.message, false);
  }
});

$('upload').onclick = async () => {
  const name = $('pendingName').value;
  const okName = pending && /^(?!\.)[A-Za-z0-9._-]{1,31}$/.test(name) && name.endsWith(pending.kind === 'font' ? '.fnt' : '.bin');
  if (!okName) { toast(t('media_err_name'), false); return; }
  if (media.files.some((f) => f.name === name) && !confirm(t('media_confirm_replace', { n: name }))) return;
  $('upload').disabled = true;
  const r = await api('POST', '/api/media?name=' + encodeURIComponent(name), pending.buf);
  $('upload').disabled = false;
  if (!r.ok) { if (r.status !== 401) toast(t('media_err_upload') + ': ' + errText(r), false); return; }
  delete thumbs[name];
  delete fontPx[name];
  pending = null;
  $('file').value = '';
  $('pending').style.display = 'none';
  $('pendingInfo').textContent = '';
  toast(t('media_uploaded'));
  loadAll();
};

async function onDelete(e) {
  const b = e.target.closest('[data-del]');
  if (!b) return;
  const name = b.dataset.del;
  if (!confirm(t(usedNames.has(name) ? 'media_confirm_delete_used' : 'media_confirm_delete', { n: name }))) return;
  const r = await api('POST', '/api/media/delete?name=' + encodeURIComponent(name));
  if (!r.ok) { if (r.status !== 401) toast(t('media_err_upload') + ': ' + errText(r), false); return; }
  loadAll();
}
$('files').addEventListener('click', onDelete);
$('fonts').addEventListener('click', onDelete);

function onLangChange() { render(); }

navbar('/media.html');
loadAll();
