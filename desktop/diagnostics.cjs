const fs = require('node:fs/promises');
const path = require('node:path');
const { createHash } = require('node:crypto');
const { safeError } = require('./processes.cjs');

class Diagnostics {
  constructor({ file, version, appPath, pluginPath, configPath }) {
    this.file = file;
    this.application = { version, location: appPath, executable: process.execPath,
      plugin: pluginPath, config: configPath, startedAt: new Date().toISOString(), fingerprint: '' };
    this.errors = [];
    this.writes = Promise.resolve();
  }

  async load() {
    try {
      const stored = JSON.parse(await fs.readFile(this.file, 'utf8'));
      if (Array.isArray(stored)) this.errors = stored.filter(entry => entry && typeof entry.message === 'string' && Number.isFinite(Date.parse(entry.at)))
        .slice(-20).map(entry => ({ at: entry.at, category: safeError(entry.category || '应用'), message: safeError(entry.message), count: Number(entry.count) || 1 }));
    } catch { /* first launch or an incomplete old diagnostic file */ }
    try {
      const hash = createHash('sha256');
      for (const file of ['package.json', 'main.cjs', 'renderer.cjs', 'index.html', 'style.css', 'player.cjs', 'player-host.cjs', 'tray-icon.cjs', 'diagnostics.cjs', 'playback.cjs', 'media.cjs', 'sites.cjs', 'assets/muralop.png', 'assets/muralop@2x.png']) {
        hash.update(file);
        hash.update(await fs.readFile(path.join(this.application.location, file)));
      }
      try { hash.update(await fs.readFile(this.application.plugin)); } catch { /* report a missing plugin on startup */ }
      this.application.fingerprint = hash.digest('hex').slice(0, 12);
    } catch { /* paths and version remain useful if a source file is unavailable */ }
  }

  record(category, value) {
    const message = safeError(value);
    if (!message) return;
    const previous = this.errors.at(-1);
    if (previous?.category === category && previous.message === message) {
      previous.at = new Date().toISOString();
      previous.count++;
    } else {
      this.errors.push({ at: new Date().toISOString(), category, message, count: 1 });
      if (this.errors.length > 20) this.errors.shift();
    }
    clearTimeout(this.timer);
    this.timer = setTimeout(() => { void this.persist().catch(() => {}); }, 250);
  }

  persist() {
    clearTimeout(this.timer);
    this.timer = null;
    const data = JSON.stringify(this.errors, null, 2);
    this.writes = this.writes.catch(() => {}).then(async () => {
      await fs.mkdir(path.dirname(this.file), { recursive: true });
      const temporary = this.file + '.tmp';
      await fs.writeFile(temporary, data, { mode: 0o600 });
      await fs.rename(temporary, this.file);
    });
    return this.writes;
  }

  clear() { this.errors = []; return this.persist(); }
  flush() { return this.timer ? this.persist() : this.writes; }

  snapshot(state) {
    // Deliberately whitelist fields: no cookies, source URLs or signed streams.
    return { application: this.application, platform: `${process.platform} ${process.arch}`,
      electron: process.versions.electron, chromium: process.versions.chrome,
      playback: { ready: Boolean(state.playerReady), phase: state.phase,
        monitors: (state.monitors || []).map(({ id, width, height }) => ({ id, width, height })) },
      errors: this.errors.slice(), errorFile: this.file };
  }
}

module.exports = { Diagnostics };
