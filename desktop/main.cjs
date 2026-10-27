const { app, BrowserWindow, WebContentsView, ipcMain, dialog, Tray, Menu, nativeImage, session } = require('electron');
const path = require('node:path');
const fs = require('node:fs/promises');
const { existsSync } = require('node:fs');
const { pathToFileURL } = require('node:url');
const { Processes, safeError } = require('./processes.cjs');
const { PlayerPlugin } = require('./player.cjs');
const { Playback } = require('./playback.cjs');
const { youtubeVideo, httpUrl, electronCookie, exportCookies, resolveMedia } = require('./media.cjs');

app.setName('Muralop');
const args = process.argv.slice(1);
const option = name => { const index = args.indexOf(name); return index < 0 ? '' : args[index + 1] || ''; };
const smoke = option('--smoke-test');
// Keep Chromium's profile beside, not inside, the backend's config folder.
app.setPath('userData', option('--data-dir') ? path.resolve(option('--data-dir'))
  : path.join(app.getPath('appData'), process.platform === 'linux' ? 'muralop' : 'Muralop', 'desktop'));
const binaryDir = path.dirname(process.execPath);
const suffix = process.platform === 'win32' ? '.exe' : '';
function locate(name, explicit) {
  if (explicit) return explicit;
  const sibling = path.join(binaryDir, name + suffix);
  return existsSync(sibling) ? sibling : name + suffix;
}
const backend = locate('muralop', option('--backend') || process.env.MURALOP_BACKEND);
const defaultExtractor = locate('yt-dlp', process.env.MURALOP_YTDLP);
process.env.PATH = binaryDir + path.delimiter + process.env.PATH;
const processes = new Processes();
const player = new PlayerPlugin(backend, processes);
let window, browser, tray, trayKey, playback, youtubeSession, pollTimer;
let settings = { autoPlay: true, quality: '', extractor: '', browser: 'chrome', profile: '' };
let selectedMonitor = '', lastAuto = '', lastAutoAt = 0, importing = false;
const labels = { connecting: '连接中', ready: '就绪', resolving: '解析中', loading: '加载中', playing: '播放中', paused: '已暂停', cancelled: '已取消', error: '失败' };

function icon(phase) {
  const n = 24, pixels = Buffer.alloc(n * n * 4);
  const rgb = phase === 'error' ? [238, 103, 103] : ['resolving', 'loading', 'connecting'].includes(phase) ? [97, 162, 244] : [74, 196, 158];
  for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
    const distance = Math.hypot(x - 11.5, y - 11.5);
    const ring = distance > 8 && distance < 11;
    const mark = phase === 'error' ? (Math.abs(x - 11.5) < 1.5 && (y > 5 && y < 13 || y > 15 && y < 18))
      : phase === 'paused' ? (y > 6 && y < 18 && (x > 7 && x < 10 || x > 13 && x < 16))
      : (x > 8 && x < 17 && Math.abs(y - 11.5) < (17 - x) * 0.65);
    if (ring || mark) { const at = (y * n + x) * 4; pixels[at] = rgb[2]; pixels[at + 1] = rgb[1]; pixels[at + 2] = rgb[0]; pixels[at + 3] = 255; }
  }
  return nativeImage.createFromBitmap(pixels, { width: n, height: n, scaleFactor: 1 });
}

function publish(state = playback.state) {
  const label = labels[state.phase] || state.phase;
  if (window && !window.isDestroyed()) {
    window.setTitle(`Muralop · ${label}${state.title ? ' · ' + state.title : ''}`);
    window.webContents.send('state', { ...state, settings, defaultExtractor, importing });
  }
  // Status polls arrive several times a second; only touch the tray when it changes.
  const key = [state.phase, state.title, state.paused, state.canCancel, state.backendOnline].join('\n');
  if (tray && key !== trayKey) {
    trayKey = key;
    tray.setImage(icon(state.phase));
    tray.setToolTip(`Muralop · ${label}${state.title ? '\n' + state.title : ''}`);
    tray.setContextMenu(Menu.buildFromTemplate([
      { label: `Muralop · ${label}`, enabled: false },
      { label: '打开主界面', click: () => { window.show(); window.focus(); } },
      { label: '取消当前任务', enabled: state.canCancel, click: () => { void playback.cancel(); } },
      { type: 'separator' },
      { label: state.paused ? '继续播放' : '暂停播放', enabled: state.backendOnline,
        click: () => { void player.request(state.paused ? 'resume' : 'pause').then(() => playback.poll()).catch(showError); } },
      { label: '关闭主界面（保留壁纸）', click: () => app.quit() },
      { label: '停止壁纸并退出', click: () => { void player.request('quit').catch(() => {}).finally(() => app.quit()); } },
    ]));
  }
}

