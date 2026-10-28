const { app, BrowserWindow, WebContentsView, ipcMain, dialog, session } = require('electron');
const path = require('node:path');
const fs = require('node:fs/promises');
const { existsSync } = require('node:fs');
const { pathToFileURL } = require('node:url');
const { Processes, safeError } = require('./processes.cjs');
const { PlayerPlugin } = require('./player.cjs');
const { Playback } = require('./playback.cjs');
const { sites, videoSite, isSiteVideo, videoSource } = require('./sites.cjs');
const { youtubeSource, httpUrl, electronCookie, exportCookies, resolveMedia, resolvePlaylist } = require('./media.cjs');

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
let window, browser, playback, youtubeSession, pollTimer, quitting = false;
let browserError = '', browserBounds;
let publishedPlaylist;
let settings = { quality: '', extractor: '', browser: 'chrome', profile: '', site: 'youtube' };
const activeSite = () => sites.find(site => site.id === settings.site) || sites[0];
let selectedMonitor = '', lastAuto = '', lastAutoAt = 0, importing = false;
const labels = { connecting: '连接中', ready: '就绪', resolving: '解析中', loading: '加载中', playing: '播放中', paused: '已暂停', cancelled: '已取消', error: '失败' };

function publish(state = playback.state) {
  const label = labels[state.phase] || state.phase;
  if (window && !window.isDestroyed()) {
    window.setTitle(`Muralop · ${label}${state.title ? ' · ' + state.title : ''}`);
    const payload = { ...state, settings, sites: sites.map(({ id, name }) => ({ id, name })), defaultExtractor, importing, browserError,
      browserLoading: browser?.webContents.isLoading() };
    // A large collection is sent once, not with every status or navigation event.
    if (publishedPlaylist === state.playlist) delete payload.playlist;
    else publishedPlaylist = state.playlist;
    window.webContents.send('state', payload);
  }
}

function showWindow() {
  if (!window || window.isDestroyed()) return;
  if (window.isMinimized()) window.restore();
  window.show();
  window.focus();
  publish();
}

function showError(error) {
  playback.update({ ...(playback.job ? {} : { phase: 'error', canCancel: false }), message: safeError(error.message) });
}
async function saveSettings() {
  await fs.mkdir(app.getPath('userData'), { recursive: true });
  await fs.writeFile(path.join(app.getPath('userData'), 'settings.json'), JSON.stringify(settings, null, 2), { mode: 0o600 });
}
function allowNavigation(value) { return httpUrl(value) || value === 'about:blank'; }
function navigate(value) {
  const url = String(value).trim();
  if (!httpUrl(url)) throw new Error('网页地址必须以 https:// 或 http:// 开头');
  void browser.webContents.loadURL(url).catch(error => {
    if (error.code !== 'ERR_ABORTED') { browserError = '网页加载失败：' + safeError(error.message); publish(); }
  });
}
async function play(source, monitor = selectedMonitor) {
  await playback.play(source, monitor, settings.quality);
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
    const site = activeSite();
    const domains = [...site.domains, ...(site.loginDomains || [])];
    for (const domain of domains) params.push('--domain', domain);
    const records = JSON.parse(await processes.run(helper, params, { timeout: 20000, maxBytes: 2 * 1024 * 1024 }));
    let count = 0;
    const cookies = records.map(record => electronCookie(record, domains)).filter(Boolean);
    if (!cookies.length) throw new Error(`没有读到可用的 ${site.name} 登录会话`);
    // Replace the session instead of mixing it with an existing login: two
    // Google sessions in one jar make Google refuse to sign in.
    for (const cookie of await youtubeSession.cookies.get({})) {
      if (electronCookie({ ...cookie, expires: cookie.expirationDate }, domains))
        await youtubeSession.cookies.remove(`https://${cookie.domain.replace(/^\./, '')}${cookie.path || '/'}`, cookie.name);
    }
    let skipped = 0;
    for (const cookie of cookies) {
      // One cookie Chromium refuses must not abort the rest of the session.
      try { await youtubeSession.cookies.set(cookie); count++; } catch { skipped++; }
    }
    if (!count) throw new Error('浏览器不接受导入的登录会话');
    navigate(site.home);
    playback.update({ message: `已导入 ${count} 条登录会话${skipped ? `（跳过 ${skipped} 条浏览器不接受的 Cookie）` : ''}，登录状态以网页显示为准。` });
  } finally { importing = false; publish(); }
}

async function clearSession() {
  const { response } = await dialog.showMessageBox(window, { type: 'question', buttons: ['清除', '取消'], defaultId: 1, cancelId: 1,
    message: '清除内嵌网页的登录会话？', detail: '会退出所有内嵌站点的登录，之后需要重新登录或导入。' });
  if (response !== 0) return;
  await youtubeSession.clearStorageData();
  await youtubeSession.clearCache();
  await fs.rm(path.join(app.getPath('userData'), 'youtube-cookies.txt'), { force: true });
  navigate(activeSite().home);
  playback.update({ message: '已清除登录会话，可以在网页中重新登录或导入。' });
}

