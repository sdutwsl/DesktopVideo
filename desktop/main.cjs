const { app, BrowserWindow, WebContentsView, ipcMain, dialog, session, Menu, Tray, nativeImage, clipboard } = require('electron');
const path = require('node:path');
const fs = require('node:fs/promises');
const { existsSync } = require('node:fs');
const { pathToFileURL } = require('node:url');
const { Processes, safeError } = require('./processes.cjs');
const { PlayerPlugin } = require('./player.cjs');
const { Playback } = require('./playback.cjs');
const { Diagnostics } = require('./diagnostics.cjs');
const { themeTrayIcon } = require('./tray-icon.cjs');
const { sites, videoSite, isSiteVideo, videoSource } = require('./sites.cjs');
const { youtubeSource, httpUrl, electronCookie, exportCookies, resolveMedia, resolvePlaylist } = require('./media.cjs');

app.setName('Muralop');
const args = process.argv.slice(1);
const option = name => { const index = args.indexOf(name); return index < 0 ? '' : args[index + 1] || ''; };
const smoke = option('--smoke-test');
// Keep the Chromium profile beside the application's wallpaper configuration.
app.setPath('userData', option('--data-dir') ? path.resolve(option('--data-dir'))
  : path.join(app.getPath('appData'), process.platform === 'linux' ? 'muralop' : 'Muralop', 'desktop'));
const binaryDir = path.dirname(process.execPath);
const suffix = process.platform === 'win32' ? '.exe' : '';
function locate(name, explicit) {
  if (explicit) return explicit;
  const sibling = path.join(binaryDir, name + suffix);
  return existsSync(sibling) ? sibling : name + suffix;
}
const pluginPath = path.resolve(option('--player-plugin') || process.env.MURALOP_PLAYER_PLUGIN || path.join(__dirname, 'muralop.node'));
const configRoot = path.join(app.getPath('appData'), process.platform === 'linux' ? 'muralop' : 'Muralop');
const portableConfig = process.platform === 'win32' && existsSync(path.join(binaryDir, 'config.ini')) ? path.join(binaryDir, 'config.ini') : '';
const configPath = option('--config') ? path.resolve(option('--config')) : option('--data-dir')
  ? path.join(app.getPath('userData'), 'config.ini') : portableConfig || path.join(configRoot, 'config.ini');
const legacyPath = portableConfig || option('--config') || option('--data-dir') ? ''
  : path.join(app.getPath('appData'), process.platform === 'linux' ? 'videowallpaper' : 'VideoWallpaper', 'config.ini');
const defaultExtractor = locate('yt-dlp', process.env.MURALOP_YTDLP);
process.env.PATH = binaryDir + path.delimiter + process.env.PATH;
const processes = new Processes();
const player = new PlayerPlugin({ pluginPath, configPath, legacyPath, version: app.getVersion(),
  nodePath: locate('node', process.env.MURALOP_NODE) });
let window, browser, playback, youtubeSession, pollTimer, quitting = false;
let tray, diagnostics, trayState = '';
let trayIconController;
let mediaActions = Promise.resolve();
const lastErrors = new Map();
let browserError = '', browserBounds;
let browserHasContent = false, browserSlow = false, browserLoadTimer;
let publishedPlaylist;
let settings = { quality: '', extractor: '', browser: 'chrome', profile: '', site: 'youtube' };
const activeSite = () => sites.find(site => site.id === settings.site) || sites[0];
let selectedMonitor = '', lastAuto = '', lastAutoAt = 0, importing = false;
let queuedPlay = option('--play') ? { source: option('--play'), monitor: option('--monitor') } : null;
const labels = { connecting: '连接中', ready: '就绪', resolving: '解析中', loading: '加载中', playing: '播放中', paused: '已暂停', cancelled: '已取消', error: '失败' };