function showError(error) { playback.update({ phase: 'error', canCancel: false, message: safeError(error.message) }); }
async function saveSettings() {
  await fs.mkdir(app.getPath('userData'), { recursive: true });
  await fs.writeFile(path.join(app.getPath('userData'), 'settings.json'), JSON.stringify(settings, null, 2), { mode: 0o600 });
}
function allowNavigation(value) { return httpUrl(value) || value === 'about:blank'; }
function navigate(value) {
  const url = String(value).trim();
  if (!httpUrl(url)) throw new Error('网页地址必须以 https:// 或 http:// 开头');
  void browser.webContents.loadURL(url).catch(error => { if (error.code !== 'ERR_ABORTED') showError(error); });
}
async function play(source, monitor = selectedMonitor) {
  await playback.play(source, monitor, settings.quality);
}
function autoPlay(value) {
  const url = youtubeVideo(value);
  if (!settings.autoPlay || !url || (url === lastAuto && Date.now() - lastAutoAt < 2000)) return;
  lastAuto = url; lastAutoAt = Date.now();
  void play(url).catch(showError);
}

function wireBrowser(contents) {
  contents.setAudioMuted(true);
  contents.on('will-navigate', (event, url) => { if (!allowNavigation(url)) event.preventDefault(); });
  contents.setWindowOpenHandler(({ url }) => ({ action: allowNavigation(url) ? 'allow' : 'deny',
    overrideBrowserWindowOptions: { parent: window, width: 650, height: 800,
      webPreferences: { partition: 'persist:muralop-youtube', nodeIntegration: false, contextIsolation: true, sandbox: true } } }));
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
    const records = JSON.parse(await processes.run(helper, params, { timeout: 20000, maxBytes: 2 * 1024 * 1024 }));
    let count = 0;
    const cookies = records.map(electronCookie).filter(Boolean);
    if (!cookies.length) throw new Error('没有读到可用的 YouTube 登录会话');
    // Replace the session instead of mixing it with an existing login: two
    // Google sessions in one jar make Google refuse to sign in.
    await youtubeSession.clearStorageData({ storages: ['cookies'] });
    let skipped = 0;
    for (const cookie of cookies) {
      // One cookie Chromium refuses must not abort the rest of the session.
      try { await youtubeSession.cookies.set(cookie); count++; } catch { skipped++; }
    }
    if (!count) throw new Error('浏览器不接受导入的登录会话');
    navigate('https://www.youtube.com/');
    playback.update({ message: `已导入 ${count} 条登录会话${skipped ? `（跳过 ${skipped} 条浏览器不接受的 Cookie）` : ''}，登录状态以网页显示为准。` });
  } finally { importing = false; publish(); }
}

async function clearSession() {
  const { response } = await dialog.showMessageBox(window, { type: 'question', buttons: ['清除', '取消'], defaultId: 1, cancelId: 1,
    message: '清除内嵌网页的登录会话？', detail: '会退出内嵌网页中的 YouTube 和 Google 登录，之后需要重新登录或导入。' });
  if (response !== 0) return;
  await youtubeSession.clearStorageData();
  await youtubeSession.clearCache();
  await fs.rm(path.join(app.getPath('userData'), 'youtube-cookies.txt'), { force: true });
  navigate('https://www.youtube.com/');
  playback.update({ message: '已清除登录会话，可以在网页中重新登录或导入。' });
}

