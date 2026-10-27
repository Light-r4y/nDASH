// ota_page.h
// The /ota page: update the firmware and the www partition from a browser. It is built
// into the firmware rather than kept in www, so it works with an empty www partition too.
// Endpoints: /api/ota/update, /api/www/update, /api/status.
#pragma once

const char LND_OTA_PAGE_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>nDash - Update</title>
    <link rel="stylesheet" href="/bootstrap.min.css">
    <link rel="stylesheet" href="/fontawesome/css/all.min.css">
    <style>
        :root {
            --nd-bg: #16181b;
            --nd-bg-elevated: #1f2226;
            --nd-bg-elevated-2: #262a2f;
            --nd-border: #34383f;
            --nd-text: #eceef0;
            --nd-text-muted: #8b939c;
            --nd-accent: #ffc933;
            --nd-accent-dark: #cf9f1a;
            --nd-accent-soft: rgba(255, 201, 51, 0.12);
            --nd-danger: #ff5c5c;
            --nd-success: #35d07f;
        }

        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            background: var(--nd-bg);
            color: var(--nd-text);
            min-height: 100vh;
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
        }

        .navbar {
            background: var(--nd-bg-elevated) !important;
            border-bottom: 1px solid var(--nd-border);
        }

        .navbar-brand {
            font-weight: 700;
            font-size: 1.5rem;
            color: var(--nd-text) !important;
        }

        .navbar-brand i {
            color: var(--nd-accent);
        }

        .main-container {
            max-width: 700px;
            margin: 80px auto 40px;
            padding: 0 20px;
        }

        .card {
            background: var(--nd-bg-elevated);
            border: 1px solid var(--nd-border);
            border-radius: 15px;
            margin-bottom: 30px;
            overflow: hidden;
        }

        .card-header {
            background: var(--nd-bg-elevated-2);
            color: var(--nd-text);
            border-bottom: 1px solid var(--nd-border);
            padding: 20px;
            font-size: 1.2rem;
            font-weight: 600;
            display: flex;
            align-items: center;
            gap: 10px;
        }

        .card-header i {
            color: var(--nd-accent);
        }

        .card-body {
            padding: 25px;
        }

        .status-bar {
            background: var(--nd-bg-elevated);
            border: 1px solid var(--nd-border);
            border-radius: 10px;
            padding: 12px 15px;
            margin-bottom: 18px;
            display: flex;
            align-items: center;
            gap: 10px;
            font-size: 0.9rem;
        }

        .status-dot {
            width: 10px;
            height: 10px;
            border-radius: 50%;
            background: var(--nd-text-muted);
        }

        .status-dot.connected { background: var(--nd-success); }
        .status-dot.uploading { background: var(--nd-accent); animation: pulse 1s infinite; }
        .status-dot.error { background: var(--nd-danger); }

        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.5; }
        }

        .upload-zone {
            border: 3px dashed var(--nd-border);
            border-radius: 15px;
            padding: 30px 20px;
            text-align: center;
            cursor: pointer;
            transition: all 0.3s ease;
            background: var(--nd-bg);
        }

        .upload-zone:hover {
            border-color: var(--nd-accent);
            background: var(--nd-accent-soft);
        }

        .upload-zone i {
            font-size: 2.5rem;
            color: var(--nd-accent);
            margin-bottom: 10px;
        }

        .upload-zone p {
            color: var(--nd-text-muted);
        }

        .file-name {
            font-weight: 600;
            margin-top: 10px;
            display: none;
            color: var(--nd-text);
        }

        .btn {
            border-radius: 10px;
            padding: 10px 24px;
            font-weight: 600;
        }

        .btn-primary {
            background: var(--nd-accent);
            border: none;
            color: #1a1a1a;
        }

        .btn-primary:hover {
            background: var(--nd-accent-dark);
            color: #1a1a1a;
        }

        .btn-primary:disabled {
            background: var(--nd-bg-elevated-2);
            color: var(--nd-text-muted);
        }

        .btn-outline-light {
            border: 1px solid var(--nd-border);
            color: var(--nd-text);
            background: transparent;
        }

        .btn-outline-light:hover {
            background: var(--nd-bg-elevated-2);
            color: var(--nd-text);
        }

        .alert {
            border-radius: 10px;
            padding: 12px 18px;
            margin-top: 15px;
            display: none;
            border: 1px solid var(--nd-border);
        }

        .alert.show {
            display: flex;
            align-items: center;
            gap: 10px;
        }

        .alert-success {
            background: rgba(53, 208, 127, 0.12);
            color: var(--nd-success);
        }

        .alert-danger {
            background: rgba(255, 92, 92, 0.12);
            color: var(--nd-danger);
        }

        input[type="file"] {
            display: none;
        }

        .upload-progress {
            height: 8px;
            border-radius: 4px;
            background: var(--nd-bg-elevated-2);
            overflow: hidden;
            margin-bottom: 18px;
            display: none;
        }

        .upload-progress.show {
            display: block;
        }

        .upload-progress-bar {
            height: 100%;
            width: 0;
            background: var(--nd-accent);
            transition: width 0.2s ease;
        }
    </style>
