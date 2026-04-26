// libpromeki debug frontend.  Lives at <apiBase>/promeki/, talks JSON
// to sibling endpoints (/build, /env, /options, /memspace, /log) and
// a WebSocket at /log/stream.
//
//   Tabs:
//     Log     — live WebSocket-driven log stream (newest at top), client-side
//               filters, modal for debug-channel toggles.
//     Memory  — grid of MemSpace cards, refreshes once per second while the
//               tab is visible.
//     Library — Build / Options / Environment, fetched once on first view.
'use strict';

(function() {

// API endpoints are siblings of this UI's directory: the page is
// served at <apiBase>/promeki/ so apiBase is location.pathname with
// the trailing slash trimmed.  Fetches are then made with paths like
// "/build", "/log", "/memspace" relative to apiBase.
const apiBase = location.pathname.replace(/\/$/, '');
const wsScheme = location.protocol === 'https:' ? 'wss:' : 'ws:';

// ============================================================
// DOM helpers
// ============================================================
function $(id) { return document.getElementById(id); }

function clear(node) { while (node.firstChild) node.removeChild(node.firstChild); }

// Mini element builder.  Tag, optional attrs object, then child nodes /
// strings.  `class` and `text` are special-cased; everything else goes
// through setAttribute so data-*, type, colspan etc. all work.
function el(tag, attrs) {
        const e = document.createElement(tag);
        if (attrs) {
                for (const k of Object.keys(attrs)) {
                        const v = attrs[k];
                        if (v === undefined || v === null) continue;
                        if (k === 'class') e.className = v;
                        else if (k === 'text') e.textContent = v;
                        else if (k === 'colspan') e.setAttribute('colspan', v);
                        else e.setAttribute(k, v);
                }
        }
        for (let i = 2; i < arguments.length; i++) {
                const c = arguments[i];
                if (c === undefined || c === null || c === false) continue;
                e.appendChild(typeof c === 'string' || typeof c === 'number'
                        ? document.createTextNode(String(c)) : c);
        }
        return e;
}

function escapeHtml(s) {
        return String(s)
                .replace(/&/g, '&amp;')
                .replace(/</g, '&lt;')
                .replace(/>/g, '&gt;');
}

function asString(v) {
        if (v === null || v === undefined) return '';
        if (typeof v === 'object') return JSON.stringify(v);
        if (typeof v === 'boolean') return v ? 'true' : 'false';
        return String(v);
}

function fmtNum(n) {
        if (n === null || n === undefined || isNaN(n)) return '';
        return Number(n).toLocaleString();
}

function fmtBytes(n) {
        if (n === null || n === undefined || isNaN(n)) return '';
        const v = Number(n);
        if (v < 1024) return v + ' B';
        const units = ['KiB', 'MiB', 'GiB', 'TiB', 'PiB'];
        let i = -1, x = v;
        do { x /= 1024; i++; } while (x >= 1024 && i < units.length - 1);
        return (x >= 100 ? x.toFixed(0) : x >= 10 ? x.toFixed(1) : x.toFixed(2))
                + ' ' + units[i];
}

// Tooltip text — exact byte count with thousands separators, useful
// when the human-readable form has lost precision.
function bytesTitle(n) {
        if (n === null || n === undefined || isNaN(n)) return '';
        return Number(n).toLocaleString() + ' B';
}

// Format a per-second byte rate.  Mirrors fmtBytes for the unit but
// preserves sign so a negative rate (counter went backwards) is
// visible rather than silently rounded to 0.
function fmtBytesRate(bps) {
        if (bps === null || bps === undefined || isNaN(bps)) return '';
        const v = Math.abs(bps);
        if (v < 0.5) return '0 B/s';
        const sign = bps < 0 ? '-' : '';
        if (v < 1024) return sign + v.toFixed(0) + ' B/s';
        const units = ['KiB/s', 'MiB/s', 'GiB/s', 'TiB/s'];
        let i = -1, x = v;
        do { x /= 1024; i++; } while (x >= 1024 && i < units.length - 1);
        return sign + (x >= 100 ? x.toFixed(0) : x >= 10 ? x.toFixed(1) : x.toFixed(2))
                + ' ' + units[i];
}

function fmtCountRate(cps) {
        if (cps === null || cps === undefined || isNaN(cps)) return '';
        const v = Math.abs(cps);
        if (v < 0.005) return '0/s';
        if (v < 10)    return cps.toFixed(2) + '/s';
        if (v < 100)   return cps.toFixed(1) + '/s';
        return Math.round(cps).toLocaleString() + '/s';
}

async function getJson(path) {
        const r = await fetch(apiBase + path);
        if (!r.ok) throw new Error(path + ': ' + r.status + ' ' + r.statusText);
        return await r.json();
}

function showError(target, e) {
        clear(target);
        target.appendChild(el('div', { class: 'error', text: 'ERROR: ' + e.message }));
        console.error(target.id, e);
}

// ============================================================
// Tab switching
// ============================================================
const tabHooks = {
        log:     { activate: noop,                 deactivate: noop },
        memory:  { activate: startMemoryRefresh,   deactivate: stopMemoryRefresh },
        library: { activate: ensureLibraryLoaded,  deactivate: noop },
};

function noop() {}

let activeTab = 'log';
function activateTab(name) {
        if (!tabHooks[name]) return;
        if (activeTab && tabHooks[activeTab]) tabHooks[activeTab].deactivate();
        activeTab = name;
        document.querySelectorAll('header nav button[data-tab]').forEach(b =>
                b.classList.toggle('active', b.dataset.tab === name));
        document.querySelectorAll('.tab').forEach(t =>
                t.classList.toggle('active', t.id === 'tab-' + name));
        tabHooks[name].activate();
}

document.querySelectorAll('header nav button[data-tab]').forEach(btn => {
        btn.addEventListener('click', () => activateTab(btn.dataset.tab));
});

// ============================================================
// Library tab — Build / Options / Environment
// ============================================================
const buildFieldOrder = [
        'name', 'version', 'type', 'betaVersion', 'rcVersion', 'repoIdent',
        'date', 'time', 'hostname',
        'platform', 'features', 'runtime', 'debug',
];

let libraryLoaded = false;
function ensureLibraryLoaded() {
        if (libraryLoaded) return;
        libraryLoaded = true;
        refreshBuild();
        refreshOptions();
        refreshEnv();
}

async function refreshBuild() {
        const tableEl = $('build-table');
        const linesEl = $('build-lines');
        try {
                const data = await getJson('/build');
                const tbl = el('table', { class: 'kv' });
                const thead = el('thead');
                thead.appendChild(el('tr', null,
                        el('th', { text: 'Field' }),
                        el('th', { text: 'Value' })));
                tbl.appendChild(thead);
                const tb = el('tbody');
                const seen = new Set();
                for (const k of buildFieldOrder) {
                        if (k in data) {
                                tb.appendChild(buildKvRow(k, data[k]));
                                seen.add(k);
                        }
                }
                for (const k of Object.keys(data)) {
                        if (k === 'lines' || seen.has(k)) continue;
                        tb.appendChild(buildKvRow(k, data[k]));
                }
                tbl.appendChild(tb);
                clear(tableEl);
                tableEl.appendChild(tbl);

                const lines = Array.isArray(data.lines) ? data.lines : [];
                linesEl.textContent = lines.length ? lines.join('\n') : '(no banner)';
        } catch (e) {
                showError(tableEl, e);
                linesEl.textContent = '';
        }
}
function buildKvRow(k, v) {
        return el('tr', null,
                el('td', { class: 'k', text: k }),
                el('td', { class: 'v', text: asString(v) }));
}

let optionsValues = {};
let optionsSchema = {};
async function refreshOptions() {
        const target = $('options-table');
        try {
                const [vRes, sRes] = await Promise.allSettled([
                        getJson('/options'),
                        getJson('/options/_schema'),
                ]);
                optionsValues = vRes.status === 'fulfilled' ? (vRes.value || {}) : {};
                optionsSchema = sRes.status === 'fulfilled' ? (sRes.value || {}) : {};
                renderOptions();
        } catch (e) {
                showError(target, e);
        }
}
function renderOptions() {
        const target = $('options-table');
        const filter = ($('options-filter').value || '').toLowerCase();
        const keys = new Set();
        for (const k of Object.keys(optionsValues)) keys.add(k);
        for (const k of Object.keys(optionsSchema)) keys.add(k);
        const sorted = Array.from(keys).sort();
        if (sorted.length === 0) {
                clear(target);
                target.appendChild(el('div', { class: 'empty', text: '(no options registered)' }));
                return;
        }
        const tbl = el('table', { class: 'kv' });
        const thead = el('thead');
        thead.appendChild(el('tr', null,
                el('th', { text: 'Key' }),
                el('th', { text: 'Value' }),
                el('th', { text: 'Type' }),
                el('th', { text: 'Default' }),
                el('th', { text: 'Description' })));
        tbl.appendChild(thead);
        const tb = el('tbody');
        let shown = 0;
        for (const k of sorted) {
                const sp = optionsSchema[k] || {};
                const valueText = (k in optionsValues) ? asString(optionsValues[k]) : '';
                const typeText = Array.isArray(sp.types) ? sp.types.join(' | ') : '';
                const defaultText = sp.default !== undefined ? asString(sp.default) : '';
                const descText = sp.description || '';
                if (filter) {
                        const hay = (k + ' ' + valueText + ' ' + descText).toLowerCase();
                        if (!hay.includes(filter)) continue;
                }
                shown++;
                tb.appendChild(el('tr', null,
                        el('td', { class: 'k', text: k }),
                        el('td', { class: 'v', text: valueText }),
                        el('td', { class: 'v', text: typeText }),
                        el('td', { class: 'v', text: defaultText }),
                        el('td', { class: 'desc', text: descText })));
        }
        tbl.appendChild(tb);
        clear(target);
        if (shown === 0) {
                target.appendChild(el('div', { class: 'empty',
                        text: '(no options match filter)' }));
        } else {
                target.appendChild(tbl);
        }
}
$('options-filter').addEventListener('input', renderOptions);

let envData = {};
async function refreshEnv() {
        const target = $('env-table');
        try {
                envData = await getJson('/env') || {};
                renderEnv();
        } catch (e) {
                showError(target, e);
        }
}
function renderEnv() {
        const target = $('env-table');
        const filter = ($('env-filter').value || '').toLowerCase();
        const keys = Object.keys(envData).sort();
        if (keys.length === 0) {
                clear(target);
                target.appendChild(el('div', { class: 'empty', text: '(no env vars)' }));
                return;
        }
        const tbl = el('table', { class: 'kv' });
        const thead = el('thead');
        thead.appendChild(el('tr', null,
                el('th', { text: 'Variable' }),
                el('th', { text: 'Value' })));
        tbl.appendChild(thead);
        const tb = el('tbody');
        let shown = 0;
        for (const k of keys) {
                const v = asString(envData[k]);
                if (filter) {
                        if (!k.toLowerCase().includes(filter) &&
                            !v.toLowerCase().includes(filter)) continue;
                }
                shown++;
                tb.appendChild(el('tr', null,
                        el('td', { class: 'k', text: k }),
                        el('td', { class: 'v', text: v })));
        }
        tbl.appendChild(tb);
        clear(target);
        if (shown === 0) {
                target.appendChild(el('div', { class: 'empty',
                        text: '(no env vars match filter)' }));
        } else {
                target.appendChild(tbl);
        }
}
$('env-filter').addEventListener('input', renderEnv);

// ============================================================
// Memory tab — bordered card per subsystem, 1Hz refresh
// ============================================================
const memoryGroups = [
        { title: 'Live (current)', rows: [
                ['liveCount', 'count'],
                ['liveBytes', 'bytes'],
        ]},
        { title: 'Peak', rows: [
                ['peakCount', 'count'],
                ['peakBytes', 'bytes'],
        ]},
        { title: 'Allocations', rows: [
                ['allocCount',     'count'],
                ['allocBytes',     'bytes'],
                ['allocFailCount', 'count'],
                ['maxAllocBytes',  'bytes'],
        ]},
        { title: 'Releases', rows: [
                ['releaseCount', 'count'],
                ['releaseBytes', 'bytes'],
        ]},
        { title: 'Copies', rows: [
                ['copyCount',     'count'],
                ['copyBytes',     'bytes'],
                ['copyFailCount', 'count'],
        ]},
        { title: 'Fills', rows: [
                ['fillCount', 'count'],
                ['fillBytes', 'bytes'],
        ]},
];

// Map<subsystemName, { idEl, cells: { fieldName: { valTd, rateTd, kind } } }>
let memoryCards = new Map();
// Map<subsystemName, { ts: epoch_ms, fields: { fieldName: number } }>
// Used to compute deltas → per-second rates between successive polls.
let memoryPrev = new Map();
let memoryNamesKey = '';
let memoryInterval = null;
let memoryInFlight = false;

async function refreshMemory() {
        if (memoryInFlight) return;
        memoryInFlight = true;
        const target = $('memory-grid');
        try {
                const data = await getJson('/memspace');
                const spaces = Array.isArray(data.spaces) ? data.spaces : [];
                const now = Date.now();
                const namesKey = spaces.map(s => s.name).join('|');
                if (namesKey !== memoryNamesKey) {
                        memoryNamesKey = namesKey;
                        rebuildMemoryGrid(spaces);
                }
                updateMemoryGrid(spaces, now);
                snapshotMemory(spaces, now);
        } catch (e) {
                showError(target, e);
                memoryNamesKey = '';
                memoryCards.clear();
                memoryPrev.clear();
        } finally {
                memoryInFlight = false;
        }
}

function rebuildMemoryGrid(spaces) {
        const target = $('memory-grid');
        clear(target);
        memoryCards.clear();
        if (spaces.length === 0) {
                target.appendChild(el('div', { class: 'empty',
                        text: '(no memory subsystems registered)' }));
                return;
        }
        for (const space of spaces) {
                const idSpan = el('span', { class: 'id' });
                const card = el('section', { class: 'memcard' });
                card.appendChild(el('h3', null,
                        el('span', { text: space.name }),
                        idSpan));
                const cells = {};
                for (const group of memoryGroups) {
                        const tbl = el('table', { class: 'kv' });
                        const thead = el('thead');
                        const headRow = el('tr');
                        const headCell = el('th', { text: group.title });
                        headCell.setAttribute('colspan', '3');
                        headRow.appendChild(headCell);
                        thead.appendChild(headRow);
                        tbl.appendChild(thead);
                        const tb = el('tbody');
                        for (const row of group.rows) {
                                const field = row[0];
                                const kind = row[1];
                                const valueTd = el('td', { class: 'v numeric' });
                                const rateTd  = el('td', { class: 'v numeric rate' });
                                cells[field] = { valTd: valueTd, rateTd: rateTd, kind: kind };
                                tb.appendChild(el('tr', null,
                                        el('td', { class: 'k', text: field }),
                                        valueTd,
                                        rateTd));
                        }
                        tbl.appendChild(tb);
                        card.appendChild(tbl);
                }
                memoryCards.set(space.name, { idEl: idSpan, cells: cells });
                target.appendChild(card);
        }
}

function updateMemoryGrid(spaces, now) {
        for (const space of spaces) {
                const card = memoryCards.get(space.name);
                if (!card) continue;
                card.idEl.textContent = 'id ' + asString(space.id);
                const prev = memoryPrev.get(space.name);
                const dtSec = prev ? (now - prev.ts) / 1000 : 0;
                for (const field of Object.keys(card.cells)) {
                        const slot = card.cells[field];
                        const v = space[field];
                        // Value cell — compact human form, exact count
                        // for bytes lives in the title for hover.
                        if (v === null || v === undefined) {
                                slot.valTd.textContent = '';
                                slot.valTd.removeAttribute('title');
                        } else if (slot.kind === 'bytes') {
                                slot.valTd.textContent = fmtBytes(v);
                                slot.valTd.title = bytesTitle(v);
                        } else {
                                slot.valTd.textContent = fmtNum(v);
                                slot.valTd.removeAttribute('title');
                        }
                        // Rate cell — needs a previous sample and a
                        // non-zero interval; first refresh leaves it
                        // empty, subsequent ticks fill it in.
                        const prevV = prev ? prev.fields[field] : undefined;
                        if (prev && dtSec > 0
                                        && typeof v === 'number'
                                        && typeof prevV === 'number') {
                                const rate = (v - prevV) / dtSec;
                                if (slot.kind === 'bytes') {
                                        slot.rateTd.textContent = fmtBytesRate(rate);
                                        slot.rateTd.title = Math.round(rate).toLocaleString()
                                                + ' B/s';
                                } else {
                                        slot.rateTd.textContent = fmtCountRate(rate);
                                        slot.rateTd.removeAttribute('title');
                                }
                        } else {
                                slot.rateTd.textContent = '';
                                slot.rateTd.removeAttribute('title');
                        }
                }
        }
}

function snapshotMemory(spaces, now) {
        for (const space of spaces) {
                const fields = {};
                for (const group of memoryGroups) {
                        for (const row of group.rows) {
                                const f = row[0];
                                const v = space[f];
                                if (typeof v === 'number') fields[f] = v;
                        }
                }
                memoryPrev.set(space.name, { ts: now, fields: fields });
        }
}

function startMemoryRefresh() {
        if (memoryInterval) return;
        refreshMemory();
        memoryInterval = setInterval(refreshMemory, 1000);
}
function stopMemoryRefresh() {
        if (memoryInterval) {
                clearInterval(memoryInterval);
                memoryInterval = null;
        }
}

// ============================================================
// Log tab — toolbar + WebSocket stream + channels modal
// ============================================================
const logBody = $('log-tbody');
const logStatus = $('log-status');
const filters = {
        level:  $('filter-level'),
        file:   $('filter-file'),
        thread: $('filter-thread'),
        text:   $('filter-text'),
};
let entries = [];
const MAX_ENTRIES = 5000;
let paused = false;

function passes(e) {
        const lvl = parseInt(filters.level.value, 10);
        if (lvl >= 0 && e.level > lvl) return false;
        const f = filters.file.value;
        if (f && !(e.file || '').includes(f)) return false;
        const t = filters.thread.value;
        if (t && !(e.thread || '').includes(t) && !String(e.threadId).includes(t)) return false;
        const x = filters.text.value;
        if (x && !(e.msg || '').includes(x)) return false;
        return true;
}

function rerenderLog() {
        const visible = entries.filter(passes);
        const parts = [];
        // Newest at the top — entries[] is appended in arrival order, so
        // walk it backwards.  innerHTML on a tbody is fastest for the
        // 5000-entry case; everything user-visible is escaped.
        for (let i = visible.length - 1; i >= 0; i--) {
                const e = visible[i];
                const lvl = e.levelChar || '?';
                const ts = (e.ts || '').replace('T', ' ');
                const file = (e.file || '').split('/').pop();
                const fileLine = file
                        + (e.line !== undefined && e.line !== null ? ':' + e.line : '');
                const thread = e.thread || (e.threadId !== undefined ? String(e.threadId) : '');
                parts.push(
                        '<tr class="lvl-' + escapeHtml(lvl) + '">'
                        + '<td class="ts">'  + escapeHtml(ts)        + '</td>'
                        + '<td class="lvl">' + escapeHtml(lvl)       + '</td>'
                        + '<td class="th">'  + escapeHtml(thread)    + '</td>'
                        + '<td class="fl">'  + escapeHtml(fileLine)  + '</td>'
                        + '<td class="msg">' + escapeHtml(e.msg || '') + '</td>'
                        + '</tr>');
        }
        logBody.innerHTML = parts.join('');
        logStatus.textContent = 'showing ' + visible.length + ' of ' + entries.length
                + (paused ? ' (paused)' : '');
}

[filters.level, filters.file, filters.thread, filters.text].forEach(input => {
        input.addEventListener('input', rerenderLog);
        input.addEventListener('change', rerenderLog);
});

$('btn-clear').addEventListener('click', () => {
        entries = [];
        rerenderLog();
});

const pauseBtn = $('btn-pause');
pauseBtn.addEventListener('click', () => {
        paused = !paused;
        pauseBtn.textContent = paused ? 'Resume' : 'Pause';
        pauseBtn.classList.toggle('primary', paused);
        if (!paused) rerenderLog();
});

// ============================================================
// WebSocket stream
// ============================================================
const connStatus = $('conn-status');
let socket = null;
let reconnectAttempt = 0;
function connectStream() {
        const url = wsScheme + '//' + location.host + apiBase + '/log/stream?replay=200';
        socket = new WebSocket(url);
        socket.addEventListener('open', () => {
                reconnectAttempt = 0;
                connStatus.textContent = 'connected';
                connStatus.className = 'status ok';
        });
        socket.addEventListener('message', ev => {
                let parsed;
                try { parsed = JSON.parse(ev.data); }
                catch (err) { console.warn('bad ws message:', err); return; }
                entries.push(parsed);
                if (entries.length > MAX_ENTRIES) {
                        entries.splice(0, entries.length - MAX_ENTRIES);
                }
                if (!paused) rerenderLog();
        });
        socket.addEventListener('close', () => {
                socket = null;
                connStatus.textContent = 'disconnected — retrying…';
                connStatus.className = 'status err';
                const delay = Math.min(5000, 500 * Math.pow(2, reconnectAttempt++));
                setTimeout(connectStream, delay);
        });
        socket.addEventListener('error', () => {
                if (socket) socket.close();
        });
}

// ============================================================
// Debug-channel modal
// ============================================================
const modal = $('channels-modal');
let loggerChannels = [];

$('btn-channels').addEventListener('click', openChannelsModal);
$('channels-close').addEventListener('click', closeChannelsModal);
modal.addEventListener('click', ev => {
        // Click outside the card closes the modal.
        if (ev.target === modal) closeChannelsModal();
});
document.addEventListener('keydown', ev => {
        if (ev.key === 'Escape' && modal.classList.contains('open')) {
                closeChannelsModal();
        }
});

async function openChannelsModal() {
        modal.classList.add('open');
        modal.setAttribute('aria-hidden', 'false');
        await refreshChannels();
}
function closeChannelsModal() {
        modal.classList.remove('open');
        modal.setAttribute('aria-hidden', 'true');
}

async function refreshChannels() {
        const grid = $('channels-grid');
        try {
                const status = await getJson('/log');
                loggerChannels = Array.isArray(status.debugChannels) ? status.debugChannels : [];
                renderChannels();
        } catch (e) {
                showError(grid, e);
        }
}

// Filter input must re-render in place; do NOT re-fetch — re-fetching
// loses the client's pending checkbox state and creates focus jitter.
$('channel-filter').addEventListener('input', renderChannels);

function dedupedChannels() {
        const byName = new Map();
        for (const ch of loggerChannels) {
                if (!ch || typeof ch.name !== 'string' || ch.name === '') continue;
                const cur = byName.get(ch.name);
                if (!cur) byName.set(ch.name, { name: ch.name, enabled: !!ch.enabled });
                else cur.enabled = cur.enabled || !!ch.enabled;
        }
        return Array.from(byName.values()).sort((a, b) => a.name.localeCompare(b.name));
}

function renderChannels() {
        const grid = $('channels-grid');
        clear(grid);
        const channels = dedupedChannels();
        if (channels.length === 0) {
                grid.appendChild(el('div', { class: 'empty', text: '(no channels registered)' }));
                return;
        }
        const filter = ($('channel-filter').value || '').toLowerCase();
        let shown = 0;
        for (const ch of channels) {
                if (filter && !ch.name.toLowerCase().includes(filter)) continue;
                shown++;
                const cb = el('input', { type: 'checkbox' });
                cb.checked = ch.enabled;
                cb.addEventListener('change', () => toggleChannel(ch, cb));
                const label = el('label', { 'data-name': ch.name },
                        cb,
                        el('span', { text: ch.name }));
                grid.appendChild(label);
        }
        if (shown === 0) {
                grid.appendChild(el('div', { class: 'empty', text: '(no channels match filter)' }));
        }
}

async function toggleChannel(ch, cb) {
        const target = cb.checked;
        try {
                const r = await fetch(apiBase + '/log/debug/' + encodeURIComponent(ch.name), {
                        method: 'PUT',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify({ enabled: target }),
                });
                if (!r.ok) throw new Error(r.status + ' ' + r.statusText);
                ch.enabled = target;
        } catch (e) {
                console.warn('toggle failed:', ch.name, e);
                cb.checked = !target;
        }
}

$('channels-enable-all').addEventListener('click', () => bulkToggleVisible(true));
$('channels-disable-all').addEventListener('click', () => bulkToggleVisible(false));

async function bulkToggleVisible(enabled) {
        const grid = $('channels-grid');
        const labels = grid.querySelectorAll('label[data-name]');
        const promises = [];
        for (const label of labels) {
                const name = label.getAttribute('data-name');
                const cb = label.querySelector('input[type="checkbox"]');
                if (!cb || cb.checked === enabled) continue;
                cb.checked = enabled;
                promises.push(
                        fetch(apiBase + '/log/debug/' + encodeURIComponent(name), {
                                method: 'PUT',
                                headers: {'Content-Type': 'application/json'},
                                body: JSON.stringify({ enabled: enabled }),
                        }).then(r => {
                                if (!r.ok) throw new Error(name + ': ' + r.status);
                                const entry = loggerChannels.find(c => c && c.name === name);
                                if (entry) entry.enabled = enabled;
                        }).catch(err => {
                                console.warn('bulk toggle failed:', name, err);
                                cb.checked = !enabled;
                        }));
        }
        await Promise.all(promises);
}

// ============================================================
// Resizable log columns
// ============================================================
// Persist column widths in localStorage so they survive reloads.
// Keyed by the th's `col-*` class.  The last header is left as the
// elastic column (no resizer, no stored width) so it absorbs slack.
const LOG_COL_WIDTHS_KEY = 'libpromeki.debug.logColWidths';

function loadLogColWidths() {
        try {
                const raw = localStorage.getItem(LOG_COL_WIDTHS_KEY);
                if (!raw) return {};
                const parsed = JSON.parse(raw);
                return (parsed && typeof parsed === 'object') ? parsed : {};
        } catch (_) { return {}; }
}

function saveLogColWidths(widths) {
        try { localStorage.setItem(LOG_COL_WIDTHS_KEY, JSON.stringify(widths)); }
        catch (_) {}
}

function colClass(th) {
        for (const c of th.classList) if (c.startsWith('col-')) return c;
        return null;
}

function setupLogColumnResizers() {
        const ths = document.querySelectorAll('#log-table thead th');
        if (!ths.length) return;
        const widths = loadLogColWidths();
        ths.forEach((th, i) => {
                const cls = colClass(th);
                if (cls && widths[cls]) th.style.width = widths[cls] + 'px';
                if (i === ths.length - 1) return; // elastic column
                const handle = el('div', { class: 'col-resizer' });
                th.appendChild(handle);
                handle.addEventListener('mousedown', ev =>
                        startColResize(ev, th, cls, widths, handle));
        });
}

function startColResize(ev, th, cls, widths, handle) {
        ev.preventDefault();
        const startX = ev.clientX;
        const startWidth = th.getBoundingClientRect().width;
        handle.classList.add('dragging');
        document.body.classList.add('col-resizing');
        function onMove(e) {
                const w = Math.max(20, startWidth + (e.clientX - startX));
                th.style.width = w + 'px';
        }
        function onUp() {
                document.removeEventListener('mousemove', onMove);
                document.removeEventListener('mouseup', onUp);
                handle.classList.remove('dragging');
                document.body.classList.remove('col-resizing');
                if (cls) {
                        widths[cls] = Math.round(th.getBoundingClientRect().width);
                        saveLogColWidths(widths);
                }
        }
        document.addEventListener('mousemove', onMove);
        document.addEventListener('mouseup', onUp);
}

// ============================================================
// Boot
// ============================================================
setupLogColumnResizers();
connectStream();
activateTab('log');

})();