app.on('second-instance', () => { if (window) { window.show(); window.focus(); } });
if (!smoke && !app.requestSingleInstanceLock()) app.quit();
else app.whenReady().then(async () => {
  try { settings = { ...settings, ...JSON.parse(await fs.readFile(path.join(app.getPath('userData'), 'settings.json'), 'utf8')) }; } catch { /* first launch */ }
  // Nix-managed extractor paths follow the bundled dependency after upgrades.
  if (settings.extractor.startsWith('/nix/store/')) settings.extractor = '';
  youtubeSession = session.fromPartition(smoke ? 'muralop-smoke' : 'persist:muralop-youtube');
  // Google refuses to sign in browsers that announce Electron; present plain Chromium.
  youtubeSession.setUserAgent(youtubeSession.getUserAgent().split(' ')
    .filter(token => !/^(Electron|Muralop|muralop-desktop)\//i.test(token)).join(' '));
  youtubeSession.setPermissionRequestHandler((_contents, _permission, callback) => callback(false));
  youtubeSession.setPermissionCheckHandler(() => false);
  youtubeSession.on('will-download', event => event.preventDefault());
  playback = new Playback(player, async (source, quality, signal) => {
    const cookieFile = path.join(app.getPath('userData'), 'youtube-cookies.txt');
    await exportCookies(cookieFile, await youtubeSession.cookies.get({}));
    // The backend falls back to its own yt-dlp hook when it restarts without
    // the UI; keep that path on the same login session and quality.
    // Nix store paths change on upgrade; the Nix backend has its own yt-dlp on PATH.
    const bundled = path.isAbsolute(defaultExtractor) && !defaultExtractor.startsWith('/nix/store/') ? defaultExtractor : '';
    await player.request('online', [settings.extractor || bundled, cookieFile, quality], signal);
    return resolveMedia(processes, settings.extractor || defaultExtractor, source, { cookies: cookieFile, quality, signal });
  });
  window = new BrowserWindow({ width: 1400, height: 940, minWidth: 1080, minHeight: 700,
    backgroundColor: '#101820', show: false, autoHideMenuBar: true,
    webPreferences: { preload: path.join(__dirname, 'preload.cjs'), contextIsolation: true, sandbox: true, nodeIntegration: false } });
  window.setMenu(null);
  window.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  window.webContents.on('will-navigate', event => event.preventDefault());
  browser = new WebContentsView({ webPreferences: { partition: smoke ? 'muralop-smoke' : 'persist:muralop-youtube',
    preload: path.join(__dirname, 'browser-preload.cjs'), nodeIntegration: false, contextIsolation: true, sandbox: true } });
  window.contentView.addChildView(browser);
  wireBrowser(browser.webContents);
  browser.webContents.on('did-navigate-in-page', (_event, url, isMainFrame) => { if (isMainFrame) autoPlay(url); });
  browser.webContents.on('did-finish-load', () => { browser.webContents.send('auto-play', settings.autoPlay); });
  const browserState = () => {
    if (!window.isDestroyed()) window.webContents.send('state', { ...playback.state, settings, defaultExtractor, importing,
      browserUrl: browser.webContents.getURL(), browserLoading: browser.webContents.isLoading() });
  };
  for (const event of ['did-navigate', 'did-navigate-in-page', 'did-start-loading', 'did-stop-loading']) browser.webContents.on(event, browserState);
  browser.webContents.on('render-process-gone', () => playback.update({ message: '网页进程已退出，可以点击刷新重新加载。' }));
  browser.webContents.on('did-fail-load', (_event, code, description, _url, mainFrame) => {
    if (mainFrame && code !== -3) playback.update({ message: '网页加载失败：' + description });
  });
  const localPage = pathToFileURL(path.join(__dirname, 'index.html')).href;
  const trusted = event => event.sender === window.webContents && event.senderFrame?.url === localPage;
  ipcMain.on('browser-layout', (event, bounds) => {
    if (!trusted(event) || !bounds || !['x','y','width','height'].every(k => Number.isFinite(bounds[k]))) return;
    const [width, height] = window.getContentSize();
    const x = Math.max(0, Math.min(width - 1, Math.round(bounds.x)));
    const y = Math.max(0, Math.min(height - 1, Math.round(bounds.y)));
    browser.setBounds({ x, y, width: Math.max(1, Math.min(width - x, Math.round(bounds.width))), height: Math.max(1, Math.min(height - y, Math.round(bounds.height))) });
  });
  ipcMain.on('youtube-play', (event, value) => {
    if (event.sender !== browser.webContents || event.senderFrame !== browser.webContents.mainFrame) return;
    try { if (['www.youtube.com','m.youtube.com'].includes(new URL(browser.webContents.getURL()).hostname)) autoPlay(value); } catch { /* no loaded page */ }
  });
  ipcMain.handle('muralop', async (event, action, value) => {
    if (!trusted(event)) throw new Error('Untrusted IPC sender');
    try {
      switch (action) {
        case 'state': publish(); return;
        case 'play': void play(String(value || '')).catch(showError); return;
        case 'play-current': void play(browser.webContents.getURL()).catch(showError); return;
        case 'cancel': void playback.cancel(); return;
        case 'connect': void playback.connect(); return;
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
          for (const key of ['quality','extractor','browser','profile']) if (typeof value[key] === 'string') settings[key] = value[key];
          if (typeof value.autoPlay === 'boolean') settings.autoPlay = value.autoPlay;
          browser.webContents.send('auto-play', settings.autoPlay);
          await saveSettings(); publish(); return;
        }
        case 'navigate': navigate(String(value || '')); return;
        case 'back': if (browser.webContents.navigationHistory.canGoBack()) browser.webContents.navigationHistory.goBack(); return;
        case 'forward': if (browser.webContents.navigationHistory.canGoForward()) browser.webContents.navigationHistory.goForward(); return;
        case 'reload': browser.webContents.reload(); return;
        case 'import-file': await importSession(true); return;
        case 'import-browser': await importSession(false); return;
        case 'clear-session': await clearSession(); return;
        default: throw new Error('未知操作');
      }
      await playback.poll();
    } catch (error) { showError(error); }
  });
  playback.on('state', publish);
  if (!smoke) {
    tray = new Tray(icon('connecting'));
    tray.on('double-click', () => { window.show(); window.focus(); });
    tray.on('click', () => { window.show(); window.focus(); });
  }
  window.on('closed', () => {
    clearTimeout(pollTimer);
    playback.close();
    processes.close();
    if (!browser.webContents.isDestroyed()) browser.webContents.close();
    app.quit();
  });
  await window.loadFile(path.join(__dirname, 'index.html'));
  window.show();
  publish();
  if (smoke) {
    playback.update({ phase: 'ready', message: '界面预览 · 未连接播放后端', monitors: [{ id: 'Demo', width: 1920, height: 1080 }] });
    await browser.webContents.loadURL('data:text/html;charset=utf-8,' + encodeURIComponent('<body style="background:#111820;color:#aac6bb;font:20px sans-serif;padding:50px"><h1>YouTube</h1><p>在这里浏览并选择视频</p></body>'));
    setTimeout(async () => {
      try { await fs.writeFile(path.resolve(smoke), (await window.webContents.capturePage()).toPNG()); app.exit(0); }
      catch (error) { console.error(safeError(error.message)); app.exit(1); }
    }, 1500);
  } else {
    navigate('https://www.youtube.com/');
    void playback.connect();
    // Each poll starts a short-lived RPC process: poll quickly only while a task is pending.
    const poll = async () => {
      await playback.poll();
      if (!playback.closed) pollTimer = setTimeout(poll, playback.job ? 700 : 3000);
    };
    pollTimer = setTimeout(poll, 700);
  }
}).catch(error => { console.error(safeError(error.message)); app.exit(1); });

app.on('before-quit', () => { clearTimeout(pollTimer); playback?.close(); processes.close(); tray?.destroy(); });
app.on('window-all-closed', () => app.quit());
process.on('SIGINT', () => app.quit());
process.on('SIGTERM', () => app.quit());
