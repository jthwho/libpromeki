// Promeki API Explorer — fetches /_openapi from a sibling path and
// renders an interactive form per endpoint.  Vanilla JS, no deps.

(function () {
        'use strict';

        const state = {
                doc:        null,
                endpoints:  [],   // [{ path, method, op }]
                selectedIdx: -1,
        };

        // ------------------------------------------------------------
        // Bootstrapping
        // ------------------------------------------------------------

        function openApiUrl() {
                // The explorer's index.html is served at the api's
                // base prefix.  The OpenAPI doc is a sibling at
                // <prefix>/_openapi — derive its URL from the
                // current page rather than hardcoding a prefix so
                // moving the api to a different mount point requires
                // no changes here.
                let p = window.location.pathname;
                // Strip a trailing "index.html" if the user navigated
                // there explicitly, then ensure a trailing slash so
                // path concatenation produces the right URL.
                p = p.replace(/index\.html$/, '');
                if(!p.endsWith('/')) p += '/';
                return p + '_openapi';
        }

        async function load() {
                const status = document.getElementById('api-status');
                try {
                        const res = await fetch(openApiUrl());
                        if(!res.ok) throw new Error('HTTP ' + res.status);
                        state.doc = await res.json();
                } catch(e) {
                        status.textContent = 'failed to load: ' + e.message;
                        status.classList.add('status-err');
                        return;
                }
                document.getElementById('api-title').textContent =
                        state.doc.info?.title ?? 'API';
                document.getElementById('api-version').textContent =
                        state.doc.info?.version ?? '';
                status.textContent = 'ready';
                buildEndpointList();
        }

        // ------------------------------------------------------------
        // Sidebar
        // ------------------------------------------------------------

        function buildEndpointList() {
                state.endpoints = [];
                const paths = state.doc.paths ?? {};
                for(const path of Object.keys(paths).sort()) {
                        const pathItem = paths[path];
                        for(const method of Object.keys(pathItem)) {
                                if(['get','post','put','delete','patch','head','options']
                                        .indexOf(method) === -1) continue;
                                state.endpoints.push({
                                        path,
                                        method: method.toUpperCase(),
                                        op:     pathItem[method],
                                });
                        }
                }
                renderEndpointList();
                if(state.endpoints.length > 0) selectEndpoint(0);
        }

        function renderEndpointList() {
                const filterText = document.getElementById('filter')
                        .value.trim().toLowerCase();
                const list = document.getElementById('endpoint-list');
                list.innerHTML = '';

                // Group by first tag (or "Untagged").
                const groups = new Map();
                state.endpoints.forEach((ep, idx) => {
                        const hay = (ep.path + ' ' + ep.method + ' ' +
                                (ep.op.summary ?? '')).toLowerCase();
                        if(filterText && !hay.includes(filterText)) return;
                        const tag = (ep.op.tags && ep.op.tags[0]) || 'Untagged';
                        if(!groups.has(tag)) groups.set(tag, []);
                        groups.get(tag).push(idx);
                });

                for(const tag of Array.from(groups.keys()).sort()) {
                        const li = document.createElement('li');
                        li.className = 'tag-divider';
                        li.textContent = tag;
                        list.appendChild(li);
                        for(const idx of groups.get(tag)) {
                                const ep = state.endpoints[idx];
                                const item = document.createElement('li');
                                if(idx === state.selectedIdx) {
                                        item.classList.add('selected');
                                }
                                item.dataset.idx = String(idx);
                                item.innerHTML =
                                        '<span class="method method-' +
                                                methodClass(ep.method) + '">' +
                                                escapeHtml(ep.method) + '</span>' +
                                        '<span class="path">' +
                                                escapeHtml(ep.path) + '</span>';
                                item.addEventListener('click', () => {
                                        selectEndpoint(idx);
                                });
                                list.appendChild(item);
                        }
                }
        }

        function methodClass(m) {
                const known = ['get','post','put','delete','patch'];
                m = m.toLowerCase();
                return known.includes(m) ? m : 'other';
        }

        function selectEndpoint(idx) {
                state.selectedIdx = idx;
                renderEndpointList();
                renderEndpointPanel();
        }

        // ------------------------------------------------------------
        // Detail panel
        // ------------------------------------------------------------

        function renderEndpointPanel() {
                const panel = document.getElementById('endpoint-panel');
                panel.innerHTML = '';
                const ep = state.endpoints[state.selectedIdx];
                if(!ep) return;

                const header = document.createElement('div');
                header.className = 'endpoint-header';
                header.innerHTML =
                        '<span class="method method-' +
                                methodClass(ep.method) + '">' +
                                escapeHtml(ep.method) + '</span>' +
                        '<span class="path">' + escapeHtml(ep.path) + '</span>';
                panel.appendChild(header);

                if(ep.op.summary) {
                        const h = document.createElement('h2');
                        h.style.fontSize = '14px';
                        h.style.margin = '0 0 0.4em';
                        h.textContent = ep.op.summary;
                        panel.appendChild(h);
                }
                if(ep.op.description) {
                        const p = document.createElement('p');
                        p.className = 'endpoint-summary';
                        p.textContent = ep.op.description;
                        panel.appendChild(p);
                }
                if(ep.op.tags && ep.op.tags.length > 0) {
                        const t = document.createElement('div');
                        t.className = 'endpoint-tags';
                        for(const tag of ep.op.tags) {
                                const s = document.createElement('span');
                                s.className = 'tag';
                                s.textContent = tag;
                                t.appendChild(s);
                        }
                        panel.appendChild(t);
                }

                // Parameters table.
                const params = ep.op.parameters || [];
                if(params.length > 0) {
                        const h3 = document.createElement('h3');
                        h3.textContent = 'Parameters';
                        panel.appendChild(h3);

                        const table = document.createElement('table');
                        table.className = 'params';
                        table.innerHTML =
                                '<thead><tr>' +
                                '<th>Name</th><th>In</th><th>Type</th>' +
                                '<th>Value</th></tr></thead>';
                        const tbody = document.createElement('tbody');
                        for(const p of params) {
                                const row = document.createElement('tr');
                                row.innerHTML =
                                        '<td>' + escapeHtml(p.name) +
                                                (p.required
                                                        ? '<span class="required">*</span>'
                                                        : '') +
                                                (p.description
                                                        ? '<div class="param-desc">' +
                                                                escapeHtml(p.description) +
                                                                '</div>' : '') +
                                        '</td>' +
                                        '<td>' + escapeHtml(p.in) + '</td>' +
                                        '<td>' + schemaSummary(p.schema) + '</td>' +
                                        '<td><input type="text" data-param="' +
                                                escapeHtml(p.name) + '" data-in="' +
                                                escapeHtml(p.in) + '" value="' +
                                                escapeHtml(defaultValueAsString(p.schema)) +
                                                '"></td>';
                                tbody.appendChild(row);
                        }
                        table.appendChild(tbody);
                        panel.appendChild(table);
                }

                // requestBody (object schema with properties).
                let bodyProps = [];
                if(ep.op.requestBody) {
                        const h3 = document.createElement('h3');
                        h3.textContent = 'Request body';
                        panel.appendChild(h3);
                        const media = ep.op.requestBody.content?.['application/json'];
                        const sch = media?.schema;
                        if(sch && sch.type === 'object' && sch.properties) {
                                const required = sch.required || [];
                                bodyProps = Object.keys(sch.properties);
                                const table = document.createElement('table');
                                table.className = 'params';
                                table.innerHTML =
                                        '<thead><tr>' +
                                        '<th>Name</th><th>Type</th><th>Value</th>' +
                                        '</tr></thead>';
                                const tbody = document.createElement('tbody');
                                for(const name of bodyProps) {
                                        const sub = sch.properties[name];
                                        const isReq = required.indexOf(name) !== -1;
                                        const row = document.createElement('tr');
                                        row.innerHTML =
                                                '<td>' + escapeHtml(name) +
                                                        (isReq
                                                                ? '<span class="required">*</span>'
                                                                : '') +
                                                        (sub.description
                                                                ? '<div class="param-desc">' +
                                                                        escapeHtml(sub.description) +
                                                                        '</div>' : '') +
                                                '</td>' +
                                                '<td>' + schemaSummary(sub) + '</td>' +
                                                '<td><input type="text" data-body="' +
                                                        escapeHtml(name) + '" value="' +
                                                        escapeHtml(defaultValueAsString(sub)) +
                                                        '"></td>';
                                        tbody.appendChild(row);
                                }
                                table.appendChild(tbody);
                                panel.appendChild(table);
                        } else {
                                // Free-form JSON body — show a textarea.
                                const ta = document.createElement('textarea');
                                ta.id = 'body-raw';
                                ta.placeholder = '{}';
                                panel.appendChild(ta);
                        }
                }

                // Try-it button + response area.
                const btn = document.createElement('button');
                btn.className = 'try';
                btn.textContent = 'Try it';
                btn.addEventListener('click', () => tryRequest(ep, params,
                                                              bodyProps));
                panel.appendChild(btn);

                const resp = document.createElement('div');
                resp.id = 'response-area';
                panel.appendChild(resp);
        }

        function schemaSummary(s) {
                if(!s) return '<code>any</code>';
                if(s.$ref) {
                        const name = s.$ref.split('/').pop();
                        return '<code>' + escapeHtml(name) + '</code>';
                }
                if(s.oneOf) {
                        return '<code>' + s.oneOf.map(schemaTypeOnly)
                                .join(' | ') + '</code>';
                }
                let t = s.type || 'any';
                if(s.format) t += ' (' + s.format + ')';
                if(s.enum)   t += ' [enum]';
                return '<code>' + escapeHtml(t) + '</code>';
        }

        function schemaTypeOnly(s) {
                if(s.$ref) return s.$ref.split('/').pop();
                return s.type || 'any';
        }

        function defaultValueAsString(s) {
                if(!s) return '';
                if(s.default == null) return '';
                if(typeof s.default === 'object') return JSON.stringify(s.default);
                return String(s.default);
        }

        // ------------------------------------------------------------
        // Try-it
        // ------------------------------------------------------------

        async function tryRequest(ep, params, bodyProps) {
                let url = ep.path;
                const queryParts = [];
                const headers = {};

                // Substitute path/query/header inputs.
                for(const p of params) {
                        const inp = document.querySelector(
                                'input[data-param="' + cssEscape(p.name) +
                                '"][data-in="' + cssEscape(p.in) + '"]');
                        if(!inp) continue;
                        const val = inp.value;
                        if(val === '' && !p.required) continue;
                        if(p.in === 'path') {
                                url = url.replace('{' + p.name + '}',
                                        encodeURIComponent(val));
                        } else if(p.in === 'query') {
                                queryParts.push(encodeURIComponent(p.name) +
                                        '=' + encodeURIComponent(val));
                        } else if(p.in === 'header') {
                                headers[p.name] = val;
                        }
                }
                if(queryParts.length > 0) url += '?' + queryParts.join('&');

                let body = undefined;
                if(bodyProps.length > 0) {
                        const obj = {};
                        for(const name of bodyProps) {
                                const inp = document.querySelector(
                                        'input[data-body="' + cssEscape(name) + '"]');
                                if(!inp || inp.value === '') continue;
                                // Try JSON parse first (so booleans/numbers
                                // come through correctly), fall back to
                                // string.
                                try { obj[name] = JSON.parse(inp.value); }
                                catch { obj[name] = inp.value; }
                        }
                        body = JSON.stringify(obj);
                        headers['Content-Type'] = 'application/json';
                } else {
                        const raw = document.getElementById('body-raw');
                        if(raw && raw.value.trim() !== '') {
                                body = raw.value;
                                headers['Content-Type'] = 'application/json';
                        }
                }

                const opts = { method: ep.method, headers };
                if(body !== undefined) opts.body = body;

                const respArea = document.getElementById('response-area');
                respArea.innerHTML = '<p class="hint">sending…</p>';
                let res, text;
                try {
                        res = await fetch(url, opts);
                        text = await res.text();
                } catch(e) {
                        respArea.innerHTML =
                                '<p class="status-err">network error: ' +
                                escapeHtml(e.message) + '</p>';
                        return;
                }

                let pretty = text;
                try {
                        pretty = JSON.stringify(JSON.parse(text), null, 2);
                } catch { /* leave as-is */ }

                const klass = res.ok ? 'status-ok' : 'status-err';
                respArea.innerHTML =
                        '<div class="response">' +
                        '<div class="response-status">' +
                        '<span class="' + klass + '">' + res.status +
                                ' ' + escapeHtml(res.statusText) + '</span>' +
                        '<span class="hint">' + escapeHtml(url) + '</span>' +
                        '</div>' +
                        '<pre>' + escapeHtml(pretty) + '</pre>' +
                        '</div>';
        }

        // ------------------------------------------------------------
        // Utilities
        // ------------------------------------------------------------

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

        document.getElementById('filter').addEventListener('input',
                () => renderEndpointList());

        load();
})();
