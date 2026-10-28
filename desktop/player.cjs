const { utilityProcess } = require('electron');
const { fork } = require('node:child_process');
const { EventEmitter } = require('node:events');
const path = require('node:path');
const { abortError, safeError } = require('./processes.cjs');

// One persistent Node-API plugin host, owned by this application. Driver crashes
// cannot take the window down, and status polling never spawns RPC clients.
class PlayerPlugin extends EventEmitter {
  constructor({ pluginPath, configPath, legacyPath, version, nodePath }) {
    super();
    Object.assign(this, { pluginPath, configPath, legacyPath, version, nodePath });
    this.pending = new Map();
    this.sequence = 0;
    this.closed = false;
  }

  fail(child, error) {
    if (this.child !== child) return;
    this.child = null;
    for (const request of [...this.pending.values()]) request.finish(error);
    try { child.kill(); } catch { /* already gone */ }
    if (!this.closed) this.emit('failure', error);
  }

  call(method, value, signal, timeout = 4000) {
    if (signal?.aborted) return Promise.reject(abortError());
    const child = this.child;
    if (!child) return Promise.reject(new Error('播放插件尚未启动，请点击“重启播放”。'));
    return new Promise((resolve, reject) => {
      const id = ++this.sequence;
      const finish = (error, result) => {
        if (!this.pending.delete(id)) return;
        clearTimeout(timer);
        signal?.removeEventListener('abort', cancel);
        error ? reject(error) : resolve(result);
      };
      const cancel = () => finish(abortError());
      const timer = setTimeout(() => this.fail(child, new Error('播放插件失去响应，可点击“重启播放”恢复。')), timeout);
      this.pending.set(id, { finish });
      signal?.addEventListener('abort', cancel, { once: true });
      if (signal?.aborted) { cancel(); return; }
      try {
        const message = { id, method, value };
        if (typeof child.postMessage === 'function') child.postMessage(message);
        else child.send(message, error => { if (error) this.fail(child, error); });
      }
      catch (error) { this.fail(child, error); }
    });
  }

  async connect(signal) {
    if (this.closed || signal?.aborted) throw abortError();
    if (this.starting) { await this.starting; return this.request('status', [], signal); }
    if (this.child) return this.request('status', [], signal);
    const host = path.join(__dirname, 'player-host.cjs');
    // Electron's utility processes still load Chromium's libffmpeg. On Linux
    // use the application's bundled Node runtime to isolate the entire native
    // dependency graph, including its allocator, rather than deep-binding it.
    const environment = { ...process.env };
    delete environment.ELECTRON_RUN_AS_NODE;
    const child = process.platform === 'linux'
      ? fork(host, [], { execPath: this.nodePath || 'node', execArgv: [], env: environment, stdio: ['ignore', 'pipe', 'pipe', 'ipc'] })
      : utilityProcess.fork(host, [], { serviceName: 'Muralop Playback', stdio: 'pipe' });
    this.child = child;
    this.navigationKey = null;
    let errors = '';
    child.stdout?.on('data', () => {});
    child.stderr?.on('data', chunk => { errors = (errors + chunk.toString()).slice(-4096); });
    child.on('error', error => this.fail(child, new Error('无法启动播放宿主：' + safeError(error.message))));
    child.on('message', message => {
      if (this.child !== child || !message) return;
      if (message.event === 'mpris') {
        if (['raise','quit','next','previous','open-uri','play','play-pause','pause','stop','changed'].includes(message.action))
          this.emit('mpris', { action: message.action, value: String(message.value || '') });
        return;
      }
      if (message.event === 'mpris-error') { this.emit('mpris-error', new Error(safeError(message.error))); return; }
      if (!Number.isInteger(message.id)) return;
      const pending = this.pending.get(message.id);
      if (!pending) return;
      if (message.error) this.fail(child, new Error(safeError(message.error)));
      else pending.finish(null, message.result);
    });
    child.on('exit', (code, signal) => this.fail(child,
      new Error(`播放插件异常退出（${signal || code}）${errors ? '：' + safeError(errors).trim() : ''}，可点击“重启播放”。`)));
    this.starting = this.call('start', { pluginPath: this.pluginPath, configPath: this.configPath,
      legacyPath: this.legacyPath }, signal, 15000).then(result => {
      if (result.version !== this.version) throw new Error('应用文件版本不一致，请使用同一构建中的完整应用包。');
      return result;
    }).catch(error => { this.fail(child, error); throw error; });
    try { return await this.starting; }
    finally { this.starting = null; }
  }

  async request(command, args = [], signal) {
    if (this.closed) throw abortError();
    const fields = [command, ...args];
    if (fields.some(value => typeof value !== 'string' || /[\r\n\0]/.test(value))) throw new Error('控制参数无效');
    const input = fields.join('\n');
    if (Buffer.byteLength(input) > 32768) throw new Error('控制参数过长');
    const result = await this.call('request', input, signal);
    if (result.stopped) {
      this.emit('quit');
      throw abortError('应用正在退出');
    }
    if (!result.ok) throw new Error(result.error || '播放请求失败');
    return result;
  }

  playResolved(media, monitor, signal) {
    const title = String(media.title || '').replace(/[\r\n\0]/g, ' ').slice(0, 1024);
    return this.request('play-resolved', [media.source, media.video, media.audio, media.userAgent, media.referrer, monitor, title], signal);
  }

  syncNavigation(state) {
    if (process.platform !== 'linux' || this.closed) return;
    if (!state.playerReady) { this.navigationKey = null; return; }
    const index = state.playlistIndex ?? -1;
    const count = state.playlist?.entries.length || 0;
    const fields = [state.source || '', index >= 0 && index + 1 < count ? '1' : '0', index > 0 ? '1' : '0', state.monitor || ''];
    const key = JSON.stringify(fields);
    if (this.navigationKey === key) return;
    this.navigationKey = key;
    void this.request('media-context', fields).catch(error => {
      if (error.name !== 'AbortError') this.emit('mpris-error', error);
    });
  }

  async close() {
    this.closed = true;
    const child = this.child;
    if (!child) return;
    try { await this.call('close', null, undefined, 3000); } catch { /* hung native driver is terminated below */ }
    if (this.child === child) this.fail(child, abortError());
  }
}

module.exports = { PlayerPlugin };