app.on('second-instance', showWindow);
app.on('activate', showWindow);
if (!smoke && !app.requestSingleInstanceLock()) app.quit();
else app.whenReady().then(async () => {
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
  const prepareResolver = async (quality, signal) => {
    const cookieFile = path.join(app.getPath('userData'), 'youtube-cookies.txt');
    await exportCookies(cookieFile, await youtubeSession.cookies.get({}), sites.flatMap(site => site.domains));
    // The backend falls back to its own yt-dlp hook when it restarts without
    // the UI; keep that path on the same login session and quality.
    // Nix store paths change on upgrade; the Nix backend has its own yt-dlp on PATH.
    const bundled = path.isAbsolute(defaultExtractor) && !defaultExtractor.startsWith('/nix/store/') ? defaultExtractor : '';
    await player.request('online', [settings.extractor || bundled, cookieFile, quality], signal);
    return { cookies: cookieFile, quality, signal };
  };
  playback = new Playback(player, async (source, quality, signal) =>
    resolveMedia(processes, settings.extractor || defaultExtractor, source, await prepareResolver(quality, signal)),
  async (source, signal) => resolvePlaylist(processes, settings.extractor || defaultExtractor, source, await prepareResolver(settings.quality, signal)));
  window = new BrowserWindow({ width: 1400, height: 940, minWidth: 1080, minHeight: 700,
    backgroundColor: '#101820', show: false, autoHideMenuBar: true,
    webPreferences: { preload: path.join(__dirname, 'preload.cjs'), contextIsolation: true, sandbox: true, nodeIntegration: false, backgroundThrottling: false } });
  window.setMenu(null);
  window.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  window.webContents.on('will-navigate', event => event.preventDefault());
  browser = new WebContentsView({ webPreferences: { partition: smoke ? 'muralop-smoke' : 'persist:muralop-youtube',
    preload: path.join(__dirname, 'browser-preload.cjs'), nodeIntegration: false, contextIsolation: true, sandbox: true, backgroundThrottling: false } });
  window.contentView.addChildView(browser);
  wireBrowser(browser.webContents);
  browser.webContents.on('did-navigate-in-page', (_event, url, isMainFrame) => { if (isMainFrame) autoPlay(url); });
  for (const event of ['did-navigate', 'did-navigate-in-page', 'did-start-loading', 'did-stop-loading']) browser.webContents.on(event, () => publish());
  browser.webContents.on('did-start-loading', () => { browserError = ''; browser.setVisible(true); publish(); });
  browser.webContents.on('render-process-gone', (_event, details) => {
    if (quitting) return;
    browserError = `网页进程已退出（${details.reason}），可点击“重新加载网页”恢复。`;
    browser.setVisible(false);
    publish();
  });
  browser.webContents.on('unresponsive', () => { browserError = '网页暂时没有响应，可以继续使用左侧控制，或点击“重新加载网页”。'; publish(); });
  browser.webContents.on('responsive', () => { browserError = ''; publish(); });
  browser.webContents.on('did-fail-load', (_event, code, description, _url, mainFrame) => {
    if (mainFrame && code !== -3) { browserError = '网页加载失败：' + description; publish(); }
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
        case 'quit-ui': app.quit(); return;
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
          await saveSettings(); publish(); return;
        }
        case 'site': {
          const site = sites.find(site => site.id === value);
          if (!site) throw new Error('未知的视频站点');
          settings.site = site.id;
          await saveSettings();
          navigate(site.home); publish(); return;
        }
        case 'retry-browser': browser.webContents.reload(); return;
        case 'import-file': await importSession(true); return;
        case 'import-browser': await importSession(false); return;
        case 'clear-session': await clearSession(); return;
        default: throw new Error('未知操作');
      }
      await playback.poll();
    } catch (error) { showError(error); }
  });
  playback.on('state', publish);
  // The native backend owns the application's single tray icon. Its "open UI"
  // action launches a second instance, which restores this existing window.
  window.on('close', event => {
    if (quitting || smoke) return;
    event.preventDefault();
    window.hide();
  });
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
    navigate(activeSite().home);
    void playback.connect();
    // Each poll starts a short-lived RPC process: poll quickly only while a task is pending.
    const poll = async () => {
      await playback.poll();
      if (!playback.closed) pollTimer = setTimeout(poll, playback.job ? 700 : 3000);
    };
    pollTimer = setTimeout(poll, 700);
  }
}).catch(error => { console.error(safeError(error.message)); app.exit(1); });

app.on('before-quit', () => { quitting = true; clearTimeout(pollTimer); playback?.close(); processes.close(); });
app.on('window-all-closed', () => app.quit());
process.on('SIGINT', () => app.quit());
process.on('SIGTERM', () => app.quit());