function publish(state = playback.state) {
  if (quitting) return;
  rememberError('playback', state.phase === 'error' ? state.message : '', '播放');
  rememberError('playlist', state.playlistError, '合集');
  player.syncNavigation(state);
  updateTray(state);
  const label = labels[state.phase] || state.phase;
  if (window && !window.isDestroyed()) {
    window.setTitle(`Muralop · ${label}${state.title ? ' · ' + state.title : ''}`);
    const payload = { ...state, settings, sites: sites.map(({ id, name }) => ({ id, name })), defaultExtractor, importing, browserError, diagnostics: diagnostics?.snapshot(state),
      browserLoading: browser?.webContents.isLoading(), browserHasContent, browserSlow,
      uiVisible: window.isVisible() && !window.isMinimized(), maximized: window.isMaximized() };
    // A large collection is sent once, not with every status or navigation event.
    if (publishedPlaylist === state.playlist) delete payload.playlist;
    else publishedPlaylist = state.playlist;
    window.webContents.send('state', payload);
  }
}

function rememberError(key, message, category) {
  if (message && lastErrors.get(key) !== message) diagnostics?.record(category, message);
  lastErrors.set(key, message || '');
}

function updateTray(state) {
  if (!tray) return;
  const key = `${state.playerReady}:${state.paused}:${state.mute}`;
  if (key === trayState) return;
  trayState = key;
  tray.setContextMenu(Menu.buildFromTemplate([
    { label: '打开 Muralop', click: showWindow },
    { type: 'separator' },
    { label: state.paused ? '继续播放' : '暂停播放', enabled: state.playerReady,
      click: () => { void player.request(state.paused ? 'resume' : 'pause').then(() => playback.poll()).catch(showError); } },
    { label: state.mute ? '取消静音' : '静音', enabled: state.playerReady,
      click: () => { void player.request(state.mute ? 'unmute' : 'mute').then(() => playback.poll()).catch(showError); } },
    { type: 'separator' },
    { label: '退出', click: () => { void quitApplication(); } },
  ]));
}

function createTray() {
  // Show the original native tray's fallback immediately. On KDE, replace it
  // with the user's themed wallpaper icon after the local renderer is ready.
  tray = new Tray(nativeImage.createFromPath(path.join(__dirname, 'assets', 'muralop.png')));
  tray.setToolTip('Muralop');
  tray.on('click', showWindow);
  updateTray(playback.state);
}

async function quitApplication() {
  if (quitting) return;
  quitting = true;
  trayIconController?.abort();
  clearTimeout(pollTimer);
  clearTimeout(browserLoadTimer);
  playback?.close();
  processes.close();
  await player.close();
  try { await diagnostics?.flush(); } catch { /* exiting must remain possible */ }
  tray?.destroy();
  tray = null;
  // Embedded pages and login popups must not veto an application exit.
  if (browser && !browser.webContents.isDestroyed()) browser.webContents.close({ waitForBeforeUnload: false });
  if (app.isReady()) for (const popup of BrowserWindow.getAllWindows()) if (popup !== window) popup.destroy();
  if (window && !window.isDestroyed()) window.destroy();
  app.quit();
}

function showWindow() {
  if (!window || window.isDestroyed()) return;
  if (window.isMinimized()) window.restore();
  window.show();
  window.focus();
  publish();
}

