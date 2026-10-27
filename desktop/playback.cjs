const { EventEmitter } = require('node:events');
const { httpUrl, youtubeVideo } = require('./media.cjs');
const { safeError } = require('./processes.cjs');

// The core retries stalled CDN connections itself; allow for a few of them.
const LOAD_TIMEOUT = 60000;

class Playback extends EventEmitter {
  constructor(player, resolve) {
    super();
    this.player = player;
    this.resolve = resolve;
    this.state = { phase: 'connecting', message: '正在连接播放后端…', title: '', backendOnline: false,
      monitors: [], volume: 50, mute: false, paused: false, canCancel: false };
    this.job = null;
    this.polling = false;
    this.closed = false;
  }

  update(value) {
    if (this.closed) return;
    Object.assign(this.state, value);
    this.emit('state', { ...this.state });
  }

  async connect() {
    this.update({ phase: 'connecting', message: '正在连接播放后端…' });
    try {
      const state = await this.player.connect();
      this.update({ ...state, backendOnline: true, phase: state.playback === 'playing' ? (state.paused ? 'paused' : 'playing') : 'ready',
        message: state.playback === 'playing' ? '播放中' : '已连接，可以选择视频' });
      return state;
    } catch (error) {
      this.update({ backendOnline: false, phase: 'error', message: safeError(error.message) });
      return null;
    }
  }

  async play(source, monitor = '', quality = '') {
    source = youtubeVideo(source) || String(source || '').trim();
    if (!source) throw new Error('先选择视频或输入网址');
    if (!this.state.backendOnline) throw new Error('请先连接播放后端');
    await this.cancel(false);
    const job = { controller: new AbortController(), monitor, sent: false, deadline: 0 };
    this.job = job;
    const current = () => this.job === job && !job.controller.signal.aborted && !this.closed;
    this.update({ phase: httpUrl(source) ? 'resolving' : 'loading', canCancel: true,
      message: httpUrl(source) ? '解析中 · 正在获取视频信息和播放地址…' : '加载中 · 正在打开本地视频…', title: '' });
    try {
      if (httpUrl(source)) {
        const media = await this.resolve(source, quality, job.controller.signal);
        if (!current()) return;
        this.update({ phase: 'loading', title: media.title, message: '加载中 · 正在缓冲音视频…' });
        job.sent = true;
        job.deadline = Date.now() + LOAD_TIMEOUT;
        await this.player.playResolved(media, monitor, job.controller.signal);
      } else {
        job.sent = true;
        job.deadline = Date.now() + LOAD_TIMEOUT;
        await this.player.request('set', [source, monitor], job.controller.signal);
      }
      // Only a subsequent player status can promote loading to playing.
    } catch (error) {
      if (!current() || error.name === 'AbortError') return;
      this.job = null;
      this.update({ phase: 'error', canCancel: false, message: safeError(error.message) });
    }
  }

  async cancel(show = true) {
    const job = this.job;
    this.job = null;
    job?.controller.abort();
    if (show) this.update({ phase: 'cancelled', canCancel: false, message: '已取消，可以重新选择视频' });
    if (job?.sent) {
      try { await this.player.request('stop', [job.monitor]); } catch { /* cancellation still completes if the plugin is offline */ }
    }
  }

  async poll() {
    if (this.polling || !this.state.backendOnline || this.closed) return;
    this.polling = true;
    try {
      const status = await this.player.request('status');
      this.update({ monitors: status.monitors, volume: status.volume, mute: status.mute, paused: status.paused });
      const job = this.job;
      if (this.state.phase === 'resolving') return;
      const playback = job?.monitor ? status.monitors.find(m => m.id === job.monitor)?.playback : status.playback;
      if (status.error && (job || ['playing', 'paused', 'loading'].includes(this.state.phase))) {
        this.job = null;
        this.update({ phase: 'error', canCancel: false, message: '播放失败：' + safeError(status.error) });
      } else if (playback === 'playing' && (job || ['playing', 'paused'].includes(this.state.phase))) {
        this.job = null;
        this.update({ phase: status.paused ? 'paused' : 'playing', canCancel: false,
          message: (status.paused ? '已暂停' : '播放中') + (this.state.title ? ' · ' + this.state.title : '') });
      } else if (job?.sent && Date.now() > job.deadline) {
        await this.cancel(false);
        this.update({ phase: 'error', canCancel: false, message: '视频加载超时（60 秒），可以重试或更换画质。' });
      }
    } catch (error) {
      this.job?.controller.abort();
      this.job = null;
      this.update({ backendOnline: false, phase: 'error', canCancel: false, message: '播放后端失去响应：' + safeError(error.message) });
    } finally { this.polling = false; }
  }

  close() { this.closed = true; this.job?.controller.abort(); this.job = null; }
}

module.exports = { Playback };
