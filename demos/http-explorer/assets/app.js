// HTTP Explorer Demo — root index page.
//
// Drives the three widgets on index.html:
//   • Echo  — POSTs <base>/demo/echo with text + verbose body params.
//   • Config — reads <base>/demo/config (snapshot) and
//     <base>/demo/config/_schema (specs); GUI lets the user PUT each key.
//   • State — reads <base>/demo/state/<name> for each known scalar.
//
// All API URLs are resolved relative to the document's <base href>,
// which the demo's main.cpp sets to whatever apiPrefix it chose.
// That keeps this file fully prefix-agnostic — moving the API to a
// different mount point requires no changes here.
//
// Pure vanilla JS, no deps.  This file ships as a cirf resource at
// :/demo/explorer/app.js and is served at /app.js by the demo binary.

(function () {
        'use strict';

        // Pull the API base out of the <base> element rather than
        // hardcoding it.  baseURI is the absolute URL the browser
        // resolves relative URLs against; trim the trailing slash
        // so we can write paths like "/demo/echo" below.
        const apiBase = document.baseURI.replace(/\/$/, '');
        const apiUrl = (path) => apiBase + path;

        // ------------------------------------------------------------
        // Echo widget
        // ------------------------------------------------------------

        async function echoSend() {
                const text    = document.getElementById('echo-text').value;
                const verbose = document.getElementById('echo-verbose').checked;
                const body = {};
                if(text !== '') body.text = text;
                // Verbose is omitted when unchecked so the server falls
                // back to the DemoConfig.Verbose default — that lets
                // toggling Verbose via the Config widget actually
                // change the empty-form behaviour.
                if(verbose) body.verbose = true;

                const out = document.getElementById('echo-response');
                out.classList.remove('ok', 'err');
                out.textContent = 'sending…';
                try {
                        const res  = await fetch(apiUrl('/demo/echo'), {
                                method:  'POST',
                                headers: { 'Content-Type': 'application/json' },
                                body:    JSON.stringify(body),
                        });
                        const json = await res.json();
                        out.textContent = JSON.stringify(json, null, 2);
                        out.classList.add(res.ok ? 'ok' : 'err');
                        // The echo bumps the live counter, so refresh
                        // the State widget too — feels nicer than
                        // making the user click two buttons.
                        loadState();
                } catch(e) {
                        out.textContent = 'error: ' + e.message;
                        out.classList.add('err');
                }
        }

        // ------------------------------------------------------------
        // Config widget
        // ------------------------------------------------------------

        async function loadConfig() {
                const tbody = document.querySelector('#config-table tbody');
                tbody.innerHTML = '<tr><td colspan="4">loading…</td></tr>';
                try {
                        const [snapshot, schema] = await Promise.all([
                                fetch(apiUrl('/demo/config')).then(r => r.json()),
                                fetch(apiUrl('/demo/config/_schema')).then(r => r.json()),
                        ]);
                        tbody.innerHTML = '';
                        for(const key of Object.keys(schema).sort()) {
                                const spec   = schema[key];
                                const value  = snapshot[key];
                                const desc   = spec.description ?? '';
                                const type   = (spec.types && spec.types[0]) || 'any';
                                const dft    = spec.default;
                                const range  = (spec.min !== undefined ||
                                                spec.max !== undefined)
                                        ? '[' + (spec.min ?? '') + '..' +
                                                (spec.max ?? '') + ']'
                                        : '';

                                const tr = document.createElement('tr');
                                tr.innerHTML =
                                        '<td>' + escapeHtml(key) +
                                                '<div class="desc">' +
                                                escapeHtml(desc) + '</div></td>' +
                                        '<td><input type="text" value="' +
                                                escapeHtml(stringify(value)) +
                                                '" data-key="' +
                                                escapeHtml(key) + '"></td>' +
                                        '<td><div class="desc">' +
                                                escapeHtml(type + ' ' + range) +
                                                '</div>' +
                                                (dft !== undefined
                                                        ? '<div class="desc">def: ' +
                                                                escapeHtml(stringify(dft)) +
                                                                '</div>'
                                                        : '') +
                                        '</td>' +
                                        '<td><button class="save secondary" type="button" ' +
                                                'data-save="' + escapeHtml(key) +
                                                '">Save</button></td>';
                                tbody.appendChild(tr);
                        }
                        for(const btn of tbody.querySelectorAll('button[data-save]')) {
                                btn.addEventListener('click',
                                        () => saveConfig(btn.dataset.save));
                        }
                } catch(e) {
                        tbody.innerHTML =
                                '<tr><td colspan="4">error: ' +
                                escapeHtml(e.message) + '</td></tr>';
                }
        }

        async function saveConfig(key) {
                const inp = document.querySelector(
                        '#config-table input[data-key="' + cssEscape(key) + '"]');
                if(!inp) return;
                let value;
                // Try JSON first so booleans/numbers round-trip; fall
                // back to the raw string for free-form text.
                try { value = JSON.parse(inp.value); }
                catch { value = inp.value; }

                try {
                        const res = await fetch(apiUrl('/demo/config/') +
                                        encodeURIComponent(key), {
                                method:  'PUT',
                                headers: { 'Content-Type': 'application/json' },
                                body:    JSON.stringify({ value: value }),
                        });
                        if(!res.ok) throw new Error('HTTP ' + res.status);
                        loadConfig();
                } catch(e) {
                        alert('save failed: ' + e.message);
                }
        }

        // ------------------------------------------------------------
        // State widget
        // ------------------------------------------------------------

        // The four scalar names are hard-coded here because the demo's
        // VariantLookup registration is fixed.  A more general UI would
        // discover them from a "list keys" endpoint on the lookup —
        // not a thing yet, but a reasonable future addition.
        const STATE_KEYS = ['name', 'counter', 'running', 'lastValue'];

        async function loadState() {
                const tbody = document.querySelector('#state-table tbody');
                tbody.innerHTML = '<tr><td colspan="2">loading…</td></tr>';
                try {
                        const results = await Promise.all(
                                STATE_KEYS.map(k =>
                                        fetch(apiUrl('/demo/state/') +
                                                encodeURIComponent(k))
                                        .then(r => r.json())
                                        .then(j => [k, j.value])));
                        tbody.innerHTML = '';
                        for(const [key, value] of results) {
                                const tr = document.createElement('tr');
                                tr.innerHTML =
                                        '<td>' + escapeHtml(key) + '</td>' +
                                        '<td>' + escapeHtml(stringify(value)) +
                                        '</td>';
                                tbody.appendChild(tr);
                        }
                } catch(e) {
                        tbody.innerHTML =
                                '<tr><td colspan="2">error: ' +
                                escapeHtml(e.message) + '</td></tr>';
                }
        }

        // ------------------------------------------------------------
        // Utilities
        // ------------------------------------------------------------

        function stringify(v) {
                if(v === undefined || v === null) return '';
                if(typeof v === 'object')         return JSON.stringify(v);
                return String(v);
        }
        function escapeHtml(s) {
                if(s == null) return '';
                return String(s)
                        .replace(/&/g, '&amp;').replace(/</g, '&lt;')
                        .replace(/>/g, '&gt;').replace(/"/g, '&quot;')
                        .replace(/'/g, '&#039;');
        }
        function cssEscape(s) {
                return String(s).replace(/(["\\])/g, '\\$1');
        }

        // ------------------------------------------------------------
        // Wire-up
        // ------------------------------------------------------------

        document.getElementById('echo-send')
                .addEventListener('click', echoSend);
        document.getElementById('config-refresh')
                .addEventListener('click', loadConfig);
        document.getElementById('state-refresh')
                .addEventListener('click', loadState);
        loadConfig();
        loadState();
})();
