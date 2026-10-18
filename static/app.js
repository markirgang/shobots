/**
 * SHOBOTS Master Animatronic & Robotic Control Center Client Logic
 */

document.addEventListener('DOMContentLoaded', () => {
    let fleetStatus = {};
    let selectedRobotKey = null;

    const navTabsContainer = document.getElementById('navTabs');
    const viewDashboard = document.getElementById('viewDashboard');
    const robotGrid = document.getElementById('robotGrid');
    const mainRobotFrame = document.getElementById('mainRobotFrame');
    const framePlaceholder = document.getElementById('framePlaceholder');
    const activeFrameUrlBadge = document.getElementById('activeFrameUrlBadge');
    const globalStatusText = document.getElementById('globalStatusText');
    const btnLaunchAll = document.getElementById('btnLaunchAll');
    const btnLaunchAllBanner = document.getElementById('btnLaunchAllBanner');
    const btnStartAll = document.getElementById('btnStartAll');
    const btnStartAllBanner = document.getElementById('btnStartAllBanner');
    const btnCloseAll = document.getElementById('btnCloseAll');
    const btnCloseAllBanner = document.getElementById('btnCloseAllBanner');
    const btnResetAllConfig = document.getElementById('btnResetAllConfig');
    const btnRefreshStatus = document.getElementById('btnRefreshStatus');
    const btnReloadFrame = document.getElementById('btnReloadFrame');
    const btnOpenNewTab = document.getElementById('btnOpenNewTab');

    // Modal elements
    const modalBackdrop = document.getElementById('modalConfigBackdrop');
    const modalTitle = document.getElementById('modalConfigTitle');
    const modalClose = document.getElementById('modalConfigClose');
    const btnCancelConfig = document.getElementById('btnCancelConfig');
    const btnSaveConfig = document.getElementById('btnSaveConfig');
    const btnResetSingleConfig = document.getElementById('btnResetSingleConfig');
    
    const cfgAppKey = document.getElementById('cfgAppKey');
    const cfgAppDir = document.getElementById('cfgAppDir');
    const cfgAppHost = document.getElementById('cfgAppHost');
    const cfgAppPort = document.getElementById('cfgAppPort');
    const cfgDefaultText = document.getElementById('cfgDefaultText');

    function launchAllPages() {
        Object.keys(fleetStatus).forEach(key => {
            if (fleetStatus[key] && fleetStatus[key].url) {
                window.open(fleetStatus[key].url, '_blank');
            }
        });
    }

    function closeAllServers() {
        if (confirm("Are you sure you want to close ALL running robot project servers?")) {
            controlFleet('stop', 'all');
        }
    }

    if (btnLaunchAll) btnLaunchAll.addEventListener('click', launchAllPages);
    if (btnLaunchAllBanner) btnLaunchAllBanner.addEventListener('click', launchAllPages);

    // Init Action Buttons
    if (btnRefreshStatus) btnRefreshStatus.addEventListener('click', fetchFleetStatus);
    if (btnStartAll) btnStartAll.addEventListener('click', () => controlFleet('start', 'all'));
    if (btnStartAllBanner) btnStartAllBanner.addEventListener('click', () => controlFleet('start', 'all'));
    if (btnCloseAll) btnCloseAll.addEventListener('click', closeAllServers);
    if (btnCloseAllBanner) btnCloseAllBanner.addEventListener('click', closeAllServers);
    if (btnResetAllConfig) {
        btnResetAllConfig.addEventListener('click', () => {
            if (confirm("Reset ALL robot app folders, IP addresses, and ports back to factory defaults?")) {
                resetConfig('all');
            }
        });
    }

    if (btnReloadFrame) {
        btnReloadFrame.addEventListener('click', () => {
            if (mainRobotFrame.src && mainRobotFrame.src !== 'about:blank') {
                mainRobotFrame.src = mainRobotFrame.src;
            }
        });
    }

    if (btnOpenNewTab) {
        btnOpenNewTab.addEventListener('click', () => {
            if (selectedRobotKey && fleetStatus[selectedRobotKey]) {
                window.open(fleetStatus[selectedRobotKey].url, '_blank');
            } else if (mainRobotFrame.src && mainRobotFrame.src !== 'about:blank') {
                window.open(mainRobotFrame.src, '_blank');
            }
        });
    }

    // Modal Listeners
    if (modalClose) modalClose.addEventListener('click', closeModal);
    if (btnCancelConfig) btnCancelConfig.addEventListener('click', closeModal);
    if (modalBackdrop) {
        modalBackdrop.addEventListener('click', (e) => {
            if (e.target === modalBackdrop) closeModal();
        });
    }

    if (btnSaveConfig) {
        btnSaveConfig.addEventListener('click', () => {
            const key = cfgAppKey.value;
            const dir = cfgAppDir.value.trim();
            const host = cfgAppHost.value.trim();
            const port = parseInt(cfgAppPort.value.trim());

            if (!dir || !host || isNaN(port)) {
                alert("Please fill in a valid Folder, IP Address, and Port.");
                return;
            }

            saveConfig(key, dir, host, port);
        });
    }

    if (btnResetSingleConfig) {
        btnResetSingleConfig.addEventListener('click', () => {
            const key = cfgAppKey.value;
            if (key) {
                resetConfig(key);
            }
        });
    }

    // Initial Status Check
    fetchFleetStatus();
    setInterval(fetchFleetStatus, 2500);

    function fetchFleetStatus() {
        fetch('/api/status')
            .then(res => res.json())
            .then(data => {
                fleetStatus = data;
                updateGlobalStatus();
                renderNavTabs();
                renderRobotGrid();
            })
            .catch(err => {
                console.error('Failed to fetch status:', err);
                if (globalStatusText) {
                    globalStatusText.textContent = 'OFFLINE';
                    globalStatusText.style.color = 'var(--accent-red)';
                }
            });
    }

    function updateGlobalStatus() {
        const keys = Object.keys(fleetStatus);
        const onlineCount = keys.filter(k => fleetStatus[k].online).length;
        const total = keys.length;

        if (globalStatusText) {
            globalStatusText.textContent = `${onlineCount}/${total} ONLINE`;
            globalStatusText.style.color = onlineCount === total ? 'var(--accent-green)' : (onlineCount > 0 ? 'var(--accent-orange)' : 'var(--accent-red)');
        }
    }

    function renderNavTabs() {
        if (!navTabsContainer) return;
        
        let html = `
            <div class="nav-tab-group ${!selectedRobotKey ? 'active' : ''}">
                <button class="nav-tab-btn ${!selectedRobotKey ? 'active' : ''}" data-view="dashboard">🎛️ Control Panel</button>
            </div>
        `;

        Object.keys(fleetStatus).forEach(key => {
            const bot = fleetStatus[key];
            const isActive = selectedRobotKey === key;
            html += `
                <div class="nav-tab-group ${isActive ? 'active' : ''}">
                    <button class="nav-tab-btn ${isActive ? 'active' : ''}" data-key="${key}" title="View ${bot.name} in Switcher Frame">
                        ${bot.icon} ${bot.name}
                    </button>
                    <button class="nav-tab-launch-btn" data-key="${key}" title="Launch ${bot.name} Web Page in new browser tab">
                        🚀 Launch
                    </button>
                </div>
            `;
        });

        navTabsContainer.innerHTML = html;

        // Bind Nav Listeners
        navTabsContainer.querySelectorAll('.nav-tab-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const targetView = e.currentTarget.dataset.view;
                const key = e.currentTarget.dataset.key;

                if (targetView === 'dashboard') {
                    selectedRobotKey = null;
                    viewDashboard.classList.add('active');
                    renderNavTabs();
                    renderRobotGrid();
                } else if (key) {
                    viewDashboard.classList.add('active');
                    selectRobotForFrame(key);
                }
            });
        });

        navTabsContainer.querySelectorAll('.nav-tab-launch-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const key = e.currentTarget.dataset.key;
                if (key && fleetStatus[key]) {
                    window.open(fleetStatus[key].url, '_blank');
                }
            });
        });
    }

    function renderRobotGrid() {
        if (!robotGrid) return;
        robotGrid.innerHTML = '';

        Object.keys(fleetStatus).forEach(key => {
            const bot = fleetStatus[key];
            const card = document.createElement('div');
            card.className = `robot-card ${selectedRobotKey === key ? 'active-selected' : ''}`;

            const isOnline = bot.online;
            const statusClass = isOnline ? 'online' : 'offline';
            const statusLabel = isOnline ? 'ONLINE' : 'OFFLINE';
            const displayHost = bot.host || '127.0.0.1';

            card.innerHTML = `
                <div class="robot-card-header">
                    <div class="robot-card-icon">${bot.icon}</div>
                    <div class="robot-status-pill ${statusClass}">
                        <span class="status-dot" style="background:${isOnline ? 'var(--accent-green)' : 'var(--accent-red)'}"></span>
                        <span>${statusLabel}</span>
                    </div>
                </div>

                <div class="robot-card-body">
                    <div class="robot-name">${bot.name}</div>
                    <div class="robot-port">⚡ http://${displayHost}:${bot.port}</div>
                    <div class="robot-dir-path" title="${bot.dir}">📁 ${bot.dir}</div>
                    <div class="robot-desc">${bot.description}</div>
                </div>

                <div class="robot-card-actions">
                    <button class="btn-preset primary btn-launch-page" data-key="${key}" style="width:100%; font-size:0.78rem;">
                        🚀 Launch Web Page
                    </button>
                    <button class="btn-preset btn-switch-frame" data-key="${key}" style="width:100%; font-size:0.75rem;">
                        🌐 Open Switcher
                    </button>
                    <div class="action-row">
                        <button class="btn-preset btn-start-single" data-key="${key}" style="font-size:0.7rem; ${isOnline ? 'opacity:0.6;' : ''}">
                            ▶ Start
                        </button>
                        <button class="btn-preset danger btn-stop-single" data-key="${key}" style="font-size:0.7rem; ${!isOnline ? 'opacity:0.6;' : ''}">
                            ⏹ Stop
                        </button>
                    </div>
                    <button class="btn-preset secondary btn-edit-config" data-key="${key}" style="width:100%; font-size:0.7rem; justify-content:center;">
                        ⚙️ Configure Combination
                    </button>
                </div>
            `;

            robotGrid.appendChild(card);
        });

        // Bind Card Action Listeners
        document.querySelectorAll('.btn-launch-page').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const key = e.currentTarget.dataset.key;
                if (key && fleetStatus[key]) {
                    window.open(fleetStatus[key].url, '_blank');
                }
            });
        });

        document.querySelectorAll('.btn-switch-frame').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const key = e.currentTarget.dataset.key;
                selectRobotForFrame(key);
            });
        });

        document.querySelectorAll('.btn-start-single').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const key = e.currentTarget.dataset.key;
                controlFleet('start', key);
            });
        });

        document.querySelectorAll('.btn-stop-single').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const key = e.currentTarget.dataset.key;
                controlFleet('stop', key);
            });
        });

        document.querySelectorAll('.btn-edit-config').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const key = e.currentTarget.dataset.key;
                openConfigModal(key);
            });
        });
    }

    function selectRobotForFrame(key) {
        if (!fleetStatus[key]) return;
        selectedRobotKey = key;
        const bot = fleetStatus[key];

        // Hide Placeholder & Update Frame URL
        if (framePlaceholder) framePlaceholder.style.display = 'none';
        if (activeFrameUrlBadge) activeFrameUrlBadge.textContent = `${bot.icon} ${bot.name} (${bot.url})`;

        // Load iframe if different
        const url = bot.url;
        if (mainRobotFrame.src !== url && mainRobotFrame.src !== url + '/') {
            mainRobotFrame.src = url;
        }

        renderNavTabs();
        renderRobotGrid();
    }

    function controlFleet(action, key) {
        fetch(`/api/${action}`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ robot: key })
        })
        .then(res => res.json())
        .then(() => {
            setTimeout(fetchFleetStatus, 600);
        })
        .catch(err => console.error('Control fleet error:', err));
    }

    function openConfigModal(key) {
        const bot = fleetStatus[key];
        if (!bot) return;

        cfgAppKey.value = key;
        modalTitle.textContent = `⚙️ Configure ${bot.icon} ${bot.name}`;
        cfgAppDir.value = bot.dir || '';
        cfgAppHost.value = bot.host || '127.0.0.1';
        cfgAppPort.value = bot.port || 8090;

        cfgDefaultText.textContent = `Folder: ${bot.default_dir} | Host: ${bot.default_host} | Port: ${bot.default_port}`;

        if (modalBackdrop) modalBackdrop.style.display = 'flex';
    }

    function closeModal() {
        if (modalBackdrop) modalBackdrop.style.display = 'none';
    }

    function saveConfig(key, dir, host, port) {
        fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                key: key,
                dir: dir,
                host: host,
                port: port,
                restart: true
            })
        })
        .then(res => res.json())
        .then(data => {
            closeModal();
            if (data.status) {
                fleetStatus = data.status;
                updateGlobalStatus();
                renderNavTabs();
                renderRobotGrid();
            } else {
                fetchFleetStatus();
            }
        })
        .catch(err => {
            alert('Error saving configuration: ' + err);
        });
    }

    function resetConfig(key) {
        fetch('/api/config/reset', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                key: key,
                restart: true
            })
        })
        .then(res => res.json())
        .then(data => {
            closeModal();
            if (data.status) {
                fleetStatus = data.status;
                updateGlobalStatus();
                renderNavTabs();
                renderRobotGrid();
            } else {
                fetchFleetStatus();
            }
        })
        .catch(err => {
            alert('Error resetting configuration: ' + err);
        });
    }
});