function showError(error) {
  diagnostics?.record('应用', error.message);
  playback.update({ ...(playback.job ? {} : { phase: 'error', canCancel: false }), message: safeError(error.message) });
}
async function saveSettings() {
  await fs.mkdir(app.getPath('userData'), { recursive: true });
  await fs.writeFile(path.join(app.getPath('userData'), 'settings.json'), JSON.stringify(settings, null, 2), { mode: 0o600 });
}
async function switchSite(id) {
  const site = sites.find(site => site.id === id);
  if (!site) throw new Error('未知的视频站点');
  if (settings.site === site.id) return;
  settings.site = site.id;
  await saveSettings();
  navigate(site.home);
  publish();
}
function allowNavigation(value) { return httpUrl(value) || value === 'about:blank'; }
function beginBrowserLoad(reset = false) {
  if (quitting) return;
  clearTimeout(browserLoadTimer);
  browserError = '';
  lastErrors.delete('browser');
  browserSlow = false;
  if (reset) { browserHasContent = false; browser.setVisible(false); }
  browserLoadTimer = setTimeout(() => {
    if (!quitting && browser.webContents.isLoading()) { browserSlow = true; publish(); }
  }, 8000);
  publish();
}
function browserFailed(message) {
  if (quitting) return;
  clearTimeout(browserLoadTimer);
  browserError = message;
  rememberError('browser', message, '网页');
  browserSlow = false;
  if (!browserHasContent) browser.setVisible(false);
  publish();
}
function navigate(value) {
  const url = String(value).trim();
  if (!httpUrl(url)) throw new Error('网页地址必须以 https:// 或 http:// 开头');
  beginBrowserLoad(true);
  void browser.webContents.loadURL(url).catch(error => {
    if (error.code !== 'ERR_ABORTED') browserFailed('网页加载失败：' + safeError(error.message));
  });
}
async function play(source, monitor = selectedMonitor) {
  await playback.play(source, monitor, settings.quality);
}
function playQueued() {
  const request = queuedPlay;
  queuedPlay = null;
  if (request) void play(request.source, request.monitor).catch(showError);
}
async function handleMediaAction({ action, value }) {
  if (quitting) return;
  switch (action) {
    case 'raise': showWindow(); return;
    case 'quit': await quitApplication(); return;
    case 'open-uri': void play(value).catch(showError); return;
    case 'next':
    case 'previous': {
      if (value !== playback.state.source || playback.state.playlistIndex < 0) return;
      const index = playback.state.playlistIndex + (action === 'next' ? 1 : -1);
      if (playback.state.playlist?.entries[index])
        void playback.playEntry(index, playback.state.monitor || '', settings.quality).catch(showError);
      return;
    }
    case 'stop':
      await playback.cancel(false);
      await playback.control(() => player.request('stop-all'));
      playback.invalidateStatus();
      playback.update({ phase: 'ready', canCancel: false, message: '已停止播放，可以重新选择视频' });
      break;
    case 'play':
    case 'play-pause': {
      const result = await playback.control(async () => {
        const status = await player.request('status');
        const command = action === 'play-pause' && !status.paused && ['playing', 'loading'].includes(status.mediaPlayback || status.playback) ? 'pause' : 'play';
        return player.request(command);
      });
      playback.invalidateStatus();
      if (!playback.job) {
        const active = result.mediaPlayback || result.playback;
        const phase = active === 'playing' ? (result.paused ? 'paused' : 'playing') : active === 'loading' ? 'loading' : 'ready';
        playback.update({ phase, message: phase === 'playing' ? '播放中' : phase === 'paused' ? '已暂停'
          : phase === 'loading' ? '加载中 · 正在恢复播放…' : '播放已就绪，可以选择视频' });
      }
      break;
    }
    case 'pause': {
      const status = await playback.control(() => player.request('pause'));
      playback.invalidateStatus();
      playback.update({ paused: status.paused });
      break;
    }
  }
  await playback.poll();
}
function autoPlay(value) {
  const url = youtubeSource(value) || videoSource(value);
  if (!url || (url === lastAuto && Date.now() - lastAutoAt < 2000)) return;
  lastAuto = url; lastAutoAt = Date.now();
  void play(url).catch(showError);
}

function wireBrowser(contents) {
  contents.setAudioMuted(true);
  contents.on('will-navigate', (event, url) => {
    if (!allowNavigation(url)) event.preventDefault();
    else if (videoSite(contents.getURL()) && isSiteVideo(url)) { event.preventDefault(); autoPlay(url); }
  });
  contents.setWindowOpenHandler(({ url }) => {
    if (videoSite(contents.getURL()) && isSiteVideo(url)) { autoPlay(url); return { action: 'deny' }; }
    return { action: allowNavigation(url) ? 'allow' : 'deny',
      overrideBrowserWindowOptions: { parent: window, width: 650, height: 800,
        webPreferences: { partition: 'persist:muralop-youtube', nodeIntegration: false, contextIsolation: true, sandbox: true } } };
  });
  contents.on('did-create-window', popup => { wireBrowser(popup.webContents); });
}