</head>
<body>
    <nav class="navbar navbar-dark fixed-top">
        <div class="container">
            <a class="navbar-brand" href="/">
                <i class="fas fa-gauge-high"></i> nDash
            </a>
            <a href="/" class="btn btn-outline-light btn-sm">
                <i class="fas fa-arrow-left"></i> Back to control
            </a>
        </div>
    </nav>

    <div class="main-container">
        <div class="status-bar">
            <div class="status-dot" id="statusDot"></div>
            <span id="statusText">Ready</span>
        </div>
        <div class="upload-progress" id="uploadProgress">
            <div class="upload-progress-bar" id="uploadProgressBar"></div>
        </div>

        <!-- Firmware update -->
        <div class="card">
            <div class="card-header"><i class="fas fa-microchip"></i> Firmware update</div>
            <div class="card-body">
                <div class="upload-zone" onclick="document.getElementById('fwInput').click()">
                    <i class="fas fa-cloud-upload-alt"></i>
                    <p><strong>Choose a firmware file</strong> (.bin)</p>
                    <div class="file-name" id="fwFileName"></div>
                </div>
                <input type="file" id="fwInput" accept=".bin" onchange="onFileSelected(event, 'fw')">
                <button class="btn btn-primary w-100 mt-3" id="fwBtn" disabled onclick="startUpdate('fw')">
                    <i class="fas fa-upload"></i> Flash device
                </button>
            </div>
        </div>

        <!-- Web interface update -->
        <div class="card">
            <div class="card-header"><i class="fas fa-globe"></i> Web interface update</div>
            <div class="card-body">
                <div class="upload-zone" onclick="document.getElementById('webInput').click()">
                    <i class="fas fa-cloud-upload-alt"></i>
                    <p><strong>Choose a filesystem image</strong> (www.bin)</p>
                    <div class="file-name" id="webFileName"></div>
                </div>
                <input type="file" id="webInput" accept=".bin" onchange="onFileSelected(event, 'web')">
                <button class="btn btn-primary w-100 mt-3" id="webBtn" disabled onclick="startUpdate('web')">
                    <i class="fas fa-upload"></i> Update files
                </button>
            </div>
        </div>

        <div class="alert" id="alertMessage"></div>
    </div>

    <script>
        const files = { fw: null, web: null };
        const endpoints = { fw: '/api/ota/update', web: '/api/www/update' };
        const labels = { fw: 'the firmware', web: 'the web interface files' };

        function onFileSelected(event, kind) {
            const file = event.target.files[0];
            if (!file) return;
            files[kind] = file;
            const nameEl = document.getElementById(kind + 'FileName');
            nameEl.textContent = file.name + ' (' + formatSize(file.size) + ')';
            nameEl.style.display = 'block';
            document.getElementById(kind + 'Btn').disabled = false;
        }

        const UPLOAD_TIMEOUT_MS = 5 * 60 * 1000;

        function upload(url, file, onProgress) {
            const xhr = new XMLHttpRequest();
            const promise = new Promise((resolve, reject) => {
                xhr.open('POST', url);
                xhr.setRequestHeader('Content-Type', 'application/octet-stream');
                xhr.timeout = UPLOAD_TIMEOUT_MS;
                xhr.upload.onprogress = (e) => {
                    if (e.lengthComputable) onProgress(e.loaded / e.total);
                };
                xhr.onload = () => resolve({ status: xhr.status, text: xhr.responseText });
                xhr.onerror = () => reject(new Error('connection lost'));
                xhr.ontimeout = () => reject(new Error('no response from the device'));
                xhr.onabort = () => reject(new Error('aborted'));
                xhr.send(file);
            });
            return { xhr, promise };
        }

        async function fetchWithTimeout(url, ms) {
            const ctrl = new AbortController();
            const t = setTimeout(() => ctrl.abort(), ms);
            try {
                return await fetch(url, { cache: 'no-store', signal: ctrl.signal });
            } finally {
                clearTimeout(t);
            }
        }

        async function fetchBuildId() {
            try {
                const r = await fetchWithTimeout('/.build_id', 3000);
                return r.ok ? (await r.text()).trim() : null;
            } catch (e) {
                return null;
            }
        }

        async function fetchUpdateResult(id) {
            try {
                const r = await fetchWithTimeout('/api/status', 3000);
                if (!r.ok) return null;
                const u = (await r.json()).www_update;
                return (u && u.id === id && u.done) ? u : null;
            } catch (e) {
                return null;
            }
        }

        function watchResult(id, buildIdBefore) {
            let stop = false;
            const promise = (async () => {
                while (!stop) {
                    await new Promise(r => setTimeout(r, 3000));
                    if (stop) break;
                    const u = await fetchUpdateResult(id);
                    if (u) return u;
                    if (buildIdBefore !== null) {
                        const b = await fetchBuildId();
                        if (b !== null && b !== buildIdBefore) return { done: true, ok: true };
                    }
                }
                return null;
            })();
            return { promise, cancel: () => { stop = true; } };
        }

        function showWebSuccess(u) {
            setProgress(1);
            setStatus('connected', 'Files updated');
            let stats = '';
            if (u && typeof u.blocks_written === 'number') {
                const total = u.blocks_written + u.blocks_skipped;
                stats = ' Rewritten ' + u.blocks_written + ' of ' + total +
                    ' blocks, ' + (u.total_ms / 1000).toFixed(1) + ' s on the device.';
            }
            showAlert('Web interface files updated.' + stats + ' Reloading page...', 'success');
            setTimeout(() => { window.location.href = '/'; }, 3000);
        }

        function setProgress(fraction) {
            document.getElementById('uploadProgress').classList.add('show');
            document.getElementById('uploadProgressBar').style.width =
                Math.round(fraction * 100) + '%';
        }

        async function waitForDevice() {
            for (let i = 0; i < 60; i++) {
                await new Promise(r => setTimeout(r, 2000));
                try {
                    const r = await fetch('/api/status', { cache: 'no-store' });
                    if (r.ok) return true;
                } catch (e) { }
            }
            return false;
        }

        async function diagnose(startedAt) {
            try {
                const r = await fetch('/api/status', { cache: 'no-store' });
                if (!r.ok) return ' The device answers, but with HTTP ' + r.status + '.';
                const d = await r.json();
                const elapsed = (Date.now() - startedAt) / 1000;
                if (typeof d.uptime_s === 'number' && d.uptime_s < elapsed) {
                    return ' The device REBOOTED during the update ' + Math.round(d.uptime_s) +
                        ' s ago (reason: ' + (d.reset_reason || 'unknown') + '). Reload the page to see which files it has now.';
                }
                return ' The device is running and did not reboot, but its reply was lost. Reload the page to check whether the update applied.';
            } catch (e) {
                return ' The device is not reachable right now - reconnect to its Wi-Fi network and reload the page.';
            }
        }

        async function startUpdate(kind) {
            const file = files[kind];
            if (!file) return;
            if (!confirm('Update ' + labels[kind] + '?')) return;

            setStatus('uploading', 'Uploading... 0%');
            setProgress(0);
            document.getElementById(kind + 'Btn').disabled = true;

            let uploaded = false;
            let answered = false;
            const startedAt = Date.now();
            let writeTimer = null;
            const stopWriteTimer = () => { if (writeTimer) { clearInterval(writeTimer); writeTimer = null; } };

            const updateId = Math.random().toString(36).slice(2, 12);
            let url = endpoints[kind];
            if (kind === 'web') url += (url.includes('?') ? '&' : '?') + 'id=' + updateId;
            const buildIdBefore = kind === 'web' ? await fetchBuildId() : null;
            let watcher = null;

            try {
                const up = upload(url, file, (f) => {
                    setProgress(f);
                    if (f < 1) {
                        setStatus('uploading', 'Uploading... ' + Math.round(f * 100) + '%');
                    } else if (!uploaded) {
                        uploaded = true;
                        const writeStart = Date.now();
                        setStatus('uploading', 'Sent, the device is receiving and writing it... 0 s');
                        writeTimer = setInterval(() => {
                            setStatus('uploading', 'Sent, the device is receiving and writing it... ' +
                                Math.round((Date.now() - writeStart) / 1000) + ' s');
                        }, 1000);
                    }
                });

                let res;
                if (kind === 'web') {
                    watcher = watchResult(updateId, buildIdBefore);
                    const first = await Promise.race([
                        up.promise.then(r => ({ via: 'response', r })),
                        watcher.promise.then(u => ({ via: 'device', u }))
                    ]);
                    watcher.cancel();
                    if (first.via === 'device' && first.u) {
                        up.xhr.abort();
                        stopWriteTimer();
                        answered = true;
                        if (!first.u.ok) throw new Error(first.u.error || 'Update failed');
                        showWebSuccess(first.u);
                        return;
                    }
                    res = first.r;
                } else {
                    res = await up.promise;
                }
                stopWriteTimer();

                answered = true;
                let result = {};
                try { result = JSON.parse(res.text); } catch (e) { }
                if (res.status !== 200 || result.status !== 'success') {
                    const text = (res.text || '').trim();
                    const msg = (text && text.length < 120 && !text.includes('<'))
                        ? text : ('HTTP ' + res.status);
                    throw new Error(msg);
                }

                if (kind === 'fw') {
                    setStatus('uploading', 'Success! Rebooting...');
                    showAlert('Firmware updated, the device is rebooting. Waiting for it to come back...', 'success');
                    const back = await waitForDevice();
                    if (back) {
                        window.location.href = '/';
                    } else {
                        setStatus('error', 'Device did not come back');
                        showAlert('The device has not responded for 2 minutes. Reconnect to its Wi-Fi network and open the page again.', 'danger');
                    }
                } else {
                    showWebSuccess(result);
                }
            } catch (err) {
                let error = err;
                if (watcher) watcher.cancel();
                stopWriteTimer();
                if (kind === 'web' && uploaded && !answered) {
                    setStatus('uploading', 'Connection lost, asking the device for the result...');
                    for (let i = 0; i < 15; i++) {
                        const u = await fetchUpdateResult(updateId);
                        if (u) {
                            if (u.ok) { showWebSuccess(u); return; }
                            error = new Error(u.error || 'Update failed');
                            answered = true;
                            break;
                        }
                        const b = buildIdBefore !== null ? await fetchBuildId() : null;
                        if (b !== null && b !== buildIdBefore) { showWebSuccess(null); return; }
                        await new Promise(r => setTimeout(r, 2000));
                    }
                }
                setStatus('error', 'Update error');
                let hint = '';
                if (!answered) {
                    showAlert('Error: ' + error.message + '. Checking the device...', 'danger');
                    hint = await diagnose(startedAt);
                }
                showAlert('Error: ' + error.message + '.' + hint, 'danger');
                document.getElementById(kind + 'Btn').disabled = false;
            }
        }

        function setStatus(type, message) {
            document.getElementById('statusDot').className = 'status-dot ' + type;
            document.getElementById('statusText').textContent = message;
        }

        function showAlert(message, type) {
            const alert = document.getElementById('alertMessage');
            alert.className = 'alert alert-' + type + ' show';
            alert.innerHTML = '<i class="fas fa-' +
                (type === 'success' ? 'check-circle' : 'exclamation-circle') + '"></i> ' + message;
        }

        function formatSize(bytes) {
            if (bytes === 0) return '0 Bytes';
            const k = 1024;
            const sizes = ['Bytes', 'KB', 'MB'];
            const i = Math.floor(Math.log(bytes) / Math.log(k));
            return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
        }

        setStatus('connected', 'Ready');
    </script>
</body>
</html>
)rawliteral";
