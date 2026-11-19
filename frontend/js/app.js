/* frontend/js/app.js */

(() => {
    const API_BASE = `http://localhost:${window.location.port}`; // Proxy runs on same host/port as UI
    const refreshBtn = document.getElementById('refresh-btn');
    const themeToggle = document.getElementById('theme-toggle');
    const pageTitle = document.getElementById('page-title');
    const navItems = document.querySelectorAll('.nav-item');
    const tabContents = document.querySelectorAll('.tab-content');
    const nodeIdSpan = document.getElementById('node-id');

    // Mini‑browser elements
    const methodSelect = document.getElementById('method-select');
    const urlInput = document.getElementById('url-input');
    const goBtn = document.getElementById('go-btn');
    const previewFrame = document.getElementById('preview-frame');
    const rawView = document.getElementById('raw-response');
    const headersTableBody = document.querySelector('#headers-table tbody');
    const browserBadges = document.getElementById('browser-badges');
    const browserTabs = document.getElementById('browser-tabs');
    const previewDiv = document.getElementById('preview-view');
    const rawDiv = document.getElementById('raw-view');
    const headersDiv = document.getElementById('headers-view');

    // Cache analytics elements
    const cacheTableBody = document.querySelector('#cache-table tbody');
    const clearCacheBtn = document.getElementById('clear-cache-btn');
    const policySelect = document.getElementById('policy-select');
    const policyBadge = document.getElementById('policy-badge');

    // Metric cards container
    const metricsGrid = document.getElementById('metrics-grid');

    // Chart objects (will be instantiated on first load)
    let hitMissChart, responseTimeChart, throughputChart, memoryChart;

    function fetchJson(path) {
        return fetch(`${API_BASE}${path}`)
            .then(res => {
                if (!res.ok) throw new Error('Network error: ' + res.status);
                return res.json();
            })
            .catch(err => {
                console.error('Fetch error', err);
                return null;
            });
    }

    function updateMetrics(data) {
        // Clear previous cards
        metricsGrid.innerHTML = '';
        const cards = [
            { label: 'Total Requests', value: data.total_requests, type: 'info' },
            { label: 'Cache Hits', value: data.cache_hits, type: 'success' },
            { label: 'Cache Misses', value: data.cache_misses, type: 'warning' },
            { label: 'Cache Hit Ratio', value: data.cache_hit_ratio_percent?.toFixed(2) + '%', type: 'purple' },
            { label: 'Active Connections', value: data.active_connections, type: 'info' },
            { label: 'Active Workers', value: data.active_workers, type: 'info' },
            { label: 'Queue Length', value: data.queue_length, type: 'info' },
            { label: 'Avg Response Time (ms)', value: data.avg_response_time_ms?.toFixed(1), type: 'info' },
            { label: 'P95 Response Time (ms)', value: data.p95_response_time_ms?.toFixed(1), type: 'info' },
            { label: 'Throughput (req/s)', value: data.throughput_rps?.toFixed(2), type: 'info' },
            { label: 'Cache Memory (MB)', value: (data.cache_memory_bytes / (1024 * 1024)).toFixed(2), type: 'info' },
        ];
        cards.forEach(c => {
            const el = document.createElement('div');
            el.className = `metric-card ${c.type}`;
            el.innerHTML = `
                <div class="metric-header"><span class="metric-label">${c.label}</span></div>
                <div class="metric-val">${c.value}</div>
            `;
            metricsGrid.appendChild(el);
        });
    }

    function initCharts() {
        const hitMissCtx = document.getElementById('hitMissChart').getContext('2d');
        const responseTimeCtx = document.getElementById('responseTimeChart').getContext('2d');
        const throughputCtx = document.getElementById('throughputChart').getContext('2d');
        const memoryCtx = document.getElementById('memoryChart').getContext('2d');

        hitMissChart = new Chart(hitMissCtx, {
            type: 'doughnut',
            data: { labels: ['Hit', 'Miss'], datasets: [{ data: [0, 0], backgroundColor: ['#10b981', '#f59e0b'] }] },
            options: { responsive: true, plugins: { legend: { position: 'bottom' } } }
        });
        responseTimeChart = new Chart(responseTimeCtx, {
            type: 'line',
            data: { labels: [], datasets: [{ label: 'Response Time (ms)', data: [], borderColor: '#3b82f6', fill: false }] },
            options: { responsive: true, scales: { x: { type: 'realtime', realtime: { delay: 2000 } } } }
        });
        throughputChart = new Chart(throughputCtx, {
            type: 'line',
            data: { labels: [], datasets: [{ label: 'Throughput (req/s)', data: [], borderColor: '#8b5cf6', fill: false }] },
            options: { responsive: true }
        });
        memoryChart = new Chart(memoryCtx, {
            type: 'line',
            data: { labels: [], datasets: [{ label: 'Cache Memory (MB)', data: [], borderColor: '#06b6d4', fill: false }] },
            options: { responsive: true }
        });
    }

    function updateCharts(data) {
        // Hit/Miss doughnut
        hitMissChart.data.datasets[0].data = [data.cache_hits, data.cache_misses];
        hitMissChart.update();

        const now = new Date().toLocaleTimeString();
        // Response time line (use avg_response_time_ms)
        responseTimeChart.data.labels.push(now);
        responseTimeChart.data.datasets[0].data.push(data.avg_response_time_ms);
        if (responseTimeChart.data.labels.length > 30) {
            responseTimeChart.data.labels.shift();
            responseTimeChart.data.datasets[0].data.shift();
        }
        responseTimeChart.update();

        // Throughput line
        throughputChart.data.labels.push(now);
        throughputChart.data.datasets[0].data.push(data.throughput_rps);
        if (throughputChart.data.labels.length > 30) { throughputChart.data.labels.shift(); throughputChart.data.datasets[0].data.shift(); }
        throughputChart.update();

        // Memory line
        memoryChart.data.labels.push(now);
        memoryChart.data.datasets[0].data.push(data.cache_memory_mb);
        if (memoryChart.data.labels.length > 30) { memoryChart.data.labels.shift(); memoryChart.data.datasets[0].data.shift(); }
        memoryChart.update();
    }

    async function refreshDashboard() {
        const stats = await fetchJson('/api/stats');
        if (!stats) return;
        updateMetrics(stats);
        updateCharts(stats);
        nodeIdSpan.textContent = stats.node_id || 'node-1';
    }

    // Mini‑browser logic
    async function performRequest() {
        const method = methodSelect.value;
        const url = urlInput.value.trim();
        if (!url) return alert('Please enter a URL');

        // Build a proxy request via the same origin (the proxy listens on the same port)
        const proxyUrl = `${API_BASE}${new URL(url).pathname}`; // simple mapping, more elaborate logic could be added
        const opts = { method, headers: { 'X-Proxy-Client': 'mini-browser' } };
        const start = performance.now();
        let res;
        try {
            res = await fetch(url, opts);
        } catch (e) {
            alert('Request failed: ' + e.message);
            return;
        }
        const latency = performance.now() - start;
        const clone = res.clone();
        const text = await clone.text();
        // Update badges based on X‑Proxy‑Cache header if present
        const cacheHeader = res.headers.get('X-Proxy-Cache') || 'BYPASS';
        const status = res.status;
        const badgeClass = {
            'HIT': 'badge-hit',
            'MISS': 'badge-miss',
            'REVALIDATED': 'badge-reval',
            'PEER_HIT': 'badge-peer',
            'BYPASS': 'badge-bypass',
            'ORIGIN_ERROR': 'badge-error'
        }[cacheHeader] || 'badge-bypass';
        browserBadges.innerHTML = `<span class="badge ${badgeClass}">${cacheHeader}</span> <span class="badge badge-${status}">${status}</span> <span class="badge">${latency.toFixed(1)} ms</span>`;
        // Populate preview / raw / headers
        previewFrame.srcdoc = text;
        rawView.textContent = text;
        // Headers table
        headersTableBody.innerHTML = '';
        res.headers.forEach((value, name) => {
            const tr = document.createElement('tr');
            tr.innerHTML = `<td>${name}</td><td>${value}</td>`;
            headersTableBody.appendChild(tr);
        });
    }

    function switchTab(tabId) {
        tabContents.forEach(t => t.classList.toggle('active', t.id === tabId));
        pageTitle.textContent = document.querySelector(`.nav-item[data-tab="${tabId.replace('-tab','')}"] button`).textContent.trim();
    }

    // Navigation click handlers
    navItems.forEach(item => {
        item.addEventListener('click', () => {
            navItems.forEach(i => i.classList.remove('active'));
            item.classList.add('active');
            const tab = item.dataset.tab + '-tab';
            switchTab(tab);
        });
    });

    // Preset URL pills
    document.querySelectorAll('.preset-pill').forEach(p => {
        p.addEventListener('click', () => {
            urlInput.value = p.dataset.url;
        });
    });

    // Browser tab view toggle
    browserTabs.addEventListener('click', e => {
        if (!e.target.matches('.browser-tab')) return;
        const view = e.target.dataset.view;
        previewDiv.style.display = view === 'preview' ? '' : 'none';
        rawDiv.style.display = view === 'raw' ? '' : 'none';
        headersDiv.style.display = view === 'headers' ? '' : 'none';
        // active class
        browserTabs.querySelectorAll('.browser-tab').forEach(t => t.classList.toggle('active', t === e.target));
    });

    // Mini‑browser button
    goBtn.addEventListener('click', performRequest);
    // Refresh button on dashboard
    refreshBtn.addEventListener('click', refreshDashboard);
    // Theme toggle
    themeToggle.addEventListener('click', () => {
        const html = document.documentElement;
        const current = html.getAttribute('data-theme');
        html.setAttribute('data-theme', current === 'dark' ? 'light' : 'dark');
    });
    // Cache clear
    clearCacheBtn.addEventListener('click', async () => {
        await fetchJson('/api/cache/clear');
        await loadCacheTable();
    });
    // Policy change
    policySelect.addEventListener('change', async () => {
        const type = policySelect.value;
        await fetchJson(`/api/cache/policy?type=${type}`);
        policyBadge.textContent = `Policy: ${type}`;
        await loadCacheTable();
    });

    // Load cache entries into table
    async function loadCacheTable() {
        const entries = await fetchJson('/api/cache');
        if (!entries) return;
        cacheTableBody.innerHTML = '';
        entries.forEach(e => {
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td>${e.key}</td>
                <td>${e.is_expired ? 'Expired' : 'Valid'}</td>
                <td>${e.age_seconds}</td>
                <td>${e.ttl_seconds}</td>
                <td>${(e.memory_bytes/1024).toFixed(1)}</td>
                <td>${e.access_count}</td>
            `;
            cacheTableBody.appendChild(tr);
        });
    }

    // Initial load
    initCharts();
    refreshDashboard();
    loadCacheTable();
    // Auto‑refresh every 5 seconds
    setInterval(refreshDashboard, 5000);
})();