async function importSession(fromFile) {
  if (importing) return;
  let file;
  if (fromFile) {
    const chosen = await dialog.showOpenDialog(window, { title: '导入 cookies.txt', properties: ['openFile'], filters: [{ name: 'Cookie 文件', extensions: ['txt'] }] });
    if (chosen.canceled) return;
    file = chosen.filePaths[0];
  }
  importing = true;
  publish();
  try {
    const helper = process.env.MURALOP_SESSION_HELPER || locate('muralop-session');
    const params = file ? ['--cookie-file', file] : ['--browser', settings.browser, '--profile', settings.profile];
    const domains = [...new Set(sites.flatMap(site => [...site.domains, ...(site.loginDomains || [])]))];
    for (const domain of domains) params.push('--domain', domain);
    const records = JSON.parse(await processes.run(helper, params, { timeout: 20000, maxBytes: 2 * 1024 * 1024 }));
    let count = 0;
    const cookies = records.map(record => electronCookie(record, domains)).filter(Boolean);
    if (!cookies.length) throw new Error('没有读到已接入视频平台的可用登录会话');
    const importedSites = sites.filter(site => cookies.some(cookie => {
      const host = new URL(cookie.url).hostname;
      return [...site.domains, ...(site.loginDomains || [])].some(domain => host === domain || host.endsWith('.' + domain));
    }));
    const replacementDomains = importedSites.flatMap(site => [...site.domains, ...(site.loginDomains || [])]);
    // Replace the session instead of mixing it with an existing login: two
    // Google sessions in one jar make Google refuse to sign in.
    for (const cookie of await youtubeSession.cookies.get({})) {
      if (electronCookie({ ...cookie, expires: cookie.expirationDate }, replacementDomains))
        await youtubeSession.cookies.remove(`https://${cookie.domain.replace(/^\./, '')}${cookie.path || '/'}`, cookie.name);
    }
    let skipped = 0;
    for (const cookie of cookies) {
      // One cookie Chromium refuses must not abort the rest of the session.
      try { await youtubeSession.cookies.set(cookie); count++; } catch { skipped++; }
    }
    if (!count) throw new Error('浏览器不接受导入的登录会话');
    await youtubeSession.cookies.flushStore();
    playback.clearPlaylistCache();
    navigate(activeSite().home);
    playback.update({ message: `已为 ${importedSites.map(site => site.name).join('、')} 导入 ${count} 条会话${skipped ? `（跳过 ${skipped} 条浏览器不接受的 Cookie）` : ''}，切换平台即可使用，登录状态以网页显示为准。` });
  } finally { importing = false; publish(); }
}

async function clearSession() {
  const { response } = await dialog.showMessageBox(window, { type: 'question', buttons: ['清除', '取消'], defaultId: 1, cancelId: 1,
    message: '清除内嵌网页的登录会话？', detail: '会退出所有内嵌站点的登录，之后需要重新登录或导入。' });
  if (response !== 0) return;
  await youtubeSession.clearStorageData();
  await youtubeSession.clearCache();
  await fs.rm(path.join(app.getPath('userData'), 'youtube-cookies.txt'), { force: true });
  playback.clearPlaylistCache();
  navigate(activeSite().home);
  playback.update({ message: '已清除登录会话，可以在网页中重新登录或导入。' });
}

app.on('second-instance', (_event, argv) => {
  const value = name => { const index = argv.indexOf(name); return index < 0 ? '' : argv[index + 1] || ''; };
  if (value('--play')) {
    queuedPlay = { source: value('--play'), monitor: value('--monitor') };
    if (playback?.state.playerReady) playQueued();
  }
  if (!argv.includes('--background')) showWindow();
});
app.on('activate', showWindow);
if (!smoke && !app.requestSingleInstanceLock()) app.quit();
else app.whenReady().then(async () => {
  diagnostics = new Diagnostics({ file: path.join(app.getPath('userData'), 'errors.json'),
    version: app.getVersion(), appPath: __dirname, pluginPath, configPath });
  await diagnostics.load();
  try { settings = { ...settings, ...JSON.parse(await fs.readFile(path.join(app.getPath('userData'), 'settings.json'), 'utf8')) }; } catch { /* first launch */ }
  delete settings.autoPlay;
  // Nix-managed extractor paths follow the bundled dependency after upgrades.
  if (settings.extractor.startsWith('/nix/store/')) settings.extractor = '';
  youtubeSession = session.fromPartition(smoke ? 'muralop-smoke' : 'persist:muralop-youtube');
  // Google refuses to sign in browsers that announce Electron; present plain Chromium.
  youtubeSession.setUserAgent(youtubeSession.getUserAgent().split(' ')
    .filter(token => !/^(Electron|Muralop|muralop-desktop)\//i.test(token)).join(' '));
  youtubeSession.setPermissionRequestHandler((_contents, _permission, callback) => callback(false));
  youtubeSession.setPermissionCheckHandler(() => false);
  youtubeSession.on('will-download', event => event.preventDefault());
  const prepareResolver = async (quality, signal, syncPlayer = true) => {
    const cookieFile = path.join(app.getPath('userData'), 'youtube-cookies.txt');
    await exportCookies(cookieFile, await youtubeSession.cookies.get({}), sites.flatMap(site => site.domains));
    // Restored videos use the same extractor, login session and quality.
    // Nix store paths change on upgrade; bundled yt-dlp is also on PATH.
    const bundled = path.isAbsolute(defaultExtractor) && !defaultExtractor.startsWith('/nix/store/') ? defaultExtractor : '';
    if (syncPlayer) await player.request('online', [settings.extractor || bundled, cookieFile, quality], signal);
    return { cookies: cookieFile, quality, signal };
  };
  playback = new Playback(player, async (source, quality, signal) =>
    resolveMedia(processes, settings.extractor || defaultExtractor, source, await prepareResolver(quality, signal)),
  async (source, signal, onUpdate) => resolvePlaylist(processes, settings.extractor || defaultExtractor, source,
    { ...await prepareResolver('', signal, false), onUpdate }));
  window = new BrowserWindow({ width: 1400, height: 940, minWidth: 1080, minHeight: 700,
    backgroundColor: '#101820', show: false, autoHideMenuBar: true, frame: false,
    webPreferences: { preload: path.join(__dirname, 'preload.cjs'), contextIsolation: true, sandbox: true, nodeIntegration: false, backgroundThrottling: false } });
  window.setMenu(null);
  window.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  window.webContents.on('will-navigate', event => event.preventDefault());
  browser = new WebContentsView({ webPreferences: { partition: smoke ? 'muralop-smoke' : 'persist:muralop-youtube',
    preload: path.join(__dirname, 'browser-preload.cjs'), nodeIntegration: false, contextIsolation: true, sandbox: true, backgroundThrottling: false } });
  browser.setBackgroundColor('#111c25');
  browser.setVisible(false);
  window.contentView.addChildView(browser);
  wireBrowser(browser.webContents);
  browser.webContents.on('did-navigate-in-page', (_event, url, isMainFrame) => { if (isMainFrame) autoPlay(url); });
  for (const event of ['did-navigate', 'did-navigate-in-page']) browser.webContents.on(event, () => publish());
  browser.webContents.on('did-start-loading', () => beginBrowserLoad());
  browser.webContents.on('dom-ready', () => {
    if (quitting || browserError) return;
    if (!browser.webContents.getURL() || browser.webContents.getURL() === 'about:blank') return;
    browserHasContent = true;
    browser.setVisible(true);
    publish();
  });
  browser.webContents.on('did-stop-loading', () => {
    clearTimeout(browserLoadTimer);
    browserSlow = false;
    publish();
  });
  browser.webContents.on('render-process-gone', (_event, details) => {
    if (quitting) return;
    browserHasContent = false;
    browserFailed(`网页进程已退出（${details.reason}），可点击“重新加载”恢复。`);
  });
  browser.webContents.on('unresponsive', () => browserFailed('网页暂时没有响应，可继续使用左侧控制，或点击“重新加载”。'));
  browser.webContents.on('responsive', () => { browserError = ''; publish(); });
  browser.webContents.on('did-fail-load', (_event, code, description, _url, mainFrame) => {
    if (mainFrame && code !== -3) browserFailed('网页加载失败：' + description);
  });
  const localPage = pathToFileURL(path.join(__dirname, 'index.html')).href;
  const trusted = event => event.sender === window.webContents && event.senderFrame?.url === localPage;
  ipcMain.on('browser-layout', (event, bounds) => {
    if (!trusted(event) || !bounds || !['x','y','width','height'].every(k => Number.isFinite(bounds[k]))) return;
    if (window.isMinimized() || bounds.width <= 0 || bounds.height <= 0) return;
    const [width, height] = window.getContentSize();
    const x = Math.max(0, Math.min(width - 1, Math.round(bounds.x)));
    const y = Math.max(0, Math.min(height - 1, Math.round(bounds.y)));
    const next = { x, y, width: Math.max(1, Math.min(width - x, Math.round(bounds.width))), height: Math.max(1, Math.min(height - y, Math.round(bounds.height))) };
    if (browserBounds && Object.keys(next).every(key => browserBounds[key] === next[key])) return;
    browserBounds = next;
    browser.setBounds(next);
  });
  ipcMain.on('video-sites-ready', event => {
    if (event.sender === browser.webContents && event.senderFrame === browser.webContents.mainFrame)
      event.sender.send('video-sites', sites);
  });
  ipcMain.on('video-play', (event, value) => {
    if (event.sender !== browser.webContents || event.senderFrame !== browser.webContents.mainFrame) return;
    if (videoSite(browser.webContents.getURL())) autoPlay(value);
  });
  ipcMain.handle('muralop', async (event, action, value) => {
    if (!trusted(event)) throw new Error('Untrusted IPC sender');
    try {
      switch (action) {
        case 'state': publishedPlaylist = undefined; publish(); return;
        case 'play': void play(String(value || '')).catch(showError); return;
        case 'play-entry': void playback.playEntry(value, selectedMonitor, settings.quality).catch(showError); return;
        case 'cancel': void playback.cancel(); return;
        case 'connect': void playback.connect(); return;
        case 'copy-diagnostics': clipboard.writeText(JSON.stringify(diagnostics.snapshot(playback.state), null, 2)); return '已复制诊断信息';
        case 'clear-errors': await diagnostics.clear(); publish(); return '已清空错误记录';
        case 'monitor': selectedMonitor = String(value || ''); return;
        case 'pause': await player.request(playback.state.paused ? 'resume' : 'pause'); break;
        case 'mute': await player.request(playback.state.mute ? 'unmute' : 'mute'); break;
        case 'volume': await player.request('volume', [String(Math.max(0, Math.min(100, Math.round(Number(value) || 0))))]); break;
        case 'clear': if (selectedMonitor) await player.request('clear', [selectedMonitor]); break;
        case 'file': {
          const chosen = await dialog.showOpenDialog(window, { properties: ['openFile'], filters: [{ name: '视频', extensions: ['mp4','mkv','webm','avi','mov','wmv','m4v'] }] });
          if (!chosen.canceled) void play(chosen.filePaths[0]).catch(showError);
          return;
        }
        case 'settings': {
          if (!value || typeof value !== 'object') return;
          if (typeof value.extractor === 'string' && value.extractor !== settings.extractor) playback.clearPlaylistCache();
          for (const key of ['quality','extractor','browser','profile']) if (typeof value[key] === 'string') settings[key] = value[key];
          await saveSettings(); publish(); return;
        }
        case 'site-menu': {
          if (!value || !Number.isFinite(value.x) || !Number.isFinite(value.y)) return;
          Menu.buildFromTemplate(sites.map(site => ({ label: site.name, type: 'radio', checked: site.id === activeSite().id,
            click: () => { void switchSite(site.id).catch(showError); } })))
            .popup({ window, x: Math.round(value.x), y: Math.round(value.y) });
          return;
        }
        case 'window-minimize': window.minimize(); return;
        case 'window-maximize': window.isMaximized() ? window.unmaximize() : window.maximize(); return;
        case 'window-hide': window.hide(); return;
        case 'retry-browser': beginBrowserLoad(true); browser.webContents.reload(); return;
        case 'import-file': await importSession(true); return;
        case 'import-browser': await importSession(false); return;
        case 'clear-session': await clearSession(); return;
        default: throw new Error('未知操作');
      }
      await playback.poll();
    } catch (error) { showError(error); }
  });
  playback.on('state', state => { publish(state); if (state.playerReady && queuedPlay) playQueued(); });
  player.on('failure', error => playback.playerFailed(safeError(error.message)));
  player.on('quit', () => { void quitApplication(); });
  player.on('mpris', action => {
    if (action.action === 'raise' || action.action === 'quit') { void handleMediaAction(action).catch(showError); return; }
    mediaActions = mediaActions.catch(() => {}).then(() => handleMediaAction(action)).catch(showError);
  });
  player.on('mpris-error', error => { diagnostics.record('MPRIS', error.message); publish(); });
  if (!smoke) createTray();
  for (const event of ['show', 'hide', 'minimize', 'restore', 'maximize', 'unmaximize']) window.on(event, () => publish());
  // A single application-owned tray restores this exact window and web contents.
  window.on('close', event => {
    if (quitting || smoke) return;
    event.preventDefault();
    window.hide();
  });
  window.on('closed', () => {
    clearTimeout(pollTimer);
    if (!quitting) void quitApplication();
  });
  await window.loadFile(path.join(__dirname, 'index.html'));
  if (!smoke && !quitting) {
    const target = tray;
    trayIconController = new AbortController();
    void themeTrayIcon(processes, window.webContents, trayIconController.signal).then(icon => {
      if (icon && !quitting && tray === target && !target.isDestroyed()) target.setImage(icon);
    }).catch(() => {});
  }
  if (!args.includes('--background')) window.show();
  publish();
  if (smoke) {
    playback.update({ phase: 'ready', message: '界面预览 · 未启动播放插件', monitors: [{ id: 'Demo', width: 1920, height: 1080 }] });
    await browser.webContents.loadURL('data:text/html;charset=utf-8,' + encodeURIComponent('<body style="background:#111820;color:#aac6bb;font:20px sans-serif;padding:50px"><h1>YouTube</h1><p>在这里浏览并选择视频</p></body>'));
    setTimeout(async () => {
      try { await fs.writeFile(path.resolve(smoke), (await window.webContents.capturePage()).toPNG()); app.exit(0); }
      catch (error) { console.error(safeError(error.message)); app.exit(1); }
    }, 1500);
  } else {
    navigate(activeSite().home);
    void playback.connect();
    // Status uses the persistent plugin host; no subprocess per poll.
    const poll = async () => {
      await playback.poll();
      if (!playback.closed) pollTimer = setTimeout(poll, playback.job ? 700 : 3000);
    };
    pollTimer = setTimeout(poll, 700);
  }
}).catch(error => { console.error(safeError(error.message)); app.exit(1); });

app.on('before-quit', event => {
  if (quitting) return;
  event.preventDefault();
  void quitApplication();
});
app.on('window-all-closed', () => app.quit());
process.on('SIGINT', () => app.quit());
process.on('SIGTERM', () => app.quit());
